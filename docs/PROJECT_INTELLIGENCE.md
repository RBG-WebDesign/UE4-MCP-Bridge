# Project intelligence

A persistent, incrementally updated index of a UE4.27 project, so an agent can
answer "where is X, what depends on it, what already exists" without a full
re-scan and, for the offline half, without the editor running.

Implementation: `mcp-server/src/project-index.ts`.
Test: `mcp-server/tests/project-index.test.ts`.

## It consumes, it does not duplicate

The bridge already owns every fact source this index needs. The index is a
cache with an invalidation policy on top of them, nothing more.

| Fact | Owned by | This index |
|---|---|---|
| Asset existence and type | UE asset registry via `puerts_find_assets` | calls it, stores the answer |
| Blueprint graphs, members, pins | `puerts_graph_inspect` | calls it, stores the answer |
| Widget tree | `puerts_widget_inspect` | calls it, stores the answer |
| Behavior tree structure | `puerts_behavior_tree_inspect` | calls it, stores the answer |
| Engine C++ symbols | `engine_source_search` / `engine_source_read` | does not index the engine at all |
| Reflected engine API | the `unreal-api` MCP server | does not index it at all |
| Project C++ symbols | nothing owned this before | parses headers itself (see below) |

Two rules follow from that table:

1. If a fact has a native command, the index stores the command's output shape
   verbatim. It does not re-derive, re-parse or "normalize" it. When the command
   changes shape, the schema version bumps and the index rebuilds.
2. The index never becomes the authority. A stale entry is a cache miss, and the
   answer is always re-obtainable from the owning capability.

The one exception is project C++ under `Source/` and `Plugins/*/Source/`. No
existing bridge capability covers it: `engine_source_*` reads the engine install,
and the asset registry does not see C++ that has not been compiled and loaded.
That gap is why the offline half exists.

## What is indexed

### Implemented this wave, editor-free

| Kind | Source on disk | Facts stored |
|---|---|---|
| `cpp_header`, `cpp_source` | `Source/**`, `Plugins/*/Source/**` | class, struct, enum, interface, `UFUNCTION` and `UPROPERTY` names, with line, owning module, first base class, and whether the declaration carried a reflection macro |
| `build_cs` | `*.Build.cs` | module name, `PublicDependencyModuleNames`, `PrivateDependencyModuleNames` |
| `config_ini` | `Config/**/*.ini` | action and axis mappings (`+ActionMappings`, `+AxisMappings`), gameplay tags (`+GameplayTagList`) |
| `content_asset` | `Content/**/*.uasset`, `*.umap` | long package path (`/Game/...`) and whether it is a map |

Module attribution is by nearest ancestor `*.Build.cs`, which is how UBT decides
it too, so a header in `Private/Foo/Bar.h` is credited to the right module.

Content indexing here is a **file inventory only**: path, package name, map or
not. It deliberately does not open `.uasset` files. Asset type, class, and
references come from the asset registry, which needs the editor.

### Designed, not implemented this wave

Each of these needs a live editor. The native command that supplies it already
exists, which is the point: the remaining work is calling it and folding the
result into a `FileEntry`, not building a new capability.

| Fact | Native command | Key it hangs off | Invalidated by |
|---|---|---|---|
| Asset class, type, and disk path for every asset | `puerts_find_assets` (`find_assets`) | package path | `.uasset` mtime or size |
| Blueprint variables, functions, event graph nodes and pins | `puerts_graph_inspect` (`graph_inspect`) | Blueprint package path | `.uasset` mtime or size |
| Widget tree and slot properties | `puerts_widget_inspect` (`widget_inspect`) | Widget Blueprint package path | `.uasset` mtime or size |
| Behavior tree nodes, decorators, blackboard keys | `puerts_behavior_tree_inspect` (`behavior_tree_inspect`) | BT package path | `.uasset` mtime or size |
| Material graph expressions and parameters | none yet. A `material_inspect` command is missing and must be added before this row can be implemented | material package path | `.uasset` mtime or size |
| Asset reference edges (what refers to what) | none yet. Needs a native `asset_dependencies` command wrapping `IAssetRegistry::GetReferencers` / `GetDependencies` | package path | `.uasset` mtime or size |
| Reusable project patterns (recurring graph shapes worth promoting into `mcp-server/src/patterns/`) | derived from the `graph_inspect` entries above, not a command of its own | pattern name | any contributing Blueprint entry re-indexed |

Note the honest part of that table: two rows have no command yet. Material graph
inspection and asset reference edges are platform gaps, and per the
capability-first rule in AGENTS.md they get fixed as bridge capabilities before
the index tries to carry them.

