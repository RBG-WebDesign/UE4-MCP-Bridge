# REFRONT plan

What the 54 tools marked `migration_action: REFRONT` in `docs/TOOL_INVENTORY.json`
actually need, group by group.

REFRONT means: a working C++ implementation already exists and is compiled into
the plugin; only the doorway is wrong. The tool is reached through the legacy
Python HTTP listener and should be reached through the native pipe instead. The
implementation is reused, not rewritten.

The action was assigned per module, on paper, before anyone read the builders.
This document is the reading. Six of the 54 are re-classified below and the
reasons are given; the rest hold.

Source of truth: `docs/TOOL_INVENTORY.json` (generated) and
`docs/CAPABILITY_SCOREBOARD.json` -> `cpp_builders_awaiting_refront`.

## Summary

| Builder | Tools | Verdict |
|---|---|---|
| BlueprintMutatorLibrary | 17 | 11 REFRONT (done this wave, as `blueprint_member_patch`), 5 MERGE into `blueprint_graph_patch`, 1 blocked on a missing patch op |
| MCPBridgePIEAgent | 10 | REFRONT, one native command per tool. Largest remaining group. |
| AnimPoseLibrary | 5 | 3 REFRONT straight, 2 need a wrapper (the batch sweep is orchestration, not a builder call) |
| MCPBridgeClothOptimizer | 4 | REFRONT, straight pass-through |
| MCPBridgeInputLibrary | 4 | 3 REFRONT straight, 1 (`input_mapping_info`) is the read half and pairs with them |
| WidgetBlueprintBuilderLibrary | 4 | MERGE into `widget_build`, not REFRONT: they are presets over a builder that is already native |
| AutoPIEHelper | 3 | REFRONT, straight pass-through |
| FolderVisibilityLibrary | 3 | REFRONT, straight pass-through |
| MCPBridgeDataLibrary | 2 | REFRONT; the two tools are one command with a flag |
| AnimBlueprintBuilderLibrary | 1 | REFRONT, straight pass-through |
| BlueprintGraphBuilderLibrary | 1 | REFRONT (`blueprint_compile`); already reachable inside three native commands, needs standalone exposure |

Straight pass-through means the native command forwards the request to one
builder entry point and returns its result. Wrapper means the command has to own
something the builder does not: resolution, ordering, a transaction boundary,
convergence, or an inspector to verify against.

Every wrapper below inherits the same requirement the two shipped patch commands
meet: transactional, convergent, independently inspected, failure-atomic. A
builder that cannot support that is re-fronted READ ONLY and the gap is recorded,
rather than shipping a mutation that cannot roll back.

## The finding that shapes the whole list

`BlueprintMutator/BPMutatorHelpers.cpp:27-37` is the shared transaction wrapper
behind every `UBlueprintMutatorLibrary` entry point. It opens an
`FScopedTransaction`, runs the body, and on a false return logs and returns
false **without calling `Cancel()`**:

```cpp
const FScopedTransaction Transaction(TransactionName);
Blueprint->Modify();
const bool bBodyOk = Body();
if (!bBodyOk)
{
    UE_LOG(LogBlueprintMutator, Warning, ...);
    return false;                       // no Transaction.Cancel()
}
```

So the library is transactional and is not self-cancelling. Called on its own
from the legacy listener, a mutation that fails partway keeps what it already
wrote.

What saves `blueprint_member_patch` is nesting, not the library: UE4.27's
transaction buffer counts nested `BeginTransaction` calls and only finalises the
record when the count returns to zero, so the library's inner scope decrements
rather than commits, and the command's `ActiveTransaction->Cancel()` cancels the
whole record. That is why the command refuses to run without an active
transaction, and why `rollback_succeeded` is decided by re-reading the members
rather than by trusting the undo.

It also means the same is NOT automatically true of the other builders on this
list. Each group below states what its builder actually does, and a builder with
no rollback of its own is re-fronted read-only until the boundary exists.

Fixing `RunMutation` is one edit at one site rather than eleven. It is not done
here because it changes the behaviour of the legacy listener path as well, which
is a separate decision with its own evidence.

