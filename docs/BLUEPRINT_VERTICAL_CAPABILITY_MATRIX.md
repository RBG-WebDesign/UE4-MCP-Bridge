# Blueprint production vertical capability matrix

Repository-only audit. Unreal, UBT, install sync, live MCP, PIE, and editor state were not used.

## Goal and status language

The vertical is complete only when one structured feature request can be planned, generated,
organized, compiled, saved, reloaded, independently inspected, repaired, and proven without
unsupported behavior being reported as supported.

Status terms used below:

- **Implemented**: a canonical native command and read-back path exist in the audited source.
- **Partial**: useful native behavior exists, but the production contract is incomplete.
- **Missing**: no operation was found in the audited Blueprint surfaces.
- **Advertised but refused**: a public schema or description accepts or names the operation, but
  the default native route refuses it or cannot preserve it.
- **Legacy only**: the operation remains on the disabled HTTP lane and is not part of the default
  named-pipe production path.

Verification is separate from implementation. The generated metadata records
`puerts_blueprint_build`, `puerts_blueprint_graph_patch`, `puerts_blueprint_member_patch`, and
`puerts_graph_inspect` as live verified, with narrower caveats recorded below.

## Canonical command ownership

| Canonical command | Owns | Current proof | Notes |
|---|---|---|---|
| `puerts_blueprint_build` | Create or converge class, parent at creation, SCS components, variables, and one event graph | Live verified base builder | Authoring limited to `/Game/MCPGenerated/`; removal is implemented only for MCP-managed variables and components |
| `puerts_blueprint_member_patch` | Incremental variables, functions, interfaces, dispatchers, and component remove/rename | Live verified warm and cold | Eleven operation kinds plus `set_function`; one transaction, compiler messages, independent member read-back, measured rollback |
| `puerts_blueprint_graph_patch` | Incremental nodes, pin defaults, links, and positions in an existing graph | Live verified warm and cold | Seven operation kinds; selectors resolve before mutation; uses the builder node vocabulary |
| `puerts_graph_inspect` | Parent, components, variables, interfaces, functions, graph list, and one graph | Live verified base read | Read-only under `/Game` and `/Engine`; macro graphs can be selected by name; some node types and pin defaults are lossy or unmapped |

These four commands are the production spine. New orchestration should compose them rather than
create another editor mutation stack.

## Alias and duplicate route map

The default compatibility aliases use the native pipe. The same public names also remain in the
legacy HTTP modules, so each row below is a duplicate registration across modes, not a distinct
capability.

| Legacy public name or group | Canonical native capability | Compatibility result |
|---|---|---|
| `blueprint_build_from_json` | `puerts_blueprint_build` | Alias inspects first, normalizes the path, pins legacy `CallFunction` to `KismetSystemLibrary`, then builds |
| `blueprint_info` | `puerts_graph_inspect` | Direct alias, live result-shape proof still absent |
| `blueprint_inspect` | `puerts_graph_inspect` | Collection actions adapt; `macros`, `nodes`, `node_detail`, and `find_nodes` are refused |
| `blueprint_add_variable`, `blueprint_remove_variable`, `blueprint_set_variable_default` | `puerts_blueprint_member_patch` | One operation per alias |
| `blueprint_add_function`, `blueprint_remove_function` | `puerts_blueprint_member_patch` | One operation per alias; no alias for `set_function` |
| `blueprint_add_event_dispatcher`, `blueprint_remove_event_dispatcher` | `puerts_blueprint_member_patch` | One operation per alias |
| `blueprint_add_interface`, `blueprint_remove_interface` | `puerts_blueprint_member_patch` | One operation per alias |
| `blueprint_component_remove`, `blueprint_component_rename` | `puerts_blueprint_member_patch` | One operation per alias |
| `blueprint_node_add`, `blueprint_node_delete`, `blueprint_node_move`, `blueprint_pins_connect` | `puerts_blueprint_graph_patch` | One operation per alias |

Default-lane holes among the 18 legacy graph tools:

- `blueprint_node_set_enabled` has no compatibility alias and graph patch has no enabled-state op.
- `blueprint_pins_break` has no compatibility alias. Canonical graph patch can disconnect named
  pairs, but has no atomic break-all-links-on-one-pin operation.
