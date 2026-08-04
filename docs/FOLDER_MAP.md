# Where everything lives

Machine-level map of the MCP bridge and the project that hosts it, as of
2026-07-29. `REPOSITORY_LAYOUT.md` covers the layout *inside* a repository; this
covers which directory is which *on disk*, which is what got confusing.

## The short version

There is **one bridge repository**, checked out **twice**, for two different jobs.

| Directory | Job |
|---|---|
| `D:\Unreal Projects\UE4_Bridge` | **Where you edit the bridge.** Bridge only, no game files. |
| `...\SF_Repository\Sinfeld_240301` | **Where the bridge runs.** The UE4 project that loads `Plugins/MCPBridge`. |

Both are clones of `github.com/RBG-WebDesign/UE4_Bridge.git`. Edit in the first,
sync to the second with git, rebuild. Never copy files between them by hand as a
habit: that is how the two lines drifted apart in the first place.

## Everything on disk

```
D:\
├── UE\
│   ├── UE_4.27\                    the engine. UE_ENGINE_ROOT points here.
│   │                               engine_source_search reads Engine/Source from it.
│   ├── UE_5.7\                     unrelated. The bridge targets 4.27 only.
│   └── UE_BridgeDashboard\         separate repo, 1 commit, Slate dashboard plugin.
│                                   Its own product. Not part of the bridge.
│                                   (UE_Bridge\ was here. Deleted 2026-07-29.)
│
├── wt\
│   └── fix\                        git worktree of the game repo on branch `main`.
│                                   Holds 4 modified SFLocomotionMatching C++ files.
│                                   Kept deliberately: that work is uncommitted.
│
└── Unreal Projects\
    ├── UE4_Bridge\                 ==> CANONICAL BRIDGE. Edit here.
    ├── MCPBridge-Server\           public MIT distribution of the server, v0.4.0.
    │                               Separate remote. Needs a synced release when
    │                               mcp-server/ changes materially.
    ├── MASTER_PROJECT\
    │   └── SF_Repository\
    │       └── Sinfeld_240301\     ==> THE GAME PROJECT. The bridge runs here.
    ├── CodePlayground\             hosts the external PromptBrush plugin.
    └── _bridge-archive-2026-07-29\ everything removed during consolidation.
                                    Delete once you are satisfied. See its README.
```

## Canonical bridge: `D:\Unreal Projects\UE4_Bridge`

Bridge only. No `Content/`, `Source/`, `Config/`, or `.uproject`. That is
deliberate: the git remote is public, and keeping game files out of this tree
means a stray commit cannot leak them.

```
UE4_Bridge\
├── AGENTS.md                canonical instructions for every AI client. Edit this one.
├── CLAUDE.md                imports AGENTS.md via @AGENTS.md, plus Claude specifics
├── GEMINI.md                points at AGENTS.md, plus Gemini specifics
├── .mcp.json                Claude Code config. Sets UE_ENGINE_ROOT.
├── clients\                 configs for the clients that do NOT read .mcp.json
│   ├── codex-config.toml        -> ~/.codex/config.toml
│   └── gemini-settings.json     -> ~/.gemini/settings.json
├── mcp-server\              the TypeScript MCP server
│   ├── src\                     25 tool modules, 146 registered tools
│   ├── incubator\               written but NOT shipped. Nothing here compiles.
│   ├── tests\                   16 suites, 199 assertions, mock listener
│   └── dist\                    build output. Clients run dist/index.js.
├── Plugins\MCPBridge\       everything that runs inside UE4
│   ├── Content\Python\          the listener: 169 routes, 22 handler modules
│   └── Source\                  4 C++ modules, built by UBT not npm
├── Scripts\                 bridge-doctor, check-layout, mcp-smoke
└── docs\                    playbooks, specs, this file
```

## Game project: `SF_Repository\Sinfeld_240301`

Two version control systems share this directory, and they do not overlap:

- **Git** tracks the bridge: `Plugins/MCPBridge/`, `mcp-server/`, `docs/`, `Scripts/`
- **Perforce** tracks the game: `Content/`, `Source/`, `Config/`

Game paths are gitignored, so `git status` here shows bridge work only. That
separation is load-bearing. `npm run check:layout` fails if a game path ever
becomes tracked by git.

Bridge-relevant additions in this tree:

```
Sinfeld_240301\
├── Plugins\MCPBridge\       the plugin UE4 actually loads
├── mcp-server\              its own build. .mcp.json points at dist/index.js here.
├── BridgeExtensions\        project-local tools, auto-discovered
│   └── sinfeld\             25 locomotion tools. DISABLED: their listener
│                            commands do not exist yet.
└── Saved\AgentScratch\      temporary agent output. Never the repo root.
```

## Why two checkouts and not one

UE4 loads plugins from `<Project>/Plugins/`, so the plugin has to be physically
inside the game project. A directory junction from there into the canonical clone
was considered and rejected: both paths are tracked by the same git repo, so a
junction causes double-tracking, and a broken junction silently unloads the plugin
with no error you would notice.

Two checkouts of one repo, synced by git, is the boring option that does not
surprise anyone.

## Which folder do I use

| Task | Where |
|---|---|
| Edit server tools, listener handlers, C++, docs | `D:\Unreal Projects\UE4_Bridge` |
| Run an AI client against a live editor | `SF_Repository\Sinfeld_240301` |
| Build the C++ plugin with UBT | `Sinfeld_240301` (needs the `.uproject`) |
| Add a Sinfeld-only tool | `Sinfeld_240301\BridgeExtensions\sinfeld\` |
| Add a general UE4.27 tool | `UE4_Bridge\mcp-server\src\tools\` |
| Publish | neither directly. See `PUBLISHING.md`. |

## Keeping them in step

```bash
npm run doctor
```

It warns when a sibling clone sits at a different commit, when `dist/` is older
than `src/`, when an MCP server process is running from a path that no longer
exists, and when a client config points somewhere stale. Those four are how the
folders got tangled last time.

The clone-divergent warning is expected while the two trees are on different
branches. It is telling you they differ, not that something is wrong.

## What was removed

- `D:\UE\UE_Bridge` — deleted. April clone on a stale branch, using the retired
  `ue4-plugin/` and `unreal-plugin/` layout, with 38 uncommitted files. Bundled to
  the archive first: all 10 refs, a 3,294-line patch of its uncommitted tracked
  changes, and its 12 untracked files, 7 of which existed nowhere else. Eight
  orphaned node processes were still serving its MCP server against port 8080; a
  client that connected got three-month-old tools. Restore with
  `git clone _bridge-archive-2026-07-29/UE_Bridge-april-clone/UE_Bridge-all-refs.bundle`.
- Five git worktrees under `.claude/worktrees/`, all clean and all on the same
  commit. Their branch refs still exist, so no commit was lost.
- `_codex_git_merge_backups\` and `_codex_redirector_cleanup_backups\` from the
  game project root.

Everything above is recoverable from `_bridge-archive-2026-07-29\`.
