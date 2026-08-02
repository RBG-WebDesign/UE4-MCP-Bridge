#!/usr/bin/env node
// Validation for Scripts/package-mcp-bridge.ps1: run it, open what it produced,
// and check the artefact against what a person dropping it into a UE4.27
// project actually needs.
//
//   node Scripts/package-acceptance.mjs
//
// Two kinds of case, kept apart on purpose:
//
//   PROVEN  the package has this property, and the assertion fails if it stops
//           having it.
//   GAP     the package does NOT have this property. The assertion states the
//           gap exactly as it is today, so when someone closes it this file
//           fails and has to be updated rather than quietly going stale. A
//           known limitation nobody can see is a limitation nobody fixes.
//
// The verdict this file supports: package-mcp-bridge.ps1 produces a well-formed
// UE4 plugin SOURCE tree, and not a self-sufficient installable artefact. The
// GAP cases below are why.
//
// Never runs UAT. The -RunUAT path compiles the plugin and needs UE4.27
// installed, so nothing here says anything about it.

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, readdirSync, rmSync, cpSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const packager = join(repoRoot, "Scripts", "package-mcp-bridge.ps1");

const failures = [];
function assert(kind, condition, label) {
  if (condition) { console.log(`  PASS  [${kind}] ${label}`); return true; }
  console.log(`  FAIL  [${kind}] ${label}`); failures.push(label); return false;
}

const powershell = (args) => execFileSync("powershell.exe",
  ["-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", ...args], { encoding: "utf8" });

const runPackager = (outputRoot, pluginPath) => powershell([
  "-File", packager, "-OutputRoot", outputRoot,
  ...(pluginPath ? ["-PluginPath", pluginPath] : []),
]);

/** Entry names inside a zip, without extracting it. */
const zipEntries = (zipPath) => powershell([
  "-Command",
  "Add-Type -AssemblyName System.IO.Compression.FileSystem; " +
  `$z=[System.IO.Compression.ZipFile]::OpenRead('${zipPath}'); ` +
  "$z.Entries | ForEach-Object { $_.FullName }; $z.Dispose()",
]).split(/\r?\n/).map((l) => l.trim().replaceAll("\\", "/")).filter(Boolean);

