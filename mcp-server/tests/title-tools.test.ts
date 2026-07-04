/**
 * Unit tests for data-driven title manifest MCP tools.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createTitleTools } from "../src/tools/titles.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18773;

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
  const tools = createTitleTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  server.setHandler("title_manifest_create", (params) => ({
    success: true,
    data: {
      path: "D:/Project/Saved/MCP/titles/seinfeld_titles.json",
      manifest: { id: params.id, cues: params.cues, resolution: params.resolution ?? [1920, 1080] },
      validation: { valid: true, errors: [], warnings: [], cue_count: (params.cues as unknown[]).length },
    },
  }));

  server.setHandler("title_manifest_validate", (params) => ({
    success: true,
    data: {
      manifest: params.manifest ?? { id: params.id },
      validation: { valid: true, errors: [], warnings: [], cue_count: 1 },
    },
  }));

  server.setHandler("title_manifest_from_reference", (params) => ({
    success: true,
    data: {
      path: "D:/Project/Saved/MCP/titles/reference_titles.json",
      manifest: {
        id: params.id,
        cues: [{ id: "main_title", box: [0.203125, 0.7013888889, 0.41015625, 0.1597222222] }],
      },
    },
  }));

  server.setHandler("title_widget_build_from_manifest", (params) => ({
    success: true,
    data: {
      widget_spec: { root: { type: "CanvasPanel", name: "RootCanvas" }, title_manifest: params.manifest },
    },
  }));

  server.setHandler("title_controller_create", (params) => ({
    success: true,
    data: { controller_spec_path: "D:/Project/Saved/MCP/titles/BP_TitleController_controller_spec.json" },
  }));

  server.setHandler("title_sequence_bind", (params) => ({
    success: true,
    data: { sequence_spec: { sequence_path: params.sequence_path, durationFrames: 132 } },
  }));

  server.setHandler("title_render_compare", () => ({
    success: true,
    data: { available: true, mean_absolute_error: 4.2, max_channel_error: 32, size: [1920, 1080] },
  }));

  server.setHandler("title_manifest_adjust", (params) => ({
    success: true,
    data: { path: "D:/Project/Saved/MCP/titles/seinfeld_titles.json", adjustments: params.adjustments },
  }));
}

async function teardown(): Promise<void> {
  await server.stop();
}

test("title tools are registered", async () => {
  for (const name of [
    "title_manifest_create",
    "title_manifest_validate",
    "title_manifest_from_reference",
    "title_widget_build_from_manifest",
    "title_controller_create",
    "title_sequence_bind",
    "title_render_compare",
    "title_manifest_adjust",
  ]) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
});

test("title_manifest_create forwards manifest cues", async () => {
  const result = await callTool("title_manifest_create", {
    id: "seinfeld_titles",
    resolution: [1920, 1080],
    fps: 29.97,
    cues: [{
      id: "main_title",
      type: "main_title",
      text: "SEINFELD",
      box: [0.2, 0.7, 0.41, 0.16],
      holdFrames: 90,
    }],
  });
  assertEqual(result.success as boolean, true, "create success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.path as string, "D:/Project/Saved/MCP/titles/seinfeld_titles.json", "manifest path");
});

test("title_manifest_from_reference supports measured pixel boxes", async () => {
  const result = await callTool("title_manifest_from_reference", {
    id: "reference_titles",
    frame_size: [1280, 720],
    cues: [{
      id: "main_title",
      type: "main_title",
      text: "SEINFELD",
      pixel_box: [260, 505, 525, 115],
    }],
  });
  assertEqual(result.success as boolean, true, "from reference success");
  const data = result.data as Record<string, unknown>;
  const manifest = data.manifest as Record<string, unknown>;
  const cues = manifest.cues as Array<Record<string, unknown>>;
  const box = cues[0].box as number[];
  assert(Math.abs(box[0] - 0.203125) < 0.00001, "normalized x should be returned");
});

test("title_render_compare forwards comparison paths", async () => {
  const result = await callTool("title_render_compare", {
    reference_path: "D:/refs/title.png",
    render_path: "D:/renders/title.png",
  });
  assertEqual(result.success as boolean, true, "compare success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.mean_absolute_error as number, 4.2, "mean error");
});

test("title_manifest_adjust accepts correction pass", async () => {
  const result = await callTool("title_manifest_adjust", {
    id: "seinfeld_titles",
    adjustments: [{ id: "main_title", box_delta: [0.01, -0.01, 0, 0] }],
  });
  assertEqual(result.success as boolean, true, "adjust success");
  const data = result.data as Record<string, unknown>;
  const adjustments = data.adjustments as Array<Record<string, unknown>>;
  assertEqual(adjustments[0].id as string, "main_title", "adjusted cue");
});

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
