import { z } from "zod";
import type { PuerTSClient } from "../puerts-client.js";
import type { ToolDefinition } from "../types.js";

const target = { actor: z.string().optional(), object_path: z.string().optional() };
const vector = z.object({ x: z.number().optional(), y: z.number().optional(), z: z.number().optional() }).strict();
const rotator = z.object({ pitch: z.number().optional(), yaw: z.number().optional(), roll: z.number().optional() }).strict();
const physicsActor = z.object({
  name: z.string().min(1).max(64),
  mesh: z.string(),
  location: vector.optional(),
  rotation: rotator.optional(),
  scale: vector.optional(),
  simulate_physics: z.boolean().optional(),
  mass_kg: z.number().optional(),
  linear_damping: z.number().optional(),
  angular_damping: z.number().optional(),
}).strict();

/** The value of a reflected property. Spelling the alternatives out matters:
    an untyped schema tells a client nothing, and a client with nothing to go
    on sends a struct or an array as JSON text instead of as JSON. */
const reflectedValue = z.union([
  z.string(),
  z.number(),
  z.boolean(),
  z.null(),
  z.array(z.unknown()),
  z.record(z.unknown()),
]).describe(
  "Value in the property's own reflected shape: a number for a float, "
  + "{\"x\":0,\"y\":0,\"z\":0} for a vector, {\"pitch\":0,\"yaw\":0,\"roll\":0} for a rotator, "
  + "an array of strings for Tags.",
);

/** One SimpleConstructionScript component of a generated Blueprint. */
const blueprintComponent = z.object({
  class: z.string().describe(
    "Component class as a reflected short name (\"StaticMeshComponent\") or a "
    + "full path (\"/Script/Engine.PointLightComponent\"). Must derive from ActorComponent.",
  ),
  name: z.string().min(1).max(64),
  attach_to: z.string().optional().describe(
    "Name of a component declared earlier in this array or already on the "
    + "Blueprint. Omit to add at the root.",
  ),
}).strict();

/** The node types UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON can
    spawn today. The authority is GetSupportedNodeTypes() in
    Plugins/MCPBridge/Source/MCPBridgeGraphBuilder; the native command
    re-validates against it, so this enum can only reject earlier, never let a
    node type through that the builder cannot build. The 11 builder passes are
    written up in docs/superpowers/specs/, but only these eight reached this
    repository's implementation. */
const blueprintNodeType = z.enum([
  "BeginPlay",
  "ActorBeginOverlap",
  "ActorEndOverlap",
  "PrintString",
  "CallFunction",
  "Branch",
  "Sequence",
  "Comment",
]);

const blueprintGraphNode = z.object({
  id: z.string().min(1).describe("Unique within this graph; connections address nodes by it."),
  type: blueprintNodeType,
  params: z.record(z.unknown()).optional().describe(
    "Pin defaults by pin name. PrintString takes InString; CallFunction takes "
    + "class and function plus any pin defaults; Sequence takes num_outputs; "
    + "Comment takes text, width, height.",
  ),
  x: z.number().optional(),
  y: z.number().optional(),
}).strict();

const blueprintGraph = z.object({
  nodes: z.array(blueprintGraphNode).max(200),
  connections: z.array(z.object({
    from: z.string().describe("nodeId.pinRole on the source node."),
    to: z.string().describe("nodeId.pinRole on the target node."),
  }).strict()).max(400).optional(),
}).strict().describe(
  "Event graph description. Pin roles: \"exec\" is direction-aware (Then on "
  + "the source, Execute on the target), \"then\" is always Then, and any "
  + "other role is a literal pin name, so data pins wire through the same array.",
);

