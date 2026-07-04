/**
 * Unit tests for project intelligence tools.
 *
 * Uses MockUnrealServer with custom handlers. No UE4 instance required.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createIntelligenceTools } from "../src/tools/intelligence.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18771;

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
  const tools = createIntelligenceTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  server.setHandler("project_index_rebuild", (params) => ({
    success: true,
    data: {
      index_dir: "D:/Project/Saved/MCP/index",
      categories: { assets: 2, blueprints: 1 },
      received: params,
    },
  }));

  server.setHandler("project_index_query", (params) => ({
    success: true,
    data: {
      query: params.query ?? "",
      count: 1,
      results: [
        { category: "blueprints", name: "BP_LockedDoor", path: "/Game/BP_LockedDoor", type: "Blueprint" },
      ],
    },
  }));

  server.setHandler("project_semantic_diff", (_params) => ({
    success: true,
    data: {
      diff: {
        summary: { added: 1, removed: 0, changed: 1 },
        categories: {
          blueprints: {
            added: ["/Game/BP_New"],
            removed: [],
            changed: [{ id: "/Game/BP_LockedDoor", changes: { variables_changed: true } }],
          },
        },
      },
    },
  }));

  server.setHandler("gameplay_pattern_search", (_params) => ({
    success: true,
    data: {
      count: 1,
      matches: [
        {
          pattern: "key_door",
          category: "blueprints",
          name: "BP_LockedDoor",
          path: "/Game/BP_LockedDoor",
          why: ["matched token 'key'", "matched token 'door'"],
        },
      ],
    },
  }));
}

async function teardown(): Promise<void> {
  await server.stop();
}

test("all 4 intelligence tools are registered", async () => {
  const expected = [
    "project_index_rebuild",
    "project_index_query",
    "project_semantic_diff",
    "gameplay_pattern_search",
  ];
  for (const name of expected) {
    assert(toolMap.has(name), `Tool '${name}' should be registered`);
  }
  assertEqual(toolMap.size, 4, "intelligence tool count");
});

test("project_index_rebuild forwards category and limit options", async () => {
  const result = await callTool("project_index_rebuild", {
    categories: ["assets", "blueprints"],
    path_prefix: "/Game/Generated",
    limit: 25,
  });
  assertEqual(result.success as boolean, true, "rebuild success");
  const data = result.data as Record<string, unknown>;
  const received = data.received as Record<string, unknown>;
  assertEqual((received.categories as string[]).length, 2, "category count");
  assertEqual(received.path_prefix as string, "/Game/Generated", "path prefix forwarded");
});

test("project_index_query returns indexed matches", async () => {
  const result = await callTool("project_index_query", {
    query: "locked door",
    category: "blueprints",
    include_raw: false,
  });
  assertEqual(result.success as boolean, true, "query success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.count as number, 1, "query count");
  const results = data.results as Array<Record<string, unknown>>;
  assertEqual(results[0].name as string, "BP_LockedDoor", "match name");
});

test("project_semantic_diff returns semantic summary", async () => {
  const result = await callTool("project_semantic_diff", {
    categories: "blueprints",
    asset_paths: ["/Game/BP_LockedDoor"],
  });
  assertEqual(result.success as boolean, true, "diff success");
  const data = result.data as Record<string, unknown>;
  const diff = data.diff as Record<string, unknown>;
  const summary = diff.summary as Record<string, unknown>;
  assertEqual(summary.changed as number, 1, "changed count");
});

test("gameplay_pattern_search validates known pattern names", async () => {
  const tool = toolMap.get("gameplay_pattern_search");
  if (!tool) throw new Error("Tool not found: gameplay_pattern_search");
  const result = tool.inputSchema.safeParse({ pattern: "not_a_pattern" });
  assert(!result.success, "unknown pattern should fail Zod validation");
});

test("gameplay_pattern_search returns why each pattern matched", async () => {
  const result = await callTool("gameplay_pattern_search", { pattern: "key_door" });
  assertEqual(result.success as boolean, true, "pattern search success");
  const data = result.data as Record<string, unknown>;
  const matches = data.matches as Array<Record<string, unknown>>;
  assertEqual(matches.length, 1, "match count");
  assert(Array.isArray(matches[0].why), "why is array");
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
