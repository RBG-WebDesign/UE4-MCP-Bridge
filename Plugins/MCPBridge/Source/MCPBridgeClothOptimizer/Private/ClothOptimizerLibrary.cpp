// Copyright 2026 RareBird Games. All Rights Reserved.

#include "ClothOptimizerLibrary.h"

#include "ClothConfigNv.h"
#include "ClothingAsset.h"
#include "ClothingAssetBase.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PointWeightMap.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/Package.h"
#include "ScopedTransaction.h"

namespace ClothOptimizer
{
    static TSharedRef<FJsonObject> BuildNvConfigJson(const UClothConfigNv* Config);

    static FString ObjectPath(const UObject* Object)
    {
        return Object ? Object->GetPathName() : FString();
    }

    static FString WeightTargetName(const uint8 Target)
    {
        switch (static_cast<EWeightMapTargetCommon>(Target))
        {
        case EWeightMapTargetCommon::MaxDistance:
            return TEXT("MaxDistance");
        case EWeightMapTargetCommon::BackstopDistance:
            return TEXT("BackstopDistance");
        case EWeightMapTargetCommon::BackstopRadius:
            return TEXT("BackstopRadius");
        case EWeightMapTargetCommon::AnimDriveStiffness:
            return TEXT("AnimDriveStiffness");
        case EWeightMapTargetCommon::AnimDriveDamping:
            return TEXT("AnimDriveDamping");
        default:
            return TEXT("None");
        }
    }

    static FString BuildTopologyHash(const UClothingAssetCommon* Asset)
    {
        FSHA1 Sha;
        for (const FClothLODDataCommon& Lod : Asset->LodData)
        {
            const FClothPhysicalMeshData& Mesh = Lod.PhysicalMeshData;
            if (Mesh.Vertices.Num() > 0)
            {
                Sha.Update(reinterpret_cast<const uint8*>(Mesh.Vertices.GetData()), Mesh.Vertices.Num() * sizeof(FVector));
            }
            if (Mesh.Indices.Num() > 0)
            {
                Sha.Update(reinterpret_cast<const uint8*>(Mesh.Indices.GetData()), Mesh.Indices.Num() * sizeof(uint32));
            }
        }
        Sha.Final();
        uint8 Digest[FSHA1::DigestSize];
        Sha.GetHash(Digest);
        return FString::Printf(TEXT("sha1:%s"), *BytesToHex(Digest, FSHA1::DigestSize).ToLower());
    }

    static UClothingAssetCommon* FindClothingAsset(USkeletalMesh* SkeletalMesh, const FString& AssetPath)
    {
        if (!SkeletalMesh)
        {
            return nullptr;
        }
        for (UClothingAssetBase* BaseAsset : SkeletalMesh->GetMeshClothingAssets())
        {
            UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(BaseAsset);
            if (Asset && (Asset->GetPathName() == AssetPath || Asset->GetName() == AssetPath))
            {
                return Asset;
            }
        }
        return nullptr;
    }

    static FPointWeightMap* FindMaxDistanceMask(UClothingAssetCommon* Asset)
    {
        if (!Asset || Asset->LodData.Num() == 0)
        {
            return nullptr;
        }
        for (FPointWeightMap& Mask : Asset->LodData[0].PointWeightMaps)
        {
            if (Mask.bEnabled && Mask.CurrentTarget == static_cast<uint8>(EWeightMapTargetCommon::MaxDistance))
            {
                return &Mask;
            }
        }
        return nullptr;
    }

    static bool WriteMaskBackup(
        const USkeletalMesh* SkeletalMesh,
        const UClothingAssetCommon* Asset,
        const FPointWeightMap& Mask,
        const FString& ContentHash,
        FString& OutPath)
    {
        const FString BackupDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ClothMCP"), TEXT("Backups"));
        IFileManager::Get().MakeDirectory(*BackupDirectory, true);
        const FString SafeAssetName = Asset->GetName().Replace(TEXT("/"), TEXT("_"));
        const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
        OutPath = FPaths::Combine(BackupDirectory, FString::Printf(TEXT("%s-%s.json"), *SafeAssetName, *Timestamp));

        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("schemaVersion"), 1);
        Root->SetStringField(TEXT("createdAtUtc"), FDateTime::UtcNow().ToIso8601());
        Root->SetStringField(TEXT("skeletalMesh"), SkeletalMesh->GetPathName());
        Root->SetStringField(TEXT("clothingAsset"), Asset->GetPathName());
        Root->SetStringField(TEXT("contentHash"), ContentHash);
        Root->SetStringField(TEXT("target"), TEXT("MaxDistance"));
        if (const UClothConfigNv* Config = Asset->GetClothConfig<UClothConfigNv>())
        {
            Root->SetObjectField(TEXT("nvConfig"), BuildNvConfigJson(Config));
        }
        TArray<TSharedPtr<FJsonValue>> Values;
        Values.Reserve(Mask.Values.Num());
        for (const float Value : Mask.Values)
        {
            Values.Add(MakeShared<FJsonValueNumber>(Value));
        }
        Root->SetArrayField(TEXT("values"), Values);

