#!/usr/bin/env node
// Failure atomicity for MUTATIONS of an asset that already exists.
//
// Scripts/bp-failure-atomicity.mjs, bt-failure-atomicity.mjs and
// wbp-failure-atomicity.mjs all prove the CREATE contract: a failed build must
// leave no asset behind. This proves the other one, which no script covered:
// a failed mutation of an asset that was already on disk must leave that asset
// exactly as it was.
//
// One helper, not one script per mutator. `checkMutationIsAtomic` is the whole
// contract; the table at the bottom is the only thing a new mutator adds. The
// nineteen UBlueprintMutatorLibrary entry points share one transaction wrapper
// (BPMutatorHelpers.cpp), so they share one proof: a lever that reaches the
// wrapper's failure path proves the wrapper, and a second near-identical test
// per entry point would prove the same line twice.
//
// What "atomic" is asserted to mean here, all four measured independently of
// what the command says about itself:
//
//   1. the .uasset file's SHA-256 is unchanged
//   2. the member structure hash read back by the inspector is unchanged
//   3. the asset's package is not dirty afterwards
//   4. no source-control entry appeared (`p4 opened` unchanged, when p4 exists)
//
// NOT RUN by the lane that wrote it: no editor was launched. Running it is the
// integrator's step.
//
// Usage:
//   node Scripts/mutator-atomicity.mjs
//   import { checkMutationIsAtomic, startBridge } from "./mutator-atomicity.mjs";

import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");