- Filtered legacy inspection actions have no equivalent result adapter even though the canonical
  reader contains the underlying graph data.

## Capability matrix

### Planning and architecture

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Natural-language intent to structured plan | Missing | None in audited Blueprint tools | Add a server-side planner that emits data only, never editor calls from raw prose |
| Blueprint, C++, or hybrid selection | Missing | Policy exists only in the task specification | Return selected architecture and concrete reasons |
| Required asset and dependency plan | Missing | Callers hand-author command parameters | Plan stable asset paths, parent dependencies, assets, generated source, and operation order |
| Unsupported capability declaration | Partial | Schemas reject some bad requests | Planner must return an explicit refused list before any mutation |
| Deterministic operation ordering | Partial | Native batches preserve caller order | Planner must topologically order creation, members, graphs, compile, save, reload, and proof |

### Blueprint class and components

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Create Blueprint class | Implemented | `puerts_blueprint_build` | Keep path restriction explicit |
| Select parent class on create | Implemented | `parent_class` accepts reflected short or full paths | Independently prove all planned parent kinds |
| Change parent of existing Blueprint | Missing | Existing build loads the asset but does not reparent it | Add explicit `set_parent_class` with compatibility validation and rollback |
| Create components and hierarchy | Implemented | Build `components[]`, ordered `attach_to` | Read-back must compare class, parent, and requested template properties |
| Set component template properties and asset references | Implemented | Reflected `properties` object | Inspector contract must expose the requested property subset for independent proof |
| Remove and rename components | Implemented | Member patch `remove_component`, `rename_component` | Component rename is present; arbitrary reparent and reorder are missing |
| Reparent existing component | Missing | No member operation | Add desired parent and sibling-order operation with SCS read-back |
| Component metadata and variable flags | Missing | No public contract | Add only fields required by fixtures, with reflected read-back |

### Variables and types

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Add/remove variables | Implemented | Build or member patch | Downward convergence only removes MCP-managed builder variables |
| Set defaults and category | Implemented | Build and member patch | Container defaults are intentionally restricted and must remain explicit |
| Arrays and sets | Implemented | Builder `container`; member Type Descriptor | Fixture proof must include member read-back and graph use |
| Maps | Partial | Type Descriptor supports `container_type: Map` and `value_type`; builder schema does not | Standardize one public map descriptor and add contract tests |
| Struct and enum typed variables | Implemented for references | Builder and Type Descriptor accept reflected paths | Creation of user-defined struct and enum assets is not in the audited Blueprint vertical |
| Variable rename preserving references | Missing | Component rename exists, variable rename does not | Add `rename_variable` and verify every call site and default survives |
| Variable metadata beyond category | Missing | No editable, tooltip, private, expose-on-spawn, replication, or RepNotify contract | Add a narrow metadata object with exact supported keys and read-back |
| Change variable type | Intentionally refused | Existing name with a different type is rejected | Keep refusal unless a safe reference-preserving migration is designed |

### Functions, macros, dispatchers, and interfaces

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Create/remove functions | Implemented | Member patch | None for basic lifecycle |
| Inputs, outputs, locals | Implemented | `set_function` authoritative lists | Local rename is refused; add explicit rename only if a fixture needs it |
| Function pure, const, access, category, tooltip | Implemented | `set_function.metadata` | Preserve call sites on parameter rename and prove independently |
| Author function body | Implemented | Graph patch targets the function graph by name | No one-call desired-state function contract or body-level downward reconciliation |
| Rename function preserving call sites | Missing | Parameter rename exists, graph/function rename does not | Add explicit rename with local and external call-site impact report |
| Create/remove/rename macros | Missing | Inspector lists macro graphs only | Add macro lifecycle and tunnel signature operations before advertising macro support |
| Author existing macro graph body | Partial | Graph patch can target an existing macro graph | `MacroInstance` is excluded from the canonical node vocabulary |
| Add/remove event dispatchers | Implemented | Member patch | Live fix creates both multicast property and signature graph |
| Bind, assign, call, clear dispatcher nodes | Advertised but refused | Internal registry has delegate factories | Canonical builder vocabulary excludes them and alias refuses three legacy forms |
| Add/remove interfaces | Implemented | Member patch | Interface asset creation is outside audited surfaces |
| Implement interface function body | Partial | Engine-generated function graph can be targeted | No explicit override operation or interface-message node contract |
| Blueprint overrides | Missing as first-class contract | Event nodes can reference a parent event | Add override selection, signature validation, and inspector proof |