## Suggested wave order

| Wave | Group | Why |
|---|---|---|
| next | BlueprintMutatorLibrary | The front exists. `blueprint_graph_patch` is live_verified and `blueprint_member_patch` is implemented; what remains is live acceptance, not code. |
| then | FolderVisibility, MCPBridgeInputLibrary, MCPBridgeDataLibrary | Config and non-asset state. Cheap, no asset can be damaged, and `folder_hidden_list` / `input_mapping_info` are the read-backs already. |
| then | MCPBridgePIEAgent | Ten tools, no asset writes at all. The read half needs nothing new; the write half is user-gated by AGENTS.md regardless, so it cannot be proven unattended anyway. |
| then | AutoPIEHelper, AnimBlueprintBuilderLibrary, BlueprintGraphBuilderLibrary | Small and self-contained; `anim_blueprint_build` exposes the missing-inspector gap. |
| last | AnimPoseLibrary, MCPBridgeClothOptimizer | The two that can damage a skeletal mesh or an animation and have no rollback of their own. Ship their read tools, fix the boundary, then front the writes. |

---

## BlueprintMutatorLibrary (17 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintMutatorLibrary.h`

The largest group, in the largest capability category (blueprint, 29 tools). It
splits cleanly in two, and the split is the finding: five of these tools are not
REFRONT at all, because `blueprint_graph_patch` already re-fronted the same
primitive in wave one.

### Already covered by `blueprint_graph_patch`: MERGE, not REFRONT

`UBlueprintGraphBuilderLibrary::PatchBlueprintGraphFromJSON` supports
`add_node`, `update_node`, `remove_node`, `set_pin_default`, `connect_pins`,
`disconnect_pins` and `move_node`, each addressed by a resolved selector.

| Legacy tool | C++ entry point | Native command | Action |
|---|---|---|---|
| `blueprint_node_add` | `UBlueprintMutatorLibrary::AddNode` | `blueprint_graph_patch` op `add_node` | MERGE |
| `blueprint_node_delete` | `DeleteNode` | op `remove_node` | MERGE |
| `blueprint_node_move` | `MoveNode` | op `move_node` | MERGE |
| `blueprint_pins_connect` | `ConnectPins` | op `connect_pins` | MERGE |
| `blueprint_pins_break` | `BreakPinLinks` | op `disconnect_pins` | MERGE |

Re-fronting these as five separate native commands would ship a second, weaker
path to the same mutation: one node per round trip, no batch, no selector
resolution, no plan, and no single rollback boundary. That is the interface the
product goal exists to avoid. They are compatibility aliases onto the batch op,
or they are retired.

`blueprint_pins_break` is the one MERGE with a real difference: it breaks ALL
links on a named pin, where `disconnect_pins` names both ends. A caller wanting
the old behaviour issues one `disconnect_pins` per link, which is knowable from
`graph_inspect`. No capability is lost; a convenience is.

### Blocked: needs a patch op that does not exist

| Legacy tool | C++ entry point | Native command | Action |
|---|---|---|---|
| `blueprint_node_set_enabled` | `UBlueprintMutatorLibrary::SetNodeEnabled` | `blueprint_graph_patch` op `set_node_enabled` (does not exist) | MERGE, blocked |

`update_node` only writes pin defaults; it cannot reach `SetEnabledState`. The
fix is an op in `PatchBlueprintGraphFromJSON`, in the file the graph patch owns,
not a twelfth op in the member command. Deliberately NOT done this wave: it
belongs to whoever owns `BlueprintGraphBuilderLibrary.cpp` next, and adding it
from here would collide.

### Re-fronted this wave: `blueprint_member_patch`

The other eleven are the member half of a Blueprint, which `blueprint_graph_patch`
cannot reach at all and `blueprint_build` reaches only by restating the whole
asset. One batched native command, `blueprint_member_patch`, with one operation
per tool.

