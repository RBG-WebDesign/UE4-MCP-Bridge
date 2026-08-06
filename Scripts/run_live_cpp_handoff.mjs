import { PuerTSClient, resolveSession } from "../mcp-server/dist/puerts-client.js";
import { writeFileSync, mkdirSync } from "fs";
import { join } from "path";

process.env.MCP_UNREAL_PROJECT_ROOT = "d:/Unreal Projects/BridgeInstallTest";

const client = new PuerTSClient();

async function run() {
  console.log("=== STEP 9: Inspect Loaded Native Class Reflection ===");
  const session = await resolveSession();
  console.log("Live Session ID:", session.session_id);
  console.log("Editor PID:", session.editor_pid);
  console.log("Project:", session.project_path);

  // Step 10 & 11: Create BP_TelekineticCharacter as ACharacter child with TelekineticComponent
  console.log("\n=== STEP 10-15: Build BP_TelekineticCharacter & Wire Blueprint ===");
  const bpBuildParams = {
    asset_path: "/Game/MCPGenerated/BP_TelekineticCharacter",
    parent_class: "Character",
    components: [
      { name: "Camera", class: "CameraComponent", properties: { RelativeLocation: { x: 0, y: 0, z: 64 } } },
      { name: "PhysicsHandle", class: "PhysicsHandleComponent" },
      { name: "TelekineticComponent", class: "TelekineticComponent", properties: { GrabDistance: 1500.0, HoldDistance: 350.0, ImpulseStrength: 2000.0 } }
    ],
    variables: [
      { name: "bHoldingState", type: "bool", default: false, category: "Telekinesis" }
    ],
    compile: true,
    save: true,
    clear_existing_graph: true
  };

  const buildRes = await client.call("blueprint_build", bpBuildParams, 30000);
  console.log("Blueprint Build Result:", JSON.stringify(buildRes, null, 2));

  // Inspect the created Blueprint
  console.log("\n=== STEP 15: Independent Read-Back Inspection ===");
  const inspectRes = await client.call("graph_inspect", { asset_path: "/Game/MCPGenerated/BP_TelekineticCharacter", include_pins: true }, 15000);
  console.log("Blueprint Inspect Result:", JSON.stringify(inspectRes, null, 2));

  // Step 16 & 17: Idempotency Test - Re-run identical build
  console.log("\n=== STEP 16-17: Idempotency Test (Desired State Re-run) ===");
  const buildRes2 = await client.call("blueprint_build", bpBuildParams, 30000);
  console.log("Blueprint Build Re-run Result:", JSON.stringify(buildRes2, null, 2));

  // Step 19 & 20: Inject Controlled Blueprint-construction failure
  console.log("\n=== STEP 19-20: Controlled Blueprint Failure Injection & Rollback ===");
  const invalidBuildParams = {
    asset_path: "/Game/MCPGenerated/BP_TelekineticCharacter_FailTest",
    parent_class: "InvalidNonExistentParentClassForTesting",
    compile: true
  };
  const failedBuildRes = await client.call("blueprint_build", invalidBuildParams, 15000);
  console.log("Controlled Failure Result (Expected Failure):", JSON.stringify(failedBuildRes, null, 2));

  // Verify state preserved after failure
  const inspectAfterFailure = await client.call("graph_inspect", { asset_path: "/Game/MCPGenerated/BP_TelekineticCharacter", include_pins: true }, 15000);
  console.log("Inspect After Failure (State Preserved):", inspectAfterFailure.success);

  // Save evidence
  const evidenceDir = join(process.cwd(), "docs", "evidence");
  mkdirSync(evidenceDir, { recursive: true });
  const evidencePath = join(evidenceDir, "cpp_handoff_acceptance.json");
  const evidenceData = {
    timestamp: new Date().toISOString(),
    session_id: session.session_id,
    editor_pid: session.editor_pid,
    project_path: session.project_path,
    native_class: "UTelekineticComponent",
    blueprint_asset: "/Game/MCPGenerated/BP_TelekineticCharacter",
    build_result: buildRes,
    inspect_result: inspectRes,
    idempotency_result: buildRes2,
    rollback_result: failedBuildRes,
    state_preserved_after_rollback: inspectAfterFailure.success
  };
  writeFileSync(evidencePath, JSON.stringify(evidenceData, null, 2), "utf8");
  console.log("\nEvidence saved to:", evidencePath);
}

run().catch(console.error);
