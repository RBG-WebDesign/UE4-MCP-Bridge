#!/usr/bin/env node
/**
 * Merge one lane branch into the integration branch.
 *
 * This exists because a plain `git merge` produces conflicts that CANNOT be
 * resolved by keeping both sides, and the failure is silent enough to reach a
 * compiler before anyone notices.
 *
 * Every lane in this program appends to the same handful of files: the service
 * header and cpp, the module Build.cs, the tool schemas, the runtime registry,
 * the PuerTS typings, the annotations table and the test suite. Two lanes
 * appending at the same place is a conflict git reports as "both added", and
 * git FACTORS OUT THE COMMON SUFFIX of the two hunks. For a list of strings
 * that is harmless. For code it is not: both sides end mid-function and share
 * one tail, so keeping both sides closes one function and leaves the other
 * open. The result is a syntactically broken file whose error appears hundreds
 * of lines away, or a UHT "Missing '*' in Expected a pointer type" pointing at
 * a declaration that reads perfectly.
 *
 * So there are two strategies here and the file decides which one applies:
 *
 *   UNION   append-only files. Keep both sides verbatim, in order.
 *   GRAFT   files whose conflicts split declarations. Throw the merged text
 *           away, take the version already on the integration branch, and
 *           re-add whole brace-matched units taken from the lane's own file.
 *
 * Graft is correct because a whole function copied out of the lane's file is
 * complete by construction. It never depends on how git chose to align the two
 * versions.
 *
 * NOT automated on purpose: the tool count assertions and the generated files.
 * A count is one shared scalar that every lane moves and only a real run knows
 * the answer; the generated files must be regenerated, never merged.
 *
 *   node Scripts/merge-lane.mjs lane/x-name            resolve, leave staged
 *   node Scripts/merge-lane.mjs lane/x-name --dry      report the plan only
 *
 * After it returns, ALWAYS:
 *   npx tsc -p puerts-runtime/tsconfig.json && npx tsc -p mcp-server/tsconfig.json
 *   node Scripts/generate-tool-inventory.mjs --write
 *   npm run verify
 * and only then commit. Typecheck before Unreal ever sees it.
 */

import { execFileSync } from "node:child_process";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const git = (...args) =>
  execFileSync("git", args, { cwd: repoRoot, encoding: "utf-8", maxBuffer: 64 * 1024 * 1024 });

/** Files whose conflicts split declarations. These are grafted, never unioned. */
const GRAFT = [
  {
    file: "puerts-runtime/src/registry.ts",
    kind: "functions",
    fnAnchor: "async function buildPhysics(",
    rowAnchor: '{ name: "physics_build"',
  },
  {
    file: "mcp-server/src/tools/puerts.ts",
    kind: "rows",
    rowAnchor: '["puerts_physics_build"',
  },
  {
    file: "Plugins/MCPBridge/Source/MCPBridgePuerTS/Public/MCPPuerTSBridgeService.h",
    kind: "ufunctions",
  },
];

/** Regenerated after the merge set, never merged as text. */
const GENERATED = [
  "docs/TOOL_INVENTORY.json",
  "docs/CAPABILITY_SCOREBOARD.json",
  "docs/CAPABILITY_PRESERVATION_AUDIT.md",
];

/** Left for a human: one shared scalar that only a real run can settle. */
const MANUAL = ["mcp-server/tests/puerts-tools.test.ts", "mcp-server/tests/compat-tools.test.ts"];

// ---------------------------------------------------------------- union

/**
 * Keep both sides of every conflict, verbatim and in order.
 *
 * No deduplication. An earlier version of this deduplicated by trimmed line
 * content, which looked reasonable for a module list and silently ate the
 * repeated closing braces that every block of code ends with.
 */
function union(text) {
  const lines = text.split(/\r?\n/);
  const out = [];
  let mode = "normal";
  let ours = [];
  let theirs = [];
  let blocks = 0;
  for (const line of lines) {
    if (line.startsWith("<<<<<<<")) { mode = "ours"; ours = []; theirs = []; blocks++; continue; }
    if (line.startsWith("=======") && mode === "ours") { mode = "theirs"; continue; }
    if (line.startsWith(">>>>>>>") && mode === "theirs") { out.push(...ours, ...theirs); mode = "normal"; continue; }
    if (mode === "ours") ours.push(line);
    else if (mode === "theirs") theirs.push(line);
    else out.push(line);
  }
  return { text: out.join("\n"), blocks };
}

