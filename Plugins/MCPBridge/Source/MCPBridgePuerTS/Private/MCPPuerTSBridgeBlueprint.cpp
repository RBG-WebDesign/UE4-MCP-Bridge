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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
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
    if (bHasVariables)
    {
        FString VariableError;
        if (!RunVariablePass(Blueprint, VariablesJson, /*bValidateOnly=*/false, VariableResults, VariableError))
        {
            Errors.Add(MakeShared<FJsonValueString>(VariableError));
        }
    }

    // A dropped connection used to be a log line: the graph came out with a
    // hole in it, compiled clean, and reported complete success. The builder
    // now hands back what it could not wire, and a shortfall fails the build,
    // which also means the asset is not saved.
    TArray<FString> UnresolvedConnections;
    int32 ConnectionsMade = 0;
    if (bHasGraph)
    {
        FString GraphJson;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&GraphJson);
        FJsonSerializer::Serialize(GraphObject->ToSharedRef(), Writer);
        UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSONWithReport(
            Blueprint, GraphJson, bClearExistingGraph, UnresolvedConnections, ConnectionsMade);
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

    TArray<TSharedPtr<FJsonValue>> UnresolvedValues;
    for (const FString& Unresolved : UnresolvedConnections)
    {
        UnresolvedValues.Add(MakeShared<FJsonValueString>(Unresolved));
    }

    TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
    Graph->SetBoolField(TEXT("requested"), bHasGraph);
    Graph->SetBoolField(TEXT("cleared_existing"), bHasGraph && bClearExistingGraph);
    Graph->SetNumberField(TEXT("node_count"), RequestedNodeTypes.Num());
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
    OutResultJson = SerializeJson(Result);
    return true;
}
