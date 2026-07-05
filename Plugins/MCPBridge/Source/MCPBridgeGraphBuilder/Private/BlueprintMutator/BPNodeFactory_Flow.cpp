// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPNodeFactory.h"
#include "BPNodeFactory_Internals.h"
#include "BPGLogCategories.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchName.h"
#include "K2Node_SwitchString.h"
#include "K2Node_MultiGate.h"
#include "K2Node_DoOnceMultiInput.h"
#include "Engine/UserDefinedEnum.h"

// =========================================================================================
// Phase 4.5 Batch 1 — Switch + MultiGate + DoOnceMultiInput (flow control factories)
//
// NOTE: This file uses the namespaced BPNodeFactoryInternal::ParseJsonConfig +
// PositionAndFinalize helpers (declared in BPNodeFactory_Internals.h). The original
// BPNodeFactory.cpp has an anonymous-namespace ParseJsonConfig that is NOT importable
// from this unity-build-unit; the namespaced helpers were introduced to avoid symbol
// collision while keeping existing factories untouched.
// =========================================================================================

//------------------------------------------------------------------------------
// Factory 1.1 — SwitchEnum → UK2Node_SwitchEnum
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateSwitchEnum(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString EnumPath;
    Cfg->TryGetStringField(TEXT("enum_path"), EnumPath);
    if (EnumPath.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSwitchEnum: missing enum_path"));
        return nullptr;
    }
    UEnum* Enum = LoadObject<UEnum>(nullptr, *EnumPath);
    if (!Enum)
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSwitchEnum: enum not found '%s'"), *EnumPath);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_SwitchEnum> Creator(*Graph);
    UK2Node_SwitchEnum* Node = Creator.CreateNode();
    // NOTE: UK2Node_SwitchEnum::SetEnum is not exported from the BlueprintGraph MinimalAPI
    // class (verified via LNK2019 against ?SetEnum@UK2Node_SwitchEnum@@...). We assign the
    // Enum UPROPERTY directly — CreateCasePins (invoked by Finalize → AllocateDefaultPins)
    // calls SetEnum itself when Enum != nullptr (K2Node_SwitchEnum.cpp line 144), which
    // regenerates EnumEntries + EnumFriendlyNames + emits case pins from inside the module.
    Node->Enum = Enum;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 1.2 — SwitchInt → UK2Node_SwitchInteger
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateSwitchInt(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    int32 StartIndex = 0;
    int32 NumCases   = 2;
    bool  bHasDefault = true;
    Cfg->TryGetNumberField(TEXT("start_index"), StartIndex);
    Cfg->TryGetNumberField(TEXT("num_cases"), NumCases);
    Cfg->TryGetBoolField  (TEXT("has_default"), bHasDefault);
    if (NumCases < 1) NumCases = 1;

    FGraphNodeCreator<UK2Node_SwitchInteger> Creator(*Graph);
    UK2Node_SwitchInteger* Node = Creator.CreateNode();
    Node->StartIndex     = StartIndex;        // direct UPROPERTY on UK2Node_SwitchInteger
    Node->bHasDefaultPin = bHasDefault;       // bitfield UPROPERTY on base UK2Node_Switch
    BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);

    // NOTE: Plan flagged [TBD] for the case-pin accessor. UK2Node_Switch has no public
    // GetNumCasePins() in 4.27 — we count Output exec pins (excluding the "default" which
    // is added when bHasDefaultPin=true). AddPinToSwitchNode is the virtual engine-canonical
    // API for growing the case list, implemented per-subclass (UK2Node_SwitchInteger uses it
    // to append the next-index case).
    auto CountCasePins = [bHasDefault](UK2Node_SwitchInteger* N)
    {
        int32 Count = 0;
        for (UEdGraphPin* P : N->Pins)
        {
            if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                // Exclude the "Default" pin from the case-count.
                if (bHasDefault && P->PinName == TEXT("Default")) continue;
                ++Count;
            }
        }
        return Count;
    };
    while (CountCasePins(Node) < NumCases) Node->AddPinToSwitchNode();
    return Node;
}

