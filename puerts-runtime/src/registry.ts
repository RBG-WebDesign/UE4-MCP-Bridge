import * as UE from "ue";
import * as puerts from "puerts";
import {
  CommandResponse,
  JsonObject,
  JsonValue,
  Permission,
  ToolContext,
  ToolDefinition,
  allPermissions,
  optionalBoolean,
  optionalNumber,
  optionalString,
  requireObject,
  requireString,
  response,
} from "./types";
import {
  commandFailure,
  decodeStructuredValue,
  objectArray,
  optionalObject,
  refuse,
  resolveObject,
  stringArray,
} from "./runtime";

/**
 * What this runtime will and will not execute, derived from the registry that
 * decides it rather than from a second hand-maintained list. Reported inside
 * puerts_diagnostic because a model otherwise learns a capability is gone by
 * calling it and reading a failure, which costs a round trip and leaves a
 * refusal in the transcript that looks like a bug.
 *
 * Capabilities are the registry's own tool names, not a dotted re-spelling of
 * them: the name here is the name a caller sends, so there is nothing to map
 * and nothing to keep in step.
 */
function capabilityReport(): JsonObject {
  const available: string[] = [];
  const unavailable: JsonObject[] = [];
  for (const tool of toolDefinitions) {
    const reason = quarantinedTools[tool.name];
    if (reason === undefined) {
      available.push(tool.name);
    } else {
      unavailable.push({
        capability: tool.name,
        reason_code: "capability_quarantined",
        message: reason,
      });
    }
  }
  return { available, unavailable };
}

