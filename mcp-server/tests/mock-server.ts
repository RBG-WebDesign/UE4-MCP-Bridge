/**
 * Mock HTTP server that mimics the Python listener's responses.
 *
 * Used by unit tests to validate MCP server behavior without
 * needing a running UE4 instance.
 */

import http from "http";

interface MockActor {
  name: string;
  class: string;
  location: { x: number; y: number; z: number };
  rotation: { pitch: number; yaw: number; roll: number };
  scale: { x: number; y: number; z: number };
  folder: string;
  materials?: string[];
}

interface MockMaterial {
  name: string;
  path: string;
  type: "material" | "instance";
  parent: string | null;
  parameters: {
    scalar: Array<{ name: string; value: number }>;
    vector: Array<{ name: string; value: number[] }>;
    texture: Array<{ name: string; value: string | null }>;
  };
}

interface MockBlueprint {
  name: string;
  path: string;
  parent_class: string;
  is_compiled: boolean;
  components: Array<{ name: string; class: string; parent: string | null; is_root: boolean }>;
  variables: Array<{ name: string; type: string; category: string; is_editable: boolean; tooltip: string }>;
  functions: Array<{ name: string; inputs: Array<{ name: string; type: string }>; outputs: Array<{ name: string; type: string }>; is_pure: boolean }>;
  event_graphs: string[];
}

interface MockResponse {
  success: boolean;
  data: Record<string, unknown>;
  error?: string;
}

type CommandHandler = (params: Record<string, unknown>) => MockResponse;

export class MockUnrealServer {
  private server: http.Server | null = null;
  private actors: Map<string, MockActor> = new Map();
  private materials: Map<string, MockMaterial> = new Map();
  private blueprints: Map<string, MockBlueprint> = new Map();
  private customHandlers: Map<string, CommandHandler> = new Map();
  private requestLog: Array<{ command: string; params: Record<string, unknown> }> = [];
  private nextActorId: number = 1;
  private renderMode: string = "lit";
  private cameraLocation: { x: number; y: number; z: number } = { x: 0, y: 0, z: 0 };
  private cameraRotation: { pitch: number; yaw: number; roll: number } = { pitch: 0, yaw: 0, roll: 0 };

  /**
   * Start the mock server on the given port.
   */
  async start(port: number = 8080): Promise<void> {
    this.seedMaterials();
    return new Promise((resolve, reject) => {
      this.server = http.createServer((req, res) => {
        let body = "";
        req.on("data", (chunk: Buffer) => {
          body += chunk.toString();
        });
        req.on("end", () => {
          try {
            const { command, params } = JSON.parse(body) as {
              command: string;
              params: Record<string, unknown>;
            };
            this.requestLog.push({ command, params });
            const response = this.handleCommand(command, params || {});
            res.writeHead(200, { "Content-Type": "application/json" });
            res.end(JSON.stringify(response));
          } catch (err) {
            res.writeHead(400, { "Content-Type": "application/json" });
            res.end(JSON.stringify({
              success: false,
              data: {},
              error: `Bad request: ${(err as Error).message}`,
            }));
          }
        });
      });

      this.server.on("error", reject);
      this.server.listen(port, "localhost", () => resolve());
    });
  }

  /**
   * Stop the mock server.
   */
  async stop(): Promise<void> {
    return new Promise((resolve) => {
      if (this.server) {
        this.server.close(() => resolve());
        this.server = null;
      } else {
        resolve();
      }
    });
  }

  /**
   * Register a custom handler for a command (overrides default behavior).
   */
  setHandler(command: string, handler: CommandHandler): void {
    this.customHandlers.set(command, handler);
  }

  /**
   * Get all requests received by the mock server.
   */
  getRequestLog(): Array<{ command: string; params: Record<string, unknown> }> {
    return [...this.requestLog];
  }

  /**
   * Clear request log and actors.
   */
  reset(): void {
    this.requestLog = [];
    this.actors.clear();
    this.materials.clear();
    this.blueprints.clear();
    this.customHandlers.clear();
    this.nextActorId = 1;
    this.seedMaterials();
  }

