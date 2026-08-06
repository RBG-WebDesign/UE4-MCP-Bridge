#!/usr/bin/env node
// Negative tests for the four things the bridge must refuse: a bad bearer
// token, a path outside the root it is allowed to touch, an attempt at shell
// execution, and a write to a property nobody approved.
//
//   node Scripts/security-refusal-acceptance.mjs
//
// Same method as Scripts/bridge-install-acceptance.mjs: take the shipped
// artefact, attack it, and require a refusal. A security boundary that has only
// ever been exercised by well-formed requests has not been shown to stop
// anything, and "the tests pass" is what an absent boundary also reports.
//
// HOW FAR EACH CASE GETS WITHOUT AN EDITOR, because the four are not equal and
// pretending they are would be the whole problem in miniature:
//
//   EXECUTED   the refusal happens in this process (or a child of it), against
//              the built artefact, and is observed. Shell blocking and both
//              path guards are here.
//   EXECUTED   partially: the MCP server's own half of token handling refuses
//              here; the editor's comparison of the token cannot.
//   STATIC     the refusal lives in compiled C++ that needs UE4.27 to run. The
//              assertion is that the shipped source still contains the check.
//              That catches a deletion, and nothing else. It is not a proof
//              that the running editor refuses; only a live editor shows that.
//
// Every line below prints which of those it is. The live half belongs to
// Scripts/editor-acceptance.mjs and needs an editor; see the CI split in
// .github/workflows/ci.yml.

import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const stagedRuntime = join(repoRoot, "Plugins", "MCPBridge", "Content", "JavaScript", "safety.js");
const serverDist = join(repoRoot, "mcp-server", "dist");
if (!existsSync(stagedRuntime) || !existsSync(serverDist)) {
  console.error("Run npm run build first: this tests the built artefacts, not the sources.");
  process.exit(1);
}

const failures = [];
function assert(kind, condition, label) {
  if (condition) { console.log(`  PASS  [${kind}] ${label}`); return true; }
  console.log(`  FAIL  [${kind}] ${label}`); failures.push(label); return false;
}

/** Run a snippet in a fresh Node process. installRuntimeGuards replaces the
    module loader for the whole process, so each case needs its own. */
function inChild(source) {
  try {
    return { ok: true, out: execFileSync(process.execPath, ["-e", source], { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] }) };
  } catch (error) {
    return { ok: false, out: `${error.stdout ?? ""}${error.stderr ?? ""}` };
  }
}

const nativeSource = readFileSync(
  join(repoRoot, "Plugins", "MCPBridge", "Source", "MCPBridgePuerTS", "Private", "MCPPuerTSBridgeService.cpp"), "utf8");
const nativeHeader = readFileSync(
  join(repoRoot, "Plugins", "MCPBridge", "Source", "MCPBridgePuerTS", "Public", "MCPPuerTSBridgeService.h"), "utf8");