### Graphs and node vocabulary

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Replace EventGraph from desired state | Implemented | `puerts_blueprint_build.graph` | Whole-graph replacement only |
| Incrementally patch any existing named graph | Implemented | Graph patch | Existing graph must already exist |
| Construction Script authoring | Partial | Inspector and patch can address an existing graph by name | No first-class Construction Script contract or fixture proof |
| Create/remove/rename arbitrary graphs | Missing | Functions are the only graph lifecycle surface | Required for macros and explicit graph cleanup |
| Add/remove/move nodes | Implemented | Graph patch | Add is limited to canonical vocabulary |
| Set pin defaults, connect, disconnect | Implemented | Graph patch | Break-all-pin is missing; some struct defaults inspect lossily |
| Update node routing target | Partial | Patch updates input defaults; low-level mutator can retarget CallFunction | No canonical operation exposes call-target retargeting |
| Enable/disable nodes | Legacy only | Low-level mutator and HTTP tool exist | No canonical graph patch operation or alias |
| Comments and reroute nodes | Implemented | `Comment` and `Knot`, stable x/y | No automatic region or crossing planner |
| Event, function call, branch, sequence, cast, latent delay, struct operations | Implemented | Canonical 27-type vocabulary plus `CallFunction` | Specialized nodes still need explicit factories or safe function-call equivalents |
| Loops | Missing as canonical vocabulary | Internal macros or function calls may exist | Add gated loop node forms with wildcard pin proof |
| Timers | Partial | Callable UE functions may be reachable | Delegate/function-name variants need an explicit verified recipe |
| Container operations | Partial | Callable libraries may be reachable | Wildcard array/set/map typing and read-back are not a declared contract |
| Interface message calls | Missing as canonical vocabulary | Generic function calls are not equivalent to message nodes | Add a UE4.27 interface-message node factory and proof |

Canonical builder vocabulary currently exposed by TypeScript is exactly:

`BeginPlay`, `Tick`, `ActorBeginOverlap`, `ActorEndOverlap`, `PrintString`,
`CallFunction`, `Operator`, `Delay`, `Branch`, `Sequence`, `Comment`, `Event`,
`CustomEvent`, `VariableGet`, `VariableSet`, `Cast`, `Select`, `Knot`, `MakeStruct`,
`BreakStruct`, `FormatText`, `SpawnActor`, `SwitchInt`, `SwitchString`, `MultiGate`,
`DoOnceMultiInput`, and `InputKey`.

The internal `FBPNodeRegistry` contains additional factories, including widget, macro, switch,
input, and delegate nodes. They are not production capabilities until the canonical builder gate,
TypeScript schema, inspector mapping, contract tests, and live fixture all agree.

### Organization, compile, persistence, inspection, and repair

| Capability | Status | Existing path | Gap to production |
|---|---|---|---|
| Explicit node positions | Implemented | Build x/y and graph patch move | Planner absent |
| Left-to-right layout and stable spacing | Missing | Callers choose coordinates | Add deterministic layout planning before mutation |
| Branch separation, comments, reroutes, crossing reduction | Missing as automation | Primitives exist | Add layout rules and a stable output plan |
| Complexity limits and extraction recommendations | Missing | None | Add warnings at 25 functional nodes, branch depth over 3, repetition, large loops, and excessive crossings |
| Compile with errors and warnings | Implemented | All canonical mutations compile by default; member patch returns messages | Add an explicit read-only compile-status contract or retain a documented converged member patch probe |
| Save only after clean compile and verification | Implemented | Build and patch gates | Production orchestration must never set `compile`, `save`, or `verify` false |
| Independent graph/member read-back | Implemented | `puerts_graph_inspect` | Component property proof and unmapped nodes need stricter coverage |
| Save confirmation | Partial | Mutation responses report saved/changed assets | No independent package-save verifier in audited surfaces |
| Reload and post-reload comparison | Missing | No Blueprint reload command in audited surfaces | Add package reload with pre/post hashes and dirty-state refusal |
| Idempotent second application | Implemented for canonical build and patches | Hashes, convergence plans, unchanged operations | Must be checked for every fixture |
| Repair from observed diff | Partial | Planner can manually inspect then patch | No machine-readable desired-versus-actual diff or repair plan |
| Failure atomicity and rollback | Implemented for canonical build and patches | Transaction plus asset/default snapshots and independent hash read-back | Every new destructive operation must join the same measured rollback contract |

