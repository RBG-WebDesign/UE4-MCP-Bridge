# Architecture Playbooks

This directory is the knowledge base for solved UE4.27 systems. Each file is a
"recipe": a standardized markdown spec that an LLM agent can parse and execute
to recreate a completed feature from scratch in a different project, without
re-deriving the engine research, the API dead ends, or the gotchas.

## The contract

When an agent is asked to do structural engine work (build a gameplay system,
generate blueprints or widgets, write editor C++, wire materials to UI), it
must check this directory FIRST for an existing playbook covering the pattern.
A playbook is the record of a working solution verified in a live editor. Do
not re-solve a problem a playbook already solves; follow the recipe, then
update the playbook if the recipe needed changes.

This instruction is enforced in two places:
- The MCP server ships it as server instructions (mcp-server/src/index.ts),
  so any session connected to the unreal-bridge server receives it.
- CLAUDE.md carries a Playbooks section for sessions working in this repo.

## Writing a new playbook

Copy `_TEMPLATE.md`. Every playbook uses the same six sections:

1. **System Design Intent** - the problem this solves, one paragraph.
2. **Dependencies** - exact classes, inheritance chains, modules, assets.
3. **How-To Graph Logic** - pseudocode/textual maps of the event flow.
4. **Replication Steps** - the ordered MCP tool calls / code edits to
   rebuild it from scratch. This is the executable part.
5. **UE4.27 Legacy Gotchas** - engine-specific traps discovered while
   building it. This is the most valuable section; never omit it.
6. **Verification** - how to prove it works (freeze frames, screenshots,
   CDO reads, log greps). A recipe without verification is a rumor.

Naming: kebab-case, system-first (`sitcom-title-controller.md`, not
`how-we-did-titles.md`). One system per file. If a system spans C++ and
content, one playbook covers both sides.

When a session materially changes a documented system, updating its playbook
is part of finishing the work, same as updating tests.

## Index

| Playbook | System | Status |
|---|---|---|
| [sitcom-title-controller.md](sitcom-title-controller.md) | Frame-driven sitcom credit sequencer with runtime widget creation | Verified 2026-07-06 |
| [wbp-glitch-effect.md](wbp-glitch-effect.md) | Per-widget VHS glitch via RetainerBox + custom HLSL UI material | Verified 2026-07-06 |
| [viewport-deadlock-prevention.md](viewport-deadlock-prevention.md) | Game-thread blocking rules for frame-dependent editor output | Verified 2026-07-05 |
