# Animation Blueprint Builder Design Spec

## Goal

Build Animation Blueprints from JSON. v1 target: recreate UE4's FirstPerson_AnimBP programmatically (state machine with 5 states, 7 transitions, slot node, event graph with variable updates).

## Architecture

Same pattern as BT Builder and Widget Builder: JSON spec -> C++ parser/validator/factory -> editor graph nodes. Uses the Widget Builder's three-method public API (Build/Rebuild/Validate) since AnimBPs require asset creation.

The AnimGraph module in UE4.27 exports most of its graph node classes (unlike BehaviorTreeEditor), so we can link directly without runtime FindObject workarounds.

Event graph construction is delegated to the existing BlueprintGraphBuilder -- no new C++ needed for K2 nodes.

## Module Dependencies

Add to `BlueprintGraphBuilder.Build.cs`. Note: `AnimGraph` and `AnimGraphRuntime` are NOT editor-only modules (they ship in runtime builds), but since BlueprintGraphBuilder itself is editor-only (links `UnrealEd` unconditionally), all deps go in the main `PrivateDependencyModuleNames` block for consistency:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    // ... existing deps ...
    "AnimGraph",
    "AnimGraphRuntime",
});

if (Target.bBuildEditor)
{
    // ... existing BehaviorTreeEditor, AIGraph ...
    PrivateDependencyModuleNames.Add("Persona");  // AnimBlueprint editor utilities
}
```

`AnimGraph` provides all `UAnimGraphNode_*` classes. `AnimGraphRuntime` provides runtime anim node structs (`FAnimNode_*`). `Persona` provides `UAnimBlueprintFactory` and AnimBlueprint editor utilities.

## File Structure

```
Public/
  AnimBlueprintBuilderLibrary.h            # Public API (3 UFUNCTIONs)

Private/AnimBlueprintBuilder/
  AnimBlueprintBuilderLibrary.cpp          # Dispatcher to FAnimBPBuilder
  ABPBuildSpec.h                           # Data structures for parsed JSON
  ABPBuilder.h                             # Orchestrator class
  ABPBuilder.cpp                           # 6-step build pipeline
  ABPAssetFactory.h                        # Asset creation (new AnimBP)
  ABPAssetFactory.cpp
  ABPJsonParser.h                          # JSON -> FAnimBPBuildSpec
  ABPJsonParser.cpp
  ABPValidator.h                           # Spec validation
  ABPValidator.cpp
  ABPNodeRegistry.h                        # AnimGraph node type registry
  ABPNodeRegistry.cpp                      # Type lookups, params, defaults
  ABPAnimGraphBuilder.h                    # Builds AnimGraph nodes + wiring
  ABPAnimGraphBuilder.cpp
  ABPStateMachineBuilder.h                 # Builds states, transitions, conditions
  ABPStateMachineBuilder.cpp
  ABPVariableBuilder.h                     # Adds variables to AnimInstance
  ABPVariableBuilder.cpp
