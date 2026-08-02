// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTreeBuilderLibrary.h"
#include "Json.h"
#include "MCPBridgeAssetRollback.h"
#include "ScopedTransaction.h"
#include "JsonObjectConverter.h"
#include "MCPBridgeAILibrary.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

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
    //
    // Everything below this line is inside a rollback boundary. The engine
    // validation that decides whether the spec was legal at all can only run
    // against a real UBehaviorTree, so the assets have to exist before the
    // answer is known. When the answer is no, Rollback undoes the creation; a
    // failed build must not leave an asset in the Asset Registry or a dirty
    // package for the save-on-exit flow to write during close.
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(BlackboardPath);
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    // Cancel the transaction first, then undo the creations: the transaction's
    // undo records reference these objects, and must be replayed while they are
    // still live.
    auto FailWithRollback = [&](const FString& Error) -> bool
    {
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        Rollback.Rollback();
        OutError = Error;
        return false;
    };

    bool bCreatedBlackboard = false;
    const FString BlackboardObjectPath =
        BlackboardPath + TEXT(".") + FPackageName::GetLongPackageAssetName(BlackboardPath);
    UBlackboardData* Blackboard = LoadOrCreateBridgeAsset<UBlackboardData>(
        BlackboardPath, BlackboardObjectPath, bCreatedBlackboard, OutError);
    if (Blackboard == nullptr)
    {
        return FailWithRollback(OutError);
    }
    if (bCreatedBlackboard)
    {
        Rollback.TrackCreated(Blackboard);
    }
    Blackboard->Modify();

    bool bCreatedTree = false;
    const FString TreeObjectPath =
        AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    UBehaviorTree* Tree = LoadOrCreateBridgeAsset<UBehaviorTree>(
        AssetPath, TreeObjectPath, bCreatedTree, OutError);
    if (Tree == nullptr)
    {
        return FailWithRollback(OutError);
    }
    if (bCreatedTree)
    {
        Rollback.TrackCreated(Tree);
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

    // The failure exit, shared by the build and the save. Previously both
    // packages were dirtied unconditionally below, which is what put a failed
    // build in the Save Content prompt and wrote it to disk during close even
    // when the prompt was answered "Don't Save".
    auto FailRolledBack = [&]() -> bool
    {
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }
        Rollback.Rollback();

        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetStringField(TEXT("blackboard_path"), BlackboardPath);
        Failed->SetBoolField(TEXT("created"), false);
        Failed->SetBoolField(TEXT("created_blackboard"), false);
        Failed->SetBoolField(TEXT("has_root"), false);
        Failed->SetBoolField(TEXT("saved"), false);
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and was rolled back: no asset, package, or file remains.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        Failed->SetObjectField(TEXT("cleanup"),
            Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    if (Errors.Num() > 0)
    {
        return FailRolledBack();
    }

    Tree->MarkPackageDirty();
    Blackboard->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(TreeObjectPath, SaveError)
            && SaveProjectAsset(BlackboardObjectPath, SaveError);
        if (!bSaved)
        {
            // A half-written asset is the same hazard as a half-built one.
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            return FailRolledBack();
        }
    }

    // Past every failure exit: the request succeeded, so keep what it made.
    Rollback.Commit();
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
    // Always present, so a caller never has to distinguish "clean" from
    // "this build predates the rollback boundary".
    Result->SetObjectField(TEXT("cleanup"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    OutResultJson = SerializeJson(Result);
    return true;
}

// ---------------------------------------------------------------------------
// Read-only Behavior Tree inspection.
//
// Donor references (design only, no code copied; both MIT):
// - jeebus87/ultimateunrealenginemcp @ c31ebac,
//   unreal-plugin/MCPBridge/Source/MCPBridgeEditor/Private/MCPAICommands.cpp,
//   BuildBTCompositeNodeJson / ai.behaviorTree: recursive walk shape and the
//   /Game-/Engine path guard. Its decorator arrays flatten every child's
//   decorators onto the composite; this implementation keeps each decorator
//   on the child it guards, because that is what behavior_tree_build authors.
// - tumourlove/monolith @ e67544c,
//   Source/MonolithAI/Private/MonolithAIBehaviorTreeActions.cpp,
//   FMonolithAIBehaviorTreeActions::HandleGetBehaviorTree: the
//   export-mirrors-build round-trip contract.
//
// UE4.27 BT nodes carry no GUID, so node identity here is DERIVED: the
// traversal path (parent id / child index : class : name), labeled
// identity_kind = "derived" in the response. A renamed or reordered node is a
// different identity on purpose - the identity IS the structure.
// ---------------------------------------------------------------------------
namespace
{
    struct FBTWalkContext
    {
        TArray<TSharedPtr<FJsonValue>> UnsupportedFields;
        TSet<FString> ReferencedKeys;
        FString HashLines;
        int32 Composites = 0;
        int32 Tasks = 0;
        int32 Decorators = 0;
        int32 Services = 0;
    };

    /** Editor-visible properties of one node. Structural fields are the tree
        itself and are skipped; FBlackboardKeySelector fields are lifted into
        blackboard_keys; a property FJsonObjectConverter cannot express is
        recorded in unsupported_fields rather than silently dropped. */
    void AddNodeDetail(
        UBTNode* Node,
        const FString& NodeId,
        const TSharedPtr<FJsonObject>& Out,
        FBTWalkContext& Context)
    {
        static const TSet<FName> StructuralFields = {
            FName(TEXT("Children")), FName(TEXT("Services")),
            FName(TEXT("RootNode")), FName(TEXT("RootDecorators")),
            FName(TEXT("RootDecoratorOps")),
        };
        TArray<TSharedPtr<FJsonValue>> Keys;
        TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
        {
            FProperty* Property = *It;
            if (!Property->HasAnyPropertyFlags(CPF_Edit)
                || StructuralFields.Contains(Property->GetFName()))
            {
                continue;
            }
            if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                if (StructProperty->Struct == FBlackboardKeySelector::StaticStruct())
                {
                    const FBlackboardKeySelector* Selector =
                        StructProperty->ContainerPtrToValuePtr<FBlackboardKeySelector>(Node);
                    if (Selector != nullptr && !Selector->SelectedKeyName.IsNone())
                    {
                        const FString KeyName = Selector->SelectedKeyName.ToString();
                        Keys.Add(MakeShared<FJsonValueString>(KeyName));
                        Context.ReferencedKeys.Add(KeyName);
                    }
                    continue;
                }
            }
            if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
            {
                if (ObjectProperty->PropertyClass != nullptr
                    && ObjectProperty->PropertyClass->IsChildOf(UBTNode::StaticClass()))
                {
                    continue;
                }
            }
            TSharedPtr<FJsonValue> Value = FJsonObjectConverter::UPropertyToJsonValue(
                Property, Property->ContainerPtrToValuePtr<void>(Node), 0, 0);
            if (Value.IsValid())
            {
                Properties->SetField(Property->GetName(), Value);
            }
            else
            {
                Context.UnsupportedFields.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("%s.%s"), *NodeId, *Property->GetName())));
            }
        }
        Out->SetArrayField(TEXT("blackboard_keys"), Keys);
        Out->SetObjectField(TEXT("properties"), Properties);

        // The canonical structure line: identity, class, name, referenced
        // keys, in traversal order. Properties are deliberately not hashed -
        // this is a STRUCTURE hash.
        Context.HashLines += NodeId + TEXT("|") + Node->GetClass()->GetPathName()
            + TEXT("|") + Node->GetNodeName();
        for (const TSharedPtr<FJsonValue>& Key : Keys)
        {
            Context.HashLines += TEXT("|") + Key->AsString();
        }
        Context.HashLines += TEXT("\n");
    }

    TSharedPtr<FJsonObject> DescribeLeaf(
        UBTNode* Node,
        const FString& NodeId,
        const TCHAR* Kind,
        FBTWalkContext& Context)
    {
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("id"), NodeId);
        Out->SetStringField(TEXT("identity"), TEXT("derived"));
        Out->SetStringField(TEXT("kind"), Kind);
        Out->SetStringField(TEXT("name"), Node->GetNodeName());
        Out->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
        AddNodeDetail(Node, NodeId, Out, Context);
        return Out;
    }

    TArray<TSharedPtr<FJsonValue>> DescribeDecorators(
        const TArray<UBTDecorator*>& Decorators,
        const FString& OwnerId,
        FBTWalkContext& Context)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (int32 Index = 0; Index < Decorators.Num(); ++Index)
        {
            if (Decorators[Index] == nullptr) { continue; }
            Context.Decorators++;
            Out.Add(MakeShared<FJsonValueObject>(DescribeLeaf(
                Decorators[Index],
                FString::Printf(TEXT("%s/dec%d"), *OwnerId, Index),
                TEXT("decorator"), Context)));
        }
        return Out;
    }

    TArray<TSharedPtr<FJsonValue>> DescribeServices(
        const TArray<UBTService*>& Services,
        const FString& OwnerId,
        FBTWalkContext& Context)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (int32 Index = 0; Index < Services.Num(); ++Index)
        {
            if (Services[Index] == nullptr) { continue; }
            Context.Services++;
            Out.Add(MakeShared<FJsonValueObject>(DescribeLeaf(
                Services[Index],
                FString::Printf(TEXT("%s/svc%d"), *OwnerId, Index),
                TEXT("service"), Context)));
        }
        return Out;
    }

    TSharedPtr<FJsonObject> DescribeComposite(
        UBTCompositeNode* Composite,
        const FString& NodeId,
        int32 Depth,
        FBTWalkContext& Context)
    {
        Context.Composites++;
        TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
        Out->SetStringField(TEXT("id"), NodeId);
        Out->SetStringField(TEXT("identity"), TEXT("derived"));
        Out->SetStringField(TEXT("kind"), TEXT("composite"));
        Out->SetStringField(TEXT("name"), Composite->GetNodeName());
        Out->SetStringField(TEXT("class_path"), Composite->GetClass()->GetPathName());
        AddNodeDetail(Composite, NodeId, Out, Context);
        Out->SetArrayField(TEXT("services"), DescribeServices(Composite->Services, NodeId, Context));

        TArray<TSharedPtr<FJsonValue>> ChildValues;
        if (Depth >= 64)
        {
            Context.UnsupportedFields.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("%s.children (depth limit 64 reached)"), *NodeId)));
        }
        else
        {
            for (int32 Index = 0; Index < Composite->Children.Num(); ++Index)
            {
                const FBTCompositeChild& Child = Composite->Children[Index];
                UBTNode* ChildNode = Child.ChildComposite != nullptr
                    ? static_cast<UBTNode*>(Child.ChildComposite)
                    : static_cast<UBTNode*>(Child.ChildTask);
                if (ChildNode == nullptr) { continue; }
                const FString ChildId = FString::Printf(TEXT("%s/%d:%s:%s"),
                    *NodeId, Index, *ChildNode->GetClass()->GetName(), *ChildNode->GetNodeName());
                TSharedPtr<FJsonObject> ChildJson;
                if (Child.ChildComposite != nullptr)
                {
                    ChildJson = DescribeComposite(Child.ChildComposite, ChildId, Depth + 1, Context);
                }
                else
                {
                    Context.Tasks++;
                    ChildJson = DescribeLeaf(ChildNode, ChildId, TEXT("task"), Context);
                    ChildJson->SetArrayField(TEXT("services"),
                        DescribeServices(Child.ChildTask->Services, ChildId, Context));
                }
                ChildJson->SetNumberField(TEXT("child_index"), Index);
                // Decorators stay attached to the child they guard.
                ChildJson->SetArrayField(TEXT("decorators"),
                    DescribeDecorators(Child.Decorators, ChildId, Context));
                ChildValues.Add(MakeShared<FJsonValueObject>(ChildJson));
            }
        }
        Out->SetArrayField(TEXT("children"), ChildValues);
        return Out;
    }
}

