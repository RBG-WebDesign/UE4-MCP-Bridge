// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BlueprintGraphBuilderLibrary.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Event.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_DynamicCast.h"
#include "EdGraphNode_Comment.h"
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Components/ActorComponent.h"
#include "JsonObjectConverter.h"
#include "UObject/TextProperty.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "BlueprintMutator/BPNodeRegistry.h"
#include "BlueprintInspector/BPPinSerializer.h"
#include "BlueprintInspector/BPGraphReader.h"
#include "Misc/DefaultValueHelper.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DoOnceMultiInput.h"
#include "K2Node_FormatText.h"
#include "K2Node_InputKey.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_MultiGate.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Switch.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Engine/Blueprint.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace
{
    /** One symbolic operator: the name a spec writes, and the Blueprint
        function library call it becomes. Every entry was checked against the
        4.27 headers before it was added; a name that does not resolve at build
        time is reported rather than skipped. */
    struct FOperatorEntry
    {
        const TCHAR* Op;
        const TCHAR* ClassName;
        const TCHAR* FunctionName;
    };

    const TArray<FOperatorEntry>& OperatorTable()
    {
        static const TArray<FOperatorEntry> Table = {
            { TEXT("not_bool"),              TEXT("KismetMathLibrary"),   TEXT("Not_PreBool") },
            { TEXT("and_bool"),              TEXT("KismetMathLibrary"),   TEXT("BooleanAND") },
            { TEXT("or_bool"),               TEXT("KismetMathLibrary"),   TEXT("BooleanOR") },
            { TEXT("add_float"),             TEXT("KismetMathLibrary"),   TEXT("Add_FloatFloat") },
            { TEXT("subtract_float"),        TEXT("KismetMathLibrary"),   TEXT("Subtract_FloatFloat") },
            { TEXT("multiply_float"),        TEXT("KismetMathLibrary"),   TEXT("Multiply_FloatFloat") },
            { TEXT("divide_float"),          TEXT("KismetMathLibrary"),   TEXT("Divide_FloatFloat") },
            { TEXT("greater_float"),         TEXT("KismetMathLibrary"),   TEXT("Greater_FloatFloat") },
            { TEXT("less_float"),            TEXT("KismetMathLibrary"),   TEXT("Less_FloatFloat") },
            { TEXT("greater_equal_float"),   TEXT("KismetMathLibrary"),   TEXT("GreaterEqual_FloatFloat") },
            { TEXT("less_equal_float"),      TEXT("KismetMathLibrary"),   TEXT("LessEqual_FloatFloat") },
            { TEXT("equal_float"),           TEXT("KismetMathLibrary"),   TEXT("EqualEqual_FloatFloat") },
            { TEXT("clamp_float"),           TEXT("KismetMathLibrary"),   TEXT("FClamp") },
            { TEXT("lerp_float"),            TEXT("KismetMathLibrary"),   TEXT("Lerp") },
            { TEXT("add_int"),               TEXT("KismetMathLibrary"),   TEXT("Add_IntInt") },
            { TEXT("subtract_int"),          TEXT("KismetMathLibrary"),   TEXT("Subtract_IntInt") },
            { TEXT("greater_int"),           TEXT("KismetMathLibrary"),   TEXT("Greater_IntInt") },
            { TEXT("less_int"),              TEXT("KismetMathLibrary"),   TEXT("Less_IntInt") },
            { TEXT("equal_int"),             TEXT("KismetMathLibrary"),   TEXT("EqualEqual_IntInt") },
            { TEXT("make_vector"),           TEXT("KismetMathLibrary"),   TEXT("MakeVector") },
            { TEXT("add_vector"),            TEXT("KismetMathLibrary"),   TEXT("Add_VectorVector") },
            { TEXT("multiply_vector_float"), TEXT("KismetMathLibrary"),   TEXT("Multiply_VectorFloat") },
            { TEXT("append_string"),         TEXT("KismetStringLibrary"), TEXT("Concat_StrStr") },
            { TEXT("vector_to_string"),      TEXT("KismetStringLibrary"), TEXT("Conv_VectorToString") },
            { TEXT("bool_to_string"),        TEXT("KismetStringLibrary"), TEXT("Conv_BoolToString") },
        };
        return Table;
    }

    const FOperatorEntry* FindOperator(const FString& Op)
    {
        for (const FOperatorEntry& Entry : OperatorTable())
        {
            if (Op.Equals(Entry.Op, ESearchCase::IgnoreCase))
            {
                return &Entry;
            }
        }
        return nullptr;
    }

    /** The registry-backed node types this builder advertises. The registry
        holds roughly forty factories; the ones named here are the ones that
        make sense in an actor event graph and whose config keys are written
        down in the schema. Adding a name without documenting its keys would
        publish a node type a caller cannot configure. */
    const TArray<FString>& RegistryNodeTypes()
    {
        static const TArray<FString> Types = {
            TEXT("Event"),
            TEXT("CustomEvent"),
            TEXT("VariableGet"),
            TEXT("VariableSet"),
            TEXT("Cast"),
            TEXT("Select"),
            TEXT("Knot"),
            TEXT("MakeStruct"),
            TEXT("BreakStruct"),
            TEXT("FormatText"),
            TEXT("SpawnActor"),
            TEXT("SwitchInt"),
            TEXT("SwitchString"),
            TEXT("MultiGate"),
            TEXT("DoOnceMultiInput"),
            // The one input factory that needs nothing from the project.
            // UK2Node_InputKey binds a literal FKey (K2Node_InputKey.h:28), so
            // it needs no axis or action mapping in DefaultInput.ini, which is
            // what kept the rest of the input family unadvertised: an
            // InputAction node naming a mapping the project does not have
            // compiles and never fires. Pins are Pressed, Released and Key
            // (K2Node_InputKey.cpp:56-59).
            TEXT("InputKey"),
        };
        return Types;
    }

    /** Public spec keys are snake_case; some registry factories predate that
        and read camelCase. One table, so the two spellings cannot drift. */
    FString RegistryConfigKey(const FString& SpecKey)
    {
        static const TMap<FString, FString> Aliases = {
            { TEXT("var_name"),      TEXT("varName") },
            { TEXT("target_class"),  TEXT("targetClass") },
            { TEXT("function_name"), TEXT("functionName") },
            { TEXT("event_name"),    TEXT("eventName") },
            { TEXT("parent_class"),  TEXT("parentClass") },
            { TEXT("num_outputs"),   TEXT("numOutputs") },
            { TEXT("actor_class"),   TEXT("actorClass") },
            { TEXT("widget_class"),  TEXT("widgetClass") },
            { TEXT("struct_type"),   TEXT("structType") },
            { TEXT("macro_bp"),      TEXT("macroBP") },
            { TEXT("macro_name"),    TEXT("macroName") },
        };
        const FString* Found = Aliases.Find(SpecKey);
        return Found != nullptr ? *Found : SpecKey;
    }

    /** Serialize a node's params as the ConfigJson a registry factory reads,
        with the snake_case keys translated. Keys the factory does not know are
        harmless there: they are re-read afterwards as pin defaults. */
    FString RegistryConfigJson(const TSharedPtr<FJsonObject>& Params)
    {
        TSharedPtr<FJsonObject> Config = MakeShared<FJsonObject>();
        if (Params.IsValid())
        {
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
            {
                Config->SetField(RegistryConfigKey(Pair.Key), Pair.Value);
            }
        }
        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Config.ToSharedRef(), Writer);
        return Out;
    }

    UClass* ResolveGraphClass(const FString& Name)
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

    /** Unreal text for one struct value, from a JSON object.

        FVector and FRotator get the comma form the K2 schema validates with
        FDefaultValueHelper::IsStringValidVector / IsStringValidRotator, which
        is the shape the editor itself writes into those pins. Everything else
        goes through FJsonObjectConverter and ExportText, which the compiler's
        VM backend accepts as its fallback for any struct literal
        (KismetCompilerVMBackend.cpp: ParseVector first, ImportText second). */
    bool StructTextFromJson(
        const UScriptStruct* Struct,
        const TSharedPtr<FJsonValue>& Value,
        bool bK2PinForm,
        FString& OutText,
        FString& OutError)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        if (!Value->TryGetObject(Object))
        {
            OutError = FString::Printf(
                TEXT("expects an object with the fields of %s"), *Struct->GetName());
            return false;
        }
        // A key that names no field of the struct is an error, not a no-op.
        // FJsonObjectConverter drops keys it does not recognise without a
        // word, so {"x":0,"y":0,"zz":400} silently means (0,0,0).
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Object)->Values)
        {
            bool bMatched = false;
            for (TFieldIterator<FProperty> It(Struct); It && !bMatched; ++It)
            {
                const FString FieldName = It->GetName();
                bMatched = FieldName.Equals(Pair.Key, ESearchCase::IgnoreCase)
                    // The converter also accepts a bool field without its "b".
                    || (FieldName.Len() > 1 && FieldName[0] == TEXT('b')
                        && FieldName.RightChop(1).Equals(Pair.Key, ESearchCase::IgnoreCase));
            }
            if (!bMatched)
            {
                TArray<FString> FieldNames;
                for (TFieldIterator<FProperty> It(Struct); It; ++It)
                {
                    FieldNames.Add(It->GetName());
                }
                OutError = FString::Printf(
                    TEXT("has no field '%s'. %s takes: %s"),
                    *Pair.Key, *Struct->GetName(), *FString::Join(FieldNames, TEXT(", ")));
                return false;
            }
        }

        // A field that is absent is an error for the two pin forms below,
        // whose text carries three positional numbers with no room to say
        // "unset".
        FString MissingField;
        auto ReadNumber = [&Object, &MissingField](const TCHAR* Lower, const TCHAR* Upper) -> double
        {
            double Result = 0.0;
            if (!(*Object)->TryGetNumberField(Lower, Result) && !(*Object)->TryGetNumberField(Upper, Result))
            {
                MissingField = Lower;
            }
            return Result;
        };
        auto ThreeFields = [&](const TCHAR* A, const TCHAR* AU, const TCHAR* B, const TCHAR* BU,
                               const TCHAR* C, const TCHAR* CU) -> bool
        {
            const double First = ReadNumber(A, AU);
            const double Second = ReadNumber(B, BU);
            const double Third = ReadNumber(C, CU);
            if (!MissingField.IsEmpty())
            {
                OutError = FString::Printf(
                    TEXT("expects an object with numeric %s, %s and %s (no '%s' field was found)"),
                    A, B, C, *MissingField);
                return false;
            }
            OutText = FString::Printf(TEXT("%f,%f,%f"), First, Second, Third);
            return true;
        };
        // The comma triple is the one form both consumers read for these two.
        // A graph pin validates it with FDefaultValueHelper::IsStringValidVector
        // / IsStringValidRotator, and a variable default is parsed by
        // FDefaultValueHelper::ParseVector / ParseRotator
        // (BlueprintEditorUtils.cpp:8991). ParseRotator falls back to
        // FRotator::InitFromString and would also take "P= Y= R="; ParseVector
        // has no such fallback, so "X=0.000 Y=0.000 Z=400.000" is refused by
        // the compiler with "Can't parse default value". Both are written the
        // same way here rather than relying on that asymmetry.
        if (Struct == TBaseStructure<FVector>::Get())
        {
            return ThreeFields(TEXT("x"), TEXT("X"), TEXT("y"), TEXT("Y"), TEXT("z"), TEXT("Z"));
        }
        if (Struct == TBaseStructure<FRotator>::Get())
        {
            // ParseRotator reads the three numbers as Pitch, Yaw, Roll
            // (DefaultValueHelper.cpp:521).
            return ThreeFields(TEXT("pitch"), TEXT("Pitch"), TEXT("yaw"), TEXT("Yaw"), TEXT("roll"), TEXT("Roll"));
        }
        FStructOnScope Scope(Struct);
        if (!FJsonObjectConverter::JsonObjectToUStruct(
                Object->ToSharedRef(),
                Struct,
                Scope.GetStructMemory(),
                /*CheckFlags=*/0,
                /*SkipFlags=*/0))
        {
            OutError = FString::Printf(TEXT("is not a valid %s value"), *Struct->GetName());
            return false;
        }
        // A Blueprint variable's default is read back by
        // FBlueprintEditorUtils::PropertyValueFromString_Direct, which does NOT
        // use ImportText for FLinearColor or FTransform: both go through their
        // own InitFromString (BlueprintEditorUtils.cpp:9003-9015), whose matched
        // half is the type's ToString. A graph pin takes neither, so the pin
        // form keeps the schema's own shapes.
        const void* Memory = Scope.GetStructMemory();
        if (Struct == TBaseStructure<FLinearColor>::Get())
        {
            const FLinearColor& Color = *static_cast<const FLinearColor*>(Memory);
            OutText = bK2PinForm
                ? FString::Printf(TEXT("%f,%f,%f,%f"), Color.R, Color.G, Color.B, Color.A)
                : Color.ToString();
            return true;
        }
        if (!bK2PinForm && Struct == TBaseStructure<FTransform>::Get())
        {
            OutText = static_cast<const FTransform*>(Memory)->ToString();
            return true;
        }
        // Defaults must be null, not the value itself. UScriptStruct::ExportText
        // is a delta export (Class.cpp:2916): with Defaults == Value every field
        // compares equal and a struct with no native ExportTextItem exports as
        // "()".
        Struct->ExportText(OutText, Scope.GetStructMemory(), /*Defaults=*/nullptr, nullptr, PPF_None, nullptr);
        return true;
    }

    /** Write one JSON value into one graph pin's default.

        Pin defaults are strings, and the shape that string has to take depends
        on the pin category. Writing the wrong shape does not fail: the pin
        keeps a value the compiler reads as zero. Each category is therefore
        checked against the JSON type first and refused by name. */
    bool ApplyPinDefault(UEdGraphPin* Pin, const TSharedPtr<FJsonValue>& Value, FString& OutError)
    {
        if (Pin == nullptr)
        {
            OutError = TEXT("pin was not found on the node");
            return false;
        }
        if (!Value.IsValid())
        {
            OutError = TEXT("value is missing");
            return false;
        }
        const FName Category = Pin->PinType.PinCategory;
        if (Category == UEdGraphSchema_K2::PC_Exec)
        {
            OutError = TEXT("is an execution pin and takes no default");
            return false;
        }
        if (Pin->LinkedTo.Num() > 0)
        {
            OutError = TEXT("is wired to another node, so a default would be ignored");
            return false;
        }

        if (Category == UEdGraphSchema_K2::PC_Object
            || Category == UEdGraphSchema_K2::PC_Class
            || Category == UEdGraphSchema_K2::PC_SoftObject
            || Category == UEdGraphSchema_K2::PC_SoftClass
            || Category == UEdGraphSchema_K2::PC_Interface)
        {
            if (Value->Type == EJson::Null)
            {
                Pin->DefaultObject = nullptr;
                Pin->DefaultValue.Reset();
                return true;
            }
            if (Value->Type != EJson::String)
            {
                OutError = TEXT("expects an object path string, or null to clear");
                return false;
            }
            const FString Path = Value->AsString();
            UObject* Loaded = LoadObject<UObject>(nullptr, *Path);
            if (Loaded == nullptr)
            {
                OutError = FString::Printf(TEXT("object '%s' could not be loaded"), *Path);
                return false;
            }
            Pin->DefaultObject = Loaded;
            Pin->DefaultValue.Reset();
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Boolean)
        {
            if (Value->Type != EJson::Boolean)
            {
                OutError = TEXT("expects true or false");
                return false;
            }
            Pin->DefaultValue = Value->AsBool() ? TEXT("true") : TEXT("false");
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject.IsValid())
        {
            // An enum pin holds the enumerator name, not its number.
            if (Value->Type != EJson::String)
            {
                OutError = TEXT("expects an enumerator name");
                return false;
            }
            Pin->DefaultValue = Value->AsString();
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Int
            || Category == UEdGraphSchema_K2::PC_Int64
            || Category == UEdGraphSchema_K2::PC_Byte)
        {
            if (Value->Type != EJson::Number)
            {
                OutError = TEXT("expects a whole number");
                return false;
            }
            Pin->DefaultValue = FString::Printf(TEXT("%lld"), static_cast<int64>(Value->AsNumber()));
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Float)
        {
            if (Value->Type != EJson::Number)
            {
                OutError = TEXT("expects a number");
                return false;
            }
            Pin->DefaultValue = FString::SanitizeFloat(Value->AsNumber());
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_String
            || Category == UEdGraphSchema_K2::PC_Name
            || Category == UEdGraphSchema_K2::PC_Text)
        {
            if (Value->Type != EJson::String)
            {
                OutError = TEXT("expects a string");
                return false;
            }
            const FString Text = Value->AsString();
            if (Category == UEdGraphSchema_K2::PC_Text)
            {
                Pin->DefaultTextValue = FText::FromString(Text);
            }
            else
            {
                Pin->DefaultValue = Text;
            }
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Struct)
        {
            const UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
            if (Struct == nullptr)
            {
                OutError = TEXT("is a struct pin with no struct type");
                return false;
            }
            return StructTextFromJson(Struct, Value, /*bK2PinForm=*/true, Pin->DefaultValue, OutError);
        }
        FString Fallback;
        if (!Value->TryGetString(Fallback))
        {
            OutError = FString::Printf(
                TEXT("has pin category '%s', which this builder can only set from a string"),
                *Category.ToString());
            return false;
        }
        Pin->DefaultValue = Fallback;
        return true;
    }

    /** Write a pin default and then tell the node its default moved.

        Writing Pin->DefaultObject is not what the editor's details panel does.
        A UFUNCTION carrying meta=(DeterminesOutputType="SomeClassPin") types
        its output from that pin inside PinDefaultValueChanged
        (UK2Node_CallFunction::PinDefaultValueChanged ->
        FDynamicOutputHelper::ConformOutputType, K2Node_CallFunction.cpp:1239),
        and nothing in UEdGraphPin calls it. Without this notification
        UGameplayStatics::CreateSaveGameObject hands back a bare USaveGame*
        and the node one step downstream fails to compile with "The type of
        Object is undetermined". Limitation 26, one half. */
    bool ApplyPinDefaultAndNotify(
        UEdGraphNode* Node,
        UEdGraphPin* Pin,
        const TSharedPtr<FJsonValue>& Value,
        FString& OutError)
    {
        if (!ApplyPinDefault(Pin, Value, OutError))
        {
            return false;
        }
        if (Node != nullptr && Pin != nullptr && !Pin->IsPendingKill())
        {
            Node->PinDefaultValueChanged(Pin);
        }
        return true;
    }

    /** Apply every param that is not a routing key as a pin default. A param
        naming no pin, or a value the pin cannot take, is logged with the node
        id: a silently dropped pin default is the failure mode this replaces. */
    /**
     * A node type's accepted parameter names are its RoutingKeys (the config
     * the factory reads) plus the names of its own pins (which become pin
     * defaults). A key in neither set is one the builder would silently drop,
     * which is how a spec can look applied and do nothing - the VariableGet
     * "variable" case survived precisely because a wrong key costs nothing.
     * OutUnknownParams collects them so the caller can refuse the node.
     */
    /** RoutingKeys are declared snake_case (var_name) while the registry
        factories read camelCase config (varName) and RegistryConfigJson
        converts between them, so neither spelling alone is the accepted-name
        table. Folding out underscores and case makes the two vocabularies one:
        var_name and varName both normalise to varname, while a genuinely wrong
        key like "variable" still does not match anything. */
    FString NormalizeParamKey(const FString& Key)
    {
        return Key.Replace(TEXT("_"), TEXT("")).ToLower();
    }

    void ApplyParamsAsPinDefaults(
        UEdGraphNode* Node,
        const TSharedPtr<FJsonObject>& Params,
        const TSet<FString>& RoutingKeys,
        const FString& NodeId,
        TArray<FString>& OutUnknownParams)
    {
        if (Node == nullptr || !Params.IsValid())
        {
            return;
        }
        TSet<FString> NormalizedRouting;
        for (const FString& Key : RoutingKeys) { NormalizedRouting.Add(NormalizeParamKey(Key)); }
        TSet<FString> NormalizedPins;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr) { NormalizedPins.Add(NormalizeParamKey(Pin->PinName.ToString())); }
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
        {
            const FString Normalized = NormalizeParamKey(Pair.Key);
            if (RoutingKeys.Contains(Pair.Key) || NormalizedRouting.Contains(Normalized))
            {
                continue;
            }
            if (Node->FindPin(FName(*Pair.Key)) == nullptr && !NormalizedPins.Contains(Normalized))
            {
                OutUnknownParams.Add(Pair.Key);
                continue;
            }
            UEdGraphPin* Pin = Node->FindPin(FName(*Pair.Key));
            if (Pin == nullptr)
            {
                // Normalised match only: the key names a real pin under a
                // different spelling, so it is accepted rather than refused.
                continue;
            }
            FString Error;
            if (!ApplyPinDefaultAndNotify(Node, Pin, Pair.Value, Error))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("BuildBlueprintFromJSON: node '%s' param '%s' %s"),
                    *NodeId, *Pair.Key, *Error);
            }
        }
    }

    /** The parameter names a node would have accepted, for an error message
        that tells the caller what to write instead of only what was wrong. */
    FString DescribeAcceptedParams(const UEdGraphNode* Node, const TSet<FString>& RoutingKeys)
    {
        TArray<FString> Accepted = RoutingKeys.Array();
        if (Node != nullptr)
        {
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin != nullptr) { Accepted.Add(Pin->PinName.ToString()); }
            }
        }
        Accepted.Sort();
        return Accepted.Num() > 0 ? FString::Join(Accepted, TEXT(", ")) : TEXT("none");
    }

    UEdGraphNode* SpawnOverrideEvent(UEdGraph& Graph, const TCHAR* EventName, int32 PosX, int32 PosY)
    {
        FGraphNodeCreator<UK2Node_Event> Creator(Graph);
        UK2Node_Event* EventNode = Creator.CreateNode();
        EventNode->EventReference.SetExternalMember(FName(EventName), AActor::StaticClass());
        EventNode->bOverrideFunction = true;
        EventNode->NodePosX = PosX;
        EventNode->NodePosY = PosY;
        Creator.Finalize();
        return EventNode;
    }
}

