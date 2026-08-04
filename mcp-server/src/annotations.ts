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
  // Permanent package deletion. Idempotent because an already absent asset is
  // a successful no-op; force=true may also null references in other packages.
  puerts_delete_asset: destructiveIdempotent,
  puerts_find_actors: readOnly,
  puerts_read_property: readOnly,
  puerts_get_logs: readOnly,
  puerts_physics_observe: readOnly,
  // The inverse of puerts_blueprint_build, and the one Blueprint command that
  // is genuinely read-only: the native side keeps it out of IsToolMutating so
  // no transaction is opened, and it reports the package's dirty flag before
  // and after the read so the claim is checkable rather than asserted.
  puerts_graph_inspect: readOnly,
  // The read half of puerts_behavior_tree_build: kept out of IsToolMutating on
  // the native side, reports the package dirty flag before and after the read.
  puerts_behavior_tree_inspect: readOnly,
  // The read half of puerts_widget_build: kept out of IsToolMutating on the
  // native side, reports the package dirty flag before and after the read.
  puerts_widget_inspect: readOnly,
  // The read half of puerts_anim_blueprint_build, and the reason that command
  // can verify rather than assert. Kept out of IsToolMutating on the native
  // side; reports the package dirty flag before and after the read.
  puerts_anim_blueprint_inspect: readOnly,
  // Read-only with no write counterpart, and that is a finding rather than an
  // omission: UE4.27 exposes no atomic way to rebuild a montage's section chain
  // or re-link its notifies, so a montage writer could not be failure-atomic.
  puerts_anim_montage_inspect: readOnly,
  // Read-only for the same shape of reason as the montage reader: UE4.27
  // rebuilds a blend space's triangulation from its sample set, so there is no
  // atomic sample-set replacement a writer could be failure-atomic around.
  puerts_anim_blend_space_inspect: readOnly,
  // Mutating, NOT idempotent, and not destructive. The distinction is the whole
  // shape of the command: it creates a new Animation Blueprint and REFUSES an
  // asset that already exists, because the UE4.27 builder's rebuild path clears
  // nothing and would append a second state machine rather than converge. So a
  // rerun changes nothing (idempotentHint would be a fair reading of that), but
  // it also fails, and a caller that reruns expecting a no-op success would be
  // misled. Nothing is ever destroyed: on failure the creation is rolled back,
  // and an existing asset is never touched at all.
  puerts_anim_blueprint_build: mutating,
  // Destructive and idempotent, and both halves are deliberate. Destructive
  // because it CLEARS the generated AnimGraph before repopulating it, so any
  // hand-authored node in that graph is gone whether or not the spec mentions
  // it; this is the opposite of puerts_blueprint_graph_patch, which clears
  // nothing on purpose. Idempotent because the clear is what makes a rerun
  // converge instead of stacking a second state machine beside the first.
  // The destruction is recoverable: the command refuses unless the asset is
  // saved and clean, and it restores from that on-disk copy if any step fails.
  puerts_anim_blueprint_patch: destructiveIdempotent,
  // The read half of puerts_blackboard_build: kept out of IsToolMutating on
  // the native side, reports the package dirty flag before and after the read.
  puerts_blackboard_inspect: readOnly,
  // No build half exists, so this is read-only by construction rather than by
  // discipline. See the tool description for why there is no eqs_build.
  puerts_eqs_inspect: readOnly,
  // Both navigation tools read the editor world. Nothing is spawned, no navmesh
  // is rebuilt, and no package is dirtied: unlike a viewport capture, there is
  // not even a file written, so these are readOnly rather than
  // mutatingIdempotent.
  puerts_nav_inspect: readOnly,
  puerts_nav_query: readOnly,
  // Mutating and idempotent, and not destructive. Idempotent because a rebuild
  // of an already-current navmesh converges on the same navmesh: it costs the
  // work again, but the world it leaves is the same one. Not destructive
  // because navigation data is DERIVED from the level, so nothing authored can
  // be lost; the recovery from a bad build is another build. It is outside
  // IsToolMutating on the native side and that is deliberate, not an
  // oversight: the editor's own Build Paths resets the transaction buffer
  // before running the same build, so a transaction here would record an undo
  // entry that restores nothing.
  puerts_nav_build: mutatingIdempotent,
  // The read half of puerts_ai_perception_build, plus the RunBehaviorTree call
  // sites. Same read-only contract as the other inspectors.
  puerts_ai_controller_inspect: readOnly,
  // The read half of material authoring, and for master material graphs the
  // ONLY half: UE4.27's graph mutators write outside the undo record, so a
  // failed multi-node build cannot be rolled back and no write command was
  // shipped. Kept out of IsToolMutating on the native side, reports the package
  // dirty flag before and after the read.
  puerts_material_inspect: readOnly,
  // The first native audio command, and read-only by construction rather than
  // by discipline: there is no audio_build for it to be the read half of. Kept
  // out of IsToolMutating on the native side, reports the package dirty flag
  // before and after the read. See the tool description for why a cue builder
  // is feasible where an EQS builder is not, and what still blocks it.
  puerts_audio_build: destructiveIdempotent,
  puerts_audio_inspect: readOnly,
  // The read half of MCPBridgeClothOptimizer, and the only half fronted. Its
  // three writers are not here on purpose: they do not cancel their transaction
  // on failure and they mutate cloth paint, which is authored data a re-run
  // cannot regenerate. Absent from IsToolMutating on the native side.
  puerts_cloth_inspect: readOnly,
  // The read half of puerts_scene_batch: kept out of IsToolMutating on the
  // native side, reports the level package's dirty flag before and after.
  puerts_scene_inspect: readOnly,
  // The read half of puerts_sequence_build: absent from IsToolMutating on the
  // native side, so no transaction opens, and it reports the sequence package's
  // dirty flag before and after the read.
  puerts_sequence_inspect: readOnly,
  puerts_viewport_screenshot: mutatingIdempotent,
  puerts_set_property: mutatingIdempotent,
  puerts_call_function: mutating,
  // Converges on rerun: the tree's root is replaced only on full success and
  // existing blackboard keys are left alone.
  puerts_behavior_tree_build: mutatingIdempotent,
  // Converges: a rerun that finds everything in place returns before the
  // mutation section and does not save. Destructive because the schema allows
  // it: remove_unlisted deletes keys, and a deleted key silently dangles every
  // FBlackboardKeySelector in every Behavior Tree that bound to it, which no
  // editor undo of this asset restores. Off by default, but the classification
  // has to cover what the tool can do, not what it usually does.
  puerts_blackboard_build: destructiveIdempotent,
  // Same shape and same reason: converges, and remove_unlisted deletes sense
  // configs. A listed sense is also replaced wholesale rather than patched, so
  // a partial spec overwrites the config that was there.
  puerts_ai_perception_build: destructiveIdempotent,
  // Mutating and idempotent, and NOT destructive by default: it writes only the
  // parameters it was given and leaves every other override alone. A parameter
  // already at the requested value is reported unchanged and not rewritten, so
  // a rerun dirties nothing. clear_unlisted turns the spec into the whole
  // desired state and does drop unnamed overrides, but it is opt-in and off by
  // default, which is why the classification here is the non-destructive one.
  puerts_material_instance_build: mutatingIdempotent,
  // Converges on a rerun, and DESTRUCTIVE for the same reason blueprint_build
  // and widget_build are: the spec is the whole graph, so a build aimed at an
  // existing material replaces the nodes and links it had. That replacement is
  // not opt-in and cannot be, because it is what makes the operation
  // desired-state and what makes one Modify() enough to roll the build back.
  puerts_material_build: destructiveIdempotent,
  // Mutating and idempotent, NOT destructive: it writes one texture asset and
  // touches nothing else, and a spec that already matches is reported unchanged
  // and not rewritten.
  puerts_texture_import: mutatingIdempotent,
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
  // Mutating and idempotent, and NOT destructive by default: it adds the
  // bindings it was given, exposes the widgets it was given, and leaves every
  // other binding and variable flag alone. remove_unlisted does take away
  // bindings and can clear a variable flag that graph nodes depend on, but it is
  // opt-in, off by default, and applies only inside the sections the request
  // stated. A rerun of the same spec applies nothing, compiles nothing and does
  // not save, which is the idempotent half.
  puerts_widget_bind: mutatingIdempotent,
  // Mutating and idempotent, but NOT destructive, and that distinction is the
  // reason the command exists: it clears nothing, names the nodes it touches,
  // and leaves everything it was not asked about alone. A rerun of the same
  // patch applies nothing and does not save.
  puerts_blueprint_graph_patch: mutatingIdempotent,
  // Destructive, unlike the graph patch beside it, and the difference is not
  // that this command clears more. It is that removing a MEMBER takes things
  // the caller did not name with it: FBlueprintEditorUtils::RemoveMemberVariable
  // deletes the graph nodes that referenced the variable, and removing an
  // interface deletes the function graphs that implemented it. Idempotent
  // because a rerun applies nothing and does not save.
  puerts_blueprint_member_patch: destructiveIdempotent,
  // Destructive because delete_actor is one of its two operations and an upsert
  // replaces an actor's whole tag array. Idempotent because it is desired-state
  // throughout: a rerun applies nothing, reports converged, and the level does
  // not get dirtier.
  puerts_scene_batch: destructiveIdempotent,
  // Mutating and idempotent: starting a build that is already needed twice is
  // the same request, and Lightmass has one active build, so a second start is
  // refused rather than queued. Not read-only - it starts real work and the
  // finished build writes the level's map build data - and not destructive: a
  // rebuild replaces lighting data that is itself derived, and nothing a caller
  // authored is lost.
  puerts_lighting_build: mutatingIdempotent,
  // The job API. job_status changes nothing anywhere, so it is plainly
  // read-only.
  puerts_job_status: readOnly,
  // Read-only about the EDITOR - it opens no transaction, dirties nothing and
  // spawns nothing - but deliberately not idempotent: it delivers a finished
  // job's output once and refuses the second call with job_result_consumed. A
  // client that retries on a dropped connection must expect that refusal, so
  // the hint says so rather than promising a safe retry.
  puerts_job_result: {
    readOnlyHint: true,
    destructiveHint: false,
    idempotentHint: false,
    openWorldHint: false,
  },
  // Destructive, and convergent. Destructive because stopping work part way
  // leaves it part way: a cancelled navigation build leaves a partial navmesh,
  // a cancelled render leaves the frames it already wrote, and nothing is
  // rolled back. Idempotent because a second cancel of the same job refuses
  // with job_not_running rather than doing anything again.
  puerts_job_cancel: destructiveIdempotent,
  // Destructive and NOT idempotent. It spawns a second UE process and, with
  // overwrite_existing (the default), overwrites whatever is already in the
  // output directory; a second call starts a second render rather than
  // converging. It touches no asset, so there is nothing for undo to cover.
  puerts_sequence_render_start: destructive,
  // Mutating and idempotent, NOT destructive: it writes only the class defaults
  // it was given, a value already equal to the request is reported unchanged and
  // not rewritten, and nothing is ever cleared. Worth knowing rather than
  // inferring from this classification: editor undo does not cover a CDO write
  // (finding 0r), so "mutating" here means the command's own restore boundary is
  // the way back, not Ctrl+Z.
  puerts_class_defaults_patch: mutatingIdempotent,
  // Mutating and idempotent, and deliberately NOT destructive: it is additive
  // throughout. There is no remove_unlisted, no clear_existing, and a key at the
  // requested time and value is reported unchanged rather than rewritten, so a
  // rerun writes nothing and the package does not get dirtier. The one thing it
  // overwrites is a key that already exists at the same frame on the same
  // channel with a different value, which is what "converge on this spec" means
  // and is covered by the transaction.
  puerts_sequence_build: mutatingIdempotent,
  puerts_delete_actor: destructiveIdempotent,
  puerts_save: destructive,
  // Map switching and package writes are outside editor transactions. Creation
  // refuses existing targets; load refuses dirty packages; save commits to disk.
  puerts_level_create: destructive,
  puerts_level_load: mutating,
  puerts_level_save: destructive,
  puerts_undo: destructive,
  // The missing inspector for UMCPBridgeInputLibrary. Read-only in the same
  // sense the asset inspectors are: kept out of IsToolMutating on the native
  // side, and it calls only const accessors on UInputSettings.
  puerts_input_mapping_info: readOnly,
  // Convergent by construction: a binding already present is reported
  // unchanged, so a rerun writes nothing. Destructive, and the distinction
  // matters here more than for most: remove_unlisted prunes bindings the
  // request did not name, and DefaultInput.ini is not covered by editor undo,
  // so what this removes is recovered from source control or not at all.
  puerts_input_mapping_patch: destructiveIdempotent,
  // Display-only Content Browser state, ini-persisted, and the reverse of any
  // change is another call to the same tool. Same classification the legacy
  // folder_hide / folder_show pair carries.
  puerts_folder_visibility: mutatingIdempotent,
  // Runtime-only PIE effect. Nothing is persisted, so nothing can be lost;
  // not idempotent because playing a shake twice plays it twice.
  puerts_camera_shake: mutating,
  // The read half of the PIE agent: observes a running world, edits nothing.
  // op=expect starts an in-engine condition check that only reads.
  puerts_pie_agent_query: readOnly,
  // Runtime-only and imperative. It changes a PIE world but persists no asset.
  puerts_pie_agent_control: mutating,
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
  // Reads a file the editor wrote. Touches no editor state and no pipe.
  bridge_command_status: readOnly,
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
  blueprint_info: readOnly,                 // -> puerts_graph_inspect
  blueprint_inspect: readOnly,              // -> puerts_graph_inspect
  anim_blueprint_build_from_json: mutating, // -> puerts_anim_blueprint_build
  widget_build_from_json: destructiveIdempotent, // -> puerts_widget_build
  behavior_tree_create: mutatingIdempotent, // -> puerts_behavior_tree_build
  blackboard_create: mutatingIdempotent,    // -> puerts_blackboard_build (no removals)
  viewport_fit: mutatingIdempotent,         // -> puerts_viewport_screenshot
  viewport_focus: mutatingIdempotent,       // -> puerts_viewport_screenshot
  actor_spawn: mutating,                  // -> puerts_spawn_actor
  actor_delete: destructiveIdempotent,    // -> puerts_delete_actor
  actor_modify: mutatingIdempotent,       // -> puerts_set_property
  level_actors: readOnly,                 // -> puerts_find_actors
  level_save: destructive,                // -> puerts_level_save
  level_new: destructive,                 // -> puerts_level_create
  asset_save_many: destructive,           // -> puerts_save
  asset_list: readOnly,                   // -> puerts_find_assets
  viewport_screenshot: mutatingIdempotent, // -> puerts_viewport_screenshot
  gameplay_pie_start: mutatingIdempotent, // -> puerts_pie_start
  gameplay_pie_stop: mutatingIdempotent,  // -> puerts_pie_stop
  ue_logs: readOnly,                      // -> puerts_get_logs
  undo: destructive,                      // -> puerts_undo

  // Input mappings. puerts_input_mapping_patch is destructiveIdempotent
  // because remove_unlisted can prune bindings the request did not name and
  // DefaultInput.ini is outside editor undo. None of these three aliases can
  // reach that: the translations emit one add, one targeted removal, or a
  // preset, and never remove_unlisted. Classified by what the alias can
  // actually cause, which is the point of the hint.
  input_mapping_info: readOnly,           // -> puerts_input_mapping_info
  input_mapping_add: mutatingIdempotent,  // -> puerts_input_mapping_patch (adds only)
  input_mapping_remove: destructiveIdempotent, // -> puerts_input_mapping_patch (named removals)
  input_preset_apply: mutatingIdempotent, // -> puerts_input_mapping_patch (preset, additive)

  // Folder visibility: display-only, ini-persisted, reversible by another
  // call. folder_hidden_list sends no hidden/hide/show, which the native
  // command treats as the read.
  folder_hide: mutatingIdempotent,        // -> puerts_folder_visibility
  folder_show: mutatingIdempotent,        // -> puerts_folder_visibility
  folder_hidden_list: readOnly,           // -> puerts_folder_visibility (read path)

  camera_shake_play: mutating,            // -> puerts_camera_shake (runtime only, nothing persisted)

  cloth_inspect_asset: readOnly,          // -> puerts_cloth_inspect (the only cloth half fronted)

  // -> puerts_nav_build with wait: true, which is the legacy blocking shape.
  // Same classification as the native tool: idempotent because a rebuild
  // converges on the same navmesh, and not destructive because navigation data
  // is derived from the level and cannot lose anything authored.
  ai_nav_rebuild: mutatingIdempotent,     // -> puerts_nav_build

  // PIE agent read half. pie_agent_expect is readOnly here where the legacy
  // twin was too: it starts an in-engine check that only reads. What changed
  // is when it answers, not what it touches; the alias description says so.
  pie_agent_observe: readOnly,            // -> puerts_pie_agent_query
  pie_agent_status: readOnly,             // -> puerts_pie_agent_query
  pie_agent_expect: readOnly,             // -> puerts_pie_agent_query

  // Blueprint members -> puerts_blueprint_member_patch. Every add here is
  // idempotent where its legacy twin above is plain mutating, and that is not
  // a relabelling: the native command classifies each operation against live
  // state and reports an operation whose result is already present as
  // unchanged, so a rerun applies nothing and does not dirty the package. The
  // legacy tools rerun into a failure.
  blueprint_add_variable: mutatingIdempotent,
  blueprint_remove_variable: destructiveIdempotent,
  blueprint_set_variable_default: mutatingIdempotent,
  blueprint_add_function: mutatingIdempotent,
  blueprint_remove_function: destructiveIdempotent,
  blueprint_add_event_dispatcher: mutatingIdempotent,
  blueprint_remove_event_dispatcher: destructiveIdempotent,
  blueprint_add_interface: mutatingIdempotent,
  blueprint_remove_interface: destructiveIdempotent,
  blueprint_component_remove: destructiveIdempotent,
  blueprint_component_rename: mutatingIdempotent,

  // Blueprint graph -> puerts_blueprint_graph_patch. Same reasoning: a move to
  // the position a node already holds, or a link that already exists, is
  // reported unchanged rather than rewritten.
  blueprint_node_add: mutating,           // adding a node twice adds two nodes
  blueprint_node_delete: destructiveIdempotent,
  blueprint_node_move: mutatingIdempotent,
  blueprint_pins_connect: mutatingIdempotent,
};
