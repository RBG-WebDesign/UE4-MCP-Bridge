/**
 * Unit tests for title and lower-third widget MCP tools.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createPromptBrushTools } from "../src/tools/promptbrush.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18772;

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
  const tools = createPromptBrushTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  server.setHandler("widget_title_template", (params) => ({
    success: true,
    data: {
      spec: {
        root: { type: "CanvasPanel", name: "RootCanvas" },
        animations: [
          { name: "FadeIn", target: "TitleGroup", duration: params.fade_in ?? 0.35 },
          { name: "FadeOut", target: "TitleGroup", duration: params.fade_out ?? 0.45 },
        ],
      },
    },
  }));

  server.setHandler("widget_title_card_create", (params) => ({
    success: true,
    data: {
      path: `${params.package_path ?? "/Game/UI/Titles"}/${params.asset_name ?? "WBP_TitleCard"}`,
      animations: ["FadeIn", "FadeOut", "TitleSequence"],
    },
  }));

  server.setHandler("widget_lower_third_create", (params) => ({
    success: true,
    data: {
      path: `${params.package_path ?? "/Game/UI/Titles"}/${params.asset_name ?? "WBP_LowerThird"}`,
      animations: ["FadeIn", "FadeOut", "TitleSequence"],
    },
  }));
}

async function teardown(): Promise<void> {
  await server.stop();
}

test("title widget tools are registered", async () => {
  for (const name of ["widget_title_template", "widget_title_card_create", "widget_lower_third_create"]) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
});

test("widget_title_template returns fade animations", async () => {
  const result = await callTool("widget_title_template", {
    title: "ACT I",
    subtitle: "The Apartment",
    style: "title",
  });
  assertEqual(result.success as boolean, true, "template success");
  const data = result.data as Record<string, unknown>;
  const spec = data.spec as Record<string, unknown>;
  const animations = spec.animations as Array<Record<string, unknown>>;
  assertEqual(animations[0].name as string, "FadeIn", "first animation");
  assertEqual(animations[1].name as string, "FadeOut", "second animation");
});

test("widget_title_card_create forwards path and name", async () => {
  const result = await callTool("widget_title_card_create", {
    package_path: "/Game/UI/Test",
    asset_name: "WBP_TestTitle",
    title: "SINFELD",
    preset: "cinematic",
  });
  assertEqual(result.success as boolean, true, "create success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.path as string, "/Game/UI/Test/WBP_TestTitle", "created path");
});

test("widget_lower_third_create validates preset names", async () => {
  const tool = toolMap.get("widget_lower_third_create");
  if (!tool) throw new Error("Tool not found: widget_lower_third_create");
  const result = tool.inputSchema.safeParse({ preset: "invalid" });
  assert(!result.success, "invalid preset should fail validation");
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