const scratch = mkdtempSync(join(tmpdir(), "security-refusal-"));
try {
  console.log("security refusal acceptance\n");

  // ---------------------------------------------------------------- 1. token
  console.log("-- 1. a bad bearer token");
  const { PuerTSClient } = await import(new URL("../mcp-server/dist/puerts-client.js", import.meta.url));

  // A project that advertises a live-looking session, so nothing else can be
  // the reason the request fails. Only the token is wrong.
  const tokenProject = join(scratch, "TokenProject");
  mkdirSync(join(tokenProject, "Saved", "MCPPuerTSBridge"), { recursive: true });
  writeFileSync(join(tokenProject, "Saved", "MCPPuerTSBridge", "session.json"), JSON.stringify({
    schema_version: 1, session_id: "s", session_nonce: "n", editor_pid: process.pid,
    process_start_time: "", project_path: tokenProject, uproject_path: "",
    pipe_name: "\\\\.\\pipe\\UE427PuerTSMCP_nothing_listening",
    bridge_commit: "", installed_manifest_hash: "", created_at: "", last_heartbeat_at: "",
    shutdown_state: "running",
  }));

  const callWith = async (env) => {
    const saved = { ...process.env };
    Object.assign(process.env, { MCP_UNREAL_PROJECT_ROOT: tokenProject, ...env });
    delete process.env.MCP_PUERTS_TOKEN;
    delete process.env.MCP_PUERTS_PIPE;
    for (const [k, v] of Object.entries(env)) process.env[k] = v;
    try {
      await new PuerTSClient().call("diagnostic", {}, 2000);
      return "the call was allowed to proceed";
    } catch (error) {
      return error.message;
    } finally {
      for (const key of Object.keys(process.env)) if (!(key in saved)) delete process.env[key];
      Object.assign(process.env, saved);
    }
  };

  writeFileSync(join(tokenProject, "Saved", "MCPPuerTSBridge", "token.txt"), "   \n");
  const empty = await callWith({ MCP_PUERTS_TOKEN_PATH: join(tokenProject, "Saved", "MCPPuerTSBridge", "token.txt") });
  assert("EXECUTED", /bearer token is empty/i.test(empty),
    `an empty token file stops the request before it reaches a pipe (got: ${empty})`);

  const absent = await callWith({ MCP_PUERTS_TOKEN_PATH: join(scratch, "no-such-token.txt") });
  assert("EXECUTED", /ENOENT|no such file/i.test(absent) && !/timed out/i.test(absent),
    `a missing token file stops it too, rather than connecting unauthenticated (got: ${absent})`);

  // The comparison itself is the editor's. What can be checked from here is
  // that the shipped boundary still performs it, and still refuses to run at
  // all with authentication turned off.
  assert("STATIC", nativeSource.includes("SuppliedToken != BearerToken")
    && nativeSource.includes("A valid local bearer token is required."),
    "the native boundary still compares the supplied token and refuses a mismatch");
  assert("STATIC", nativeSource.includes("Named-pipe commands require bearer authentication."),
    "and refuses to initialise at all when bRequireBearerToken is turned off");
  assert("STATIC", /bool\s+bRequireBearerToken\s*=\s*true/.test(nativeHeader),
    "bearer authentication is on by default, so a project that configures nothing is still authenticated");

  // ------------------------------------------------------------------ 2. path
  console.log("\n-- 2. a path outside the root the caller is allowed to touch");

  // 2a. The MCP server's own reader, which runs with the editor closed and is
  // the one tool here that takes a caller-supplied file path directly.
  const fakeEngine = join(scratch, "FakeEngine");
  mkdirSync(join(fakeEngine, "Engine", "Source", "Runtime"), { recursive: true });
  writeFileSync(join(fakeEngine, "Engine", "Source", "Runtime", "Inside.h"), "// inside the engine\n");
  writeFileSync(join(scratch, "Outside.txt"), "SECRET\n");
  const { createEngineSourceTools } = await import(new URL("../mcp-server/dist/tools/engine-source.js", import.meta.url));
  const readTool = createEngineSourceTools().find((t) => t.name === "engine_source_read");
  process.env.UE_ENGINE_ROOT = fakeEngine;
  const readFile_ = async (file) => JSON.stringify(await readTool.handler({ file, start_line: 1 }));

  const inside = await readFile_("Source/Runtime/Inside.h");
  assert("EXECUTED", inside.includes("inside the engine"),
    "a path inside the engine root is read, so the refusal below is about containment and not about failing everything");
  for (const escape of ["../../Outside.txt", "Source/../../Outside.txt", "..\\..\\Outside.txt"]) {
    const result = await readFile_(escape);
    assert("EXECUTED", /resolves outside the engine directory/.test(result) && !result.includes("SECRET"),
      `"${escape}" is refused by name and its content never appears in the answer`);
  }
  const absolute = await readFile_(join(scratch, "Outside.txt"));
  assert("EXECUTED", !absolute.includes("SECRET"),
    "an absolute path outside the engine root does not return the file either");

  // 2b. The guard the in-editor runtime installs over fs. Same property, other
  // side of the pipe, and the one that decides what approved TypeScript can
  // reach once it is running inside Unreal.
  const guardRoot = join(scratch, "GuardedProject");
  mkdirSync(guardRoot, { recursive: true });
  writeFileSync(join(guardRoot, "allowed.txt"), "in project\n");
  const jsRoot = JSON.stringify(guardRoot);
  const jsSafety = JSON.stringify(stagedRuntime);
  const jsOutside = JSON.stringify(join(scratch, "Outside.txt"));

  const insideGuard = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
    process.stdout.write(require("fs").readFileSync(${JSON.stringify(join(guardRoot, "allowed.txt"))}, "utf8"));
  `);
  assert("EXECUTED", insideGuard.ok && insideGuard.out.includes("in project"),
    "with the guards installed, a read inside the project still works");

  const outsideGuard = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
    require("fs").readFileSync(${jsOutside}, "utf8");
  `);
  assert("EXECUTED", !outsideGuard.ok && /File-system access outside the project is blocked/.test(outsideGuard.out),
    "a read outside the project is blocked by name");

  const traversalGuard = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
    require("fs").writeFileSync(require("path").join(${jsRoot}, "..", "escaped.txt"), "x");
  `);
  assert("EXECUTED", !traversalGuard.ok && /File-system access outside the project is blocked/.test(traversalGuard.out),
    "and so is a WRITE that climbs out with ..");
  assert("EXECUTED", !existsSync(join(scratch, "escaped.txt")),
    "the escaped file was never created");

  const chdirGuard = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
    process.chdir(${JSON.stringify(scratch)});
  `);
  assert("EXECUTED", !chdirGuard.ok && /File-system access outside the project is blocked/.test(chdirGuard.out),
    "and chdir out of the project, which would otherwise make every relative path an escape");

  assert("STATIC", nativeSource.includes("AllowedScriptRoot must stay inside the Unreal project."),
    "the native boundary refuses an AllowedScriptRoot configured outside the project");

  // ----------------------------------------------------------------- 3. shell
  console.log("\n-- 3. an attempt at shell execution");
  for (const moduleName of ["child_process", "node:child_process", "worker_threads", "cluster", "repl"]) {
    const blocked = inChild(`
      require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
      require(${JSON.stringify(moduleName)});
    `);
    assert("EXECUTED", !blocked.ok && blocked.out.includes(`Blocked Node.js module: ${moduleName}`),
      `require("${moduleName}") is refused by name`);
  }
  const spawnAttempt = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, false);
    require("child_process").execSync("echo pwned");
  `);
  assert("EXECUTED", !spawnAttempt.ok && !spawnAttempt.out.includes("pwned"),
    "an actual command never runs");

  const shellEnabled = inChild(`
    require(${jsSafety}).installRuntimeGuards(${jsRoot}, true);
  `);
  assert("EXECUTED", !shellEnabled.ok && /attempted to enable shell commands/.test(shellEnabled.out),
    "and the runtime refuses to start at all if the native side ever asks for shell commands");

  assert("STATIC", nativeSource.includes("Shell commands must remain disabled."),
    "the native boundary refuses to initialise when bAllowShellCommands is set");
  assert("STATIC", /bool\s+bAllowShellCommands\s*=\s*false/.test(nativeHeader),
    "and shell commands are off by default, so the refusal above is not the only thing holding the line");

  // -------------------------------------------------------------- 4. property
  console.log("\n-- 4. a write to a property nobody approved");
  // No part of this is executable without a live editor: the allowlist is
  // matched against the UClass hierarchy of a real UObject, which only exists
  // inside a running Unreal. Stated rather than faked.
  assert("STATIC", nativeSource.includes("IsWritablePropertyAllowed")
    && nativeSource.includes("Writable property is not approved."),
    "set_property consults the allowlist and refuses a property that is not on it");
  assert("STATIC", /if\s*\(!IsWritablePropertyAllowed\(Object,\s*PropertyName\)\)/.test(nativeSource),
    "the check is on the path that performs the write, not somewhere a caller could route around");
  assert("STATIC", nativeSource.includes('GetArray(BridgeConfigSection, TEXT("AllowedWritableProperties")'),
    "the allowlist is a project config value, so a project can narrow it");
  // Unbounded on purpose: the array has grown from a handful of properties to
  // dozens as capabilities widened (scene_batch, class_defaults_patch, ...),
  // each documented inline. A fixed character budget here (previously 400)
  // silently stops matching once the real array outgrows it, which makes this
  // assertion pass or fail based on array length instead of array content.
  // There is no nested `{` inside a `TCHAR*[]` initializer, so the first `};`
  // after the anchor is genuinely this array's own close.
  const defaults = nativeSource.match(/TEXT\("Actor\.bHidden"\)[\s\S]*?\};/)?.[0] ?? "";
  assert("STATIC", defaults.includes("Actor.ActorLabel") && !/TEXT\("\*"\)/.test(defaults),
    "and the built-in default list is a small enumerated set, with no wildcard");
  assert("STATIC", defaults.includes('TEXT("Pawn.AutoPossessPlayer")'),
    "the built-in default list permits an authored Pawn to possess Player0");
  console.log("          Not executable here: the allowlist is matched against a live UClass");
  console.log("          hierarchy. Proving the running editor refuses needs an editor, and");
  console.log("          that case is listed as editor-required in .github/workflows/ci.yml.");

  console.log(`\n${failures.length === 0 ? "all checks passed" : `${failures.length} check(s) did not hold:`}`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exitCode = failures.length === 0 ? 0 : 1;
} finally {
  rmSync(scratch, { recursive: true, force: true });
}
