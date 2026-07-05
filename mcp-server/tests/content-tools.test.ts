/**
 * Unit tests for content tools (material instances, data tables, audio,
 * maps, game template). Mock server; no UE4 instance required.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createContentTools } from "../src/tools/content.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18775;

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
  toolMap = new Map(createContentTools(client).map((t) => [t.name, t]));

  server.setHandler("material_instance_create", (params) => ({
    success: true, data: { material_instance: "/Game/MI_X", received: params },
  }));
  server.setHandler("data_table_create", (params) => ({
    success: true, data: { data_table: "/Game/DT_X", rows: ["Row1"], row_count: 1, received: params },
  }));
  server.setHandler("game_template_create", (params) => ({
    success: true,
    data: { template: (params as Record<string, unknown>).template, framework: { game_mode: "/Game/GM" } },
  }));
}

const EXPECTED = [
  "material_instance_create", "material_instance_set_params",
  "data_table_create", "data_table_fill_from_json",
  "audio_component_add", "level_new", "game_template_create",
];

test("all 7 content tools are registered", async () => {
  for (const name of EXPECTED) assert(toolMap.has(name), `missing tool: ${name}`);
  assert(toolMap.size === EXPECTED.length, `expected ${EXPECTED.length}, got ${toolMap.size}`);
});

test("material_instance_create forwards parameter overrides", async () => {
  const result = await callTool("material_instance_create", {
    path: "/Game", name: "MI_X", parent_material: "/Game/M_X",
    scalar_params: { Roughness: 0.5 },
  });
  assert(result.success === true, "should succeed");
  const received = (result.data as Record<string, unknown>).received as Record<string, unknown>;
  const scalars = received.scalar_params as Record<string, number>;
  assert(scalars.Roughness === 0.5, "scalar param forwarded");
});

test("data_table_create returns row names", async () => {
  const result = await callTool("data_table_create", {
    path: "/Game", name: "DT_X", row_struct: "/Script/GameplayTags.GameplayTagTableRow",
    rows_json: [{ Name: "Row1", Tag: "A.B" }],
  });
  const data = result.data as Record<string, unknown>;
  assert(data.row_count === 1, "row count returned");
});

test("game_template_create forwards template name", async () => {
  const result = await callTool("game_template_create", {
    path: "/Game/MyGame", base_name: "MyGame", template: "first_person",
  });
  const data = result.data as Record<string, unknown>;
  assert(data.template === "first_person", "template forwarded");
});

async function run(): Promise<void> {
  await setup();
  for (const t of tests) {
    try {
      await t.fn();
      passed += 1;
      console.log(`  PASS  ${t.name}`);
    } catch (err) {
      failed += 1;
      console.error(`  FAIL  ${t.name}`);
      console.error(`        ${(err as Error).message}`);
    }
  }
  await server.stop();
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run();
