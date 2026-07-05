/**
 * Game development workflow tools: input mappings, gameplay framework
 * creation, camera rigs, and AI assets (Blackboard + Behavior Tree).
 *
 * Input mutation and blackboard wiring go through the MCPBridgeInputLibrary
 * and MCPBridgeAILibrary C++ helpers (UE4.27 Python cannot construct FKey or
 * assign UBehaviorTree::BlackboardAsset).
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const blackboardKeySchema = z.object({
  name: z.string(),
  type: z.enum(["Bool", "Int", "Float", "String", "Name", "Vector", "Rotator", "Object", "Class"]),
  base_class: z.string().optional().describe("For Object/Class keys: base class path, e.g. /Script/Engine.Actor."),
});

export function createGamedevTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "input_mapping_add",
      description:
        "Add an action or axis mapping to project input settings (saved to " +
        "Config/DefaultInput.ini). Key names use FKey string form: W, SpaceBar, " +
        "LeftMouseButton, MouseX, Gamepad_LeftTrigger, ...",
      inputSchema: z.object({
        kind: z.enum(["action", "axis"]),
        name: z.string().describe("Action or axis name, e.g. Jump, MoveForward."),
        key: z.string().describe("FKey name."),
        scale: z.number().optional().describe("Axis scale (axis only), default 1.0."),
        shift: z.boolean().optional(),
        ctrl: z.boolean().optional(),
        alt: z.boolean().optional(),
        cmd: z.boolean().optional(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("input_mapping_add", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "input_mapping_remove",
      description: "Remove action or axis mappings. Omit 'key' to remove every key bound to the name.",
      inputSchema: z.object({
        kind: z.enum(["action", "axis"]),
        name: z.string(),
        key: z.string().optional(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("input_mapping_remove", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "input_preset_apply",
      description:
        "Apply a standard control scheme preset: first_person, third_person, top_down, " +
        "or tank (WASD movement, mouse look where applicable, Jump/Fire/Interact actions). Idempotent.",
      inputSchema: z.object({
        preset: z.enum(["first_person", "third_person", "top_down", "tank"]),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("input_preset_apply", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "gameplay_framework_create",
      description:
        "Create a linked set of gameplay framework Blueprints (GameMode, Pawn or " +
        "Character, PlayerController, GameInstance, HUD, GameState, PlayerState) named " +
        "BP_<base_name><suffix>, with the GameMode's class defaults wired to the created " +
        "pieces. Compiles and saves everything.",
      inputSchema: z.object({
        path: z.string().describe("Content directory, e.g. /Game/MyGame/Framework."),
        base_name: z.string().describe("Base name, e.g. 'MyGame'."),
        pieces: z.array(z.enum([
          "game_mode", "pawn", "character", "player_controller",
          "game_instance", "hud", "game_state", "player_state",
        ])).optional().describe("Default: game_mode, character, player_controller."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("gameplay_framework_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "project_settings_maps",
      description:
        "Set default maps and game mode: game default map, editor startup map, and/or " +
        "global default game mode. Persists to Config/DefaultEngine.ini.",
      inputSchema: z.object({
        game_default_map: z.string().optional().describe("Map path, e.g. /Game/Maps/Main.Main."),
        editor_startup_map: z.string().optional(),
        global_default_game_mode: z.string().optional()
          .describe("GameMode class path, e.g. /Game/Framework/BP_MyGameGM.BP_MyGameGM_C."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("project_settings_maps", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "camera_rig_create",
      description:
        "Add a camera rig to a Pawn/Character Blueprint using a preset: first_person " +
        "(eye-height camera, pawn control rotation), third_person (300 spring arm with lag), " +
        "top_down (800 arm at -60 pitch), or side_scroller (500 arm at -90 yaw). " +
        "Compiles and saves.",
      inputSchema: z.object({
        blueprint_path: z.string(),
        preset: z.enum(["first_person", "third_person", "top_down", "side_scroller"]),
        camera_name: z.string().optional().describe("Default 'Camera'."),
        arm_name: z.string().optional().describe("Default 'CameraBoom'."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("camera_rig_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blackboard_create",
      description: "Create a BlackboardData asset with typed keys (Bool, Int, Float, String, Name, Vector, Rotator, Object, Class).",
      inputSchema: z.object({
        path: z.string().describe("Content directory."),
        name: z.string().describe("Asset name, e.g. BB_Enemy."),
        keys: z.array(blackboardKeySchema).optional(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blackboard_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "behavior_tree_create",
      description:
        "Create a BehaviorTree asset bound to an existing Blackboard, optionally building " +
        "its node graph from a BehaviorTreeBuilder JSON spec (26 node types: Selector/" +
        "Sequence/SimpleParallel composites, MoveTo/Wait/etc tasks, Blackboard/Cooldown/etc " +
        "decorators, services). Optionally adds keys to the blackboard first.",
      inputSchema: z.object({
        path: z.string().describe("Content directory."),
        name: z.string().describe("Asset name, e.g. BT_Enemy."),
        blackboard_path: z.string().describe("Existing BlackboardData asset path."),
        keys: z.array(blackboardKeySchema).optional().describe("Keys to ensure on the blackboard first."),
        tree: z.record(z.unknown()).optional().describe("BehaviorTreeBuilder JSON spec (see docs/superpowers/specs/2026-03-19-behavior-tree-builder-design.md)."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("behavior_tree_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "ai_nav_rebuild",
      description: "Rebuild navigation data in the loaded level (RebuildNavigation console command).",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("ai_nav_rebuild", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
