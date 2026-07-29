/**
 * Extension loader tests.
 *
 * The point of extensions is to keep game-specific tools out of the shipped
 * catalog without deleting them. That only holds if the loader is strict about
 * manifests and permissive about failure: a bad manifest must be skipped, never
 * fatal, or one broken project-local file stops the bridge from starting.
 */

import { mkdtempSync, mkdirSync, writeFileSync, rmSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

import { UnrealClient } from "../src/unreal-client.js";
import {
  loadManifest,
  loadExtensions,
  discoverManifestPaths,
} from "../src/extensions.js";

let passed = 0;
let failed = 0;
function test(name: string, fn: () => void | Promise<void>): void {
  try {
    const r = fn();
    if (r instanceof Promise) throw new Error("use testAsync for async tests");
    console.log(`  PASS  ${name}`);
    passed++;
  } catch (e) {
    console.log(`  FAIL  ${name}`);
    console.log(`        ${(e as Error).message}`);
    failed++;
  }
}
async function testAsync(name: string, fn: () => Promise<void>): Promise<void> {
  try {
    await fn();
    console.log(`  PASS  ${name}`);
    passed++;
  } catch (e) {
    console.log(`  FAIL  ${name}`);
    console.log(`        ${(e as Error).message}`);
    failed++;
  }
}
function assert(cond: unknown, msg: string): asserts cond {
  if (!cond) throw new Error(msg);
}
function throws(fn: () => unknown, match: RegExp, msg: string): void {
  try {
    fn();
  } catch (e) {
    assert(match.test((e as Error).message), `${msg}: message was "${(e as Error).message}"`);
    return;
  }
  throw new Error(`${msg}: nothing thrown`);
}

const root = mkdtempSync(join(tmpdir(), "bridge-ext-"));
function writeExt(name: string, manifest: unknown): string {
  const dir = join(root, "BridgeExtensions", name);
  mkdirSync(dir, { recursive: true });
  const p = join(dir, "extension.json");
  writeFileSync(p, typeof manifest === "string" ? manifest : JSON.stringify(manifest, null, 2));
  return p;
}

const VALID = {
  name: "sinfeld",
  version: "1.0.0",
  enabled: true,
  description: "Sinfeld-specific locomotion and camera debugging tools.",
  tools: [
    {
      name: "locomotion_snapshot",
      description: "Read one live locomotion snapshot from the running PIE session.",
      command: "locomotion_debug",
      fixedParams: { operation: "snapshot" },
      params: {
        include_candidates: {
          type: "boolean",
          default: true,
          description: "Re-score the cached query and include its top five candidates.",
        },
      },
      annotations: "readOnly",
      runtimeOnly: true,
    },
  ],
};

console.log("\nextension loader\n");

test("a valid manifest parses", () => {
  const p = writeExt("sinfeld", VALID);
  const m = loadManifest(p);
  assert(m.name === "sinfeld", "name not parsed");
  assert(m.tools.length === 1, "tool not parsed");
});

test("tools are namespaced with the extension prefix", () => {
  const client = new UnrealClient();
  const exts = loadExtensions(client, root);
  const sinfeld = exts.find((e) => e.manifest.name === "sinfeld");
  assert(sinfeld, "sinfeld extension not loaded");
  assert(
    sinfeld!.tools[0].name === "sinfeld_locomotion_snapshot",
    `expected sinfeld_locomotion_snapshot, got ${sinfeld!.tools[0].name}`
  );
});

test("extension tools carry annotations so the central map is not required", () => {
  const exts = loadExtensions(new UnrealClient(), root);
  const t = exts[0].tools[0];
  assert(t.annotations, "no annotations on the extension tool");
  assert(t.annotations!.readOnlyHint === true, "readOnly preset not applied");
});

test("declared params become a real schema that rejects bad input", () => {
  const exts = loadExtensions(new UnrealClient(), root);
  const schema = exts[0].tools[0].inputSchema;
  const bad = schema.safeParse({ include_candidates: "yes please" });
  assert(!bad.success, "a string was accepted for a boolean param");
  const good = schema.safeParse({});
  assert(good.success, "defaults were not applied");
  assert(
    (good as { data: Record<string, unknown> }).data.include_candidates === true,
    "declared default was not applied"
  );
});

test("integer params reject fractions and honour min/max", () => {
  const p = writeExt("nums", {
    name: "nums",
    version: "1.0.0",
    enabled: true,
    description: "Numeric parameter coverage for the loader tests.",
    tools: [{
      name: "t",
      description: "A tool that exists only to exercise numeric parameter parsing.",
      command: "noop",
      params: { n: { type: "integer", min: 0, max: 10 } },
      annotations: "readOnly",
    }],
  });
  const m = loadManifest(p);
  assert(m.tools[0].params!.n.type === "integer", "integer type lost");
  const exts = loadExtensions(new UnrealClient(), root);
  const nums = exts.find((e) => e.manifest.name === "nums")!;
  const s = nums.tools[0].inputSchema;
  assert(!s.safeParse({ n: 1.5 }).success, "fraction accepted for integer");
  assert(!s.safeParse({ n: 99 }).success, "value above max accepted");
  assert(s.safeParse({ n: 5 }).success, "valid value rejected");
});

test("a malformed manifest throws with the offending field named", () => {
  const p = writeExt("bad_shape", { name: "bad_shape", version: "1.0.0" });
  throws(() => loadManifest(p), /description|tools/, "did not name the missing field");
});

test("invalid JSON throws a message that says so", () => {
  const p = writeExt("bad_json", "{ not json");
  throws(() => loadManifest(p), /not valid JSON/, "did not report invalid JSON");
});

test("duplicate tool names are rejected", () => {
  const p = writeExt("dupes", {
    name: "dupes",
    version: "1.0.0",
    enabled: true,
    description: "Two tools sharing one name, which must be rejected.",
    tools: [
      { name: "same", description: "First tool with a duplicated name here.", command: "a", annotations: "readOnly" },
      { name: "same", description: "Second tool with a duplicated name here.", command: "b", annotations: "readOnly" },
    ],
  });
  throws(() => loadManifest(p), /duplicate tool names/, "duplicates not rejected");
});

test("a broken extension is skipped, not fatal", () => {
  writeExt("broken", "{ still not json");
  const exts = loadExtensions(new UnrealClient(), root);
  assert(exts.length >= 1, "loading returned nothing when one extension was broken");
  assert(
    !exts.some((e) => e.manifestPath.includes("broken")),
    "the broken extension was loaded anyway"
  );
});

test("UNREAL_BRIDGE_EXTENSIONS is honoured and non-existent paths ignored", () => {
  const explicit = join(root, "BridgeExtensions", "sinfeld");
  process.env.UNREAL_BRIDGE_EXTENSIONS = `${explicit};D:/definitely/not/here`;
  const paths = discoverManifestPaths(join(root, "nowhere"));
  delete process.env.UNREAL_BRIDGE_EXTENSIONS;
  assert(paths.length === 1, `expected 1 manifest, got ${paths.length}`);
  assert(paths[0].includes("sinfeld"), "explicit path not resolved");
});

test("an extension registers nothing unless it opts in", () => {
  writeExt("not_ready", {
    name: "not_ready",
    version: "1.0.0",
    description: "An extension whose listener commands do not exist yet.",
    disabledReason: "its listener commands are not written yet",
    tools: [{
      name: "would_fail",
      description: "A tool whose backing listener command does not exist at all.",
      command: "nonexistent_command",
      annotations: "readOnly",
    }],
  });
  const m = loadManifest(join(root, "BridgeExtensions", "not_ready", "extension.json"));
  assert(m.enabled === false, "enabled did not default to false");
  const exts = loadExtensions(new UnrealClient(), root);
  assert(
    !exts.some((e) => e.manifest.name === "not_ready"),
    "a disabled extension was registered, so its tools would be advertised and always fail"
  );
});

test("the shipped Sinfeld extension is present but disabled", () => {
  // Guards the real manifest in the host project, not a fixture. Its listener
  // commands do not exist, so enabling it would advertise 25 failing tools.
  const real = "D:/Unreal Projects/MASTER_PROJECT/SF_Repository/Sinfeld_240301/BridgeExtensions/sinfeld/extension.json";
  if (!existsSync(real)) { console.log("        (host project not present, skipped)"); return; }
  const m = loadManifest(real);
  assert(m.enabled === false, "the Sinfeld extension is enabled but its listener commands do not exist");
  assert(m.tools.length === 25, `expected 25 Sinfeld tools, found ${m.tools.length}`);
  assert(!!m.disabledReason, "no disabledReason recorded");
});

void testAsync("a tool forwards its command and merges fixedParams", async () => {
  const calls: Array<{ command: string; params: unknown }> = [];
  const client = new UnrealClient();
  (client as unknown as { sendCommand: unknown }).sendCommand = async (
    command: string,
    params: Record<string, unknown>
  ) => {
    calls.push({ command, params });
    return { success: true, data: {}, error: null };
  };
  const exts = loadExtensions(client, root);
  const tool = exts.find((e) => e.manifest.name === "sinfeld")!.tools[0];
  await tool.handler({ include_candidates: false });
  assert(calls.length === 1, "sendCommand not called");
  assert(calls[0].command === "locomotion_debug", `wrong command: ${calls[0].command}`);
  const p = calls[0].params as Record<string, unknown>;
  assert(p.operation === "snapshot", "fixedParams not merged");
  assert(p.include_candidates === false, "caller params not forwarded");
}).then(() => {
  rmSync(root, { recursive: true, force: true });
  console.log(`\n${passed} passed, ${failed} failed\n`);
  if (failed > 0) process.exit(1);
});