```

15 files. The BT Builder has 13 (7 headers + 6 source). The increase comes from splitting AnimGraph building from StateMachine building (different schemas) and adding VariableBuilder + AssetFactory.

## Public API

```cpp
UCLASS()
class BLUEPRINTGRAPHBUILDER_API UAnimBlueprintBuilderLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Create new AnimBP at path and build from JSON
    UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
    static FString BuildAnimBlueprintFromJSON(
        const FString& PackagePath,
        const FString& AssetName,
        const FString& SkeletonPath,
        const FString& JsonString
    );

    // Rebuild existing AnimBP from JSON (clears and rebuilds graphs)
    UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
    static FString RebuildAnimBlueprintFromJSON(
        UAnimBlueprint* AnimBlueprint,
        const FString& JsonString
    );

    // Validate JSON without creating anything
    UFUNCTION(BlueprintCallable, CallInEditor, Category="BlueprintGraphBuilder")
    static FString ValidateAnimBlueprintJSON(
        const FString& JsonString
    );
};
```

## JSON Schema

### Top Level

```json
{
  "variables": [...],
  "anim_graph": {...},
  "state_machine": {...},
  "event_graph": {...}
}
```

The skeleton path is passed as a function parameter (not in JSON) because it's an asset reference the caller resolves. The event_graph section uses the existing BlueprintGraphBuilder JSON format and is forwarded to `UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON` on the AnimBP's event graph.

### Variables Section

```json
{
  "variables": [
    {"name": "IsMoving", "type": "bool", "default": false},
    {"name": "bIsInAir", "type": "bool", "default": false}
  ]
}
```

v1 supports `bool` only. Variables are added to the AnimBlueprint's generated class as blueprint-visible properties. They are referenced by name in transition conditions and event graph Set nodes.

### Anim Graph Section

```json
{
  "anim_graph": {
    "pipeline": [
      {"id": "sm1", "type": "StateMachine", "name": "LocomotionSM"},
      {"id": "slot1", "type": "Slot", "name": "Arms"}
    ]
  }
}
```

The pipeline is an ordered list of anim graph nodes. Wiring flows right-to-left in graph terms (toward the Root node): the last pipeline node's output feeds into `UAnimGraphNode_Root`'s "Result" input pin, and each earlier node's output feeds into the next node's input.

v1 node types:
- `StateMachine` -- creates `UAnimGraphNode_StateMachine`. References the state_machine section. Output pin: "Pose".
- `Slot` -- creates `UAnimGraphNode_Slot` with a slot name for montage overrides. Input pin: "Source", output pin: "Result".

The `UAnimGraphNode_Root` (output pose) is created automatically with the AnimGraph and has a single input pin "Result". Wiring for the FirstPerson example: `StateMachine["Pose"] -> Slot["Source"]`, `Slot["Result"] -> Root["Result"]`.

### State Machine Section

```json
{
  "state_machine": {
    "states": [
      {
        "id": "idle",
        "name": "FPP_Idle",
        "animation": "/Game/FirstPerson/Animations/FirstPerson_Idle",
        "looping": true,
        "is_entry": true
      },
      {
        "id": "run",
        "name": "FPP_Run",
        "animation": "/Game/FirstPerson/Animations/FirstPerson_Run",
        "looping": true
      },
      {
        "id": "jump_start",
        "name": "FPP_JumpStart",
        "animation": "/Game/FirstPerson/Animations/FirstPerson_JumpStart",
        "looping": false
      },
      {
        "id": "jump_loop",
        "name": "FPP_JumpLoop",
        "animation": "/Game/FirstPerson/Animations/FirstPerson_JumpLoop",
        "looping": true
      },
      {
        "id": "jump_end",
        "name": "FPP_JumpEnd",
        "animation": "/Game/FirstPerson/Animations/FirstPerson_JumpEnd",
        "looping": false
      }
    ],
    "transitions": [
      {
        "from": "idle",
        "to": "run",
        "blend_time": 0.2,
        "condition": {"type": "bool_variable", "variable": "IsMoving", "value": true}
      },
      {
        "from": "run",
        "to": "idle",
        "blend_time": 0.2,
        "condition": {"type": "bool_variable", "variable": "IsMoving", "value": false}
      },
      {
        "from": "idle",
        "to": "jump_start",
        "blend_time": 0.2,
        "condition": {"type": "bool_variable", "variable": "bIsInAir", "value": true}
      },
      {
        "from": "run",
        "to": "jump_start",
        "blend_time": 0.2,
        "condition": {"type": "bool_variable", "variable": "bIsInAir", "value": true}
      },
      {
        "from": "jump_start",
        "to": "jump_loop",
        "blend_time": 0.1,
        "condition": {"type": "time_remaining", "threshold": 0.1}
      },
      {
        "from": "jump_loop",
        "to": "jump_end",
        "blend_time": 0.1,
        "condition": {"type": "bool_variable", "variable": "bIsInAir", "value": false}
      },
      {
        "from": "jump_end",
        "to": "idle",
        "blend_time": 0.1,
        "condition": {"type": "time_remaining", "threshold": 0.1}
      }
    ]
  }
}
```

Each state contains a single `AnimGraphNode_SequencePlayer` referencing an `AnimSequence` asset by path. The `looping` flag sets the sequence player's loop mode.

Exactly one state must have `"is_entry": true`.

### Transition Conditions

v1 supports two condition types:

**bool_variable** -- reads a bool variable from the AnimInstance and compares to expected value.
```json
{"type": "bool_variable", "variable": "IsMoving", "value": true}
```
Builds a transition graph with: `UK2Node_VariableGet` (referencing the AnimInstance generated class member via self context) -> wire bool output to `UAnimGraphNode_TransitionResult`'s `bCanEnterTransition` pin. For `value: false`, insert a `UK2Node_CallFunction` (NOT) between the getter and the result. The transition graph uses `UAnimationTransitionGraph` with `UAnimationTransitionSchema` which restricts allowed node types.

**time_remaining** -- checks if the source state's animation has less than `threshold` seconds remaining.
```json
{"type": "time_remaining", "threshold": 0.1}
```
Uses `UAnimStateTransitionNode`'s built-in automatic rule instead of building a condition graph. Set `bAutomaticRuleBasedOnSequencePlayerInState = true` and `AutomaticRuleTriggerTime = threshold` on the transition node. This is much simpler than constructing a `UK2Node_TransitionRuleGetter` graph and is the standard UE4 approach for time-remaining transitions.

### Event Graph Section (Delegated)

```json
{
  "event_graph": {
    "nodes": [...],
    "connections": [...]
  }
}
```

This section is forwarded verbatim to the existing `BlueprintGraphBuilder` system. The AnimBP builder passes the AnimBP's event graph UEdGraph to the existing K2 node builder. No new C++ needed for event graph construction.

For the FirstPerson AnimBP, the event graph contains:
- BlueprintUpdateAnimation event
- TryGetPawnOwner -> IsValid check
- GetMovementComponent -> IsFalling -> Set bIsInAir
- GetVelocity -> VectorLength -> Greater(0) -> Set IsMoving

This is standard K2 node graph construction that BlueprintGraphBuilder already handles.

## Build Pipeline

```
FAnimBPBuilder::Build(PackagePath, AssetName, SkeletonPath, JsonString)
  1. PARSE:     JSON string -> FAnimBPBuildSpec
  2. VALIDATE:  Spec against registry (types, IDs, required params, animation paths)
  3. CREATE:    AnimBlueprint asset via FAnimBPAssetFactory (or skip for Rebuild)
  4. VARIABLES: Add bool variables to AnimInstance generated class
  5. BUILD:     AnimGraph nodes (StateMachine, Slot, OutputPose) + state machine internals
  6. COMPILE:   Compile the AnimBlueprint
