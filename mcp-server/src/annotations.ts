/**
 * MCP tool annotations for every registered tool.
 *
 * These are advisory hints for MCP clients (Codex uses them for approval
 * heuristics; Claude Code mostly ignores them). They are NOT a permission
 * boundary: per the MCP spec, clients must not treat them as trusted.
 *
 * Classification rules:
 * - readOnlyHint: the tool cannot change editor, project, or disk state.
 * - destructiveHint: the tool can remove or overwrite existing state in a
 *   way a plain undo may not recover (deletes, restores, arbitrary code,
 *   config changes, log erasure). Only meaningful when readOnlyHint=false;
 *   the spec DEFAULTS this to true, so mutating-but-additive tools must set
 *   it false explicitly.
 * - idempotentHint: calling twice with the same args has no additional
 *   effect beyond the first call.
 * - openWorldHint: whether the tool reaches outside the local editor and
 *   project. Everything here talks to the local UE4 instance or local disk,
 *   so this is false across the board (spec default is true).
 *
 * This map is intentionally central so a reviewer can audit the whole
 * read-only vs mutating vs destructive surface in one file. index.ts warns
 * at startup about any registered tool missing from this map; keep it in
 * sync when adding tools (step 6.5 of the Adding a New Tool checklist).
 */

import type { ToolAnnotations } from "@modelcontextprotocol/sdk/types.js";

export const readOnly: ToolAnnotations = {
  readOnlyHint: true,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false,
};

/** Mutates editor state additively; a repeat call adds more (new actor,
    new node), so not idempotent. Undoable via the UE4 transaction system. */
export const mutating: ToolAnnotations = {
  readOnlyHint: false,
  destructiveHint: false,
  idempotentHint: false,
  openWorldHint: false,
};

/** Mutates state but converges: same args, same end state. */
export const mutatingIdempotent: ToolAnnotations = {
  readOnlyHint: false,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false,
};

/** Removes or overwrites existing state (deletes, restores, arbitrary
    code execution, config changes that need editor restarts). */
export const destructive: ToolAnnotations = {
  readOnlyHint: false,
  destructiveHint: true,
  idempotentHint: false,
  openWorldHint: false,
};

/** Destructive but convergent (deleting the same thing twice is a no-op). */
export const destructiveIdempotent: ToolAnnotations = {
  readOnlyHint: false,
  destructiveHint: true,
  idempotentHint: true,
  openWorldHint: false,
};