  private seedMaterials(): void {
    const baseMat: MockMaterial = {
      name: "M_BrickWall",
      path: "/Game/Materials/M_BrickWall",
      type: "material",
      parent: null,
      parameters: {
        scalar: [{ name: "Roughness", value: 0.8 }, { name: "Metallic", value: 0.0 }],
        vector: [{ name: "BaseColor", value: [0.6, 0.3, 0.2, 1.0] }],
        texture: [{ name: "DiffuseTexture", value: "/Game/Textures/T_Brick_D" }],
      },
    };
    this.materials.set(baseMat.path, baseMat);

    const inst1: MockMaterial = {
      name: "MI_BrickWall_Red",
      path: "/Game/Materials/Instances/MI_BrickWall_Red",
      type: "instance",
      parent: "/Game/Materials/M_BrickWall",
      parameters: {
        scalar: [{ name: "Roughness", value: 0.7 }],
        vector: [{ name: "BaseColor", value: [0.9, 0.1, 0.1, 1.0] }],
        texture: [],
      },
    };
    this.materials.set(inst1.path, inst1);

    const inst2: MockMaterial = {
      name: "MI_BrickWall_Blue",
      path: "/Game/Materials/Instances/MI_BrickWall_Blue",
      type: "instance",
      parent: "/Game/Materials/M_BrickWall",
      parameters: {
        scalar: [{ name: "Roughness", value: 0.5 }],
        vector: [{ name: "BaseColor", value: [0.1, 0.1, 0.9, 1.0] }],
        texture: [],
      },
    };
    this.materials.set(inst2.path, inst2);

    const metal: MockMaterial = {
      name: "M_Metal",
      path: "/Game/Materials/M_Metal",
      type: "material",
      parent: null,
      parameters: {
        scalar: [{ name: "Roughness", value: 0.2 }, { name: "Metallic", value: 1.0 }],
        vector: [],
        texture: [],
      },
    };
    this.materials.set(metal.path, metal);

    const metalInst: MockMaterial = {
      name: "MI_Metal_Gold",
      path: "/Game/Materials/Instances/MI_Metal_Gold",
      type: "instance",
      parent: "/Game/Materials/M_Metal",
      parameters: {
        scalar: [{ name: "Roughness", value: 0.3 }],
        vector: [{ name: "BaseColor", value: [1.0, 0.84, 0.0, 1.0] }],
        texture: [],
      },
    };
    this.materials.set(metalInst.path, metalInst);
  }

  /**
   * Get current actor state.
   */
  getActors(): Map<string, MockActor> {
    return new Map(this.actors);
  }

  private handleCommand(command: string, params: Record<string, unknown>): MockResponse {
    // Check for custom handler first
    const custom = this.customHandlers.get(command);
    if (custom) return custom(params);

    // Built-in handlers
    switch (command) {
      case "ping":
      case "test_connection":
        return this.handlePing();
      case "actor_spawn":
        return this.handleActorSpawn(params);
      case "actor_modify":
        return this.handleActorModify(params);
      case "actor_delete":
        return this.handleActorDelete(params);
      case "actor_duplicate":
        return this.handleActorDuplicate(params);
      case "actor_organize":
        return this.handleActorOrganize(params);
      case "batch_spawn":
        return this.handleBatchSpawn(params);
      case "level_actors":
        return this.handleLevelActors(params);
      case "level_save":
        return this.handleLevelSave(params);
      case "level_outliner":
        return this.handleLevelOutliner(params);
      case "viewport_screenshot":
        return this.handleViewportScreenshot(params);
      case "viewport_camera":
        return this.handleViewportCamera(params);
      case "viewport_mode":
        return this.handleViewportMode(params);
      case "viewport_focus":
        return this.handleViewportFocus(params);
      case "viewport_render_mode":
        return this.handleViewportRenderMode(params);
      case "viewport_bounds":
        return this.handleViewportBounds();
      case "viewport_fit":
        return this.handleViewportFit(params);
      case "viewport_look_at":
        return this.handleViewportLookAt(params);
      case "placement_validate":
        return this.handlePlacementValidate(params);
      case "material_list":
        return this.handleMaterialList(params);
      case "material_info":
        return this.handleMaterialInfo(params);
      case "material_create":
        return this.handleMaterialCreate(params);
      case "material_apply":
        return this.handleMaterialApply(params);
      case "blueprint_list":
        return this.handleBlueprintList(params);
      case "blueprint_info":
        return this.handleBlueprintInfo(params);
      case "blueprint_create":
        return this.handleBlueprintCreate(params);
      case "blueprint_compile":
        return this.handleBlueprintCompile(params);
      case "blueprint_document":
        return this.handleBlueprintDocument(params);
      case "begin_transaction":
      case "end_transaction":
        return { success: true, data: { action: command.split("_")[0] } };
      case "undo":
        return { success: true, data: { undone: (params.count as number) || 1 } };
      case "redo":
        return { success: true, data: { redone: (params.count as number) || 1 } };
      default:
        return {
          success: false,
          data: {},
          error: `Unknown command: '${command}'`,
        };
    }
  }

  private handlePing(): MockResponse {
    return {
      success: true,
      data: {
        status: "connected",
        engine_version: "4.27.2-mock",
        project: "MockProject",
        project_dir: "/mock/project",
        content_dir: "/mock/project/Content",
        platform: "MockUser",
      },
    };
  }

