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

  const seen: PipeRequest[] = [];
  const server: Server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as PipeRequest;
    seen.push(request);
    socket.end(JSON.stringify({
      success: true,
      message: "Native command executed.",
      data: { echoed_command: request.command },
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
    assert(compat.length === 12, `expected 12 compat aliases, got ${compat.length}`);
    assert(
      Object.keys(compatAliasTargets).length === 12,
      "compatAliasTargets must name all 12 aliases for the inventory generator",
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
    assert(on.length === 12, `expected 12 aliases with the flag set, got ${on.length}`);

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
    assert(withCollision.length === 11, `collision must skip exactly the colliding alias, got ${withCollision.length}`);
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
      ["actor_delete", { actor_name: "Cube_1" }, "delete_actor", { actor: "Cube_1", confirm: true }],
      ["actor_modify", { actor_name: "Cube_1", visible: false }, "set_property", { actor: "Cube_1", property: "bHidden", value: true }],
      ["level_actors", { class_filter: "StaticMeshActor", name_filter: "Cube", limit: 5 }, "find_actors", { type: "StaticMeshActor", name: "Cube", limit: 5 }],
      ["level_save", {}, "save", {}],
      ["asset_save_many", { paths: "/Game/Maps/Test" }, "save", { assets: ["/Game/Maps/Test"] }],
      ["asset_list", { path: "/Game/Meshes", asset_type: "StaticMesh", name_pattern: "SM_" }, "find_assets", { path: "/Game/Meshes", type: "StaticMesh", name: "SM_" }],
      ["viewport_screenshot", { filename: "shot.png" }, "viewport_screenshot", { filename: "shot.png" }],
      ["gameplay_pie_start", {}, "pie_start", {}],
      ["gameplay_pie_stop", {}, "pie_stop", {}],
      ["ue_logs", { maximum_lines: 25 }, "get_logs", { maximum_lines: 25 }],
      ["undo", { transaction_id: "tx-9" }, "undo", { transaction_id: "tx-9" }],
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

    // --- unmappable parameters fail loud, and never reach the editor ------
    const unmappable: Array<[string, Record<string, unknown>, string]> = [
      ["undo", { count: 1 }, "transaction_id"],
      ["undo", { count: 3, transaction_id: "tx-9" }, "count"],
      ["actor_spawn", { asset_path: "/Game/Meshes/SM_Cube", folder: "Props" }, "folder"],
      ["actor_spawn", { asset_path: "/Game/Meshes/SM_Cube", scale: { x: 2, y: 2, z: 2 } }, "scale"],
      ["actor_modify", { actor_name: "Cube_1", location: { x: 1, y: 2, z: 3 } }, "location"],
      ["actor_delete", { actor_name: "Cube_*" }, "actor_name"],
      ["level_actors", { folder_filter: "Props" }, "folder_filter"],
      ["level_actors", { name_filter: "Cube_*" }, "name_filter"],
      ["level_save", { save_all: true }, "save_all"],
      ["viewport_screenshot", { resolution: { width: 640 } }, "resolution"],
      ["viewport_screenshot", { show_ui: true }, "show_ui"],
      ["gameplay_pie_start", { level_path: "/Game/Maps/Test" }, "level_path"],
      ["ue_logs", { severity: "Error" }, "severity"],
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
