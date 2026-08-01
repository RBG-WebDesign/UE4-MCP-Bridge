# External MCP donor audit

Read-only audit, 2026-08-01. Three external Unreal MCP repositories were cloned
outside the working tree and surveyed as capability donors for this bridge. No
code was copied, no migration was run. Structured data:
`docs/EXTERNAL_MCP_DONOR_AUDIT.json` (exact files, symbols, classifications,
risks, and required live tests per candidate).

| Donor | Commit | Engine | License | One line |
|---|---|---|---|---|
| tumourlove/monolith | `e67544c` (master) | UE 5.7/5.8 | MIT (+ATTRIBUTION.md) | 20-module C++ plugin, ~1,387 actions, HTTP+proxy, transactions everywhere, namespace dispatch |
| jeebus87/ultimateunrealenginemcp | `c31ebac` (public-release) | UE 5.7 | MIT | TS server + TCP C++ plugin, 133 tools, headless C++ authoring lane, visual review loop, 1021 mock tests |
| conaman/unreal-mcp-ue4 | `4faee6a` (main) | **UE 4.27.2** | MIT | Python remote execution, ~90 handlers, no C++, no transactions, honest about UE4.27 Python limits |

The most important negative result: **unreal-mcp-ue4, the only UE4.27 donor,
explicitly excludes Blueprint graph inspection and authoring as impossible from
stock UE4.27 Python.** That is independent confirmation that our C++ builder
lane is the only viable route on this engine, and that none of our builder
libraries should be replaced by anything in these donors.

## Ten highest-value donor candidates

Ranked; full records in the JSON. Format: capability — donor (classification).

1. **Behavior Tree inspection** — ultimateunrealenginemcp `MCPAICommands.cpp` (UE427_ADAPTATION). The BT walk uses only `BehaviorTree/BT*.h` core headers, all present in 4.27. Serialization shape for our tracked BT inspector; our own runtime tree is the object.
2. **BT export/import round-trip contract** — monolith `HandleGetBehaviorTree` + `export_behavior_tree` mirroring `build_behavior_tree_from_json` (ARCHITECTURE_REFERENCE). Exactly our graph_inspect pattern applied to BT; enables build→export→rebuild→export parity testing.
3. **Unattended-modal suppression** — monolith's `GIsRunningUnattendedScript` technique (ALGORITHM_REUSE). `CoreGlobals.h`, exists in 4.27. Directly targets finding 0c: the source-control modal that turns editor close into an unkillable hang.
4. **Pipe-client reconnection** — unreal-mcp-ue4 `remote-execution.ts` `connectWithRetry`/`tryRunCommand` (ALGORITHM_REUSE). ~40 lines adapted into `puerts-client.ts`; with per-call `pipe.txt` discovery this closes the stable-reconnection gap.
5. **Structured not-connected errors** — ultimateunrealenginemcp `plugin_not_connected` shape + backoff (SCHEMA_REUSE).
6. **Headless C++ authoring lane** — ultimateunrealenginemcp generators/parser/UHT-validator/`ue_build` (DIRECT_PORT; engine-version-neutral server-local TypeScript). Our one wholly unbuilt pillar.
7. **C++ authoring ergonomics** — monolith `verify_symbols`, `find_example_usage`, `suggest_build_cs_deps`, `lint_header`, build-error `fix_hints` (ARCHITECTURE_REFERENCE) layered on our `engine_source_*` tools.
8. **Material graph authoring contract** — monolith `build_material_graph`/`validate_material`/`export_material_graph` (UE427_ADAPTATION, the hardest port here). Our recorded high-risk gap.
9. **Blueprint graph patching contract** — monolith graph command vocabulary incl. `get_graph_summary` (10KB vs 172KB — respects our 1 MiB cap), `get_execution_flow`, pin-type grammar (ARCHITECTURE_REFERENCE). Implementation stays our compiled `BlueprintMutatorLibrary`.
10. **Visual self-verification** — ultimateunrealenginemcp `autoVerifyBounds` + `ue_orbit_review`/`ue_iterate_scene` (ALGORITHM_REUSE) on top of our existing screenshot/fit natives.

