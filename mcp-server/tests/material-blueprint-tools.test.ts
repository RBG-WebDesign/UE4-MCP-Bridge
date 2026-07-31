/**
 * Unit tests for material and blueprint tools.
 *
 * Uses MockUnrealServer to simulate the Python listener so we can test
 * tool registration, Zod input validation, and error handling without UE4.
 *
 * Run: npx tsx mcp-server/tests/material-blueprint-tools.test.ts
 */

import http from "http";
import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createMaterialTools } from "../src/tools/materials.js";
import { createBlueprintTools } from "../src/tools/blueprints.js";
import { createActorTools } from "../src/tools/actors.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18767;

let server: MockUnrealServer;
let client: UnrealClient;
let materialTools: ToolDefinition[];
let blueprintTools: ToolDefinition[];
let actorTools: ToolDefinition[];
let toolMap: Map<string, ToolDefinition>;

// ---- Test runner ----

interface TestCase {
  name: string;
  fn: () => Promise<void>;
}

const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => Promise<void>): void {
  tests.push({ name, fn });
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

function assertEqual<T>(actual: T, expected: T, label: string): void {
  if (actual !== expected) {
    throw new Error(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

function assertIncludes(str: string, substr: string, label: string): void {
  if (!str.includes(substr)) {
    throw new Error(`${label}: expected "${str}" to include "${substr}"`);
  }
}

async function callTool(name: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse(result.content[0].text) as Record<string, unknown>;
}

// ---- Setup & Teardown ----

async function setup(): Promise<void> {
  server = new MockUnrealServer();
  await server.start(TEST_PORT);
  client = new UnrealClient({ port: TEST_PORT });
  materialTools = createMaterialTools(client);
  blueprintTools = createBlueprintTools(client);
  actorTools = createActorTools(client);
  toolMap = new Map([
    ...materialTools.map((t) => [t.name, t] as [string, ToolDefinition]),
    ...blueprintTools.map((t) => [t.name, t] as [string, ToolDefinition]),
    ...actorTools.map((t) => [t.name, t] as [string, ToolDefinition]),
  ]);
}

async function teardown(): Promise<void> {
  await server.stop();
}

// ---- Tool Registration ----

test("all 4 material tools are registered", async () => {
  const expectedNames = ["material_list", "material_info", "material_create", "material_apply"];
  for (const name of expectedNames) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(materialTools.length, 4, "material tool count");
});

test("all blueprint tools are registered", async () => {
  const expectedNames = [
    "blueprint_list", "blueprint_info", "blueprint_create",
    "blueprint_compile", "blueprint_document", "blueprint_build_from_json",
    "blueprint_build_from_description", "anim_blueprint_build_from_json",
  ];
  for (const name of expectedNames) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(blueprintTools.length, 8, "blueprint tool count");
});

// ---- material_list Tests ----

test("material_list no filters returns all seeded materials", async () => {
  server.reset();
  const result = await callTool("material_list", {});
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 5, "count of seeded materials");
  assertEqual(data.truncated, false, "not truncated");
});

test("material_list with type_filter 'instance' returns only instances", async () => {
  server.reset();
  const result = await callTool("material_list", { type_filter: "instance" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const mats = data.materials as Array<Record<string, unknown>>;
  for (const mat of mats) {
    assertEqual(mat.type, "instance", "all should be instances");
  }
  assertEqual(data.count, 3, "3 instances in seeded data");
});

test("material_list with type_filter 'material' returns only base materials", async () => {
  server.reset();
  const result = await callTool("material_list", { type_filter: "material" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 2, "2 base materials");
});

test("material_list with path_filter narrows results", async () => {
  server.reset();
  const result = await callTool("material_list", { path_filter: "/Game/Materials/Instances/" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 3, "3 instances in Instances path");
});

test("material_list with name_filter wildcard", async () => {
  server.reset();
  const result = await callTool("material_list", { name_filter: "MI_BrickWall_*" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 2, "2 brick wall instances");
});

test("material_list with limit caps results", async () => {
  server.reset();
  const result = await callTool("material_list", { limit: 2 });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 2, "count capped at 2");
  assertEqual(data.truncated, true, "truncated");
});

// ---- material_info Tests ----

test("material_info returns parameters for a valid material", async () => {
  server.reset();
  const result = await callTool("material_info", { material_path: "/Game/Materials/Instances/MI_BrickWall_Red" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.type, "instance", "type");
  assertEqual(data.parent, "/Game/Materials/M_BrickWall", "parent");
  const params = data.parameters as Record<string, unknown[]>;
  assert(params.scalar.length > 0, "has scalar params");
  assert(params.vector.length > 0, "has vector params");
});

test("material_info with non-existent path returns error", async () => {
  server.reset();
  const result = await callTool("material_info", { material_path: "/Game/Materials/DoesNotExist" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error message");
});

test("material_info rejects path not starting with /", async () => {
  const tool = toolMap.get("material_info")!;
  const result = tool.inputSchema.safeParse({ material_path: "Game/Materials/M_Test" });
  assert(!result.success, "Should reject path not starting with /");
});

// ---- material_create Tests ----

test("material_create instance with parent and parameters", async () => {
  server.reset();
  const result = await callTool("material_create", {
    name: "MI_Custom",
    path: "/Game/Materials",
    type: "instance",
    parent: "/Game/Materials/M_BrickWall",
    parameters: {
      scalar: { Roughness: 0.5 },
      vector: { BaseColor: [1.0, 0.0, 0.0, 1.0] },
    },
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.type, "instance", "type");
  assertEqual(data.parent, "/Game/Materials/M_BrickWall", "parent");
  const paramsSet = data.parameters_set as Record<string, string[]>;
  assert(paramsSet.scalar.includes("Roughness"), "Roughness set");
  assert(paramsSet.vector.includes("BaseColor"), "BaseColor set");
});

test("material_create instance without required parent returns error", async () => {
  server.reset();
  const tool = toolMap.get("material_create")!;
  const result = tool.inputSchema.safeParse({
    name: "MI_NeedParent",
    path: "/Game/Materials",
    type: "instance",
  });
  assert(!result.success, "Should reject instance without parent");
});

test("material_create base material (no parent needed)", async () => {
  server.reset();
  const result = await callTool("material_create", {
    name: "M_NewBase",
    path: "/Game/Materials",
    type: "material",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.type, "material", "type");
  assertEqual(data.parent, null, "no parent");
});
test("material_create schema accepts a simple opaque surface", async () => {
  const tool = toolMap.get("material_create")!;
  assert(tool.inputSchema.safeParse({
    name: "M_Simple",
    path: "/Game/Materials",
    type: "material",
    simple: { base_color: [0.2, 0.3, 0.4], roughness: 0.6, metallic: 0.1 },
  }).success, "Should accept a simple material graph");
  assert(!tool.inputSchema.safeParse({
    name: "MI_InvalidSimple",
    path: "/Game/Materials",
    type: "instance",
    parent: "/Game/Materials/M_BrickWall",
    simple: { base_color: [0.2, 0.3, 0.4] },
  }).success, "Should reject a simple graph on an instance");
});

test("material_create rejects duplicate path", async () => {
  server.reset();
  const result = await callTool("material_create", {
    name: "M_BrickWall",
    path: "/Game/Materials",
    type: "material",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "already exists", "error");
});

test("material_create instance with missing parent returns error", async () => {
  server.reset();
  const result = await callTool("material_create", {
    name: "MI_Orphan",
    path: "/Game/Materials",
    type: "instance",
    parent: "/Game/Materials/M_DoesNotExist",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error");
});

// ---- material_apply Tests ----

test("material_apply with slot_index succeeds", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Meshes/SM_Cube", name: "ApplyTarget" });
  const result = await callTool("material_apply", {
    actor_name: "ApplyTarget",
    material_path: "/Game/Materials/M_BrickWall",
    slot_index: 0,
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.actor, "ApplyTarget", "actor");
  assertEqual(data.material_applied, "/Game/Materials/M_BrickWall", "applied");
  assertEqual(data.slot_index, 0, "slot");
  assert(typeof data.total_slots === "number", "total_slots is number");
});

test("material_apply with non-existent actor returns error", async () => {
  server.reset();
  const result = await callTool("material_apply", {
    actor_name: "Ghost",
    material_path: "/Game/Materials/M_BrickWall",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error");
});

test("material_apply with non-existent material returns error", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "MatTarget" });
  const result = await callTool("material_apply", {
    actor_name: "MatTarget",
    material_path: "/Game/Materials/M_DoesNotExist",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error");
});

test("material_apply with out-of-range slot returns error", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "SlotTarget" });
  const result = await callTool("material_apply", {
    actor_name: "SlotTarget",
    material_path: "/Game/Materials/M_BrickWall",
    slot_index: 99,
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "out of range", "error");
});

// ---- blueprint_list Tests ----

test("blueprint_list returns empty when no blueprints exist", async () => {
  server.reset();
  const result = await callTool("blueprint_list", {});
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 0, "no blueprints");
});

test("blueprint_list returns created blueprints", async () => {
  server.reset();
  await callTool("blueprint_create", { name: "BP_Test1", path: "/Game/Blueprints", parent_class: "Actor" });
  await callTool("blueprint_create", { name: "BP_Test2", path: "/Game/Blueprints", parent_class: "Pawn" });
  const result = await callTool("blueprint_list", {});
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 2, "count");
});

test("blueprint_list with parent_class_filter", async () => {
  server.reset();
  await callTool("blueprint_create", { name: "BP_Actor1", path: "/Game/Blueprints", parent_class: "Actor" });
  await callTool("blueprint_create", { name: "BP_Pawn1", path: "/Game/Blueprints", parent_class: "Pawn" });
  const result = await callTool("blueprint_list", { parent_class_filter: "Actor" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 1, "only Actor blueprints");
});

test("blueprint_list with name_filter wildcard", async () => {
  server.reset();
  await callTool("blueprint_create", { name: "BP_Enemy_A", path: "/Game/Blueprints", parent_class: "Actor" });
  await callTool("blueprint_create", { name: "BP_Enemy_B", path: "/Game/Blueprints", parent_class: "Actor" });
  await callTool("blueprint_create", { name: "BP_Player", path: "/Game/Blueprints", parent_class: "Actor" });
  const result = await callTool("blueprint_list", { name_filter: "BP_Enemy_*" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count, 2, "2 enemy blueprints");
});

// ---- blueprint_info Tests ----

test("blueprint_info returns full structure", async () => {
  server.reset();
  await callTool("blueprint_create", {
    name: "BP_Full",
    path: "/Game/Blueprints",
    parent_class: "Actor",
    components: [
      { name: "Root", class: "SceneComponent" },
      { name: "Mesh", class: "StaticMeshComponent", attach_to: "Root" },
    ],
    variables: [
      { name: "Health", type: "Float", category: "Stats" },
    ],
  });
  const result = await callTool("blueprint_info", { blueprint_path: "/Game/Blueprints/BP_Full" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.parent_class, "Actor", "parent_class");
  assertEqual(data.component_count, 2, "component_count");
  assertEqual(data.variable_count, 1, "variable_count");
  assert(Array.isArray(data.parent_chain), "parent_chain is array");
});

test("blueprint_info with non-existent path returns error", async () => {
  server.reset();
  const result = await callTool("blueprint_info", { blueprint_path: "/Game/Blueprints/BP_Missing" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error");
});

test("blueprint_info rejects path not starting with /", async () => {
  const tool = toolMap.get("blueprint_info")!;
  const result = tool.inputSchema.safeParse({ blueprint_path: "Game/BP_Test" });
  assert(!result.success, "Should reject path not starting with /");
});

// ---- blueprint_create Tests ----

test("blueprint_create minimal (name, path, parent_class only)", async () => {
  server.reset();
  const result = await callTool("blueprint_create", {
    name: "BP_Minimal",
    path: "/Game/Blueprints",
    parent_class: "Actor",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.name, "BP_Minimal", "name");
  assertEqual(data.parent_class, "Actor", "parent_class");
  assertEqual(data.compiled, true, "compiled");
});

test("blueprint_create with components", async () => {
  server.reset();
  const result = await callTool("blueprint_create", {
    name: "BP_WithComps",
    path: "/Game/Blueprints",
    parent_class: "Actor",
    components: [
      { name: "TriggerBox", class: "BoxComponent" },
      { name: "Mesh", class: "StaticMeshComponent" },
    ],
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const added = data.components_added as string[];
  assertEqual(added.length, 2, "2 components added");
  assert(added.includes("TriggerBox"), "TriggerBox added");
  assert(added.includes("Mesh"), "Mesh added");
});

test("blueprint_create with variables", async () => {
  server.reset();
  const result = await callTool("blueprint_create", {
    name: "BP_WithVars",
    path: "/Game/Blueprints",
    parent_class: "Actor",
    variables: [
      { name: "Health", type: "Float", category: "Stats", tooltip: "Current health" },
      { name: "IsAlive", type: "Boolean" },
    ],
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const added = data.variables_added as string[];
  assertEqual(added.length, 2, "2 variables added");
});

test("blueprint_create with components and variables", async () => {
  server.reset();
  const result = await callTool("blueprint_create", {
    name: "BP_FullCreate",
    path: "/Game/Blueprints",
    parent_class: "Character",
    components: [{ name: "Hitbox", class: "CapsuleComponent" }],
    variables: [{ name: "Speed", type: "Float" }],
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual((data.components_added as string[]).length, 1, "1 component");
  assertEqual((data.variables_added as string[]).length, 1, "1 variable");
  assertEqual(data.parent_class, "Character", "Character parent");
});

test("blueprint_create rejects existing asset path", async () => {
  server.reset();
  await callTool("blueprint_create", { name: "BP_Dup", path: "/Game/Blueprints", parent_class: "Actor" });
  const result = await callTool("blueprint_create", { name: "BP_Dup", path: "/Game/Blueprints", parent_class: "Actor" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "already exists", "error");
});

test("blueprint_create rejects unknown parent class", async () => {
  server.reset();
  const result = await callTool("blueprint_create", {
    name: "BP_BadParent",
    path: "/Game/Blueprints",
    parent_class: "NonExistentClass",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "Unknown parent class", "error");
});

// ---- blueprint_compile Tests ----

test("blueprint_compile success case", async () => {
  server.reset();
  await callTool("blueprint_create", { name: "BP_Compile", path: "/Game/Blueprints", parent_class: "Actor" });
  const result = await callTool("blueprint_compile", { blueprint_path: "/Game/Blueprints/BP_Compile" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.compiled, true, "compiled");
  assertEqual(data.had_errors, false, "no errors");
});

test("blueprint_compile with non-existent path returns error", async () => {
  server.reset();
  const result = await callTool("blueprint_compile", { blueprint_path: "/Game/Blueprints/BP_Missing" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error");
});

test("blueprint_compile rejects path not starting with /", async () => {
  const tool = toolMap.get("blueprint_compile")!;
  const result = tool.inputSchema.safeParse({ blueprint_path: "Game/BP_Test" });
  assert(!result.success, "Should reject path not starting with /");
});

// ---- blueprint_document Tests ----

test("blueprint_document minimal detail level", async () => {
  server.reset();
  await callTool("blueprint_create", {
    name: "BP_DocMin",
    path: "/Game/Blueprints",
    parent_class: "Actor",
    components: [{ name: "Root", class: "SceneComponent" }],
    variables: [{ name: "Enabled", type: "Boolean" }],
  });
  const result = await callTool("blueprint_document", {
    blueprint_path: "/Game/Blueprints/BP_DocMin",
    detail_level: "minimal",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const doc = data.documentation as string;
  assertIncludes(doc, "BP_DocMin", "contains name");
  assertIncludes(doc, "Components: 1", "component count");
  assertIncludes(doc, "Variables: 1", "variable count");
});

test("blueprint_document standard detail level", async () => {
  server.reset();
  await callTool("blueprint_create", {
    name: "BP_DocStd",
    path: "/Game/Blueprints",
    parent_class: "Actor",
    components: [
      { name: "DefaultSceneRoot", class: "SceneComponent" },
      { name: "TriggerBox", class: "BoxComponent" },
    ],
    variables: [{ name: "TriggerEnabled", type: "Boolean", category: "Conditions" }],
  });
  const result = await callTool("blueprint_document", {
    blueprint_path: "/Game/Blueprints/BP_DocStd",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const doc = data.documentation as string;
  assertIncludes(doc, "## Components (2)", "component section");
  assertIncludes(doc, "TriggerBox", "component listed");
  assertIncludes(doc, "## Variables (1)", "variable section");
  assertIncludes(doc, "Conditions", "category shown");
});

test("blueprint_document detailed level", async () => {
  server.reset();
  await callTool("blueprint_create", {
    name: "BP_DocDetail",
    path: "/Game/Blueprints",
    parent_class: "Pawn",
    components: [{ name: "Mesh", class: "StaticMeshComponent" }],
    variables: [{ name: "Speed", type: "Float", tooltip: "Movement speed" }],
  });
  const result = await callTool("blueprint_document", {
    blueprint_path: "/Game/Blueprints/BP_DocDetail",
    detail_level: "detailed",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const doc = data.documentation as string;
  assertIncludes(doc, "Parent: Pawn", "parent class");
  assertIncludes(doc, "## Event Graphs", "event graphs section");
});

// ---- Connection Error Tests ----

test("material tool handles connection error gracefully", async () => {
  const deadClient = new UnrealClient({ port: 19999, timeout: 1000 });
  const deadTools = createMaterialTools(deadClient);
  const tool = deadTools.find((t) => t.name === "material_list")!;
  const result = await tool.handler({});
  const parsed = JSON.parse(result.content[0].text) as Record<string, unknown>;
  assertEqual(parsed.success, false, "success");
  assertIncludes(parsed.error as string, "Connection failed", "error message");
});

test("blueprint tool handles connection error gracefully", async () => {
  const deadClient = new UnrealClient({ port: 19999, timeout: 1000 });
  const deadTools = createBlueprintTools(deadClient);
  const tool = deadTools.find((t) => t.name === "blueprint_list")!;
  const result = await tool.handler({});
  const parsed = JSON.parse(result.content[0].text) as Record<string, unknown>;
  assertEqual(parsed.success, false, "success");
  assertIncludes(parsed.error as string, "Connection failed", "error message");
});

// ---- Run ----

async function run(): Promise<void> {
  await setup();

  for (const t of tests) {
    try {
      await t.fn();
      passed++;
      console.log(`  PASS  ${t.name}`);
    } catch (err) {
      failed++;
      console.log(`  FAIL  ${t.name}`);
      console.log(`        ${(err as Error).message}`);
    }
  }

  await teardown();

  console.log(`\n${passed + failed} tests: ${passed} passed, ${failed} failed`);

  if (failed > 0) {
    process.exit(1);
  }
}

run().catch((err) => {
  console.error("Test runner crashed:", err);
  process.exit(1);
});