        FString Json;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        return FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(Json, *OutPath);
    }

    static TSharedRef<FJsonObject> VectorJson(const FVector& Value)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetNumberField(TEXT("x"), Value.X);
        Json->SetNumberField(TEXT("y"), Value.Y);
        Json->SetNumberField(TEXT("z"), Value.Z);
        return Json;
    }

    static TSharedRef<FJsonObject> BuildNvConfigJson(const UClothConfigNv* Config)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        if (!Config)
        {
            Json->SetBoolField(TEXT("available"), false);
            return Json;
        }

        Json->SetBoolField(TEXT("available"), true);
        Json->SetNumberField(TEXT("solverFrequency"), Config->SolverFrequency);
        Json->SetNumberField(TEXT("stiffnessFrequency"), Config->StiffnessFrequency);
        Json->SetNumberField(TEXT("collisionThickness"), Config->CollisionThickness);
        Json->SetNumberField(TEXT("friction"), Config->Friction);
        Json->SetNumberField(TEXT("selfCollisionRadius"), Config->SelfCollisionRadius);
        Json->SetNumberField(TEXT("selfCollisionStiffness"), Config->SelfCollisionStiffness);
        Json->SetNumberField(TEXT("selfCollisionCullScale"), Config->SelfCollisionCullScale);
        Json->SetNumberField(TEXT("tetherStiffness"), Config->TetherStiffness);
        Json->SetNumberField(TEXT("tetherLimit"), Config->TetherLimit);
        Json->SetObjectField(TEXT("damping"), VectorJson(Config->Damping));
        Json->SetObjectField(TEXT("linearDrag"), VectorJson(Config->LinearDrag));
        Json->SetObjectField(TEXT("angularDrag"), VectorJson(Config->AngularDrag));
        return Json;
    }

    static TSharedRef<FJsonObject> BuildPhysicsJson(const UPhysicsAsset* PhysicsAsset)
    {
        TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        Json->SetStringField(TEXT("path"), ObjectPath(PhysicsAsset));
        if (!PhysicsAsset)
        {
            Json->SetNumberField(TEXT("bodyCount"), 0);
            Json->SetNumberField(TEXT("constraintCount"), 0);
            return Json;
        }

        Json->SetNumberField(TEXT("bodyCount"), PhysicsAsset->SkeletalBodySetups.Num());
        Json->SetNumberField(TEXT("constraintCount"), PhysicsAsset->ConstraintSetup.Num());
        TArray<TSharedPtr<FJsonValue>> Bodies;
        for (const USkeletalBodySetup* Body : PhysicsAsset->SkeletalBodySetups)
        {
            if (!Body)
            {
                continue;
            }
            TSharedRef<FJsonObject> BodyJson = MakeShared<FJsonObject>();
            BodyJson->SetStringField(TEXT("bone"), Body->BoneName.ToString());
            BodyJson->SetNumberField(TEXT("spheres"), Body->AggGeom.SphereElems.Num());
            BodyJson->SetNumberField(TEXT("capsules"), Body->AggGeom.SphylElems.Num());
            BodyJson->SetNumberField(TEXT("boxes"), Body->AggGeom.BoxElems.Num());
            BodyJson->SetNumberField(TEXT("convexes"), Body->AggGeom.ConvexElems.Num());
            Bodies.Add(MakeShared<FJsonValueObject>(BodyJson));
        }
        Json->SetArrayField(TEXT("bodies"), Bodies);
        return Json;
    }
}

