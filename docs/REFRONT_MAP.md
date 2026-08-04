# REFRONT: readiness audit and migration plan

The tools whose implementation already exists as compiled C++ and whose
migration is therefore a matter of putting a native command in front of a
library, not of writing the capability.

REFRONT means: a working C++ implementation already exists and is compiled into
the plugin; only the doorway is wrong. The tool is reached through the legacy
Python HTTP listener and should be reached through the native pipe instead. The
implementation is reused, not rewritten.

## This document was two documents

`docs/REFRONT_MAP.md` was the readiness audit: per builder, whether it is
transactional, whether it rolls back, whether an independent inspector exists,
whether it converges, and therefore whether its tools can ship as writes or only
as reads. `docs/REFRONT_PLAN.md` was the migration table: per tool, which
builder already implements it, what the native command is called, and whether it
forwards or needs a wrapper.

They covered the same tools, were written in the same session, and reached the
same central finding. Two overlapping maps of one territory is a maintenance
trap, so they are merged here. Nothing either author established is dropped:
where they disagreed, both readings are stated and the disagreement is named.
`docs/REFRONT_PLAN.md` no longer exists; this file is what it pointed at.

Read the readiness verdicts to decide what is safe to ship as a write. Read the
migration tables to decide what to write.

## The number, and how it moved

Counts here are derived, never hand-kept:

```bash
node -e "const j=require('./docs/TOOL_INVENTORY.json');
  console.log(j.tools.filter(t=>t.migration_action==='REFRONT').length)"
```

| Point in time | Total registrations | REFRONT | ALIAS |
|---|---|---|---|
| Before this wave | 209 | 54 | 29 |
| After this wave | 240 | 28 | 81 |

The 54 is what the two source documents audited. The 28 is what is left. The
difference is 26 legacy names that now have a native command behind them and a
compatibility alias keeping the old name answering. Every one of the 26 is
listed in the group tables below.

The total grew by 31: five new native commands, and 26 new alias registrations.
An alias registration is a second row for a name that already existed, not a new
name, so no public name was added or retired by this wave.

### The MERGE argument, and its arithmetic

`REFRONT_PLAN.md` argued that some of the 54 were never REFRONT work at all,
because a stronger native command already covers them. That argument is sound
and is kept:

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

That is six table rows and ten tools. `REFRONT_PLAN.md` said "six of the 54",
counting rows, and closed with "that leaves 38 of the 54 as genuine REFRONT
work". 38 does not follow from its own tables: 54 minus ten is 44. The figure is
recorded here as a slip rather than silently corrected, because
`docs/PROJECT_FINISH_PLAN.md` quoted the 38 and anyone tracing that number
should find the reason it does not reconcile.

`REFRONT_MAP.md` declined to reclassify at all, on the grounds that it reports
readiness and `migration_action` is a compatibility decision. That caution has
since been answered by the alias mechanism itself: an alias keeps the old public
name answering, so reclassifying a tool whose alias exists retires nothing. The
26 aliased this wave are marked ALIAS in
`docs/TOOL_CAPABILITY_METADATA.json`. The remaining MERGE candidates are not
marked, because no alias fronts them yet and a label without a route is a claim
the catalog cannot honour.

### The reconciled number

**Of the 28 REFRONT rows left, six are really MERGE, so 22 are genuine REFRONT
work.**

The six: `blueprint_pins_break` and `blueprint_node_set_enabled` (both blocked,
see the BlueprintMutatorLibrary section), and the four
`WidgetBlueprintBuilderLibrary` preset tools.

The 22, by builder:

| Builder | Tools |
|---|---|
| MCPBridgePIEAgent | 7: the write half plus `gameplay_telemetry_snapshot` |
| AnimPoseLibrary | 5 |
| MCPBridgeClothOptimizer | 4 |
| AutoPIEHelper | 2: `camera_shake_spawn`, `camera_shake_trigger` |
| MCPBridgeDataLibrary | 2 |
| AnimBlueprintBuilderLibrary | 1 |
| BlueprintGraphBuilderLibrary | 1: `blueprint_compile` |

## What the four readiness columns mean

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
the missing property. That is the rule this document applies; it is not a
per-tool judgement call.

