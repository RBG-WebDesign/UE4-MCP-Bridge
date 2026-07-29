#!/usr/bin/env node
/**
 * Repository layout checker.
 *
 * Enforces what docs/REPOSITORY_LAYOUT.md describes, so the rules are executable
 * rather than a document nobody runs. Checks:
 *
 *   1. No unexpected entries at the repository root
 *   2. Language ownership per directory (TS / Python / C++ / Markdown)
 *   3. No manual backup directories (use Git or Perforce history instead)
 *   4. No game content tracked in this bridge-only repo
 *   5. Scratch output is under Saved/AgentScratch/, not loose in the tree
 *
 * Usage:  npm run check:layout
 * Exit 0 clean, 1 on any violation.
 */

import { execSync } from "node:child_process";
import { readdirSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join, extname } from "node:path";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const violations = [];
function fail(rule, detail) { violations.push({ rule, detail }); }

// ---------------------------------------------------------------- 1. root
const ALLOWED_ROOT = new Set([
  ".claude", ".git", ".gitattributes", ".github", ".gitignore", ".githooks",
  ".mcp.json", ".vscode",
  "AGENTS.md", "CLAUDE.md", "GEMINI.md",
  "README.md", "README_PROMPTBRUSH.md",
  "Plugins", "Scripts", "agents", "clients", "docs", "mcp-server", "skills",
  "tests",
  "orchestrator.mjs", "package.json", "package-lock.json",
  "node_modules", "_releases",
]);

for (const entry of readdirSync(repoRoot)) {
  if (!ALLOWED_ROOT.has(entry)) {
    fail("unexpected-root-entry",
      `"${entry}" is not an allowed root entry. Permanent docs go in docs/<category>/, ` +
      `reusable automation in Scripts/, temporary output in Saved/AgentScratch/<agent>/<task-slug>/. ` +
      `If this belongs at the root, add it to ALLOWED_ROOT in Scripts/check-layout.mjs with a reason.`);
  }
}

// ------------------------------------------------------- 2. file ownership
const OWNERSHIP = [
  {
    dir: "mcp-server/src",
    allow: [".ts"],
    label: "mcp-server/src is TypeScript only",
  },
  {
    dir: "Plugins/MCPBridge/Content/Python",
    allow: [".py", ".md", ".json", ".txt", ".ini"],
    label: "the listener tree is Python only (plus data and docs)",
  },
  {
    dir: "Plugins/MCPBridge/Source",
    allow: [".cpp", ".h", ".cs", ".inl", ".md"],
    label: "Plugins/MCPBridge/Source is C++ only (plus .Build.cs and docs)",
  },
  { dir: "docs", allow: [".md", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".json"], label: "docs/ is Markdown and assets only" },
];

function walk(dir, out = []) {
  if (!existsSync(dir)) return out;
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    if (e.name === "node_modules" || e.name === "__pycache__" || e.name === ".git") continue;
    const p = join(dir, e.name);
    if (e.isDirectory()) walk(p, out);
    else out.push(p);
  }
  return out;
}

for (const rule of OWNERSHIP) {
  const abs = join(repoRoot, rule.dir);
  for (const file of walk(abs)) {
    const ext = extname(file).toLowerCase();
    if (ext && !rule.allow.includes(ext)) {
      fail("ownership-violation",
        `${file.replace(repoRoot + "\\", "").replace(repoRoot + "/", "")} - ${rule.label} (found ${ext})`);
    }
  }
}

// --------------------------------------------------- 3. manual backup dirs
for (const entry of readdirSync(repoRoot)) {
  if (/backup|_bak$|^_legacy/i.test(entry) && entry !== "node_modules") {
    fail("manual-backup",
      `"${entry}" looks like a hand-made backup. Use Git or Perforce history instead.`);
  }
}

// ------------------------------------------- 4. no game content tracked here
let tracked = [];
try {
  tracked = execSync("git ls-files", { cwd: repoRoot, encoding: "utf-8" }).split("\n").filter(Boolean);
} catch {
  fail("git-unavailable", "could not run git ls-files; layout check is incomplete");
}

const GAME_PATHS = [/^Content\//, /^Source\//, /^Config\//, /^Saved\//, /\.uproject$/, /\.uasset$/, /\.umap$/];
for (const f of tracked) {
  if (GAME_PATHS.some((re) => re.test(f))) {
    fail("game-content-tracked",
      `${f} - this repo is bridge-only and its remote is public. Game code and content belong in Perforce.`);
  }
}

// ------------------------------------------------------ 5. scratch location
for (const f of tracked) {
  if (/^(pass\d+_report|.*_report|scratch|tmp|temp)\.(json|txt|log)$/i.test(f)) {
    fail("scratch-at-root",
      `${f} - temporary output belongs in Saved/AgentScratch/<agent>/<task-slug>/, not tracked at the root.`);
  }
}

// ---------------------------------------------------------------- report
if (violations.length === 0) {
  console.log("check:layout  PASS  no layout violations");
  process.exit(0);
}

const byRule = new Map();
for (const v of violations) {
  if (!byRule.has(v.rule)) byRule.set(v.rule, []);
  byRule.get(v.rule).push(v.detail);
}
console.log(`check:layout  FAIL  ${violations.length} violation(s)\n`);
for (const [rule, details] of byRule) {
  console.log(`  [${rule}]`);
  for (const d of details) console.log(`    - ${d}`);
  console.log("");
}
console.log("See docs/REPOSITORY_LAYOUT.md for the rules.");
process.exit(1);
