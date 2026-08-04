#!/usr/bin/env node
// Acceptance for a fresh install: a brand-new project directory, the real
// Scripts/install-mcp-bridge.ps1, and assertions on everything it produced.
//
// The installer had no automated proof at all. It was verified by someone
// running it and looking at the editor, which tests the editor as much as the
// installer and cannot run in CI. Everything the installer writes - the plugin
// copy, the pipe section, .mcp.json, the .uproject plugin list - is a file, and
// a file can be read back without Unreal ever starting.
//
//   node Scripts/fresh-install-acceptance.mjs
//
// WHERE THIS STOPS, stated rather than papered over: at the compiler. The
// installer copies sources; UBT turns them into UE4Editor-MCPBridge.dll and UBT
// needs UE4.27 installed. So the last case here does not assert success. It runs
// the install gate and requires that the ONLY thing left to complain about is
// the missing binary. That is the boundary, asserted as a boundary.
//
// One fixture, declared up front: the pinned 369 MB PuerTS bundle is not in Git
// (.gitignore excludes Plugins/*), so neither CI nor a fresh clone has it. This
// script therefore builds a bridge root whose Plugins/Puerts is a small stand-in
// with its own regenerated lock file. That proves the installer copies the
// bundle it was given and refuses one that does not match its pin. It does NOT
// prove anything about the real bundle's contents; npm run check:puerts does
// that, against the real bundle, on a machine that has one.

import { execFileSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import {
  copyFileSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync,
  realpathSync, rmSync, symlinkSync, writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { MANIFEST_NAME, MANIFEST_SCHEMA_VERSION, checkInstall, syncInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}

const fwd = (p) => p.replaceAll("\\", "/");

/** The pipe name the installer is supposed to derive, recomputed here from the
    rule rather than read back from what it wrote. Reading its own output back
    would pass for a hard-coded constant, and a constant pipe name is precisely
    the bug session isolation exists to prevent. */
function expectedPipeName(projectRoot, uprojectName) {
  let slug = uprojectName.replace(/[^A-Za-z0-9_]/g, "_").replace(/^_+|_+$/g, "");
  if (!slug) slug = "Project";
  if (slug.length > 32) slug = slug.slice(0, 32);
  const digest = createHash("sha256").update(projectRoot.toLowerCase(), "utf8").digest("hex");
  return `\\\\.\\pipe\\UE427PuerTSMCP_${slug}_${digest.slice(0, 8)}`;
}

function iniSection(text, name) {
  const lines = text.split(/\r?\n/);
  const start = lines.findIndex((l) => l.trim() === name);
  if (start < 0) return null;
  const rest = lines.slice(start + 1);
  const end = rest.findIndex((l) => /^\s*\[.+\]\s*$/.test(l));
  return (end < 0 ? rest : rest.slice(0, end)).filter((l) => l.trim() !== "");
}

function hasDirNamed(root, name) {
  const stack = [root];
  while (stack.length) {
    const dir = stack.pop();
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      if (entry.name === name) return true;
      stack.push(join(dir, entry.name));
    }
  }
  return false;
}

/** Drive the freshly generated .mcp.json exactly as a client would: launch what
    it says to launch, from where it says to launch it, and ask for the tool
    list. A path that merely looks absolute and correct is not the claim. */
async function serverAnswers(entry) {
  const child = spawn(entry.command, entry.args, {
    cwd: entry.cwd, env: { ...process.env, ...entry.env }, stdio: ["pipe", "pipe", "pipe"],
  });
  try {
    let buffer = "";
    const pending = new Map();
    child.stdout.on("data", (chunk) => {
      buffer += chunk.toString();
      for (let n; (n = buffer.indexOf("\n")) >= 0;) {
        const line = buffer.slice(0, n).trim();
        buffer = buffer.slice(n + 1);
        if (!line) continue;
        let message;
        try { message = JSON.parse(line); } catch { continue; }
        const settle = pending.get(message.id);
        if (settle) { pending.delete(message.id); settle(message); }
      }
    });
    const send = (id, method, params) => {
      child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 30000);
        pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
      });
    };
    await send(1, "initialize", {
      protocolVersion: "2024-11-05", capabilities: {},
      clientInfo: { name: "fresh-install-acceptance", version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
    const listed = await send(2, "tools/list", {});
    return listed?.result?.tools ?? [];
  } finally {
    child.kill();
  }
}

const scratch = mkdtempSync(join(tmpdir(), "fresh-install-"));
try {
  console.log(`fresh install acceptance\n  repository: ${repoRoot}\n  scratch:    ${scratch}\n`);

  console.log("-- 0. a bridge root and a brand-new project, neither of which existed a moment ago");
  const bridgeRoot = join(scratch, "bridge");
  mkdirSync(join(bridgeRoot, "Plugins"), { recursive: true });
  copyFileSync(join(repoRoot, "package.json"), join(bridgeRoot, "package.json"));
  // Junctions for the two directories the installer only ever reads through or
  // builds path strings from. A copy of node_modules would take longer than
  // everything else here put together, and Node resolves modules through the
  // real path, so a launched server still finds its dependencies.
  symlinkSync(join(repoRoot, "mcp-server"), join(bridgeRoot, "mcp-server"), "junction");
  // The pin checker, by contrast, is copied rather than linked. Node resolves a
  // module to its real path, so a junction here would make the script compute
  // its repository root as this checkout and check the bundle that is missing
  // from it, instead of the one the installer was pointed at.
  mkdirSync(join(bridgeRoot, "Scripts"));
  copyFileSync(join(repoRoot, "Scripts", "check-puerts-pin.mjs"),
    join(bridgeRoot, "Scripts", "check-puerts-pin.mjs"));
  cpSync(join(repoRoot, "Plugins", "MCPBridge"), join(bridgeRoot, "Plugins", "MCPBridge"), { recursive: true });
  const stubBundle = join(bridgeRoot, "Plugins", "Puerts");
  mkdirSync(join(stubBundle, "Content", "JavaScript"), { recursive: true });
  writeFileSync(join(stubBundle, "Puerts.uplugin"), '{"FileVersion":3,"FriendlyName":"PuerTS stand-in"}\n');
  writeFileSync(join(stubBundle, "Content", "JavaScript", "puerts.js"), "// stand-in\n");
  execFileSync(process.execPath, [join(bridgeRoot, "Scripts", "check-puerts-pin.mjs"), "--write"],
    { encoding: "utf8" });
  assert(existsSync(join(bridgeRoot, "Plugins", "Puerts.lock.json")),
    "the stand-in bundle has a lock file, so the installer's pin gate has something to enforce");
  assert(existsSync(join(repoRoot, "Plugins", "MCPBridge", "Content", "JavaScript", "registry.js")),
    "the repository plugin carries Content/JavaScript, so npm run build ran before this");

  const projectRoot = join(scratch, "FreshProject");
  mkdirSync(projectRoot);
  const canonicalProjectRoot = realpathSync(projectRoot);
  const uprojectPath = join(projectRoot, "Fresh.uproject");
  writeFileSync(uprojectPath, JSON.stringify({
    FileVersion: 3, EngineAssociation: "4.27", Category: "", Description: "",
  }, null, 2) + "\n");
  assert(readdirSync(projectRoot).length === 1,
    "the target project is a bare directory with one .uproject and nothing else");

  console.log("\n-- 1. the installer runs against it, with no editor and no compiler");
  const runInstaller = () => execFileSync("powershell.exe", [
    "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
    "-File", join(repoRoot, "Scripts", "install-mcp-bridge.ps1"),
    "-Project", projectRoot, "-BridgeRoot", bridgeRoot, "-SkipBuild",
  ], { encoding: "utf8" });
  const output = runInstaller();
  assert(/MCP Bridge install complete/.test(output), "the installer reported completion");

  console.log("\n-- 2. the plugin files are present, and the non-distributable ones are not");
  const installedPlugin = join(projectRoot, "Plugins", "MCPBridge");
  assert(existsSync(join(installedPlugin, "MCPBridge.uplugin")), "MCPBridge.uplugin is installed");
  assert(existsSync(join(installedPlugin, "Content", "JavaScript", "registry.js")),
    "the built Content/JavaScript is installed, not just the C++ sources");
  assert(existsSync(join(installedPlugin, "Source", "MCPBridgeGraphBuilder", "MCPBridgeGraphBuilder.Build.cs")),
    "the native module sources are installed");
  for (const junk of ["Binaries", "Intermediate", "Saved"]) {
    assert(!hasDirNamed(installedPlugin, junk), `no ${junk}/ was carried into the target`);
  }
  assert(existsSync(join(projectRoot, "Plugins", "Puerts", "Puerts.uplugin")),
    "the PuerTS bundle is installed alongside it (stand-in bundle, see the header)");

  console.log("\n-- 3. DefaultEngine.ini names a pipe that is derived from this project");
  const iniPath = join(projectRoot, "Config", "DefaultEngine.ini");
  assert(existsSync(iniPath), "Config/DefaultEngine.ini was created for a project that had no Config directory");
  const iniText = readFileSync(iniPath, "utf8");
  const bridgeLines = iniSection(iniText, "[MCPPuerTSBridge]");
  assert(bridgeLines !== null, "the ini has an [MCPPuerTSBridge] section");
  const pipeLines = (bridgeLines ?? []).filter((l) => /^\s*PipeName\s*=/.test(l));
  assert(pipeLines.length === 1, `exactly one PipeName line (found ${pipeLines.length})`);
  const wantPipe = expectedPipeName(canonicalProjectRoot, "Fresh");
  const gotPipe = (pipeLines[0] ?? "").split("=").slice(1).join("=").trim();
  assert(gotPipe === wantPipe, `the pipe name is the one the rule derives for this project\n          want ${wantPipe}\n          got  ${gotPipe}`);

  const pythonLines = iniSection(iniText, "[/Script/PythonScriptPlugin.PythonScriptPluginSettings]") ?? [];
  assert(pythonLines.some((l) => /^\s*bRemoteExecution\s*=\s*False/i.test(l)),
    "Python remote execution is written off");
  assert(!pythonLines.some((l) => /startup\.py/i.test(l)),
    "the legacy HTTP listener is NOT wired up: no startup.py without -EnableLegacyHttp");
  assert(!pythonLines.some((l) => /bDeveloperMode/i.test(l)),
    "Python developer mode is not turned on either");

  console.log("\n-- 4. .mcp.json points at absolute paths that exist, and launches");
  const mcpPath = join(projectRoot, ".mcp.json");
  assert(existsSync(mcpPath), ".mcp.json was generated");
  const mcpConfig = JSON.parse(readFileSync(mcpPath, "utf8"));
  const entry = mcpConfig?.mcpServers?.["unreal-bridge"];
  assert(entry !== undefined, "it declares the unreal-bridge server");
  assert(entry?.command === "node", `the command is node (got ${entry?.command})`);
  const wantEntry = fwd(join(bridgeRoot, "mcp-server", "dist", "index.js"));
  assert(entry?.args?.[0] === wantEntry, `the server path is the bridge root's built entry point\n          want ${wantEntry}\n          got  ${entry?.args?.[0]}`);
  assert(existsSync(entry?.args?.[0] ?? ""), "that path exists on disk");
  assert(entry?.cwd === fwd(bridgeRoot), `the working directory is the bridge root (got ${entry?.cwd})`);
  assert(entry?.env?.MCP_UNREAL_PROJECT_ROOT.toLowerCase() === fwd(canonicalProjectRoot).toLowerCase(),
    `MCP_UNREAL_PROJECT_ROOT is this project (got ${entry?.env?.MCP_UNREAL_PROJECT_ROOT})`);
  // The two files have to agree. They are written by different functions from
  // the same computed value, and if they ever drift the client addresses one
  // pipe while the editor opens another, which reads as an editor that is down.
  assert(entry?.env?.MCP_PUERTS_PIPE === gotPipe, "the pipe in .mcp.json is the pipe in DefaultEngine.ini");
  assert(entry?.env?.MCP_ENABLE_LEGACY_HTTP === undefined,
    "legacy HTTP is not enabled in the generated config");
  const tools = await serverAnswers(entry);
  assert(tools.length > 0, `launching exactly what .mcp.json says answers tools/list with ${tools.length} tool(s)`);
  assert(tools.some((t) => t.name?.startsWith("puerts_")),
    "the tools it lists are this bridge's native tools");

  console.log("\n-- 5. the Codex config got the same target");
  const codexPath = join(projectRoot, ".codex", "config.toml");
  assert(existsSync(codexPath), ".codex/config.toml was generated");
  const codexText = readFileSync(codexPath, "utf8");
  assert(/# BEGIN MCPBRIDGE MANAGED[\s\S]*# END MCPBRIDGE MANAGED/.test(codexText),
    "it is written inside managed markers so a rerun can replace it");
  assert(codexText.includes(gotPipe), "it names the same pipe");

  console.log("\n-- 6. the .uproject plugin list was updated and nothing else was lost");
  const uproject = JSON.parse(readFileSync(uprojectPath, "utf8"));
  assert(uproject.EngineAssociation === "4.27", "EngineAssociation survived the rewrite");
  const named = (n) => (uproject.Plugins ?? []).filter((p) => p.Name === n);
  assert(named("MCPBridge").length === 1 && named("MCPBridge")[0].Enabled === true,
    "MCPBridge is listed once and enabled");
  assert(named("Puerts").length === 1 && named("Puerts")[0].Enabled === true,
    "Puerts is listed once and enabled");
  assert(named("PythonScriptPlugin").length === 0,
    "PythonScriptPlugin is NOT enabled: the legacy listener stays off by default");

  console.log("\n-- 7. running it again changes nothing it already wrote");
  runInstaller();
  const iniAgain = readFileSync(iniPath, "utf8");
  const pipeAgain = (iniSection(iniAgain, "[MCPPuerTSBridge]") ?? []).filter((l) => /^\s*PipeName\s*=/.test(l));
  assert(pipeAgain.length === 1, `a second install leaves one PipeName line, not two (found ${pipeAgain.length})`);
  const uprojectAgain = JSON.parse(readFileSync(uprojectPath, "utf8"));
  assert(uprojectAgain.Plugins.filter((p) => p.Name === "MCPBridge").length === 1,
    "and one MCPBridge entry in the .uproject, not two");
  assert((readFileSync(codexPath, "utf8").match(/BEGIN MCPBRIDGE MANAGED/g) ?? []).length === 1,
    "and one managed Codex block, not two");

  console.log("\n-- 8. the install manifest is written and carries a schema version");
  const manifest = syncInstall(projectRoot, { build: false });
  assert(existsSync(join(installedPlugin, MANIFEST_NAME)), `${MANIFEST_NAME} was written into the plugin`);
  assert(manifest.schema_version === MANIFEST_SCHEMA_VERSION,
    `it declares schema_version ${MANIFEST_SCHEMA_VERSION} (got ${manifest.schema_version})`);
  assert(fwd(manifest.target_project).toLowerCase() === fwd(canonicalProjectRoot).toLowerCase(),
    "it records the project it was written for");
  assert(typeof manifest.group_hashes?.native_source === "string",
    "it records a hash per declared file group");

  console.log("\n-- 9. THE BOUNDARY: everything short of the compiler passes, and the gate says so");
  const gate = checkInstall(projectRoot);
  assert(!gate.ok, "the install gate does NOT pass, because nothing has been compiled");
  const beyondBoundary = gate.problems.filter((p) => !p.startsWith("dll:") && !p.startsWith("puerts_pin:"));
  assert(beyondBoundary.length === 0,
    `the only complaints are the compiler and the stand-in bundle (also complained about: ${beyondBoundary.join("; ") || "nothing"})`);
  assert(gate.problems.some((p) => p.includes("has never been built")),
    "and it names the missing binary specifically");
  assert(gate.report.stale_groups.length === 0,
    `every declared file group matches the repository byte for byte (stale: ${gate.report.stale_groups.join(", ") || "none"})`);
  assert(gate.report.catalog?.missing?.length === 0 && gate.report.catalog?.extra?.length === 0,
    "and the installed registry exposes exactly the MCP server's native catalog");
  console.log("          Past this line a UE4.27 installation is required: UBT builds");
  console.log("          UE4Editor-MCPBridge*.dll, the editor loads it, and only then can");
  console.log("          anything be said about whether the install RUNS.");

  console.log("\n-- 10. a manifest this checkout does not understand is refused, not read");
  const manifestPath = join(installedPlugin, MANIFEST_NAME);
  const written = readFileSync(manifestPath, "utf8");
  const withManifest = (mutate) => {
    const copy = JSON.parse(written);
    mutate(copy);
    writeFileSync(manifestPath, JSON.stringify(copy, null, 2) + "\n");
    return checkInstall(projectRoot);
  };

  const unversioned = withManifest((m) => { delete m.schema_version; });
  assert(unversioned.problems.some((p) => p.startsWith("manifest_schema:") && p.includes("before the manifest was versioned")),
    "a pre-versioning manifest is refused and named as such");

  const older = withManifest((m) => { m.schema_version = MANIFEST_SCHEMA_VERSION - 1; });
  assert(older.problems.some((p) => p.startsWith("manifest_schema:") && p.includes("install:sync")),
    "an older manifest is refused, and the fix given is a re-sync");

  const newer = withManifest((m) => { m.schema_version = MANIFEST_SCHEMA_VERSION + 1; });
  assert(newer.problems.some((p) => p.startsWith("manifest_schema:") && p.includes("Update this checkout")),
    "a newer manifest is refused, and the fix given is updating the checkout, not syncing over it");
  assert(!newer.problems.some((p) => p.includes("install:sync -- --project <path>, which rewrites")),
    "a newer manifest is never answered with 'sync from here', which would move the target backwards");

  // The point of versioning, made as a test: an unrecognised manifest's fields
  // are not consulted. status: deprecated is the field with teeth, and reading
  // it out of a shape this checkout does not know is exactly the optimism the
  // version guard exists to stop.
  const bothWrong = withManifest((m) => {
    m.schema_version = MANIFEST_SCHEMA_VERSION + 1;
    m.status = "deprecated";
    m.deprecated_reason = "would only be read if the version were trusted";
  });
  assert(bothWrong.problems.some((p) => p.startsWith("manifest_schema:")),
    "the version is reported first");
  assert(!bothWrong.problems.some((p) => p.includes("would only be read if the version were trusted")),
    "and no field of an unrecognised manifest is read, not even one that would have failed the check anyway");
  assert(bothWrong.report.manifest_schema.trusted === false,
    "the report states plainly that the manifest was not trusted");

  // The same claim for the shape that will actually turn up in the wild: a
  // manifest written before versioning existed. Every target installed before
  // this change has one, and it is the case where reading status optimistically
  // would be most tempting, because the field is right there.
  const oldAndDeprecated = withManifest((m) => {
    delete m.schema_version;
    m.status = "deprecated";
    m.deprecated_reason = "retired, and unreadable from here";
  });
  assert(!oldAndDeprecated.ok, "an unversioned manifest does not pass, whatever else it says");
  assert(!oldAndDeprecated.problems.some((p) => p.includes("retired, and unreadable from here")),
    "and its status field is not read either: an unversioned manifest is refused whole, not mined for fields");

  writeFileSync(manifestPath, written);
  const restored = checkInstall(projectRoot);
  assert(!restored.problems.some((p) => p.startsWith("manifest_schema:")),
    "and the manifest the installer actually writes passes its own version check");

  console.log(`\n${failures.length === 0 ? "all checks passed" : `${failures.length} check(s) did not hold:`}`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exitCode = failures.length === 0 ? 0 : 1;
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