TArray<FString> UBlueprintGraphBuilderLibrary::GetSupportedNodeTypes()
{
    // Keep in dispatch order with the if/else chain in BuildBlueprintFromJSON.
    // The chain is guarded by this list, so a type present here but missing
    // from the chain falls through to the "unknown type" warning, and a type
    // in the chain but missing here never reaches it. Either way the drift is
    // visible in the response instead of producing a silently empty graph.
    TArray<FString> Types({
        TEXT("BeginPlay"),
        TEXT("Tick"),
        TEXT("ActorBeginOverlap"),
        TEXT("ActorEndOverlap"),
        TEXT("PrintString"),
        TEXT("CallFunction"),
        TEXT("Operator"),
        TEXT("Delay"),
        TEXT("Branch"),
        TEXT("Sequence"),
        TEXT("Comment"),
    });
    // The registry-backed half. A name is advertised only if a factory for it
    // is actually registered, so a factory that is renamed or dropped removes
    // the node type from the schema instead of leaving a type that builds
    // nothing.
    for (const FString& Type : RegistryNodeTypes())
    {
        if (FBPNodeRegistry::Find(Type))
        {
            Types.Add(Type);
        }
        else
        {
            UE_LOG(LogTemp, Warning,
                TEXT("GetSupportedNodeTypes: '%s' is listed for the node registry but no factory is registered"),
                *Type);
        }
    }
    return Types;
}

TArray<FString> UBlueprintGraphBuilderLibrary::GetSupportedOperators()
{
    TArray<FString> Names;
    for (const FOperatorEntry& Entry : OperatorTable())
    {
        Names.Add(Entry.Op);
    }
    return Names;
}

namespace
{
    /** The scalar and struct keywords, in the order the error message lists
        them. The four prefixed forms are appended by GetSupportedVariableTypes
        because they take a suffix rather than standing alone. */
    const TArray<FString>& SimpleVariableTypes()
    {
        static const TArray<FString> Types = {
            TEXT("bool"), TEXT("byte"), TEXT("int"), TEXT("int64"), TEXT("float"),
            TEXT("string"), TEXT("name"), TEXT("text"),
            TEXT("vector"), TEXT("vector2d"), TEXT("rotator"), TEXT("transform"),
            TEXT("linearcolor"),
        };
        return Types;
    }

    bool PinTypeFromSpec(
        const FString& RawType,
        const FString& RawContainer,
        FEdGraphPinType& Out,
        FString& OutError)
    {
        const FString Type = RawType.TrimStartAndEnd();
        if (Type.IsEmpty())
        {
            OutError = TEXT("type is required");
            return false;
        }

        FString Prefix;
        FString Suffix;
        const bool bHasSuffix = Type.Split(TEXT(":"), &Prefix, &Suffix);
        const FString Key = (bHasSuffix ? Prefix : Type).ToLower();
        Suffix = Suffix.TrimStartAndEnd();

        if (bHasSuffix)
        {
            if (Suffix.IsEmpty())
            {
                OutError = FString::Printf(TEXT("type '%s' needs a path after the colon"), *Type);
                return false;
            }
            if (Key == TEXT("object") || Key == TEXT("class"))
            {
                UClass* Resolved = ResolveGraphClass(Suffix);
                if (Resolved == nullptr)
                {
                    OutError = FString::Printf(TEXT("class '%s' was not found"), *Suffix);
                    return false;
                }
                Out.PinCategory = (Key == TEXT("object"))
                    ? UEdGraphSchema_K2::PC_Object
                    : UEdGraphSchema_K2::PC_Class;
                Out.PinSubCategoryObject = Resolved;
            }
            else if (Key == TEXT("struct"))
            {
                UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Suffix);
                if (Struct == nullptr)
                {
                    OutError = FString::Printf(TEXT("struct '%s' was not found"), *Suffix);
                    return false;
                }
                Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
                Out.PinSubCategoryObject = Struct;
            }
            else if (Key == TEXT("enum"))
            {
                UEnum* Enum = LoadObject<UEnum>(nullptr, *Suffix);
                if (Enum == nullptr)
                {
                    OutError = FString::Printf(TEXT("enum '%s' was not found"), *Suffix);
                    return false;
                }
                Out.PinCategory = UEdGraphSchema_K2::PC_Byte;
                Out.PinSubCategoryObject = Enum;
            }
            else
            {
                OutError = FString::Printf(
                    TEXT("prefix '%s:' is not one of object:, class:, struct:, enum:"), *Prefix);
                return false;
            }
        }
        else if (Key == TEXT("bool"))   { Out.PinCategory = UEdGraphSchema_K2::PC_Boolean; }
        else if (Key == TEXT("byte"))   { Out.PinCategory = UEdGraphSchema_K2::PC_Byte; }
        else if (Key == TEXT("int"))    { Out.PinCategory = UEdGraphSchema_K2::PC_Int; }
        else if (Key == TEXT("int64"))  { Out.PinCategory = UEdGraphSchema_K2::PC_Int64; }
        else if (Key == TEXT("float"))  { Out.PinCategory = UEdGraphSchema_K2::PC_Float; }
        else if (Key == TEXT("string")) { Out.PinCategory = UEdGraphSchema_K2::PC_String; }
        else if (Key == TEXT("name"))   { Out.PinCategory = UEdGraphSchema_K2::PC_Name; }
        else if (Key == TEXT("text"))   { Out.PinCategory = UEdGraphSchema_K2::PC_Text; }
        else
        {
            UScriptStruct* Struct = nullptr;
            if (Key == TEXT("vector"))           { Struct = TBaseStructure<FVector>::Get(); }
            else if (Key == TEXT("vector2d"))    { Struct = TBaseStructure<FVector2D>::Get(); }
            else if (Key == TEXT("rotator"))     { Struct = TBaseStructure<FRotator>::Get(); }
            else if (Key == TEXT("transform"))   { Struct = TBaseStructure<FTransform>::Get(); }
            else if (Key == TEXT("linearcolor")) { Struct = TBaseStructure<FLinearColor>::Get(); }
            if (Struct == nullptr)
            {
                OutError = FString::Printf(
                    TEXT("type '%s' is not supported. Supported: %s, plus object:<class>, class:<class>, struct:<path>, enum:<path>"),
                    *Type, *FString::Join(SimpleVariableTypes(), TEXT(", ")));
                return false;
            }
            Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
            Out.PinSubCategoryObject = Struct;
        }

        const FString Container = RawContainer.TrimStartAndEnd().ToLower();
        if (Container.IsEmpty() || Container == TEXT("none"))
        {
            Out.ContainerType = EPinContainerType::None;
        }
        else if (Container == TEXT("array"))
        {
            Out.ContainerType = EPinContainerType::Array;
        }
        else if (Container == TEXT("set"))
        {
            Out.ContainerType = EPinContainerType::Set;
        }
        else
        {
            OutError = FString::Printf(
                TEXT("container '%s' is not one of none, array, set"), *RawContainer);
            return false;
        }
        return true;
    }

    /** Unreal import text for a variable default.

        This string is consumed by FProperty::ImportText, not by the K2 schema,
        so a struct has to be in ExportText form: the comma form a graph pin
        takes is not a value ImportText can read. */
    bool VariableDefaultText(
        const FEdGraphPinType& PinType,
        const TSharedPtr<FJsonValue>& Value,
        FString& OutText,
        FString& OutError)
    {
        if (!Value.IsValid())
        {
            OutError = TEXT("default is missing");
            return false;
        }
        if (PinType.ContainerType != EPinContainerType::None)
        {
            OutError = TEXT("an array or set variable takes no default in this spec; set its entries from the graph");
            return false;
        }
        // Types are checked against the JSON type, not coerced from it. A
        // FJsonValue converts freely (the string "yes" answers true to
        // TryGetBool), and a default that quietly becomes something the caller
        // did not write is the failure this whole path exists to prevent.
        const FName Category = PinType.PinCategory;
        if (Category == UEdGraphSchema_K2::PC_Boolean)
        {
            if (Value->Type != EJson::Boolean)
            {
                OutError = TEXT("expects true or false");
                return false;
            }
            OutText = Value->AsBool() ? TEXT("true") : TEXT("false");
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Byte && PinType.PinSubCategoryObject.IsValid())
        {
            const UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject.Get());
            if (Value->Type != EJson::String
                || (Enum != nullptr && Enum->GetValueByNameString(Value->AsString()) == INDEX_NONE))
            {
                OutError = FString::Printf(TEXT("expects an enumerator name of %s"),
                    Enum != nullptr ? *Enum->GetName() : TEXT("its enum"));
                return false;
            }
            OutText = Value->AsString();
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Int
            || Category == UEdGraphSchema_K2::PC_Int64
            || Category == UEdGraphSchema_K2::PC_Byte)
        {
            if (Value->Type != EJson::Number
                || static_cast<double>(static_cast<int64>(Value->AsNumber())) != Value->AsNumber())
            {
                OutError = TEXT("expects a whole number");
                return false;
            }
            OutText = FString::Printf(TEXT("%lld"), static_cast<int64>(Value->AsNumber()));
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Float)
        {
            if (Value->Type != EJson::Number)
            {
                OutError = TEXT("expects a number");
                return false;
            }
            OutText = FString::SanitizeFloat(Value->AsNumber());
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_String
            || Category == UEdGraphSchema_K2::PC_Name
            || Category == UEdGraphSchema_K2::PC_Text)
        {
            if (Value->Type != EJson::String)
            {
                OutError = TEXT("expects a string");
                return false;
            }
            OutText = Value->AsString();
            if (Category == UEdGraphSchema_K2::PC_Text)
            {
                // A string and a name are assigned straight from the text; only
                // FText goes through ImportText, which wants a quoted literal
                // (BlueprintEditorUtils.cpp:8963).
                OutText.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
                OutText.ReplaceInline(TEXT("\""), TEXT("\\\""));
                OutText = TEXT("\"") + OutText + TEXT("\"");
            }
            return true;
        }
        if (Category == UEdGraphSchema_K2::PC_Struct)
        {
            const UScriptStruct* Struct = Cast<UScriptStruct>(PinType.PinSubCategoryObject.Get());
            if (Struct == nullptr)
            {
                OutError = TEXT("is a struct with no struct type");
                return false;
            }
            return StructTextFromJson(Struct, Value, /*bK2PinForm=*/false, OutText, OutError);
        }
        if (Category == UEdGraphSchema_K2::PC_Object
            || Category == UEdGraphSchema_K2::PC_Class
            || Category == UEdGraphSchema_K2::PC_SoftObject
            || Category == UEdGraphSchema_K2::PC_SoftClass)
        {
            if (Value->Type == EJson::Null)
            {
                OutText = TEXT("None");
                return true;
            }
            if (Value->Type != EJson::String)
            {
                OutError = TEXT("expects an object path string, or null");
                return false;
            }
            const FString Path = Value->AsString();
            if (LoadObject<UObject>(nullptr, *Path) == nullptr)
            {
                OutError = FString::Printf(TEXT("object '%s' could not be loaded"), *Path);
                return false;
            }
            OutText = Path;
            return true;
        }
        OutError = FString::Printf(
            TEXT("has pin category '%s', for which this builder sets no default"), *Category.ToString());
        return false;
    }

    /** Two pin types describe the same variable when their category, their
        subcategory object and their container agree. That is the comparison a
        rerun needs: anything else is a retype, which drops the graph nodes
        that read the variable and is refused rather than performed. */
    bool PinTypesMatch(const FEdGraphPinType& A, const FEdGraphPinType& B)
    {
        return A.PinCategory == B.PinCategory
            && A.PinSubCategory == B.PinSubCategory
            && A.PinSubCategoryObject.Get() == B.PinSubCategoryObject.Get()
            && A.ContainerType == B.ContainerType;
    }

    FString DescribePinType(const FEdGraphPinType& PinType)
    {
        FString Text = PinType.PinCategory.ToString();
        if (const UObject* Sub = PinType.PinSubCategoryObject.Get())
        {
            Text += TEXT(" ") + Sub->GetName();
        }
        if (PinType.ContainerType == EPinContainerType::Array) { Text += TEXT(" array"); }
        else if (PinType.ContainerType == EPinContainerType::Set) { Text += TEXT(" set"); }
        else if (PinType.ContainerType == EPinContainerType::Map) { Text += TEXT(" map"); }
        return Text;
    }

    FString SerializeReport(const TSharedPtr<FJsonObject>& Report)
    {
        FString Out;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Report.ToSharedRef(), Writer);
        return Out;
    }
}

