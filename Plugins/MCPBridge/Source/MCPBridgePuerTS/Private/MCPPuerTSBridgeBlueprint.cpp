// Copyright 2026 RareBird Games. All Rights Reserved.

#include "MCPPuerTSBridgeService.h"

#include "AssetRegistryModule.h"
#include "BlueprintGraphBuilderLibrary.h"
#include "Components/ActorComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Json.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

namespace
{
    /** Resolve a class from either a short reflected name ("Actor",
        "StaticMeshComponent") or a full object path ("/Script/Engine.Actor",
        "/Game/MCPGenerated/BP_Probe.BP_Probe_C"). Short names carry no
        prefix in the reflection database, so ANY_PACKAGE lookup is the 4.27
        way to reach AActor from "Actor". */
    UClass* ResolveBuilderClass(const FString& Name)
    {
        if (Name.IsEmpty())
        {
            return nullptr;
        }
        if (Name.Contains(TEXT(".")) || Name.StartsWith(TEXT("/")))
        {
            return LoadObject<UClass>(nullptr, *Name);
        }
        return FindObject<UClass>(ANY_PACKAGE, *Name);
    }

    USCS_Node* FindComponentNode(const UBlueprint* Blueprint, const FString& ComponentName)
    {
        if (Blueprint == nullptr || Blueprint->SimpleConstructionScript == nullptr)
        {
            return nullptr;
        }
        for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
        {
            if (Node != nullptr && Node->GetVariableName().ToString() == ComponentName)
            {
                return Node;
            }
        }
        return nullptr;
    }

    /** One entry of the validated components list. Validation runs to
        completion before a single SCS node is created, so the build loop
        cannot fail halfway and leave an asset with some of its components. */
    struct FValidatedComponent
    {
        FString Name;
        FString AttachTo;
        UClass* Class = nullptr;
    };

    FString JoinStrings(const TArray<FString>& Values)
    {
        return FString::Join(Values, TEXT(", "));
    }
}

