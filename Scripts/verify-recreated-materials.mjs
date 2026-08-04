import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";
import { existsSync } from "fs";
import { join } from "path";

async function main() {
  const transport = new StdioClientTransport({
    command: "node",
    args: ["mcp-server/dist/index.js"],
    cwd: process.cwd(),
    env: { ...process.env, MCP_UNREAL_PROJECT_ROOT: "D:/Unreal Projects/BridgeInstallTest" }
  });
  const client = new Client({ name: "test", version: "1.0.0" }, { capabilities: {} });
  await client.connect(transport);

  console.log("=== CHECK 1: puerts_find_assets for all 4 assets ===");
  const findRes = await client.callTool({
    name: "puerts_find_assets",
    arguments: { path: "/Game/CRT_VCR_Material", recursive: true }
  });
  const findData = JSON.parse(findRes.content[0].text);
  const foundAssets = findData.data?.assets || [];
  const requiredAssetNames = [
    "M_TV_Material_Base_Recreated",
    "M_Month_Stamp_Recreated",
    "MI_TV_Wood_Recreated",
    "MI_TV_Black_Recreated"
  ];
  const foundNames = foundAssets.map(a => a.name);
  console.log("Found matching asset names:", requiredAssetNames.filter(n => foundNames.includes(n)));

  console.log("\n=== CHECK 2, 3, 4: puerts_material_inspect for TV Master Material ===");
  const inspectTV = await client.callTool({
    name: "puerts_material_inspect",
    arguments: { asset_path: "/Game/CRT_VCR_Material/M_TV_Material_Base_Recreated.M_TV_Material_Base_Recreated" }
  });
  const tvData = JSON.parse(inspectTV.content[0].text).data;
  console.log("TV Master Domain:", tvData.domain, "| Blend Mode:", tvData.blend_mode);
  console.log("TV Master Inputs:", tvData.material_inputs?.filter(i => i.connected));

  console.log("\n=== CHECK 2, 3, 5: puerts_material_inspect for Month Stamp Master Material ===");
  const inspectStamp = await client.callTool({
    name: "puerts_material_inspect",
    arguments: { asset_path: "/Game/CRT_VCR_Material/M_Month_Stamp_Recreated.M_Month_Stamp_Recreated" }
  });
  const stampData = JSON.parse(inspectStamp.content[0].text).data;
  console.log("Stamp Master Domain:", stampData.domain, "| Blend Mode:", stampData.blend_mode);
  console.log("Stamp Master Inputs:", stampData.material_inputs?.filter(i => i.connected));
  console.log("Stamp Parameters:", stampData.parameters);

  console.log("\n=== CHECK 6, 7, 8: puerts_material_inspect for Material Instances ===");
  const inspectWood = await client.callTool({
    name: "puerts_material_inspect",
    arguments: { asset_path: "/Game/CRT_VCR_Material/MI_TV_Wood_Recreated.MI_TV_Wood_Recreated" }
  });
  const woodData = JSON.parse(inspectWood.content[0].text).data;
  console.log("MI_TV_Wood Parent:", woodData.parent_path || woodData.base_material_path);
  console.log("MI_TV_Wood Overrides:", woodData.parameters);

  const inspectBlack = await client.callTool({
    name: "puerts_material_inspect",
    arguments: { asset_path: "/Game/CRT_VCR_Material/MI_TV_Black_Recreated.MI_TV_Black_Recreated" }
  });
  const blackData = JSON.parse(inspectBlack.content[0].text).data;
  console.log("MI_TV_Black Parent:", blackData.parent_path || blackData.base_material_path);
  console.log("MI_TV_Black Overrides:", blackData.parameters);

  console.log("\n=== CHECK 9: puerts_save for all 4 assets ===");
  const saveRes = await client.callTool({
    name: "puerts_save",
    arguments: {
      assets: [
        "/Game/CRT_VCR_Material/M_TV_Material_Base_Recreated.M_TV_Material_Base_Recreated",
        "/Game/CRT_VCR_Material/M_Month_Stamp_Recreated.M_Month_Stamp_Recreated",
        "/Game/CRT_VCR_Material/MI_TV_Wood_Recreated.MI_TV_Wood_Recreated",
        "/Game/CRT_VCR_Material/MI_TV_Black_Recreated.MI_TV_Black_Recreated"
      ]
    }
  });
  console.log("puerts_save response:", saveRes.content[0].text);

  console.log("\n=== CHECK 10: Disk verification of .uasset files ===");
  const diskPath = "D:/Unreal Projects/BridgeInstallTest/Content/CRT_VCR_Material";
  const filesToCheck = [
    "M_TV_Material_Base_Recreated.uasset",
    "M_Month_Stamp_Recreated.uasset",
    "MI_TV_Wood_Recreated.uasset",
    "MI_TV_Black_Recreated.uasset"
  ];
  for (const f of filesToCheck) {
    console.log(`File ${f} exists on disk:`, existsSync(join(diskPath, f)));
  }

  process.exit(0);
}
main().catch((err) => {
  console.error(err);
  process.exit(1);
});