TArray<FString> UBlueprintGraphBuilderLibrary::GetSupportedVariableTypes()
{
    TArray<FString> Types = SimpleVariableTypes();
    Types.Add(TEXT("object:"));
    Types.Add(TEXT("class:"));
    Types.Add(TEXT("struct:"));
    Types.Add(TEXT("enum:"));
    return Types;
}

FString UBlueprintGraphBuilderLibrary::ConfigureVariablesFromJSON(
    UBlueprint* Blueprint,
    const FString& VariablesJson,
    bool bValidateOnly)
{
    TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Results;

    auto Finish = [&]() -> FString
    {
        Report->SetBoolField(TEXT("success"), Errors.Num() == 0);
        Report->SetArrayField(TEXT("errors"), Errors);
        Report->SetArrayField(TEXT("variables"), Results);
        return SerializeReport(Report);
    };

    TArray<TSharedPtr<FJsonValue>> Specs;
    {
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(VariablesJson);
        if (!FJsonSerializer::Deserialize(Reader, Specs))
        {
            Errors.Add(MakeShared<FJsonValueString>(TEXT("variables must be a JSON array.")));
            return Finish();
        }
    }

    // --- Validation. Runs to completion before anything is added, so a
    // rejected spec leaves the Blueprint exactly as it was. ---
    struct FValidatedVariable
    {
        FName Name;
        FString TypeSpec;
        FEdGraphPinType PinType;
        FString Category;
        bool bHasDefault = false;
        FString DefaultText;
        bool bExists = false;
    };
    TArray<FValidatedVariable> Validated;
    TSet<FName> SeenNames;

    for (const TSharedPtr<FJsonValue>& SpecValue : Specs)
    {
        const TSharedPtr<FJsonObject>* Entry = nullptr;
        if (!SpecValue->TryGetObject(Entry))
        {
            Errors.Add(MakeShared<FJsonValueString>(TEXT("Every entry of variables must be an object.")));
            return Finish();
        }
        FValidatedVariable Variable;
        FString RawName;
        FString RawContainer;
        (*Entry)->TryGetStringField(TEXT("name"), RawName);
        (*Entry)->TryGetStringField(TEXT("type"), Variable.TypeSpec);
        (*Entry)->TryGetStringField(TEXT("container"), RawContainer);
        (*Entry)->TryGetStringField(TEXT("category"), Variable.Category);
        RawName = RawName.TrimStartAndEnd();

        if (RawName.IsEmpty())
        {
            Errors.Add(MakeShared<FJsonValueString>(TEXT("Every variable needs a non-empty name.")));
            return Finish();
        }
        for (TCHAR Character : RawName)
        {
            if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Variable name '%s' may only hold letters, digits and underscores."), *RawName)));
                return Finish();
            }
        }
        Variable.Name = FName(*RawName);
        if (SeenNames.Contains(Variable.Name))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("Duplicate variable name in the spec: %s"), *RawName)));
            return Finish();
        }
        SeenNames.Add(Variable.Name);

        FString TypeError;
        if (!PinTypeFromSpec(Variable.TypeSpec, RawContainer, Variable.PinType, TypeError))
        {
            Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("Variable '%s': %s."), *RawName, *TypeError)));
            return Finish();
        }

        if ((*Entry)->HasField(TEXT("default")))
        {
            FString DefaultError;
            if (!VariableDefaultText(
                    Variable.PinType, (*Entry)->TryGetField(TEXT("default")),
                    Variable.DefaultText, DefaultError))
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Variable '%s' default: %s."), *RawName, *DefaultError)));
                return Finish();
            }
            Variable.bHasDefault = true;
        }

        if (Blueprint != nullptr)
        {
            const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
                [&Variable](const FBPVariableDescription& Description)
                {
                    return Description.VarName == Variable.Name;
                });
            if (Index != INDEX_NONE)
            {
                Variable.bExists = true;
                const FEdGraphPinType& Existing = Blueprint->NewVariables[Index].VarType;
                if (!PinTypesMatch(Existing, Variable.PinType))
                {
                    Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("Variable '%s' already exists as %s; the spec asks for %s. Retyping is not done implicitly: it would drop every graph node that reads it."),
                        *RawName, *DescribePinType(Existing), *DescribePinType(Variable.PinType))));
                    return Finish();
                }
            }
            else if (Blueprint->ParentClass != nullptr
                && FindFProperty<FProperty>(Blueprint->ParentClass, Variable.Name) != nullptr)
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Variable '%s' is already a property of the parent class %s; a Blueprint variable cannot mask it."),
                    *RawName, *Blueprint->ParentClass->GetName())));
                return Finish();
            }
        }
        Validated.Add(Variable);
    }

    if (bValidateOnly)
    {
        for (const FValidatedVariable& Variable : Validated)
        {
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("name"), Variable.Name.ToString());
            Result->SetStringField(TEXT("type"), Variable.TypeSpec);
            Result->SetBoolField(TEXT("created"), !Variable.bExists);
            Result->SetBoolField(TEXT("default_applied"), false);
            Results.Add(MakeShared<FJsonValueObject>(Result));
        }
        return Finish();
    }

    if (Blueprint == nullptr)
    {
        Errors.Add(MakeShared<FJsonValueString>(TEXT("A Blueprint is required to apply variables.")));
        return Finish();
    }

    // --- Mutation. ---
    for (const FValidatedVariable& Variable : Validated)
    {
        bool bCreated = false;
        if (!Variable.bExists)
        {
            // AddMemberVariable takes the default as import text and calls
            // MarkBlueprintAsStructurallyModified, which recompiles the
            // skeleton, so a VariableGet or VariableSet node built later in
            // this same request can resolve the variable.
            if (!FBlueprintEditorUtils::AddMemberVariable(
                    Blueprint, Variable.Name, Variable.PinType, Variable.DefaultText))
            {
                Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("Variable '%s' could not be added."), *Variable.Name.ToString())));
                continue;
            }
            bCreated = true;
        }
        else if (Variable.bHasDefault)
        {
            // A rerun with a changed default has to converge. The description
            // string alone does not do it: recompiling copies the old CDO's
            // values onto the new CDO to preserve user edits, so the CDO is
            // written directly first, the way the Details panel does it.
            const int32 Index = Blueprint->NewVariables.IndexOfByPredicate(
                [&Variable](const FBPVariableDescription& Description)
                {
                    return Description.VarName == Variable.Name;
                });
            if (Index != INDEX_NONE)
            {
                Blueprint->NewVariables[Index].DefaultValue = Variable.DefaultText;
            }
            UClass* GeneratedClass = Blueprint->GeneratedClass;
            UObject* DefaultObject = GeneratedClass != nullptr
                ? GeneratedClass->GetDefaultObject(/*bCreateIfNeeded=*/false)
                : nullptr;
            FProperty* Property = GeneratedClass != nullptr
                ? FindFProperty<FProperty>(GeneratedClass, Variable.Name)
                : nullptr;
            if (DefaultObject != nullptr && Property != nullptr)
            {
                DefaultObject->Modify();
                // The same parser the compiler will use on the description
                // string. Reaching for FProperty::ImportText here instead would
                // mean the text has to satisfy two different parsers that
                // disagree about FVector, FRotator, FLinearColor and FTransform.
                if (!FBlueprintEditorUtils::PropertyValueFromString(
                        Property, Variable.DefaultText, reinterpret_cast<uint8*>(DefaultObject), DefaultObject))
                {
                    Errors.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("Variable '%s' default '%s' could not be parsed by reflection."),
                        *Variable.Name.ToString(), *Variable.DefaultText)));
                }
            }
        }

        if (!Variable.Category.IsEmpty())
        {
            FBlueprintEditorUtils::SetBlueprintVariableCategory(
                Blueprint, Variable.Name, /*InStructScope=*/nullptr, FText::FromString(Variable.Category));
        }

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("name"), Variable.Name.ToString());
        Result->SetStringField(TEXT("type"), Variable.TypeSpec);
        Result->SetBoolField(TEXT("created"), bCreated);
        Result->SetBoolField(TEXT("default_applied"), Variable.bHasDefault);
        Results.Add(MakeShared<FJsonValueObject>(Result));
    }
    return Finish();
}

/** The node-creation primitive, lifted out of the build loop so that patching
    can spawn a node without rebuilding a graph around it.

    It was inline in BuildBlueprintFromJSONWithReport, which meant the only way
    to create one node the way the builder creates them was to run the builder,
    and the only way to run the builder was to hand it a whole graph. Moving it
    here is what lets add_node in a patch produce a node identical to one
    blueprint_build would have produced, inside the caller's own transaction,
    compile and verification boundary rather than a second one.

    Returns the node, or nullptr with the reason appended to OutFailedNodes in
    the same wording the build report already used. Nothing else changed: the
    dispatch, the routing keys, the unknown-parameter refusal and the position
    override are the original code, and the `continue` statements that skipped a
    node in the loop are now the `return nullptr` that reports one. */
static UEdGraphNode* SpawnNodeFromSpec(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& NodeObjRef,
    const FString& NodeId,
    const FString& NodeType,
    TArray<FString>& OutFailedNodes)
{
    // Named so the moved body reads exactly as it did inside the loop, where
    // NodeObj was a pointer into the parsed spec array.
    const TSharedPtr<FJsonObject>* NodeObj = &NodeObjRef;

    UEdGraphNode* SpawnedNode = nullptr;

    TSharedPtr<FJsonObject> Params;
    {
        const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
        if ((*NodeObj)->TryGetObjectField(TEXT("params"), ParamsObj))
        {
            Params = *ParamsObj;
        }
    }
    // Params consumed by the dispatch itself rather than as pin defaults.
    TSet<FString> RoutingKeys;

    if (NodeType == TEXT("BeginPlay"))
    {
        SpawnedNode = SpawnOverrideEvent(*Graph, TEXT("ReceiveBeginPlay"), 0, 0);
    }
    else if (NodeType == TEXT("Tick"))
    {
        SpawnedNode = SpawnOverrideEvent(*Graph, TEXT("ReceiveTick"), 0, 600);
    }
    else if (NodeType == TEXT("ActorBeginOverlap"))
    {
        SpawnedNode = SpawnOverrideEvent(*Graph, TEXT("ReceiveActorBeginOverlap"), 0, 200);
    }
    else if (NodeType == TEXT("ActorEndOverlap"))
    {
        SpawnedNode = SpawnOverrideEvent(*Graph, TEXT("ReceiveActorEndOverlap"), 0, 400);
    }
    else if (NodeType == TEXT("PrintString") || NodeType == TEXT("Delay")
        || NodeType == TEXT("CallFunction") || NodeType == TEXT("Operator"))
    {
        UClass* TargetClass = nullptr;
        FString FuncName;
        if (NodeType == TEXT("PrintString"))
        {
            TargetClass = UKismetSystemLibrary::StaticClass();
            FuncName = TEXT("PrintString");
        }
        else if (NodeType == TEXT("Delay"))
        {
            TargetClass = UKismetSystemLibrary::StaticClass();
            FuncName = TEXT("Delay");
        }
        else if (NodeType == TEXT("Operator"))
        {
            FString Op;
            if (Params.IsValid())
            {
                Params->TryGetStringField(TEXT("op"), Op);
            }
            RoutingKeys.Add(TEXT("op"));
            const FOperatorEntry* Entry = FindOperator(Op);
            if (Entry == nullptr)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("BuildBlueprintFromJSON: node '%s' names no known operator ('%s')"),
                    *NodeId, *Op);
                return nullptr;
            }
            TargetClass = ResolveGraphClass(Entry->ClassName);
            FuncName = Entry->FunctionName;
        }
        else
        {
            FString ClassName;
            if (Params.IsValid())
            {
                Params->TryGetStringField(TEXT("class"), ClassName);
                Params->TryGetStringField(TEXT("function"), FuncName);
            }
            RoutingKeys.Add(TEXT("class"));
            RoutingKeys.Add(TEXT("function"));
            TargetClass = ResolveGraphClass(ClassName);
            if (TargetClass == nullptr)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("BuildBlueprintFromJSON: node '%s' class '%s' not found for CallFunction"),
                    *NodeId, *ClassName);
                return nullptr;
            }
        }

        UFunction* Func = TargetClass != nullptr ? TargetClass->FindFunctionByName(*FuncName) : nullptr;
        if (Func == nullptr)
        {
            UE_LOG(LogTemp, Error,
                TEXT("BuildBlueprintFromJSON: node '%s' function '%s' not found on '%s'"),
                *NodeId, *FuncName,
                TargetClass != nullptr ? *TargetClass->GetName() : TEXT("<null class>"));
            return nullptr;
        }

        FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
        UK2Node_CallFunction* CallNode = Creator.CreateNode();
        CallNode->SetFromFunction(Func);
        CallNode->NodePosX = 300;
        CallNode->NodePosY = 200;
        Creator.Finalize();
        SpawnedNode = CallNode;
    }
    else if (NodeType == TEXT("Branch"))
    {
        FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
        UK2Node_IfThenElse* Node = Creator.CreateNode();
        Creator.Finalize();
        SpawnedNode = Node;
    }
    else if (NodeType == TEXT("Sequence"))
    {
        FGraphNodeCreator<UK2Node_ExecutionSequence> Creator(*Graph);
        UK2Node_ExecutionSequence* Node = Creator.CreateNode();
        Creator.Finalize();
        int32 NumOutputs = 2;
        if (Params.IsValid())
        {
            Params->TryGetNumberField(TEXT("num_outputs"), NumOutputs);
        }
        RoutingKeys.Add(TEXT("num_outputs"));
        auto CountExecOutputs = [](UK2Node_ExecutionSequence* N)
        {
            int32 Count = 0;
            for (UEdGraphPin* P : N->Pins)
            {
                if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    Count++;
                }
            }
            return Count;
        };
        while (CountExecOutputs(Node) < NumOutputs)
        {
            Node->AddInputPin();
        }
        SpawnedNode = Node;
    }
    else if (NodeType == TEXT("Comment"))
    {
        FGraphNodeCreator<UEdGraphNode_Comment> Creator(*Graph);
        UEdGraphNode_Comment* Node = Creator.CreateNode();
        Creator.Finalize();
        if (Params.IsValid())
        {
            FString Text;
            if (Params->TryGetStringField(TEXT("text"), Text))
            {
                Node->NodeComment = Text;
            }
            int32 W = 400, H = 200;
            Params->TryGetNumberField(TEXT("width"), W);
            Params->TryGetNumberField(TEXT("height"), H);
            Node->NodeWidth = W;
            Node->NodeHeight = H;
        }
        RoutingKeys.Append({ TEXT("text"), TEXT("width"), TEXT("height") });
        SpawnedNode = Node;
    }
    else if (FBPNodeRegistry::FFactory Factory = FBPNodeRegistry::Find(NodeType))
    {
        // The mutator's factory table. Its config keys are routing, not pin
        // defaults, so every key the factory reads is skipped afterwards.
        SpawnedNode = Factory(Graph, FVector2D(600.0f, 200.0f), RegistryConfigJson(Params));
        if (SpawnedNode == nullptr)
        {
            UE_LOG(LogTemp, Error,
                TEXT("BuildBlueprintFromJSON: node '%s' of type '%s' was refused by its factory; see the LogBlueprintMutator line above for the reason"),
                *NodeId, *NodeType);
            // This used to `continue` silently, which is what let a refused
            // node stay in the reported count. The supplied keys are named
            // because the usual cause is a key the factory does not read:
            // VariableGet takes varName, and a spec written with "variable"
            // produces exactly this, with no other symptom.
            TArray<FString> SuppliedKeys;
            if (Params.IsValid())
            {
                for (const auto& Pair : Params->Values) { SuppliedKeys.Add(Pair.Key); }
                SuppliedKeys.Sort();
            }
            OutFailedNodes.Add(FString::Printf(
                TEXT("%s: %s: factory_refused: the node factory created nothing. Supplied "
                     "parameters: [%s]. A parameter this node type does not read is ignored, "
                     "and a missing required one makes the factory refuse."),
                *NodeId, *NodeType,
                SuppliedKeys.Num() > 0 ? *FString::Join(SuppliedKeys, TEXT(", ")) : TEXT("none")));
            return nullptr;
        }
        RoutingKeys.Append({
            TEXT("var_name"), TEXT("scope"), TEXT("target_class"), TEXT("function_name"),
            TEXT("event_name"), TEXT("parent_class"), TEXT("parameters"), TEXT("purity"),
            TEXT("actor_class"), TEXT("widget_class"), TEXT("struct_type"),
            TEXT("num_outputs"), TEXT("num_inputs"), TEXT("start_index"), TEXT("num_cases"),
            TEXT("has_default"), TEXT("case_values"), TEXT("is_case_sensitive"),
            TEXT("is_random"), TEXT("loop"), TEXT("start_index_from_zero"),
            TEXT("text"), TEXT("width"), TEXT("height"), TEXT("color"),
            TEXT("fkey_name"), TEXT("consume_input"), TEXT("execute_when_paused"),
            TEXT("override_parent"),
        });
        // A variable set node names its value pin after the variable, which
        // a caller writing a spec should not have to repeat.
        if (NodeType == TEXT("VariableSet") && Params.IsValid() && Params->HasField(TEXT("value")))
        {
            FString VarName;
            Params->TryGetStringField(TEXT("var_name"), VarName);
            FString Error;
            if (!ApplyPinDefaultAndNotify(SpawnedNode, SpawnedNode->FindPin(FName(*VarName)),
                    Params->TryGetField(TEXT("value")), Error))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("BuildBlueprintFromJSON: node '%s' value for variable '%s' %s"),
                    *NodeId, *VarName, *Error);
            }
            RoutingKeys.Add(TEXT("value"));
        }
    }
    else
    {
        // Reached only when GetSupportedNodeTypes names a type that has
        // neither a case above nor a registry factory, which is the drift
        // case that list exists to make visible.
        UE_LOG(LogTemp, Error,
            TEXT("BuildBlueprintFromJSON: Node type '%s' is advertised by GetSupportedNodeTypes but has no dispatch case"),
            *NodeType);
        return nullptr;
    }

    // An unknown authoring key is refused, not ignored. It is almost always
    // a misspelling of a real one, and applying the rest of the spec around
    // it produces a node that looks authored and is not - which is how the
    // VariableGet "variable" case cost nothing and stayed invisible.
    // Matching is normalised (underscores and case folded) so the
    // snake_case routing vocabulary and the camelCase factory config are
    // one accepted-name set; see NormalizeParamKey.
    TArray<FString> UnknownParams;
    ApplyParamsAsPinDefaults(SpawnedNode, Params, RoutingKeys, NodeId, UnknownParams);
    if (UnknownParams.Num() > 0 && SpawnedNode != nullptr)
    {
        UnknownParams.Sort();
        OutFailedNodes.Add(FString::Printf(
            TEXT("%s: %s: unknown_parameter: [%s] is not accepted by this node type. "
                 "Accepted parameters: [%s]."),
            *NodeId, *NodeType,
            *FString::Join(UnknownParams, TEXT(", ")),
            *DescribeAcceptedParams(SpawnedNode, RoutingKeys)));
        SpawnedNode->GetGraph()->RemoveNode(SpawnedNode);
        return nullptr;
    }

    // Per-node position override (top-level "x" / "y" on the node spec)
    if (SpawnedNode)
    {
        int32 PosX, PosY;
        if ((*NodeObj)->TryGetNumberField(TEXT("x"), PosX)) SpawnedNode->NodePosX = PosX;
        if ((*NodeObj)->TryGetNumberField(TEXT("y"), PosY)) SpawnedNode->NodePosY = PosY;
    }

    // A null return means the factory refused the spec: an unknown config
    // key, a missing required parameter, a class that would not resolve.
    // Recording it as a created node is what produced phantom counts, so
    // the id is reported as a failure and kept out of the map. A node that
    // landed in a different graph is treated the same way: it is not the
    // graph the caller asked to author.
    if (SpawnedNode == nullptr)
    {
        OutFailedNodes.Add(FString::Printf(
            TEXT("%s: %s: the node factory created nothing. Check the parameter names for "
                 "this node type: a key the factory does not read is ignored, and a missing "
                 "required key makes it refuse."),
            *NodeId, *NodeType));
        return nullptr;
    }
    if (SpawnedNode->GetGraph() != Graph)
    {
        OutFailedNodes.Add(FString::Printf(
            TEXT("%s: %s: the node was created in a different graph than the one requested."),
            *NodeId, *NodeType));
        return nullptr;
    }
    return SpawnedNode;
}

void UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON(
    UBlueprint* Blueprint,
    const FString& JsonString,
    bool bClearExistingGraph)
{
    // The Blueprint-callable entry point keeps its old shape. Everything that
    // needs to know whether the graph came out whole goes through the
    // reporting overload; this one throws the report away, as it always did.
    TArray<FString> UnresolvedConnections;
    int32 ConnectionsMade = 0;
    TArray<FString> FailedNodes;
    int32 CreatedNodes = 0;
    BuildBlueprintFromJSONWithReport(
        Blueprint, JsonString, bClearExistingGraph, UnresolvedConnections, ConnectionsMade,
        FailedNodes, CreatedNodes);
}

/**
 * One pin lookup for the whole connection vocabulary, shared by the builder and
 * by graph_patch so a build and a patch address the same pin by the same word.
 *
 *   "exec"      direction-aware: output side is PN_Then, input side PN_Execute
 *   "then"      always PN_Then
 *   "AsResult"  a dynamic cast's result pin, asked for by the node, because it
 *               is named "As" plus the target type's DISPLAY name
 *               (K2Node_DynamicCast.cpp:63) and no caller can compute that
 *   otherwise   the literal pin name, then the pin's DISPLAY name
 *
 * The display-name fallback is not a convenience. A latent function's output
 * exec pin is PN_Then carrying a friendly name of "Completed"
 * (K2Node_CallFunction.cpp:880), so every Delay, every timeline-style latent
 * call and every async action in the engine shows the caller a pin called
 * Completed that no name lookup can find. Without this, the documented
 * vocabulary cannot express the one connection those nodes exist to make.
 */
static UEdGraphPin* ResolveGraphPinByRole(
    UEdGraphNode* Node,
    const FString& Role,
    EEdGraphPinDirection Direction)
{
    if (Node == nullptr) { return nullptr; }
    if (Role == TEXT("exec"))
    {
        return Node->FindPin(Direction == EGPD_Output
            ? UEdGraphSchema_K2::PN_Then : UEdGraphSchema_K2::PN_Execute);
    }
    if (Role == TEXT("then"))
    {
        return Node->FindPin(UEdGraphSchema_K2::PN_Then);
    }
    if (Role == TEXT("AsResult"))
    {
        if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            return CastNode->GetCastResultPin();
        }
    }
    if (UEdGraphPin* ByName = Node->FindPin(FName(*Role), Direction))
    {
        return ByName;
    }
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin == nullptr || Pin->Direction != Direction || Pin->PinFriendlyName.IsEmpty())
        {
            continue;
        }
        if (Pin->PinFriendlyName.ToString().Equals(Role, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }
    // Direction-blind last resort, which is what this lookup always was.
    return Node->FindPin(FName(*Role));
}

void UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSONWithReport(
    UBlueprint* Blueprint,
    const FString& JsonString,
    bool bClearExistingGraph,
    TArray<FString>& OutUnresolvedConnections,
    int32& OutConnectionsMade,
    TArray<FString>& OutFailedNodes,
    int32& OutCreatedNodes)
{
    OutUnresolvedConnections.Reset();
    OutConnectionsMade = 0;
    OutFailedNodes.Reset();
    OutCreatedNodes = 0;

    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Blueprint is null"));
        return;
    }

    // --- Step 1: Parse JSON ---
    TSharedPtr<FJsonObject> RootObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
    if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Invalid JSON: %s"), *JsonString);
        return;
    }

    // --- Step 2: Get event graph ---
    UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
    if (!Graph && Blueprint->UbergraphPages.Num() > 0)
    {
        Graph = Blueprint->UbergraphPages[0];
    }
    if (!Graph)
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: No event graph found on Blueprint"));
        return;
    }

    // --- Step 3: Defer clearing the existing nodes ---
    //
    // The old nodes used to be destroyed here, before a single new one existed.
    // That made a failing build destructive: an unresolvable connection was
    // detected further down, by which point the previous graph was already
    // gone and nothing could put it back. Rolling that back afterwards was
    // tried and is ruled out - cloning a live event graph crashed the editor
    // inside StaticDuplicateObjectEx (findings 0k).
    //
    // So the old nodes are kept until the replacement is known to be whole.
    // New nodes are spawned alongside them and connections resolve only
    // against the new ones, because NodeMap is keyed by spec id and the old
    // nodes were never in it. At the end, exactly one set is deleted: the old
    // one on success, the new one on failure. Do not destroy until the thing
    // that might fail has succeeded.
    TArray<UEdGraphNode*> DeferredNodesToRemove;
    if (bClearExistingGraph)
    {
        DeferredNodesToRemove = Graph->Nodes;
    }

    // --- Step 4: Spawn nodes ---
    const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
    if (!RootObject->TryGetArrayField(TEXT("nodes"), NodesArray))
    {
        UE_LOG(LogTemp, Error, TEXT("BuildBlueprintFromJSON: Missing 'nodes' array"));
        return;
    }

    TMap<FString, UEdGraphNode*> NodeMap;

    for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
    {
        const TSharedPtr<FJsonObject>* NodeObj = nullptr;
        if (!NodeValue->TryGetObject(NodeObj))
        {
            continue;
        }

        FString NodeId, NodeType;
        (*NodeObj)->TryGetStringField(TEXT("id"), NodeId);
        (*NodeObj)->TryGetStringField(TEXT("type"), NodeType);

        if (NodeId.IsEmpty() || NodeType.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Skipping node with missing id or type"));
            continue;
        }

        if (!GetSupportedNodeTypes().Contains(NodeType))
        {
            UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Unknown node type '%s', skipping"), *NodeType);
            continue;
        }

        UEdGraphNode* SpawnedNode =
            SpawnNodeFromSpec(Graph, *NodeObj, NodeId, NodeType, OutFailedNodes);
        if (SpawnedNode == nullptr)
        {
            continue;
        }
        NodeMap.Add(NodeId, SpawnedNode);
        OutCreatedNodes++;
    }

    // --- Step 5: Wire connections ---
    const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
    if (RootObject->TryGetArrayField(TEXT("connections"), ConnectionsArray))
    {
        for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
        {
            const TSharedPtr<FJsonObject>* ConnObj = nullptr;
            if (!ConnValue->TryGetObject(ConnObj))
            {
                OutUnresolvedConnections.Add(
                    TEXT("a connections entry that is not an object"));
                continue;
            }

            FString FromStr, ToStr;
            (*ConnObj)->TryGetStringField(TEXT("from"), FromStr);
            (*ConnObj)->TryGetStringField(TEXT("to"), ToStr);

            // Parse "nodeId.pinRole"
            FString FromNodeId, FromPinRole, ToNodeId, ToPinRole;
            if (!FromStr.Split(TEXT("."), &FromNodeId, &FromPinRole)
                || !ToStr.Split(TEXT("."), &ToNodeId, &ToPinRole))
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Connection endpoints must read nodeId.pinRole: %s -> %s"), *FromStr, *ToStr);
                OutUnresolvedConnections.Add(FString::Printf(
                    TEXT("%s -> %s (an endpoint does not read nodeId.pinRole)"), *FromStr, *ToStr));
                continue;
            }

            UEdGraphNode** FromNodePtr = NodeMap.Find(FromNodeId);
            UEdGraphNode** ToNodePtr = NodeMap.Find(ToNodeId);
            if (!FromNodePtr || !ToNodePtr)
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Connection references unknown node(s): %s -> %s"), *FromStr, *ToStr);
                OutUnresolvedConnections.Add(FString::Printf(
                    TEXT("%s -> %s (node '%s' spawned no node)"),
                    *FromStr, *ToStr, !FromNodePtr ? *FromNodeId : *ToNodeId));
                continue;
            }

            UEdGraphPin* SourcePin = ResolveGraphPinByRole(*FromNodePtr, FromPinRole, EGPD_Output);
            UEdGraphPin* TargetPin = ResolveGraphPinByRole(*ToNodePtr, ToPinRole, EGPD_Input);

            if (!SourcePin || !TargetPin)
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Could not resolve pins for connection %s -> %s"), *FromStr, *ToStr);
                // Name the pins that ARE there, on the side that failed. A
                // caller told only that their role matched nothing has to open
                // the editor to find out what to write instead.
                UEdGraphNode* MissingOn = !SourcePin ? *FromNodePtr : *ToNodePtr;
                const EEdGraphPinDirection MissingDir = !SourcePin ? EGPD_Output : EGPD_Input;
                TArray<FString> Available;
                for (const UEdGraphPin* Pin : MissingOn->Pins)
                {
                    if (Pin == nullptr || Pin->Direction != MissingDir) { continue; }
                    Available.Add(Pin->PinFriendlyName.IsEmpty()
                        ? Pin->PinName.ToString()
                        : FString::Printf(TEXT("%s (shown as %s)"),
                            *Pin->PinName.ToString(), *Pin->PinFriendlyName.ToString()));
                }
                Available.Sort();
                OutUnresolvedConnections.Add(FString::Printf(
                    TEXT("%s -> %s (no %s pin '%s' on %s; it has: %s)"),
                    *FromStr, *ToStr,
                    !SourcePin ? TEXT("output") : TEXT("input"),
                    !SourcePin ? *FromPinRole : *ToPinRole,
                    !SourcePin ? *FromNodeId : *ToNodeId,
                    Available.Num() > 0 ? *FString::Join(Available, TEXT(", ")) : TEXT("no pins of that direction")));
                continue;
            }

            SourcePin->MakeLinkTo(TargetPin);
            OutConnectionsMade++;

            // MakeLinkTo moves the pointers and stops. The graph editor does
            // more: UEdGraphSchema_K2::TryCreateConnection notifies both ends,
            // and a node that types a wildcard pin from what it is wired to
            // does that work there. UK2Node_DynamicCast::NotifyPinConnectionListChanged
            // (K2Node_DynamicCast.cpp:347) is the case that matters: without
            // this its Object pin stays PC_Wildcard and the compile fails with
            // "The type of Object is undetermined". The base override also
            // clears a connected input pin's literal, which is what the editor
            // does and what an unread default deserves. Limitation 26.
            if (!SourcePin->IsPendingKill())
            {
                (*FromNodePtr)->PinConnectionListChanged(SourcePin);
            }
            if (!TargetPin->IsPendingKill())
            {
                (*ToNodePtr)->PinConnectionListChanged(TargetPin);
            }
        }
    }

    // --- Step 5b: Commit or abort the deferred replacement ---
    //
    // Exactly one set of nodes is deleted here. On success the old graph goes
    // and the new one stands; on failure the new nodes go and the caller's
    // graph is left exactly as it was found, which is what makes a failing
    // build non-destructive instead of something rollback has to repair.
    {
        const bool bGraphIsWhole =
            OutFailedNodes.Num() == 0 && OutUnresolvedConnections.Num() == 0;
        if (bGraphIsWhole)
        {
            for (UEdGraphNode* Node : DeferredNodesToRemove)
            {
                if (Node != nullptr)
                {
                    FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile=*/true);
                }
            }
        }
        else
        {
            // Unconditional on purpose. This used to be guarded by
            // DeferredNodesToRemove.Num() > 0, which made non-destruction a
            // property of clear_existing_graph rather than of failure: an
            // additive build (clear_existing_graph false) that failed left its
            // half-built nodes wired into the caller's graph, because there
            // were no deferred nodes to notice. Same defect as 0k, one branch
            // over. A failed build discards what it made, whatever mode it ran
            // in.
            for (const TPair<FString, UEdGraphNode*>& Spawned : NodeMap)
            {
                if (Spawned.Value != nullptr)
                {
                    FBlueprintEditorUtils::RemoveNode(Blueprint, Spawned.Value, /*bDontRecompile=*/true);
                }
            }
            NodeMap.Reset();
            OutCreatedNodes = 0;
            OutConnectionsMade = 0;
            UE_LOG(LogTemp, Warning,
                TEXT("BuildBlueprintFromJSON: the replacement graph was not whole, so it was "
                     "discarded and the existing graph left untouched (%d node failure(s), "
                     "%d unresolved connection(s))"),
                OutFailedNodes.Num(), OutUnresolvedConnections.Num());
        }
    }

    // --- Step 6: Mark and compile ---
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    Blueprint->MarkPackageDirty();

    UE_LOG(LogTemp, Log, TEXT("BuildBlueprintFromJSON: Done. %d nodes spawned, %d connections made, %d unresolved."),
        NodeMap.Num(), OutConnectionsMade, OutUnresolvedConnections.Num());
}