const specs = [
  ["puerts_diagnostic", "diagnostic", "Prove the in-process PuerTS context, game thread, named-pipe transport, and actor-query timing.", z.object({ actor_limit: z.number().optional() }).strict()],
  ["puerts_find_assets", "find_assets", "Find UE4.27 assets by path, type, or name.", z.object({ path: z.string().optional(), type: z.string().optional(), name: z.string().optional(), recursive: z.boolean().optional(), limit: z.number().optional() }).strict()],
  ["puerts_find_actors", "find_actors", "Find actors in the current editor level.", z.object({ name: z.string().optional(), type: z.string().optional(), limit: z.number().optional() }).strict()],
  ["puerts_read_property", "read_property", "Read an Unreal reflected property.", z.object({ ...target, property: z.string() }).strict()],
  ["puerts_set_property", "set_property", "Set an approved Unreal reflected property in a transaction.", z.object({ ...target, property: z.string(), value: reflectedValue }).strict()],
  ["puerts_call_function", "call_function", "Call a native-approved Unreal function.", z.object({ actor: z.string(), function: z.string(), arguments: z.array(z.unknown()).optional() }).strict()],
  ["puerts_spawn_actor", "spawn_actor", "Spawn an actor in a transaction.", z.object({ class_path: z.string(), location: vector.optional(), rotation: rotator.optional() }).strict()],
  ["puerts_delete_actor", "delete_actor", "Delete an actor in a transaction. Requires confirm=true.", z.object({ actor: z.string(), confirm: z.literal(true) }).strict()],
  ["puerts_sky_shader_create", "sky_shader_create", "Create an animated native HLSL aurora sky material and apply it to a sky sphere in one transaction.", z.object({ asset_path: z.string().optional(), sky_actor: z.string().optional() }).strict()],
  ["puerts_blueprint_build", "blueprint_build",
    "Create or update a compiled Blueprint actor asset from one JSON spec: parent class, "
    + "components, and an event graph. Supported graph node types are BeginPlay, "
    + "ActorBeginOverlap, ActorEndOverlap, PrintString, CallFunction, Branch, Sequence, and "
    + "Comment; anything else is rejected before the asset is touched. Rerunning the same "
    + "spec converges: the asset is loaded rather than duplicated, an existing component of "
    + "the same name and class is left alone, and the event graph is rebuilt from the spec. "
    + "Assets are limited to /Game/MCPGenerated/. Response reports compile status with "
    + "compiler errors and warnings; the asset is only saved when it built clean.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native "
        + "command enforces the same limit; this rejects earlier, at the client.",
      ),
      parent_class: z.string().optional().describe("Actor subclass, short name or path. Defaults to Actor."),
      components: z.array(blueprintComponent).max(64).optional(),
      graph: blueprintGraph.optional(),
      compile: z.boolean().optional().describe("Default true."),
      save: z.boolean().optional().describe("Default true. A build with errors is never saved."),
      clear_existing_graph: z.boolean().optional().describe(
        "Default true: the event graph is replaced by the spec rather than appended to.",
      ),
    }).strict()],
  ["puerts_physics_build", "physics_build", "Build a validated static-mesh rigid-body scene in one transaction.", z.object({ actors: z.array(physicsActor).min(1).max(200) }).strict()],
  ["puerts_physics_observe", "physics_observe", "Read rigid-body transforms and velocities from the editor or PIE world.", z.object({ actors: z.array(z.string()).max(200).optional() }).strict()],
  ["puerts_viewport_screenshot", "viewport_screenshot", "Fit requested actors and save a PNG of the active editor viewport.", z.object({ actors: z.array(z.string()).max(200).optional(), filename: z.string().optional() }).strict()],
  ["puerts_save", "save", "Save approved project assets and the current level.", z.object({ assets: z.array(z.string()).optional(), level_path: z.string().optional() }).strict()],
  ["puerts_pie_start", "pie_start", "Request Play In Editor start.", z.object({}).strict()],
  ["puerts_pie_stop", "pie_stop", "Request Play In Editor stop.", z.object({}).strict()],
  ["puerts_get_logs", "get_logs", "Read captured Unreal logs and command output.", z.object({ maximum_lines: z.number().optional() }).strict()],
  ["puerts_undo", "undo", "Undo the exact last PuerTS transaction.", z.object({ transaction_id: z.string() }).strict()],
] as const;

/** One native pipe tool: its public MCP name, the runtime command it sends,
    and the schema every caller (direct or aliased) must satisfy. */
export interface NativeToolSpec {
  readonly name: string;
  readonly command: string;
  readonly description: string;
  readonly inputSchema: z.ZodType;
}

export const nativeToolSpecs: readonly NativeToolSpec[] = specs.map(
  ([name, command, description, inputSchema]) => ({ name, command, description, inputSchema }),
);

const specsByName = new Map(nativeToolSpecs.map((spec) => [spec.name, spec]));

