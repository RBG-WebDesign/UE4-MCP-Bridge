// Shared acceptance body for the simple Blueprint state-machine features.
// Each caller owns its concrete graph. This file only keeps the evidence
// protocol identical across the six recipes.

export async function exerciseBlueprintState(h, options) {
  const {
    assetPath, actorClass, actorLabel, controlSpec, finalSpec,
    expectedVariables, marker, includePie, runtimeProperty, runtimeValue,
  } = options;

  if (!await h.assertFreshFixture(assetPath, "puerts_graph_inspect")) return;

  const controlBuild = await h.call("puerts_blueprint_build", controlSpec, {
    label: "1. build the minimal state-moving control",
  });
  if (controlBuild?.success !== true) return;
  const controlRead = await h.call("puerts_graph_inspect", {
    asset_path: assetPath, include_pins: true,
  }, { label: "1. inspect the control independently" });
  const controlHash = controlRead?.data?.graph?.structure_hash_sha1 ?? null;

  const built = await h.call("puerts_blueprint_build", finalSpec, {
    label: "2. replace the control with the complete feature",
  });
  if (built?.success !== true) return;
  const read = await h.call("puerts_graph_inspect", {
    asset_path: assetPath, include_pins: true,
  }, { label: "3. inspect the complete feature independently" });
  const hash = read?.data?.graph?.structure_hash_sha1 ?? null;
  const variables = (read?.data?.variables ?? []).map((value) => value.name);
  const graph = JSON.stringify(read?.data?.graph?.nodes ?? []);
  h.check(built?.data?.compile_status === "UpToDate", "2. the feature Blueprint compiles", String(built?.data?.compile_status));
  h.check(typeof hash === "string" && hash !== controlHash,
    "3. the control moved to the requested feature state", `${controlHash} -> ${hash}`);
  h.check(expectedVariables.every((name) => variables.includes(name)),
    "3. every requested state variable reads back", JSON.stringify(variables));
  h.check(graph.includes(marker), "3. the independent graph read contains the feature marker");

  const sha = h.fileSha(assetPath);
  const rerun = await h.call("puerts_blueprint_build", finalSpec, {
    label: "4. rerun the identical desired state",
  });
  if (rerun?.success !== true) return;
  const again = await h.call("puerts_graph_inspect", { asset_path: assetPath }, {
    label: "4. inspect the converged rerun",
  });
  h.check(again?.data?.graph?.structure_hash_sha1 === hash, "4. the graph hash converged");
  h.check(sha === null || h.fileSha(assetPath) === sha, "4. the converged rerun wrote no new bytes");

  if (!includePie) {
    h.policy("5. prove runtime state movement",
      "PIE is off by default. Re-run with --pie only after explicit user authorization.");
  } else {
    const placed = await h.call("puerts_scene_batch", {
      operations: [{ op: "upsert_actor", label: actorLabel, class: actorClass, location: { x: 0, y: 0, z: 150 } }],
    }, { label: "5. place the fresh feature fixture" });
    let started = false;
    try {
      if (placed?.success === true) {
        started = (await h.call("puerts_pie_start", {}, {
          label: "5. start explicitly authorized PIE",
        }))?.success === true;
      }
      if (started) {
        await new Promise((resolve) => setTimeout(resolve, 350));
        const state = await h.call("puerts_pie_agent_query", {
          op: "read_property", actor: actorLabel, property: runtimeProperty,
        }, { label: "5. read live feature state independently" });
        h.check(state?.data?.value === runtimeValue,
          `5. runtime moved ${runtimeProperty} to its expected value`, String(state?.data?.value));
      }
    } finally {
      if (started) await h.call("puerts_pie_stop", {}, { label: "5. stop the explicitly authorized PIE session" });
    }
  }

  h.seal({ asset_path: assetPath, inspect_tool: "puerts_graph_inspect", structure_hash: hash });
}
