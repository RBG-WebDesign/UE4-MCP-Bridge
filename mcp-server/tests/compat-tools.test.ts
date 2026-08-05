/**
 * Compatibility alias router tests.
 *
 * Covers the four things that make an alias safe to hand an old prompt:
 * it is invisible unless the flag is set, it never shadows a real tool, it
 * translates legacy parameters onto the native schema and reaches the native
 * command over the pipe, and it refuses an unmappable parameter loudly.
 *
 * A mock named-pipe server stands in for the editor, so no UE4 is needed.
 */

import { mkdtemp, rm, writeFile } from "node:fs/promises";
import { createServer, type Server } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { PuerTSClient } from "../src/puerts-client.js";
import { advertiseSession } from "./session-fixture.js";
import {
  compatAliasTargets,
  createCompatTools,
  registerCompatAliases,
} from "../src/tools/compat.js";
import { createPuertsTools } from "../src/tools/puerts.js";
import type { ToolDefinition } from "../src/types.js";

let passed = 0;
const failures: string[] = [];

function assert(condition: boolean, message: string): void {
  if (condition) {
    passed += 1;
    return;
  }
  failures.push(message);
}

interface PipeRequest { tool?: string; command?: string; params?: Record<string, unknown>; auth?: string }

/** Payload of an alias call: the native JSON plus the compat wrapper fields. */
async function call(tool: ToolDefinition, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const result = await tool.handler(params);
  return JSON.parse(result.content[0]?.text ?? "null") as Record<string, unknown>;
}

/** Key order is decided by the native schema's parse, not by the translation,
    so compare parameter sets by content. */
function stable(value: unknown): string {
  return JSON.stringify(value, (_key, inner: unknown) => {
    if (inner === null || typeof inner !== "object" || Array.isArray(inner)) return inner;
    const record = inner as Record<string, unknown>;
    return Object.fromEntries(Object.keys(record).sort().map((key) => [key, record[key]]));
  });
}

function toolNamed(tools: ToolDefinition[], name: string): ToolDefinition {
  const tool = tools.find((candidate) => candidate.name === name);
  if (tool === undefined) throw new Error(`compat alias missing: ${name}`);
  return tool;
}

