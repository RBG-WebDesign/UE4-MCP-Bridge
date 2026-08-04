// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Desired-state Event trigger authoring for an existing UE4.27 Level Sequence.
// The SequenceDirector Blueprint is a non-standalone subobject of the sequence,
// so the sequence package is the atomic save and restore boundary for both.

#include "MCPPuerTSBridgeService.h"

#include "MCPBridgeBlueprintMembers.h"
#include "MCPBridgeContentSnapshot.h"

#include "Channels/MovieSceneChannelData.h"
#include "Channels/MovieSceneEvent.h"
#include "Editor.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Json.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "LevelSequence.h"
#include "LevelSequenceDirector.h"
#include "Misc/FrameRate.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneEventUtils.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Sections/MovieSceneEventSectionBase.h"
#include "Sections/MovieSceneEventTriggerSection.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "UObject/Package.h"

namespace
{
    FString SerializeEventJson(const TSharedPtr<FJsonObject>& Object)
    {
        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Out;
    }

    TArray<TSharedPtr<FJsonValue>> EventStrings(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FString& Value : Values)
        {
            Out.Add(MakeShared<FJsonValueString>(Value));
        }
        return Out;
    }

    TArray<TSharedPtr<FJsonValue>> EventFrames(const TArray<int32>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const int32 Value : Values)
        {
            Out.Add(MakeShared<FJsonValueNumber>(Value));
        }
        return Out;
    }

    bool SplitSequenceEventPath(
        const FString& Input,
        FString& OutPackagePath,
        FString& OutObjectPath,
        FString& OutError)
    {
        FString Path = Input.TrimStartAndEnd();
        if (!Path.StartsWith(TEXT("/Game/MCPGenerated/")))
        {
            OutError = TEXT("Sequence event authoring is limited to /Game/MCPGenerated/.");
            return false;
        }
        const int32 Dot = Path.Find(TEXT("."), ESearchCase::CaseSensitive);
        OutPackagePath = Dot == INDEX_NONE ? Path : Path.Left(Dot);
        const FString AssetName = FPackageName::GetLongPackageAssetName(OutPackagePath);
        if (AssetName.IsEmpty() || !FPackageName::IsValidLongPackageName(OutPackagePath))
        {
            OutError = FString::Printf(TEXT("asset_path '%s' is not a valid asset package."), *Input);
            return false;
        }
        OutObjectPath = OutPackagePath + TEXT(".") + AssetName;
        return true;
    }

    FFrameNumber EventDisplayToTick(const UMovieScene* MovieScene, int32 DisplayFrame)
    {
        return FFrameRate::TransformTime(
            FFrameTime(FFrameNumber(DisplayFrame)),
            MovieScene->GetDisplayRate(),
            MovieScene->GetTickResolution()).FloorToFrame();
    }

    int32 EventTickToDisplay(const UMovieScene* MovieScene, FFrameNumber Tick)
    {
        return FFrameRate::TransformTime(
            FFrameTime(Tick),
            MovieScene->GetTickResolution(),
            MovieScene->GetDisplayRate()).RoundToFrame().Value;
    }

    bool IsEndpointIdentifier(const FString& Name)
    {
        if (Name.IsEmpty() || Name.Len() > 64)
        {
            return false;
        }
        const TCHAR First = Name[0];
        if (!(FChar::IsAlpha(First) || First == TEXT('_')))
        {
            return false;
        }
        for (const TCHAR Character : Name)
        {
            if (!(FChar::IsAlnum(Character) || Character == TEXT('_')))
            {
                return false;
            }
        }
        return true;
    }

    ULevelSequence* LoadEventSequence(const FString& ObjectPath, FString& OutError)
    {
        UObject* Object = StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *ObjectPath);
        ULevelSequence* Sequence = Cast<ULevelSequence>(Object);
        if (Sequence == nullptr)
        {
            OutError = FString::Printf(
                TEXT("'%s' does not exist or is not a Level Sequence. Create it with "
                     "puerts_sequence_build before adding events."), *ObjectPath);
        }
        return Sequence;
    }

    bool ResolveEventBinding(
        UMovieScene* MovieScene,
        const FString& BindingId,
        bool& bOutMaster,
        FGuid& OutGuid,
        UClass*& OutBoundClass,
        FString& OutError)
    {
        bOutMaster = BindingId == TEXT("master");
        OutGuid.Invalidate();
        OutBoundClass = nullptr;
        if (bOutMaster)
        {
            return true;
        }
        if (!FGuid::Parse(BindingId, OutGuid))
        {
            OutError = FString::Printf(
                TEXT("binding_id '%s' is neither 'master' nor an observed binding FGuid from "
                     "puerts_sequence_inspect."), *BindingId);
            return false;
        }
        if (FMovieScenePossessable* Possessable = MovieScene->FindPossessable(OutGuid))
        {
            OutBoundClass = const_cast<UClass*>(Possessable->GetPossessedObjectClass());
            return true;
        }
        if (FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(OutGuid))
        {
            OutBoundClass = Spawnable->GetObjectTemplate() != nullptr
                ? Spawnable->GetObjectTemplate()->GetClass()
                : nullptr;
            return true;
        }
        OutError = FString::Printf(
            TEXT("binding_id '%s' is not a possessable or spawnable in this sequence."), *BindingId);
        return false;
    }

    void CollectEventTracks(
        UMovieScene* MovieScene,
        bool bMaster,
        const FGuid& BindingGuid,
        TArray<UMovieSceneEventTrack*>& OutTracks)
    {
        if (bMaster)
        {
            for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
            {
                if (UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(Track))
                {
                    OutTracks.Add(EventTrack);
                }
            }
            return;
        }
        for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
        {
            if (Binding.GetObjectGuid() != BindingGuid) { continue; }
            for (UMovieSceneTrack* Track : Binding.GetTracks())
            {
                if (UMovieSceneEventTrack* EventTrack = Cast<UMovieSceneEventTrack>(Track))
                {
                    OutTracks.Add(EventTrack);
                }
            }
            return;
        }
    }

    UK2Node_CustomEvent* FindNamedEndpoint(UBlueprint* Director, const FString& EndpointName)
    {
        if (Director == nullptr) { return nullptr; }
        for (UEdGraph* Graph : Director->UbergraphPages)
        {
            if (Graph == nullptr) { continue; }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node);
                if (Event != nullptr && Event->CustomFunctionName.ToString() == EndpointName)
                {
                    return Event;
                }
            }
        }
        return nullptr;
    }

    FString EndpointNameForEvent(
        FMovieSceneEvent& Event,
        UMovieSceneEventSectionBase* Section,
        UBlueprint* Director)
    {
        if (Director == nullptr) { return FString(); }
        UK2Node* Node = FMovieSceneEventUtils::FindEndpoint(&Event, Section, Director);
        if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(Node))
        {
            return Custom->CustomFunctionName.ToString();
        }
        return FString();
    }

    TSharedPtr<FJsonObject> BuildEventSnapshot(ULevelSequence* Sequence)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        UMovieScene* MovieScene = Sequence != nullptr ? Sequence->GetMovieScene() : nullptr;
        UBlueprint* Director = Sequence != nullptr ? Sequence->GetDirectorBlueprint() : nullptr;
        Out->SetStringField(TEXT("director_blueprint"),
            Director != nullptr ? Director->GetPathName() : FString());

        TArray<FString> Endpoints;
        if (Director != nullptr)
        {
            for (UEdGraph* Graph : Director->UbergraphPages)
            {
                if (Graph == nullptr) { continue; }
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    if (const UK2Node_CustomEvent* Event = Cast<UK2Node_CustomEvent>(Node))
                    {
                        Endpoints.Add(Event->CustomFunctionName.ToString()
                            + (Event->bCallInEditor ? TEXT("|editor") : TEXT("|runtime")));
                    }
                }
            }
        }
        Endpoints.Sort();
        Out->SetArrayField(TEXT("endpoints"), EventStrings(Endpoints));

        TArray<TSharedPtr<FJsonObject>> Tracks;
        if (MovieScene != nullptr)
        {
            auto AddTrack = [&](UMovieSceneTrack* RawTrack, const FString& Binding)
            {
                UMovieSceneEventTrack* Track = Cast<UMovieSceneEventTrack>(RawTrack);
                if (Track == nullptr) { return; }
                TSharedPtr<FJsonObject> TrackJson = MakeShared<FJsonObject>();
                TrackJson->SetStringField(TEXT("binding_id"), Binding);
                TArray<TSharedPtr<FJsonValue>> Sections;
                for (UMovieSceneSection* RawSection : Track->GetAllSections())
                {
                    TSharedPtr<FJsonObject> SectionJson = MakeShared<FJsonObject>();
                    SectionJson->SetStringField(TEXT("class"),
                        RawSection != nullptr ? RawSection->GetClass()->GetPathName() : FString());
                    TArray<TSharedPtr<FJsonValue>> Keys;
                    UMovieSceneEventTriggerSection* Section =
                        Cast<UMovieSceneEventTriggerSection>(RawSection);
                    if (Section != nullptr)
                    {
                        TMovieSceneChannelData<FMovieSceneEvent> Data = Section->EventChannel.GetData();
                        const TArrayView<const FFrameNumber> Times = Data.GetTimes();
                        TArrayView<FMovieSceneEvent> Values = Data.GetValues();
                        for (int32 Index = 0; Index < Times.Num(); ++Index)
                        {
                            TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
                            Key->SetNumberField(TEXT("frame"), EventTickToDisplay(MovieScene, Times[Index]));
                            Key->SetNumberField(TEXT("tick"), Times[Index].Value);
                            Key->SetStringField(TEXT("endpoint"),
                                EndpointNameForEvent(Values[Index], Section, Director));
                            Keys.Add(MakeShared<FJsonValueObject>(Key));
                        }
                    }
                    SectionJson->SetArrayField(TEXT("keys"), Keys);
                    Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
                }
                TrackJson->SetArrayField(TEXT("sections"), Sections);
                Tracks.Add(TrackJson);
            };
            for (UMovieSceneTrack* Track : MovieScene->GetMasterTracks())
            {
                AddTrack(Track, TEXT("master"));
            }
            for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
            {
                for (UMovieSceneTrack* Track : Binding.GetTracks())
                {
                    AddTrack(Track, Binding.GetObjectGuid().ToString());
                }
            }
        }
        Tracks.Sort([](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
        {
            return A->GetStringField(TEXT("binding_id")) < B->GetStringField(TEXT("binding_id"));
        });
        TArray<TSharedPtr<FJsonValue>> TrackValues;
        for (const TSharedPtr<FJsonObject>& Track : Tracks)
        {
            TrackValues.Add(MakeShared<FJsonValueObject>(Track));
        }
        Out->SetArrayField(TEXT("tracks"), TrackValues);
        return Out;
    }

    FString EventSnapshotHash(ULevelSequence* Sequence)
    {
        return Sequence != nullptr
            ? MCPBridgeBlueprintMembers::StructureHash(BuildEventSnapshot(Sequence))
            : FString();
    }
}

