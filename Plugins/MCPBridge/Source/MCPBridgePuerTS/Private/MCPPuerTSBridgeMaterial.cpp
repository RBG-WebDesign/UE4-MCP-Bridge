// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Factories/MaterialFactoryNew.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTime.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

bool UMCPPuerTSBridgeService::CreateAuroraSkyMaterialJson(
    const FString& AssetPath,
    const FString& SkyActorName,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Sky shader creation requires an active transaction.");
        return false;
    }
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Sky shader assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    AActor* SkyActor = FindLevelActor(SkyActorName);
    if (SkyActor == nullptr)
    {
        OutError = TEXT("Sky actor was not found.");
        return false;
    }
    TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
    SkyActor->GetComponents(MeshComponents);
    if (MeshComponents.Num() == 0
        || MeshComponents[0] == nullptr
        || MeshComponents[0]->GetStaticMesh() == nullptr)
    {
        OutError = TEXT("Sky actor has no usable static mesh component.");
        return false;
    }

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingAsset =
        AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UMaterial* Material =
        ExistingAsset.IsValid() ? Cast<UMaterial>(ExistingAsset.GetAsset()) : nullptr;
    if (ExistingAsset.IsValid() && Material == nullptr)
    {
        OutError = TEXT("An existing non-material asset uses the requested path.");
        return false;
    }

    const bool bCreatedMaterial = Material == nullptr;
    if (bCreatedMaterial)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
        Material = Package != nullptr
            ? Cast<UMaterial>(Factory->FactoryCreateNew(
                UMaterial::StaticClass(),
                Package,
                FName(*AssetName),
                RF_Public | RF_Standalone | RF_Transactional,
                nullptr,
                GWarn))
            : nullptr;
        if (Material == nullptr)
        {
            OutError = TEXT("Unreal could not create the sky shader material.");
            return false;
        }
    }

    if (bCreatedMaterial)
    {
        Material->Modify();
    Material->MaterialDomain = MD_Surface;
    Material->BlendMode = BLEND_Opaque;
    Material->SetShadingModel(MSM_Unlit);
    Material->TwoSided = true;
    Material->bIsSky = true;

    UMaterialExpressionTextureCoordinate* UV =
        NewObject<UMaterialExpressionTextureCoordinate>(Material, NAME_None, RF_Transactional);
    UV->MaterialExpressionEditorX = -620;
    UV->MaterialExpressionEditorY = -80;

    UMaterialExpressionTime* Time =
        NewObject<UMaterialExpressionTime>(Material, NAME_None, RF_Transactional);
    Time->bIgnorePause = true;
    Time->MaterialExpressionEditorX = -620;
    Time->MaterialExpressionEditorY = 100;

    UMaterialExpressionCustom* Aurora =
        NewObject<UMaterialExpressionCustom>(Material, NAME_None, RF_Transactional);
    Aurora->Description = TEXT("Native PuerTS Aurora Nebula Sky");
    Aurora->OutputType = CMOT_Float3;
    Aurora->MaterialExpressionEditorX = -280;
    Aurora->MaterialExpressionEditorY = 0;
    Aurora->Code =
        TEXT("float2 p=UV*2.0-1.0;")
        TEXT("float t=T*0.08;")
        TEXT("float h=saturate(1.0-abs(p.y));")
        TEXT("float a=sin(p.x*15.0+sin(p.y*9.0-t*5.0)*2.0+t*3.0);")
        TEXT("a=pow(saturate(a*0.5+0.5),8.0)*pow(h,2.2);")
        TEXT("float b=pow(saturate(sin(p.x*9.0-p.y*13.0+t*2.1)*0.5+0.5),14.0)*h;")
        TEXT("float2 cell=floor(UV*float2(900.0,450.0));")
        TEXT("float hash=frac(sin(dot(cell,float2(12.9898,78.233)))*43758.5453);")
        TEXT("float stars=step(0.9975,hash)*(0.5+2.5*frac(hash*91.7));")
        TEXT("float3 base=lerp(float3(0.002,0.004,0.025),float3(0.055,0.005,0.12),h);")
        TEXT("float3 glow=a*float3(0.02,1.8,0.75)+b*float3(0.65,0.05,1.5);")
        TEXT("return base+glow+float3(stars,stars,stars);");

    FCustomInput UVInput;
    UVInput.InputName = TEXT("UV");
    UVInput.Input.Connect(0, UV);
    Aurora->Inputs.Add(UVInput);

    FCustomInput TimeInput;
    TimeInput.InputName = TEXT("T");
    TimeInput.Input.Connect(0, Time);
    Aurora->Inputs.Add(TimeInput);

    Material->Expressions.Add(UV);
    Material->Expressions.Add(Time);
    Material->Expressions.Add(Aurora);
    Material->EmissiveColor.Connect(0, Aurora);
    Material->PostEditChange();
    Material->MarkPackageDirty();
        AssetRegistry.AssetCreated(Material);
    }

    UStaticMeshComponent* SourceSkyMesh = MeshComponents[0];
    FTransform DesiredSkyTransform = SourceSkyMesh->GetComponentTransform();
    if (SkyActor->GetActorScale3D().GetAbsMax() < 0.01f)
    {
        DesiredSkyTransform.SetScale3D(SourceSkyMesh->GetRelativeScale3D());
    }
    AStaticMeshActor* PersistentSkyActor =
        Cast<AStaticMeshActor>(FindLevelActor(TEXT("MCP_NativeAuroraSkybox")));
    bool bCreatedSkyActor = false;
    if (PersistentSkyActor == nullptr)
    {
        UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (World == nullptr || World->GetCurrentLevel() == nullptr)
        {
            OutError = TEXT("Editor world is unavailable for the persistent skybox.");
            return false;
        }
        PersistentSkyActor = Cast<AStaticMeshActor>(GEditor->AddActor(
            World->GetCurrentLevel(),
            AStaticMeshActor::StaticClass(),
            SourceSkyMesh->GetComponentTransform(),
            true,
            RF_Transactional));
        if (PersistentSkyActor == nullptr)
        {
            OutError = TEXT("Unreal could not create the persistent skybox actor.");
            return false;
        }
        PersistentSkyActor->SetActorLabel(TEXT("MCP_NativeAuroraSkybox"));
        bCreatedSkyActor = true;
    }

    PersistentSkyActor->Modify();
    PersistentSkyActor->SetActorTransform(DesiredSkyTransform);
    UStaticMeshComponent* SkyMesh = PersistentSkyActor->GetStaticMeshComponent();
    if (SkyMesh == nullptr)
    {
        OutError = TEXT("Persistent skybox has no static mesh component.");
        return false;
    }

    SkyMesh->Modify();
    SkyMesh->SetStaticMesh(SourceSkyMesh->GetStaticMesh());
    SkyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SkyMesh->SetCastShadow(false);
    SkyMesh->SetMaterial(0, Material);
    PersistentSkyActor->MarkPackageDirty();

    SkyActor->Modify();
    SkyActor->SetActorScale3D(FVector(0.000001f));
    SkyActor->SetActorHiddenInGame(true);
    SkyActor->MarkPackageDirty();

    UMaterialInterface* AppliedMaterial = SkyMesh->GetMaterial(0);
    if (AppliedMaterial != Material)
    {
        OutError = TEXT("Persistent skybox rejected the generated material.");
        return false;
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("material_path"), Material->GetPathName());
    Result->SetStringField(TEXT("applied_material_path"), AppliedMaterial->GetPathName());
    Result->SetStringField(TEXT("actor_path"), PersistentSkyActor->GetPathName());
    Result->SetStringField(TEXT("original_actor_path"), SkyActor->GetPathName());
    Result->SetStringField(TEXT("component_path"), SkyMesh->GetPathName());
    Result->SetStringField(TEXT("effect"), TEXT("animated aurora bands, nebula gradient, and procedural stars"));
    Result->SetNumberField(TEXT("material_expression_count"), Material->Expressions.Num());
    Result->SetBoolField(TEXT("created"), bCreatedMaterial);
    Result->SetBoolField(TEXT("sky_actor_created"), bCreatedSkyActor);
    Result->SetBoolField(TEXT("compile_requested"), bCreatedMaterial);
    OutResultJson = SerializeJson(Result);
    return true;
}