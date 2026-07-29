# Bridge consolidation, 2026-07-29

What was wrong, what was fixed, and what still needs a decision.

## The short version

The bridge was not broken by tampering. It was broken by an **abandoned git merge**
in the game project that left 27 files with unresolved index entries. One of them
was `mcp-server/src/index.ts`, which ended up importing two modules that the same
merge had staged for deletion. The server had not compiled since
2026-07-28 08:52, so every session since then ran a stale `dist/` that advertised
172 tools, 26 of which had no listener route and failed with
`Unknown command` on every call.

Meanwhile the same repo had been cloned four times and developed in two
directions at once, so no single tree had all the work.

## What was actually on disk

Five clones of one repo (`RBG-WebDesign/UE4_Bridge.git`), plus 7 git worktrees.

| Location | Branch | Date | State |
|---|---|---|---|
| `D:\Unreal Projects\UE4_Bridge` | main | 07-28 | Newest bridge line. Bridge-only. **Now canonical.** |
| `SF_Repository\Sinfeld_240301` | claude/possessed-left-arm... | 07-28 | Live game project. Mid-merge, 27 conflicts, server not compiling. |
| `D:\UE\UE_Bridge` | feature/blueprint-inspector-mutator | 04-23 | Stale. Retired `ue4-plugin/` + `unreal-plugin/` layout. 38 uncommitted files. |
| `D:\UE\UE_BridgeDashboard` | main | 03-18 | Separate repo, 1 commit, Slate dashboard plugin. Untouched. |
| `D:\Unreal Projects\MCPBridge-Server` | main | 07-05 | Public MIT split, v0.4.0. Untouched. |

`origin/main` on GitHub is still at `0a825fb` from 2026-07-06. Nothing has been
published since. Nothing was pushed during this work.

## Neither main tree was a superset

Since the July 6 split the two lines each grew work the other did not have:

| `D:\Unreal Projects\UE4_Bridge` had | `Sinfeld_240301` had |
|---|---|
| animation tools + `AnimPoseLibrary` | `annotations.ts` (tool safety classification) |
| `unreal-client.ts` with auth + configurable endpoint (202 lines vs 110) | `engine-source.ts` (439 lines) |
| `refresh_tools` | `pie-agent.ts` + `MCPBridgePIEAgent` C++ module |
| `client-config` tests | `cloth.ts` + `MCPBridgeClothOptimizer` C++ module |
| | `folder_hide` / `folder_show` / `folder_hidden_list` |

Everything on both sides is now in the canonical tree.

`engine-source.ts` deserves special mention. It is the "read the installed UE4.27
source" capability, it was already written, and it existed **only** inside an
abandoned worktree under `.claude/worktrees/`. It had never been committed to any
branch and would have been lost whenever those worktrees were pruned.

## What changed

Three commits on `consolidate/unify-bridge-2026-07-29`:

1. **`7f62d49`** Reunify the two divergent lines. 45 files, +6852 lines.
2. **`081d91e`** Inspector-backed smoke test and configs for all three clients.
3. **`0e74168`** `AGENTS.md` made canonical, plus an executable layout checker.

Safety tag `pre-consolidation-2026-07-29` marks the state before any of it.

### Documentation

`AGENTS.md` was a find-and-replace copy of `CLAUDE.md` with "Claude" swapped to
"Codex" everywhere, which produced `.Codex/agents/`, `.Codex/skills/`,
"Codex.ai/code", and a heading reading "Imported Claude Cowork project
instructions". It also documented 16 tool modules when 21 exist, while
`CLAUDE.md` documented `engine-source.ts` that only existed in a worktree.

`AGENTS.md` is now the single canonical file, written client-neutral against
verified facts. `CLAUDE.md` imports it via `@AGENTS.md`; `GEMINI.md` points at it.
Each keeps only genuinely client-specific notes.

### Multi-client support

Codex reads `~/.codex/config.toml` and Gemini reads `~/.gemini/settings.json`.
Neither reads `.mcp.json`, so neither could ever start this server. Templates are
now in `clients/`, with absolute paths (those clients launch the server from their
own working directory) and `UE_ENGINE_ROOT` set.

### Engine source access

`engine_source_search` and `engine_source_read` need `UE_ENGINE_ROOT`. Their
fallback reads `EngineAssociation` from a `.uproject`, and a bridge-only clone has
none, so they failed silently. All three client configs now set it to
`D:/UE/UE_4.27`. Verified: 6 matches for `FRunnable` in `Core`.

### Verification

```bash
npm run check:layout   # root entries, language ownership, no backups, no game content
npm run verify         # build + 186 unit tests + 7 smoke checks
npm run smoke          # stdio JSON-RPC against the built server, like a real client
npm run inspect        # MCP Inspector web UI
```

