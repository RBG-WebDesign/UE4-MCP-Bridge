/**
 * Unit tests for actor tools.
 *
 * Uses MockUnrealServer to simulate the Python listener so we can test
 * tool registration, Zod input validation, and error handling without UE4.
 *
 * Run: npx tsx mcp-server/tests/actor-tools.test.ts
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createActorTools } from "../src/tools/actors.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18765;

let server: MockUnrealServer;
let client: UnrealClient;
let tools: ToolDefinition[];
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
  tools = createActorTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));
}

async function teardown(): Promise<void> {
  await server.stop();
}

// ---- Tool Registration Tests ----

test("all 9 actor tools are registered", async () => {
  const expectedNames = [
    "actor_selection", "actor_spawn", "actor_duplicate", "actor_delete", "actor_modify",
    "actor_organize", "actor_snap_to_socket", "batch_spawn", "placement_validate",
  ];
  for (const name of expectedNames) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(tools.length, 9, "total tool count");
});

test("actor_spawn schema requires asset_path", async () => {
  const tool = toolMap.get("actor_spawn")!;
  const schema = tool.inputSchema;
  const result = schema.safeParse({});
  assert(!result.success, "Should reject missing asset_path");
});

test("actor_spawn schema validates vector types", async () => {
  const tool = toolMap.get("actor_spawn")!;
  const result = tool.inputSchema.safeParse({
    asset_path: "/Game/Test",
    location: { x: "not_a_number", y: 0, z: 0 },
  });
  assert(!result.success, "Should reject non-numeric vector component");
});

test("actor_modify schema requires actor_name", async () => {
  const tool = toolMap.get("actor_modify")!;
  const result = tool.inputSchema.safeParse({});
  assert(!result.success, "Should reject missing actor_name");
});

test("actor_delete schema requires actor_name", async () => {
  const tool = toolMap.get("actor_delete")!;
  const result = tool.inputSchema.safeParse({});
  assert(!result.success, "Should reject missing actor_name");
});

test("batch_spawn schema requires spawns array", async () => {
  const tool = toolMap.get("batch_spawn")!;
  const result = tool.inputSchema.safeParse({});
  assert(!result.success, "Should reject missing spawns");
});

test("scene adapter schemas reject unmappable fields", async () => {
  const duplicate = toolMap.get("actor_duplicate")!;
  const duplicateResult = duplicate.inputSchema.safeParse({ actor_name: "Cube", unsupported: true });
  assert(!duplicateResult.success, "actor_duplicate must reject unmappable fields");

  const batch = toolMap.get("batch_spawn")!;
  const batchResult = batch.inputSchema.safeParse({
    spawns: [{ asset_path: "/Script/Engine.StaticMeshActor", unsupported: true }],
  });
  assert(!batchResult.success, "batch_spawn must reject unmappable fields");
});

test("placement_validate schema requires actors array", async () => {
  const tool = toolMap.get("placement_validate")!;
  const result = tool.inputSchema.safeParse({});
  assert(!result.success, "Should reject missing actors");
});

// ---- actor_spawn Tests ----

test("actor_spawn succeeds with valid params", async () => {
  server.reset();
  const result = await callTool("actor_spawn", {
    asset_path: "/Game/Meshes/SM_Cube",
    location: { x: 100, y: 200, z: 300 },
    name: "TestCube",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.name, "TestCube", "actor name");
  const validation = data.validation as Record<string, unknown>;
  assertEqual(validation.valid, true, "validation.valid");
});

test("actor_spawn fails with empty asset_path", async () => {
  server.reset();
  const result = await callTool("actor_spawn", {
    asset_path: "",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "asset_path", "error message");
});

test("actor_spawn fails with invalid asset_path format", async () => {
  server.reset();
  const result = await callTool("actor_spawn", {
    asset_path: "Game/Meshes/SM_Cube",
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "must start with '/'", "error message");
});

test("actor_spawn rejects zero scale", async () => {
  server.reset();
  const result = await callTool("actor_spawn", {
    asset_path: "/Game/Meshes/SM_Cube",
    scale: { x: 1, y: 0, z: 1 },
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "Scale", "error message");
});

test("actor_spawn with validate=false omits validation", async () => {
  server.reset();
  const result = await callTool("actor_spawn", {
    asset_path: "/Game/Meshes/SM_Cube",
    validate: false,
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.validation, undefined, "validation should be absent");
});

// ---- actor_modify Tests ----

test("actor_modify succeeds with location change", async () => {
  server.reset();
  // Spawn first
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "Movable" });
  const result = await callTool("actor_modify", {
    actor_name: "Movable",
    location: { x: 500, y: 500, z: 500 },
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  const modified = data.modified_properties as string[];
  assert(modified.includes("location"), "should include location");
});

test("actor_modify fails for non-existent actor", async () => {
  server.reset();
  const result = await callTool("actor_modify", {
    actor_name: "DoesNotExist",
    location: { x: 0, y: 0, z: 0 },
  });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error message");
});

test("actor_modify fails with no properties", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "NoMod" });
  const result = await callTool("actor_modify", { actor_name: "NoMod" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "No properties", "error message");
});

// ---- actor_delete Tests ----

test("actor_delete succeeds for existing actor", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "DeleteMe" });
  const result = await callTool("actor_delete", { actor_name: "DeleteMe" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.deleted_count, 1, "deleted_count");
});

test("actor_delete fails for non-existent actor", async () => {
  server.reset();
  const result = await callTool("actor_delete", { actor_name: "Ghost" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "No actors found", "error message");
});

test("actor_delete with wildcard pattern", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "Wall_01" });
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "Wall_02" });
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "Floor_01" });
  const result = await callTool("actor_delete", { actor_name: "Wall_*" });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.deleted_count, 2, "deleted_count");
});

// ---- actor_duplicate Tests ----

test("actor_duplicate succeeds with offset", async () => {
  server.reset();
  await callTool("actor_spawn", {
    asset_path: "/Game/Test",
    name: "Original",
    location: { x: 100, y: 0, z: 0 },
  });
  const result = await callTool("actor_duplicate", {
    actor_name: "Original",
    offset: { x: 200, y: 0, z: 0 },
    new_name: "TheCopy",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.name, "TheCopy", "copy name");
  const loc = data.location as { x: number; y: number; z: number };
  assertEqual(loc.x, 300, "copy X = original X + offset X");
});

test("actor_duplicate fails for non-existent actor", async () => {
  server.reset();
  const result = await callTool("actor_duplicate", { actor_name: "Nope" });
  assertEqual(result.success, false, "success");
  assertIncludes(result.error as string, "not found", "error message");
});

// ---- actor_organize Tests ----

test("actor_organize moves actors to folder", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "A" });
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "B" });
  const result = await callTool("actor_organize", {
    actors: ["A", "B"],
    folder: "Environment/Walls",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual((data.moved as string[]).length, 2, "moved count");
});

test("actor_organize reports not-found actors", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "Exists" });
  const result = await callTool("actor_organize", {
    actors: ["Exists", "Missing"],
    folder: "TestFolder",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual((data.not_found as string[]).length, 1, "not_found count");
});

// ---- batch_spawn Tests ----

test("batch_spawn spawns multiple actors", async () => {
  server.reset();
  const result = await callTool("batch_spawn", {
    spawns: [
      { asset_path: "/Game/Test", name: "B1", location: { x: 0, y: 0, z: 0 } },
      { asset_path: "/Game/Test", name: "B2", location: { x: 100, y: 0, z: 0 } },
      { asset_path: "/Game/Test", name: "B3", location: { x: 200, y: 0, z: 0 } },
    ],
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.total, 3, "total");
  assertEqual(data.succeeded, 3, "succeeded");
  assertEqual(data.failed, 0, "failed");
});

test("batch_spawn reports per-item failures", async () => {
  server.reset();
  const result = await callTool("batch_spawn", {
    spawns: [
      { asset_path: "/Game/Test", name: "Good" },
      { asset_path: "", name: "Bad" },
    ],
  });
  assertEqual(result.success, true, "overall success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.succeeded, 1, "succeeded");
  assertEqual(data.failed, 1, "failed");
});

// ---- placement_validate Tests ----

test("placement_validate returns results for actors", async () => {
  server.reset();
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "P1" });
  await callTool("actor_spawn", { asset_path: "/Game/Test", name: "P2" });
  const result = await callTool("placement_validate", {
    actors: ["P1", "P2"],
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.actors_checked, 2, "actors_checked");
});

// ---- Custom Error Handler Tests ----

test("handles connection error gracefully", async () => {
  // Use a client pointing to a port with no server
  const deadClient = new UnrealClient({ port: 19999, timeout: 1000 });
  const deadTools = createActorTools(deadClient);
  const spawnTool = deadTools.find((t) => t.name === "actor_spawn")!;
  const result = await spawnTool.handler({ asset_path: "/Game/Test" });
  const parsed = JSON.parse(result.content[0].text) as Record<string, unknown>;
  assertEqual(parsed.success, false, "success");
  assertIncludes(parsed.error as string, "Connection failed", "error message");
});

test("custom handler overrides default behavior", async () => {
  server.reset();
  server.setHandler("actor_spawn", () => ({
    success: false,
    data: {},
    error: "Custom error for testing",
  }));
  const result = await callTool("actor_spawn", {
    asset_path: "/Game/Test",
  });
  assertEqual(result.success, false, "success");
  assertEqual(result.error, "Custom error for testing", "custom error");
});

// ---- Run ----

async function run(): Promise<void> {
  await setup();

  for (const t of tests) {
    try {
      // Reset server state between tests unless test manages its own
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