const scratch = mkdtempSync(join(tmpdir(), "package-acceptance-"));
try {
  console.log(`package acceptance\n  repository: ${repoRoot}\n  scratch:    ${scratch}\n`);

  const descriptor = JSON.parse(readFileSync(join(repoRoot, "Plugins", "MCPBridge", "MCPBridge.uplugin"), "utf8"));

  console.log("-- 1. it runs and names the artefact after the descriptor");
  const outputRoot = join(scratch, "Releases");
  const log = runPackager(outputRoot);
  const zipName = `MCPBridge_UE${descriptor.EngineVersion}_v${descriptor.VersionName}.zip`;
  const zipPath = join(outputRoot, zipName);
  assert("PROVEN", existsSync(zipPath), `${zipName} was produced, so the version in the file name comes from the descriptor`);
  assert("PROVEN", log.includes(zipPath), "and the script printed the path it wrote");
  assert("PROVEN", !existsSync(join(outputRoot, "_stage")), "the staging directory was cleaned up");
  assert("PROVEN", !existsSync(join(repoRoot, "Releases")),
    "nothing was written into the repository when an output root was given");

  console.log("\n-- 2. the zip is shaped like a plugin somebody can drop into Plugins/");
  const entries = zipEntries(zipPath);
  const roots = new Set(entries.map((e) => e.split("/")[0]));
  assert("PROVEN", roots.size === 1 && roots.has("MCPBridge"),
    `everything is under a single MCPBridge/ root (found: ${[...roots].join(", ")})`);
  assert("PROVEN", entries.includes("MCPBridge/MCPBridge.uplugin"),
    "the descriptor is at the root of that directory, where UE4 looks for it");
  for (const junk of ["Binaries", "Build", "Intermediate", "Saved", "__pycache__", ".vs"]) {
    assert("PROVEN", !entries.some((e) => e.split("/").includes(junk)), `no ${junk}/ was packaged`);
  }
  const longest = entries.reduce((a, b) => (a.length > b.length ? a : b), "");
  assert("PROVEN", longest.length - "MCPBridge/".length <= 170,
    `the longest plugin-relative path is within the 170 character budget (${longest.length - 10}: ${longest})`);

  console.log("\n-- 3. every module the descriptor declares is actually in the zip");
  for (const module of descriptor.Modules) {
    assert("PROVEN", entries.includes(`MCPBridge/Source/${module.Name}/${module.Name}.Build.cs`),
      `${module.Name} ships its Build.cs, so UBT can build the module the descriptor promises`);
  }

  console.log("\n-- 4. the generated runtime is in the zip");
  // Content/JavaScript is produced by npm run build and staged by
  // Scripts/stage-puerts-runtime.mjs. It is NOT in Git. Without it the plugin
  // loads and the PuerTS lane has nothing to execute, which is the failure this
  // case is here to make loud.
  for (const file of ["bootstrap.js", "registry.js", "runtime.js", "safety.js"]) {
    assert("PROVEN", entries.includes(`MCPBridge/Content/JavaScript/${file}`),
      `Content/JavaScript/${file} is packaged`);
  }

  console.log("\n-- 5. GAPS: what the artefact does not carry");

  // 5a. The packager does not build, and does not check that someone else did.
  // Demonstrated rather than asserted from reading: package a plugin tree with
  // the generated runtime removed and watch it succeed.
  const strippedPlugin = join(scratch, "StrippedPlugin", "MCPBridge");
  cpSync(join(repoRoot, "Plugins", "MCPBridge"), strippedPlugin, { recursive: true });
  rmSync(join(strippedPlugin, "Content", "JavaScript"), { recursive: true, force: true });
  const strippedOut = join(scratch, "StrippedReleases");
  runPackager(strippedOut, strippedPlugin);
  const strippedZip = join(strippedOut, zipName);
  const strippedEntries = existsSync(strippedZip) ? zipEntries(strippedZip) : [];
  assert("GAP", existsSync(strippedZip)
    && !strippedEntries.some((e) => e.startsWith("MCPBridge/Content/JavaScript/")),
    "packaging a tree with no Content/JavaScript still produces a zip, with no warning. " +
    "Run npm run build before packaging, or the release ships a plugin whose PuerTS lane is empty");

  // 5b. The descriptor names a required plugin the zip does not contain.
  const requires = (descriptor.Plugins ?? []).map((p) => p.Name);
  // Compared component by component: MCPBridgePuerTS is a module of this plugin
  // and is in the zip, which a substring match would happily mistake for the
  // dependency being satisfied.
  const carriesBundle = entries.some((e) => e.split("/").some((seg) => seg.toLowerCase() === "puerts"))
    || entries.some((e) => e.toLowerCase().endsWith("/puerts.uplugin"));
  assert("GAP", requires.includes("Puerts") && !carriesBundle,
    "the descriptor requires the Puerts plugin and the zip does not contain it. UE4 refuses to load " +
    "MCPBridge without it, so the zip alone is not droppable: the recipient must install the pinned " +
    "bundle separately");
  const setupText = existsSync(join(repoRoot, "Plugins", "MCPBridge", "TEAM_SETUP.md"))
    ? readFileSync(join(repoRoot, "Plugins", "MCPBridge", "TEAM_SETUP.md"), "utf8") : "";
  assert("GAP", !/puerts/i.test(setupText),
    "and the setup notes carried inside the zip never mention PuerTS, so nothing in the artefact tells " +
    "the recipient about the dependency");

  // 5c. No provenance. Scripts/bridge-install.mjs treats a plugin copy with no
  // MCPBridgeInstall.json as unmanaged and refuses it, so a project installed
  // from this zip cannot pass the install gate until it is re-synced.
  assert("GAP", !entries.some((e) => e.endsWith("MCPBridgeInstall.json")),
    "the zip carries no MCPBridgeInstall.json, so install:check calls a project installed from it an " +
    "unmanaged copy of unknown provenance");

  // 5d. Source only.
  assert("GAP", !entries.some((e) => e.endsWith(".dll")),
    "the zip contains no compiled binaries, so the target must be a C++ project with UE4.27 and UBT " +
    "available. -RunUAT builds those, needs an engine, and is not exercised here");

  console.log(`\n${failures.length === 0 ? "all checks passed" : `${failures.length} check(s) did not hold:`}`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exitCode = failures.length === 0 ? 0 : 1;
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
