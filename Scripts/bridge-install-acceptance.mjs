#!/usr/bin/env node
// Acceptance for the install gate. Sabotage is the whole method: a gate that has
// only ever been run against a correct install has not been shown to reject
// anything, and "it passed" is the answer a broken gate gives too.
//
// Every case below takes a known-good install, breaks exactly one thing in a
// COPY, and requires the gate to fail AND to name the group that was broken.
// Naming matters as much as failing: the report is what tells someone whether to
// rebuild, re-run npm run build, or re-sync, and a gate that just says "stale"
// sends them looking through all three.
//
//   node Scripts/bridge-install-acceptance.mjs [--from <a currently passing project>]
//
// No editor and no UBT: the gate is pure file comparison, so its own test is too.

import { createHash } from "node:crypto";
import {
  appendFileSync, cpSync, existsSync, mkdtempSync, readFileSync, readdirSync,
  rmSync, statSync, unlinkSync, writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkInstall, MANIFEST_NAME } from "./bridge-install.mjs";

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const argv = process.argv.slice(2);
const fromAt = argv.indexOf("--from");
const source = fromAt >= 0 ? argv[fromAt + 1] : process.env.MCP_UNREAL_PROJECT_ROOT;
if (!source) throw new Error("Pass --from <project> or set MCP_UNREAL_PROJECT_ROOT to a passing install");

const failures = [];
function assert(condition, label) {
  if (condition) { console.log(`  PASS  ${label}`); return true; }
  console.log(`  FAIL  ${label}`); failures.push(label); return false;
}

const scratch = mkdtempSync(join(tmpdir(), "bridge-install-acceptance-"));
const clones = [];
function clone(name) {
  const dir = join(scratch, name);
  cpSync(source, dir, { recursive: true });
  clones.push(dir);
  return dir;
}