The live half reuses the same `FileEntry` record and the same mtime and size
invalidation, keyed on the `.uasset` file that backs the object. That is the
reason the offline half already inventories `Content/` by path: it establishes
the file identity and change signal that the editor-backed facts will attach to.

## Persistence

Format: a single JSON object, one file, schema-versioned.

```
<projectRoot>/Saved/BridgeIntelligence/project-index.json
```

`Saved/` because the store is a cache. Losing it costs one full re-index and
nothing else, and UE projects already treat `Saved/` as disposable and
untracked. The path is overridable per call (`options.storePath`), which is how
the test indexes a real project without writing into it.

```jsonc
{
  "schemaVersion": 1,
  "projectRoot": "D:\\Unreal Projects\\BridgeInstallTest",
  "generatedAt": "2026-08-02T00:00:00.000Z",
  "files": {
    "Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintGraphBuilderLibrary.h": {
      "kind": "cpp_header",
      "size": 24576,
      "mtimeMs": 1754000000000,
      "indexedAt": "2026-08-02T00:00:00.000Z",
      "invalidatedBy": "new",
      "symbols": [
        {
          "name": "UBlueprintGraphBuilderLibrary",
          "kind": "class",
          "line": 11,
          "module": "MCPBridgeGraphBuilder",
          "reflected": true,
          "base": "UBlueprintFunctionLibrary"
        }
      ]
    }
  }
}
```

Keys are project-relative paths with forward slashes, so the store is stable
across the two path separators Windows tooling mixes.

Size on a real project: `BridgeInstallTest` (project source plus the MCPBridge
and Puerts plugin sources) is 339 files, 841 symbols, roughly 158 KB of JSON,
built in about 160 ms cold.

## Incremental update

One entry per file. On every run the indexer walks the scan roots, stats each
file, and compares against the stored entry:

| Condition | `invalidatedBy` | Action |
|---|---|---|
| No stored entry | `new` | parse |
| `size` differs | `size_changed` | parse |
| `mtimeMs` differs | `mtime_changed` | parse |
| `schemaVersion` or `projectRoot` differs from the store | `schema_version_changed` | parse everything |
| Neither differs | (entry keeps its previous reason) | reuse, counted as skipped |
| Stored path no longer on disk | n/a | dropped, reported in `stats.removed` |

`invalidatedBy` is persisted on the entry, not just returned in stats, so "why
is this entry what it is" survives a restart.

Size is checked before mtime because a same-second edit that changes length is
the case a coarse mtime clock can miss. Content hashing is deliberately not
used: it costs a full read of every file on every run, which is the exact cost
the index exists to avoid. A file edited and reverted within the same mtime tick
and back to the same byte count is the known false negative.

A corrupt or unparseable store is not an error. It is treated as absent, and the
next run is a full re-index.

## Non-goals

- **Not a search engine.** No inverted index, no ranking, no fuzzy matching
  beyond a substring pass in `findSymbols`. Grep and `engine_source_search`
  already cover text search.
- **Not a C++ parser.** Regex line scanning. Templates, macro-generated class
  names and multi-line base lists are missed. The upgrade path, if the miss rate
  ever justifies it, is the UnrealHeaderTool manifest, not a hand-written parser.
- **Not an engine index.** The engine is large, unchanging between installs, and
  already served by `engine_source_*` and the `unreal-api` database.
- **Not a `.uasset` reader.** Binary package parsing is a separate risk surface
  and the asset registry already does it correctly inside the editor.
- **No file watcher.** Update is pull-based, on demand. A watcher is a daemon,
  and a daemon is a lifecycle problem the bridge does not currently have.
- **Not registered as an MCP tool yet.** The core is a library so it can be
  proven offline first. Exposing it means touching `index.ts` and
  `annotations.ts`, which is integration work, not index work.

## API

```ts
indexProject(projectRoot, { storePath?, includePlugins?, write? })
  -> { success, data: { index, stats } | null, error? }

loadIndex(storePath) -> ProjectIndex | null
defaultStorePath(projectRoot) -> string

findSymbols(index, name, exact?) -> SymbolHit[]
listModules(index) -> ModuleFact[]
listInputMappings(index) -> InputMappingFact[]
listGameplayTags(index) -> GameplayTagFact[]
listContentAssets(index) -> ContentAssetFact[]
```

`stats` reports `scanned`, `indexed`, `skipped`, `removed`, `durationMs`, and a
count per invalidation reason.
