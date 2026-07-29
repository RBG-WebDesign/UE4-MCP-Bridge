#!/usr/bin/env node
/**
 * Bridge doctor: detect the ways this project has actually broken before.
 *
 * Every failure that caused the 2026-07-29 consolidation was silent. Nothing
 * announced itself; the bridge just behaved oddly for a week. Each check here
 * corresponds to one of those real failures, so they announce themselves now.
 *
 *   1. Abandoned merge      - unresolved index entries, possibly with no MERGE_HEAD
 *   2. Stale build          - dist/ older than src/, so clients run outdated tools
 *   3. Missing build        - no node_modules or dist, so the server never starts
 *   4. Divergent clones     - another clone of this repo ahead of or behind this one
 *   5. Unmerged worktrees   - a worktree holding files that exist nowhere else
 *   6. Unregistered tools   - a file in src/tools/ that index.ts never imports
 *   7. Doc drift            - AGENTS.md stating counts that no longer match reality
 *   8. Engine root          - UE_ENGINE_ROOT missing, so engine_source_* silently fail
 *
 * Usage:  npm run doctor
 *         npm run doctor -- --quiet     (only print problems)
 * Exit 0 healthy, 1 if any ERROR. Warnings alone do not fail.
 */

import { execSync } from "node:child_process";
import { readFileSync, existsSync, readdirSync, statSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const quiet = process.argv.includes("--quiet");

const findings = [];
const ok = (check, detail) => findings.push({ level: "ok", check, detail });
const warn = (check, detail, fix) => findings.push({ level: "warn", check, detail, fix });
const err = (check, detail, fix) => findings.push({ level: "error", check, detail, fix });

function git(args, cwd = repoRoot) {
  try { return execSync(`git ${args}`, { cwd, encoding: "utf-8", stdio: ["ignore", "pipe", "ignore"] }).trim(); }
  catch { return null; }
}

function newestMtime(dir, exts) {
  let newest = 0;
  const walk = (d) => {
    if (!existsSync(d)) return;
    for (const e of readdirSync(d, { withFileTypes: true })) {
      if (e.name === "node_modules" || e.name === "__pycache__") continue;
      const p = join(d, e.name);
      if (e.isDirectory()) walk(p);
      else if (!exts || exts.some((x) => e.name.endsWith(x))) {
        const m = statSync(p).mtimeMs;
        if (m > newest) newest = m;
      }
    }
  };
  walk(dir);
  return newest;
}

// ---------------------------------------------- 1. abandoned merge
{
  const unmerged = git("ls-files -u");
  const paths = git("diff --name-only --diff-filter=U");
  if (unmerged) {
    const n = new Set(paths ? paths.split("\n").filter(Boolean) : []).size;
    const hasMergeHead = existsSync(join(repoRoot, ".git", "MERGE_HEAD"));
    err("abandoned-merge",
      `${n} path(s) have unresolved index entries${hasMergeHead ? "" : " and there is NO MERGE_HEAD, so git cannot say what was being merged"}`,
      "Archive the stage blobs (git ls-files -u, then git cat-file -p <sha>), then `git reset --mixed HEAD` to clear the index without touching the working tree.");
  } else {
    ok("abandoned-merge", "index is clean");
  }
}

// ---------------------------------------------- 2 & 3. build freshness
{
  const dist = join(repoRoot, "mcp-server", "dist");
  const src = join(repoRoot, "mcp-server", "src");
  if (!existsSync(join(repoRoot, "node_modules"))) {
    err("deps-missing", "node_modules is absent, so the MCP server cannot start and clients will show zero Unreal tools",
      "npm install");
  } else if (!existsSync(join(dist, "index.js"))) {
    err("build-missing", "mcp-server/dist/index.js is absent, so the MCP server cannot start and clients will show zero Unreal tools",
      "npm run build");
  } else {
    const srcT = newestMtime(src, [".ts"]);
    const distT = newestMtime(dist, [".js"]);
    if (srcT > distT) {
      const mins = Math.round((srcT - distT) / 60000);
      err("build-stale",
        `dist/ is ${mins} minute(s) older than src/. Clients are running an outdated tool set and may advertise tools the listener cannot serve.`,
        "npm run build, then restart your MCP client");
    } else {
      ok("build-fresh", "dist/ is newer than src/");
    }
  }
}

// ---------------------------------------------- 4. divergent clones
{
  const remote = git("config --get remote.origin.url");
  const myHead = git("rev-parse HEAD");
  const siblings = [
    "D:/Unreal Projects/UE4_Bridge",
    "D:/UE/UE_Bridge",
    "D:/Unreal Projects/MASTER_PROJECT/SF_Repository/Sinfeld_240301",
  ].filter((p) => {
    try { return statSync(p).isDirectory() && statSync(join(p, ".git")).isDirectory(); }
    catch { return false; }
  });

  let others = 0;
  for (const s of siblings) {
    const sReal = s.replace(/\//g, "\\").toLowerCase();
    if (sReal === repoRoot.toLowerCase()) continue;
    const sRemote = git("config --get remote.origin.url", s);
    if (!sRemote || !remote || sRemote !== remote) continue;
    others++;
    const sHead = git("rev-parse HEAD", s);
    const sBranch = git("rev-parse --abbrev-ref HEAD", s);
    if (sHead && sHead !== myHead) {
      const known = git(`cat-file -e ${sHead}`) !== null;
      warn("clone-divergent",
        `${s} is on ${sBranch} at ${sHead.slice(0, 7)}, different from this tree's HEAD${known ? "" : " (a commit this clone has never seen)"}`,
        "Decide which clone is canonical and sync the other with git. Two clones both called 'main' is how this project lost work before.");
    }
  }
  if (others === 0) ok("clone-divergent", "no sibling clone of this remote found");
  else if (!findings.some((f) => f.check === "clone-divergent" && f.level === "warn"))
    ok("clone-divergent", `${others} sibling clone(s) found, all at this HEAD`);
}

// ---------------------------------------------- 5. worktrees
{
  const wt = git("worktree list");
  if (wt) {
    const lines = wt.split("\n").filter(Boolean);
    if (lines.length > 1) {
      warn("worktrees",
        `${lines.length - 1} extra worktree(s). engine-source.ts once existed ONLY inside one of these and was nearly lost.`,
        "Before `git worktree remove`, diff each worktree's mcp-server/src/tools against this tree for files that exist nowhere else.");
    } else ok("worktrees", "no extra worktrees");
  }
}

// ---------------------------------------------- 6. unregistered tools
{
  const toolsDir = join(repoRoot, "mcp-server", "src", "tools");
  const indexPath = join(repoRoot, "mcp-server", "src", "index.ts");
  if (existsSync(toolsDir) && existsSync(indexPath)) {
    const index = readFileSync(indexPath, "utf-8");
    const orphans = readdirSync(toolsDir)
      .filter((f) => f.endsWith(".ts"))
      .filter((f) => !index.includes(`./tools/${f.replace(/\.ts$/, ".js")}`));
    if (orphans.length) {
      err("tool-unregistered",
        `src/tools/ contains ${orphans.length} file(s) index.ts never imports: ${orphans.join(", ")}`,
        "Either register them in index.ts, or move them to mcp-server/incubator/ if their listener handlers do not exist yet. An unregistered file in src/ still trips the registry-consistency test.");
    } else ok("tool-unregistered", "every file in src/tools is imported by index.ts");
  }
}

// ---------------------------------------------- 7. doc drift
{
  const agents = join(repoRoot, "AGENTS.md");
  if (existsSync(agents)) {
    const text = readFileSync(agents, "utf-8");
    const toolsDir = join(repoRoot, "mcp-server", "src", "tools");
    const actualModules = existsSync(toolsDir)
      ? readdirSync(toolsDir).filter((f) => f.endsWith(".ts")).length : 0;

    const claim = text.match(/`tools\/`\s*-\s*(\d+)\s*modules|(\d+)\s*tool modules/);
    const claimed = claim ? Number(claim[1] ?? claim[2]) : null;
    if (claimed !== null && claimed !== actualModules) {
      err("doc-drift",
        `AGENTS.md claims ${claimed} tool modules but src/tools has ${actualModules}`,
        "Update AGENTS.md. Documentation that describes a codebase which does not exist is how three clients ended up with three inconsistent instruction files.");
    } else if (claimed !== null) {
      ok("doc-drift", `AGENTS.md module count matches reality (${actualModules})`);
    }

    for (const stale of ["\.Codex/", "Codex\\.ai/code"]) {
      if (new RegExp(stale).test(text)) {
        err("doc-mangled",
          `AGENTS.md contains "${stale}", the signature of a find-and-replace copy of CLAUDE.md`,
          "AGENTS.md must be written client-neutral, not generated by substituting one client's name for another.");
      }
    }
  }
}

// ---------------------------------------------- 8. engine root
{
  const root = process.env.UE_ENGINE_ROOT;
  if (!root) {
    warn("engine-root",
      "UE_ENGINE_ROOT is not set in this shell, so engine_source_search and engine_source_read fail",
      "It is already set in .mcp.json and clients/, so MCP clients are fine. Export it for shell use: UE_ENGINE_ROOT=D:/UE/UE_4.27");
  } else if (!existsSync(join(root, "Engine", "Source"))) {
    err("engine-root", `UE_ENGINE_ROOT=${root} but ${join(root, "Engine", "Source")} does not exist`,
      "Point it at the directory containing Engine/Source.");
  } else {
    ok("engine-root", root);
  }
}

// ------------------------------------- 9. UBT module declarations
// Every module directory under Source/ must be declared in the .uplugin, or UBT
// silently never builds it. That is not hypothetical: MCPBridgePIEAgent and
// MCPBridgeClothOptimizer had sources and compiled DLLs but no declaration, so
// their sources drifted uncompiled until 2026-07-29.
{
  const sourceDir = join(repoRoot, "Plugins", "MCPBridge", "Source");
  const upluginPath = join(repoRoot, "Plugins", "MCPBridge", "MCPBridge.uplugin");
  if (existsSync(sourceDir) && existsSync(upluginPath)) {
    let declared = [];
    try {
      const raw = readFileSync(upluginPath, "utf-8").replace(/^﻿/, "");
      declared = (JSON.parse(raw).Modules ?? []).map((m) => m.Name);
    } catch (e) {
      err("uplugin-unparsable", `MCPBridge.uplugin is not valid JSON: ${e.message}`,
        "Fix the JSON. UBT cannot read the plugin at all in this state.");
    }
    const onDisk = readdirSync(sourceDir, { withFileTypes: true })
      .filter((e) => e.isDirectory())
      .filter((e) => existsSync(join(sourceDir, e.name, `${e.name}.Build.cs`)))
      .map((e) => e.name);

    const undeclared = onDisk.filter((m) => !declared.includes(m));
    const phantom = declared.filter((m) => !onDisk.includes(m));

    if (undeclared.length) {
      err("uplugin-module-undeclared",
        `${undeclared.join(", ")} has a .Build.cs but is not declared in MCPBridge.uplugin, so UBT never builds it`,
        "Add a Modules entry with Name, Type and LoadingPhase. Editor-only tooling is Type 'Editor'; anything registering a Slate tab or creating UObjects at startup wants LoadingPhase 'PostEngineInit'.");
    }
    if (phantom.length) {
      err("uplugin-module-phantom",
        `${phantom.join(", ")} is declared in MCPBridge.uplugin but has no .Build.cs on disk, so the editor fails to load the plugin`,
        "Remove the stale Modules entry, or restore the module source.");
    }
    if (!undeclared.length && !phantom.length) {
      ok("uplugin-modules", `${onDisk.length} module(s) on disk, all declared`);
    }
  }
}

// ------------------------------------- 10. port ownership and zombie servers
// Eight orphaned node processes were found still serving a deleted clone's MCP
// server against port 8080. A client that connected got three-month-old tools.
{
  let procs = [];
  try {
    const raw = execSync(
      'powershell -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like \'*mcp-server*dist*index.js*\' } | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress"',
      { encoding: "utf-8", stdio: ["ignore", "pipe", "ignore"], timeout: 30000 }
    ).trim();
    if (raw) { const p = JSON.parse(raw); procs = Array.isArray(p) ? p : [p]; }
  } catch { /* non-Windows or powershell unavailable */ }

  if (procs.length) {
    const canonical = repoRoot.replace(/\\/g, "/").toLowerCase();
    const foreign = procs.filter((p) => {
      const cl = (p.CommandLine ?? "").replace(/\\/g, "/").toLowerCase();
      // a server whose script path no longer exists, or lives outside this tree
      const m = cl.match(/([a-z]:\/[^"']*?mcp-server\/dist\/index\.js)/);
      if (!m) return false;
      return !existsSync(m[1]) || !m[1].startsWith(canonical);
    });
    if (foreign.length) {
      warn("mcp-server-foreign",
        `${foreign.length} MCP server process(es) running from outside this tree or from a deleted path (PIDs ${foreign.map((p) => p.ProcessId).join(", ")})`,
        "These answer the same clients and may serve an old tool set. Stop them: Stop-Process -Id <pid> -Force");
    } else {
      ok("mcp-server-processes", `${procs.length} running, all from this tree`);
    }
  } else {
    ok("mcp-server-processes", "none running");
  }
}

// ------------------------------------- 11. client configuration drift
// Codex and Gemini do not read .mcp.json. Their templates carry absolute paths,
// which rot silently when the tree moves.
{
  const checks = [
    { file: ".mcp.json", label: "Claude Code" },
    { file: join("clients", "codex-config.toml"), label: "Codex" },
    { file: join("clients", "gemini-settings.json"), label: "Gemini" },
  ];
  const problems = [];
  for (const c of checks) {
    const p = join(repoRoot, c.file);
    if (!existsSync(p)) { problems.push(`${c.label}: ${c.file} missing`); continue; }
    const text = readFileSync(p, "utf-8");
    if (!/UE_ENGINE_ROOT/.test(text)) {
      problems.push(`${c.label}: no UE_ENGINE_ROOT, so engine_source_* will fail`);
    }
    // absolute server paths must still resolve
    for (const m of text.matchAll(/([A-Za-z]:[\\/][^"']*?mcp-server[\\/]dist[\\/]index\.js)/g)) {
      if (!existsSync(m[1].replace(/\\/g, "/"))) {
        problems.push(`${c.label}: points at ${m[1]}, which does not exist`);
      }
    }
    for (const m of text.matchAll(/UE_ENGINE_ROOT["'\s:=]+([A-Za-z]:[\\/][^"'\n,}]*)/g)) {
      const root = m[1].trim().replace(/["',]+$/, "");
      if (!existsSync(join(root, "Engine", "Source"))) {
        problems.push(`${c.label}: UE_ENGINE_ROOT ${root} has no Engine/Source`);
      }
    }
  }
  if (problems.length) {
    err("client-config", problems.join("; "),
      "Fix the template. A client whose config points at a stale path fails with no useful message.");
  } else {
    ok("client-config", "Claude Code, Codex and Gemini configs all resolve");
  }
}

// ------------------------------------- 12. unreachable listener handlers
// A handler module nothing routes to is dead weight that reads as capability.
{
  const routerPath = join(repoRoot, "Plugins", "MCPBridge", "Content", "Python", "mcp_bridge", "router.py");
  const handlersDir = join(repoRoot, "Plugins", "MCPBridge", "Content", "Python", "mcp_bridge", "handlers");
  if (existsSync(routerPath) && existsSync(handlersDir)) {
    const router = readFileSync(routerPath, "utf-8");
    const orphans = readdirSync(handlersDir)
      .filter((f) => f.endsWith(".py") && f !== "__init__.py")
      .map((f) => f.replace(/\.py$/, ""))
      .filter((mod) => !new RegExp(`handlers\\.${mod}\\b|handlers import[^\\n]*\\b${mod}\\b`).test(router));
    if (orphans.length) {
      warn("handler-unrouted",
        `handler module(s) router.py never imports: ${orphans.join(", ")}`,
        "Either route them, or delete them. An unrouted handler looks like capability and is not.");
    } else {
      ok("handler-unrouted", "every handler module is imported by router.py");
    }
  }
}

// ---------------------------------------------------------------- report
const errors = findings.filter((f) => f.level === "error");
const warns = findings.filter((f) => f.level === "warn");

console.log("bridge doctor\n");
for (const f of findings) {
  if (quiet && f.level === "ok") continue;
  const tag = f.level === "ok" ? "  ok  " : f.level === "warn" ? " WARN " : " ERROR";
  console.log(`${tag} ${f.check}${f.detail ? `: ${f.detail}` : ""}`);
  if (f.fix && f.level !== "ok") console.log(`        fix: ${f.fix}`);
}
console.log("");
console.log(`${findings.length - errors.length - warns.length} ok, ${warns.length} warning(s), ${errors.length} error(s)`);
process.exit(errors.length > 0 ? 1 : 0);