| Legacy tool | C++ entry point | Native op | Pass-through or wrapper |
|---|---|---|---|
| `blueprint_add_variable` | `AddVariable` | `add_variable` | wrapper |
| `blueprint_remove_variable` | `RemoveVariable` | `remove_variable` | wrapper |
| `blueprint_set_variable_default` | `SetVariableDefault` | `set_variable_default` | wrapper |
| `blueprint_add_function` | `AddFunction` | `add_function` | wrapper |
| `blueprint_remove_function` | `RemoveFunction` | `remove_function` | wrapper |
| `blueprint_add_interface` | `AddInterfaceImplementation` | `add_interface` | wrapper |
| `blueprint_remove_interface` | `RemoveInterfaceImplementation` | `remove_interface` | wrapper |
| `blueprint_add_event_dispatcher` | `AddEventDispatcher` | `add_event_dispatcher` | wrapper |
| `blueprint_remove_event_dispatcher` | `RemoveEventDispatcher` | `remove_event_dispatcher` | wrapper |
| `blueprint_component_remove` | `RemoveSCSNode` | `remove_component` | wrapper |
| `blueprint_component_rename` | `RenameSCSNode` | `rename_component` | wrapper |

Wrapper, not pass-through, for all eleven, and the reason is specific. Every
`UBlueprintMutatorLibrary` entry point runs `FBPMutatorHelpers::RunMutation`,
which opens its own transaction, marks the Blueprint structurally modified and
runs a FULL COMPILE. So:

- **Not convergent on its own.** `AddVariable` returns false when the variable
  exists; `AddFunction` returns an empty string. Rerunning a batch through the
  raw library is a failure, not a no-op. The command classifies each operation
  against live state and skips the ones already true.
- **Expensive to half-apply.** A batch that fails on operation four has already
  compiled the asset three times. The command resolves and validates the whole
  batch before the first mutation.
- **No inspection of its own.** Convergence and verification are measured through
  `MCPBridgeBlueprintMembers`, the snapshot `graph_inspect` now also reports as
  `member_structure_hash_sha1`, so the patch and the independent reader cannot
  disagree about what changed.
- **Rollback is not free.** The transaction is cancelled and the asset rollback
  boundary runs, then the members are READ AGAIN and `rollback_succeeded` reports
  what the read found, not what the undo claimed. Findings 0g.

---

## MCPBridgePIEAgent (10 tools)

`Plugins/MCPBridge/Source/MCPBridgePIEAgent`. The largest remaining group and the
obvious next wave.

| Legacy tool | Native command | Pass-through or wrapper |
|---|---|---|
| `pie_agent_move_to` | `pie_agent_move_to` | pass-through (async: returns an operation id) |
| `pie_agent_look_at` | `pie_agent_look_at` | pass-through |
| `pie_agent_press` | `pie_agent_press` | pass-through |
| `pie_agent_observe` | `pie_agent_observe` | pass-through, read only |
| `pie_agent_status` | `pie_agent_status` | pass-through, read only |
| `pie_agent_expect` | `pie_agent_expect` | pass-through, read only (polls in-engine) |
| `pie_agent_record_start` | `pie_agent_record_start` | wrapper: owns the recording file path limit |
| `pie_agent_record_stop` | `pie_agent_record_stop` | wrapper: same |
| `pie_agent_replay` | `pie_agent_replay` | wrapper: same |
| `gameplay_telemetry_snapshot` | `pie_telemetry_snapshot` | pass-through, read only |

Not transactable and correctly so: these drive a running PIE session, they do not
edit assets. The convergence and rollback requirements do not apply; the
requirement that does is that every one of them refuses cleanly when PIE is not
running, which the allowlist in `utils/editor_state.py` handles today and the
native service will have to handle itself.

---

