/**
 * One-call isolated object inspection with dynamic camera fitting.
 */

import { execFile } from "child_process";
import { dirname, join } from "path";
import { fileURLToPath } from "url";
import { promisify } from "util";
import { z } from "zod";
import { UnrealClient, type UnrealResponse } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";
import { waitForScreenshotFile } from "./viewport.js";

const execFileAsync = promisify(execFile);
const viewSchema = z.enum(["front", "rear", "right", "left", "top", "bottom"]);
type InspectionView = z.infer<typeof viewSchema>;

const ALL_VIEWS: InspectionView[] = ["front", "rear", "right", "left", "top", "bottom"];

interface CaptureRecord {
  view: InspectionView;
  metadata: Record<string, unknown>;
  filepath: string;
  filename: string;
  resolution: Record<string, unknown>;
  fileSizeBytes: number;
}

function toolResponse(result: UnrealResponse | Record<string, unknown>): {
  content: Array<{ type: "text"; text: string }>;
} {
  return {
    content: [{ type: "text", text: JSON.stringify(result, null, 2) }],
  };
}

function safeFilename(value: string): string {
  const cleaned = value.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, "");
  return cleaned || "Inspection";
}

function failedStage(stage: string, result: UnrealResponse): Record<string, unknown> {
  return {
    success: false,
    data: { stage, bridge_data: result.data },
    error: result.error || `${stage} failed`,
  };
}

async function createContactSheet(
  captures: CaptureRecord[],
  outputPath: string
): Promise<{ filepath?: string; error?: string }> {
  if (captures.length < 2) {
    return {};
  }

  const here = dirname(fileURLToPath(import.meta.url));
  const scriptPath = join(here, "..", "..", "..", "scripts", "inspection_contact_sheet.ps1");
  const args = [
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    scriptPath,
    "-Output",
    outputPath,
    "-ImageList",
    captures.map((capture) => capture.filepath).join("|"),
  ];

  try {
    await execFileAsync(
      "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
      args,
      { timeout: 30000, windowsHide: true }
    );
    return { filepath: outputPath };
  } catch (error: unknown) {
    const message = error instanceof Error ? error.message : String(error);
    return { error: message };
  }
}

