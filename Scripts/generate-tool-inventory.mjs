#!/usr/bin/env node
// Generates and verifies docs/TOOL_INVENTORY.json: the frozen record of every
// tool the bridge has ever shipped, across both lanes. The inventory exists so
// no tool can silently disappear during the native migration. Each entry
// records the public contract (name, description, parameters, annotations),
// the backend that executes it today, and its migration state.
//
// Usage:
//   node Scripts/generate-tool-inventory.mjs --write   regenerate the inventory
//   node Scripts/generate-tool-inventory.mjs           verify the inventory
//                                                      matches the built catalog
//
// Requires mcp-server/dist (run npm run build first).

import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const distDir = join(repoRoot, 'mcp-server', 'dist');
const srcToolsDir = join(repoRoot, 'mcp-server', 'src', 'tools');
const inventoryPath = join(repoRoot, 'docs', 'TOOL_INVENTORY.json');

const write = process.argv.includes('--write');

if (!existsSync(join(distDir, 'index.js'))) {
  console.error('FAIL: mcp-server/dist is missing. Run npm run build first.');
  process.exit(1);
}

const dist = (p) => import(pathToFileURL(join(distDir, p)).href);

const { UnrealClient } = await dist('unreal-client.js');
const { PuerTSClient } = await dist('puerts-client.js');
const { OperationHistory } = await dist('history.js');
const { toolAnnotations } = await dist('annotations.js');
const { compatAliasTargets } = await dist('tools/compat.js');

const legacyClient = new UnrealClient();
const puertsClient = new PuerTSClient();
const history = new OperationHistory();

// module file -> [factory export, backend, args]
const MODULES = [
  ['engine-source', 'createEngineSourceTools', 'server_local', []],
  ['blueprint-production', 'createBlueprintProductionTools', 'server_local', []],
  ['status', 'createStatusTools', 'server_local', []],
  ['puerts', 'createPuertsTools', 'native_pipe', [puertsClient]],
  // Legacy names kept as router aliases onto the native lane. They reuse the
  // legacy public names on purpose, so an inventory entry is identified by
  // (name, module), never by name alone.
  ['compat', 'createCompatTools', 'native_pipe_alias', [puertsClient]],
  ['system', 'createSystemTools', 'legacy_http', [legacyClient]],
  ['project', 'createProjectTools', 'legacy_http', [legacyClient]],
  ['actors', 'createActorTools', 'legacy_http', [legacyClient]],
  ['level', 'createLevelTools', 'legacy_http', [legacyClient]],
  ['viewport', 'createViewportTools', 'legacy_http', [legacyClient]],
  ['materials', 'createMaterialTools', 'legacy_http', [legacyClient]],
  ['blueprints', 'createBlueprintTools', 'legacy_http', [legacyClient]],
  ['operations', 'createOperationsTools', 'legacy_http', [legacyClient, history]],
  ['promptbrush', 'createPromptBrushTools', 'legacy_http', [legacyClient]],
  ['gameplay', 'createGameplayTools', 'legacy_http', [legacyClient]],
  ['effects', 'createEffectsTools', 'legacy_http', [legacyClient]],
  ['intelligence', 'createIntelligenceTools', 'legacy_http', [legacyClient]],
  ['titles', 'createTitleTools', 'legacy_http', [legacyClient]],
  ['blueprint-graph', 'createBlueprintGraphTools', 'legacy_http', [legacyClient]],
  ['cpp', 'createCppTools', 'legacy_http', [legacyClient]],
  ['gamedev', 'createGamedevTools', 'legacy_http', [legacyClient]],
  ['content', 'createContentTools', 'legacy_http', [legacyClient]],
  ['animation', 'createAnimationTools', 'legacy_http', [legacyClient]],
  ['pie-agent', 'createPieAgentTools', 'legacy_http', [legacyClient]],
  ['cloth', 'createClothTools', 'legacy_http', [legacyClient]],
  ['optimization', 'createOptimizationTools', 'legacy_http', [legacyClient]],
];

// Legacy tools whose capability already exists in the native catalog: exactly
// the set a compat alias fronts, so the two cannot drift. The legacy entry
// stays hybrid_candidate (its HTTP implementation is still what answers the
// name when MCP_ENABLE_LEGACY_HTTP=1); the alias entry is the wrap.
//
// This was a hand-kept literal until 2026-07-31 and had gone stale: two of its
// keys, actor_properties and system_logs, named tools that do not exist, so
// only 10 of the advertised 12 were ever classified. Deriving it removes that
// failure mode, and the assertion below catches the next one.
const NATIVE_OVERLAP = compatAliasTargets;