// A fingerprint of the entire project tree, used to prove that --check writes
// nothing. Sizes and mtimes are included deliberately: a check that rewrote a
// file with identical bytes would still be a check that writes.
function treeFingerprint(root) {
  const parts = [];
  const visit = (dir, prefix) => {
    for (const entry of readdirSync(dir, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const full = join(dir, entry.name);
      const rel = prefix ? `${prefix}/${entry.name}` : entry.name;
      if (entry.isDirectory()) visit(full, rel);
      else if (entry.isFile()) {
        const s = statSync(full);
        parts.push(`${rel}:${s.size}:${s.mtimeMs}`);
      }
    }
  };
  visit(root, "");
  return createHash("sha256").update(parts.join("\n")).digest("hex");
}

const firstFile = (dir, filter) => {
  const stack = [dir];
  while (stack.length) {
    const d = stack.pop();
    for (const e of readdirSync(d, { withFileTypes: true })) {
      const full = join(d, e.name);
      if (e.isDirectory()) stack.push(full);
      else if (filter(full)) return full;
    }
  }
  return null;
};

try {
  console.log(`install gate acceptance\n  source install: ${source}\n  scratch:        ${scratch}\n`);

  console.log("-- 0. the unmodified install is accepted");
  const baseline = clone("baseline");
  const base = checkInstall(baseline);
  assert(base.ok, `a correct installation passes${base.ok ? "" : ` [${base.problems.join("; ")}]`}`);

  console.log("\n-- 1. verification mode writes nothing");
  const before = treeFingerprint(baseline);
  checkInstall(baseline);
  checkInstall(baseline);
  assert(treeFingerprint(baseline) === before,
    "two --check runs leave the project tree byte-for-byte and timestamp identical");

  console.log("\n-- 2. a stale native source copy is rejected");
  const nativeStale = clone("native-stale");
  const cpp = firstFile(join(nativeStale, "Plugins", "MCPBridge", "Source"), (f) => f.endsWith(".cpp"));
  appendFileSync(cpp, "\n// stale: an edit the repository does not have\n");
  const nativeResult = checkInstall(nativeStale);
  assert(!nativeResult.ok, "a modified installed .cpp is rejected");
  assert(nativeResult.report.stale_groups.includes("native_source"),
    `the report names native_source (named: ${nativeResult.report.stale_groups.join(", ") || "nothing"})`);

  console.log("\n-- 3. a stale JavaScript copy is rejected, independently of native source");
  const jsStale = clone("js-stale");
  appendFileSync(join(jsStale, "Plugins", "MCPBridge", "Content", "JavaScript", "runtime.js"),
    "\n// stale: an edit the repository does not have\n");
  const jsResult = checkInstall(jsStale);
  assert(!jsResult.ok, "a modified installed runtime.js is rejected");
  assert(jsResult.report.stale_groups.includes("content_javascript"),
    `the report names content_javascript (named: ${jsResult.report.stale_groups.join(", ") || "nothing"})`);
  assert(!jsResult.report.stale_groups.includes("native_source"),
    "native source is NOT reported stale: the two groups are independent, which is why they are separate");

  console.log("\n-- 4. a stale DLL is rejected");
  const dllStale = clone("dll-stale");
  const dll = firstFile(join(dllStale, "Plugins", "MCPBridge", "Binaries"), (f) => f.endsWith(".dll"));
  appendFileSync(dll, "stale");
  const dllResult = checkInstall(dllStale);
  assert(!dllResult.ok, "a DLL that is not the binary built at install time is rejected");
  assert(dllResult.problems.some((p) => p.startsWith("dll:")),
    `the report names the dll (got: ${dllResult.problems.join("; ")})`);

  console.log("\n-- 5. sources newer than the binary are rejected even when nothing else moved");
  const notRebuilt = clone("not-rebuilt");
  const anyCpp = firstFile(join(notRebuilt, "Plugins", "MCPBridge", "Source"), (f) => f.endsWith(".cpp"));
  const contents = readFileSync(anyCpp);
  const future = new Date(Date.now() + 60_000);
  writeFileSync(anyCpp, contents); // same bytes, newer mtime: synced but not rebuilt
  const { utimesSync } = await import("node:fs");
  utimesSync(anyCpp, future, future);
  const rebuildResult = checkInstall(notRebuilt);
  assert(!rebuildResult.ok, "an install whose sources are newer than every DLL is rejected");
  assert(rebuildResult.problems.some((p) => p.includes("synced but not rebuilt")),
    `the report says the project was not rebuilt (got: ${rebuildResult.problems.join("; ")})`);

  console.log("\n-- 6. a mismatched PuerTS pin is rejected");
  const pinStale = clone("pin-stale");
  const pinned = join(pinStale, "Plugins", "Puerts", "Puerts.uplugin");
  assert(existsSync(pinned), "the pinned bundle is present to sabotage");
  appendFileSync(pinned, "\n");
  const pinResult = checkInstall(pinStale);
  assert(!pinResult.ok, "an altered pinned PuerTS file is rejected");
  assert(pinResult.problems.some((p) => p.startsWith("puerts_pin:")),
    `the report names puerts_pin (got: ${pinResult.problems.join("; ")})`);

  console.log("\n-- 7. an unpinned file where the runtime would load it is rejected");
  const injected = clone("pin-injected");
  writeFileSync(join(injected, "Plugins", "Puerts", "Content", "JavaScript", "injected.js"), "// not in the lock\n");
  const injectedResult = checkInstall(injected);
  assert(!injectedResult.ok, "an unpinned file under the bundle's Content/ is rejected");
  assert(injectedResult.problems.some((p) => p.includes("unpinned file in a loaded directory")),
    `the report says why it matters (got: ${injectedResult.problems.join("; ")})`);

  console.log("\n-- 8. an unmanaged copy with no manifest is rejected");
  const noManifest = clone("no-manifest");
  unlinkSync(join(noManifest, "Plugins", "MCPBridge", MANIFEST_NAME));
  const noManifestResult = checkInstall(noManifest);
  assert(!noManifestResult.ok, "a plugin copy with no install manifest is rejected");
  assert(noManifestResult.problems.some((p) => p.includes("unmanaged plugin copy")),
    "the report says the copy is unmanaged rather than merely stale");

  console.log("\n-- 9. a deprecated target is blocked even when it is current");
  const deprecated = clone("deprecated");
  const manifestPath = join(deprecated, "Plugins", "MCPBridge", MANIFEST_NAME);
  const manifest = JSON.parse(readFileSync(manifestPath, "utf8"));
  manifest.status = "deprecated";
  manifest.deprecated_reason = "retired in favour of another target";
  writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + "\n");
  const deprecatedResult = checkInstall(deprecated);
  assert(!deprecatedResult.ok, "a target marked deprecated is blocked from acceptance");
  assert(deprecatedResult.problems.some((p) => p.includes("deprecated")),
    "the report gives the recorded reason");

  console.log("\n-- 10. the catalog is checked, not just the file hash");
  const catalogBroken = clone("catalog-broken");
  const registry = join(catalogBroken, "Plugins", "MCPBridge", "Content", "JavaScript", "registry.js");
  writeFileSync(registry, readFileSync(registry, "utf8").replace('{ name: "graph_inspect"', '{ name: "graph_inspekt"'));
  const catalogResult = checkInstall(catalogBroken);
  assert(!catalogResult.ok, "a registry missing a native command is rejected");
  assert(catalogResult.problems.some((p) => p.startsWith("catalog:") && p.includes("graph_inspect")),
    `the report names the missing command (got: ${catalogResult.problems.filter((p) => p.startsWith("catalog:")).join("; ")})`);

  console.log(`\n${failures.length === 0 ? "all checks passed" : `${failures.length} check(s) did not hold:`}`);
  for (const f of failures) console.log(`  - ${f}`);
  process.exitCode = failures.length === 0 ? 0 : 1;
} finally {
  for (const dir of clones) rmSync(dir, { recursive: true, force: true });
  rmSync(scratch, { recursive: true, force: true });
}