// ---------------------------------------------------------------- graft

/** Walk from `start` to the line that closes the first brace group. */
function braceBlock(lines, start, open = "{", close = "}") {
  let depth = 0;
  let seen = false;
  for (let k = start; k < lines.length; k++) {
    for (const c of lines[k]) {
      if (c === open) { depth++; seen = true; }
      else if (c === close) depth--;
    }
    if (seen && depth === 0) return k;
  }
  return start;
}

/** Include an immediately preceding /** ... *\/ comment. */
function withLeadingComment(lines, start) {
  if (!lines[start - 1]?.trim().endsWith("*/")) return start;
  let j = start - 1;
  while (j >= 0 && !lines[j].trim().startsWith("/**")) j--;
  return j >= 0 ? j : start;
}

const FN = /^(?:export\s+)?(?:async\s+)?function\s+(\w+)\s*\(/;

function topLevelFunctions(lines) {
  const found = new Map();
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(FN);
    if (!m) continue;
    const end = braceBlock(lines, i);
    found.set(m[1], lines.slice(withLeadingComment(lines, i), end + 1));
    i = end;
  }
  return found;
}

/** UFUNCTION declarations, keyed by method name. A declaration runs from its
 *  comment through the line containing ");". */
function ufunctions(lines) {
  const found = new Map();
  for (let i = 0; i < lines.length; i++) {
    if (!/^\s*UFUNCTION\s*\(/.test(lines[i])) continue;
    const start = withLeadingComment(lines, i);
    let end = i;
    let name = null;
    for (let k = i + 1; k < lines.length; k++) {
      const m = lines[k].match(/\b(\w+)\s*\(/);
      if (m && !name) name = m[1];
      if (lines[k].includes(");")) { end = k; break; }
      if (/^\s*UFUNCTION\s*\(/.test(lines[k])) { end = k - 1; break; }
    }
    if (name) found.set(name, lines.slice(start, end + 1));
    i = end;
  }
  return found;
}

/** Bracket-matched tuple rows, keyed by the tool name they start with. */
function tupleRows(lines) {
  const found = new Map();
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(/^\s*\["(\w+)"/);
    if (!m) continue;
    const end = braceBlock(lines, i, "[", "]");
    found.set(m[1], lines.slice(i, end + 1));
    i = end;
  }
  return found;
}

function graftFile(spec, branch) {
  const path = join(repoRoot, spec.file);
  if (!existsSync(path)) return `${spec.file}: absent, skipped`;

  // Throw the conflicted merge away and start from the integration branch.
  git("checkout", "HEAD", "--", spec.file);
  const dst = readFileSync(path, "utf-8").split(/\r?\n/);
  const src = git("show", `${branch}:${spec.file}`).split(/\r?\n/);

  const notes = [];
  let out = dst;

  if (spec.kind === "functions" || spec.kind === "rows") {
    if (spec.kind === "functions") {
      const have = topLevelFunctions(dst);
      const mine = topLevelFunctions(src);
      const added = [...mine.keys()].filter((n) => !have.has(n));
      const rows = src.filter((l) => /^\s*\{\s*name:\s*"/.test(l) && added.some((n) => l.includes(`execute: ${n}`)));
      const next = [];
      let didFns = false;
      let didRows = false;
      for (const line of out) {
        if (!didFns && added.length && line.startsWith(spec.fnAnchor)) {
          for (const n of added) next.push(...mine.get(n), "");
          didFns = true;
        }
        if (!didRows && rows.length && line.includes(spec.rowAnchor)) { next.push(...rows); didRows = true; }
        next.push(line);
      }
      out = next;
      notes.push(`fns +${added.length} (${added.join(", ") || "none"})`, `rows +${rows.length}`);
      if (added.length && !didFns) notes.push("ANCHOR MISS: function anchor not found");
    } else {
      const have = tupleRows(dst);
      const mine = tupleRows(src);
      const added = [...mine.keys()].filter((n) => !have.has(n));
      const next = [];
      let done = false;
      for (const line of out) {
        if (!done && added.length && line.includes(spec.rowAnchor)) {
          for (const n of added) next.push(...mine.get(n));
          done = true;
        }
        next.push(line);
      }
      out = next;
      notes.push(`rows +${added.length} (${added.join(", ") || "none"})`);
      if (added.length && !done) notes.push("ANCHOR MISS: row anchor not found");
    }
  } else if (spec.kind === "ufunctions") {
    const have = ufunctions(dst);
    const mine = ufunctions(src);
    const added = [...mine.keys()].filter((n) => !have.has(n));
    // Insert before the class-closing brace, which is the last "};" in the file.
    let anchor = -1;
    for (let i = out.length - 1; i >= 0; i--) if (out[i].trim() === "};") { anchor = i; break; }
    if (anchor < 0) return `${spec.file}: no class-closing brace found, LEFT ALONE`;
    const next = out.slice(0, anchor);
    for (const n of added) next.push("", ...mine.get(n));
    next.push(...out.slice(anchor));
    out = next;
    notes.push(`UFUNCTIONs +${added.length} (${added.join(", ") || "none"})`);
  }

  writeFileSync(path, out.join("\n"));
  return `${spec.file}: ${notes.join(", ")}`;
}

// ---------------------------------------------------------------- main

function main() {
  const branch = process.argv[2];
  const dry = process.argv.includes("--dry");
  if (!branch) {
    console.error("usage: node Scripts/merge-lane.mjs lane/<name> [--dry]");
    process.exit(2);
  }

  try {
    git("merge", "--no-ff", branch, "-m", `Integrate ${branch}`);
    console.log(`${branch}: merged with no conflicts`);
    return;
  } catch {
    // Conflicts are the normal case for this repo's lane shape.
  }

  const conflicted = git("diff", "--name-only", "--diff-filter=U").split("\n").filter(Boolean);
  console.log(`${branch}: ${conflicted.length} conflicted file(s)`);

  const graftPaths = new Set(GRAFT.map((g) => g.file));
  const plan = conflicted.map((f) => {
    if (graftPaths.has(f)) return [f, "GRAFT"];
    if (GENERATED.includes(f)) return [f, "REGENERATE"];
    if (MANUAL.includes(f)) return [f, "MANUAL"];
    return [f, "UNION"];
  });
  for (const [f, how] of plan) console.log(`  ${how.padEnd(10)} ${f}`);
  if (dry) { console.log("\n--dry: nothing written"); return; }

  console.log("");
  for (const [f, how] of plan) {
    if (how === "UNION") {
      const path = join(repoRoot, f);
      const { text, blocks } = union(readFileSync(path, "utf-8"));
      writeFileSync(path, text);
      console.log(`  unioned ${f} (${blocks} block(s))`);
    } else if (how === "GRAFT") {
      console.log(`  ${graftFile(GRAFT.find((g) => g.file === f), branch)}`);
    } else if (how === "REGENERATE") {
      try { git("checkout", "--theirs", "--", f); } catch { /* either side is fine, it is regenerated */ }
      console.log(`  ${f}: taken from the lane, REGENERATE before committing`);
    }
  }

  const manual = plan.filter(([, how]) => how === "MANUAL").map(([f]) => f);
  console.log("\nNOT resolved, and deliberately so:");
  for (const f of manual) console.log(`  ${f}  <- one shared count, only a real run knows it`);
  console.log("\nNext, in this order:");
  console.log("  1. resolve the count assertion(s) above by hand");
  console.log("  2. npx tsc -p puerts-runtime/tsconfig.json && npx tsc -p mcp-server/tsconfig.json");
  console.log("  3. node Scripts/generate-tool-inventory.mjs --write");
  console.log("  4. npm run verify");
  console.log("  5. git add -A && git commit");
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href.toLowerCase() === import.meta.url.toLowerCase()) {
  main();
}
