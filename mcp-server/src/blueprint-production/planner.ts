import { createHash } from "node:crypto";
import { graphComplexityWarnings, lintGraph, planGraphLayout } from "./layout.js";
import type {
  ArchitectureDecision, BlueprintDesiredState, BlueprintGraph, CommandPlanEntry, FeaturePlan, FeatureRequest,
  LinterFinding, ObservedBlueprint, PlannerPolicy, RepairOperation, UnsupportedCapability,
} from "./types.js";

export const DEFAULT_POLICY: PlannerPolicy = {
  max_functional_nodes: 25,
  max_branch_depth: 3,
  horizontal_gap: 320,
  vertical_gap: 180,
  large_loop_iterations: 100,
};

export const SUPPORTED_NODE_TYPES = new Set([
  "BeginPlay", "Tick", "ActorBeginOverlap", "ActorEndOverlap", "PrintString", "CallFunction",
  "Operator", "Delay", "Branch", "Sequence", "Comment", "Event", "CustomEvent", "VariableGet",
  "VariableSet", "Cast", "Select", "Knot", "MakeStruct", "BreakStruct", "FormatText", "SpawnActor",
  "SwitchInt", "SwitchString", "MultiGate", "DoOnceMultiInput", "InputKey", "InputAction",
  "InputAxisEvent", "MacroInstance", "AddDelegate", "RemoveDelegate", "CallDelegate", "ClearDelegate", "AssignDelegate", "CreateDelegate",
]);

const CAPABILITY_GAPS: Record<string, string> = {
  parent_change: "Existing Blueprint reparenting has no canonical operation.",
  graph_lifecycle: "Arbitrary graph create, remove, and rename are not canonical capabilities.",
  interface_message: "Interface message nodes are not in the canonical node vocabulary.",
  loop_nodes: "Loop nodes and wildcard container pin proof are not canonical capabilities.",
  node_enabled: "Node enabled state has no canonical graph patch operation.",
  break_pin_all: "Break-all-links on one pin has no canonical graph patch operation.",
  cpp_generation: "The Blueprint production planner defines C++ handoff only; source generation is external.",
  package_reload: "Second-session cold reload and hash comparison remain a separate proof campaign.",
  independent_save_confirmation: "Save confirmation is not available until cold reload campaign.",
};

