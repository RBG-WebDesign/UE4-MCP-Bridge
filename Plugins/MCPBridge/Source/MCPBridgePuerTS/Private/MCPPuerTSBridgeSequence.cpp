// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Sequencer: the read half (sequence_inspect) and the desired-state write half
// (sequence_build) of ULevelSequence authoring, for UE4.27.
//
// Why one file: the two commands share a canonical reader, and the reason
// MCPBridgeBlueprintMembers.h exists as a header is that graph_inspect and
// blueprint_member_patch live in different translation units. These two do not,
// so the reader is an anonymous namespace here rather than a fourth shared
// header nobody else includes.
//
// Both commands speak DISPLAY-RATE frames, which is what Sequencer shows and
// what a caller writing "the shot is 90 frames long" means. UE4.27 stores key
// times in TICKS, at a tick resolution that defaults to 60000 per second. Every
// conversion goes through FFrameRate::TransformTime, the engine's own integer
// path, so a spec and a read cannot disagree about what frame 90 is.
//
// UE4.27 ONLY, and the 4.27 Sequencer API is not the 4.26 one and not the UE5
// one. The three that would silently produce wrong code:
//   - There is no FMovieSceneDoubleChannel in 4.27. A transform section is nine
//     FMovieSceneFloatChannel plus a weight channel.
//   - FMovieSceneObjectBindingID(Guid, SequenceID, Space) is gone. The 4.27
//     form is UE::MovieScene::FRelativeObjectBindingID(Guid).
//   - Rotation channel order is Roll, Pitch, Yaw (indices 3, 4, 5), which is not
//     FRotator's member order. Getting it wrong swaps axes silently.
// Verified against D:/UE/UE_4.27/Engine/Source; the per-call citations are on
// the calls themselves.

#include "MCPPuerTSBridgeService.h"

#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeBlueprintMembers.h"

#include "AssetRegistryModule.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "LevelSequence.h"
#include "Misc/FrameRate.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneCommonHelpers.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneSpawnTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "UObject/Package.h"

namespace
{
    // ---------------------------------------------------------------------
    // Frames.
    // ---------------------------------------------------------------------

    /** Display-rate frame -> tick-resolution frame. FloorToFrame is what
        ULevelSequenceFactoryNew uses for playback-range boundaries
        (LevelSequenceFactoryNew.cpp:34), so a boundary derived here lands where
        the editor's own would. */
    FFrameNumber DisplayToTick(const UMovieScene* MovieScene, int32 DisplayFrame)
    {
        return FFrameRate::TransformTime(
            FFrameTime(FFrameNumber(DisplayFrame)),
            MovieScene->GetDisplayRate(),
            MovieScene->GetTickResolution()).FloorToFrame();
    }

    /** Tick-resolution frame -> display-rate frame, for reporting. */
    int32 TickToDisplay(const UMovieScene* MovieScene, FFrameNumber Tick)
    {
        return FFrameRate::TransformTime(
            FFrameTime(Tick),
            MovieScene->GetTickResolution(),
            MovieScene->GetDisplayRate()).RoundToFrame().Value;
    }

    /**
     * A frames-per-second number as an FFrameRate.
     *
     * ponytail: 1/1000 precision, so 23.976 becomes 23976/1000 rather than the
     * exact NTSC 24000/1001. Every integer rate (24, 25, 30, 48, 60) is exact,
     * which is what the schema's callers ask for. The upgrade, if a drop-frame
     * project ever needs it, is a {numerator, denominator} form on the schema,
     * not more cleverness here.
     */
    FFrameRate FrameRateFromDecimal(double Fps)
    {
        const int32 Rounded = FMath::RoundToInt(Fps);
        if (FMath::IsNearlyEqual(Fps, static_cast<double>(Rounded), 1e-6))
        {
            return FFrameRate(Rounded, 1);
        }
        return FFrameRate(FMath::RoundToInt(Fps * 1000.0), 1000);
    }

    bool RatesMatch(const FFrameRate& A, const FFrameRate& B)
    {
        return A.IsValid() && B.IsValid()
            && FMath::IsNearlyEqual(A.AsDecimal(), B.AsDecimal(), 1e-6);
    }

    // ---------------------------------------------------------------------
    // Channels.
    // ---------------------------------------------------------------------

    /**
     * Channel names, per section class, in the engine's own channel order.
     *
     * Derived from MovieScene3DTransformSection.cpp:506-541 (CacheChannelProxy),
     * where indices 0-2 are Translation X/Y/Z, 3-5 are Rotation Roll/Pitch/Yaw
     * and 6-8 are Scale X/Y/Z. Rotation is NOT in FRotator declaration order,
     * which is the single easiest thing to get wrong in this file.
     *
     * Deliberately not read from FMovieSceneChannelMetaData: that is editor-only
     * data, and a name that exists in one build configuration and not another
     * would move the structure hash for reasons that have nothing to do with the
     * asset.
     */
    const TCHAR* const GTransformChannelNames[] = {
        TEXT("Location.X"), TEXT("Location.Y"), TEXT("Location.Z"),
        TEXT("Rotation.Roll"), TEXT("Rotation.Pitch"), TEXT("Rotation.Yaw"),
        TEXT("Scale.X"), TEXT("Scale.Y"), TEXT("Scale.Z"),
        TEXT("Weight"),
    };

    FString FloatChannelLabel(const UMovieSceneSection* Section, int32 Index)
    {
        if (Section->IsA<UMovieScene3DTransformSection>()
            && Index >= 0 && Index < static_cast<int32>(UE_ARRAY_COUNT(GTransformChannelNames)))
        {
            return GTransformChannelNames[Index];
        }
        if (Section->IsA<UMovieSceneFloatSection>() && Index == 0)
        {
            return TEXT("Value");
        }
        return FString::Printf(TEXT("float[%d]"), Index);
    }

    FString BoolChannelLabel(const UMovieSceneSection* Section, int32 Index)
    {
        if (Section->IsA<UMovieSceneBoolSection>() && Index == 0) { return TEXT("Value"); }
        return FString::Printf(TEXT("bool[%d]"), Index);
    }

    /** The channel index a Transform key component writes to, or INDEX_NONE. */
    int32 TransformChannelIndex(const FString& Component)
    {
        static const TMap<FString, int32> Indices = {
            { TEXT("location.x"), 0 }, { TEXT("location.y"), 1 }, { TEXT("location.z"), 2 },
            { TEXT("rotation.roll"), 3 }, { TEXT("rotation.pitch"), 4 }, { TEXT("rotation.yaw"), 5 },
            { TEXT("scale.x"), 6 }, { TEXT("scale.y"), 7 }, { TEXT("scale.z"), 8 },
        };
        const int32* Found = Indices.Find(Component.ToLower());
        return Found != nullptr ? *Found : INDEX_NONE;
    }

    FString InterpolationName(ERichCurveInterpMode Mode)
    {
        switch (Mode)
        {
        case RCIM_Linear:   return TEXT("Linear");
        case RCIM_Constant: return TEXT("Constant");
        case RCIM_Cubic:    return TEXT("Cubic");
        default:            return TEXT("None");
        }
    }

