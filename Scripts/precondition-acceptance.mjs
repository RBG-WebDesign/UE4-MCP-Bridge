#!/usr/bin/env node
// Live acceptance for the scene_structure_hash precondition, against a running
// UE4.27 editor.
//
// The property under test is that a plan built against one level state cannot
// be applied to a different one by accident, and that the refusal costs
// nothing: no transaction, no undo entry, no dirty package, no actor moved.
//
// The refusal is enforced in UMCPPuerTSBridgeService::CheckPreconditions, which
// runs inside AcceptCommand. That position is the point. AcceptCommand opens
// the FScopedTransaction for every mutating tool BEFORE the JavaScript registry
// runs, so a check anywhere in the tool body would already be inside a
// transaction and condition 4 could not be true however carefully the tool then
// cancelled it.
//
//   node Scripts/precondition-acceptance.mjs
//
// Requires a live editor. It moves one probe actor and puts it back, so it
// dirties the level package and does not save. Run npm run build first.

import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
if (!existsSync(join(repoRoot, "mcp-server", "dist", "index.js"))) {
  throw new Error("mcp-server/dist is missing. Run npm run build first.");
}

const projectArg = process.argv.indexOf("--project");
if (projectArg >= 0) process.env.MCP_UNREAL_PROJECT_ROOT = process.argv[projectArg + 1];
const project = process.env.MCP_UNREAL_PROJECT_ROOT ?? "D:/Unreal Projects/BridgeInstallTest";
process.env.MCP_UNREAL_PROJECT_ROOT = project;

const dist = (p) => import(pathToFileURL(join(repoRoot, "mcp-server", "dist", p)).href);
const { PuerTSClient } = await dist("puerts-client.js");