  private handleActorSpawn(params: Record<string, unknown>): MockResponse {
    const assetPath = params.asset_path as string;
    if (!assetPath) {
      return { success: false, data: {}, error: "Missing 'asset_path'" };
    }
    if (!assetPath.startsWith("/")) {
      return { success: false, data: {}, error: `Invalid asset_path: must start with '/' (got '${assetPath}')` };
    }

    const scale = params.scale as { x: number; y: number; z: number } | undefined;
    if (scale) {
      for (const axis of ["x", "y", "z"] as const) {
        if (scale[axis] === 0) {
          return { success: false, data: {}, error: `Scale ${axis} cannot be zero (would collapse the actor)` };
        }
      }
    }

    const loc = (params.location as { x: number; y: number; z: number }) || { x: 0, y: 0, z: 0 };
    const rot = (params.rotation as { pitch: number; yaw: number; roll: number }) || { pitch: 0, yaw: 0, roll: 0 };
    const actorScale = scale || { x: 1, y: 1, z: 1 };

    const name = (params.name as string) || `Actor_${this.nextActorId}`;
    const folder = (params.folder as string) || "";

    const actor: MockActor = {
      name,
      class: "StaticMeshActor",
      location: { ...loc },
      rotation: { ...rot },
      scale: { ...actorScale },
      folder,
    };
    this.actors.set(name, actor);
    this.nextActorId++;

    const data: Record<string, unknown> = { ...actor };

    if (params.validate !== false) {
      data.validation = { valid: true, errors: [] };
    }

    return { success: true, data };
  }

  private handleActorModify(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string;
    if (!actorName) {
      return { success: false, data: {}, error: "Missing 'actor_name'" };
    }

    const actor = this.actors.get(actorName);
    if (!actor) {
      return { success: false, data: {}, error: `Actor not found: ${actorName}` };
    }

    const modified: string[] = [];

    const loc = params.location as { x: number; y: number; z: number } | undefined;
    if (loc !== undefined) {
      actor.location = { ...loc };
      modified.push("location");
    }

    const rot = params.rotation as { pitch: number; yaw: number; roll: number } | undefined;
    if (rot !== undefined) {
      actor.rotation = { ...rot };
      modified.push("rotation");
    }

    const scale = params.scale as { x: number; y: number; z: number } | undefined;
    if (scale !== undefined) {
      for (const axis of ["x", "y", "z"] as const) {
        if (scale[axis] === 0) {
          return { success: false, data: {}, error: `Scale ${axis} cannot be zero (would collapse the actor)` };
        }
      }
      actor.scale = { ...scale };
      modified.push("scale");
    }

    if (modified.length === 0) {
      return { success: false, data: {}, error: "No properties to modify (provide location, rotation, scale, visible, or mesh)" };
    }

    const data: Record<string, unknown> = {
      modified_properties: modified,
      actor: { ...actor },
    };

    if (params.validate !== false) {
      data.validation = { valid: true, errors: [] };
    }

    return { success: true, data };
  }

  private handleActorDelete(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string;
    if (!actorName) {
      return { success: false, data: {}, error: "Missing 'actor_name'" };
    }

    const deleted: string[] = [];

    if (actorName.includes("*") || actorName.includes("?")) {
      // Simple wildcard matching
      const regex = new RegExp(
        "^" + actorName.replace(/\*/g, ".*").replace(/\?/g, ".") + "$"
      );
      for (const [name] of this.actors) {
        if (regex.test(name)) {
          deleted.push(name);
        }
      }
    } else {
      if (this.actors.has(actorName)) {
        deleted.push(actorName);
      }
    }

    if (deleted.length === 0) {
      return { success: false, data: {}, error: `No actors found matching: ${actorName}` };
    }

    for (const name of deleted) {
      this.actors.delete(name);
    }

    return {
      success: true,
      data: { deleted_count: deleted.length, deleted_actors: deleted },
    };
  }

  private handleActorDuplicate(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string;
    if (!actorName) {
      return { success: false, data: {}, error: "Missing 'actor_name'" };
    }

    const source = this.actors.get(actorName);
    if (!source) {
      return { success: false, data: {}, error: `Actor not found: ${actorName}` };
    }

    const offset = (params.offset as { x: number; y: number; z: number }) || undefined;
    const newName = (params.new_name as string) || `${actorName}_copy`;

    const newActor: MockActor = {
      name: newName,
      class: source.class,
      location: offset
        ? {
            x: source.location.x + offset.x,
            y: source.location.y + offset.y,
            z: source.location.z + offset.z,
          }
        : { ...source.location },
      rotation: { ...source.rotation },
      scale: { ...source.scale },
      folder: source.folder,
    };
    this.actors.set(newName, newActor);

    const data: Record<string, unknown> = { ...newActor };

    if (params.validate !== false && offset) {
      data.validation = { valid: true, errors: [] };
    }

    return { success: true, data };
  }

  private handleActorOrganize(params: Record<string, unknown>): MockResponse {
    const actorNames = params.actors as string[];
    const folder = params.folder as string;

    if (!actorNames || !Array.isArray(actorNames) || actorNames.length === 0) {
      return { success: false, data: {}, error: "Missing 'actors' list" };
    }
    if (!folder) {
      return { success: false, data: {}, error: "Missing 'folder' path" };
    }

    const moved: string[] = [];
    const notFound: string[] = [];

    for (const name of actorNames) {
      const actor = this.actors.get(name);
      if (actor) {
        actor.folder = folder;
        moved.push(name);
      } else {
        notFound.push(name);
      }
    }

    return {
      success: true,
      data: { moved, not_found: notFound, folder },
    };
  }

