/**
 * Compatibility alias router.
 *
 * Legacy HTTP tool names kept alive as router-level aliases onto the
 * native named-pipe catalog, per the Wrap action in docs/TOOL_MIGRATION.md.
 * An alias keeps the old public name and a close copy of the old schema,
 * translates the parameters into the native tool's schema, and executes
 * through exactly the same path the puerts_* tool uses
 * (executeNativeCommand in tools/puerts.ts). No HTTP, no Python, no listener.
 *
 * Two rules make this safe to hand an old prompt:
 *
 * 1. Every result is wrapped with requested_tool / canonical_tool / backend /
 *    compat, so a caller can see it was routed and migrate to the native name.
 * 2. A legacy parameter with no native equivalent is a loud, structured
 *    failure naming the parameter and the canonical tool. Nothing is guessed
 *    and nothing is silently dropped: a screenshot that quietly ignores
 *    `resolution` is worse than one that refuses, because the caller cannot
 *    tell the difference from a correct result.
 *
 * Aliases are never advertised by default. index.ts registers them only when
 * MCP_COMPAT_ALIASES=1, and skips any alias whose name a real registered tool
 * already owns (which is what happens when MCP_ENABLE_LEGACY_HTTP=1 is also
 * set: the real legacy tool wins, the alias steps aside).
 */

import { z } from "zod";
import type { PuerTSClient } from "../puerts-client.js";
import type { ToolDefinition } from "../types.js";
import { compatAliasAnnotations } from "../annotations.js";
import { executeNativeCommand, nativeFailureEnvelope, nativeToolSpec } from "./puerts.js";

const vector = z.object({
  x: z.number().describe("X coordinate"),
  y: z.number().describe("Y coordinate"),
  z: z.number().describe("Z coordinate"),
});

const rotation = z.object({
  pitch: z.number().describe("Pitch in degrees"),
  yaw: z.number().describe("Yaw in degrees"),
  roll: z.number().describe("Roll in degrees"),
});

type Params = Record<string, unknown>;

/** Either the translated native call, or the reason it cannot be made. */
type Translation =
  | { readonly ok: true; readonly params: Params }
  | { readonly ok: false; readonly parameters: string[]; readonly reasons: string[] };

function routed(params: Params): Translation {
  return { ok: true, params };
}

function unmappable(parameters: string[], reasons: string[]): Translation {
  return { ok: false, parameters, reasons };
}

/** Collect the legacy parameters that were actually supplied and have no
    native counterpart. Returns [] when the call is fully translatable. */
function rejectSupplied(
  params: Params,
  rules: ReadonlyArray<readonly [string, string]>,
): { parameters: string[]; reasons: string[] } {
  const parameters: string[] = [];
  const reasons: string[] = [];
  for (const [name, reason] of rules) {
    if (params[name] !== undefined) {
      parameters.push(name);
      reasons.push(`${name}: ${reason}`);
    }
  }
  return { parameters, reasons };
}

const WILDCARD = /[*?]/;

// --- Blueprint member editing: eleven legacy names, one batch command -------
//
// The eleven UBlueprintMutatorLibrary member tools are each exactly one
// operation of puerts_blueprint_member_patch, so the translation is the same
// three lines eleven times over: the legacy blueprint_path becomes asset_path
// and the legacy parameters become one entry in operations. Written as a
// factory rather than eleven copies, because eleven copies of the same shape
// is eleven places for one of them to drift.

/** The Blueprint type descriptor, unchanged from blueprint-graph.ts. The
    native command takes the same shape puerts_graph_inspect reports. */
const blueprintTypeDescriptor = z.object({
  category: z.string().describe(
    "Pin category: bool, byte, int, int64, float, name, string, text, object, class, struct, enum."),
  sub_category: z.string().optional(),
  sub_category_object: z.string().optional().describe(
    "Object path for object/class/struct/enum types, e.g. /Script/Engine.Actor."),
  container_type: z.enum(["none", "array", "set", "map"]).optional(),
});

const blueprintParamList = z.array(z.object({
  name: z.string(),
  type: blueprintTypeDescriptor,
}));

const legacyBlueprintPath = z.string().describe(
  "Content path of the Blueprint asset, e.g. /Game/MCPGenerated/BP_Door.");

/** One narrowing every member alias inherits, stated once. */
const MEMBER_PATH_NOTE =
  " NARROWER THAN THE LEGACY TOOL IN ONE WAY: puerts_blueprint_member_patch only patches "
  + "Blueprints under /Game/MCPGenerated/, and refuses any other path by name. That limit is the "
  + "native command's, not this alias's, and it is refused rather than worked around.";

/** Build one member alias: legacy name -> a single-operation member patch. */
function memberAlias(
  name: string,
  op: string,
  description: string,
  shape: Record<string, z.ZodTypeAny>,
  operation: (params: Params) => Params,
): CompatAlias {
  return {
    name,
    canonical: "puerts_blueprint_member_patch",
    description: `${description}${MEMBER_PATH_NOTE}`,
    inputSchema: z.object({ blueprint_path: legacyBlueprintPath, ...shape }),
    translate: (params) => routed({
      asset_path: params.blueprint_path,
      operations: [{ op, ...operation(params) }],
    }),
  };
}

/** Copy the keys that carry through unchanged, skipping the absent ones. A
    key present with the value undefined is not the same as an absent key to a
    strict native schema, so it must not be written. */
function carry(params: Params, keys: readonly string[], into: Params = {}): Params {
  for (const key of keys) {
    if (params[key] !== undefined) into[key] = params[key];
  }
  return into;
}

// --- Blueprint graph editing ------------------------------------------------
//
// The node and pin tools are the same story one level down: each is one
// operation of puerts_blueprint_graph_patch, addressed by the node_guid the
// legacy tool already took.

/** The node types blueprint_node_add could name that the native builder does
    not support, and what to do instead. Refused by name: a node type quietly
    dropped is a graph that compiles and does nothing. */
