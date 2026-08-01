import * as UE from "ue";
import * as puerts from "puerts";
import {
  CommandResponse,
  JsonObject,
  JsonSchema,
  JsonValue,
  Permission,
  ToolContext,
  ToolDefinition,
  optionalBoolean,
  optionalNumber,
  optionalString,
  requireString,
  response,
} from "./types";
import {
  commandFailure,
  decodeStructuredValue,
  objectArray,
  optionalObject,
  outputSchema,
  resolveObject,
  stringArray,
} from "./runtime";

const allPermissions: readonly Permission[] = [
  "assets.read", "assets.write", "actors.read", "actors.spawn", "actors.delete",
  "reflection.read", "reflection.write", "functions.call", "level.save", "editor.pie",
  "logs.read", "viewport.capture", "transactions.undo",
];

const targetProperties = {
  actor: { type: "string" },
  object_path: { type: "string" },
} as const;

function schema(
  properties: Readonly<Record<string, Readonly<Record<string, JsonValue>>>>,
  required: readonly string[] = [],
): JsonSchema {
  return { type: "object", properties, required, additionalProperties: false };
}

async function diagnostic(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const actorLimit = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "actor_limit", 500))));
  const data = JSON.parse(context.bridge.GetDiagnosticsJson(actorLimit)) as JsonObject;
  return response(true, "Native PuerTS diagnostic complete.", data);
}
async function findAssets(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const path = optionalString(input, "path") ?? "/Game";
  if (!path.startsWith("/Game") && !path.startsWith("/Engine")) {
    throw new Error("Asset search is limited to /Game and /Engine");
  }
  const recursive = optionalBoolean(input, "recursive", true);
  const typeFilter = optionalString(input, "type") ?? "";
  const nameFilter = optionalString(input, "name") ?? "";
  const limit = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "limit", 100))));
  const assetsJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.FindAssetsJson(path, typeFilter, nameFilter, recursive, limit, assetsJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(assetsJson)) as JsonObject;
  return response(true, "Assets found.", parsed);
}
async function findActors(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const nameFilter = optionalString(input, "name")?.toLowerCase();
  const typeFilter = optionalString(input, "type")?.toLowerCase();
  const limit = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "limit", 200))));
  const actors: JsonValue[] = [];
  const parsed = JSON.parse(context.bridge.GetLevelActorsJson()) as { actors?: unknown };
  const snapshots = Array.isArray(parsed.actors) ? parsed.actors : [];
  for (const snapshot of snapshots) {
    if (snapshot === null || Array.isArray(snapshot) || typeof snapshot !== "object") {
      continue;
    }
    const actor = snapshot as JsonObject;
    const name = typeof actor.name === "string" ? actor.name : "";
    const label = typeof actor.label === "string" ? actor.label : name;
    const className = typeof actor.class_name === "string" ? actor.class_name : "";
    if (nameFilter !== undefined
      && !name.toLowerCase().includes(nameFilter)
      && !label.toLowerCase().includes(nameFilter)) {
      continue;
    }
    if (typeFilter !== undefined && !className.toLowerCase().includes(typeFilter)) {
      continue;
    }
    actors.push(actor);
    if (actors.length >= limit) {
      break;
    }
  }
  return response(true, "Actors found.", { actors, count: actors.length });
}
/** Read one reflected property. Actors and object paths resolve to the same
    UObject and marshal through the same native FJsonObjectConverter call, so a
    struct, array, or map reads back as real JSON rather than as the empty
    object a PuerTS struct wrapper produces under Object.keys. */
async function readProperty(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const object = resolveObject(context.bridge, input);
  const property = requireString(input, "property");
  const valueJson = puerts.$ref<string>("");
  const objectPath = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ReadObjectPropertyJson(object, property, valueJson, objectPath, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(valueJson)) as { value?: JsonValue };
  return response(true, "Property read.", {
    target: { path: puerts.$unref(objectPath) },
    property,
    value: parsed.value === undefined ? null : parsed.value,
  });
}

