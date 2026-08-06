import { createHash } from "node:crypto";
import type { BlueprintGraph, GraphNode, LinterFinding, PlannerPolicy, PlannerWarning, PositionedGraph } from "./types.js";

function snap16(val: number): number {
  return Math.round(val / 16) * 16;
}

function stableNodeOrder(graph: BlueprintGraph): GraphNode[] {
  const byId = new Map(graph.nodes.map((node) => [node.id, node]));
  const indegree = new Map(graph.nodes.map((node) => [node.id, 0]));
  const outgoing = new Map(graph.nodes.map((node) => [node.id, [] as string[]]));
  for (const edge of graph.connections) {
    if (!byId.has(edge.from) || !byId.has(edge.to)) continue;
    outgoing.get(edge.from)?.push(edge.to);
    indegree.set(edge.to, (indegree.get(edge.to) ?? 0) + 1);
  }
  const ready = [...indegree].filter(([, degree]) => degree === 0).map(([id]) => id).sort();
  const ordered: GraphNode[] = [];
  while (ready.length > 0) {
    const id = ready.shift() as string;
    ordered.push(byId.get(id) as GraphNode);
    for (const next of (outgoing.get(id) ?? []).sort()) {
      const degree = (indegree.get(next) ?? 1) - 1;
      indegree.set(next, degree);
      if (degree === 0) {
        ready.push(next);
        ready.sort();
      }
    }
  }
  const seen = new Set(ordered.map((node) => node.id));
  return [...ordered, ...graph.nodes.filter((node) => !seen.has(node.id)).sort((a, b) => a.id.localeCompare(b.id))];
}

export function planGraphLayout(graph: BlueprintGraph, policy: PlannerPolicy): PositionedGraph {
  const ordered = stableNodeOrder(graph);
  const column = new Map<string, number>();
  for (const node of ordered) {
    const incoming = graph.connections.filter((edge) => edge.to === node.id);
    column.set(node.id, incoming.length === 0 ? 0 : Math.max(...incoming.map((edge) => (column.get(edge.from) ?? -1) + 1)));
  }
  const laneCounts = new Map<string, number>();
  const nodes = ordered.map((node) => {
    const lane = node.branch_lane ?? 0;
    const laneKey = `${column.get(node.id) ?? 0}:${lane}`;
    const offset = laneCounts.get(laneKey) ?? 0;
    laneCounts.set(laneKey, offset + 1);
    const rawX = (column.get(node.id) ?? 0) * policy.horizontal_gap;
    const rawY = lane * policy.vertical_gap + offset * Math.round(policy.vertical_gap * 0.55);
    return {
      ...node,
      x: snap16(rawX),
      y: snap16(rawY),
    };
  });

  const comments = [...new Set(nodes.map((node) => node.region).filter((value): value is string => Boolean(value)))].sort().map((region) => {
    const members = nodes.filter((node) => node.region === region);
    const minX = Math.min(...members.map((node) => node.x));
    const maxX = Math.max(...members.map((node) => node.x));
    const minY = Math.min(...members.map((node) => node.y));
    const maxY = Math.max(...members.map((node) => node.y));
    return { region, x: snap16(minX - 80), y: snap16(minY - 100), width: snap16(maxX - minX + 360), height: snap16(maxY - minY + 260) };
  });

  const positioned = new Map(nodes.map((node) => [node.id, node]));
  const reroutes = graph.connections.flatMap((edge, connection_index) => {
    const from = positioned.get(edge.from);
    const to = positioned.get(edge.to);
    if (!from || !to || Math.abs(to.x - from.x) <= policy.horizontal_gap * 2 || from.y === to.y) return [];
    return [{ id: `reroute_${connection_index}`, x: snap16(Math.round((from.x + to.x) / 2)), y: snap16(from.y), connection_index }];
  });

  // Calculate overlaps & crossings
  const posKeys = new Set<string>();
  let overlapCount = 0;
  for (const n of nodes) {
    const key = `${n.x},${n.y}`;
    if (posKeys.has(key)) overlapCount++;
    posKeys.add(key);
  }

  let wireCrossings = 0;
  for (let i = 0; i < graph.connections.length; i++) {
    for (let j = i + 1; j < graph.connections.length; j++) {
      const e1 = graph.connections[i];
      const e2 = graph.connections[j];
      const n1a = positioned.get(e1.from);
      const n1b = positioned.get(e1.to);
      const n2a = positioned.get(e2.from);
      const n2b = positioned.get(e2.to);
      if (n1a && n1b && n2a && n2b) {
        if ((n1a.y < n2a.y && n1b.y > n2b.y) || (n1a.y > n2a.y && n1b.y < n2b.y)) {
          wireCrossings++;
        }
      }
    }
  }

  const layoutPayload = JSON.stringify({
    nodes: nodes.map((n) => ({ id: n.id, x: n.x, y: n.y })),
    comments,
    reroutes: reroutes.map((r) => ({ id: r.id, x: r.x, y: r.y })),
  });
  const layout_hash_sha1 = createHash("sha1").update(layoutPayload).digest("hex");

  return { ...graph, nodes, comments, reroutes, layout_hash_sha1, overlap_count: overlapCount, wire_crossings: wireCrossings };
}

