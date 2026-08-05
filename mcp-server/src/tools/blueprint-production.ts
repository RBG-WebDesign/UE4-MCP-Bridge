import { z } from "zod";
import { diffDesiredState, planFeature } from "../blueprint-production/public.js";
import type { FeatureRequest, ObservedBlueprint } from "../blueprint-production/public.js";
import type { ToolDefinition } from "../types.js";

const unknownRecord = z.record(z.unknown());
const graphNodeSchema = z.object({
  id: z.string().regex(/^[A-Za-z][A-Za-z0-9_]*$/),
  type: z.string().min(1),
  params: unknownRecord.optional(),
  functional: z.boolean().optional(),
  branch_depth: z.number().int().min(0).optional(),
  branch_lane: z.number().int().optional(),
  region: z.string().min(1).optional(),
  repeat_key: z.string().min(1).optional(),
  loop_expected_iterations: z.number().int().min(0).optional(),
  x: z.number().optional(),
  y: z.number().optional(),
}).strict();
const graphConnectionSchema = z.object({
  from: z.string().min(1), from_pin: z.string().min(1), to: z.string().min(1), to_pin: z.string().min(1),
}).strict();
const graphSchema = z.object({
  name: z.string().min(1),
  kind: z.enum(["event", "function", "macro", "construction"]),
  nodes: z.array(graphNodeSchema),
  connections: z.array(graphConnectionSchema),
}).strict();
const variableSchema = z.object({
  name: z.string().min(1),
  type: unknownRecord,
  default: z.unknown().optional(),
  category: z.string().optional(),
  metadata: unknownRecord.optional(),
}).strict();
const functionSchema = z.object({
  name: z.string().min(1),
  inputs: z.array(unknownRecord).optional(),
  outputs: z.array(unknownRecord).optional(),
  locals: z.array(unknownRecord).optional(),
  metadata: unknownRecord.optional(),
}).strict();
const blueprintSchema = z.object({
  asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_/]+$/),
  parent_class: z.string().min(1),
  depends_on: z.array(z.string().min(1)).optional(),
  components: z.array(unknownRecord),
  variables: z.array(variableSchema),
  functions: z.array(functionSchema),
  macros: z.array(unknownRecord),
  dispatchers: z.array(unknownRecord),
  interfaces: z.array(z.string().min(1)),
  graphs: z.array(graphSchema),
  convergence: z.object({ remove_unlisted: z.array(z.enum(["variables", "components"])) }).strict(),
}).strict();
const architectureSignalsSchema = z.object({
  reusable_runtime_system: z.boolean().optional(),
  complex_algorithm: z.boolean().optional(),
  performance_sensitive: z.boolean().optional(),
  replicated_or_authoritative: z.boolean().optional(),
  large_data_transform: z.boolean().optional(),
  shared_base_class: z.boolean().optional(),
  strict_runtime_tests: z.boolean().optional(),
  asset_composition: z.boolean().optional(),
  designer_settings: z.boolean().optional(),
  event_wiring: z.boolean().optional(),
  visual_extension_points: z.boolean().optional(),
}).strict();
const featureRequestSchema = z.object({
  schema_version: z.literal(1),
  feature_id: z.string().regex(/^[a-z][a-z0-9_]*$/),
  intent: z.string().min(1),
  constraints: z.object({
    ue_version: z.literal("4.27"),
    authoring_root: z.literal("/Game/MCPGenerated"),
    networked: z.boolean(),
    designer_editable: z.boolean(),
  }).strict(),
  policy: z.object({
    max_functional_nodes: z.number().int().positive().optional(),
    max_branch_depth: z.number().int().min(0).optional(),
    horizontal_gap: z.number().int().positive().optional(),
    vertical_gap: z.number().int().positive().optional(),
    large_loop_iterations: z.number().int().positive().optional(),
  }).strict().optional(),
  architecture_signals: architectureSignalsSchema,
  cpp: z.object({ classes: z.array(z.object({
    name: z.string().min(1), header: z.string().min(1), source: z.string().min(1), responsibility: z.string().min(1),
  }).strict()) }).strict().optional(),
  blueprints: z.array(blueprintSchema),
  required_capabilities: z.array(z.string().min(1)).optional(),
}).strict();
const observedBlueprintSchema = z.object({
  asset_path: z.string().min(1),
  parent_class: z.string().optional(),
  components: z.array(unknownRecord).optional(),
  variables: z.array(unknownRecord).optional(),
  functions: z.array(unknownRecord).optional(),
  dispatchers: z.array(unknownRecord).optional(),
  interfaces: z.array(z.string()).optional(),
  graphs: z.array(graphSchema).optional(),
}).strict();

const inputSchema = z.object({
  request: featureRequestSchema,
  observed_blueprints: z.array(observedBlueprintSchema).optional(),
}).strict();

export function createBlueprintProductionTools(): ToolDefinition[] {
  return [{
    name: "blueprint_production_plan",
    description: "Plan a complete UE4.27 Blueprint production request without contacting Unreal. Returns deterministic architecture, layout, canonical command order, unsupported capabilities, proof requirements, and desired-versus-observed repairs. Raw intent is never forwarded to mutation commands.",
    inputSchema,
    handler: async (params) => {
      const parsed = inputSchema.parse(params);
      const request = parsed.request as FeatureRequest;
      const observed = parsed.observed_blueprints as ObservedBlueprint[] | undefined;
      const plan = planFeature(request);
      const observedByPath = new Map((observed ?? []).map((item) => [item.asset_path, item]));
      const repairs = plan.blueprints.flatMap((desired) => diffDesiredState(desired, observedByPath.get(desired.asset_path)));
      return { content: [{ type: "text", text: JSON.stringify({ plan, repairs }, null, 2) }] };
    },
  }];
}
