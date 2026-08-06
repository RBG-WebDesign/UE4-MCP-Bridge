#!/usr/bin/env node
// Issue A diagnostic: catch structure_hash_sha1 changing for an unchanged level
// and say exactly which field moved.
//
// This does not need a C++ change to be useful. puerts_scene_inspect already
// reports every field the hash consumes - object name, class path, label,
// folder, tags, world transform, attach parent and socket, and per component
// the name, class path, attach parent and relative transform - so two payloads
// taken either side of a hash change locate the difference. If the hash moves
// while every hash-relevant field is identical, that is itself the finding: the
// difference is then in the canonical encoder or in which objects were
// enumerated, not in the scene.
//
//   node Scripts/hash-determinism-probe.mjs                 (all phases)
//   node Scripts/hash-determinism-probe.mjs --reads 20      (per-phase samples)
//
// Writes hash-input-before.json / -after.json / -diff.json into
// Saved/MCPPuerTSBridge/hash-probe/ on the first mismatch of each phase.

import { mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const projectArg = process.argv.indexOf("--project");
if (projectArg >= 0) process.env.MCP_UNREAL_PROJECT_ROOT = process.argv[projectArg + 1];
process.env.MCP_UNREAL_PROJECT_ROOT ??= "D:/Unreal Projects/BridgeInstallTest";
const readsArg = process.argv.indexOf("--reads");
const READS = readsArg >= 0 ? Number(process.argv[readsArg + 1]) : 10;

const { PuerTSClient } = await import(
  pathToFileURL(join(repoRoot, "mcp-server", "dist", "puerts-client.js")).href);
const client = new PuerTSClient();
const call = (c, p, t = 60000) => client.call(c, p, t);
const outDir = join(process.env.MCP_UNREAL_PROJECT_ROOT, "Saved", "MCPPuerTSBridge", "hash-probe");
mkdirSync(outDir, { recursive: true });

/** Only the fields the hash actually consumes, in a stable shape. Bounds and
    reflected properties are excluded because StructureHash excludes them. */
function hashRelevant(actor) {
  return {
    id: actor.id,
    class_path: actor.class_path,
    label: actor.label,
    folder: actor.folder,
    tags: [...(actor.tags ?? [])],
    transform: actor.transform,
    attach_parent: actor.attach_parent,
    attach_socket: actor.attach_socket,
    components: (actor.components ?? []).map((c) => ({
      name: c.name,
      class_path: c.class_path,
      attach_parent: c.attach_parent,
      relative_location: c.relative_location,
      relative_rotation: c.relative_rotation,
      relative_scale: c.relative_scale,
    })),
  };
}

async function sample() {
  const r = await call("scene_inspect", {});
  // Check success explicitly. An earlier version of this probe did not, so a
  // refused read produced an undefined hash that the caller counted as a second
  // distinct value - reporting an unstable hash when what actually happened was
  // a failed call. A read that did not succeed is not a hash sample.
  if (r.success !== true) {
    const detail = JSON.stringify({ errors: r.errors, error_code: r.error_code, message: r.message });
    throw new Error(`scene_inspect did not succeed, so this is a call failure and not a hash sample: ${detail}`);
  }
  const hash = r.data?.structure_hash_sha1;
  if (typeof hash !== "string" || !/^[0-9a-f]{40}$/.test(hash)) {
    throw new Error(`scene_inspect succeeded but returned no usable hash: ${JSON.stringify(hash)}`);
  }
  return {
    hash,
    actorCount: r.data?.actor_count,
    actors: (r.data?.actors ?? []).map(hashRelevant),
  };
}

/** First differing record and every changed field, per the required artifacts. */
function diff(before, after) {
  const byId = (s) => new Map(s.actors.map((a) => [a.id, a]));
  const [b, a] = [byId(before), byId(after)];
  const changes = [];
  for (const id of new Set([...b.keys(), ...a.keys()])) {
    if (!b.has(id)) { changes.push({ id, kind: "actor_added" }); continue; }
    if (!a.has(id)) { changes.push({ id, kind: "actor_removed" }); continue; }
    const [x, y] = [b.get(id), a.get(id)];
    const fields = [];
    for (const key of Object.keys(x)) {
      const [sx, sy] = [JSON.stringify(x[key]), JSON.stringify(y[key])];
      if (sx !== sy) fields.push({ field: key, before: x[key], after: y[key] });
    }
    if (fields.length > 0) changes.push({ id, kind: "actor_changed", fields });
  }
  return {
    actor_count_before: before.actorCount,
    actor_count_after: after.actorCount,
    hash_before: before.hash,
    hash_after: after.hash,
    identical_hash_relevant_state: changes.length === 0,
    first_changed_record: changes[0] ?? null,
    changes,
  };
}

const results = [];
async function phase(name, mutate) {
  process.stdout.write(`\n${name}\n`);
  if (mutate) await mutate();
  let baseline = await sample();
  const seen = new Map([[baseline.hash, 1]]);
  let firstMismatch = null;
  const callFailures = [];
  for (let i = 1; i < READS; i++) {
    let next;
    try { next = await sample(); }
    catch (error) {
      // Recorded and reported separately. A failed read is its own defect and
      // must never be laundered into "the hash is unstable".
      callFailures.push({ read: i, error: error.message });
      continue;
    }
    seen.set(next.hash, (seen.get(next.hash) ?? 0) + 1);
    if (next.hash !== baseline.hash && firstMismatch === null) {
      firstMismatch = diff(baseline, next);
      const stem = name.replace(/[^a-z0-9]+/gi, "-").toLowerCase();
      writeFileSync(join(outDir, `hash-input-before.${stem}.json`), JSON.stringify(baseline, null, 1));
      writeFileSync(join(outDir, `hash-input-after.${stem}.json`), JSON.stringify(next, null, 1));
      writeFileSync(join(outDir, `hash-input-diff.${stem}.json`), JSON.stringify(firstMismatch, null, 1));
      baseline = next;
    }
  }
  const distinct = seen.size;
  console.log(`  reads=${READS} ok=${READS - callFailures.length} failed_calls=${callFailures.length}`
    + ` distinct_hashes=${distinct}` + (distinct === 1 ? "  STABLE" : "  UNSTABLE"));
  for (const f of callFailures) console.log(`  CALL FAILURE read ${f.read}: ${f.error}`);
  if (firstMismatch) {
    console.log(`  hash-relevant state identical across the change: ${firstMismatch.identical_hash_relevant_state}`);
    console.log(`  first changed record: ${JSON.stringify(firstMismatch.first_changed_record)?.slice(0, 400)}`);
    console.log(`  artifacts in ${outDir}`);
  }
  results.push({ phase: name, distinct, firstMismatch });
}

const PROBE = "MCPHashProbe";
const CUBE = "/Script/Engine.StaticMeshActor";
const upsert = (props) => call("scene_batch", { operations: [{ op: "upsert_actor", label: PROBE, class: CUBE, ...props }] });

try {
  await phase("1. repeated inspection, no mutation", null);
  await phase("2. after actor spawn", () => upsert({ location: { x: 700, y: -700, z: 300 } }));
  await phase("3. after actor move", () => upsert({ select: { label: PROBE }, location: { x: 700, y: -700, z: 500 } }));
  await phase("4. after actor rotation", () => upsert({ select: { label: PROBE }, rotation: { pitch: 0, yaw: 45, roll: 0 } }));
  await phase("5. after a rotation that lands near zero", () => upsert({ select: { label: PROBE }, rotation: { pitch: 0, yaw: 359.9999, roll: 0 } }));
  await phase("6. after a move to near-zero coordinates", () => upsert({ select: { label: PROBE }, location: { x: 0.0004, y: -0.0004, z: 0.0006 } }));
  await phase("7. after tag change", () => upsert({ select: { label: PROBE }, tags: ["b", "a", "c"] }));
  await phase("8. after actor delete", () => call("scene_batch", { operations: [{ op: "delete_actor", select: { label: PROBE } }] }));
} finally {
  await call("scene_batch", { operations: [{ op: "delete_actor", select: { label: PROBE } }] }).catch(() => {});
}

const unstable = results.filter((r) => r.distinct > 1);
console.log(`\n${unstable.length === 0 ? "ALL STABLE" : `UNSTABLE in ${unstable.length} phase(s): ${unstable.map((u) => u.phase).join("; ")}`}`);