## Advertised-but-refused and unsupported-green hazards

1. `puerts_blueprint_build.remove_unlisted` publicly names `functions`, `macros`,
   `graph_nodes`, and `interfaces`, but the native command accepts true only for `variables` and
   `components`. The schema should expose only implemented scopes or describe the refusal in a
   machine-readable capability response.
2. Legacy `blueprint_inspect` advertises ten actions. The native alias refuses `macros`, `nodes`,
   `node_detail`, and `find_nodes` even though the canonical full reader can return related data.
3. Legacy `blueprint_node_add` describes factories that its native alias refuses:
   `CreateWidget`, `MacroInstance`, `SwitchEnum`, `SwitchName`, `InputAction`,
   `InputAxisEvent`, `AddDelegate`, `RemoveDelegate`, and `CallDelegate`.
4. The C++ node registry contains more factories than the canonical builder's 27-word allowlist.
   Registry presence alone must never be counted as supported.
5. The low-level `BuildBlueprintFromJSON` logs and skips unsupported nodes. Only the canonical
   native command validates and turns node or connection shortfalls into failure. Production
   orchestration must never call the low-level function directly.
6. `compile:false`, `save:false`, and `verify:false` are useful diagnostic switches but cannot be
   accepted in a production-vertical execution plan.
7. `blueprint_create` and the other direct tools in `blueprints.ts` and `blueprint-graph.ts` use
   the legacy HTTP client. Their presence in source does not make them default-lane capabilities.

## Required desired-state contracts

### 1. Planner request

```json
{
  "schema_version": 1,
  "feature_id": "locked_door",
  "intent": "Build a locked door that opens after the player collects a key.",
  "constraints": {
    "ue_version": "4.27",
    "authoring_root": "/Game/MCPGenerated",
    "networked": false,
    "designer_editable": true
  },
  "existing_assets": [],
  "policy": {
    "max_functional_nodes": 25,
    "max_branch_depth": 3,
    "horizontal_gap": 320,
    "vertical_gap": 180
  }
}
```

### 2. Planner response

```json
{
  "schema_version": 1,
  "feature_id": "locked_door",
  "architecture": {
    "selected": "hybrid",
    "reasons": ["state and authority belong in tested C++", "assets and event hooks remain editable in Blueprint"]
  },
  "cpp": { "classes": [], "files": [] },
  "blueprints": [],
  "dependency_order": [],
  "layout_plans": [],
  "verification_plan": [],
  "unsupported": [],
  "warnings": []
}
```

`selected` is exactly `blueprint`, `cpp`, or `hybrid`. Every generated file, asset, and operation
must be named before execution. A non-empty `unsupported` array makes the plan non-executable.

### 3. Blueprint desired state

Each `blueprints[]` entry must be declarative:

```json
{
  "asset_path": "/Game/MCPGenerated/BP_LockedDoor",
  "parent_class": "/Script/Game.LockedDoorBase",
  "components": [],
  "variables": [],
  "functions": [],
  "macros": [],
  "dispatchers": [],
  "interfaces": [],
  "graphs": [
    { "name": "EventGraph", "kind": "event", "nodes": [], "connections": [], "layout": {} }
  ],
  "convergence": { "remove_unlisted": [] }
}
```

The executor may lower this into existing build, member patch, and graph patch calls. The public
desired state must not expose three overlapping mutation grammars to the planner.

### 4. Execution response

```json
{
  "success": false,
  "phase": "verify",
  "feature_id": "locked_door",
  "plan_hash_sha1": "",
  "assets": [],
  "compile": { "status": "Error", "warnings": [], "errors": [] },
  "save": { "attempted": false, "succeeded": false },
  "reload": { "attempted": false, "succeeded": false },
  "verification": { "passed": false, "mismatches": [] },
  "converged": false,
  "rollback": { "attempted": true, "succeeded": true, "mismatches": [] },
  "errors": []
}
```