    ERichCurveInterpMode InterpolationFromName(const FString& Name)
    {
        if (Name.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))   { return RCIM_Linear; }
        if (Name.Equals(TEXT("Constant"), ESearchCase::IgnoreCase)) { return RCIM_Constant; }
        return RCIM_Cubic;
    }

    /** One float key, written with the interpolation asked for. The three
        Add*Key overloads are FMovieSceneFloatChannel.h:289-293; they set the
        interp and tangent modes that a hand-assembled FMovieSceneFloatValue
        would leave at their defaults. */
    void AddFloatKey(FMovieSceneFloatChannel& Channel, FFrameNumber Tick, float Value, ERichCurveInterpMode Mode)
    {
        switch (Mode)
        {
        case RCIM_Linear:   Channel.AddLinearKey(Tick, Value); break;
        case RCIM_Constant: Channel.AddConstantKey(Tick, Value); break;
        default:            Channel.AddCubicKey(Tick, Value); break;
        }
    }

    // ---------------------------------------------------------------------
    // Track vocabulary.
    // ---------------------------------------------------------------------

    /** The spec's type name for a track that exists. Visibility is tested
        before Bool because UMovieSceneVisibilityTrack derives from
        UMovieSceneBoolTrack, and answering "Bool" for a visibility track would
        make the two indistinguishable in the hash. */
    FString TrackTypeName(const UMovieSceneTrack* Track)
    {
        if (Track->IsA<UMovieScene3DTransformTrack>()) { return TEXT("Transform"); }
        if (Track->IsA<UMovieSceneVisibilityTrack>())  { return TEXT("Visibility"); }
        if (Track->IsA<UMovieSceneBoolTrack>())        { return TEXT("Bool"); }
        if (Track->IsA<UMovieSceneFloatTrack>())       { return TEXT("Float"); }
        if (Track->IsA<UMovieSceneCameraCutTrack>())   { return TEXT("Camera"); }
        if (Track->IsA<UMovieSceneSpawnTrack>())       { return TEXT("Spawn"); }
        return Track->GetClass()->GetName();
    }

    UClass* TrackClassForType(const FString& Type)
    {
        if (Type == TEXT("Transform"))  { return UMovieScene3DTransformTrack::StaticClass(); }
        if (Type == TEXT("Float"))      { return UMovieSceneFloatTrack::StaticClass(); }
        if (Type == TEXT("Bool"))       { return UMovieSceneBoolTrack::StaticClass(); }
        if (Type == TEXT("Visibility")) { return UMovieSceneVisibilityTrack::StaticClass(); }
        if (Type == TEXT("Camera"))     { return UMovieSceneCameraCutTrack::StaticClass(); }
        return nullptr;
    }

    FString TrackPropertyPath(const UMovieSceneTrack* Track)
    {
        const UMovieScenePropertyTrack* Property = Cast<UMovieScenePropertyTrack>(Track);
        return Property != nullptr ? Property->GetPropertyPath().ToString() : FString();
    }

    // ---------------------------------------------------------------------
    // The canonical read.
    // ---------------------------------------------------------------------

    TSharedPtr<FJsonObject> DescribeRate(const FFrameRate& Rate)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetNumberField(TEXT("numerator"), Rate.Numerator);
        Out->SetNumberField(TEXT("denominator"), Rate.Denominator);
        Out->SetNumberField(TEXT("fps"), Rate.AsDecimal());
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeSection(const UMovieScene* MovieScene, UMovieSceneSection* Section)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        // HasStartFrame/HasEndFrame first: MovieSceneSection.h:258-270 says
        // GetInclusiveStartFrame on an infinite range returns garbage rather
        // than failing, and garbage in a hash is worse than an absent field.
        Out->SetBoolField(TEXT("has_start"), Section->HasStartFrame());
        Out->SetBoolField(TEXT("has_end"), Section->HasEndFrame());
        if (Section->HasStartFrame())
        {
            const FFrameNumber Start = Section->GetInclusiveStartFrame();
            Out->SetNumberField(TEXT("start_tick"), Start.Value);
            Out->SetNumberField(TEXT("start_frame"), TickToDisplay(MovieScene, Start));
        }
        if (Section->HasEndFrame())
        {
            const FFrameNumber End = Section->GetExclusiveEndFrame();
            Out->SetNumberField(TEXT("end_tick"), End.Value);
            Out->SetNumberField(TEXT("end_frame"), TickToDisplay(MovieScene, End));
        }
        Out->SetNumberField(TEXT("row_index"), Section->GetRowIndex());
        Out->SetStringField(TEXT("section_class"), Section->GetClass()->GetName());

        if (const UMovieSceneCameraCutSection* Cut = Cast<UMovieSceneCameraCutSection>(Section))
        {
            Out->SetStringField(TEXT("camera_binding_id"), Cut->GetCameraBindingID().GetGuid().ToString());
        }

        TArray<TSharedPtr<FJsonValue>> Channels;
        int32 KeyCount = 0;

        FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
        const TArrayView<FMovieSceneFloatChannel* const> FloatChannels =
            Proxy.GetChannels<FMovieSceneFloatChannel>();
        for (int32 Index = 0; Index < FloatChannels.Num(); ++Index)
        {
            const FMovieSceneFloatChannel* Channel = FloatChannels[Index];
            if (Channel == nullptr) { continue; }
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), FloatChannelLabel(Section, Index));
            Entry->SetStringField(TEXT("value_type"), TEXT("float"));
            const TArrayView<const FFrameNumber> Times = Channel->GetTimes();
            const TArrayView<const FMovieSceneFloatValue> Values = Channel->GetValues();
            Entry->SetNumberField(TEXT("key_count"), Times.Num());
            KeyCount += Times.Num();
            TArray<TSharedPtr<FJsonValue>> Keys;
            for (int32 K = 0; K < Times.Num() && K < Values.Num(); ++K)
            {
                TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
                Key->SetNumberField(TEXT("tick"), Times[K].Value);
                Key->SetNumberField(TEXT("frame"), TickToDisplay(MovieScene, Times[K]));
                Key->SetNumberField(TEXT("value"), Values[K].Value);
                Key->SetStringField(TEXT("interpolation"),
                    InterpolationName(Values[K].InterpMode.GetValue()));
                Keys.Add(MakeShared<FJsonValueObject>(Key));
            }
            Entry->SetArrayField(TEXT("keys"), Keys);
            Channels.Add(MakeShared<FJsonValueObject>(Entry));
        }

        const TArrayView<FMovieSceneBoolChannel* const> BoolChannels =
            Proxy.GetChannels<FMovieSceneBoolChannel>();
        for (int32 Index = 0; Index < BoolChannels.Num(); ++Index)
        {
            const FMovieSceneBoolChannel* Channel = BoolChannels[Index];
            if (Channel == nullptr) { continue; }
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), BoolChannelLabel(Section, Index));
            Entry->SetStringField(TEXT("value_type"), TEXT("bool"));
            const TArrayView<const FFrameNumber> Times = Channel->GetTimes();
            const TArrayView<const bool> Values = Channel->GetValues();
            Entry->SetNumberField(TEXT("key_count"), Times.Num());
            KeyCount += Times.Num();
            TArray<TSharedPtr<FJsonValue>> Keys;
            for (int32 K = 0; K < Times.Num() && K < Values.Num(); ++K)
            {
                TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
                Key->SetNumberField(TEXT("tick"), Times[K].Value);
                Key->SetNumberField(TEXT("frame"), TickToDisplay(MovieScene, Times[K]));
                Key->SetBoolField(TEXT("value"), Values[K]);
                Keys.Add(MakeShared<FJsonValueObject>(Key));
            }
            Entry->SetArrayField(TEXT("keys"), Keys);
            Channels.Add(MakeShared<FJsonValueObject>(Entry));
        }

        Out->SetArrayField(TEXT("channels"), Channels);
        Out->SetNumberField(TEXT("key_count"), KeyCount);
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeTrack(
        const UMovieScene* MovieScene,
        UMovieSceneTrack* Track,
        const FString& BindingId,
        const FString& BindingName)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("binding"), BindingId);
        Out->SetStringField(TEXT("binding_name"), BindingName);
        Out->SetStringField(TEXT("type"), TrackTypeName(Track));
        Out->SetStringField(TEXT("property"), TrackPropertyPath(Track));
        Out->SetStringField(TEXT("track_class"), Track->GetClass()->GetPathName());
        Out->SetStringField(TEXT("track_name"), Track->GetTrackName().ToString());

        TArray<UMovieSceneSection*> Sections = Track->GetAllSections();
        // Canonical order: by start tick, then row, then class. Unreal's own
        // array order is the order sections happen to have been added, so a
        // hash over that would move when an unrelated edit reordered them.
        Sections.Sort([](const UMovieSceneSection& A, const UMovieSceneSection& B)
        {
            const int32 StartA = A.HasStartFrame() ? A.GetInclusiveStartFrame().Value : MIN_int32;
            const int32 StartB = B.HasStartFrame() ? B.GetInclusiveStartFrame().Value : MIN_int32;
            if (StartA != StartB) { return StartA < StartB; }
            if (A.GetRowIndex() != B.GetRowIndex()) { return A.GetRowIndex() < B.GetRowIndex(); }
            return A.GetClass()->GetName() < B.GetClass()->GetName();
        });

        TArray<TSharedPtr<FJsonValue>> SectionJson;
        for (UMovieSceneSection* Section : Sections)
        {
            if (Section == nullptr) { continue; }
            SectionJson.Add(MakeShared<FJsonValueObject>(DescribeSection(MovieScene, Section)));
        }
        Out->SetArrayField(TEXT("sections"), SectionJson);
        return Out;
    }

    /** Sort key for one track entry. Bindings sort by NAME rather than by guid:
        a guid is stable within an asset but is regenerated when a binding is
        recreated, so ordering by it would make two structurally identical
        sequences hash differently. The guid is still in the payload. */
    FString TrackSortKey(const TSharedPtr<FJsonObject>& Track)
    {
        return Track->GetStringField(TEXT("binding_name"))
            + TEXT("|") + Track->GetStringField(TEXT("binding"))
            + TEXT("|") + Track->GetStringField(TEXT("type"))
            + TEXT("|") + Track->GetStringField(TEXT("property"))
            + TEXT("|") + Track->GetStringField(TEXT("track_class"));
    }

    /**
     * The whole sequence as canonical JSON.
     *
     * Everything under "resolution" is deliberately OUTSIDE the hashed set:
     * whether a possessable resolves to a live actor depends on which level the
     * editor has open, which is a property of the session and not of the asset.
     * A hash that moved when someone opened a different map would report a
     * change nobody made.
     */
    TSharedPtr<FJsonObject> BuildSnapshot(ULevelSequence* Sequence, UWorld* Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        UMovieScene* MovieScene = Sequence->GetMovieScene();
        if (MovieScene == nullptr)
        {
            Out->SetBoolField(TEXT("movie_scene_missing"), true);
            return Out;
        }

        Out->SetObjectField(TEXT("frame_rate"), DescribeRate(MovieScene->GetDisplayRate()));
        Out->SetObjectField(TEXT("tick_resolution"), DescribeRate(MovieScene->GetTickResolution()));

        TSharedPtr<FJsonObject> Playback = MakeShared<FJsonObject>();
        const TRange<FFrameNumber> Range = MovieScene->GetPlaybackRange();
        // TRange's own bound accessors rather than MovieScene::DiscreteInclusiveLower:
        // the namespace MovieScene and the class UMovieScene collide readably in
        // this file, and a playback range built by SetPlaybackRange is always a
        // plain [inclusive, exclusive) pair of closed bounds.
        if (Range.GetLowerBound().IsClosed())
        {
            const FFrameNumber Start = Range.GetLowerBoundValue();
            Playback->SetNumberField(TEXT("start_tick"), Start.Value);
            Playback->SetNumberField(TEXT("start_frame"), TickToDisplay(MovieScene, Start));
        }
        if (Range.GetUpperBound().IsClosed())
        {
            const FFrameNumber End = Range.GetUpperBoundValue();
            Playback->SetNumberField(TEXT("end_tick"), End.Value);
            Playback->SetNumberField(TEXT("end_frame"), TickToDisplay(MovieScene, End));
        }
        Out->SetObjectField(TEXT("playback_range"), Playback);

        // --- Bindings, sorted by name then guid. ---
        TArray<TSharedPtr<FJsonValue>> Bindings;
        TArray<TSharedPtr<FJsonValue>> Resolution;
        TMap<FGuid, FString> BindingNames;

        for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
        {
            const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), Possessable.GetGuid().ToString());
            Entry->SetStringField(TEXT("kind"), TEXT("possessable"));
            Entry->SetStringField(TEXT("name"), Possessable.GetName());
            Entry->SetStringField(TEXT("class_path"),
                Possessable.GetPossessedObjectClass() != nullptr
                    ? Possessable.GetPossessedObjectClass()->GetPathName()
                    : FString());
            Bindings.Add(MakeShared<FJsonValueObject>(Entry));
            BindingNames.Add(Possessable.GetGuid(), Possessable.GetName());

            TSharedPtr<FJsonObject> Resolved = MakeShared<FJsonObject>();
            Resolved->SetStringField(TEXT("binding"), Possessable.GetGuid().ToString());
            TArray<TSharedPtr<FJsonValue>> Actors;
            if (Context != nullptr)
            {
                // LocateBoundObjects is a plain lookup: no player, no evaluation
                // template, no PIE (LevelSequence.cpp:360-375). It needs the
                // actor loaded, so it answers empty when the level that holds it
                // is not open, which is reported rather than hidden.
                for (UObject* Bound : Sequence->LocateBoundObjects(Possessable.GetGuid(), Context))
                {
                    if (Bound == nullptr) { continue; }
                    TSharedPtr<FJsonObject> Actor = MakeShared<FJsonObject>();
                    Actor->SetStringField(TEXT("object_path"), Bound->GetPathName());
                    if (const AActor* AsActor = Cast<AActor>(Bound))
                    {
                        Actor->SetStringField(TEXT("label"), AsActor->GetActorLabel());
                    }
                    Actors.Add(MakeShared<FJsonValueObject>(Actor));
                }
            }
            Resolved->SetBoolField(TEXT("resolved"), Actors.Num() > 0);
            Resolved->SetArrayField(TEXT("actors"), Actors);
            Resolution.Add(MakeShared<FJsonValueObject>(Resolved));
        }

        for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
        {
            const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Index);
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("id"), Spawnable.GetGuid().ToString());
            Entry->SetStringField(TEXT("kind"), TEXT("spawnable"));
            Entry->SetStringField(TEXT("name"), Spawnable.GetName());
            const UObject* Template = Spawnable.GetObjectTemplate();
            Entry->SetStringField(TEXT("class_path"),
                Template != nullptr ? Template->GetClass()->GetPathName() : FString());
            Bindings.Add(MakeShared<FJsonValueObject>(Entry));
            BindingNames.Add(Spawnable.GetGuid(), Spawnable.GetName());
        }

        Bindings.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
        {
            const TSharedPtr<FJsonObject>* ObjectA = nullptr;
            const TSharedPtr<FJsonObject>* ObjectB = nullptr;
            if (!A->TryGetObject(ObjectA) || !B->TryGetObject(ObjectB)) { return false; }
            const FString KeyA = (*ObjectA)->GetStringField(TEXT("name"))
                + TEXT("|") + (*ObjectA)->GetStringField(TEXT("kind"))
                + TEXT("|") + (*ObjectA)->GetStringField(TEXT("id"));
            const FString KeyB = (*ObjectB)->GetStringField(TEXT("name"))
                + TEXT("|") + (*ObjectB)->GetStringField(TEXT("kind"))
                + TEXT("|") + (*ObjectB)->GetStringField(TEXT("id"));
            return KeyA < KeyB;
        });
        Out->SetArrayField(TEXT("bindings"), Bindings);
        Out->SetArrayField(TEXT("resolution"), Resolution);

        // --- Tracks: master first by name, then per binding. ---
        TArray<TSharedPtr<FJsonObject>> Tracks;
        for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
        {
            if (Track == nullptr) { continue; }
            Tracks.Add(DescribeTrack(MovieScene, Track, TEXT("master"), TEXT("master")));
        }
        for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
        {
            const FString* Name = BindingNames.Find(Binding.GetObjectGuid());
            for (UMovieSceneTrack* Track : Binding.GetTracks())
            {
                if (Track == nullptr) { continue; }
                Tracks.Add(DescribeTrack(
                    MovieScene, Track,
                    Binding.GetObjectGuid().ToString(),
                    Name != nullptr ? *Name : Binding.GetName()));
            }
        }
        Tracks.Sort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
        {
            return TrackSortKey(A) < TrackSortKey(B);
        });

        TArray<TSharedPtr<FJsonValue>> TrackJson;
        int32 SectionCount = 0;
        int32 KeyCount = 0;
        for (const TSharedPtr<FJsonObject>& Track : Tracks)
        {
            const TArray<TSharedPtr<FJsonValue>>& Sections = Track->GetArrayField(TEXT("sections"));
            SectionCount += Sections.Num();
            for (const TSharedPtr<FJsonValue>& Section : Sections)
            {
                const TSharedPtr<FJsonObject>* SectionObject = nullptr;
                if (Section->TryGetObject(SectionObject))
                {
                    KeyCount += static_cast<int32>((*SectionObject)->GetNumberField(TEXT("key_count")));
                }
            }
            TrackJson.Add(MakeShared<FJsonValueObject>(Track));
        }
        Out->SetArrayField(TEXT("tracks"), TrackJson);
        Out->SetNumberField(TEXT("binding_count"), Bindings.Num());
        Out->SetNumberField(TEXT("track_count"), TrackJson.Num());
        Out->SetNumberField(TEXT("section_count"), SectionCount);
        Out->SetNumberField(TEXT("key_count"), KeyCount);
        return Out;
    }

    /**
     * SHA-1 over the snapshot with the session-dependent half removed, through
     * MCPBridgeBlueprintMembers::StructureHash - the same digest, the same
     * spelling and the same lowercase hex the Blueprint, graph, Behavior Tree
     * and scene inspectors report as structure_hash_sha1. There is one hashing
     * scheme in this plugin and this is not a second one.
     *
     * The hash covers keyframe VALUES as well as shape, unlike the Blueprint
     * inspectors, and that is deliberate: for a sequence, a key value IS the
     * content. A camera that stops moving is a different sequence, not the same
     * sequence with different text.
     */
    FString SnapshotHash(const TSharedPtr<FJsonObject>& Snapshot)
    {
        if (!Snapshot.IsValid()) { return FString(); }
        TSharedPtr<FJsonObject> ForHash = MakeShared<FJsonObject>(*Snapshot);
        ForHash->RemoveField(TEXT("resolution"));
        return MCPBridgeBlueprintMembers::StructureHash(ForHash);
    }

    /** Strip keys from a snapshot for reporting only. Never used before
        hashing: a hash of a filtered read is a hash of the request. */
    void StripKeys(const TSharedPtr<FJsonObject>& Snapshot)
    {
        for (const TSharedPtr<FJsonValue>& Track : Snapshot->GetArrayField(TEXT("tracks")))
        {
            const TSharedPtr<FJsonObject>* TrackObject = nullptr;
            if (!Track->TryGetObject(TrackObject)) { continue; }
            for (const TSharedPtr<FJsonValue>& Section : (*TrackObject)->GetArrayField(TEXT("sections")))
            {
                const TSharedPtr<FJsonObject>* SectionObject = nullptr;
                if (!Section->TryGetObject(SectionObject)) { continue; }
                for (const TSharedPtr<FJsonValue>& Channel : (*SectionObject)->GetArrayField(TEXT("channels")))
                {
                    const TSharedPtr<FJsonObject>* ChannelObject = nullptr;
                    if (!Channel->TryGetObject(ChannelObject)) { continue; }
                    (*ChannelObject)->RemoveField(TEXT("keys"));
                }
            }
        }
    }

    // ---------------------------------------------------------------------
    // Asset addressing, shared by both commands.
    // ---------------------------------------------------------------------

    /** Package path and object path from either form a caller might send. */
    bool SplitAssetPath(const FString& AssetPath, FString& OutPackagePath, FString& OutObjectPath, FString& OutError)
    {
        if (!AssetPath.Contains(TEXT(".")))
        {
            if (!FPackageName::IsValidLongPackageName(AssetPath))
            {
                OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *AssetPath);
                return false;
            }
            OutPackagePath = AssetPath;
            OutObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
            return true;
        }
        OutPackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
        OutObjectPath = AssetPath;
        return true;
    }

    ULevelSequence* FindLevelSequence(const FString& ObjectPath, FString& OutError)
    {
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FAssetData AssetData =
            AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
        if (!AssetData.IsValid())
        {
            return nullptr;
        }
        UObject* Asset = AssetData.GetAsset();
        ULevelSequence* Sequence = Cast<ULevelSequence>(Asset);
        if (Sequence == nullptr)
        {
            OutError = FString::Printf(
                TEXT("The asset at '%s' is a %s, not a LevelSequence."),
                *ObjectPath, *AssetData.AssetClass.ToString());
        }
        return Sequence;
    }

    UWorld* EditorWorld()
    {
        return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    TArray<TSharedPtr<FJsonValue>> StringsToJsonValues(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }
}