Honorable mentions (JSON ranks 11-12): unreal-mcp-ue4's p4 source-control
handler group (LEGACY_FALLBACK — this session reverted probe assets by hand;
that should be a command), and monolith's namespace-dispatch +
`monolith_discover` terse discovery (defer until a migrated group needs it).

## Quick wins

Small, low-risk, high-relief; each still needs its live test:

- Reconnection retry in `puerts-client.ts` (candidate 4) and the structured
  not-connected error (candidate 5).
- `GIsRunningUnattendedScript` guard scoped around exit/save-adjacent commands
  (candidate 3) — pairs with finding 0c's transient-purge fix.
- `autoVerifyBounds` appended to spawn/modify responses (candidate 10).
- BT inspection (candidate 1) is nearly a quick win: the serializer is
  small, 4.27-safe, and the acceptance extension is already specified.

## UE5 code requiring major adaptation

- All monolith C++ modules: conditional gates on EnhancedInput, StateTree,
  SmartObjects, PoseSearch, ControlRig, RigVM, CommonUI, MetaSound,
  GeometryScripting. Anything imported must be extracted function-by-function
  with those gates removed, per our forbidden-UE5-patterns table in AGENTS.md.
- ultimateunrealenginemcp's entire plugin (EnhancedInput in Build.cs,
  StateTree, WorldPartition, Chaos, LiveLink handlers). Its *headless
  TypeScript* is the portable half.
- monolith's material module: 4.27 `UMaterialExpression` API covers most of
  it, but layers/Substrate-era pieces must be dropped.

## What unreal-mcp-ue4 can restore immediately

It is UE4.27-proven and Python-based, i.e. shaped like our opt-in legacy lane.
Capabilities it has that none of our 205 registrations cover:

- **Source-control operations** (`ue_source_control_tools.py`): provider info,
  add, checkout, revert, submit. Our finding-0c cleanup was manual p4.
- **LevelSequence track/key/camera-cut editing** (`ue_sequence_tools.py`) —
  broader than our titles-module sequence binding.
- **Texture import** (`ue_texture_tools.py`) and StringTable creation.
- **World outliner tree** (`ue_get_world_outliner.py`) as a hierarchy, richer
  than our flat `level_actors`.
- Its 30+ live e2e smoke scripts are TEST_REUSE material for legacy-lane
  acceptance, which today has zero recorded live proof.

All of it is transactionless Python: acceptable only behind the existing
legacy opt-in, or as reference for native wrappers.

## Existing code we keep

Everything on the audit keep-list, now donor-confirmed: the named-pipe +
PuerTS transport (donors' HTTP/TCP/UDP are all reference-only), the
transaction-wrapped allowlisted C++ service, all 14 builder libraries
(monolith's BT/Blueprint writers duplicate what we already compiled),
`graph_inspect` and its acceptance-harness pattern (monolith independently
converged on the same export-mirrors-build design), pipe.txt discovery, and
the generated inventory/metadata gates. No donor replaces any of it.

## Code we must not copy

- `manage_editor.run_python` and unreal-mcp-ue4's transactionless mutation
  handlers — the arbitrary-execution surface our RETIRE proposal exists to
  remove, and mutations below our transaction bar.
- Donor transports as any default path — the strict tooling protocol stands.
- UE5-only modules wholesale (list above).
- Any complete donor plugin — extraction is function-level, with MIT
  attribution recorded (monolith additionally carries an ATTRIBUTION.md whose
  PolyForm-inspired provenance note should travel with borrowed design).

## Recommended first donor-assisted capability

**Behavior Tree inspection** (candidates 1+2 together). It is already our
tracked next capability; two donors supply a proven serialization shape and a
round-trip contract; the BT core API is 4.27-stable; the live test is a small
extension of the existing `smoke:bt` acceptance; and landing it upgrades
`puerts_behavior_tree_build` from `native_live_partial` to live-verified —
the exact promotion our verification model is waiting on.
