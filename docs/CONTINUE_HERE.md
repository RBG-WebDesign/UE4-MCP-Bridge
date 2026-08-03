# Continue here

Written 2026-08-03 at the point integrator context ran out. Every commit named
here exists and every claim is checkable. The wave is NOT complete.

## State

- Integration branch `bridge/native-consolidation-2026-07-31` at `34b22dd`, **tree clean**.
- `npm run verify`: green. 220 tools, frozen count 220, every suite 0 failed.
- `install:check` against `D:/Unreal Projects/BridgeInstallTest`: current.
- No orphan agents. An editor may be running; close it before any rebuild.

## Merged this wave

| Lane | Branch @ commit | What landed |
|---|---|---|
| Q | `lane/q-finding-0p` @ `1cbf605` | Diagnosis of finding 0p, five-way comparison |
| P | `lane/p-slices` @ `53412fc` | Seven slice harnesses, 16 gaps, a capability regression |
| J | `lane/j-animation` @ `7f53a3b` | AnimBP/montage/blend-space inspectors, create-only AnimBP builder |
| K | `lane/k-ai-gameplay` @ `e41a17d` | Blackboard, EQS, navigation, perception, AI controller |

Plus the integrator's own **finding 0p fix, verified live**: variable defaults are
now read from the CDO, so `default_value` survives a compile.

## NOT merged, all committed and safe

| Lane | Branch @ commit | Why not merged |
|---|---|---|
| I | `lane/i-material` @ `8dc589e` | Merge attempted twice and ABORTED. See the merge trap below. |
| L | `lane/l-level-scene` @ `d2d20c9` | Not attempted, integrator context ran out |
| M | `lane/m-refront2` @ `4911f16` | Not attempted |
| N | `lane/n-packaging` @ `8495f08` | Not attempted |
| O | `lane/o-perf-live` @ `0abd4e5` | Not attempted |

Nothing is lost. Each branch carries its own complete work and its agent's
report is in the session transcript.

## THE MERGE TRAP, read before merging anything

Five lanes all extended the same seven shared files: `MCPPuerTSBridgeService.h`
and `.cpp`, `MCPBridgePuerTS.Build.cs`, `mcp-server/src/annotations.ts`,
`mcp-server/src/tools/puerts.ts`, `puerts-runtime/src/registry.ts`,
`puerts-runtime/types/puerts-bootstrap.d.ts`.

A mechanical "keep both sides" union of the conflict markers **DOES NOT WORK**
for `registry.ts` and `puerts.ts`. Git's conflict regions there split function
and array-literal bodies, so unioning two partial hunks yields syntactically
broken TypeScript. It typechecked as an unbalanced-brace error 300 lines away
from the real damage. Do not repeat this; it cost most of an integrator session.

What DOES work, proven on `registry.ts`:

```bash
git merge --no-ff lane/<name>
BASE=$(git merge-base HEAD lane/<name>)
git checkout HEAD -- puerts-runtime/src/registry.ts        # restore the valid merged file
git diff "$BASE" lane/<name> -- puerts-runtime/src/registry.ts > /tmp/lane.patch
# then insert the patch's added lines at a known anchor, e.g. before
# "async function buildPhysics(" for functions and before the
# '{ name: "physics_build"' entry for registry rows
npx tsc -p puerts-runtime/tsconfig.json                     # must exit 0 BEFORE moving on
```

Union IS safe for the append-only files: the service header and cpp, Build.cs,
annotations.ts, and the bootstrap typings.

Two more rules learned the hard way:

- **Never dedupe by trimmed line content** when unioning code. It strips
  legitimate repeated closing braces and corrupts the file silently.
- The tool-count assertion in `mcp-server/tests/puerts-tools.test.ts` is one
  shared scalar every lane moves. Sum the additions: base 25, J +4, K +7, I +2.
  `npm run verify` confirms the real number.
- `docs/TOOL_INVENTORY.json`, `docs/CAPABILITY_SCOREBOARD.json` and
  `docs/CAPABILITY_PRESERVATION_AUDIT.md` are GENERATED. Never merge them as
  text. Run `node Scripts/generate-tool-inventory.mjs --write` after the merge.

## Paste this into a fresh session

```
Continue the UE4_Bridge finish program as integration lead.

Repo: D:\Unreal Projects\UE4_Bridge
Branch: bridge/native-consolidation-2026-07-31 at 34b22dd (clean, verify green)

Read docs/CONTINUE_HERE.md first, especially THE MERGE TRAP. Then
docs/PROJECT_FINISH_SCOREBOARD.json and docs/CAPABILITY_FINDINGS.md 0m to 0q.

Do not restart planning. Do not relaunch the lanes: their work is committed.

1. Merge lane/i-material (8dc589e), lane/l-level-scene (d2d20c9),
   lane/m-refront2 (4911f16), lane/n-packaging (8495f08) and
   lane/o-perf-live (0abd4e5), ONE AT A TIME, using the recipe in
   CONTINUE_HERE. After each: npx tsc on both tsconfigs, regenerate the
   inventory, npm run verify. Do not start the next merge until verify is green.
2. Then CLOSE THE EDITOR and run one batch compile of the target:
   node Scripts/bridge-install.mjs --sync --project "D:/Unreal Projects/BridgeInstallTest"
   Roughly 6000 lines of C++ from lanes I, J, K, L and M have NEVER been
   compiled. Expect real errors. Each lane named its own highest-risk construct
   in its report.
   If the linker blames a symbol in neither header, delete the target's
   Plugins/MCPBridge/Intermediate and rebuild. That is finding 0m.
3. Then relaunch the editor and run the live suites, plus the new ones:
   Scripts/mutator-atomicity.mjs, Scripts/bp-member-patch-acceptance.mjs
   (1 red check left, finding 0q), and the seven slice harnesses.
4. Settle finding 0q with its one measurement: read compile_status on the FULL
   member-patch fixture immediately after it is built and BEFORE the patch runs.
   If it is already UpToDateWithWarnings the patch is innocent.
5. Lane N proved the packaged zip installs and compiles but never LOADS.
   docs/RELEASE.md section 3a steps 5-7 is the scripted procedure. Run it.

Rules that earned their place: merge one at a time and resolve centrally; a
lane cannot mark its own work live_verified; install:check before and after
every live run; compilation and mocks are not live proof.

Commit integrations separately. Do not push.
```

## Open defects, highest value first

1. **Capability regression (lane P).** Four of seven slices are blocked on
   `scene_batch`, and the capabilities it needs shipped in the legacy catalog
   and did not survive the native migration: `actor_spawn` took `name`,
   `folder` and `scale`; `puerts_spawn_actor` takes none. `level_actors` took
   `include_transforms` and `folder_filter`; `puerts_find_actors` takes neither.
   AGENTS.md requires legacy capability to be preserved. `lane/l-level-scene`
   is the branch that closes it.
2. **Finding 0q.** `blueprint_member_patch` leaves the Blueprint compiling with
   warnings where a fresh one does not. One measurement settles it, above.
3. **Compile messages are unreachable.** A caller told `UpToDateWithWarnings`
   cannot find out what the warnings were through any command.
4. **Packaging: source YES, binaries NO.** Lane N ran `-RunUAT` and proved it
   cannot work in 4.27 with a citation (`BuildPluginCommand.Automation.cs:66`,
   `:129`, `:133` write a one-plugin host descriptor with no
   `AdditionalPluginDirectories`). Not an excuse this time; it was attempted.
5. **A zip install has no `[MCPPuerTSBridge]` section** and the compiled default
   pipe name is one constant, so two zip installs on one machine fight over one
   pipe.
