// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"
#include "AssetRegistryModule.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    struct FBlendSampleSpec
    {
        FString Animation;
        float Position = 0.0f;
        float RateScale = 1.0f;
        bool bHasRateScale = false;
    };

    bool ResolveBuildPath(
        const FString& AssetPath,
        FString& OutPackagePath,
        FString& OutObjectPath,
        FString& OutAssetName,
        FString& OutError)
    {
        OutPackagePath = AssetPath.TrimStartAndEnd();
        if (!OutPackagePath.StartsWith(TEXT("/Game/MCPGenerated/")))
        {
            OutError = TEXT("anim_blend_space_build is limited to /Game/MCPGenerated/.");
            return false;
        }
        if (OutPackagePath.Contains(TEXT("."))
            || !FPackageName::IsValidLongPackageName(OutPackagePath))
        {
            OutError = TEXT("asset_path must be a valid package path without an object suffix.");
            return false;
        }
        OutAssetName = FPackageName::GetLongPackageAssetName(OutPackagePath);
        OutObjectPath = OutPackagePath + TEXT(".") + OutAssetName;
        return true;
    }

    bool ParseAxisSpec(
        const TSharedPtr<FJsonObject>& Request,
        FString& OutName,
        float& OutMin,
        float& OutMax,
        int32& OutGridDivisions,
        FString& OutError)
    {
        const TSharedPtr<FJsonObject>* AxisPtr = nullptr;
        if (!Request->TryGetObjectField(TEXT("axis"), AxisPtr) || AxisPtr == nullptr)
        {
            OutError = TEXT("axis is required: {name, min, max, grid_divisions}.");
            return false;
        }
        const TSharedPtr<FJsonObject>& Axis = *AxisPtr;
        if (!Axis->TryGetStringField(TEXT("name"), OutName) || OutName.TrimStartAndEnd().IsEmpty())
        {
            OutError = TEXT("axis.name is required.");
            return false;
        }
        double MinValue = 0.0, MaxValue = 0.0;
        if (!Axis->TryGetNumberField(TEXT("min"), MinValue)
            || !Axis->TryGetNumberField(TEXT("max"), MaxValue))
        {
            OutError = TEXT("axis.min and axis.max are required numbers.");
            return false;
        }
        OutMin = static_cast<float>(MinValue);
        OutMax = static_cast<float>(MaxValue);
        if (!(OutMax > OutMin))
        {
            OutError = FString::Printf(
                TEXT("axis.max (%f) must be greater than axis.min (%f)."), OutMax, OutMin);
            return false;
        }
        int32 GridDivisions = 4;
        Axis->TryGetNumberField(TEXT("grid_divisions"), GridDivisions);
        if (GridDivisions < 1)
        {
            OutError = TEXT("axis.grid_divisions must be at least 1.");
            return false;
        }
        OutGridDivisions = GridDivisions;
        return true;
    }

    bool ParseSampleSpecs(
        const TSharedPtr<FJsonObject>& Request,
        TArray<FBlendSampleSpec>& OutSamples,
        FString& OutError)
    {
        const TArray<TSharedPtr<FJsonValue>>* Samples = nullptr;
        if (!Request->TryGetArrayField(TEXT("samples"), Samples) || Samples == nullptr
            || Samples->Num() == 0 || Samples->Num() > 64)
        {
            OutError = TEXT("samples must contain between 1 and 64 entries.");
            return false;
        }
        TSet<float> SeenPositions;
        for (int32 Index = 0; Index < Samples->Num(); ++Index)
        {
            const TSharedPtr<FJsonObject> SampleObject = (*Samples)[Index]->AsObject();
            if (!SampleObject.IsValid())
            {
                OutError = FString::Printf(TEXT("samples[%d] must be an object."), Index);
                return false;
            }
            FBlendSampleSpec Spec;
            if (!SampleObject->TryGetStringField(TEXT("animation"), Spec.Animation)
                || Spec.Animation.TrimStartAndEnd().IsEmpty())
            {
                OutError = FString::Printf(TEXT("samples[%d].animation is required."), Index);
                return false;
            }
            double Position = 0.0;
            if (!SampleObject->TryGetNumberField(TEXT("position"), Position))
            {
                OutError = FString::Printf(TEXT("samples[%d].position is required."), Index);
                return false;
            }
            Spec.Position = static_cast<float>(Position);
            if (SeenPositions.Contains(Spec.Position))
            {
                OutError = FString::Printf(
                    TEXT("samples[%d] duplicates position %f. Two samples at the same point is "
                         "exactly the malformed state this builder exists to refuse before it "
                         "ever reaches the asset."), Index, Spec.Position);
                return false;
            }
            SeenPositions.Add(Spec.Position);
            double RateScale = 1.0;
            if (SampleObject->TryGetNumberField(TEXT("rate_scale"), RateScale))
            {
                Spec.RateScale = static_cast<float>(RateScale);
                Spec.bHasRateScale = true;
                if (Spec.RateScale <= 0.0f)
                {
                    OutError = FString::Printf(TEXT("samples[%d].rate_scale must be positive."), Index);
                    return false;
                }
            }
            OutSamples.Add(Spec);
        }
        return true;
    }
}

