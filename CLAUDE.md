# CLAUDE.md

@AGENTS.md

`AGENTS.md` is the canonical instruction file for this repository and applies to
every AI coding agent, including Claude Code. The `@AGENTS.md` line above imports
it, so its contents are already in context. Do not duplicate that content here.

Put only genuinely Claude-Code-specific things below this line.

## Claude Code specifics

- MCP config lives in `.mcp.json` at the repo root. It is already committed and
  sets `UE_ENGINE_ROOT`. Codex and Gemini need their own config; templates are in
  `clients/`.
- MCP servers connect at session start. After `npm run build`, restart the session
  or the rebuilt tools will not appear. A missing `mcp-server/dist/` is the usual
  reason the `unreal-bridge` tools are absent entirely; fix with
  `npm install && npm run build`, then restart.
- Research agents live in `.claude/agents/`: `project-researcher`,
  `ue4-cpp-expert`, `bridge-architecture`, `spec-and-plan-reader`, `orchestrator`,
  `documentation`, `integration-test`, `mcp-server`, `unreal-python`,
  `validation-safety`. Dispatch these for codebase questions instead of guessing;
  they return answers with paths and line numbers.
- Skills live in `.claude/skills/`.
- The scenario prompt templates in the root `agents/` and `skills/` directories
  are a different thing: they feed the orchestrator and PromptBrush, not Claude
  Code's agent system.
