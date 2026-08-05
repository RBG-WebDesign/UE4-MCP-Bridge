#!/usr/bin/env node
// Live acceptance for puerts_project_settings_patch against a running UE4.27
// editor.
//
// Proves, in order: that a Project Settings page is readable through the plain
// property reader with no bespoke tool, that plan_only writes nothing, that an
// applied change reaches the ini FILE on disk (read back by this script, not
// by the tool that wrote it), that a rerun converges instead of rewriting, that
// a property without CPF_Config is refused rather than set in memory, and that
// an unknown property names close matches. Restores every value it touched.
//
// Requires MCP_UNREAL_PROJECT_ROOT pointing at the running editor's project.
import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const serverPath = join(root, "mcp-server", "dist", "index.js");
if (!existsSync(serverPath)) throw new Error("Run npm run build first");
requireCurrentInstall();

const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT to the running editor's project.");
const iniPath = join(projectRoot, "Config", "DefaultGame.ini");

const child = spawn(process.execPath, [serverPath], {
  stdio: ["pipe", "pipe", "inherit"],
  env: process.env,
});
let buffer = "";
let nextId = 1;
const pending = new Map();
child.stdout.on("data", (chunk) => {
  buffer += chunk.toString();
  for (let newline; (newline = buffer.indexOf("\n")) >= 0;) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    if (!line) continue;
    const message = JSON.parse(line);
    const complete = pending.get(message.id);
    if (complete) { pending.delete(message.id); complete(message); }
  }
});

function send(method, params) {
  const id = nextId++;
  child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 40000);
    pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
  });
}

/** Returns the parsed payload whether the command succeeded or refused, so a
    refusal can be asserted on instead of thrown over. */
async function raw(name, args = {}) {
  const message = await send("tools/call", { name, arguments: args });
  const text = message?.result?.content?.[0]?.text;
  if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
  return JSON.parse(text);
}

async function call(name, args = {}) {
  const result = await raw(name, args);
  if (!result.success) throw new Error(`${name}: ${result.errors?.join("; ") || result.message}`);
  return result;
}

function assert(condition, label) {
  if (!condition) throw new Error(`FAIL  ${label}`);
  console.log(`  PASS  ${label}`);
}

const SECTION = "GeneralProjectSettings";
const CDO = "/Script/EngineSettings.Default__GeneralProjectSettings";
const INI_SECTION = "/Script/EngineSettings.GeneralProjectSettings";
const PROBE_DESCRIPTION = "MCP bridge acceptance probe, safe to delete.";

/** Reads one key out of the ini section on disk. This script parses the file
    itself on purpose: asking the writer whether it wrote is not verification. */
function iniValue(key) {
  if (!existsSync(iniPath)) return undefined;
  const lines = readFileSync(iniPath, "utf8").split(/\r?\n/);
  let inSection = false;
  let found;
  for (const line of lines) {
    const trimmed = line.trim();
    if (trimmed.startsWith("[")) { inSection = trimmed === `[${INI_SECTION}]`; continue; }
    if (!inSection) continue;
    const eq = trimmed.indexOf("=");
    if (eq < 0) continue;
    if (trimmed.slice(0, eq).trim() === key) found = trimmed.slice(eq + 1).trim();
  }
  return found;
}

let originalDescription;
let originalBorderless;