bool UMCPPuerTSBridgeService::BuildSequenceEventTrackJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    const double StartSeconds = FPlatformTime::Seconds();
    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Sequence event build spec must be a JSON object.");
        return false;
    }

    const TSet<FString> AllowedTopLevel = {
        TEXT("asset_path"), TEXT("binding_id"), TEXT("endpoint_name"), TEXT("events"),
        TEXT("call_in_editor"), TEXT("plan_only"), TEXT("save")
    };
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Spec->Values)
    {
        if (!AllowedTopLevel.Contains(Field.Key))
        {
            OutError = FString::Printf(TEXT("Unsupported sequence event field '%s'."), *Field.Key);
            return false;
        }
    }

    if (!Spec->HasTypedField<EJson::String>(TEXT("asset_path"))
        || !Spec->HasTypedField<EJson::String>(TEXT("endpoint_name"))
        || (Spec->HasField(TEXT("binding_id"))
            && !Spec->HasTypedField<EJson::String>(TEXT("binding_id"))))
    {
        OutError = TEXT("asset_path and endpoint_name must be strings; binding_id must be a string when supplied.");
        return false;
    }
    for (const FString& BooleanField : { TEXT("call_in_editor"), TEXT("plan_only"), TEXT("save") })
    {
        if (Spec->HasField(BooleanField)
            && !Spec->HasTypedField<EJson::Boolean>(BooleanField))
        {
            OutError = FString::Printf(TEXT("%s must be a boolean."), *BooleanField);
            return false;
        }
    }

    FString AssetPath;
    FString EndpointName;
    FString BindingId = TEXT("master");
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    Spec->TryGetStringField(TEXT("endpoint_name"), EndpointName);
    Spec->TryGetStringField(TEXT("binding_id"), BindingId);
    AssetPath = AssetPath.TrimStartAndEnd();
    EndpointName = EndpointName.TrimStartAndEnd();
    BindingId = BindingId.TrimStartAndEnd();
    if (!IsEndpointIdentifier(EndpointName))
    {
        OutError = TEXT("endpoint_name must be a Blueprint identifier of 1..64 letters, digits or "
            "underscores, beginning with a letter or underscore.");
        return false;
    }

    FString PackagePath;
    FString ObjectPath;
    if (!SplitSequenceEventPath(AssetPath, PackagePath, ObjectPath, OutError))
    {
        return false;
    }
    ULevelSequence* Sequence = LoadEventSequence(ObjectPath, OutError);
    if (Sequence == nullptr) { return false; }
    UMovieScene* MovieScene = Sequence->GetMovieScene();
    if (MovieScene == nullptr)
    {
        OutError = TEXT("The Level Sequence has no MovieScene.");
        return false;
    }

    bool bMaster = true;
    FGuid BindingGuid;
    UClass* BoundClass = nullptr;
    if (!ResolveEventBinding(
            MovieScene, BindingId, bMaster, BindingGuid, BoundClass, OutError))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* EventArray = nullptr;
    if (!Spec->TryGetArrayField(TEXT("events"), EventArray) || EventArray->Num() == 0)
    {
        OutError = TEXT("events must be a non-empty array of {frame: integer} objects.");
        return false;
    }
    if (EventArray->Num() > 1024)
    {
        OutError = TEXT("events accepts at most 1024 keys in one request.");
        return false;
    }
    TArray<int32> RequestedFrames;
    TSet<int32> SeenFrames;
    for (int32 Index = 0; Index < EventArray->Num(); ++Index)
    {
        const TSharedPtr<FJsonObject>* Entry = nullptr;
        if (!(*EventArray)[Index]->TryGetObject(Entry))
        {
            OutError = FString::Printf(TEXT("events[%d] is not an object."), Index);
            return false;
        }
        if ((*Entry)->Values.Num() != 1 || !(*Entry)->HasField(TEXT("frame")))
        {
            OutError = FString::Printf(
                TEXT("events[%d] must contain exactly {frame}. Payloads and per-key endpoint "
                     "shapes are not supported by this command."), Index);
            return false;
        }
        double FrameNumber = 0.0;
        if (!(*Entry)->TryGetNumberField(TEXT("frame"), FrameNumber)
            || !FMath::IsFinite(FrameNumber)
            || FrameNumber != FMath::FloorToDouble(FrameNumber)
            || FrameNumber < static_cast<double>(MIN_int32)
            || FrameNumber > static_cast<double>(MAX_int32))
        {
            OutError = FString::Printf(TEXT("events[%d].frame must be a 32-bit integer."), Index);
            return false;
        }
        const int32 Frame = static_cast<int32>(FrameNumber);
        if (SeenFrames.Contains(Frame))
        {
            OutError = FString::Printf(
                TEXT("events[%d]: frame %d is declared twice. One frame has one desired event."),
                Index, Frame);
            return false;
        }
        const FFrameNumber Tick = EventDisplayToTick(MovieScene, Frame);
        if (!MovieScene->GetPlaybackRange().Contains(Tick))
        {
            OutError = FString::Printf(
                TEXT("events[%d]: frame %d is outside the sequence playback range."), Index, Frame);
            return false;
        }
        SeenFrames.Add(Frame);
        RequestedFrames.Add(Frame);
    }
    RequestedFrames.Sort();

    const bool bCallInEditor = Spec->HasTypedField<EJson::Boolean>(TEXT("call_in_editor"))
        && Spec->GetBoolField(TEXT("call_in_editor"));
    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    TArray<UMovieSceneEventTrack*> ExistingTracks;
    CollectEventTracks(MovieScene, bMaster, BindingGuid, ExistingTracks);
    if (ExistingTracks.Num() > 1)
    {
        OutError = FString::Printf(
            TEXT("binding_id '%s' has %d Event tracks. This command cannot choose which one to "
                 "patch, so it changed nothing."), *BindingId, ExistingTracks.Num());
        return false;
    }
    UMovieSceneEventTrack* Track = ExistingTracks.Num() == 1 ? ExistingTracks[0] : nullptr;
    UMovieSceneEventTriggerSection* Section = nullptr;
    if (Track != nullptr)
    {
        for (UMovieSceneSection* ExistingSection : Track->GetAllSections())
        {
            UMovieSceneEventTriggerSection* Trigger =
                Cast<UMovieSceneEventTriggerSection>(ExistingSection);
            if (Trigger == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("binding_id '%s' has an Event track containing unsupported section class "
                         "'%s'. Only one trigger section is patchable."),
                    *BindingId,
                    ExistingSection != nullptr ? *ExistingSection->GetClass()->GetPathName() : TEXT("null"));
                return false;
            }
            if (Section != nullptr)
            {
                OutError = FString::Printf(
                    TEXT("binding_id '%s' has more than one Event trigger section. This command "
                         "cannot choose one, so it changed nothing."), *BindingId);
                return false;
            }
            Section = Trigger;
        }
    }

    UBlueprint* Director = Sequence->GetDirectorBlueprint();
    UK2Node_CustomEvent* Endpoint = FindNamedEndpoint(Director, EndpointName);
    FBridgeContentSnapshot Snapshot;
    FString SnapshotRefusal;
    if (!Snapshot.Capture(PackagePath, SnapshotRefusal))
    {
        OutError = SnapshotRefusal;
        return false;
    }
    const FString PreHash = EventSnapshotHash(Sequence);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetStringField(TEXT("binding_id"), BindingId);
    Result->SetStringField(TEXT("endpoint_name"), EndpointName);
    Result->SetNumberField(TEXT("requested_key_count"), RequestedFrames.Num());
    Result->SetArrayField(TEXT("event_frames"), EventFrames(RequestedFrames));
    Result->SetStringField(TEXT("pre_structure_hash"), PreHash);

    auto Finish = [&](bool bSuccess) -> bool
    {
        Result->SetNumberField(
            TEXT("elapsed_ms"), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        OutResultJson = SerializeEventJson(Result);
        return bSuccess;
    };

    if (bPlanOnly)
    {
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create_track"), Track == nullptr);
        Result->SetBoolField(TEXT("would_create_section"), Section == nullptr);
        Result->SetBoolField(TEXT("would_create_director"), Director == nullptr);
        Result->SetBoolField(TEXT("would_create_endpoint"), Endpoint == nullptr);
        Result->SetStringField(TEXT("director_blueprint"),
            Director != nullptr ? Director->GetPathName() : FString());
        Result->SetBoolField(TEXT("verified"), false);
        Result->SetBoolField(TEXT("saved"), false);
        return Finish(true);
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("sequence_event_track_build requires an active transaction.");
        return Finish(false);
    }

    auto Fail = [&](const FString& Error) -> bool
    {
        ActiveTransaction->Cancel();
        FString RestoreError;
        const bool bRestoreMechanism = Snapshot.Restore(RestoreError);
        Sequence = LoadEventSequence(ObjectPath, RestoreError);
        const FString RecoveredHash = EventSnapshotHash(Sequence);
        const bool bRollbackSucceeded = bRestoreMechanism
            && Sequence != nullptr && RecoveredHash == PreHash;
        Result->SetBoolField(TEXT("rollback_attempted"), true);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
        Result->SetStringField(TEXT("rollback_verified_by"), TEXT("native event snapshot"));
        Result->SetStringField(TEXT("post_structure_hash"), RecoveredHash);
        Result->SetObjectField(TEXT("rollback"), Snapshot.BuildEvidence());
        Result->SetBoolField(TEXT("verified"), false);
        Result->SetBoolField(TEXT("saved"), false);
        OutError = bRollbackSucceeded
            ? Error
            : FString::Printf(
                TEXT("%s Rollback did not restore both the sequence and its contained director "
                     "Blueprint: recovered hash '%s', expected '%s'. %s"),
                *Error, *RecoveredHash, *PreHash, *RestoreError);
        return Finish(false);
    };

    const bool bSequenceModified = Sequence->Modify();
    const bool bMovieSceneModified = MovieScene->Modify();
    Result->SetBoolField(TEXT("transaction_covers_sequence"), bSequenceModified && bMovieSceneModified);
    if (!bSequenceModified || !bMovieSceneModified)
    {
        return Fail(TEXT("Sequence or MovieScene Modify() returned false; no event mutation was retained."));
    }

    bool bCreatedDirector = false;
    if (Director == nullptr)
    {
        Director = FKismetEditorUtilities::CreateBlueprint(
            ULevelSequenceDirector::StaticClass(),
            Sequence,
            FName(TEXT("SequenceDirector")),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass());
        if (Director == nullptr)
        {
            return Fail(TEXT("UE4.27 could not create the contained SequenceDirector Blueprint."));
        }
        Director->ClearFlags(RF_Standalone);
        Sequence->SetDirectorBlueprint(Director);
        bCreatedDirector = true;
    }
    const bool bDirectorModified = Director->Modify();
    Result->SetBoolField(TEXT("transaction_covers_director"), bDirectorModified);
    if (!bDirectorModified)
    {
        return Fail(TEXT("SequenceDirector Blueprint Modify() returned false; no endpoint mutation was retained."));
    }

    bool bCreatedTrack = false;
    if (Track == nullptr)
    {
        Track = bMaster
            ? MovieScene->AddMasterTrack<UMovieSceneEventTrack>()
            : MovieScene->AddTrack<UMovieSceneEventTrack>(BindingGuid);
        if (Track == nullptr)
        {
            return Fail(TEXT("UE4.27 could not create the Event track."));
        }
        bCreatedTrack = true;
    }
    if (!Track->Modify())
    {
        return Fail(TEXT("Event track Modify() returned false."));
    }

    bool bCreatedSection = false;
    if (Section == nullptr)
    {
        Section = Cast<UMovieSceneEventTriggerSection>(Track->CreateNewSection());
        if (Section == nullptr)
        {
            return Fail(TEXT("UE4.27 did not create an Event trigger section."));
        }
        Section->SetRange(MovieScene->GetPlaybackRange());
        Track->AddSection(*Section);
        bCreatedSection = true;
    }
    if (!Section->Modify())
    {
        return Fail(TEXT("Event trigger section Modify() returned false."));
    }

    bool bCreatedEndpoint = false;
    if (Endpoint == nullptr)
    {
        FMovieSceneEventEndpointParameters Parameters =
            FMovieSceneEventEndpointParameters::Generate(MovieScene, BindingGuid);
        Parameters.SanitizedEventName = EndpointName;
        Endpoint = FMovieSceneEventUtils::CreateUserFacingEvent(Director, Parameters);
        if (Endpoint == nullptr || Endpoint->CustomFunctionName.ToString() != EndpointName)
        {
            return Fail(FString::Printf(
                TEXT("Could not create director endpoint '%s' without a name collision."),
                *EndpointName));
        }
        bCreatedEndpoint = true;
    }
    if (!Endpoint->Modify())
    {
        return Fail(TEXT("Director endpoint Modify() returned false."));
    }
    const bool bEndpointCallChanged = Endpoint->bCallInEditor != bCallInEditor;
    Endpoint->bCallInEditor = bCallInEditor;
    FMovieSceneEventUtils::BindEventSectionToBlueprint(Section, Director);

    int32 AppliedKeys = 0;
    int32 UnchangedKeys = 0;
    TMovieSceneChannelData<FMovieSceneEvent> Data = Section->EventChannel.GetData();
    for (const int32 DisplayFrame : RequestedFrames)
    {
        const FFrameNumber Tick = EventDisplayToTick(MovieScene, DisplayFrame);
        const int32 ExistingIndex = Data.FindKey(Tick);
        if (ExistingIndex != INDEX_NONE)
        {
            TArrayView<FMovieSceneEvent> Values = Data.GetValues();
            const FString ExistingEndpoint =
                EndpointNameForEvent(Values[ExistingIndex], Section, Director);
            if (ExistingEndpoint == EndpointName)
            {
                ++UnchangedKeys;
                continue;
            }
        }
        FMovieSceneEvent Event;
        UEdGraphPin* BoundObjectPin =
            FMovieSceneEventUtils::FindBoundObjectPin(Endpoint, BoundClass);
        FMovieSceneEventUtils::SetEndpoint(&Event, Section, Endpoint, BoundObjectPin);
        Data.UpdateOrAddKey(Tick, Event);
        ++AppliedKeys;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Director);
    FKismetEditorUtilities::CompileBlueprint(Director);
    if (Director->Status != BS_UpToDate || Director->GeneratedClass == nullptr)
    {
        return Fail(FString::Printf(
            TEXT("SequenceDirector Blueprint compile status is %d, not UpToDate."),
            static_cast<int32>(Director->Status)));
    }

    // Independent native read-back. This re-queries the channel and resolves
    // every key's endpoint from the Director Blueprint after compilation.
    Data = Section->EventChannel.GetData();
    for (const int32 DisplayFrame : RequestedFrames)
    {
        const int32 Index = Data.FindKey(EventDisplayToTick(MovieScene, DisplayFrame));
        if (Index == INDEX_NONE)
        {
            return Fail(FString::Printf(
                TEXT("Verification could not find the Event key at frame %d."), DisplayFrame));
        }
        TArrayView<FMovieSceneEvent> Values = Data.GetValues();
        const FString ReadEndpoint = EndpointNameForEvent(Values[Index], Section, Director);
        if (ReadEndpoint != EndpointName)
        {
            return Fail(FString::Printf(
                TEXT("Verification read endpoint '%s' at frame %d, expected '%s'."),
                *ReadEndpoint, DisplayFrame, *EndpointName));
        }
        if (Values[Index].Ptrs.Function == nullptr)
        {
            return Fail(FString::Printf(
                TEXT("Verification found no compiled director function for frame %d."), DisplayFrame));
        }
    }

    const FString PostHash = EventSnapshotHash(Sequence);
    bool bSaved = false;
    if (bSave && (bCreatedDirector || bCreatedTrack || bCreatedSection
        || bCreatedEndpoint || bEndpointCallChanged || AppliedKeys > 0))
    {
        FString SaveError;
        bSaved = SaveProjectAsset(PackagePath, SaveError);
        if (!bSaved)
        {
            return Fail(FString::Printf(
                TEXT("The verified sequence and contained director Blueprint could not be saved: %s"),
                *SaveError));
        }
    }

    Result->SetStringField(TEXT("director_blueprint"), Director->GetPathName());
    Result->SetBoolField(TEXT("plan_only"), false);
    Result->SetBoolField(TEXT("created_track"), bCreatedTrack);
    Result->SetBoolField(TEXT("created_section"), bCreatedSection);
    Result->SetBoolField(TEXT("created_endpoint"), bCreatedEndpoint);
    Result->SetNumberField(TEXT("applied_key_count"), AppliedKeys);
    Result->SetNumberField(TEXT("unchanged_key_count"), UnchangedKeys);
    Result->SetStringField(TEXT("pre_structure_hash"), PreHash);
    Result->SetStringField(TEXT("post_structure_hash"), PostHash);
    Result->SetStringField(TEXT("compile_status"), TEXT("UpToDate"));
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("rollback_attempted"), false);
    Result->SetBoolField(TEXT("rollback_succeeded"), false);
    Result->SetObjectField(TEXT("rollback"), Snapshot.BuildEvidence());
    return Finish(true);
}