async function diagnostic(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const actorLimit = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "actor_limit", 500))));
  const data = JSON.parse(context.bridge.GetDiagnosticsJson(actorLimit)) as JsonObject;
  data.capabilities = capabilityReport();
  return response(true, "Native PuerTS diagnostic complete.", data);
}
async function findAssets(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const path = optionalString(input, "path") ?? "/Game";
  if (!path.startsWith("/Game") && !path.startsWith("/Engine")) {
    refuse("path_not_allowed","Asset search is limited to /Game and /Engine");
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
async function deleteAsset(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/")) {
    refuse("path_not_allowed","Asset deletion is limited to /Game/");
  }
  const confirmed = optionalBoolean(input, "confirm", false);
  if (!confirmed) {
    refuse("confirmation_required","delete_asset requires confirm=true because asset deletion is permanent");
  }
  const force = optionalBoolean(input, "force", false);
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.DeleteAsset(assetPath, confirmed, force, resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const deleted = parsed.deleted === true;
  const alreadyAbsent = parsed.already_absent === true;
  const blocked = parsed.blocked === true;
  const result = response(
    deleted || alreadyAbsent,
    deleted ? "Asset deleted." : alreadyAbsent ? "Asset already absent." : "Asset deletion refused.",
    parsed,
  );
  const referencers = stringArray(parsed, "referencers");
  if (blocked) {
    result.errors.push(referencers.length > 0
      ? "Asset is referenced by: " + referencers.join(", ")
      : "Asset deletion was blocked by a reference.");
  } else if (!deleted && !alreadyAbsent) {
    result.errors.push(typeof parsed.failure_reason === "string"
      ? parsed.failure_reason
      : "UE4.27 did not verify the asset as deleted.");
  }
  if (deleted) {
    result.changed_assets.push(assetPath, ...referencers);
  }
  if (force && referencers.length > 0) {
    result.warnings.push("force=true may have nulled references in the reported referencer packages.");
  }
  return result;
}
function jsonObjectAt(value: JsonValue | undefined): JsonObject | undefined {
  return value !== null && value !== undefined && typeof value === "object" && !Array.isArray(value)
    ? value
    : undefined;
}

/**
 * The actor rows find_actors filters, in one shape whichever reader produced
 * them.
 *
 * GetLevelActorsJson is the default and stays the default: object name, path,
 * class, label and world location, which is exactly what every existing caller
 * already reads, from a plain actor iteration.
 *
 * folder_filter and include_transforms cannot be answered from it, and those
 * two shipped in the legacy level_actors and did not survive the native
 * migration. A request that uses either reads the level through
 * InspectSceneJson instead - the SAME snapshot scene_inspect reports and
 * scene_batch converges against, rather than a third reader that could disagree
 * with both. Its extra cost (bounds per actor, a whole-level hash) is paid only
 * by the requests that need what it knows.
 */
function levelActorRows(context: ToolContext, useSnapshot: boolean, includeTransforms: boolean): JsonObject[] {
  if (!useSnapshot) {
    const parsed = JSON.parse(context.bridge.GetLevelActorsJson()) as { actors?: JsonValue };
    const raw = Array.isArray(parsed.actors) ? parsed.actors : [];
    return raw.map(jsonObjectAt).filter((row): row is JsonObject => row !== undefined);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectSceneJson(
    JSON.stringify({ include_components: false }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as { actors?: JsonValue };
  const raw = Array.isArray(parsed.actors) ? parsed.actors : [];
  const rows: JsonObject[] = [];
  for (const entry of raw) {
    const actor = jsonObjectAt(entry);
    if (actor === undefined) { continue; }
    const transform = jsonObjectAt(actor.transform);
    // Renamed into the reader the default path uses, never the other way round:
    // an existing caller reads name, class_name and location, and a response
    // that changed those keys because a filter was added would be a second
    // regression fixing the first.
    const row: JsonObject = {
      name: typeof actor.id === "string" ? actor.id : "",
      path: typeof actor.path === "string" ? actor.path : "",
      class_name: typeof actor.class === "string" ? actor.class : "",
      label: typeof actor.label === "string" ? actor.label : "",
      location: transform?.location ?? null,
      folder: typeof actor.folder === "string" ? actor.folder : "",
    };
    if (includeTransforms && transform !== undefined) {
      row.transform = transform;
    }
    rows.push(row);
  }
  return rows;
}

async function findActors(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const nameFilter = optionalString(input, "name")?.toLowerCase();
  const typeFilter = optionalString(input, "type")?.toLowerCase();
  // Prefix, not substring, because that is what the legacy level_actors did:
  // folder.startswith(folder_filter), so "Courtyard" takes "Courtyard/Lighting"
  // and does not take "OldCourtyard".
  const folderFilter = optionalString(input, "folder_filter");
  const includeTransforms = optionalBoolean(input, "include_transforms", false);
  const limit = Math.max(1, Math.min(500, Math.trunc(optionalNumber(input, "limit", 200))));
  const actors: JsonValue[] = [];
  for (const actor of levelActorRows(context, folderFilter !== undefined || includeTransforms, includeTransforms)) {
    const name = typeof actor.name === "string" ? actor.name : "";
    const label = typeof actor.label === "string" ? actor.label : name;
    const className = typeof actor.class_name === "string" ? actor.class_name : "";
    const folder = typeof actor.folder === "string" ? actor.folder : "";
    if (nameFilter !== undefined
      && !name.toLowerCase().includes(nameFilter)
      && !label.toLowerCase().includes(nameFilter)) {
      continue;
    }
    if (typeFilter !== undefined && !className.toLowerCase().includes(typeFilter)) {
      continue;
    }
    if (folderFilter !== undefined && !folder.startsWith(folderFilter)) {
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
      && classPath !== "/Script/CinematicCamera.CineCameraActor"
      && classPath !== "/Script/LevelSequence.LevelSequenceActor"
      // NavMeshBoundsVolume is NotBlueprintable, so the usual "wrap it in an
      // empty Blueprint" workaround (which is how project-native C++ classes
      // get placed at all) is refused by the engine itself, not by this
      // allowlist - there is no other route to a navigable level.
      && classPath !== "/Script/NavigationSystem.NavMeshBoundsVolume") {
    throw new Error(
      "Actor classes are limited to /Game, /Script/Engine, CineCameraActor, "
      + "LevelSequenceActor, and NavMeshBoundsVolume");
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

  // name, folder and scale shipped in the legacy actor_spawn and did not
  // survive the native migration; the compat alias refuses all three today.
  // They are restored here by finishing the spawn through scene_batch rather
  // than by teaching the native spawn three more parameters: that command
  // already labels, folders and scales an actor, verifies the result by reading
  // the level back, and refuses a label that belongs to someone else. Both
  // calls are inside the one transaction this command opened, so a finish that
  // fails takes the spawn with it.
  const label = optionalString(input, "name");
  const folder = optionalString(input, "folder");
  const scale = optionalObject(input, "scale");
  const result = response(true, "Actor spawned.", parsed);
  result.changed_actors.push(actor.path);
  if (label === undefined && folder === undefined && scale === undefined) {
    return result;
  }
  if (typeof actor.name !== "string" || actor.name.length === 0) {
    throw new Error("Native spawn returned no object name, so the spawned actor cannot be addressed");
  }

  const operation: JsonObject = { op: "upsert_actor", select: { name: actor.name } };
  if (label !== undefined) { operation.label = label; }
  if (folder !== undefined) { operation.folder = folder; }
  if (scale !== undefined) { operation.scale = scale; }
  const finishJson = puerts.$ref<string>("");
  const finishError = puerts.$ref<string>("");
  if (!context.bridge.ApplySceneBatchJson(
    JSON.stringify({ operations: [operation], verify: true }), finishJson, finishError)) {
    const body = puerts.$unref(finishJson);
    return commandFailure(
      new Error(puerts.$unref(finishError)),
      body.length > 0 ? (JSON.parse(body) as JsonObject) : {},
    );
  }
  // The batch's own report, verbatim: which of the three actually changed, the
  // verification read-back, and the structure hashes either side of it.
  parsed.finish = JSON.parse(puerts.$unref(finishJson)) as JsonObject;
  return result;
}
async function deleteActor(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const confirmed = optionalBoolean(input, "confirm", false);
  if (!confirmed) {
    refuse("confirmation_required","delete_actor requires confirm=true");
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
    refuse("path_not_allowed","Blueprint assets are limited to /Game/MCPGenerated/");
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
    refuse("path_not_allowed","Blueprint patching is limited to /Game/MCPGenerated/");
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
    refuse("path_not_allowed","Blueprint member patching is limited to /Game/MCPGenerated/");
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
    refuse("path_not_allowed","Widget Blueprints are limited to /Game/MCPGenerated/");
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

/** Bind widget properties to Blueprint functions or variables, and expose
    widgets as members of the generated class. The write half of what
    widget_inspect could already read: without it every widget this bridge
    authors is static and unreachable from a graph by name.

    Like every builder here, this function owns the envelope and nothing else.
    Which delegate a property name resolves to, whether a source can drive it,
    and what a failed compile restores are all the native side's, because they
    are UE4.27's rules and not this layer's. */
async function bindWidget(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Widget binding is limited to /Game/MCPGenerated/");
  }
  const bindings = input.bindings === undefined ? undefined : objectArray(input, "bindings");
  const exposeAsVariable = input.expose_as_variable === undefined
    ? undefined
    : stringArray(input, "expose_as_variable");
  if (bindings === undefined && exposeAsVariable === undefined) {
    throw new Error("widget_bind needs bindings, expose_as_variable, or both");
  }

  const spec: JsonObject = {
    asset_path: assetPath,
    remove_unlisted: optionalBoolean(input, "remove_unlisted", false),
    plan_only: optionalBoolean(input, "plan_only", false),
    save: optionalBoolean(input, "save", true),
  };
  // Absent and empty are different: an absent section is one the caller did not
  // state, and remove_unlisted must never prune a section nobody described.
  if (bindings !== undefined) { spec.bindings = bindings; }
  if (exposeAsVariable !== undefined) { spec.expose_as_variable = exposeAsVariable; }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BindWidgetJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const applied = typeof parsed.applied_change_count === "number" ? parsed.applied_change_count : 0;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Widget binding reported errors."
      : parsed.plan_only === true
        ? "Widget binding planned, nothing applied."
        : applied === 0
          ? "Widget bindings already matched the spec."
          : `Widget bindings applied: ${applied}.`,
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  // A plan and a converged rerun changed nothing, so neither reports a changed
  // asset. Claiming one would make an idempotency check meaningless.
  const objectPath = parsed.object_path;
  if (applied > 0 && typeof objectPath === "string" && objectPath.length > 0) {
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
    refuse("path_not_allowed","Blueprint inspection is limited to /Game and /Engine");
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
    refuse("path_not_allowed","Behavior Tree assets are limited to /Game/MCPGenerated/");
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
    refuse("path_not_allowed","Behavior Tree inspection is limited to /Game and /Engine");
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

/** Reconcile a whole Blackboard asset from one desired-state spec. Same shape
    as buildBehaviorTree: composition here, every protected operation in the
    native library. behavior_tree_build's add-only key path is untouched; this
    is the command that can update, remove and reparent. */
async function buildBlackboard(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Blackboard assets are limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    keys: objectArray(input, "keys"),
    remove_unlisted: optionalBoolean(input, "remove_unlisted", false),
    plan_only: optionalBoolean(input, "plan_only", false),
    save: optionalBoolean(input, "save", true),
  };
  const parentPath = optionalString(input, "parent_path");
  if (parentPath !== undefined) {
    spec.parent_path = parentPath;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildBlackboardJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const planOnly = parsed.plan_only === true;
  const converged = parsed.converged === true;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Blackboard build reported errors."
      : planOnly
        ? "Blackboard change planned."
        : converged
          ? "Blackboard already matches the spec."
          : parsed.created === true ? "Blackboard created." : "Blackboard updated.",
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  // A plan and a converged rerun both changed nothing, so neither reports a
  // changed asset: a client told to save something nobody edited will save it.
  const objectPath = parsed.object_path;
  if (!planOnly && !converged && errors.length === 0
    && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Read a Blackboard back as JSON. The read half of buildBlackboard, same
    shape as inspectBehaviorTree: no transaction, nothing dirtied, dirty flag
    reported before and after. */
async function inspectBlackboard(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Blackboard inspection is limited to /Game and /Engine");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectBlackboardJson(
    JSON.stringify({ asset_path: assetPath }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Blackboard inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Read an Environment Query back as JSON. Read-only, and there is no build
    half: UEnvironmentQueryGraph::UpdateAsset rebuilds UEnvQuery::Options from
    the editor graph, so a write to Options is wiped the next time the asset is
    opened. The response says so in build_unsupported_reason. */
async function inspectEnvQuery(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Environment Query inspection is limited to /Game and /Engine");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectEnvQueryJson(
    JSON.stringify({ asset_path: assetPath }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Environment Query inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Read the editor world's navigation configuration. Read-only: no navmesh is
    rebuilt and no actor is touched. */
async function inspectNavigation(context: ToolContext): Promise<CommandResponse> {
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectNavigationJson(JSON.stringify({}), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Navigation configuration inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Answer a batch of navigation queries. Read-only: every kind is a const
    query on UNavigationSystemV1. The batch is validated natively before the
    first query runs, so this layer owns nothing but the envelope. */
async function queryNavigation(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const queries = objectArray(input, "queries");
  if (queries.length === 0) {
    throw new Error("queries must be a non-empty array; a query batch with no entries is not a request.");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.QueryNavigationJson(
    JSON.stringify({ queries: queries as unknown as JsonValue }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Navigation queried.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Reconcile a DataTable from its complete row JSON. The native command owns
    validation, rollback, independent export read-back and saving. */
async function buildDataTable(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","DataTable builds are limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = { asset_path: assetPath };
  const rowStruct = optionalString(input, "row_struct");
  if (rowStruct !== undefined) { spec.row_struct = rowStruct; }
  const rows = input.rows_json;
  if (rows === undefined) {
    spec.rows_json = [];
  } else if (typeof rows === "string" || Array.isArray(rows)) {
    spec.rows_json = rows as JsonValue;
  } else {
    throw new Error("rows_json must be a JSON array or JSON text.");
  }
  if (input.plan_only !== undefined) {
    spec.plan_only = optionalBoolean(input, "plan_only", false);
  }
  if (input.save !== undefined) {
    spec.save = optionalBoolean(input, "save", true);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildDataTableJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planOnly = parsed.plan_only === true;
  const created = parsed.created === true;
  const unchanged = parsed.unchanged === true;
  const result = response(
    true,
    planOnly
      ? "DataTable build planned."
      : created ? "DataTable created."
      : unchanged ? "DataTable already matches the requested rows."
      : "DataTable rows replaced.",
    parsed,
  );
  const objectPath = parsed.object_path;
  if (!planOnly && !unchanged && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Reconcile one Sound Cue node tree. Validation, rollback, graph regeneration,
    independent read-back and saving all stay inside the native command. */
async function buildAudio(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Sound Cue builds are limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    first_node: requireString(input, "first_node"),
    nodes: objectArray(input, "nodes") as unknown as JsonValue,
  };
  const properties = optionalObject(input, "properties");
  if (properties !== undefined) { spec.properties = properties; }
  for (const flag of ["plan_only", "save"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildSoundCueJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planOnly = parsed.plan_only === true;
  const unchanged = parsed.unchanged === true;
  const result = response(
    true,
    planOnly ? "Sound Cue build planned." : unchanged ? "Sound Cue already matches." : "Sound Cue built.",
    parsed,
  );
  if (!planOnly && !unchanged && typeof parsed.object_path === "string") {
    result.changed_assets.push(parsed.object_path);
  }
  return result;
}


/** Read a Sound Cue or Sound Wave. Read-only: the native side opens no
    transaction and touches no package. */
async function inspectAudioAsset(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Audio inspection is limited to /Game and /Engine");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectAudioAssetJson(
    JSON.stringify({ asset_path: assetPath }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Audio asset inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Read a skeletal mesh's cloth setup. Read-only, and the read half only: the
    cloth optimizer's three writers are not fronted, because they do not cancel
    their transaction on failure and cloth paint is authored data. */
async function inspectCloth(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const skeletalMesh = requireString(input, "skeletal_mesh");
  if (!skeletalMesh.startsWith("/Game/") && !skeletalMesh.startsWith("/Engine/")) {
    refuse("path_not_allowed","Cloth inspection is limited to /Game and /Engine");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectClothJson(
    JSON.stringify({ skeletal_mesh: skeletalMesh }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "Cloth setup inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Rebuild the editor world's navigation. Mutating and deliberately outside a
    transaction: navmesh tiles are derived data the transaction buffer does not
    record, and the editor's own Build Paths resets the transaction buffer
    before triggering the same build. Every precondition is refused natively
    before a generator runs, because UNavigationSystemV1::Build returns silently
    when it has nothing to do. */
async function buildNavigation(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const wait = optionalBoolean(input, "wait", false);
  const planOnly = optionalBoolean(input, "plan_only", false);
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildNavigationJson(
    JSON.stringify({ wait, plan_only: planOnly }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const status = typeof parsed.status === "string" ? parsed.status : "unknown";
  const message = status === "planned"
    ? "Navigation build planned; nothing was rebuilt."
    : status === "complete"
      ? "Navigation rebuilt."
      : "Navigation rebuild started.";
  const result = response(true, message, parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Reconcile the AIPerceptionComponent configuration on an existing
    AIController Blueprint. The Blueprint must already exist: this configures a
    controller, blueprint_build creates one. */
async function buildAIPerception(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Perception authoring is limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    senses: objectArray(input, "senses"),
    remove_unlisted: optionalBoolean(input, "remove_unlisted", false),
    plan_only: optionalBoolean(input, "plan_only", false),
    compile: optionalBoolean(input, "compile", true),
    save: optionalBoolean(input, "save", true),
  };
  const componentName = optionalString(input, "component_name");
  if (componentName !== undefined) {
    spec.component_name = componentName;
  }
  const dominantSense = optionalString(input, "dominant_sense");
  if (dominantSense !== undefined) {
    spec.dominant_sense = dominantSense;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildAIPerceptionJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  const warnings = stringArray(parsed, "warnings");
  delete parsed.errors;
  delete parsed.warnings;

  const planOnly = parsed.plan_only === true;
  const applied = typeof parsed.applied_change_count === "number" ? parsed.applied_change_count : 0;
  const result = response(
    errors.length === 0,
    errors.length > 0
      ? "Perception build reported errors."
      : planOnly
        ? "Perception change planned."
        : applied === 0 ? "Perception already matches the spec." : "Perception configured.",
    parsed,
  );
  result.errors.push(...errors);
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (!planOnly && applied > 0 && errors.length === 0
    && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Read an AIController Blueprint back as JSON: its perception config and the
    RunBehaviorTree call sites that wire it to a tree. The independent read
    half of buildAIPerception. */
async function inspectAIController(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","AIController inspection is limited to /Game and /Engine");
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectAIControllerJson(
    JSON.stringify({ asset_path: assetPath }), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;
  const result = response(true, "AIController inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Read a Widget Blueprint back as JSON. The independent read half of
    widget_build, same shape as inspectGraph and inspectBehaviorTree: no
    transaction, nothing dirtied, dirty flag reported both sides. */
async function inspectWidget(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Widget Blueprint inspection is limited to /Game and /Engine");
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
    refuse("path_not_allowed","Animation Blueprint inspection is limited to /Game and /Engine");
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
    refuse("path_not_allowed","Animation Montage inspection is limited to /Game and /Engine");
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

async function buildAnimMontage(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/")) {
    refuse("path_not_allowed","Animation Montage authoring is limited to /Game/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    skeleton: requireString(input, "skeleton"),
    slots: objectArray(input, "slots") as unknown as JsonValue,
    sections: objectArray(input, "sections") as unknown as JsonValue,
    plan_only: optionalBoolean(input, "plan_only", false),
    save: optionalBoolean(input, "save", true),
  };
  if (input.notifies !== undefined) {
    if (!Array.isArray(input.notifies)) throw new Error("notifies must be an array");
    spec.notifies = input.notifies;
  }
  const blend = optionalObject(input, "blend");
  if (blend !== undefined) spec.blend = blend;
  const syncGroup = optionalString(input, "sync_group");
  if (syncGroup !== undefined) spec.sync_group = syncGroup;
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildAnimMontageJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const result = response(
    true,
    parsed.plan_only === true ? "Animation Montage build planned."
      : parsed.built === true ? "Animation Montage built."
      : "Animation Montage build completed.",
    parsed,
  );
  if (parsed.built === true && parsed.plan_only !== true
      && typeof parsed.object_path === "string" && parsed.object_path.length > 0) {
    result.changed_assets.push(parsed.object_path);
  }
  return result;
}

/** Read a Blend Space, Blend Space 1D or Aim Offset back as JSON: axes and
    samples. Read-only with no write counterpart, for the reason recorded on
    InspectAnimBlendSpaceJson. */
async function inspectAnimBlendSpace(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Blend Space inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectAnimBlendSpaceJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Blend Space inspected.", parsed);
  result.warnings.push(...warnings);
  return result;
}

/** Create a NEW UBlendSpace1D from one JSON spec: skeleton, one axis, and a
    sample list. Create-only, for the reason recorded on BuildAnimBlendSpaceJson:
    UE4.27 rebuilds a blend space's whole triangulation from its sample set, so
    there is no atomic way to replace an existing one's samples - only to build
    a new asset from nothing and refuse if it already exists. */
async function buildAnimBlendSpace(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Blend Space builds are limited to /Game/MCPGenerated/");
  }
  const spec: JsonObject = {
    asset_path: assetPath,
    skeleton_path: requireString(input, "skeleton_path"),
    axis: requireObject(input, "axis"),
    samples: objectArray(input, "samples") as unknown as JsonValue,
  };
  for (const flag of ["plan_only", "save"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildAnimBlendSpaceJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planOnly = parsed.plan_only === true;
  const result = response(
    true,
    planOnly ? "Blend Space build planned." : "Blend Space built.",
    parsed,
  );
  if (!planOnly && typeof parsed.object_path === "string") {
    result.changed_assets.push(parsed.object_path);
  }
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
/** The spec both anim_blueprint_build and anim_blueprint_patch send to the
    native side. One builder, because the two commands differ only in which
    asset_path they accept: a second copy would let a spec that built become a
    spec that cannot be patched. */
function animBlueprintSpec(input: JsonObject): JsonObject {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Animation Blueprints are limited to /Game/MCPGenerated/");
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
  return spec;
}

async function buildAnimBlueprint(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec = animBlueprintSpec(input);

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

/** Replace the generated contents of an Animation Blueprint that already
    exists. The envelope only; the native side owns the refusals, the snapshot,
    the rebuild, the compile and the read-back.

    Note what is NOT here: no attempt to decide client-side whether the asset
    exists or is dirty. Both are races from this side, and the native command
    answers them at the moment it matters with the message the caller needs. */
async function patchAnimBlueprint(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec = animBlueprintSpec(input);

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchAnimBlueprintJson(JSON.stringify(spec), resultJson, error)) {
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
      ? "Animation Blueprint patch reported errors and the asset was restored from disk."
      : "Animation Blueprint patched.",
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

/** Read a UMaterial or a UMaterialInstanceConstant back as JSON. The read half
    material authoring never had, same shape as inspectGraph,
    inspectBehaviorTree and inspectWidget: no transaction, nothing dirtied,
    dirty flag reported both sides. One tool for both asset kinds because a
    caller holding an asset path usually does not know which it has;
    asset_kind in the response says which one answered. */
async function inspectMaterial(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Material inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectMaterialJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Material inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return result;
}

/** Create or update a UMaterialInstanceConstant and set its parameters from
    one desired-state spec. The native side validates every parameter against
    the parent before it touches an asset, so an unknown name is a refusal that
    names the closest matches rather than a silently dropped write. plan_only
    answers without opening a transaction, which is why the changed_assets
    report below is conditional on it. */
async function buildMaterialInstance(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Material instances are limited to /Game/MCPGenerated/");
  }
  const planOnly = optionalBoolean(input, "plan_only", false);
  const spec: JsonObject = {
    asset_path: assetPath,
    scalars: optionalObject(input, "scalars") ?? {},
    vectors: optionalObject(input, "vectors") ?? {},
    textures: optionalObject(input, "textures") ?? {},
    switches: optionalObject(input, "switches") ?? {},
    plan_only: planOnly,
    clear_unlisted: optionalBoolean(input, "clear_unlisted", false),
    save: optionalBoolean(input, "save", true),
  };
  const parentPath = optionalString(input, "parent_path");
  if (parentPath !== undefined) {
    spec.parent_path = parentPath;
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildMaterialInstanceJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const created = parsed.created === true;
  const unchanged = parsed.unchanged === true;
  const result = response(
    true,
    planOnly
      ? "Material instance planned."
      : created ? "Material instance created."
      : unchanged ? "Material instance already matches the spec."
      : "Material instance updated.",
    parsed,
  );
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (!planOnly && !unchanged && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Author a UMaterial graph from one desired-state spec. The spec is the whole
    graph, so a build aimed at an existing material replaces the graph it had
    and a rerun of the same spec converges. The native side validates every
    node, link and output before it touches an asset, so a bad spec is one
    refusal listing everything wrong with it rather than a half-built graph.
    plan_only answers without creating anything, which is why changed_assets
    below is conditional on it. */
async function buildMaterial(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Materials are limited to /Game/MCPGenerated/");
  }
  const planOnly = optionalBoolean(input, "plan_only", false);
  const spec: JsonObject = {
    asset_path: assetPath,
    parameters: objectArray(input, "parameters"),
    expressions: objectArray(input, "expressions"),
    connections: objectArray(input, "connections"),
    outputs: optionalObject(input, "outputs") ?? {},
    two_sided: optionalBoolean(input, "two_sided", false),
    plan_only: planOnly,
    save: optionalBoolean(input, "save", true),
  };
  for (const key of ["domain", "blend_mode", "shading_model"] as const) {
    const value = optionalString(input, key);
    if (value !== undefined) { spec[key] = value; }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildMaterialJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const created = parsed.created === true;
  const result = response(
    true,
    planOnly
      ? "Material planned."
      : created ? "Material created." : "Material graph replaced.",
    parsed,
  );
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (!planOnly && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Generate a UTexture2D so a texture parameter has something to point at.
    Convergent: the same spec produces the same source bytes, and an asset that
    already carries them is reported unchanged and not rewritten, so a rerun
    dirties nothing and reports no changed asset. */
async function importTexture(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Textures are limited to /Game/MCPGenerated/");
  }
  const planOnly = optionalBoolean(input, "plan_only", false);
  const spec: JsonObject = {
    asset_path: assetPath,
    width: optionalNumber(input, "width", 256),
    pattern: optionalString(input, "pattern") ?? "solid",
    checker_size: optionalNumber(input, "checker_size", 32),
    plan_only: planOnly,
    save: optionalBoolean(input, "save", true),
  };
  // Absent means square: the native side defaults height to width, and sending
  // a guessed 256 here would silently override that.
  if (input.height !== undefined) { spec.height = optionalNumber(input, "height", 256); }
  for (const key of ["color", "color_b"] as const) {
    const value = optionalObject(input, key);
    if (value !== undefined) { spec[key] = value; }
  }
  const compression = optionalString(input, "compression");
  if (compression !== undefined) { spec.compression = compression; }
  if (input.srgb !== undefined) { spec.srgb = optionalBoolean(input, "srgb", true); }
  // Passed through rather than dropped so the native refusal is the one the
  // caller reads: a client-side "unknown key" would not say why.
  const sourceFile = optionalString(input, "source_file");
  if (sourceFile !== undefined) { spec.source_file = sourceFile; }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ImportTextureJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;

  const created = parsed.created === true;
  const unchanged = parsed.unchanged === true;
  const result = response(
    true,
    planOnly
      ? "Texture planned."
      : created ? "Texture created."
      : unchanged ? "Texture already matches the spec."
      : "Texture updated.",
    parsed,
  );
  const objectPath = parsed.object_path;
  if (!planOnly && !unchanged && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

/** Read the editor's current level back as JSON. The read half of scene_batch,
    same shape as inspectGraph and inspectWidget: no transaction, nothing
    dirtied, dirty flag reported both sides. The level is whatever the editor
    has open; level_path is an assertion that refuses a mismatch rather than a
    path to load, because loading a level would discard unsaved work. */
async function inspectScene(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const request: JsonObject = {};
  const levelPath = optionalString(input, "level_path");
  if (levelPath !== undefined) { request.level_path = levelPath; }
  const actors = stringArray(input, "actors");
  if (actors.length > 0) { request.actors = actors; }
  const properties = stringArray(input, "include_properties");
  if (properties.length > 0) { request.include_properties = properties; }
  if (input.include_components !== undefined) {
    request.include_components = optionalBoolean(input, "include_components", true);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectSceneJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return response(true, "Level inspected.", parsed);
}

/** Apply a desired-state description of many actors to the current level in one
    transaction.

    The envelope is all this function owns. Selector resolution, validation,
    convergence and mutation are the native command's; so are the transaction,
    the rollback boundary, the independent read-back and the verification. The
    same split patchBlueprintGraph makes.

    Nothing here saves, and nothing here decides to: the native side leaves the
    level dirty and says so, because a level written to disk is not something a
    rollback can take back. */
async function applySceneBatch(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const operations = objectArray(input, "operations");
  if (operations.length === 0) {
    throw new Error("operations must be a non-empty array; a batch with no operations is not a request.");
  }
  const spec: JsonObject = { operations: operations as unknown as JsonValue };
  const levelPath = optionalString(input, "level_path");
  if (levelPath !== undefined) { spec.level_path = levelPath; }
  // Forwarded only when the caller actually set them, so the native side keeps
  // its own defaults rather than having this layer restate them in a second
  // place where the two can drift apart.
  for (const flag of ["plan_only", "verify"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ApplySceneBatchJson(JSON.stringify(spec), resultJson, error)) {
    // A refused batch has structure worth keeping: which selectors matched more
    // than one actor and what they matched, the plan that was refused, and
    // whether the rollback restored the level. Throwing here would discard it.
    const failureBody = puerts.$unref(resultJson);
    if (failureBody.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(failureBody) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const applied = typeof parsed.applied_operation_count === "number" ? parsed.applied_operation_count : 0;
  const planOnly = parsed.plan_only === true;
  const result = response(
    true,
    planOnly
      ? "Scene batch planned."
      : applied === 0 ? "Level already matches the batch." : "Level updated.",
    parsed,
  );
  result.warnings.push(...warnings);
  if (!planOnly && applied > 0) {
    const levelPackage = parsed.level_path;
    if (typeof levelPackage === "string" && levelPackage.length > 0) {
      // The level is dirty in memory and nothing was written. Reporting it as a
      // changed asset is what tells a client there is something to save; the
      // save itself stays puerts_save's call.
      result.changed_assets.push(levelPackage);
    }
    for (const spawned of stringArray(parsed, "spawned_actors")) {
      result.changed_actors.push(spawned);
    }
  }
  return result;
}

/** Start a Lightmass lighting build for the level the editor has open, or
    report the state of one.

    This function is an envelope and nothing else, deliberately: the decision
    that matters, that the command returns as soon as the build has STARTED and
    never blocks on it finishing, is the native side's and is documented there.
    The response says so in `waited` and `completion` rather than leaving a
    caller to assume a returned success means baked lighting. */
async function buildLighting(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = {};
  for (const key of ["action", "quality", "level_path"]) {
    const value = optionalString(input, key);
    if (value !== undefined) { spec[key] = value; }
  }
  if (input.only_current_level !== undefined) {
    spec.only_current_level = optionalBoolean(input, "only_current_level", true);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildLightingJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const started = parsed.started === true;
  const result = response(
    true,
    started
      ? "Lighting build started. It is NOT finished: poll with action=\"status\"."
      : "Lighting status read.",
    parsed,
  );
  if (started) {
    // No changed_assets. The build has not written anything yet, and naming the
    // level here would tell a client there is something to save when there is
    // not; the map build data changes when the build completes, minutes later.
    result.warnings.push(
      "lighting_build does not wait for the build to finish. Poll it with action=\"status\" until "
      + "build_running is false before screenshotting or saving.");
  }
  return result;
}

/** Set inherited class-default (CDO) values on a generated Blueprint.

    The envelope only. The validate-before-mutate pass, the snapshot-and-restore
    boundary that exists because a transaction provably does not cover a CDO
    write (finding 0r), the convergence test and the read-back are the native
    command's. */
async function patchClassDefaults(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = {
    asset_path: requireString(input, "asset_path"),
    properties: requireObject(input, "properties") as JsonValue,
  };
  for (const flag of ["plan_only", "verify", "save"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchClassDefaultsJson(JSON.stringify(spec), resultJson, error)) {
    // A refused patch carries the closest property names, which of the requested
    // values were already satisfied, and whether the restore put the class
    // defaults back. Throwing here would discard all of it.
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const applied = typeof parsed.applied_property_count === "number" ? parsed.applied_property_count : 0;
  const result = response(
    true,
    parsed.plan_only === true
      ? "Class default patch planned."
      : applied === 0 ? "Class defaults already match the request." : "Class defaults changed.",
    parsed,
  );
  if (parsed.transaction_covers_cdo === false) {
    result.warnings.push(
      "CDO->Modify() returned false: this class default object did not reach the undo buffer, so "
      + "editor undo will not revert the write. The command's own restore boundary is what makes a "
      + "failure atomic.");
  }
  if (applied > 0 && typeof parsed.asset_path === "string") {
    result.changed_assets.push(parsed.asset_path);
  }
  return result;
}

/** Read the project's input mappings. The missing inspector for
    UMCPBridgeInputLibrary: the library could add and remove bindings and had no
    reader, so every mutation was only ever confirmed by the mutator's own
    report. READ ONLY - no transaction, and the native side touches only const
    accessors on UInputSettings. */
/** The control schemes input_preset_apply shipped, kept here rather than in
    C++ because a preset is a project recipe, not an engine primitive: AGENTS.md
    puts recipes in the PuerTS layer and keeps the native side to the mapping
    operations themselves. The values are the legacy handler's, unchanged, so an
    old prompt naming a preset gets the same bindings it always did. */
const inputPresets: Readonly<Record<string, { actions: JsonObject[]; axes: JsonObject[] }>> = {
  first_person: {
    axes: [
      { name: "MoveForward", key: "W", scale: 1.0 },
      { name: "MoveForward", key: "S", scale: -1.0 },
      { name: "MoveRight", key: "D", scale: 1.0 },
      { name: "MoveRight", key: "A", scale: -1.0 },
      { name: "Turn", key: "MouseX", scale: 1.0 },
      { name: "LookUp", key: "MouseY", scale: -1.0 },
    ],
    actions: [
      { name: "Jump", key: "SpaceBar" },
      { name: "Fire", key: "LeftMouseButton" },
      { name: "Interact", key: "E" },
      { name: "Crouch", key: "LeftControl" },
      { name: "Sprint", key: "LeftShift" },
    ],
  },
  third_person: {
    axes: [
      { name: "MoveForward", key: "W", scale: 1.0 },
      { name: "MoveForward", key: "S", scale: -1.0 },
      { name: "MoveRight", key: "D", scale: 1.0 },
      { name: "MoveRight", key: "A", scale: -1.0 },
      { name: "Turn", key: "MouseX", scale: 1.0 },
      { name: "LookUp", key: "MouseY", scale: -1.0 },
    ],
    actions: [
      { name: "Jump", key: "SpaceBar" },
      { name: "Interact", key: "E" },
      { name: "Sprint", key: "LeftShift" },
    ],
  },
  top_down: {
    axes: [
      { name: "MoveForward", key: "W", scale: 1.0 },
      { name: "MoveForward", key: "S", scale: -1.0 },
      { name: "MoveRight", key: "D", scale: 1.0 },
      { name: "MoveRight", key: "A", scale: -1.0 },
    ],
    actions: [
      { name: "Interact", key: "E" },
      { name: "PrimaryAction", key: "LeftMouseButton" },
    ],
  },
  tank: {
    axes: [
      { name: "MoveForward", key: "W", scale: 1.0 },
      { name: "MoveForward", key: "S", scale: -1.0 },
      { name: "TurnBody", key: "D", scale: 1.0 },
      { name: "TurnBody", key: "A", scale: -1.0 },
    ],
    actions: [
      { name: "Fire", key: "SpaceBar" },
      { name: "Interact", key: "E" },
    ],
  },
};

async function inspectInputMappings(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const request: JsonObject = {};
  for (const key of ["action_name", "axis_name", "key"]) {
    const value = optionalString(input, key);
    if (value !== undefined) { request[key] = value; }
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectInputMappingsJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  // No changed_assets on purpose: reading the input settings changed nothing.
  return response(true, "Input mappings read.", parsed);
}

/** Reconcile the project's input mappings against a desired set.

    One reconciling call rather than four setters: the legacy lane had
    input_mapping_add, input_mapping_remove and input_preset_apply as three
    round trips per binding, and a control scheme is eleven bindings. The whole
    desired set travels once, is classified against what exists before anything
    is written, and a binding already present is reported unchanged rather than
    reapplied.

    A preset expands here, into the same actions and axes arrays a caller could
    have written by hand, so there is one code path and a preset cannot mean
    something the explicit form cannot express. */
async function patchInputMappings(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = {};
  const presetName = optionalString(input, "preset");
  const actions = objectArray(input, "actions");
  const axes = objectArray(input, "axes");
  if (presetName !== undefined) {
    const preset = inputPresets[presetName];
    if (preset === undefined) {
      throw new Error(
        "Unknown input preset '" + presetName + "'. Available: "
        + Object.keys(inputPresets).sort().join(", "),
      );
    }
    // Explicit entries win by being appended after the preset's: the native
    // side classifies the whole list against live state, and an exact duplicate
    // is reported unchanged rather than applied twice.
    spec.actions = [...preset.actions, ...actions] as unknown as JsonValue;
    spec.axes = [...preset.axes, ...axes] as unknown as JsonValue;
  } else {
    if (input.actions !== undefined) { spec.actions = actions as unknown as JsonValue; }
    if (input.axes !== undefined) { spec.axes = axes as unknown as JsonValue; }
  }
  for (const key of ["remove_actions", "remove_axes"]) {
    if (input[key] !== undefined) { spec[key] = objectArray(input, key) as unknown as JsonValue; }
  }
  for (const flag of ["remove_unlisted", "plan_only"]) {
    if (input[flag] !== undefined) { spec[flag] = optionalBoolean(input, flag, false); }
  }
  if (spec.actions === undefined && spec.axes === undefined
    && spec.remove_actions === undefined && spec.remove_axes === undefined) {
    throw new Error(
      "input_mapping_patch needs at least one of preset, actions, axes, remove_actions or "
      + "remove_axes. A patch with nothing in it is not a request; call puerts_input_mapping_info "
      + "to read the current bindings.",
    );
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchInputMappingsJson(JSON.stringify(spec), resultJson, error)) {
    // A refused patch carries the rollback verdict and the per-operation
    // reasons; throwing here would discard them and leave the caller unable to
    // tell a restored project from a half-written one.
    const failureBody = puerts.$unref(resultJson);
    if (failureBody.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(failureBody) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  delete parsed.errors;
  const applied = typeof parsed.applied_change_count === "number" ? parsed.applied_change_count : 0;
  const planOnly = parsed.plan_only === true;
  const result = response(
    errors.length === 0,
    planOnly
      ? "Input mapping patch planned."
      : applied === 0 ? "Input mappings already match the patch." : "Input mappings patched.",
    parsed,
  );
  result.errors.push(...errors);
  // Config/DefaultInput.ini is not an asset, so nothing goes in changed_assets:
  // telling a client to save a package that was never dirtied would be a lie
  // that costs it a round trip.
  return result;
}

/** Hide or show Content Browser folders, or read the hidden set.

    Display-only editor state in Config/FolderVisibility.ini. Calling with no
    hidden, hide or show is the read: it changes nothing and answers with the
    hidden set, which is what folder_hidden_list asked for. */
async function setFolderVisibility(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = {};
  for (const key of ["hidden", "hide", "show"]) {
    if (input[key] !== undefined) { spec[key] = stringArray(input, key) as unknown as JsonValue; }
  }
  if (input.plan_only !== undefined) { spec.plan_only = optionalBoolean(input, "plan_only", false); }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.SetFolderVisibilityJson(JSON.stringify(spec), resultJson, error)) {
    const failureBody = puerts.$unref(resultJson);
    if (failureBody.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(failureBody) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const errors = stringArray(parsed, "errors");
  delete parsed.errors;
  const applied = typeof parsed.applied_change_count === "number" ? parsed.applied_change_count : 0;
  const readOnlyCall = spec.hidden === undefined && spec.hide === undefined && spec.show === undefined;
  const result = response(
    errors.length === 0,
    readOnlyCall
      ? "Hidden Content Browser folders read."
      : applied === 0 ? "Folder visibility already matches the request." : "Folder visibility changed.",
    parsed,
  );
  result.errors.push(...errors);
  return result;
}

/** Play a camera shake on the running PIE session's player camera. Runtime
    only: nothing is persisted, so there is nothing to save and nothing to undo.
    The native side refuses when PIE is not running rather than reporting a
    shake nobody could have seen. */
async function playCameraShake(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = { shake_class: requireString(input, "shake_class") };
  if (input.scale !== undefined) { spec.scale = optionalNumber(input, "scale", 1.0); }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PlayCameraShakeJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Camera shake played.", JSON.parse(puerts.$unref(resultJson)) as JsonObject);
}

/** Map the PIE agent's native {success, data, error} envelope without changing
    its data contract. */
function pieAgentResponse(op: string, resultJson: string): CommandResponse {
  const agent = JSON.parse(resultJson) as {
    success?: boolean; data?: JsonValue; error?: string | null;
  };
  const succeeded = agent.success === true;
  const result = response(
    succeeded,
    succeeded ? "PIE agent " + op + " complete." : "PIE agent " + op + " failed.",
    agent.data === undefined || agent.data === null ? {} : agent.data,
  );
  if (!succeeded) {
    result.errors.push(typeof agent.error === "string" && agent.error.length > 0
      ? agent.error
      : "The PIE agent refused without giving a reason.");
  }
  return result;
}

/** The read-only half of the PIE agent: observe, read_property, status, expect. */
async function queryPIEAgent(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const op = requireString(input, "op");
  if (op !== "observe" && op !== "read_property" && op !== "status" && op !== "expect") {
    throw new Error("pie_agent_query op must be observe, read_property, status or expect.");
  }
  const request: JsonObject = { op };
  for (const key of ["radius", "class_filter", "log_lines", "operation_id", "conditions", "within_seconds"]) {
    if (input[key] !== undefined) { request[key] = input[key] as JsonValue; }
  }
  if (op === "read_property") {
    request.actor = requireString(input, "actor");
    request.property = requireString(input, "property");
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.QueryPIEAgentJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return pieAgentResponse(op, puerts.$unref(resultJson));
}

/** Imperative, runtime-only half of the PIE agent. It never starts PIE and it
    never changes an asset; the caller must already have a live session. */
async function controlPIEAgent(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const op = requireString(input, "op");
  if (op !== "move_to" && op !== "look_at" && op !== "press" && op !== "key_state"
      && op !== "axis_state" && op !== "clear_axes" && op !== "record_start"
      && op !== "record_stop" && op !== "replay") {
    throw new Error(
      "pie_agent_control op must be move_to, look_at, press, key_state, axis_state, clear_axes, "
      + "record_start, record_stop or replay.",
    );
  }
  const request: JsonObject = { op };
  for (const key of [
    "location", "actor", "timeout", "acceptance_radius", "action", "key", "keys",
    "hold_seconds", "pressed", "axis", "value", "events", "class_filters",
    "log_categories", "max_events", "script", "seed", "budget_seconds", "export",
  ]) {
    if (input[key] !== undefined) { request[key] = input[key] as JsonValue; }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ControlPIEAgentJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return pieAgentResponse(op, puerts.$unref(resultJson));
}
/** Read a ULevelSequence back as JSON. The independent read half of
    buildSequence, same shape as inspectGraph, inspectWidget and inspectScene:
    no transaction, nothing dirtied, dirty flag reported both sides. Reading is
    wider than authoring on purpose - a caller usually needs to read a project's
    existing sequences before it can author one. */
async function inspectSequence(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/") && !assetPath.startsWith("/Engine/")) {
    refuse("path_not_allowed","Level Sequence inspection is limited to /Game and /Engine");
  }
  const request: JsonObject = { asset_path: assetPath };
  if (input.include_keys !== undefined) {
    request.include_keys = optionalBoolean(input, "include_keys", true);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.InspectLevelSequenceJson(JSON.stringify(request), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const result = response(true, "Level Sequence inspected.", parsed);
  result.warnings.push(...warnings);
  // No changed_assets and no changed_actors on purpose: reading changed nothing.
  return result;
}

/** Create or update a ULevelSequence from one desired-state spec.

    The envelope is all this function owns. Binding resolution, track and
    section identity, per-operation convergence, the transaction, the rollback
    boundary and the independent read-back are the native command's - the same
    split applySceneBatch and patchBlueprintGraph make.

    A refused build carries structure worth keeping (which actor label matched
    nothing or matched twice, how far the applier got, whether the rollback
    restored the asset), so a failure with a body is returned rather than
    thrown. */
async function buildSequence(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Level Sequence authoring is limited to /Game/MCPGenerated/");
  }
  const planOnly = optionalBoolean(input, "plan_only", false);
  const spec: JsonObject = {
    asset_path: assetPath,
    bindings: objectArray(input, "bindings") as unknown as JsonValue,
    tracks: objectArray(input, "tracks") as unknown as JsonValue,
    plan_only: planOnly,
    save: optionalBoolean(input, "save", true),
  };
  // Forwarded only when the caller actually set them, so the native side keeps
  // its own defaults rather than having this layer restate them in a second
  // place where the two can drift apart. An omitted frame_rate on an existing
  // sequence means "leave it alone", which a default of 30 here would silently
  // turn into a rate change.
  if (input.frame_rate !== undefined) {
    spec.frame_rate = optionalNumber(input, "frame_rate", 30);
  }
  const playbackRange = optionalObject(input, "playback_range");
  if (playbackRange !== undefined) { spec.playback_range = playbackRange; }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildLevelSequenceJson(JSON.stringify(spec), resultJson, error)) {
    const failureBody = puerts.$unref(resultJson);
    if (failureBody.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(failureBody) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const warnings = stringArray(parsed, "warnings");
  delete parsed.warnings;

  const applied = typeof parsed.applied_operation_count === "number" ? parsed.applied_operation_count : 0;
  const created = parsed.created === true;
  const result = response(
    true,
    planOnly
      ? "Level Sequence build planned."
      : created ? "Level Sequence created."
      : applied === 0 ? "Level Sequence already matches the spec."
      : "Level Sequence updated.",
    parsed,
  );
  result.warnings.push(...warnings);
  const objectPath = parsed.object_path;
  if (!planOnly && applied > 0 && typeof objectPath === "string" && objectPath.length > 0) {
    result.changed_assets.push(objectPath);
  }
  return result;
}

async function buildSequenceEventTrack(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/MCPGenerated/")) {
    refuse("path_not_allowed","Sequence event authoring is limited to /Game/MCPGenerated/");
  }
  const events = objectArray(input, "events");
  if (events.length === 0) throw new Error("events must be a non-empty array");
  const spec: JsonObject = {
    asset_path: assetPath,
    endpoint_name: requireString(input, "endpoint_name"),
    events: events as unknown as JsonValue,
    plan_only: optionalBoolean(input, "plan_only", false),
    save: optionalBoolean(input, "save", true),
  };
  const bindingId = optionalString(input, "binding_id");
  if (bindingId !== undefined) spec.binding_id = bindingId;
  if (input.call_in_editor !== undefined) {
    spec.call_in_editor = optionalBoolean(input, "call_in_editor", false);
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.BuildSequenceEventTrackJson(JSON.stringify(spec), resultJson, error)) {
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planOnly = parsed.plan_only === true;
  const changed = parsed.created_track === true || parsed.created_section === true
    || parsed.created_endpoint === true
    || (typeof parsed.applied_key_count === "number" && parsed.applied_key_count > 0)
    || parsed.pre_structure_hash !== parsed.post_structure_hash;
  const result = response(
    true,
    planOnly ? "Sequence event track build planned."
      : changed ? "Sequence event track updated."
      : "Sequence event track already matches.",
    parsed,
  );
  if (!planOnly && changed && typeof parsed.object_path === "string" && parsed.object_path.length > 0) {
    result.changed_assets.push(parsed.object_path);
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

async function createLevel(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const levelPath = requireString(input, "level_path");
  const templatePath = optionalString(input, "template_path") ?? "";
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.CreateLevelJson(levelPath, templatePath, resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const result = response(true, "Level created, saved and loaded.", JSON.parse(puerts.$unref(resultJson)) as JsonObject);
  result.changed_assets.push(levelPath);
  result.warnings.push("Level creation and save are not covered by editor undo.");
  return result;
}

async function loadLevel(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const levelPath = requireString(input, "level_path");
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.LoadLevelJson(levelPath, resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(true, "Level loaded.", JSON.parse(puerts.$unref(resultJson)) as JsonObject);
}

async function saveLevel(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.SaveLevelJson(optionalBoolean(input, "save_all", false), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const data = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const result = response(true, "Current level saved.", data);
  const savedLevel = data.level_saved;
  if (typeof savedLevel === "string" && savedLevel.length > 0) {
    result.changed_assets.push(savedLevel);
  }
  result.changed_assets.push(...stringArray(data, "assets_saved"));
  result.warnings.push("Saved packages are not restored by editor undo.");
  return result;
}

async function saveChanges(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const changedAssets: string[] = [];
  for (const assetPath of stringArray(input, "assets")) {
    if (!assetPath.startsWith("/Game/")) {
      refuse("path_not_allowed","Only project assets under /Game can be saved");
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
        ? "Level save failed. level_path must be a valid package path under /Game/."
        : "Current level save failed. If this level has never been saved, the "
          + "bridge refuses rather than opening a Save As dialog; retry with "
          + "level_path set to a /Game/ package path to name it.");
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

/** The one entry point behind job_status, job_result and job_cancel. Every
    decision - is it running, may it be cancelled, has the result already been
    collected - is the native command's, because all of it depends on live
    engine state this layer cannot see. See MCPPuerTSBridgeJobs.cpp. */
async function controlJob(
  context: ToolContext,
  input: JsonObject,
  op: "status" | "result" | "cancel",
): Promise<CommandResponse> {
  const request: JsonObject = { op };
  const jobId = optionalString(input, "job_id");
  if (jobId !== undefined) { request.job_id = jobId; }
  if (op !== "status" && request.job_id === undefined) {
    throw new Error(`job_${op} needs a job_id. Call job_status with no job_id to list them.`);
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ControlJobJson(JSON.stringify(request), resultJson, error)) {
    // A refusal carries job_error_code and, where a job was found, its whole
    // record. Throwing would discard both and leave the caller unable to tell
    // "your editor restarted" from "no such job".
    const body = puerts.$unref(resultJson);
    if (body.length > 0) {
      return commandFailure(new Error(puerts.$unref(error)), JSON.parse(body) as JsonObject);
    }
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const job = optionalObject(parsed, "job");
  const state = job !== undefined && typeof job.state === "string" ? job.state : "";
  const message = op === "status"
    ? (jobId === undefined ? "Job list read." : `Job is ${state}.`)
    : op === "result"
      ? `Job result collected; the job is ${state}.`
      : parsed.stopped_now === true
        ? "Job cancelled; it has already stopped."
        : "Cancel requested. It takes effect at the job's next check, not now.";
  return response(true, message, parsed);
}

async function jobStatus(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  return controlJob(context, input, "status");
}
async function jobResult(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  return controlJob(context, input, "result");
}
async function jobCancel(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  return controlJob(context, input, "cancel");
}

/** Start an out-of-process ULevelSequence render and return a job id. Nothing
    is rendered when this returns; that is the point. */
async function startSequenceRender(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const assetPath = requireString(input, "asset_path");
  if (!assetPath.startsWith("/Game/")) {
    refuse("path_not_allowed","Sequence rendering is limited to /Game");
  }
  const spec: JsonObject = { asset_path: assetPath };
  for (const key of ["output_directory", "format", "output_format"]) {
    const value = optionalString(input, key);
    if (value !== undefined) { spec[key] = value; }
  }
  for (const key of ["resolution_x", "resolution_y", "frame_rate", "warm_up_frames"]) {
    if (input[key] !== undefined) { spec[key] = optionalNumber(input, key, 0); }
  }
  for (const key of ["overwrite_existing", "plan_only"]) {
    if (input[key] !== undefined) { spec[key] = optionalBoolean(input, key, false); }
  }

  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.RenderLevelSequenceJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planned = parsed.status === "planned";
  const result = response(
    true,
    planned
      ? "Sequence render planned; no process was started."
      : "Sequence render STARTED in a second process. It is not finished.",
    parsed,
  );
  if (!planned) {
    // No changed_assets. A render writes image files, not assets: nothing in
    // the content browser changed and there is nothing to save.
    result.warnings.push(
      "Nothing is rendered yet. Poll puerts_job_status with the job_id until state is no longer "
      + "\"running\", then collect with puerts_job_result. puerts_job_cancel kills the render "
      + "process; frames already written stay on disk.");
  }
  return result;
}

async function configureProjectMaps(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const spec: JsonObject = {};
  for (const key of ["game_default_map", "editor_startup_map", "global_default_game_mode"]) {
    const value = optionalString(input, key);
    if (value !== undefined) { spec[key] = value; }
  }
  if (Object.keys(spec).length === 0) {
    throw new Error(
      "Provide at least one of: game_default_map, editor_startup_map, global_default_game_mode",
    );
  }
  for (const key of ["game_default_map", "editor_startup_map"]) {
    const value = spec[key];
    if (typeof value === "string" && !value.startsWith("/Game/")) {
      throw new Error(`${key} must name a /Game map.`);
    }
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.ConfigureProjectMapsJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  return response(
    true,
    "Project default maps and game mode updated in Config/DefaultEngine.ini.",
    JSON.parse(puerts.$unref(resultJson)) as JsonObject,
  );
}

async function patchProjectSettings(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const section = optionalString(input, "section");
  if (section === undefined || section.trim() === "") {
    throw new Error(
      "section must name the settings class behind a Project Settings page, such as "
      + "GameMapsSettings or ProjectPackagingSettings.",
    );
  }
  const properties = input.properties;
  if (typeof properties !== "object" || properties === null || Array.isArray(properties)
    || Object.keys(properties as JsonObject).length === 0) {
    throw new Error("properties must be a non-empty object of {propertyName: value}.");
  }
  const spec: JsonObject = { section, properties: properties as JsonObject };
  if (input.plan_only === true) { spec.plan_only = true; }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PatchProjectSettingsJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const data = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const result = response(
    true,
    data.applied === true
      ? `Project settings persisted to ${String(data.ini)}.`
      : `Planned only. Nothing was written to ${String(data.ini)}.`,
    data,
  );
  const unverified = data.persisted_but_not_value_compared;
  if (Array.isArray(unverified) && unverified.length > 0) {
    result.warnings.push(
      `Container properties were confirmed present in the ini but not compared element by element: `
      + `${unverified.join(", ")}. Read them back if the exact contents matter.`,
    );
  }
  return result;
}

async function startProjectPackage(context: ToolContext, input: JsonObject): Promise<CommandResponse> {
  const rawMaps = input.maps;
  if (!Array.isArray(rawMaps) || rawMaps.length === 0 || rawMaps.length > 32) {
    throw new Error("maps must contain 1 to 32 saved /Game map package names.");
  }
  const maps: string[] = [];
  for (const value of rawMaps) {
    if (typeof value !== "string" || !value.startsWith("/Game/")) {
      throw new Error("Every map must be a /Game package name.");
    }
    maps.push(value);
  }
  const target = optionalString(input, "target") ?? "Win64";
  const configuration = optionalString(input, "configuration") ?? "Development";
  if (target !== "Win64") { throw new Error("target must be Win64."); }
  if (configuration !== "Development" && configuration !== "Shipping") {
    throw new Error("configuration must be Development or Shipping.");
  }
  const spec: JsonObject = { maps, target, configuration };
  const outputDirectory = optionalString(input, "output_directory");
  if (outputDirectory !== undefined) { spec.output_directory = outputDirectory; }
  if (input.plan_only !== undefined) {
    spec.plan_only = optionalBoolean(input, "plan_only", false);
  }
  const resultJson = puerts.$ref<string>("");
  const error = puerts.$ref<string>("");
  if (!context.bridge.PackageProjectJson(JSON.stringify(spec), resultJson, error)) {
    throw new Error(puerts.$unref(error));
  }
  const parsed = JSON.parse(puerts.$unref(resultJson)) as JsonObject;
  const planned = parsed.status === "planned";
  const result = response(
    true,
    planned
      ? "Project package planned; no process was started."
      : "Project cook, stage and package STARTED in a child UAT process. It is not finished.",
    parsed,
  );
  if (!planned) {
    result.warnings.push(
      "Poll puerts_job_status with the job_id until state is terminal. Cancellation stops the "
      + "owned UAT process tree but leaves partial staged and archived files.",
    );
  }
  return result;
}

/**
 * Tools that are registered but refused, with the reason a caller gets back.
 *
 * Quarantine rather than deletion, deliberately. A deleted tool answers
 * "Tool is not registered", which reads like a version mismatch and invites a
 * retry; a quarantined one names the defect. The definition stays in
 * toolDefinitions so its schema, permissions and timeout remain reviewable and
 * so unquarantining is the removal of one line here.
 *
 * This is the RUNTIME half of the boundary. The MCP server drops the same
 * tools from discovery (mcp-server/src/tools/puerts.ts, QUARANTINED_TOOLS), so
 * a current client never sees them; this half is what stops a stale client,
 * a hand-written pipe request or a compatibility alias from reaching the
 * native call anyway. The two lists are asserted equal by
 * mcp-server/tests/puerts-tools.test.ts, because they cannot import each other
 * across the process boundary.
 *
 * Not enforced in the C++ allowlist as well: the allowlist is compiled into the
 * editor, so a fix there ships only on a rebuild, and this check runs strictly
 * before any native call is made. Add it there too if a quarantine ever has to
 * survive a runtime that has been replaced.
 */
export const quarantinedTools: Readonly<Record<string, string>> = {
  level_load:
    "Disabled after a repeatable editor crash. Bridge-driven level switching killed the "
    + "UE4.27 editor 3 times out of 3 on 2026-08-05, at two different crash sites "
    + "(EXCEPTION_ACCESS_VIOLATION inside LoadLevelJson's UEditorLoadingAndSavingUtils::LoadMap "
    + "call, and asynchronously in FTickFunctionTask::DoTask a few seconds later). The editor's "
    + "own startup map load never crashes, so the fault is in the PuerTS UObject binding table "
    + "not surviving UWorld teardown. Work in the level the editor already has open, and use "
    + "puerts_save with level_path to move it to a new package. See docs/CAPABILITY_FINDINGS.md, "
    + "section Unknown.",
  level_create:
    "Disabled after a repeatable editor crash. It loads the map it creates, so it reaches the "
    + "same PuerTS binding-table fault as level_load and was reproduced on a brand-new blank map "
    + "on 2026-08-05. Create content in the open level and use puerts_save with level_path. "
    + "See docs/CAPABILITY_FINDINGS.md, section Unknown.",
};

export const toolDefinitions: readonly ToolDefinition[] = [
  { name: "diagnostic", permissions: ["actors.read"], executionTimeoutMs: 2000, execute: diagnostic },
  { name: "find_assets", permissions: ["assets.read"], executionTimeoutMs: 4000, execute: findAssets },
  { name: "delete_asset", permissions: ["assets.delete"], executionTimeoutMs: 30000, execute: deleteAsset },
  // folder_filter and include_transforms are the legacy level_actors parameters
  // restored. Either one routes the read through the scene snapshot, which is a
  // heavier read than the default actor iteration, hence the larger budget.
  { name: "find_actors", permissions: ["actors.read"], executionTimeoutMs: 15000, execute: findActors },
  { name: "read_property", permissions: ["reflection.read"], executionTimeoutMs: 2000, execute: readProperty },
  { name: "set_property", permissions: ["reflection.write"], executionTimeoutMs: 2000, execute: setProperty },
  { name: "call_function", permissions: ["functions.call"], executionTimeoutMs: 2000, execute: callApprovedFunction },
  // name, folder and scale are the legacy actor_spawn parameters restored. They
  // are finished through scene_batch inside the same transaction, so the budget
  // covers a spawn plus a one-operation batch with its verification read.
  { name: "spawn_actor", permissions: ["actors.spawn", "reflection.write"], executionTimeoutMs: 15000, execute: spawnActor },
  { name: "delete_actor", permissions: ["actors.delete"], executionTimeoutMs: 2000, execute: deleteActor },
  { name: "sky_shader_create", permissions: ["assets.write", "reflection.write"], executionTimeoutMs: 10000, execute: createSkyShader },
  { name: "blueprint_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildBlueprint },
  { name: "blueprint_graph_patch", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: patchBlueprintGraph },
  { name: "blueprint_member_patch", permissions: ["assets.write"], executionTimeoutMs: 60000, execute: patchBlueprintMembers },
  { name: "widget_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildWidget },
  { name: "widget_bind", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: bindWidget },
  { name: "graph_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectGraph },
  { name: "behavior_tree_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildBehaviorTree },
  { name: "behavior_tree_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectBehaviorTree },
  { name: "widget_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectWidget },
  { name: "anim_blueprint_build", permissions: ["assets.write"], executionTimeoutMs: 60000, execute: buildAnimBlueprint },
  { name: "anim_blueprint_patch", permissions: ["assets.write"], executionTimeoutMs: 90000, execute: patchAnimBlueprint },
  { name: "anim_blueprint_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAnimBlueprint },
  { name: "anim_montage_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAnimMontage },
  { name: "anim_blend_space_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAnimBlendSpace },
  { name: "anim_blend_space_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildAnimBlendSpace },
  { name: "blackboard_build", permissions: ["assets.write"], executionTimeoutMs: 20000, execute: buildBlackboard },
  { name: "blackboard_inspect", permissions: ["assets.read"], executionTimeoutMs: 10000, execute: inspectBlackboard },
  { name: "eqs_inspect", permissions: ["assets.read"], executionTimeoutMs: 10000, execute: inspectEnvQuery },
  // Navigation reads the level, not an asset, so it is actors.read rather than
  // assets.read: nav data, bounds volumes and modifier volumes are all actors.
  { name: "nav_inspect", permissions: ["actors.read"], executionTimeoutMs: 20000, execute: inspectNavigation },
  { name: "nav_query", permissions: ["actors.read"], executionTimeoutMs: 20000, execute: queryNavigation },
  // 25000, not 60000: the blocking wait path cannot be interrupted by this
  // layer's timer anyway, so the number that matters is the pipe deadline the
  // client is holding. Kept under it so a refusal arrives as a refusal.
  { name: "nav_build", permissions: ["actors.spawn", "level.save"], executionTimeoutMs: 25000, execute: buildNavigation },
  { name: "ai_perception_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildAIPerception },
  { name: "ai_controller_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAIController },
  { name: "material_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectMaterial },
  { name: "audio_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildAudio },
  { name: "data_table_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildDataTable },
  { name: "audio_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectAudioAsset },
  // 25000: the snapshot walks every LOD's render sections and every clothing
  // asset's physical mesh, and a character mesh with several cloth LODs is a
  // much larger read than a Blueprint graph.
  { name: "cloth_inspect", permissions: ["assets.read"], executionTimeoutMs: 25000, execute: inspectCloth },
  { name: "material_instance_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildMaterialInstance },
  // A master material build blocks on a full shader compile, which is a long
  // way past the 30s an instance permutation needs.
  { name: "material_build", permissions: ["assets.write"], executionTimeoutMs: 90000, execute: buildMaterial },
  { name: "texture_import", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: importTexture },
  { name: "scene_inspect", permissions: ["actors.read", "reflection.read"], executionTimeoutMs: 15000, execute: inspectScene },
  // preconditions is declared here but never forwarded to ApplySceneBatchJson:
  // it is enforced in C++ by CheckPreconditions, which runs in AcceptCommand
  // BEFORE the transaction opens and therefore before this registry is reached
  // at all. It appears in the schema only because additionalProperties is false,
  // so an undeclared field would be refused here as a typo before the check
  // that actually reads it ever ran.
  { name: "scene_batch", permissions: ["actors.spawn", "actors.delete", "reflection.write"], executionTimeoutMs: 60000, execute: applySceneBatch },
  // Starts a Lightmass build and returns without waiting for it. The budget is
  // for the synchronous half, the scene gather and Lightmass export, which is
  // the only part this command blocks on.
  { name: "lighting_build", permissions: ["assets.write"], executionTimeoutMs: 60000, execute: buildLighting },
  { name: "class_defaults_patch", permissions: ["assets.write", "reflection.write"], executionTimeoutMs: 20000, execute: patchClassDefaults },
  { name: "input_mapping_info", permissions: ["project.config.read"], executionTimeoutMs: 3000, execute: inspectInputMappings },
  { name: "input_mapping_patch", permissions: ["project.config.read", "project.config.write"], executionTimeoutMs: 10000, execute: patchInputMappings },
  { name: "folder_visibility", permissions: ["project.config.read", "project.config.write"], executionTimeoutMs: 5000, execute: setFolderVisibility },
  { name: "camera_shake", permissions: ["editor.pie"], executionTimeoutMs: 3000, execute: playCameraShake },
  { name: "pie_agent_query", permissions: ["pie.observe"], executionTimeoutMs: 5000, execute: queryPIEAgent },
  { name: "pie_agent_control", permissions: ["editor.pie"], executionTimeoutMs: 5000, execute: controlPIEAgent },
  { name: "sequence_inspect", permissions: ["assets.read"], executionTimeoutMs: 15000, execute: inspectSequence },
  { name: "sequence_build", permissions: ["assets.write", "actors.read"], executionTimeoutMs: 30000, execute: buildSequence },
  { name: "physics_build", permissions: ["actors.spawn"], executionTimeoutMs: 10000, execute: buildPhysics },
  { name: "physics_observe", permissions: ["actors.read"], executionTimeoutMs: 2000, execute: observePhysics },
  { name: "viewport_screenshot", permissions: ["viewport.capture"], executionTimeoutMs: 2000, execute: captureViewport },
  { name: "level_create", permissions: ["level.save"], executionTimeoutMs: 30000, execute: createLevel },
  { name: "level_load", permissions: ["level.save"], executionTimeoutMs: 30000, execute: loadLevel },
  { name: "level_save", permissions: ["level.save"], executionTimeoutMs: 30000, execute: saveLevel },
  { name: "save", permissions: ["assets.write", "level.save"], executionTimeoutMs: 5000, execute: saveChanges },
  { name: "pie_start", permissions: ["editor.pie"], executionTimeoutMs: 2000, execute: startPie },
  { name: "pie_stop", permissions: ["editor.pie"], executionTimeoutMs: 2000, execute: stopPie },
  { name: "get_logs", permissions: ["logs.read"], executionTimeoutMs: 1000, execute: getLogs },
  { name: "undo", permissions: ["transactions.undo"], executionTimeoutMs: 2000, execute: undo },
  // The job API. All three are short reads of an in-memory record plus one
  // live query, so their budgets are small on purpose: a job command that took
  // seconds would be occupying the command slot the job needs free.
  { name: "job_status", permissions: ["jobs.control"], executionTimeoutMs: 5000, execute: jobStatus },
  { name: "job_result", permissions: ["jobs.control"], executionTimeoutMs: 5000, execute: jobResult },
  { name: "job_cancel", permissions: ["jobs.control"], executionTimeoutMs: 5000, execute: jobCancel },
  // Loads the sequence, writes a manifest and spawns a process. It does not
  // wait for the render, so the budget covers the asset load and the spawn.
  { name: "sequence_render_start", permissions: ["assets.read", "jobs.control"], executionTimeoutMs: 20000, execute: startSequenceRender },
  { name: "project_settings_maps", permissions: ["project.config.write"], executionTimeoutMs: 10000, execute: configureProjectMaps },
  // Reaches every Project Settings page, so the timeout covers a CDO write plus
  // one ini rewrite and read-back, not an asset build.
  { name: "project_settings_patch", permissions: ["project.config.write"], executionTimeoutMs: 10000, execute: patchProjectSettings },
  { name: "project_package_start", permissions: ["assets.read", "jobs.control"], executionTimeoutMs: 20000, execute: startProjectPackage },
  { name: "anim_montage_build", permissions: ["assets.write"], executionTimeoutMs: 60000, execute: buildAnimMontage },
  { name: "sequence_event_track_build", permissions: ["assets.write"], executionTimeoutMs: 30000, execute: buildSequenceEventTrack },
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

  async execute(context: ToolContext, name: string, input: JsonObject): Promise<CommandResponse> {
    const tool = this.tools.get(name);
    if (tool === undefined) {
      const result = response(false, "Unknown tool.");
      result.errors.push("Tool is not registered: " + name);
      return result;
    }
    // Before the permission check and before any native call: a quarantined
    // tool must not execute even for a caller that holds every permission.
    const quarantineReason = quarantinedTools[name];
    if (quarantineReason !== undefined) {
      const result = response(false, "The " + name + " capability is disabled.");
      result.error_code = "capability_quarantined";
      result.errors.push(quarantineReason);
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