function describeZod(schema) {
  // Best-effort structural summary of a Zod schema: parameter names, type
  // constructor, and optionality. Not a full JSON Schema; the frozen contract
  // is the shape, which is what a replacement must reproduce.
  const def = schema?._def;
  if (!def) return { kind: 'unknown' };
  const kind = def.typeName ?? 'unknown';
  if (kind === 'ZodObject') {
    const shape = typeof def.shape === 'function' ? def.shape() : def.shape;
    const params = {};
    for (const [key, value] of Object.entries(shape ?? {})) {
      let inner = value;
      let optional = false;
      while (inner?._def?.typeName === 'ZodOptional' || inner?._def?.typeName === 'ZodDefault') {
        optional = true;
        inner = inner._def.innerType;
      }
      params[key] = `${inner?._def?.typeName?.replace('Zod', '').toLowerCase() ?? 'unknown'}${optional ? '?' : ''}`;
    }
    return { kind: 'object', params };
  }
  return { kind };
}

function listenerCommands(moduleFile) {
  const srcPath = join(srcToolsDir, `${moduleFile}.ts`);
  if (!existsSync(srcPath)) return [];
  const source = readFileSync(srcPath, 'utf8');
  const commands = new Set();
  for (const match of source.matchAll(/executeCommand[^(]*\(\s*["']([a-z0-9_/]+)["']/g)) {
    commands.add(match[1]);
  }
  return [...commands].sort();
}

function testFileFor(moduleFile) {
  const candidates = [
    `${moduleFile}-tools.test.ts`,
    `${moduleFile}.test.ts`,
  ];
  for (const candidate of candidates) {
    if (existsSync(join(repoRoot, 'mcp-server', 'tests', candidate))) {
      return `mcp-server/tests/${candidate}`;
    }
  }
  return null;
}

const tools = [];
for (const [moduleFile, factoryName, backend, args] of MODULES) {
  const mod = await dist(`tools/${moduleFile}.js`);
  const factory = mod[factoryName];
  if (typeof factory !== 'function') {
    console.error(`FAIL: ${moduleFile}.js does not export ${factoryName}`);
    process.exit(1);
  }
  const definitions = factory(...args);
  const moduleCommands = listenerCommands(moduleFile);
  const testFile = testFileFor(moduleFile);
  for (const definition of definitions) {
    let state;
    let target = null;
    if (backend === 'native_pipe') {
      // Placeholder; replaced below from the curated verification level, so
      // the state and the evidence dimension cannot disagree.
      state = 'native';
    } else if (backend === 'native_pipe_alias') {
      // The Wrap action from docs/TOOL_MIGRATION.md: old name, native execution.
      state = 'wrap';
      target = compatAliasTargets[definition.name] ?? null;
      if (target === null) {
        console.error(`FAIL: compat alias ${definition.name} has no target_replacement`);
        process.exit(1);
      }
    } else if (backend === 'server_local') {
      state = 'server_local';
    } else if (definition.name in NATIVE_OVERLAP) {
      state = 'hybrid_candidate';
      target = NATIVE_OVERLAP[definition.name];
    } else {
      state = 'legacy_untested';
    }
    tools.push({
      name: definition.name,
      module: `mcp-server/src/tools/${moduleFile}.ts`,
      backend,
      migration_state: state,
      target_replacement: target,
      description: definition.description,
      schema: describeZod(definition.inputSchema),
      // Per-tool annotations win, matching how index.ts resolves them. Compat
      // aliases rely on this: they share a name with a legacy tool but carry
      // the classification of the native tool they execute.
      annotations: definition.annotations ?? toolAnnotations[definition.name] ?? null,
      module_listener_commands: moduleCommands,
      test_file: testFile,
    });
  }
}
// Every alias must front a legacy tool that actually exists, or the alias is
// advertising a name nobody ever had.
const legacyNames = new Set(tools.filter((t) => t.backend === 'legacy_http').map((t) => t.name));
const orphanAliases = Object.keys(NATIVE_OVERLAP).filter((name) => !legacyNames.has(name));
if (orphanAliases.length > 0) {
  console.error(`FAIL: compat alias names with no legacy tool of that name: ${orphanAliases.join(', ')}`);
  process.exit(1);
}

// Compat aliases share their names with the legacy tools they front, so the
// identity of an inventory entry is (name, module), and so is the sort key.
const identity = (t) => `${t.name} ${t.module}`;
tools.sort((a, b) => a.name.localeCompare(b.name) || a.module.localeCompare(b.module));

// ---- Capability metadata merge ----
// docs/TOOL_CAPABILITY_METADATA.json is the one hand-curated input: canonical
// capability names, migration actions, C++ builder ownership, live evidence.
// Everything countable is derived here so no count can drift.
const metadataPath = join(repoRoot, 'docs', 'TOOL_CAPABILITY_METADATA.json');
const metadata = JSON.parse(readFileSync(metadataPath, 'utf8'));
const metaErrors = [];

for (const [key, builder] of Object.entries(metadata.builders)) {
  if (!existsSync(join(repoRoot, builder.path))) {
    metaErrors.push(`builder path does not exist: ${key} -> ${builder.path}`);
  }
}

const toolNames = new Set(tools.map((t) => t.name));
const moduleKey = (t) => t.module.split('/').pop().replace('.ts', '');
const OWNER_BY_BACKEND = {
  native_pipe: 'MCPBridgePuerTS service',
  server_local: 'mcp-server (engine source on disk)',
  native_pipe_alias: 'compat alias router',
  legacy_http: 'legacy Python listener',
};

for (const t of tools) {
  const defaults = metadata.module_defaults[moduleKey(t)] ?? {};
  const override = metadata.tools[t.name] ?? {};
  const isAlias = t.backend === 'native_pipe_alias';
  const merged = { ...defaults, ...override };

  if ((t.backend === 'native_pipe' || t.backend === 'server_local') && !metadata.tools[t.name]) {
    metaErrors.push(`native/server tool lacks explicit metadata: ${t.name}`);
  }
  if (t.backend === 'legacy_http' && !metadata.module_defaults[moduleKey(t)] && !metadata.tools[t.name]) {
    metaErrors.push(`legacy tool lacks metadata (no module default, no override): ${t.name}`);
  }

  t.capability_category = merged.capability_category ?? moduleKey(t);
  t.canonical_capability = merged.canonical_capability ?? `${t.capability_category}.${t.name}`;
  t.owner = OWNER_BY_BACKEND[t.backend];
  t.lifecycle = merged.lifecycle ?? 'active';
  t.migration_action = isAlias ? 'ALIAS'
    : (merged.migration_action
      ?? ((t.backend === 'native_pipe' || t.backend === 'server_local') ? 'KEEP' : null));
  t.replacement_tool = isAlias
    ? (t.target_replacement ?? null)
    : (merged.replacement_tool ?? null);
  t.compatibility_alias = isAlias || compatAliasTargets[t.name] !== undefined;
  t.cpp_builder = merged.cpp_builder ?? null;
  t.cpp_entry_point = merged.cpp_entry_point ?? null;
  t.python_handler = t.backend === 'legacy_http' ? (merged.python_handler ?? null) : null;
  t.live_evidence = merged.live_evidence ?? [];
  t.migration_risk = merged.migration_risk ?? 'low';
  t.capability_loss_risk = merged.capability_loss_risk ?? 'low';
  t.notes = merged.notes ?? null;

  // The verification dimension: strictly scoped per backend type
  if (t.backend === 'native_pipe') {
    t.verification = override.verification ?? 'live_verified';
  } else if (isAlias) {
    t.verification = 'compat_verified';
  } else if (t.backend === 'legacy_http') {
    t.verification = t.replacement_tool ? 'replaced' : 'retired';
  } else if (t.backend === 'server_local') {
    t.verification = override.verification ?? (t.name === 'bridge_command_status' ? 'pending_live' : 'editor_free_verified');
  }
  // migration_state derives from verification for native tools so the two
  // dimensions cannot disagree.
  if (t.backend === 'native_pipe') {
    t.migration_state = {
      live_verified: 'native',
      live_partial: 'native_live_partial',
      pending_live: 'native_pending_live',
    }[t.verification] ?? 'native_pending_live';
  }

  if (isAlias) {
    // An alias is the same capability as its target, never a new one.
    const target = tools.find((x) => x.name === t.replacement_tool && x.backend === 'native_pipe');
    if (target === undefined) {
      metaErrors.push(`alias ${t.name} has no canonical native tool (${t.replacement_tool})`);
    } else {
      const targetMeta = metadata.tools[target.name] ?? {};
      t.canonical_capability = targetMeta.canonical_capability ?? t.canonical_capability;
    }
  }

  if (t.migration_action === null) {
    metaErrors.push(`tool has no migration_action: ${t.name} (${t.backend})`);
  }
  if (t.cpp_builder !== null && metadata.builders[t.cpp_builder] === undefined) {
    metaErrors.push(`unknown cpp_builder '${t.cpp_builder}' on ${t.name}`);
  }
  if (t.python_handler !== null && !existsSync(join(repoRoot, t.python_handler))) {
    metaErrors.push(`python handler does not exist: ${t.python_handler} (${t.name})`);
  }
  for (const evidence of t.live_evidence) {
    if (!existsSync(join(repoRoot, evidence))) {
      metaErrors.push(`live evidence path does not exist: ${evidence} (${t.name})`);
    }
  }
  if ((t.migration_action === 'ALIAS' || t.migration_action === 'MERGE')
    && (t.replacement_tool === null || !toolNames.has(t.replacement_tool))) {
    metaErrors.push(`${t.migration_action} on ${t.name} names no existing replacement tool`);
  }
  if (t.migration_action === 'RETIRE') {
    if (t.live_evidence.length === 0) metaErrors.push(`RETIRE without evidence: ${t.name}`);
    if (t.replacement_tool === null) metaErrors.push(`RETIRE without replacement: ${t.name}`);
    if (!t.notes) metaErrors.push(`RETIRE without a recorded rationale: ${t.name}`);
  }
}

// ---- Promotion Matrix Validation ----
const matrixPath = join(repoRoot, 'docs', 'MOCK_PROMOTION_MATRIX.json');
if (existsSync(matrixPath)) {
  const matrixData = JSON.parse(readFileSync(matrixPath, 'utf8'));
  const matrixMap = new Map(matrixData.map((row) => [row.name, row]));
  for (const t of tools) {
    if (t.verification === 'mock_only' || matrixMap.has(t.name)) {
      const row = matrixMap.get(t.name);
      if (!row) {
        metaErrors.push(`mock registration '${t.name}' has no entry in MOCK_PROMOTION_MATRIX.json`);
        continue;
      }
      if (!row.disposition) {
        metaErrors.push(`matrix row '${t.name}' has no disposition`);
      }
      if (row.migration_action === 'ALIAS') {
        if (!row.replacement_tool || !toolNames.has(row.replacement_tool)) {
          metaErrors.push(`matrix alias '${t.name}' names missing replacement tool '${row.replacement_tool}'`);
        }
      }
    }
  }
}

// Prose documents must not carry hand-maintained tool counts; those drift.
const PROSE_COUNT_CHECKS = [
  ['AGENTS.md', /all \d+ tools/],
  ['docs/TOOL_MIGRATION.md', /\d+ (native pipe|legacy HTTP|server-local)/],
  ['docs/TOOL_MIGRATION.md', /Advertising \d+ /],
];
for (const [file, pattern] of PROSE_COUNT_CHECKS) {
  const text = readFileSync(join(repoRoot, file), 'utf8');
  const match = text.match(pattern);
  if (match) metaErrors.push(`hand-maintained count in ${file}: "${match[0]}" (cite the generated inventory instead)`);
}

if (metaErrors.length > 0) {
  console.error('FAIL: capability metadata is inconsistent with the repository.');
  for (const error of metaErrors) console.error(`  ${error}`);
  process.exit(1);
}

// ---- Derived outputs: scoreboard and audit report ----
const tally = (field) => tools.reduce((acc, t) => {
  const key = t[field] ?? 'null';
  acc[key] = (acc[key] ?? 0) + 1;
  return acc;
}, {});

const canonicalTools = tools.filter((t) => t.backend === 'native_pipe' || t.backend === 'server_local');
const compatTools = tools.filter((t) => t.backend === 'native_pipe_alias');
const legacyTools = tools.filter((t) => t.backend === 'legacy_http');

const tallySubset = (arr, field) => arr.reduce((acc, t) => {
  const key = t[field] ?? 'null';
  acc[key] = (acc[key] ?? 0) + 1;
  return acc;
}, {});

const uniqueCapabilities = new Set(
  tools.filter((t) => t.migration_action !== 'RETIRE').map((t) => t.canonical_capability),
);
const refrontByBuilder = {};
for (const t of tools) {
  if (t.migration_action === 'REFRONT' && t.cpp_builder !== null) {
    (refrontByBuilder[t.cpp_builder] ??= []).push(t.name);
  }
}
const portTools = tools.filter((t) => t.migration_action === 'PORT');
const noReplacement = tools.filter((t) =>
  t.backend === 'legacy_http' && (t.migration_action === 'PORT' || t.migration_action === 'KEEP'));

const scoreboard = {
  generated_by: 'Scripts/generate-tool-inventory.mjs (do not edit by hand)',
  total_registrations: tools.length,
  unique_canonical_capabilities: uniqueCapabilities.size,
  canonical_capabilities_readiness: tallySubset(canonicalTools, 'verification'),
  compatibility_aliases_readiness: tallySubset(compatTools, 'verification'),
  legacy_http_disposition: tallySubset(legacyTools, 'migration_action'),
  by_backend: tally('backend'),
  by_verification: tally('verification'),
  by_migration_action: tally('migration_action'),
  by_capability_category: tally('capability_category'),
  cpp_builders_awaiting_refront: Object.fromEntries(
    Object.entries(refrontByBuilder).sort((a, b) => b[1].length - a[1].length),
  ),
  direct_puerts_workflow_candidates: portTools.filter((t) => (t.notes ?? '').includes('puerts_workflow')).map((t) => t.name),
  native_wrappers_required: portTools.filter((t) => (t.notes ?? '').includes('new_native_wrapper')).map((t) => t.name),
  legacy_without_current_replacement: noReplacement.map((t) => t.name),
  retire_proposed: tools.filter((t) => t.migration_action === 'RETIRE').map((t) => t.name),
};
const scoreboardPath = join(repoRoot, 'docs', 'CAPABILITY_SCOREBOARD.json');
const scoreboardSerialized = JSON.stringify(scoreboard, null, 2) + '\n';

const auditMd = `# Capability preservation audit

Generated by \`Scripts/generate-tool-inventory.mjs --write\`. Do not edit by
hand; edit \`docs/TOOL_CAPABILITY_METADATA.json\` and regenerate. Checked on
every \`npm run verify\`.

The legacy registrations remain preserved. Their live usability and unique
capability coverage vary by tool.

## Counts (derived, cannot drift)

- Total public registrations: ${tools.length}
- Unique canonical capabilities (excluding proposed retirements): ${uniqueCapabilities.size}
- By backend: ${Object.entries(scoreboard.by_backend).map(([k, v]) => `${k} ${v}`).join(', ')}
- By verification: ${Object.entries(scoreboard.by_verification).map(([k, v]) => `${k} ${v}`).join(', ')}
- By migration action: ${Object.entries(scoreboard.by_migration_action).map(([k, v]) => `${k} ${v}`).join(', ')}

## Existing C++ builders awaiting re-fronting

${Object.entries(scoreboard.cpp_builders_awaiting_refront)
    .map(([builder, list]) => `- **${builder}** (${list.length} tools): ${list.join(', ')}`)
    .join('\n') || '- none'}

## Highest-value next migrations

Ranked by existing-implementation leverage: a REFRONT reuses a compiled C++
builder, so each is one doorway away from native.

${Object.entries(scoreboard.cpp_builders_awaiting_refront)
    .slice(0, 5)
    .map(([builder, list], i) => `${i + 1}. Re-front **${builder}** (${list.length} tools)`)
    .join('\n')}

## Legacy capabilities with no current replacement

${noReplacement.length} legacy tools have no native equivalent today (every
PORT and KEEP entry; the scoreboard lists them). Recorded capability-loss
risks above "low":

${tools.filter((t) => t.capability_loss_risk !== 'low')
    .map((t) => `- \`${t.name}\` (${t.capability_loss_risk}): ${t.notes ?? ''}`)
    .join('\n') || '- none recorded'}

## Count discrepancies resolved

- "171 tools" (AGENTS.md, stale) and "187 tools" (legacy acceptance era) were
  hand-maintained snapshots; the generated inventory is now the only count and
  a verify check rejects new hand counts in prose.
- "20 native live-verified" (session reporting, 2026-08-01) overstated the
  evidence: the handoff's own status table records partial coverage for spawn,
  delete, call_function, physics, PIE, and logs. The verification dimension
  now encodes that per tool, with evidence paths that must exist on disk.
- Registered tools are not usable tools: legacy registrations require the
  explicit opt-in lane, and aliases are compatibility names, not capabilities.
`;
const auditPath = join(repoRoot, 'docs', 'CAPABILITY_PRESERVATION_AUDIT.md');

const inventory = {
  purpose:
    'Frozen inventory of every bridge tool across both lanes. No tool may be removed until its replacement reaches migrated_verified. See docs/TOOL_MIGRATION.md.',
  states: [
    'native', 'native_pending_live', 'native_live_partial', 'server_local', 'legacy_verified',
    'legacy_untested', 'native_equivalent', 'puerts_equivalent', 'hybrid_candidate', 'wrap',
    'needs_port', 'duplicate', 'blocked', 'deprecated', 'migrated_verified',
  ],
  state_definitions: {
    native: 'default catalog, unit-tested, and proven against a live editor',
    native_pending_live: 'default catalog and unit-tested; the live editor acceptance has not passed yet',
    native_live_partial: 'live acceptance passed, but part of the evidence is the builder own report because no independent reader exists yet',
    legacy_untested: 'no live editor proof of any kind; a non-null test_file covers only the mock listener',
  },
  tool_count: tools.length,
  by_backend: tools.reduce((acc, t) => ((acc[t.backend] = (acc[t.backend] ?? 0) + 1), acc), {}),
  tools,
};

const serialized = JSON.stringify(inventory, null, 2) + '\n';

if (write) {
  writeFileSync(inventoryPath, serialized);
  writeFileSync(scoreboardPath, scoreboardSerialized);
  writeFileSync(auditPath, auditMd);
  console.log(`Wrote ${inventoryPath}: ${tools.length} tools (${JSON.stringify(inventory.by_backend)}).`);
  console.log(`Wrote ${scoreboardPath} and ${auditPath} from the same run.`);
  process.exit(0);
}

// The derived outputs are regenerated in memory on every verify; a stale file
// on disk is the same failure as a stale inventory.
if (!existsSync(scoreboardPath) || readFileSync(scoreboardPath, 'utf8') !== scoreboardSerialized) {
  console.error('FAIL: docs/CAPABILITY_SCOREBOARD.json is missing or stale. Regenerate with --write.');
  process.exit(1);
}
if (!existsSync(auditPath) || readFileSync(auditPath, 'utf8') !== auditMd) {
  console.error('FAIL: docs/CAPABILITY_PRESERVATION_AUDIT.md is missing or stale. Regenerate with --write.');
  process.exit(1);
}

if (!existsSync(inventoryPath)) {
  console.error(`FAIL: ${inventoryPath} does not exist. Generate it with --write.`);
  process.exit(1);
}

const frozen = JSON.parse(readFileSync(inventoryPath, 'utf8'));
const frozenIds = new Set(frozen.tools.map(identity));
const currentIds = new Set(tools.map(identity));
const removed = [...frozenIds].filter((n) => !currentIds.has(n));
const added = [...currentIds].filter((n) => !frozenIds.has(n));

// Contract drift: description or schema changed for a surviving tool.
const frozenById = new Map(frozen.tools.map((t) => [identity(t), t]));
const drifted = tools.filter((t) => {
  const old = frozenById.get(identity(t));
  if (!old) return false;
  return old.description !== t.description || JSON.stringify(old.schema) !== JSON.stringify(t.schema);
});

if (removed.length || added.length || drifted.length) {
  console.error('FAIL: tool catalog no longer matches docs/TOOL_INVENTORY.json.');
  for (const n of removed) console.error(`  removed tool: ${n} (removal requires migrated_verified state and a deliberate --write)`);
  for (const n of added) console.error(`  new tool not in inventory: ${n}`);
  for (const t of drifted) console.error(`  contract drift: ${identity(t)}`);
  console.error('If deliberate, regenerate with --write and review the diff.');
  process.exit(1);
}

console.log(`OK: catalog matches inventory (${tools.length} tools; frozen count ${frozen.tool_count}).`);