```

Steps 1-2 are read-only. Step 3 creates the asset. Steps 4-5 mutate the asset. Step 6 finalizes. If any step fails, return the error string. Steps 3-5 are atomic: if step 5 fails, the partially-built asset remains (caller can delete it).

### Rebuild Path

Same as Build but skips step 3. Instead:
- Clears existing AnimGraph nodes
- Clears existing state machine graph
- Preserves existing variables (only adds missing ones)
- Rebuilds from JSON

**The two clear steps above were never implemented.**
`AnimBlueprintBuilder/ABPBuilder.cpp:147` says so in its own comment: v1 Rebuild
assumes a clean AnimBlueprint. Running it over an existing graph appends a
second state machine and a second copy of every state rather than converging,
and no rollback boundary can restore the previous contents of an asset that
already existed. This is why `puerts_anim_blueprint_build` is create-only and
refuses an existing asset, and why there is no `anim_blueprint_patch`. Adding
the clear pass, plus a content snapshot the rollback boundary can restore, is
what unblocks a patch command.

## Data Structures (ABPBuildSpec.h)

```cpp
struct FAnimBPVariableSpec
{
    FString Name;
    FString Type;       // "bool" for v1
    FString Default;    // "true" or "false"
};

struct FAnimBPTransitionConditionSpec
{
    FString Type;       // "bool_variable" or "time_remaining"
    FString Variable;   // for bool_variable
    FString Value;      // "true" or "false" for bool_variable
    float Threshold;    // for time_remaining
};

struct FAnimBPTransitionSpec
{
    FString From;       // state id
    FString To;         // state id
    float BlendTime;    // crossfade duration in seconds
    FAnimBPTransitionConditionSpec Condition;
};