FString UClothOptimizerLibrary::InspectClothAsset(const FString& SkeletalMeshPath)
{
    USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    FString Json;
    FString Error;
    if (!BuildClothSnapshot(Mesh, Json, Error))
    {
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetNumberField(TEXT("schemaVersion"), 1);
        Root->SetBoolField(TEXT("success"), false);
        Root->SetStringField(TEXT("error"), Error);
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        FJsonSerializer::Serialize(Root, Writer);
    }
    return Json;
}

bool UClothOptimizerLibrary::ApplyFabricProfile(
    const FString& SkeletalMeshPath,
    const FString& ClothingAssetName,
    const FString& FabricProfile,
    FString& OutBackupPath,
    FString& OutError)
{
    OutBackupPath.Reset();
    OutError.Reset();

    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    UClothingAssetCommon* Asset = ClothOptimizer::FindClothingAsset(SkeletalMesh, ClothingAssetName);
    FPointWeightMap* Mask = ClothOptimizer::FindMaxDistanceMask(Asset);
    UClothConfigNv* Config = Asset ? Asset->GetClothConfig<UClothConfigNv>() : nullptr;
    if (!SkeletalMesh || !Asset || !Mask || !Config)
    {
        OutError = TEXT("The skeletal mesh, clothing asset, Max Distance mask, or NvCloth config is unavailable.");
        return false;
    }
    if (!Asset->PhysicsAsset || Asset->PhysicsAsset->SkeletalBodySetups.Num() == 0)
    {
        OutError = TEXT("The selected clothing asset has no usable Physics Asset.");
        return false;
    }

    const FString ContentHash = ClothOptimizer::BuildTopologyHash(Asset);
    if (!ClothOptimizer::WriteMaskBackup(SkeletalMesh, Asset, *Mask, ContentHash, OutBackupPath))
    {
        OutError = TEXT("Could not write the pre-apply cloth backup.");
        return false;
    }

    const bool bHeavyDenim = FabricProfile.Equals(TEXT("heavy_denim"), ESearchCase::IgnoreCase);
    const bool bCloseFit = FabricProfile.Equals(TEXT("heavy_denim_close_fit"), ESearchCase::IgnoreCase);
    if (!bHeavyDenim && !bCloseFit)
    {
        OutError = FString::Printf(TEXT("Unsupported fabric profile: %s"), *FabricProfile);
        return false;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Apply Cloth Fabric Profile")));
    SkeletalMesh->Modify();
    Asset->Modify();
    Config->Modify();
    Config->SolverFrequency = 120.0f;
    Config->StiffnessFrequency = 100.0f;
    Config->Damping = FVector(0.50f);
    Config->Friction = 0.40f;
    Config->LinearDrag = FVector(0.40f);
    Config->AngularDrag = FVector(0.40f);
    Config->TetherStiffness = 1.0f;
    Config->TetherLimit = 0.90f;
    Config->CollisionThickness = bCloseFit ? 0.50f : 2.0f;

    Config->MarkPackageDirty();
    Asset->MarkPackageDirty();
    SkeletalMesh->MarkPackageDirty();
    SkeletalMesh->PostEditChange();

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.AddUnique(SkeletalMesh->GetOutermost());
    const FEditorFileUtils::EPromptReturnCode SaveResult = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
    if (SaveResult != FEditorFileUtils::PR_Success)
    {
        OutError = TEXT("The fabric profile changed in memory, but the skeletal mesh package was not saved.");
        return false;
    }
    return true;
}

bool UClothOptimizerLibrary::SmoothMaxDistanceTransition(
    const FString& SkeletalMeshPath,
    const FString& ClothingAssetName,
    const int32 Iterations,
    const float Strength,
    FString& OutSummary,
    FString& OutBackupPath,
    FString& OutError)
{
    OutSummary.Reset();
    OutBackupPath.Reset();
    OutError.Reset();

    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    if (!SkeletalMesh)
    {
        OutError = TEXT("The skeletal mesh could not be loaded.");
        return false;
    }

    TArray<float> CandidateValues;
    FString ContentHash;
    if (!GenerateMaxDistanceCandidate(
        SkeletalMesh,
        ClothingAssetName,
        Iterations,
        Strength,
        CandidateValues,
        ContentHash,
        OutSummary,
        OutError))
    {
        return false;
    }

    return ApplyMaxDistanceCandidate(
        SkeletalMesh,
        ClothingAssetName,
        ContentHash,
        CandidateValues,
        TEXT("Mask Only"),
        OutBackupPath,
        OutError);
}

bool UClothOptimizerLibrary::ApplyLowerLegGradient(
    const FString& SkeletalMeshPath,
    const FString& ClothingAssetName,
    const float KneeHeightFraction,
    FString& OutSummary,
    FString& OutBackupPath,
    FString& OutError)
{
    OutSummary.Reset();
    OutBackupPath.Reset();
    OutError.Reset();

    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshPath);
    UClothingAssetCommon* Asset = ClothOptimizer::FindClothingAsset(SkeletalMesh, ClothingAssetName);
    FPointWeightMap* Mask = ClothOptimizer::FindMaxDistanceMask(Asset);
    if (!SkeletalMesh || !Asset || !Mask || Asset->LodData.Num() == 0)
    {
        OutError = TEXT("The skeletal mesh, clothing asset, or Max Distance mask is unavailable.");
        return false;
    }

    const FClothPhysicalMeshData& PhysicalMesh = Asset->LodData[0].PhysicalMeshData;
    if (Mask->Values.Num() != PhysicalMesh.Vertices.Num() || PhysicalMesh.Vertices.Num() == 0)
    {
        OutError = TEXT("The Max Distance mask does not match the physical cloth mesh.");
        return false;
    }

    float MinZ = TNumericLimits<float>::Max();
    float MaxZ = TNumericLimits<float>::Lowest();
    float ExistingMaximum = 0.0f;
    for (int32 Index = 0; Index < PhysicalMesh.Vertices.Num(); ++Index)
    {
        MinZ = FMath::Min(MinZ, PhysicalMesh.Vertices[Index].Z);
        MaxZ = FMath::Max(MaxZ, PhysicalMesh.Vertices[Index].Z);
        ExistingMaximum = FMath::Max(ExistingMaximum, Mask->Values[Index]);
    }
    const float Height = MaxZ - MinZ;
    if (Height <= KINDA_SMALL_NUMBER || ExistingMaximum <= KINDA_SMALL_NUMBER)
    {
        OutError = TEXT("The cloth bounds or existing Max Distance range is invalid.");
        return false;
    }

    const float SafeKneeFraction = FMath::Clamp(KneeHeightFraction, 0.35f, 0.65f);
    const float KneeZ = MinZ + Height * SafeKneeFraction;
    TArray<float> CandidateValues = Mask->Values;
    int32 ChangedVertices = 0;
    int32 ActivatedVertices = 0;
    for (int32 Index = 0; Index < PhysicalMesh.Vertices.Num(); ++Index)
    {
        const float VertexZ = PhysicalMesh.Vertices[Index].Z;
        if (VertexZ > KneeZ)
        {
            continue;
        }

        const float T = FMath::Clamp((VertexZ - MinZ) / (KneeZ - MinZ), 0.0f, 1.0f);
        const float SmoothT = T * T * (3.0f - 2.0f * T);
        const float DesiredValue = ExistingMaximum * SmoothT;
        if (!FMath::IsNearlyEqual(CandidateValues[Index], DesiredValue, 0.0001f))
        {
            if (CandidateValues[Index] <= KINDA_SMALL_NUMBER && DesiredValue > KINDA_SMALL_NUMBER)
            {
                ++ActivatedVertices;
            }
            CandidateValues[Index] = DesiredValue;
            ++ChangedVertices;
        }
    }

    OutSummary = FString::Printf(
        TEXT("%d of %d vertices adjusted, %d fixed lower-leg vertices activated, hem 0.000 to knee %.3f over %.0f%% of garment height"),
        ChangedVertices,
        CandidateValues.Num(),
        ActivatedVertices,
        ExistingMaximum,
        SafeKneeFraction * 100.0f);

    return ApplyMaxDistanceCandidate(
        SkeletalMesh,
        ClothingAssetName,
        ClothOptimizer::BuildTopologyHash(Asset),
        CandidateValues,
        TEXT("Mask Only"),
        OutBackupPath,
        OutError);
}