Straight pass-through means the native command forwards the request to one
builder entry point and returns its result. Wrapper means the command has to own
something the builder does not: resolution, ordering, a transaction boundary,
convergence, or an inspector to verify against.

Every wrapper inherits the same requirement the shipped patch commands meet:
transactional, convergent, independently inspected, failure-atomic. A builder
that cannot support that is re-fronted READ ONLY and the gap is recorded, rather
than shipping a mutation that cannot roll back.

## The defect that shaped most of this document

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Private/BlueprintMutator/BPMutatorHelpers.cpp`
is the shared transaction wrapper behind every `UBlueprintMutatorLibrary` entry
point. As both source documents found it, it opened an `FScopedTransaction`, ran
the body, and on a false return logged and returned false **without calling
`Cancel()`**:

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

The scoped transaction then destructs normally and commits whatever the body
already wrote. So the library was transactional and was not failure-atomic.
Called on its own from the legacy listener, a mutation that failed partway kept
what it had already written.

What saved `blueprint_member_patch` was nesting, not the library: UE4.27's
transaction buffer counts nested `BeginTransaction` calls and only finalises the
record when the count returns to zero, so the library's inner scope decrements
rather than commits, and the command's `ActiveTransaction->Cancel()` cancels the
whole record. That is why the command refuses to run without an active
transaction, and why `rollback_succeeded` is decided by re-reading the members
rather than by trusting the undo.

It also meant the same was NOT automatically true of the other builders on this
list. Each group below states what its builder actually does, and a builder with
no rollback of its own is re-fronted read-only until the boundary exists.

Repo-wide at the time of the audit: only three C++ call sites outside
`MCPBridgePuerTS` opened a transaction at all, and there was not one
`CancelTransaction` among them. AGENTS.md says "Every tool that modifies editor
state is wrapped in a UE4 transaction". For that C++ layer it was not true.

**Status.** The fix is one edit at one site rather than nineteen, and it exists:
`lane/g-mutator-atomicity` commit 3ba4580, "Give the mutator failure path a
rollback that actually restores", with `Scripts/mutator-atomicity.mjs` as its
live evidence. It is NOT in this branch's tree. Anyone reading
`BPMutatorHelpers.cpp` here will still find the uncancelled path quoted above.
The aliases landed in this document's wave do not depend on it: they route to
`blueprint_member_patch` and `blueprint_graph_patch`, both of which own their
own rollback boundary and were built for a library that does not cancel.

## Grouping

Five groups, ordered so a wave can take one and finish it.

---

## Group 1: Blueprint editing (18)

### BlueprintMutatorLibrary (17)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintMutatorLibrary.h`

- Transactional: yes, `BPMutatorHelpers.cpp:27`
- Rollback on failure: no in this tree; fixed on `lane/g-mutator-atomicity`
- Independent inspector: yes, `UBlueprintInspectorLibrary`
- Convergent: refuses duplicates, no `bUnchanged`
- **Ships as: MUTATING**, because the two native patch commands own the boundary
  the library lacks

The largest group, in the largest capability category (blueprint, 29 tools). It
splits in two, and the split is the finding: five of these tools were never
REFRONT, because `blueprint_graph_patch` already re-fronted the same primitive
in wave one.

#### The member half: eleven tools, one batch command

The eleven are the member half of a Blueprint, which `blueprint_graph_patch`
cannot reach at all and `blueprint_build` reaches only by restating the whole
asset. One batched native command, `blueprint_member_patch`, with one operation
per tool.