bool UBlueprintGraphBuilderLibrary::AddComponentToBlueprint(
    UBlueprint* Blueprint,
    TSubclassOf<UActorComponent> ComponentClass,
    const FString& ComponentName,
    const FString& AttachToName)
{
    if (!Blueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Blueprint is null"));
        return false;
    }

    if (!ComponentClass)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: ComponentClass is null"));
        return false;
    }

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Blueprint has no SimpleConstructionScript"));
        return false;
    }

    // Create the SCS node
    USCS_Node* NewNode = SCS->CreateNode(ComponentClass, *ComponentName);
    if (!NewNode)
    {
        UE_LOG(LogTemp, Error, TEXT("AddComponentToBlueprint: Failed to create SCS node for %s"), *ComponentName);
        return false;
    }

    // Attach to parent or add as root
    if (!AttachToName.IsEmpty())
    {
        // Find the parent node
        TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();
        USCS_Node* ParentNode = nullptr;
        for (USCS_Node* Node : AllNodes)
        {
            if (Node && Node->GetVariableName().ToString() == AttachToName)
            {
                ParentNode = Node;
                break;
            }
        }

        if (ParentNode)
        {
            ParentNode->AddChildNode(NewNode);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("AddComponentToBlueprint: Parent '%s' not found, adding as root"), *AttachToName);
            SCS->AddNode(NewNode);
        }
    }
    else
    {
        SCS->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogTemp, Log, TEXT("AddComponentToBlueprint: Added '%s' (%s) to %s"),
        *ComponentName, *ComponentClass->GetName(), *Blueprint->GetName());

    return true;
}

bool UBlueprintGraphBuilderLibrary::SetComponentProperty(
    UBlueprint* Blueprint,
    const FString& ComponentName,
    const FString& PropertyName,
    const FString& JsonValue)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Blueprint or SCS is null"));
        return false;
    }

    // Find the SCS node by name
    TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    USCS_Node* TargetNode = nullptr;
    for (USCS_Node* Node : AllNodes)
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            TargetNode = Node;
            break;
        }
    }

    if (!TargetNode)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Component '%s' not found"), *ComponentName);
        return false;
    }

    UActorComponent* Template = TargetNode->ComponentTemplate;
    if (!Template)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Component '%s' has no template"), *ComponentName);
        return false;
    }

    // Find the property by name
    FProperty* Property = Template->GetClass()->FindPropertyByName(*PropertyName);
    if (!Property)
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Property '%s' not found on %s"),
            *PropertyName, *Template->GetClass()->GetName());
        return false;
    }

    // Set the property value from string using ImportText (works in UE4.27)
    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Template);
    if (!Property->ImportText(*JsonValue, ValuePtr, 0, Template))
    {
        UE_LOG(LogTemp, Error, TEXT("SetComponentProperty: Failed to set '%s' from value: %s"),
            *PropertyName, *JsonValue);
        return false;
    }

    Template->MarkPackageDirty();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

    UE_LOG(LogTemp, Log, TEXT("SetComponentProperty: Set %s.%s = %s"),
        *ComponentName, *PropertyName, *JsonValue);

    return true;
}

FString UBlueprintGraphBuilderLibrary::CompileAndReport(UBlueprint* Blueprint)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors;
    TArray<TSharedPtr<FJsonValue>> Warnings;

    auto ToJson = [&](bool bSuccess, const FString& StatusLabel) -> FString
    {
        Root->SetBoolField(TEXT("success"), bSuccess);
        Root->SetStringField(TEXT("status"), StatusLabel);
        Root->SetArrayField(TEXT("errors"), Errors);
        Root->SetArrayField(TEXT("warnings"), Warnings);
        FString Out;
        TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
        FJsonSerializer::Serialize(Root.ToSharedRef(), W);
        return Out;
    };

    if (!Blueprint)
    {
        Errors.Add(MakeShared<FJsonValueString>(TEXT("blueprint null")));
        return ToJson(false, TEXT("Error"));
    }

    FCompilerResultsLog Results;
    Results.SetSourcePath(Blueprint->GetPathName());
    Results.BeginEvent(TEXT("CompileAndReport"));
    FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None, &Results);
    Results.EndEvent();

    for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
    {
        const FString Text = Msg->ToText().ToString();
        switch (Msg->GetSeverity())
        {
            case EMessageSeverity::Error:
            case EMessageSeverity::CriticalError:
                Errors.Add(MakeShared<FJsonValueString>(Text));
                break;
            case EMessageSeverity::Warning:
            case EMessageSeverity::PerformanceWarning:
                Warnings.Add(MakeShared<FJsonValueString>(Text));
                break;
            default:
                break;
        }
    }

    FString StatusLabel;
    switch (Blueprint->Status)
    {
        case BS_UpToDate:                 StatusLabel = TEXT("UpToDate"); break;
        case BS_UpToDateWithWarnings:     StatusLabel = TEXT("UpToDateWithWarnings"); break;
        case BS_Error:                    StatusLabel = TEXT("Error"); break;
        case BS_Dirty:                    StatusLabel = TEXT("Dirty"); break;
        case BS_BeingCreated:             StatusLabel = TEXT("BeingCreated"); break;
        default:                          StatusLabel = TEXT("Unknown"); break;
    }

    const bool bSuccess = (Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
    return ToJson(bSuccess, StatusLabel);
}

namespace
{
    /** The operator name whose table entry produced this function call, or an
        empty string. The forward direction resolves a name to a class and a
        function; this walks the same single table backwards, so a table entry
        added or removed changes both directions at once. */
    FString OperatorNameFor(const UClass* OwningClass, const FName& FunctionName)
    {
        if (OwningClass == nullptr)
        {
            return FString();
        }
        const FString ClassName = OwningClass->GetName();
        for (const FOperatorEntry& Entry : OperatorTable())
        {
            if (ClassName == Entry.ClassName && FunctionName == FName(Entry.FunctionName))
            {
                return Entry.Op;
            }
        }
        return FString();
    }

    /** The four AActor override events the builder spawns by name, mapped back
        to the spec words for them. */
    FString ActorEventTypeFor(const FName& MemberName)
    {
        if (MemberName == FName(TEXT("ReceiveBeginPlay")))          { return TEXT("BeginPlay"); }
        if (MemberName == FName(TEXT("ReceiveTick")))               { return TEXT("Tick"); }
        if (MemberName == FName(TEXT("ReceiveActorBeginOverlap")))  { return TEXT("ActorBeginOverlap"); }
        if (MemberName == FName(TEXT("ReceiveActorEndOverlap")))    { return TEXT("ActorEndOverlap"); }
        return FString();
    }

    /** One pin default, back in the JSON shape a spec would have written.
        The inverse of ApplyPinDefault, category by category. Struct pins are
        the one lossy case and say so: only the three the builder writes as a
        comma triple can be read back as an object, and anything else is
        reported as the pin's raw text with the pin named in
        lossy_pin_defaults, rather than being dropped or guessed at. */
    TSharedPtr<FJsonValue> PinDefaultAsJson(const UEdGraphPin* Pin, bool& bOutLossy)
    {
        bOutLossy = false;
        const FName Category = Pin->PinType.PinCategory;

        if (Category == UEdGraphSchema_K2::PC_Object
            || Category == UEdGraphSchema_K2::PC_Class
            || Category == UEdGraphSchema_K2::PC_SoftObject
            || Category == UEdGraphSchema_K2::PC_SoftClass
            || Category == UEdGraphSchema_K2::PC_Interface)
        {
            if (Pin->DefaultObject != nullptr)
            {
                return MakeShared<FJsonValueString>(Pin->DefaultObject->GetPathName());
            }
            if (Pin->DefaultValue.IsEmpty() || Pin->DefaultValue == TEXT("None"))
            {
                return MakeShared<FJsonValueNull>();
            }
            return MakeShared<FJsonValueString>(Pin->DefaultValue);
        }
        if (Category == UEdGraphSchema_K2::PC_Boolean)
        {
            return MakeShared<FJsonValueBoolean>(Pin->DefaultValue.ToBool());
        }
        if (Category == UEdGraphSchema_K2::PC_Byte && Pin->PinType.PinSubCategoryObject.IsValid())
        {
            // An enum pin holds the enumerator name, which is what the spec writes.
            return MakeShared<FJsonValueString>(Pin->DefaultValue);
        }
        if (Category == UEdGraphSchema_K2::PC_Int
            || Category == UEdGraphSchema_K2::PC_Int64
            || Category == UEdGraphSchema_K2::PC_Byte
            || Category == UEdGraphSchema_K2::PC_Float)
        {
            return MakeShared<FJsonValueNumber>(FCString::Atod(*Pin->DefaultValue));
        }
        if (Category == UEdGraphSchema_K2::PC_Text)
        {
            return MakeShared<FJsonValueString>(Pin->DefaultTextValue.ToString());
        }
        if (Category == UEdGraphSchema_K2::PC_String || Category == UEdGraphSchema_K2::PC_Name)
        {
            return MakeShared<FJsonValueString>(Pin->DefaultValue);
        }
        if (Category == UEdGraphSchema_K2::PC_Struct)
        {
            // FDefaultValueHelper is the engine's own parser for these pin
            // default strings; it accepts both the comma form the builder
            // writes and the X= form, so anything it rejects is genuinely
            // lossy.
            const UScriptStruct* Struct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
            if (Struct == TBaseStructure<FVector>::Get())
            {
                FVector Vector;
                if (FDefaultValueHelper::ParseVector(Pin->DefaultValue, Vector))
                {
                    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
                    Object->SetNumberField(TEXT("x"), Vector.X);
                    Object->SetNumberField(TEXT("y"), Vector.Y);
                    Object->SetNumberField(TEXT("z"), Vector.Z);
                    return MakeShared<FJsonValueObject>(Object);
                }
            }
            if (Struct == TBaseStructure<FRotator>::Get())
            {
                FRotator Rotator;
                if (FDefaultValueHelper::ParseRotator(Pin->DefaultValue, Rotator))
                {
                    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
                    Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
                    Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
                    Object->SetNumberField(TEXT("roll"), Rotator.Roll);
                    return MakeShared<FJsonValueObject>(Object);
                }
            }
            if (Struct == TBaseStructure<FLinearColor>::Get())
            {
                FLinearColor Color;
                if (FDefaultValueHelper::ParseLinearColor(Pin->DefaultValue, Color))
                {
                    TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
                    Object->SetNumberField(TEXT("r"), Color.R);
                    Object->SetNumberField(TEXT("g"), Color.G);
                    Object->SetNumberField(TEXT("b"), Color.B);
                    Object->SetNumberField(TEXT("a"), Color.A);
                    return MakeShared<FJsonValueObject>(Object);
                }
            }
            bOutLossy = true;
            return MakeShared<FJsonValueString>(Pin->DefaultValue);
        }
        bOutLossy = true;
        return MakeShared<FJsonValueString>(Pin->DefaultValue);
    }

    /** Does this pin carry a default the caller wrote, rather than the one the
        node gave itself? An exec pin has none, a wired pin's default is not
        read, and a hidden pin is not a caller's to set. */
    bool PinCarriesAuthoredDefault(const UEdGraphPin* Pin)
    {
        if (Pin == nullptr
            || Pin->Direction != EGPD_Input
            || Pin->bHidden
            || Pin->bOrphanedPin
            || Pin->LinkedTo.Num() > 0
            || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            return false;
        }
        if (Pin->DefaultObject != nullptr)
        {
            return true;
        }
        if (!Pin->DefaultTextValue.IsEmpty())
        {
            return true;
        }
        return !Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != Pin->AutogeneratedDefaultValue;
    }

    void SetClassPathField(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, const UClass* Class)
    {
        if (Class != nullptr)
        {
            Params->SetStringField(Key, Class->GetPathName());
        }
        else
        {
            Params->SetField(Key, MakeShared<FJsonValueNull>());
        }
    }

    int32 CountExecPins(const UEdGraphNode* Node, EEdGraphPinDirection Direction)
    {
        int32 Count = 0;
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin != nullptr
                && Pin->Direction == Direction
                && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                Count++;
            }
        }
        return Count;
    }

    /** The routing params of a node: the keys that are not pin defaults but
        the words a spec used to choose this node in the first place. Only the
        types whose routing is a UPROPERTY the node actually keeps are
        reported. MakeStruct, BreakStruct, SpawnActor, Select, Knot and
        FormatText hold their configuration in their pins rather than in a
        field, so they report pin defaults alone; that is a real gap and it is
        recorded rather than guessed at. */
    void AddRoutingParams(
        const UEdGraphNode* Node,
        const FString& NodeType,
        const TSharedPtr<FJsonObject>& Params)
    {
        if (const UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(Node))
        {
            Params->SetStringField(TEXT("text"), Comment->NodeComment);
            Params->SetNumberField(TEXT("width"), Comment->NodeWidth);
            Params->SetNumberField(TEXT("height"), Comment->NodeHeight);
            return;
        }
        if (const UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
        {
            Params->SetStringField(TEXT("event_name"), CustomEvent->CustomFunctionName.ToString());
            return;
        }
        if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
        {
            Params->SetStringField(TEXT("event_name"), Event->EventReference.GetMemberName().ToString());
            if (NodeType == TEXT("Event"))
            {
                SetClassPathField(Params, TEXT("parent_class"),
                    Event->EventReference.GetMemberParentClass(nullptr));
            }
            return;
        }
        if (const UK2Node_InputKey* InputKey = Cast<UK2Node_InputKey>(Node))
        {
            Params->SetStringField(TEXT("fkey_name"), InputKey->InputKey.GetFName().ToString());
            Params->SetBoolField(TEXT("consume_input"), InputKey->bConsumeInput != 0);
            Params->SetBoolField(TEXT("execute_when_paused"), InputKey->bExecuteWhenPaused != 0);
            Params->SetBoolField(TEXT("override_parent"), InputKey->bOverrideParentBinding != 0);
            return;
        }
        if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            const UClass* Owner = Call->FunctionReference.GetMemberParentClass(nullptr);
            const FName FunctionName = Call->FunctionReference.GetMemberName();
            if (NodeType == TEXT("Operator"))
            {
                Params->SetStringField(TEXT("op"), OperatorNameFor(Owner, FunctionName));
                return;
            }
            if (NodeType == TEXT("CallFunction"))
            {
                SetClassPathField(Params, TEXT("class"), Owner);
                Params->SetStringField(TEXT("function"), FunctionName.ToString());
            }
            // PrintString and Delay are the function itself; a spec names
            // neither class nor function for them.
            return;
        }
        if (const UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
        {
            SetClassPathField(Params, TEXT("target_class"), CastNode->TargetType);
            Params->SetBoolField(TEXT("purity"), CastNode->IsNodePure());
            return;
        }
        if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node))
        {
            Params->SetStringField(TEXT("var_name"), Variable->GetVarName().ToString());
            const bool bSelfContext = Variable->VariableReference.IsSelfContext();
            Params->SetStringField(TEXT("scope"), bSelfContext ? TEXT("self") : TEXT("target"));
            if (!bSelfContext)
            {
                SetClassPathField(Params, TEXT("target_class"),
                    Variable->VariableReference.GetMemberParentClass(nullptr));
            }
            return;
        }
        if (const UK2Node_SwitchInteger* SwitchInt = Cast<UK2Node_SwitchInteger>(Node))
        {
            Params->SetNumberField(TEXT("start_index"), SwitchInt->StartIndex);
            Params->SetBoolField(TEXT("has_default"), SwitchInt->bHasDefaultPin != 0);
            return;
        }
        if (const UK2Node_SwitchString* SwitchString = Cast<UK2Node_SwitchString>(Node))
        {
            TArray<TSharedPtr<FJsonValue>> Cases;
            for (const FName& PinName : SwitchString->PinNames)
            {
                Cases.Add(MakeShared<FJsonValueString>(PinName.ToString()));
            }
            Params->SetArrayField(TEXT("case_values"), Cases);
            Params->SetBoolField(TEXT("is_case_sensitive"), SwitchString->bIsCaseSensitive != 0);
            Params->SetBoolField(TEXT("has_default"), SwitchString->bHasDefaultPin != 0);
            return;
        }
        if (const UK2Node_DoOnceMultiInput* DoOnce = Cast<UK2Node_DoOnceMultiInput>(Node))
        {
            // The factory takes the total, the node keeps the count above its
            // built-in one.
            Params->SetNumberField(TEXT("num_inputs"), DoOnce->NumAdditionalInputs + 1);
            return;
        }
        // MultiGate derives from ExecutionSequence, so it has to be asked
        // first; both report their exec output count the same way.
        if (Node->IsA<UK2Node_MultiGate>() || Node->IsA<UK2Node_ExecutionSequence>())
        {
            Params->SetNumberField(TEXT("num_outputs"), CountExecPins(Node, EGPD_Output));
            return;
        }
    }
}

