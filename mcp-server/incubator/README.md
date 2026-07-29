# Incubator: TypeScript-only tools with no listener support yet

Nothing in this directory is compiled, registered, or shipped. `tsconfig.json` only
includes `src/**/*`, and the registry-consistency test only scans `src/`, so these files
sit here without breaking the build or advertising dead tools.

## Why these are here

Each of these defines MCP tools whose `sendCommand()` targets have **no route in**
`Plugins/MCPBridge/Content/Python/mcp_bridge/router.py` and **no handler** anywhere in the
Python listener. Registering them would publish tools to Claude Code / Codex / Gemini that
fail every time they are called.

They were found untracked in the game project working tree
(`SF_Repository/Sinfeld_240301`) during the 2026-07-29 consolidation. They had never been
committed to any branch. They are kept because they are real work, not deleted.

| File | Tools defined | Missing listener commands |
|---|---|---|
| `tools/inspection.ts` | inspection_capture, inspection_status, inspection_cleanup | inspection_open, inspection_prepare, inspection_status, inspection_view, inspection_cleanup |
| `tools/locomotion.ts` | 12 locomotion/motion-matching debug tools | locomotion_debug |
| `tools/fixed-camera-locomotion.ts` | 4 fixed-camera transition tools | fixed_camera_locomotion_debug |

## To graduate one of these

1. Write the handler in `Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/`.
2. Register the command in `router.py`'s `COMMAND_ROUTES`.
3. Add annotations for every tool to `mcp-server/src/annotations.ts`.
4. Move the file back to `mcp-server/src/tools/`, import it in `src/index.ts`, and add its
   factory to the `allTools` array.
5. Move its test back to `mcp-server/tests/` and add it to the `test` script in
   `package.json`.
6. Run `npm test`. The registry-consistency test verifies steps 1-2 actually happened.

## A note on scope

These three are specific to the Sinfeld game (motion matching, fixed-camera locomotion
tuning), not to controlling UE4.27 in general. The bridge is meant to be a general-purpose
UE4.27 control surface. Consider whether these belong in a project-local extension rather
than in the shipped tool set.
