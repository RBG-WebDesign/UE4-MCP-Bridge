export type Architecture = "blueprint" | "cpp" | "hybrid";

export interface PlannerPolicy {
  max_functional_nodes: number;
  max_branch_depth: number;
  horizontal_gap: number;
  vertical_gap: number;
  large_loop_iterations: number;
}

export interface ArchitectureSignals {
  reusable_runtime_system?: boolean;
  complex_algorithm?: boolean;
  performance_sensitive?: boolean;
  replicated_or_authoritative?: boolean;
  large_data_transform?: boolean;
  shared_base_class?: boolean;
  strict_runtime_tests?: boolean;
  asset_composition?: boolean;
  designer_settings?: boolean;
  event_wiring?: boolean;
  visual_extension_points?: boolean;
}

export interface BlueprintVariable {
  name: string;
  type: Record<string, unknown>;
  default?: unknown;
  category?: string;
  metadata?: Record<string, unknown>;
}

export interface BlueprintFunction {
  name: string;
  inputs?: Array<Record<string, unknown>>;
  outputs?: Array<Record<string, unknown>>;
  locals?: Array<Record<string, unknown>>;
  metadata?: Record<string, unknown>;
}

export interface GraphNode {
  id: string;
  type: string;
  params?: Record<string, unknown>;
  functional?: boolean;
  branch_depth?: number;
  branch_lane?: number;
  region?: string;
  repeat_key?: string;
  loop_expected_iterations?: number;
  x?: number;
  y?: number;
}

export interface GraphConnection {
  from: string;
  from_pin: string;
  to: string;
  to_pin: string;
}

export interface BlueprintGraph {
  name: string;
  kind: "event" | "function" | "macro" | "construction";
  nodes: GraphNode[];
  connections: GraphConnection[];
}

export interface BlueprintDesiredState {
  asset_path: string;
  parent_class: string;
  depends_on?: string[];
  components: Array<Record<string, unknown>>;
  variables: BlueprintVariable[];
  functions: BlueprintFunction[];
  macros: Array<Record<string, unknown>>;
  dispatchers: Array<Record<string, unknown>>;
  interfaces: string[];
  graphs: BlueprintGraph[];
  convergence: { remove_unlisted: Array<"variables" | "components"> };
}

export interface CppProperty {
  name: string;
  type: string;
  specifiers: string[];
  category?: string;
}

export interface CppFunction {
  name: string;
  return_type: string;
  parameters: Array<{ name: string; type: string }>;
  specifiers: string[];
  category?: string;
}

export interface CppDelegate {
  name: string;
  parameters: Array<{ name: string; type: string }>;
}

export interface CppClassPlan {
  name: string;
  header: string;
  source: string;
  parent_class?: string;
  responsibility: string;
  properties?: CppProperty[];
  functions?: CppFunction[];
  delegates?: CppDelegate[];
  unreal_metadata?: Record<string, unknown>;
  blueprint_integration_points?: string[];
}

export interface FeatureRequest {
  schema_version: 1;
  feature_id: string;
  intent: string;
  constraints: {
    ue_version: "4.27";
    authoring_root: "/Game/MCPGenerated";
    networked: boolean;
    designer_editable: boolean;
  };
  policy?: Partial<PlannerPolicy>;
  architecture_signals: ArchitectureSignals;
  cpp?: { classes: CppClassPlan[] };
  blueprints: BlueprintDesiredState[];
  required_capabilities?: string[];
}

export interface PositionedGraph extends BlueprintGraph {
  nodes: Array<GraphNode & { x: number; y: number }>;
  comments: Array<{ region: string; x: number; y: number; width: number; height: number }>;
  reroutes: Array<{ id: string; x: number; y: number; connection_index: number }>;
  layout_hash_sha1?: string;
  overlap_count?: number;
  wire_crossings?: number;
}

export interface LinterFinding {
  code: string;
  severity: "info" | "warning" | "error";
  message: string;
  graph?: string;
  node_ids?: string[];
  recommended_action: string;
  asset_path?: string;
}

export interface UnsupportedCapability {
  capability: string;
  reason: string;
  asset_path?: string;
  graph?: string;
}

export interface PlannerWarning {
  code: string;
  message: string;
  asset_path?: string;
  graph?: string;
  severity?: "info" | "warning" | "error";
  node_ids?: string[];
  recommended_action?: string;
}

export interface CommandPlanEntry {
  order: number;
  phase: "build" | "members" | "graphs" | "inspect";
  command: "puerts_blueprint_build" | "puerts_blueprint_member_patch" | "puerts_blueprint_graph_patch" | "puerts_graph_inspect";
  asset_path: string;
  params: Record<string, unknown>;
}

export interface ProofStep {
  order: number;
  kind: "asset" | "compile" | "save" | "reload" | "read_back" | "idempotency" | "rollback";
  asset_path: string;
  requirement: string;
  available: boolean;
}

export interface ArchitectureDecision {
  selected: Architecture;
  reasons: string[];
  cpp_responsibilities: string[];
  blueprint_responsibilities: string[];
  hybrid_integration_points: string[];
  build_order: string[];
}

export interface FeaturePlan {
  schema_version: 1;
  feature_id: string;
  executable: boolean;
  architecture: ArchitectureDecision;
  cpp: { classes: CppClassPlan[]; files: string[] };
  blueprints: BlueprintDesiredState[];
  dependency_order: string[];
  layout_plans: Array<{ asset_path: string; graph: PositionedGraph }>;
  command_plan: CommandPlanEntry[];
  verification_plan: ProofStep[];
  unsupported: UnsupportedCapability[];
  warnings: PlannerWarning[];
  linter_findings?: LinterFinding[];
  plan_hash_sha1: string;
}

export interface ObservedBlueprint {
  asset_path: string;
  parent_class?: string;
  components?: Array<Record<string, unknown>>;
  variables?: Array<Record<string, unknown>>;
  functions?: Array<Record<string, unknown>>;
  dispatchers?: Array<Record<string, unknown>>;
  interfaces?: string[];
  graphs?: BlueprintGraph[];
}

export interface RepairOperation {
  asset_path: string;
  scope: "asset" | "members" | "graph";
  path: string;
  expected: unknown;
  observed: unknown;
  command: CommandPlanEntry["command"];
}
