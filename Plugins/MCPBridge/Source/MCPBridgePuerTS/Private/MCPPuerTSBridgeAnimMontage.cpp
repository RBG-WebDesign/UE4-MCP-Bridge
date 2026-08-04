// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AssetRegistryModule.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "MCPBridgeContentSnapshot.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
    FString MontageJson(const TSharedPtr<FJsonObject>& Object)
    {
        FString Json;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
        return Json;
    }

    bool OnlyFields(const TSharedPtr<FJsonObject>& Object, const TSet<FString>& Allowed,
        const FString& Location, FString& OutError)
    {
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object->Values)
        {
            if (!Allowed.Contains(Field.Key))
            {
                OutError = FString::Printf(TEXT("Unsupported field '%s' at %s."), *Field.Key, *Location);
                return false;
            }
        }
        return true;
    }

    bool Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double Default,
        double& Out, const FString& Location, FString& OutError)
    {
        Out = Default;
        if (!Object->HasField(Field)) { return true; }
        if (!Object->TryGetNumberField(Field, Out) || !FMath::IsFinite(Out))
        {
            OutError = FString::Printf(TEXT("%s.%s must be a finite number."), *Location, Field);
            return false;
        }
        return true;
    }

    bool ApplyMontage(UAnimMontage* Montage, const TSharedPtr<FJsonObject>& Request,
        USkeleton* Skeleton, int32& OutSegments, FString& OutError)
    {
        const TArray<TSharedPtr<FJsonValue>>* NotifyValues = nullptr;
        if (Request->TryGetArrayField(TEXT("notifies"), NotifyValues)
            && NotifyValues != nullptr && NotifyValues->Num() > 0)
        {
            OutError = TEXT("notifies are refused: safe notify and notify-state authoring requires "
                "full start/end FAnimLinkableElement read-back, which this implementation does not claim.");
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* SlotValues = nullptr;
        if (!Request->TryGetArrayField(TEXT("slots"), SlotValues)
            || SlotValues == nullptr || SlotValues->Num() == 0)
        {
            OutError = TEXT("slots must be a non-empty array.");
            return false;
        }
        TArray<FSlotAnimationTrack> BuiltSlots;
        TSet<FString> SlotNames;
        OutSegments = 0;
        for (int32 SlotIndex = 0; SlotIndex < SlotValues->Num(); ++SlotIndex)
        {
            const TSharedPtr<FJsonObject> SlotObject = (*SlotValues)[SlotIndex]->AsObject();
            const FString Location = FString::Printf(TEXT("slots[%d]"), SlotIndex);
            if (!SlotObject.IsValid()
                || !OnlyFields(SlotObject, { TEXT("slot_name"), TEXT("segments") }, Location, OutError))
            {
                if (OutError.IsEmpty()) { OutError = Location + TEXT(" must be an object."); }
                return false;
            }
            FString SlotName;
            if (!SlotObject->TryGetStringField(TEXT("slot_name"), SlotName)
                || SlotName.IsEmpty() || SlotNames.Contains(SlotName))
            {
                OutError = Location + TEXT(".slot_name must be non-empty and unique.");
                return false;
            }
            SlotNames.Add(SlotName);
            const TArray<TSharedPtr<FJsonValue>>* SegmentValues = nullptr;
            if (!SlotObject->TryGetArrayField(TEXT("segments"), SegmentValues)
                || SegmentValues == nullptr || SegmentValues->Num() == 0)
            {
                OutError = Location + TEXT(".segments must be a non-empty array.");
                return false;
            }
            FSlotAnimationTrack Slot;
            Slot.SlotName = FName(*SlotName);
            float PreviousEnd = 0.0f;
            for (int32 SegmentIndex = 0; SegmentIndex < SegmentValues->Num(); ++SegmentIndex)
            {
                const TSharedPtr<FJsonObject> SegmentObject = (*SegmentValues)[SegmentIndex]->AsObject();
                const FString SegmentLocation = FString::Printf(TEXT("%s.segments[%d]"), *Location, SegmentIndex);
                const TSet<FString> Allowed = { TEXT("animation"), TEXT("start_time"),
                    TEXT("start_position"), TEXT("end_position"), TEXT("play_rate"), TEXT("loop_count") };
                if (!SegmentObject.IsValid() || !OnlyFields(SegmentObject, Allowed, SegmentLocation, OutError))
                {
                    if (OutError.IsEmpty()) { OutError = SegmentLocation + TEXT(" must be an object."); }
                    return false;
                }
                FString AnimationPath;
                if (!SegmentObject->TryGetStringField(TEXT("animation"), AnimationPath) || AnimationPath.IsEmpty())
                {
                    OutError = SegmentLocation + TEXT(".animation must be a non-empty string.");
                    return false;
                }
                UAnimSequenceBase* Animation = LoadObject<UAnimSequenceBase>(nullptr, *AnimationPath);
                if (Animation == nullptr || Animation->GetSkeleton() != Skeleton)
                {
                    OutError = FString::Printf(TEXT("Animation '%s' is missing or uses a different skeleton."), *AnimationPath);
                    return false;
                }
                double Start, AnimStart, AnimEnd, Rate, Loops;
                if (!Number(SegmentObject, TEXT("start_time"), PreviousEnd, Start, SegmentLocation, OutError)
                    || !Number(SegmentObject, TEXT("start_position"), 0.0, AnimStart, SegmentLocation, OutError)
                    || !Number(SegmentObject, TEXT("end_position"), Animation->SequenceLength, AnimEnd, SegmentLocation, OutError)
                    || !Number(SegmentObject, TEXT("play_rate"), 1.0, Rate, SegmentLocation, OutError)
                    || !Number(SegmentObject, TEXT("loop_count"), 1.0, Loops, SegmentLocation, OutError))
                {
                    return false;
                }
                if (Start < PreviousEnd || AnimStart < 0.0 || AnimEnd <= AnimStart
                    || AnimEnd > Animation->SequenceLength || FMath::IsNearlyZero(Rate)
                    || Loops < 1.0 || !FMath::IsNearlyEqual(Loops, FMath::RoundToDouble(Loops)))
                {
                    OutError = SegmentLocation + TEXT(" has invalid timing, play_rate or loop_count.");
                    return false;
                }
                FAnimSegment Segment;
                Segment.AnimReference = Animation;
                Segment.StartPos = Start;
                Segment.AnimStartTime = AnimStart;
                Segment.AnimEndTime = AnimEnd;
                Segment.AnimPlayRate = Rate;
                Segment.LoopingCount = static_cast<int32>(Loops);
                PreviousEnd = Segment.GetEndPos();
                Slot.AnimTrack.AnimSegments.Add(Segment);
                ++OutSegments;
            }
            BuiltSlots.Add(Slot);
        }

        const TArray<TSharedPtr<FJsonValue>>* SectionValues = nullptr;
        if (!Request->TryGetArrayField(TEXT("sections"), SectionValues)
            || SectionValues == nullptr || SectionValues->Num() == 0)
        {
            OutError = TEXT("sections must be a non-empty array.");
            return false;
        }
        struct FSectionInput { FString Name; FString Next; float Time; };
        TArray<FSectionInput> BuiltSections;
        TSet<FString> SectionNames;
        float PreviousTime = -1.0f;
        for (int32 Index = 0; Index < SectionValues->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> Object = (*SectionValues)[Index]->AsObject();
            const FString Location = FString::Printf(TEXT("sections[%d]"), Index);
            if (!Object.IsValid() || !OnlyFields(Object,
                    { TEXT("name"), TEXT("start_time"), TEXT("next_section") }, Location, OutError))
            {
                if (OutError.IsEmpty()) { OutError = Location + TEXT(" must be an object."); }
                return false;
            }
            FSectionInput Section;
            double Time;
            if (!Object->TryGetStringField(TEXT("name"), Section.Name) || Section.Name.IsEmpty()
                || !Number(Object, TEXT("start_time"), 0.0, Time, Location, OutError))
            {
                if (OutError.IsEmpty()) { OutError = Location + TEXT(".name must be non-empty."); }
                return false;
            }
            Object->TryGetStringField(TEXT("next_section"), Section.Next);
            if (SectionNames.Contains(Section.Name) || Time < 0.0 || Time < PreviousTime)
            {
                OutError = Location + TEXT(" has a duplicate name or is out of montage order.");
                return false;
            }
            SectionNames.Add(Section.Name);
            Section.Time = Time;
            PreviousTime = Time;
            BuiltSections.Add(Section);
        }
        for (const FSectionInput& Section : BuiltSections)
        {
            if (!Section.Next.IsEmpty() && Section.Next != TEXT("None") && !SectionNames.Contains(Section.Next))
            {
                OutError = FString::Printf(TEXT("Section '%s' points to missing next_section '%s'."),
                    *Section.Name, *Section.Next);
                return false;
            }
        }

        Montage->SetSkeleton(Skeleton);
        Montage->SlotAnimTracks = BuiltSlots;
        Montage->SequenceLength = Montage->CalculateSequenceLength();
        Montage->CompositeSections.Reset();
        for (const FSectionInput& Input : BuiltSections)
        {
            if (Input.Time > Montage->SequenceLength)
            {
                OutError = FString::Printf(TEXT("Section '%s' starts after the montage ends."), *Input.Name);
                return false;
            }
            FCompositeSection Section;
            Section.SectionName = FName(*Input.Name);
            Section.NextSectionName = Input.Next.IsEmpty() ? NAME_None : FName(*Input.Next);
            Section.LinkMontage(Montage, Input.Time, 0);
            Montage->CompositeSections.Add(Section);
        }
        Montage->Notifies.Reset();

        float BlendIn = 0.25f, BlendOut = 0.25f, BlendTrigger = -1.0f;
        bool bAutoBlendOut = true;
        const TSharedPtr<FJsonObject>* BlendPointer = nullptr;
        if (Request->TryGetObjectField(TEXT("blend"), BlendPointer) && BlendPointer != nullptr)
        {
            const TSharedPtr<FJsonObject> Blend = *BlendPointer;
            if (!OnlyFields(Blend, { TEXT("blend_in_time"), TEXT("blend_out_time"),
                    TEXT("blend_out_trigger_time"), TEXT("enable_auto_blend_out") }, TEXT("blend"), OutError))
            {
                return false;
            }
            double In, Out, Trigger;
            if (!Number(Blend, TEXT("blend_in_time"), BlendIn, In, TEXT("blend"), OutError)
                || !Number(Blend, TEXT("blend_out_time"), BlendOut, Out, TEXT("blend"), OutError)
                || !Number(Blend, TEXT("blend_out_trigger_time"), BlendTrigger, Trigger, TEXT("blend"), OutError)
                || In < 0.0 || Out < 0.0 || Trigger < -1.0)
            {
                if (OutError.IsEmpty()) { OutError = TEXT("blend timing is invalid."); }
                return false;
            }
            BlendIn = In; BlendOut = Out; BlendTrigger = Trigger;
            if (Blend->HasField(TEXT("enable_auto_blend_out")))
            {
                if (!Blend->HasTypedField<EJson::Boolean>(TEXT("enable_auto_blend_out")))
                {
                    OutError = TEXT("blend.enable_auto_blend_out must be boolean.");
                    return false;
                }
                bAutoBlendOut = Blend->GetBoolField(TEXT("enable_auto_blend_out"));
            }
        }
        FString SyncGroup;
        if (Request->HasField(TEXT("sync_group")) && !Request->TryGetStringField(TEXT("sync_group"), SyncGroup))
        {
            OutError = TEXT("sync_group must be a string.");
            return false;
        }
        Montage->BlendIn.SetBlendTime(BlendIn);
        Montage->BlendOut.SetBlendTime(BlendOut);
        Montage->BlendOutTriggerTime = BlendTrigger;
        Montage->bEnableAutoBlendOut = bAutoBlendOut;
        Montage->SyncGroup = SyncGroup.IsEmpty() ? NAME_None : FName(*SyncGroup);
        Montage->UpdateLinkableElements();
        return true;
    }

    bool InspectMontage(UMCPPuerTSBridgeService* Service, const FString& PackagePath,
        TSharedPtr<FJsonObject>& OutInspection, FString& OutError)
    {
        TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
        Request->SetStringField(TEXT("asset_path"), PackagePath);
        FString Json;
        if (!Service->InspectAnimMontageJson(MontageJson(Request), Json, OutError)) { return false; }
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, OutInspection) || !OutInspection.IsValid())
        {
            OutError = TEXT("anim_montage_inspect returned invalid JSON.");
            return false;
        }
        return true;
    }
}