## AnimPoseLibrary (5 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AnimPoseLibrary.h`,
entry points `ApplyReanchorPlan`, `ValidateReanchorPlan`.

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `anim_pose_snapshot` | pose readers | `anim_pose_snapshot` | pass-through, read only |
| `anim_pose_delta` | pose readers | `anim_pose_delta` | pass-through, read only |
| `anim_root_motion_analyze` | root track readers | `anim_root_motion_analyze` | pass-through, read only |
| `anim_reanchor` | `ValidateReanchorPlan` + `ApplyReanchorPlan` | `anim_reanchor` | wrapper: validate-then-apply, transaction, read-back |
| `anim_batch_reanchor` | the same two, per sequence | `anim_reanchor` with a list | wrapper: the sweep, the verdicts, the refusal on divergent clips |

Builder state: transactional, no cancel on failure, no convergence (a reanchor
applies its delta unconditionally, so running it twice moves the clip twice),
and a dry-run twin in `ValidateReanchorPlan` that is a genuine independent read.

`anim_batch_reanchor` is marked `destructiveHint: true` and writes AnimSequence
assets. It is the one tool in this group that must be failure-atomic across
several assets, which the single-asset rollback boundary does not cover today.
**Ship the three readers and the dry-run path only.** The write path waits for
convergence in the builder and a multi-asset rollback boundary; a native front
over an unconvergent write would only make double-application reachable from
further away.

---

## MCPBridgeClothOptimizer (4 tools)

`Plugins/MCPBridge/Source/MCPBridgeClothOptimizer`.

| Legacy tool | Native command | Pass-through or wrapper |
|---|---|---|
| `cloth_inspect_asset` | `cloth_inspect` | pass-through, read only |
| `cloth_apply_fabric_profile` | `cloth_apply_profile` | pass-through (the module owns its own transaction) |
| `cloth_apply_lower_leg_gradient` | `cloth_apply_gradient` | pass-through |
| `cloth_smooth_max_distance` | `cloth_smooth` | pass-through |

Builder state: transactional, no cancel on failure, a pre-apply mask backup, a
content-hash guard but no no-op path. The three writers need `cloth_inspect_asset`
as their independent read-back and it is in the same group, so the pairing is
free; the rollback boundary is not. **Ship `cloth_inspect_asset` first**, then the
writers once a failed apply provably restores the mask.

---

## MCPBridgeInputLibrary (4 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/MCPBridgeInputLibrary.h`,
entry points `AddActionMapping`, `RemoveActionMapping`, `AddAxisMapping`,
`RemoveAxisMapping`.

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `input_mapping_add` | `AddActionMapping` / `AddAxisMapping` | `input_mapping_patch` op `add` | wrapper |
| `input_mapping_remove` | `RemoveActionMapping` / `RemoveAxisMapping` | `input_mapping_patch` op `remove` | wrapper |
| `input_mapping_info` | mapping readers | `input_mapping_info` | pass-through, read only |
| `input_preset_apply` | the add entry points, in a loop | `input_mapping_patch` with a list | wrapper |

`input_preset_apply` is a batch of adds with a name, so it is the same command
with a canned operation list rather than a fourth entry point. These write
`DefaultInput.ini`, not an asset, so the asset rollback boundary does not apply
and the command needs its own: snapshot the config section, restore it on
failure, and verify by reading the mappings back.

---

## WidgetBlueprintBuilderLibrary (4 tools): MERGE, not REFRONT

| Legacy tool | Native command | Action |
|---|---|---|
| `widget_title_card_create` | `widget_build` with a title-card tree | MERGE |
| `widget_lower_third_create` | `widget_build` with a lower-third tree | MERGE |
| `widget_title_template` | `widget_build` | MERGE |
| `title_widget_build_from_manifest` | `widget_build` | MERGE (the manifest translation is a PuerTS workflow) |

Re-classified. `widget_build` is already native and already fronts
`WidgetBlueprintBuilderLibrary`; these four are preset trees over it, not
separate capabilities. Re-fronting them would add four native commands that
differ from `widget_build` only by the JSON they send. Per AGENTS.md, a workflow
repeatedly assembled from the same call is promoted into a PuerTS workflow, not
into a native command.

`widget_build` has no inspector-verified preset path yet, so the presets should
be proven against `widget_inspect` when they move.

---

