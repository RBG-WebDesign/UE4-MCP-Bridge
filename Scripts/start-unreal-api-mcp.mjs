#!/usr/bin/env node
// Launches unreal-api-mcp from its dedicated virtual environment.
//
// This exists so the committed .mcp.json can name a repository-relative path
// instead of an absolute one under someone's user profile. The venv location is
// derived from this file's own location, so it is correct after a fresh clone
// to any directory.
//
// Why a venv rather than uvx: unreal-api-mcp does
// "from mcp.server.fastmcp import FastMCP", and uvx resolves the newest mcp,
// which is 2.0.0, where fastmcp was removed. Pinning through uvx does not work
// on Windows either, because uv hardlinks packages from a shared cache and a
// pywin32 DLL already mapped by another uv-managed process cannot be replaced
// through any hardlink to it. A plain venv touches none of that.
//
// Create it with Scripts/setup-unreal-api-mcp.ps1.
import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = dirname(dirname(fileURLToPath(import.meta.url)));

/** UNREAL_API_MCP_EXE wins, then bridge.local.json, then the repo-local venv. */
function resolveExecutable() {
  if (process.env.UNREAL_API_MCP_EXE) return process.env.UNREAL_API_MCP_EXE;
  const localConfig = join(repoRoot, "bridge.local.json");
  if (existsSync(localConfig)) {
    try {
      const parsed = JSON.parse(readFileSync(localConfig, "utf8").replace(/^﻿/, ""));
      if (typeof parsed.UNREAL_API_MCP_EXE === "string" && parsed.UNREAL_API_MCP_EXE.trim()) {
        return parsed.UNREAL_API_MCP_EXE;
      }
    } catch {
      // A malformed local file must not stop this launcher; fall through.
    }
  }
  const name = process.platform === "win32" ? "unreal-api-mcp.exe" : "unreal-api-mcp";
  const binDir = process.platform === "win32" ? "Scripts" : "bin";
  return join(repoRoot, ".venv-unreal-api", binDir, name);
}

const exe = resolveExecutable();
if (!existsSync(exe)) {
  console.error(
    `[unreal-api launcher] Not found: ${exe}\n`
    + `Create it with:  powershell -ExecutionPolicy Bypass -File Scripts/setup-unreal-api-mcp.ps1\n`
    + `Or point UNREAL_API_MCP_EXE (or bridge.local.json) at an existing install.`
  );
  process.exit(1);
}

// stdio inherit: this process is a pass-through for the MCP stdio protocol and
// must not buffer or reinterpret a single byte of it.
const child = spawn(exe, process.argv.slice(2), { stdio: "inherit", env: process.env });
child.on("exit", (code, signal) => process.exit(signal ? 1 : code ?? 0));
child.on("error", (error) => {
  console.error(`[unreal-api launcher] Failed to start ${exe}: ${error.message}`);
  process.exit(1);
});
