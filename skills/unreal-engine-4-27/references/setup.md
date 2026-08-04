# Setup

Everything here runs from the repository root. One command does the whole
install:

```bash
python Scripts/ue427.py install --agent all --scope user
```

On Windows you can also use the shim from the repository root:

```powershell
.\ue427 install --agent all --scope user
```

## What install does

1. Links the canonical skill at `skills/unreal-engine-4-27` into each agent's
   skills directory. Links, not copies, so editing the repository updates
   every agent at once. If the filesystem refuses a link, it falls back to a
   copy and says so.
2. Registers the existing `unreal-bridge` MCP server with each agent, using
   that agent's own configuration mechanism.
3. Backs up anything it replaces, outside the skills directory so the backup
   is never discovered as a second stale skill.
4. Prints every changed path. It is idempotent: running it twice changes
   nothing the second time.

## Install locations

| Client | Skill | MCP registration |
|---|---|---|
| Claude Code | `~/.claude/skills/unreal-engine-4-27` | `claude mcp add --scope user`, which writes `~/.claude.json` |
| Claude Desktop, Code tab | shares Claude Code's | shares Claude Code's |
| OpenAI Codex | `~/.agents/skills/unreal-engine-4-27` | `codex mcp add`, falling back to `~/.codex/config.toml` |
| Google Antigravity | `~/.gemini/config/skills/unreal-engine-4-27` | `~/.gemini/config/mcp_config.json` |
| Gemini CLI | `~/.agents/skills/unreal-engine-4-27` (shared with Codex) | `~/.gemini/settings.json` |

Claude Desktop's Code tab runs Claude Code and shares its `CLAUDE.md`, project
skills, hooks and MCP configuration, so `--agent claude` covers both. Its Chat
tab uses a separate `claude_desktop_config.json` and is not a coding surface;
the installer leaves it untouched.

Project scope uses `.claude/skills` and `.agents/skills` under the project,
plus that client's project-level MCP config. Antigravity reads
`.agents/mcp_config.json` and `.agents/skills` from the workspace, which is the
same directory Codex and Gemini use:

```bash
python Scripts/ue427.py install --agent all --scope project --project "D:/Unreal Projects/MyGame"
```

Gemini also reads `.gemini/skills`. The installer uses the shared
`.agents/skills` location, which current Gemini builds discover, so the
separate directory is only needed if a future build stops reading the shared
one.

## Options

```
--agent claude|codex|gemini|all   repeatable, defaults to every detected client
--scope user|project              repeatable, defaults to user
--project <path>                  the UE4.27 .uproject or its directory
--copy                            copy the skill instead of linking
--dry-run                         print actions, write nothing
```

`--project` is verified before anything is written. A project that is not
Unreal Engine 4.27 is refused, and nothing is installed.

## Server configuration

Every client is pointed at the same server already in this repository:

```
command: node
args:    <repo>/mcp-server/dist/index.js
env:     UE_ENGINE_ROOT, MCP_UNREAL_PROJECT_ROOT
```

Paths are absolute because Codex and Gemini launch the server from their own
working directory. Values come from the repository `.mcp.json` unless you
override them with `--project`, so there are no hardcoded machine paths in
the installer.

There is no second server, no HTTP endpoint, and no port. Editor traffic uses
the authenticated named pipe, per AGENTS.md.

## Build the server first

MCP clients connect at startup and run whatever is in `dist/`:

```bash
npm install
npm run build
```

After a rebuild, restart the agent or it keeps running the old tools.
`python Scripts/ue427.py doctor` reports a stale build.

## Launching

```bash
python Scripts/ue427.py start claude
python Scripts/ue427.py start codex
python Scripts/ue427.py start gemini
python Scripts/ue427.py start antigravity
```

`start` verifies the project is 4.27, warns if no editor advertises a session,
then launches the agent from the project directory.

`start antigravity` opens **Antigravity IDE**, not `Antigravity.exe`, which is
the agent manager. The IDE is the surface that reads a workspace's `AGENTS.md`,
`GEMINI.md`, and `.agents` directory. It opens this repository as the
workspace, because that is where the rules and the canonical skill live; the
UE4.27 project is reached over MCP by configuration, not by working directory.

Gemini CLI run from Antigravity's integrated terminal detects the IDE through
the `ANTIGRAVITY_CLI_ALIAS` environment variable that Antigravity sets, so
there is no preference to configure. That is also the way to use Gemini on
this project through your Antigravity subscription rather than an API key.

Open the editor first. Use the repository launcher, which refuses duplicate
editor processes unless you explicitly ask for them:

```powershell
Scripts/start-ue4-project.ps1
```

## Keeping it current

```bash
python Scripts/ue427.py update    # git pull, npm install, npm run build, reinstall
python Scripts/ue427.py doctor    # diagnose
python Scripts/ue427.py repair    # fix what is safe to fix, then re-run doctor
python Scripts/ue427.py verify    # prove each client discovers the skill
```

Because the skill is linked rather than copied, a `git pull` that changes
`skills/unreal-engine-4-27` reaches every agent with no reinstall. Reinstall
is only needed when the MCP server path or environment changes.

## Gemini workspace trust

Gemini disables MCP servers in untrusted folders. Start Gemini once inside
the Unreal project and approve the trust prompt:

```powershell
cd "D:\Unreal Projects\YourProject"
gemini
```

Then `gemini mcp list` shows `unreal-bridge` enabled. Do not use a trust
bypass flag; trust the specific project folder.
