/**
 * Machine-local paths, kept out of git.
 *
 * UE_ENGINE_ROOT and MCP_UNREAL_PROJECT_ROOT are absolute paths that differ on
 * every machine, and they used to live in the committed .mcp.json. That made a
 * fresh clone point at someone else's drive layout, and it put a user name in a
 * public repository. They now come from bridge.local.json at the repository
 * root, which is gitignored, with bridge.local.example.json committed as the
 * template.
 *
 * The environment still wins. A value already set by the client, a shell, or CI
 * is never overwritten here, so this is a fallback for the common case and not
 * a new source of truth that could silently disagree with an explicit setting.
 *
 * Every consumer reads process.env at call time (resolveSession,
 * resolveEngineRoot, resolveUbtBatch), so seeding the environment once at
 * startup reaches all of them without threading a config object through.
 */

import { existsSync, readFileSync } from "fs";
import { dirname, join } from "path";
import { fileURLToPath } from "url";

/** Keys this file is allowed to set. Anything else in the JSON is reported and
    ignored, so a typo fails loudly instead of looking like it worked. */
const ALLOWED_KEYS = [
  "UE_ENGINE_ROOT",
  "MCP_UNREAL_PROJECT_ROOT",
  "MCP_PUERTS_SESSION_ID",
  "UNREAL_API_MCP_EXE",
] as const;

/** The repository root, three levels up from this module whether it is running
    from src (tsx) or dist (node): <root>/mcp-server/{src,dist}/local-config. */
export function repositoryRoot(): string {
  return dirname(dirname(dirname(fileURLToPath(import.meta.url))));
}

export function localConfigPath(): string {
  return join(repositoryRoot(), "bridge.local.json");
}

export interface LocalConfigResult {
  path: string;
  found: boolean;
  applied: string[];
  /** Present in the file but already set in the environment, so left alone. */
  skipped: string[];
  unknown: string[];
  error?: string;
}

/**
 * Seed process.env from bridge.local.json. Never throws: a malformed local file
 * must not stop the server from starting, because the environment or the
 * .uproject fallback may already supply everything needed.
 */
export function loadLocalConfig(): LocalConfigResult {
  const path = localConfigPath();
  const result: LocalConfigResult = { path, found: false, applied: [], skipped: [], unknown: [] };
  if (!existsSync(path)) return result;
  result.found = true;

  let parsed: Record<string, unknown>;
  try {
    // Strip a UTF-8 BOM. Windows editors and PowerShell write one routinely and
    // JSON.parse rejects it, which would read as "no config" rather than an error.
    parsed = JSON.parse(readFileSync(path, "utf8").replace(/^﻿/, "")) as Record<string, unknown>;
  } catch (error: unknown) {
    result.error = `bridge.local.json is not valid JSON: ${String(error)}`;
    return result;
  }
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    result.error = "bridge.local.json must be a JSON object of {KEY: \"value\"}.";
    return result;
  }

  for (const [key, value] of Object.entries(parsed)) {
    if (key.startsWith("//") || key === "$schema") continue;
    if (!(ALLOWED_KEYS as readonly string[]).includes(key)) {
      result.unknown.push(key);
      continue;
    }
    if (typeof value !== "string" || value.trim() === "") {
      result.unknown.push(key);
      continue;
    }
    if (process.env[key] !== undefined && process.env[key] !== "") {
      result.skipped.push(key);
      continue;
    }
    process.env[key] = value;
    result.applied.push(key);
  }
  return result;
}