FString UBlueprintGraphBuilderLibrary::GetNodeTypeForNode(const UEdGraphNode* Node)
{
    if (Node == nullptr)
    {
        return FString();
    }
    // Narrowest first. UK2Node_CustomEvent derives from UK2Node_Event and
    // UK2Node_MultiGate from UK2Node_ExecutionSequence, so asking the base
    // first would report both as their base type and rebuild the wrong node.
    if (Node->IsA<UEdGraphNode_Comment>())      { return TEXT("Comment"); }
    if (Node->IsA<UK2Node_CustomEvent>())       { return TEXT("CustomEvent"); }
    if (const UK2Node_Event* Event = Cast<UK2Node_Event>(Node))
    {
        if (Event->bOverrideFunction)
        {
            const FString ActorEvent = ActorEventTypeFor(Event->EventReference.GetMemberName());
            if (!ActorEvent.IsEmpty())
            {
                return ActorEvent;
            }
        }
        return TEXT("Event");
    }
    if (Node->IsA<UK2Node_InputKey>())          { return TEXT("InputKey"); }
    if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
    {
        const UClass* Owner = Call->FunctionReference.GetMemberParentClass(nullptr);
        const FName FunctionName = Call->FunctionReference.GetMemberName();
        if (Owner == UKismetSystemLibrary::StaticClass())
        {
            if (FunctionName == FName(TEXT("PrintString"))) { return TEXT("PrintString"); }
            if (FunctionName == FName(TEXT("Delay")))       { return TEXT("Delay"); }
        }
        const FString Operator = OperatorNameFor(Owner, FunctionName);
        return Operator.IsEmpty() ? TEXT("CallFunction") : TEXT("Operator");
    }
    if (Node->IsA<UK2Node_IfThenElse>())        { return TEXT("Branch"); }
    if (Node->IsA<UK2Node_MultiGate>())         { return TEXT("MultiGate"); }
    if (Node->IsA<UK2Node_ExecutionSequence>()) { return TEXT("Sequence"); }
    if (Node->IsA<UK2Node_MakeStruct>())        { return TEXT("MakeStruct"); }
    if (Node->IsA<UK2Node_BreakStruct>())       { return TEXT("BreakStruct"); }
    if (Node->IsA<UK2Node_VariableSet>())       { return TEXT("VariableSet"); }
    if (Node->IsA<UK2Node_VariableGet>())       { return TEXT("VariableGet"); }
    if (Node->IsA<UK2Node_DynamicCast>())       { return TEXT("Cast"); }
    if (Node->IsA<UK2Node_Select>())            { return TEXT("Select"); }
    if (Node->IsA<UK2Node_Knot>())              { return TEXT("Knot"); }
    if (Node->IsA<UK2Node_FormatText>())        { return TEXT("FormatText"); }
    if (Node->IsA<UK2Node_SpawnActorFromClass>()) { return TEXT("SpawnActor"); }
    if (Node->IsA<UK2Node_SwitchInteger>())     { return TEXT("SwitchInt"); }
    if (Node->IsA<UK2Node_SwitchString>())      { return TEXT("SwitchString"); }
    if (Node->IsA<UK2Node_DoOnceMultiInput>())  { return TEXT("DoOnceMultiInput"); }
    return FString();
}

FString UBlueprintGraphBuilderLibrary::DescribeBlueprintGraphJSON(
    UBlueprint* Blueprint,
    const FString& GraphName,
    bool bIncludePins)
{
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    if (Blueprint == nullptr)
    {
        Root->SetStringField(TEXT("error"), TEXT("blueprint null"));
        return SerializeReport(Root);
    }

    UEdGraph* Graph = nullptr;
    if (GraphName.IsEmpty())
    {
        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Graph == nullptr && Blueprint->UbergraphPages.Num() > 0)
        {
            Graph = Blueprint->UbergraphPages[0];
        }
    }
    else
    {
        Graph = FBPGraphReader::FindGraphByName(Blueprint, GraphName);
    }
    if (Graph == nullptr)
    {
        Root->SetStringField(TEXT("error"), GraphName.IsEmpty()
            ? TEXT("This Blueprint has no event graph.")
            : FString::Printf(TEXT("Graph '%s' was not found on this Blueprint."), *GraphName));
        return SerializeReport(Root);
    }

    const FString AssetPath = Blueprint->GetPathName();
    const FString ResolvedGraphName = Graph->GetName();

    // Canonical ordering, not graph order. Unreal's own array order is an
    // implementation detail that a reconstruct, a paste or a load can permute,
    // so every array this function emits is sorted by a stable identity:
    // nodes by NodeGuid, pins by direction then PinId, connections by their
    // two endpoint strings. Sorted keys make two reads comparable by hash
    // rather than only by content.
    using FSortedEntry = TPair<FString, TSharedPtr<FJsonValue>>;
    auto EmitSorted = [](TArray<FSortedEntry>& Entries) -> TArray<TSharedPtr<FJsonValue>>
    {
        Entries.Sort([](const FSortedEntry& A, const FSortedEntry& B) { return A.Key < B.Key; });
        TArray<TSharedPtr<FJsonValue>> Out;
        Out.Reserve(Entries.Num());
        for (const FSortedEntry& Entry : Entries) { Out.Add(Entry.Value); }
        return Out;
    };

    TArray<FSortedEntry> NodeEntries;
    TArray<FSortedEntry> ConnectionEntries;
    TArray<FSortedEntry> UnmappedEntries;
    TArray<FSortedEntry> LossyEntries;
    int32 ConnectionCount = 0;

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr)
        {
            continue;
        }
        const FString NodeId = Node->GetName();
        const FString NodeGuid = Node->NodeGuid.ToString();
        const FString NodeType = GetNodeTypeForNode(Node);

        TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
        // The identity tuple, repeated on the node rather than only on the
        // graph: a NodeGuid is unique inside one graph and says nothing about
        // which graph of which asset it came from, so a node lifted out of
        // this array on its own would otherwise be ambiguous.
        Entry->SetStringField(TEXT("asset_path"), AssetPath);
        Entry->SetStringField(TEXT("graph_name"), ResolvedGraphName);
        Entry->SetStringField(TEXT("id"), NodeId);
        Entry->SetStringField(TEXT("node_guid"), NodeGuid);
        if (NodeType.IsEmpty())
        {
            Entry->SetField(TEXT("type"), MakeShared<FJsonValueNull>());
            TSharedPtr<FJsonObject> Miss = MakeShared<FJsonObject>();
            Miss->SetStringField(TEXT("id"), NodeId);
            Miss->SetStringField(TEXT("node_guid"), NodeGuid);
            Miss->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
            UnmappedEntries.Emplace(NodeGuid, MakeShared<FJsonValueObject>(Miss));
        }
        else
        {
            Entry->SetStringField(TEXT("type"), NodeType);
        }
        Entry->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
        Entry->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        Entry->SetNumberField(TEXT("x"), Node->NodePosX);
        Entry->SetNumberField(TEXT("y"), Node->NodePosY);
        Entry->SetStringField(TEXT("comment"), Node->NodeComment);
        Entry->SetStringField(TEXT("enabled"),
            Node->GetDesiredEnabledState() == ENodeEnabledState::Disabled ? TEXT("Disabled")
            : Node->GetDesiredEnabledState() == ENodeEnabledState::DevelopmentOnly ? TEXT("DevelopmentOnly")
            : TEXT("Enabled"));

        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        AddRoutingParams(Node, NodeType, Params);
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (!PinCarriesAuthoredDefault(Pin))
            {
                continue;
            }
            bool bLossy = false;
            Params->SetField(Pin->PinName.ToString(), PinDefaultAsJson(Pin, bLossy));
            if (bLossy)
            {
                const FString Text = FString::Printf(
                    TEXT("%s.%s (%s default reported as its raw pin text)"),
                    *NodeId, *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString());
                LossyEntries.Emplace(NodeGuid + Pin->PinId.ToString(), MakeShared<FJsonValueString>(Text));
            }
        }
        Entry->SetObjectField(TEXT("params"), Params);

        if (bIncludePins)
        {
            TArray<FSortedEntry> PinEntries;
            for (UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr)
                {
                    continue;
                }
                // Inputs before outputs, then by PinId, so a node that
                // reconstructs its pins in a different order still reads the
                // same way.
                const FString Key = FString::Printf(TEXT("%d|%s"),
                    Pin->Direction == EGPD_Input ? 0 : 1, *Pin->PinId.ToString());
                PinEntries.Emplace(Key, MakeShared<FJsonValueObject>(FBPPinSerializer::Serialize(Pin)));
            }
            Entry->SetArrayField(TEXT("pins"), EmitSorted(PinEntries));
        }
        NodeEntries.Emplace(NodeGuid, MakeShared<FJsonValueObject>(Entry));

        // Output pins only, so each link is reported once and in the direction
        // a spec writes it.
        for (const UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin == nullptr || Pin->Direction != EGPD_Output)
            {
                continue;
            }
            for (const UEdGraphPin* Linked : Pin->LinkedTo)
            {
                UEdGraphNode* LinkedNode = Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                if (LinkedNode == nullptr)
                {
                    continue;
                }
                const FString From = FString::Printf(TEXT("%s.%s"), *NodeId, *Pin->PinName.ToString());
                const FString To = FString::Printf(
                    TEXT("%s.%s"), *LinkedNode->GetName(), *Linked->PinName.ToString());

                TSharedPtr<FJsonObject> Connection = MakeShared<FJsonObject>();
                Connection->SetStringField(TEXT("from"), From);
                Connection->SetStringField(TEXT("to"), To);
                // Endpoints again as identity rather than as display: two pins
                // of one node can share a name across directions, and a node
                // name is only unique inside its own graph.
                Connection->SetStringField(TEXT("from_node_guid"), NodeGuid);
                Connection->SetStringField(TEXT("from_pin_id"), Pin->PinId.ToString());
                Connection->SetStringField(TEXT("to_node_guid"), LinkedNode->NodeGuid.ToString());
                Connection->SetStringField(TEXT("to_pin_id"), Linked->PinId.ToString());

                const FString Key = FString::Printf(TEXT("%s|%s|%s|%s"),
                    *NodeGuid, *Pin->PinId.ToString(),
                    *LinkedNode->NodeGuid.ToString(), *Linked->PinId.ToString());
                ConnectionEntries.Emplace(Key, MakeShared<FJsonValueObject>(Connection));
                ConnectionCount++;
            }
        }
    }

    // --- Canonical structure hash ---
    //
    // What the graph IS, with nothing in it that a rebuild would change for
    // free. Object names and NodeGuids are deliberately absent: a node the
    // removal ledger recreates is a new UObject with a new name and a new guid
    // while being the same node in every sense a caller cares about, and
    // findings 0j is the record of what happens when a comparison forgets that.
    // Positions, comments, enabled state and pin defaults are all present,
    // because those are the parts a "nothing changed" claim is usually wrong
    // about.
    //
    // Structural, therefore also PREDICTABLE: a caller can compute what this
    // hash will be after a change it has not made yet, which is what makes it
    // usable as a convergence check rather than only as an after-the-fact
    // comparison. Two structurally identical nodes at the same position hash
    // the same, which is correct - they are interchangeable by construction.
    {
        auto StructuralKeyOf = [](const UEdGraphNode* N) -> FString
        {
            if (N == nullptr) { return TEXT("<null>"); }
            TArray<FString> PinParts;
            for (const UEdGraphPin* P : N->Pins)
            {
                if (P == nullptr) { continue; }
                PinParts.Add(FString::Printf(TEXT("%s:%d:%s:%s"),
                    *P->PinName.ToString(), static_cast<int32>(P->Direction),
                    *P->DefaultValue,
                    P->DefaultObject != nullptr ? *P->DefaultObject->GetPathName() : TEXT("")));
            }
            PinParts.Sort();
            return FString::Printf(TEXT("%s|%s|%d|%d|%s|%d|[%s]"),
                *GetNodeTypeForNode(const_cast<UEdGraphNode*>(N)),
                *N->GetClass()->GetName(), N->NodePosX, N->NodePosY,
                *N->NodeComment, static_cast<int32>(N->GetDesiredEnabledState()),
                *FString::Join(PinParts, TEXT(",")));
        };

        TArray<FString> NodeLines;
        TArray<FString> LinkLines;
        for (const UEdGraphNode* Node : Graph->Nodes)
        {
            if (Node == nullptr) { continue; }
            const FString FromKey = StructuralKeyOf(Node);
            NodeLines.Add(FromKey);
            for (const UEdGraphPin* Pin : Node->Pins)
            {
                if (Pin == nullptr || Pin->Direction != EGPD_Output) { continue; }
                for (const UEdGraphPin* Linked : Pin->LinkedTo)
                {
                    const UEdGraphNode* LinkedNode =
                        Linked != nullptr ? Linked->GetOwningNodeUnchecked() : nullptr;
                    if (LinkedNode == nullptr) { continue; }
                    LinkLines.Add(FString::Printf(TEXT("%s#%s->%s#%s"),
                        *FromKey, *Pin->PinName.ToString(),
                        *StructuralKeyOf(LinkedNode), *Linked->PinName.ToString()));
                }
            }
        }
        // Sorted, so the hash describes the graph and not the order Unreal
        // happens to hold its nodes in.
        NodeLines.Sort();
        LinkLines.Sort();
        const FString Canonical = FString::Printf(TEXT("nodes:\n%s\nlinks:\n%s\n"),
            *FString::Join(NodeLines, TEXT("\n")), *FString::Join(LinkLines, TEXT("\n")));

        FSHA1 Sha;
        const FTCHARToUTF8 Utf8(*Canonical);
        Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
        Sha.Final();
        uint8 Digest[FSHA1::DigestSize];
        Sha.GetHash(Digest);
        Root->SetStringField(TEXT("structure_hash_sha1"),
            BytesToHex(Digest, FSHA1::DigestSize).ToLower());
        Root->SetStringField(TEXT("structure_hash_basis"),
            TEXT("node type, class, position, comment, enabled state and pin defaults, plus links "
                 "between those structural keys; object names and NodeGuids deliberately excluded"));
    }

    Root->SetStringField(TEXT("asset_path"), AssetPath);
    Root->SetStringField(TEXT("name"), ResolvedGraphName);
    Root->SetStringField(TEXT("graph_name"), ResolvedGraphName);
    Root->SetStringField(TEXT("graph_guid"), Graph->GraphGuid.ToString());
    Root->SetNumberField(TEXT("node_count"), NodeEntries.Num());
    Root->SetNumberField(TEXT("connection_count"), ConnectionCount);
    Root->SetBoolField(TEXT("pins_included"), bIncludePins);
    Root->SetArrayField(TEXT("nodes"), EmitSorted(NodeEntries));
    Root->SetArrayField(TEXT("connections"), EmitSorted(ConnectionEntries));
    Root->SetArrayField(TEXT("unmapped_nodes"), EmitSorted(UnmappedEntries));
    Root->SetArrayField(TEXT("lossy_pin_defaults"), EmitSorted(LossyEntries));
    return SerializeReport(Root);
}

bool UBlueprintGraphBuilderLibrary::ConfigureUserDefinedEnum(
    UUserDefinedEnum* Enum,
    const TArray<FString>& DisplayNames)
{
    if (!Enum || DisplayNames.Num() <= 0)
    {
        return false;
    }

    while (Enum->NumEnums() > 1)
    {
        FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, 0);
    }

    while (Enum->NumEnums() - 1 < DisplayNames.Num())
    {
        FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
    }

    for (int32 Index = 0; Index < DisplayNames.Num(); ++Index)
    {
        FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Index, FText::FromString(DisplayNames[Index]));
    }

    FEnumEditorUtils::EnsureAllDisplayNamesExist(Enum);
    Enum->MarkPackageDirty();
    return true;
}

// ---------------------------------------------------------------------------
// Incremental graph patching
// ---------------------------------------------------------------------------

