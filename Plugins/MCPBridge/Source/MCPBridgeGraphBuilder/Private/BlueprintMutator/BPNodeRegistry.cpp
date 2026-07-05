// Copyright 2026 RareBird Games. All Rights Reserved.

#include "BPNodeRegistry.h"
#include "BPNodeFactory.h"
#include "BPGLogCategories.h"

TMap<FString, FBPNodeRegistry::FFactory>& FBPNodeRegistry::Map()
{
    static TMap<FString, FFactory> Singleton;
    return Singleton;
}

void FBPNodeRegistry::Register(const FString& NodeType, FFactory Factory)
{
    Map().Add(NodeType, MoveTemp(Factory));
}

FBPNodeRegistry::FFactory FBPNodeRegistry::Find(const FString& NodeType)
{
    const FFactory* Found = Map().Find(NodeType);
    return Found ? *Found : FFactory();
}

TArray<FString> FBPNodeRegistry::ListRegistered()
{
    TArray<FString> Names;
    Map().GetKeys(Names);
    return Names;
}

void FBPNodeRegistry::Initialize()
{
    UE_LOG(LogBlueprintMutator, Log, TEXT("FBPNodeRegistry::Initialize()"));
    // Factory registrations populated incrementally by Tasks 3.2-3.18.
    Register(TEXT("CallFunction"), &FBPNodeFactory::CreateCallFunction);
    Register(TEXT("Event"),        &FBPNodeFactory::CreateEvent);
    Register(TEXT("CustomEvent"),  &FBPNodeFactory::CreateCustomEvent);
    Register(TEXT("VariableGet"),  &FBPNodeFactory::CreateVariableGet);
    Register(TEXT("VariableSet"),  &FBPNodeFactory::CreateVariableSet);
    Register(TEXT("Branch"),       &FBPNodeFactory::CreateBranch);
    Register(TEXT("Sequence"),     &FBPNodeFactory::CreateSequence);
    Register(TEXT("Knot"),         &FBPNodeFactory::CreateKnot);
    Register(TEXT("Comment"),      &FBPNodeFactory::CreateComment);
    Register(TEXT("Cast"),         &FBPNodeFactory::CreateCast);
    Register(TEXT("CreateWidget"), &FBPNodeFactory::CreateCreateWidget);
    Register(TEXT("SpawnActor"),   &FBPNodeFactory::CreateSpawnActor);
    Register(TEXT("MacroInstance"), &FBPNodeFactory::CreateMacroInstance);
    Register(TEXT("MakeStruct"),    &FBPNodeFactory::CreateMakeStruct);
    Register(TEXT("BreakStruct"),   &FBPNodeFactory::CreateBreakStruct);
    Register(TEXT("Select"),        &FBPNodeFactory::CreateSelect);
    Register(TEXT("FormatText"),    &FBPNodeFactory::CreateFormatText);
    // Phase 4.5 Batch 1 — Switch + MultiGate + DoOnceMultiInput
    Register(TEXT("SwitchEnum"),       &FBPNodeFactory::CreateSwitchEnum);
    Register(TEXT("SwitchInt"),        &FBPNodeFactory::CreateSwitchInt);
    Register(TEXT("SwitchName"),       &FBPNodeFactory::CreateSwitchName);
    Register(TEXT("SwitchString"),     &FBPNodeFactory::CreateSwitchString);
    Register(TEXT("MultiGate"),        &FBPNodeFactory::CreateMultiGate);
    Register(TEXT("DoOnceMultiInput"), &FBPNodeFactory::CreateDoOnceMultiInput);
    // Phase 4.5 Batch 2 — Input Action/Axis/Key/Touch + Getters (12 live factories)
    Register(TEXT("InputAction"),              &FBPNodeFactory::CreateInputAction);
    Register(TEXT("InputAxisEvent"),           &FBPNodeFactory::CreateInputAxisEvent);
    Register(TEXT("InputKey"),                 &FBPNodeFactory::CreateInputKey);
    Register(TEXT("InputKeyEvent"),            &FBPNodeFactory::CreateInputKeyEvent);
    Register(TEXT("InputTouch"),               &FBPNodeFactory::CreateInputTouch);
    Register(TEXT("InputTouchEvent"),          &FBPNodeFactory::CreateInputTouchEvent);
    Register(TEXT("InputVectorAxisEvent"),     &FBPNodeFactory::CreateInputVectorAxisEvent);
    Register(TEXT("InputAxisKeyEvent"),        &FBPNodeFactory::CreateInputAxisKeyEvent);
    Register(TEXT("GetInputAxisValue"),        &FBPNodeFactory::CreateGetInputAxisValue);
    Register(TEXT("GetInputAxisKeyValue"),     &FBPNodeFactory::CreateGetInputAxisKeyValue);
    Register(TEXT("GetInputVectorAxisValue"),  &FBPNodeFactory::CreateGetInputVectorAxisValue);
    Register(TEXT("InputActionEvent"),         &FBPNodeFactory::CreateInputActionEvent);
    // Register(TEXT("EnhancedInputAction"),   &FBPNodeFactory::CreateEnhancedInputAction);  // Deferred to Batch 15
    // Phase 4.5 Batch 3 — Delegates (Add/Remove/Call/Clear/Assign/Create)
    Register(TEXT("AddDelegate"),    &FBPNodeFactory::CreateAddDelegate);
    Register(TEXT("RemoveDelegate"), &FBPNodeFactory::CreateRemoveDelegate);
    Register(TEXT("CallDelegate"),   &FBPNodeFactory::CreateCallDelegate);
    Register(TEXT("ClearDelegate"),  &FBPNodeFactory::CreateClearDelegate);
    Register(TEXT("AssignDelegate"), &FBPNodeFactory::CreateAssignDelegate);
    Register(TEXT("CreateDelegate"), &FBPNodeFactory::CreateCreateDelegate);
    UE_LOG(LogBlueprintMutator, Log, TEXT("FBPNodeRegistry: registered %d factories"), Map().Num());
}