const failures = [];
function assert(condition, label, detail = "") {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}${detail ? `\n        ${detail}` : ""}`);
  failures.push(label);
  return false;
}

const client = new PuerTSClient();
const call = (command, params, timeout = 60000) => client.call(command, params, timeout);

console.log(`precondition acceptance against ${project}\n`);

const PROBE = "MCPPreconditionProbe";
const HOME = { x: 700, y: -700, z: 300 };
const MOVED = { x: 700, y: -700, z: 500 };
const CUBE = "/Script/Engine.StaticMeshActor";

// A probe actor of this script's own, so the test never depends on what the
// level happens to contain and never moves something a human placed. No mesh
// is assigned: StaticMesh is not on AllowedWritableProperties, and the hash
// under test covers transforms and hierarchy rather than asset references, so
// an empty StaticMeshActor exercises everything this acceptance reads.
const setup = await call("scene_batch", {
  operations: [{ op: "upsert_actor", label: PROBE, class: CUBE, location: HOME }],
});
assert(setup.success === true, "the probe actor exists", JSON.stringify(setup.errors));
const probeName = setup.data?.applied?.[0]?.actor ?? setup.data?.changed?.[0]?.actor ?? PROBE;

try {
  // --- 1: plan_only returns the hash the precondition consumes --------------
  const plan = await call("scene_batch", {
    plan_only: true,
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: MOVED }],
  });
  const planned = plan.data?.pre_structure_hash;
  assert(plan.success === true, "a plan_only batch succeeds");
  assert(typeof planned === "string" && /^[0-9a-f]{40}$/.test(planned),
    "1. plan_only returns pre_structure_hash as 40 lowercase hex", `got ${JSON.stringify(planned)}`);

  const inspected = await call("scene_inspect", {});
  assert(inspected.data?.structure_hash_sha1 === planned,
    "1. the plan's hash is the same value the independent inspector reports",
    `inspect ${inspected.data?.structure_hash_sha1}, plan ${planned}`);

  // --- 2: an unchanged scene accepts the apply ------------------------------
  // Read the hash again immediately before the apply. This run has occasionally
  // failed here with the scene having moved between the plan and the apply,
  // twice in roughly six runs, and neither a settling-editor nor an async
  // navigation rebuild explained it (both were measured and neither drifts).
  // Capturing the immediately-prior hash makes the next occurrence say whether
  // the scene really changed - and if so, what the diff is against the plan -
  // rather than only that the apply was refused.
  const justBefore = (await call("scene_inspect", {})).data?.structure_hash_sha1;
  const accepted = await call("scene_batch", {
    preconditions: { scene_structure_hash: planned },
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: MOVED }],
  });
  assert(accepted.success === true,
    "2. an unchanged scene accepts the apply",
    `errors=${JSON.stringify(accepted.errors)} planned=${planned} `
    + `hash_immediately_before_apply=${justBefore} `
    + `${justBefore === planned ? "(UNCHANGED: the refusal is not a real conflict)"
      : "(CHANGED: something mutated the level between plan and apply)"} `
    + `conflict_data=${JSON.stringify(accepted.data ?? {})}`);
  assert(accepted.data?.post_structure_hash !== planned,
    "2. and the apply actually moved the scene off the planned hash");

  // The prefixed spelling has to work too, or the documented request shape is
  // a shape the bridge refuses.
  const prefixed = await call("scene_batch", {
    preconditions: { scene_structure_hash: `sha1:${accepted.data?.post_structure_hash}` },
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: MOVED }],
  });
  assert(prefixed.success === true,
    "2. a sha1:-prefixed hash is accepted", JSON.stringify(prefixed.errors));
  assert(prefixed.data?.converged === true,
    "2. and re-applying the same desired state still converges to a no-op");

  // --- 3, 4, 5, 9, 10: a changed scene conflicts ----------------------------
  // `planned` is now stale: the applies above moved the probe.
  const beforeConflict = await call("scene_inspect", {});
  const conflict = await call("scene_batch", {
    preconditions: { scene_structure_hash: planned },
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: HOME }],
  });

  assert(conflict.success === false, "3. a changed scene refuses the apply");
  assert(conflict.error_code === "state_conflict",
    "3. the refusal carries error_code state_conflict", `got ${JSON.stringify(conflict.error_code)}`);
  assert(conflict.error_code !== undefined,
    "10. error_code survived the C++ response normalizer");
  assert(conflict.data?.precondition === "scene_structure_hash",
    "9. data names which precondition failed");
  assert(conflict.data?.expected === planned,
    "9. data carries the expected hash", `got ${JSON.stringify(conflict.data?.expected)}`);
  assert(conflict.data?.observed === beforeConflict.data?.structure_hash_sha1,
    "9. data carries the observed hash, and it matches an independent read",
    `observed ${conflict.data?.observed}, inspect ${beforeConflict.data?.structure_hash_sha1}`);
  assert(conflict.data?.expected !== conflict.data?.observed,
    "9. expected and observed actually differ");

  assert(conflict.transaction_id === "",
    "4. the refusal opened no transaction", `got ${JSON.stringify(conflict.transaction_id)}`);
  assert(conflict.data?.transaction_opened === false,
    "4. and says so explicitly");
  assert((conflict.changed_assets ?? []).length === 0 && (conflict.changed_actors ?? []).length === 0,
    "5. the refusal reports no changed asset or actor");

  const afterConflict = await call("scene_inspect", {});
  assert(afterConflict.data?.structure_hash_sha1 === beforeConflict.data?.structure_hash_sha1,
    "5. the level hashes the same before and after the refusal",
    `${beforeConflict.data?.structure_hash_sha1} -> ${afterConflict.data?.structure_hash_sha1}`);
  const probeAfter = afterConflict.data?.actors?.find((a) => a.label === PROBE);
  assert(Math.round(probeAfter?.transform?.location?.z ?? -1) === MOVED.z,
    "5. the probe actor did not move; the refused batch would have returned it home",
    `z=${probeAfter?.transform?.location?.z}`);

  // --- 6: the editor is unchanged ------------------------------------------
  assert(afterConflict.session?.session_id === beforeConflict.session?.session_id
    && afterConflict.session?.editor_pid === beforeConflict.session?.editor_pid,
    "6. the same editor process served both sides of the refusal");

  // --- 7: no preconditions keeps the existing behavior ----------------------
  const compat = await call("scene_batch", {
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: HOME }],
  });
  assert(compat.success === true,
    "7. a batch with no preconditions applies against the level as it is now",
    JSON.stringify(compat.errors));

  // plan_only ignores the precondition, which is what makes re-planning after a
  // conflict a single call rather than a call plus a field to remember to drop.
  const replan = await call("scene_batch", {
    plan_only: true,
    preconditions: { scene_structure_hash: planned },
    operations: [{ op: "upsert_actor", select: { label: PROBE }, location: MOVED }],
  });
  assert(replan.success === true,
    "plan_only ignores a stale precondition, so re-planning after a conflict is one call",
    JSON.stringify(replan.errors));
} finally {
  const cleanup = await call("scene_batch", {
    operations: [{ op: "delete_actor", select: { label: PROBE } }],
  });
  console.log(`\n  cleanup: probe ${cleanup.success === true ? "removed" : "NOT REMOVED"}`
    + ` (${probeName}); the level is left dirty and unsaved on purpose`);
}

console.log(`\n${failures.length === 0 ? "OK" : "FAIL"}: precondition acceptance, ${failures.length} failure(s)`);
if (failures.length > 0) {
  for (const failure of failures) console.log(`  - ${failure}`);
  process.exit(1);
}