Every error must include `code`, `asset_path`, `graph`, `operation_index`, `node_selector`,
`pin`, `message`, `closest_matches`, and `recoverable_with` when applicable. No response may set
`success:true` unless compile, save, reload, read-back, and convergence checks required by the
plan have passed.

## Read-back and rollback contract

For every asset, proof must compare requested and observed values for:

1. Asset identity and exact parent class.
2. Component class, hierarchy, selected properties, and asset references.
3. Variable type, container, category, metadata, and effective CDO default.
4. Function signature, locals, flags, category, tooltip, and graph body.
5. Macro signature and graph body.
6. Dispatchers and both their multicast property and signature graph.
7. Interfaces and generated implementation graphs.
8. Graph kind, mapped node type, routing fields, visible pin defaults, links, comments, enabled
   state, and position.
9. Compiler warnings and errors.
10. Package dirty state before and after read, save result, reload result, and post-reload hashes.

Failure behavior:

- Validate the complete batch and every reference before the first mutation.
- Refuse missing, ambiguous, inherited, native, unsafe, or unsupported targets by exact name.
- Apply one asset batch inside one outer transaction and one asset snapshot boundary.
- Do not save until compilation and independent read-back pass.
- On failure, revert the transaction, restore non-transactional CDO defaults and package state,
  reload when required, then inspect again.
- Report `rollback_succeeded` only from the post-rollback read, never from an attempted undo.
- New asset creation failure must remove the created package and prove it no longer resolves.
- Existing asset failure must preserve the pre-operation member and graph hashes and file bytes.

## C++ versus Blueprint policy

Choose **C++** for reusable runtime systems, complex algorithms, performance-sensitive loops,
replication and authority, large data transforms, shared base classes, strict state machines, and
logic that needs normal source tests.

Choose **Blueprint** for components, asset references, designer settings, per-instance defaults,
event wiring, animation and audio hooks, level-specific behavior, visual extension points, and
small orchestration graphs.

Choose **hybrid** when both lists apply. Prefer a tested C++ base class with a clean Blueprint
child. The planner must not choose C++ merely because source generation is easier. It must name
which responsibility belongs to each layer and why.

Escalate a graph toward functions, macros, or C++ when it exceeds 25 functional nodes, exceeds
three nested branch levels, repeats a node pattern, performs a large data-processing loop, or
creates excessive execution-wire crossings.

## Exact eight acceptance fixture specifications

Each fixture uses a unique fresh path, runs plan-only first, applies once, independently inspects,
compiles, saves, reloads, compares again, applies the identical plan a second time, and confirms no
duplicate members or nodes and no new file bytes. Runtime PIE is a separate explicitly authorized
proof step and is not required for the repository gate.

### F1 Basic Actor

- Asset: `/Game/MCPGenerated/BPV_F1_BasicActor`, parent `Actor`.
- Components: root `SceneComponent`; child `StaticMeshComponent` with cube mesh and stable relative
  transform.
- Variables: bool, float, string, vector, object asset reference, array, set, and map, organized
  into two categories with effective defaults.
- Functions: pure const `ComputeScore(float Base) -> float Result` with local `Multiplier` and a
  small authored body.
- EventGraph: BeginPlay calls `ComputeScore`, writes a variable, and prints a marker inside a
  comment region.
- Proves: class creation, hierarchy, properties, containers, defaults, function metadata/body,
  positions, connections, compile, save, reload, and idempotency.

### F2 Interaction System

- Assets: `/Game/MCPGenerated/BPI_BPV_Interactable` and
  `/Game/MCPGenerated/BPV_F2_InteractableActor`.
- Actor implements `BPI_BPV_Interactable`, owns a box collision component and an interaction
  prompt component reference.
- Interface function `Interact(Actor Instigator)` has an implementation graph.
- Dispatcher `OnInteracted(Actor Instigator)` is bound or called by canonical delegate nodes.
- EventGraph receives overlap, validates the actor, sends an interface message, and invokes the
  dispatcher with separated branch layout.
- Proves: interface asset integration, implementation graph, message calls, dispatcher property and
  signature, delegate nodes, component references, and independent read-back.

### F3 C++ Hybrid Actor