bool UMCPPuerTSBridgeService::InspectBehaviorTreeJson(
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
    // Reading is allowed anywhere under the two content roots, like every
    // other read in this bridge; authoring stays limited to /Game/MCPGenerated/.
    if (!AssetPath.StartsWith(TEXT("/Game/")) && !AssetPath.StartsWith(TEXT("/Engine/")))
    {
        OutError = TEXT("Behavior Tree inspection is limited to /Game and /Engine.");
        return false;
    }

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

    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData AssetData = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    if (!AssetData.IsValid())
    {
        OutError = FString::Printf(TEXT("No asset was found at '%s'."), *ObjectPath);
        return false;
    }
    UBehaviorTree* Tree = Cast<UBehaviorTree>(AssetData.GetAsset());
    if (Tree == nullptr)
    {
        OutError = FString::Printf(
            TEXT("The asset at '%s' is a %s, not a BehaviorTree."),
            *ObjectPath, *AssetData.AssetClass.ToString());
        return false;
    }

    // Read-only, measured rather than asserted: the command is not in
    // IsToolMutating so no transaction opens, nothing below calls Modify or
    // MarkPackageDirty, and both dirty readings are reported.
    const UPackage* Package = Tree->GetOutermost();
    const bool bDirtyBefore = Package != nullptr && Package->IsDirty();

    TArray<TSharedPtr<FJsonValue>> Warnings;
    FBTWalkContext Context;

    TSharedPtr<FJsonValue> Root = MakeShared<FJsonValueNull>();
    if (Tree->RootNode != nullptr)
    {
        Root = MakeShared<FJsonValueObject>(
            DescribeComposite(Tree->RootNode, TEXT("root"), 0, Context));
    }
    else
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Behavior Tree has no root node.")));
    }
    const TArray<TSharedPtr<FJsonValue>> RootDecorators =
        DescribeDecorators(Tree->RootDecorators, TEXT("root"), Context);

    TSharedPtr<FJsonValue> BlackboardKeys = MakeShared<FJsonValueNull>();
    if (Tree->BlackboardAsset != nullptr)
    {
        const FString KeysJson = UMCPBridgeAILibrary::ListBlackboardKeys(Tree->BlackboardAsset);
        TSharedPtr<FJsonValue> Parsed;
        TSharedRef<TJsonReader<>> KeysReader = TJsonReaderFactory<>::Create(KeysJson);
        if (FJsonSerializer::Deserialize(KeysReader, Parsed) && Parsed.IsValid())
        {
            BlackboardKeys = Parsed;
        }
        else
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("The blackboard key reader returned JSON that could not be parsed.")));
        }
    }
    else
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("This Behavior Tree has no Blackboard assigned.")));
    }

    uint8 Sha1[FSHA1::DigestSize];
    const FTCHARToUTF8 HashUtf8(*Context.HashLines);
    FSHA1::HashBuffer(HashUtf8.Get(), HashUtf8.Length(), Sha1);

    const bool bDirtyAfter = Package != nullptr && Package->IsDirty();
    if (bDirtyAfter && !bDirtyBefore)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Reading this Behavior Tree dirtied its package. That is a defect in the "
                 "inspector, not a property of the asset: report it.")));
    }

    TArray<TSharedPtr<FJsonValue>> ReferencedKeys;
    {
        TArray<FString> Sorted = Context.ReferencedKeys.Array();
        Sorted.Sort();
        for (const FString& Key : Sorted)
        {
            ReferencedKeys.Add(MakeShared<FJsonValueString>(Key));
        }
    }

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), PackagePath);
    Result->SetStringField(TEXT("object_path"), Tree->GetPathName());
    Result->SetStringField(TEXT("behavior_tree_class"), Tree->GetClass()->GetPathName());
    Result->SetStringField(TEXT("blackboard_path"),
        Tree->BlackboardAsset != nullptr ? Tree->BlackboardAsset->GetPathName() : FString());
    Result->SetBoolField(TEXT("package_dirty_before"), bDirtyBefore);
    Result->SetBoolField(TEXT("package_dirty_after"), bDirtyAfter);
    Result->SetStringField(TEXT("identity_kind"), TEXT("derived"));
    Result->SetField(TEXT("root"), Root);
    Result->SetArrayField(TEXT("root_decorators"), RootDecorators);
    Result->SetNumberField(TEXT("composite_count"), Context.Composites);
    Result->SetNumberField(TEXT("task_count"), Context.Tasks);
    Result->SetNumberField(TEXT("decorator_count"), Context.Decorators);
    Result->SetNumberField(TEXT("service_count"), Context.Services);
    Result->SetField(TEXT("blackboard_keys"), BlackboardKeys);
    Result->SetArrayField(TEXT("referenced_blackboard_keys"), ReferencedKeys);
    Result->SetArrayField(TEXT("unsupported_fields"), Context.UnsupportedFields);
    Result->SetStringField(TEXT("structure_hash_sha1"), BytesToHex(Sha1, FSHA1::DigestSize));
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