export const toolAnnotations: Record<string, ToolAnnotations> = {
  // --- PuerTS native named-pipe lane ---------------------------------------
  puerts_diagnostic: readOnly,
  puerts_find_assets: readOnly,
  puerts_find_actors: readOnly,
  puerts_read_property: readOnly,
  puerts_get_logs: readOnly,
  puerts_physics_observe: readOnly,
  // The inverse of puerts_blueprint_build, and the one Blueprint command that
  // is genuinely read-only: the native side keeps it out of IsToolMutating so
  // no transaction is opened, and it reports the package's dirty flag before
  // and after the read so the claim is checkable rather than asserted.
  puerts_graph_inspect: readOnly,
  puerts_viewport_screenshot: mutatingIdempotent,
  puerts_set_property: mutatingIdempotent,
  puerts_call_function: mutating,
  // Converges on rerun: the tree's root is replaced only on full success and
  // existing blackboard keys are left alone.
  puerts_behavior_tree_build: mutatingIdempotent,
  puerts_spawn_actor: mutating,
  puerts_sky_shader_create: mutating,
  puerts_physics_build: mutating,
  puerts_pie_start: mutatingIdempotent,
  puerts_pie_stop: mutatingIdempotent,
  // Converges on a rerun, but clear_existing_graph defaults to true, so a
  // build aimed at an existing Blueprint replaces its event graph.
  puerts_blueprint_build: destructiveIdempotent,
  // Converges on a rerun, and the spec is the whole widget tree, so a build
  // aimed at an existing Widget Blueprint replaces the hierarchy it had.
  puerts_widget_build: destructiveIdempotent,
  puerts_delete_actor: destructiveIdempotent,
  puerts_save: destructive,
  puerts_undo: destructive,
  // --- Performance analysis and optimization -------------------------------
  // Audits and catalogs are pure reads. Captures drive the viewport and write
  // screenshots or reports under Saved/, so they are mutating-but-convergent
  // rather than read-only: no project content changes, but there are side
  // effects on disk. Applying fixes writes assets and is destructive.
  optimization_tool_catalog: readOnly,
  optimization_asset_audit: readOnly,
  optimization_scene_audit: readOnly,
  optimization_vr_audit: readOnly,
  optimization_texture_harm_rank: readOnly,
  optimization_asset_cost_rank: readOnly,
  optimization_fix_candidates: readOnly,
  optimization_insights_trace_summary: readOnly,
  optimization_visual_logger_ingest: readOnly,

  optimization_quick_triage: mutatingIdempotent,
  optimization_run_console_checklist: mutatingIdempotent,
  optimization_gpu_capture: mutatingIdempotent,
  optimization_profilegpu_capture: mutatingIdempotent,
  optimization_rendering_capture: mutatingIdempotent,
  optimization_viewmode_capture_set: mutatingIdempotent,
  optimization_cpu_capture: mutatingIdempotent,
  optimization_memory_streaming_capture: mutatingIdempotent,
  optimization_camera_path_profile: mutatingIdempotent,
  optimization_stat_capture: mutatingIdempotent,
  optimization_generate_report: mutatingIdempotent,
  optimization_insights_trace_start: mutatingIdempotent,
  optimization_insights_trace_stop: mutatingIdempotent,

  // Writes asset changes; save_assets puts them beyond a plain editor undo.
  optimization_apply_low_risk_fixes: destructive,
  // Only mutates when apply_candidates is set, but the schema allows it.
  optimization_before_after_verify: destructive,

  // --- Animation pose analysis: reads are pure, re-anchoring rewrites assets ---
  anim_pose_snapshot: readOnly,
  anim_pose_delta: readOnly,
  anim_root_motion_analyze: readOnly,
  // Re-anchoring overwrites keyframes on the source AnimSequence. The write is
  // convergent (re-anchoring an already-anchored clip is a no-op) but the
  // original curve is not recoverable by a plain editor undo.
  anim_reanchor: destructiveIdempotent,
  anim_batch_reanchor: destructiveIdempotent,

  // Re-reads the tool registry and re-advertises it. Touches no project state.
  refresh_tools: readOnly,

  // --- Pure reads: queries, inspection, docs, diagnostics ---
  asset_info: readOnly,
  asset_list: readOnly,
  asset_load_diagnostics: readOnly,
  actor_selection: readOnly,
  blueprint_document: readOnly,
  blueprint_info: readOnly,
  blueprint_inspect: readOnly,
  blueprint_list: readOnly,
  cloth_inspect_asset: readOnly,
  cloth_apply_fabric_profile: mutatingIdempotent,
  cloth_smooth_max_distance: mutatingIdempotent,
  cloth_apply_lower_leg_gradient: mutatingIdempotent,
  bridge_command_manifest: readOnly,
  engine_source_read: readOnly,
  engine_source_search: readOnly,
  gameplay_pattern_search: readOnly,
  gameplay_telemetry_snapshot: readOnly,
  gameplay_pie_status: readOnly,
  pie_agent_observe: readOnly,
  pie_agent_status: readOnly,
  help: readOnly,
  history_list: readOnly,
  input_mapping_info: readOnly,
  job_list: readOnly,
  job_status: readOnly,
  level_actors: readOnly,
  level_outliner: readOnly,
  material_info: readOnly,
  material_list: readOnly,
  placement_validate: readOnly,
  project_index_query: readOnly,
  project_info: readOnly,
  project_semantic_diff: readOnly,
  prompt_spec_list: readOnly,
  prompt_status: readOnly,
  test_connection: readOnly,
  title_manifest_validate: readOnly,
  ue_logs: readOnly,
  viewport_bounds: readOnly,

  // --- Viewport: changes camera/view state only, never content ---
  viewport_camera: mutatingIdempotent,
  viewport_fit: mutatingIdempotent,
  viewport_focus: mutatingIdempotent,
  viewport_look_at: mutatingIdempotent,
  viewport_mode: mutatingIdempotent,
  viewport_render_mode: mutatingIdempotent,
  viewport_screenshot: mutatingIdempotent, // writes an image file, touches nothing in the project
  title_render_compare: mutatingIdempotent, // drives the viewport to capture comparison shots

  // --- Additive content creation / modification (transaction-undoable) ---
  actor_duplicate: mutating,
  actor_modify: mutating,
  actor_organize: mutating,
  actor_snap_to_socket: mutating,
  actor_spawn: mutating,
  anim_blueprint_build_from_json: mutating,
  audio_component_add: mutating,
  batch_spawn: mutating,
  behavior_tree_create: mutating,
  blackboard_create: mutating,
  blueprint_add_event_dispatcher: mutating,
  blueprint_add_function: mutating,
  blueprint_add_interface: mutating,
  blueprint_add_variable: mutating,
  blueprint_build_from_description: mutating,
  blueprint_build_from_json: mutating,
  blueprint_component_rename: mutating,
  blueprint_create: mutating,
  blueprint_node_add: mutating,
  blueprint_node_move: mutating,
  blueprint_node_set_enabled: mutating,
  blueprint_pins_connect: mutating,
  blueprint_set_variable_default: mutating,
  camera_rig_create: mutating,
  camera_shake_blueprint: mutating,
  camera_shake_play: mutating, // runtime-only effect, nothing persisted
  camera_shake_spawn: mutating,
  camera_shake_trigger: mutating,
  checkpoint_create: mutating,
  console_effect: mutating, // runtime-only console command effect
  cpp_class_create: mutating, // writes new source files, never overwrites existing ones
  data_table_create: mutating,
  data_table_fill_from_json: mutating,
  game_template_create: mutating,
  gameplay_framework_create: mutating,
  input_mapping_add: mutating,
  input_preset_apply: mutating,
  material_apply: mutating,
  material_create: mutating,
  material_instance_create: mutating,
  material_instance_set_params: mutating,
  pp_preset: mutating,
  pp_volume_modify: mutating,
  pp_volume_spawn: mutating,
  prompt_generate: mutating, // generates whole systems, but only creates new assets
  title_controller_create: mutating,
  title_manifest_adjust: mutating,
  title_manifest_create: mutating,
  title_manifest_from_reference: mutating,
  title_sequence_bind: mutating,
  title_widget_build_from_manifest: mutating,
  widget_build_from_json: mutating,
  widget_lower_third_create: mutating,
  widget_title_card_create: mutating,
  widget_title_template: mutating,

  // --- Mutating but convergent ---
  ai_nav_rebuild: mutatingIdempotent,
  asset_save_many: mutatingIdempotent,
  blueprint_compile: mutatingIdempotent,
  cpp_build: mutatingIdempotent, // UBT incremental build; repeat = up to date
  gameplay_pie_start: mutatingIdempotent, // session state only
  gameplay_pie_stop: mutatingIdempotent,
  gameplay_run_acceptance_tests: mutatingIdempotent, // runs PIE-side checks, persists nothing
  pie_agent_look_at: mutatingIdempotent,
  pie_agent_move_to: mutatingIdempotent,
  pie_agent_record_start: mutatingIdempotent,
  pie_agent_record_stop: mutatingIdempotent,
  pie_agent_expect: readOnly,
  pie_agent_press: mutating,
  pie_agent_replay: mutating,
  level_save: mutatingIdempotent,
  // Folder visibility: display-only Content Browser state, ini-persisted,
  // fully reversible via folder_show with no args.
  folder_hide: mutatingIdempotent,
  folder_show: mutatingIdempotent,
  folder_hidden_list: readOnly,
  project_index_rebuild: mutatingIdempotent,
  project_settings_maps: mutatingIdempotent, // writes DefaultEngine.ini map entries

  // --- Destructive: removes or overwrites existing state ---
  batch_operations: destructive, // batch can contain deletes/modifies of existing actors
  blueprint_component_remove: destructiveIdempotent,
  blueprint_node_delete: destructiveIdempotent,
  blueprint_pins_break: destructiveIdempotent,
  blueprint_remove_event_dispatcher: destructiveIdempotent,
  blueprint_remove_function: destructiveIdempotent,
  blueprint_remove_interface: destructiveIdempotent,
  blueprint_remove_variable: destructiveIdempotent,
  actor_delete: destructiveIdempotent,
  input_mapping_remove: destructiveIdempotent,
  bridge_clear_log: destructiveIdempotent,
  clear_output_log: destructiveIdempotent,
  checkpoint_restore: destructive, // discards everything after the checkpoint
  job_cancel: destructiveIdempotent, // kills a running build job
  level_new: destructive, // switches level; unsaved work in the current one is at risk
  project_enable_plugins: destructive, // edits the .uproject, requires editor restart
  python_proxy: destructive, // arbitrary code with full unreal-module access
  redo: destructive,
  restart_listener: destructive, // drops in-flight bridge state
  undo: destructive, // discards current state by design
};

