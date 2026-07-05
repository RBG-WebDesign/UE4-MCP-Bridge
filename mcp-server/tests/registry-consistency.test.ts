/**
 * Registry consistency test.
 *
 * Statically cross-checks the TypeScript tool layer against the Python
 * command router in the tracked plugin tree. Catches:
 *   1. Commands the TS server sends that have no Python route
 *      (advertised-but-dead tools -- callers would get "Unknown command").
 *   2. Python routes with no TypeScript caller that are not on the
 *      documented internal-only allowlist (handlers missing schemas).
 *
 * Pure file parsing. No UE4 instance and no mock server required.
 */

import { readFileSync, readdirSync, statSync } from "fs";
import { join, dirname } from "path";
import { fileURLToPath } from "url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..");
const routerPath = join(
  repoRoot,
  "Plugins", "MCPBridge", "Content", "Python", "mcp_bridge", "router.py"
);
const srcRoot = join(here, "..", "src");

// This test cross-checks the TS tool layer against the plugin's Python
// router, which only exists in the mono-repo. In the standalone server
// repository the router is not present; skip with a clear notice rather
// than fail (the mono-repo CI still enforces consistency).
import { existsSync } from "fs";
if (!existsSync(routerPath)) {
  console.log(
    "  SKIP  registry consistency: plugin router.py not present " +
    "(standalone server checkout; enforced in the mono-repo CI)"
  );
  process.exit(0);
}

// Routes that intentionally have no TypeScript tool schema. Each entry must
// be documented in docs/TOOL_REFERENCE.md or the router itself as internal.
const INTERNAL_ONLY_ROUTES = new Set<string>([
  // Alias of clear_output_log kept for backward compatibility.
  "bridge_clear_log",
  // Called by the C++ MCPBridge editor panel over HTTP, not by MCP clients.
  "bridge_status",
  "bridge_latest_notification",
  "bridge_copy_project_info",
  "bridge_self_test",
]);

// Route prefixes reserved for panel-driven internal suites (no MCP schema yet).
const INTERNAL_ONLY_PREFIXES = ["optimization_"];

let passed = 0;
let failed = 0;

function check(name: string, fn: () => void): void {
  try {
    fn();
    passed += 1;
    console.log(`  PASS  ${name}`);
  } catch (err) {
    failed += 1;
    console.error(`  FAIL  ${name}`);
    console.error(`        ${(err as Error).message}`);
  }
}

function listTsFiles(dir: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(dir)) {
    const full = join(dir, entry);
    if (statSync(full).isDirectory()) {
      out.push(...listTsFiles(full));
    } else if (entry.endsWith(".ts")) {
      out.push(full);
    }
  }
  return out;
}

function parseRouterRoutes(): Set<string> {
  const src = readFileSync(routerPath, "utf-8");
  const start = src.indexOf("COMMAND_ROUTES");
  if (start < 0) throw new Error(`COMMAND_ROUTES not found in ${routerPath}`);
  const braceStart = src.indexOf("{", start);
  // The dispatch table ends at the first closing brace at column 0.
  const end = src.indexOf("\n}", braceStart);
  const body = src.slice(braceStart, end);
  const routes = new Set<string>();
  for (const match of body.matchAll(/"([a-z0-9_]+)"\s*:/g)) {
    routes.add(match[1]);
  }
  if (routes.size === 0) throw new Error("Parsed zero routes from router.py");
  return routes;
}

function parseSentCommands(): Set<string> {
  const sent = new Set<string>();
  for (const file of listTsFiles(srcRoot)) {
    const src = readFileSync(file, "utf-8");
    for (const match of src.matchAll(/sendCommand\(\s*["']([a-z0-9_]+)["']/g)) {
      sent.add(match[1]);
    }
  }
  if (sent.size === 0) throw new Error("Parsed zero sendCommand calls from TS source");
  return sent;
}

const routes = parseRouterRoutes();
const sent = parseSentCommands();

check("every TS-sent command has a Python route in the tracked plugin", () => {
  const missing = [...sent].filter((cmd) => !routes.has(cmd)).sort();
  if (missing.length > 0) {
    throw new Error(
      `Commands sent by the MCP server with no route in router.py ` +
      `(advertised-but-dead tools): ${missing.join(", ")}`
    );
  }
});

check("every Python route is either called from TS or allowlisted internal-only", () => {
  const orphans = [...routes]
    .filter((route) =>
      !sent.has(route) &&
      !INTERNAL_ONLY_ROUTES.has(route) &&
      !INTERNAL_ONLY_PREFIXES.some((prefix) => route.startsWith(prefix)))
    .sort();
  if (orphans.length > 0) {
    throw new Error(
      `Routes with no TypeScript caller and no internal-only allowlist entry ` +
      `(handlers missing schemas): ${orphans.join(", ")}`
    );
  }
});

check("internal-only allowlist contains no stale entries", () => {
  const stale = [...INTERNAL_ONLY_ROUTES].filter((route) => !routes.has(route)).sort();
  if (stale.length > 0) {
    throw new Error(`Allowlisted routes no longer present in router.py: ${stale.join(", ")}`);
  }
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