  private handleBatchSpawn(params: Record<string, unknown>): MockResponse {
    const spawns = params.spawns as Array<Record<string, unknown>>;
    if (!spawns || !Array.isArray(spawns) || spawns.length === 0) {
      return { success: false, data: {}, error: "'spawns' array is empty" };
    }

    const results: Array<Record<string, unknown>> = [];
    for (let i = 0; i < spawns.length; i++) {
      const result = this.handleActorSpawn(spawns[i]);
      results.push({
        index: i,
        success: result.success,
        data: result.data,
        error: result.error,
      });
    }

    const successCount = results.filter((r) => r.success).length;
    return {
      success: true,
      data: {
        total: spawns.length,
        succeeded: successCount,
        failed: spawns.length - successCount,
        results,
      },
    };
  }

  private handleLevelActors(params: Record<string, unknown>): MockResponse {
    const classFilter = (params.class_filter as string) || "";
    const folderFilter = (params.folder_filter as string) || "";
    const nameFilter = (params.name_filter as string) || "";
    const includeTransforms = params.include_transforms !== false;
    const includeComponents = params.include_components === true;
    let limit = (params.limit as number) || 500;
    if (limit < 1) limit = 500;

    const actors: Array<Record<string, unknown>> = [];
    for (const actor of this.actors.values()) {
      if (actors.length >= limit) break;

      if (classFilter && actor.class.toLowerCase() !== classFilter.toLowerCase()) {
        continue;
      }
      if (folderFilter && !actor.folder.startsWith(folderFilter)) {
        continue;
      }
      if (nameFilter) {
        if (nameFilter.includes("*") || nameFilter.includes("?")) {
          const regex = new RegExp("^" + nameFilter.replace(/\*/g, ".*").replace(/\?/g, ".") + "$");
          if (!regex.test(actor.name)) continue;
        } else {
          if (actor.name !== nameFilter) continue;
        }
      }

      const entry: Record<string, unknown> = {
        name: actor.name,
        class: actor.class,
        folder: actor.folder,
      };

      if (includeTransforms) {
        entry.location = { ...actor.location };
        entry.rotation = { ...actor.rotation };
        entry.scale = { ...actor.scale };
      }

      if (includeComponents) {
        entry.components = [
          { name: "StaticMeshComponent0", class: "StaticMeshComponent" },
          { name: "DefaultSceneRoot", class: "SceneComponent" },
        ];
      }

      actors.push(entry);
    }

    // Sort by folder then name
    actors.sort((a, b) => {
      const fa = (a.folder as string) || "";
      const fb = (b.folder as string) || "";
      if (fa !== fb) return fa.localeCompare(fb);
      return ((a.name as string) || "").localeCompare((b.name as string) || "");
    });

    const data: Record<string, unknown> = {
      count: actors.length,
      total_in_level: this.actors.size,
      actors,
    };

    if (actors.length >= limit) {
      data.truncated = true;
      data.limit = limit;
    }

    return { success: true, data };
  }

  private handleLevelSave(params: Record<string, unknown>): MockResponse {
    const saveAll = params.save_all === true;
    return {
      success: true,
      data: {
        level_saved: "MockLevel",
        assets_saved_count: saveAll ? 5 : 0,
        save_all: saveAll,
      },
    };
  }

  private handleLevelOutliner(params: Record<string, unknown>): MockResponse {
    const rootFolder = (params.root_folder as string) || "";
    const folders: Record<string, string[]> = {};
    const unfoldered: string[] = [];

    for (const actor of this.actors.values()) {
      if (!actor.folder) {
        unfoldered.push(actor.name);
        continue;
      }
      if (rootFolder && !actor.folder.startsWith(rootFolder)) continue;
      if (!folders[actor.folder]) folders[actor.folder] = [];
      folders[actor.folder].push(actor.name);
    }

    const folderEntries = Object.entries(folders).map(([path, actors]) => ({
      path,
      actor_count: actors.length,
      children: [] as string[],
    }));

    const data: Record<string, unknown> = {
      folders: folderEntries,
      folder_count: folderEntries.length,
      total_actors: this.actors.size,
      unfoldered_actor_count: unfoldered.length,
    };
    if (rootFolder) data.root_folder = rootFolder;

    return { success: true, data };
  }

  private handleViewportScreenshot(params: Record<string, unknown>): MockResponse {
    const resolution = (params.resolution as { width?: number; height?: number }) || {};
    const resX = resolution.width || 1920;
    const resY = resolution.height || 1080;
    const filename = (params.filename as string) || "viewport_20260314_120000.png";

    return {
      success: true,
      data: {
        filepath: `/mock/project/Saved/Screenshots/MCPBridge/${filename}`,
        filename,
        // "complete" (not "pending") so unit tests never poll the filesystem
        // for this fake path; the pending path is covered by integration tests.
        status: "complete",
        resolution: { width: resX, height: resY },
        file_size_bytes: 245760,
        capture_method: "AutomationLibrary.take_high_res_screenshot",
        camera_location: { ...this.cameraLocation },
        camera_rotation: { ...this.cameraRotation },
      },
    };
  }

