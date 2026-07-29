/**
 * PIE gameplay-agent chest regression.
 *
 * Requires UE4.27 with the PIE agent module loaded and a treasure chest in the
 * current level. This test starts and stops PIE, so run it only when runtime
 * testing has been explicitly requested.
 */

import { UnrealClient } from "../../src/unreal-client.js";
import { createGameplayTools } from "../../src/tools/gameplay.js";
import { createPieAgentTools } from "../../src/tools/pie-agent.js";
import type { ToolDefinition } from "../../src/types.js";

const client = new UnrealClient();
const tools: ToolDefinition[] = [...createGameplayTools(client), ...createPieAgentTools(client)];
const byName = new Map(tools.map((tool) => [tool.name, tool]));

async function call(name: string, params: Record<string, unknown> = {}): Promise<Record<string, unknown>> {
  const tool = byName.get(name);
  if (!tool) throw new Error(`Missing tool: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse(result.content[0].text) as Record<string, unknown>;
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

async function run(): Promise<void> {
  const start = await call("gameplay_pie_start");
  assert(start.success === true, `PIE start failed: ${start.error}`);
  try {
    const recording = await call("pie_agent_record_start", { class_filters: ["Collectible"] });
    assert(recording.success === true, `record_start failed: ${recording.error}`);
    const move = await call("pie_agent_move_to", { actor: "BP_TreasureChest", acceptance_radius: 150, timeout: 25 });
    assert(move.success === true, `move_to failed: ${move.error}`);
    const press = await call("pie_agent_press", { action: "UI_Interact" });
    assert(press.success === true, `press failed: ${press.error}`);
    const expected = await call("pie_agent_expect", {
      conditions: ["actor_count(CollectibleCoin) >= 10", "no_log_matching(\"Error|ensure\")"], within_seconds: 10,
    });
    assert(expected.success === true, `expect failed: ${expected.error}`);
    const transcript = await call("pie_agent_record_stop");
    assert(transcript.success === true, `record_stop failed: ${transcript.error}`);
    const events = ((transcript.data as Record<string, unknown>).events ?? []) as Array<{ type: string }>;
    const spawns = events.filter((event) => event.type === "spawn").length;
    const destroys = events.filter((event) => event.type === "destroy").length;
    assert(spawns >= 10, `expected at least 10 collectible spawns, got ${spawns}`);
    assert(destroys < spawns / 2, `too many collectibles destroyed immediately: ${destroys}/${spawns}`);
    const observation = await call("pie_agent_observe", { radius: 3000 });
    assert(observation.success === true, `observe failed: ${observation.error}`);
    console.log("PIE gameplay-agent chest regression passed.");
  } finally {
    await call("gameplay_pie_stop");
  }
}

run().catch((error) => {
  console.error(`PIE gameplay-agent integration test failed: ${(error as Error).message}`);
  process.exit(1);
});
