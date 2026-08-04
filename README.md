<div align="center">

# UE4 MCP Bridge

**Drive the Unreal Engine 4.27 editor from Claude Code, OpenAI Codex, and Google Gemini.**

A local Model Context Protocol server that reaches into a running UE4.27 editor
over an authenticated named pipe, so an agent can build, inspect, and verify real
game content instead of telling you how to do it by hand.

[![Engine](https://img.shields.io/badge/Unreal%20Engine-4.27%20only-0E1128?style=for-the-badge&logo=unrealengine&logoColor=white)](https://www.unrealengine.com/)
[![Node](https://img.shields.io/badge/Node.js-18%2B-339933?style=for-the-badge&logo=node.js&logoColor=white)](https://nodejs.org/)
[![MCP](https://img.shields.io/badge/Protocol-MCP-6E56CF?style=for-the-badge)](https://modelcontextprotocol.io/)
[![Transport](https://img.shields.io/badge/Transport-Named%20Pipe-1F6FEB?style=for-the-badge)](#architecture)

[![Claude Code](https://img.shields.io/badge/Claude%20Code-supported-D97757?style=flat-square)](#-multi-client-support)
[![Codex](https://img.shields.io/badge/OpenAI%20Codex-supported-000000?style=flat-square&logo=openai&logoColor=white)](#-multi-client-support)
[![Gemini](https://img.shields.io/badge/Gemini%20CLI-supported-4285F4?style=flat-square)](#-multi-client-support)

[Quick start](#-quick-start) · [The `ue427` command](#-the-ue427-command) ·
[Architecture](#architecture) · [Safety](#-safety) · [Docs](#-documentation)

</div>

---

## Why this exists

Most editor automation dies at the round trip. Create a node, inspect, connect a
pin, inspect, set a property, inspect. This bridge optimizes for the opposite:
plan in one pass, send one desired-state call, run it inside a UE4 transaction,
then read it back with an independent inspector to prove it worked.

> **Engine target is Unreal Engine 4.27, and only 4.27.**
> If an API exists in a later engine but is not confirmed in 4.27, it is not used
> here. The skill and its tooling refuse other versions rather than guessing.

---

## Architecture

```mermaid
flowchart LR
    A["Agent client<br/>Claude Code · Codex · Gemini"]
    B["MCP server<br/>TypeScript"]
    C["PuerTS runtime<br/>Node.js inside UE4"]
    D["MCPBridgePuerTS<br/>native C++ module"]
    E["UE4.27 game thread"]

    A -- "stdio (MCP)" --> B
    B -- "authenticated named pipe" --> C
    C -- "approved calls" --> D
    D --> E

    style A fill:#6E56CF,stroke:#4C3BA6,color:#fff
    style B fill:#1F6FEB,stroke:#144A9E,color:#fff
    style C fill:#2EA043,stroke:#1A7431,color:#fff
    style D fill:#BF8700,stroke:#8A6100,color:#fff
    style E fill:#0E1128,stroke:#000,color:#fff
```

Each editor publishes a session manifest containing its process identity, project
path, pipe name, and a nonce. Every request carries that nonce and every response
carries the editor identity, so the client talks to exactly one editor and refuses
a reply from anywhere else. With two editors open, guessing would mean authoring
assets in the wrong project and reporting success. It refuses instead.

---

## Highlights

| | |
|---|---|
| **One transport, no fallbacks** | Editor traffic goes through `puerts_*` tools over an authenticated pipe. No HTTP, no sockets, no shell workarounds. A failing tool reports its error rather than silently switching lanes. |
| **A first-class agent skill** | One canonical skill, installed to all three clients, that teaches tool discovery, workflow selection, safety checks, and failure recovery. |
| **Diagnostics that name the fix** | `ue427 doctor` checks the build, skill install, MCP config, project version, and whether a live editor actually advertises a session. |
| **Transactional and reversible** | Every state-changing tool runs inside a UE4 transaction and returns a transaction id. Failed mutators roll back rather than leaving partial state. |
| **Inspectors for every builder** | `blueprint_build` pairs with `graph_inspect`, `behavior_tree_build` with `behavior_tree_inspect`, so desired state can be diffed against actual state without opening the editor. |
| **Batch over chatter** | `puerts_scene_batch` and the `*_build` family express whole desired states in one call instead of hundreds of small ones. |

---

## Quick start

### 1. Build the server

```bash
npm install
npm run build
```

### 2. Install the plugin into your UE4.27 project

```powershell
.\Scripts\install-mcp-bridge.ps1 "D:\Unreal Projects\MyGame\MyGame.uproject"
```

This builds the MCP server, installs and enables `MCPBridge` plus the pinned
PuerTS plugin, patches `DefaultEngine.ini`, and writes the client config into the
target project. Rerun it later to update that project.

### 3. Wire up your agents

```bash
python Scripts/ue427.py install --agent all --scope user
```

Then open the project in `UE4Editor` and start your agent from the project
directory:

```bash
python Scripts/ue427.py start claude
```

<details>
<summary><b>Verify it is working</b></summary>

<br>

```bash
python Scripts/ue427.py doctor    # full health report
python Scripts/ue427.py verify    # prove each client discovers the skill
```

Then ask your agent: *"Run puerts_diagnostic and tell me the result."* It should
report the PuerTS context, the game thread, the pipe transport, and actor query
timing. If it reports `session_missing`, `doctor` will tell you which project the
bridge is pointed at versus which editor is actually running.

</details>

---

## The `ue427` command

One entry point for installing, diagnosing, and launching across every client.

```bash
python Scripts/ue427.py <command>      # or: npm run ue427 -- <command>
```

The repository root also ships `ue427.cmd` (Windows) and `ue427` (POSIX).

| Command | What it does |
|---|---|
| `install` | Links the canonical skill into each agent and registers the `unreal-bridge` server |
| `uninstall` | Removes the skill and the MCP registration |
| `doctor` | Diagnoses build, skill install, MCP config, project version, and live editor session |
| `repair` | Fixes what is safe to fix automatically, then re-runs `doctor` |
| `update` | `git pull`, rebuild, reinstall |
| `verify` | Asks each client whether it genuinely discovers the skill |
| `start <agent>` | Verifies the project is 4.27, then launches the agent in it |
| `catalog` | Regenerates the tool catalog from `annotations.ts` |

Useful flags: `--agent claude|codex|gemini|all`, `--scope user|project`,
`--project <path>`, `--copy`, `--dry-run`.

> [!TIP]
> `install` creates **links**, not copies. Editing `skills/unreal-engine-4-27/`
> in this repository updates every agent at once, with no reinstall. `doctor`
> warns if a copy has crept in and can drift.

---

## Multi-client support

One skill source, installed everywhere.

| Client | Repository rules | Skill | MCP registration |
|---|---|---|---|
| **Claude Code** | `CLAUDE.md` imports `AGENTS.md` | `~/.claude/skills/unreal-engine-4-27` | `claude mcp add` (writes `~/.claude.json`) |
| **Claude Desktop, Code tab** | same as Claude Code | same as Claude Code | same as Claude Code |
| **OpenAI Codex** | reads `AGENTS.md` natively | `~/.agents/skills/unreal-engine-4-27` | `codex mcp add`, falling back to `~/.codex/config.toml` |
| **Google Antigravity** | reads `AGENTS.md` and `GEMINI.md` | `~/.gemini/config/skills` globally, `.agents/skills` per project | `~/.gemini/config/mcp_config.json`, or `.agents/mcp_config.json` per project |
| **Gemini CLI** | `GEMINI.md` imports `AGENTS.md` | `~/.agents/skills/unreal-engine-4-27` | `~/.gemini/settings.json` |

Codex, Gemini and Antigravity all read the shared `.agents/skills` location in a
project, so one directory serves all three. At user scope Antigravity differs:
its global customization root is `~/.gemini/config`, not `~/.agents`.

> [!NOTE]
> **Claude Desktop needs no separate install.** Its Code tab runs Claude Code
> and shares `CLAUDE.md`, project skills, hooks, and MCP configuration with the
> CLI, so `--agent claude` already covers it. Its Chat tab is a different
> surface with its own `claude_desktop_config.json`; the installer deliberately
> leaves that alone rather than adding a second, redundant registration.

> [!NOTE]
> Gemini disables MCP servers in untrusted folders. Start `gemini` once inside
> your Unreal project directory and approve the trust prompt. Do not use a trust
> bypass flag; trust the specific folder.

---

## What the agent can do

<details open>
<summary><b>Authoring</b></summary>

<br>

- Blueprints from JSON with schema validation, then compile and verify
- Widget Blueprints, Behavior Trees (26 node types), Blackboards, Anim Blueprints
- Materials, material instances, textures, sky shaders
- Level Sequences, camera rigs, and render jobs
- Actors: spawn, transform, organize, batch, delete

</details>

<details>
<summary><b>Inspection</b></summary>

<br>

- Graph, widget, material, sequence, animation, navigation, and physics inspectors
- Actor and asset search, property reads, editor log capture
- Engine C++ source search and read, which work with the editor closed

</details>

<details>
<summary><b>Verification</b></summary>

<br>

- Blueprint compile with structured errors that name the asset, graph, node, and pin
- Viewport screenshots and camera control
- Play In Editor control and runtime agent queries
- Navigation and lighting builds as background jobs with pollable status

</details>

The native catalog is **65 tools**, each classified read-only, mutating, or
destructive in a single auditable file. The generated summary lives in
[`references/tool-catalog.md`](skills/unreal-engine-4-27/references/tool-catalog.md),
and the complete reference is [`docs/TOOL_REFERENCE.md`](docs/TOOL_REFERENCE.md).

---

## Safety

> [!IMPORTANT]
> Connecting an agent gives it live access to a running editor and the project on
> disk. Save and commit before a long agent-driven session, and review the diff.

**Transport.** Editor traffic uses an authenticated local named pipe into the
in-process PuerTS runtime. There is no HTTP listener and no port in this path.
The legacy Python HTTP listener still exists for migration testing and stays
disabled unless a human sets `MCP_ENABLE_LEGACY_HTTP=1` before both the editor
and the server start. It is never an automatic fallback.

**Privilege.** Privileged operations are delegated to the native C++ boundary,
and each native tool must appear in the C++ allowlist with narrow permissions.
Property writes go through an allowlist rather than arbitrary reflection writes,
and path handling refuses escapes outside the project.

**Recoverability.** State-changing tools run inside UE4 transactions. Make a
source control checkpoint before anything destructive: deleting assets or source,
renaming public classes, changing serialization formats, replacing project
config, or migrating large content groups.

**Version.** The skill and its tooling verify Unreal Engine 4.27 from the
`.uproject` and from `Build.version` for source builds. There is no environment
variable that switches the check off.

---

## Repository layout

```text
mcp-server/src/                 MCP server: tool definitions and the pipe client
puerts-runtime/                 TypeScript executed inside UE4
Plugins/MCPBridge/Source/       Editor C++ modules, built by UnrealBuildTool
Plugins/MCPBridge/Content/      Legacy Python listener, opt-in only
skills/unreal-engine-4-27/      The canonical agent skill (installed to clients)
Scripts/                        Repo automation, acceptance tests, ue427 CLI
clients/                        Ready-to-paste MCP configs
docs/                           Specs, playbooks, references
```

These boundaries are enforced by `npm run check:layout`.

---

## Testing

```bash
npm run verify            # build + unit tests + perf + smoke. The gate for the TS side.
npm test                  # unit suites against a mock listener, no UE4 needed
npm run test:editor-free  # every acceptance CI can run without an editor
npm run doctor            # repo health: stale build, divergent clones, doc drift
```

Anything touching a real editor has a second gate, because `npm run verify` knows
nothing about which plugin a target project is running:

```bash
npm run install:check -- --project "D:\Unreal Projects\MyGame"
npm run install:sync  -- --project "D:\Unreal Projects\MyGame"
```

A plugin copy goes stale silently. It did once: two projects carried an install,
one was a day behind, and a live run against it passed while proving nothing.
`install:check` compares by content and refuses on any difference.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| No `puerts_*` tools in the client | `mcp-server/dist` is missing. `npm run build`, then restart the client. |
| Tools behave like an older version | Stale build. Rebuild and restart: MCP servers connect at startup. |
| `session_missing` | No editor advertises a session for the configured project. Run `ue427 doctor`; it names both paths. |
| Calls hang | The game thread is busy compiling, loading, or in PIE. Wait, then read `puerts_get_logs`. |
| Connect then timeout, repeatedly | Zombie editor processes and stale pipes. Launch through `Scripts/start-ue4-project.ps1`. |
| Skill not offered by an agent | `ue427 verify` asks each client directly rather than assuming a file on disk means discovery. |

Full matrix: [`references/troubleshooting.md`](skills/unreal-engine-4-27/references/troubleshooting.md).

---

## Documentation

| Document | Contents |
|---|---|
| [`AGENTS.md`](AGENTS.md) | Canonical instructions for every AI agent working here. Start here. |
| [`SKILL.md`](skills/unreal-engine-4-27/SKILL.md) | The agent-facing interface guide |
| [`references/setup.md`](skills/unreal-engine-4-27/references/setup.md) | Installing the skill and MCP config per client |
| [`references/operations.md`](skills/unreal-engine-4-27/references/operations.md) | Session model, transactions, batching, UE4.27 API safety |
| [`references/security.md`](skills/unreal-engine-4-27/references/security.md) | What the bridge exposes and where the boundaries are |
| [`docs/playbooks/`](docs/playbooks/) | Verified recipes for solved UE4.27 systems |
| [`docs/SETUP.md`](docs/SETUP.md) | Manual setup, step by step |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How the layers fit together |
| [`docs/TOOL_REFERENCE.md`](docs/TOOL_REFERENCE.md) | Complete tool reference |
| [`README_PROMPTBRUSH.md`](README_PROMPTBRUSH.md) | Prompt-driven gameplay generation |

> [!TIP]
> Before any structural engine or Blueprint generation task, read
> `docs/playbooks/` for an existing recipe instead of re-deriving the solution.

---

<div align="center">

**Built for Unreal Engine 4.27.** Not affiliated with Epic Games.

</div>