  private handleViewportCamera(params: Record<string, unknown>): MockResponse {
    const location = params.location as number[] | undefined;
    const rotation = params.rotation as number[] | undefined;
    const fov = params.fov as number | undefined;

    if (location === undefined && rotation === undefined && fov === undefined) {
      return { success: false, data: {}, error: "Provide at least one of: location, rotation, fov" };
    }

    if (location) {
      this.cameraLocation = { x: location[0], y: location[1], z: location[2] };
    }
    if (rotation) {
      this.cameraRotation = { pitch: rotation[0], yaw: rotation[1], roll: rotation[2] };
    }

    return {
      success: true,
      data: {
        location: { ...this.cameraLocation },
        rotation: { ...this.cameraRotation },
      },
    };
  }

  private handleViewportMode(params: Record<string, unknown>): MockResponse {
    const mode = (params.mode as string) || "";
    const validModes = ["perspective", "top", "bottom", "front", "back", "left", "right"];
    if (!validModes.includes(mode)) {
      return { success: false, data: {}, error: `Unknown view mode: '${mode}'. Available: ${validModes}` };
    }
    // Simulate camera move for mode
    this.cameraLocation = { x: -2000, y: -2000, z: 1500 };
    this.cameraRotation = { pitch: -30, yaw: 45, roll: 0 };
    return {
      success: true,
      data: {
        location: { ...this.cameraLocation },
        rotation: { ...this.cameraRotation },
        mode,
      },
    };
  }

  private handleViewportFocus(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string;
    if (!actorName) {
      return { success: false, data: {}, error: "Missing 'actor_name'" };
    }

    const actor = this.actors.get(actorName);
    if (!actor) {
      return { success: false, data: {}, error: `Actor not found: ${actorName}. Use level_actors to list available actors.` };
    }

    const distance = (params.distance as number) || 500;
    this.cameraLocation = {
      x: actor.location.x - distance * 0.7,
      y: actor.location.y - distance * 0.7,
      z: actor.location.z + distance * 0.5,
    };

    return {
      success: true,
      data: {
        focused_on: actorName,
        camera_location: { ...this.cameraLocation },
        camera_rotation: { pitch: -25, yaw: 45, roll: 0 },
        actor_bounds: {
          min: { x: actor.location.x - 50, y: actor.location.y - 50, z: actor.location.z - 50 },
          max: { x: actor.location.x + 50, y: actor.location.y + 50, z: actor.location.z + 50 },
        },
      },
    };
  }

  private handleViewportRenderMode(params: Record<string, unknown>): MockResponse {
    const mode = (params.mode as string) || "";
    const validModes = ["lit", "unlit", "wireframe", "detail_lighting", "lighting_only", "light_complexity", "shader_complexity", "collision"];
    if (!validModes.includes(mode)) {
      return { success: false, data: {}, error: `Unknown render mode: '${mode}'. Available: ${validModes}` };
    }

    const previousMode = this.renderMode;
    this.renderMode = mode;

    return {
      success: true,
      data: { render_mode: mode, previous_mode: previousMode },
    };
  }

  private handleViewportBounds(): MockResponse {
    return {
      success: true,
      data: {
        camera_location: { ...this.cameraLocation },
        camera_rotation: { ...this.cameraRotation },
        is_perspective: true,
      },
    };
  }

  private handleViewportFit(params: Record<string, unknown>): MockResponse {
    const actorNames = (params.actor_names as string[]) || [];
    const padding = (params.padding as number) || 1.2;

    let targets: MockActor[];
    if (actorNames.length > 0) {
      targets = [];
      for (const name of actorNames) {
        const actor = this.actors.get(name);
        if (actor) targets.push(actor);
      }
      if (targets.length === 0) {
        return { success: false, data: {}, error: "No matching actors found" };
      }
    } else {
      targets = [...this.actors.values()];
      if (targets.length === 0) {
        return { success: false, data: {}, error: "No actors in the level to fit" };
      }
    }

    // Compute center
    let cx = 0, cy = 0, cz = 0;
    for (const a of targets) {
      cx += a.location.x; cy += a.location.y; cz += a.location.z;
    }
    cx /= targets.length; cy /= targets.length; cz /= targets.length;

    this.cameraLocation = { x: cx - 500 * padding, y: cy - 500 * padding, z: cz + 300 * padding };

    return {
      success: true,
      data: {
        fitted_actors: targets.map((a) => a.name),
        fitted_count: targets.length,
        camera_location: { ...this.cameraLocation },
        camera_rotation: { pitch: -30, yaw: 45, roll: 0 },
        combined_bounds: {
          min: { x: cx - 100, y: cy - 100, z: cz - 100 },
          max: { x: cx + 100, y: cy + 100, z: cz + 100 },
        },
        padding,
      },
    };
  }

