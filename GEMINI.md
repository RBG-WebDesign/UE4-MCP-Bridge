# GEMINI.md

@AGENTS.md

`AGENTS.md` is the canonical instruction file for this repository and applies to
every AI coding agent, including Gemini. The `@AGENTS.md` line above imports it,
so its contents are already in context rather than being a file Gemini has to
decide to open. Nothing in this file overrides it.

Do not copy content out of `AGENTS.md` into here. An earlier version of this
repository kept a per-client duplicate that drifted out of date and described
files that did not exist.

## Gemini specifics

- Gemini does not read `.mcp.json`. Copy the `mcpServers` block from
  `clients/gemini-settings.json` into `~/.gemini/settings.json` (user-wide) or
  `.gemini/settings.json` (per project).
- That template uses absolute paths, because Gemini launches the server from its
  own working directory, and sets `UE_ENGINE_ROOT` so the `engine_source_*` tools
  can find the installed engine.
- Build the server before first use: `npm run build`. Restart Gemini afterwards;
  MCP servers connect at startup.
- Verify the connection with `npm run smoke`. It exercises the same stdio path
  Gemini uses and reports exactly which stage fails.