/** Write one approved reflected property. The value travels as JSON into the
    native writer, which drives FJsonObjectConverter, so any reflected type
    works instead of the three hand-coded vector and rotator cases. The
    response reports the value read back from reflection, not the request. */
async function setProperty(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const object = resolveObject(context.bridge, input);
  const property = requireString(input, "property");
  if (input.value === undefined) {
    throw new Error("value is required");
  }
  const value = decodeStructuredValue(input.value);
  const objectPath = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.SetObjectPropertyJson(
    object,
    property,
    JSON.stringify({ value }),
    objectPath,
    error,
  )) {
    throw new Error(puerts.$unref(error));
  }
  const path = puerts.$unref(objectPath);
  const valueJson = puerts.$ref<string>("");
  const readBackPath = puerts.$ref<string>("");
  const readBackError = puerts.$ref<string>("");
  const readBack = context.bridge.ReadObjectPropertyJson(object, property, valueJson, readBackPath, readBackError)
    ? (JSON.parse(puerts.$unref(valueJson)) as { value?: JsonValue }).value
    : undefined;
  const result = response(true, "Property changed.", {
    target: { path },
    property,
    value: readBack === undefined ? value : readBack,
  });
  if (path.includes(":PersistentLevel.")) {
    result.changed_actors.push(path);
  } else {
    result.changed_assets.push(path);
  }
  return result;
}

