import { createHash } from "node:crypto";
import { diffDesiredState, planFeature as planBase } from "./planner.js";
import type { FeaturePlan, FeatureRequest } from "./types.js";

const GAP_REASONS: Record<string, string> = {
  interface_asset_creation: "Blueprint Interface asset creation is not on the canonical production spine.",
  interface_message: "Interface message nodes are not in the canonical node vocabulary.",
  cpp_generation: "This planner defines the C++ handoff, but source generation remains external.",
  user_defined_types: "User-defined struct and enum asset creation is outside this vertical.",
  loop_nodes: "Loop nodes and wildcard container pin proof are not canonical capabilities.",
  parent_change: "Changing the parent of an existing Blueprint has no canonical operation.",
  rollback_fixture_injection: "The live harness lacks a canonical atomic failure injection operation.",
};

function canonicalGraphOperations(params: Record<string, unknown>): Record<string, unknown> {
  const operations = params.operations as Array<Record<string, unknown>>;
  if (operations.length !== 1 || operations[0].op !== "replace_from_desired_state") return params;
  const graph = operations[0].graph as { nodes: Array<Record<string, unknown>>; connections: Array<Record<string, string>> };
  return {
    ...params,
    operations: [
      ...graph.nodes.map((node) => ({ op: "add_node", new_id: node.id, type: node.type, params: node.params, x: node.x, y: node.y })),
      ...graph.connections.map((edge) => ({ op: "connect_pins", source: { new_id: edge.from }, source_pin: edge.from_pin, target: { new_id: edge.to }, target_pin: edge.to_pin })),
    ],
  };
}

export function planFeature(request: FeatureRequest): FeaturePlan {
  const plan = planBase(request);
  plan.command_plan = plan.command_plan.map((entry) => entry.command === "puerts_blueprint_graph_patch" ? { ...entry, params: canonicalGraphOperations(entry.params) } : entry);
  for (const capability of [...(request.required_capabilities ?? [])].sort()) {
    if (plan.unsupported.some((item) => item.capability === capability)) continue;
    plan.unsupported.push({ capability, reason: GAP_REASONS[capability] ?? "The requested capability is not proven on the canonical production spine." });
  }
  plan.unsupported.sort((a, b) => `${a.capability}:${a.asset_path ?? ""}`.localeCompare(`${b.capability}:${b.asset_path ?? ""}`));
  plan.executable = plan.unsupported.length === 0;
  plan.plan_hash_sha1 = createHash("sha1").update(JSON.stringify({ ...plan, plan_hash_sha1: "" })).digest("hex");
  return plan;
}

export { diffDesiredState };
