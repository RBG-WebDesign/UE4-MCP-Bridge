# Operations

How the bridge behaves once it is connected, and the rules that keep results
trustworthy.

## The session model

The client addresses exactly one editor, by session, and never falls back.

Each editor writes `Saved/MCPPuerTSBridge/session.json` in its project,
containing the session id, a nonce, the process id and creation time, the
project path, the pipe name, a heartbeat, and the shutdown state. The file is
published by moving a staged file into place, so a reader never sees a half
written manifest.

Every request carries the nonce and the editor refuses a mismatch. Every
response carries the editor identity and the client refuses a reply that came
from somewhere else. If no session is advertised, the client returns a
structured refusal instead of guessing a pipe name.

That refusal is the feature. With two editors open, guessing means authoring
assets in the wrong project and reporting success.

`MCP_UNREAL_PROJECT_ROOT` selects the target project. `MCP_PUERTS_SESSION_ID`
pins one session exactly.

When a call fails with `session_missing`, the usual cause is that
`MCP_UNREAL_PROJECT_ROOT` points at a different project than the editor that
is actually running. `python Scripts/ue427.py doctor` compares the two and
names both paths.

## Transactions

Every tool that modifies editor state is wrapped in a UE4 transaction, and
the result carries a `transaction_id`. `puerts_undo` rolls back.

Viewport operations are not transactable. Camera moves, mode switches, and
render mode changes are not wrapped, by design. Do not expect to undo them.

A failed mutator rolls back rather than leaving partial state. The error
names the asset, graph, node, and pin, plus the rollback result. Read it
before retrying, and change the input rather than repeating the same call.

## Reading results

Every response has the same shape:

- `success`: the only value that means the operation happened.
- `errors`, `warnings`: read both. A warning often explains a silent no-op.
- `changed_assets`, `changed_actors`: what actually moved. Compare this
  against what you intended. An empty list on a call you expected to change
  something is a finding, not a pass.
- `transaction_id`: what `puerts_undo` would roll back.
- `log_output`: editor log lines captured for the call.

An empty success is worse than an error, because the caller cannot tell it
from an empty level. Treat a suspicious empty result as a tracked Unknown and
record it in `docs/CAPABILITY_FINDINGS.md`.

## Batching and desired state

Prefer one desired-state call over many small ones. The fast path is: plan,
send one batch, run it in a transaction, read it back with an independent
inspector, verify, return.

- `puerts_scene_batch` instead of a loop of `puerts_spawn_actor`
- `puerts_blueprint_build` then `puerts_graph_inspect` to confirm
- `puerts_behavior_tree_build` then `puerts_behavior_tree_inspect`

Each major builder has an inspector with the same canonical shape, so desired
state can be compared against actual state without opening the editor.

## Long running jobs

`puerts_lighting_build`, `puerts_nav_build`, and
`puerts_sequence_render_start` can return a job handle instead of blocking.

- `puerts_job_status` reports progress.
- `puerts_job_result` collects the finished result.
- `puerts_job_cancel` stops the work. It is destructive: stopping part way
  can leave partial output.

Poll at a sensible interval. Do not spin.

## Play In Editor

UE4.27 editor scripting does not raise during PIE. It logs that the editor is
in play mode and returns nothing, so an unguarded call can report success with
zero actors while a full level is playing. The bridge refuses editor-only
commands during PIE with a message naming the command.

Stop PIE before editor state changes. Ask the user before starting PIE, and
before any `puerts_pie_agent_*` tool.

## Threading

Tools execute on the UE4 game thread. Issue them sequentially. Parallel calls
deadlock or fail in confusing ways, even when they look independent.

If the game thread is busy compiling shaders, loading a level, or showing a
modal dialog, requests look like hangs rather than refusals. Wait, then read
`puerts_get_logs`.

## UE4.27 API safety

This is a 4.27 project. If an API exists in a later engine but is not
confirmed in 4.27, do not use it.

| Not available here | Use instead | System |
|---|---|---|
| `EnhancedInputComponent` | `InputComponent` | Input |
| `EnhancedInputSubsystem` | `BindAxis` / `BindAction` | Input |
| `UE::Tasks`, `Tasks::Launch` | `FAsyncTask`, `FTimerManager`, `SetTimer` | Async |
| `MassAI` | `BehaviorTree` plus `AIController` | AI |
| `SmartObjects` | manual triggers, overlap volumes | AI |
| `StateTree` | `BehaviorTree` | AI |
| `AnimNext` | `UAnimInstance`, `Montage_Play` | Animation |
| `LevelEditorSubsystem` | `GEditor` directly | Editor |
| `EditorUtilitySubsystem` | `FKismetEditorUtilities` | Editor |
| `EditorPlaySessionSubsystem` | `GEditor->RequestPlaySession` | Play |

Camera shakes on this 4.27.2 build use the transitional API:
`UCameraShakeBase` from `Camera/CameraShakeBase.h`, started with
`StartCameraShake()` on `APlayerCameraManager`. The older `UCameraShake` and
`PlayCameraShake` names do not exist here.

Verify a call before writing it. Use the `unreal-api` MCP server for
reflected types, and `engine_source_search` / `engine_source_read` for
non-reflected C++ such as Slate widgets, `FRunnable`, and macros. Both work
with the editor closed.

## Behavior Trees

UE4.27 protects `RootNode`, `BlackboardAsset`, root decorators, and decorator
operations from Python. Use `puerts_behavior_tree_inspect` and
`puerts_behavior_tree_build` rather than raw reflection, and never hand-edit
a `.uasset`.

## Trigger volumes

`OnBeginOverlap` fires only on an outside to inside transition, so a player
who spawns already inside a volume never fires it. Never place a trigger
volume on a PlayerStart. Keep at least 1.5 times the volume extent away, and
query the PlayerStart position before spawning.
