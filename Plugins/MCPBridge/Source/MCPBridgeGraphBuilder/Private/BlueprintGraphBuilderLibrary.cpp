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
#include "Engine/Blueprint.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
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
    void ApplyParamsAsPinDefaults(
        UEdGraphNode* Node,
        const TSharedPtr<FJsonObject>& Params,
        const TSet<FString>& RoutingKeys,
        const FString& NodeId)
    {
        if (Node == nullptr || !Params.IsValid())
        {
            return;
        }
        for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
        {
            if (RoutingKeys.Contains(Pair.Key))
            {
                continue;
            }
            UEdGraphPin* Pin = Node->FindPin(FName(*Pair.Key));
            FString Error;
            if (!ApplyPinDefaultAndNotify(Node, Pin, Pair.Value, Error))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("BuildBlueprintFromJSON: node '%s' param '%s' %s"),
                    *NodeId, *Pair.Key, *Error);
            }
        }
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
    BuildBlueprintFromJSONWithReport(
        Blueprint, JsonString, bClearExistingGraph, UnresolvedConnections, ConnectionsMade);
}

void UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSONWithReport(
    UBlueprint* Blueprint,
    const FString& JsonString,
    bool bClearExistingGraph,
    TArray<FString>& OutUnresolvedConnections,
    int32& OutConnectionsMade)
{
    OutUnresolvedConnections.Reset();
    OutConnectionsMade = 0;

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

    // --- Step 3: Clear existing nodes ---
    if (bClearExistingGraph)
    {
        TArray<UEdGraphNode*> NodesToRemove = Graph->Nodes;
        for (UEdGraphNode* Node : NodesToRemove)
        {
            FBlueprintEditorUtils::RemoveNode(Blueprint, Node, /*bDontRecompile=*/true);
        }
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
                    continue;
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
                    continue;
                }
            }

            UFunction* Func = TargetClass != nullptr ? TargetClass->FindFunctionByName(*FuncName) : nullptr;
            if (Func == nullptr)
            {
                UE_LOG(LogTemp, Error,
                    TEXT("BuildBlueprintFromJSON: node '%s' function '%s' not found on '%s'"),
                    *NodeId, *FuncName,
                    TargetClass != nullptr ? *TargetClass->GetName() : TEXT("<null class>"));
                continue;
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
                continue;
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
            continue;
        }

        ApplyParamsAsPinDefaults(SpawnedNode, Params, RoutingKeys, NodeId);

        // Per-node position override (top-level "x" / "y" on the node spec)
        if (SpawnedNode)
        {
            int32 PosX, PosY;
            if ((*NodeObj)->TryGetNumberField(TEXT("x"), PosX)) SpawnedNode->NodePosX = PosX;
            if ((*NodeObj)->TryGetNumberField(TEXT("y"), PosY)) SpawnedNode->NodePosY = PosY;
        }

        NodeMap.Add(NodeId, SpawnedNode);
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

            // Resolve a pin by role:
            //   "exec" — direction-aware: output-side maps to PN_Then, input-side to PN_Execute
            //   "then" — always PN_Then
            //   "AsResult" — a dynamic cast's result pin, asked for by the node
            //   anything else — treated as a literal pin name via FindPin
            auto ResolvePin = [](UEdGraphNode* N, const FString& Role, EEdGraphPinDirection Dir) -> UEdGraphPin*
            {
                if (Role == TEXT("exec"))
                {
                    return N->FindPin(Dir == EGPD_Output ? UEdGraphSchema_K2::PN_Then : UEdGraphSchema_K2::PN_Execute);
                }
                if (Role == TEXT("then"))
                {
                    return N->FindPin(UEdGraphSchema_K2::PN_Then);
                }
                // A cast names its result pin "As" plus the target type's
                // DISPLAY name (K2Node_DynamicCast.cpp:63), which for a
                // Blueprint generated class is neither the asset name nor
                // anything a caller can compute from the spec. Ask the node.
                if (Role == TEXT("AsResult"))
                {
                    if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(N))
                    {
                        return CastNode->GetCastResultPin();
                    }
                }
                return N->FindPin(FName(*Role));
            };

            UEdGraphPin* SourcePin = ResolvePin(*FromNodePtr, FromPinRole, EGPD_Output);
            UEdGraphPin* TargetPin = ResolvePin(*ToNodePtr, ToPinRole, EGPD_Input);

            if (!SourcePin || !TargetPin)
            {
                UE_LOG(LogTemp, Warning, TEXT("BuildBlueprintFromJSON: Could not resolve pins for connection %s -> %s"), *FromStr, *ToStr);
                OutUnresolvedConnections.Add(FString::Printf(
                    TEXT("%s -> %s (no %s pin '%s' on %s)"),
                    *FromStr, *ToStr,
                    !SourcePin ? TEXT("output") : TEXT("input"),
                    !SourcePin ? *FromPinRole : *ToPinRole,
                    !SourcePin ? *FromNodeId : *ToNodeId));
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
