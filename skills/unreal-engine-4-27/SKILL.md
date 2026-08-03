---
name: unreal-engine-4-27
description: "Drive a live Unreal Engine 4.27 editor through the UE4_Bridge MCP server (unreal-bridge) using its native puerts_* tools over an authenticated local named pipe. Supports Unreal Engine 4.27 ONLY; it must not run for any other Unreal version (not 4.26, not 5.x). Trigger when working in an Unreal Engine 4.27 project, when a .uproject has EngineAssociation 4.27, or for UE4 editor automation: actors, assets, Blueprints, widgets, materials, levels, Sequencer, Behavior Trees, Anim Blueprints, navigation, physics, PIE, or editor scripting in UE4.27. If the project engine version is not 4.27, STOP and tell the user this bridge supports Unreal Engine 4.27 only. Skip for conceptual questions, Unity, Godot, or non-Unreal uses of blueprint/sequencer/widget."
license: MIT
---

# Unreal Engine 4.27 Bridge Skill

You drive a live Unreal Engine 4.27 editor through the `unreal-bridge` MCP
server in this repository. Every editor operation goes through a `puerts_*`
tool, which reaches the editor over an authenticated local named pipe into the
in-process PuerTS runtime and then onto the UE4 game thread.

This skill supports **Unreal Engine 4.27 only**. If the project is 4.26, 5.x,
or unknown, stop and say so.

## Transport rule, not a preference

1. Use only `puerts_*` tools for Unreal Editor operations.
2. Never use HTTP, REST, local web servers, Remote Control, Python sockets,
   shell commands, or workaround scripts to talk to UE4.
3. If a native tool fails, report its exact error. Do not switch transports.
4. `engine_source_*` tools are fine: they read engine source from local disk
   and never contact the editor. They work with the editor closed.

A missing capability is a bridge gap to fix, not a reason to reach for another
transport. See "When a tool does not exist" below.

The editor executables are `UE4Editor` and `UE4Editor-Cmd`.

## Start every session with these steps, in order

1. **Verify the project is Unreal Engine 4.27.** Run
   `python Scripts/ue427.py doctor`, or
   `python skills/unreal-engine-4-27/scripts/verify_project_version.py <project>`.
   It reads `EngineAssociation` from the `.uproject`, falls back to
   `Engine/Build/Build.version` for source builds, and requires major 4,
   minor 27. Anything else: **stop immediately** and tell the user. Do not
   run any editor operation.
2. **Check bridge health.** Call `puerts_diagnostic`. It proves the PuerTS
   context, the game thread, the named-pipe transport, and actor-query timing
   in one call. If it fails, read `session_error_code` before anything else.
3. **Confirm the editor session matches.** The client targets one editor by
   session and never falls back. A `session_missing` error means no editor is
   open for the configured project, or `MCP_UNREAL_PROJECT_ROOT` points at a
   different project than the one that is running. `Scripts/ue427.py doctor`
   diagnoses this directly.
4. **Inspect before you mutate.** Read state first: `puerts_scene_inspect`,
   `puerts_find_actors`, `puerts_graph_inspect`, `puerts_read_property`, or
   the matching `*_inspect` tool for the asset type. Never write blind.
5. **Save a recovery point before a bulk change.** Call `puerts_save` so disk
   state is current before a multi-asset mutation. Before anything
   destructive, make a source control checkpoint.
6. **Run mutations sequentially.** Tools execute on the game thread. Never
   issue editor calls in parallel, even when they look independent.
7. **Check every result before the next call.** Each tool returns
   `success`, plus `errors`, `warnings`, `changed_assets`, `changed_actors`,
   and `transaction_id`. Anything that is not an explicit success is a stop.
8. **Save after a verified success.** Call `puerts_save` when the sequence
   completed and you have inspected the result.

## Prefer one batch over many round trips

The product goal is prompt to working feature speed, measured in editor round
trips. Plan in one pass, send one desired-state call, then verify with an
independent inspector:

- `puerts_blueprint_build` then `puerts_graph_inspect`
- `puerts_behavior_tree_build` then `puerts_behavior_tree_inspect`
- `puerts_scene_batch` instead of a loop of `puerts_spawn_actor`
- `puerts_widget_build` then `puerts_widget_inspect`

Do not assemble a feature from hundreds of low level calls when one batch or
upsert expresses the same intent. If you find yourself repeating the same
sequence, that is a signal to promote it into a batch command, not to keep
looping.

## Tool catalog

`references/tool-catalog.md` lists every tool with its real safety
classification taken from `mcp-server/src/annotations.ts`. Summary:

