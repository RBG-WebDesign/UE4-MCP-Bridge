/**
 * Validate a project-local extension manifest without starting the server.
 *
 * Usage:  npx tsx tests/extension-manifest-check.ts <path-to-extension-dir-or-json>
 *
 * Prints the tool names the loader would register, their annotations, and any
 * validation failure with the offending field named. Exits non-zero on failure
 * so a host project can gate on it.
 */

import { loadManifest } from "../src/extensions.js";
import { existsSync, statSync } from "node:fs";
import { join } from "node:path";

const arg = process.argv[2];
if (!arg) {
  console.error("usage: tsx tests/extension-manifest-check.ts <extension dir or extension.json>");
  process.exit(2);
}
const manifestPath = existsSync(arg) && statSync(arg).isDirectory() ? join(arg, "extension.json") : arg;

try {
  const m = loadManifest(manifestPath);
  const prefix = m.toolPrefix ?? `${m.name}_`;
  console.log(`\n${m.name} v${m.version}`);
  console.log(`${m.description}\n`);
  const byAnnotation = new Map<string, number>();
  for (const t of m.tools) {
    byAnnotation.set(t.annotations, (byAnnotation.get(t.annotations) ?? 0) + 1);
  }
  for (const t of m.tools) {
    const params = t.params ? Object.keys(t.params).length : 0;
    const flags = [t.annotations, t.runtimeOnly ? "runtimeOnly" : null].filter(Boolean).join(", ");
    console.log(`  ${prefix}${t.name}`);
    console.log(`      -> ${t.command}${t.fixedParams ? ` ${JSON.stringify(t.fixedParams)}` : ""}  (${params} param${params === 1 ? "" : "s"}; ${flags})`);
  }
  console.log(`\n${m.tools.length} tool(s)`);
  for (const [k, v] of byAnnotation) console.log(`  ${k}: ${v}`);
  const commands = [...new Set(m.tools.map((t) => t.command))];
  console.log(`\nlistener commands required: ${commands.join(", ")}`);
  console.log("These must exist in the host project's router. The core registry-consistency");
  console.log("test does not cover extensions: it only scans the bridge's own src/.\n");
  process.exit(0);
} catch (e) {
  console.error(`\nINVALID: ${(e as Error).message}\n`);
  process.exit(1);
}
