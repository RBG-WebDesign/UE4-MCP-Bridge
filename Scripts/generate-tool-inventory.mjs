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

const legacyClient = new UnrealClient();
const puertsClient = new PuerTSClient();
const history = new OperationHistory();

// module file -> [factory export, backend, args]
const MODULES = [
  ['engine-source', 'createEngineSourceTools', 'server_local', []],
  ['puerts', 'createPuertsTools', 'native_pipe', [puertsClient]],
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

// Legacy tools whose capability already exists in the native 17. These are the
// first Wrap/Retire candidates; target_replacement names the native tool.
const NATIVE_OVERLAP = {
  actor_spawn: 'puerts_spawn_actor',
  actor_delete: 'puerts_delete_actor',
  actor_properties: 'puerts_read_property',
  actor_modify: 'puerts_set_property',
  level_save: 'puerts_save',
  level_actors: 'puerts_find_actors',
  asset_list: 'puerts_find_assets',
  viewport_screenshot: 'puerts_viewport_screenshot',
  gameplay_pie_start: 'puerts_pie_start',
  gameplay_pie_stop: 'puerts_pie_stop',
  system_logs: 'puerts_get_logs',
  undo: 'puerts_undo',
};

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
      state = 'native';
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
      annotations: toolAnnotations[definition.name] ?? definition.annotations ?? null,
      module_listener_commands: moduleCommands,
      test_file: testFile,
    });
  }
}
tools.sort((a, b) => a.name.localeCompare(b.name));

const inventory = {
  purpose:
    'Frozen inventory of every bridge tool across both lanes. No tool may be removed until its replacement reaches migrated_verified. See docs/TOOL_MIGRATION.md.',
  states: [
    'native', 'server_local', 'legacy_verified', 'legacy_untested', 'native_equivalent',
    'puerts_equivalent', 'hybrid_candidate', 'needs_port', 'duplicate', 'blocked',
    'deprecated', 'migrated_verified',
  ],
  tool_count: tools.length,
  by_backend: tools.reduce((acc, t) => ((acc[t.backend] = (acc[t.backend] ?? 0) + 1), acc), {}),
  tools,
};

const serialized = JSON.stringify(inventory, null, 2) + '\n';

if (write) {
  writeFileSync(inventoryPath, serialized);
  console.log(`Wrote ${inventoryPath}: ${tools.length} tools (${JSON.stringify(inventory.by_backend)}).`);
  process.exit(0);
}

if (!existsSync(inventoryPath)) {
  console.error(`FAIL: ${inventoryPath} does not exist. Generate it with --write.`);
  process.exit(1);
}

const frozen = JSON.parse(readFileSync(inventoryPath, 'utf8'));
const frozenNames = new Set(frozen.tools.map((t) => t.name));
const currentNames = new Set(tools.map((t) => t.name));
const removed = [...frozenNames].filter((n) => !currentNames.has(n));
const added = [...currentNames].filter((n) => !frozenNames.has(n));

// Contract drift: description or schema changed for a surviving tool.
const frozenByName = new Map(frozen.tools.map((t) => [t.name, t]));
const drifted = tools.filter((t) => {
  const old = frozenByName.get(t.name);
  if (!old) return false;
  return old.description !== t.description || JSON.stringify(old.schema) !== JSON.stringify(t.schema);
});

if (removed.length || added.length || drifted.length) {
  console.error('FAIL: tool catalog no longer matches docs/TOOL_INVENTORY.json.');
  for (const n of removed) console.error(`  removed tool: ${n} (removal requires migrated_verified state and a deliberate --write)`);
  for (const n of added) console.error(`  new tool not in inventory: ${n}`);
  for (const t of drifted) console.error(`  contract drift: ${t.name}`);
  console.error('If deliberate, regenerate with --write and review the diff.');
  process.exit(1);
}

console.log(`OK: catalog matches inventory (${tools.length} tools; frozen count ${frozen.tool_count}).`);
