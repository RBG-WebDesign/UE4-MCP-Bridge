// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTreeBuilderLibrary.h"
#include "Json.h"
#include "MCPBridgeAILibrary.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
    /** Load the asset at ObjectPath as T, or create it in a new package.
        An existing asset of a different class is an error, never a retype. */
    template <typename T>
    T* LoadOrCreateBridgeAsset(
        const FString& PackagePath,
        const FString& ObjectPath,
        bool& bOutCreated,
        FString& OutError)
    {
        bOutCreated = false;
        FAssetRegistryModule& AssetRegistry =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FAssetData Existing = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
        if (Existing.IsValid())
        {
            T* Asset = Cast<T>(Existing.GetAsset());
            if (Asset == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("The asset at '%s' is a %s, not a %s."),
                    *ObjectPath, *Existing.AssetClass.ToString(), *T::StaticClass()->GetName());
            }
            return Asset;
        }
        UPackage* Package = CreatePackage(*PackagePath);
        if (Package == nullptr)
        {
            OutError = FString::Printf(TEXT("Unreal could not create the package '%s'."), *PackagePath);
            return nullptr;
        }
        const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
        T* Asset = NewObject<T>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
        if (Asset == nullptr)
        {
            OutError = FString::Printf(TEXT("Unreal could not create the asset '%s'."), *ObjectPath);
            return nullptr;
        }
        AssetRegistry.Get().AssetCreated(Asset);
        bOutCreated = true;
        return Asset;
    }
}

bool UMCPPuerTSBridgeService::BuildBehaviorTreeJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Behavior Tree build requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Behavior Tree spec must be a JSON object.");
        return false;
    }

    // --- Validation before any asset exists. ---

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Behavior Tree assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    FString BlackboardPath = AssetPath + TEXT("_BB");
    Spec->TryGetStringField(TEXT("blackboard_path"), BlackboardPath);
    if (!BlackboardPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(BlackboardPath))
    {
        OutError = TEXT("The blackboard is limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    // The root is required because a BehaviorTree without nodes is the empty
    // shell this builder exists to prevent.
    const TSharedPtr<FJsonObject>* Root = nullptr;
    if (!Spec->TryGetObjectField(TEXT("root"), Root))
    {
        OutError = TEXT("root is required: a Behavior Tree without nodes does nothing in PIE.");
        return false;
    }

    FString KeysJson = TEXT("[]");
    const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
    if (Spec->TryGetArrayField(TEXT("keys"), Keys))
    {
        for (const TSharedPtr<FJsonValue>& Value : *Keys)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every entry of keys must be an object with name and type.");
                return false;
            }
        }
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&KeysJson);
        FJsonSerializer::Serialize(*Keys, Writer);
    }

    const bool bSave = !Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        || Spec->GetBoolField(TEXT("save"));

    // --- Mutation starts here. ---

    bool bCreatedBlackboard = false;
    const FString BlackboardObjectPath =
        BlackboardPath + TEXT(".") + FPackageName::GetLongPackageAssetName(BlackboardPath);
    UBlackboardData* Blackboard = LoadOrCreateBridgeAsset<UBlackboardData>(
        BlackboardPath, BlackboardObjectPath, bCreatedBlackboard, OutError);
    if (Blackboard == nullptr)
    {
        return false;
    }
    Blackboard->Modify();

    bool bCreatedTree = false;
    const FString TreeObjectPath =
        AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    UBehaviorTree* Tree = LoadOrCreateBridgeAsset<UBehaviorTree>(
        AssetPath, TreeObjectPath, bCreatedTree, OutError);
    if (Tree == nullptr)
    {
        return false;
    }
    Tree->Modify();

    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    // The three protected operations, in dependency order, all through the
    // libraries already compiled into MCPBridgeGraphBuilder. Each answers an
    // empty string on success and the reason on failure.
    const FString KeysError = UMCPBridgeAILibrary::AddBlackboardKeys(Blackboard, KeysJson);
    if (!KeysError.IsEmpty())
    {
        Errors.Add(MakeShared<FJsonValueString>(KeysError));
    }
    const FString AssignError = UMCPBridgeAILibrary::SetBlackboardAsset(Tree, Blackboard);
    if (!AssignError.IsEmpty())
    {
        Errors.Add(MakeShared<FJsonValueString>(AssignError));
    }
    if (Errors.Num() == 0)
    {
        // The builder wants {"root": ...} and replaces the tree's root only on
        // full success, so a failed build leaves an existing tree untouched.
        FString BuildJson;
        {
            TSharedPtr<FJsonObject> BuildSpec = MakeShared<FJsonObject>();
            BuildSpec->SetObjectField(TEXT("root"), *Root);
            BuildJson = SerializeJson(BuildSpec);
        }
        const FString BuildError = UBehaviorTreeBuilderLibrary::BuildBehaviorTreeFromJSON(Tree, BuildJson);
        if (!BuildError.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(BuildError));
        }
    }

    Tree->MarkPackageDirty();
    Blackboard->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        if (Errors.Num() > 0)
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("Save was skipped: the Behavior Tree did not build cleanly.")));
        }
        else
        {
            FString SaveError;
            bSaved = SaveProjectAsset(TreeObjectPath, SaveError)
                && SaveProjectAsset(BlackboardObjectPath, SaveError);
            if (!bSaved)
            {
                Errors.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            }
        }
    }
    if (bCreatedTree || bCreatedBlackboard)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Asset creation is not undoable: undo restores level and actor state, not new packages.")));
    }

    // The keys that actually landed, read back from the asset rather than
    // echoed from the spec.
    TSharedPtr<FJsonValue> KeysOnAsset = MakeShared<FJsonValueNull>();
    {
        const FString ListedJson = UMCPBridgeAILibrary::ListBlackboardKeys(Blackboard);
        TSharedPtr<FJsonValue> Listed;
        TSharedRef<TJsonReader<>> ListedReader = TJsonReaderFactory<>::Create(ListedJson);
        if (FJsonSerializer::Deserialize(ListedReader, Listed) && Listed.IsValid())
        {
            KeysOnAsset = Listed;
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Tree->GetPathName());
    Result->SetStringField(TEXT("blackboard_path"), BlackboardPath);
    Result->SetStringField(TEXT("blackboard_object_path"), Blackboard->GetPathName());
    Result->SetBoolField(TEXT("created"), bCreatedTree);
    Result->SetBoolField(TEXT("created_blackboard"), bCreatedBlackboard);
    Result->SetBoolField(TEXT("has_root"),
        Tree->RootNode != nullptr);
    Result->SetField(TEXT("blackboard_keys"), KeysOnAsset);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