namespace
{
/** One resolved selector: the node it names, or why it names no single node.
    bDeferred is the third state, and it exists because "no node yet" and "no such
    node" are not the same answer: a selector naming a node this same batch adds
    is correct and simply cannot be checked until pass 2. Without it the deferred
    case arrives at the failure branch with an empty Failure string and the batch
    is refused for a selector that was always going to resolve. */
struct FPatchTarget
{
    UEdGraphNode* Node = nullptr;
    FString Description;
    FString Failure;
    bool bAmbiguous = false;
    bool bDeferred = false;
};

/** A node's addressable facts, in the order a selector is allowed to use them.
    Deliberately does NOT include the UObject name on its own: that name changes
    whenever a node is recreated, and a patch that targets by it edits whatever
    happens to hold the name afterwards (findings 0j). It is accepted only as a
    disambiguator ALONGSIDE a structural field. */
FString DescribeNodeForSelector(UEdGraphNode* Node)
{
    if (Node == nullptr) { return TEXT("<null>"); }
    FString VarOrFunc;
    if (const UK2Node_Variable* Var = Cast<UK2Node_Variable>(Node))
    {
        VarOrFunc = FString::Printf(TEXT(" var=%s"), *Var->GetVarName().ToString());
    }
    else if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
    {
        VarOrFunc = FString::Printf(TEXT(" fn=%s"), *Call->GetFunctionName().ToString());
    }
    return FString::Printf(TEXT("%s [%s%s] at %d,%d (guid %s, object %s)"),
        *UBlueprintGraphBuilderLibrary::GetNodeTypeForNode(Node),
        *Node->GetClass()->GetName(), *VarOrFunc,
        Node->NodePosX, Node->NodePosY,
        *Node->NodeGuid.ToString(), *Node->GetName());
}

/** Resolve one selector against a graph. Every supplied field must match, and
    exactly one node must survive. */
FPatchTarget ResolveSelector(
    UEdGraph* Graph,
    const TSharedPtr<FJsonObject>& Selector,
    const TMap<FString, UEdGraphNode*>& NewNodesThisBatch)
{
    FPatchTarget Result;
    if (!Selector.IsValid())
    {
        Result.Failure = TEXT("the selector is missing or is not an object");
        return Result;
    }

    // A node created earlier in this same batch. Needed because a caller adding
    // a node and then wiring it has no guid to name it by yet, and the alternative
    // - a second round trip to discover it - is the round trip this command exists
    // to remove.
    FString NewId;
    if (Selector->TryGetStringField(TEXT("new_id"), NewId) && !NewId.IsEmpty())
    {
        if (UEdGraphNode* const* Found = NewNodesThisBatch.Find(NewId))
        {
            // In pass 1 the map holds reserved keys with no node behind them yet.
            // That is a deferred hit, not a miss.
            if (*Found == nullptr)
            {
                Result.bDeferred = true;
                Result.Description = FString::Printf(
                    TEXT("<new_id '%s', added by this batch>"), *NewId);
                return Result;
            }
            Result.Node = *Found;
            Result.Description = DescribeNodeForSelector(*Found);
            return Result;
        }
        Result.Failure = FString::Printf(
            TEXT("new_id '%s' names no node added earlier in this batch"), *NewId);
        return Result;
    }

    FString NodeGuid, NodeClass, NodeType, VarName, FunctionName, ObjectId;
    Selector->TryGetStringField(TEXT("node_guid"), NodeGuid);
    Selector->TryGetStringField(TEXT("node_class"), NodeClass);
    Selector->TryGetStringField(TEXT("type"), NodeType);
    Selector->TryGetStringField(TEXT("var_name"), VarName);
    Selector->TryGetStringField(TEXT("function"), FunctionName);
    Selector->TryGetStringField(TEXT("id"), ObjectId);
    double X = 0.0, Y = 0.0;
    const bool bHasX = Selector->TryGetNumberField(TEXT("x"), X);
    const bool bHasY = Selector->TryGetNumberField(TEXT("y"), Y);

    const bool bHasStructural = !NodeGuid.IsEmpty() || !NodeClass.IsEmpty() || !NodeType.IsEmpty()
        || !VarName.IsEmpty() || !FunctionName.IsEmpty();
    if (!bHasStructural)
    {
        Result.Failure = ObjectId.IsEmpty()
            ? TEXT("the selector names no node: supply node_guid, or type/node_class with "
                   "var_name, function, id or position")
            : FString::Printf(
                TEXT("the selector names only the object id '%s'. A UObject name is not stable - "
                     "it changes whenever a node is recreated - so it is accepted only together "
                     "with a structural field such as type, node_class, var_name or function."),
                *ObjectId);
        return Result;
    }

    TArray<UEdGraphNode*> Matches;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (Node == nullptr) { continue; }
        if (!NodeGuid.IsEmpty() && Node->NodeGuid.ToString() != NodeGuid) { continue; }
        if (!NodeClass.IsEmpty() && Node->GetClass()->GetName() != NodeClass) { continue; }
        if (!NodeType.IsEmpty()
            && UBlueprintGraphBuilderLibrary::GetNodeTypeForNode(Node) != NodeType) { continue; }
        if (!ObjectId.IsEmpty() && Node->GetName() != ObjectId) { continue; }
        if (!VarName.IsEmpty())
        {
            const UK2Node_Variable* Var = Cast<UK2Node_Variable>(Node);
            if (Var == nullptr || Var->GetVarName().ToString() != VarName) { continue; }
        }
        if (!FunctionName.IsEmpty())
        {
            const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
            if (Call == nullptr || Call->GetFunctionName().ToString() != FunctionName) { continue; }
        }
        if (bHasX && Node->NodePosX != static_cast<int32>(X)) { continue; }
        if (bHasY && Node->NodePosY != static_cast<int32>(Y)) { continue; }
        Matches.Add(Node);
    }

    if (Matches.Num() == 1)
    {
        Result.Node = Matches[0];
        Result.Description = DescribeNodeForSelector(Matches[0]);
        return Result;
    }
    if (Matches.Num() == 0)
    {
        Result.Failure = TEXT("the selector matched no node in this graph");
        return Result;
    }

    // Ambiguity is refused rather than resolved by picking the first. Position
    // is offered as the way out because it is the one field that separates
    // otherwise identical nodes, which is exactly when this fires.
    Result.bAmbiguous = true;
    TArray<FString> Descriptions;
    for (UEdGraphNode* Node : Matches) { Descriptions.Add(DescribeNodeForSelector(Node)); }
    Descriptions.Sort();
    Result.Failure = FString::Printf(
        TEXT("the selector matched %d nodes and a patch will not choose between them. "
             "Add x and y to disambiguate, or address one by node_guid. Matches: %s"),
        Matches.Num(), *FString::Join(Descriptions, TEXT("; ")));
    return Result;
}

/** Pin lookup by the same role vocabulary the builder's connections use, so a
    patch and a build address the same pin by the same word. */
UEdGraphPin* ResolvePatchPin(UEdGraphNode* Node, const FString& Role, EEdGraphPinDirection Direction)
{
    return ResolveGraphPinByRole(Node, Role, Direction);
}

/** "nodeId.pinRole" as a patch writes it, split into its two halves. */
bool SplitEndpoint(const FString& Endpoint, FString& OutNode, FString& OutPin)
{
    int32 Dot = INDEX_NONE;
    if (!Endpoint.FindLastChar(TEXT('.'), Dot) || Dot <= 0 || Dot >= Endpoint.Len() - 1)
    {
        return false;
    }
    OutNode = Endpoint.Left(Dot);
    OutPin = Endpoint.Mid(Dot + 1);
    return true;
}
}

