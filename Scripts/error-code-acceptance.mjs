#!/usr/bin/env node
// Live acceptance for stable error codes, against a running UE4.27 editor.
//
// The property under test is that a client can branch on a refusal without
// pattern-matching English, and that the code survives every layer between the
// refusal and the client: the JavaScript runtime, the C++ response normalizer
// (which rebuilds the envelope from a field allowlist), and the named pipe.
//
// Codes exist only where a client would act differently on seeing one. A
// refusal with no code is not a defect, so this asserts the coded paths and
// records the uncoded ones rather than demanding a code everywhere.
//
//   node Scripts/error-code-acceptance.mjs

import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
if (!existsSync(join(repoRoot, "mcp-server", "dist", "index.js"))) {
  throw new Error("mcp-server/dist is missing. Run npm run build first.");
}
const projectArg = process.argv.indexOf("--project");
if (projectArg >= 0) process.env.MCP_UNREAL_PROJECT_ROOT = process.argv[projectArg + 1];
process.env.MCP_UNREAL_PROJECT_ROOT ??= "D:/Unreal Projects/BridgeInstallTest";

const { PuerTSClient } = await import(
  pathToFileURL(join(repoRoot, "mcp-server", "dist", "puerts-client.js")).href);
const client = new PuerTSClient();

const failures = [];
function assert(condition, label, detail = "") {
  if (condition) { console.log(`  PASS  ${label}`); return; }
  console.log(`  FAIL  ${label}${detail ? `\n        ${detail}` : ""}`);
  failures.push(label);
}

console.log(`error-code acceptance against ${process.env.MCP_UNREAL_PROJECT_ROOT}\n`);

// Each case: a request that must be refused, the code it must carry, and
// whether a transaction is expected to have been opened before the refusal.
//
// That last column is not a detail. AcceptCommand opens the FScopedTransaction
// for every mutating tool BEFORE the JavaScript registry runs, so a refusal
// raised in the runtime - which is where the path and confirmation gates fire -
// happens with a transaction already open. It records nothing and UE discards
// an empty transaction, but the id is real and a test that demanded an empty
// one would be asserting something the architecture does not promise. Only the
// checks that run inside AcceptCommand itself, quarantine and preconditions,
// refuse before it opens.
const cases = [
  ["path_not_allowed", "find_assets", { path: "/Script/Engine" }, false],
  ["path_not_allowed", "save", { assets: ["/Engine/BasicShapes/Cube"] }, false],
  ["path_not_allowed", "delete_asset", { asset_path: "/Engine/Probe", confirm: true }, false],
  ["path_not_allowed", "blueprint_build", { asset_path: "/Game/Elsewhere/BP_Probe" }, true],
  ["confirmation_required", "delete_actor", { actor: "NoSuchProbeActor", confirm: false }, true],
  ["capability_quarantined", "level_load", { level_path: "/Game/Maps/Probe" }, false],
];

for (const [code, command, params, mutating] of cases) {
  const result = await client.call(command, params, 20000);
  assert(result.success === false, `${command} is refused`, JSON.stringify(result.errors));
  assert(result.error_code === code,
    `${command} carries error_code ${code}`,
    `got ${JSON.stringify(result.error_code)}; message: ${JSON.stringify(result.errors?.[0])}`);
  assert((result.changed_assets ?? []).length === 0 && (result.changed_actors ?? []).length === 0,
    `${command} changed nothing`);
  if (mutating) {
    assert(result.transaction_id !== "",
      `${command} is mutating, so AcceptCommand opened a transaction before the runtime refused`,
      "an empty id here would mean the transaction boundary moved; preconditions depend on where it is");
  } else {
    assert(result.transaction_id === "",
      `${command} refused without opening a transaction`);
  }
}

// The code must survive CompleteCommand, which rebuilds the envelope from a
// field allowlist and silently drops anything not on it. That is how error_code
// was lost the first time it was added, and only a live call can catch it.
const survived = await client.call("find_assets", { path: "/Script/Engine" }, 20000);
assert(Object.prototype.hasOwnProperty.call(survived, "error_code"),
  "error_code survives the C++ response normalizer");

// A success must not carry one, or a client cannot use its presence as a signal.
const ok = await client.call("diagnostic", { actor_limit: 1 }, 20000);
assert(ok.success === true && ok.error_code === undefined,
  "a successful command carries no error_code", `got ${JSON.stringify(ok.error_code)}`);

// The code must not leak from one command into the next: it is per-command
// state on the service, and a stale one would mislabel an unrelated refusal.
const uncoded = await client.call("call_function", { actor: "NoSuchProbeActor", function: "NoSuchFn" }, 20000);
assert(uncoded.success === false, "an uncoded refusal still fails");
assert(uncoded.error_code === undefined,
  "an uncoded refusal carries no stale code from the previous command",
  `got ${JSON.stringify(uncoded.error_code)}`);

console.log(`\n${failures.length === 0 ? "OK" : "FAIL"}: error-code acceptance, ${failures.length} failure(s)`);
if (failures.length > 0) {
  for (const f of failures) console.log(`  - ${f}`);
  process.exit(1);
}