// -------------------------------------------------------------------------
// sequence_inspect
// -------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectLevelSequenceJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError) const
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("Inspection request must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Request->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (AssetPath.IsEmpty())
    {
        OutError = TEXT("asset_path is required.");
        return false;
    }
    // Reading is allowed anywhere under the two content roots, like every other
    // read in this bridge; authoring stays limited to /Game/MCPGenerated/.
    if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")))
    {
        OutError = TEXT("Level Sequence inspection is limited to /Game and /Engine.");
        return false;
    }
    bool bIncludeKeys = true;
    Request->TryGetBoolField(TEXT("include_keys"), bIncludeKeys);

    FString PackagePath;
    FString ObjectPath;
    if (!SplitAssetPath(AssetPath, PackagePath, ObjectPath, OutError))
    {
        return false;
    }
    ULevelSequence* Sequence = FindLevelSequence(ObjectPath, OutError);
    if (Sequence == nullptr)
    {
        if (OutError.IsEmpty())
        {
            OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        }
        return false;
    }

    const UPackage* Package = Sequence->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    UWorld* World = EditorWorld();
    if (World == nullptr)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("There is no editor world, so no possessable could be resolved to an actor. "
                 "The structure and the hash are unaffected: actor resolution is reported under "
                 "resolution and is deliberately outside the hashed set.")));
    }

    TSharedPtr<FJsonObject> Snapshot = BuildSnapshot(Sequence, World);
    const FString Hash = SnapshotHash(Snapshot);
    if (!bIncludeKeys)
    {
        StripKeys(Snapshot);
    }

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Level Sequence dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Sequence->GetPathName());
    Result->SetStringField(TEXT("identity_kind"), TEXT("observed"));
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetBoolField(TEXT("keys_included"), bIncludeKeys);
    Result->SetObjectField(TEXT("sequence"), Snapshot);
    // Repeated at the top level as well as inside the snapshot, because every
    // other inspector in this bridge answers with a top-level structure_hash_sha1
    // and a caller should not have to know which nesting this one chose.
    Result->SetNumberField(TEXT("binding_count"), Snapshot->GetNumberField(TEXT("binding_count")));
    Result->SetNumberField(TEXT("track_count"), Snapshot->GetNumberField(TEXT("track_count")));
    Result->SetNumberField(TEXT("section_count"), Snapshot->GetNumberField(TEXT("section_count")));
    Result->SetNumberField(TEXT("key_count"), Snapshot->GetNumberField(TEXT("key_count")));
    Result->SetStringField(TEXT("structure_hash_sha1"), Hash);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}

// -------------------------------------------------------------------------
// sequence_build
// -------------------------------------------------------------------------

namespace
{
    /** One binding the spec asked for, resolved against the editor before
        anything is written. */
    struct FResolvedBinding
    {
        FString Id;
        FString Name;
        bool bSpawnable = false;
        AActor* Actor = nullptr;   // possessable only
        UClass* SpawnClass = nullptr;   // spawnable only
        FGuid Guid;                // filled in during apply
    };

