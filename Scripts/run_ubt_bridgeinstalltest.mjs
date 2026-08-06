import { runUbt } from "../mcp-server/dist/cpp-authoring.js";

const result = runUbt({
  project_file: "D:/Unreal Projects/BridgeInstallTest/BridgeInstallTest.uproject",
  target: "BridgeInstallTestEditor",
  engine_root: "D:/UE/UE_4.27",
});

console.log("Success:", result.success);
console.log("Error:", result.error);
console.log("Data:", JSON.stringify(result.data, null, 2));