export function createInspectionTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "inspection_capture",
      description:
        "Rapidly inspect an asset in the dedicated blank level. Replaces the target, centers it by bounds, dynamically fits six cameras, captures selected labeled views, and optionally creates a contact sheet.",
      inputSchema: z.object({
        asset_path: z.string().startsWith("/").describe("Asset to inspect, such as a Static Mesh, Skeletal Mesh, or Blueprint"),
        target_name: z.string().optional().describe("Readable target label used in capture filenames"),
        rotation: z.object({
          pitch: z.number(),
          yaw: z.number(),
          roll: z.number(),
        }).optional().describe("Object rotation before bounds are measured"),
        scale: z.object({
          x: z.number().refine((value) => value !== 0, "Scale X cannot be zero"),
          y: z.number().refine((value) => value !== 0, "Scale Y cannot be zero"),
          z: z.number().refine((value) => value !== 0, "Scale Z cannot be zero"),
        }).optional().describe("Object scale before bounds are measured"),
        padding: z.number().min(1.01).max(3).optional().describe("Camera framing padding, default 1.18"),
        mode: z.enum(["rapid", "detail"]).optional().describe("Rapid defaults to 960x540, detail defaults to 1920x1080"),
        views: z.array(viewSchema).min(1).max(6).optional().describe("Views to capture, default all six"),
        resolution: z.object({
          width: z.number().int().positive(),
          height: z.number().int().positive(),
        }).optional().describe("Override the mode resolution"),
        contact_sheet: z.boolean().optional().describe("Create one tiled PNG from multiple views, default true"),
        save_current_level: z.boolean().optional().describe("Save the current level before switching to the inspection level, default true"),
        cleanup_after: z.boolean().optional().describe("Remove the temporary target after capture, default true"),
      }),
      handler: async (params) => {
        const openResult = await client.sendCommand("inspection_open", {
          save_current_level: params.save_current_level ?? true,
        });
        if (!openResult.success) {
          return toolResponse(failedStage("inspection_open", openResult));
        }

        const prepareResult = await client.sendCommand("inspection_prepare", {
          asset_path: params.asset_path,
          target_name: params.target_name,
          rotation: params.rotation,
          scale: params.scale,
          padding: params.padding,
        });
        if (!prepareResult.success) {
          return toolResponse(failedStage("inspection_prepare", prepareResult));
        }

        const mode = params.mode === "detail" ? "detail" : "rapid";
        const resolution = (params.resolution as { width: number; height: number } | undefined) ??
          (mode === "detail" ? { width: 1920, height: 1080 } : { width: 960, height: 540 });
        const views = (params.views as InspectionView[] | undefined) ?? ALL_VIEWS;
        const targetName = safeFilename(String(params.target_name || "Inspection"));
        const sessionId = new Date().toISOString().replace(/[-:.TZ]/g, "").slice(0, 14);
        const captures: CaptureRecord[] = [];

        for (const view of views) {
          const viewResult = await client.sendCommand("inspection_view", { view });
          if (!viewResult.success) {
            return toolResponse(failedStage(`inspection_view:${view}`, viewResult));
          }

          const filename = `${targetName}_${sessionId}_${view}_${mode}.png`;
          const screenshotResult = await client.sendCommand("viewport_screenshot", {
            filename,
            resolution,
            show_ui: false,
          });
          if (!screenshotResult.success) {
            return toolResponse(failedStage(`viewport_screenshot:${view}`, screenshotResult));
          }
          await waitForScreenshotFile(screenshotResult.data);

          const filepath = screenshotResult.data.filepath;
          if (typeof filepath !== "string") {
            return toolResponse({
              success: false,
              data: { stage: `viewport_screenshot:${view}` },
              error: "Screenshot response did not include a filepath",
            });
          }

          captures.push({
            view,
            metadata: viewResult.data,
            filepath,
            filename: String(screenshotResult.data.filename || filename),
            resolution: (screenshotResult.data.resolution as Record<string, unknown>) || resolution,
            fileSizeBytes: Number(screenshotResult.data.file_size_bytes || 0),
          });
        }

        let contactSheet: { filepath?: string; error?: string } = {};
        if ((params.contact_sheet ?? true) && captures.length > 1) {
          const outputPath = join(
            dirname(captures[0].filepath),
            `${targetName}_${sessionId}_${mode}_ContactSheet.png`
          );
          contactSheet = await createContactSheet(captures, outputPath);
        }

        let cleanupResult: UnrealResponse | null = null;
        if (params.cleanup_after ?? true) {
          cleanupResult = await client.sendCommand("inspection_cleanup", {});
        }

        return toolResponse({
          success: true,
          data: {
            mode,
            target: prepareResult.data.target,
            coordinate_system: prepareResult.data.coordinate_system,
            camera_distances: prepareResult.data.camera_distances,
            captures,
            contact_sheet: contactSheet.filepath || null,
            contact_sheet_error: contactSheet.error || null,
            cleanup: cleanupResult?.data || null,
            inspection_level: openResult.data.level_path,
          },
        });
      },
    },
    {
      name: "inspection_status",
      description: "Report whether the isolated inspection level and Blueprint rig are ready, including target and camera metadata.",
      inputSchema: z.object({}),
      handler: async (params) => toolResponse(await client.sendCommand("inspection_status", params)),
    },
    {
      name: "inspection_cleanup",
      description: "Remove temporary inspection targets and hide all camera labels while preserving the reusable rig.",
      inputSchema: z.object({}),
      handler: async (params) => toolResponse(await client.sendCommand("inspection_cleanup", params)),
    },
  ];
}