bool UMCPPuerTSBridgeService::BuildAnimBlendSpaceJson(
    const FString& RequestJson,
    FString& OutResultJson,
    FString& OutError)
{
    TSharedPtr<FJsonObject> Request;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RequestJson);
    if (!FJsonSerializer::Deserialize(Reader, Request) || !Request.IsValid())
    {
        OutError = TEXT("anim_blend_space_build request must be a JSON object.");
        return false;
    }

    FString RequestedPath;
    Request->TryGetStringField(TEXT("asset_path"), RequestedPath);
    FString PackagePath, ObjectPath, AssetName;
    if (!ResolveBuildPath(RequestedPath, PackagePath, ObjectPath, AssetName, OutError)) { return false; }

    FString SkeletonPath;
    if (!Request->TryGetStringField(TEXT("skeleton_path"), SkeletonPath)
        || SkeletonPath.TrimStartAndEnd().IsEmpty())
    {
        OutError = TEXT("skeleton_path is required.");
        return false;
    }
    USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
    if (Skeleton == nullptr)
    {
        OutError = FString::Printf(TEXT("No Skeleton was found at '%s'."), *SkeletonPath);
        return false;
    }

    FString AxisName;
    float AxisMin = 0.0f, AxisMax = 0.0f;
    int32 AxisGridDivisions = 4;
    if (!ParseAxisSpec(Request, AxisName, AxisMin, AxisMax, AxisGridDivisions, OutError)) { return false; }

    TArray<FBlendSampleSpec> Samples;
    if (!ParseSampleSpecs(Request, Samples, OutError)) { return false; }

    TArray<UAnimSequence*> ResolvedAnimations;
    for (const FBlendSampleSpec& Spec : Samples)
    {
        UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *Spec.Animation);
        if (Animation == nullptr)
        {
            OutError = FString::Printf(TEXT("No AnimSequence was found at '%s'."), *Spec.Animation);
            return false;
        }
        if (Animation->GetSkeleton() != Skeleton)
        {
            OutError = FString::Printf(
                TEXT("'%s' targets skeleton '%s', not '%s'."),
                *Spec.Animation,
                Animation->GetSkeleton() != nullptr
                    ? *Animation->GetSkeleton()->GetPathName() : TEXT("none"),
                *SkeletonPath);
            return false;
        }
        if (Spec.Position < AxisMin || Spec.Position > AxisMax)
        {
            OutError = FString::Printf(
                TEXT("Sample at %f for '%s' is outside the axis range [%f, %f]."),
                Spec.Position, *Spec.Animation, AxisMin, AxisMax);
            return false;
        }
        ResolvedAnimations.Add(Animation);
    }

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    if (AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid())
    {
        OutError = FString::Printf(
            TEXT("An asset already exists at '%s'. anim_blend_space_build is create-only and "
                 "never edits an existing Blend Space's sample set."), *ObjectPath);
        return false;
    }

    const bool bPlanOnly = Request->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Request->GetBoolField(TEXT("plan_only"));
    if (bPlanOnly)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetStringField(TEXT("object_path"), ObjectPath);
        Result->SetBoolField(TEXT("plan_only"), true);
        Result->SetBoolField(TEXT("would_create"), true);
        Result->SetNumberField(TEXT("expected_sample_count"), Samples.Num());
        OutResultJson = SerializeJson(Result);
        return true;
    }
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("anim_blend_space_build requires an active transaction.");
        return false;
    }

    FBridgeAssetRollback CreationRollback;
    CreationRollback.Snapshot(PackagePath);
    const TArray<FString> DirtyBefore = CreationRollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = CreationRollback.SourceControlState();

    auto Fail = [&](const FString& Error) -> bool
    {
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }
        CreationRollback.Rollback();
        const bool bRollbackSucceeded =
            !AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath)).IsValid();
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("asset_path"), PackagePath);
        Result->SetBoolField(TEXT("built"), false);
        Result->SetBoolField(TEXT("saved"), false);
        Result->SetBoolField(TEXT("rollback_succeeded"), bRollbackSucceeded);
        Result->SetStringField(TEXT("rollback_verified_by"), TEXT("anim_blend_space_inspect"));
        Result->SetStringField(TEXT("error"), Error);
        Result->SetObjectField(
            TEXT("rollback"), CreationRollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutResultJson = SerializeJson(Result);
        OutError = Error;
        return false;
    };

    UPackage* Package = CreatePackage(*PackagePath);
    UBlendSpace1D* BlendSpace = Package != nullptr ? NewObject<UBlendSpace1D>(
        Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional) : nullptr;
    if (BlendSpace == nullptr) { return Fail(TEXT("Unreal could not create the Blend Space 1D.")); }
    CreationRollback.TrackCreated(BlendSpace);
    AssetRegistry.Get().AssetCreated(BlendSpace);

    if (!BlendSpace->Modify())
    {
        return Fail(TEXT("Blend Space Modify() returned false; no authoring writes were attempted."));
    }

    BlendSpace->SetSkeleton(Skeleton);

    // BlendParameters[3] is a fixed C array, EditAnywhere but not otherwise
    // exposed - the editor's own Details panel reaches it the same way, by
    // reflection, because FBlendSpaceDetails is the only friend the class
    // grants. Index 0 is the only axis a BlendSpace1D reads.
    FStructProperty* AxisProperty =
        FindFProperty<FStructProperty>(BlendSpace->GetClass(), TEXT("BlendParameters"));
    if (AxisProperty == nullptr)
    {
        return Fail(TEXT("UBlendSpaceBase has no reflected BlendParameters property. "
                          "The engine's blend space layout has changed; report this."));
    }
    FBlendParameter* Axis = AxisProperty->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 0);
    Axis->DisplayName = AxisName;
    Axis->Min = AxisMin;
    Axis->Max = AxisMax;
    Axis->GridNum = AxisGridDivisions;

    for (int32 Index = 0; Index < Samples.Num(); ++Index)
    {
        const FBlendSampleSpec& Spec = Samples[Index];
        if (!BlendSpace->IsAnimationCompatibleWithSkeleton(ResolvedAnimations[Index]))
        {
            return Fail(FString::Printf(
                TEXT("'%s' is not compatible with this Blend Space."), *Spec.Animation));
        }
        if (!BlendSpace->AddSample(ResolvedAnimations[Index], FVector(Spec.Position, 0.0f, 0.0f)))
        {
            return Fail(FString::Printf(
                TEXT("Unreal refused sample %d ('%s' at %f)."), Index, *Spec.Animation, Spec.Position));
        }
    }

    // AddSample has no RateScale parameter, so a non-default rate is written
    // after the fact through the same reflection route as the axis, this
    // time indexing the array element AddSample just appended.
    const bool bAnySampleHasRateScale = Samples.ContainsByPredicate(
        [](const FBlendSampleSpec& Spec) { return Spec.bHasRateScale; });
    if (bAnySampleHasRateScale)
    {
        FArrayProperty* SampleDataProperty =
            FindFProperty<FArrayProperty>(BlendSpace->GetClass(), TEXT("SampleData"));
        if (SampleDataProperty == nullptr)
        {
            return Fail(TEXT("UBlendSpaceBase has no reflected SampleData property. "
                              "The engine's blend space layout has changed; report this."));
        }
        FScriptArrayHelper SampleDataHelper(
            SampleDataProperty, SampleDataProperty->ContainerPtrToValuePtr<void>(BlendSpace));
        for (int32 Index = 0; Index < Samples.Num(); ++Index)
        {
            if (!Samples[Index].bHasRateScale) { continue; }
            if (!SampleDataHelper.IsValidIndex(Index))
            {
                return Fail(FString::Printf(
                    TEXT("Sample %d has no backing array element after AddSample."), Index));
            }
            FBlendSample* Raw = reinterpret_cast<FBlendSample*>(SampleDataHelper.GetRawPtr(Index));
            Raw->RateScale = Samples[Index].RateScale;
        }
    }

    // The whole reason this command can exist: every sample lands first, and
    // ONLY THEN does the triangulation get rebuilt once, so a caller never
    // observes (or leaves saved) a grid rebuilt from a partial sample set.
    BlendSpace->ValidateSampleData();