bool UMCPPuerTSBridgeService::BuildBlueprintJson(
    const FString& SpecJson,
    FString& OutResultJson,
    FString& OutError)
{
    if (ActiveTransaction == nullptr)
    {
        OutError = TEXT("Blueprint build requires an active transaction.");
        return false;
    }

    TSharedPtr<FJsonObject> Spec;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
    if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
    {
        OutError = TEXT("Blueprint spec must be a JSON object.");
        return false;
    }

    // --- Validation. Everything that can be checked without the asset is
    // checked here, before the asset exists, so a rejected request leaves the
    // project exactly as it was. ---

    FString AssetPath;
    Spec->TryGetStringField(TEXT("asset_path"), AssetPath);
    if (!AssetPath.StartsWith(TEXT("/Game/MCPGenerated/"))
        || !FPackageName::IsValidLongPackageName(AssetPath))
    {
        OutError = TEXT("Blueprint assets are limited to valid paths under /Game/MCPGenerated/.");
        return false;
    }

    FString ParentClassName = TEXT("Actor");
    Spec->TryGetStringField(TEXT("parent_class"), ParentClassName);
    UClass* ParentClass = ResolveBuilderClass(ParentClassName);
    if (ParentClass == nullptr)
    {
        OutError = FString::Printf(TEXT("Parent class was not found: %s"), *ParentClassName);
        return false;
    }
    if (!ParentClass->IsChildOf(AActor::StaticClass()))
    {
        OutError = FString::Printf(
            TEXT("Parent class must derive from Actor: %s does not."), *ParentClass->GetPathName());
        return false;
    }
    if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        OutError = FString::Printf(
            TEXT("Unreal refuses Blueprints of class %s."), *ParentClass->GetPathName());
        return false;
    }

    TArray<FValidatedComponent> Components;
    const TArray<TSharedPtr<FJsonValue>>* ComponentSpecs = nullptr;
    if (Spec->TryGetArrayField(TEXT("components"), ComponentSpecs))
    {
        TSet<FString> ComponentNames;
        for (const TSharedPtr<FJsonValue>& Value : *ComponentSpecs)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every entry of components must be an object.");
                return false;
            }
            FValidatedComponent Component;
            FString ClassName;
            (*Entry)->TryGetStringField(TEXT("class"), ClassName);
            (*Entry)->TryGetStringField(TEXT("name"), Component.Name);
            (*Entry)->TryGetStringField(TEXT("attach_to"), Component.AttachTo);
            if (Component.Name.IsEmpty())
            {
                OutError = TEXT("Every component needs a non-empty name.");
                return false;
            }
            if (ComponentNames.Contains(Component.Name))
            {
                OutError = FString::Printf(TEXT("Duplicate component name in the spec: %s"), *Component.Name);
                return false;
            }
            ComponentNames.Add(Component.Name);
            Component.Class = ResolveBuilderClass(ClassName);
            if (Component.Class == nullptr)
            {
                OutError = FString::Printf(TEXT("Component class was not found: %s"), *ClassName);
                return false;
            }
            if (!Component.Class->IsChildOf(UActorComponent::StaticClass()))
            {
                OutError = FString::Printf(
                    TEXT("Component class must derive from ActorComponent: %s does not."),
                    *Component.Class->GetPathName());
                return false;
            }
            Components.Add(Component);
        }
    }

    const TSharedPtr<FJsonObject>* GraphObject = nullptr;
    const bool bHasGraph = Spec->TryGetObjectField(TEXT("graph"), GraphObject);
    TArray<FString> RequestedNodeTypes;
    int32 ConnectionCount = 0;
    if (bHasGraph)
    {
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (!(*GraphObject)->TryGetArrayField(TEXT("nodes"), Nodes))
        {
            OutError = TEXT("graph.nodes must be an array.");
            return false;
        }
        const TArray<FString> Supported = UBlueprintGraphBuilderLibrary::GetSupportedNodeTypes();
        TArray<FString> Unsupported;
        TSet<FString> NodeIds;
        for (const TSharedPtr<FJsonValue>& Value : *Nodes)
        {
            const TSharedPtr<FJsonObject>* Entry = nullptr;
            if (!Value->TryGetObject(Entry))
            {
                OutError = TEXT("Every entry of graph.nodes must be an object.");
                return false;
            }
            FString NodeId;
            FString NodeType;
            (*Entry)->TryGetStringField(TEXT("id"), NodeId);
            (*Entry)->TryGetStringField(TEXT("type"), NodeType);
            if (NodeId.IsEmpty() || NodeType.IsEmpty())
            {
                OutError = TEXT("Every graph node needs a non-empty id and type.");
                return false;
            }
            if (NodeIds.Contains(NodeId))
            {
                OutError = FString::Printf(TEXT("Duplicate graph node id: %s"), *NodeId);
                return false;
            }
            NodeIds.Add(NodeId);
            if (!Supported.Contains(NodeType))
            {
                Unsupported.AddUnique(NodeType);
            }
            RequestedNodeTypes.Add(NodeType);
        }
        if (Unsupported.Num() > 0)
        {
            OutError = FString::Printf(
                TEXT("Unsupported graph node type(s): %s. The builder supports: %s."),
                *JoinStrings(Unsupported), *JoinStrings(Supported));
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
        if ((*GraphObject)->TryGetArrayField(TEXT("connections"), Connections))
        {
            for (const TSharedPtr<FJsonValue>& Value : *Connections)
            {
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                if (!Value->TryGetObject(Entry))
                {
                    OutError = TEXT("Every entry of graph.connections must be an object.");
                    return false;
                }
                FString From;
                FString To;
                (*Entry)->TryGetStringField(TEXT("from"), From);
                (*Entry)->TryGetStringField(TEXT("to"), To);
                FString FromId;
                FString FromPin;
                FString ToId;
                FString ToPin;
                if (!From.Split(TEXT("."), &FromId, &FromPin) || !To.Split(TEXT("."), &ToId, &ToPin))
                {
                    OutError = FString::Printf(
                        TEXT("Connection endpoints must read nodeId.pinRole: '%s' -> '%s'."), *From, *To);
                    return false;
                }
                if (!NodeIds.Contains(FromId) || !NodeIds.Contains(ToId))
                {
                    OutError = FString::Printf(
                        TEXT("Connection references an unknown node id: '%s' -> '%s'."), *From, *To);
                    return false;
                }
                ConnectionCount++;
            }
        }
    }

    const bool bClearExistingGraph = Spec->HasTypedField<EJson::Boolean>(TEXT("clear_existing_graph"))
        ? Spec->GetBoolField(TEXT("clear_existing_graph"))
        : true;
    const bool bCompile = Spec->HasTypedField<EJson::Boolean>(TEXT("compile"))
        ? Spec->GetBoolField(TEXT("compile"))
        : true;
    const bool bSave = Spec->HasTypedField<EJson::Boolean>(TEXT("save"))
        ? Spec->GetBoolField(TEXT("save"))
        : true;

    // --- Resolve the asset. ---

    const FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
    const FString ObjectPath = AssetPath + TEXT(".") + AssetName;
    FAssetRegistryModule& AssetRegistry =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    const FAssetData ExistingAsset = AssetRegistry.Get().GetAssetByObjectPath(FName(*ObjectPath));
    UBlueprint* Blueprint = ExistingAsset.IsValid() ? Cast<UBlueprint>(ExistingAsset.GetAsset()) : nullptr;
    if (ExistingAsset.IsValid() && Blueprint == nullptr)
    {
        OutError = TEXT("An existing non-Blueprint asset uses the requested path.");
        return false;
    }

    // The last checks that need the asset. A rerun that asks for a different
    // parent, or for a different class under a component name the Blueprint
    // already uses, is a mistake rather than an update.
    if (Blueprint != nullptr)
    {
        if (Blueprint->ParentClass != ParentClass)
        {
            OutError = FString::Printf(
                TEXT("Existing Blueprint has parent class %s; the spec asks for %s. Reparenting is not done implicitly."),
                Blueprint->ParentClass != nullptr ? *Blueprint->ParentClass->GetPathName() : TEXT("none"),
                *ParentClass->GetPathName());
            return false;
        }
        for (const FValidatedComponent& Component : Components)
        {
            const USCS_Node* Node = FindComponentNode(Blueprint, Component.Name);
            if (Node != nullptr && Node->ComponentClass != Component.Class)
            {
                OutError = FString::Printf(
                    TEXT("Component '%s' already exists as %s; the spec asks for %s."),
                    *Component.Name,
                    Node->ComponentClass != nullptr ? *Node->ComponentClass->GetPathName() : TEXT("none"),
                    *Component.Class->GetPathName());
                return false;
            }
        }
    }

    // An attach target may be declared earlier in this spec or already present
    // on the Blueprint. Anything else would silently fall back to the root,
    // which is a hierarchy the caller did not ask for.
    {
        TSet<FString> AvailableParents;
        for (const FValidatedComponent& Component : Components)
        {
            if (!Component.AttachTo.IsEmpty()
                && !AvailableParents.Contains(Component.AttachTo)
                && FindComponentNode(Blueprint, Component.AttachTo) == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("Component '%s' attaches to '%s', which is neither declared earlier in components nor already on the Blueprint."),
                    *Component.Name, *Component.AttachTo);
                return false;
            }
            AvailableParents.Add(Component.Name);
        }
    }

    // --- Mutation starts here. ---

    const bool bCreated = Blueprint == nullptr;
    if (bCreated)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        if (Package == nullptr)
        {
            OutError = TEXT("Unreal could not create the Blueprint package.");
            return false;
        }
        Blueprint = FKismetEditorUtilities::CreateBlueprint(
            ParentClass,
            Package,
            FName(*AssetName),
            BPTYPE_Normal,
            UBlueprint::StaticClass(),
            UBlueprintGeneratedClass::StaticClass(),
            FName(TEXT("MCPPuerTSBridge")));
        if (Blueprint == nullptr)
        {
            OutError = TEXT("Unreal could not create the Blueprint asset.");
            return false;
        }
        AssetRegistry.Get().AssetCreated(Blueprint);
    }

    Blueprint->Modify();

    TArray<TSharedPtr<FJsonValue>> ComponentResults;
    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    for (const FValidatedComponent& Component : Components)
    {
        const bool bComponentExisted = FindComponentNode(Blueprint, Component.Name) != nullptr;
        if (!bComponentExisted
            && !UBlueprintGraphBuilderLibrary::AddComponentToBlueprint(
                Blueprint, Component.Class, Component.Name, Component.AttachTo))
        {
            Errors.Add(MakeShared<FJsonValueString>(
                FString::Printf(TEXT("Component '%s' could not be added."), *Component.Name)));
            continue;
        }
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Component.Name);
        Result->SetStringField(TEXT("class"), Component.Class->GetPathName());
        Result->SetStringField(TEXT("attach_to"), Component.AttachTo);
        Result->SetBoolField(TEXT("created"), !bComponentExisted);
        ComponentResults.Add(MakeShared<FJsonValueObject>(Result));
    }

    if (bHasGraph)
    {
        FString GraphJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&GraphJson);
        FJsonSerializer::Serialize(GraphObject->ToSharedRef(), Writer);
        UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(Blueprint, GraphJson, bClearExistingGraph);
    }

    FString CompileStatus = TEXT("Skipped");
    bool bCompileSucceeded = !bCompile;
    if (bCompile)
    {
        const FString ReportJson = UBlueprintGraphBuilderLibrary::CompileAndReport(Blueprint);
        TSharedPtr<FJsonObject> Report;
        TSharedRef<TJsonReader<>> ReportReader = TJsonReaderFactory<>::Create(ReportJson);
        if (FJsonSerializer::Deserialize(ReportReader, Report) && Report.IsValid())
        {
            bCompileSucceeded = Report->GetBoolField(TEXT("success"));
            Report->TryGetStringField(TEXT("status"), CompileStatus);
            const TArray<TSharedPtr<FJsonValue>>* ReportErrors = nullptr;
            if (Report->TryGetArrayField(TEXT("errors"), ReportErrors))
            {
                Errors.Append(*ReportErrors);
            }
            const TArray<TSharedPtr<FJsonValue>>* ReportWarnings = nullptr;
            if (Report->TryGetArrayField(TEXT("warnings"), ReportWarnings))
            {
                Warnings.Append(*ReportWarnings);
            }
        }
        else
        {
            bCompileSucceeded = false;
            CompileStatus = TEXT("Unknown");
            Errors.Add(MakeShared<FJsonValueString>(TEXT("The Blueprint compiler report could not be parsed.")));
        }
    }

    Blueprint->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        if (Errors.Num() > 0)
        {
            Warnings.Add(MakeShared<FJsonValueString>(
                TEXT("Save was skipped: the Blueprint did not build cleanly.")));
        }
        else
        {
            FString SaveError;
            bSaved = SaveProjectAsset(ObjectPath, SaveError);
            if (!bSaved)
            {
                Errors.Add(MakeShared<FJsonValueString>(
                    FString::Printf(TEXT("Asset save failed: %s"), *SaveError)));
            }
        }
    }

    if (bCreated)
    {
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("Blueprint asset creation is not undoable: undo restores level and actor state, not the new package.")));
    }

    TArray<TSharedPtr<FJsonValue>> NodeTypeValues;
    for (const FString& NodeType : RequestedNodeTypes)
    {
        NodeTypeValues.Add(MakeShared<FJsonValueString>(NodeType));
    }

    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetBoolField(TEXT("requested"), bHasGraph);
    Graph->SetBoolField(TEXT("cleared_existing"), bHasGraph && bClearExistingGraph);
    Graph->SetNumberField(TEXT("node_count"), RequestedNodeTypes.Num());
    Graph->SetNumberField(TEXT("connection_count"), ConnectionCount);
    Graph->SetArrayField(TEXT("node_types"), NodeTypeValues);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString());
    Result->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetArrayField(TEXT("components"), ComponentResults);
    Result->SetObjectField(TEXT("graph"), Graph);
    Result->SetBoolField(TEXT("compiled"), bCompile && bCompileSucceeded);
    Result->SetStringField(TEXT("compile_status"), CompileStatus);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);
    OutResultJson = SerializeJson(Result);
    return true;
}
