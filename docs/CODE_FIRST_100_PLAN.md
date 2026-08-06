# UE4 Bridge code-first 100% completion plan

## Purpose and boundary

This plan finishes repository-side implementation before any new Unreal verification cycle. It targets UE4.27 only and the authenticated PuerTS named-pipe path only.

This planning pass does not launch, inspect, build, install into, or communicate with Unreal. The later implementation pass must also keep Unreal closed. It may run focused editor-free TypeScript contract checks after the code is written, followed by one consolidated editor-free gate.

`CODE_COMPLETE_100` means every intended repository capability is present, registered, annotated, allowlisted, typed, represented in generated inventory, covered by a focused editor-free contract, and every legacy registration has a migration disposition.

It does not mean runtime verified, native-build verified, warm/cold verified, PIE verified, package verified, or beta proven. Those are a separate handoff to the user's test team.

## Source-of-truth snapshot

- Branch: `bridge/native-consolidation-2026-07-31`
- Audited HEAD at planning start: `7fa6c583b11ccc201a6a5e4a652d70b5de4b9f57`
- Current generated inventory: 307 registrations, 244 unique public names, 209 canonical capabilities
- Current backends: 71 native pipe, 63 native aliases, 3 server-local, 170 legacy HTTP
- Live-verified canonical capabilities in the generated scoreboard: 19 of 209

The montage writer, Sequencer event writer, DataTable writer, and the final
`ALIAS`-disposition compatibility front now have their repository surfaces and
editor-free contracts. Native UE4.27 compilation and live read-back remain part
of the later test-team handoff.

The exhaustive current-registration ledger is `docs/TOOL_INVENTORY.json`,
generated with the scoreboard and preservation audit by
`Scripts/generate-tool-inventory.mjs`. Those generated files are normative for
current counts. The disposition counts below preserve the original planning
baseline and should not be read as current generated totals.

## Dispositions

- `DONE_CODE`: code exists and the current evidence already includes live verification. No repository work remains.
- `DEFER_LIVE`: code exists and its editor-free contract is present or newly landed. Only Unreal-side proof remains.
- `PATCH_EXISTING`: keep the public contract and repair a specific partial implementation or contract defect.
- `ALIAS`: preserve the legacy public schema through a strict adapter to an existing native command. Refuse unmappable parameters.
- `REFRONT`: compiled or otherwise reusable implementation exists, but it needs an authenticated native public front.
- `PORT`: legacy behavior is useful and requires a new narrow native implementation before the old public name can leave HTTP.
- `MERGE`: preserve the public behavior as a deterministic workflow over existing native primitives instead of adding another engine mutation primitive.
- `KEEP_LEGACY`: retain the existing disabled-by-default legacy registration. No speculative port is justified for code-first 100.
- `RETIRE`: remove only after explicit human approval and a source-control checkpoint.
- `ENGINE_LIMIT`: document the UE4.27 or rollback limit and do not pretend that code can prove it away.

Final current-public-name counts are: 16 `DONE_CODE`, 103 `DEFER_LIVE`, 16 `PATCH_EXISTING`, 7 `ALIAS`, 13 `REFRONT`, 25 `PORT`, 7 `MERGE`, 49 `KEEP_LEGACY`, 1 `RETIRE`, and 4 `ENGINE_LIMIT`.

The two montage and Sequencer event fronts are planned additions and are listed separately in the JSON ledger so they cannot be mistaken for current registrations.

## Ownership rule

One root integrator owns all shared surfaces:

- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Public/MCPPuerTSBridgeService.h`
- `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeService.cpp`
- `puerts-runtime/src/registry.ts`
- `puerts-runtime/types/puerts-bootstrap.d.ts`
- `mcp-server/src/tools/puerts.ts`
- `mcp-server/src/annotations.ts`
- `mcp-server/src/index.ts`
- `mcp-server/tests/puerts-tools.test.ts`
- `mcp-server/tests/registry-consistency.test.ts`
- `package.json`
- `docs/TOOL_CAPABILITY_METADATA.json`
- generated inventory, scoreboard, preservation audit, and skill catalog

Workers may own only disjoint domain tool modules, disjoint C++ command files, one exclusive compat-adapter patch, or disjoint focused harnesses. Only one worker may edit `mcp-server/src/tools/compat.ts`, and no other agent may touch it until that patch is integrated. Workers do not edit shared registries or generated metadata.

## Implementation waves

### Wave 1: close native fronts and partial contracts

Finish the two stranded C++ writers, then close the remaining native contract gaps. This wave establishes the narrow native commands that later aliases and workflows reuse.

1. Montage and Sequencer event fronts
   - Public additions: `puerts_anim_montage_build`, `puerts_sequence_event_track_build`
   - Runtime commands: `anim_montage_build`, `sequence_event_track_build`
   - Existing C++: `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeAnimMontage.cpp`, `Plugins/MCPBridge/Source/MCPBridgePuerTS/Private/MCPPuerTSBridgeSequenceEvent.cpp`
   - Shared fronts: service header, service allowlist and mutation classification, runtime declarations and registry, MCP schema, annotations, focused registry/schema tests
   - Parity: expose every C++ accepted field without renaming. Return the C++ result envelope unchanged.
   - Safety: both are convergent destructive asset writers, require the native transaction boundary, preserve `plan_only`, independently inspect after mutation, and report verified rollback on failure.
   - Smallest check: one registry/schema/dispatch assertion per new command in `mcp-server/tests/puerts-tools.test.ts`, plus `registry-consistency.test.ts`.

```ts
await executeNativeCommand(client, "anim_montage_build", params);
await executeNativeCommand(client, "sequence_event_track_build", params);
```

2. Existing native contract repairs
   - Members: `puerts_blueprint_member_patch`, `puerts_camera_shake`, `puerts_cloth_inspect`, `puerts_delete_actor`, `puerts_folder_visibility`, `puerts_input_mapping_info`, `puerts_input_mapping_patch`
   - Exact domain files: `MCPPuerTSBridgeBlueprintMember.cpp`, `MCPPuerTSBridgeEditorState.cpp`, `MCPPuerTSBridgeCloth.cpp`, `MCPPuerTSBridgeService.cpp`, `MCPPuerTSBridgeInspect.cpp`, `MCPBridgeGraphBuilder/Private/FolderVisibilityLibrary.cpp`, and `MCPBridgeGraphBuilder/Private/MCPBridgeInputLibrary.cpp`
   - Reuse: existing strict-field validators, `FBridgeContentSnapshot`, transaction ownership, inspector hashes, and `nativeFailureEnvelope`.
   - Parity: every declared field is either implemented or rejected by name. Result fields must describe actual readback, not requested state.
   - Safety: no arbitrary reflection, no silent field drop, CDO/config writes snapshot and restore outside transaction assumptions, destructive requests keep confirmation.
   - Smallest check: extend only the relevant source-contract cases in `puerts-tools.test.ts` and existing domain tests.

```cpp
if (!OnlyFields(Request, Allowed, TEXT("request"), OutError)) { return false; }
```

3. Partial legacy-facing contracts
   - Members: `asset_info`, `audio_component_add`, `blueprint_create`, `blueprint_list`, `camera_shake_blueprint`, `cloth_inspect_asset`, `gameplay_telemetry_snapshot`, `material_create`, `pp_preset`
   - Files: the matching module in `mcp-server/src/tools/`, its current legacy handler under `Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/`, and the corresponding focused test
   - Reuse: current native inspectors/builders. Do not copy Python behavior into a second transport.
   - Parity: preserve the legacy schema and response vocabulary; explicitly reject any field the selected native primitive cannot represent.
   - Completion: every tool either routes natively with contract coverage or has a precise remaining native dependency assigned in this wave.

### Wave 2: refront compiled code and merge workflows

1. Animation pose family
   - Members: `anim_pose_snapshot`, `anim_pose_delta`, `anim_root_motion_analyze`, `anim_reanchor`, `anim_batch_reanchor`
   - Files: `Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/AnimPoseLibrary.h`, `Private/AnimPose/AnimPoseLibrary.cpp`, one new disjoint PuerTS animation command file, `mcp-server/src/tools/animation.ts`, and `mcp-server/tests/animation-tools.test.ts`
   - Reuse: `UAnimPoseLibrary` and the existing asset snapshot/restore helpers.
   - Parity: preserve bone filters, frames, thresholds, dry-run, backup, force, recursive and report fields.
   - Safety: readers are read-only. Reanchor writers must converge, snapshot every touched sequence, verify through a canonical sequence pose result, and restore the whole batch on failure.
   - Smallest check: source-contract test for all five operation schemas and rollback fields.

```cpp
return UAnimPoseLibrary::ApplyReanchorPlan(Sequence, Plan, Report);
```

2. Blueprint completion family
   - Members: `blueprint_build_from_description`, `blueprint_build_from_json`, `blueprint_compile`, `blueprint_create`, `blueprint_document`, `blueprint_list`, `blueprint_node_set_enabled`, `blueprint_pins_break`, `puerts_blueprint_member_patch`
   - Files: `mcp-server/src/tools/blueprints.ts`, `mcp-server/src/tools/blueprint-graph.ts`, `MCPBridgeGraphBuilder/Private/BlueprintGraphBuilderLibrary.cpp`, `Private/BlueprintMutator/BlueprintMutatorLibrary.cpp`, `Private/BlueprintMutator/BPPinOps.cpp`, `MCPPuerTSBridgeBlueprint.cpp`, `MCPPuerTSBridgeBlueprintMember.cpp`, and existing blueprint tests
   - Reuse: `puerts_blueprint_build`, `puerts_blueprint_graph_patch`, `puerts_blueprint_member_patch`, and `puerts_graph_inspect`.
   - Parity: old graph and step formats are translated deterministically. Compile returns real compiler status. Document/list remain read-only projections.
   - Safety: one asset snapshot per workflow, compile and canonical readback before save, full restore on failure. `blueprint_node_set_enabled` and `blueprint_pins_break` are `MERGE` operations over the existing graph patch command, not new C++ entry points.
   - Smallest check: translator fixtures plus negative atomicity cases in `blueprint-graph-tools.test.ts`.

```ts
return graphPatch({ asset_path, graph, operations: [{ op: "set_node_enabled", node_guid, enabled }] });
```

3. DataTable and gameplay-pattern fronts
   - Members: `data_table_create`, `data_table_fill_from_json`, `gameplay_pattern_search`
   - Files: `MCPBridgeGraphBuilder/Public/MCPBridgeDataLibrary.h`, `Private/MCPBridgeDataLibrary.cpp`, the existing gameplay pattern library, one disjoint PuerTS content command file, `mcp-server/src/tools/content.ts`, `mcp-server/src/tools/gameplay.ts`, and focused tests
   - Reuse: current C++ libraries. Add no feature-specific generator.
   - Parity: preserve create/update preconditions, row JSON, search filters, and structured matches.
   - Safety: DataTable changes are desired-state, transactional, and verified by row names plus canonical row JSON. Pattern search is read-only.
   - Smallest check: schema and dispatch contracts with deterministic fixture JSON.

```cpp
const FString Report = UMCPBridgeDataLibrary::FillDataTableFromJSON(Table, RowsJson);
```

4. UI/title workflows
   - Members: `title_widget_build_from_manifest`, `widget_lower_third_create`, `widget_title_card_create`, `widget_title_template`
   - Files: `mcp-server/src/tools/titles.ts`, `mcp-server/src/tools/promptbrush.ts`, `mcp-server/src/tools/compat.ts`, and `mcp-server/tests/widget-title-tools.test.ts`
   - Reuse: `puerts_widget_build` and `puerts_widget_inspect`.
   - Parity: translate all colors, fonts, timings, text fields, manifest paths and presets to the canonical widget-tree shape.
   - Safety: pure deterministic translation first, one desired-state widget build second, inspector comparison before save.
   - Smallest check: snapshot the generated widget tree for each of the four public schemas.

```ts
const tree = titleManifestToWidgetTree(loadManifest(params));
```

### Wave 3: native ports and strict compatibility adapters

1. Scene and level family
   - Members: `actor_duplicate`, `actor_organize`, `actor_selection`, `actor_snap_to_socket`, `batch_spawn`, `level_outliner`, `puerts_delete_actor`
   - Files: `mcp-server/src/tools/actors.ts`, `mcp-server/src/tools/level.ts`, `mcp-server/src/tools/compat.ts`, `MCPPuerTSBridgeScene.cpp`, `mcp-server/tests/actor-tools.test.ts`, and `level-viewport-tools.test.ts`
   - Reuse: `puerts_scene_batch`, `puerts_scene_inspect`, `puerts_find_actors`.
   - Parity: preserve labels, offsets, socket names, validation switches, folder tree fields, and per-operation results.
   - Safety: one batch transaction, selection and outliner are read-only, socket operations verify attachment and transform, destructive delete keeps confirmation and post-delete absence.
   - Smallest check: adapter translation fixtures and one scene-batch source contract.

```ts
return sceneBatch({ operations: [{ op: "set_folder", actors, folder }] });
```

2. Materials, audio, post-process and effects
   - Members: `audio_component_add`, `material_apply`, `material_create`, `material_info`, `material_instance_create`, `material_instance_set_params`, `material_list`, `pp_preset`, `pp_volume_modify`, `pp_volume_spawn`, `console_effect`
   - Files: `mcp-server/src/tools/content.ts`, `materials.ts`, `effects.ts`, `compat.ts`, `MCPPuerTSBridgeMaterial.cpp`, `MCPPuerTSBridgeMaterialBuild.cpp`, `MCPPuerTSBridgeMaterialInstance.cpp`, `MCPPuerTSBridgeAudioBuild.cpp`, and existing material/content tests
   - Reuse: material and audio desired-state builders plus their inspectors, `puerts_scene_batch`, and the current component allowlists.
   - Parity: preserve preset names, material parameter kinds, component/slot selection, post-process settings and audio attachment fields.
   - Safety: frozen preset and property allowlists only, asset snapshots for builders, scene transaction for actor/component changes, independent inspect after write.
   - Smallest check: one translation fixture per public schema and source-contract coverage for slot/property allowlists.

```ts
return buildMaterialInstance({ asset_path, parent_path, scalars, vectors, textures, switches });
```

3. Gameplay, PIE query and cameras
   - Members: `camera_rig_create`, `camera_shake_blueprint`, `camera_shake_spawn`, `camera_shake_trigger`, `puerts_camera_shake`, `game_template_create`, `gameplay_framework_create`, `gameplay_pattern_search`, `gameplay_pie_status`, `gameplay_telemetry_snapshot`
   - Files: `mcp-server/src/tools/gamedev.ts`, `gameplay.ts`, `effects.ts`, `compat.ts`, the current camera-shake command file, `MCPPuerTSBridgeEditorState.cpp`, and focused gameplay/PIE tests
   - Reuse: scene batch, Blueprint desired-state build, class-default patch, `puerts_pie_agent_query`, and the UE4.27 `UCameraShakeBase` API already used by the bridge.
   - Parity: preserve templates, framework components, telemetry fields, camera settings and status vocabulary.
   - Safety: authoring is transactional and inspectable. Runtime status/telemetry are read-only. Runtime control remains user-gated and is not run during code-first completion.
   - Smallest check: schema and translation fixtures only.

```ts
return executeNativeCommand(client, "pie_agent_query", { op: "status" });
```

4. Project and intelligence
   - Members: `asset_info`, `asset_load_diagnostics`, `project_enable_plugins`, `project_info`, `project_index_query`, `project_index_rebuild`, `project_semantic_diff`
   - Files: `mcp-server/src/tools/project.ts`, `intelligence.ts`, `mcp-server/tests/project-index.test.ts`, `project-index` helpers already in the repository, and server-local descriptor code
   - Reuse: asset registry readers and canonical Saved/MCP index shape.
   - Parity: preserve dependency/referencer switches, plugin names, descriptor fields, query filters and semantic-diff result categories.
   - Safety: reads stay server-local or native read-only. Descriptor edits use validated atomic replacement and report restart required. Index rebuild writes only its owned Saved/MCP artifact.
   - Smallest check: throwaway descriptor and deterministic index fixtures.

```ts
await atomicWrite(uprojectPath, JSON.stringify(validateProjectDescriptor(next), null, 2));
```

5. Viewport, operations and remaining config contracts
   - Members: `viewport_bounds`, `viewport_camera`, `viewport_look_at`, `viewport_mode`, `viewport_render_mode`, `batch_operations`, `history_list`, `redo`, `cloth_inspect_asset`, `puerts_cloth_inspect`, `puerts_folder_visibility`, `puerts_input_mapping_info`, `puerts_input_mapping_patch`
   - Files: `mcp-server/src/tools/viewport.ts`, `operations.ts`, `cloth.ts`, `compat.ts`, `MCPPuerTSBridgeViewport.cpp`, `MCPPuerTSBridgeCloth.cpp`, folder/input libraries, and their existing focused tests
   - Reuse: viewport state service, typed domain workflows, native undo IDs, cloth inspector, folder visibility and input mapping commands.
   - Parity: preserve strict viewport enums, prior-state fields, history entries, redo count, cloth report vocabulary, folder lists and input preset/action/axis shapes.
   - Safety: viewport changes are not transactions and return prior/readback state. `batch_operations` refuses cross-domain atomicity claims. Redo uses owned transaction history only. Config writes snapshot disk state and verify it.
   - Smallest check: strict-enum, typed-dispatch and schema/source-contract fixtures.

```ts
if (domains.size !== 1) return refuse("batch_operations cannot promise one cross-domain transaction");
```

### Wave 4: reconcile and close the repository gate

The integrator performs this wave after all source patches are present.

1. Add or update every public annotation and native permission.
2. Regenerate PuerTS declarations and the skill tool catalog.
3. Regenerate `TOOL_INVENTORY.json`, `CAPABILITY_SCOREBOARD.json`, and `CAPABILITY_PRESERVATION_AUDIT.md` from current source.
4. Assert that no public registration is missing metadata, annotations, migration disposition, or a focused editor-free contract.
5. Run each focused editor-free check once, after its whole wave is integrated.
6. Run one final `npm run verify` and one `npm run test:editor-free`. Do not start a repair loop. Record any failure once with its owning group.

No UBT, Unreal launch, install sync, live MCP call, PIE, viewport capture, packaging load, warm/cold run, or acceptance run belongs in this wave.

## Legacy, retirement, and limits

`KEEP_LEGACY` means the tool remains behind `MCP_ENABLE_LEGACY_HTTP=1`; the native server never falls back to it. This is an explicit code-complete disposition, not a claim of native migration.

The only retirement candidate is `python_proxy`. It conflicts with the native allowlist boundary and the repository rule against prototyping editor operations through arbitrary Python. Removal requires a human-approved destructive checkpoint. The administrative tools previously proposed for retirement are restored to `KEEP_LEGACY`; preservation metadata did not authorize deleting them.

The four engine-limit items are:

- `cloth_apply_fabric_profile`
- `cloth_apply_lower_leg_gradient`
- `cloth_smooth_max_distance`
- `puerts_lighting_build`

The cloth writers stay limited until the bridge can prove full NvCloth rollback and independent readback in UE4.27. Lighting remains environment-dependent because Lightmass/Swarm completion cannot be established by repository code alone. These are documented limits, not missing implementation disguised as success.

## Completion and handoff

The code-first executor stops when the regenerated ledger proves:

- every intended public name is registered exactly once per declared backend;
- every runtime command is allowlisted, permissioned, typed and reachable from its MCP schema;
- every mutating command declares truthful transaction, idempotence and destructive semantics;
- every adapter preserves parameters/results or refuses the precise mismatch;
- every builder has an independent canonical inspector or a documented engine limit;
- every focused editor-free contract passes in one consolidated run;
- every legacy registration is `ALIAS`, `REFRONT`, `PORT`, `MERGE`, `KEEP_LEGACY`, `RETIRE`, or `ENGINE_LIMIT` with no unclassified row.

At that point report `CODE_COMPLETE_100`, not “100% verified” and not “beta ready.” Hand the remaining live-proof ledger to the user's Unreal test team as one batch.