#if WITH_EDITORONLY_DATA
    int32 InvalidIndex = INDEX_NONE;
    const TArray<FBlendSample>& ValidatedSamples = BlendSpace->GetBlendSamples();
    for (int32 Index = 0; Index < ValidatedSamples.Num(); ++Index)
    {
        if (!ValidatedSamples[Index].bIsValid) { InvalidIndex = Index; break; }
    }
    if (InvalidIndex != INDEX_NONE)
    {
        return Fail(FString::Printf(
            TEXT("Sample %d did not validate after ValidateSampleData(): the grid could not be "
                 "triangulated for the requested sample set."), InvalidIndex));
    }
#endif

    BlendSpace->PostEditChange();
    BlendSpace->MarkPackageDirty();

    TSharedPtr<FJsonObject> InspectRequest = MakeShared<FJsonObject>();
    InspectRequest->SetStringField(TEXT("asset_path"), PackagePath);
    FString InspectJson, InspectError;
    if (!InspectAnimBlendSpaceJson(SerializeJson(InspectRequest), InspectJson, InspectError))
    {
        return Fail(FString::Printf(
            TEXT("anim_blend_space_inspect failed after the build: %s"), *InspectError));
    }
    TSharedPtr<FJsonObject> Inspection;
    TSharedRef<TJsonReader<>> InspectReader = TJsonReaderFactory<>::Create(InspectJson);
    if (!FJsonSerializer::Deserialize(InspectReader, Inspection) || !Inspection.IsValid())
    {
        return Fail(TEXT("anim_blend_space_inspect returned invalid JSON after the build."));
    }
    const TArray<TSharedPtr<FJsonValue>>* ReadSamples = nullptr;
    if (!Inspection->TryGetArrayField(TEXT("samples"), ReadSamples) || ReadSamples == nullptr
        || ReadSamples->Num() != Samples.Num())
    {
        return Fail(TEXT("Blend Space read-back sample count did not match the request."));
    }

    FString StructureHash;
    Inspection->TryGetStringField(TEXT("structure_hash_sha1"), StructureHash);
    const bool bSave = !Request->HasField(TEXT("save")) || Request->GetBoolField(TEXT("save"));
    bool bSaved = false;
    FString SaveError;
    if (bSave)
    {
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
    }
    CreationRollback.Commit();

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), ObjectPath);
    Result->SetBoolField(TEXT("built"), true);
    Result->SetBoolField(TEXT("created"), true);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetBoolField(TEXT("verified"), true);
    Result->SetStringField(TEXT("verified_by"), TEXT("anim_blend_space_inspect"));
    Result->SetStringField(TEXT("structure_hash_sha1"), StructureHash);
    Result->SetNumberField(TEXT("sample_count"), Samples.Num());
    if (!SaveError.IsEmpty()) { Result->SetStringField(TEXT("save_warning"), SaveError); }
    OutResultJson = SerializeJson(Result);
    return true;
}