## AutoPIEHelper (3 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AutoPIEHelper.h`.

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `camera_shake_play` | `PlayCameraShakeOnPlayer` | `camera_shake_play` | pass-through |
| `camera_shake_trigger` | `PlayCameraShakeByPath` | `camera_shake_trigger` | pass-through |
| `camera_shake_spawn` | `AShakeTriggerActor` | `camera_shake_spawn` | wrapper: spawns an actor, so it needs the actor transaction |

UE4.27 note that applies to all three: this build uses `UCameraShakeBase` with
`StartCameraShake()`, not `UCameraShake` / `PlayCameraShake`.

---

## FolderVisibilityLibrary (3 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/FolderVisibilityLibrary.h`.

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `folder_hide` | `HideFolder` | `folder_visibility_set` | pass-through |
| `folder_show` | `ShowFolder` | `folder_visibility_set` | pass-through |
| `folder_hidden_list` | `GetHiddenFolders` | `folder_hidden_list` | pass-through, read only |

Hide and show are one command with a boolean. Editor-view state, not asset
state: no transaction, and `folder_hidden_list` is the read-back.

---

## MCPBridgeDataLibrary (2 tools)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/MCPBridgeDataLibrary.h`,
entry point `FillDataTableFromJSON`.

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `data_table_create` | `FillDataTableFromJSON` | `data_table_build` (create if absent) | wrapper |
| `data_table_fill_from_json` | `FillDataTableFromJSON` | `data_table_build` (existing only) | wrapper |

The same entry point behind both, so one desired-state command with a
`create_if_missing` flag. Wrapper because `data_table_fill_from_json` REPLACES
all rows: that is destructive, needs the create-or-mutate decision made before
anything is touched, and needs the row set read back to verify.

---

## AnimBlueprintBuilderLibrary (1 tool)

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `anim_blueprint_build_from_json` | `BuildAnimBlueprintFromJSON` | `anim_blueprint_build` | pass-through |

`ValidateAnimBlueprintJSON` and `RebuildAnimBlueprintFromJSON` already exist
beside it, so the validate-before-mutate and the rebuild-versus-create decision
are the builder's, not the command's. Same shape as `behavior_tree_build`. It has
no inspector: `anim_blueprint_inspect` is the gap this one exposes, and the
capability rule says fix the gap before the feature expands.

---

## BlueprintGraphBuilderLibrary (1 tool)

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `blueprint_compile` | `CompileAndReport` | `blueprint_compile` | pass-through |

Already called inside `blueprint_build`, `blueprint_graph_patch` and
`blueprint_member_patch`; what is missing is the standalone tool, for compiling a
Blueprint a human edited. Cheapest item on this list.

---

## Re-classifications, collected

Six tools whose `migration_action` was assigned on paper and does not survive
reading the code:

| Tool | Was | Is | Why |
|---|---|---|---|
| `blueprint_node_add` | REFRONT | MERGE | `blueprint_graph_patch` op `add_node` |
| `blueprint_node_delete` | REFRONT | MERGE | op `remove_node` |
| `blueprint_node_move` | REFRONT | MERGE | op `move_node` |
| `blueprint_pins_connect` | REFRONT | MERGE | op `connect_pins` |
| `blueprint_pins_break` | REFRONT | MERGE | op `disconnect_pins`, one call per link |
| `widget_title_card_create`, `widget_lower_third_create`, `widget_title_template`, `title_widget_build_from_manifest` | REFRONT | MERGE | preset trees over the already-native `widget_build` |

And one that is MERGE but cannot be merged yet:

| Tool | Blocked on |
|---|---|
| `blueprint_node_set_enabled` | a `set_node_enabled` op in `PatchBlueprintGraphFromJSON`; `update_node` writes pin defaults only |

That leaves 38 of the 54 as genuine REFRONT work, 11 of which landed this wave.

The inventory's `migration_action` values are not edited by this document.
Changing them means editing `docs/TOOL_CAPABILITY_METADATA.json` and regenerating,
which retires public tool names, and that is a decision with a compatibility
cost rather than a documentation cleanup.