**Read-only** (safe to call unprompted): `puerts_diagnostic`,
`puerts_find_actors`, `puerts_find_assets`, `puerts_read_property`,
`puerts_get_logs`, `puerts_scene_inspect`, `puerts_graph_inspect`,
`puerts_material_inspect`, `puerts_widget_inspect`,
`puerts_behavior_tree_inspect`, `puerts_blackboard_inspect`,
`puerts_anim_blueprint_inspect`, `puerts_anim_montage_inspect`,
`puerts_anim_blend_space_inspect`, `puerts_sequence_inspect`,
`puerts_nav_inspect`, `puerts_nav_query`, `puerts_eqs_inspect`,
`puerts_ai_controller_inspect`, `puerts_audio_inspect`,
`puerts_cloth_inspect`, `puerts_physics_observe`,
`puerts_input_mapping_info`, `puerts_job_status`, plus `engine_source_search`
and `engine_source_read`.

**Mutating**: `puerts_spawn_actor`, `puerts_set_property`,
`puerts_call_function`, `puerts_blueprint_graph_patch`,
`puerts_class_defaults_patch`, `puerts_material_instance_build`,
`puerts_texture_import`, `puerts_nav_build`, `puerts_lighting_build`,
`puerts_sequence_build`, `puerts_widget_bind`, `puerts_physics_build`,
`puerts_sky_shader_create`, `puerts_viewport_screenshot`.

**Destructive** (overwrites or removes state; checkpoint first):
`puerts_save`, `puerts_undo`, `puerts_delete_actor`, `puerts_scene_batch`,
`puerts_blueprint_build`, `puerts_widget_build`,
`puerts_blueprint_member_patch`, `puerts_material_build`,
`puerts_blackboard_build`, `puerts_ai_perception_build`,
`puerts_anim_blueprint_patch`, `puerts_input_mapping_patch`,
`puerts_sequence_render_start`, `puerts_job_cancel`.

Build tools are destructive because they replace an existing asset definition
wholesale. That is expected; it is why you inspect first and checkpoint before
bulk work.

## Play In Editor

Editor-only tools behave differently or refuse while PIE runs. Stop PIE before
editor state changes.

When an authoring job is done, stop after lightweight editor-side checks and
let the user test. Fine unprompted: `puerts_find_actors`, `puerts_scene_inspect`,
`puerts_graph_inspect`, `puerts_viewport_screenshot`. Ask first:
`puerts_pie_start`, `puerts_pie_stop`, and the `puerts_pie_agent_*` tools.

## Visual feedback

After a spatial operation (`puerts_spawn_actor`, `puerts_scene_batch`,
transform changes), take a `puerts_viewport_screenshot` and look at it. This is
default behavior, not an optional extra.

## Long running work

`puerts_lighting_build`, `puerts_nav_build`, and
`puerts_sequence_render_start` can return a job handle. Poll `puerts_job_status`,
collect with `puerts_job_result`, and stop with `puerts_job_cancel`. Do not
busy-poll in a tight loop.

## When a tool does not exist

If the operation you need has no `puerts_*` tool, that is a platform gap. Do
not improvise a transport and do not write a feature-specific workaround.
Add the capability to the bridge, following "Adding a new tool" in `AGENTS.md`:
schema in `mcp-server/src/tools/puerts.ts`, execution in
`puerts-runtime/src/registry.ts`, minimum C++ in `MCPBridgePuerTS` when
reflection is not enough, the native allowlist entry, and a classification in
`mcp-server/src/annotations.ts`. Then run `npm run verify`.

One capability per session, landed with evidence, beats three half-landed.

## Failure handling

Read `session_error_code` first when a call fails:

- `session_missing`: no editor advertises a session for the configured
  project. Either no editor is open for it, or `MCP_UNREAL_PROJECT_ROOT`
  points somewhere else. Run `python Scripts/ue427.py doctor`.
- Session identity mismatch: the reply came from a different editor than the
  request targeted. The client refuses it on purpose. Never guess a pipe name.
- Tool-level errors name the asset, graph, node, and pin, plus the rollback
  result. Read them and fix the input; do not retry the same call unchanged.

If the editor game thread is busy or blocked, requests can look like hangs
rather than refusals. Wait, then check `puerts_get_logs`.

Any project-state query discrepancy (wrong actor count, an empty result that
should not be empty) is a tracked Unknown. Record it in
`docs/CAPABILITY_FINDINGS.md` immediately. A tool that controls Unreal must be
able to trust its own state queries.

## Playbooks

Before any structural engine or blueprint generation task, read
`docs/playbooks/` for an existing recipe and follow it instead of re-deriving
the solution. When you materially change a documented system, updating its
playbook is part of finishing the work.

## References

- `references/setup.md`: installing the skill and MCP config for each agent,
  the `ue427` commands, launching the editor.
- `references/operations.md`: session model, transactions, batching, job
  handles, UE4.27 API safety.
- `references/tool-catalog.md`: every tool with its safety classification.
- `references/security.md`: what the bridge exposes and the boundaries.
- `references/troubleshooting.md`: symptom table for the failures that
  actually happen.
