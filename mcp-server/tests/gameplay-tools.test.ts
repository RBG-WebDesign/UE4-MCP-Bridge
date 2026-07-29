/**
 * Unit tests for gameplay tools.
 *
 * Uses MockUnrealServer with setHandler() to simulate UE4 PIE responses.
 * No UE4 instance required.
 *
 * Run: npx tsx mcp-server/tests/gameplay-tools.test.ts
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createGameplayTools } from "../src/tools/gameplay.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18770;

let server: MockUnrealServer;
let client: UnrealClient;
let toolMap: Map<string, ToolDefinition>;

interface TestCase { name: string; fn: () => Promise<void>; }
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

async function callTool(name: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse((result.content[0] as { text: string }).text) as Record<string, unknown>;
}

async function setup(): Promise<void> {
  server = new MockUnrealServer();
  await server.start(TEST_PORT);
  client = new UnrealClient({ port: TEST_PORT });
  const tools = createGameplayTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  // Register mock handlers for all 4 gameplay commands
  server.setHandler("gameplay_pie_start", (_params) => ({
    success: true,
    data: { status: "pie_ready" },
  }));

  server.setHandler("gameplay_pie_stop", (_params) => ({
    success: true,
    data: { status: "pie_stopped" },
  }));

  server.setHandler("gameplay_telemetry_snapshot", (_params) => ({
    success: true,
    data: {
      log_lines: ["[2026.03.18] GameStarted", "[2026.03.18] PIE: play in editor start"],
      possessed_pawn_class: "BP_Character_C",
      ai_controller_states: { "BP_Enemy_1": "active_with_blackboard" },
      visible_widgets: ["WBP_HUD_C"],
      fps: 60.0,
      pie_world_name: "World_TestLevel",
    },
  }));

  server.setHandler("gameplay_run_acceptance_tests", (params) => {
    const predicates = (params.tests as string[]) ?? [];
    return {
      success: true,
      data: {
        total: predicates.length,
        passed: predicates.length,
        failed: 0,
        results: predicates.map((t) => {
          const parts = t.split(":");
          return { predicate: parts[0], target: parts[1] ?? null, passed: true, observed: "mock" };
        }),
      },
    };
  });
}

async function teardown(): Promise<void> {
  await server.stop();
}

// ---- Tool registration ----

test("all 5 gameplay tools are registered", async () => {
  const expected = [
    "gameplay_pie_start",
    "gameplay_pie_stop",
    // Added 2026-07-29. gameplay_pie_start cannot wait for readiness: handlers
    // run on the game thread, so waiting blocks the tick that starts PIE. It
    // returns as soon as the request is queued, and callers poll this.
    "gameplay_pie_status",
    "gameplay_telemetry_snapshot",
    "gameplay_run_acceptance_tests",
  ];
  for (const name of expected) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(toolMap.size, 5, "gameplay tool count");
});

// ---- gameplay_pie_start ----

test("gameplay_pie_start returns pie_ready status", async () => {
  const result = await callTool("gameplay_pie_start", {});
  assertEqual(result.success as boolean, true, "pie_start success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.status as string, "pie_ready", "pie_start status");
});

test("gameplay_pie_start accepts optional level_path", async () => {
  const result = await callTool("gameplay_pie_start", { level_path: "/Game/Maps/TestLevel" });
  assertEqual(result.success as boolean, true, "pie_start with level_path");
});

// ---- gameplay_pie_stop ----

test("gameplay_pie_stop returns pie_stopped status", async () => {
  const result = await callTool("gameplay_pie_stop", {});
  assertEqual(result.success as boolean, true, "pie_stop success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.status as string, "pie_stopped", "pie_stop status");
});

// ---- gameplay_telemetry_snapshot ----

test("gameplay_telemetry_snapshot returns telemetry frame shape", async () => {
  const result = await callTool("gameplay_telemetry_snapshot", {});
  assertEqual(result.success as boolean, true, "snapshot success");
  const data = result.data as Record<string, unknown>;
  assert(Array.isArray(data.log_lines), "log_lines is array");
  assert(typeof data.possessed_pawn_class === "string", "possessed_pawn_class is string");
  assert(Array.isArray(data.visible_widgets), "visible_widgets is array");
  assert(typeof data.ai_controller_states === "object", "ai_controller_states is object");
  assert(typeof data.fps === "number", "fps is number");
  assert("pie_world_name" in data, "pie_world_name field present");
});

// ---- gameplay_run_acceptance_tests ----

test("gameplay_run_acceptance_tests rejects empty tests array (Zod validation)", async () => {
  const tool = toolMap.get("gameplay_run_acceptance_tests");
  if (!tool) throw new Error("Tool not found: gameplay_run_acceptance_tests");
  // Zod validation is enforced by the MCP framework before handlers run.
  // We verify it directly via safeParse() since callTool() bypasses the framework layer.
  const result = tool.inputSchema.safeParse({ tests: [] });
  assert(!result.success, "empty tests array should fail Zod validation");
});

test("gameplay_run_acceptance_tests returns results array with correct shape", async () => {
  const result = await callTool("gameplay_run_acceptance_tests", {
    tests: ["pawn_possessed:BP_Character", "log_contains:GameStarted"],
  });
  assertEqual(result.success as boolean, true, "run_tests success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.total as number, 2, "total is 2");
  assertEqual(data.passed as number, 2, "passed is 2");
  const results = data.results as Array<Record<string, unknown>>;
  assert(Array.isArray(results), "results is array");
  assertEqual(results.length, 2, "results has 2 entries");
  assert("predicate" in results[0], "result has predicate");
  assert("passed" in results[0], "result has passed");
  assert("observed" in results[0], "result has observed");
});

test("gameplay_run_acceptance_tests accepts optional timeout_seconds", async () => {
  const result = await callTool("gameplay_run_acceptance_tests", {
    tests: ["survive:5"],
    timeout_seconds: 10,
  });
  assertEqual(result.success as boolean, true, "run_tests with timeout_seconds");
});

// ---- Runner ----

async function run(): Promise<void> {
  await setup();
  for (const t of tests) {
    try {
      await t.fn();
      console.log(`  PASS  ${t.name}`);
      passed++;
    } catch (e) {
      console.log(`  FAIL  ${t.name}: ${(e as Error).message}`);
      failed++;
    }
  }
  await teardown();
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run();
