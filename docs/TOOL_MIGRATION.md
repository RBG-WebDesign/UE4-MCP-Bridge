# Tool migration policy

The bridge has two catalogs: 17 native pipe tools (default) and 170 legacy HTTP
tools (opt-in via `MCP_ENABLE_LEGACY_HTTP=1`), plus 2 server-local engine-source
tools. The legacy catalog is the starting capability library for the native
system, not dead code. Its names, schemas, validation rules, and tests are the
salvageable assets; only the HTTP/Python execution paths get replaced.

## Inventory

`docs/TOOL_INVENTORY.json` is the frozen record of every tool: public contract
(name, description, parameter shape), backend, source module, listener commands,
test file, and migration state. `Scripts/generate-tool-inventory.mjs` generates
it from the built catalogs and verifies it in `npm run verify`. A tool that
disappears from the build, appears unrecorded, or changes contract fails the
gate until the inventory is deliberately regenerated with `--write` and the
diff is reviewed.

## Classification actions

Every legacy tool gets exactly one action:

| Action | Meaning |
|---|---|
| Keep | Already executes through the preferred path. |
| Wrap | Old name kept as a router alias to an existing native command. |
| Port | Implementation rebuilt in PuerTS TypeScript or native C++. |
| Combine | Several narrow tools replaced by one batch command; old names become adapters. |
| Retire | Removed only after a replacement passes the same behavior tests. |

A tool reaches `migrated_verified` only when old and new paths produce
equivalent results under the same test. Until then the legacy implementation
stays in the tree.

## Migration states

`native`, `server_local`, `legacy_verified`, `legacy_untested`,
`native_equivalent`, `puerts_equivalent`, `hybrid_candidate`, `needs_port`,
`duplicate`, `blocked`, `deprecated`, `migrated_verified`.

Initial assignment is automatic: the 12 legacy tools whose capability already
exists in the native 17 are `hybrid_candidate` with `target_replacement` set;
the rest are `legacy_untested`. Reclassification is a reviewed inventory edit.

## Alias policy

Old public names survive as router-level aliases, not as default-advertised MCP
tools. Advertising 189 names in `tools/list` bloats every client's tool
selection context. The default catalog stays curated and native. A
compatibility mode may register alias names for old prompts; an aliased result
must report `requested_tool` and `canonical_tool` so callers can migrate.

## Layer ownership for ported tools

- Native C++: Blueprint/Widget/Material graph editing, asset creation, package
  saves, transactions, PIE control, UBT integration, large batches. The
  existing C++ builder modules (`MCPBridgeGraphBuilder`, `MCPBridgePIEAgent`,
  `MCPBridgeClothOptimizer`) are re-fronted through the `MCPBridgePuerTS`
  allowlist, not rewritten.
- PuerTS TypeScript: composition, normalization, multi-step workflows, project
  inspection, dependency ordering, batch preparation, repair loops, reporting.
- Do not port tool-by-tool to reach a count. The target shape is a smaller set
  of batch primitives plus workflow tools, with old names as adapters.

## First migration slice

Prove the routing model on tools that already have native equivalents:
project/actor inspection, spawn, modify, property read/write, save,
transactions, undo, PIE control. That slice demonstrates old names routing to
native implementations with no Python or HTTP involved. Then migrate by
capability group in the order the feature pipeline needs: assets and blueprint
compile first, then UMG, input, animation, materials, effects, build tools.