async function main(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-compat-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-compat-test-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  // The client resolves and verifies a session on every call, so the alias
  // routing this suite measures cannot be reached without one.
  const { responseSession } = await advertiseSession(pipeName);

  const seen: PipeRequest[] = [];
  const server: Server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as PipeRequest;
    seen.push(request);
    const responseData = request.command === "pie_agent_control"
      && ["move_to", "press", "replay"].includes(String(request.params?.op))
      && request.params?.export !== true
      ? { operation_id: 7, status: "running" }
      : request.command === "pie_agent_query" && request.params?.op === "status"
        ? seen.some((entry) => entry.command === "pie_agent_control" && entry.params?.actor === "CancelTarget")
          ? { status: "cancelled", error: "cancelled by caller" }
          : { status: "succeeded", result: { completed: true } }
        : request.command === "data_table_build" ? {
          object_path: "/Game/MCPGenerated/DT_Items.DT_Items",
          row_struct: "/Script/GameplayTags.GameplayTagTableRow",
          row_names: ["Row1"],
          row_count: 1,
          saved: true,
        }
        : request.command === "blueprint_build" ? {
          asset_path: request.params?.asset_path,
          compiled: true,
          compile_status: "UpToDate",
        }        : request.command === "graph_inspect" ? {
          graphs: [{ name: "EventGraph" }],
          functions: [{ name: "Use" }],
          variables: [{ name: "Health" }],
          interfaces: [{ path: "/Game/BPI_Use" }],
          event_dispatchers: [{ name: "OnUsed" }],
          components: [{ name: "Root" }],
        }
      : request.command === "scene_inspect"
        ? { actors: [
            { label: "A", transform: { location: { x: 0, y: 0, z: 0 } }, bounds: { extent: { x: 1, y: 0, z: 0 } } },
            { label: "B", transform: { location: { x: 5, y: 0, z: 0 } }, bounds: { extent: { x: 1, y: 0, z: 0 } } },
            { label: "Near", transform: { location: { x: 0.5, y: 0, z: 0 } }, bounds: { extent: { x: 0, y: 0, z: 0 } } },
            { label: "Duplicate", transform: { location: { x: 0, y: 0, z: 0 } }, bounds: { extent: { x: 0, y: 0, z: 0 } } },
            { label: "Duplicate", transform: { location: { x: 1, y: 0, z: 0 } }, bounds: { extent: { x: 0, y: 0, z: 0 } } },
          ] }
      : { echoed_command: request.command };
    socket.end(JSON.stringify({
      session: responseSession,
      success: true,
      message: "Native command executed.",
      data: responseData,
      changed_assets: [],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "tx-1",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const client = new PuerTSClient();
    const native = createPuertsTools(client);
    const compat = createCompatTools(client);

    // --- catalog shape ----------------------------------------------------
    // A literal on purpose: this is the canary that catches an alias silently
    // disappearing. Raise it deliberately when a wave adds legacy names.
    const ALIAS_COUNT = 63;
    assert(compat.length === ALIAS_COUNT, `expected ${ALIAS_COUNT} compat aliases, got ${compat.length}`);
    assert(
      Object.keys(compatAliasTargets).length === ALIAS_COUNT,
      `compatAliasTargets must name all ${ALIAS_COUNT} aliases for the inventory generator`,
    );
    const nativeNames = new Set(native.map((tool) => tool.name));
    const badTargets = Object.entries(compatAliasTargets)
      .filter(([, canonical]) => !nativeNames.has(canonical))
      .map(([alias, canonical]) => `${alias} -> ${canonical}`);
    assert(badTargets.length === 0, `alias targets that are not native tools: ${badTargets.join(", ")}`);
    assert(
      compat.every((tool) => tool.annotations !== undefined),
      "every alias must carry the annotations of the native tool it fronts",
    );

    // --- registration is gated by MCP_COMPAT_ALIASES ----------------------
    const off = registerCompatAliases([], client, { env: {}, warn: () => {} });
    assert(off.length === 0, "aliases must not register without MCP_COMPAT_ALIASES=1");
    const wrongValue = registerCompatAliases([], client, { env: { MCP_COMPAT_ALIASES: "true" }, warn: () => {} });
    assert(wrongValue.length === 0, "only the exact value 1 enables aliases");
    const on = registerCompatAliases(native, client, { env: { MCP_COMPAT_ALIASES: "1" }, warn: () => {} });
    assert(on.length === ALIAS_COUNT, `expected ${ALIAS_COUNT} aliases with the flag set, got ${on.length}`);

    // --- a name a real tool already owns is skipped, not fatal ------------
    const warnings: string[] = [];
    const occupied: ToolDefinition[] = [
      ...native,
      { name: "actor_spawn", description: "legacy HTTP tool", inputSchema: compat[0].inputSchema, handler: async () => ({ content: [] }) },
    ];
    const withCollision = registerCompatAliases(occupied, client, {
      env: { MCP_COMPAT_ALIASES: "1" },
      warn: (message) => warnings.push(message),
    });
    assert(
      withCollision.length === ALIAS_COUNT - 1,
      `collision must skip exactly the colliding alias, got ${withCollision.length}`,
    );
    assert(
      !withCollision.some((tool) => tool.name === "actor_spawn"),
      "the colliding alias must not be registered",
    );
    assert(
      warnings.some((message) => message.includes("actor_spawn") && message.includes("skipped")),
      "a skipped alias must warn on stderr naming the tool",
    );

    // --- actor_spawn translates and routes to the native spawn path -------
    seen.length = 0;
    const spawn = await call(toolNamed(compat, "actor_spawn"), {
      asset_path: "/Game/Meshes/SM_Cube",
      location: { x: 10, y: 20, z: 30 },
      rotation: { pitch: 0, yaw: 90, roll: 0 },
    });
    assert(seen.length === 1, "actor_spawn must make exactly one native call");
    assert(seen[0]?.command === "spawn_actor", `actor_spawn must route to spawn_actor, got ${seen[0]?.command}`);
    assert(seen[0]?.auth === "test-token", "the alias must authenticate like any native call");
    const spawnParams = seen[0]?.params ?? {};
    assert(spawnParams.class_path === "/Game/Meshes/SM_Cube", "asset_path must be translated to class_path");
    assert(spawnParams.asset_path === undefined, "the legacy parameter name must not reach the runtime");
    assert(
      JSON.stringify(spawnParams.location) === JSON.stringify({ x: 10, y: 20, z: 30 }),
      "location must pass through unchanged",
    );
    assert(
      JSON.stringify(spawnParams.rotation) === JSON.stringify({ pitch: 0, yaw: 90, roll: 0 }),
      "rotation must pass through unchanged",
    );

    // --- the wrapper fields are on the native result ----------------------
    assert(spawn.success === true, "a routed call must return the native result");
    assert(spawn.requested_tool === "actor_spawn", "requested_tool must name the alias");
    assert(spawn.canonical_tool === "puerts_spawn_actor", "canonical_tool must name the native tool");
    assert(spawn.backend === "named_pipe", "backend must report the native transport");
    assert(spawn.compat === true, "compat must mark the result as routed");
    assert(spawn.transaction_id === "tx-1", "the native envelope must survive the wrapping");

    // --- other translations reach the right command -----------------------
    const routings: Array<[string, Record<string, unknown>, string, Record<string, unknown>]> = [
      ["blueprint_info", { blueprint_path: "/Game/MCPGenerated/BP_A" }, "graph_inspect", { asset_path: "/Game/MCPGenerated/BP_A" }],
      ["placement_validate", { actors: ["A", "B", "Missing"], gap_threshold: 2, overlap_threshold: 1 },
        "scene_inspect", { actors: ["A", "B", "Missing"], include_components: false }],
      ["blueprint_inspect", { blueprint_path: "/Game/MCPGenerated/BP_A", action: "variables" },
        "graph_inspect", { asset_path: "/Game/MCPGenerated/BP_A" }],
      ["anim_blueprint_build_from_json", {
        package_path: "/Game/MCPGenerated/Anim", asset_name: "ABP_Guard",
        skeleton_path: "/Game/Characters/SK_Guard", json_spec: JSON.stringify({
          anim_graph: { pipeline: [{ id: "sm", type: "StateMachine", name: "Locomotion" }] },
          state_machine: { states: [{ id: "idle", name: "Idle", animation: "/Game/Anim/A_Idle", is_entry: true }], transitions: [] },
        }),
      }, "anim_blueprint_build", {
        asset_path: "/Game/MCPGenerated/Anim/ABP_Guard", skeleton_path: "/Game/Characters/SK_Guard",
        anim_graph: { pipeline: [{ id: "sm", type: "StateMachine", name: "Locomotion" }] },
        state_machine: { states: [{ id: "idle", name: "Idle", animation: "/Game/Anim/A_Idle", is_entry: true }], transitions: [] },
      }],
      ["widget_build_from_json", { package_path: "/Game/MCPGenerated/UI/", asset_name: "WBP_HUD", widget_json: { type: "CanvasPanel", name: "Root" } },
        "widget_build", { asset_path: "/Game/MCPGenerated/UI/WBP_HUD", tree: { root: { type: "CanvasPanel", name: "Root" } } }],
      ["widget_build_from_json", { package_path: "/Game/MCPGenerated/UI", asset_name: "WBP_HUD", widget_json: { root: { type: "CanvasPanel", name: "Root" }, animations: [] } },
        "widget_build", { asset_path: "/Game/MCPGenerated/UI/WBP_HUD", tree: { root: { type: "CanvasPanel", name: "Root" }, animations: [] } }],
      ["behavior_tree_create", { path: "/Game/MCPGenerated/AI/", name: "BT_Guard", blackboard_path: "/Game/MCPGenerated/AI/BB_Guard", keys: [{ name: "Target", type: "Object", base_class: "/Script/Engine.Actor" }], tree: { id: "root", type: "Selector" } },
        "behavior_tree_build", { asset_path: "/Game/MCPGenerated/AI/BT_Guard", blackboard_path: "/Game/MCPGenerated/AI/BB_Guard", keys: [{ name: "Target", type: "Object", base_class: "/Script/Engine.Actor" }], root: { id: "root", type: "Selector" } }],
      ["blackboard_create", { path: "/Game/MCPGenerated/AI", name: "BB_Guard", keys: [{ name: "Alerted", type: "Bool" }] },
        "blackboard_build", { asset_path: "/Game/MCPGenerated/AI/BB_Guard", keys: [{ name: "Alerted", type: "Bool" }] }],
      ["viewport_fit", { actor_names: ["Door", "Frame"] }, "viewport_screenshot", { actors: ["Door", "Frame"] }],
      ["viewport_focus", { actor_name: "Door" }, "viewport_screenshot", { actors: ["Door"] }],
      ["actor_delete", { actor_name: "Cube_1" }, "delete_actor", { actor: "Cube_1", confirm: true }],
      ["actor_modify", { actor_name: "Cube_1", visible: false }, "set_property", { actor: "Cube_1", property: "bHidden", value: true }],
      ["level_actors", { class_filter: "StaticMeshActor", name_filter: "Cube", limit: 5 }, "find_actors", { type: "StaticMeshActor", name: "Cube", include_transforms: true, limit: 5 }],
      // The capability legacy shipped and the native migration dropped. Both
      // parameters are native again, under their legacy names, and the alias
      // states the legacy default for include_transforms rather than inheriting
      // the native one.
      ["level_actors", { folder_filter: "Courtyard", include_transforms: false }, "find_actors", { folder_filter: "Courtyard", include_transforms: false }],
      ["actor_spawn", { asset_path: "/Script/Engine.StaticMeshActor", name: "Pillar_NE", folder: "Courtyard/Structure", scale: { x: 1, y: 1, z: 4 } },
        "spawn_actor", { class_path: "/Script/Engine.StaticMeshActor", name: "Pillar_NE", folder: "Courtyard/Structure", scale: { x: 1, y: 1, z: 4 } }],
      ["level_save", {}, "level_save", { save_all: false }],
      ["level_save", { save_all: true }, "level_save", { save_all: true }],
      ["level_new", { path: "/Game/Maps/NewMap" }, "level_create", { level_path: "/Game/Maps/NewMap" }],
      ["level_new", { path: "/Game/Maps/Copy", template: "/Game/Maps/Template" }, "level_create",
        { level_path: "/Game/Maps/Copy", template_path: "/Game/Maps/Template" }],
      ["asset_save_many", { paths: "/Game/Maps/Test" }, "save", { assets: ["/Game/Maps/Test"] }],
      ["asset_list", { path: "/Game/Meshes", asset_type: "StaticMesh", name_pattern: "SM_" }, "find_assets", { path: "/Game/Meshes", type: "StaticMesh", name: "SM_" }],
      ["viewport_screenshot", { filename: "shot.png" }, "viewport_screenshot", { filename: "shot.png" }],
      ["gameplay_pie_start", {}, "pie_start", {}],
      ["gameplay_pie_stop", {}, "pie_stop", {}],
      ["ue_logs", { maximum_lines: 25 }, "get_logs", { maximum_lines: 25 }],
      ["undo", { transaction_id: "tx-9" }, "undo", { transaction_id: "tx-9" }],

      // Config and non-asset state. The shapes worth pinning are the ones a
      // reader would have to re-derive: an axis add carries scale and no
      // modifiers, a remove without a key removes every key bound to the name,
      // folder_show with no arguments means unhide everything, and
      // folder_hidden_list is the read because it sends no set and no delta.
      ["input_mapping_info", { action_name: "PF_Pause" }, "input_mapping_info", { action_name: "PF_Pause" }],
      ["input_mapping_add", { kind: "action", name: "Jump", key: "SpaceBar", shift: true },
        "input_mapping_patch", { actions: [{ name: "Jump", key: "SpaceBar", shift: true }] }],
      ["input_mapping_add", { kind: "axis", name: "MoveForward", key: "S", scale: -1 },
        "input_mapping_patch", { axes: [{ name: "MoveForward", key: "S", scale: -1 }] }],
      ["input_mapping_remove", { kind: "action", name: "Jump" },
        "input_mapping_patch", { remove_actions: [{ name: "Jump" }] }],
      ["input_preset_apply", { preset: "first_person" }, "input_mapping_patch", { preset: "first_person" }],
      ["folder_hide", { folders: ["/Game/A", "/Game/B"] }, "folder_visibility", { hide: ["/Game/A", "/Game/B"] }],
      ["folder_show", {}, "folder_visibility", { hidden: [] }],
      ["folder_hidden_list", {}, "folder_visibility", {}],
      ["camera_shake_play", { shake_class: "/Game/CS.CS_C", scale: 2 },
        "camera_shake", { shake_class: "/Game/CS.CS_C", scale: 2 }],
      ["actor_organize", { actors: ["A", "B"], folder: "Lighting/Interior" }, "scene_batch", {
        operations: [
          { op: "upsert_actor", select: { label: "A" }, folder: "Lighting/Interior" },
          { op: "upsert_actor", select: { label: "B" }, folder: "Lighting/Interior" },
        ], verify: true,
      }],
      ["batch_spawn", { spawns: [{ asset_path: "/Script/Engine.StaticMeshActor", name: "Pillar", location: { x: 1, y: 2, z: 3 } }] }, "scene_batch", {
        operations: [{ op: "upsert_actor", class: "/Script/Engine.StaticMeshActor", label: "Pillar", location: { x: 1, y: 2, z: 3 } }], verify: true,
      }],
      ["batch_operations", { operations: [{ command: "actor_delete", params: { actor_name: "OldPillar" } }] }, "scene_batch", {
        operations: [{ op: "delete_actor", select: { label: "OldPillar" } }], verify: true,
      }],
      ["pie_agent_observe", { radius: 800 }, "pie_agent_query", { op: "observe", radius: 800 }],
      ["pie_agent_status", { operation_id: 3 }, "pie_agent_query", { op: "status", operation_id: 3 }],
      ["pie_agent_expect", { conditions: ["actor_count:Cube:1"] },
        "pie_agent_query", { op: "expect", conditions: ["actor_count:Cube:1"] }],
      ["project_settings_maps", { game_default_map: "/Game/Maps/Main.Main" },
        "project_settings_maps", { game_default_map: "/Game/Maps/Main.Main" }],
      ["data_table_create", {
        path: "/Game/MCPGenerated", name: "DT_Items",
        row_struct: "/Script/GameplayTags.GameplayTagTableRow", rows_json: [{ Name: "Row1", Tag: "Items.Key" }],
      }, "data_table_build", {
        asset_path: "/Game/MCPGenerated/DT_Items",
        row_struct: "/Script/GameplayTags.GameplayTagTableRow", rows_json: [{ Name: "Row1", Tag: "Items.Key" }],
      }],
      ["data_table_fill_from_json", {
        data_table: "/Game/MCPGenerated/DT_Items.DT_Items", rows_json: "[]",
      }, "data_table_build", { asset_path: "/Game/MCPGenerated/DT_Items", rows_json: "[]" }],
      ["pie_agent_look_at", { actor: "BP_Target" },
        "pie_agent_control", { op: "look_at", actor: "BP_Target" }],
      ["pie_agent_record_start", { events: ["spawn"], max_events: 64 },
        "pie_agent_control", { op: "record_start", events: ["spawn"], max_events: 64 }],
      ["pie_agent_record_stop", {}, "pie_agent_control", { op: "record_stop" }],
      ["pie_agent_replay", { export: true },
        "pie_agent_control", { op: "replay", export: true }],

      // Blueprint members: one legacy call is one operation of the batch
      // command. The four with renamed parameters are the ones pinned here.
      ["blueprint_add_variable",
        { blueprint_path: "/Game/MCPGenerated/BP_A", name: "Health", type: { category: "float" }, default_value: 5 },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "add_variable", name: "Health", type: { category: "float" }, default: 5 }] }],
      ["blueprint_set_variable_default",
        { blueprint_path: "/Game/MCPGenerated/BP_A", name: "Health", default_value: 7 },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "set_variable_default", name: "Health", default: 7 }] }],
      ["blueprint_add_interface",
        { blueprint_path: "/Game/MCPGenerated/BP_A", interface_class: "/Game/BPI_X.BPI_X_C" },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "add_interface", path: "/Game/BPI_X.BPI_X_C" }] }],
      ["blueprint_add_event_dispatcher",
        { blueprint_path: "/Game/MCPGenerated/BP_A", name: "OnHit", signature: { parameters: [{ name: "Amount", type: { category: "float" } }] } },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "add_event_dispatcher", name: "OnHit", parameters: [{ name: "Amount", type: { category: "float" } }] }] }],
      ["blueprint_component_rename",
        { blueprint_path: "/Game/MCPGenerated/BP_A", old_name: "Mesh", new_name: "Body" },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "rename_component", from: "Mesh", to: "Body" }] }],
      ["blueprint_component_remove",
        { blueprint_path: "/Game/MCPGenerated/BP_A", component_name: "Mesh" },
        "blueprint_member_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", operations: [{ op: "remove_component", name: "Mesh" }] }],

      // Blueprint graph: the node_guid becomes a selector, and the three
      // respelled config keys are the reason this alias is not a pass-through.
      ["blueprint_node_add",
        { blueprint_path: "/Game/MCPGenerated/BP_A", graph_name: "EventGraph", node_type: "CallFunction", position: [10, 20], config: { targetClass: "/Script/Engine.KismetSystemLibrary", functionName: "PrintString" } },
        "blueprint_graph_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", graph: "EventGraph", operations: [{ op: "add_node", new_id: "compat_added_node", node: { id: "compat_added_node", type: "CallFunction", params: { class: "/Script/Engine.KismetSystemLibrary", function: "PrintString" }, x: 10, y: 20 } }] }],
      ["blueprint_node_delete",
        { blueprint_path: "/Game/MCPGenerated/BP_A", graph_name: "EventGraph", node_guid: "abc" },
        "blueprint_graph_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", graph: "EventGraph", operations: [{ op: "remove_node", target: { node_guid: "abc" } }] }],
      ["blueprint_node_move",
        { blueprint_path: "/Game/MCPGenerated/BP_A", graph_name: "EventGraph", node_guid: "abc", position: [3, 4] },
        "blueprint_graph_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", graph: "EventGraph", operations: [{ op: "move_node", target: { node_guid: "abc" }, x: 3, y: 4 }] }],
      ["blueprint_pins_connect",
        { blueprint_path: "/Game/MCPGenerated/BP_A", graph_name: "EventGraph", source_node_guid: "a", source_pin: "then", target_node_guid: "b", target_pin: "execute" },
        "blueprint_graph_patch",
        { asset_path: "/Game/MCPGenerated/BP_A", graph: "EventGraph", operations: [{ op: "connect_pins", from: { target: { node_guid: "a" }, pin: "then" }, to: { target: { node_guid: "b" }, pin: "execute" } }] }],
    ];
    for (const [alias, input, command, expected] of routings) {
      seen.length = 0;
      const payload = await call(toolNamed(compat, alias), input);
      assert(seen[0]?.command === command, `${alias} must route to ${command}, got ${seen[0]?.command}`);
      assert(
        stable(seen[0]?.params) === stable(expected),
        `${alias} translated to ${JSON.stringify(seen[0]?.params)}, expected ${JSON.stringify(expected)}`,
      );
      assert(payload.requested_tool === alias && payload.compat === true, `${alias} result is missing the compat wrapper`);
      assert(payload.canonical_tool === compatAliasTargets[alias], `${alias} reported the wrong canonical_tool`);
    }

    seen.length = 0;
    const legacyBlueprintBuild = await call(toolNamed(compat, "blueprint_build_from_json"), {
      blueprint_path: "/Game/MCPGenerated/BP_A.BP_A",
      graph: {
        nodes: [
          { id: "begin", type: "BeginPlay" },
          { id: "print", type: "CallFunction", function: "PrintString", params: { InString: "Hello" } },
        ],
        connections: [{ from: "begin.exec", to: "print.exec" }],
      },
      clear_existing: true,
    });
    assert(seen.length === 2, "blueprint_build_from_json must inspect once, then build once");
    assert(seen[0]?.command === "graph_inspect", "blueprint_build_from_json must preserve the legacy existing-asset precondition");
    assert(stable(seen[0]?.params) === stable({ asset_path: "/Game/MCPGenerated/BP_A" }),
      "blueprint_build_from_json must normalize an object path before inspection");
    assert(seen[1]?.command === "blueprint_build", "blueprint_build_from_json must use the native Blueprint builder");
    assert(stable(seen[1]?.params) === stable({
      asset_path: "/Game/MCPGenerated/BP_A",
      graph: {
        nodes: [
          { id: "begin", type: "BeginPlay" },
          { id: "print", type: "CallFunction", params: { class: "KismetSystemLibrary", function: "PrintString", InString: "Hello" } },
        ],
        connections: [{ from: "begin.exec", to: "print.exec" }],
      },
      clear_existing_graph: true,
    }), `blueprint_build_from_json translated to ${JSON.stringify(seen[1]?.params)}`);
    const legacyBuildData = legacyBlueprintBuild.data as Record<string, unknown>;
    assert(legacyBuildData.path === "/Game/MCPGenerated/BP_A.BP_A", "legacy Blueprint path must survive the result adapter");
    assert(legacyBuildData.compiled === true, "legacy Blueprint build must report the native compile verdict");
    assert(legacyBuildData.compile_method === "MCPBridgeGraphBuilder", "legacy Blueprint build must identify the native compile path");
    // These three legacy tools blocked until their native operation reached a
    // terminal state. Preserve that envelope by polling the existing query op.
    for (const [alias, input, expectedControl] of [
      ["pie_agent_move_to", { actor: "BP_Target", timeout: 1 }, { op: "move_to", actor: "BP_Target", timeout: 1 }],
      ["pie_agent_press", { action: "Interact", hold_seconds: 0 }, { op: "press", action: "Interact", hold_seconds: 0 }],
      ["pie_agent_replay", { script: [], budget_seconds: 1 }, { op: "replay", script: [], budget_seconds: 1 }],
    ] as const) {
      seen.length = 0;
      const payload = await call(toolNamed(compat, alias), input);
      assert(seen.length === 2, `${alias} must make one control call and one status call`);
      assert(seen[0]?.command === "pie_agent_control", `${alias} must start through pie_agent_control`);
      assert(stable(seen[0]?.params) === stable(expectedControl), `${alias} control params changed shape`);
      assert(seen[1]?.command === "pie_agent_query", `${alias} must poll through pie_agent_query`);
      assert(stable(seen[1]?.params) === stable({ op: "status", operation_id: 7 }),
        `${alias} status params changed shape`);
      const terminal = payload.data as Record<string, unknown>;
      assert(terminal.operation_id === 7 && terminal.completed === true,
        `${alias} must restore the legacy terminal envelope`);
    }

    seen.length = 0;
    const strictRefusal = await call(toolNamed(compat, "pie_agent_look_at"), {
      actor: "BP_Target", unsupported: true,
    });
    assert(strictRefusal.success === false, "PIE aliases must reject unsupported parameters loudly");
    assert(seen.length === 0, "a rejected PIE alias must make zero pipe calls");
    const emptySettings = await call(toolNamed(compat, "project_settings_maps"), {});
    assert(emptySettings.success === false, "project_settings_maps must require at least one setting");
    assert(seen.length === 0, "rejected empty project settings must make zero pipe calls");

    seen.length = 0;
    const cancelled = await call(toolNamed(compat, "pie_agent_move_to"), {
      actor: "CancelTarget", timeout: 1,
    });
    assert(seen.length === 2, "a cancelled PIE operation must stop after its terminal status");
    assert(cancelled.success === false, "a cancelled PIE operation must preserve a failed legacy verdict");

    const inspect = await call(toolNamed(compat, "blueprint_inspect"), {
      blueprint_path: "/Game/MCPGenerated/BP_A",
      action: "variables",
    });
    assert(
      stable(inspect.data) === stable({ action: "variables", result: [{ name: "Health" }] }),
      `blueprint_inspect must retain the legacy action/result shape, got ${JSON.stringify(inspect.data)}`,
    );

    const placement = await call(toolNamed(compat, "placement_validate"), {
      actors: ["A", "B", "Missing"], gap_threshold: 2, overlap_threshold: 1,
    });
    assert(
      stable(placement.data) === stable({
        actors_checked: 2,
        not_found: ["Missing"],
        issues: [{ type: "gap", actors: ["A", "B"], gap: 3, threshold: 2 }],
        issue_count: 1,
      }),
      `placement_validate must preserve legacy gap results, got ${JSON.stringify(placement.data)}`,
    );
    const duplicate = await call(toolNamed(compat, "placement_validate"), { actors: ["Duplicate", "A"] });
    assert(duplicate.success === false, "placement_validate must refuse duplicate actor labels");
    assert(
      Array.isArray(duplicate.errors) && duplicate.errors.some((error) => String(error).includes("matched 2 actors")),
      `placement_validate duplicate-label refusal must explain the ambiguity, got ${JSON.stringify(duplicate.errors)}`,
    );
    // --- unmappable parameters fail loud, and never reach the editor ------
    const unmappable: Array<[string, Record<string, unknown>, string]> = [
      ["behavior_tree_create", { path: "/Game/MCPGenerated/AI", name: "BT_Empty", blackboard_path: "/Game/MCPGenerated/AI/BB_Guard" }, "tree"],
      ["viewport_fit", {}, "actor_names"],
      ["blueprint_inspect", { blueprint_path: "/Game/MCPGenerated/BP_A", action: "node_detail", graph_name: "EventGraph", node_guid: "abc" }, "action"],
      ["anim_blueprint_build_from_json", { package_path: "/Game/MCPGenerated/Anim", asset_name: "ABP_Bad", skeleton_path: "/Game/Skeletons/SK", json_spec: "[" }, "json_spec"],
      ["viewport_fit", { actor_names: ["Door"], padding: 1.5 }, "padding"],
      ["viewport_focus", { actor_name: "Door", distance: 500 }, "distance"],
      ["undo", { count: 1 }, "transaction_id"],
      ["undo", { count: 3, transaction_id: "tx-9" }, "count"],
      ["actor_spawn", { asset_path: "/Game/Meshes/SM_Cube", validate: false }, "validate"],
      ["actor_modify", { actor_name: "Cube_1", location: { x: 1, y: 2, z: 3 } }, "location"],
      ["actor_delete", { actor_name: "Cube_*" }, "actor_name"],
      ["level_actors", { include_components: true }, "include_components"],
      ["level_actors", { name_filter: "Cube_*" }, "name_filter"],

      ["viewport_screenshot", { resolution: { width: 640 } }, "resolution"],
      ["viewport_screenshot", { show_ui: true }, "show_ui"],
      ["gameplay_pie_start", { level_path: "/Game/Maps/Test" }, "level_path"],
      ["ue_logs", { severity: "Error" }, "severity"],
      ["camera_shake_play", { shake_class: "/Game/CS.CS_C", play_space: "World" }, "play_space"],
      ["input_mapping_add", { kind: "axis", name: "MoveForward", key: "W", shift: true }, "shift"],
      ["input_mapping_add", { kind: "action", name: "Jump", key: "SpaceBar", scale: 2 }, "scale"],
      ["folder_hide", {}, "folder"],
      // A node type the native builder has no factory for is refused by name.
      // Silently dropping it would build a graph that compiles and does nothing.
      ["blueprint_node_add",
        { blueprint_path: "/Game/MCPGenerated/BP_A", graph_name: "EventGraph", node_type: "InputAction" },
        "node_type"],
      ["batch_operations", { operations: [{ command: "project_index_rebuild", params: {} }] }, "operations"],
      ["batch_operations", { operations: [{ command: "actor_delete", params: { actor_name: "Old_*" } }] }, "operations"],
    ];
    for (const [alias, input, parameter] of unmappable) {
      seen.length = 0;
      const payload = await call(toolNamed(compat, alias), input);
      assert(payload.success === false, `${alias} with ${parameter} must fail rather than drop it`);
      assert(seen.length === 0, `${alias} with an unmappable ${parameter} must not reach the editor`);
      const unmapped = payload.unmapped_parameters;
      assert(
        Array.isArray(unmapped) && unmapped.includes(parameter),
        `${alias} must name ${parameter} in unmapped_parameters, got ${JSON.stringify(unmapped)}`,
      );
      assert(
        typeof payload.message === "string" && payload.message.includes(compatAliasTargets[alias]),
        `${alias} failure must name the canonical tool ${compatAliasTargets[alias]}`,
      );
      assert(
        Array.isArray(payload.errors) && payload.errors.length > 0,
        `${alias} failure must explain the parameter in errors`,
      );
      assert(payload.requested_tool === alias && payload.compat === true, `${alias} failure is missing the compat wrapper`);
    }

    // A parameter the compat schema does not know at all is a schema failure,
    // still structured and still without touching the editor.
    seen.length = 0;
    const rejected = await call(toolNamed(compat, "level_save"), { save_all: "yes" });
    assert(rejected.success === false, "a type-invalid legacy parameter must fail");
    assert(seen.length === 0, "a schema failure must not reach the editor");
    assert(rejected.requested_tool === "level_save", "a schema failure still reports the alias");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }

  for (const failure of failures) console.error(`  FAIL  ${failure}`);
  console.log(`  ${failures.length === 0 ? "PASS" : "FAIL"}  compat alias router (${passed} assertions, ${failures.length} failed)`);
  if (failures.length > 0) process.exit(1);
}

main().catch((error: unknown) => {
  console.error(`  FAIL  ${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
});