struct FAnimBPStateSpec
{
    FString Id;
    FString Name;
    FString Animation;  // asset path to AnimSequence
    bool bLooping;
    bool bIsEntry;
};

struct FAnimBPAnimGraphNodeSpec
{
    FString Id;
    FString Type;       // "StateMachine" or "Slot"
    FString Name;       // display name or slot name
};

struct FAnimBPBuildSpec
{
    TArray<FAnimBPVariableSpec> Variables;
    TArray<FAnimBPAnimGraphNodeSpec> AnimGraphPipeline;
    TArray<FAnimBPStateSpec> States;
    TArray<FAnimBPTransitionSpec> Transitions;
    // event_graph JSON is stored as raw string, forwarded to BlueprintGraphBuilder
    FString EventGraphJson;
};

// All AnimBP building is editor-only (graph nodes are editor constructs).
// This entire subsystem compiles only in editor targets (BlueprintGraphBuilder links UnrealEd).
struct FAnimBPBuildContext
{
    UAnimBlueprint* AnimBlueprint = nullptr;
    USkeleton* Skeleton = nullptr;
    const FAnimBPNodeRegistry* Registry = nullptr;
    UAnimationStateMachineGraph* StateMachineGraph = nullptr; // from GetStateMachineGraph()
    TMap<FString, UAnimStateNode*> StateNodeMap;      // state id -> state graph node
    TMap<FString, UAnimGraphNode_Base*> AnimGraphNodeMap; // pipeline id -> anim graph node
};
```

## Asset Creation (FAnimBPAssetFactory)

Uses `UAnimBlueprintFactory` from the Persona module. Configuration steps:

```cpp
UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
Factory->TargetSkeleton = Skeleton;              // USkeleton* resolved from SkeletonPath
Factory->ParentClass = UAnimInstance::StaticClass();

FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
UObject* NewAsset = AssetTools.Get().CreateAsset(AssetName, PackagePath, UAnimBlueprint::StaticClass(), Factory);
UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(NewAsset);