/** Look up a native tool by its public puerts_* name. Throws on an unknown
    name so a mistyped alias target fails at construction, not at call time. */
export function nativeToolSpec(name: string): NativeToolSpec {
  const spec = specsByName.get(name);
  if (spec === undefined) throw new Error(`Unknown native tool: ${name}`);
  return spec;
}

/** The failure envelope the native lane returns instead of throwing, so a
    caller always sees the same response shape. */
export function nativeFailureEnvelope(
  errors: string[],
  message = "Native PuerTS command failed.",
): Record<string, unknown> {
  return {
    success: false,
    message,
    data: {},
    changed_assets: [],
    changed_actors: [],
    warnings: [],
    errors,
    log_output: [],
    transaction_id: "",
    transport: "named_pipe",
  };
}

/** Parameters that carry an object or an array. An MCP client that has no
    type information for a parameter sends structured input as JSON text, and
    that text reaches Unreal reflection as a string: a struct write dies in the
    runtime validator, an array write reaches C++ as a non-array. Listing the
    parameters here, one line per tool, keeps that decision reviewable. */
const structuredParameters: Readonly<Record<string, readonly string[]>> = {
  puerts_set_property: ["value"],
  puerts_call_function: ["arguments"],
  puerts_spawn_actor: ["location", "rotation"],
  puerts_physics_build: ["actors"],
  puerts_physics_observe: ["actors"],
  puerts_viewport_screenshot: ["actors"],
  puerts_save: ["assets"],
  puerts_blueprint_build: ["components", "graph"],
};

/** Round-trip budget per tool, in milliseconds. Absent means the 7 second
    default in PuerTSClient. Asset authoring blocks the game thread for as
    long as Unreal needs to compile and save, and a client-side timeout while
    the editor is still working reports a failure for work that succeeds. */
const commandTimeouts: Readonly<Record<string, number>> = {
  puerts_blueprint_build: 30000,
};

/** Decode a JSON-encoded object or array back into the structure it encodes.
    Anything else is returned untouched, so a string property value stays a
    string even when it happens to be valid JSON for a number or a quoted
    string. Only text that both opens and closes as an object or an array is
    considered. */
export function decodeStructuredValue(value: unknown): unknown {
  if (typeof value !== "string") return value;
  const text = value.trim();
  const looksStructured = (text.startsWith("{") && text.endsWith("}"))
    || (text.startsWith("[") && text.endsWith("]"));
  if (!looksStructured) return value;
  try {
    const parsed: unknown = JSON.parse(text);
    return parsed !== null && typeof parsed === "object" ? parsed : value;
  } catch {
    return value;
  }
}

/** Apply decodeStructuredValue to the structured parameters of one tool. */
export function decodeStructuredParams(
  toolName: string,
  params: Record<string, unknown>,
): Record<string, unknown> {
  const keys = structuredParameters[toolName];
  if (keys === undefined) return params;
  const decoded: Record<string, unknown> = { ...params };
  for (const key of keys) {
    if (key in decoded) decoded[key] = decodeStructuredValue(decoded[key]);
  }
  return decoded;
}

/** The single execution path for every native command. Both the puerts_*
    tools and the compatibility aliases go through this, so an alias cannot
    drift from the tool it fronts. */
export async function executeNativeCommand(
  client: PuerTSClient,
  spec: NativeToolSpec,
  params: Record<string, unknown>,
): Promise<Record<string, unknown>> {
  try {
    const parsed = spec.inputSchema.parse(
      decodeStructuredParams(spec.name, params),
    ) as Record<string, unknown>;
    return await client.call(
      spec.command,
      parsed,
      commandTimeouts[spec.name],
    ) as unknown as Record<string, unknown>;
  } catch (error: unknown) {
    return nativeFailureEnvelope([error instanceof Error ? error.message : String(error)]);
  }
}

export function createPuertsTools(client: PuerTSClient): ToolDefinition[] {
  return nativeToolSpecs.map((spec) => ({
    name: spec.name,
    description: `[PRIMARY NATIVE IPC] ${spec.description}`,
    inputSchema: spec.inputSchema,
    handler: async (params: Record<string, unknown>) => {
      const result = await executeNativeCommand(client, spec, params);
      return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
    },
  }));
}
