#!/usr/bin/env node
// Finding 0r: a failed member batch does not restore variable DEFAULTS.
//
// The question this answers is narrow and it decides where the fix goes:
// when a batch cancels its transaction, does the variable default written to
// the generated class CDO come back?
//
//   CDO still holds the new value  -> the write is not covered by the
//                                     transaction (or a compile baked it in),
//                                     and the fix belongs at the WRITER.
//   CDO holds the original value   -> the rollback works and the member hash
//                                     moved for some other reason, and the fix
//                                     belongs wherever that other reason is.
//
// The CDO is read with puerts_read_property, a different tool and a different
// native entry point, so it shares no comparator with the writer or with the
// member inspector. That is the same independence finding 0p needed, and it is
// the reason the answer here can be trusted at all.
//
// Fresh fixture per run. A fixed path is what makes mutator-atomicity's control
// report a false red on its second run (finding 0n, 0r).
//
//   node Scripts/member-rollback-diagnosis.mjs

import { startBridge } from "./mutator-atomicity.mjs";
import { requireCurrentInstall } from "./bridge-install.mjs";
import { pathToFileURL } from "node:url";

const projectRoot = process.env.MCP_UNREAL_PROJECT_ROOT;
const ORIGINAL = 0.5;
const REQUESTED = 0.75;

const rows = [];
function row(label, value) { rows.push({ label, value }); console.log(`  ${label.padEnd(46)} ${value}`); }

async function main() {
  if (!projectRoot) throw new Error("Set MCP_UNREAL_PROJECT_ROOT");
  requireCurrentInstall(projectRoot);

  const bridge = startBridge("member-rollback-diagnosis");
  await bridge.ready();
  const call = bridge.call;
  try {
    const runId = Date.now().toString(36);
    const BP = `/Game/MCPGenerated/BP_RollbackProbe_${runId}`;
    const CDO = `${BP}.Default__BP_RollbackProbe_${runId}_C`;

    // The path must not already exist, asserted rather than assumed.
    const pre = await call("puerts_graph_inspect", { asset_path: BP });
    if (pre.success === true) throw new Error(`${BP} already exists; the run is not clean`);

    const built = await call("puerts_blueprint_build", {
      asset_path: BP,
      parent_class: "Actor",
      components: [{ class: "SceneComponent", name: "Root" }],
      variables: [{ name: "Ratio", type: "float", default: ORIGINAL }],
      compile: true, save: true,
    });
    if (built.success !== true) throw new Error(`fixture build failed: ${JSON.stringify(built.errors)}`);

    // A failed read must ABORT, not be compared. The first version of this
    // script passed the wrong parameter name, so both reads returned the same
    // error string, the comparison said "unchanged", and the verdict was
    // confidently wrong in the direction that exonerates the writer. A probe
    // that cannot read the thing it is measuring has no verdict to give.
    const readCdo = async () => {
      const r = await call("puerts_read_property", { object_path: CDO, property: "Ratio" });
      if (r.success !== true) {
        throw new Error(`CDO read failed, so this probe has no verdict: ${JSON.stringify(r.errors ?? r.message)}`);
      }
      return r.data?.value;
    };
    const readHash = async () => {
      const r = await call("puerts_graph_inspect", { asset_path: BP });
      return r.data?.member_structure_hash_sha1 ?? "(none)";
    };

    console.log("\n== before the failing batch ==");
    const cdoBefore = await readCdo();
    const hashBefore = await readHash();
    row("CDO Ratio", cdoBefore);
    row("member hash", hashBefore);

    // Operation 0 is VALID and will apply. Operation 1 cannot, so the batch
    // fails after operation 0 has already written and compiled. That is the
    // exact shape lever (3) of mutator-atomicity uses.
    console.log("\n== failing batch: set_variable_default 0.75, then a bad add_variable ==");
    const patch = await call("puerts_blueprint_member_patch", {
      asset_path: BP,
      operations: [
        { op: "set_variable_default", name: "Ratio", default: REQUESTED },
        { op: "add_variable", name: "BadFloat", type: { category: "float" }, default: "not-a-number" },
      ],
      compile: true, save: true, verify: true,
    });
    row("batch success (must be false)", String(patch.success));
    row("batch says rollback restored", String(patch.data?.rollback_succeeded ?? "(not reported)"));

    console.log("\n== after the failing batch, read independently ==");
    const cdoAfter = await readCdo();
    const hashAfter = await readHash();
    row("CDO Ratio", cdoAfter);
    row("member hash", hashAfter);

    const cdoMoved = String(cdoAfter) !== String(cdoBefore);
    const hashMoved = hashAfter !== hashBefore;

    console.log("\n== verdict ==");
    if (cdoMoved) {
      console.log(`  THE WRITER. The CDO still holds ${cdoAfter} after a cancelled transaction;`);
      console.log(`  it was ${cdoBefore} before. The default write is not covered by the`);
      console.log("  rollback, so the fix belongs at SetVariableDefault, not at the boundary.");
    } else if (hashMoved) {
      console.log("  NOT THE DEFAULT. The CDO was restored, and the member hash still moved,");
      console.log("  so something else in the member snapshot is not being rolled back.");
    } else {
      console.log("  NEITHER MOVED. The rollback restored both. If mutator-atomicity is still");
      console.log("  red, its fixture is dirty rather than the product being wrong.");
    }

    console.log(`\n  cdo_moved=${cdoMoved} hash_moved=${hashMoved} fixture=${BP}`);
  } finally {
    bridge.close();
  }
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href.toLowerCase() === import.meta.url.toLowerCase()) {
  await main();
}
