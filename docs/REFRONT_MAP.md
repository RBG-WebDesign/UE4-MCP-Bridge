# REFRONT map

The 54 legacy tools whose implementation already exists as compiled C++ and
whose migration is therefore a matter of putting a native command in front of a
library, not of writing the capability.

This is the readiness audit: per builder, whether it is transactional, whether it
rolls back, whether an independent inspector exists, whether it converges, and
therefore whether its tools can ship as writes or only as reads.
`docs/REFRONT_PLAN.md` is the other cut of the same 54: the per-tool migration
table, naming each native command and whether it forwards or needs a wrapper.
Read this to decide what is safe, the plan to decide what to write.

Derived from `docs/TOOL_INVENTORY.json` by filtering `migration_action ==
"REFRONT"`, not from prose. The count is a fact of that file:

```bash
node -e "const j=require('./docs/TOOL_INVENTORY.json');
  console.log(j.tools.filter(t=>t.migration_action==='REFRONT').length)"
# 54
```

`docs/PROJECT_FINISH_SCOREBOARD.json` claims 54 and the inventory agrees. Of the
208 tools in the inventory the rest split 75 KEEP, 39 PORT, 29 ALIAS, 10 MERGE,
1 RETIRE.

## Overlap with docs/REFRONT_PLAN.md

`docs/REFRONT_PLAN.md` covers the same 54 tools and was written in the same
session. Both read the builders directly and both land on the same central
finding about `BPMutatorHelpers.cpp`. They are not in conflict, and one of them
should probably be deleted; that is an integration call, not this document's.

Where they differ:

- This map keeps all 54 in the REFRONT bucket, because that is what
  `migration_action` says in the inventory today. It reports readiness, not
  reclassification.
- `REFRONT_PLAN.md` argues that 6 of the 54 are really MERGE (five graph node
  and pin tools that are already operations inside `blueprint_graph_patch`, and
  the four widget preset tools over the already-native `widget_build`), leaving
  38 as genuine REFRONT work. That argument is sound and this map does not
  contradict it: a tool that is MERGE is a tool that needs no new front at all,
  which is a stronger version of "ready".
- Neither document edits `migration_action`. Changing it retires public tool
  names, which is a compatibility decision.

## What the four columns mean

A REFRONT is only cheap when the library underneath already behaves. Four
properties decide whether the new native command may be annotated MUTATING or
has to ship READ-ONLY first:

- **Transactional.** One UE4 transaction around the whole operation.
- **Failure-atomic.** A failed operation leaves the asset as it was. A
  transaction that is opened and never cancelled is not this.
- **Independently inspected.** A separate read-only function returns the same
  data shape, so the result can be checked without trusting the writer's own
  report.
- **Convergent.** Rerunning changes nothing and says so.

Where a library has all four, the command in front of it can be MUTATING on day
one. Where it does not, the read half ships first and the write half waits for
the missing property. That is the rule this map applies; it is not a
per-tool judgement call.