function stable(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(stable).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.entries(value as Record<string, unknown>).sort(([a], [b]) => a.localeCompare(b)).map(([key, item]) => `${JSON.stringify(key)}:${stable(item)}`).join(",")}}`;
  return JSON.stringify(value);
}

function validateRequest(request: FeatureRequest): void {
  if (request.schema_version !== 1) throw new Error("schema_version must be 1");
  if (!/^[a-z][a-z0-9_]*$/.test(request.feature_id)) throw new Error("feature_id must be lowercase snake_case");
  if (request.constraints.ue_version !== "4.27") throw new Error("Only UE4.27 is supported");
  if (request.constraints.authoring_root !== "/Game/MCPGenerated") throw new Error("authoring_root must be /Game/MCPGenerated");
  const paths = new Set<string>();
  for (const blueprint of request.blueprints) {
    if (!/^\/Game\/MCPGenerated\/[A-Za-z0-9_/]+$/.test(blueprint.asset_path)) throw new Error(`Unsafe asset path: ${blueprint.asset_path}`);
    if (paths.has(blueprint.asset_path)) throw new Error(`Duplicate asset path: ${blueprint.asset_path}`);
    paths.add(blueprint.asset_path);
    for (const graph of blueprint.graphs) {
      const ids = new Set<string>();
      for (const node of graph.nodes) {
        if (!/^[A-Za-z][A-Za-z0-9_.]*$/.test(node.id)) throw new Error(`Invalid node id ${node.id} in ${blueprint.asset_path}:${graph.name}`);
        if (ids.has(node.id)) throw new Error(`Duplicate node id ${node.id} in ${blueprint.asset_path}:${graph.name}`);
        ids.add(node.id);
      }
      for (const edge of graph.connections) {
        if (!ids.has(edge.from) || !ids.has(edge.to)) throw new Error(`Connection references a missing node in ${blueprint.asset_path}:${graph.name}`);
        if (typeof edge.from_pin !== "string" || typeof edge.to_pin !== "string") throw new Error(`Invalid pin references in connection ${edge.from} -> ${edge.to}`);
      }
    }
  }
}

function chooseArchitecture(request: FeatureRequest): ArchitectureDecision {
  const signals = request.architecture_signals;
  const cpp = Boolean(signals.reusable_runtime_system || signals.complex_algorithm || signals.performance_sensitive || signals.replicated_or_authoritative || signals.large_data_transform || signals.shared_base_class || signals.strict_runtime_tests);
  const blueprint = Boolean(signals.asset_composition || signals.designer_settings || signals.event_wiring || signals.visual_extension_points || request.blueprints.length > 0);

  const cpp_responsibilities: string[] = [];
  const blueprint_responsibilities: string[] = [];
  const hybrid_integration_points: string[] = [];

  if (signals.reusable_runtime_system) cpp_responsibilities.push("Reusable runtime system logic");
  if (signals.complex_algorithm) cpp_responsibilities.push("Complex algorithms & math calculations");
  if (signals.performance_sensitive) cpp_responsibilities.push("Performance-critical tick operations & physics trace handling");
  if (signals.replicated_or_authoritative) cpp_responsibilities.push("Network authority & state replication logic");

  if (signals.asset_composition) blueprint_responsibilities.push("Component composition & asset references");
  if (signals.designer_settings) blueprint_responsibilities.push("Designer tweaks & editable default properties");
  if (signals.event_wiring) blueprint_responsibilities.push("Input action wiring & visual orchestration graphs");
  if (signals.visual_extension_points) blueprint_responsibilities.push("Animation, audio, and visual effect triggers");

  if (request.cpp?.classes.length) {
    for (const c of request.cpp.classes) {
      hybrid_integration_points.push(`Native class ${c.name} exposed via UFUNCTION/UPROPERTY to Blueprint assets`);
    }
  }

  const build_order = [
    "Plan feature & IR",
    "Generate C++ source code",
    "Compile editor target once",
    "Create Blueprint child assets",
    "Wire Blueprint graphs & layout",
    "Verify compilation & save snapshot",
  ];

  if (cpp && blueprint) {
    return {
      selected: "hybrid",
      reasons: [
        "reusable, complex, authoritative, or testable runtime behavior belongs in C++",
        "assets, designer settings, and event wiring remain editable in Blueprint",
      ],
      cpp_responsibilities,
      blueprint_responsibilities,
      hybrid_integration_points,
      build_order,
    };
  }
  if (cpp) {
    return {
      selected: "cpp",
      reasons: ["the request is reusable or algorithmic runtime behavior with no Blueprint composition requirement"],
      cpp_responsibilities,
      blueprint_responsibilities: [],
      hybrid_integration_points: [],
      build_order,
    };
  }
  return {
    selected: "blueprint",
    reasons: ["the request is asset composition and small visual orchestration without a native runtime requirement"],
    cpp_responsibilities: [],
    blueprint_responsibilities,
    hybrid_integration_points: [],
    build_order,
  };
}

function dependencyOrder(blueprints: BlueprintDesiredState[]): string[] {
  const paths = new Set(blueprints.map((blueprint) => blueprint.asset_path));
  const dependencies = new Map(blueprints.map((blueprint) => [blueprint.asset_path, new Set((blueprint.depends_on ?? []).filter((item) => paths.has(item)))]));
  const ordered: string[] = [];
  while (dependencies.size > 0) {
    const ready = [...dependencies].filter(([, deps]) => deps.size === 0).map(([path]) => path).sort();
    if (ready.length === 0) throw new Error(`Blueprint dependency cycle: ${[...dependencies.keys()].sort().join(", ")}`);
    for (const path of ready) {
      ordered.push(path);
      dependencies.delete(path);
      for (const deps of dependencies.values()) deps.delete(path);
    }
  }
  return ordered;
}

function unsupportedFor(request: FeatureRequest): UnsupportedCapability[] {
  const gaps = new Map<string, UnsupportedCapability>();
  const add = (capability: string, asset_path?: string, graph?: string, reason?: string): void => {
    const key = `${capability}:${asset_path ?? ""}:${graph ?? ""}`;
    gaps.set(key, { capability, reason: reason ?? CAPABILITY_GAPS[capability] ?? "Capability is not supported by the canonical Blueprint production spine.", asset_path, graph });
  };
  add("package_reload", undefined, undefined, "Second-session cold reload and hash comparison remain a separate proof campaign.");
  add("independent_save_confirmation", undefined, undefined, "Save confirmation is not available until cold reload campaign.");
  for (const capability of request.required_capabilities ?? []) if (CAPABILITY_GAPS[capability]) add(capability);
  for (const blueprint of request.blueprints) {
    for (const graph of blueprint.graphs) for (const node of graph.nodes) if (!SUPPORTED_NODE_TYPES.has(node.type)) add(`node:${node.type}`, blueprint.asset_path, graph.name, `Node type ${node.type} is not in the canonical Blueprint vocabulary.`);
  }
  const architecture = chooseArchitecture(request).selected;
  if ((architecture === "cpp" || architecture === "hybrid") && (request.cpp?.classes.length ?? 0) > 0) add("cpp_generation");
  return [...gaps.values()].sort((a, b) => `${a.capability}:${a.asset_path ?? ""}:${a.graph ?? ""}`.localeCompare(`${b.capability}:${b.asset_path ?? ""}:${b.graph ?? ""}`));
}

function loweredGraph(graph: ReturnType<typeof planGraphLayout>): Record<string, unknown> {
  const nodes = graph.nodes.map((node) => ({ id: node.id, type: node.type, x: node.x, y: node.y, params: node.params ?? {} }));
  for (const comment of graph.comments) nodes.push({ id: `comment_${comment.region.replace(/[^A-Za-z0-9_]/g, "_")}`, type: "Comment", x: comment.x, y: comment.y, params: { text: comment.region, width: comment.width, height: comment.height } });
  for (const reroute of graph.reroutes) nodes.push({ id: reroute.id, type: "Knot", x: reroute.x, y: reroute.y, params: {} });
  const connections = graph.connections.flatMap((edge, index) => {
    const reroute = graph.reroutes.find((item) => item.connection_index === index);
    if (!reroute) return [{ from: `${edge.from}.${edge.from_pin}`, to: `${edge.to}.${edge.to_pin}` }];
    return [
      { from: `${edge.from}.${edge.from_pin}`, to: `${reroute.id}.InputPin` },
      { from: `${reroute.id}.OutputPin`, to: `${edge.to}.${edge.to_pin}` },
    ];
  });
  return { nodes, connections };
}

function lowerVariable(variable: BlueprintDesiredState["variables"][number]): Record<string, unknown> {
  const descriptor = variable.type;
  const category = String(descriptor.category ?? "");
  const reference = descriptor.subcategory_object;
  const type = ["object", "class", "struct", "enum"].includes(category) && typeof reference === "string"
    ? `${category}:${reference}`
    : category;
  const containerType = String(descriptor.container_type ?? "none").toLowerCase();
  return {
    name: variable.name,
    type,
    ...(variable.default !== undefined ? { default: variable.default } : {}),
    ...(containerType === "array" || containerType === "set" ? { container: containerType } : {}),
    ...(variable.category !== undefined ? { category: variable.category } : {}),
  };
}

function commandPlan(order: string[], blueprints: BlueprintDesiredState[], layouts: FeaturePlan["layout_plans"]): CommandPlanEntry[] {
  const byPath = new Map(blueprints.map((blueprint) => [blueprint.asset_path, blueprint]));
  const layoutByGraph = new Map(layouts.map((item) => [`${item.asset_path}:${item.graph.name}`, item.graph]));
  const commands: Omit<CommandPlanEntry, "order">[] = [];
  for (const path of order) {
    const blueprint = byPath.get(path) as BlueprintDesiredState;
    const eventGraph = blueprint.graphs.find((graph) => graph.kind === "event");
    // Pass 1: Create assets and structure
    commands.push({ phase: "build", command: "puerts_blueprint_build", asset_path: path, params: {
      asset_path: path, parent_class: blueprint.parent_class, components: blueprint.components,
      variables: blueprint.variables.map(lowerVariable),
      ...(eventGraph ? { graph: loweredGraph(layoutByGraph.get(`${path}:${eventGraph.name}`) as ReturnType<typeof planGraphLayout>) } : {}),
      compile: true, save: true, clear_existing_graph: true,
      remove_unlisted: Object.fromEntries(blueprint.convergence.remove_unlisted.map((scope) => [scope, true])),
    } });
    const memberOperations: Array<Record<string, unknown>> = [
      ...blueprint.functions.map((fn) => ({ op: "set_function", ...fn })),
      ...blueprint.interfaces.map((interfacePath) => ({ op: "add_interface", path: interfacePath })),
      ...blueprint.variables.filter((variable) => variable.metadata).map((variable) => ({ op: "set_variable_metadata", name: variable.name, metadata: variable.metadata })),
      ...blueprint.dispatchers.map((dispatcher) => ({ op: "add_event_dispatcher", ...dispatcher })),
      ...blueprint.macros.map((macro) => ({ op: "set_macro", ...macro })),
    ];
    if (memberOperations.length > 0) commands.push({ phase: "members", command: "puerts_blueprint_member_patch", asset_path: path, params: { asset_path: path, operations: memberOperations, compile: true, save: true, verify: true } });

    // Pass 2: Behavior and graph wiring
    for (const graph of blueprint.graphs.filter((item) => item.kind !== "event").sort((a, b) => a.name.localeCompare(b.name))) commands.push({ phase: "graphs", command: "puerts_blueprint_graph_patch", asset_path: path, params: { asset_path: path, graph: graph.name, operations: [{ op: "replace_from_desired_state", graph: loweredGraph(layoutByGraph.get(`${path}:${graph.name}`) as ReturnType<typeof planGraphLayout>) }], compile: true, save: true, verify: true } });
    commands.push({ phase: "inspect", command: "puerts_graph_inspect", asset_path: path, params: { asset_path: path, include_pins: true } });
  }
  return commands.map((command, index) => ({ ...command, order: index + 1 }));
}

export function planFeature(request: FeatureRequest): FeaturePlan {
  validateRequest(request);
  const policy = { ...DEFAULT_POLICY, ...request.policy };
  const order = dependencyOrder(request.blueprints);
  const layouts = order.flatMap((asset_path) => {
    const blueprint = request.blueprints.find((item) => item.asset_path === asset_path) as BlueprintDesiredState;
    return [...blueprint.graphs].sort((a, b) => a.name.localeCompare(b.name)).map((graph) => ({ asset_path, graph: planGraphLayout(graph, policy) }));
  });
  const unsupported = unsupportedFor(request);
  const warnings = layouts.flatMap(({ asset_path, graph }) => graphComplexityWarnings(asset_path, graph, policy));
  const linter_findings = layouts.flatMap(({ asset_path, graph }) => lintGraph(asset_path, graph, policy));
  const cppClasses = [...(request.cpp?.classes ?? [])].sort((a, b) => a.name.localeCompare(b.name));
  const unsigned = {
    schema_version: 1 as const,
    feature_id: request.feature_id,
    executable: unsupported.length === 0,
    architecture: chooseArchitecture(request),
    cpp: { classes: cppClasses, files: cppClasses.flatMap((item) => [item.header, item.source]).sort() },
    blueprints: order.map((path) => request.blueprints.find((item) => item.asset_path === path) as BlueprintDesiredState),
    dependency_order: order,
    layout_plans: layouts,
    command_plan: commandPlan(order, request.blueprints, layouts),
    verification_plan: order.flatMap((asset_path, assetIndex) => [
      { order: assetIndex * 7 + 1, kind: "asset" as const, asset_path, requirement: "asset identity, parent, members, and graphs match desired state", available: true },
      { order: assetIndex * 7 + 2, kind: "compile" as const, asset_path, requirement: "compile has no errors", available: true },
      { order: assetIndex * 7 + 3, kind: "save" as const, asset_path, requirement: "mutation reports saved only after clean compile and independent same-session read-back", available: true },
      { order: assetIndex * 7 + 4, kind: "reload" as const, asset_path, requirement: "second-session cold reload and hash comparison remain a separate proof campaign", available: false },
      { order: assetIndex * 7 + 5, kind: "read_back" as const, asset_path, requirement: "independent canonical graph and member inspection matches", available: true },
      { order: assetIndex * 7 + 6, kind: "idempotency" as const, asset_path, requirement: "second identical application changes no bytes or members", available: true },
      { order: assetIndex * 7 + 7, kind: "rollback" as const, asset_path, requirement: "failed atomic batch restores independently observed hashes", available: true },
    ]),
    unsupported,
    warnings,
    linter_findings,
  };
  return { ...unsigned, plan_hash_sha1: createHash("sha1").update(stable(unsigned)).digest("hex") };
}

function comparable(value: unknown): string {
  return stable(value ?? null);
}

export function diffDesiredState(desired: BlueprintDesiredState, observed: ObservedBlueprint | undefined): RepairOperation[] {
  if (!observed) return [{ asset_path: desired.asset_path, scope: "asset", path: desired.asset_path, expected: desired, observed: null, command: "puerts_blueprint_build" }];
  const repairs: RepairOperation[] = [];
  const compare = (scope: RepairOperation["scope"], path: string, expected: unknown, actual: unknown, command: RepairOperation["command"]): void => {
    if (comparable(expected) !== comparable(actual)) repairs.push({ asset_path: desired.asset_path, scope, path, expected, observed: actual, command });
  };
  compare("asset", "parent_class", desired.parent_class, observed.parent_class, "puerts_blueprint_build");
  compare("asset", "components", desired.components, observed.components ?? [], "puerts_blueprint_build");
  compare("members", "variables", desired.variables, observed.variables ?? [], "puerts_blueprint_member_patch");
  compare("members", "functions", desired.functions, observed.functions ?? [], "puerts_blueprint_member_patch");
  compare("members", "dispatchers", desired.dispatchers, observed.dispatchers ?? [], "puerts_blueprint_member_patch");
  compare("members", "interfaces", desired.interfaces, observed.interfaces ?? [], "puerts_blueprint_member_patch");
  compare("graph", "graphs", desired.graphs, observed.graphs ?? [], "puerts_blueprint_graph_patch");
  return repairs;
}