export function graphComplexityWarnings(assetPath: string, graph: BlueprintGraph, policy: PlannerPolicy): PlannerWarning[] {
  const warnings: PlannerWarning[] = [];
  const functional = graph.nodes.filter((node) => node.functional !== false && node.type !== "Comment" && node.type !== "Knot");
  if (functional.length > policy.max_functional_nodes) {
    warnings.push({
      code: "FUNCTIONAL_NODE_LIMIT",
      message: `${functional.length} functional nodes exceed ${policy.max_functional_nodes}; extract a function or C++ primitive.`,
      asset_path: assetPath,
      graph: graph.name,
      severity: "warning",
      node_ids: functional.map((n) => n.id),
      recommended_action: "Extract a Blueprint function or native C++ method to reduce node complexity.",
    });
  }
  const depth = Math.max(0, ...graph.nodes.map((node) => node.branch_depth ?? 0));
  if (depth > policy.max_branch_depth) {
    warnings.push({
      code: "BRANCH_DEPTH_LIMIT",
      message: `Branch depth ${depth} exceeds ${policy.max_branch_depth}; extract the nested flow.`,
      asset_path: assetPath,
      graph: graph.name,
      severity: "warning",
      recommended_action: "Refactor deeply nested conditional branches into dedicated helper functions.",
    });
  }
  const repeats = new Map<string, number>();
  for (const node of graph.nodes) if (node.repeat_key) repeats.set(node.repeat_key, (repeats.get(node.repeat_key) ?? 0) + 1);
  for (const [key, count] of [...repeats].sort()) {
    if (count >= 3) {
      warnings.push({
        code: "REPEATED_PATTERN",
        message: `Pattern ${key} repeats ${count} times; extract it once.`,
        asset_path: assetPath,
        graph: graph.name,
        severity: "info",
        recommended_action: "Collapse duplicate node sequences into a reusable function or macro.",
      });
    }
  }
  for (const node of graph.nodes) {
    if ((node.loop_expected_iterations ?? 0) > policy.large_loop_iterations) {
      warnings.push({
        code: "LARGE_LOOP",
        message: `${node.id} may execute ${node.loop_expected_iterations} iterations; prefer tested C++.`,
        asset_path: assetPath,
        graph: graph.name,
        severity: "warning",
        node_ids: [node.id],
        recommended_action: "Move large loops into native C++ functions to ensure runtime performance.",
      });
    }
  }
  return warnings;
}

export function lintGraph(assetPath: string, graph: PositionedGraph, policy: PlannerPolicy): LinterFinding[] {
  const findings: LinterFinding[] = [];

  // Check functional node limit
  const functional = graph.nodes.filter((node) => node.functional !== false && node.type !== "Comment" && node.type !== "Knot");
  if (functional.length > policy.max_functional_nodes) {
    findings.push({
      code: "FUNCTIONAL_NODE_LIMIT",
      severity: "warning",
      message: `${functional.length} functional nodes exceed threshold ${policy.max_functional_nodes}.`,
      asset_path: assetPath,
      graph: graph.name,
      node_ids: functional.map((n) => n.id),
      recommended_action: "Extract a function, macro, or C++ native method.",
    });
  }

  // Check branch depth
  const depth = Math.max(0, ...graph.nodes.map((node) => node.branch_depth ?? 0));
  if (depth > policy.max_branch_depth) {
    findings.push({
      code: "BRANCH_DEPTH_LIMIT",
      severity: "warning",
      message: `Max branch depth ${depth} exceeds threshold ${policy.max_branch_depth}.`,
      asset_path: assetPath,
      graph: graph.name,
      recommended_action: "Flatten nested conditional branches into guard clauses or state machines.",
    });
  }

  // Check orphan nodes (nodes with 0 incoming and 0 outgoing connections)
  const connectedNodes = new Set<string>();
  for (const edge of graph.connections) {
    connectedNodes.add(edge.from);
    connectedNodes.add(edge.to);
  }
  const orphans = graph.nodes.filter((node) => node.functional !== false && !["BeginPlay", "Tick", "Event", "CustomEvent", "InputKey", "InputAction", "InputAxisEvent"].includes(node.type) && !connectedNodes.has(node.id));
  if (orphans.length > 0) {
    findings.push({
      code: "ORPHAN_NODE",
      severity: "warning",
      message: `Found ${orphans.length} unconnected functional node(s).`,
      asset_path: assetPath,
      graph: graph.name,
      node_ids: orphans.map((n) => n.id),
      recommended_action: "Wire execution and data pins or remove unreferenced nodes.",
    });
  }

  // Check complex event graph
  if (graph.kind === "event" && functional.length > 20) {
    findings.push({
      code: "COMPLEX_EVENT_GRAPH",
      severity: "warning",
      message: `EventGraph contains ${functional.length} nodes; event graphs should only orchestrate calls.`,
      asset_path: assetPath,
      graph: graph.name,
      recommended_action: "Move algorithm implementation from EventGraph into member functions or native C++.",
    });
  }

  // Check wire crossings
  if ((graph.wire_crossings ?? 0) > 0) {
    findings.push({
      code: "WIRE_CROSSING",
      severity: "info",
      message: `${graph.wire_crossings} execution wire crossing(s) detected.`,
      asset_path: assetPath,
      graph: graph.name,
      recommended_action: "Adjust branch lanes or insert reroute nodes to clean wire paths.",
    });
  }

  return findings;
}