`Scripts/mcp-smoke.mjs` drives the built server the same way a client does and
checks initialize, tools/list, per-tool schema and annotations, engine source
access, and the live editor link. The editor check is a SKIP rather than a FAIL
when Unreal is closed, so it works in CI; `--require-editor` makes it strict.

Current results: layout PASS, build clean, 186 unit tests pass, 7/7 smoke pass,
146 tools registered and all 146 annotated.

## Nothing was deleted

Everything uncommitted was copied to
`Sinfeld_240301\Saved\AgentScratch\claude-bridge-consolidation\`:

| Directory | Contents |
|---|---|
| `audit/` | The full audit that drove these decisions |
| `master-conflict-snapshot/` | The 26 conflicted files from the game project, plus the exact `git status` |
| `rescued-from-april-clone/` | All 37 uncommitted files from `D:\UE\UE_Bridge`, 7 of which exist nowhere else |
| `preserved-root-artifacts/` | `pass3_report.json`, moved off the repo root |

The 7 files that exist only in the April clone, never committed anywhere:

```
mcp-server/src/tools/widgets.ts                                   130 lines
mcp-server/tests/tool-expansion.test.ts                           165
mcp-server/tests/validation-core-tools.test.ts                    246
unreal-plugin/.../generation/kismet_registry.py                    94
unreal-plugin/.../utils/responses.py                               43
unreal-plugin/.../tests/test_log_parsing.py                        69
unreal-plugin/.../tests/test_tool_expansion_helpers.py             50
```

They target the retired `unreal-plugin/` layout, so they need porting rather than
copying. Review them before deleting that clone.

## Still needs your decision

These were deliberately left alone. They are destructive, or they touch the live
game project, or both.

### 1. The abandoned merge in the game project

`Sinfeld_240301` has 27 paths with unresolved index entries and **no `MERGE_HEAD`**,
so git cannot say what was being merged. The conflicts span bridge files and game
files (`Scripts/`, `docs/Design/`, `Sinfeld_Demo.uproject`, `.p4ignore`). Guessing
at a resolution risks losing work, which is why it was not touched.

Good news: the working-tree files contain no conflict markers. Only the index is
stuck. Inspect with:

```bash
git -C "D:/Unreal Projects/MASTER_PROJECT/SF_Repository/Sinfeld_240301" diff --name-only --diff-filter=U
```

The snapshot in `master-conflict-snapshot/` has the on-disk content of all 26 of
those files as they stand now.

### 2. How the game project should consume the bridge

The game project needs `Plugins/MCPBridge` on disk for UE4 to load it, and it is
the same git repo as the canonical clone. Once its index is unstuck, sync it with
git rather than copying files.

A directory junction from the game project into the canonical clone was
considered and rejected: both paths are tracked by the same repo, so a junction
causes double-tracking, and a broken junction silently unloads the plugin.

### 3. Retiring the stale clones  [DONE 2026-07-29]

- `D:\UE\UE_Bridge` — retired. Bundled to
  `_bridge-archive-2026-07-29/UE_Bridge-april-clone/` (all 10 refs, verified), plus
  its uncommitted patch and 12 untracked files. Eight orphaned node processes were
  still serving that clone's MCP server against port 8080; all terminated.
- Worktrees — five confirmed clean and pruned. Their branch refs still point at
  5d9ad23, so no commit was lost. `youthful-hugle-c78871` and `D:\wt\fix` remain:
  the first is in use, the second holds four modified locomotion C++ files.
- `D:\UE\UE_BridgeDashboard` and `D:\Unreal Projects\MCPBridge-Server` are separate
  products with their own remotes. Left alone.

### 4. Game content backups in the game project

`_codex_redirector_cleanup_backups/` and `_codex_git_merge_backups/` hold `.uasset`
copies of campaign levels from June. They violate the no-manual-backups rule and
`check:layout` would flag them, but they are game content, so removing them is
your call.

### 5. Three tool modules in the incubator  [DECIDED 2026-07-29]

`inspection.ts`, `locomotion.ts` and `fixed-camera-locomotion.ts` define 28 tools
between them with no Python handler anywhere.

`locomotion.ts` and `fixed-camera-locomotion.ts` are Sinfeld-specific and move to a
project-local extension, separate from the general bridge. They stay incubated
until that extension's contract is designed.

`inspection.ts` may graduate into the general bridge, but only once its Python
handlers, routes, annotations, and live-editor tests all exist.

See `mcp-server/incubator/README.md`.

### 6. Publishing  [PAUSED by decision]

Nothing was pushed. Publishing stays paused. When it resumes, use a sanitized
branch rooted at `origin/main` and apply the bridge tree as one clean commit.
Full procedure and reasoning: `docs/PUBLISHING.md`.
