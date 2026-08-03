# Security

What this skill gives an agent, and where the boundaries are.

## The blast radius is a live editor

Connecting an agent to this bridge gives it live access to a running Unreal
Editor and the project on disk. Treat that the way you would treat running
code an assistant wrote, because that is what it is. Tools create, modify,
move, and delete assets and actors, and can save those changes to disk.

Save and commit, or shelve, before a long agent-driven session. Review the
diff before submitting.

## Transport boundary

Editor traffic goes over an authenticated local named pipe into the
in-process PuerTS runtime. There is no HTTP listener, no TCP port, and no
Remote Control endpoint in this path. Nothing in the skill or the installer
opens a network socket.

Requests carry a per-session nonce. The editor refuses a request whose nonce
does not match, and the client refuses a response from an editor it did not
address. A missing session is a structured refusal, never a guessed pipe.

The legacy Python HTTP listener still exists in the plugin for migration
testing. It is disabled unless a human sets `MCP_ENABLE_LEGACY_HTTP=1` before
both the editor and the MCP server start. It is not a fallback, and no tool
in this skill reaches for it. Leave it off.

## Native allowlist

Privileged operations are delegated from PuerTS to the native C++ boundary in
`MCPBridgePuerTS`, and each native tool name must appear in the C++
allowlist with narrow permissions. Adding a tool means adding it there
deliberately. A script cannot widen its own permissions at runtime.

Property writes go through a property allowlist rather than arbitrary
reflection writes. Path handling refuses escapes outside the project.
`npm run test:security` covers bad tokens, path escape, shell execution, and
the property allowlist, and runs without an editor.

## What the agent should not do on its own

- Do not start PIE unprompted. Ask first, and for every `puerts_pie_agent_*`
  tool.
- Do not run destructive operations without a source control checkpoint.
  Destructive means deleting assets or source, renaming public classes,
  changing serialization formats, replacing project config, modifying engine
  source, removing plugins, or migrating large content groups.
- Do not commit, push, merge, or reset unless asked.
- Do not report success before validation finishes. If a build or test fails,
  say so and show the output.

## Annotations are hints, not a permission boundary

`mcp-server/src/annotations.ts` classifies every tool as read-only, mutating,
or destructive. Clients use these for approval heuristics. Per the MCP spec,
a client must not treat them as a trust boundary: they describe intent, they
do not enforce it. The real enforcement is the native allowlist and the
transaction system.

The classification is kept in one file on purpose, so the whole surface can
be audited in a single read. `references/tool-catalog.md` is generated from
it.

## Approval mode

Running an agent with per-tool approval disabled removes the human gate in
front of every mutating call. If you do that, keep the blast radius small:
narrow prompts, read-heavy work, a throwaway project. Prefer leaving
approvals on while the bridge is connected to a project you care about.

## Skill installation

The installer writes only to agent configuration and skill directories. It
backs up anything it replaces, merges MCP configuration rather than
overwriting the file, and leaves unrelated servers and settings untouched.
It never modifies a `.uproject` unless explicitly asked, and when it does, it
takes a timestamped backup, preserves existing plugin entries, and never
changes the engine association.