//------------------------------------------------------------------------------
// Factory 1.3 — SwitchName → UK2Node_SwitchName
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateSwitchName(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    const TArray<TSharedPtr<FJsonValue>>* CasesArr = nullptr;
    if (!Cfg->TryGetArrayField(TEXT("case_values"), CasesArr))
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSwitchName: missing case_values"));
        return nullptr;
    }
    bool bHasDefault = true;
    Cfg->TryGetBoolField(TEXT("has_default"), bHasDefault);

    FGraphNodeCreator<UK2Node_SwitchName> Creator(*Graph);
    UK2Node_SwitchName* Node = Creator.CreateNode();
    Node->PinNames.Reset();
    for (const TSharedPtr<FJsonValue>& V : *CasesArr)
    {
        FString Name;
        if (V.IsValid() && V->TryGetString(Name)) Node->PinNames.Add(FName(*Name));
    }
    Node->bHasDefaultPin = bHasDefault;
    // NOTE: PinNames must be set BEFORE Finalize — CreateCasePins (invoked by Finalize →
    // AllocateDefaultPins) iterates PinNames to emit the exec output pins.
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 1.4 — SwitchString → UK2Node_SwitchString
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateSwitchString(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    const TArray<TSharedPtr<FJsonValue>>* CasesArr = nullptr;
    if (!Cfg->TryGetArrayField(TEXT("case_values"), CasesArr))
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateSwitchString: missing case_values"));
        return nullptr;
    }
    bool bHasDefault = true;
    bool bCaseSens   = false;
    Cfg->TryGetBoolField(TEXT("has_default"), bHasDefault);
    Cfg->TryGetBoolField(TEXT("is_case_sensitive"), bCaseSens);

    FGraphNodeCreator<UK2Node_SwitchString> Creator(*Graph);
    UK2Node_SwitchString* Node = Creator.CreateNode();
    Node->PinNames.Reset();
    for (const TSharedPtr<FJsonValue>& V : *CasesArr)
    {
        FString S;
        if (V.IsValid() && V->TryGetString(S)) Node->PinNames.Add(FName(*S));
    }
    Node->bHasDefaultPin   = bHasDefault;
    Node->bIsCaseSensitive = bCaseSens;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 1.5 — MultiGate → UK2Node_MultiGate
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateMultiGate(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    int32 NumOutputs     = 2;
    bool  bRandom        = false;
    bool  bLoop          = false;
    bool  bStartFromZero = true;
    Cfg->TryGetNumberField(TEXT("num_outputs"), NumOutputs);
    Cfg->TryGetBoolField(TEXT("is_random"), bRandom);
    Cfg->TryGetBoolField(TEXT("loop"), bLoop);
    Cfg->TryGetBoolField(TEXT("start_index_from_zero"), bStartFromZero);
    if (NumOutputs < 1) NumOutputs = 1;

    FGraphNodeCreator<UK2Node_MultiGate> Creator(*Graph);
    UK2Node_MultiGate* Node = Creator.CreateNode();
    BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);

    // NOTE: [TBD resolved] MultiGate behavior knobs are exposed as input pins (not UPROPERTYs) —
    // verified via K2Node_MultiGate.cpp lines 520-523 (CreatePin for "IsRandom", "Loop",
    // "StartIndex" with "-1" as autogenerated default). The typed accessors GetLoopPin/
    // GetIsRandomPin/GetStartIndexPin exist but are NOT exported from BlueprintGraph
    // MinimalAPI (verified via LNK2019). We call FindPin directly with the literal pin
    // names the cpp uses, which is the same pattern CreateCreateWidget uses for "Class".
    // NOTE: [TBD resolved] Output pin growth — UK2Node_MultiGate inherits AddInputPin()
    // from UK2Node_ExecutionSequence (confirmed K2Node_ExecutionSequence.h line 40). The
    // method name is misleading: AddInputPin on a sequence-like node appends an OUTPUT exec
    // pin. We count via a local lambda for parity with CreateSequence in BPNodeFactory.cpp.
    auto CountOutputs = [](UK2Node_MultiGate* N)
    {
        int32 Count = 0;
        for (UEdGraphPin* P : N->Pins)
        {
            if (P && P->Direction == EGPD_Output && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                ++Count;
        }
        return Count;
    };
    while (CountOutputs(Node) < NumOutputs) Node->AddInputPin();

    // Behavior flag defaults via FindPin on the literal pin names emitted by
    // UK2Node_MultiGate::AllocateDefaultPins (K2Node_MultiGate.cpp lines 520-523).
    if (UEdGraphPin* LoopPin = Node->FindPin(TEXT("Loop")))
    {
        LoopPin->DefaultValue = bLoop ? TEXT("true") : TEXT("false");
    }
    if (UEdGraphPin* RandomPin = Node->FindPin(TEXT("IsRandom")))
    {
        RandomPin->DefaultValue = bRandom ? TEXT("true") : TEXT("false");
    }
    if (UEdGraphPin* StartIndexPin = Node->FindPin(TEXT("StartIndex")))
    {
        // start_index_from_zero=true → StartIndex=0. When false we use -1, which is the
        // engine's sentinel ("use last-fired+1 on next fire", per MultiGate expansion logic).
        StartIndexPin->DefaultValue = bStartFromZero ? TEXT("0") : TEXT("-1");
    }
    return Node;
}

//------------------------------------------------------------------------------
// Factory 1.6 — DoOnceMultiInput → UK2Node_DoOnceMultiInput
//------------------------------------------------------------------------------
UEdGraphNode* FBPNodeFactory::CreateDoOnceMultiInput(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    int32 NumInputs = 2;
    Cfg->TryGetNumberField(TEXT("num_inputs"), NumInputs);
    if (NumInputs < 1) NumInputs = 1;

    FGraphNodeCreator<UK2Node_DoOnceMultiInput> Creator(*Graph);
    UK2Node_DoOnceMultiInput* Node = Creator.CreateNode();
    BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);

    // NOTE: UK2Node_DoOnceMultiInput has NumBaseInputs=1 (private static, K2Node_DoOnceMultiInput.h
    // line 34) so Finalize creates 1 input exec pin by default. IK2Node_AddPinInterface's
    // AddInputPin (line 67) grows the pin list one at a time; GetMaxInputPinsNum() caps it.
    auto CountInputs = [](UK2Node_DoOnceMultiInput* N)
    {
        int32 Count = 0;
        for (UEdGraphPin* P : N->Pins)
        {
            if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                ++Count;
        }
        return Count;
    };
    while (CountInputs(Node) < NumInputs && Node->CanAddPin())
    {
        Node->AddInputPin();
    }
    return Node;
}