| Legacy tool | C++ entry point | Native op | Pass-through or wrapper | Status |
|---|---|---|---|---|
| `blueprint_add_variable` | `AddVariable` | `add_variable` | wrapper | ALIAS landed |
| `blueprint_remove_variable` | `RemoveVariable` | `remove_variable` | wrapper | ALIAS landed |
| `blueprint_set_variable_default` | `SetVariableDefault` | `set_variable_default` | wrapper | ALIAS landed |
| `blueprint_add_function` | `AddFunction` | `add_function` | wrapper | ALIAS landed |
| `blueprint_remove_function` | `RemoveFunction` | `remove_function` | wrapper | ALIAS landed |
| `blueprint_add_interface` | `AddInterfaceImplementation` | `add_interface` | wrapper | ALIAS landed |
| `blueprint_remove_interface` | `RemoveInterfaceImplementation` | `remove_interface` | wrapper | ALIAS landed |
| `blueprint_add_event_dispatcher` | `AddEventDispatcher` | `add_event_dispatcher` | wrapper | ALIAS landed |
| `blueprint_remove_event_dispatcher` | `RemoveEventDispatcher` | `remove_event_dispatcher` | wrapper | ALIAS landed |
| `blueprint_component_remove` | `RemoveSCSNode` | `remove_component` | wrapper | ALIAS landed |
| `blueprint_component_rename` | `RenameSCSNode` | `rename_component` | wrapper | ALIAS landed |

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

One narrowing every member alias inherits: `blueprint_member_patch` only patches
Blueprints under `/Game/MCPGenerated/`, and refuses any other path by name. That
is the native command's limit, not the alias's, and the alias refuses rather
than working around it.

#### Covered by `blueprint_graph_patch`: MERGE, not REFRONT

`UBlueprintGraphBuilderLibrary::PatchBlueprintGraphFromJSON` supports
`add_node`, `update_node`, `remove_node`, `set_pin_default`, `connect_pins`,
`disconnect_pins` and `move_node`, each addressed by a resolved selector.

| Legacy tool | C++ entry point | Native command | Action | Status |
|---|---|---|---|---|
| `blueprint_node_add` | `AddNode` | `blueprint_graph_patch` op `add_node` | MERGE | ALIAS landed |
| `blueprint_node_delete` | `DeleteNode` | op `remove_node` | MERGE | ALIAS landed |
| `blueprint_node_move` | `MoveNode` | op `move_node` | MERGE | ALIAS landed |
| `blueprint_pins_connect` | `ConnectPins` | op `connect_pins` | MERGE | ALIAS landed |
| `blueprint_pins_break` | `BreakPinLinks` | op `disconnect_pins` | MERGE | **no alias** |
| `blueprint_node_set_enabled` | `SetNodeEnabled` | op `set_node_enabled` (does not exist) | MERGE | **blocked** |

Re-fronting these as separate native commands would ship a second, weaker path
to the same mutation: one node per round trip, no batch, no selector resolution,
no plan, and no single rollback boundary. That is the interface the product goal
exists to avoid. They are compatibility aliases onto the batch op, or they are
retired.

Two carry a difference worth stating:

- `blueprint_node_add` has no alias-free translation. The native builder's node
  vocabulary is not the legacy tool's: `CreateWidget`, `MacroInstance`,
  `SwitchEnum`, `SwitchName`, `InputAction`, `InputAxisEvent`, `AddDelegate`,
  `RemoveDelegate` and `CallDelegate` have no native factory, and the alias
  refuses each by name rather than sending a type that would build nothing.
  Three config keys are respelled for the native `params` object: CallFunction
  `targetClass` and `functionName` become `class` and `function`, Sequence
  `numOutputs` becomes `num_outputs`, InputKey `key` becomes `fkey_name`.
  Everything else passes through untouched, because the builder translates
  snake_case params back to the camelCase the registry factories read
  (`BlueprintGraphBuilderLibrary.cpp`, `RegistryConfigKey`), so a legacy
  camelCase key already arrives in the spelling the factory wants. Only the node
  types the builder dispatches itself, which never see that table, need
  renaming. The legacy tool also returned the new node's GUID; the alias returns
  the patch response, which describes the created node. A caller that needs the
  GUID reads it back with `graph_inspect`.
- `blueprint_pins_break` breaks ALL links on a named pin, where `disconnect_pins`
  names both ends. An alias cannot know the other ends without a read it is not
  allowed to make, so **no alias exists**: a caller wanting the old behaviour
  issues one `disconnect_pins` per link, which is knowable from `graph_inspect`.
  No capability is lost; a convenience is.

