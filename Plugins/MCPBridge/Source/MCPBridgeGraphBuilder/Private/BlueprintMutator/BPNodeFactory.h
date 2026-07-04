#pragma once
#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;

class FBPNodeFactory
{
public:
    static UEdGraphNode* CreateCallFunction(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateEvent      (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateCustomEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateVariableGet(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateVariableSet(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateBranch  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSequence(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateKnot    (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateComment (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateCast        (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateCreateWidget(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSpawnActor  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateMacroInstance(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateMakeStruct   (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateBreakStruct  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSelect       (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateFormatText   (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);

    // Phase 4.5 Batch 1 — Switch + MultiGate + DoOnceMultiInput (flow control)
    static UEdGraphNode* CreateSwitchEnum       (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSwitchInt        (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSwitchName       (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateSwitchString     (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateMultiGate        (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateDoOnceMultiInput (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);

    // Phase 4.5 Batch 2 — Input Action/Axis/Key/Touch + Getters (12 factories)
    // Factory 2.13 (EnhancedInputAction) is deferred to Batch 15 per plan — scaffold below is
    // left commented out until the project-level EnhancedInput plugin gate is added.
    static UEdGraphNode* CreateInputAction           (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputAxisEvent        (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputKey              (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputKeyEvent         (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputTouch            (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputTouchEvent       (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputVectorAxisEvent  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputAxisKeyEvent     (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateGetInputAxisValue     (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateGetInputAxisKeyValue  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateGetInputVectorAxisValue(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateInputActionEvent      (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    // static UEdGraphNode* CreateEnhancedInputAction   (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson); // Deferred to Batch 15

    // Phase 4.5 Batch 3 — Delegates (Add/Remove/Call/Clear/Assign/Create)
    static UEdGraphNode* CreateAddDelegate    (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateRemoveDelegate (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateCallDelegate   (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateClearDelegate  (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateAssignDelegate (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
    static UEdGraphNode* CreateCreateDelegate (UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson);
};
