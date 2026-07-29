/**
 * Project-local tool extensions.
 *
 * The bridge is a general UE4.27 control surface. Tools that only make sense for
 * one game (Sinfeld locomotion debugging, fixed-camera tuning) do not belong in
 * the shipped catalog, but they still need a supported way to exist.
 *
 * An extension is a directory containing `extension.json`. It declares tools
 * that forward to listener commands. There is no TypeScript to compile in the
 * host project: every tool in this codebase's incubator is already a thin
 * pass-through to `client.sendCommand(command, params)`, so a declarative
 * manifest covers the real shape without a second build step.
 *
 * Discovery, in order:
 *   1. UNREAL_BRIDGE_EXTENSIONS - path-separator-delimited list of extension
 *      directories or manifest files. Explicit config wins.
 *   2. <cwd>/BridgeExtensions/<name>/extension.json - convention for a host
 *      project that keeps its extension beside the .uproject.
 *
 * Every tool is namespaced with the extension's prefix, so an extension can
 * never shadow or be mistaken for a core tool. `sinfeld_locomotion_snapshot` is
 * visibly not part of the bridge.
 *
 * Failures here are never fatal. A malformed manifest is reported on stderr and
 * skipped: a broken project-local extension must not stop the bridge from
 * starting.
 */

import { readFileSync, existsSync, readdirSync, statSync } from "node:fs";
import { join, resolve, isAbsolute, dirname } from "node:path";
import { z } from "zod";
import type { ToolAnnotations } from "@modelcontextprotocol/sdk/types.js";

import { UnrealClient } from "./unreal-client.js";
import type { ToolDefinition } from "./types.js";
import {
  readOnly,
  mutating,
  mutatingIdempotent,
  destructive,
  destructiveIdempotent,
} from "./annotations.js";

const ANNOTATION_PRESETS: Record<string, ToolAnnotations> = {
  readOnly,
  mutating,
  mutatingIdempotent,
  destructive,
  destructiveIdempotent,
};

/** One declared parameter. Deliberately small: enough for pass-through tools,
    not a general JSON Schema dialect. */
const ParamSpec = z.object({
  type: z.enum(["string", "number", "integer", "boolean", "string[]"]),
  description: z.string().optional(),
  optional: z.boolean().optional(),
  default: z.unknown().optional(),
  enum: z.array(z.string()).optional(),
  min: z.number().optional(),
  max: z.number().optional(),
});
type ParamSpec = z.infer<typeof ParamSpec>;

const ToolSpec = z.object({
  name: z.string().regex(/^[a-z][a-z0-9_]*$/, "tool names are lower_snake_case"),
  description: z.string().min(20, "write a description a model can act on"),
  /** Listener command this forwards to. */
  command: z.string().regex(/^[a-z][a-z0-9_]*$/),
  /** Constant params merged into every call, e.g. {"operation": "snapshot"}. */
  fixedParams: z.record(z.unknown()).optional(),
  params: z.record(ParamSpec).optional(),
  annotations: z.enum([
    "readOnly", "mutating", "mutatingIdempotent", "destructive", "destructiveIdempotent",
  ]),
  /** Marks a tool that only works during PIE. Recorded for documentation and
      for the consent rules in AGENTS.md; it does not gate anything by itself. */
  runtimeOnly: z.boolean().optional(),
});

const Manifest = z.object({
  name: z.string().regex(/^[a-z][a-z0-9_]*$/, "extension names are lower_snake_case"),
  version: z.string(),
  description: z.string(),
  /**
   * Must be true before any tool is registered. Defaults to false on purpose.
   *
   * An extension can be written, validated and reviewed long before its listener
   * handlers exist. Registering it in that state would advertise tools that fail
   * on every call, which is the exact failure this project spent a week
   * debugging. Opting in is a deliberate act taken once the handlers are real.
   */
  enabled: z.boolean().default(false),
  /** Why it is disabled. Surfaced on stderr so the reason is never a mystery. */
  disabledReason: z.string().optional(),
  /** Defaults to `${name}_`. Set "" only if you accept collision risk. */
  toolPrefix: z.string().optional(),
  tools: z.array(ToolSpec),
});
export type Manifest = z.infer<typeof Manifest>;

export interface LoadedExtension {
  manifest: Manifest;
  manifestPath: string;
  tools: ToolDefinition[];
}