/**
 * Compatibility aliases (src/tools/compat.ts, registered only under
 * MCP_COMPAT_ALIASES=1).
 *
 * These reuse the legacy public names, so they cannot live in the map above:
 * a name has exactly one entry there and it belongs to the legacy HTTP tool.
 * An alias executes the native tool it fronts, so it must carry that tool's
 * classification, not the legacy one. Three differ from their legacy twins:
 * actor_modify (the native writer is convergent), and level_save /
 * asset_save_many (a native save puts changes beyond a plain editor undo).
 *
 * compat.ts attaches these per-tool, and index.ts prefers a per-tool
 * annotation over the central map, so the right hints reach the client
 * whichever lane answers the name.
 */
export const compatAliasAnnotations: Record<string, ToolAnnotations> = {
  actor_spawn: mutating,                  // -> puerts_spawn_actor
  actor_delete: destructiveIdempotent,    // -> puerts_delete_actor
  actor_modify: mutatingIdempotent,       // -> puerts_set_property
  level_actors: readOnly,                 // -> puerts_find_actors
  level_save: destructive,                // -> puerts_save
  asset_save_many: destructive,           // -> puerts_save
  asset_list: readOnly,                   // -> puerts_find_assets
  viewport_screenshot: mutatingIdempotent, // -> puerts_viewport_screenshot
  gameplay_pie_start: mutatingIdempotent, // -> puerts_pie_start
  gameplay_pie_stop: mutatingIdempotent,  // -> puerts_pie_stop
  ue_logs: readOnly,                      // -> puerts_get_logs
  undo: destructive,                      // -> puerts_undo
};