## The defect that shapes most of this map

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Private/BlueprintMutator/BPMutatorHelpers.cpp:27-37`
is the shared transaction wrapper for all 19 `UBlueprintMutatorLibrary` entry
points. It opens an `FScopedTransaction`, runs the body, and on a false return
logs and returns false **without cancelling**. The scoped transaction then
destructs normally and commits whatever the body already wrote.

So `UBlueprintMutatorLibrary` is transactional and is not failure-atomic. It is
one fix at one site rather than nineteen, and until it lands, every command in
front of that library has to own the rollback boundary itself. That is exactly
what `puerts_blueprint_member_patch` does: it refuses to run without an active
transaction, cancels it on any failure, and then decides whether the rollback
worked by re-reading the members rather than by trusting the undo.

Repo-wide: only three C++ call sites outside `MCPBridgePuerTS` open a
transaction at all, and there is not one `CancelTransaction` among them.
AGENTS.md says "Every tool that modifies editor state is wrapped in a UE4
transaction". For this C++ layer that is not true today.

## Grouping

Five groups, ordered so a wave can take one and finish it. Group 1 is the only
one with a proven native front today.

## Group 1: Blueprint editing (18) - the only group with a proven native front

### BlueprintMutatorLibrary (17)

- Transactional: yes, `BPMutatorHelpers.cpp:27`
- Rollback on failure: **no**, see defect below
- Independent inspector: yes, `UBlueprintInspectorLibrary`
- Convergent: refuses duplicates, no `bUnchanged`
- **Ships as: MUTATING**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `blueprint_add_event_dispatcher` | AddEventDispatcher | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_add_function` | AddFunction | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_add_interface` | AddInterfaceImplementation | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_add_variable` | AddVariable | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_component_remove` | RemoveSCSNode | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_component_rename` | RenameSCSNode | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_node_add` | AddNode | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_node_delete` | DeleteNode | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_node_move` | MoveNode | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_node_set_enabled` | SetNodeEnabled | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_pins_break` | BreakPinLinks | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_pins_connect` | ConnectPins | `puerts_blueprint_graph_patch` (exists, live_verified) | no | MUTATING |
| `blueprint_remove_event_dispatcher` | RemoveEventDispatcher | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_remove_function` | RemoveFunction | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_remove_interface` | RemoveInterfaceImplementation | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_remove_variable` | RemoveVariable | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |
| `blueprint_set_variable_default` | SetVariableDefault | `puerts_blueprint_member_patch` (this lane, implemented) | no | MUTATING |

### BlueprintGraphBuilderLibrary (1)

- Transactional: no, deliberately: the caller owns the boundary
- Rollback on failure: hand-rolled abort, `:1732`
- Independent inspector: yes, `DescribeBlueprintGraphJSON`
- Convergent: yes, `bUnchanged` + `bPlanOnly`
- **Ships as: MUTATING**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `blueprint_compile` | CompileAndReport | compile step inside both patch commands | no | MUTATING |


## Group 2: PIE agent (10) - read half first, and every write is user-gated

### MCPBridgePIEAgent (10)

- Transactional: n/a, mutates a PIE world not an asset
- Rollback on failure: no
- Independent inspector: yes, `Observe` / `GetOperationStatus`
- Convergent: no, imperative API
- **Ships as: READ-ONLY**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `gameplay_telemetry_snapshot` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | yes | READ-ONLY |
| `pie_agent_expect` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | yes | READ-ONLY |
| `pie_agent_look_at` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_move_to` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_observe` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | yes | READ-ONLY |
| `pie_agent_press` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_record_start` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_record_stop` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_replay` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | no | READ-ONLY |
| `pie_agent_status` | (not recorded in the inventory) | `puerts_pie_agent` (does not exist) | yes | READ-ONLY |


## Group 3: Asset mutators with real damage potential (9) - READ-ONLY first, no exceptions

### AnimPoseLibrary (5)

- Transactional: yes, `:172`
- Rollback on failure: **no**, and the source says undo is not trustworthy here
- Independent inspector: yes, dry-run twin `ValidateReanchorPlan`
- Convergent: **no**, applies a delta unconditionally
- **Ships as: READ-ONLY**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `anim_batch_reanchor` | (not recorded in the inventory) | `puerts_anim_pose` (does not exist) | no | READ-ONLY |
| `anim_pose_delta` | (not recorded in the inventory) | `puerts_anim_pose` (does not exist) | yes | READ-ONLY |
| `anim_pose_snapshot` | (not recorded in the inventory) | `puerts_anim_pose` (does not exist) | yes | READ-ONLY |
| `anim_reanchor` | (not recorded in the inventory) | `puerts_anim_pose` (does not exist) | no | READ-ONLY |
| `anim_root_motion_analyze` | (not recorded in the inventory) | `puerts_anim_pose` (does not exist) | yes | READ-ONLY |

### MCPBridgeClothOptimizer (4)

- Transactional: yes, `:269` and `:699`
- Rollback on failure: no cancel; pre-apply mask backup `:693`
- Independent inspector: yes, `InspectClothAsset`
- Convergent: content-hash guard, no no-op path
- **Ships as: READ-ONLY**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `cloth_apply_fabric_profile` | (not recorded in the inventory) | `puerts_cloth` (does not exist) | no | READ-ONLY |
| `cloth_apply_lower_leg_gradient` | (not recorded in the inventory) | `puerts_cloth` (does not exist) | no | READ-ONLY |
| `cloth_inspect_asset` | (not recorded in the inventory) | `puerts_cloth` (does not exist) | yes | READ-ONLY |
| `cloth_smooth_max_distance` | (not recorded in the inventory) | `puerts_cloth` (does not exist) | no | READ-ONLY |


## Group 4: Config and non-asset state (10) - cheap, low risk, one inspector missing

### MCPBridgeInputLibrary (4)

- Transactional: no, config write
- Rollback on failure: no
- Independent inspector: **none**
- Convergent: yes, exact-duplicate reject `:48`
- **Ships as: READ-ONLY**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `input_mapping_add` | AddActionMapping | `puerts_input_mapping` (does not exist) | no | READ-ONLY |
| `input_mapping_info` | (not recorded in the inventory) | `puerts_input_mapping` (does not exist) | yes | READ-ONLY |
| `input_mapping_remove` | RemoveActionMapping | `puerts_input_mapping` (does not exist) | no | READ-ONLY |
| `input_preset_apply` | (not recorded in the inventory) | `puerts_input_mapping` (does not exist) | no | READ-ONLY |