bool UClothOptimizerLibrary::BuildClothSnapshot(USkeletalMesh* SkeletalMesh, FString& OutJson, FString& OutError)
{
    OutJson.Reset();
    OutError.Reset();
    if (!SkeletalMesh)
    {
        OutError = TEXT("Skeletal mesh was not found or could not be loaded.");
        return false;
    }

    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetBoolField(TEXT("success"), true);
    Root->SetStringField(TEXT("skeletalMesh"), SkeletalMesh->GetPathName());
    Root->SetStringField(TEXT("skeleton"), ClothOptimizer::ObjectPath(SkeletalMesh->GetSkeleton()));
    Root->SetNumberField(TEXT("lodCount"), SkeletalMesh->GetLODNum());

    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
    for (int32 Index = 0; Index < Materials.Num(); ++Index)
    {
        TSharedRef<FJsonObject> MaterialJson = MakeShared<FJsonObject>();
        MaterialJson->SetNumberField(TEXT("index"), Index);
        MaterialJson->SetStringField(TEXT("slotName"), Materials[Index].MaterialSlotName.ToString());
        MaterialJson->SetStringField(TEXT("material"), ClothOptimizer::ObjectPath(Materials[Index].MaterialInterface));
        MaterialSlots.Add(MakeShared<FJsonValueObject>(MaterialJson));
    }
    Root->SetArrayField(TEXT("materialSlots"), MaterialSlots);

    TArray<TSharedPtr<FJsonValue>> Sections;
    if (FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel())
    {
        for (int32 LodIndex = 0; LodIndex < ImportedModel->LODModels.Num(); ++LodIndex)
        {
            const FSkeletalMeshLODModel& LodModel = ImportedModel->LODModels[LodIndex];
            for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
            {
                const FSkelMeshSection& Section = LodModel.Sections[SectionIndex];
                const UClothingAssetBase* BoundAsset = SkeletalMesh->GetSectionClothingAsset(LodIndex, SectionIndex);
                TSharedRef<FJsonObject> SectionJson = MakeShared<FJsonObject>();
                SectionJson->SetNumberField(TEXT("lod"), LodIndex);
                SectionJson->SetNumberField(TEXT("section"), SectionIndex);
                SectionJson->SetNumberField(TEXT("materialIndex"), Section.MaterialIndex);
                SectionJson->SetNumberField(TEXT("triangleCount"), Section.NumTriangles);
                SectionJson->SetBoolField(TEXT("hasCloth"), BoundAsset != nullptr);
                SectionJson->SetStringField(TEXT("clothAsset"), ClothOptimizer::ObjectPath(BoundAsset));
                Sections.Add(MakeShared<FJsonValueObject>(SectionJson));
            }
        }
    }
    Root->SetArrayField(TEXT("renderSections"), Sections);

    TArray<TSharedPtr<FJsonValue>> ClothAssets;
    for (const UClothingAssetBase* BaseAsset : SkeletalMesh->GetMeshClothingAssets())
    {
        const UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(BaseAsset);
        if (!Asset)
        {
            continue;
        }

        TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
        AssetJson->SetStringField(TEXT("name"), Asset->GetName());
        AssetJson->SetStringField(TEXT("path"), Asset->GetPathName());
        AssetJson->SetStringField(TEXT("guid"), Asset->GetAssetGuid().ToString());
        AssetJson->SetStringField(TEXT("contentHash"), ClothOptimizer::BuildTopologyHash(Asset));
        AssetJson->SetStringField(TEXT("physicsAsset"), ClothOptimizer::ObjectPath(Asset->PhysicsAsset));
        AssetJson->SetObjectField(TEXT("nvConfig"), ClothOptimizer::BuildNvConfigJson(Asset->GetClothConfig<UClothConfigNv>()));

        TArray<TSharedPtr<FJsonValue>> Lods;
        for (int32 LodIndex = 0; LodIndex < Asset->LodData.Num(); ++LodIndex)
        {
            const FClothLODDataCommon& Lod = Asset->LodData[LodIndex];
            const FClothPhysicalMeshData& PhysicalMesh = Lod.PhysicalMeshData;
            TSharedRef<FJsonObject> LodJson = MakeShared<FJsonObject>();
            LodJson->SetNumberField(TEXT("lod"), LodIndex);
            LodJson->SetNumberField(TEXT("physicalVertexCount"), PhysicalMesh.Vertices.Num());
            LodJson->SetNumberField(TEXT("triangleCount"), PhysicalMesh.Indices.Num() / 3);
            LodJson->SetNumberField(TEXT("fixedVertexCount"), PhysicalMesh.NumFixedVerts);

            TArray<TSharedPtr<FJsonValue>> AppliedMaps;
            for (const TPair<uint32, FPointWeightMap>& AppliedPair : PhysicalMesh.WeightMaps)
            {
                TSharedRef<FJsonObject> AppliedJson = MakeShared<FJsonObject>();
                AppliedJson->SetStringField(TEXT("target"), ClothOptimizer::WeightTargetName(static_cast<uint8>(AppliedPair.Key)));
                AppliedJson->SetNumberField(TEXT("valueCount"), AppliedPair.Value.Values.Num());
                AppliedJson->SetBoolField(TEXT("present"), AppliedPair.Value.Values.Num() > 0);
                AppliedJson->SetBoolField(TEXT("validLength"), AppliedPair.Value.Values.Num() == 0 || AppliedPair.Value.Values.Num() == PhysicalMesh.Vertices.Num());
                AppliedMaps.Add(MakeShared<FJsonValueObject>(AppliedJson));
            }
            LodJson->SetArrayField(TEXT("appliedWeightMaps"), AppliedMaps);

            TArray<TSharedPtr<FJsonValue>> Masks;
            for (const FPointWeightMap& Mask : Lod.PointWeightMaps)
            {
                TSharedRef<FJsonObject> MaskJson = MakeShared<FJsonObject>();
                MaskJson->SetStringField(TEXT("name"), Mask.Name.ToString());
                MaskJson->SetStringField(TEXT("target"), ClothOptimizer::WeightTargetName(Mask.CurrentTarget));
                MaskJson->SetBoolField(TEXT("enabled"), Mask.bEnabled);
                MaskJson->SetNumberField(TEXT("valueCount"), Mask.Values.Num());
                float Minimum = 0.0f;
                float Maximum = 0.0f;
                if (Mask.Values.Num() > 0)
                {
                    Minimum = FMath::Min(Mask.Values);
                    Maximum = FMath::Max(Mask.Values);
                }
                MaskJson->SetNumberField(TEXT("minimum"), Minimum);
                MaskJson->SetNumberField(TEXT("maximum"), Maximum);
                MaskJson->SetBoolField(TEXT("validLength"), Mask.Values.Num() == PhysicalMesh.Vertices.Num());
                Masks.Add(MakeShared<FJsonValueObject>(MaskJson));
            }
            LodJson->SetArrayField(TEXT("masks"), Masks);
            Lods.Add(MakeShared<FJsonValueObject>(LodJson));
        }
        AssetJson->SetArrayField(TEXT("lods"), Lods);
        ClothAssets.Add(MakeShared<FJsonValueObject>(AssetJson));
    }
    Root->SetArrayField(TEXT("clothAssets"), ClothAssets);
    Root->SetNumberField(TEXT("clothAssetCount"), ClothAssets.Num());
    Root->SetObjectField(TEXT("meshPhysicsAsset"), ClothOptimizer::BuildPhysicsJson(SkeletalMesh->GetPhysicsAsset()));

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
    if (!FJsonSerializer::Serialize(Root, Writer))
    {
        OutError = TEXT("Failed to serialize the cloth snapshot.");
        return false;
    }
    return true;
}