function buildZod(params: Record<string, ParamSpec> | undefined): z.ZodType {
  if (!params || Object.keys(params).length === 0) return z.object({});
  const shape: Record<string, z.ZodTypeAny> = {};
  for (const [key, spec] of Object.entries(params)) {
    let field: z.ZodTypeAny;
    switch (spec.type) {
      case "string":
        field = spec.enum ? z.enum(spec.enum as [string, ...string[]]) : z.string();
        break;
      case "number":
      case "integer": {
        let n = spec.type === "integer" ? z.number().int() : z.number();
        if (spec.min !== undefined) n = n.min(spec.min);
        if (spec.max !== undefined) n = n.max(spec.max);
        field = n;
        break;
      }
      case "boolean":
        field = z.boolean();
        break;
      case "string[]":
        field = z.array(z.string());
        break;
    }
    if (spec.description) field = field.describe(spec.description);
    if (spec.default !== undefined) field = field.default(spec.default as never);
    else if (spec.optional) field = field.optional();
    shape[key] = field;
  }
  return z.object(shape);
}

function toToolDefinition(
  client: UnrealClient,
  manifest: Manifest,
  spec: z.infer<typeof ToolSpec>
): ToolDefinition {
  const prefix = manifest.toolPrefix ?? `${manifest.name}_`;
  return {
    name: `${prefix}${spec.name}`,
    description: spec.description,
    inputSchema: buildZod(spec.params),
    annotations: ANNOTATION_PRESETS[spec.annotations],
    handler: async (params) => {
      const merged = { ...(spec.fixedParams ?? {}), ...params };
      const result = await client.sendCommand(spec.command, merged);
      return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
    },
  };
}

/** Every manifest path implied by the environment and by convention. */
export function discoverManifestPaths(cwd: string = process.cwd()): string[] {
  const found: string[] = [];
  const fromEnv = process.env.UNREAL_BRIDGE_EXTENSIONS;
  if (fromEnv) {
    for (const raw of fromEnv.split(/[;,]/).map((s) => s.trim()).filter(Boolean)) {
      const p = isAbsolute(raw) ? raw : resolve(cwd, raw);
      if (!existsSync(p)) continue;
      found.push(statSync(p).isDirectory() ? join(p, "extension.json") : p);
    }
  }
  const conventional = join(cwd, "BridgeExtensions");
  if (existsSync(conventional) && statSync(conventional).isDirectory()) {
    for (const entry of readdirSync(conventional, { withFileTypes: true })) {
      if (!entry.isDirectory()) continue;
      found.push(join(conventional, entry.name, "extension.json"));
    }
  }
  return found.filter((p) => existsSync(p));
}

/** Parse and validate one manifest. Throws with a usable message. */
export function loadManifest(manifestPath: string): Manifest {
  let raw: unknown;
  try {
    raw = JSON.parse(readFileSync(manifestPath, "utf-8").replace(/^﻿/, ""));
  } catch (e) {
    throw new Error(`${manifestPath} is not valid JSON: ${(e as Error).message}`);
  }
  const parsed = Manifest.safeParse(raw);
  if (!parsed.success) {
    const detail = parsed.error.issues
      .map((i) => `${i.path.join(".") || "(root)"}: ${i.message}`)
      .join("; ");
    throw new Error(`${manifestPath} is not a valid extension manifest: ${detail}`);
  }
  const names = parsed.data.tools.map((t) => t.name);
  const dupes = names.filter((n, i) => names.indexOf(n) !== i);
  if (dupes.length) {
    throw new Error(`${manifestPath} declares duplicate tool names: ${[...new Set(dupes)].join(", ")}`);
  }
  return parsed.data;
}

/**
 * Load every discoverable extension. Never throws: a bad manifest is reported
 * and skipped so one broken extension cannot stop the server starting.
 */
export function loadExtensions(
  client: UnrealClient,
  cwd: string = process.cwd()
): LoadedExtension[] {
  const loaded: LoadedExtension[] = [];
  for (const manifestPath of discoverManifestPaths(cwd)) {
    try {
      const manifest = loadManifest(manifestPath);
      if (!manifest.enabled) {
        console.error(
          `[Unreal MCP Bridge] extension "${manifest.name}" is present but disabled` +
            `${manifest.disabledReason ? `: ${manifest.disabledReason}` : ""}` +
            ` (${manifest.tools.length} tool(s) not registered). Set "enabled": true in ${manifestPath} once its listener commands exist.`
        );
        continue;
      }
      loaded.push({
        manifest,
        manifestPath,
        tools: manifest.tools.map((t) => toToolDefinition(client, manifest, t)),
      });
    } catch (e) {
      console.error(`[Unreal MCP Bridge] extension skipped: ${(e as Error).message}`);
    }
  }
  return loaded;
}

/** Directory holding an extension's Python handlers, by convention. */
export function extensionHandlerDir(manifestPath: string): string {
  return join(dirname(manifestPath), "handlers");
}