bool UMCPPuerTSBridgeService::BuildAnimMontageJson(
    const FString& RequestJson, FString& OutResultJson, FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> RequestReader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(RequestReader, Request) || !Request.IsValid())
    {
        OutError = TEXT("anim_montage_build request must be a JSON object.");
        return false;
    }
    const TSet<FString> Allowed = { TEXT("asset_path"), TEXT("skeleton"), TEXT("slots"),
        TEXT("sections"), TEXT("notifies"), TEXT("blend"), TEXT("sync_group"), TEXT("plan_only"), TEXT("save") };
    if (!OnlyFields(Request, Allowed, TEXT("request"), OutError)) { return false; }
    FString RequestedPath, SkeletonPath;
    if (!Request->TryGetStringField(TEXT("asset_path"), RequestedPath) || RequestedPath.IsEmpty()
        || !Request->TryGetStringField(TEXT("skeleton"), SkeletonPath) || SkeletonPath.IsEmpty())
    {
        OutError = TEXT("asset_path and skeleton are required strings.");
        return false;
    }
    FString PackagePath = RequestedPath.Contains(TEXT("."))
        ? FPackageName::ObjectPathToPackageName(RequestedPath) : RequestedPath;
    FString AssetName = RequestedPath.Contains(TEXT("."))
        ? FPackageName::ObjectPathToObjectName(RequestedPath) : FPackageName::GetLongPackageAssetName(RequestedPath);
    FString ObjectPath = PackagePath + TEXT(".") + AssetName;
    if (!RequestedPath.StartsWith(TEXT("/Game/"))
        || !FPackageName::IsValidLongPackageName(PackagePath, true) || AssetName.IsEmpty())
    {
        OutError = TEXT("asset_path must be a valid /Game package or object path.");
        return false;
    }
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (Skeleton == nullptr)
    {
        OutError = FString::Printf(TEXT("No Skeleton was found at '%s'."), *SkeletonPath);
        return false;
    }
    if ((Request->HasField(TEXT("plan_only")) && !Request->HasTypedField<EJson::Boolean>(TEXT("plan_only")))
        || (Request->HasField(TEXT("save")) && !Request->HasTypedField<EJson::Boolean>(TEXT("save"))))
    {
        OutError = TEXT("plan_only and save must be boolean.");
        return false;
    }
    const bool bPlanOnly = Request->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Request->GetBoolField(TEXT("plan_only"));
    const bool bSave = !Request->HasField(TEXT("save")) || Request->GetBoolField(TEXT("save"));
    UAnimMontage* Scratch = NewObject<UAnimMontage>(GetTransientPackage());
    int32 SegmentCount = 0;
    if (!ApplyMontage(Scratch, Request, Skeleton, SegmentCount, OutError)) { return false; }

    FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingData = Registry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UAnimMontage* Montage = ExistingData.IsValid() ? Cast<UAnimMontage>(ExistingData.GetAsset()) : nullptr;
    if (ExistingData.IsValid() && Montage == nullptr)
    {
        OutError = FString::Printf(TEXT("The asset at '%s' is not an AnimMontage."), *ObjectPath);
        return false;
    }
    const bool bCreating = Montage == nullptr;
    FString BeforeHash;
    if (!bCreating)
    {
        TSharedPtr<FJsonObject> Before;
        if (!InspectMontage(this, PackagePath, Before, OutError)) { return false; }
        Before->TryGetStringField(TEXT("structure_hash_sha1"), BeforeHash);
    }
    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetStringField(TEXT("object_path"), ObjectPath);
        Result->SetBoolField(TEXT("built"), false);
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), bCreating);
        Result->SetNumberField(TEXT("expected_change_count"), 1);
        Result->SetBoolField(TEXT("saved"), false);
        OutResultJson = MontageJson(Result);
        return true;
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("anim_montage_build requires an active transaction.");
        return false;
    }

    FBridgeAssetRollback CreationRollback;
    CreationRollback.Snapshot(PackagePath);
    const TArray<FString> DirtyBefore = CreationRollback.DirtyPackages();
    const TArray<FString> SourceBefore = CreationRollback.SourceControlState();
    FBridgeContentSnapshot UpdateSnapshot;
    if (!bCreating)
    {
        FString Refusal;
        if (!UpdateSnapshot.Capture(PackagePath, Refusal)) { OutError = Refusal; return false; }
    }
    auto Fail = [&](const FString& Error) -> bool
    {
        ActiveTransaction->Cancel();
        bool bRestored = false;
        TSharedPtr<FJsonObject> Evidence;
        if (bCreating)
        {
            CreationRollback.Rollback();
            bRestored = !Registry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
            Evidence = CreationRollback.BuildEvidence(DirtyBefore, SourceBefore);
        }
        else
        {
            FString RestoreError;
            const bool bMechanism = UpdateSnapshot.Restore(RestoreError);
            TSharedPtr<FJsonObject> Restored;
            FString InspectError, RestoredHash;
            if (InspectMontage(this, PackagePath, Restored, InspectError))
            {
                Restored->TryGetStringField(TEXT("structure_hash_sha1"), RestoredHash);
            }
            bRestored = bMechanism && !BeforeHash.IsEmpty() && RestoredHash == BeforeHash;
            Evidence = UpdateSnapshot.BuildEvidence();
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetBoolField(TEXT("built"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRestored);
        Result->SetStringField(TEXT("rollback_verified_by"), TEXT("anim_montage_inspect"));
        Result->SetStringField(TEXT("error"), Error);
        Result->SetObjectField(TEXT("rollback"), Evidence);
        OutResultJson = MontageJson(Result);
        OutError = Error;
        return false;
    };

    if (bCreating)
    {
        UPackage* Package = CreatePackage(*PackagePath);
        Montage = Package != nullptr ? NewObject<UAnimMontage>(Package, FName(*AssetName),
            RF_Public | RF_Standalone | RF_Transactional) : nullptr;
        if (Montage == nullptr) { return Fail(TEXT("Unreal could not create the AnimMontage.")); }
        CreationRollback.TrackCreated(Montage);
        Registry.Get().AssetCreated(Montage);
    }
    if (!Montage->Modify())
    {
        return Fail(TEXT("AnimMontage Modify() returned false; no authoring writes were attempted."));
    }
    if (!ApplyMontage(Montage, Request, Skeleton, SegmentCount, OutError)) { return Fail(OutError); }
    Montage->PostEditChange();
    Montage->MarkPackageDirty();

    TSharedPtr<FJsonObject> Inspection;
    FString InspectError;
    if (!InspectMontage(this, PackagePath, Inspection, InspectError))
    {
        return Fail(TEXT("anim_montage_inspect failed after mutation: ") + InspectError);
    }
    const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
    Inspection->TryGetArrayField(TEXT("slots"), Slots);
    Inspection->TryGetArrayField(TEXT("sections"), Sections);
    int32 ReadSegments = 0;
    if (Slots != nullptr)
    {
        for (const TSharedPtr<FJsonValue>& SlotValue : *Slots)
        {
            const TArray<TSharedPtr<FJsonValue>>* Segments = nullptr;
            const TSharedPtr<FJsonObject> Slot = SlotValue->AsObject();
            if (Slot.IsValid() && Slot->TryGetArrayField(TEXT("segments"), Segments) && Segments != nullptr)
            {
                ReadSegments += Segments->Num();
            }
        }
    }
    if (Slots == nullptr || Sections == nullptr || Slots->Num() != Scratch->SlotAnimTracks.Num()
        || Sections->Num() != Scratch->CompositeSections.Num() || ReadSegments != SegmentCount)
    {
        return Fail(TEXT("AnimMontage serialized read-back did not preserve slot, segment and section counts."));
    }
    FString ReadSkeleton;
    Inspection->TryGetStringField(TEXT("skeleton"), ReadSkeleton);
    if (ReadSkeleton != Skeleton->GetPathName())
    {
        return Fail(TEXT("AnimMontage serialized read-back did not preserve the skeleton."));
    }
    FString StructureHash;
    Inspection->TryGetStringField(TEXT("structure_hash_sha1"), StructureHash);
    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
        if (!bSaved) { return Fail(TEXT("AnimMontage save failed: ") + SaveError); }
    }
    CreationRollback.Commit();
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetBoolField(TEXT("built"), true);
    Result->SetBoolField(TEXT("created"), bCreating);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetStringField(TEXT("verified_by"), TEXT("anim_montage_inspect"));
    Result->SetStringField(TEXT("structure_hash_sha1_before"), BeforeHash);
    Result->SetStringField(TEXT("structure_hash_sha1"), StructureHash);
    Result->SetNumberField(TEXT("slot_count"), Scratch->SlotAnimTracks.Num());
    Result->SetNumberField(TEXT("segment_count"), SegmentCount);
    Result->SetNumberField(TEXT("section_count"), Scratch->CompositeSections.Num());
    Result->SetNumberField(TEXT("notify_count"), 0);
    OutResultJson = MontageJson(Result);
    return true;
}