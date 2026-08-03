# Troubleshooting

Start with `python Scripts/ue427.py doctor`. It checks the build, the skill
install, MCP registration per client, the project version, and whether a live
editor actually advertises a session for the configured project. Most entries
below are things it detects and names for you.

| Symptom | Cause and fix |
|---|---|
| `session_missing` in a tool error | No editor advertises a session for the configured project. Either no editor is open for it, or `MCP_UNREAL_PROJECT_ROOT` points at a different project than the one running. `doctor` prints both paths. Open the right project with `Scripts/start-ue4-project.ps1`, or reinstall with `--project`. |
| No `puerts_*` tools at all in the client | `mcp-server/dist` is missing, so the server never started. Run `npm install && npm run build`, then restart the client. `ue427 repair` does both. |
| Tools are present but behave like an older version | Stale build: `dist` is older than `src`. Rebuild and restart the client. MCP servers connect at startup, so a rebuild alone is not enough. |
| A tool you expect does not exist | It may genuinely not be implemented. Check `references/tool-catalog.md`. If it is missing, add it as a `puerts_*` capability following the checklist in `AGENTS.md`. Do not reach for another transport. |
| Version refused | The project is not Unreal Engine 4.27. This bridge supports 4.27 only, and refuses 4.26, 5.x, and unknown versions. There is no bypass. |
| Unknown engine version | `EngineAssociation` is a GUID or blank (a source build) and no `Engine/Build/Build.version` was found. Set `UE_ENGINE_ROOT`, or pass `--engine-root`. |
| Calls hang rather than fail | The game thread is busy: shader compilation, level load, a modal dialog, or PIE. Wait, then read `puerts_get_logs`. The pipe accepts the connection and then waits, so this looks like a hang rather than a refusal. |
| Editor-only tool returns nothing during play | UE4.27 scripting does not raise during PIE. Stop PIE, then retry. An empty success is not an empty level. |
| Connect then timeout, repeatedly | Zombie `UE4Editor` processes and stale pipes. Check for leftover editor processes; a reboot clears them. Launch through `Scripts/start-ue4-project.ps1`, which refuses duplicate editors unless you explicitly opt in. |
| Skill not offered by the agent | Run `python Scripts/ue427.py verify`. It asks Codex through its app-server protocol and Gemini through its CLI whether they actually list the skill, rather than assuming a file on disk means discovery. |
| Gemini lists the skill but its MCP server shows disabled | Gemini disables MCP servers in untrusted folders. Start `gemini` once inside the Unreal project directory and approve the trust prompt. Do not use a trust bypass flag. |
| Skill edits do not reach an agent | The skill was copied rather than linked, so it can drift. `doctor` reports this. `ue427 repair` reinstalls it as a link to the canonical `skills/unreal-engine-4-27`. |
| Codex cannot launch the server | Codex starts the server from its own working directory, so a relative path never resolves. The installer always writes absolute paths; if you hand-edited `~/.codex/config.toml`, restore an absolute path. |
| Wrong project gets edited | This should be impossible: the client refuses a reply from an editor it did not address. If you see it, stop and record it in `docs/CAPABILITY_FINDINGS.md` immediately. A tool that controls Unreal must be able to trust its state queries. |
| `engine_source_*` tools fail | `UE_ENGINE_ROOT` is not set for that process. It is set in `.mcp.json` for MCP clients; export it in your shell for direct script use. |

## Diagnosing in order

1. `python Scripts/ue427.py doctor` for the whole install and session picture.
2. `puerts_diagnostic` to prove the PuerTS context, game thread, and pipe.
3. `puerts_get_logs` for what the editor itself reported.

List at most three likely causes, add one diagnostic that distinguishes them,
run it, remove the disproven causes, then make the smallest fix. Do not stack
hypotheses before measuring.