    /** One key the spec asked for, flattened to a single channel. A Transform
        key with a location and a rotation is six of these; that is the level at
        which convergence is decided, because a channel is what actually holds a
        key. */
    struct FResolvedKey
    {
        int32 ChannelIndex = 0;
        bool bBoolChannel = false;
        int32 DisplayFrame = 0;
        float FloatValue = 0.0f;
        bool bBoolValue = false;
        ERichCurveInterpMode Interp = RCIM_Cubic;
        FString ChannelName;
    };

    struct FResolvedSection
    {
        int32 StartFrame = 0;
        int32 EndFrame = 0;
        int32 RowIndex = 0;
        bool bHasRowIndex = false;
        TArray<FResolvedKey> Keys;
        /** Camera tracks only: display frame -> binding id to cut to. */
        TArray<TPair<int32, FString>> CameraCuts;
    };

    struct FResolvedTrack
    {
        int32 Index = 0;
        FString BindingId;   // a spec binding id, or "master"
        FString Type;
        FString Property;
        TArray<FResolvedSection> Sections;
    };

    /** Resolve an actor class path. The allowlist is the same shape scene_batch
        uses: engine actors, cinematic cameras, and this project's own content. */
    UClass* ResolveActorClass(const FString& Path, FString& OutError)
    {
        if (!Path.StartsWith(TEXT("/Game/"))
            && !Path.StartsWith(TEXT("/Script/Engine."))
            && !Path.StartsWith(TEXT("/Script/CinematicCamera.")))
        {
            OutError = FString::Printf(
                TEXT("actor_class '%s' is outside the allowed roots. Spawnable classes are limited "
                     "to /Game/, /Script/Engine. and /Script/CinematicCamera.."), *Path);
            return nullptr;
        }
        UClass* Class = LoadObject<UClass>(nullptr, *Path);
        if (Class == nullptr && !Path.EndsWith(TEXT("_C")))
        {
            // A Blueprint asset path names the asset; the class is the asset
            // plus _C. Trying it here means a caller can pass the path other
            // tools hand back rather than knowing that rule.
            Class = LoadObject<UClass>(nullptr, *(Path + TEXT("_C")));
        }
        if (Class == nullptr)
        {
            OutError = FString::Printf(TEXT("actor_class '%s' does not load as a class."), *Path);
            return nullptr;
        }
        if (!Class->IsChildOf(AActor::StaticClass()))
        {
            OutError = FString::Printf(
                TEXT("actor_class '%s' is a %s, which does not derive from Actor. A spawnable is an "
                     "actor."), *Path, *Class->GetName());
            return nullptr;
        }
        return Class;
    }

    /** Find an actor in the editor world by its label. Zero and more than one
        are both refusals that NAME what was found, never a guess: two actors
        called "SliceBeacon" is not a request with an answer. */
    AActor* FindActorByLabel(UWorld* World, const FString& Label, FString& OutError)
    {
        if (World == nullptr)
        {
            OutError = TEXT("There is no editor world, so no actor can be possessed. Open a level.");
            return nullptr;
        }
        TArray<AActor*> Matches;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            AActor* Actor = *It;
            if (Actor != nullptr && Actor->GetActorLabel() == Label)
            {
                Matches.Add(Actor);
            }
        }
        if (Matches.Num() == 0)
        {
            OutError = FString::Printf(
                TEXT("No actor in the current level has the label '%s'."), *Label);
            return nullptr;
        }
        if (Matches.Num() > 1)
        {
            TArray<FString> Names;
            for (const AActor* Actor : Matches)
            {
                Names.Add(FString::Printf(TEXT("%s (%s)"), *Actor->GetName(), *Actor->GetClass()->GetName()));
            }
            OutError = FString::Printf(
                TEXT("The label '%s' matches %d actors: %s. A binding needs exactly one."),
                *Label, Matches.Num(), *FString::Join(Names, TEXT(", ")));
            return nullptr;
        }
        return Matches[0];
    }

    /** The track on this binding (or master) with this class and property.
        UMovieScene::FindTrack matches on class and track name only, so two Float
        tracks on one binding animating different properties are the same track
        to it; the property is part of the identity here. */
    UMovieSceneTrack* FindMatchingTrack(
        UMovieScene* MovieScene,
        const FGuid& Guid,
        bool bMaster,
        UClass* TrackClass,
        const FString& Property)
    {
        auto Matches = [&](UMovieSceneTrack* Track) -> bool
        {
            if (Track == nullptr || Track->GetClass() != TrackClass) { return false; }
            if (Property.IsEmpty()) { return true; }
            return TrackPropertyPath(Track) == Property;
        };
        if (bMaster)
        {
            for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
            {
                if (Matches(Track)) { return Track; }
            }
            return nullptr;
        }
        for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
        {
            if (Binding.GetObjectGuid() != Guid) { continue; }
            for (UMovieSceneTrack* Track : Binding.GetTracks())
            {
                if (Matches(Track)) { return Track; }
            }
        }
        return nullptr;
    }

    /** The section on this track covering exactly this tick range. Exact rather
        than overlapping on purpose: a section IS its range here, so "the same
        section" and "the same range" are the same statement, and an overlap
        match would make a spec that asks for two abutting sections converge onto
        one. */
    UMovieSceneSection* FindSectionByRange(
        UMovieSceneTrack* Track,
        FFrameNumber StartTick,
        FFrameNumber EndTick)
    {
        for (UMovieSceneSection* Section : Track->GetAllSections())
        {
            if (Section == nullptr || !Section->HasStartFrame() || !Section->HasEndFrame()) { continue; }
            if (Section->GetInclusiveStartFrame() == StartTick
                && Section->GetExclusiveEndFrame() == EndTick)
            {
                return Section;
            }
        }
        return nullptr;
    }
}