async function main() {
  await send("initialize", {
    protocolVersion: "2024-11-05",
    capabilities: {},
    clientInfo: { name: "project-settings-patch-acceptance", version: "1.0.0" },
  });

  console.log("\n-- 1. a Project Settings page reads through the plain property reader");
  const readDescription = await call("puerts_read_property", { object_path: CDO, property: "Description" });
  const readBorderless = await call("puerts_read_property", { object_path: CDO, property: "bUseBorderlessWindow" });
  originalDescription = readDescription.data.value ?? "";
  originalBorderless = readBorderless.data.value ?? false;
  assert(typeof originalDescription === "string", "Description reads off the settings CDO with no bespoke tool");
  assert(typeof originalBorderless === "boolean", "bUseBorderlessWindow reads back as a real boolean");

  console.log("\n-- 2. plan_only changes nothing on disk");
  const beforePlan = existsSync(iniPath) ? readFileSync(iniPath) : Buffer.alloc(0);
  const planned = await call("puerts_project_settings_patch", {
    section: SECTION,
    properties: { Description: PROBE_DESCRIPTION },
    plan_only: true,
  });
  const afterPlan = existsSync(iniPath) ? readFileSync(iniPath) : Buffer.alloc(0);
  assert(planned.data.applied === false, "plan_only reports applied=false");
  assert(planned.data.properties_to_apply.includes("Description"), "plan_only names the property it would write");
  assert(beforePlan.equals(afterPlan), "plan_only left DefaultGame.ini byte-identical");

  console.log("\n-- 3. an applied change reaches the ini file on disk");
  const applied = await call("puerts_project_settings_patch", {
    section: SECTION,
    properties: { Description: PROBE_DESCRIPTION, bUseBorderlessWindow: !originalBorderless },
  });
  assert(applied.data.applied === true, "the patch reports applied=true");
  assert(applied.data.ini.replace(/\\/g, "/").endsWith("Config/DefaultGame.ini"),
    `the engine routed this page to DefaultGame.ini (${applied.data.ini})`);
  assert(iniValue("Description") === PROBE_DESCRIPTION,
    "this script read Description back out of the ini file itself");
  // UE writes bools as True/False. Compare case-insensitively rather than
  // hardcoding the engine's casing into the assertion.
  assert((iniValue("bUseBorderlessWindow") ?? "").toLowerCase() === String(!originalBorderless),
    `the ini holds bUseBorderlessWindow=${!originalBorderless}`);

  console.log("\n-- 4. a rerun converges instead of rewriting");
  const again = await call("puerts_project_settings_patch", {
    section: SECTION,
    properties: { Description: PROBE_DESCRIPTION, bUseBorderlessWindow: !originalBorderless },
  });
  assert(again.data.unchanged_properties.length === 2, "both properties are reported unchanged");
  assert((again.data.applied_properties ?? []).length === 0, "nothing was rewritten on the second run");

  console.log("\n-- 5. refusals name the problem instead of half-writing");
  const notConfig = await raw("puerts_project_settings_patch", {
    section: "StaticMeshActor",
    properties: { bHidden: true },
  });
  assert(notConfig.success === false, "a property without CPF_Config is refused");
  assert(JSON.stringify(notConfig.errors).includes("not a config property"),
    "the refusal says why: it would change in memory and never reach the ini");

  const unknown = await raw("puerts_project_settings_patch", {
    section: SECTION,
    properties: { Descriptionn: "typo" },
  });
  assert(unknown.success === false, "an unknown property is refused");
  assert(JSON.stringify(unknown.errors).includes("Description"),
    "the refusal offers the closest config property names");

  const badSection = await raw("puerts_project_settings_patch", {
    section: "NoSuchSettingsClass",
    properties: { Description: "x" },
  });
  assert(badSection.success === false, "an unresolvable section is refused rather than guessed at");

  // The editor shows EncryptionKey as VisibleAnywhere: a human cannot retype it
  // in the Project Settings window, and neither can this tool. Overwriting a
  // live key leaves already-packaged content unreadable and cannot be undone.
  const readOnly = await raw("puerts_project_settings_patch", {
    section: "CryptoKeysSettings",
    properties: { EncryptionKey: "bm90LWEtcmVhbC1rZXk=" },
  });
  assert(readOnly.success === false, "a VisibleAnywhere property is refused, matching the editor's own UI");
  assert(JSON.stringify(readOnly.errors).includes("read-only in the Project Settings window"),
    "the refusal explains the parity rule rather than naming a forbidden class");

  // The same page's editable toggles are still writable, so this is UI parity,
  // not a blanket ban on the crypto page.
  const editableOnSamePage = await raw("puerts_project_settings_patch", {
    section: "CryptoKeysSettings",
    properties: { bEncryptPakIniFiles: false },
    plan_only: true,
  });
  assert(editableOnSamePage.success === true,
    "an EditAnywhere property on that same page is still allowed");
}

main()
  .then(async () => {
    console.log("\n-- 6. restore what this script touched");
    const restored = await call("puerts_project_settings_patch", {
      section: SECTION,
      properties: { Description: originalDescription, bUseBorderlessWindow: originalBorderless },
    });
    assert(restored.success === true, "originals restored");
    assert((iniValue("Description") ?? "") === originalDescription,
      "the ini holds the original Description again");
    console.log("\nproject settings patch acceptance: all checks passed.");
    child.kill();
    process.exit(0);
  })
  .catch(async (error) => {
    console.error(`\n${error.message}`);
    if (originalDescription !== undefined) {
      try {
        await call("puerts_project_settings_patch", {
          section: SECTION,
          properties: { Description: originalDescription, bUseBorderlessWindow: originalBorderless },
        });
        console.error("(originals were restored before exiting)");
      } catch (restoreError) {
        console.error(`(RESTORE FAILED: ${restoreError.message})`);
      }
    }
    child.kill();
    process.exit(1);
  });