const UNSUPPORTED_NODE_TYPES: Readonly<Record<string, string>> = {
  CreateWidget: "no native factory. Build the graph with puerts_blueprint_build instead.",
  MacroInstance: "no native factory.",
  SwitchEnum: "no native factory; SwitchInt and SwitchString are the two switch types available.",
  SwitchName: "no native factory; SwitchInt and SwitchString are the two switch types available.",
  InputAction: "no native factory. InputKey is the one input node type the native builder "
    + "advertises, because it binds a literal FKey and needs no project input mapping.",
  InputAxisEvent: "no native factory. See InputAction.",
  AddDelegate: "no native factory.",
  RemoveDelegate: "no native factory.",
  CallDelegate: "no native factory.",
};

/** Legacy config keys that the native params object spells differently.
    Everything else passes through untouched, and correctly so: the builder
    translates snake_case params back to the camelCase the registry factories
    read (BlueprintGraphBuilderLibrary.cpp RegistryConfigKey), so a legacy
    camelCase key already arrives at the factory in the spelling it wants.
    Only the node types the builder dispatches itself, which never see that
    table, need renaming here. */
const NODE_CONFIG_RENAMES: Readonly<Record<string, Readonly<Record<string, string>>>> = {
  CallFunction: { targetClass: "class", functionName: "function" },
  Sequence: { numOutputs: "num_outputs" },
  InputKey: { key: "fkey_name" },
};

interface CompatAlias {
  /** The legacy public name callers already have in their prompts. */
  readonly name: string;
  /** The puerts_* tool that actually executes the work. */
  readonly canonical: string;
  readonly description: string;
  readonly inputSchema: z.ZodType;
  readonly translate: (params: Params) => Translation;
}