bool UMCPPuerTSBridgeService::BuildLevelSequenceJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Sequence build spec must be a JSON object.");
        return false;
    }

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    AssetPath = AssetPath.TrimStartAndEnd();
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/")))
    {
        OutError = TEXT("Level Sequence authoring is limited to /Game/MCPGenerated/.");
        return false;
    }
    FString PackagePath;
    FString ObjectPath;
    if (!SplitAssetPath(AssetPath, PackagePath, ObjectPath, OutError))
    {
        return false;
    }
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);

    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);

    auto Finish = [&](bool bSuccess) -> bool
    {
        Result->SetArrayField(TEXT("warnings"), Warnings);
        Result->SetNumberField(TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeJson(Result);
        return bSuccess;
    };

    // ---------------------------------------------------------------------
    // Validate the whole spec before anything is created. Everything below
    // this block either refuses with a named reason or is known to be legal.
    // ---------------------------------------------------------------------

    UWorld* World = EditorWorld();

    TArray<FResolvedBinding> Bindings;
    TMap<FString, int32> BindingsById;
    const TArray<TSharedPtr<FJsonValue>>* BindingArray = nullptr;
    if (Spec->TryGetArrayField(TEXT("bindings"), BindingArray))
    {
        for (int32 Index = 0; Index < BindingArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!(*BindingArray)[Index]->TryGetObject(Entry))
            {
                OutError = FString::Printf(TEXT("bindings[%d] is not an object."), Index);
                return Finish(false);
            }
            FResolvedBinding Resolved;
            (*Entry)->TryGetStringField(TEXT("id"), Resolved.Id);
            (*Entry)->TryGetStringField(TEXT("name"), Resolved.Name);
            FString Kind;
            (*Entry)->TryGetStringField(TEXT("kind"), Kind);
            if (Resolved.Id.IsEmpty() || Resolved.Name.IsEmpty())
            {
                OutError = FString::Printf(TEXT("bindings[%d] needs a non-empty id and name."), Index);
                return Finish(false);
            }
            if (BindingsById.Contains(Resolved.Id))
            {
                OutError = FString::Printf(
                    TEXT("bindings[%d]: the id '%s' is declared twice. A binding id addresses one "
                         "binding."), Index, *Resolved.Id);
                return Finish(false);
            }
            if (Kind == TEXT("spawnable"))
            {
                Resolved.bSpawnable = true;
                FString ClassPath;
                (*Entry)->TryGetStringField(TEXT("actor_class"), ClassPath);
                if (ClassPath.IsEmpty())
                {
                    OutError = FString::Printf(
                        TEXT("bindings[%d] ('%s') is a spawnable and needs actor_class."),
                        Index, *Resolved.Id);
                    return Finish(false);
                }
                Resolved.SpawnClass = ResolveActorClass(ClassPath, OutError);
                if (Resolved.SpawnClass == nullptr)
                {
                    OutError = FString::Printf(TEXT("bindings[%d] ('%s'): %s"), Index, *Resolved.Id, *OutError);
                    return Finish(false);
                }
            }
            else if (Kind == TEXT("possessable"))
            {
                FString Label;
                (*Entry)->TryGetStringField(TEXT("actor_label"), Label);
                if (Label.IsEmpty())
                {
                    OutError = FString::Printf(
                        TEXT("bindings[%d] ('%s') is a possessable and needs actor_label."),
                        Index, *Resolved.Id);
                    return Finish(false);
                }
                Resolved.Actor = FindActorByLabel(World, Label, OutError);
                if (Resolved.Actor == nullptr)
                {
                    OutError = FString::Printf(TEXT("bindings[%d] ('%s'): %s"), Index, *Resolved.Id, *OutError);
                    return Finish(false);
                }
            }
            else
            {
                OutError = FString::Printf(
                    TEXT("bindings[%d] ('%s'): kind must be 'possessable' or 'spawnable', not '%s'."),
                    Index, *Resolved.Id, *Kind);
                return Finish(false);
            }
            BindingsById.Add(Resolved.Id, Bindings.Num());
            Bindings.Add(Resolved);
        }
    }

    TArray<FResolvedTrack> Tracks;
    const TArray<TSharedPtr<FJsonValue>>* TrackArray = nullptr;
    if (Spec->TryGetArrayField(TEXT("tracks"), TrackArray))
    {
        for (int32 Index = 0; Index < TrackArray->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!(*TrackArray)[Index]->TryGetObject(Entry))
            {
                OutError = FString::Printf(TEXT("tracks[%d] is not an object."), Index);
                return Finish(false);
            }
            FResolvedTrack Track;
            Track.Index = Index;
            (*Entry)->TryGetStringField(TEXT("binding"), Track.BindingId);
            (*Entry)->TryGetStringField(TEXT("type"), Track.Type);
            (*Entry)->TryGetStringField(TEXT("property"), Track.Property);

            if (Track.Type == TEXT("Event") || Track.Type == TEXT("Audio"))
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: '%s' tracks are not authored by this command, and that is a "
                         "stated gap rather than an oversight. An event track binds to a Level "
                         "Sequence Director Blueprint, which no tool in this bridge authors, and an "
                         "audio section needs a USoundBase, which nothing in this bridge can "
                         "produce or import."),
                    Index, *Track.Type);
                return Finish(false);
            }
            if (TrackClassForType(Track.Type) == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: type must be Transform, Float, Bool, Visibility or Camera, "
                         "not '%s'."), Index, *Track.Type);
                return Finish(false);
            }
            const bool bMaster = Track.BindingId == TEXT("master");
            if (!bMaster && !BindingsById.Contains(Track.BindingId))
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: binding '%s' is not declared in this spec's bindings, and is "
                         "not \"master\"."), Index, *Track.BindingId);
                return Finish(false);
            }
            if (Track.Type == TEXT("Camera") && !bMaster)
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: a Camera track is a master track. Its binding must be "
                         "\"master\"; the camera it cuts to is the value on each key."), Index);
                return Finish(false);
            }
            if ((Track.Type == TEXT("Float") || Track.Type == TEXT("Bool")) && Track.Property.IsEmpty())
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: a %s track needs property, the path Sequencer animates "
                         "(\"Light.Intensity\")."), Index, *Track.Type);
                return Finish(false);
            }
            if (Track.Type == TEXT("Visibility") && Track.Property.IsEmpty())
            {
                // The engine's own visibility track animates bHidden. Filling it
                // in rather than refusing means a caller does not have to know
                // the property name to hide something.
                Track.Property = TEXT("bHidden");
            }

            const TArray<TSharedPtr<FJsonValue>>* SectionArray = nullptr;
            if (!(*Entry)->TryGetArrayField(TEXT("sections"), SectionArray))
            {
                OutError = FString::Printf(TEXT("tracks[%d]: sections is required."), Index);
                return Finish(false);
            }
            for (int32 S = 0; S < SectionArray->Num(); ++S)
            {
                const TSharedPtr<FJsonObject>* SectionEntry = nullptr;
                if (!(*SectionArray)[S]->TryGetObject(SectionEntry))
                {
                    OutError = FString::Printf(TEXT("tracks[%d].sections[%d] is not an object."), Index, S);
                    return Finish(false);
                }
                FResolvedSection Section;
                Section.StartFrame = static_cast<int32>((*SectionEntry)->GetNumberField(TEXT("start_frame")));
                Section.EndFrame = static_cast<int32>((*SectionEntry)->GetNumberField(TEXT("end_frame")));
                if (Section.EndFrame <= Section.StartFrame)
                {
                    OutError = FString::Printf(
                        TEXT("tracks[%d].sections[%d]: end_frame (%d) must be greater than "
                             "start_frame (%d). The range is end-exclusive, the same convention "
                             "Sequencer uses."),
                        Index, S, Section.EndFrame, Section.StartFrame);
                    return Finish(false);
                }
                double RowIndex = 0.0;
                if ((*SectionEntry)->TryGetNumberField(TEXT("row_index"), RowIndex))
                {
                    Section.RowIndex = static_cast<int32>(RowIndex);
                    Section.bHasRowIndex = true;
                }

                const TArray<TSharedPtr<FJsonValue>>* KeyArray = nullptr;
                (*SectionEntry)->TryGetArrayField(TEXT("keys"), KeyArray);
                for (int32 K = 0; KeyArray != nullptr && K < KeyArray->Num(); ++K)
                {
                    const TSharedPtr<FJsonObject>* KeyEntry = nullptr;
                    if (!(*KeyArray)[K]->TryGetObject(KeyEntry))
                    {
                        OutError = FString::Printf(
                            TEXT("tracks[%d].sections[%d].keys[%d] is not an object."), Index, S, K);
                        return Finish(false);
                    }
                    const int32 Frame = static_cast<int32>((*KeyEntry)->GetNumberField(TEXT("frame")));
                    FString InterpName;
                    (*KeyEntry)->TryGetStringField(TEXT("interpolation"), InterpName);
                    const ERichCurveInterpMode Interp = InterpolationFromName(InterpName);
                    const TSharedPtr<FJsonValue> Value = (*KeyEntry)->TryGetField(TEXT("value"));
                    if (!Value.IsValid())
                    {
                        OutError = FString::Printf(
                            TEXT("tracks[%d].sections[%d].keys[%d]: value is required."), Index, S, K);
                        return Finish(false);
                    }

                    if (Track.Type == TEXT("Camera"))
                    {
                        FString CameraId;
                        if (!Value->TryGetString(CameraId) || !BindingsById.Contains(CameraId))
                        {
                            OutError = FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: a Camera key's value is the "
                                     "id of a binding declared in this spec, as a string. '%s' is "
                                     "not one."),
                                Index, S, K, *CameraId);
                            return Finish(false);
                        }
                        Section.CameraCuts.Add(TPair<int32, FString>(Frame, CameraId));
                        if (!InterpName.IsEmpty())
                        {
                            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: interpolation is ignored on "
                                     "a camera cut, which has no curve."), Index, S, K)));
                        }
                        continue;
                    }

                    if (Track.Type == TEXT("Transform"))
                    {
                        const TSharedPtr<FJsonObject>* Components = nullptr;
                        if (!Value->TryGetObject(Components))
                        {
                            OutError = FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: a Transform key's value is "
                                     "{location, rotation, scale}, any subset."), Index, S, K);
                            return Finish(false);
                        }
                        // The caller writes rotation as the {pitch, yaw, roll}
                        // rotator every other tool in this bridge uses. The
                        // engine's channel order is Roll, Pitch, Yaw, and the
                        // mapping happens here so nobody outside this file has
                        // to know that.
                        static const TCHAR* Groups[3] = { TEXT("location"), TEXT("rotation"), TEXT("scale") };
                        static const TCHAR* Axes[3][3] = {
                            { TEXT("x"), TEXT("y"), TEXT("z") },
                            { TEXT("roll"), TEXT("pitch"), TEXT("yaw") },
                            { TEXT("x"), TEXT("y"), TEXT("z") },
                        };
                        for (int32 G = 0; G < 3; ++G)
                        {
                            const TSharedPtr<FJsonObject>* Group = nullptr;
                            if (!(*Components)->TryGetObjectField(Groups[G], Group)) { continue; }
                            for (int32 A = 0; A < 3; ++A)
                            {
                                double Component = 0.0;
                                if (!(*Group)->TryGetNumberField(Axes[G][A], Component)) { continue; }
                                FResolvedKey ResolvedKey;
                                ResolvedKey.ChannelIndex = TransformChannelIndex(
                                    FString::Printf(TEXT("%s.%s"), Groups[G], Axes[G][A]));
                                if (ResolvedKey.ChannelIndex == INDEX_NONE) { continue; }
                                // Reported under the same channel name the
                                // inspector uses, so a build log and a read line
                                // up without a spelling translation.
                                ResolvedKey.ChannelName = GTransformChannelNames[ResolvedKey.ChannelIndex];
                                ResolvedKey.DisplayFrame = Frame;
                                ResolvedKey.FloatValue = static_cast<float>(Component);
                                ResolvedKey.Interp = Interp;
                                Section.Keys.Add(ResolvedKey);
                            }
                        }
                        continue;
                    }

                    FResolvedKey ResolvedKey;
                    ResolvedKey.DisplayFrame = Frame;
                    ResolvedKey.ChannelIndex = 0;
                    ResolvedKey.ChannelName = TEXT("Value");
                    ResolvedKey.Interp = Interp;
                    if (Track.Type == TEXT("Float"))
                    {
                        double Number = 0.0;
                        if (!Value->TryGetNumber(Number))
                        {
                            OutError = FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: a Float key's value is a "
                                     "number."), Index, S, K);
                            return Finish(false);
                        }
                        ResolvedKey.FloatValue = static_cast<float>(Number);
                    }
                    else
                    {
                        bool bValue = false;
                        if (!Value->TryGetBool(bValue))
                        {
                            OutError = FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: a %s key's value is true or "
                                     "false."), Index, S, K, *Track.Type);
                            return Finish(false);
                        }
                        ResolvedKey.bBoolChannel = true;
                        ResolvedKey.bBoolValue = bValue;
                        if (!InterpName.IsEmpty())
                        {
                            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                                TEXT("tracks[%d].sections[%d].keys[%d]: interpolation is ignored on "
                                     "a boolean channel, which has none."), Index, S, K)));
                        }
                    }
                    Section.Keys.Add(ResolvedKey);
                }
                Track.Sections.Add(Section);
            }
            Tracks.Add(Track);
        }
    }

    // Two tracks with the same identity in one spec have no single desired
    // state, the same refusal scene_batch makes for two operations on one actor.
    {
        TSet<FString> Seen;
        for (const FResolvedTrack& Track : Tracks)
        {
            const FString Key = Track.BindingId + TEXT("|") + Track.Type + TEXT("|") + Track.Property;
            if (Seen.Contains(Key))
            {
                OutError = FString::Printf(
                    TEXT("tracks[%d]: binding '%s' already has a %s track for property '%s' in this "
                         "same spec. Two entries with one identity have no single desired state; "
                         "put every section in one track entry."),
                    Track.Index, *Track.BindingId, *Track.Type, *Track.Property);
                return Finish(false);
            }
            Seen.Add(Key);
        }
    }

    // ---------------------------------------------------------------------
    // Load or create.
    // ---------------------------------------------------------------------

    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(PackagePath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    FString FindError;
    ULevelSequence* Sequence = FindLevelSequence(ObjectPath, FindError);
    if (Sequence == nullptr && !FindError.IsEmpty())
    {
        OutError = FindError;
        return Finish(false);
    }
    const bool bCreating = Sequence == nullptr;
    Result->SetBoolField(TEXT("created"), bCreating);

    // Plan mode answers before anything is created, which is what makes it
    // genuinely read-only rather than "read-only apart from the asset".
    if (bPlanOnly)
    {
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetStringField(TEXT("pre_structure_hash"),
            Sequence != nullptr ? SnapshotHash(BuildSnapshot(Sequence, World)) : FString());
        // The plan is stated as counts rather than as a per-item verdict on
        // purpose: an item's satisfied-ness is decided immediately before its
        // own mutation, against state an earlier item may have moved, so a
        // per-item verdict computed here would be a promise this command
        // deliberately does not make.
        int32 KeyTotal = 0;
        int32 SectionTotal = 0;
        int32 CutTotal = 0;
        for (const FResolvedTrack& Track : Tracks)
        {
            SectionTotal += Track.Sections.Num();
            for (const FResolvedSection& Section : Track.Sections)
            {
                KeyTotal += Section.Keys.Num();
                CutTotal += Section.CameraCuts.Num();
            }
        }
        Result->SetNumberField(TEXT("requested_binding_count"), Bindings.Num());
        Result->SetNumberField(TEXT("requested_track_count"), Tracks.Num());
        Result->SetNumberField(TEXT("requested_section_count"), SectionTotal);
        Result->SetNumberField(TEXT("requested_key_count"), KeyTotal);
        Result->SetNumberField(TEXT("requested_camera_cut_count"), CutTotal);
        Result->SetStringField(TEXT("prediction_unavailable_reason"),
            TEXT("a predicted structure hash is not offered: creating the bindings assigns FGuids "
                 "the engine makes at creation time, and a hash predicted without them would be a "
                 "hash of a different asset. Run without plan_only and compare "
                 "post_structure_hash against puerts_sequence_inspect."));
        return Finish(true);
    }

    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Sequence building requires an active transaction.");
        return Finish(false);
    }

    int32 AppliedCount = 0;
    TArray<FString> AppliedOperations;
    TArray<FString> UnchangedOperations;
    FString PreHash;

    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        Rollback.Rollback();
        // Trust the read-back, not the undo. A cancelled transaction LOOKS
        // transactional; the only thing that settles whether the asset came back
        // is hashing it again.
        FString Recovered;
        bool bRecovered = false;
        if (bCreating)
        {
            // A created asset should be gone entirely, so the check is the
            // rollback boundary's own evidence rather than a hash of nothing.
            bRecovered = Rollback.BuildEvidence(DirtyBefore, SourceControlBefore)
                ->GetBoolField(TEXT("rollback_succeeded"));
        }
        else if (Sequence != nullptr)
        {
            Recovered = SnapshotHash(BuildSnapshot(Sequence, World));
            bRecovered = Recovered == PreHash;
        }
        OutError = bRecovered
            ? Error
            : FString::Printf(
                TEXT("%s The rollback did NOT restore the sequence: the structure hash is '%s', "
                     "expected '%s'. Treat '%s' as damaged and do not save it."),
                *Error, *Recovered, *PreHash, *PackagePath);

        Result->SetBoolField(TEXT("rollback_attempted"), true);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRecovered);
        Result->SetStringField(TEXT("pre_structure_hash"), PreHash);
        Result->SetStringField(TEXT("post_structure_hash"), Recovered);
        Result->SetArrayField(TEXT("applied_before_failure"), StringsToJsonValues(AppliedOperations));
        Result->SetArrayField(TEXT("unchanged_before_failure"), StringsToJsonValues(UnchangedOperations));
        Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        return Finish(false);
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(*PackagePath);
        if (Package == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("Unreal could not create the package '%s'."), *PackagePath));
        }
        Sequence = NewObject<ULevelSequence>(
            Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (Sequence == nullptr)
        {
            return FailWithRollback(TEXT("Unreal could not create the Level Sequence."));
        }
        // ULevelSequenceFactoryNew is private to the LevelSequenceEditor PLUGIN
        // and cannot be included from here, so this reproduces what it does
        // (LevelSequenceFactoryNew.cpp:26-37). Initialize() is not optional:
        // without it GetMovieScene() is null and every call below would fault.
        Sequence->Initialize();
        Rollback.TrackCreated(Sequence);
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        AssetRegistryModule.Get().AssetCreated(Sequence);
        AppliedOperations.Add(FString::Printf(TEXT("create sequence '%s'"), *PackagePath));
        AppliedCount++;
    }

    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (MovieScene == nullptr)
    {
        return FailWithRollback(FString::Printf(
            TEXT("The Level Sequence at '%s' has no MovieScene."), *PackagePath));
    }
    PreHash = bCreating ? FString() : SnapshotHash(BuildSnapshot(Sequence, World));
    Result->SetStringField(TEXT("pre_structure_hash"), PreHash);

    // Modify() before anything is written. UMovieScene's own AddTrack,
    // AddPossessable and AddSpawnable modify it themselves, but the section and
    // channel writes below do not (MovieScenePropertyTrack.cpp:84-137 never
    // calls Modify), so one call here is what puts the whole command inside the
    // undo record and makes the failure path atomic.
    Sequence->Modify();
    MovieScene->Modify();

    // ---------------------------------------------------------------------
    // Rate and range.
    // ---------------------------------------------------------------------

    double RequestedFps = 0.0;
    if (Spec->TryGetNumberField(TEXT("frame_rate"), RequestedFps) && RequestedFps > 0.0)
    {
        const FFrameRate Desired = FrameRateFromDecimal(RequestedFps);
        if (RatesMatch(MovieScene->GetDisplayRate(), Desired))
        {
            UnchangedOperations.Add(FString::Printf(TEXT("display rate already %g fps"), Desired.AsDecimal()));
        }
        else
        {
            if (!bCreating)
            {
                Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("The display rate changed from %g to %g fps. UE4.27 stores key times in "
                         "ticks, so existing keys did NOT move: they now land on different display "
                         "frames. Read the sequence back before assuming a timing."),
                    MovieScene->GetDisplayRate().AsDecimal(), Desired.AsDecimal())));
            }
            MovieScene->SetDisplayRate(Desired);
            AppliedOperations.Add(FString::Printf(TEXT("set display rate to %g fps"), Desired.AsDecimal()));
            AppliedCount++;
        }
    }

    const TSharedPtr<FJsonObject>* PlaybackRange = nullptr;
    if (Spec->TryGetObjectField(TEXT("playback_range"), PlaybackRange))
    {
        const int32 StartFrame = static_cast<int32>((*PlaybackRange)->GetNumberField(TEXT("start_frame")));
        const int32 EndFrame = static_cast<int32>((*PlaybackRange)->GetNumberField(TEXT("end_frame")));
        if (EndFrame <= StartFrame)
        {
            return FailWithRollback(FString::Printf(
                TEXT("playback_range: end_frame (%d) must be greater than start_frame (%d)."),
                EndFrame, StartFrame));
        }
        // Converted AFTER any display-rate change above, because the conversion
        // depends on the rate this command may have just set. Reading the frames
        // once at the top and converting them here would silently use the old
        // rate: exactly the class of bug where a value decided before an earlier
        // operation ran is applied after it.
        const FFrameNumber StartTick = DisplayToTick(MovieScene, StartFrame);
        const FFrameNumber EndTick = DisplayToTick(MovieScene, EndFrame);
        const TRange<FFrameNumber> Desired(StartTick, EndTick);
        if (MovieScene->GetPlaybackRange() == Desired)
        {
            UnchangedOperations.Add(TEXT("playback range already correct"));
        }
        else
        {
            MovieScene->SetPlaybackRange(Desired);
            AppliedOperations.Add(FString::Printf(
                TEXT("set playback range to frames %d..%d"), StartFrame, EndFrame));
            AppliedCount++;
        }
    }

    // ---------------------------------------------------------------------
    // Bindings.
    // ---------------------------------------------------------------------

    for (FResolvedBinding& Binding : Bindings)
    {
        // Asked here, not read from a plan. An earlier binding in this same
        // batch can have created the one this loop is about to look for.
        FGuid Existing;
        if (Binding.bSpawnable)
        {
            for (int32 Index = 0; Index < MovieScene->GetSpawnableCount(); ++Index)
            {
                const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(Index);
                if (Spawnable.GetName() == Binding.Name)
                {
                    const UObject* Template = Spawnable.GetObjectTemplate();
                    if (Template != nullptr && Template->GetClass() != Binding.SpawnClass)
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("binding '%s': the sequence already has a spawnable named '%s' of "
                                 "class %s, and the spec asks for %s. Replacing a spawnable's class "
                                 "would silently drop every track bound to it, so this is a refusal "
                                 "rather than a rewrite. Rename the binding or remove the existing "
                                 "one in Sequencer."),
                            *Binding.Id, *Binding.Name,
                            *Template->GetClass()->GetName(), *Binding.SpawnClass->GetName()));
                    }
                    Existing = Spawnable.GetGuid();
                    break;
                }
            }
        }
        else
        {
            for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
            {
                const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
                if (Possessable.GetName() == Binding.Name)
                {
                    Existing = Possessable.GetGuid();
                    break;
                }
            }
        }

        if (Existing.IsValid())
        {
            Binding.Guid = Existing;
            if (!Binding.bSpawnable)
            {
                // A possessable can exist and point at the wrong actor, or at
                // nothing, so "it exists" is not "it is satisfied".
                bool bBoundCorrectly = false;
                for (UObject* Bound : Sequence->LocateBoundObjects(Existing, World))
                {
                    if (Bound == Binding.Actor) { bBoundCorrectly = true; break; }
                }
                if (!bBoundCorrectly)
                {
                    Sequence->UnbindPossessableObjects(Existing);
                    // ULevelSequence::BindPossessableObject is a SILENT no-op on
                    // a null Context (LevelSequence.cpp:347-353): no log, no
                    // return value, and the binding simply is not written. The
                    // world was checked when the label was resolved, so it is
                    // non-null here, and this refusal exists so that stays true.
                    if (World == nullptr)
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("binding '%s': there is no editor world to bind against."), *Binding.Id));
                    }
                    Sequence->BindPossessableObject(Existing, *Binding.Actor, World);
                    AppliedOperations.Add(FString::Printf(
                        TEXT("rebind possessable '%s' to '%s'"), *Binding.Name, *Binding.Actor->GetActorLabel()));
                    AppliedCount++;
                }
                else
                {
                    UnchangedOperations.Add(FString::Printf(
                        TEXT("possessable '%s' already bound"), *Binding.Name));
                }
            }
            else
            {
                UnchangedOperations.Add(FString::Printf(TEXT("spawnable '%s' already present"), *Binding.Name));
            }
        }
        else if (Binding.bSpawnable)
        {
            // The object template must be an inner of the UMovieScene, and
            // RF_Transactional so the template itself is inside the undo record.
            // This is what FLevelSequenceEditorActorSpawner does for a UClass
            // (LevelSequenceEditorActorSpawner.cpp:129-140), minus the editor
            // plugin dependency that path would cost.
            const FName TemplateName = MakeUniqueObjectName(
                MovieScene, UObject::StaticClass(), Binding.SpawnClass->GetFName());
            UObject* Template = NewObject<UObject>(
                MovieScene, Binding.SpawnClass, TemplateName, RF_Transactional);
            if (Template == nullptr)
            {
                return FailWithRollback(FString::Printf(
                    TEXT("binding '%s': Unreal could not create a spawnable template of class %s."),
                    *Binding.Id, *Binding.SpawnClass->GetName()));
            }
            const FString UniqueName = MovieSceneHelpers::MakeUniqueSpawnableName(MovieScene, Binding.Name);
            if (UniqueName != Binding.Name)
            {
                // MakeUniqueSpawnableName appends " (2)" on a collision, and a
                // binding whose name silently moved is a binding a rerun would
                // not find, which would break convergence rather than a build.
                return FailWithRollback(FString::Printf(
                    TEXT("binding '%s': the name '%s' collides with something the sequence already "
                         "holds, and Unreal would rename it to '%s'. A renamed binding is one a "
                         "rerun of this spec cannot find, so this is a refusal. Pick another name."),
                    *Binding.Id, *Binding.Name, *UniqueName));
            }
            Binding.Guid = MovieScene->AddSpawnable(Binding.Name, *Template);
            if (!Binding.Guid.IsValid())
            {
                return FailWithRollback(FString::Printf(
                    TEXT("binding '%s': AddSpawnable returned no guid."), *Binding.Id));
            }
            AppliedOperations.Add(FString::Printf(
                TEXT("add spawnable '%s' (%s)"), *Binding.Name, *Binding.SpawnClass->GetName()));
            AppliedCount++;
        }
        else
        {
            Binding.Guid = MovieScene->AddPossessable(Binding.Name, Binding.Actor->GetClass());
            if (!Binding.Guid.IsValid())
            {
                return FailWithRollback(FString::Printf(
                    TEXT("binding '%s': AddPossessable returned no guid."), *Binding.Id));
            }
            Sequence->BindPossessableObject(Binding.Guid, *Binding.Actor, World);
            AppliedOperations.Add(FString::Printf(
                TEXT("add possessable '%s' -> '%s'"), *Binding.Name, *Binding.Actor->GetActorLabel()));
            AppliedCount++;
        }

        // A spawnable with no spawn track never spawns, which is a sequence that
        // authors correctly and plays empty - the worst kind of silent success.
        // The engine's own block is LevelSequenceEditorActorSpawner.cpp:229-246.
        if (Binding.bSpawnable)
        {
            UMovieSceneSpawnTrack* SpawnTrack = Cast<UMovieSceneSpawnTrack>(
                MovieScene->FindTrack(UMovieSceneSpawnTrack::StaticClass(), Binding.Guid, NAME_None));
            if (SpawnTrack == nullptr)
            {
                SpawnTrack = Cast<UMovieSceneSpawnTrack>(
                    MovieScene->AddTrack(UMovieSceneSpawnTrack::StaticClass(), Binding.Guid));
                if (SpawnTrack == nullptr)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("binding '%s': could not add the spawn track that makes a spawnable "
                             "spawn."), *Binding.Id));
                }
                UMovieSceneBoolSection* SpawnSection =
                    Cast<UMovieSceneBoolSection>(SpawnTrack->CreateNewSection());
                if (SpawnSection == nullptr)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("binding '%s': the spawn track produced no section."), *Binding.Id));
                }
                SpawnTrack->Modify();
                SpawnSection->GetChannel().SetDefault(true);
                SpawnSection->SetRange(TRange<FFrameNumber>::All());
                SpawnTrack->AddSection(*SpawnSection);
                SpawnTrack->SetObjectId(Binding.Guid);
                AppliedOperations.Add(FString::Printf(
                    TEXT("add spawn track for '%s'"), *Binding.Name));
                AppliedCount++;
            }
            else
            {
                UnchangedOperations.Add(FString::Printf(
                    TEXT("spawn track for '%s' already present"), *Binding.Name));
            }
        }
    }

    // ---------------------------------------------------------------------
    // Tracks, sections, keys.
    // ---------------------------------------------------------------------

    for (const FResolvedTrack& TrackSpec : Tracks)
    {
        const bool bMaster = TrackSpec.BindingId == TEXT("master");
        FGuid BindingGuid;
        if (!bMaster)
        {
            const int32* Index = BindingsById.Find(TrackSpec.BindingId);
            if (Index == nullptr || !Bindings[*Index].Guid.IsValid())
            {
                return FailWithRollback(FString::Printf(
                    TEXT("tracks[%d]: binding '%s' has no guid, so the binding loop did not create "
                         "it. This is a defect in this command, not in the request."),
                    TrackSpec.Index, *TrackSpec.BindingId));
            }
            BindingGuid = Bindings[*Index].Guid;
        }
        UClass* TrackClass = TrackClassForType(TrackSpec.Type);

        // Asked live, immediately before the mutation. An earlier track in this
        // same spec cannot have created this one (identities are unique, and
        // duplicates were refused above), but an earlier RUN can have, and so
        // can a hand edit between two calls.
        UMovieSceneTrack* Track = FindMatchingTrack(
            MovieScene, BindingGuid, bMaster, TrackClass, TrackSpec.Property);
        if (Track == nullptr)
        {
            Track = bMaster
                ? MovieScene->AddMasterTrack(TrackClass)
                : MovieScene->AddTrack(TrackClass, BindingGuid);
            if (Track == nullptr)
            {
                return FailWithRollback(FString::Printf(
                    TEXT("tracks[%d]: Unreal could not add a %s track to '%s'."),
                    TrackSpec.Index, *TrackSpec.Type, *TrackSpec.BindingId));
            }
            if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
            {
                if (!TrackSpec.Property.IsEmpty())
                {
                    // SetPropertyNameAndPath check()s on an empty name or path
                    // (MovieScenePropertyTrack.cpp:18-28), so the emptiness was
                    // refused during validation rather than asserted here.
                    FString Left;
                    FString Right;
                    const FString Name = TrackSpec.Property.Split(TEXT("."), &Left, &Right,
                        ESearchCase::CaseSensitive, ESearchDir::FromEnd)
                        ? Right : TrackSpec.Property;
                    PropertyTrack->SetPropertyNameAndPath(FName(*Name), TrackSpec.Property);
                }
            }
            const FString PropertySuffix = TrackSpec.Property.IsEmpty()
                ? FString() : FString::Printf(TEXT(" (%s)"), *TrackSpec.Property);
            AppliedOperations.Add(FString::Printf(
                TEXT("add %s track on '%s'%s"),
                *TrackSpec.Type, *TrackSpec.BindingId, *PropertySuffix));
            AppliedCount++;
        }
        else
        {
            UnchangedOperations.Add(FString::Printf(
                TEXT("%s track on '%s' already present"), *TrackSpec.Type, *TrackSpec.BindingId));
        }
        Track->Modify();

        for (const FResolvedSection& SectionSpec : TrackSpec.Sections)
        {
            // A camera cut track owns its own section ranges: AddNewCameraCut
            // computes the end from the next cut (MovieSceneCameraCutTrack.cpp
            // :31-56), so a range asked for here would be overwritten by the
            // engine and reporting it as applied would be a lie.
            if (TrackSpec.Type == TEXT("Camera"))
            {
                UMovieSceneCameraCutTrack* CutTrack = Cast<UMovieSceneCameraCutTrack>(Track);
                if (CutTrack == nullptr)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("tracks[%d]: the master Camera track is a %s, not a camera cut track."),
                        TrackSpec.Index, *Track->GetClass()->GetName()));
                }
                for (const TPair<int32, FString>& Cut : SectionSpec.CameraCuts)
                {
                    const int32* CameraIndex = BindingsById.Find(Cut.Value);
                    if (CameraIndex == nullptr || !Bindings[*CameraIndex].Guid.IsValid())
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("tracks[%d]: camera cut at frame %d names binding '%s', which has "
                                 "no guid."), TrackSpec.Index, Cut.Key, *Cut.Value));
                    }
                    const FGuid CameraGuid = Bindings[*CameraIndex].Guid;
                    const FFrameNumber CutTick = DisplayToTick(MovieScene, Cut.Key);

                    // Asked live: an earlier cut in this same spec shifts the
                    // section boundaries of the ones around it, so a verdict
                    // computed before the loop would be answering about a track
                    // that no longer exists in that shape.
                    bool bSatisfied = false;
                    for (UMovieSceneSection* Section : CutTrack->GetAllSections())
                    {
                        const UMovieSceneCameraCutSection* CutSection =
                            Cast<UMovieSceneCameraCutSection>(Section);
                        if (CutSection == nullptr || !Section->HasStartFrame()) { continue; }
                        if (Section->GetInclusiveStartFrame() == CutTick
                            && CutSection->GetCameraBindingID().GetGuid() == CameraGuid)
                        {
                            bSatisfied = true;
                            break;
                        }
                    }
                    if (bSatisfied)
                    {
                        UnchangedOperations.Add(FString::Printf(
                            TEXT("camera cut to '%s' at frame %d already present"), *Cut.Value, Cut.Key));
                        continue;
                    }
                    if (CutTrack->AddNewCameraCut(
                            UE::MovieScene::FRelativeObjectBindingID(CameraGuid), CutTick) == nullptr)
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("tracks[%d]: Unreal refused the camera cut to '%s' at frame %d."),
                            TrackSpec.Index, *Cut.Value, Cut.Key));
                    }
                    AppliedOperations.Add(FString::Printf(
                        TEXT("cut to camera '%s' at frame %d"), *Cut.Value, Cut.Key));
                    AppliedCount++;
                }
                if (SectionSpec.Keys.Num() > 0)
                {
                    Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("tracks[%d]: start_frame and end_frame on a Camera section are "
                             "advisory. A camera cut section's range is computed by the engine from "
                             "the next cut, so only each key's frame was used."), TrackSpec.Index)));
                }
                continue;
            }

            const FFrameNumber StartTick = DisplayToTick(MovieScene, SectionSpec.StartFrame);
            const FFrameNumber EndTick = DisplayToTick(MovieScene, SectionSpec.EndFrame);
            UMovieSceneSection* Section = FindSectionByRange(Track, StartTick, EndTick);
            if (Section == nullptr)
            {
                Section = Track->CreateNewSection();
                if (Section == nullptr)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("tracks[%d]: a %s track produced no section."),
                        TrackSpec.Index, *TrackSpec.Type));
                }
                // CreateNewSection only creates: AddSection is what attaches it
                // (MovieSceneTrack.h:349-356), and a section that is never added
                // is a section that never plays.
                Section->SetRange(TRange<FFrameNumber>(StartTick, EndTick));
                if (SectionSpec.bHasRowIndex) { Section->SetRowIndex(SectionSpec.RowIndex); }
                Track->AddSection(*Section);
                AppliedOperations.Add(FString::Printf(
                    TEXT("add %s section on '%s' at frames %d..%d"),
                    *TrackSpec.Type, *TrackSpec.BindingId, SectionSpec.StartFrame, SectionSpec.EndFrame));
                AppliedCount++;
            }
            else
            {
                UnchangedOperations.Add(FString::Printf(
                    TEXT("%s section on '%s' at frames %d..%d already present"),
                    *TrackSpec.Type, *TrackSpec.BindingId, SectionSpec.StartFrame, SectionSpec.EndFrame));
                if (SectionSpec.bHasRowIndex && Section->GetRowIndex() != SectionSpec.RowIndex)
                {
                    Section->Modify();
                    Section->SetRowIndex(SectionSpec.RowIndex);
                    AppliedOperations.Add(FString::Printf(
                        TEXT("set row index %d on the %s section at frames %d..%d"),
                        SectionSpec.RowIndex, *TrackSpec.Type, SectionSpec.StartFrame, SectionSpec.EndFrame));
                    AppliedCount++;
                }
            }
            Section->Modify();

            for (const FResolvedKey& Key : SectionSpec.Keys)
            {
                const FFrameNumber KeyTick = DisplayToTick(MovieScene, Key.DisplayFrame);
                // The proxy is rebuilt on demand and nulled on modification
                // (MovieScene3DTransformSection.cpp:449, :541), so it is fetched
                // per key rather than cached across the loop.
                FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
                if (Key.bBoolChannel)
                {
                    const TArrayView<FMovieSceneBoolChannel* const> Channels =
                        Proxy.GetChannels<FMovieSceneBoolChannel>();
                    if (!Channels.IsValidIndex(Key.ChannelIndex) || Channels[Key.ChannelIndex] == nullptr)
                    {
                        return FailWithRollback(FString::Printf(
                            TEXT("tracks[%d]: the %s section has no boolean channel %d ('%s')."),
                            TrackSpec.Index, *TrackSpec.Type, Key.ChannelIndex, *Key.ChannelName));
                    }
                    FMovieSceneBoolChannel& Channel = *Channels[Key.ChannelIndex];
                    TMovieSceneChannelData<bool> Data = Channel.GetData();
                    const int32 Found = Data.FindKey(KeyTick);
                    if (Found != INDEX_NONE && Data.GetValues()[Found] == Key.bBoolValue)
                    {
                        UnchangedOperations.Add(FString::Printf(
                            TEXT("%s key at frame %d already %s"),
                            *Key.ChannelName, Key.DisplayFrame, Key.bBoolValue ? TEXT("true") : TEXT("false")));
                        continue;
                    }
                    if (Found != INDEX_NONE) { Data.RemoveKey(Found); }
                    Data.AddKey(KeyTick, Key.bBoolValue);
                    AppliedOperations.Add(FString::Printf(
                        TEXT("key %s = %s at frame %d"),
                        *Key.ChannelName, Key.bBoolValue ? TEXT("true") : TEXT("false"), Key.DisplayFrame));
                    AppliedCount++;
                    continue;
                }

                const TArrayView<FMovieSceneFloatChannel* const> Channels =
                    Proxy.GetChannels<FMovieSceneFloatChannel>();
                if (!Channels.IsValidIndex(Key.ChannelIndex) || Channels[Key.ChannelIndex] == nullptr)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("tracks[%d]: the %s section has no float channel %d ('%s')."),
                        TrackSpec.Index, *TrackSpec.Type, Key.ChannelIndex, *Key.ChannelName));
                }
                FMovieSceneFloatChannel& Channel = *Channels[Key.ChannelIndex];
                TMovieSceneChannelData<FMovieSceneFloatValue> Data = Channel.GetData();
                const int32 Found = Data.FindKey(KeyTick);
                if (Found != INDEX_NONE)
                {
                    const FMovieSceneFloatValue& Current = Data.GetValues()[Found];
                    if (FMath::IsNearlyEqual(Current.Value, Key.FloatValue, KINDA_SMALL_NUMBER)
                        && Current.InterpMode.GetValue() == Key.Interp)
                    {
                        UnchangedOperations.Add(FString::Printf(
                            TEXT("%s key at frame %d already %g"),
                            *Key.ChannelName, Key.DisplayFrame, Key.FloatValue));
                        continue;
                    }
                    // Removed and re-added rather than assigned in place, so the
                    // tangents Add*Key computes match the interpolation asked
                    // for instead of keeping the ones the old value had.
                    Data.RemoveKey(Found);
                }
                AddFloatKey(Channel, KeyTick, Key.FloatValue, Key.Interp);
                AppliedOperations.Add(FString::Printf(
                    TEXT("key %s = %g at frame %d (%s)"),
                    *Key.ChannelName, Key.FloatValue, Key.DisplayFrame,
                    *InterpolationName(Key.Interp)));
                AppliedCount++;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Verify by an independent read, then save.
    // ---------------------------------------------------------------------

    TSharedPtr<FJsonObject> PostSnapshot = BuildSnapshot(Sequence, World);
    const FString PostHash = SnapshotHash(PostSnapshot);

    // The claim this command makes is that the spec is now satisfied. Checking
    // it means asking the same questions again against the asset as it now is,
    // which is a different act from remembering that each write returned.
    for (const FResolvedTrack& TrackSpec : Tracks)
    {
        const bool bMaster = TrackSpec.BindingId == TEXT("master");
        FGuid BindingGuid;
        if (!bMaster)
        {
            const int32* Index = BindingsById.Find(TrackSpec.BindingId);
            if (Index != nullptr) { BindingGuid = Bindings[*Index].Guid; }
        }
        UMovieSceneTrack* Track = FindMatchingTrack(
            MovieScene, BindingGuid, bMaster, TrackClassForType(TrackSpec.Type), TrackSpec.Property);
        if (Track == nullptr)
        {
            return FailWithRollback(FString::Printf(
                TEXT("verification: the %s track on '%s' is not in the sequence after the build "
                     "reported writing it."), *TrackSpec.Type, *TrackSpec.BindingId));
        }
        if (TrackSpec.Type == TEXT("Camera")) { continue; }
        for (const FResolvedSection& SectionSpec : TrackSpec.Sections)
        {
            UMovieSceneSection* Section = FindSectionByRange(
                Track,
                DisplayToTick(MovieScene, SectionSpec.StartFrame),
                DisplayToTick(MovieScene, SectionSpec.EndFrame));
            if (Section == nullptr)
            {
                return FailWithRollback(FString::Printf(
                    TEXT("verification: the %s section on '%s' at frames %d..%d is not in the "
                         "sequence after the build reported writing it."),
                    *TrackSpec.Type, *TrackSpec.BindingId, SectionSpec.StartFrame, SectionSpec.EndFrame));
            }
            for (const FResolvedKey& Key : SectionSpec.Keys)
            {
                const FFrameNumber KeyTick = DisplayToTick(MovieScene, Key.DisplayFrame);
                FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
                bool bFound = false;
                if (Key.bBoolChannel)
                {
                    const TArrayView<FMovieSceneBoolChannel* const> Channels =
                        Proxy.GetChannels<FMovieSceneBoolChannel>();
                    if (Channels.IsValidIndex(Key.ChannelIndex) && Channels[Key.ChannelIndex] != nullptr)
                    {
                        TMovieSceneChannelData<bool> Data = Channels[Key.ChannelIndex]->GetData();
                        const int32 At = Data.FindKey(KeyTick);
                        bFound = At != INDEX_NONE && Data.GetValues()[At] == Key.bBoolValue;
                    }
                }
                else
                {
                    const TArrayView<FMovieSceneFloatChannel* const> Channels =
                        Proxy.GetChannels<FMovieSceneFloatChannel>();
                    if (Channels.IsValidIndex(Key.ChannelIndex) && Channels[Key.ChannelIndex] != nullptr)
                    {
                        TMovieSceneChannelData<FMovieSceneFloatValue> Data =
                            Channels[Key.ChannelIndex]->GetData();
                        const int32 At = Data.FindKey(KeyTick);
                        bFound = At != INDEX_NONE
                            && FMath::IsNearlyEqual(Data.GetValues()[At].Value, Key.FloatValue, KINDA_SMALL_NUMBER);
                    }
                }
                if (!bFound)
                {
                    return FailWithRollback(FString::Printf(
                        TEXT("verification: the key on %s at frame %d is not in the sequence after "
                             "the build reported writing it."), *Key.ChannelName, Key.DisplayFrame));
                }
            }
        }
    }

    Rollback.Commit();

    bool bSaved = false;
    if (bSave && AppliedCount > 0)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(PackagePath, SaveError);
        if (!bSaved)
        {
            // The asset is correct and the transaction stands; only the write to
            // disk failed. Reported as a warning rather than a rollback, because
            // discarding a correct sequence over a file-lock is the worse trade.
            Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("The sequence was built but could not be saved: %s. It is dirty in the editor; "
                     "puerts_save can write it."), *SaveError)));
        }
    }

    const UPackage* Package = Sequence->GetOutermost();
    Result->SetStringField(TEXT("object_path"), Sequence->GetPathName());
    Result->SetStringField(TEXT("post_structure_hash"), PostHash);
    Result->SetBoolField(TEXT("converged"), AppliedCount == 0);
    Result->SetNumberField(TEXT("applied_operation_count"), AppliedCount);
    Result->SetArrayField(TEXT("applied_operations"), StringsToJsonValues(AppliedOperations));
    Result->SetArrayField(TEXT("unchanged_operations"), StringsToJsonValues(UnchangedOperations));
    Result->SetNumberField(TEXT("binding_count"), PostSnapshot->GetNumberField(TEXT("binding_count")));
    Result->SetNumberField(TEXT("track_count"), PostSnapshot->GetNumberField(TEXT("track_count")));
    Result->SetNumberField(TEXT("section_count"), PostSnapshot->GetNumberField(TEXT("section_count")));
    Result->SetNumberField(TEXT("key_count"), PostSnapshot->GetNumberField(TEXT("key_count")));
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("package_dirty"), Package != nullptr && Package->IsDirty());
    Result->SetBoolField(TEXT("rollback_attempted"), false);
    Result->SetBoolField(TEXT("rollback_succeeded"), false);
    Result->SetObjectField(TEXT("cleanup"), Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));

    // The spec's ids are not stored in the asset - the asset's identity is the
    // FGuid - so the mapping back is part of the response, or a caller has no
    // way to find in an inspect result the binding it just asked for.
    TSharedPtr<FJsonObject> BindingGuids = MakeShared<FJsonObject>();
    for (const FResolvedBinding& Binding : Bindings)
    {
        BindingGuids->SetStringField(Binding.Id, Binding.Guid.ToString());
    }
    Result->SetObjectField(TEXT("binding_guids"), BindingGuids);
    return Finish(true);
}