`blueprint_node_set_enabled` is blocked on a `set_node_enabled` op in
`PatchBlueprintGraphFromJSON`. `update_node` only writes pin defaults; it cannot
reach `SetEnabledState`. The fix belongs to whoever owns
`BlueprintGraphBuilderLibrary.cpp` next, not to a twelfth op in the member
command.

### BlueprintGraphBuilderLibrary (1)

- Transactional: no, deliberately: the caller owns the boundary
- Rollback on failure: hand-rolled abort, `:1732`
- Independent inspector: yes, `DescribeBlueprintGraphJSON`
- Convergent: yes, `bUnchanged` + `bPlanOnly`
- **Ships as: MUTATING**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper | Status |
|---|---|---|---|---|
| `blueprint_compile` | `CompileAndReport` | `blueprint_compile` | pass-through | **no native command** |

Already called inside `blueprint_build`, `blueprint_graph_patch` and
`blueprint_member_patch`; what is missing is the standalone tool, for compiling a
Blueprint a human edited. Cheapest item on this list, and it cannot be aliased
until it exists: both patch commands require at least one operation, so neither
can express a compile that changes nothing.

---

## Group 2: PIE agent (10)

`Plugins/MCPBridge/Source/MCPBridgePIEAgent`.

- Transactional: n/a, mutates a PIE world not an asset
- Rollback on failure: no
- Independent inspector: yes, `Observe` / `GetOperationStatus`
- Convergent: no, imperative API
- **Ships as: READ + user-gated CONTROL; telemetry remains**

| Legacy tool | Native command | Pass-through or wrapper | Status |
|---|---|---|---|
| `pie_agent_observe` | `pie_agent_query` op `observe` | pass-through, read only | ALIAS landed |
| `pie_agent_status` | `pie_agent_query` op `status` | pass-through, read only | ALIAS landed |
| `pie_agent_expect` | `pie_agent_query` op `expect` | pass-through, read only (polls in-engine) | ALIAS landed |
| `pie_agent_move_to` | `pie_agent_control` op `move_to` | pass-through (async: returns an operation id) | LANDED, live pending |
| `pie_agent_look_at` | `pie_agent_control` op `look_at` | pass-through | LANDED, live pending |
| `pie_agent_press` | `pie_agent_control` op `press` | pass-through | LANDED, live pending |
| `pie_agent_record_start` | `pie_agent_control` op `record_start` | pass-through | LANDED, live pending |
| `pie_agent_record_stop` | `pie_agent_control` op `record_stop` | pass-through | LANDED, live pending |
| `pie_agent_replay` | `pie_agent_control` op `replay` | pass-through | LANDED, live pending |
| `gameplay_telemetry_snapshot` | `pie_telemetry_snapshot` | pass-through, read only | not fronted |

Not transactable and correctly so: these drive a running PIE session, they do not
edit assets. The convergence and rollback requirements do not apply; the
requirement that does is that every one of them refuses cleanly when PIE is not
running, which the allowlist in `utils/editor_state.py` handles today and the
native service handles itself.

The control half is user-gated by AGENTS.md regardless of readiness. Its source,
editor-free contract, native compile and final link are complete; live proof remains.

One behaviour difference, in the return rather than the request: the legacy
`pie_agent_expect` blocked in the MCP server until the conditions passed or the
deadline expired, and answered with the verdict. `pie_agent_query` op `expect`
starts the check and answers with `operation_id` and status `running`; poll op
`status` for the verdict. The blocking loop is orchestration and belongs in the
caller, not in a command that occupies the game thread while it waits. The alias
description says so.

`gameplay_telemetry_snapshot` is deliberately NOT aliased onto `observe`. It
reports new log lines SINCE THE LAST SNAPSHOT, plus AI controller states, and
`observe` reports a log tail and no AI states. Routing one onto the other would
answer a delta question with a window's worth of lines and look correct.

---

## Group 3: Asset mutators with real damage potential (9): READ-ONLY first, no exceptions

### AnimPoseLibrary (5)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AnimPoseLibrary.h`, entry
points `ApplyReanchorPlan`, `ValidateReanchorPlan`.