bool UClothOptimizerLibrary::GenerateMaxDistanceCandidate(
    USkeletalMesh* SkeletalMesh,
    const FString& ClothingAssetPath,
    const int32 Iterations,
    const float Strength,
    TArray<float>& OutValues,
    FString& OutContentHash,
    FString& OutSummary,
    FString& OutError)
{
    OutValues.Reset();
    OutContentHash.Reset();
    OutSummary.Reset();
    OutError.Reset();

    UClothingAssetCommon* Asset = ClothOptimizer::FindClothingAsset(SkeletalMesh, ClothingAssetPath);
    if (!Asset || Asset->LodData.Num() == 0)
    {
        OutError = TEXT("The selected clothing asset or its LOD0 data is unavailable.");
        return false;
    }

    FPointWeightMap* Mask = ClothOptimizer::FindMaxDistanceMask(Asset);
    const FClothPhysicalMeshData& PhysicalMesh = Asset->LodData[0].PhysicalMeshData;
    if (!Mask || Mask->Values.Num() != PhysicalMesh.Vertices.Num())
    {
        OutError = TEXT("The selected clothing asset does not have a valid LOD0 Max Distance mask.");
        return false;
    }

    TArray<TSet<int32>> Neighbors;
    Neighbors.SetNum(PhysicalMesh.Vertices.Num());
    for (int32 Index = 0; Index + 2 < PhysicalMesh.Indices.Num(); Index += 3)
    {
        const int32 A = static_cast<int32>(PhysicalMesh.Indices[Index]);
        const int32 B = static_cast<int32>(PhysicalMesh.Indices[Index + 1]);
        const int32 C = static_cast<int32>(PhysicalMesh.Indices[Index + 2]);
        if (!Neighbors.IsValidIndex(A) || !Neighbors.IsValidIndex(B) || !Neighbors.IsValidIndex(C))
        {
            OutError = TEXT("Physical mesh indices are outside the vertex array.");
            return false;
        }
        Neighbors[A].Add(B); Neighbors[A].Add(C);
        Neighbors[B].Add(A); Neighbors[B].Add(C);
        Neighbors[C].Add(A); Neighbors[C].Add(B);
    }

    const TArray<float> OriginalValues = Mask->Values;
    OutValues = OriginalValues;
    const float SafeStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
    const int32 SafeIterations = FMath::Clamp(Iterations, 1, 12);
    int32 ChangedVertices = 0;

    for (int32 Iteration = 0; Iteration < SafeIterations; ++Iteration)
    {
        TArray<float> Previous = OutValues;
        for (int32 VertexIndex = 0; VertexIndex < Previous.Num(); ++VertexIndex)
        {
            if (OriginalValues[VertexIndex] <= KINDA_SMALL_NUMBER || Neighbors[VertexIndex].Num() == 0)
            {
                OutValues[VertexIndex] = 0.0f;
                continue;
            }

            float Sum = 0.0f;
            for (const int32 Neighbor : Neighbors[VertexIndex])
            {
                Sum += Previous[Neighbor];
            }
            const float Average = Sum / static_cast<float>(Neighbors[VertexIndex].Num());
            const float Smoothed = FMath::Lerp(Previous[VertexIndex], Average, SafeStrength);
            OutValues[VertexIndex] = FMath::Clamp(FMath::Min(Smoothed, OriginalValues[VertexIndex]), 0.0f, OriginalValues[VertexIndex]);
        }
    }

    float OriginalMaximum = 0.0f;
    float CandidateMaximum = 0.0f;
    for (int32 Index = 0; Index < OutValues.Num(); ++Index)
    {
        OriginalMaximum = FMath::Max(OriginalMaximum, OriginalValues[Index]);
        CandidateMaximum = FMath::Max(CandidateMaximum, OutValues[Index]);
        if (!FMath::IsNearlyEqual(OriginalValues[Index], OutValues[Index], 0.0001f))
        {
            ChangedVertices++;
        }
    }

    OutContentHash = ClothOptimizer::BuildTopologyHash(Asset);
    OutSummary = FString::Printf(
        TEXT("%d of %d vertices adjusted, maximum %.3f to %.3f, %d smoothing passes at %.0f%% strength"),
        ChangedVertices,
        OutValues.Num(),
        OriginalMaximum,
        CandidateMaximum,
        SafeIterations,
        SafeStrength * 100.0f);
    return true;
}

