# Continue here

Updated 2026-08-03, after all nine wave-four lanes merged and the batch compile
passed. Every claim below is checkable.

## State

- Integration branch `bridge/native-consolidation-2026-07-31`, tree clean.
- `npm run verify`: green. 256 registrations, frozen count 256.
- **All nine lanes are MERGED.** Nothing is left unmerged.
- **The batch compile PASSES.** Zero errors, fresh DLL, install matches the repo.
- The merged plugin LOADS and serves: `mcp-smoke --require-editor`, 12 passed,
  0 failed, 0 skipped, 12 actors over the named pipe.
- 45 native commands in the allowlist: 25 that write, 20 read-only.

## Live suite status on the merged build

| Suite | Result |
|---|---|
| `mcp-smoke --require-editor` | 12 passed, 0 failed, 0 skipped |
| `graph-inspect-acceptance` | passed |
| `behavior-tree-acceptance` | passed |
| `bp-graph-patch-acceptance` | all checks passed |
| `mutator-atomicity` | **2 red, finding 0r** |
| `bp-member-patch-acceptance` | 1 red, finding 0q |

## Wave five is IN FLIGHT

Six lanes are running as of this writing. If this session ended mid-wave, their
branches exist and each was told to commit before stopping. Check them first:

| Lane | Branch | Domain | Rights |
|---|---|---|---|
| R | `lane/r-finding-0q` | finding 0q | editor AND build lock, exclusive |
| S | `lane/s-sequencer` | Sequencer, the last empty domain | none |
| T | `lane/t-material-graph` | material_build, texture_import | none |
| U | `lane/u-level-lighting` | lighting_build, class_defaults_patch, capability regression | none |
| V | `lane/v-widget-bind` | widget bindings and animations | none |
| W | `lane/w-domain-gaps` | nav_build, anim_blueprint_patch, audio, cloth | none |

`git log --oneline HEAD..lane/<name>` tells you whether a lane committed
anything. A branch level with HEAD did no work worth keeping.

**Merging them: use `node Scripts/merge-lane.mjs lane/<name>`.** It encodes the
recipe that cost a session to work out, and its header explains why a plain
union produces broken TypeScript. Run it one lane at a time, resolve the tool
count assertion it deliberately leaves alone, typecheck BOTH tsconfigs, then
regenerate the inventory, then verify. Do not start the next merge until verify
is green.

## Findings 0p and 0r are FIXED and live-verified

Do not reopen them. `mutator-atomicity` is green twice consecutively with its
control, and the three suites that share the mutator library still pass.

## Paste this into a fresh session

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Branch: bridge/native-consolidation-2026-07-31 (clean, verify green, 256 tools)

Read docs/CONTINUE_HERE.md, then docs/CAPABILITY_FINDINGS.md findings 0m to 0r.
All nine lanes are merged and the batch compile passes. Do not re-merge them and
do not relaunch those lanes.

1. Settle finding 0r first: apply set_variable_default inside a transaction,
   cancel it, read the CDO with puerts_read_property. If the CDO still holds the
   value the write is not transacted, so fix at the writer rather than at the
   rollback boundary. Then re-run Scripts/mutator-atomicity.mjs.
2. Give Scripts/mutator-atomicity.mjs a per-run fresh fixture path, the way
   Scripts/bp-member-patch-acceptance.mjs does. Its control variable survives
   between runs, so a second run reports a false red.
3. Settle finding 0q: read compile_status on the FULL member-patch fixture
   immediately after it is built and BEFORE the patch runs. If it is already
   UpToDateWithWarnings the patch is innocent and the assertion is wrong.
4. Close the capability regression lane P found: puerts_spawn_actor lost name,
   folder and scale, and puerts_find_actors lost include_transforms and
   folder_filter, against AGENTS.md's rule that legacy capability is preserved.
   scene_batch and scene_inspect are merged and compiled now, so this is wiring.
5. Run the seven slice harnesses against the live editor (npm run slice:summary).
   Nothing has ever run them with the new commands present.
6. Run the perf harness live for its first evidence file, and lane N's
   docs/RELEASE.md section 3a steps 5-7 to prove the packaged plugin LOADS.

Rules that earned their place: merge one branch at a time and resolve centrally;
install:check before AND after every live run; a lane cannot mark its own work
live_verified; every harness needs a control that must SUCCEED and move state;
compilation and mocks are not live proof.

Commit integrations separately. Do not push.
```

## Merge lessons, do not rediscover these

**Never line-merge `registry.ts`, `mcp-server/src/tools/puerts.ts`, or
`MCPPuerTSBridgeService.h`.** Git factors out the common suffix of two
conflicting hunks, so both sides end mid-declaration and share one tail. A union
closes one and leaves the other open, and it surfaces as an unbalanced brace or
a UHT "Missing '*' in Expected a pointer type" hundreds of lines away.

Graft whole brace-matched units from the lane's own file instead. Scripts used
this session are disposable, but the shape is: take the merged file, extract the
functions or declarations the lane has and the merged file lacks, insert at a
known anchor, then typecheck before moving on.

**Also learned:**

- Never dedupe by trimmed line content when unioning code. It eats repeated
  closing braces and corrupts silently.
- The tool-count assertions in `puerts-tools.test.ts` and `compat-tools.test.ts`
  are shared scalars every lane moves. Take the real number from a run.
- `TOOL_INVENTORY.json`, `CAPABILITY_SCOREBOARD.json` and
  `CAPABILITY_PRESERVATION_AUDIT.md` are GENERATED. Regenerate, never merge.
- `bUseUnity = false` is set on MCPBridgePuerTS deliberately. One command per
  .cpp with its own anonymous-namespace helpers collides under a unity build,
  and the collision scales with the number of commands. Do not turn it back on
  without renaming every file-local helper.

## Open defects, highest value first

1. **Finding 0r**, above. Blocks trusting any mutator atomicity claim.
2. **Capability regression** (lane P): `scene_batch`'s inputs shipped in the
   legacy catalog and did not survive the native migration.
3. **Finding 0q**: `blueprint_member_patch` leaves the Blueprint compiling with
   warnings where a fresh one does not.
4. **Compile messages are unreachable.** A caller told `UpToDateWithWarnings`
   cannot find out what the warnings were through any command.
5. **Packaging: source YES, binaries NO.** Lane N ran `-RunUAT` and proved it
   cannot work in 4.27 with an engine citation. The zip installs and compiles;
   it has never been proven to LOAD.
6. **A zip install has no `[MCPPuerTSBridge]` section** and the compiled default
   pipe name is one constant, so two zip installs on one machine contend for one
   pipe.