  private handleViewportLookAt(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string | undefined;
    const location = params.location as number[] | undefined;

    let target: { x: number; y: number; z: number };

    if (actorName) {
      const actor = this.actors.get(actorName);
      if (!actor) {
        return { success: false, data: {}, error: `Actor not found: ${actorName}. Use level_actors to list available actors.` };
      }
      target = { ...actor.location };
    } else if (location && location.length === 3) {
      target = { x: location[0], y: location[1], z: location[2] };
    } else {
      return { success: false, data: {}, error: "Provide 'actor_name' or 'location'" };
    }

    // Compute simple look-at rotation
    const dx = target.x - this.cameraLocation.x;
    const dy = target.y - this.cameraLocation.y;
    const dz = target.z - this.cameraLocation.z;
    const yaw = Math.atan2(dy, dx) * 180 / Math.PI;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const pitch = Math.atan2(dz, dist) * 180 / Math.PI;

    this.cameraRotation = { pitch, yaw, roll: 0 };

    return {
      success: true,
      data: {
        camera_location: { ...this.cameraLocation },
        camera_rotation: { ...this.cameraRotation },
        target_location: target,
      },
    };
  }

  // ---- Material Handlers ----

  private handleMaterialList(params: Record<string, unknown>): MockResponse {
    const pathFilter = (params.path_filter as string) || "/Game/";
    const nameFilter = (params.name_filter as string) || "";
    const typeFilter = (params.type_filter as string) || "all";
    const limit = Math.min((params.limit as number) || 200, 2000);

    const results: Array<Record<string, unknown>> = [];
    for (const mat of this.materials.values()) {
      if (results.length >= limit) break;
      if (!mat.path.startsWith(pathFilter)) continue;
      if (typeFilter !== "all" && mat.type !== typeFilter) continue;
      if (nameFilter) {
        const regex = new RegExp("^" + nameFilter.replace(/\*/g, ".*").replace(/\?/g, ".") + "$", "i");
        if (!regex.test(mat.name)) continue;
      }
      results.push({
        name: mat.name,
        path: mat.path,
        type: mat.type,
        parent: mat.parent,
      });
    }

    return {
      success: true,
      data: { count: results.length, truncated: results.length >= limit, materials: results },
    };
  }

  private handleMaterialInfo(params: Record<string, unknown>): MockResponse {
    const matPath = params.material_path as string;
    if (!matPath) return { success: false, data: {}, error: "Missing 'material_path'" };
    if (!matPath.startsWith("/")) return { success: false, data: {}, error: `material_path must start with '/' (got '${matPath}')` };

    const mat = this.materials.get(matPath);
    if (!mat) return { success: false, data: {}, error: `Material not found: ${matPath}` };

    const parentChain: string[] = [];
    if (mat.parent) {
      parentChain.push(mat.parent);
    }

    return {
      success: true,
      data: {
        name: mat.name,
        path: mat.path,
        type: mat.type,
        parent: mat.parent,
        parent_chain: parentChain,
        parameters: mat.parameters,
      },
    };
  }

  private handleMaterialCreate(params: Record<string, unknown>): MockResponse {
    const name = params.name as string;
    if (!name) return { success: false, data: {}, error: "Missing 'name'" };

    const path = (params.path as string) || "/Game/Materials";
    if (!path.startsWith("/")) return { success: false, data: {}, error: `path must start with '/' (got '${path}')` };

    const matType = (params.type as string) || "material";
    const parentPath = (params.parent as string) || "";

    const fullPath = `${path}/${name}`;
    if (this.materials.has(fullPath)) {
      return { success: false, data: {}, error: `Asset already exists at: ${fullPath}` };
    }

    if (matType === "instance") {
      if (!parentPath) return { success: false, data: {}, error: "type is 'instance' but no 'parent' material path provided" };
      if (!this.materials.has(parentPath)) return { success: false, data: {}, error: `Parent material not found: ${parentPath}` };
    }

    const parametersSet: Record<string, string[]> = { scalar: [], vector: [], texture: [] };
    const initParams = (params.parameters as Record<string, Record<string, unknown>>) || {};

    const newMat: MockMaterial = {
      name,
      path: fullPath,
      type: matType as "material" | "instance",
      parent: matType === "instance" ? parentPath : null,
      parameters: { scalar: [], vector: [], texture: [] },
    };

    if (initParams.scalar) {
      for (const [pName, pVal] of Object.entries(initParams.scalar)) {
        newMat.parameters.scalar.push({ name: pName, value: pVal as number });
        parametersSet.scalar.push(pName);
      }
    }
    if (initParams.vector) {
      for (const [pName, pVal] of Object.entries(initParams.vector)) {
        newMat.parameters.vector.push({ name: pName, value: pVal as number[] });
        parametersSet.vector.push(pName);
      }
    }
    if (initParams.texture) {
      for (const [pName, pVal] of Object.entries(initParams.texture)) {
        newMat.parameters.texture.push({ name: pName, value: pVal as string });
        parametersSet.texture.push(pName);
      }
    }

    this.materials.set(fullPath, newMat);

    return {
      success: true,
      data: {
        name,
        path: fullPath,
        type: matType,
        parent: matType === "instance" ? parentPath : null,
        parameters_set: parametersSet,
      },
    };
  }

