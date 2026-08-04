// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPMemberReader.h"
#include "BPGLogCategories.h"
#include "BPTypeDescriptor.h"
#include "BlueprintMutatorLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_Tunnel.h"
#include "UObject/Class.h"
#include "UObject/Script.h"

FString FBPMemberReader::ListGraphs(UBlueprint* Blueprint)
{
    if (!Blueprint)
    {
        return TEXT("{\"error\":\"blueprint null\"}");
    }

    UE_LOG(LogBlueprintInspector, Log, TEXT("ListGraphs: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;

    auto AddGraphs = [&](const TArray<UEdGraph*>& Graphs, const TCHAR* TypeLabel)
    {
        for (UEdGraph* Graph : Graphs)
        {
            if (!Graph) continue;
            TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
            Entry->SetStringField(TEXT("name"), Graph->GetName());
            Entry->SetStringField(TEXT("type"), TypeLabel);
            Out.Add(MakeShared<FJsonValueObject>(Entry));
        }
    };

    AddGraphs(Blueprint->UbergraphPages, TEXT("Ubergraph"));
    AddGraphs(Blueprint->FunctionGraphs, TEXT("Function"));
    AddGraphs(Blueprint->MacroGraphs, TEXT("Macro"));

    FString Result;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(Out, Writer);
    return Result;
}

namespace
{
    /** Serialize the user-defined parameter pins on an entry/result node as
        name+type+default JSON entries.

        The default is reported because a caller cannot converge on what it
        cannot read. A parameter default lives in FUserPinInfo::PinDefaultValue
        and nowhere else a reader can reach, so leaving it out made every
        set-a-default request look unsatisfied forever: the writer wrote it, the
        reader could not see it, and the next identical request wrote it again. */
    TArray<TSharedPtr<FJsonValue>> SerializeUserDefinedPins(const TArray<TSharedPtr<FUserPinInfo>>& Pins)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const TSharedPtr<FUserPinInfo>& Info : Pins)
        {
            if (!Info.IsValid()) continue;
            TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
            P->SetStringField(TEXT("name"), Info->PinName.ToString());
            P->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(Info->PinType));
            P->SetStringField(TEXT("default"), Info->PinDefaultValue);
            Out.Add(MakeShared<FJsonValueObject>(P));
        }
        return Out;
    }

    /** A function's access level, from the one place UE4.27 keeps it: the
        entry node's extra flags. Exactly one of the three is set on a
        user-declared function; a function that somehow carries none reports
        "public", which is what the compiler treats an unflagged function as. */
    FString FunctionAccessLevel(int32 ExtraFlags)
    {
        if ((ExtraFlags & FUNC_Private) != 0)   { return TEXT("private"); }
        if ((ExtraFlags & FUNC_Protected) != 0) { return TEXT("protected"); }
        return TEXT("public");
    }

    /** Local variables declared on a function graph.

        These are FBPVariableDescription entries on the ENTRY NODE, not on
        Blueprint::NewVariables, so no member reader that walks the Blueprint's
        variable list will ever see them. */
    TArray<TSharedPtr<FJsonValue>> SerializeLocalVariables(const TArray<FBPVariableDescription>& Locals)
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FBPVariableDescription& Local : Locals)
        {
            TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
            L->SetStringField(TEXT("name"), Local.VarName.ToString());
            L->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(Local.VarType));
            L->SetStringField(TEXT("default"), Local.DefaultValue);
            Out.Add(MakeShared<FJsonValueObject>(L));
        }
        // Canonical order, for the same reason the snapshot sorts every other
        // member collection: the engine's array order is the order they were
        // added, and a hash over it would move when nothing had changed.
        Out.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
        {
            const TSharedPtr<FJsonObject>* OA = nullptr;
            const TSharedPtr<FJsonObject>* OB = nullptr;
            const FString NA = A.IsValid() && A->TryGetObject(OA) ? (*OA)->GetStringField(TEXT("name")) : FString();
            const FString NB = B.IsValid() && B->TryGetObject(OB) ? (*OB)->GetStringField(TEXT("name")) : FString();
            return NA < NB;
        });
        return Out;
    }

    bool DetectIsOverride(UK2Node_FunctionEntry* Entry, UBlueprint* Blueprint)
    {
        if (!Entry || !Blueprint) return false;
        const FName FnName = Entry->FunctionReference.GetMemberName();
        if (FnName.IsNone()) return false;
        UClass* ParentClass = Blueprint->ParentClass;
        if (!ParentClass) return false;
        return ParentClass->FindFunctionByName(FnName) != nullptr;
    }
}

