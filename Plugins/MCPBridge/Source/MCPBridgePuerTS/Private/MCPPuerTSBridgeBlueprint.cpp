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
#include "JsonObjectConverter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MCPBridgeAssetRollback.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

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

    /** One requested property of a component template. The order the caller
        wrote them in is kept, so an error message names the property the
        caller can find in its own spec. */
    struct FRequestedProperty
    {
        FString Name;
        TSharedPtr<FJsonValue> Value;
    };

    /** One entry of the validated components list. Validation runs to
        completion before a single SCS node is created, so the build loop
        cannot fail halfway and leave an asset with some of its components. */
    struct FValidatedComponent
    {
        FString Name;
        FString AttachTo;
        UClass* Class = nullptr;
        TArray<FRequestedProperty> Properties;
    };

    /** Does this JSON value have the shape its property can actually take?

        FJsonObjectConverter answers "yes" to more than it can honour. Its
        fallback for a value it does not recognise is FProperty::ImportText
        with the result discarded, so `"RelativeScale3D": "big"` sets nothing,
        reports success, and leaves a caller believing a scale was applied.
        Requiring the shape up front turns that into a rejection with a reason.
        The table is deliberately narrow: a category not named here still goes
        to the converter. */
    bool CheckJsonShape(const FProperty* Property, const TSharedPtr<FJsonValue>& Value, FString& OutError)
    {
        const EJson Type = Value->Type;
        const TCHAR* Expected = nullptr;
        if (Property->IsA<FStructProperty>())
        {
            Expected = (Type == EJson::Object) ? nullptr : TEXT("an object such as {\"x\":0,\"y\":0,\"z\":0}");
        }
        else if (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>())
        {
            Expected = (Type == EJson::Array) ? nullptr : TEXT("an array");
        }
        else if (Property->IsA<FMapProperty>())
        {
            Expected = (Type == EJson::Object) ? nullptr : TEXT("an object of key/value pairs");
        }
        else if (Property->IsA<FBoolProperty>())
        {
            Expected = (Type == EJson::Boolean) ? nullptr : TEXT("true or false");
        }
        else if (Property->IsA<FEnumProperty>() || Property->IsA<FByteProperty>())
        {
            // An enum is the one numeric property where an in-range check is
            // not pedantry: writing 7.5 into Mobility passes reflection and
            // then trips an engine ensure inside PostEditChangeProperty.
            const UEnum* Enum = nullptr;
            if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
            {
                Enum = EnumProperty->GetEnum();
            }
            else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
            {
                Enum = ByteProperty->Enum;
            }
            if (Type != EJson::String && Type != EJson::Number)
            {
                Expected = TEXT("an enumerator name or its numeric value");
            }
            else if (Enum != nullptr)
            {
                const bool bValid = (Type == EJson::String)
                    ? Enum->GetValueByNameString(Value->AsString()) != INDEX_NONE
                    : (static_cast<double>(static_cast<int64>(Value->AsNumber())) == Value->AsNumber()
                        && Enum->IsValidEnumValue(static_cast<int64>(Value->AsNumber())));
                if (!bValid)
                {
                    TArray<FString> Names;
                    for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
                    {
                        Names.Add(FString::Printf(
                            TEXT("%s=%lld"), *Enum->GetNameStringByIndex(Index), Enum->GetValueByIndex(Index)));
                    }
                    OutError = FString::Printf(
                        TEXT("expects a %s enumerator: %s"), *Enum->GetName(), *FString::Join(Names, TEXT(", ")));
                    return false;
                }
            }
        }
        else if (Property->IsA<FNumericProperty>())
        {
            Expected = (Type == EJson::Number) ? nullptr : TEXT("a number");
        }
        else if (Property->IsA<FStrProperty>() || Property->IsA<FNameProperty>() || Property->IsA<FTextProperty>())
        {
            Expected = (Type == EJson::String) ? nullptr : TEXT("a string");
        }
        if (Expected == nullptr)
        {
            return true;
        }
        OutError = FString::Printf(TEXT("expects %s"), Expected);
        return false;
    }

    /** Marshal one JSON value into one reflected property, or, with a null
        ValuePtr, check that it could be without writing anything.

        FJsonObjectConverter does the work for every type it handles well, which
        after the Phase L serializer change is the same path read_property and
        set_property use. Two shapes are resolved here instead:

        - A UObject* property from an asset path string. The converter's own
          route for that case is FProperty::ImportText, which ignores its own
          parse result: an unresolvable path silently leaves the property null
          and still reports success. An asset reference that quietly does not
          apply is exactly the failure this tool exists to make visible, so the
          object is loaded explicitly and a miss is an error.
        - An array of UObject* properties, element by element, for the same
          reason. OverrideMaterials is the common case. */
    bool ApplyJsonToProperty(
        FProperty* Property,
        void* ValuePtr,
        const TSharedPtr<FJsonValue>& Value,
        FString& OutError)
    {
        if (!Value.IsValid())
        {
            OutError = TEXT("value is missing");
            return false;
        }
        if (FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
        {
            FString Path;
            if (Value->Type != EJson::Null && !Value->TryGetString(Path))
            {
                OutError = TEXT("expects an asset path string such as \"/Engine/BasicShapes/Cube.Cube\", or null to clear");
                return false;
            }
            if (Value->Type == EJson::Null || Path.IsEmpty() || Path == TEXT("None"))
            {
                if (ValuePtr != nullptr)
                {
                    ObjectProperty->SetObjectPropertyValue(ValuePtr, nullptr);
                }
                return true;
            }
            UObject* Loaded = LoadObject<UObject>(nullptr, *Path);
            if (Loaded == nullptr)
            {
                OutError = FString::Printf(TEXT("asset '%s' could not be loaded"), *Path);
                return false;
            }
            if (ObjectProperty->PropertyClass != nullptr && !Loaded->IsA(ObjectProperty->PropertyClass))
            {
                OutError = FString::Printf(
                    TEXT("'%s' is a %s, but the property holds a %s"),
                    *Path,
                    *Loaded->GetClass()->GetName(),
                    *ObjectProperty->PropertyClass->GetName());
                return false;
            }
            if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
            {
                UClass* LoadedClass = Cast<UClass>(Loaded);
                if (LoadedClass == nullptr
                    || (ClassProperty->MetaClass != nullptr && !LoadedClass->IsChildOf(ClassProperty->MetaClass)))
                {
                    OutError = FString::Printf(
                        TEXT("'%s' is not a class deriving from %s"),
                        *Path,
                        ClassProperty->MetaClass != nullptr ? *ClassProperty->MetaClass->GetName() : TEXT("Object"));
                    return false;
                }
            }
            if (ValuePtr != nullptr)
            {
                ObjectProperty->SetObjectPropertyValue(ValuePtr, Loaded);
            }
            return true;
        }
        if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
        {
            if (CastField<FObjectProperty>(ArrayProperty->Inner) != nullptr)
            {
                const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
                if (!Value->TryGetArray(Items))
                {
                    OutError = TEXT("expects an array of asset path strings");
                    return false;
                }
                if (ValuePtr == nullptr)
                {
                    for (int32 Index = 0; Index < Items->Num(); ++Index)
                    {
                        FString ElementError;
                        if (!ApplyJsonToProperty(ArrayProperty->Inner, nullptr, (*Items)[Index], ElementError))
                        {
                            OutError = FString::Printf(TEXT("element %d: %s"), Index, *ElementError);
                            return false;
                        }
                    }
                    return true;
                }
                FScriptArrayHelper ArrayHelper(ArrayProperty, ValuePtr);
                ArrayHelper.Resize(Items->Num());
                for (int32 Index = 0; Index < Items->Num(); ++Index)
                {
                    FString ElementError;
                    if (!ApplyJsonToProperty(
                            ArrayProperty->Inner, ArrayHelper.GetRawPtr(Index), (*Items)[Index], ElementError))
                    {
                        OutError = FString::Printf(TEXT("element %d: %s"), Index, *ElementError);
                        return false;
                    }
                }
                return true;
            }
        }
        if (!CheckJsonShape(Property, Value, OutError))
        {
            return false;
        }
        if (ValuePtr == nullptr)
        {
            // The shape is as much as can be checked without writing. The
            // converter has no dry run, so a value of the right shape that it
            // still refuses is reported at apply time.
            return true;
        }
        if (!FJsonObjectConverter::JsonValueToUProperty(Value, Property, ValuePtr))
        {
            OutError = FString::Printf(
                TEXT("value is not valid for its reflected type (%s)"), *Property->GetCPPType());
            return false;
        }
        return true;
    }

    FString JoinStrings(const TArray<FString>& Values)
    {
        return FString::Join(Values, TEXT(", "));
    }

    /** Graph node types that only mean anything on an Actor.

        The four event types are spawned as AActor override events
        (SpawnOverrideEvent binds ReceiveBeginPlay and friends against
        AActor::StaticClass()), and UK2Node_InputKey binds a key on an actor
        that can receive input. On a UObject or SaveGame parent each of them
        would build a node that can never fire, which is worse than a
        rejection because it compiles. Everything else in the vocabulary
        (CallFunction, variables, flow, Cast, CustomEvent) is parent-neutral. */
    bool IsActorOnlyNodeType(const FString& NodeType)
    {
        return NodeType == TEXT("BeginPlay")
            || NodeType == TEXT("Tick")
            || NodeType == TEXT("ActorBeginOverlap")
            || NodeType == TEXT("ActorEndOverlap")
            || NodeType == TEXT("InputKey");
    }

    /** Run the builder's variable pass and unpack its JSON report.
        Returns false with the first error when the pass rejected the spec, so
        the caller can fail the whole request with a reason the caller can find
        in its own spec. OutApplied collects the per-variable results. */
    // ---------------------------------------------------------------------
    // Managed-variable ownership.
    //
    // remove_unlisted deletes work, so the question "may this be removed?" has
    // to have a conservative answer that does not depend on getting a list of
    // exclusions right. The answer is an explicit mark: blueprint_build stamps
    // MCPManaged=1 on every variable it declares, and removal only ever
    // considers variables carrying that stamp.
    //
    // That satisfies the whole protection list by construction rather than by
    // enumeration. An inherited variable, a native C++ UPROPERTY, an
    // engine-generated variable and a variable a human added in the editor all
    // share one property: this builder never declared them, so none of them
    // carries the mark. A Blueprint authored before this change has no marked
    // variables at all, so remove_unlisted removes nothing from it until a
    // build declares its managed set - which is the correct default for an
    // asset whose history the bridge does not know.
    const TCHAR* ManagedVariableMetaKey = TEXT("MCPManaged");

    bool IsManagedVariable(const UBlueprint* Blueprint, const FName& VarName)
    {
        FString Value;
        return FBlueprintEditorUtils::GetBlueprintVariableMetaData(
                   Blueprint, VarName, nullptr, FName(ManagedVariableMetaKey), Value)
            && Value == TEXT("1");
    }

    void MarkVariableManaged(UBlueprint* Blueprint, const FName& VarName)
    {
        FBlueprintEditorUtils::SetBlueprintVariableMetaData(
            Blueprint, VarName, nullptr, FName(ManagedVariableMetaKey), TEXT("1"));
    }

    /** Every graph node that reads or writes this variable, as
        "GraphName.NodeName". Reported before a removal rather than after, so a
        caller sees what a removal would break while it is still preventable. */
    TArray<FString> FindVariableReferences(const UBlueprint* Blueprint, const FName& VarName)
    {
        TArray<FString> Locations;
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (const UEdGraph* Graph : Graphs)
        {
            if (Graph == nullptr) { continue; }
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                const UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
                if (VariableNode == nullptr) { continue; }
                if (VariableNode->VariableReference.GetMemberName() == VarName)
                {
                    Locations.Add(FString::Printf(TEXT("%s.%s"),
                        *Graph->GetName(), *Node->GetName()));
                }
            }
        }
        Locations.Sort();
        return Locations;
    }

    /** The graph nodes that reference a variable, for deletion under force. */
    TArray<UEdGraphNode*> CollectVariableReferenceNodes(UBlueprint* Blueprint, const FName& VarName)
    {
        TArray<UEdGraphNode*> Nodes;
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph == nullptr) { continue; }
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_Variable* VariableNode = Cast<UK2Node_Variable>(Node);
                if (VariableNode != nullptr
                    && VariableNode->VariableReference.GetMemberName() == VarName)
                {
                    Nodes.Add(Node);
                }
            }
        }
        return Nodes;
    }

    TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Values.Num());
        for (const FString& Value : Values) { Out.Add(MakeShared<FJsonValueString>(Value)); }
        return Out;
    }

    bool RunVariablePass(
        UBlueprint* Blueprint,
        const FString& VariablesJson,
        bool bValidateOnly,
        TArray<TSharedPtr<FJsonValue>>& OutApplied,
        FString& OutError)
    {
        const FString ReportJson = UBlueprintGraphBuilderLibrary::ConfigureVariablesFromJSON(
            Blueprint, VariablesJson, bValidateOnly);
        TSharedPtr<FJsonObject> Report;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
        if (!FJsonSerializer::Deserialize(Reader, Report) || !Report.IsValid())
        {
            OutError = TEXT("The variable pass returned a report that could not be parsed.");
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
        if (Report->TryGetArrayField(TEXT("errors"), Errors) && Errors->Num() > 0)
        {
            OutError = (*Errors)[0]->AsString();
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>* Variables = nullptr;
        if (Report->TryGetArrayField(TEXT("variables"), Variables))
        {
            OutApplied = *Variables;
        }
        return true;
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
    // The only parent rule is the engine's own. An Actor-only check used to
    // sit here and it removed the SaveGame subclass, the ActorComponent
    // subclass and every data-only Blueprint at once, none of which Unreal
    // objects to. What is genuinely Actor-only is a capability, not a parent,
    // so it is gated per feature below: components need a
    // SimpleConstructionScript, and the actor event node types bind AActor
    // override functions.
    if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
    {
        OutError = FString::Printf(
            TEXT("Unreal refuses Blueprints of class %s."), *ParentClass->GetPathName());
        return false;
    }
    const bool bActorParent = ParentClass->IsChildOf(AActor::StaticClass());

    TArray<FValidatedComponent> Components;
    const TArray<TSharedPtr<FJsonValue>>* ComponentSpecs = nullptr;
    if (Spec->TryGetArrayField(TEXT("components"), ComponentSpecs))
    {
        if (ComponentSpecs->Num() > 0 && !bActorParent)
        {
            OutError = FString::Printf(
                TEXT("Components need an Actor parent: %s does not derive from Actor, and only "
                     "an Actor Blueprint has a SimpleConstructionScript to hold them. Drop the "
                     "components array, or change parent_class."),
                *ParentClass->GetPathName());
            return false;
        }
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

            // Properties are checked against the component class here, before
            // anything is created, so a misspelled property name or an asset
            // path that does not resolve rejects the whole spec rather than
            // leaving a Blueprint whose mesh is silently null.
            const TSharedPtr<FJsonObject>* PropertyObject = nullptr;
            if ((*Entry)->TryGetObjectField(TEXT("properties"), PropertyObject))
            {
                for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertyObject)->Values)
                {
                    FProperty* Property = FindFProperty<FProperty>(Component.Class, *Pair.Key);
                    if (Property == nullptr)
                    {
                        OutError = FString::Printf(
                            TEXT("Component '%s' property '%s': %s has no reflected property by that name."),
                            *Component.Name, *Pair.Key, *Component.Class->GetName());
                        return false;
                    }
                    FString PropertyError;
                    if (!ApplyJsonToProperty(Property, nullptr, Pair.Value, PropertyError))
                    {
                        OutError = FString::Printf(
                            TEXT("Component '%s' property '%s': %s."),
                            *Component.Name, *Pair.Key, *PropertyError);
                        return false;
                    }
                    Component.Properties.Add({ Pair.Key, Pair.Value });
                }
            }
            Components.Add(Component);
        }
    }

    // Member variables. The builder library owns type resolution, default
    // conversion and the conflict rules; this pass runs it in validate-only
    // mode so a bad variable spec rejects the request before the asset exists,
    // exactly like a bad component property.
    FString VariablesJson;
    const TArray<TSharedPtr<FJsonValue>>* VariableSpecs = nullptr;
    const bool bHasVariables = Spec->TryGetArrayField(TEXT("variables"), VariableSpecs);
    if (bHasVariables)
    {
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&VariablesJson);
        FJsonSerializer::Serialize(*VariableSpecs, Writer);
        TArray<TSharedPtr<FJsonValue>> Ignored;
        if (!RunVariablePass(nullptr, VariablesJson, /*bValidateOnly=*/true, Ignored, OutError))
        {
            return false;
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
            if (!bActorParent && IsActorOnlyNodeType(NodeType))
            {
                OutError = FString::Printf(
                    TEXT("Graph node '%s' is of type '%s', which needs an Actor parent: %s does "
                         "not derive from Actor. BeginPlay, Tick, ActorBeginOverlap, "
                         "ActorEndOverlap and InputKey bind actor entry points. A non-Actor "
                         "Blueprint takes variables and the parent-neutral node types "
                         "(CallFunction, CustomEvent, Cast, variables, flow)."),
                    *NodeId, *NodeType, *ParentClass->GetPathName());
                return false;
            }
            // An operator name is routing, not a pin default: a misspelled one
            // builds no node at all, so it is checked here rather than left to
            // produce a graph with a hole in it.
            if (NodeType == TEXT("Operator"))
            {
                FString Op;
                const TSharedPtr<FJsonObject>* Params = nullptr;
                if ((*Entry)->TryGetObjectField(TEXT("params"), Params))
                {
                    (*Params)->TryGetStringField(TEXT("op"), Op);
                }
                const TArray<FString> Operators = UBlueprintGraphBuilderLibrary::GetSupportedOperators();
                if (!Operators.Contains(Op))
                {
                    OutError = FString::Printf(
                        TEXT("Graph node '%s' asks for operator '%s'. Supported operators: %s."),
                        *NodeId, *Op, *JoinStrings(Operators));
                    return false;
                }
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
        if (bHasVariables)
        {
            TArray<TSharedPtr<FJsonValue>> Ignored;
            if (!RunVariablePass(Blueprint, VariablesJson, /*bValidateOnly=*/true, Ignored, OutError))
            {
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

    // --- Convergence scopes: remove_unlisted, plan_only, force. ---
    //
    // Nothing is ever removed by default. remove_unlisted is opt-in per scope,
    // and a scope this session does not implement is REJECTED rather than
    // ignored: silently accepting "functions": true would read as a promise to
    // prune functions and quietly not do it, which is worse than refusing.
    bool bRemoveUnlistedVariables = false;
    {
        const TSharedPtr<FJsonObject>* RemoveUnlisted = nullptr;
        if (Spec->TryGetObjectField(TEXT("remove_unlisted"), RemoveUnlisted)
            && RemoveUnlisted != nullptr)
        {
            static const TCHAR* SupportedScopes[] = { TEXT("variables") };
            static const TCHAR* KnownScopes[] = {
                TEXT("variables"), TEXT("components"), TEXT("functions"),
                TEXT("macros"), TEXT("graph_nodes"), TEXT("interfaces") };
            for (const auto& Pair : (*RemoveUnlisted)->Values)
            {
                bool bKnown = false;
                for (const TCHAR* Scope : KnownScopes)
                {
                    if (Pair.Key.Equals(Scope, ESearchCase::IgnoreCase)) { bKnown = true; break; }
                }
                if (!bKnown)
                {
                    OutError = FString::Printf(
                        TEXT("unsupported_scope: '%s' is not a remove_unlisted scope. Known scopes: ")
                        TEXT("variables, components, functions, macros, graph_nodes, interfaces."),
                        *Pair.Key);
                    return false;
                }
                bool bRequested = false;
                if (!Pair.Value->TryGetBool(bRequested))
                {
                    OutError = FString::Printf(
                        TEXT("unsupported_scope: remove_unlisted.%s must be a boolean."), *Pair.Key);
                    return false;
                }
                if (!bRequested) { continue; }
                bool bSupported = false;
                for (const TCHAR* Scope : SupportedScopes)
                {
                    if (Pair.Key.Equals(Scope, ESearchCase::IgnoreCase)) { bSupported = true; break; }
                }
                if (!bSupported)
                {
                    OutError = FString::Printf(
                        TEXT("unsupported_scope: remove_unlisted.%s is not implemented yet. Only ")
                        TEXT("'variables' converges downward today; the other scopes are rejected ")
                        TEXT("rather than ignored so a caller is never told a prune happened when ")
                        TEXT("it did not."),
                        *Pair.Key);
                    return false;
                }
                bRemoveUnlistedVariables = true;
            }
        }
    }
    const bool bPlanOnly = Spec->HasTypedField<EJson::Boolean>(TEXT("plan_only"))
        && Spec->GetBoolField(TEXT("plan_only"));
    const bool bForceRemoveReferenced =
        Spec->HasTypedField<EJson::Boolean>(TEXT("force_remove_referenced"))
        && Spec->GetBoolField(TEXT("force_remove_referenced"));

    // The desired managed set, by name, from the spec.
    TSet<FName> DesiredVariables;
    {
        const TArray<TSharedPtr<FJsonValue>>* SpecVariables = nullptr;
        if (Spec->TryGetArrayField(TEXT("variables"), SpecVariables))
        {
            for (const TSharedPtr<FJsonValue>& Value : *SpecVariables)
            {
                const TSharedPtr<FJsonObject>* Entry = nullptr;
                FString Name;
                if (Value->TryGetObject(Entry) && (*Entry)->TryGetStringField(TEXT("name"), Name))
                {
                    DesiredVariables.Add(FName(*Name));
                }
            }
        }
    }

    // The plan, computed against the asset as it stands. Shared by plan_only
    // and by the apply path, so what a caller previews is what runs.
    TArray<FString> PlanToAdd, PlanToUpdate, PlanToRemove, PlanProtected, PlanBlocked;
    TSharedPtr<FJsonObject> ReferencedVariables = MakeShared<FJsonObject>();
    if (Blueprint != nullptr)
    {
        for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
        {
            const bool bManaged = IsManagedVariable(Blueprint, Existing.VarName);
            const bool bDesired = DesiredVariables.Contains(Existing.VarName);
            if (bDesired) { PlanToUpdate.Add(Existing.VarName.ToString()); continue; }
            if (!bManaged)
            {
                // Not ours: inherited, native, engine-generated, or authored by
                // a human. Reported so the caller can see it was considered.
                PlanProtected.Add(Existing.VarName.ToString());
                continue;
            }
            const TArray<FString> References = FindVariableReferences(Blueprint, Existing.VarName);
            if (References.Num() > 0)
            {
                ReferencedVariables->SetArrayField(
                    Existing.VarName.ToString(), StringsToJson(References));
                if (!bForceRemoveReferenced)
                {
                    PlanBlocked.Add(FString::Printf(
                        TEXT("%s: referenced by %d graph node(s); pass force_remove_referenced to "
                             "delete them with it"),
                        *Existing.VarName.ToString(), References.Num()));
                    continue;
                }
            }
            PlanToRemove.Add(Existing.VarName.ToString());
        }
        for (const FName& Desired : DesiredVariables)
        {
            const bool bExists = Blueprint->NewVariables.ContainsByPredicate(
                [&](const FBPVariableDescription& D) { return D.VarName == Desired; });
            if (!bExists) { PlanToAdd.Add(Desired.ToString()); }
        }
    }
    else
    {
        for (const FName& Desired : DesiredVariables) { PlanToAdd.Add(Desired.ToString()); }
    }
    PlanToAdd.Sort(); PlanToUpdate.Sort(); PlanToRemove.Sort();
    PlanProtected.Sort(); PlanBlocked.Sort();

    if (bPlanOnly)
    {
        // Read-only by construction: this returns before the mutation section,
        // so no package is created, nothing is dirtied and the transaction the
        // service opened is cancelled rather than committed.
        if (ActiveTransaction != nullptr) { ActiveTransaction->Cancel(); }

        TArray<FString> CurrentVariables;
        if (Blueprint != nullptr)
        {
            for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
            {
                CurrentVariables.Add(Existing.VarName.ToString());
            }
            CurrentVariables.Sort();
        }
        TArray<FString> DesiredNames;
        for (const FName& Desired : DesiredVariables) { DesiredNames.Add(Desired.ToString()); }
        DesiredNames.Sort();

        TSharedPtr<FJsonObject> Plan = MakeShared<FJsonObject>();
        Plan->SetStringField(TEXT("asset_path"), AssetPath);
        Plan->SetBoolField(TEXT("plan_only"), true);
        Plan->SetBoolField(TEXT("asset_exists"), Blueprint != nullptr);
        Plan->SetBoolField(TEXT("remove_unlisted_variables"), bRemoveUnlistedVariables);
        Plan->SetArrayField(TEXT("current_variables"), StringsToJson(CurrentVariables));
        Plan->SetArrayField(TEXT("desired_variables"), StringsToJson(DesiredNames));
        Plan->SetArrayField(TEXT("variables_to_add"), StringsToJson(PlanToAdd));
        Plan->SetArrayField(TEXT("variables_to_update"), StringsToJson(PlanToUpdate));
        Plan->SetArrayField(TEXT("variables_to_remove"),
            StringsToJson(bRemoveUnlistedVariables ? PlanToRemove : TArray<FString>()));
        Plan->SetArrayField(TEXT("protected_variables"), StringsToJson(PlanProtected));
        Plan->SetObjectField(TEXT("referenced_variables"), ReferencedVariables);
        Plan->SetArrayField(TEXT("blocked_removals"),
            StringsToJson(bRemoveUnlistedVariables ? PlanBlocked : TArray<FString>()));
        Plan->SetNumberField(TEXT("expected_change_count"),
            PlanToAdd.Num() + PlanToUpdate.Num()
            + (bRemoveUnlistedVariables ? PlanToRemove.Num() : 0));
        Plan->SetArrayField(TEXT("errors"), TArray<TSharedPtr<FJsonValue>>());
        Plan->SetArrayField(TEXT("warnings"), TArray<TSharedPtr<FJsonValue>>());
        OutResultJson = SerializeJson(Plan);
        return true;
    }

    // --- Mutation starts here. ---
    //
    // Everything below is inside the same rollback boundary the Behavior Tree
    // builder uses (MCPBridgeAssetRollback.h). The checks above reject a
    // malformed spec before anything exists, but the failures that can only be
    // found by building - a connection whose role names no pin, a component
    // that will not attach, a compile error - are discovered once the asset is
    // already real. Without a rollback those leave a registered, dirty
    // Blueprint that the save-on-exit flow writes to disk during close, even
    // when the Save Content prompt is answered "Don't Save". Findings 0c.
    FBridgeAssetRollback Rollback;
    Rollback.Snapshot(AssetPath);
    const TArray<FString> DirtyBefore = Rollback.DirtyPackages();
    const TArray<FString> SourceControlBefore = Rollback.SourceControlState();

    // Cancel the transaction before destroying anything: its undo records
    // reference these objects and must be replayed while they are still live.
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

    const bool bCreated = Blueprint == nullptr;
    if (bCreated)
    {
        UPackage* Package = CreatePackage(*AssetPath);
        if (Package == nullptr)
        {
            return FailWithRollback(TEXT("Unreal could not create the Blueprint package."));
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
            return FailWithRollback(TEXT("Unreal could not create the Blueprint asset."));
        }
        AssetRegistry.Get().AssetCreated(Blueprint);
        Rollback.TrackCreated(Blueprint);
    }

    Blueprint->Modify();

    TArray<TSharedPtr<FJsonValue>> ComponentResults;
    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;
    bool bTemplatesChanged = false;
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
        // Properties are applied to the SCS template, which is the archetype
        // every spawned instance copies, on both new and existing components:
        // a rerun with a changed value has to converge on the new value.
        TArray<TSharedPtr<FJsonValue>> AppliedProperties;
        if (Component.Properties.Num() > 0)
        {
            USCS_Node* Node = FindComponentNode(Blueprint, Component.Name);
            UActorComponent* Template = Node != nullptr ? Node->ComponentTemplate : nullptr;
            if (Template == nullptr)
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Component '%s' has no component template, so its properties were not applied."),
                    *Component.Name)));
            }
            else
            {
                Template->Modify();
                for (const FRequestedProperty& Requested : Component.Properties)
                {
                    FProperty* Property = FindFProperty<FProperty>(Template->GetClass(), *Requested.Name);
                    if (Property == nullptr)
                    {
                        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                            TEXT("Component '%s' property '%s': %s has no reflected property by that name."),
                            *Component.Name, *Requested.Name, *Template->GetClass()->GetName())));
                        continue;
                    }
                    FString PropertyError;
                    if (!ApplyJsonToProperty(
                            Property,
                            Property->ContainerPtrToValuePtr<void>(Template),
                            Requested.Value,
                            PropertyError))
                    {
                        Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                            TEXT("Component '%s' property '%s': %s."),
                            *Component.Name, *Requested.Name, *PropertyError)));
                        continue;
                    }
                    // Name the property that changed: a component only rebuilds
                    // the state that depends on it when the event says which
                    // property moved.
                    FPropertyChangedEvent ChangedEvent(Property, EPropertyChangeType::ValueSet);
                    Template->PostEditChangeProperty(ChangedEvent);
                    AppliedProperties.Add(MakeShared<FJsonValueString>(Requested.Name));
                }
                bTemplatesChanged = bTemplatesChanged || AppliedProperties.Num() > 0;
            }
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Component.Name);
        Result->SetStringField(TEXT("class"), Component.Class->GetPathName());
        Result->SetStringField(TEXT("attach_to"), Component.AttachTo);
        Result->SetBoolField(TEXT("created"), !bComponentExisted);
        Result->SetArrayField(TEXT("properties_applied"), AppliedProperties);
        ComponentResults.Add(MakeShared<FJsonValueObject>(Result));
    }

    if (bTemplatesChanged)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }

    // Variables are applied before the graph, because a VariableGet or
    // VariableSet node can only allocate its pins once the skeleton class
    // carries the property. AddMemberVariable recompiles the skeleton, so by
    // the time the graph pass runs the variables are resolvable.
    TArray<TSharedPtr<FJsonValue>> VariableResults;
    TArray<FString> RemovedVariables;
    TArray<FString> SkippedVariables;
    TArray<FString> RemovedReferenceNodes;

    // The removal ledger. Captured BEFORE any deletion, because the whole point
    // is that the deletion may have to be undone and
    // FScopedTransaction::Cancel() does not restore
    // FBlueprintEditorUtils::RemoveMemberVariable - findings 0g, measured, not
    // assumed. FBPVariableDescription carries the name, pin type, default
    // value, category, metadata, replication settings, the RepNotify function
    // and the VarGuid in one struct, so copying it captures the variable whole.
    struct FRemovalLedgerEntry
    {
        FBPVariableDescription Description;
        bool bWasManaged = false;
        TArray<FString> ReferenceLocations;
    };
    TArray<FRemovalLedgerEntry> RemovalLedger;
    TSharedPtr<FJsonObject> RollbackLedgerJson;
    if (bHasVariables)
    {
        FString VariableError;
        if (!RunVariablePass(Blueprint, VariablesJson, /*bValidateOnly=*/false, VariableResults, VariableError))
        {
            Errors.Add(MakeShared<FJsonValueString>(VariableError));
        }
    }

    // Claim ownership of every variable this spec declares. Idempotent, and it
    // is what makes a later remove_unlisted able to tell its own work from
    // everything else on the asset.
    if (Errors.Num() == 0)
    {
        for (const FName& Desired : DesiredVariables)
        {
            const bool bExists = Blueprint->NewVariables.ContainsByPredicate(
                [&](const FBPVariableDescription& D) { return D.VarName == Desired; });
            if (bExists) { MarkVariableManaged(Blueprint, Desired); }
        }
    }

    // Downward convergence. Runs inside the transaction the service opened, so
    // a compile failure below rolls the removals back with everything else.
    if (bRemoveUnlistedVariables && Errors.Num() == 0)
    {
        SkippedVariables = PlanProtected;
        for (const FString& Blocked : PlanBlocked)
        {
            SkippedVariables.Add(Blocked);
        }
        for (const FString& Name : PlanToRemove)
        {
            const FName VarName(*Name);
            // Ledger first, deletion second. Never the other way round.
            {
                const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
                    [&](const FBPVariableDescription& D) { return D.VarName == VarName; });
                if (Index != INDEX_NONE)
                {
                    FRemovalLedgerEntry Entry;
                    Entry.Description = Blueprint->NewVariables[Index];
                    Entry.bWasManaged = IsManagedVariable(Blueprint, VarName);
                    Entry.ReferenceLocations = FindVariableReferences(Blueprint, VarName);
                    RemovalLedger.Add(Entry);
                }
            }
            if (bForceRemoveReferenced)
            {
                // Delete the nodes that read or write it first: removing the
                // member while nodes still reference it leaves the graph with
                // unresolved pins that only surface at compile time.
                for (UEdGraphNode* Node : CollectVariableReferenceNodes(Blueprint, VarName))
                {
                    if (Node == nullptr) { continue; }
                    RemovedReferenceNodes.Add(FString::Printf(TEXT("%s.%s"),
                        *Node->GetGraph()->GetName(), *Node->GetName()));
                    Node->GetGraph()->Modify();
                    Node->Modify();
                    Node->GetGraph()->RemoveNode(Node);
                }
            }
            FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, VarName);
            RemovedVariables.Add(Name);
        }
        RemovedReferenceNodes.Sort();
        SkippedVariables.Sort();
    }

    // A dropped connection used to be a log line: the graph came out with a
    // hole in it, compiled clean, and reported complete success. The builder
    // now hands back what it could not wire, and a shortfall fails the build,
    // which also means the asset is not saved.
    TArray<FString> UnresolvedConnections;
    int32 ConnectionsMade = 0;
    TArray<FString> FailedNodes;
    int32 CreatedNodes = 0;
    if (bHasGraph)
    {
        FString GraphJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&GraphJson);
        FJsonSerializer::Serialize(GraphObject->ToSharedRef(), Writer);
        UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSONWithReport(
            Blueprint, GraphJson, bClearExistingGraph, UnresolvedConnections, ConnectionsMade,
            FailedNodes, CreatedNodes);
        // A node the factory refused is a failed build, not a quiet omission.
        // This used to be invisible: node_count came from the spec, so a
        // refused node was still counted and the build reported success.
        if (FailedNodes.Num() > 0)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d of %d requested graph node(s) were not created: %s. The build is failed ")
                TEXT("rather than saved, because a graph missing a node the caller asked for is ")
                TEXT("not the graph they specified. Partial graph creation is not a success mode."),
                FailedNodes.Num(), RequestedNodeTypes.Num(), *JoinStrings(FailedNodes))));
        }
        if (ConnectionsMade != ConnectionCount)
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%d of %d graph connection(s) could not be wired and were dropped: %s. ")
                TEXT("A connection endpoint is nodeId.pinRole, and a role that names no pin of ")
                TEXT("that direction is silently discarded by the graph editor, so the build is ")
                TEXT("failed rather than saved. A pure node has no exec pins: a const ")
                TEXT("BlueprintCallable UFUNCTION with a return value is promoted to ")
                TEXT("BlueprintPure by UHT."),
                ConnectionCount - ConnectionsMade, ConnectionCount,
                *JoinStrings(UnresolvedConnections))));
        }
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

    // The failure exit, shared by the build and the save. MarkPackageDirty used
    // to run unconditionally here, which is what left a failed build in the
    // Save Content prompt and on disk during close.
    auto FailRolledBack = [&]() -> bool
    {
        if (ActiveTransaction != nullptr)
        {
            ActiveTransaction->Cancel();
        }

        // Restore from the ledger rather than trusting the cancelled
        // transaction. Findings 0g: RemoveMemberVariable calls Modify(), which
        // made it look transactional, and it is not recovered on this path -
        // a failed build permanently dropped the variables.
        TArray<FString> RestoredVariables;
        TArray<FString> RestorationMismatches;
        for (const FRemovalLedgerEntry& Entry : RemovalLedger)
        {
            const bool bPresent = Blueprint->NewVariables.ContainsByPredicate(
                [&](const FBPVariableDescription& D) { return D.VarName == Entry.Description.VarName; });
            if (!bPresent)
            {
                // Re-add the captured struct whole: name, pin type, default
                // value, category, metadata, replication and VarGuid come back
                // together because they were never separated.
                Blueprint->NewVariables.Add(Entry.Description);
            }
            if (Entry.bWasManaged)
            {
                MarkVariableManaged(Blueprint, Entry.Description.VarName);
            }
            RestoredVariables.Add(Entry.Description.VarName.ToString());
        }
        if (RemovalLedger.Num() > 0)
        {
            FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
            UBlueprintGraphBuilderLibrary::CompileAndReport(Blueprint);
        }

        // Independent confirmation. rollback_succeeded is NOT allowed to come
        // from "we ran the restore": it is re-read off the asset, because the
        // previous version of this reported true while the data was gone.
        for (const FRemovalLedgerEntry& Entry : RemovalLedger)
        {
            const FName VarName = Entry.Description.VarName;
            const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
                [&](const FBPVariableDescription& D) { return D.VarName == VarName; });
            if (Index == INDEX_NONE)
            {
                RestorationMismatches.Add(FString::Printf(
                    TEXT("%s: not present after rollback"), *VarName.ToString()));
                continue;
            }
            const FBPVariableDescription& Now = Blueprint->NewVariables[Index];
            if (Now.VarType != Entry.Description.VarType)
            {
                RestorationMismatches.Add(FString::Printf(
                    TEXT("%s: pin type differs after rollback"), *VarName.ToString()));
            }
            if (Now.DefaultValue != Entry.Description.DefaultValue)
            {
                RestorationMismatches.Add(FString::Printf(
                    TEXT("%s: default value differs after rollback"), *VarName.ToString()));
            }
            if (Now.Category.ToString() != Entry.Description.Category.ToString())
            {
                RestorationMismatches.Add(FString::Printf(
                    TEXT("%s: category differs after rollback"), *VarName.ToString()));
            }
            if (Entry.bWasManaged && !IsManagedVariable(Blueprint, VarName))
            {
                RestorationMismatches.Add(FString::Printf(
                    TEXT("%s: managed ownership marker not restored"), *VarName.ToString()));
            }
        }
        // Reference nodes are deleted only under force, and the graph is
        // rebuilt from the spec on every build, so a node the caller still
        // declares comes back with the rebuild. One that the caller stopped
        // declaring is reported rather than silently absent.
        TArray<FString> ReferenceNodesRestored;
        for (const FRemovalLedgerEntry& Entry : RemovalLedger)
        {
            const TArray<FString> Now = FindVariableReferences(Blueprint, Entry.Description.VarName);
            for (const FString& Location : Now) { ReferenceNodesRestored.Add(Location); }
            for (const FString& Was : Entry.ReferenceLocations)
            {
                if (!Now.Contains(Was))
                {
                    RestorationMismatches.Add(FString::Printf(
                        TEXT("%s: reference node %s was not restored"),
                        *Entry.Description.VarName.ToString(), *Was));
                }
            }
        }

        RollbackLedgerJson = MakeShared<FJsonObject>();
        RollbackLedgerJson->SetNumberField(TEXT("removal_ledger_count"), RemovalLedger.Num());
        RollbackLedgerJson->SetArrayField(TEXT("variables_removed"), StringsToJson(RemovedVariables));
        RollbackLedgerJson->SetArrayField(TEXT("variables_restored"), StringsToJson(RestoredVariables));
        RollbackLedgerJson->SetArrayField(TEXT("reference_nodes_removed"), StringsToJson(RemovedReferenceNodes));
        RollbackLedgerJson->SetArrayField(TEXT("reference_nodes_restored"), StringsToJson(ReferenceNodesRestored));
        RollbackLedgerJson->SetArrayField(TEXT("restoration_mismatches"), StringsToJson(RestorationMismatches));
        RollbackLedgerJson->SetBoolField(TEXT("rollback_succeeded"),
            RestorationMismatches.Num() == 0);

        // The asset boundary runs LAST, so its package dirty-state restore is
        // the final word. Restoring variables marks the Blueprint structurally
        // modified and recompiles it, which dirties the package; running the
        // boundary afterwards puts the flag back where the request found it.
        Rollback.Rollback();

        TSharedPtr<FJsonObject> Failed = MakeShared<FJsonObject>();
        Failed->SetStringField(TEXT("asset_path"), AssetPath);
        Failed->SetBoolField(TEXT("created"), false);
        Failed->SetStringField(TEXT("compile_status"), TEXT("RolledBack"));
        Failed->SetBoolField(TEXT("saved"), false);
        // The counts belong on the FAILURE payload above all: a caller needs to
        // see which nodes and connections were refused precisely when the build
        // did not succeed. Omitting them here was how a failed build still
        // looked featureless.
        TSharedPtr<FJsonObject> FailedGraph = MakeShared<FJsonObject>();
        FailedGraph->SetNumberField(TEXT("requested_node_count"), RequestedNodeTypes.Num());
        FailedGraph->SetNumberField(TEXT("created_node_count"), CreatedNodes);
        FailedGraph->SetNumberField(TEXT("failed_node_count"), FailedNodes.Num());
        FailedGraph->SetArrayField(TEXT("failed_nodes"), StringsToJson(FailedNodes));
        FailedGraph->SetNumberField(TEXT("requested_connection_count"), ConnectionCount);
        FailedGraph->SetNumberField(TEXT("created_connection_count"), ConnectionsMade);
        FailedGraph->SetNumberField(TEXT("failed_connection_count"), ConnectionCount - ConnectionsMade);
        FailedGraph->SetArrayField(TEXT("unresolved_connections"), StringsToJson(UnresolvedConnections));
        Failed->SetObjectField(TEXT("graph"), FailedGraph);
        Failed->SetArrayField(TEXT("errors"), Errors);
        Warnings.Add(MakeShared<FJsonValueString>(
            TEXT("The build failed and was rolled back: no asset, package, or file remains.")));
        Failed->SetArrayField(TEXT("warnings"), Warnings);
        TSharedPtr<FJsonObject> CleanupJson =
            Rollback.BuildEvidence(DirtyBefore, SourceControlBefore);
        if (RollbackLedgerJson.IsValid())
        {
            // The asset-creation boundary reports on what IT tracked. The
            // removal ledger is the destructive half, and its verdict is
            // authoritative: cleanup.rollback_succeeded may not stay true while
            // a removal survived, which is exactly how 0g hid.
            CleanupJson->SetObjectField(TEXT("removal_ledger"), RollbackLedgerJson);
            bool bLedgerOk = false;
            RollbackLedgerJson->TryGetBoolField(TEXT("rollback_succeeded"), bLedgerOk);
            bool bAssetOk = false;
            CleanupJson->TryGetBoolField(TEXT("rollback_succeeded"), bAssetOk);
            CleanupJson->SetBoolField(TEXT("rollback_succeeded"), bAssetOk && bLedgerOk);
        }
        Failed->SetObjectField(TEXT("cleanup"), CleanupJson);
        OutResultJson = SerializeJson(Failed);
        return true;
    };

    if (Errors.Num() > 0)
    {
        return FailRolledBack();
    }

    Blueprint->MarkPackageDirty();

    bool bSaved = false;
    if (bSave)
    {
        FString SaveError;
        bSaved = SaveProjectAsset(ObjectPath, SaveError);
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

    TArray<TSharedPtr<FJsonValue>> UnresolvedValues;
    for (const FString& Unresolved : UnresolvedConnections)
    {
        UnresolvedValues.Add(MakeShared<FJsonValueString>(Unresolved));
    }

    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetBoolField(TEXT("requested"), bHasGraph);
    Graph->SetBoolField(TEXT("cleared_existing"), bHasGraph && bClearExistingGraph);
    // node_count is what Unreal actually holds, not what was asked for. The
    // requested/created/failed split is reported alongside it so the two can
    // never be confused again.
    Graph->SetNumberField(TEXT("node_count"), CreatedNodes);
    Graph->SetNumberField(TEXT("requested_node_count"), RequestedNodeTypes.Num());
    Graph->SetNumberField(TEXT("created_node_count"), CreatedNodes);
    Graph->SetNumberField(TEXT("failed_node_count"), FailedNodes.Num());
    Graph->SetArrayField(TEXT("failed_nodes"), StringsToJson(FailedNodes));
    Graph->SetNumberField(TEXT("requested_connection_count"), ConnectionCount);
    Graph->SetNumberField(TEXT("created_connection_count"), ConnectionsMade);
    Graph->SetNumberField(TEXT("failed_connection_count"), ConnectionCount - ConnectionsMade);
    // connection_count is the number of links actually made. The number asked
    // for is reported separately; when they differ the build has already
    // failed, and unresolved_connections says which pairs were dropped.
    Graph->SetNumberField(TEXT("connection_count"), ConnectionsMade);
    Graph->SetNumberField(TEXT("connections_requested"), ConnectionCount);
    Graph->SetArrayField(TEXT("unresolved_connections"), UnresolvedValues);
    Graph->SetArrayField(TEXT("node_types"), NodeTypeValues);

    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("asset_path"), AssetPath);
    Result->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    Result->SetStringField(TEXT("generated_class_path"),
        Blueprint->GeneratedClass != nullptr ? Blueprint->GeneratedClass->GetPathName() : FString());
    Result->SetStringField(TEXT("parent_class"), ParentClass->GetPathName());
    Result->SetBoolField(TEXT("created"), bCreated);
    Result->SetArrayField(TEXT("components"), ComponentResults);
    Result->SetArrayField(TEXT("variables"), VariableResults);
    Result->SetObjectField(TEXT("graph"), Graph);
    Result->SetBoolField(TEXT("compiled"), bCompile && bCompileSucceeded);
    Result->SetStringField(TEXT("compile_status"), CompileStatus);
    Result->SetBoolField(TEXT("saved"), bSaved);
    Result->SetArrayField(TEXT("errors"), Errors);
    Result->SetArrayField(TEXT("warnings"), Warnings);

    // Convergence report. converged is measured off the asset after the work,
    // not inferred from the plan: the managed set must equal the desired set.
    {
        TArray<FString> ManagedAfter;
        for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
        {
            if (IsManagedVariable(Blueprint, Existing.VarName))
            {
                ManagedAfter.Add(Existing.VarName.ToString());
            }
        }
        ManagedAfter.Sort();
        TArray<FString> DesiredNames;
        for (const FName& Desired : DesiredVariables) { DesiredNames.Add(Desired.ToString()); }
        DesiredNames.Sort();
        const bool bConverged = !bRemoveUnlistedVariables || ManagedAfter == DesiredNames;

        TSharedPtr<FJsonObject> Convergence = MakeShared<FJsonObject>();
        Convergence->SetArrayField(TEXT("added_variables"), StringsToJson(PlanToAdd));
        Convergence->SetArrayField(TEXT("updated_variables"), StringsToJson(PlanToUpdate));
        Convergence->SetArrayField(TEXT("removed_variables"), StringsToJson(RemovedVariables));
        Convergence->SetArrayField(TEXT("skipped_variables"), StringsToJson(SkippedVariables));
        Convergence->SetArrayField(TEXT("removed_reference_nodes"), StringsToJson(RemovedReferenceNodes));
        Convergence->SetArrayField(TEXT("managed_variables_after"), StringsToJson(ManagedAfter));
        Convergence->SetStringField(TEXT("compile_status"), CompileStatus);
        Convergence->SetBoolField(TEXT("converged"), bConverged);
        Result->SetObjectField(TEXT("convergence"), Convergence);
    }

    // Always present, so a caller never has to distinguish "clean" from
    // "this build predates the rollback boundary".
    Result->SetObjectField(TEXT("cleanup"),
        Rollback.BuildEvidence(DirtyBefore, SourceControlBefore));
    OutResultJson = SerializeJson(Result);
    return true;
}