- Transactional: yes, `:172`
- Rollback on failure: **no**, and the source says undo is not trustworthy here
- Independent inspector: yes, dry-run twin `ValidateReanchorPlan`
- Convergent: **no**, applies a delta unconditionally
- **Ships as: READ-ONLY**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `anim_pose_snapshot` | pose readers | `anim_pose_snapshot` | pass-through, read only |
| `anim_pose_delta` | pose readers | `anim_pose_delta` | pass-through, read only |
| `anim_root_motion_analyze` | root track readers | `anim_root_motion_analyze` | pass-through, read only |
| `anim_reanchor` | `ValidateReanchorPlan` + `ApplyReanchorPlan` | `anim_reanchor` | wrapper: validate-then-apply, transaction, read-back |
| `anim_batch_reanchor` | the same two, per sequence | `anim_reanchor` with a list | wrapper: the sweep, the verdicts, the refusal on divergent clips |

A reanchor applies its delta unconditionally, so running it twice moves the clip
twice. `anim_batch_reanchor` is marked `destructiveHint: true` and writes
AnimSequence assets. It is the one tool in this group that must be
failure-atomic across several assets, which the single-asset rollback boundary
does not cover today. **Ship the three readers and the dry-run path only.** The
write path waits for convergence in the builder and a multi-asset rollback
boundary; a native front over an unconvergent write would only make
double-application reachable from further away.

### MCPBridgeClothOptimizer (4)

`Plugins/MCPBridge/Source/MCPBridgeClothOptimizer`.

- Transactional: yes, `:269` and `:699`
- Rollback on failure: no cancel; pre-apply mask backup `:693`
- Independent inspector: yes, `InspectClothAsset`
- Convergent: content-hash guard, no no-op path
- **Ships as: READ-ONLY**

| Legacy tool | Native command | Pass-through or wrapper | Status |
|---|---|---|---|
| `cloth_inspect_asset` | `cloth_inspect` | pass-through, read only | ALIAS landed, lane W, uncompiled |
| `cloth_apply_fabric_profile` | `cloth_apply_profile` | pass-through (the module owns its own transaction) | **not fronted** |
| `cloth_apply_lower_leg_gradient` | `cloth_apply_gradient` | pass-through | **not fronted** |
| `cloth_smooth_max_distance` | `cloth_smooth` | pass-through | **not fronted** |

The three writers need `cloth_inspect_asset` as their independent read-back and
it is in the same group, so the pairing is free; the rollback boundary is not.
**Ship `cloth_inspect_asset` first**, then the writers once a failed apply
provably restores the mask.

**The read half landed.** `puerts_cloth_inspect` fronts
`UClothOptimizerLibrary::InspectClothAsset` as a straight pass-through, with
`cloth_inspect_asset` aliased onto it. The snapshot is the library's own and the
module's editor panel reads the same function, so an MCP read and what a human
sees in the panel cannot disagree. One thing the command does add: the library
reports its failures INSIDE the snapshot as `success: false` with an error
string, so the command lifts that into a refusal rather than returning a success
envelope wrapping a failure.

The three writers stay where they are. What separates this group from
navigation, where a non-transactional write shipped anyway, is what the write
costs: a navmesh is derived from the level and the recovery from a bad build is
another build, while cloth paint is AUTHORED and a half-applied mask is lost
work. That is the line, and it is why `write_unsupported_reason` is in every
`cloth_inspect` response rather than only in this document.

---

## Group 4: Config and non-asset state (10)

### MCPBridgeInputLibrary (4)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/MCPBridgeInputLibrary.h`,
entry points `AddActionMapping`, `RemoveActionMapping`, `AddAxisMapping`,
`RemoveAxisMapping`.

- Transactional: no, config write
- Rollback on failure: no
- Independent inspector: **none before this wave**
- Convergent: yes, exact-duplicate reject `:48`
- **Shipped as: READ-ONLY inspector plus a MUTATING patch that owns its own
  snapshot boundary**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper | Status |
|---|---|---|---|---|
| `input_mapping_info` | mapping readers | `input_mapping_info` | pass-through, read only | ALIAS landed |
| `input_mapping_add` | `AddActionMapping` / `AddAxisMapping` | `input_mapping_patch` | wrapper | ALIAS landed |
| `input_mapping_remove` | `RemoveActionMapping` / `RemoveAxisMapping` | `input_mapping_patch` | wrapper | ALIAS landed |
| `input_preset_apply` | the add entry points, in a loop | `input_mapping_patch` with a preset | wrapper | ALIAS landed |

