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
    variables: objectArray(input, "variables"),
    compile: optionalBoolean(input, "compile", true),
    save: optionalBoolean(input, "save", true),
    clear_existing_graph: optionalBoolean(input, "clear_existing_graph", true),
    plan_only: optionalBoolean(input, "plan_only", false),
    force_remove_referenced: optionalBoolean(input, "force_remove_referenced", false),
  };
  // Opt-in downward convergence. Passed through untouched so the native side
  // owns which scopes are supported and rejects the rest by name.
  const removeUnlisted = optionalObject(input, "remove_unlisted");
  if (removeUnlisted !== undefined) {
    spec.remove_unlisted = removeUnlisted;
  }
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

/** Change selected existing graph state without rebuilding the graph.

    blueprint_build is desired-state and takes a whole graph, which is right for
    authoring and wrong for changing one pin default on a graph of forty nodes:
    the caller has to restate the entire graph correctly or lose the parts they
    did not mention. This is the other shape - name the nodes to touch and what
    to do to them - and it never clears anything.

    The envelope is all this function owns. Selector resolution, validation and
    mutation are the native builder's; the transaction, compile, independent
    read-back, verification and save boundary are the native service's. */
async function patchBlueprintGraph(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Blueprint patching is limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = { asset_path: assetPath };
  const graph = optionalString(input, "graph");
  if (graph !== undefined) { spec.graph = graph; }
  const operations = objectArray(input, "operations");
  if (operations.length === 0) {
    throw new Error("operations must be a non-empty array; a patch with no operations is not a request.");
  }
  spec.operations = operations as unknown as JsonValue;
  // Forwarded only when the caller actually set them, so the native side keeps
  // its own defaults rather than having this layer restate them in a second
  // place where the two can drift apart.
  for (const flag of ["plan_only", "compile", "save", "verify"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchBlueprintGraphJson(JSON.stringify(spec), resultJson, error)) {
    // A refused patch has structure worth keeping: which selectors were
    // ambiguous and what they matched, which ones matched nothing, and whether
    // the rollback restored the graph. The native side now answers a failure
    // with that body as well as the sentence, and throwing here would discard
    // it. Envelope, message and error text are exactly what a thrown error
    // already produced; only the previously empty data object is filled.
    const failureBody = puerts.$unref(resultJson);
    if (failureBody.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(failureBody) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  delete parsed.errors;

  const applied = typeof parsed.applied_operation_count === "number" ? parsed.applied_operation_count : 0;
  const planOnly = parsed.plan_only === true;
  const result = response(
    errors.length === 0,
    planOnly
      ? "Blueprint graph patch planned."
      : applied === 0 ? "Blueprint graph already matches the patch." : "Blueprint graph patched.",
    parsed,
  );
  result.errors.push(...errors);
  const objectPath = parsed.asset_path;
  if (!planOnly && applied > 0 && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Change a Blueprint's members: variables, functions, interfaces, event
    dispatchers and components.

    blueprint_graph_patch owns nodes and pins and cannot reach any of these, and
    blueprint_build reaches them only by restating the whole asset. This is the
    incremental shape for the member half.

    The envelope is all this function owns. The mutations are
    UBlueprintMutatorLibrary's, and the classification, transaction, compile,
    independent read-back, verification and save boundary are the native
    service's, exactly as patchBlueprintGraph splits them. */
async function patchBlueprintMembers(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Blueprint member patching is limited to /Game/MCPGenerated/");
  }
  const operations = objectArray(input, "operations");
  if (operations.length === 0) {
    throw new Error("operations must be a non-empty array; a patch with no operations is not a request.");
  }
  const spec: JsonObject = { asset_path: assetPath, operations: operations as unknown as JsonValue };
  // Forwarded only when the caller actually set them, so the native side keeps
  // its own defaults rather than having this layer restate them in a second
  // place where the two can drift apart.
  for (const flag of ["plan_only", "compile", "save", "verify"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchBlueprintMembersJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  delete parsed.errors;

  const applied = typeof parsed.applied_operation_count === "number" ? parsed.applied_operation_count : 0;
  const planOnly = parsed.plan_only === true;
  const result = response(
    errors.length === 0,
    planOnly
      ? "Blueprint member patch planned."
      : applied === 0 ? "Blueprint members already match the patch." : "Blueprint members patched.",
    parsed,
  );
  result.errors.push(...errors);
  const objectPath = parsed.asset_path;
  if (!planOnly && applied > 0 && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Create or replace a UMG Widget Blueprint from one JSON widget tree. The
    tree grammar belongs to the MCPBridgeGraphBuilder widget builder, reached
    through the native service; this function owns nothing but the envelope,
    exactly as buildBlueprint does. The native side validates the whole tree
    before it touches an asset, so an unsupported widget type is a rejection
    rather than a half-built hierarchy. */
async function buildWidget(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Widget Blueprints are limited to /Game/MCPGenerated/");
  }
  const tree = optionalObject(input, "tree");
  if (tree === undefined) {
    throw new Error("tree is required and must hold the root widget");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    tree,
    save: optionalBoolean(input, "save", true),
  };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildWidgetJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const created = parsed.created === true;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Widget Blueprint build reported errors."
      : created ? "Widget Blueprint created." : "Widget Blueprint updated.",
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

/** Read a Blueprint back as JSON. The inverse of blueprint_build, and the
    only Blueprint command that is not a build: it opens no transaction, marks
    nothing dirty, and answers with the asset's own dirty flag read before and
    after the work so the caller can check that for itself. Everything below
    the envelope is the native reader's; this function owns nothing but the
    response shape, the same way buildBlueprint does. */
async function inspectGraph(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    throw new Error("Blueprint inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = {
    asset_path: assetPath,
    graph_name: optionalString(input, "graph_name") ?? "",
    include_pins: optionalBoolean(input, "include_pins", false),
  };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectBlueprintJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Blueprint inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed
  // nothing, and a read that reports a changed asset would tell a client to
  // save something nobody edited.
  return result;
}

/** Create or update a BehaviorTree asset with its Blackboard from one spec.
    The same shape as buildBlueprint: composition here, every protected
    operation in the native libraries. The native side replaces the tree's
    root only on full success, so a failed build leaves an existing tree
    untouched. */
async function buildBehaviorTree(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Behavior Tree assets are limited to /Game/MCPGenerated/");
  }
  const root = optionalObject(input, "root");
  if (root === undefined) {
    throw new Error("root is required: a Behavior Tree without nodes does nothing in PIE");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    root,
    keys: objectArray(input, "keys"),
    save: optionalBoolean(input, "save", true),
  };
  const blackboardPath = optionalString(input, "blackboard_path");
  if (blackboardPath !== undefined) {
    spec.blackboard_path = blackboardPath;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildBehaviorTreeJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const created = parsed.created === true;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Behavior Tree build reported errors."
      : created ? "Behavior Tree created." : "Behavior Tree updated.",
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  for (const key of ["object_path", "blackboard_object_path"]) {
    const path = parsed[key];
    if (typeof path === "string" && path.length > 0) {
      result.changed_assets.push(path);
    }
  }
  return result;
}

/** Read a Behavior Tree back as JSON. The read half of buildBehaviorTree,
    same shape as inspectGraph: no transaction, nothing dirtied, dirty flag
    reported before and after so the caller can check for itself. */
async function inspectBehaviorTree(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    throw new Error("Behavior Tree inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectBehaviorTreeJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Behavior Tree inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return result;
}

/** Read a Widget Blueprint back as JSON. The independent read half of
    widget_build, same shape as inspectGraph and inspectBehaviorTree: no
    transaction, nothing dirtied, dirty flag reported both sides. */
async function inspectWidget(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    throw new Error("Widget Blueprint inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectWidgetJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Widget Blueprint inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return result;
}

/** Read an Animation Blueprint back as JSON. The independent read half of
    buildAnimBlueprint, same shape as inspectGraph, inspectBehaviorTree and
    inspectWidget: no transaction, nothing dirtied, dirty flag reported both
    sides. Reading is wider than authoring on purpose - a caller usually needs
    to read a project's existing AnimBP before it can author a new one. */
async function inspectAnimBlueprint(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    throw new Error("Animation Blueprint inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectAnimBlueprintJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Animation Blueprint inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return result;
}

/** Read an Animation Montage back as JSON: sections, slot tracks, notifies.
    Read-only with no write counterpart, and that is the finding rather than an
    omission - see the montage note on InspectAnimMontageJson. */
async function inspectAnimMontage(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    throw new Error("Animation Montage inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectAnimMontageJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Animation Montage inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Create a NEW Animation Blueprint from one JSON spec. The spec grammar
    belongs to UAnimBlueprintBuilderLibrary, reached through the native
    service; this function owns nothing but the envelope, exactly as
    buildWidget does.

    Create-only: the native side refuses an asset that already exists, because
    the builder's rebuild path clears nothing and would append a second state
    machine. A rerun is therefore a refusal, not a no-op, and that is why this
    tool is annotated mutating rather than mutating-idempotent. */
async function buildAnimBlueprint(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    throw new Error("Animation Blueprints are limited to /Game/MCPGenerated/");
  }
  const animGraph = optionalObject(input, "anim_graph");
  if (animGraph === undefined) {
    throw new Error("anim_graph is required and must hold the pipeline array");
  }
  const stateMachine = optionalObject(input, "state_machine");
  if (stateMachine === undefined) {
    throw new Error("state_machine is required and must hold states and transitions");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    skeleton_path: requireString(input, "skeleton_path"),
    anim_graph: animGraph,
    state_machine: stateMachine,
    variables: objectArray(input, "variables"),
    save: optionalBoolean(input, "save", true),
  };
  const eventGraph = optionalObject(input, "event_graph");
  if (eventGraph !== undefined) {
    spec.event_graph = eventGraph;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildAnimBlueprintJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Animation Blueprint build reported errors and was rolled back."
      : "Animation Blueprint created.",
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (errors.length === 0 && typeof objectPath === "string" && objectPath.length > 0) {
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
  { name: "blueprint_build", inputSchema: schema({ asset_path: { type: "string" }, parent_class: { type: "string" }, components: { type: "array", items: { type: "object" } }, variables: { type: "array", items: { type: "object" } }, graph: { type: "object" }, compile: { type: "boolean" }, save: { type: "boolean" }, clear_existing_graph: { type: "boolean" }, remove_unlisted: { type: "object" }, plan_only: { type: "boolean" }, force_remove_referenced: { type: "boolean" } }, ["asset_path"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildBlueprint },
  { name: "blueprint_graph_patch", inputSchema: schema({ asset_path: { type: "string" }, graph: { type: "string" }, operations: { type: "array", items: { type: "object" } }, plan_only: { type: "boolean" }, compile: { type: "boolean" }, save: { type: "boolean" }, verify: { type: "boolean" } }, ["asset_path", "operations"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 30000, execute: patchBlueprintGraph },
  { name: "blueprint_member_patch", inputSchema: schema({ asset_path: { type: "string" }, operations: { type: "array", items: { type: "object" } }, plan_only: { type: "boolean" }, compile: { type: "boolean" }, save: { type: "boolean" }, verify: { type: "boolean" } }, ["asset_path", "operations"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 60000, execute: patchBlueprintMembers },
  { name: "widget_build", inputSchema: schema({ asset_path: { type: "string" }, tree: { type: "object" }, save: { type: "boolean" } }, ["asset_path", "tree"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildWidget },
  { name: "graph_inspect", inputSchema: schema({ asset_path: { type: "string" }, graph_name: { type: "string" }, include_pins: { type: "boolean" } }, ["asset_path"]), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectGraph },
  { name: "behavior_tree_build", inputSchema: schema({ asset_path: { type: "string" }, blackboard_path: { type: "string" }, keys: { type: "array", items: { type: "object" } }, root: { type: "object" }, save: { type: "boolean" } }, ["asset_path", "root"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildBehaviorTree },
  { name: "behavior_tree_inspect", inputSchema: schema({ asset_path: { type: "string" } }, ["asset_path"]), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectBehaviorTree },
  { name: "widget_inspect", inputSchema: schema({ asset_path: { type: "string" } }, ["asset_path"]), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectWidget },
  { name: "anim_blueprint_build", inputSchema: schema({ asset_path: { type: "string" }, skeleton_path: { type: "string" }, variables: { type: "array", items: { type: "object" } }, anim_graph: { type: "object" }, state_machine: { type: "object" }, event_graph: { type: "object" }, save: { type: "boolean" } }, ["asset_path", "skeleton_path", "anim_graph", "state_machine"]), outputSchema, permissions: ["assets.write"], executionTimeoutMs: 60000, execute: buildAnimBlueprint },
  { name: "anim_blueprint_inspect", inputSchema: schema({ asset_path: { type: "string" } }, ["asset_path"]), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAnimBlueprint },
  { name: "anim_montage_inspect", inputSchema: schema({ asset_path: { type: "string" } }, ["asset_path"]), outputSchema, permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAnimMontage },
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
