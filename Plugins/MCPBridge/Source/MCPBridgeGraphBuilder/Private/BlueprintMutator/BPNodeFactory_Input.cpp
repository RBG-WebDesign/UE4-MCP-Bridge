#include "BPNodeFactory.h"
#include "BPNodeFactory_Internals.h"
#include "BPGLogCategories.h"
#include "EdGraph/EdGraph.h"

// InputCore FKey marshaling (added to Build.cs as part of Batch 2).
#include "InputCoreTypes.h"
// FInputChord used by UK2Node_InputKeyEvent (Slate framework commands).
#include "Framework/Commands/InputChord.h"
// EInputEvent (IE_Pressed etc.) for event-variant input nodes.
#include "Engine/EngineBaseTypes.h"

#include "K2Node_InputAction.h"
#include "K2Node_InputActionEvent.h"
#include "K2Node_InputAxisEvent.h"
#include "K2Node_InputAxisKeyEvent.h"
#include "K2Node_InputKey.h"
#include "K2Node_InputKeyEvent.h"
#include "K2Node_InputTouch.h"
#include "K2Node_InputTouchEvent.h"
#include "K2Node_InputVectorAxisEvent.h"
#include "K2Node_GetInputAxisValue.h"
#include "K2Node_GetInputAxisKeyValue.h"
#include "K2Node_GetInputVectorAxisValue.h"
// Needed because the getter-node Initialize() helpers are NOT exported from BlueprintGraph
// (their classes are MinimalAPI). We replicate the helper bodies in-factory by calling
// UK2Node_CallFunction::SetFromFunction on AActor::Get{Input|InputAxis|...}Value.
#include "GameFramework/Actor.h"

// =========================================================================================
// Phase 4.5 Batch 2 — Input Action/Axis/Key/Touch + Getters (12 live factories).
//
// All bodies use the BPNodeFactoryInternal::ParseJsonConfig + PositionAndFinalize helpers
// (declared in BPNodeFactory_Internals.h). No `using namespace` — explicit qualification
// keeps us safe from the anon-namespace ParseJsonConfig in BPNodeFactory.cpp when the
// unity build fuses translation units.
//
// Build.cs note: this batch adds the "InputCore" PrivateDependencyModuleName so `FKey`
// symbols link. Slate (for FInputChord) was already a dep before this batch.
//
// Factory 2.13 (EnhancedInputAction) is deferred per plan — see Batch 15 in the spec. The
// EnhancedInput plugin lives under Engine/Plugins/Experimental and cannot be referenced
// unconditionally without gating the include/link, which is beyond this batch's scope.
// =========================================================================================