- Source: `ABPVHybridSwitchBase`, generated in the fixture module with a bool state, one
  `BlueprintCallable` command, one `BlueprintPure` query, and one `BlueprintImplementableEvent`.
- Asset: `/Game/MCPGenerated/BPV_F3_HybridSwitch`, parent
  `/Script/<FixtureModule>.BPVHybridSwitchBase`.
- Blueprint child adds mesh and audio components, assigns engine assets, sets designer defaults,
  and implements the visual extension event with no algorithmic graph.
- Proves: architecture decision, source manifest, class dependency ordering, parent resolution,
  native-call nodes, extension event override, compile boundaries, and clean child organization.

### F4 Stateful Door

- Asset: `/Game/MCPGenerated/BPV_F4_StatefulDoor`, parent `Actor` or the audited hybrid base if the
  planner selects hybrid.
- Components: frame, movable door panel, trigger volume, and optional audio component.
- Variables: `bLocked`, `bOpen`, `OpenOffset`, `OpenDuration`, and key identifier, all designer
  organized.
- Functions: `CanOpen`, `OpenDoor`, and `CloseDoor`; dispatcher `OnDoorStateChanged(bool bOpen)`.
- Timing uses a verified UE4.27 timer recipe unless timeline lifecycle is added explicitly.
- EventGraph remains orchestration only and visually separates locked, opening, and closing paths.
- Proves: state logic, timers or an explicit timeline capability, dispatch, designer settings,
  latent/timed flow, and complexity policy.

### F5 Inventory Component

- Asset: `/Game/MCPGenerated/BPV_F5_InventoryComponent`, parent `ActorComponent`.
- Types: item struct reference and enum reference; item array, tag set, and count map.
- Functions: `AddItem`, `RemoveItem`, `ContainsItem`, and pure `GetItemCount`; at least one local
  variable and one loop over the item array.
- Dispatcher: `OnInventoryChanged` called after a successful mutation.
- Graph layout extracts repeated validation and flags the data loop for C++ if policy thresholds
  are exceeded.
- Proves: non-Actor parent, containers, struct operations, loop factory, function bodies, locals,
  dispatcher calls, and wildcard pin typing.

### F6 Inheritance Test

- Parent asset: `/Game/MCPGenerated/BPV_F6_BaseInteractable`, parent `Actor`, with components,
  inherited defaults, virtual/overridable function `Activate`, and extension event.
- Child asset: `/Game/MCPGenerated/BPV_F6_ChildInteractable`, parent generated class of the parent.
- Child overrides `Activate`, calls parent, changes allowed defaults, adds one component, and leaves
  inherited members intact.
- A repair pass renames one function parameter using `rename_from` and proves child call-site links
  survive.
- Proves: Blueprint parent dependency, inherited read-back, overrides, parent calls, default
  inheritance, and signature repair.

### F7 Graph Cleanup Test

- Asset: `/Game/MCPGenerated/BPV_F7_GraphCleanup` seeded with a compiling but deliberately poor
  graph containing crossing execution wires, crowded data nodes, repeated logic, and no comments.
- Record the pre-cleanup structural behavior signature separately from positions and comments.
- Apply deterministic left-to-right layout, vertical branch lanes, fixed gaps, comment regions,
  and only necessary knot nodes. Extract repeated logic only when behavior equivalence can be
  proven.
- Proves: graph diff, move, comment, knot, optional extraction, stable repeated layout, unchanged
  functional signature, and complexity warnings.

### F8 Failure and Rollback Test

- Asset: `/Game/MCPGenerated/BPV_F8_Rollback`, created and saved in a valid baseline state.
- Capture member hash, graph hash, compile status, package dirty state, and file SHA before failure.
- Submit one atomic batch containing a valid early change and a later invalid pin connection,
  unsupported node, or ambiguous selector.
- Require `success:false`, exact failing asset/graph/node/pin, no save, rollback attempted, rollback
  independently verified, identical hashes and file SHA, and no new package for a failed create.
- Reload and inspect once more, then confirm the original valid plan still converges.
- Proves: classify-before-mutate, structured refusal, transaction and snapshot restore, package
  cleanup, post-rollback read-back, and recovery guidance.

### Integrated capstone: first-person physics pickup and throw