FString FBPMemberReader::ListFunctions(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListFunctions: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (!Graph) continue;

        UK2Node_FunctionEntry* Entry = nullptr;
        UK2Node_FunctionResult* Result = nullptr;
        for (UEdGraphNode* N : Graph->Nodes)
        {
            if (!Entry)  Entry  = Cast<UK2Node_FunctionEntry>(N);
            if (!Result) Result = Cast<UK2Node_FunctionResult>(N);
        }

        TSharedPtr<FJsonObject> FnObj = MakeShared<FJsonObject>();
        FnObj->SetStringField(TEXT("name"), Graph->GetName());
        FnObj->SetStringField(TEXT("category"),
            Entry ? Entry->MetaData.Category.ToString() : FString());

        const int32 ExtraFlags = Entry ? Entry->GetExtraFlags() : 0;
        const bool bIsPure  = (ExtraFlags & FUNC_BlueprintPure) != 0;
        const bool bIsConst = (ExtraFlags & FUNC_Const) != 0;
        const bool bIsOverride = DetectIsOverride(Entry, Blueprint);

        FnObj->SetBoolField(TEXT("is_pure"), bIsPure);
        FnObj->SetBoolField(TEXT("is_override"), bIsOverride);
        FnObj->SetBoolField(TEXT("is_const"), bIsConst);
        FnObj->SetStringField(TEXT("tooltip"),
            Entry ? Entry->MetaData.ToolTip.ToString() : FString());
        FnObj->SetStringField(TEXT("access"), FunctionAccessLevel(ExtraFlags));

        static const TArray<TSharedPtr<FUserPinInfo>> EmptyPins;
        FnObj->SetArrayField(TEXT("inputs"),  SerializeUserDefinedPins(Entry  ? Entry->UserDefinedPins  : EmptyPins));
        FnObj->SetArrayField(TEXT("outputs"), SerializeUserDefinedPins(Result ? Result->UserDefinedPins : EmptyPins));

        static const TArray<FBPVariableDescription> EmptyLocals;
        FnObj->SetArrayField(TEXT("locals"),
            SerializeLocalVariables(Entry ? Entry->LocalVariables : EmptyLocals));

        Out.Add(MakeShared<FJsonValueObject>(FnObj));
    }

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}

FString FBPMemberReader::ListVariables(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListVariables: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FBPVariableDescription& V : Blueprint->NewVariables)
    {
        // An event dispatcher IS a member variable: a multicast delegate one,
        // paired with a signature graph. ListEventDispatchers already reports
        // them, and reporting the same member in both lists would offer a caller
        // remove_variable on something only remove_event_dispatcher can correctly
        // take apart. SMyBlueprint.cpp:1232-1236 excludes delegate properties
        // from the editor's own variable list for the same reason.
        if (V.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate) continue;

        TSharedPtr<FJsonObject> VO = MakeShared<FJsonObject>();
        VO->SetStringField(TEXT("name"), V.VarName.ToString());
        VO->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(V.VarType));
        VO->SetStringField(TEXT("category"), V.Category.ToString());

        // The CDO, not V.DefaultValue. The description is editor scratch that
        // KismetCompiler empties on every Full compile after copying it into
        // the CDO, so reporting it answered "" for every variable on any
        // Blueprint compiled since the value was set. Finding 0p.
        //
        // The fallback is not a safety net, it is the correct answer in the one
        // case the CDO cannot give one: before a variable's first full compile
        // there is no generated property to read.
        FString DefaultValue;
        if (!UBlueprintMutatorLibrary::TryReadVariableDefaultFromCDO(Blueprint, V.VarName, DefaultValue))
        {
            DefaultValue = V.DefaultValue;
        }
        VO->SetStringField(TEXT("default_value"), DefaultValue);
        VO->SetBoolField(TEXT("is_editable"), (V.PropertyFlags & CPF_Edit) != 0);
        VO->SetBoolField(TEXT("is_replicated"), (V.PropertyFlags & CPF_Net) != 0);

        FString Tooltip;
        for (const FBPVariableMetaDataEntry& Entry : V.MetaDataArray)
        {
            if (Entry.DataKey == FName(TEXT("tooltip")))
            {
                Tooltip = Entry.DataValue;
                break;
            }
        }
        VO->SetStringField(TEXT("tooltip"), Tooltip);

        Out.Add(MakeShared<FJsonValueObject>(VO));
    }

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}