const aliases: readonly CompatAlias[] = [
  {
    name: "blueprint_info",
    canonical: "puerts_graph_inspect",
    description:
      "Inspect an existing Blueprint through the native graph and member reader. The native result "
      + "includes parent class, components, variables, functions, graphs and compile status.",
    inputSchema: z.object({ blueprint_path: legacyBlueprintPath }),
    translate: (params) => routed({ asset_path: params.blueprint_path }),
  },
  {
    name: "widget_build_from_json",
    canonical: "puerts_widget_build",
    description:
      "Create or replace a Widget Blueprint through the native desired-state builder. package_path "
      + "and asset_name become one asset_path. A legacy widget_json without a root key is wrapped "
      + "as {root: widget_json}, exactly as the legacy handler did. The native builder is limited "
      + "to /Game/MCPGenerated/ and refuses any other path.",
    inputSchema: z.object({
      package_path: z.string().startsWith("/"),
      asset_name: z.string().min(1),
      widget_json: z.record(z.unknown()),
    }),
    translate: (params) => {
      const widget = params.widget_json as Params;
      return routed({
        asset_path: `${String(params.package_path).replace(/\/+$/, "")}/${params.asset_name}`,
        tree: Object.prototype.hasOwnProperty.call(widget, "root") ? widget : { root: widget },
      });
    },
  },
  {
    name: "behavior_tree_create",
    canonical: "puerts_behavior_tree_build",
    description:
      "Create or update a Behavior Tree through the native desired-state builder. path and name "
      + "become one asset_path; blackboard_path, keys and tree retain their legacy shapes. The "
      + "legacy empty-tree form is refused because puerts_behavior_tree_build requires a root, "
      + "and the native builder is limited to /Game/MCPGenerated/.",
    inputSchema: z.object({
      path: z.string(),
      name: z.string().min(1),
      blackboard_path: z.string(),
      keys: z.array(z.object({
        name: z.string(),
        type: z.string(),
        base_class: z.string().optional(),
      })).optional(),
      tree: z.record(z.unknown()).optional(),
    }),
    translate: (params) => {
      if (params.tree === undefined) {
        return unmappable(
          ["tree"],
          ["tree: puerts_behavior_tree_build requires a root; it cannot create the legacy empty Behavior Tree."],
        );
      }
      return routed(carry(params, ["blackboard_path", "keys"], {
        asset_path: `${String(params.path).replace(/\/+$/, "")}/${params.name}`,
        root: params.tree,
      }));
    },
  },
  {
    name: "blackboard_create",
    canonical: "puerts_blackboard_build",
    description:
      "Create or converge a Blackboard through the native desired-state builder. path and name "
      + "become one asset_path and keys pass through unchanged. The alias never enables "
      + "remove_unlisted, so existing keys the call did not name are preserved. The native builder "
      + "is limited to /Game/MCPGenerated/.",
    inputSchema: z.object({
      path: z.string(),
      name: z.string().min(1),
      keys: z.array(z.object({
        name: z.string(),
        type: z.string(),
        base_class: z.string().optional(),
      })).optional(),
    }),
    translate: (params) => routed(carry(params, ["keys"], {
      asset_path: `${String(params.path).replace(/\/+$/, "")}/${params.name}`,
    })),
  },
  {
    name: "viewport_fit",
    canonical: "puerts_viewport_screenshot",
    description:
      "Frame named actors and capture the active viewport. The native command has no padding control, "
      + "so padding is refused rather than ignored. actor_names is required because an omitted list "
      + "meant fit every actor to the legacy tool but means capture the current view natively.",
    inputSchema: z.object({
      actor_names: z.array(z.string()).optional(),
      padding: z.number().optional().describe("Not supported natively; supplying it fails the call"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["padding", "puerts_viewport_screenshot uses the editor's native actor framing and exposes no padding control."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      if (!Array.isArray(params.actor_names) || params.actor_names.length === 0) {
        return unmappable(
          ["actor_names"],
          ["actor_names: legacy viewport_fit framed every level actor when omitted, while puerts_viewport_screenshot captures the current view. Name the actors to preserve fit semantics."],
        );
      }
      return routed({ actors: params.actor_names });
    },
  },
  {
    name: "viewport_focus",
    canonical: "puerts_viewport_screenshot",
    description:
      "Frame one named actor and capture the active viewport. The native command has no distance "
      + "control, so distance is refused rather than ignored.",
    inputSchema: z.object({
      actor_name: z.string(),
      distance: z.number().optional().describe("Not supported natively; supplying it fails the call"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["distance", "puerts_viewport_screenshot uses the editor's native actor framing and exposes no camera-distance control."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      return routed({ actors: [params.actor_name] });
    },
  },
  {
    name: "actor_spawn",
    canonical: "puerts_spawn_actor",
    description:
      "Spawn an actor from an asset path at a given location/rotation/scale. asset_path becomes class_path. " +
      "scale, name and folder pass straight through: puerts_spawn_actor takes all three again.",
    inputSchema: z.object({
      asset_path: z.string().describe("Asset or class path (e.g. /Game/Meshes/SM_Cube, /Script/Engine.StaticMeshActor)"),
      location: vector.optional().describe("World location"),
      rotation: rotation.optional().describe("Rotation"),
      scale: vector.optional().describe("World scale"),
      name: z.string().optional().describe("Actor label for the spawned actor"),
      folder: z.string().optional().describe("World Outliner folder path"),
      validate: z.boolean().optional().describe("Native spawn always validates; only true is accepted"),
    }),
    // scale, name and folder were refused here until 2026-08-03, because the
    // native spawn had dropped all three in the migration. They are native
    // parameters again, under the same names, so the alias is a pass-through
    // rather than a refusal with advice.
    translate: (params) => {
      if (params.validate === false) {
        return unmappable(
          ["validate"],
          ["validate: puerts_spawn_actor always validates the spawn; validation cannot be disabled."],
        );
      }
      const translated: Params = { class_path: params.asset_path };
      for (const key of ["location", "rotation", "scale", "name", "folder"] as const) {
        if (params[key] !== undefined) translated[key] = params[key];
      }
      return routed(translated);
    },
  },
  {
    name: "actor_delete",
    canonical: "puerts_delete_actor",
    description:
      "Delete one actor by exact name. The legacy wildcard form has no native equivalent and is refused. " +
      "Calling this tool is the confirmation the native tool requires.",
    inputSchema: z.object({
      actor_name: z.string().describe("Exact actor name or label. Wildcards (* ?) are not supported natively."),
    }),
    translate: (params) => {
      const actorName = params.actor_name as string;
      if (WILDCARD.test(actorName)) {
        return unmappable(
          ["actor_name"],
          ["actor_name: puerts_delete_actor deletes exactly one named actor. Expand the pattern with puerts_find_actors and delete each result."],
        );
      }
      // confirm is not a data parameter: it is the native tool's guard against
      // an accidental delete. Calling actor_delete is that explicit intent.
      return routed({ actor: actorName, confirm: true });
    },
  },
  {
    name: "actor_modify",
    canonical: "puerts_set_property",
    description:
      "Change an actor's visibility through the native reflection writer. location, rotation, scale and mesh " +
      "are not reachable from an actor label alone and are refused with the native call that does work.",
    inputSchema: z.object({
      actor_name: z.string().describe("Actor name or label"),
      visible: z.boolean().optional().describe("Set visibility (writes Actor.bHidden)"),
      location: vector.optional().describe("Not reachable natively by actor label; supplying it fails the call"),
      rotation: rotation.optional().describe("Not reachable natively by actor label; supplying it fails the call"),
      scale: vector.optional().describe("Not reachable natively by actor label; supplying it fails the call"),
      mesh: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      validate: z.boolean().optional().describe("Native writes always validate; only true is accepted"),
    }),
    translate: (params) => {
      // The native actor-target writer resolves properties on the AActor class
      // and against the Actor.* allowlist. Transforms live on the root
      // SceneComponent (SceneComponent.RelativeLocation and friends), which is
      // reachable only through object_path, and the alias has no way to derive
      // that path from a label without guessing the component name.
      const transformReason =
        "puerts_set_property reaches transforms only through object_path on the actor's root SceneComponent " +
        "(properties RelativeLocation, RelativeRotation, RelativeScale3D). Call puerts_set_property directly with that path.";
      const rejected = rejectSupplied(params, [
        ["location", transformReason],
        ["rotation", transformReason],
        ["scale", transformReason],
        ["mesh", "puerts_set_property cannot swap a StaticMesh asset; StaticMeshComponent.StaticMesh is not on the native writable-property allowlist."],
      ]);
      if (params.validate === false) {
        rejected.parameters.push("validate");
        rejected.reasons.push("validate: puerts_set_property always validates the write; validation cannot be disabled.");
      }
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      if (params.visible === undefined) {
        return unmappable(
          ["visible"],
          ["visible: this alias maps only visibility onto puerts_set_property. Supply visible, or call puerts_set_property directly for any other property."],
        );
      }
      // Actor.bHidden is the allowlisted property; it is the inverse of visible.
      return routed({ actor: params.actor_name, property: "bHidden", value: params.visible === false });
    },
  },
  {
    name: "level_actors",
    canonical: "puerts_find_actors",
    description:
      "List actors in the current level. class_filter becomes type, name_filter becomes name, " +
      "folder_filter, include_transforms and limit pass through. " +
      "Native filters are substring matches, so wildcard patterns are refused.",
    inputSchema: z.object({
      class_filter: z.string().optional().describe("Class name substring. Wildcards (* ?) are not supported natively."),
      name_filter: z.string().optional().describe("Actor label substring. Wildcards (* ?) are not supported natively."),
      folder_filter: z.string().optional().describe("World Outliner folder prefix"),
      include_transforms: z.boolean().optional().describe("Include location, rotation and scale. Defaults to true, as it did legacy-side."),
      include_components: z.boolean().optional().describe("Not supported natively; supplying it fails the call"),
      limit: z.number().int().min(1).optional().describe("Max actors to return"),
    }),
    // folder_filter was refused and include_transforms was accepted only as
    // true, because the native find_actors had dropped both in the migration.
    // Both are native parameters again, under the same names. include_components
    // is still refused: puerts_scene_inspect is the tool that reports components.
    translate: (params) => {
      const rejected = rejectSupplied(params, []);
      if (params.include_components === true) {
        rejected.parameters.push("include_components");
        rejected.reasons.push("include_components: puerts_find_actors returns no component list. Read the level with puerts_scene_inspect (include_components) instead.");
      }
      for (const key of ["class_filter", "name_filter"] as const) {
        const value = params[key];
        if (typeof value === "string" && WILDCARD.test(value)) {
          rejected.parameters.push(key);
          rejected.reasons.push(`${key}: puerts_find_actors matches by case-insensitive substring, not by wildcard pattern. Pass the literal substring.`);
        }
      }
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const translated: Params = {};
      if (params.class_filter !== undefined) translated.type = params.class_filter;
      if (params.name_filter !== undefined) translated.name = params.name_filter;
      if (params.folder_filter !== undefined) translated.folder_filter = params.folder_filter;
      // The legacy default was true and the native default is false, so the
      // alias states it rather than inheriting it: a legacy caller that never
      // passed the flag still gets the transforms it always got.
      translated.include_transforms = params.include_transforms !== false;
      if (params.limit !== undefined) translated.limit = params.limit;
      return routed(translated);
    },
  },
  {
    name: "level_save",
    canonical: "puerts_level_save",
    description:
      "Save the current level and, when requested, every dirty project package through the native "
      + "level save command.",
    inputSchema: z.object({
      save_all: z.boolean().optional().describe("Also save all dirty project packages. Default false."),
    }),
    translate: (params) => routed({ save_all: params.save_all === true }),
  },
  {
    name: "level_new",
    canonical: "puerts_level_create",
    description:
      "Create, save and load a new level through the native map command. Refuses an existing target "
      + "and unsaved packages before switching levels.",
    inputSchema: z.object({
      path: z.string().min(1).describe("New map package path, for example /Game/Maps/MyMap."),
      template: z.string().min(1).optional().describe("Existing map package path to copy from."),
    }),
    translate: (params) => routed({
      level_path: params.path,
      ...(params.template === undefined ? {} : { template_path: params.template }),
    }),
  },
  {
    name: "asset_save_many",
    canonical: "puerts_save",
    description: "Save the explicit asset paths passed in, through the native save command.",
    inputSchema: z.object({
      paths: z.union([z.string(), z.array(z.string())]).describe("Asset paths to save (must be under /Game/)"),
    }),
    translate: (params) => {
      const paths = params.paths;
      const assets = typeof paths === "string" ? [paths] : paths as string[];
      if (assets.length === 0) {
        return unmappable(
          ["paths"],
          ["paths: an empty list would make puerts_save save the current level instead of assets. Pass at least one /Game asset path."],
        );
      }
      return routed({ assets });
    },
  },
  {
    name: "asset_list",
    canonical: "puerts_find_assets",
    description:
      "List assets with optional filters. asset_type becomes type and name_pattern becomes name; " +
      "path and recursive pass through unchanged.",
    inputSchema: z.object({
      path: z.string().optional().describe("Path prefix to search under (default /Game)"),
      asset_type: z.string().optional().describe("Filter by asset type class name"),
      name_pattern: z.string().optional().describe("Filter by name substring"),
      recursive: z.boolean().optional().describe("Search recursively (default true)"),
      limit: z.number().int().min(1).optional().describe("Max assets to return (native cap is 500)"),
    }),
    translate: (params) => {
      const translated: Params = {};
      if (params.path !== undefined) translated.path = params.path;
      if (params.asset_type !== undefined) translated.type = params.asset_type;
      if (params.name_pattern !== undefined) translated.name = params.name_pattern;
      if (params.recursive !== undefined) translated.recursive = params.recursive;
      if (params.limit !== undefined) translated.limit = params.limit;
      return routed(translated);
    },
  },
  {
    name: "viewport_screenshot",
    canonical: "puerts_viewport_screenshot",
    description:
      "Capture the active viewport to a PNG file. filename passes through; the native capture uses a fixed " +
      "resolution and hides the editor UI, so resolution and show_ui are refused.",
    inputSchema: z.object({
      filename: z.string().optional().describe("Output filename (auto-generated if omitted)"),
      actors: z.array(z.string()).max(200).optional().describe("Actors to fit before capturing (native extension)"),
      resolution: z.object({
        width: z.number().int().min(1).optional(),
        height: z.number().int().min(1).optional(),
      }).optional().describe("Not supported natively; supplying it fails the call"),
      show_ui: z.boolean().optional().describe("Not supported natively; supplying it fails the call"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["resolution", "puerts_viewport_screenshot captures the active viewport at its own resolution. There is no native resolution override."],
        ["show_ui", "puerts_viewport_screenshot always captures without editor UI. There is no native toggle."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const translated: Params = {};
      if (params.filename !== undefined) translated.filename = params.filename;
      if (params.actors !== undefined) translated.actors = params.actors;
      return routed(translated);
    },
  },
  {
    name: "gameplay_pie_start",
    canonical: "puerts_pie_start",
    description:
      "Request Play In Editor start. level_path is refused: the native command starts PIE in the current level " +
      "and cannot load one first.",
    inputSchema: z.object({
      level_path: z.string().optional().describe("Not supported natively; supplying it fails the call"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["level_path", "puerts_pie_start plays the level already open in the editor. There is no native level-load command to open another one first."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      return routed({});
    },
  },
  {
    name: "gameplay_pie_stop",
    canonical: "puerts_pie_stop",
    description: "Request Play In Editor stop.",
    inputSchema: z.object({}),
    translate: () => routed({}),
  },
  {
    name: "ue_logs",
    canonical: "puerts_get_logs",
    description:
      "Fetch recent UE4 log entries. The native reader returns the captured tail verbatim, so the legacy " +
      "category and severity filters are refused rather than ignored.",
    inputSchema: z.object({
      category: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      severity: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      maximum_lines: z.number().int().min(1).optional().describe("Max lines to return (native cap is 500)"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["category", "puerts_get_logs returns the captured log tail unfiltered. Filter the returned lines client-side."],
        ["severity", "puerts_get_logs returns the captured log tail unfiltered. Filter the returned lines client-side."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const translated: Params = {};
      if (params.maximum_lines !== undefined) translated.maximum_lines = params.maximum_lines;
      return routed(translated);
    },
  },
  {
    name: "undo",
    canonical: "puerts_undo",
    description:
      "Undo one MCP transaction. The native undo targets an exact transaction_id rather than a depth, " +
      "so a legacy count-only call is refused instead of undoing something unrelated.",
    inputSchema: z.object({
      transaction_id: z.string().optional().describe("The transaction_id returned by the call being undone. Required natively."),
      count: z.number().int().min(1).optional().describe("Legacy undo depth; only count=1 is expressible natively"),
    }),
    translate: (params) => {
      const count = params.count;
      if (typeof count === "number" && count !== 1) {
        return unmappable(
          ["count"],
          [`count: puerts_undo undoes exactly one identified transaction, never ${count}. Call it once per transaction_id, newest first.`],
        );
      }
      if (typeof params.transaction_id !== "string" || params.transaction_id.length === 0) {
        return unmappable(
          ["transaction_id"],
          ["transaction_id: puerts_undo requires the transaction_id of the call being undone. Legacy undo had no such parameter, so the alias cannot supply one. Read transaction_id from the response of the call you want to reverse."],
        );
      }
      return routed({ transaction_id: params.transaction_id });
    },
  },
  // --- Input mappings: four legacy names onto one reconciling command --------
  {
    name: "input_mapping_info",
    canonical: "puerts_input_mapping_info",
    description:
      "Inspect Unreal input action and axis mappings. The three filters pass through unchanged; " +
      "the native read is exact-match, as the legacy one was.",
    inputSchema: z.object({
      action_name: z.string().optional().describe("Exact action name to filter, such as PF_Pause"),
      axis_name: z.string().optional().describe("Exact axis name to filter"),
      key: z.string().optional().describe("Exact key name to filter, such as Escape"),
    }),
    translate: (params) => {
      const translated: Params = {};
      for (const key of ["action_name", "axis_name", "key"] as const) {
        if (params[key] !== undefined) translated[key] = params[key];
      }
      return routed(translated);
    },
  },
  {
    name: "input_mapping_add",
    canonical: "puerts_input_mapping_patch",
    description:
      "Add one action or axis mapping. kind selects which half of the native patch the mapping " +
      "goes into; a parameter belonging to the other half is refused rather than dropped, because " +
      "a scale silently ignored on an action mapping reads as a working binding that is not one.",
    inputSchema: z.object({
      kind: z.enum(["action", "axis"]),
      name: z.string().describe("Action or axis name, e.g. Jump, MoveForward."),
      key: z.string().describe("FKey name."),
      scale: z.number().optional().describe("Axis scale (axis only), default 1.0."),
      shift: z.boolean().optional().describe("Action only."),
      ctrl: z.boolean().optional().describe("Action only."),
      alt: z.boolean().optional().describe("Action only."),
      cmd: z.boolean().optional().describe("Action only."),
    }),
    translate: (params) => {
      const modifiers = ["shift", "ctrl", "alt", "cmd"] as const;
      if (params.kind === "axis") {
        const rejected = rejectSupplied(params, modifiers.map((name) => [
          name,
          `an axis mapping has no modifier keys. Unreal stores ${name} on FInputActionKeyMapping only; ` +
          "bind the modifier as its own action, or use kind \"action\".",
        ] as const));
        if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
        const axis: Params = { name: params.name, key: params.key };
        if (params.scale !== undefined) axis.scale = params.scale;
        return routed({ axes: [axis] });
      }
      const rejected = rejectSupplied(params, [
        ["scale", "an action mapping has no scale. Unreal stores Scale on FInputAxisKeyMapping only; use kind \"axis\"."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const action: Params = { name: params.name, key: params.key };
      for (const name of modifiers) {
        if (params[name] !== undefined) action[name] = params[name];
      }
      return routed({ actions: [action] });
    },
  },
  {
    name: "input_mapping_remove",
    canonical: "puerts_input_mapping_patch",
    description:
      "Remove action or axis mappings. Omit 'key' to remove every key bound to the name, exactly " +
      "as the legacy tool did. Never sets remove_unlisted, so nothing the call did not name is touched.",
    inputSchema: z.object({
      kind: z.enum(["action", "axis"]),
      name: z.string(),
      key: z.string().optional(),
    }),
    translate: (params) => {
      const entry: Params = { name: params.name };
      if (params.key !== undefined) entry.key = params.key;
      return routed(params.kind === "axis" ? { remove_axes: [entry] } : { remove_actions: [entry] });
    },
  },
  {
    name: "input_preset_apply",
    canonical: "puerts_input_mapping_patch",
    description:
      "Apply a standard control scheme preset. The four preset names and their bindings are the " +
      "legacy handler's, unchanged, so an old prompt gets the bindings it always did. Additive: " +
      "existing mappings the preset does not name are left alone.",
    inputSchema: z.object({
      preset: z.enum(["first_person", "third_person", "top_down", "tank"]),
    }),
    translate: (params) => routed({ preset: params.preset }),
  },
  // --- Content Browser folder visibility ------------------------------------
  {
    name: "folder_hide",
    canonical: "puerts_folder_visibility",
    description:
      "Hide /Game subfolders in the Content Browser (display-only). folder and folders both " +
      "collapse into the native hide list.",
    inputSchema: z.object({
      folder: z.string().optional().describe("Single folder path, e.g. /Game/HorrorEngine"),
      folders: z.array(z.string()).optional().describe("Multiple folder paths to hide at once"),
    }),
    translate: (params) => {
      const folders = collectFolders(params);
      if (folders.length === 0) {
        return unmappable(
          ["folder"],
          ["folder: folder_hide needs 'folder' or 'folders'. An empty hide list would be a call that asks for nothing."],
        );
      }
      return routed({ hide: folders });
    },
  },
  {
    name: "folder_show",
    canonical: "puerts_folder_visibility",
    description:
      "Unhide Content Browser folders. Called with no arguments it unhides EVERYTHING, which the " +
      "native command expresses as the empty desired set (hidden: []) rather than as a separate " +
      "show-all verb.",
    inputSchema: z.object({
      folder: z.string().optional().describe("Single folder path to unhide"),
      folders: z.array(z.string()).optional().describe("Multiple folder paths to unhide"),
    }),
    translate: (params) => {
      const folders = collectFolders(params);
      // The legacy escape hatch: no arguments meant unhide everything.
      return routed(folders.length === 0 ? { hidden: [] } : { show: folders });
    },
  },
  {
    name: "folder_hidden_list",
    canonical: "puerts_folder_visibility",
    description:
      "List Content Browser folders currently hidden. The native command with no hidden, hide or " +
      "show is the read: it changes nothing and answers with the hidden set.",
    inputSchema: z.object({}),
    translate: () => routed({}),
  },
  // --- Cloth, read half only ------------------------------------------------
  {
    name: "cloth_inspect_asset",
    canonical: "puerts_cloth_inspect",
    description:
      "Inspect a skeletal mesh's cloth setup: render-section bindings, physical cloth LOD counts, " +
      "authoring masks, NvCloth config, topology hash and Physics Asset coverage. The parameter " +
      "passes through unchanged. Only the READ half of the cloth optimizer is native; its three " +
      "writers keep their legacy names because a failed apply does not yet restore the mask.",
    inputSchema: z.object({
      skeletal_mesh: z.string().describe("Path to the skeletal mesh"),
    }),
    translate: (params) => routed({ skeletal_mesh: params.skeletal_mesh }),
  },
  // --- Navigation build -----------------------------------------------------
  {
    name: "ai_nav_rebuild",
    canonical: "puerts_nav_build",
    description:
      "Rebuild navigation data in the loaded level. The legacy tool ran the RebuildNavigation " +
      "console command and blocked until it finished, so this alias routes to wait: true, which " +
      "is the same UNavigationSystemV1::Build the editor's Build Paths runs. That blocks the game " +
      "thread, and on a large level it can outlast the 30 second pipe deadline: the build still " +
      "completes in the editor, but the call reports a timeout. Call puerts_nav_build directly " +
      "with wait: false for the non-blocking form that answers immediately and is polled with " +
      "puerts_nav_inspect. Takes no parameters, as the legacy tool did.",
    inputSchema: z.object({}),
    translate: () => routed({ wait: true }),
  },
  // --- Camera shake ---------------------------------------------------------
  {
    name: "camera_shake_play",
    canonical: "puerts_camera_shake",
    description:
      "Play a camera shake on the PIE player camera. play_space has no native equivalent and is " +
      "refused: the native chain uses the shake asset's own play space, and quietly ignoring a " +
      "requested one would report a World-space shake that played in camera space.",
    inputSchema: z.object({
      shake_class: z.string().describe("Path to a CameraShake class (a Blueprint's runtime path usually ends in _C)"),
      scale: z.number().optional().describe("Intensity multiplier, default 1.0"),
      play_space: z.string().optional().describe("Not supported natively; supplying it fails the call"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["play_space", "puerts_camera_shake calls StartCameraShake with the shake asset's own play space. There is no native override."],
      ]);
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const translated: Params = { shake_class: params.shake_class };
      if (params.scale !== undefined) translated.scale = params.scale;
      return routed(translated);
    },
  },
  // --- PIE agent, read-only half --------------------------------------------
  {
    name: "pie_agent_observe",
    canonical: "puerts_pie_agent_query",
    description:
      "Capture a PIE snapshot: pawn transform and velocity, nearby actors with physics and " +
      "collision state, widgets, player counters, and the Output Log tail. Every parameter passes " +
      "through unchanged.",
    inputSchema: z.object({
      radius: z.number().positive().optional().describe("Actor scan radius around the pawn in uu (default 1500)"),
      class_filter: z.string().optional().describe("Only include actors whose class contains this text"),
      log_lines: z.number().int().min(0).optional().describe("Output Log tail length (default 20)"),
    }),
    translate: (params) => {
      const translated: Params = { op: "observe" };
      for (const key of ["radius", "class_filter", "log_lines"] as const) {
        if (params[key] !== undefined) translated[key] = params[key];
      }
      return routed(translated);
    },
  },
  {
    name: "pie_agent_status",
    canonical: "puerts_pie_agent_query",
    description:
      "Poll the current PIE agent asynchronous operation. Operation id 0 means the latest operation.",
    inputSchema: z.object({
      operation_id: z.number().int().nonnegative().optional(),
    }),
    translate: (params) => {
      const translated: Params = { op: "status" };
      if (params.operation_id !== undefined) translated.operation_id = params.operation_id;
      return routed(translated);
    },
  },
  {
    name: "pie_agent_expect",
    canonical: "puerts_pie_agent_query",
    description:
      "Start an in-engine check of declarative actor_count, counter and log regex conditions. " +
      "ONE DIFFERENCE FROM THE LEGACY TOOL, and it is in the return rather than the request: the " +
      "legacy tool blocked in the MCP server until the conditions passed or the deadline expired, " +
      "and answered with the verdict. The native command starts the check and answers with " +
      "operation_id and status \"running\". Poll pie_agent_status for the verdict. The blocking " +
      "loop is orchestration and belongs in the caller, not in a command that occupies the game " +
      "thread while it waits.",
    inputSchema: z.object({
      conditions: z.array(z.string()).min(1),
      within_seconds: z.number().positive().optional().describe("Deadline in seconds (default 5)"),
    }),
    translate: (params) => {
      const translated: Params = { op: "expect", conditions: params.conditions };
      if (params.within_seconds !== undefined) translated.within_seconds = params.within_seconds;
      return routed(translated);
    },
  },

  // --- Blueprint members: eleven legacy names onto one batch command --------
  memberAlias(
    "blueprint_add_variable", "add_variable",
    "Add a member variable to a Blueprint, then compile and save. default_value is the native "
    + "command's default; type is the same descriptor, unchanged.",
    {
      name: z.string().describe("Variable name."),
      type: blueprintTypeDescriptor,
      default_value: z.unknown().optional(),
      category: z.string().optional().describe("Editor category."),
    },
    (params) => {
      const op = carry(params, ["name", "type", "category"]);
      if (params.default_value !== undefined) op.default = params.default_value;
      return op;
    },
  ),
  memberAlias(
    "blueprint_remove_variable", "remove_variable",
    "Remove a member variable from a Blueprint, then compile and save. An inherited or native "
    + "property is refused by name rather than removed.",
    { name: z.string().describe("Variable name to remove.") },
    (params) => carry(params, ["name"]),
  ),
  memberAlias(
    "blueprint_set_variable_default", "set_variable_default",
    "Set a member variable's default value, then compile and save. A value the variable's type "
    + "cannot hold is refused before anything is written.",
    {
      name: z.string().describe("Variable name."),
      default_value: z.unknown().describe("New default value (JSON-serializable)."),
    },
    (params) => ({ name: params.name, default: params.default_value }),
  ),
  memberAlias(
    "blueprint_add_function", "add_function",
    "Create a user function graph with typed inputs and outputs, then compile and save.",
    {
      name: z.string().describe("Function name."),
      inputs: blueprintParamList.optional(),
      outputs: blueprintParamList.optional(),
    },
    (params) => carry(params, ["name", "inputs", "outputs"]),
  ),
  memberAlias(
    "blueprint_remove_function", "remove_function",
    "Remove a user function graph by name, then compile and save.",
    { name: z.string().describe("Function name to remove.") },
    (params) => carry(params, ["name"]),
  ),
  memberAlias(
    "blueprint_add_event_dispatcher", "add_event_dispatcher",
    "Create an event dispatcher with an optional parameter signature, then compile and save. The "
    + "legacy signature object holds one key, parameters, which is what the native operation takes "
    + "directly.",
    {
      name: z.string().describe("Dispatcher name."),
      signature: z.object({ parameters: blueprintParamList }).optional(),
    },
    (params) => {
      const op = carry(params, ["name"]);
      const signature = params.signature as { parameters?: unknown } | undefined;
      if (signature?.parameters !== undefined) op.parameters = signature.parameters;
      return op;
    },
  ),
  memberAlias(
    "blueprint_remove_event_dispatcher", "remove_event_dispatcher",
    "Remove an event dispatcher by name, then compile and save.",
    { name: z.string().describe("Dispatcher name to remove.") },
    (params) => carry(params, ["name"]),
  ),
  memberAlias(
    "blueprint_add_interface", "add_interface",
    "Implement a Blueprint Interface, then compile and save. interface_class is the native "
    + "operation's path; a class that loads but is not an interface is refused by name.",
    { interface_class: z.string().describe("Interface class path.") },
    (params) => ({ path: params.interface_class }),
  ),
  memberAlias(
    "blueprint_remove_interface", "remove_interface",
    "Remove an interface implementation and its generated function graphs, then compile and save.",
    { interface_class: z.string().describe("Interface class path.") },
    (params) => ({ path: params.interface_class }),
  ),
  memberAlias(
    "blueprint_component_remove", "remove_component",
    "Remove a component (SCS node); children re-parent to the removed node's parent. Compiles and "
    + "saves.",
    { component_name: z.string().describe("Component variable name.") },
    (params) => ({ name: params.component_name }),
  ),
  memberAlias(
    "blueprint_component_rename", "rename_component",
    "Rename a component (SCS node), then compile and save. A rename whose target name is already "
    + "taken is refused; a rename already applied is reported unchanged rather than repeated.",
    { old_name: z.string(), new_name: z.string() },
    (params) => ({ from: params.old_name, to: params.new_name }),
  ),

  // --- Blueprint graph: node and pin editing onto the batch patch -----------
  {
    name: "blueprint_node_add",
    canonical: "puerts_blueprint_graph_patch",
    description:
      "Add one node to a Blueprint graph. node_type and config become the native node spec. "
      + "TWO DIFFERENCES FROM THE LEGACY TOOL. First, the native builder supports a different node "
      + "vocabulary: CreateWidget, MacroInstance, SwitchEnum, SwitchName, InputAction, "
      + "InputAxisEvent, AddDelegate, RemoveDelegate and CallDelegate have no native factory and "
      + "are refused by name rather than silently producing nothing. Second, the return is the "
      + "patch response, which describes the created node, not a bare GUID; read the node back "
      + "with puerts_graph_inspect if a later call needs to address it. "
      + "Three config keys are respelled for the native params object: CallFunction targetClass "
      + "and functionName become class and function, Sequence numOutputs becomes num_outputs, and "
      + "InputKey key becomes fkey_name. Every other key passes through untouched.",
    inputSchema: z.object({
      blueprint_path: legacyBlueprintPath,
      graph_name: z.string().describe("Target graph, e.g. EventGraph or a function name."),
      node_type: z.string().describe("Node factory name."),
      position: z.tuple([z.number(), z.number()]).optional().describe("Graph position [x, y]."),
      config: z.record(z.unknown()).optional().describe("Node-type-specific configuration."),
    }),
    translate: (params) => {
      const nodeType = params.node_type as string;
      const unsupported = UNSUPPORTED_NODE_TYPES[nodeType];
      if (unsupported !== undefined) {
        return unmappable(
          ["node_type"],
          [`node_type: puerts_blueprint_graph_patch cannot add a ${nodeType} node: ${unsupported}`],
        );
      }
      const renames = NODE_CONFIG_RENAMES[nodeType] ?? {};
      const config = (params.config ?? {}) as Params;
      const nodeParams: Params = {};
      for (const [key, value] of Object.entries(config)) {
        nodeParams[renames[key] ?? key] = value;
      }
      // The native command requires new_id so later operations in the same
      // batch can address the node. A one-node batch has no later operation,
      // so the name only has to be stable.
      const newId = "compat_added_node";
      const node: Params = { id: newId, type: nodeType };
      if (Object.keys(nodeParams).length > 0) node.params = nodeParams;
      if (Array.isArray(params.position)) {
        const [x, y] = params.position as [number, number];
        node.x = x;
        node.y = y;
      }
      return routed({
        asset_path: params.blueprint_path,
        graph: params.graph_name,
        operations: [{ op: "add_node", new_id: newId, node }],
      });
    },
  },
  {
    name: "blueprint_node_delete",
    canonical: "puerts_blueprint_graph_patch",
    description:
      "Delete a node by GUID, links broken first, then compile and save. The GUID becomes the "
      + "native selector {node_guid}, which must resolve to exactly one node or the call is a "
      + "refusal naming what it matched.",
    inputSchema: z.object({
      blueprint_path: legacyBlueprintPath,
      graph_name: z.string(),
      node_guid: z.string(),
    }),
    translate: (params) => routed({
      asset_path: params.blueprint_path,
      graph: params.graph_name,
      operations: [{ op: "remove_node", target: { node_guid: params.node_guid } }],
    }),
  },
  {
    name: "blueprint_node_move",
    canonical: "puerts_blueprint_graph_patch",
    description:
      "Move a node to a new graph position, then compile and save. A node already at that "
      + "position is reported unchanged and the package is not dirtied, which the legacy tool did "
      + "not do.",
    inputSchema: z.object({
      blueprint_path: legacyBlueprintPath,
      graph_name: z.string(),
      node_guid: z.string(),
      position: z.tuple([z.number(), z.number()]).describe("Graph position [x, y]."),
    }),
    translate: (params) => {
      const [x, y] = params.position as [number, number];
      return routed({
        asset_path: params.blueprint_path,
        graph: params.graph_name,
        operations: [{ op: "move_node", target: { node_guid: params.node_guid }, x, y }],
      });
    },
  },
  {
    name: "blueprint_pins_connect",
    canonical: "puerts_blueprint_graph_patch",
    description:
      "Connect two pins between nodes. The connection is validated by "
      + "UEdGraphSchema_K2::CanCreateConnection before linking, and a batch that cannot make the "
      + "link rolls back rather than leaving the graph half-wired.",
    inputSchema: z.object({
      blueprint_path: legacyBlueprintPath,
      graph_name: z.string(),
      source_node_guid: z.string().describe("GUID of the output-side node."),
      source_pin: z.string().describe("Output pin name, e.g. then, ReturnValue."),
      target_node_guid: z.string().describe("GUID of the input-side node."),
      target_pin: z.string().describe("Input pin name, e.g. execute, Condition."),
    }),
    translate: (params) => routed({
      asset_path: params.blueprint_path,
      graph: params.graph_name,
      operations: [{
        op: "connect_pins",
        from: { target: { node_guid: params.source_node_guid }, pin: params.source_pin },
        to: { target: { node_guid: params.target_node_guid }, pin: params.target_pin },
      }],
    }),
  },
];

/** The legacy folder pair took folder, folders, or both. */
function collectFolders(params: Params): string[] {
  const folders: string[] = [];
  if (typeof params.folder === "string" && params.folder.length > 0) folders.push(params.folder);
  if (Array.isArray(params.folders)) {
    for (const entry of params.folders as unknown[]) {
      if (typeof entry === "string" && entry.length > 0 && !folders.includes(entry)) folders.push(entry);
    }
  }
  return folders;
}

/** alias name -> the puerts_* tool that executes it. Consumed by
    Scripts/generate-tool-inventory.mjs to record target_replacement. */
export const compatAliasTargets: Readonly<Record<string, string>> = Object.freeze(
  Object.fromEntries(aliases.map((alias) => [alias.name, alias.canonical])),
);

export function createCompatTools(client: PuerTSClient): ToolDefinition[] {
  return aliases.map((alias) => {
    // Resolve now: a typo in a canonical name is a startup failure, not a
    // runtime surprise on the first call.
    const spec = nativeToolSpec(alias.canonical);
    const stamp = (payload: Record<string, unknown>): { content: Array<{ type: "text"; text: string }> } => ({
      content: [{
        type: "text" as const,
        text: JSON.stringify({
          ...payload,
          requested_tool: alias.name,
          canonical_tool: alias.canonical,
          backend: "named_pipe",
          compat: true,
        }, null, 2),
      }],
    });

    return {
      name: alias.name,
      description: `[COMPAT ALIAS -> ${alias.canonical}] ${alias.description}`,
      inputSchema: alias.inputSchema,
      annotations: compatAliasAnnotations[alias.name],
      handler: async (params: Record<string, unknown>) => {
        let parsed: Params;
        try {
          parsed = alias.inputSchema.parse(params) as Params;
        } catch (error: unknown) {
          return stamp(nativeFailureEnvelope(
            [error instanceof Error ? error.message : String(error)],
            `Legacy parameters rejected by the ${alias.name} compatibility schema.`,
          ));
        }
        const translation = alias.translate(parsed);
        if (!translation.ok) {
          const payload = nativeFailureEnvelope(
            translation.reasons,
            `${alias.name} cannot be routed to ${alias.canonical}: ` +
            `${translation.parameters.join(", ")} ${translation.parameters.length === 1 ? "has" : "have"} no native equivalent.`,
          );
          payload.unmapped_parameters = translation.parameters;
          return stamp(payload);
        }
        payloadShapeGuard(translation.params);
        return stamp(await executeNativeCommand(client, spec, translation.params));
      },
    };
  });
}

/** Translations must produce plain objects; anything else means a translate
    function returned something the native schema would silently mangle. */
function payloadShapeGuard(params: Params): void {
  if (params === null || typeof params !== "object" || Array.isArray(params)) {
    throw new Error("Compat translation produced a non-object parameter set");
  }
}

export interface CompatRegistrationOptions {
  readonly env?: NodeJS.ProcessEnv;
  readonly warn?: (message: string) => void;
}

/**
 * The aliases index.ts should append, given the tools already registered.
 *
 * Returns [] unless MCP_COMPAT_ALIASES=1. An alias whose name a registered
 * tool already owns is skipped with a warning, mirroring how a colliding
 * project-local extension is skipped: a compatibility shim must never shadow
 * the real tool, and a collision must never be fatal.
 */
export function registerCompatAliases(
  registered: readonly ToolDefinition[],
  client: PuerTSClient,
  options: CompatRegistrationOptions = {},
): ToolDefinition[] {
  const env = options.env ?? process.env;
  if (env.MCP_COMPAT_ALIASES !== "1") return [];
  const warn = options.warn ?? ((message: string) => console.error(message));
  const taken = new Set(registered.map((tool) => tool.name));
  const accepted: ToolDefinition[] = [];
  for (const alias of createCompatTools(client)) {
    if (taken.has(alias.name)) {
      warn(
        `[Unreal MCP Bridge] compat alias "${alias.name}" skipped: a registered tool already owns that name. ` +
        `Call ${compatAliasTargets[alias.name]} for the native path.`,
      );
      continue;
    }
    taken.add(alias.name);
    accepted.push(alias);
  }
  if (accepted.length > 0) {
    warn(`[Unreal MCP Bridge] MCP_COMPAT_ALIASES=1: ${accepted.length} legacy name(s) routed to native tools.`);
  }
  return accepted;
}