bool UClothOptimizerLibrary::ApplyMaxDistanceCandidate(
    USkeletalMesh* SkeletalMesh,
    const FString& ClothingAssetPath,
    const FString& ExpectedContentHash,
    const TArray<float>& Values,
    const FString& FabricProfile,
    FString& OutBackupPath,
    FString& OutError)
{
    OutBackupPath.Reset();
    OutError.Reset();
    UClothingAssetCommon* Asset = ClothOptimizer::FindClothingAsset(SkeletalMesh, ClothingAssetPath);
    FPointWeightMap* Mask = ClothOptimizer::FindMaxDistanceMask(Asset);
    if (!SkeletalMesh || !Asset || !Mask || Asset->LodData.Num() == 0)
    {
        OutError = TEXT("The selected mesh, clothing asset, or Max Distance mask is unavailable.");
        return false;
    }

    const FString CurrentHash = ClothOptimizer::BuildTopologyHash(Asset);
    if (CurrentHash != ExpectedContentHash)
    {
        OutError = TEXT("The cloth topology changed after candidate generation. Analyze and generate again.");
        return false;
    }
    if (Values.Num() != Asset->LodData[0].PhysicalMeshData.Vertices.Num())
    {
        OutError = TEXT("Candidate value count does not match the physical cloth vertex count.");
        return false;
    }
    if (!ClothOptimizer::WriteMaskBackup(SkeletalMesh, Asset, *Mask, CurrentHash, OutBackupPath))
    {
        OutError = TEXT("Could not write the pre-apply mask backup.");
        return false;
    }

    const FScopedTransaction Transaction(FText::FromString(TEXT("Apply Cloth Optimizer Max Distance Candidate")));
    SkeletalMesh->Modify();
    Asset->Modify();
    Mask->Values = Values;

    UClothConfigNv* Config = Asset->GetClothConfig<UClothConfigNv>();
    if (Config && !FabricProfile.Equals(TEXT("Mask Only"), ESearchCase::IgnoreCase))
    {
        Config->Modify();
        if (FabricProfile.Equals(TEXT("Heavy Denim"), ESearchCase::IgnoreCase))
        {
            Config->SolverFrequency = 120.0f;
            Config->StiffnessFrequency = 100.0f;
            Config->Damping = FVector(0.50f);
            Config->Friction = 0.40f;
            Config->LinearDrag = FVector(0.40f);
            Config->AngularDrag = FVector(0.40f);
            Config->TetherStiffness = 1.0f;
            Config->TetherLimit = 0.90f;
            Config->CollisionThickness = 2.0f;
        }
        else if (FabricProfile.Equals(TEXT("Heavy Denim Close Fit"), ESearchCase::IgnoreCase))
        {
            Config->SolverFrequency = 120.0f;
            Config->StiffnessFrequency = 100.0f;
            Config->Damping = FVector(0.50f);
            Config->Friction = 0.40f;
            Config->LinearDrag = FVector(0.40f);
            Config->AngularDrag = FVector(0.40f);
            Config->TetherStiffness = 1.0f;
            Config->TetherLimit = 0.90f;
            Config->CollisionThickness = 0.50f;
        }
        else if (FabricProfile.Equals(TEXT("Medium Denim"), ESearchCase::IgnoreCase))
        {
            Config->SolverFrequency = 120.0f;
            Config->StiffnessFrequency = 80.0f;
            Config->Damping = FVector(0.35f);
            Config->Friction = 0.30f;
            Config->LinearDrag = FVector(0.30f);
            Config->AngularDrag = FVector(0.30f);
            Config->TetherStiffness = 0.85f;
            Config->TetherLimit = 0.95f;
            Config->CollisionThickness = 2.0f;
        }
        else if (FabricProfile.Equals(TEXT("Medium Trench"), ESearchCase::IgnoreCase))
        {
            Config->SolverFrequency = 180.0f;
            Config->StiffnessFrequency = 30.0f;
            Config->Damping = FVector(0.25f);
            Config->Friction = 0.20f;
            Config->LinearDrag = FVector(0.25f);
            Config->AngularDrag = FVector(0.25f);
            Config->TetherStiffness = 0.50f;
            Config->TetherLimit = 1.0f;
            Config->CollisionThickness = 3.0f;
        }
        else if (FabricProfile.Equals(TEXT("Light Fabric"), ESearchCase::IgnoreCase))
        {
            Config->SolverFrequency = 120.0f;
            Config->StiffnessFrequency = 30.0f;
            Config->Damping = FVector(0.15f);
            Config->Friction = 0.10f;
            Config->LinearDrag = FVector(0.15f);
            Config->AngularDrag = FVector(0.15f);
            Config->TetherStiffness = 0.30f;
            Config->TetherLimit = 1.0f;
            Config->CollisionThickness = 2.0f;
        }
        Config->MarkPackageDirty();
    }
    Asset->ApplyParameterMasks(true);
    Asset->MarkPackageDirty();
    SkeletalMesh->MarkPackageDirty();
    SkeletalMesh->PostEditChange();

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.AddUnique(SkeletalMesh->GetOutermost());
    const FEditorFileUtils::EPromptReturnCode SaveResult = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false);
    if (SaveResult != FEditorFileUtils::PR_Success)
    {
        OutError = TEXT("The candidate was applied in memory, but the package was not saved. Use Undo or save the asset manually.");
        return false;
    }
    return true;
}