The missing inspector was the group's one real gap and it is closed:
`input_mapping_info` is the read half, and it is the same digest
`input_mapping_patch` reports as `pre_mapping_hash_sha1` and
`post_mapping_hash_sha1`, so a patch is verifiable against a read that did not
perform it.

`input_preset_apply` is a batch of adds with a name, so it is the same command
with a canned operation list rather than a fourth entry point. The presets
expand in the PuerTS layer into the same actions and axes a caller could write
by hand, so a preset cannot mean something the explicit form cannot express.

These write `DefaultInput.ini`, not an asset, so the asset rollback boundary
does not apply and the command owns its own: both mapping arrays are snapshotted
before the first write, restored exactly on any failure, and
`rollback_succeeded` is decided by re-reading the mappings and comparing the
hash.

`remove_unlisted` is refused outright when neither `actions` nor `axes` is
given, because a bare `remove_unlisted` would erase the project's whole input
configuration from a request that named nothing.

### FolderVisibilityLibrary (3)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/FolderVisibilityLibrary.h`.

- Transactional: n/a, state is an ini not a UObject
- Rollback on failure: n/a
- Independent inspector: yes, `GetHiddenFolders`
- Convergent: yes, `AddUnique` / `Remove`
- **Ships as: MUTATING**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper | Status |
|---|---|---|---|---|
| `folder_hide` | `HideFolder` | `folder_visibility` | pass-through | ALIAS landed |
| `folder_show` | `ShowFolder` | `folder_visibility` | pass-through | ALIAS landed |
| `folder_hidden_list` | `GetHiddenFolders` | `folder_visibility` (read path) | pass-through, read only | ALIAS landed |

Hide and show are one command. Editor-view state, not asset state: no
transaction, and the reverse of any change is another call. The command is
desired-state first: `hidden` is the whole set and it converges onto it, while
`hide` and `show` are the delta form the legacy pair used. The two shapes are
mutually exclusive and a request carrying both is refused rather than resolved
by a rule the caller cannot see. Calling with none of the three is the read,
which is what `folder_hidden_list` asked for. The legacy escape hatch that
`folder_show` with no arguments unhides everything is expressed as the empty
desired set, `hidden: []`.

### AutoPIEHelper (3)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AutoPIEHelper.h`.

- Transactional: n/a, PIE control is not transactable
- Rollback on failure: n/a
- Independent inspector: partial, `IsPIERunning` returns a bool
- Convergent: yes, already-running guard `:25`
- **Ships as: MUTATING**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper | Status |
|---|---|---|---|---|
| `camera_shake_play` | `PlayCameraShakeOnPlayer` | `camera_shake` | pass-through | ALIAS landed |
| `camera_shake_trigger` | `AShakeTriggerActor` | needs an actor-spawning command | wrapper | not fronted |
| `camera_shake_spawn` | `AShakeTriggerActor` | needs an actor-spawning command | wrapper | not fronted |

UE4.27 note that applies to all three: this build uses `UCameraShakeBase` with
`StartCameraShake()`, not `UCameraShake` / `PlayCameraShake`.

Correction to the earlier audit, which recorded `camera_shake_trigger` as
`PlayCameraShakeByPath`: the legacy tool spawns a `ShakeTriggerActor` that plays
a shake when the player overlaps its box, and `camera_shake_spawn` spawns a
CameraShakeSourceActor. Both write actors into the level, so both need the actor
transaction and neither is reachable from `camera_shake`, which only plays a
shake on the running session's player camera. Aliasing either onto it would
report a spawn that did not happen.

`camera_shake_play` also drops one legacy parameter loudly:
`play_space` has no native equivalent, because the chain uses the shake asset's
own play space. Supplying it fails the call rather than being ignored.

---

## Group 5: Builders that are not ready to be fronted at all (7)

### AnimBlueprintBuilderLibrary (1)

