/**
 * C++ generation and build tools, plus the async job API.
 *
 * cpp_build runs UnrealBuildTool as a background job inside the listener
 * process; poll job_status for structured compiler errors. Building the
 * editor target while the editor is open reports compile errors normally
 * but may fail at link with locked DLLs (reported distinctly); loading new
 * classes requires an editor restart.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

const parentClassEnum = z.enum([
  "Actor", "Pawn", "Character", "GameModeBase", "GameStateBase",
  "PlayerController", "PlayerState", "HUD", "AIController",
  "ActorComponent", "SceneComponent", "GameInstance", "SaveGame",
  "Object", "BTTaskNode", "BTService", "BlueprintFunctionLibrary",
]);

export function createCppTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "cpp_class_create",
      description:
        "Generate a UCLASS header/source pair in a game module (UE4.27 wizard-style " +
        "boilerplate with GENERATED_BODY and constructor). Does not compile; run " +
        "cpp_build afterwards. New classes require an editor restart to load.",
      inputSchema: z.object({
        name: z.string().describe("Class name WITHOUT the A/U prefix, e.g. DoorActor."),
        parent_class: parentClassEnum.default("Actor").describe("Base class."),
        module: z.string().optional().describe("Game module name. Default: first module in the .uproject."),
        subdir: z.string().optional().describe("Subdirectory under Public/Private (or module root)."),
        uclass_specifiers: z.string().optional().describe("UCLASS() specifiers, e.g. 'Blueprintable'."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cpp_class_create", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "cpp_build",
      description:
        "Compile the project with UnrealBuildTool as a background job. Returns a job_id " +
        "immediately; poll job_status for progress and structured compiler errors " +
        "(file, line, code, message).",
      inputSchema: z.object({
        target: z.string().optional().describe("Build target. Default: <ProjectName>Editor."),
        platform: z.string().optional().describe("Default Win64."),
        configuration: z.string().optional().describe("Default Development."),
        extra_args: z.array(z.string()).optional().describe("Extra UBT arguments."),
        timeout_seconds: z.number().positive().optional().describe("Job kill deadline. Default 1800."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("cpp_build", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "job_status",
      description:
        "Get a background job's status (running/succeeded/failed/cancelled), structured " +
        "result (e.g. parsed compiler errors for cpp_build), and output tail.",
      inputSchema: z.object({
        job_id: z.string(),
        include_output: z.boolean().optional().describe("Include the output tail. Default true."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("job_status", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "job_list",
      description: "List all background jobs started in this editor session, newest first.",
      inputSchema: z.object({}),
      handler: async (params) => {
        const result = await client.sendCommand("job_list", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "job_cancel",
      description: "Cancel a running background job (terminates its subprocess).",
      inputSchema: z.object({
        job_id: z.string(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("job_cancel", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
