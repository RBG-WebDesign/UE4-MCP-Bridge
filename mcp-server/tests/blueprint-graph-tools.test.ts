/**
 * Unit tests for Blueprint graph editing tools.
 *
 * Uses MockUnrealServer with custom handlers. No UE4 instance required.
 */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createBlueprintGraphTools } from "../src/tools/blueprint-graph.js";
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
  const tools = createBlueprintGraphTools(client);
  toolMap = new Map(tools.map((t) => [t.name, t]));

  server.setHandler("blueprint_inspect", (params) => ({
    success: true,
    data: { action: params.action, result: [{ name: "Health" }], received: params },
  }));
  server.setHandler("blueprint_node_add", (params) => ({
    success: true,
    data: { node_guid: "ABC123", node_type: params.node_type, graph: params.graph_name },
  }));
  server.setHandler("blueprint_pins_connect", (params) => {
    const p = params as Record<string, string>;
    if (p.target_pin === "InString") {
      return { success: false, data: {}, error: "ConnectPins failed: schema rejected the connection" };
    }
    return {
      success: true,
      data: { connection: `${p.source_node_guid}.${p.source_pin} -> ${p.target_node_guid}.${p.target_pin}`, saved: true },
    };
  });
  server.setHandler("blueprint_add_variable", (params) => ({
    success: true,
    data: { variable: (params as Record<string, unknown>).name, saved: true, received: params },
  }));
}

const EXPECTED_TOOLS = [
  "blueprint_inspect",
  "blueprint_add_variable", "blueprint_remove_variable", "blueprint_set_variable_default",
  "blueprint_add_function", "blueprint_remove_function",
  "blueprint_add_event_dispatcher", "blueprint_remove_event_dispatcher",
  "blueprint_add_interface", "blueprint_remove_interface",
  "blueprint_component_remove", "blueprint_component_rename",
  "blueprint_node_add", "blueprint_node_delete", "blueprint_node_move",
  "blueprint_node_set_enabled", "blueprint_pins_connect", "blueprint_pins_break",
];

test("all 18 blueprint graph tools are registered", async () => {
  for (const name of EXPECTED_TOOLS) {
    assert(toolMap.has(name), `missing tool: ${name}`);
  }
  assert(toolMap.size === EXPECTED_TOOLS.length, `expected ${EXPECTED_TOOLS.length}, got ${toolMap.size}`);
});

test("blueprint_inspect forwards action and path", async () => {
  const result = await callTool("blueprint_inspect", {
    blueprint_path: "/Game/BP_Test",
    action: "variables",
  });
  assert(result.success === true, "inspect should succeed");
  const data = result.data as Record<string, unknown>;
  const received = data.received as Record<string, unknown>;
  assert(received.action === "variables", "action forwarded");
  assert(received.blueprint_path === "/Game/BP_Test", "path forwarded");
});

test("blueprint_node_add returns node guid", async () => {
  const result = await callTool("blueprint_node_add", {
    blueprint_path: "/Game/BP_Test",
    graph_name: "EventGraph",
    node_type: "CustomEvent",
    config: { eventName: "OnHit" },
  });
  const data = result.data as Record<string, unknown>;
  assert(data.node_guid === "ABC123", "node guid returned");
});

test("blueprint_pins_connect surfaces schema rejection as failure", async () => {
  const bad = await callTool("blueprint_pins_connect", {
    blueprint_path: "/Game/BP_Test",
    graph_name: "EventGraph",
    source_node_guid: "A", source_pin: "then",
    target_node_guid: "B", target_pin: "InString",
  });
  assert(bad.success === false, "schema-rejected connection must fail");
  assert(String(bad.error).includes("schema"), "error mentions schema rejection");

  const good = await callTool("blueprint_pins_connect", {
    blueprint_path: "/Game/BP_Test",
    graph_name: "EventGraph",
    source_node_guid: "A", source_pin: "then",
    target_node_guid: "B", target_pin: "execute",
  });
  assert(good.success === true, "valid connection succeeds");
});

test("blueprint_add_variable forwards type descriptor", async () => {
  const result = await callTool("blueprint_add_variable", {
    blueprint_path: "/Game/BP_Test",
    name: "Health",
    type: { category: "float" },
    default_value: 100,
  });
  assert(result.success === true, "add variable succeeds");
  const data = result.data as Record<string, unknown>;
  const received = data.received as Record<string, unknown>;
  const t = received.type as Record<string, unknown>;
  assert(t.category === "float", "type descriptor forwarded");
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