### FolderVisibilityLibrary (3)

- Transactional: n/a, state is an ini not a UObject
- Rollback on failure: n/a
- Independent inspector: yes, `GetHiddenFolders`
- Convergent: yes, `AddUnique` / `Remove`
- **Ships as: MUTATING**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `folder_hidden_list` | GetHiddenFolders | `puerts_folder_visibility` (does not exist) | yes | READ-ONLY |
| `folder_hide` | HideFolder | `puerts_folder_visibility` (does not exist) | no | MUTATING |
| `folder_show` | ShowFolder | `puerts_folder_visibility` (does not exist) | no | MUTATING |

### AutoPIEHelper (3)

- Transactional: n/a, PIE control is not transactable
- Rollback on failure: n/a
- Independent inspector: partial, `IsPIERunning` returns a bool
- Convergent: yes, already-running guard `:25`
- **Ships as: MUTATING**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `camera_shake_play` | PlayCameraShakeOnPlayer | `puerts_camera_shake` (does not exist) | no | MUTATING |
| `camera_shake_spawn` | (not recorded in the inventory) | `puerts_camera_shake` (does not exist) | no | MUTATING |
| `camera_shake_trigger` | PlayCameraShakeByPath | `puerts_camera_shake` (does not exist) | no | MUTATING |


## Group 5: Builders that are not ready to be fronted at all (7)

### AnimBlueprintBuilderLibrary (1)

- Transactional: **no**
- Rollback on failure: **no**
- Independent inspector: **none**
- Convergent: partial, skips existing variables
- **Ships as: BLOCKED**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `anim_blueprint_build_from_json` | BuildAnimBlueprintFromJSON | `puerts_anim_blueprint_build` (does not exist) | no | BLOCKED |

### WidgetBlueprintBuilderLibrary (4)

- Transactional: **no**
- Rollback on failure: **no**, clears the tree before it can fail
- Independent inspector: yes, but in `MCPBridgePuerTS`
- Convergent: **no**, destructive replace
- **Ships as: BLOCKED**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `title_widget_build_from_manifest` | (not recorded in the inventory) | `puerts_widget_build` (exists, live_verified) | no | BLOCKED |
| `widget_lower_third_create` | (not recorded in the inventory) | `puerts_widget_build` (exists, live_verified) | no | BLOCKED |
| `widget_title_card_create` | (not recorded in the inventory) | `puerts_widget_build` (exists, live_verified) | no | BLOCKED |
| `widget_title_template` | (not recorded in the inventory) | `puerts_widget_build` (exists, live_verified) | no | BLOCKED |

### MCPBridgeDataLibrary (2)

- Transactional: **no**
- Rollback on failure: **no**
- Independent inspector: **none**
- Convergent: **no**, wholesale row replace
- **Ships as: BLOCKED**

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `data_table_create` | FillDataTableFromJSON | `puerts_data_table` (does not exist) | no | BLOCKED |
| `data_table_fill_from_json` | FillDataTableFromJSON | `puerts_data_table` (does not exist) | no | BLOCKED |


## Suggested wave order

| Wave | Group | Why |
|---|---|---|
| next | 1 | The front already exists. `blueprint_graph_patch` is live_verified and covers 6; `blueprint_member_patch` is implemented and covers 11. The remaining work is live acceptance, not code. |
| then | 4 | Ten tools over config and non-asset state. No asset can be damaged, `FolderVisibilityLibrary` is already reconcile-shaped, and the only gap is an inspector for `MCPBridgeInputLibrary`. |
| then | 2 | Ten PIE agent tools. The read half (`observe`, `expect`, `status`, `telemetry_snapshot`) needs nothing new. The write half is user-gated by AGENTS.md regardless of readiness, so it cannot be proven unattended anyway. |
| then | 3 | Nine tools that can damage a skeletal mesh or an animation. Ship the four read tools, then fix convergence in `AnimPoseLibrary` before any reanchor is fronted. |
| last | 5 | Seven tools whose builders have neither a transaction nor a rollback. Fix the builder first; a native front would only make the damage reachable from further away. |

## Two things this map does not settle

- No tool here is proven by a live run. The verdicts are read from source, and
  source that opens a transaction is not the same as source that recovers from a
  failure. Each group's first native command needs one live acceptance before
  its verdict is anything more than a reading.
- `blueprint_compile` is counted in group 1 because the inventory puts it there,
  but no standalone `puerts_blueprint_compile` exists or is proposed. It is a
  step inside both patch commands. If a caller genuinely needs to compile
  without patching, that is a new tool, not a re-front.