FString FBPMemberReader::ListMacros(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListMacros: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;
    for (UEdGraph* Graph : Blueprint->MacroGraphs)
    {
        if (!Graph) continue;

        TArray<TSharedPtr<FJsonValue>> Inputs;
        TArray<TSharedPtr<FJsonValue>> Outputs;

        for (UEdGraphNode* N : Graph->Nodes)
        {
            UK2Node_Tunnel* Tunnel = Cast<UK2Node_Tunnel>(N);
            if (!Tunnel) continue;
            for (const TSharedPtr<FUserPinInfo>& Info : Tunnel->UserDefinedPins)
            {
                if (!Info.IsValid()) continue;
                TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
                P->SetStringField(TEXT("name"), Info->PinName.ToString());
                P->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(Info->PinType));
                if (Info->DesiredPinDirection == EGPD_Output)
                {
                    Inputs.Add(MakeShared<FJsonValueObject>(P));
                }
                else
                {
                    Outputs.Add(MakeShared<FJsonValueObject>(P));
                }
            }
        }

        TSharedPtr<FJsonObject> MacroObj = MakeShared<FJsonObject>();
        MacroObj->SetStringField(TEXT("name"), Graph->GetName());
        MacroObj->SetStringField(TEXT("category"), FString());
        MacroObj->SetArrayField(TEXT("inputs"), Inputs);
        MacroObj->SetArrayField(TEXT("outputs"), Outputs);
        Out.Add(MakeShared<FJsonValueObject>(MacroObj));
    }

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}

FString FBPMemberReader::ListInterfaces(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListInterfaces: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;
    for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
    {
        TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("name"), Desc.Interface ? Desc.Interface->GetName() : FString());
        O->SetStringField(TEXT("path"), Desc.Interface ? Desc.Interface->GetPathName() : FString());
        Out.Add(MakeShared<FJsonValueObject>(O));
    }
    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}

FString FBPMemberReader::ListEventDispatchers(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListEventDispatchers: %s"), *Blueprint->GetName());

    TArray<TSharedPtr<FJsonValue>> Out;
    for (UEdGraph* G : Blueprint->DelegateSignatureGraphs)
    {
        if (!G) continue;
        UK2Node_FunctionEntry* Entry = nullptr;
        for (UEdGraphNode* N : G->Nodes)
        {
            if (!Entry) Entry = Cast<UK2Node_FunctionEntry>(N);
        }

        TArray<TSharedPtr<FJsonValue>> Params;
        if (Entry)
        {
            for (const TSharedPtr<FUserPinInfo>& Info : Entry->UserDefinedPins)
            {
                if (!Info.IsValid()) continue;
                TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
                P->SetStringField(TEXT("name"), Info->PinName.ToString());
                P->SetObjectField(TEXT("type"), FBPTypeDescriptor::ToJson(Info->PinType));
                Params.Add(MakeShared<FJsonValueObject>(P));
            }
        }

        TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("name"), G->GetName());
        O->SetArrayField(TEXT("parameters"), Params);
        Out.Add(MakeShared<FJsonValueObject>(O));
    }

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}

FString FBPMemberReader::ListSCSNodes(UBlueprint* Blueprint)
{
    if (!Blueprint) return TEXT("{\"error\":\"blueprint null\"}");
    UE_LOG(LogBlueprintInspector, Log, TEXT("ListSCSNodes: %s"), *Blueprint->GetName());

    USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
    if (!SCS) return TEXT("[]");

    // Build parent lookup by walking root nodes + their children recursively.
    TMap<USCS_Node*, USCS_Node*> ParentOf;
    for (USCS_Node* Root : SCS->GetRootNodes())
    {
        if (!Root) continue;
        TArray<USCS_Node*> Stack;
        Stack.Add(Root);
        while (Stack.Num() > 0)
        {
            USCS_Node* Cur = Stack.Pop();
            for (USCS_Node* Child : Cur->GetChildNodes())
            {
                if (!Child) continue;
                ParentOf.Add(Child, Cur);
                Stack.Add(Child);
            }
        }
    }

    TArray<TSharedPtr<FJsonValue>> Out;
    const TArray<USCS_Node*>& Roots = SCS->GetRootNodes();
    for (USCS_Node* N : SCS->GetAllNodes())
    {
        if (!N) continue;
        TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("name"), N->GetVariableName().ToString());

        FString ClassPath;
        if (N->ComponentTemplate && N->ComponentTemplate->GetClass())
        {
            ClassPath = N->ComponentTemplate->GetClass()->GetPathName();
        }
        O->SetStringField(TEXT("class_path"), ClassPath);

        FString ParentName;
        if (USCS_Node** Parent = ParentOf.Find(N))
        {
            if (*Parent)
            {
                ParentName = (*Parent)->GetVariableName().ToString();
            }
        }
        O->SetStringField(TEXT("parent"), ParentName);

        O->SetBoolField(TEXT("is_root"), Roots.Contains(N));

        Out.Add(MakeShared<FJsonValueObject>(O));
    }

    FString S;
    TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
    FJsonSerializer::Serialize(Out, W);
    return S;
}