Three of the four columns were closed at the command layer rather than in the
library, the same way `blueprint_member_patch` closed them for
`UBlueprintMutatorLibrary`. The fourth was not, and the shape of the command is
the consequence.

- Transactional: yes, via `IsToolMutating` in `MCPPuerTSBridgeService`
- Rollback on failure: yes, `FBridgeAssetRollback` around asset creation, with
  the transaction cancelled before the boundary runs
- Independent inspector: yes, `InspectAnimBlueprintJson`
- Convergent: **yes since lane W**, and it was not fixable at the command layer,
  which is why it took a change to the builder. `Rebuild` used to say in its own
  comment that it assumed a clean AnimBlueprint and cleared nothing, so
  rebuilding over an existing graph appended a second state machine rather than
  converging. `FAnimBPBuilder::ClearGeneratedGraph` is the pass it never had and
  `Rebuild` calls it; the event graph gets `bClearExistingGraph = true` for the
  same reason. UNCOMPILED.
- Failure-atomic: **no, and this is now the only blocker.**
  `FBridgeAssetRollback` can delete an asset the command created but cannot
  restore the previous contents of one that already existed, and the builder
  compiles between the transaction and any undo. The clear pass makes that
  strictly worse for a patch path rather than better: a failed rerun used to
  leave a DUPLICATED AnimBlueprint and now leaves an EMPTIED one. Both are
  unrecoverable; the new one loses more. Finding 0t.
- **Ships as: MUTATING, create-only.** The command refuses an existing asset, so
  the only mutation reachable is creating an asset that did not exist, and that
  is failure-atomic. A rerun is a refusal, not a no-op.

To unblock a patch command: add the clear pass `Rebuild` is missing, then a
content snapshot the boundary can restore.

**Both are done, and both are uncompiled.** Lane W landed the clear pass
(`FAnimBPBuilder::ClearGeneratedGraph`). Lane Y landed the snapshot, and it is
not the one this map predicted: duplicating the AnimBlueprint into a transient
package was read and rejected, because `FBlueprintEditorUtils::PostDuplicateBlueprint`
regenerates every node and variable guid and builds a fresh generated class
without carrying the old CDO's values across, so that restore would silently
drop every variable default. The snapshot that works is the one already on
disk: `FBridgeContentSnapshot` refuses to start unless the asset is saved and
clean, and restores with `UPackageTools::ReloadPackages`. Finding 0t.

So `puerts_anim_blueprint_build` stays CREATE-ONLY on purpose - create and edit
are separate commands, each refusing the other's case - and
`puerts_anim_blueprint_patch` is the edit half.

| legacy tool | C++ entry point | native command it would front | read-only today | ships as |
|---|---|---|---|---|
| `anim_blueprint_build_from_json` | BuildAnimBlueprintFromJSON | `puerts_anim_blueprint_build` (implemented, uncompiled) | no | MUTATING (create-only) |
| (none: new capability) | RebuildAnimBlueprintFromJSON | `puerts_anim_blueprint_patch` (lane Y, implemented, uncompiled) | no | DESTRUCTIVE IDEMPOTENT |

Three read tools were added beside it and front nothing legacy:
`puerts_anim_blueprint_inspect`, `puerts_anim_montage_inspect` and
`puerts_anim_blend_space_inspect`. The last two have no write counterpart on
purpose. UE4.27 exposes no atomic operation for rebuilding a montage's
`NextSectionName` chain or re-linking its `FAnimLinkableElement` notifies, and
it rebuilds a blend space's triangulation from the sample set rather than
offering an atomic sample-set replacement. In both cases a half-applied edit
leaves an asset that plays or interpolates the wrong thing rather than one that
fails, so neither writer could be failure-atomic.
| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `anim_blueprint_build_from_json` | `BuildAnimBlueprintFromJSON` | `anim_blueprint_build` | pass-through |

`ValidateAnimBlueprintJSON` and `RebuildAnimBlueprintFromJSON` already exist
beside it, so the validate-before-mutate and the rebuild-versus-create decision
are the builder's, not the command's. Same shape as `behavior_tree_build`. It has
no inspector: `anim_blueprint_inspect` is the gap this one exposes, and the
capability rule says fix the gap before the feature expands.