// Factory initializes the AnimGraph and EventGraph automatically.
// Verify AnimBP->AnimGraphs has at least one graph after creation.
FAssetRegistryModule::AssetCreated(AnimBP);
```

The skeleton must be resolved first via `LoadObject<USkeleton>(nullptr, *SkeletonPath)`. If the skeleton path is invalid, return an error before attempting asset creation.

## UE4.27 AnimGraph Internals

### Graph Schema Hierarchy

An AnimBlueprint contains multiple UEdGraph instances, each with its own schema:

| Graph | Schema Class | Contains |
|-------|-------------|----------|
| AnimGraph | AnimationGraphSchema | StateMachine, Slot, Root nodes |
| StateMachineGraph | AnimationStateMachineSchema | State nodes, entry node, transition nodes |
| StateGraph (per state) | AnimationStateGraphSchema | SequencePlayer + ResultNode |
| TransitionGraph (per transition) | AnimationTransitionSchema | Condition logic + TransitionResult |
| EventGraph | EdGraphSchema_K2 | Standard K2 nodes |

### Key Classes (all in AnimGraph module, DLL-exported)

**AnimGraph layer:**
- `UAnimGraphNode_StateMachine` (extends `UAnimGraphNode_StateMachineBase`) -- container for a state machine. Access the state machine graph via `GetStateMachineGraph()` accessor (lazily creates it), do NOT assume a direct pointer member.
- `UAnimGraphNode_Slot` -- montage slot override node, has `SlotName` (FName). Input pin "Source", output pin "Result".
- `UAnimGraphNode_Root` -- output pose node (created automatically with AnimGraph). Input pin "Result".

**State machine layer:**
- `UAnimStateNode` -- a state, contains a `UAnimationStateGraph*` with the state's anim nodes
- `UAnimStateEntryNode` -- entry point, one per state machine, connects to the initial state
- `UAnimStateTransitionNode` -- transition, contains a `UAnimationTransitionGraph*` with condition logic
- `UAnimationStateMachineGraph` -- the graph that holds states, entry, and transitions

**Inside states:**
- `UAnimGraphNode_SequencePlayer` -- plays an AnimSequence, has `FAnimNode_SequencePlayer` with `Sequence` pointer and `bLoopAnimation`
- `UAnimGraphNode_StateResult` -- the state's output pose (one per state graph)

**Inside transitions:**
- `UAnimGraphNode_TransitionResult` -- the condition output (bool `bCanEnterTransition` pin)
- `UK2Node_VariableGet` -- reads AnimInstance variables via self context
- For time_remaining: use `UAnimStateTransitionNode::bAutomaticRuleBasedOnSequencePlayerInState` property instead of building a graph (no `UK2Node_TransitionRuleGetter` needed for v1)

### Node Creation Pattern

AnimGraph nodes are `UAnimGraphNode_Base` subclasses. Each wraps a runtime `FAnimNode_*` struct. Creating them:

```cpp
UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
SMNode->AllocateDefaultPins();
AnimGraph->AddNode(SMNode, false, false);
```

State nodes are created in the state machine graph:
```cpp
UAnimStateNode* StateNode = NewObject<UAnimStateNode>(StateMachineGraph);
StateNode->AllocateDefaultPins();
StateMachineGraph->AddNode(StateNode, false, false);
```

### Pin Wiring

AnimGraph nodes connect via pose pins (FPoseLink). The output pin of one node links to the input pin of the next:

```cpp
UEdGraphPin* OutputPin = FindPinByDirection(SourceNode, EGPD_Output);
UEdGraphPin* InputPin = FindPinByDirection(TargetNode, EGPD_Input);
OutputPin->MakeLinkTo(InputPin);
```

State machine connections work differently -- transitions connect states by linking pins between state nodes and transition nodes:

```
StateA.OutputPin -> Transition.InputPin
Transition.OutputPin -> StateB.InputPin
```

## Validation Rules

1. Exactly one state must have `is_entry: true`
2. All state IDs must be unique
3. All transition `from`/`to` must reference existing state IDs
4. All animation paths must be loadable `UAnimSequence*` assets
5. Variable names referenced in conditions must exist in the `variables` array
6. Pipeline must contain exactly one `StateMachine` node
7. `time_remaining` conditions are only valid on transitions from non-looping states
8. `blend_time` must be >= 0
9. Variable types must be "bool" (v1 restriction)
10. Pipeline node IDs must be unique

## v1 Limitations

- Single state machine only (no nested or multiple state machines)
- States contain only a single SequencePlayer (no blend spaces, aim offsets, or layered blends)
- Transition conditions: bool variable check or time remaining only (no complex expressions)
- Variables: bool only (no float, int, enum, or struct)
- No cached poses, no blend nodes, no IK
- No sub-state machines or conduits
- Event graph forwarded to BlueprintGraphBuilder (no AnimBP-specific event nodes)

## v2+ Expansion Path

- Float/int variables
- Blend spaces (1D and 2D)
- Layered blend per bone
- Multiple state machines
- Cached poses
- Aim offset
- Complex transition conditions (AND/OR, float comparisons)
- Conduits

## Implementation Pass Plan

| Pass | Scope | Output |
|------|-------|--------|
| 1 | Asset creation + skeleton binding + empty AnimGraph + compile | AnimBP asset with correct skeleton, opens in editor, compiles clean |
| 2 | Variables (bool) on AnimInstance + compile to verify | Variables visible in My Blueprint panel after compile |
| 3 | AnimGraph pipeline (StateMachine + Slot + Root wiring) | AnimGraph shows connected nodes in editor |
| 4 | State machine: states with SequencePlayer + entry node wiring | States visible with animations, entry node connected to is_entry state |
| 5 | Transitions with bool_variable conditions | Transitions connect states, condition graphs evaluate |
| 6 | TimeRemaining conditions (via bAutomaticRuleBasedOnSequencePlayerInState) + blend times | Jump chain works (Start->Loop->End) |
| 7 | Event graph delegation to BlueprintGraphBuilder + final compile | Full AnimBP: anim graph + event graph + compile |
| 8 | Integration test: recreate FirstPerson_AnimBP from JSON | End-to-end validation against reference AnimBP |

Each pass builds on the previous and produces a testable result. Passes 1-2 include compile steps because AnimInstance variables are only visible in the editor after compilation.