Run this only after F1 through F8 pass. It is the final feature-level acceptance, not a ninth
primitive fixture.

- Assets: a first-person `Character` or `Pawn` and an interactable physics actor under
  `/Game/MCPGenerated/BPV_Capstone_*`.
- Character components: camera and `PhysicsHandleComponent`; the physics actor owns a movable,
  simulated primitive component and implements the interaction interface proven by F2.
- Variables: grab distance, hold distance, throw impulse, held component/object references,
  interaction state, ignored actor or object collections, and designer categories/defaults.
- Functions: camera trace, candidate validation, pickup, update held target, release, and throw.
  Large trace filtering or reusable physics policy belongs in C++ when the planner selects hybrid.
- Event flow: verified UE4.27 input events request pickup/release/throw, interface calls cross the
  actor boundary, dispatcher events report grabbed/released/thrown state, and Tick or a timer moves
  the handle target while an object is held.
- Graph requirements: explicit branch and validity flow, component and asset references, stable
  left-to-right layout, comment regions, bounded crossings, and no large algorithm in EventGraph.
- Static proof: deterministic plan generation, supported node inventory, schema validation, stable
  operation order, and no unsupported factory presented as available.
- Later live proof: compile, save, reload, full independent read-back, identical second execution,
  and the F8 rollback invariant. Runtime pickup, hold, release, and throw behavior requires explicit
  PIE authorization and occurs only after the repository gate.

## Required contract changes before implementation can be called complete

1. Add the structured planner response and architecture decision policy.
2. Define one public Blueprint desired-state schema that lowers into the existing three canonical
   mutation commands.
3. Add parent change, variable rename/metadata, macro lifecycle, graph lifecycle, override,
   interface-message, loop, delegate, node-enabled, and break-pin operations or refuse them in the
   capability manifest.
4. Reconcile the internal node registry with the canonical builder allowlist. Do not count a
   factory until schema, inspector, tests, and proof agree.
5. Add deterministic layout planning and complexity warnings. Reuse `Comment`, `Knot`, and node
   position primitives.
6. Add explicit save confirmation, package reload, post-reload inspection, and desired-versus-
   observed mismatch output.
7. Extend independent component property read-back for the property subset each plan requests.
8. Make production execution hardwire compile, save, and verify on.
9. Generate fixture plans editor-free, then run all eight in one later live campaign.
10. Keep unsupported operations out of green inventory states until the full read-back contract
    and live proof pass.

## Blocking questions

1. Is creation of Blueprint Interface, User Defined Struct, and User Defined Enum assets inside
   this vertical, or may the planner require pre-existing type assets? F2 and F5 need one answer.
2. Must existing Blueprints outside `/Game/MCPGenerated/` be mutable, or is that root an intentional
   product boundary? The current native writers refuse every other path.
3. Does the C++ hybrid fixture use the repository's existing C++ generation path, or should this
   vertical define only the handoff contract to it? That path was outside this targeted audit.
4. Is a verified timer recipe sufficient for F4, or is native Timeline lifecycle a required
   Blueprint capability?
5. Should macros be mandatory production output, or a supported option used only after function
   extraction is inappropriate? Macro lifecycle is currently absent.

## Files inspected

- `mcp-server/src/tools/puerts.ts`
- `mcp-server/src/tools/blueprints.ts`
- `mcp-server/src/tools/blueprint-graph.ts`
- `mcp-server/src/tools/compat.ts`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeBlueprint.cpp`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeBlueprintMember.cpp`
- `Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintGraphBuilderLibrary.h`
- `Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintMutatorLibrary.h`
- `Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintInspectorLibrary.h`
- Targeted Blueprint mutator and inspector implementation files under
  `Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Private/BlueprintMutator/` and
  `Private/BlueprintInspector/`
- `mcp-server/tests/puerts-tools.test.ts`
- `mcp-server/tests/blueprint-graph-tools.test.ts`
- `mcp-server/tests/integration/blueprint-integration.test.ts`
- `docs/TOOL_CAPABILITY_METADATA.json`
- `docs/TOOL_INVENTORY.json`
- `docs/CAPABILITY_FINDINGS.md`
- `Scripts/feature-blueprint-state.mjs`
- `Scripts/generate-tool-inventory.mjs`
- User-supplied Blueprint vertical specification and fixture list