### WidgetBlueprintBuilderLibrary (4): MERGE, not REFRONT

- Transactional: **no**
- Rollback on failure: **no**, clears the tree before it can fail
- Independent inspector: yes, but in `MCPBridgePuerTS`
- Convergent: **no**, destructive replace
- **Ships as: BLOCKED**

| Legacy tool | Native command | Action |
|---|---|---|
| `widget_title_card_create` | `widget_build` with a title-card tree | MERGE |
| `widget_lower_third_create` | `widget_build` with a lower-third tree | MERGE |
| `widget_title_template` | `widget_build` | MERGE |
| `title_widget_build_from_manifest` | `widget_build` | MERGE (the manifest translation is a PuerTS workflow) |

`widget_build` is already native and already fronts
`WidgetBlueprintBuilderLibrary`; these four are preset trees over it, not
separate capabilities. Re-fronting them would add four native commands that
differ from `widget_build` only by the JSON they send. Per AGENTS.md, a workflow
repeatedly assembled from the same call is promoted into a PuerTS workflow, not
into a native command.

`widget_build` has no inspector-verified preset path yet, so the presets should
be proven against `widget_inspect` when they move.

### MCPBridgeDataLibrary (2)

`Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/MCPBridgeDataLibrary.h`,
entry point `FillDataTableFromJSON`.

- Transactional: **no**
- Rollback on failure: **no**
- Independent inspector: **none**
- Convergent: **no**, wholesale row replace
- **Ships as: BLOCKED**

| Legacy tool | C++ entry point | Native command | Pass-through or wrapper |
|---|---|---|---|
| `data_table_create` | `FillDataTableFromJSON` | `data_table_build` (create if absent) | wrapper |
| `data_table_fill_from_json` | `FillDataTableFromJSON` | `data_table_build` (existing only) | wrapper |

The same entry point behind both, so one desired-state command with a
`create_if_missing` flag. Wrapper because `data_table_fill_from_json` REPLACES
all rows: that is destructive, needs the create-or-mutate decision made before
anything is touched, and needs the row set read back to verify.

---

## Suggested wave order, for what is left

| Wave | Group | Why |
|---|---|---|
| next | AutoPIEHelper, BlueprintGraphBuilderLibrary | Three small items: `blueprint_compile` is the cheapest tool on the list, and the two camera-shake spawners need one actor-spawning command between them. |
| then | MCPBridgePIEAgent write half | Seven tools, no asset writes. User-gated by AGENTS.md, so plan the acceptance around a human being present. |
| then | AnimBlueprintBuilderLibrary, MCPBridgeDataLibrary | Small and self-contained; `anim_blueprint_build` exposes the missing-inspector gap, and the data table command needs the create-or-mutate decision made up front. |
| last | AnimPoseLibrary, MCPBridgeClothOptimizer | The two that can damage a skeletal mesh or an animation and have no rollback of their own. Ship their read tools, fix the boundary, then front the writes. |

The MERGE items are not waves. `blueprint_pins_break` needs no work unless
someone wants the convenience back; `blueprint_node_set_enabled` needs one op in
`PatchBlueprintGraphFromJSON`; the four widget presets need a PuerTS workflow
over `widget_build` and an inspector-verified preset path.

## What this document still does not settle

- **No tool here is proven by a live run.** The verdicts are read from source,
  and source that opens a transaction is not the same as source that recovers
  from a failure. Everything landed in this wave is `implemented` in
  `docs/TOOL_CAPABILITY_METADATA.json`, never `live_verified`. The five native
  commands added for groups 2 and 4 are not compiled either: no editor was built
  in the wave that wrote them.
- **The alias translations are unit-tested, not live-tested.** The parameter
  shapes are asserted against the mock listener in
  `mcp-server/tests/compat-tools.test.ts`, which proves the alias sends what it
  claims to send. It does not prove the editor accepts it.
- **`blueprint_node_add` is the riskiest of them.** Its three config key
  respellings are read out of the builder's own translation table, not observed
  in a running editor. A wrong key would be a node that builds with a routing
  parameter missing.