async function callApprovedFunction(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const qualifiedName = requireString(input, "function");
  const actorName = requireString(input, "actor");
  const argumentsValue = input.arguments === undefined
    ? undefined
    : decodeStructuredValue(input.arguments);
  if (argumentsValue !== undefined && !Array.isArray(argumentsValue)) {
    throw new Error("arguments must be an array");
  }
  const resultJson = puerts.$ref<string>("");
  const actorPath = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.CallActorFunctionJson(
    actorName,
    qualifiedName,
    JSON.stringify({ arguments: argumentsValue ?? [] }),
    resultJson,
    actorPath,
    error,
  )) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const path = puerts.$unref(actorPath);
  const result = response(true, "Function called.", { target: { path }, ...parsed });
  if (qualifiedName === "Actor.SetActorLabel") {
    result.changed_actors.push(path);
  }
  return result;
}
async function spawnActor(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const classPath = requireString(input, "class_path");
  if (!classPath.startsWith("/Game/") && !classPath.startsWith("/Script/Engine.")
      && classPath !== "/Script/CinematicCamera.CineCameraActor") {
    throw new Error("Actor classes are limited to /Game and /Script/Engine");
  }
  const location = optionalObject(input, "location") ?? {};
  const rotation = optionalObject(input, "rotation") ?? {};
  const actorJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.SpawnActorJson(
    classPath,
    optionalNumber(location, "x", 0),
    optionalNumber(location, "y", 0),
    optionalNumber(location, "z", 0),
    optionalNumber(rotation, "pitch", 0),
    optionalNumber(rotation, "yaw", 0),
    optionalNumber(rotation, "roll", 0),
    actorJson,
    error,
  )) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(actorJson)) as JsonObject;
  const actor = parsed.actor;
  if (actor === null || Array.isArray(actor) || typeof actor !== "object" || typeof actor.path !== "string") {
    throw new Error("Native spawn returned invalid actor JSON");
  }
  const result = response(true, "Actor spawned.", parsed);
  result.changed_actors.push(actor.path);
  return result;
}
async function deleteActor(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const confirmed = optionalBoolean(input, "confirm", false);
  if (!confirmed) {
    throw new Error("delete_actor requires confirm=true");
  }
  const actorPath = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.DeleteLevelActor(requireString(input, "actor"), confirmed, actorPath, error)) {
    throw new Error(puerts.$unref(error));
  }
  const path = puerts.$unref(actorPath);
  const result = response(true, "Actor deleted.", { actor_path: path });
  result.changed_actors.push(path);
  return result;
}
async function createSkyShader(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = optionalString(input, "asset_path") ?? "/Game/MCPGenerated/M_NativeAuroraSky";
  const skyActor = optionalString(input, "sky_actor") ?? "Sky Sphere";
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.CreateAuroraSkyMaterialJson(assetPath, skyActor, resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const materialPath = parsed.material_path;
  const actorPath = parsed.actor_path;
  if (typeof materialPath !== "string" || typeof actorPath !== "string") {
    throw new Error("Native sky shader returned invalid asset or actor paths");
  }
  const created = parsed.created === true;
  const skyActorCreated = parsed.sky_actor_created === true;
  const result = response(
    true,
    created || skyActorCreated
      ? "Animated aurora sky shader created and applied."
      : "Existing aurora sky shader reapplied.",
    parsed,
  );
  if (created) {
    result.changed_assets.push(materialPath);
    result.warnings.push("Shader compilation may finish asynchronously; capture the viewport after shaders settle.");
  }
  result.changed_actors.push(actorPath);
  const originalActorPath = parsed.original_actor_path;
  if (typeof originalActorPath === "string" && originalActorPath !== actorPath) {
    result.changed_actors.push(originalActorPath);
  }
  return result;
}

/** Create or update a Blueprint actor asset from one spec. The composition
    happens here; the node spawning stays in MCPBridgeGraphBuilder, reached
    through the native service. The native side validates the whole spec
    before it touches an asset, so an unsupported node type is a rejection
    rather than a Blueprint with a partial graph. */
async function buildBlueprint(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Blueprint assets are limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    parent_class: optionalString(input, "parent_class") ?? "Actor",
    components: objectArray(input, "components"),
    compile: optionalBoolean(input, "compile", true),
    save: optionalBoolean(input, "save", true),
    clear_existing_graph: optionalBoolean(input, "clear_existing_graph", true),
  };
  const graph = optionalObject(input, "graph");
  if (graph !== undefined) {
    spec.graph = graph;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildBlueprintJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  // The envelope's errors and warnings are the contract; repeating them
  // inside data would give a caller two places to disagree about.
  delete parsed.errors;
  delete parsed.warnings;

  const created = parsed.created === true;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Blueprint build reported errors."
      : created ? "Blueprint created." : "Blueprint updated.",
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

async function buildPhysics(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const actors = input.actors;
  if (!Array.isArray(actors) || actors.length === 0 || actors.length > 200) {
    throw new Error("actors must contain between 1 and 200 physics actor specs");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildPhysicsSceneJson(JSON.stringify({ actors }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const result = response(true, "Physics scene built.", parsed);
  const created = parsed.actors;
  if (Array.isArray(created)) {
    for (const actor of created) {
      if (actor !== null && !Array.isArray(actor) && typeof actor === "object" && typeof actor.path === "string") {
        result.changed_actors.push(actor.path);
      }
    }
  }
  return result;
}

async function observePhysics(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  const actors = stringArray(input, "actors");
  if (!context.bridge.ObservePhysicsSceneJson(JSON.stringify({ actors }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Physics state observed.", JSON.parse(puerts.$unref(resultJson)) as JsonObject);
}

async function captureViewport(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.CaptureViewportJson(JSON.stringify(input), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Viewport screenshot requested.", JSON.parse(puerts.$unref(resultJson)) as JsonObject);
}

async function saveChanges(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const changedAssets: string[] = [];
  for (const assetPath of stringArray(input, "assets")) {
    if (!assetPath.startsWith("/Game/")) {
      throw new Error("Only project assets under /Game can be saved");
    }
    const error = puerts.$ref<string>("");
    if (!context.bridge.SaveProjectAsset(assetPath, error)) {
      throw new Error(puerts.$unref(error) + ": " + assetPath);
    }
    changedAssets.push(assetPath);
  }

  const requestedLevelPath = optionalString(input, "level_path");
  let level = "";
  if (requestedLevelPath !== undefined || changedAssets.length === 0) {
    const savedPath = puerts.$ref<string>("");
    if (!context.bridge.SaveCurrentLevel(requestedLevelPath ?? "", savedPath)) {
      throw new Error(requestedLevelPath !== undefined
        ? "Level save failed. Save-as paths must be under /Game/MCPTests/."
        : "Current level save failed.");
    }
    level = puerts.$unref(savedPath);
  }

  const result = response(true, "Assets and levels saved.", { level, assets: changedAssets });
  result.changed_assets.push(...changedAssets);
  if (level.length > 0) {
    result.changed_assets.push(level);
    result.warnings.push("Undo restores editor state in memory. Save again after undo to update the file on disk.");
  }
  return result;
}
async function startPie(context: ToolContext): Promise<CommandResponse> {
  const error = puerts.$ref<string>("");
  if (!context.bridge.StartPlayInEditor(error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Play In Editor start requested.");
}

async function stopPie(context: ToolContext): Promise<CommandResponse> {
  const error = puerts.$ref<string>("");
  if (!context.bridge.StopPlayInEditor(error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Play In Editor stop requested.");
}

async function getLogs(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const maximumLines = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "maximum_lines", 100))));
  const parsed = JSON.parse(context.bridge.GetRecentLogs(maximumLines)) as { lines?: unknown };
  const lines = Array.isArray(parsed.lines)
    ? parsed.lines.filter((entry: unknown): entry is string => typeof entry === "string")
    : [];
  const result = response(true, "Logs captured.", { count: lines.length });
  result.log_output.push(...lines);
  return result;
}

async function undo(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const expectedId = requireString(input, "transaction_id");
  const outId = puerts.$ref<string>("");
  const outError = puerts.$ref<string>("");
  if (!context.bridge.UndoLastMCPTransaction(expectedId, outId, outError)) {
    throw new Error(puerts.$unref(outError));
  }
  const result = response(true, "Last MCP transaction undone.", { undone_transaction_id: puerts.$unref(outId) });
  result.transaction_id = puerts.$unref(outId);
  return result;
}

export const toolDefinitions: readonly ToolDefinition[] = [
  { name: "diagnostic", inputSchema: schema({ actor_limit: { type: "number" } }), outputSchema, permissions: ["actors.read"], executionTimeoutMs: 2000, execute: diagnostic },
  { name: "find_assets", inputSchema: schema({ path: { type: "string" }, type: { type: "string" }, name: { type: "string" }, recursive: { type: "boolean" }, limit: { type: "number" } }), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 4000, execute: findAssets },
  { name: "find_actors", inputSchema: schema({ name: { type: "string" }, type: { type: "string" }, limit: { type: "number" } }), outputSchema, permissions: ["actors.read"], executionTimeoutMs: 2000, execute: findActors },
  { name: "read_property", inputSchema: schema({ ...targetProperties, property: { type: "string" } }, ["property"]), outputSchema, permissions: ["reflection.read"], executionTimeoutMs: 2000, execute: readProperty },
  { name: "set_property", inputSchema: schema({ ...targetProperties, property: { type: "string" }, value: {} }, ["property", "value"]), outputSchema, permissions: ["reflection.write"], executionTimeoutMs: 2000, execute: setProperty },
  { name: "call_function", inputSchema: schema({ actor: { type: "string" }, function: { type: "string" }, arguments: { type: "array" } }, ["actor", "function"]), outputSchema, permissions: ["functions.call"], executionTimeoutMs: 2000, execute: callApprovedFunction },
  { name: "spawn_actor", inputSchema: schema({ class_path: { type: "string" }, location: { type: "object" }, rotation: { type: "object" } }, ["class_path"]), outputSchema, permissions: ["actors.spawn"], executionTimeoutMs: 3000, execute: spawnActor },
  { name: "delete_actor", inputSchema: schema({ actor: { type: "string" }, confirm: { type: "boolean" } }, ["actor", "confirm"]), outputSchema, permissions: ["actors.delete"], executionTimeoutMs: 2000, execute: deleteActor },
  { name: "sky_shader_create", inputSchema: schema({ asset_path: { type: "string" }, sky_actor: { type: "string" } }), outputSchema, permissions: ["assets.write", "reflection.write"], executionTimeoutMs: 10000, execute: createSkyShader },
  { name: "blueprint_build", inputSchema: schema({ asset_path: { type: "string" }, parent_class: { type: "string" }, components: { type: "array", items: { type: "object" } }, graph: { type: "object" }, compile: { type: "boolean" }, save: { type: "boolean" }, clear_existing_graph: { type: "boolean" } }, ["asset_path"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildBlueprint },
  { name: "physics_build", inputSchema: schema({ actors: { type: "array", items: { type: "object" } } }, ["actors"]), outputSchema, permissions: ["actors.spawn"], executionTimeoutMs: 10000, execute: buildPhysics },
  { name: "physics_observe", inputSchema: schema({ actors: { type: "array", items: { type: "string" } } }), outputSchema, permissions: ["actors.read"], executionTimeoutMs: 2000, execute: observePhysics },
  { name: "viewport_screenshot", inputSchema: schema({ actors: { type: "array", items: { type: "string" } }, filename: { type: "string" } }), outputSchema, permissions: ["viewport.capture"], executionTimeoutMs: 2000, execute: captureViewport },
  { name: "save", inputSchema: schema({ assets: { type: "array", items: { type: "string" } }, level_path: { type: "string" } }), outputSchema, permissions: ["assets.write", "level.save"], executionTimeoutMs: 5000, execute: saveChanges },
  { name: "pie_start", inputSchema: schema({}), outputSchema, permissions: ["editor.pie"], executionTimeoutMs: 2000, execute: startPie },
  { name: "pie_stop", inputSchema: schema({}), outputSchema, permissions: ["editor.pie"], executionTimeoutMs: 2000, execute: stopPie },
  { name: "get_logs", inputSchema: schema({ maximum_lines: { type: "number" } }), outputSchema, permissions: ["logs.read"], executionTimeoutMs: 1000, execute: getLogs },
  { name: "undo", inputSchema: schema({ transaction_id: { type: "string" } }, ["transaction_id"]), outputSchema, permissions: ["transactions.undo"], executionTimeoutMs: 2000, execute: undo },
];

export class ToolRegistry {
  private readonly tools = new Map<string, ToolDefinition>();
  private readonly permissions = new Set<Permission>(allPermissions);

  constructor(definitions: readonly ToolDefinition[]) {
    for (const definition of definitions) {
      if (this.tools.has(definition.name)) {
        throw new Error("Duplicate tool definition: " + definition.name);
      }
      this.tools.set(definition.name, definition);
    }
  }

  manifest(): JsonValue {
    return [...this.tools.values()].map((tool: ToolDefinition) => ({
      name: tool.name,
      input_schema: tool.inputSchema as unknown as JsonValue,
      output_schema: tool.outputSchema as unknown as JsonValue,
      permissions: [...tool.permissions],
      execution_timeout_ms: tool.executionTimeoutMs,
    }));
  }

  async execute(context: ToolContext, name: string, input: JsonObject): Promise<CommandResponse> {
    const tool = this.tools.get(name);
    if (tool === undefined) {
      const result = response(false, "Unknown tool.");
      result.errors.push("Tool is not registered: " + name);
      return result;
    }
    for (const permission of tool.permissions) {
      if (!this.permissions.has(permission)) {
        const result = response(false, "Permission denied.");
        result.errors.push("Missing permission: " + permission);
        return result;
      }
    }
    let timeoutHandle: ReturnType<typeof setTimeout> | undefined;
    try {
      const timeout = new Promise<CommandResponse>((resolve: (value: CommandResponse) => void) => {
        timeoutHandle = setTimeout(() => {
          const result = response(false, "PuerTS execution timed out.");
          result.errors.push("Tool exceeded " + tool.executionTimeoutMs + " ms.");
          resolve(result);
        }, tool.executionTimeoutMs);
      });
      return await Promise.race([tool.execute(context, input), timeout]);
    } catch (error: unknown) {
      return commandFailure(error);
    } finally {
      if (timeoutHandle !== undefined) {
        clearTimeout(timeoutHandle);
      }
    }
  }
}
