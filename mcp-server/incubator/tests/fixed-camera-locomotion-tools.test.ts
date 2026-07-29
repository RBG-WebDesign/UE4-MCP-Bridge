/** Unit tests for the fixed-camera locomotion debugger tools. */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createFixedCameraLocomotionTools } from "../src/tools/fixed-camera-locomotion.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18779;
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
  toolMap = new Map(createFixedCameraLocomotionTools(client).map((tool) => [tool.name, tool]));
  server.setHandler("fixed_camera_locomotion_debug", (params) => ({
    success: true,
    data: { echoed: params },
  }));
}

test("all four fixed-camera tools are registered", async () => {
  const expected = [
    "fixed_camera_locomotion_snapshot",
    "fixed_camera_locomotion_overlay_set",
    "fixed_camera_locomotion_transition_force",
    "fixed_camera_locomotion_reset",
  ];
  assertEqual(toolMap.size, expected.length, "tool count");
  for (const name of expected) assert(toolMap.has(name), `${name} should be registered`);
});

test("snapshot forwards to the isolated bridge command", async () => {
  const result = await callTool("fixed_camera_locomotion_snapshot", {});
  assertEqual(result.success as boolean, true, "snapshot success");
  const request = server.getRequestLog()[0];
  assertEqual(request.command, "fixed_camera_locomotion_debug", "bridge command");
  assertEqual(request.params.operation as string, "snapshot", "operation");
});

test("overlay forwards a runtime-only toggle", async () => {
  await callTool("fixed_camera_locomotion_overlay_set", { enabled: false });
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "overlay_set", "operation");
  assertEqual(request.params.enabled as boolean, false, "enabled flag");
});

test("transition schema permits only known montage sections", async () => {
  const tool = toolMap.get("fixed_camera_locomotion_transition_force");
  if (!tool) throw new Error("transition tool missing");
  assert(tool.inputSchema.safeParse({ section: "Spin180_L" }).success, "known section should pass");
  assert(!tool.inputSchema.safeParse({ section: "BadSection" }).success, "unknown section should fail");
  await callTool("fixed_camera_locomotion_transition_force", { section: "Turn90_R" });
  const request = server.getRequestLog().slice(-1)[0];
  assertEqual(request.params.operation as string, "transition_force", "operation");
  assertEqual(request.params.section as string, "Turn90_R", "section");
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