/** Unit tests for the motion-matching and KAI locomotion debugger tools. */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createLocomotionTools } from "../src/tools/locomotion.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18777;
let server: MockUnrealServer;
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
  const client = new UnrealClient({ port: TEST_PORT });
  toolMap = new Map(createLocomotionTools(client).map((tool) => [tool.name, tool]));
  server.setHandler("locomotion_debug", (params) => ({
    success: true,
    data: { echoed: params },
  }));
}

test("all 21 locomotion tools are registered", async () => {
  const expected = [
    "locomotion_debug_snapshot",
    "locomotion_capture_start",
    "locomotion_capture_mark",
    "locomotion_capture_flag",
    "locomotion_capture_stop",
    "locomotion_capture_status",
    "locomotion_capture_list",
    "locomotion_capture_get",
    "motion_query_inspect",
    "motion_candidates_inspect",
    "motion_database_validate",
    "trajectory_debug",
    "pose_history_inspect",
    "animation_state_inspect",
    "root_motion_inspect",
    "contact_debug",
    "locomotion_overlay_set",
    "locomotion_force_candidate",
    "locomotion_weights_set",
    "locomotion_capture_compare",
    "locomotion_performance_get",
  ];
  assertEqual(toolMap.size, expected.length, "tool count");
  for (const name of expected) assert(toolMap.has(name), `${name} should be registered`);
});

test("snapshot forwards through the single locomotion_debug command", async () => {
  const result = await callTool("locomotion_debug_snapshot", { include_candidates: true });
  assertEqual(result.success as boolean, true, "snapshot success");
  const requests = server.getRequestLog();
  assertEqual(requests[0].command, "locomotion_debug", "bridge command");
  assertEqual(requests[0].params.operation as string, "snapshot", "operation");
  assertEqual(requests[0].params.include_candidates as boolean, true, "candidate flag");
});

test("visual glitch marker forwards a bounded note", async () => {
  await callTool("locomotion_capture_flag", { note: "left foot popped" });
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "capture_flag", "flag operation");
  assertEqual(request.params.note as string, "left foot popped", "flag note");
  const tool = toolMap.get("locomotion_capture_flag");
  if (!tool) throw new Error("flag tool missing");
  assert(!tool.inputSchema.safeParse({ note: "x".repeat(161) }).success, "notes above 160 characters should fail");
});

test("candidate inspection uses its focused operation", async () => {
  await callTool("motion_candidates_inspect", {});
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "candidates_inspect", "candidate operation");
});

test("force candidate switches to clear operation when requested", async () => {
  await callTool("locomotion_force_candidate", { clear: true });
  assertEqual(server.getRequestLog().slice(-1)[0].params.operation as string, "force_candidate_clear", "clear operation");
  await callTool("locomotion_force_candidate", { sample_index: 42, allow_incompatible: false });
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "force_candidate", "force operation");
  assertEqual(request.params.sample_index as number, 42, "sample index");
});

test("weight reset never forwards a persistent settings mutation", async () => {
  await callTool("locomotion_weights_set", { reset: true });
  assertEqual(server.getRequestLog().slice(-1)[0].params.operation as string, "weights_clear", "weight reset operation");
});

test("capture schemas reject unsafe row limits", async () => {
  const tool = toolMap.get("locomotion_capture_get");
  if (!tool) throw new Error("capture tool missing");
  assert(!tool.inputSchema.safeParse({ limit: 501 }).success, "limit above 500 should fail");
  assert(!tool.inputSchema.safeParse({ limit: 0 }).success, "zero limit should fail");
});

test("database validation forwards an asset object path", async () => {
  const path = "/Game/Animation/DA_Locomotion.DA_Locomotion";
  await callTool("motion_database_validate", { database_path: path });
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "database_validate", "validation operation");
  assertEqual(request.params.database_path as string, path, "database path");
});

async function run(): Promise<void> {
  await setup();
  for (const current of tests) {
    try {
      await current.fn();
      console.log(`  PASS  ${current.name}`);
      passed++;
    } catch (error) {
      console.log(`  FAIL  ${current.name}: ${(error as Error).message}`);
      failed++;
    }
  }
  await server.stop();
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run();