/** Spawn the built MCP server over stdio and return a JSON-RPC caller. */
export function startBridge(clientName) {
  const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
  if (!existsSync(serverPath)) throw new Error("Run npm run build first");
  const child = spawn(process.execPath, [serverPath], { stdio: ["pipe", "pipe", "inherit"], env: process.env });
  let buffer = "";
  let nextId = 1;
  const pending = new Map();
  child.stdout.on("data", (chunk) => {
    buffer += chunk.toString();
    for (let n; (n = buffer.indexOf("\n")) >= 0;) {
      const line = buffer.slice(0, n).trim();
      buffer = buffer.slice(n + 1);
      if (!line) continue;
      const message = JSON.parse(line);
      const complete = pending.get(message.id);
      if (complete) { pending.delete(message.id); complete(message); }
    }
  });
  const send = (method, params) => {
    const id = nextId++;
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
      pending.set(id, (message) => { clearTimeout(timer); resolve(message); });
    });
  };
  const call = async (name, args = {}) => {
    const message = await send("tools/call", { name, arguments: args });
    const text = message?.result?.content?.[0]?.text;
    if (typeof text !== "string") throw new Error(`${name} returned no JSON content`);
    return JSON.parse(text);
  };
  const ready = async () => {
    await send("initialize", {
      protocolVersion: "2024-11-05", capabilities: {},
      clientInfo: { name: clientName, version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");
  };
  return { call, ready, close: () => child.stdin.end() };
}

export const contentFile = (projectRoot, packagePath) =>
  join(projectRoot, "Content", packagePath.replace("/Game/", "").replaceAll("/", "\\") + ".uasset");

/** `p4 opened` output, or null when p4 is not installed or not configured. */
export function p4Opened(projectRoot) {
  try {
    return execSync("p4 opened", {
      cwd: join(projectRoot, "Content"), encoding: "utf8", stdio: ["ignore", "pipe", "ignore"],
    });
  } catch {
    return null;
  }
}

/**
 * Everything about the asset that a mutation must not move when it fails.
 *
 * The file hash and the inspector read are deliberately both here and neither
 * is derived from the other: the file answers what reached disk, the inspector
 * answers what is in memory, and a mutation that half-applied without saving
 * moves only the second. Trusting either alone misses one of the two ways this
 * can go wrong.
 */
export async function fingerprint(call, projectRoot, assetPath) {
  const file = contentFile(projectRoot, assetPath);
  const read = await call("puerts_graph_inspect", { asset_path: assetPath });
  return {
    file_exists: existsSync(file),
    file_sha256: existsSync(file) ? createHash("sha256").update(readFileSync(file)).digest("hex") : null,
    member_hash: read.data?.member_structure_hash_sha1 ?? null,
    package_dirty: read.data?.package_dirty_after ?? null,
    p4_opened: p4Opened(projectRoot),
    inspect_ok: read.success === true,
  };
}

/**
 * Run one mutation that is expected to fail and assert it changed nothing.
 *
 * `mutate` is any async function that performs the mutation and resolves with
 * the command response. A new mutator needs a row in the table, not a new file.
 *
 * `expectFailure: false` inverts the whole check into the control case: the
 * mutation must succeed AND the fingerprint must move. Without that direction a
 * harness whose fixture never changes passes every atomicity assertion for the
 * wrong reason, which is indistinguishable from a working fix.
 */
export async function checkMutationIsAtomic({
  call, assert, projectRoot, assetPath, label, mutate, expectFailure = true,
}) {
  const before = await fingerprint(call, projectRoot, assetPath);
  if (!assert(before.inspect_ok && before.file_exists, `${label}: the fixture is present before the mutation`)) {
    return { before, after: null, response: null };
  }

  let response;
  try {
    response = await mutate();
  } catch (error) {
    // A tool that rejects at the MCP layer never reached the mutator. That is
    // still a failed mutation and the asset still has to be untouched.
    response = { success: false, errors: [String(error?.message ?? error)] };
  }
  const after = await fingerprint(call, projectRoot, assetPath);

  if (!expectFailure) {
    assert(response.success === true, `${label}: the control mutation succeeds`);
    assert(after.file_sha256 !== before.file_sha256 || after.member_hash !== before.member_hash,
      `${label}: the control mutation actually moves the fingerprint (so the checks above can fail)`);
    return { before, after, response };
  }

  assert(response.success === false, `${label}: the mutation fails`);
  assert(after.file_sha256 === before.file_sha256,
    `${label}: the .uasset is byte-identical (${before.file_sha256?.slice(0, 12)} vs ${after.file_sha256?.slice(0, 12)})`);
  assert(after.member_hash === before.member_hash,
    `${label}: the member structure read back is unchanged (${before.member_hash} vs ${after.member_hash})`);
  assert(after.package_dirty === false, `${label}: the asset's package is not dirty afterwards`);
  if (before.p4_opened !== null && after.p4_opened !== null) {
    assert(before.p4_opened === after.p4_opened, `${label}: no source-control entry appeared`);
  } else {
    console.log(`  INFO  ${label}: p4 unavailable; source-control quiescence not measured`);
  }
  return { before, after, response };
}

// --- The table. One row per lever; a new mutator adds a row. -----------------

const BP = "/Game/MCPGenerated/BP_MutatorAtomicity";

// Rebuilt from this whole spec on every run, with remove_unlisted so a variable
// left behind by an earlier run cannot survive into the measurement. A fixture
// that carries state between runs makes every later red unreadable, which is
// what happened to bp-member-patch-acceptance.
const FIXTURE = {
  asset_path: BP,
  parent_class: "Actor",
  components: [{ class: "SceneComponent", name: "Root" }],
  variables: [
    { name: "Count", type: "int", default: 7 },
    { name: "Ratio", type: "float", default: 0.5 },
    { name: "Label", type: "string", default: "seed" },
  ],
  graph: {
    nodes: [
      { id: "begin", type: "BeginPlay", x: 0, y: 0 },
      { id: "say", type: "PrintString", x: 300, y: 0, params: { InString: "atomicity probe" } },
    ],
    connections: [{ from: "begin.then", to: "say.exec" }],
  },
  remove_unlisted: { variables: true },
};

/**
 * The levers are ordered by which layer is supposed to stop them, because that
 * is the thing worth knowing when one goes red. Atomicity is asserted the same
 * way for all of them: whoever refuses, the asset must not move.
 *
 * "not-a-number" on a FLOAT is the value that used to get through. UE4.27's
 * FNumericProperty::ImportText reports success for it and writes 0.0
 * (PropertyNumeric.cpp:113-123), so it passed validation, applied, verified
 * against itself and saved. int rejects the same string, which is why the
 * defect was invisible until someone used a float.
 */
const LEVERS = [
  {
    label: "(1) set_variable_default, a float value the property type cannot hold",
    // Refused by pass 1: the variable exists, so CompareVariableDefault has a
    // compiled property to test the value against and returns Unavailable
    // before anything is mutated. Nothing reaches the mutator.
    caught_by: "validate-before-mutate",
    operations: [{ op: "set_variable_default", name: "Ratio", default: "not-a-number" }],
  },
  {
    label: "(2) add_variable, a NEW float variable whose default cannot be held",
    // The lever that actually reaches the mutator. A variable that does not
    // exist yet has no compiled property, so pass 1 has nothing to check the
    // default against; FBPVariableOps::AddVariable can only ask once its own
    // compile has produced the property, which is after the write. This is the
    // path through FBPMutatorHelpers::RunMutation's failure branch and the
    // command boundary's revert.
    caught_by: "the mutator, after the write",
    operations: [{ op: "add_variable", name: "BadFloat", type: { category: "float" }, default: "not-a-number" }],
  },
  {
    label: "(3) a batch whose first operation lands and whose second cannot",
    // The first operation succeeds and compiles; the second is lever (2).
    // Proves the failure path unwinds work done by an EARLIER operation in the
    // batch, not only the operation that failed.
    caught_by: "the mutator, after two writes",
    operations: [
      { op: "set_variable_default", name: "Ratio", default: 0.75 },
      { op: "add_variable", name: "BadFloat", type: { category: "float" }, default: "not-a-number" },
    ],
  },
];

async function main() {
  const projectRoot = requireCurrentInstall();
  const bridge = startBridge("mutator-atomicity");
  const failures = [];
  const assert = (condition, label) => {
    if (condition) { console.log(`  PASS  ${label}`); return true; }
    console.log(`  FAIL  ${label}`);
    failures.push(label);
    return false;
  };
  const evidence = { asset: BP, levers: [] };

  try {
    await bridge.ready();

    console.log("\n-- reseed the fixture from its whole spec");
    const seeded = await bridge.call("puerts_blueprint_build", FIXTURE);
    assert(seeded.success === true, `(0) the fixture builds ${seeded.success ? "" : JSON.stringify(seeded.errors)}`);
    assert(seeded.data?.saved === true, "(0) the fixture is saved, so there is a file to compare against");

    for (const lever of LEVERS) {
      console.log(`\n-- ${lever.label}`);
      const result = await checkMutationIsAtomic({
        call: bridge.call, assert, projectRoot, assetPath: BP, label: lever.label,
        mutate: () => bridge.call("puerts_blueprint_member_patch", {
          asset_path: BP, operations: lever.operations, compile: true, save: true, verify: true,
        }),
      });
      evidence.levers.push({ label: lever.label, caught_by: lever.caught_by, ...result });
    }

    console.log("\n-- control: a mutation that must succeed and must move the asset");
    const control = await checkMutationIsAtomic({
      call: bridge.call, assert, projectRoot, assetPath: BP, label: "(4) control",
      expectFailure: false,
      mutate: () => bridge.call("puerts_blueprint_member_patch", {
        asset_path: BP,
        operations: [{ op: "add_variable", name: "AtomicityControl", type: { category: "float" }, default: 1 }],
        compile: true, save: true, verify: true,
      }),
    });
    evidence.levers.push({ label: "(4) control", ...control });

    const evidenceDir = join(repoRoot, "docs", "evidence");
    mkdirSync(evidenceDir, { recursive: true });
    const out = join(evidenceDir, "mutator-atomicity.json");
    writeFileSync(out, JSON.stringify(evidence, null, 2));
    console.log(`\nevidence: ${out}`);

    if (failures.length > 0) {
      console.log(`\n${failures.length} check(s) did not hold:`);
      for (const f of failures) console.log(`  - ${f}`);
      process.exitCode = 1;
    } else {
      console.log("\nall checks passed");
    }
  } catch (error) {
    console.error(error);
    process.exitCode = 1;
  } finally {
    bridge.close();
  }
}

// Importing this file must not launch an editor session; only running it does.
//
// pathToFileURL, not a hand-built "file://" + path. On Windows the hand-built
// form produced file://D:/Unreal Projects/... while import.meta.url is
// file:///D:/Unreal%20Projects/... - three slashes and a percent-encoded space.
// They never matched, so main() never ran and this script exited 0 having
// tested nothing, which is the one result a test must never produce.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  await main();
}