//------------------------------------------------------------------------------
// Factory 2.1 — InputAction → UK2Node_InputAction
//------------------------------------------------------------------------------
// NOTE: InputActionName is FName UPROPERTY (K2Node_InputAction.h line 25). Three behavior
// bitfields — bConsumeInput / bExecuteWhenPaused / bOverrideParentBinding — are exposed as
// UPROPERTY bitfields (lines 29/33/37). Direct assignment is safe; AllocateDefaultPins
// fires from Finalize and creates the Pressed/Released exec pins automatically.
UEdGraphNode* FBPNodeFactory::CreateInputAction(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString ActionName;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("action_name"), ActionName);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (ActionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputAction: missing action_name"));
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputAction> Creator(*Graph);
    UK2Node_InputAction* Node = Creator.CreateNode();
    Node->InputActionName = FName(*ActionName);
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.2 — InputAxisEvent → UK2Node_InputAxisEvent
//------------------------------------------------------------------------------
// NOTE: InputAxisName is FName UPROPERTY (K2Node_InputAxisEvent.h line 22). Shape matches
// InputAction: three behavior bitfields directly assigned before Finalize.
UEdGraphNode* FBPNodeFactory::CreateInputAxisEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString AxisName;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("axis_name"), AxisName);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (AxisName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputAxisEvent: missing axis_name"));
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputAxisEvent> Creator(*Graph);
    UK2Node_InputAxisEvent* Node = Creator.CreateNode();
    Node->InputAxisName = FName(*AxisName);
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.3 — InputKey → UK2Node_InputKey
//------------------------------------------------------------------------------
// NOTE: InputKey is FKey UPROPERTY (K2Node_InputKey.h line 28). FKey's FName-ctor does no
// validation; `IsValid()` only returns true if the key name resolves via EKeys registry.
// If caller supplies an unknown FKey name, we reject up front rather than spawning a node
// that will emit a ValidateNodeDuringCompilation error at compile time.
UEdGraphNode* FBPNodeFactory::CreateInputKey(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString FKeyStr;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("fkey_name"), FKeyStr);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (FKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputKey: missing fkey_name"));
        return nullptr;
    }

    const FName KeyFName(*FKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputKey: unrecognized FKey '%s'"), *FKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputKey> Creator(*Graph);
    UK2Node_InputKey* Node = Creator.CreateNode();
    Node->InputKey = Key;
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.4 — InputKeyEvent → UK2Node_InputKeyEvent
//------------------------------------------------------------------------------
// NOTE: Unlike UK2Node_InputKey which exposes FKey directly, UK2Node_InputKeyEvent wraps
// its key in an FInputChord struct (K2Node_InputKeyEvent.h line 21 — `FInputChord InputChord;`).
// The chord struct's Key field holds the FKey (InputChord.h line 23). Modifier bits on the
// chord default to false; callers can extend ConfigJson to set them later.
// InputKeyEvent is a UK2Node_Event subclass — its InputKeyEvent enum defaults to IE_Pressed.
UEdGraphNode* FBPNodeFactory::CreateInputKeyEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString FKeyStr;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("fkey_name"), FKeyStr);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (FKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputKeyEvent: missing fkey_name"));
        return nullptr;
    }
    const FName KeyFName(*FKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputKeyEvent: unrecognized FKey '%s'"), *FKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputKeyEvent> Creator(*Graph);
    UK2Node_InputKeyEvent* Node = Creator.CreateNode();
    Node->InputChord.Key = Key;
    Node->InputKeyEvent = IE_Pressed;  // Default: fire on press; caller may override elsewhere.
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.5 — InputTouch → UK2Node_InputTouch
//------------------------------------------------------------------------------
// NOTE: InputTouch has NO key/axis identifier — it fires on any touch. Only behavior flags
// are configurable (K2Node_InputTouch.h lines 25/29/33).
UEdGraphNode* FBPNodeFactory::CreateInputTouch(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);

    FGraphNodeCreator<UK2Node_InputTouch> Creator(*Graph);
    UK2Node_InputTouch* Node = Creator.CreateNode();
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.6 — InputTouchEvent → UK2Node_InputTouchEvent
//------------------------------------------------------------------------------
// NOTE: InputTouchEvent is the UK2Node_Event variant — exposes InputKeyEvent phase
// (EInputEvent) in addition to the behavior flags. Default IE_Pressed mirrors the in-editor
// node palette's default (K2Node_InputTouchEvent.h line 20).
UEdGraphNode* FBPNodeFactory::CreateInputTouchEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);

    FGraphNodeCreator<UK2Node_InputTouchEvent> Creator(*Graph);
    UK2Node_InputTouchEvent* Node = Creator.CreateNode();
    Node->InputKeyEvent = IE_Pressed;
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.7 — InputVectorAxisEvent → UK2Node_InputVectorAxisEvent
//------------------------------------------------------------------------------
// NOTE: [TBD resolved] Plan flagged uncertainty whether this node uses `InputAxisKeyName`
// FName or `AxisKey` FKey in 4.27. Verified via engine header: UK2Node_InputVectorAxisEvent
// inherits from UK2Node_InputAxisKeyEvent (K2Node_InputVectorAxisEvent.h line 14), and the
// parent exposes `FKey AxisKey` UPROPERTY (K2Node_InputAxisKeyEvent.h line 24). So this is
// an FKey-based factory, not FName. ConfigJson key `axis_name` here is used as the FKey
// string (e.g. "Gamepad_Special_Left" is an FKey, not an axis name — UE's naming is
// inconsistent here, but the engine key registry accepts it).
UEdGraphNode* FBPNodeFactory::CreateInputVectorAxisEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString AxisKeyStr;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    // Accept either "axis_name" (plan's schema — historic FName convention) or "fkey_name"
    // (what the underlying UPROPERTY actually is) for user ergonomics.
    if (!Cfg->TryGetStringField(TEXT("axis_name"), AxisKeyStr))
    {
        Cfg->TryGetStringField(TEXT("fkey_name"), AxisKeyStr);
    }
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (AxisKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputVectorAxisEvent: missing axis_name/fkey_name"));
        return nullptr;
    }
    const FName KeyFName(*AxisKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputVectorAxisEvent: unrecognized FKey '%s'"), *AxisKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputVectorAxisEvent> Creator(*Graph);
    UK2Node_InputVectorAxisEvent* Node = Creator.CreateNode();
    Node->AxisKey = Key;                  // inherited from UK2Node_InputAxisKeyEvent
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.8 — InputAxisKeyEvent → UK2Node_InputAxisKeyEvent
//------------------------------------------------------------------------------
// NOTE: AxisKey is FKey UPROPERTY on UK2Node_InputAxisKeyEvent (K2Node_InputAxisKeyEvent.h
// line 24). "MouseX" is a typical value — it's registered as an FKey in EKeys, not an FName
// axis mapping.
UEdGraphNode* FBPNodeFactory::CreateInputAxisKeyEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString FKeyStr;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("fkey_name"), FKeyStr);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (FKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputAxisKeyEvent: missing fkey_name"));
        return nullptr;
    }
    const FName KeyFName(*FKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputAxisKeyEvent: unrecognized FKey '%s'"), *FKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputAxisKeyEvent> Creator(*Graph);
    UK2Node_InputAxisKeyEvent* Node = Creator.CreateNode();
    Node->AxisKey = Key;
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.9 — GetInputAxisValue → UK2Node_GetInputAxisValue (pure getter)
//------------------------------------------------------------------------------
// NOTE: [TBD resolved] Plan worried this node may require `Initialize(FName)` instead of
// direct `InputAxisName=` assign. Engine header confirms both are available:
// `FName InputAxisName` is a UPROPERTY (K2Node_GetInputAxisValue.h line 22) and
// `void Initialize(const FName AxisName)` is exported (line 49). We use direct assign for
// parity with the action/axis event nodes, which matches the BPNodeFactory.cpp pattern for
// UK2Node_CallFunction — the base class — where SetFromFunction is preferred over
// Initialize*.  The getter node inherits UK2Node_CallFunction; AllocateDefaultPins will
// resolve the function reference + axis-name in a separate pass.
UEdGraphNode* FBPNodeFactory::CreateGetInputAxisValue(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString AxisName;
    bool bConsume = true, bExecPaused = false;
    Cfg->TryGetStringField(TEXT("axis_name"), AxisName);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    if (AxisName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateGetInputAxisValue: missing axis_name"));
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_GetInputAxisValue> Creator(*Graph);
    UK2Node_GetInputAxisValue* Node = Creator.CreateNode();
    // NOTE: UK2Node_GetInputAxisValue::Initialize(FName) is declared in the header but not
    // exported from the BlueprintGraph module (the class is MinimalAPI; verified via LNK2019).
    // We inline the Initialize body — direct-assign InputAxisName + SetFromFunction on
    // AActor::GetInputAxisValue. Engine source: K2Node_GetInputAxisValue.cpp lines 32-36.
    Node->InputAxisName = FName(*AxisName);
    Node->SetFromFunction(AActor::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(AActor, GetInputAxisValue)));
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.10 — GetInputAxisKeyValue → UK2Node_GetInputAxisKeyValue
//------------------------------------------------------------------------------
// NOTE: InputAxisKey is FKey UPROPERTY (K2Node_GetInputAxisKeyValue.h line 24). The node
// exposes Initialize(FKey) — used for parity with GetInputAxisValue factory above so the
// CallFunction reference gets populated correctly.
UEdGraphNode* FBPNodeFactory::CreateGetInputAxisKeyValue(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString FKeyStr;
    bool bConsume = true, bExecPaused = false;
    Cfg->TryGetStringField(TEXT("fkey_name"), FKeyStr);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    if (FKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateGetInputAxisKeyValue: missing fkey_name"));
        return nullptr;
    }
    const FName KeyFName(*FKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateGetInputAxisKeyValue: unrecognized FKey '%s'"), *FKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_GetInputAxisKeyValue> Creator(*Graph);
    UK2Node_GetInputAxisKeyValue* Node = Creator.CreateNode();
    // NOTE: Initialize(FKey) not exported (MinimalAPI class); inlined from
    // K2Node_GetInputAxisKeyValue.cpp lines 29-36.
    Node->InputAxisKey = Key;
    Node->SetFromFunction(AActor::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(AActor, GetInputAxisKeyValue)));
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.11 — GetInputVectorAxisValue → UK2Node_GetInputVectorAxisValue
//------------------------------------------------------------------------------
// NOTE: GetInputVectorAxisValue inherits from GetInputAxisKeyValue (K2Node_GetInputVectorAxisValue.h
// line 15), so InputAxisKey / Initialize(FKey) are both inherited. ConfigJson: `axis_name` is
// the FKey string (e.g. "Gamepad_Special_Left") — same ergonomic alias as Factory 2.7.
UEdGraphNode* FBPNodeFactory::CreateGetInputVectorAxisValue(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString AxisKeyStr;
    bool bConsume = true, bExecPaused = false;
    if (!Cfg->TryGetStringField(TEXT("axis_name"), AxisKeyStr))
    {
        Cfg->TryGetStringField(TEXT("fkey_name"), AxisKeyStr);
    }
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    if (AxisKeyStr.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateGetInputVectorAxisValue: missing axis_name/fkey_name"));
        return nullptr;
    }
    const FName KeyFName(*AxisKeyStr);
    FKey Key(KeyFName);
    if (!Key.IsValid())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateGetInputVectorAxisValue: unrecognized FKey '%s'"), *AxisKeyStr);
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_GetInputVectorAxisValue> Creator(*Graph);
    UK2Node_GetInputVectorAxisValue* Node = Creator.CreateNode();
    // NOTE: Initialize(FKey) not exported (MinimalAPI class); inlined from
    // K2Node_GetInputVectorAxisValue.cpp lines 17-21. Note this binds to
    // AActor::GetInputVectorAxisValue, not the 1D-axis variant.
    Node->InputAxisKey = Key;   // inherited from UK2Node_GetInputAxisKeyValue
    Node->SetFromFunction(AActor::StaticClass()->FindFunctionByName(
        GET_FUNCTION_NAME_CHECKED(AActor, GetInputVectorAxisValue)));
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.12 — InputActionEvent → UK2Node_InputActionEvent
//------------------------------------------------------------------------------
// NOTE: InputActionEvent is the event-phase-exposing variant (UK2Node_Event subclass). It has
// FName InputActionName (K2Node_InputActionEvent.h line 20) + TEnumAsByte<EInputEvent>
// InputKeyEvent (line 23) + the three behavior bitfields. We default phase to IE_Pressed;
// the "event" in the name refers to the event-binding nature, not the press/release split.
UEdGraphNode* FBPNodeFactory::CreateInputActionEvent(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
{
    if (!Graph) return nullptr;
    TSharedPtr<FJsonObject> Cfg;
    if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;

    FString ActionName;
    bool bConsume = true, bExecPaused = false, bOverrideParent = false;
    Cfg->TryGetStringField(TEXT("action_name"), ActionName);
    Cfg->TryGetBoolField(TEXT("consume_input"), bConsume);
    Cfg->TryGetBoolField(TEXT("execute_when_paused"), bExecPaused);
    Cfg->TryGetBoolField(TEXT("override_parent"), bOverrideParent);
    if (ActionName.IsEmpty())
    {
        UE_LOG(LogBlueprintMutator, Warning, TEXT("CreateInputActionEvent: missing action_name"));
        return nullptr;
    }

    FGraphNodeCreator<UK2Node_InputActionEvent> Creator(*Graph);
    UK2Node_InputActionEvent* Node = Creator.CreateNode();
    Node->InputActionName = FName(*ActionName);
    Node->InputKeyEvent = IE_Pressed;
    Node->bConsumeInput = bConsume;
    Node->bExecuteWhenPaused = bExecPaused;
    Node->bOverrideParentBinding = bOverrideParent;
    return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
}

//------------------------------------------------------------------------------
// Factory 2.13 (DEFERRED) — EnhancedInputAction → UK2Node_EnhancedInputAction
//------------------------------------------------------------------------------
// Scaffold left commented out intentionally. The EnhancedInput plugin lives at
// Engine/Plugins/Experimental/EnhancedInput, and its header is not visible to this
// build-config without adding the plugin as a dependency. Per plan's Batch 15, this will be
// implemented there with a module-available gate (IPluginManager::FindPlugin("EnhancedInput"))
// returning nullptr if not enabled. Leaving scaffold here for reviewers:
//
// #include "K2Node_EnhancedInputAction.h"   // requires EnhancedInput plugin dep
// UEdGraphNode* FBPNodeFactory::CreateEnhancedInputAction(UEdGraph* Graph, FVector2D Pos, const FString& ConfigJson)
// {
//     if (!Graph) return nullptr;
//     TSharedPtr<FJsonObject> Cfg;
//     if (!BPNodeFactoryInternal::ParseJsonConfig(ConfigJson, Cfg)) return nullptr;
//     FString InputActionPath;
//     Cfg->TryGetStringField(TEXT("input_action_path"), InputActionPath);
//     if (InputActionPath.IsEmpty()) return nullptr;
//     UInputAction* IA = LoadObject<UInputAction>(nullptr, *InputActionPath);
//     if (!IA) return nullptr;
//     FGraphNodeCreator<UK2Node_EnhancedInputAction> Creator(*Graph);
//     UK2Node_EnhancedInputAction* Node = Creator.CreateNode();
//     Node->InputAction = IA;
//     return BPNodeFactoryInternal::PositionAndFinalize(Creator, Node, Pos);
// }
