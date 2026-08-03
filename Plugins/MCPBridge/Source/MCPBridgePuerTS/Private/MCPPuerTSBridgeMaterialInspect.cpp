// Copyright 2026 RareBird Games. All Rights Reserved.
//
// Read-only material inspection, in the same shape graph_inspect,
// behavior_tree_inspect and widget_inspect use.
//
// Why it exists: materials were the one authoring domain in this bridge with no
// reader at all. puerts_sky_shader_create could write a material graph and the
// only evidence it landed was its own response, which is the same class of
// claim widget_build had before widget_inspect. This walks the saved asset
// instead, so a desired material can be compared against actual saved state
// without opening the editor.
//
// It answers for a UMaterial and for a UMaterialInstanceConstant at the same
// tool name, because a caller holding an asset path usually does not care which
// it has. asset_kind says which one answered.
//
// READ ONLY, and that is the contract rather than a hope: material_inspect is
// absent from IsToolMutating so no transaction opens, nothing here calls
// Modify, MarkPackageDirty or a compile, and the package dirty flag is read
// before and after the walk and reported both ways.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "MCPBridgeMaterialSnapshot.h"
#include "Engine/EngineTypes.h"
#include "Json.h"
// EMaterialDomain is defined here, not in Material.h, which only forward
// declares it through TEnumAsByte.
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
    const TCHAR* MaterialDomainLabel(EMaterialDomain Domain)
    {
        switch (Domain)
        {
            case MD_Surface:          return TEXT("Surface");
            case MD_DeferredDecal:    return TEXT("DeferredDecal");
            case MD_LightFunction:    return TEXT("LightFunction");
            case MD_Volume:           return TEXT("Volume");
            case MD_PostProcess:      return TEXT("PostProcess");
            case MD_UI:               return TEXT("UI");
            case MD_RuntimeVirtualTexture: return TEXT("RuntimeVirtualTexture");
            default:                  return TEXT("Unknown");
        }
    }

    const TCHAR* BlendModeLabel(EBlendMode BlendMode)
    {
        switch (BlendMode)
        {
            case BLEND_Opaque:          return TEXT("Opaque");
            case BLEND_Masked:          return TEXT("Masked");
            case BLEND_Translucent:     return TEXT("Translucent");
            case BLEND_Additive:        return TEXT("Additive");
            case BLEND_Modulate:        return TEXT("Modulate");
            case BLEND_AlphaComposite:  return TEXT("AlphaComposite");
            case BLEND_AlphaHoldout:    return TEXT("AlphaHoldout");
            default:                    return TEXT("Unknown");
        }
    }
}

bool UMCPPuerTSBridgeService::InspectMaterialJson(
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
        OutError = TEXT("Material inspection is limited to /Game and /Engine.");
        return false;
    }

    // Both spellings work: a package path (/Game/X/M_Y) and the object path the
    // other tools hand back (/Game/X/M_Y.M_Y).
    FString ObjectPath = AssetPath;
    FString PackagePath = AssetPath;
    if (!AssetPath.Contains(TEXT(".")))
    {
        if (!FPackageName::IsValidLongPackageName(AssetPath))
        {
            OutError = FString::Printf(TEXT("'%s' is not a valid package path."), *AssetPath);
            return false;
        }
        ObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    }
    else
    {
        PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
    }

    FAssetRegistryModule& AssetRegistryModule =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        return false;
    }
    UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(AssetData.GetAsset());
    if (MaterialInterface == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not a Material or Material Instance."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    UMaterial* Material = Cast<UMaterial>(MaterialInterface);
    UMaterialInstance* Instance = Cast<UMaterialInstance>(MaterialInterface);
    if (Material == nullptr && Instance == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, which is neither a UMaterial nor a UMaterialInstance."),
            *ObjectPath, *MaterialInterface->GetClass()->GetName());
        return false;
    }

    // The cleanliness contract, measured rather than asserted: loading an asset
    // to read it must not dirty it, and nothing below this line calls Modify,
    // MarkPackageDirty or a compile.
    const UPackage* Package = MaterialInterface->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    TArray<TSharedPtr<FJsonValue>> Unsupported;

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), MaterialInterface->GetPathName());
    Result->SetStringField(TEXT("asset_kind"),
        Material != nullptr ? TEXT("material") : TEXT("material_instance"));
    Result->SetStringField(TEXT("asset_class"), MaterialInterface->GetClass()->GetPathName());
    Result->SetStringField(TEXT("identity_kind"), TEXT("observed"));

    if (Material != nullptr)
    {
        Result->SetStringField(TEXT("domain"), MaterialDomainLabel(Material->MaterialDomain.GetValue()));
        Result->SetStringField(TEXT("blend_mode"), BlendModeLabel(Material->BlendMode.GetValue()));
        Result->SetBoolField(TEXT("two_sided"), Material->TwoSided != 0);
        Result->SetStringField(TEXT("parent_path"), FString());
        Result->SetStringField(TEXT("base_material_path"), Material->GetPathName());

        TArray<TSharedPtr<FJsonValue>> Expressions;
        TArray<TSharedPtr<FJsonValue>> Connections;
        TArray<TSharedPtr<FJsonValue>> MaterialInputs;
        MCPBridgeMaterialSnapshot::DescribeGraph(
            Material, Expressions, Connections, MaterialInputs, Unsupported);
        Result->SetArrayField(TEXT("expressions"), Expressions);
        Result->SetArrayField(TEXT("connections"), Connections);
        Result->SetArrayField(TEXT("material_inputs"), MaterialInputs);
        Result->SetNumberField(TEXT("expression_count"), Expressions.Num());
        Result->SetNumberField(TEXT("connection_count"), Connections.Num());
    }
    else
    {
        UMaterial* BaseMaterial = Instance->GetMaterial();
        Result->SetStringField(TEXT("parent_path"),
            Instance->Parent != nullptr ? Instance->Parent->GetPathName() : FString());
        Result->SetStringField(TEXT("base_material_path"),
            BaseMaterial != nullptr ? BaseMaterial->GetPathName() : FString());
        // A material instance has no expression graph of its own. The arrays
        // are present and empty rather than absent, so a caller can read the
        // same field names for either kind without branching on asset_kind.
        Result->SetArrayField(TEXT("expressions"), TArray<TSharedPtr<FJsonValue>>());
        Result->SetArrayField(TEXT("connections"), TArray<TSharedPtr<FJsonValue>>());
        Result->SetArrayField(TEXT("material_inputs"), TArray<TSharedPtr<FJsonValue>>());
        Result->SetNumberField(TEXT("expression_count"), 0);
        Result->SetNumberField(TEXT("connection_count"), 0);
        if (Instance->Parent == nullptr)
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("This material instance has no parent, so it publishes no parameters.")));
        }
    }

    const TArray<TSharedPtr<FJsonValue>> Parameters =
        MCPBridgeMaterialSnapshot::DescribeParameters(MaterialInterface);
    Result->SetArrayField(TEXT("parameters"), Parameters);
    Result->SetNumberField(TEXT("parameter_count"), Parameters.Num());

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this material dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetArrayField(TEXT("unsupported_fields"), Unsupported);
    Result->SetStringField(TEXT("structure_hash_sha1"),
        MCPBridgeMaterialSnapshot::StructureHash(MaterialInterface));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