  private handleMaterialApply(params: Record<string, unknown>): MockResponse {
    const actorName = params.actor_name as string;
    const matPath = params.material_path as string;
    if (!actorName) return { success: false, data: {}, error: "Missing 'actor_name'" };
    if (!matPath) return { success: false, data: {}, error: "Missing 'material_path'" };

    if (!this.materials.has(matPath)) return { success: false, data: {}, error: `Material not found: ${matPath}` };

    const actor = this.actors.get(actorName);
    if (!actor) return { success: false, data: {}, error: `Actor not found: ${actorName}` };

    const slotIndex = (params.slot_index as number) || 0;
    const totalSlots = 3;

    if (slotIndex < 0 || slotIndex >= totalSlots) {
      return { success: false, data: {}, error: `Slot index ${slotIndex} out of range (actor has ${totalSlots} material slots)` };
    }

    if (!actor.materials) actor.materials = ["/Game/Materials/M_Default", "/Game/Materials/M_Default", "/Game/Materials/M_Default"];
    const previousMaterial = actor.materials[slotIndex] || "/Game/Materials/M_Default";
    actor.materials[slotIndex] = matPath;

    return {
      success: true,
      data: {
        actor: actorName,
        component: "StaticMeshComponent0",
        slot_index: slotIndex,
        slot_name: null,
        material_applied: matPath,
        previous_material: previousMaterial,
        total_slots: totalSlots,
      },
    };
  }

  // ---- Blueprint Handlers ----

  private handleBlueprintList(params: Record<string, unknown>): MockResponse {
    const pathFilter = (params.path_filter as string) || "/Game/";
    const nameFilter = (params.name_filter as string) || "";
    const parentClassFilter = (params.parent_class_filter as string) || "";
    const limit = Math.min((params.limit as number) || 200, 2000);

    const results: Array<Record<string, unknown>> = [];
    for (const bp of this.blueprints.values()) {
      if (results.length >= limit) break;
      if (!bp.path.startsWith(pathFilter)) continue;
      if (nameFilter) {
        const regex = new RegExp("^" + nameFilter.replace(/\*/g, ".*").replace(/\?/g, ".") + "$", "i");
        if (!regex.test(bp.name)) continue;
      }
      if (parentClassFilter && bp.parent_class.toLowerCase() !== parentClassFilter.toLowerCase()) continue;

      results.push({
        name: bp.name,
        path: bp.path,
        parent_class: bp.parent_class,
        is_compiled: bp.is_compiled,
      });
    }

    return {
      success: true,
      data: { count: results.length, truncated: results.length >= limit, blueprints: results },
    };
  }

  private handleBlueprintInfo(params: Record<string, unknown>): MockResponse {
    const bpPath = params.blueprint_path as string;
    if (!bpPath) return { success: false, data: {}, error: "Missing 'blueprint_path'" };
    if (!bpPath.startsWith("/")) return { success: false, data: {}, error: `blueprint_path must start with '/' (got '${bpPath}')` };

    const bp = this.blueprints.get(bpPath);
    if (!bp) return { success: false, data: {}, error: `Blueprint not found: ${bpPath}` };

    return {
      success: true,
      data: {
        name: bp.name,
        path: bp.path,
        parent_class: bp.parent_class,
        parent_chain: [bp.parent_class, "Object"],
        is_compiled: bp.is_compiled,
        components: bp.components,
        variables: bp.variables,
        functions: bp.functions,
        event_graphs: bp.event_graphs,
        component_count: bp.components.length,
        variable_count: bp.variables.length,
        function_count: bp.functions.length,
      },
    };
  }

