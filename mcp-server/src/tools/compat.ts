/**
 * Compatibility alias router.
 *
 * Twelve legacy HTTP tool names kept alive as router-level aliases onto the
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
    name: "actor_spawn",
    canonical: "puerts_spawn_actor",
    description:
      "Spawn an actor from an asset path at a given location/rotation. asset_path becomes class_path. " +
      "scale, name and folder have no native equivalent and are refused rather than dropped.",
    inputSchema: z.object({
      asset_path: z.string().describe("Asset or class path (e.g. /Game/Meshes/SM_Cube, /Script/Engine.StaticMeshActor)"),
      location: vector.optional().describe("World location"),
      rotation: rotation.optional().describe("Rotation"),
      scale: vector.optional().describe("Not supported natively; supplying it fails the call"),
      name: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      folder: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      validate: z.boolean().optional().describe("Native spawn always validates; only true is accepted"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["scale", "puerts_spawn_actor spawns at unit scale. Set the scale afterwards with puerts_set_property on the root SceneComponent (RelativeScale3D)."],
        ["name", "puerts_spawn_actor does not label the spawned actor. Rename it with puerts_call_function Actor.SetActorLabel."],
        ["folder", "puerts_spawn_actor cannot place the actor in a World Outliner folder. There is no native folder command."],
      ]);
      if (params.validate === false) {
        rejected.parameters.push("validate");
        rejected.reasons.push("validate: puerts_spawn_actor always validates the spawn; validation cannot be disabled.");
      }
      if (rejected.parameters.length > 0) return unmappable(rejected.parameters, rejected.reasons);
      const translated: Params = { class_path: params.asset_path };
      if (params.location !== undefined) translated.location = params.location;
      if (params.rotation !== undefined) translated.rotation = params.rotation;
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
      "List actors in the current level. class_filter becomes type, name_filter becomes name, limit passes through. " +
      "Native filters are substring matches, so wildcard patterns and folder filtering are refused.",
    inputSchema: z.object({
      class_filter: z.string().optional().describe("Class name substring. Wildcards (* ?) are not supported natively."),
      name_filter: z.string().optional().describe("Actor label substring. Wildcards (* ?) are not supported natively."),
      folder_filter: z.string().optional().describe("Not supported natively; supplying it fails the call"),
      include_transforms: z.boolean().optional().describe("Native results always include transforms; only true is accepted"),
      include_components: z.boolean().optional().describe("Not supported natively; supplying it fails the call"),
      limit: z.number().int().min(1).optional().describe("Max actors to return"),
    }),
    translate: (params) => {
      const rejected = rejectSupplied(params, [
        ["folder_filter", "puerts_find_actors has no folder filter. Filter the returned actors client-side."],
      ]);
      if (params.include_components === true) {
        rejected.parameters.push("include_components");
        rejected.reasons.push("include_components: puerts_find_actors returns no component list. Read components with puerts_read_property.");
      }
      if (params.include_transforms === false) {
        rejected.parameters.push("include_transforms");
        rejected.reasons.push("include_transforms: puerts_find_actors always returns the native actor snapshot; transforms cannot be excluded.");
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
      if (params.limit !== undefined) translated.limit = params.limit;
      return routed(translated);
    },
  },
  {
    name: "level_save",
    canonical: "puerts_save",
    description:
      "Save the current level through the native save command. save_all has no native equivalent: " +
      "puerts_save takes an explicit asset list instead.",
    inputSchema: z.object({
      save_all: z.boolean().optional().describe("Not supported natively; supplying true fails the call"),
    }),
    translate: (params) => {
      if (params.save_all === true) {
        return unmappable(
          ["save_all"],
          ["save_all: puerts_save never saves every dirty asset. Pass the explicit asset paths to puerts_save (assets) or use the asset_save_many alias."],
        );
      }
      // No assets and no level_path means "save the current level" natively.
      return routed({});
    },
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
];

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
