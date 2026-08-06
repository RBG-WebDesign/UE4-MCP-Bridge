# Animation Blueprint Tools

Detail for `puerts_anim_blueprint_build` and `puerts_anim_blueprint_patch`. Both
take the same spec; only `asset_path` behaves differently.

## Which one

`build` refuses a path that is occupied. `patch` refuses a path that is empty.
Neither has to guess whether you meant create or edit, and a rerun of a `build`
spec is a refusal rather than a no-op.

## Spec limits inherited from the UE4.27 builder (v1)

- `variables` are type `"bool"` only.
- `anim_graph.pipeline` node types are `"StateMachine"` and `"Slot"`. The
  pipeline is wired in array order and terminates at the graph's Root node. For a
  Slot node, `name` is the slot name montages play into.
- Every state plays exactly one AnimSequence.
- Transition conditions are `{type:"bool_variable", variable, value}` or
  `{type:"time_remaining", threshold}`. A `time_remaining` condition sets UE4.27's
  automatic remaining-time rule, which uses the transition's `blend_time` as the
  trigger offset because 4.27 exposes no separate trigger time.
- `state_machine.states[].id` is referenced by transitions and is not persisted on
  the node. `name` is what the inspector reports back. Exactly one state should
  set `is_entry`.
- `blend_time` is the crossfade in seconds, default 0.2.
- `event_graph` uses the same grammar `puerts_blueprint_build`'s `graph` uses.
  See `blueprint-tools.md`.
- `skeleton_path` is required. An Animation Blueprint has no meaning without one.

## `anim_blueprint_build`

Creates a new Animation Blueprint in one transaction: member variables, the anim
graph pipeline, a state machine with its states and transitions, and an optional
event graph.

The asset is compiled and the compile result is returned (`compile_status`, plus
compiler errors and warnings) rather than assumed. Before the asset is saved it is
read back through `puerts_anim_blueprint_inspect` and every requested state and
transition must be present.

A failure at any point cancels the transaction and rolls the creation back. The
response carries a `cleanup` object naming what was created, what was removed, and
whether any package is still dirty.

## `anim_blueprint_patch`

Replaces the generated contents of an Animation Blueprint that already exists.

### Convergent

The builder clears the generated AnimGraph before repopulating it, so running the
same spec twice leaves the same states and transitions rather than a second state
machine beside the first. Compare `verification.actual_states` and
`verification.actual_transitions` to confirm that.

`structure_hash_sha1` is **not** promised to be stable across a rerun, because
node identity in `puerts_anim_blueprint_inspect` is derived from each node's
UObject name and a clear-and-rebuild reassigns those.

### Precondition: the asset must be saved and clean

This is a refusal, not a warning. The rollback boundary here is the `.uasset` on
disk, so an asset with in-memory edits has no restore source that represents them
and the command declines rather than silently discard them. Save it and call
again.

### Failure-atomic

Nothing is written to disk until the compile and the
`puerts_anim_blueprint_inspect` read-back have both passed. On any failure the
transaction is cancelled, the package is reloaded from disk, and the asset is read
back again to decide `rollback_succeeded` rather than assert it. Because the file
is never written on a failure path, even a restore that fails leaves a recoverable
asset: reopening the editor gets the original back.

### Cost worth knowing

Restoring from disk clears the editor's undo history, because the undo records
point at objects the reload destroys. The response reports that in
`restore.undo_history_cleared`.

## Known fixed bugs

Both `FAnimBPAnimGraphBuilder::FindAnimGraph` and the
`puerts_anim_blueprint_inspect` read-back path once compared
`Graph->Schema->GetClass()->GetName()` instead of `Graph->Schema->GetName()`,
always yielding `"Class"` since `Schema` is already a
`TSubclassOf<UEdGraphSchema>`. Fixed and re-verified live 2026-08-05.
