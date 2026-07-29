# Incubator: tools that are not part of the shipped bridge

Nothing here is compiled, registered, or shipped. `tsconfig.json` only includes
`src/**/*`, and the registry-consistency test only scans `src/`, so these files
sit here without breaking the build or advertising tools that cannot work.

## Why these are here

Each file defines MCP tools whose `sendCommand()` targets have **no route** in
`Plugins/MCPBridge/Content/Python/mcp_bridge/router.py` and **no handler** anywhere
in the Python listener. Registering them would publish tools to Claude Code, Codex
and Gemini that fail on every call. That is exactly what happened before the
2026-07-29 consolidation: 26 such tools were advertised and returned
`Unknown command` every time, which is most of why the bridge seemed unreliable.

They were found untracked in the game project working tree and had never been
committed to any branch. They are kept because they are real work.

| File | Tools | Missing listener commands |
|---|---|---|
| `tools/inspection.ts` | 3 | inspection_open, inspection_prepare, inspection_status, inspection_view, inspection_cleanup |
| `tools/locomotion.ts` | 21 | locomotion_debug |
| `tools/fixed-camera-locomotion.ts` | 4 | fixed_camera_locomotion_debug |

28 tools in total.

## Decisions of 2026-07-29

These are settled. Do not re-litigate them in passing.

### `locomotion.ts` and `fixed-camera-locomotion.ts` are not bridge tools

They are specific to the Sinfeld game: motion matching, trajectory debugging,
fixed-camera transition tuning. The bridge is a general-purpose UE4.27 control
surface, and game-specific tooling does not belong in the shipped tool set.

They belong in a **project-local extension**, separate from the general bridge.
That extension does not exist yet. Its contract has to be designed first: how a
host project registers extra tools, where its handlers live, how the
registry-consistency test covers them, and how they stay out of any public
release.

**Until that contract is designed, these stay here.** Do not register them in
`src/index.ts` and do not move them into `src/tools/`.

### `inspection.ts` may become a general bridge tool

Capturing and inspecting editor state is general, not Sinfeld-specific, so this
one has a path into the shipped bridge. It graduates only when **all four** of
these exist:

1. Python handlers in `Plugins/MCPBridge/Content/Python/mcp_bridge/handlers/`
2. Routes in `router.py`'s `COMMAND_ROUTES` for all five commands
3. Annotations for every tool in `mcp-server/src/annotations.ts`
4. Tests against a **live editor**, not just the mock listener

Partial credit does not count. Three out of four leaves a tool that type-checks,
passes unit tests, and still fails when a user calls it.

## Graduating a file

1. Write the handler and wrap editor mutations in `@transactional`.
2. Register the command in `router.py`.
3. Add annotations for every tool to `src/annotations.ts`.
4. Move the file to `mcp-server/src/tools/`, import it in `src/index.ts`, and add
   its factory to `allTools`.
5. Move its test to `mcp-server/tests/` and add it to `test` in `package.json`.
6. Add a live-editor test under `mcp-server/tests/integration/`.
7. Run `npm run verify`.

Steps 1 and 2 are enforced: `tests/registry-consistency.test.ts` fails if the
server advertises a command the listener cannot route. Step 3 is enforced twice,
by a startup warning and by the smoke test. Nothing enforces step 6, so it is the
one that gets skipped. Do not skip it.
