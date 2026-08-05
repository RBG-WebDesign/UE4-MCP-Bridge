#!/usr/bin/env node
// Prints the UE engine root for a project, or exits 1 with the reason.
//
//   node Scripts/resolve-engine-root.mjs "D:/Unreal Projects/MyGame"
//
// Thin wrapper over resolveEngineRoot in mcp-server/src/tools/engine-source.ts,
// which is what engine_source_search and engine_source_read already use:
// UE_ENGINE_ROOT, then EngineAssociation from the .uproject resolved through
// the registry (HKLM for launcher installs, HKCU\...\Builds for source builds).
// Setup scripts call this instead of reimplementing that lookup.
//
// The resolver reads the .uproject from the current directory, so this changes
// directory to the project before asking.
import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = dirname(dirname(fileURLToPath(import.meta.url)));
const built = join(repoRoot, "mcp-server", "dist", "tools", "engine-source.js");
if (!existsSync(built)) {
  console.error(`Not built: ${built}. Run npm run build first.`);
  process.exit(1);
}

const target = process.argv[2] ? resolve(process.argv[2]) : process.cwd();
if (!existsSync(target)) {
  console.error(`No such directory: ${target}`);
  process.exit(1);
}

try {
  process.chdir(target);
  const { resolveEngineRoot } = await import(pathToFileURL(built).href);
  console.log(resolveEngineRoot().replace(/\\/g, "/"));
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
