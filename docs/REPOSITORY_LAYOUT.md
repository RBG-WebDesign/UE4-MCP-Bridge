# Repository layout

These rules are enforced by `npm run check:layout` (`Scripts/check-layout.mjs`).
Run it before handing off work or submitting to source control. If the checker
and this document ever disagree, the checker is what actually runs; fix both.

## What this repository is

The UE4.27 MCP bridge, and only that. Its git remote is public. Game code,
content, and configuration live in Perforce, not here.

## Root

Nothing new goes at the root without a reason. The allowed entries are:

| Entry | What it is |
|---|---|
| `AGENTS.md` | Canonical agent instructions. The one to edit. |
| `CLAUDE.md`, `GEMINI.md` | Thin per-client pointers to `AGENTS.md` |
| `README.md`, `README_PROMPTBRUSH.md` | Human-facing docs |
| `.mcp.json` | Claude Code MCP config |
| `mcp-server/` | TypeScript MCP server |
| `Plugins/` | The UE4 plugin (Python listener + C++ modules) |
| `docs/` | All other documentation |
| `clients/` | MCP configs for Codex and Gemini |
| `Scripts/` | Repository automation |
| `agents/` | Scenario prompt templates for the orchestrator |
| `skills/` | Scenario prompt templates, plus `unreal-engine-4-27/`, the canonical Agent Skill installed into every client |
| `puerts-runtime/` | Declared npm workspace: the TypeScript that runs inside UE4. Built before `mcp-server` |
| `reports/` | Machine-readable session records, one JSON per session. See `reports/README.md` |
| `Saved/` | Gitignored, repository-owned acceptance-test fixtures under `Saved/AgentScratch/` |
| `ue427`, `ue427.cmd` | Launcher shims for `Scripts/ue427.py` |
| `tests/` | Cross-cutting tests |
| `orchestrator.mjs` | Multi-agent batch coordinator |
| `package.json`, `package-lock.json` | Workspace root |
| `.claude/`, `.github/`, `.githooks/`, `.vscode/` | Tooling config |
| `node_modules/`, `_releases/` | Generated, gitignored |

To add a root entry, add it to `ALLOWED_ROOT` in `Scripts/check-layout.mjs` with
a comment explaining why. Making the checker pass by weakening it silently is the
thing this file exists to prevent.

## Where things go

| Kind of file | Location |
|---|---|
| Permanent documentation | `docs/<category>/` |
| Documentation images | `docs/assets/<topic>/` |
| Architecture playbooks | `docs/playbooks/` |
| Design specs | `docs/superpowers/specs/` |
| Reusable automation | `Scripts/` |
| Acceptance-test fixtures | `Saved/AgentScratch/<agent>/<task-slug>/` in this repository, gitignored |
| Temporary logs, JSON, screenshots, exports, notes, experiments | `Saved/AgentScratch/<agent>/<task-slug>/` in the **game project**, never here |
| Tools with no listener support yet | `mcp-server/incubator/` |

## Language ownership

These boundaries are hard and the checker enforces them by file extension.

| Directory | Contains |
|---|---|
| `mcp-server/src/` | TypeScript only |
| `Plugins/MCPBridge/Content/Python/` | Python only (plus data and docs) |
| `Plugins/MCPBridge/Source/` | C++ only (plus `.Build.cs` and docs) |
| `docs/` | Markdown and image assets only |
| `docs/evidence/` | Captured output and structured results, so `.txt` and `.json` are allowed here as well |

`docs/evidence/` is the one exception to the Markdown rule, and it is narrow on
purpose: a raw console capture is evidence, and rewrapping it as Markdown to
satisfy a checker would make it look edited. The exception does not apply
anywhere else under `docs/`; a stray `.txt` outside `docs/evidence/` still fails.

## Rules the checker enforces

1. **No unexpected root entries.** See the table above.
2. **Language ownership.** No `.py` under `mcp-server/src/`, no `.ts` under
   `Plugins/MCPBridge/Source/`, and so on.
3. **No manual backup directories.** Anything matching `*backup*`, `*_bak`, or
   `_legacy*` fails. Use Git or Perforce history. Hand-copied backup folders go
   stale, get committed by accident, and hide which copy is real.
4. **No game content tracked here.** Anything matching `Content/`, `Source/`,
   `Config/`, `Saved/`, `*.uproject`, `*.uasset`, or `*.umap` fails. The remote is
   public.
5. **No scratch output at the root.** Report and log files belong in
   `Saved/AgentScratch/`.

## Why rule 3 exists

Manual backup copies caused real confusion in this project. Directories named
`_codex_redirector_cleanup_backups/` and `_codex_git_merge_backups/` accumulated
in the game project alongside four separate clones of this repository, and it
stopped being obvious which tree was authoritative. Version control already keeps
history. Use it.

## Why rule 4 exists

The public remote's history contains game source from before the bridge/game
boundary was drawn. Paths are untracked at HEAD, but a push of a pre-boundary
branch would publish them. Branch from `origin/main` for bridge work, and enable
the guard:

```bash
git config core.hooksPath .githooks
```