  private handleBlueprintCreate(params: Record<string, unknown>): MockResponse {
    const name = params.name as string;
    if (!name) return { success: false, data: {}, error: "Missing 'name'" };

    const path = (params.path as string) || "/Game/Blueprints";
    if (!path.startsWith("/")) return { success: false, data: {}, error: `path must start with '/' (got '${path}')` };

    const parentClass = (params.parent_class as string) || "Actor";
    const fullPath = `${path}/${name}`;

    if (this.blueprints.has(fullPath)) {
      return { success: false, data: {}, error: `Asset already exists at: ${fullPath}` };
    }

    const validClasses = ["Actor", "Pawn", "Character", "PlayerController", "GameModeBase", "ActorComponent", "SceneComponent", "HUD", "PlayerState", "GameStateBase"];
    if (!validClasses.includes(parentClass)) {
      return { success: false, data: {}, error: `Unknown parent class: '${parentClass}'. Common classes: ${validClasses.sort()}` };
    }

    const componentsAdded: string[] = [];
    const componentsFailed: string[] = [];
    const components: MockBlueprint["components"] = [];
    const componentDefs = (params.components as Array<Record<string, unknown>>) || [];

    for (const def of componentDefs) {
      const compName = def.name as string;
      const compClass = def.class as string;
      if (!compName || !compClass) {
        componentsFailed.push(compName || "unnamed");
        continue;
      }
      components.push({
        name: compName,
        class: compClass,
        parent: (def.attach_to as string) || null,
        is_root: components.length === 0,
      });
      componentsAdded.push(compName);
    }

    const variablesAdded: string[] = [];
    const variablesFailed: string[] = [];
    const variables: MockBlueprint["variables"] = [];
    const variableDefs = (params.variables as Array<Record<string, unknown>>) || [];

    for (const def of variableDefs) {
      const varName = def.name as string;
      if (!varName) {
        variablesFailed.push("unnamed");
        continue;
      }
      variables.push({
        name: varName,
        type: (def.type as string) || "unknown",
        category: (def.category as string) || "",
        is_editable: def.editable !== false,
        tooltip: (def.tooltip as string) || "",
      });
      variablesAdded.push(varName);
    }

    const bp: MockBlueprint = {
      name,
      path: fullPath,
      parent_class: parentClass,
      is_compiled: true,
      components,
      variables,
      functions: [],
      event_graphs: ["EventGraph"],
    };
    this.blueprints.set(fullPath, bp);

    return {
      success: true,
      data: {
        name,
        path: fullPath,
        parent_class: parentClass,
        components_added: componentsAdded,
        components_failed: componentsFailed,
        variables_added: variablesAdded,
        variables_failed: variablesFailed,
        variables_skipped_reason: null,
        compiled: true,
        compile_errors: [],
      },
    };
  }

  private handleBlueprintCompile(params: Record<string, unknown>): MockResponse {
    const bpPath = params.blueprint_path as string;
    if (!bpPath) return { success: false, data: {}, error: "Missing 'blueprint_path'" };
    if (!bpPath.startsWith("/")) return { success: false, data: {}, error: `blueprint_path must start with '/' (got '${bpPath}')` };

    const bp = this.blueprints.get(bpPath);
    if (!bp) return { success: false, data: {}, error: `Blueprint not found: ${bpPath}` };

    bp.is_compiled = true;

    return {
      success: true,
      data: {
        name: bp.name,
        path: bpPath,
        compiled: true,
        had_errors: false,
        errors: [],
        warnings: [],
      },
    };
  }

  private handleBlueprintDocument(params: Record<string, unknown>): MockResponse {
    const infoResult = this.handleBlueprintInfo(params);
    if (!infoResult.success) return infoResult;

    const info = infoResult.data;
    const detailLevel = (params.detail_level as string) || "standard";
    const lines: string[] = [];

    lines.push(`# ${info.name}`);
    lines.push(`Parent: ${info.parent_class}`);
    lines.push(`Compiled: ${info.is_compiled ? "Yes" : "No"}`);

    if (detailLevel === "minimal") {
      lines.push(`Components: ${(info.components as unknown[]).length}`);
      lines.push(`Variables: ${(info.variables as unknown[]).length}`);
      lines.push(`Functions: ${(info.functions as unknown[]).length}`);
    } else {
      const comps = info.components as Array<Record<string, unknown>>;
      if (comps.length > 0) {
        lines.push("");
        lines.push(`## Components (${comps.length})`);
        for (const c of comps) {
          const root = c.is_root ? " [ROOT]" : "";
          lines.push(`- ${c.name} (${c.class})${root}`);
        }
      }

      const vars = info.variables as Array<Record<string, unknown>>;
      if (vars.length > 0) {
        lines.push("");
        lines.push(`## Variables (${vars.length})`);
        for (const v of vars) {
          const cat = v.category ? ` [Category: ${v.category}]` : "";
          lines.push(`- ${v.name} : ${v.type}${cat}`);
        }
      }

      const evts = info.event_graphs as string[];
      if (evts.length > 0) {
        lines.push("");
        lines.push("## Event Graphs");
        for (const e of evts) {
          lines.push(`- ${e}`);
        }
      }
    }

    return {
      success: true,
      data: {
        name: info.name,
        path: info.path,
        documentation: lines.join("\n"),
      },
    };
  }

  private handlePlacementValidate(params: Record<string, unknown>): MockResponse {
    const actorNames = params.actors as string[];
    if (!actorNames || !Array.isArray(actorNames) || actorNames.length === 0) {
      return { success: false, data: {}, error: "Missing 'actors' list" };
    }

    const found: MockActor[] = [];
    const notFound: string[] = [];
    for (const name of actorNames) {
      const actor = this.actors.get(name);
      if (actor) found.push(actor);
      else notFound.push(name);
    }

    return {
      success: true,
      data: {
        actors_checked: found.length,
        not_found: notFound,
        issues: [],
        issue_count: 0,
      },
    };
  }
}
