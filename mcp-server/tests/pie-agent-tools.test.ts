/** Unit tests for the PIE gameplay agent MCP tools. */

import { MockUnrealServer } from "./mock-server.js";
import { UnrealClient } from "../src/unreal-client.js";
import { createPieAgentTools } from "../src/tools/pie-agent.js";
import type { ToolDefinition } from "../src/types.js";

const TEST_PORT = 18772;

let passed = 0;
let failed = 0;

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

async function main(): Promise<void> {
  const mock = new MockUnrealServer();
  await mock.start(TEST_PORT);
  const client = new UnrealClient({ port: TEST_PORT });
  const tools = createPieAgentTools(client);
  const byName = new Map<string, ToolDefinition>(tools.map((tool) => [tool.name, tool]));

  const run = async (name: string, fn: () => Promise<void>): Promise<void> => {
    try {
      await fn();
      console.log(`  PASS  ${name}`);
      passed++;
    } catch (error) {
      console.log(`  FAIL  ${name}: ${(error as Error).message}`);
      failed++;
    }
  };

  await run("registers all nine PIE agent tools", async () => {
    assert(tools.length === 9, `expected 9 tools, got ${tools.length}`);
  });

  await run("move_to polls status until succeeded", async () => {
    let statusCalls = 0;
    mock.setHandler("pie_agent_move_to", () => ({ success: true, data: { operation_id: 7, status: "running" } }));
    mock.setHandler("pie_agent_status", () => {
      statusCalls++;
      return statusCalls < 3
        ? { success: true, data: { operation_id: 7, status: "running", result: {} } }
        : { success: true, data: { operation_id: 7, status: "succeeded", result: { arrived: true } } };
    });
    const response = await byName.get("pie_agent_move_to")!.handler({ actor: "BP_TreasureChest" });
    const payload = JSON.parse(response.content[0].text) as { success: boolean; data: { arrived?: boolean } };
    assert(payload.success, "move_to should succeed");
    assert(payload.data.arrived === true, "arrived result should surface");
    assert(statusCalls >= 3, "status should be polled until terminal");
  });

  await run("failed press returns the engine error", async () => {
    mock.setHandler("pie_agent_press", () => ({ success: true, data: { operation_id: 8, status: "running" } }));
    mock.setHandler("pie_agent_status", () => ({
      success: true,
      data: { operation_id: 8, status: "failed", result: {}, error: "No matching action binding was live" },
    }));
    const response = await byName.get("pie_agent_press")!.handler({ action: "UI_Interact" });
    const payload = JSON.parse(response.content[0].text) as { success: boolean; error?: string };
    assert(!payload.success, "press should fail");
    assert(payload.error?.includes("action binding") === true, "press error should be preserved");
  });

  await run("observe is a direct passthrough", async () => {
    mock.setHandler("pie_agent_observe", () => ({ success: true, data: { actor_count: 42 } }));
    const response = await byName.get("pie_agent_observe")!.handler({ radius: 2000 });
    const payload = JSON.parse(response.content[0].text) as { data: { actor_count?: number } };
    assert(payload.data.actor_count === 42, "observe result should be unchanged");
  });

  await mock.stop();
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

main();
