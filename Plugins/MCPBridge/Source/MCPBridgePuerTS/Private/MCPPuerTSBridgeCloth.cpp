// Copyright 2026 RareBird Games. All Rights Reserved.
//
// cloth_inspect: a native front for the READ half of MCPBridgeClothOptimizer,
// which was compiled into the plugin and reachable only through the legacy
// Python listener.
//
// The read half and only the read half, and that is docs/REFRONT_MAP.md group 3
// applied rather than a judgement made here. The module's three writers
// (ApplyFabricProfile, SmoothMaxDistanceTransition, ApplyLowerLegGradient) open
// a transaction, write a JSON mask backup, and then never cancel that
// transaction on the failure path. They mutate a skeletal mesh's cloth paint,
// which is authored data a re-run cannot regenerate, so an unrecoverable failure
// there costs work rather than time. They ship when a failed apply provably
// restores the mask, not before.
//
// Straight pass-through: this command adds path resolution and an envelope, and
// the snapshot itself is UClothOptimizerLibrary::BuildClothSnapshot's, unchanged.
// That is deliberate. The optimizer's own editor panel reads the same snapshot,
// so a caller comparing an MCP read against what a human sees in the panel is
// comparing one function's output with itself.

#include "MCPPuerTSBridgeService.h"

#include "ClothOptimizerLibrary.h"
#include "Json.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
    /** The optimizer library loads by object path, so a bare package path has
        to grow its asset name first. Same rule the other inspectors use. */
    bool ResolveClothAssetPath(const FString& AssetPath, FString& OutObjectPath, FString& OutError)
    {
        const FString Trimmed = AssetPath.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            OutError = TEXT("skeletal_mesh is required.");
            return false;
        }
        if (!Trimmed.StartsWith(TEXT("/Game/")) && !Trimmed.StartsWith(TEXT("/Engine/")))
        {
            OutError = TEXT("Cloth inspection is limited to /Game and /Engine.");
            return false;
        }
        if (Trimmed.Contains(TEXT(".")))
        {
            OutObjectPath = Trimmed;
            return true;
        }
        if (!FPackageName::IsValidLongPackageName(Trimmed))
        {
            OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *Trimmed);
            return false;
        }
        OutObjectPath = Trimmed + TEXT(".") + FPackageName::GetLongPackageAssetName(Trimmed);
        return true;
    }
}

// ---------------------------------------------------------------------------
// cloth_inspect
// ---------------------------------------------------------------------------

bool UMCPPuerTSBridgeService::InspectClothJson(
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
    Request->TryGetStringField(TEXT("skeletal_mesh"), AssetPath);
    FString ObjectPath;
    if (!ResolveClothAssetPath(AssetPath, ObjectPath, OutError)) { return false; }

    const FString SnapshotJson = UClothOptimizerLibrary::InspectClothAsset(ObjectPath);
    TSharedPtr<FJsonObject> Snapshot;
    TSharedRef<TJsonReader<>> SnapshotReader = TJsonReaderFactory<>::Create(SnapshotJson);
    if (!FJsonSerializer::Deserialize(SnapshotReader, Snapshot) || !Snapshot.IsValid())
    {
        OutError = FString::Printf(
            TEXT("UClothOptimizerLibrary::InspectClothAsset did not answer with JSON for '%s'."),
            *ObjectPath);
        return false;
    }

    // The library reports its own failures inside the snapshot rather than by
    // returning nothing, so an unsuccessful read has to be lifted out here.
    // Passing it through as a successful response carrying success:false would
    // put a failure behind a success envelope, which is the shape a caller is
    // least likely to check.
    bool bLibrarySucceeded = false;
    Snapshot->TryGetBoolField(TEXT("success"), bLibrarySucceeded);
    if (!bLibrarySucceeded)
    {
        FString LibraryError;
        Snapshot->TryGetStringField(TEXT("error"), LibraryError);
        OutError = LibraryError.IsEmpty()
            ? FString::Printf(TEXT("Cloth inspection of '%s' failed without naming a reason."), *ObjectPath)
            : LibraryError;
        return false;
    }

    TArray<TSharedPtr<FJsonValue>> Warnings;

    // One dirty flag, not a before/after pair, and that is the honest shape
    // here: InspectClothAsset loads the mesh itself, so this command has no
    // moment at which it could observe the package before the read without
    // loading it a second way. A true here means the mesh was already dirty or
    // loading it dirtied it, and either is worth seeing.
    const UPackage* Package = FindPackage(nullptr, *FPackageName::ObjectPathToPackageName(ObjectPath));
    const bool bDirtyAfterRead = Package != nullptr && Package->IsDirty();

    // A skeletal mesh with no clothing asset is a legitimate answer, and it is
    // the one most likely to be misread as a broken tool, so it is named.
    const TArray<TSharedPtr<FJsonValue>>* ClothAssets = nullptr;
    const int32 ClothAssetCount =
        Snapshot->TryGetArrayField(TEXT("clothAssets"), ClothAssets) ? ClothAssets->Num() : 0;
    if (ClothAssetCount == 0)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This skeletal mesh has no clothing assets. That is a mesh with no cloth on it, "
                 "not a failed read.")));
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetObjectField(TEXT("cloth"), Snapshot);
    Result->SetStringField(TEXT("skeletal_mesh"), ObjectPath);
    Result->SetNumberField(TEXT("clothing_asset_count"), ClothAssetCount);
    Result->SetStringField(TEXT("write_unsupported_reason"),
        TEXT("The three cloth writers in MCPBridgeClothOptimizer (ApplyFabricProfile, "
             "SmoothMaxDistanceTransition, ApplyLowerLegGradient) are not fronted natively. They "
             "open a transaction and do not cancel it on the failure path, and what they mutate is "
             "a skeletal mesh's cloth paint: authored data that a re-run cannot regenerate. They "
             "ship when a failed apply provably restores the mask. Until then the legacy tools "
             "cloth_apply_fabric_profile, cloth_smooth_max_distance and "
             "cloth_apply_lower_leg_gradient remain the only way to reach them, and they carry a "
             "confirm parameter for a reason. docs/REFRONT_MAP.md group 3."));
    Result->SetBoolField(TEXT("package_dirty_after_read"), bDirtyAfterRead);
    Result->SetArrayField(TEXT("warnings"), Warnings);

    OutResultJson = SerializeJson(Result);
    return true;
}
