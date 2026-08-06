import { PuerTSClient, resolveSession } from "../mcp-server/dist/puerts-client.js";
import { readFileSync, writeFileSync } from "fs";
import { join } from "path";

process.env.MCP_UNREAL_PROJECT_ROOT = "d:/Unreal Projects/BridgeInstallTest";

const client = new PuerTSClient();

async function runCold() {
  console.log("=== COLD READ-ONLY INSPECTION ===");
  const session = await resolveSession();
  console.log("Cold Session ID:", session.session_id);
  console.log("Cold Editor PID:", session.editor_pid);

  const coldInspectRes = await client.call("graph_inspect", { asset_path: "/Game/MCPGenerated/BP_TelekineticCharacter", include_pins: true }, 15000);
  console.log("Cold Inspect Result:", JSON.stringify(coldInspectRes, null, 2));

  const evidencePath = join(process.cwd(), "docs", "evidence", "cpp_handoff_acceptance.json");
  const existing = JSON.parse(readFileSync(evidencePath, "utf8"));
  existing.cold_session = session;
  existing.cold_inspect_result = coldInspectRes;
  existing.cold_persistence_proven = coldInspectRes.success && coldInspectRes.data?.asset_path === "/Game/MCPGenerated/BP_TelekineticCharacter";

  writeFileSync(evidencePath, JSON.stringify(existing, null, 2), "utf8");
  console.log("Updated evidence file with cold inspection results:", evidencePath);
}

runCold().catch(console.error);