bool UBlueprintGraphBuilderLibrary::PatchBlueprintGraphFromJSON(
    UBlueprint* Blueprint,
    const FString& PatchJson,
    bool bPlanOnly,
    FString& OutReportJson)
{
    TSharedPtr<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Errors;
    auto Finish = [&](bool bOk) -> bool
    {
        Report->SetArrayField(TEXT("errors"), Errors);
        Report->SetBoolField(TEXT("ok"), bOk);
        FString Text;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Report.ToSharedRef(), Writer);
        OutReportJson = Text;
        return bOk;
    };
    auto Fail = [&](const FString& Message) -> bool
    {
        Errors.Add(MakeShared<FJsonValueString>(Message));
        return Finish(false);
    };

    if (Blueprint == nullptr) { return Fail(TEXT("Blueprint is null.")); }

    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PatchJson);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return Fail(TEXT("The patch must be a JSON object."));
    }

    FString GraphName;
    Root->TryGetStringField(TEXT("graph"), GraphName);
    UEdGraph* Graph = nullptr;
    if (GraphName.IsEmpty())
    {
        Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
        if (Graph == nullptr && Blueprint->UbergraphPages.Num() > 0)
        {
            Graph = Blueprint->UbergraphPages[0];
        }
    }
    else
    {
        for (UEdGraph* Candidate : Blueprint->UbergraphPages)
        {
            if (Candidate != nullptr && Candidate->GetName() == GraphName) { Graph = Candidate; }
        }
        for (UEdGraph* Candidate : Blueprint->FunctionGraphs)
        {
            if (Candidate != nullptr && Candidate->GetName() == GraphName) { Graph = Candidate; }
        }
    }
    if (Graph == nullptr)
    {
        return Fail(FString::Printf(TEXT("No graph named '%s' on this Blueprint."),
            GraphName.IsEmpty() ? TEXT("<event graph>") : *GraphName));
    }
    Report->SetStringField(TEXT("graph"), Graph->GetName());

    const TArray<TSharedPtr<FJsonValue>>* Operations = nullptr;
    if (!Root->TryGetArrayField(TEXT("operations"), Operations) || Operations == nullptr)
    {
        return Fail(TEXT("operations must be an array."));
    }

    static const TSet<FString> SupportedOps = {
        TEXT("add_node"), TEXT("update_node"), TEXT("remove_node"), TEXT("set_pin_default"),
        TEXT("connect_pins"), TEXT("disconnect_pins"), TEXT("move_node"),
    };

    // --- Pass 1: resolve and validate the whole batch, mutating nothing ---
    //
    // Every selector resolves before the first change. A batch that half-applies
    // and then meets an unresolvable target has already done the damage the
    // transaction then has to undo, and findings 0k is the record of what that
    // costs. Refusing up front is the same insight one level down: do not start
    // changing until the thing that might fail has succeeded.
    struct FResolvedOp
    {
        FString Op;
        int32 Index = 0;
        const TSharedPtr<FJsonObject>* Spec = nullptr;
        UEdGraphNode* Target = nullptr;
        UEdGraphPin* TargetPin = nullptr;
        FString NewId;
        bool bUnchanged = false;
        FString UnchangedReason;
    };

    TArray<FResolvedOp> Resolved;
    TArray<TSharedPtr<FJsonValue>> MatchedNodes, UnmatchedSelectors, AmbiguousSelectors;
    TArray<TSharedPtr<FJsonValue>> NodesToAdd, NodesToUpdate, NodesToRemove;
    TArray<TSharedPtr<FJsonValue>> PinDefaultsToChange, LinksToAdd, LinksToRemove;
    TMap<FString, UEdGraphNode*> PlannedNewNodes;
    // Link state as the batch will leave it, not as the graph holds it now.
    // Two operations in one batch can name the same pair - a disconnect then a
    // reconnect is the ordinary way to reseat a link - and a plan that tested
    // every one of them against the pre-batch graph called the second one
    // "already linked" and predicted one change where there are two.
    TMap<TPair<UEdGraphPin*, UEdGraphPin*>, bool> PlannedLinkState;

    int32 Index = 0;
    for (const TSharedPtr<FJsonValue>& OpValue : *Operations)
    {
        const TSharedPtr<FJsonObject>* OpObj = nullptr;
        const int32 OpIndex = Index++;
        if (!OpValue->TryGetObject(OpObj))
        {
            return Fail(FString::Printf(TEXT("operation %d is not an object."), OpIndex));
        }
        FString Op;
        (*OpObj)->TryGetStringField(TEXT("op"), Op);
        if (!SupportedOps.Contains(Op))
        {
            TArray<FString> Names = SupportedOps.Array();
            Names.Sort();
            return Fail(FString::Printf(
                TEXT("operation %d names an unsupported op '%s'. Supported: %s."),
                OpIndex, *Op, *FString::Join(Names, TEXT(", "))));
        }

        FResolvedOp R;
        R.Op = Op;
        R.Index = OpIndex;
        R.Spec = OpObj;

        auto RecordFailure = [&](const FPatchTarget& Target, const TCHAR* Field)
        {
            const FString Line = FString::Printf(TEXT("operation %d (%s) %s: %s"),
                OpIndex, *Op, Field, *Target.Failure);
            if (Target.bAmbiguous) { AmbiguousSelectors.Add(MakeShared<FJsonValueString>(Line)); }
            else { UnmatchedSelectors.Add(MakeShared<FJsonValueString>(Line)); }
        };

        auto ResolveNamed = [&](const TCHAR* Field, UEdGraphNode*& OutNode) -> bool
        {
            const TSharedPtr<FJsonObject>* SelectorObj = nullptr;
            if (!(*OpObj)->TryGetObjectField(Field, SelectorObj))
            {
                UnmatchedSelectors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s): %s is missing"), OpIndex, *Op, Field)));
                return false;
            }
            const FPatchTarget Target = ResolveSelector(Graph, *SelectorObj, PlannedNewNodes);
            if (Target.bDeferred)
            {
                // Only the link operations can name a node before it exists;
                // everything else needs the real node during planning.
                UnmatchedSelectors.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s) %s names a node this batch adds. Only connect_pins and "
                         "disconnect_pins can address a node before it exists. Set what this "
                         "operation would set through the add_node params instead, or patch the "
                         "node in a second call once it is real."),
                    OpIndex, *Op, Field)));
                return false;
            }
            if (Target.Node == nullptr) { RecordFailure(Target, Field); return false; }
            MatchedNodes.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("operation %d (%s) %s -> %s"), OpIndex, *Op, Field, *Target.Description)));
            OutNode = Target.Node;
            return true;
        };

        if (Op == TEXT("add_node"))
        {
            const TSharedPtr<FJsonObject>* NodeSpec = nullptr;
            if (!(*OpObj)->TryGetObjectField(TEXT("node"), NodeSpec))
            {
                return Fail(FString::Printf(TEXT("operation %d (add_node) has no node object."), OpIndex));
            }
            FString NewId, NodeType;
            (*OpObj)->TryGetStringField(TEXT("new_id"), NewId);
            (*NodeSpec)->TryGetStringField(TEXT("type"), NodeType);
            if (NewId.IsEmpty()) { (*NodeSpec)->TryGetStringField(TEXT("id"), NewId); }
            if (NewId.IsEmpty())
            {
                return Fail(FString::Printf(
                    TEXT("operation %d (add_node) needs new_id so later operations can address it."), OpIndex));
            }
            if (!GetSupportedNodeTypes().Contains(NodeType))
            {
                return Fail(FString::Printf(
                    TEXT("operation %d (add_node) names node type '%s', which this builder does not "
                         "support. Patching adds no node types of its own."), OpIndex, *NodeType));
            }
            R.NewId = NewId;
            PlannedNewNodes.Add(NewId, nullptr);
            NodesToAdd.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: %s"), *NewId, *NodeType)));
        }
        else if (Op == TEXT("connect_pins") || Op == TEXT("disconnect_pins"))
        {
            const TSharedPtr<FJsonObject>* FromObj = nullptr;
            const TSharedPtr<FJsonObject>* ToObj = nullptr;
            if (!(*OpObj)->TryGetObjectField(TEXT("from"), FromObj)
                || !(*OpObj)->TryGetObjectField(TEXT("to"), ToObj))
            {
                return Fail(FString::Printf(
                    TEXT("operation %d (%s) needs from and to objects, each with a target selector "
                         "and a pin."), OpIndex, *Op));
            }
            const TSharedPtr<FJsonObject>* FromSel = nullptr;
            const TSharedPtr<FJsonObject>* ToSel = nullptr;
            (*FromObj)->TryGetObjectField(TEXT("target"), FromSel);
            (*ToObj)->TryGetObjectField(TEXT("target"), ToSel);
            FString FromPinRole, ToPinRole;
            (*FromObj)->TryGetStringField(TEXT("pin"), FromPinRole);
            (*ToObj)->TryGetStringField(TEXT("pin"), ToPinRole);
            if (FromPinRole.IsEmpty() || ToPinRole.IsEmpty())
            {
                return Fail(FString::Printf(
                    TEXT("operation %d (%s) needs a pin role on both ends."), OpIndex, *Op));
            }

            const FPatchTarget FromTarget =
                ResolveSelector(Graph, FromSel != nullptr ? *FromSel : nullptr, PlannedNewNodes);
            const FPatchTarget ToTarget =
                ResolveSelector(Graph, ToSel != nullptr ? *ToSel : nullptr, PlannedNewNodes);
            if (FromTarget.Node == nullptr && !FromTarget.bDeferred)
            {
                RecordFailure(FromTarget, TEXT("from"));
            }
            else
            {
                MatchedNodes.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s) from -> %s"), OpIndex, *Op, *FromTarget.Description)));
            }
            if (ToTarget.Node == nullptr && !ToTarget.bDeferred)
            {
                RecordFailure(ToTarget, TEXT("to"));
            }
            else
            {
                MatchedNodes.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s) to -> %s"), OpIndex, *Op, *ToTarget.Description)));
            }

            // A node this batch is about to add exists only as a reserved key
            // during planning, so its pins cannot be checked yet. Those ends are
            // validated for real at apply time, where the node is real.
            const bool bFromPlanned = FromTarget.bDeferred;
            const bool bToPlanned = ToTarget.bDeferred;
            if (FromTarget.Node != nullptr && ToTarget.Node != nullptr)
            {
                UEdGraphPin* FromPin = ResolvePatchPin(FromTarget.Node, FromPinRole, EGPD_Output);
                UEdGraphPin* ToPin = ResolvePatchPin(ToTarget.Node, ToPinRole, EGPD_Input);
                if (FromPin == nullptr)
                {
                    UnmatchedSelectors.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("operation %d (%s): no output pin '%s' on %s"),
                        OpIndex, *Op, *FromPinRole, *DescribeNodeForSelector(FromTarget.Node))));
                }
                else if (ToPin == nullptr)
                {
                    UnmatchedSelectors.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("operation %d (%s): no input pin '%s' on %s"),
                        OpIndex, *Op, *ToPinRole, *DescribeNodeForSelector(ToTarget.Node))));
                }
                else
                {
                    const TPair<UEdGraphPin*, UEdGraphPin*> LinkKey(FromPin, ToPin);
                    const bool* PlannedState = PlannedLinkState.Find(LinkKey);
                    const bool bLinked = PlannedState != nullptr
                        ? *PlannedState : FromPin->LinkedTo.Contains(ToPin);
                    const bool bWantLinked = Op == TEXT("connect_pins");
                    const FString Line = FString::Printf(TEXT("%s.%s -> %s.%s"),
                        *DescribeNodeForSelector(FromTarget.Node), *FromPinRole,
                        *DescribeNodeForSelector(ToTarget.Node), *ToPinRole);
                    if (bLinked == bWantLinked)
                    {
                        R.bUnchanged = true;
                        R.UnchangedReason = bWantLinked
                            ? TEXT("already linked") : TEXT("already not linked");
                    }
                    else if (bWantLinked) { LinksToAdd.Add(MakeShared<FJsonValueString>(Line)); }
                    else { LinksToRemove.Add(MakeShared<FJsonValueString>(Line)); }
                    PlannedLinkState.Add(LinkKey, bWantLinked);
                }
            }
            else if (bFromPlanned || bToPlanned)
            {
                TArray<TSharedPtr<FJsonValue>>& Bucket =
                    Op == TEXT("connect_pins") ? LinksToAdd : LinksToRemove;
                Bucket.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s): an end names a node this batch adds; verified at apply"),
                    OpIndex, *Op)));
            }
        }
        else if (ResolveNamed(TEXT("target"), R.Target))
        {
            if (Op == TEXT("remove_node"))
            {
                NodesToRemove.Add(MakeShared<FJsonValueString>(DescribeNodeForSelector(R.Target)));
            }
            else if (Op == TEXT("move_node"))
            {
                double NewX = R.Target->NodePosX, NewY = R.Target->NodePosY;
                (*OpObj)->TryGetNumberField(TEXT("x"), NewX);
                (*OpObj)->TryGetNumberField(TEXT("y"), NewY);
                if (static_cast<int32>(NewX) == R.Target->NodePosX
                    && static_cast<int32>(NewY) == R.Target->NodePosY)
                {
                    R.bUnchanged = true;
                    R.UnchangedReason = TEXT("already at that position");
                }
                else
                {
                    NodesToUpdate.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("move %s to %d,%d"), *DescribeNodeForSelector(R.Target),
                        static_cast<int32>(NewX), static_cast<int32>(NewY))));
                }
            }
            else if (Op == TEXT("set_pin_default"))
            {
                FString PinRole;
                (*OpObj)->TryGetStringField(TEXT("pin"), PinRole);
                UEdGraphPin* Pin = ResolvePatchPin(R.Target, PinRole, EGPD_Input);
                if (Pin == nullptr)
                {
                    UnmatchedSelectors.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("operation %d (set_pin_default): no input pin '%s' on %s"),
                        OpIndex, *PinRole, *DescribeNodeForSelector(R.Target))));
                }
                else
                {
                    R.TargetPin = Pin;
                    // Compared against what the pin already holds, so rerunning
                    // the same patch reports zero changes rather than rewriting
                    // an identical value and dirtying the package for nothing.
                    // Only a string value can be compared without applying it;
                    // anything else is treated as a change, which is the safe
                    // direction to be wrong in.
                    const TSharedPtr<FJsonValue> Value = (*OpObj)->TryGetField(TEXT("value"));
                    FString AsString;
                    // Equals with an explicit case sensitivity, NOT ==.
                    // FString::operator== folds case, so a pin holding "two"
                    // compared equal to "TWO" and the change was classified as
                    // a no-op: the patch reported converged and wrote nothing.
                    if (Value.IsValid() && Value->Type == EJson::String
                        && Value->TryGetString(AsString)
                        && Pin->DefaultValue.Equals(AsString, ESearchCase::CaseSensitive))
                    {
                        R.bUnchanged = true;
                        R.UnchangedReason = TEXT("pin already holds that value");
                    }
                    else
                    {
                        PinDefaultsToChange.Add(MakeShared<FJsonValueString>(FString::Printf(
                            TEXT("%s.%s: currently '%s'"),
                            *DescribeNodeForSelector(R.Target), *PinRole, *Pin->DefaultValue)));
                    }
                }
            }
            else if (Op == TEXT("update_node"))
            {
                NodesToUpdate.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("update %s"), *DescribeNodeForSelector(R.Target))));
            }
        }
        Resolved.Add(R);
    }

    Report->SetArrayField(TEXT("matched_nodes"), MatchedNodes);
    Report->SetArrayField(TEXT("unmatched_selectors"), UnmatchedSelectors);
    Report->SetArrayField(TEXT("ambiguous_selectors"), AmbiguousSelectors);
    Report->SetArrayField(TEXT("nodes_to_add"), NodesToAdd);
    Report->SetArrayField(TEXT("nodes_to_update"), NodesToUpdate);
    Report->SetArrayField(TEXT("nodes_to_remove"), NodesToRemove);
    Report->SetArrayField(TEXT("pin_defaults_to_change"), PinDefaultsToChange);
    Report->SetArrayField(TEXT("links_to_add"), LinksToAdd);
    Report->SetArrayField(TEXT("links_to_remove"), LinksToRemove);
    const int32 ExpectedChanges = NodesToAdd.Num() + NodesToUpdate.Num() + NodesToRemove.Num()
        + PinDefaultsToChange.Num() + LinksToAdd.Num() + LinksToRemove.Num();
    Report->SetNumberField(TEXT("expected_change_count"), ExpectedChanges);
    Report->SetNumberField(TEXT("requested_operation_count"), Resolved.Num());

    // A prediction is offered only where it is genuinely deterministic. A batch
    // that changes nothing leaves the graph it found, so the hash is known
    // exactly. Anything else depends on pin sets Unreal only materialises once
    // the node exists, and a guessed hash a caller then compares against is
    // worse than admitting there is none.
    Report->SetBoolField(TEXT("predicted_structure_hash_is_current"), ExpectedChanges == 0);
    if (ExpectedChanges != 0)
    {
        Report->SetStringField(TEXT("prediction_unavailable_reason"),
            TEXT("the batch changes the graph, and a node's structural key includes pin defaults "
                 "that Unreal only materialises once the node exists. A predicted hash is offered "
                 "only for a no-op batch, where it is the current hash by definition."));
    }

    if (UnmatchedSelectors.Num() > 0 || AmbiguousSelectors.Num() > 0)
    {
        // The per-selector detail is carried in the error text, not only in the
        // report arrays. A refusal travels back through the failure envelope,
        // which keeps the error string and drops the data, so a caller reading
        // only "1 selector matched more than one" would be told that something
        // is ambiguous and never told what it matched or how to disambiguate -
        // the actionable half, missing on the one path that needs it.
        auto TakeLines = [](const TArray<TSharedPtr<FJsonValue>>& Lines, int32 Limit,
                            TArray<FString>& Out)
        {
            for (int32 LineIndex = 0; LineIndex < Lines.Num() && LineIndex < Limit; ++LineIndex)
            {
                FString Line;
                if (Lines[LineIndex]->TryGetString(Line)) { Out.Add(Line); }
            }
        };
        // Bounded per array rather than across both, so a batch with many
        // unmatched selectors cannot crowd out the ambiguous ones or vice versa.
        // Each line is itself as long as its match list, which is the remaining
        // ceiling here. plan_only is not the escape hatch: this refusal is
        // decided before the plan_only branch, so a plan run of the same batch
        // returns this same string.
        const int32 PerKindLimit = 3;
        TArray<FString> Details;
        TakeLines(UnmatchedSelectors, PerKindLimit, Details);
        TakeLines(AmbiguousSelectors, PerKindLimit, Details);
        const int32 Withheld =
            UnmatchedSelectors.Num() + AmbiguousSelectors.Num() - Details.Num();

        FString Message = FString::Printf(
            TEXT("%d selector(s) matched no node and %d matched more than one. Nothing was changed: "
                 "a patch resolves its whole batch before it mutates anything. %s"),
            UnmatchedSelectors.Num(), AmbiguousSelectors.Num(),
            *FString::Join(Details, TEXT(" ")));
        if (Withheld > 0)
        {
            Message += FString::Printf(
                TEXT(" (+%d further selector(s) with the same problem, not listed here. Fix these "
                     "first; the next run reports the rest.)"), Withheld);
        }
        return Fail(Message);
    }

    if (bPlanOnly)
    {
        Report->SetBoolField(TEXT("plan_only"), true);
        return Finish(true);
    }

    // --- Pass 2: apply ---
    TArray<TSharedPtr<FJsonValue>> CreatedNodes, UpdatedNodes, RemovedNodes;
    TArray<TSharedPtr<FJsonValue>> ChangedPinDefaults, CreatedLinks, RemovedLinks;
    TArray<TSharedPtr<FJsonValue>> FailedOperations, UnchangedOperations;
    TMap<FString, UEdGraphNode*> AddedThisBatch;
    int32 Applied = 0;

    for (FResolvedOp& R : Resolved)
    {
        // A link operation's "nothing to do" is decided below, against the graph
        // as this batch has left it, never against the plan. Carrying the plan's
        // verdict here skipped the reconnect half of a disconnect-then-reconnect
        // pair - the plan saw the link still present - and left the link broken.
        // set_pin_default and move_node keep the plan-time short-circuit: no
        // other operation in a batch can change the answer for them, because
        // both are addressed by a resolved node that only they write.
        if (R.bUnchanged && R.Op != TEXT("connect_pins") && R.Op != TEXT("disconnect_pins"))
        {
            UnchangedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("operation %d (%s): %s"), R.Index, *R.Op, *R.UnchangedReason)));
            continue;
        }

        if (R.Op == TEXT("add_node"))
        {
            const TSharedPtr<FJsonObject>* NodeSpec = nullptr;
            (*R.Spec)->TryGetObjectField(TEXT("node"), NodeSpec);
            FString NodeType;
            (*NodeSpec)->TryGetStringField(TEXT("type"), NodeType);
            TArray<FString> Failures;
            UEdGraphNode* Node = SpawnNodeFromSpec(Graph, *NodeSpec, R.NewId, NodeType, Failures);
            if (Node == nullptr)
            {
                for (const FString& F : Failures) { FailedOperations.Add(MakeShared<FJsonValueString>(F)); }
                if (Failures.Num() == 0)
                {
                    FailedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                        TEXT("operation %d (add_node): the node factory created nothing for '%s'."),
                        R.Index, *NodeType)));
                }
                continue;
            }
            AddedThisBatch.Add(R.NewId, Node);
            CreatedNodes.Add(MakeShared<FJsonValueString>(DescribeNodeForSelector(Node)));
            Applied++;
        }
        else if (R.Op == TEXT("remove_node"))
        {
            const FString Described = DescribeNodeForSelector(R.Target);
            FBlueprintEditorUtils::RemoveNode(Blueprint, R.Target, /*bDontRecompile=*/true);
            RemovedNodes.Add(MakeShared<FJsonValueString>(Described));
            Applied++;
        }
        else if (R.Op == TEXT("move_node"))
        {
            double NewX = R.Target->NodePosX, NewY = R.Target->NodePosY;
            (*R.Spec)->TryGetNumberField(TEXT("x"), NewX);
            (*R.Spec)->TryGetNumberField(TEXT("y"), NewY);
            R.Target->Modify();
            R.Target->NodePosX = static_cast<int32>(NewX);
            R.Target->NodePosY = static_cast<int32>(NewY);
            UpdatedNodes.Add(MakeShared<FJsonValueString>(DescribeNodeForSelector(R.Target)));
            Applied++;
        }
        else if (R.Op == TEXT("set_pin_default"))
        {
            FString PinRole;
            (*R.Spec)->TryGetStringField(TEXT("pin"), PinRole);
            FString Error;
            if (!ApplyPinDefaultAndNotify(R.Target, R.TargetPin,
                    (*R.Spec)->TryGetField(TEXT("value")), Error))
            {
                FailedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (set_pin_default) on %s.%s: %s"),
                    R.Index, *DescribeNodeForSelector(R.Target), *PinRole, *Error)));
                continue;
            }
            ChangedPinDefaults.Add(MakeShared<FJsonValueString>(FString::Printf(
                TEXT("%s.%s = '%s'"), *DescribeNodeForSelector(R.Target), *PinRole,
                *R.TargetPin->DefaultValue)));
            Applied++;
        }
        else if (R.Op == TEXT("update_node"))
        {
            const TSharedPtr<FJsonObject>* Params = nullptr;
            (*R.Spec)->TryGetObjectField(TEXT("params"), Params);
            TArray<FString> Unknown;
            const TSet<FString> NoRoutingKeys;
            ApplyParamsAsPinDefaults(R.Target, Params != nullptr ? *Params : nullptr,
                NoRoutingKeys, DescribeNodeForSelector(R.Target), Unknown);
            if (Unknown.Num() > 0)
            {
                Unknown.Sort();
                FailedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (update_node) on %s: unknown_parameter: [%s] is not accepted "
                         "by this node type. Accepted: [%s]."),
                    R.Index, *DescribeNodeForSelector(R.Target),
                    *FString::Join(Unknown, TEXT(", ")),
                    *DescribeAcceptedParams(R.Target, NoRoutingKeys))));
                continue;
            }
            UpdatedNodes.Add(MakeShared<FJsonValueString>(DescribeNodeForSelector(R.Target)));
            Applied++;
        }
        else if (R.Op == TEXT("connect_pins") || R.Op == TEXT("disconnect_pins"))
        {
            // Re-resolved here rather than reused from pass 1, because an end may
            // name a node this batch has only just created.
            const TSharedPtr<FJsonObject>* FromObj = nullptr;
            const TSharedPtr<FJsonObject>* ToObj = nullptr;
            (*R.Spec)->TryGetObjectField(TEXT("from"), FromObj);
            (*R.Spec)->TryGetObjectField(TEXT("to"), ToObj);
            const TSharedPtr<FJsonObject>* FromSel = nullptr;
            const TSharedPtr<FJsonObject>* ToSel = nullptr;
            (*FromObj)->TryGetObjectField(TEXT("target"), FromSel);
            (*ToObj)->TryGetObjectField(TEXT("target"), ToSel);
            FString FromPinRole, ToPinRole;
            (*FromObj)->TryGetStringField(TEXT("pin"), FromPinRole);
            (*ToObj)->TryGetStringField(TEXT("pin"), ToPinRole);
            UEdGraphNode* FromNode = ResolveSelector(Graph, *FromSel, AddedThisBatch).Node;
            UEdGraphNode* ToNode = ResolveSelector(Graph, *ToSel, AddedThisBatch).Node;
            UEdGraphPin* FromPin = ResolvePatchPin(FromNode, FromPinRole, EGPD_Output);
            UEdGraphPin* ToPin = ResolvePatchPin(ToNode, ToPinRole, EGPD_Input);
            if (FromNode == nullptr || ToNode == nullptr || FromPin == nullptr || ToPin == nullptr)
            {
                FailedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s): an endpoint could not be resolved at apply time "
                         "(from pin '%s', to pin '%s')."),
                    R.Index, *R.Op, *FromPinRole, *ToPinRole)));
                continue;
            }
            // Asked here, of the live graph, because an earlier operation in this
            // same batch may have just changed the answer.
            const bool bWantLinked = R.Op == TEXT("connect_pins");
            if (FromPin->LinkedTo.Contains(ToPin) == bWantLinked)
            {
                UnchangedOperations.Add(MakeShared<FJsonValueString>(FString::Printf(
                    TEXT("operation %d (%s): %s"), R.Index, *R.Op,
                    bWantLinked ? TEXT("already linked") : TEXT("already not linked"))));
                continue;
            }
            const FString Line = FString::Printf(TEXT("%s.%s -> %s.%s"),
                *DescribeNodeForSelector(FromNode), *FromPinRole,
                *DescribeNodeForSelector(ToNode), *ToPinRole);
            // Nothing is marked dirty until the operation is known to be real,
            // so a converged rerun leaves the package as clean as it found it.
            FromNode->Modify();
            ToNode->Modify();
            if (bWantLinked)
            {
                FromPin->MakeLinkTo(ToPin);
                CreatedLinks.Add(MakeShared<FJsonValueString>(Line));
            }
            else
            {
                FromPin->BreakLinkTo(ToPin);
                RemovedLinks.Add(MakeShared<FJsonValueString>(Line));
            }
            FromNode->PinConnectionListChanged(FromPin);
            ToNode->PinConnectionListChanged(ToPin);
            Applied++;
        }
    }

    Report->SetNumberField(TEXT("applied_operation_count"), Applied);
    Report->SetArrayField(TEXT("failed_operations"), FailedOperations);
    Report->SetArrayField(TEXT("unchanged_operations"), UnchangedOperations);
    Report->SetArrayField(TEXT("created_nodes"), CreatedNodes);
    Report->SetArrayField(TEXT("updated_nodes"), UpdatedNodes);
    Report->SetArrayField(TEXT("removed_nodes"), RemovedNodes);
    Report->SetArrayField(TEXT("changed_pin_defaults"), ChangedPinDefaults);
    Report->SetArrayField(TEXT("created_links"), CreatedLinks);
    Report->SetArrayField(TEXT("removed_links"), RemovedLinks);

    if (FailedOperations.Num() > 0)
    {
        return Fail(FString::Printf(
            TEXT("%d of %d operation(s) failed during apply. The caller must roll back: this "
                 "function opens no transaction of its own."),
            FailedOperations.Num(), Resolved.Num()));
    }
    if (Applied > 0)
    {
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    return Finish(true);
}
