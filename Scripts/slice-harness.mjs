#!/usr/bin/env node
// Shared machinery for the seven one-prompt vertical slice harnesses.
//
// A slice asks one question: can an agent go from a single prompt to a
// finished, verified Unreal artifact in this domain? The answer is only worth
// anything if a run that cannot finish says exactly WHY, so this file exists to
// keep three verdicts apart that a plain acceptance script collapses into
// "failed":
//
//   MISSING PRIMITIVE   the tool the step needs is not in the catalog at all,
//                       or exists only behind the legacy HTTP opt-in. No editor
//                       can change this, and it is a work item for a lane.
//   PRESENT BUT FAILING the tool is registered, was called, and did not do what
//                       it says. That is a bug in shipped code.
//   UNPROVEN            the tool is registered and nothing called it, because
//                       no editor was addressable or the install gate refused.
//
// A harness that dies on the first undefined tool reports one gap per run. This
// one keeps going: every step is classified, so a single run against a closed
// editor still returns the complete missing-primitive list for its domain.
//
// Phases follow Scripts/bp-graph-patch-acceptance.mjs:
//   --phase=warm  (default) run the slice, seal each artifact it produced
//   --phase=cold  after an editor restart, re-read every sealed artifact and
//                 require the same structure hash and the same bytes on disk
//
// Usage from a slice file:
//   import { runSlice } from "./slice-harness.mjs";
//   await runSlice({ id, title, proves }, async (h) => { ... });

import { execSync, spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { requireCurrentInstall } from "./bridge-install.mjs";

export const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..");
const evidenceDir = join(repoRoot, "docs", "evidence");

/** Every tool the repository knows about, whether or not it is registered.
    The distinction between "no such tool anywhere" and "exists only in the
    legacy HTTP lane" is the difference between writing a command and porting
    one, so a harness must not report them with the same word. */
const inventory = JSON.parse(
  readFileSync(join(repoRoot, "docs", "TOOL_INVENTORY.json"), "utf8"),
);
const inventoryByName = new Map(inventory.tools.map((t) => [t.name, t]));

export const STATUS = {
  pass: "PASS",
  fail: "FAIL",
  missing: "MISSING_PRIMITIVE",
  legacy: "LEGACY_ONLY",
  unproven: "UNPROVEN_NO_EDITOR",
  skipped: "SKIPPED_PRIOR_STEP_BLOCKED",
  policy: "NOT_ATTEMPTED_BY_POLICY",
};

const VERDICT_RANK = {
  PASS: 0,
  UNPROVEN_NO_EDITOR: 1,
  PRESENT_BUT_FAILING: 2,
  BLOCKED_MISSING_PRIMITIVE: 3,
};
const EXIT_FOR_VERDICT = {
  PASS: 0,
  PRESENT_BUT_FAILING: 1,
  BLOCKED_MISSING_PRIMITIVE: 2,
  UNPROVEN_NO_EDITOR: 3,
};

function argPhase() {
  return process.argv.includes("--phase=cold") ? "cold" : "warm";
}

/** Start the built server exactly the way a real MCP client does: no extra
    environment, so the catalog measured here is the catalog a client sees. */
function startServer() {
  const serverPath = join(repoRoot, "mcp-server", "dist", "index.js");
  if (!existsSync(serverPath)) {
    throw new Error("mcp-server/dist/index.js is missing. Run npm run build first.");
  }
  const child = spawn(process.execPath, [serverPath], {
    stdio: ["pipe", "pipe", "inherit"],
    env: process.env,
  });
  let buffer = "";
  let nextId = 1;
  const pending = new Map();
  child.stdout.on("data", (chunk) => {
    buffer += chunk.toString();
    for (let n; (n = buffer.indexOf("\n")) >= 0;) {
      const line = buffer.slice(0, n).trim();
      buffer = buffer.slice(n + 1);
      if (!line) continue;
      let message;
      try { message = JSON.parse(line); } catch { continue; }
      const settle = pending.get(message.id);
      if (settle) { pending.delete(message.id); settle(message); }
    }
  });
  const send = (method, params) => {
    const id = nextId++;
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", id, method, params }) + "\n");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`${method} timed out`)), 120000);
      pending.set(id, (m) => { clearTimeout(timer); resolve(m); });
    });
  };
  return { child, send };
}

/** The .uasset a /Game path resolves to inside the target project. */
function contentFile(projectRoot, packagePath) {
  const name = packagePath.split(".")[0];
  const leaf = name.split("/").pop();
  return join(projectRoot, "Content", name.replace("/Game/", "").replaceAll("/", "\\"))
    + (leaf ? ".uasset" : "");
}

export async function runSlice(spec, warmBody) {
  const phase = argPhase();
  const started = Date.now();
  const evidence = {
    slice: spec.id,
    title: spec.title,
    proves: spec.proves,
    phase,
    generated_at: new Date().toISOString(),
    verdict: "PASS",
    editor: { available: false, reason: "not attempted" },
    install_gate: { ok: false, reason: "not attempted" },
    catalog: { registered_tool_count: 0 },
    steps: [],
    missing_primitives: [],
    checks: { passed: 0, failed: 0 },
    sealed_artifacts: [],
    mcp_round_trips: 0,
  };

  const { child, send } = startServer();
  let worstVerdict = "PASS";
  const raise = (v) => {
    if (VERDICT_RANK[v] > VERDICT_RANK[worstVerdict]) worstVerdict = v;
  };

  try {
    await send("initialize", {
      protocolVersion: "2024-11-05",
      capabilities: {},
      clientInfo: { name: `slice-${spec.id}`, version: "1.0.0" },
    });
    child.stdin.write(JSON.stringify({ jsonrpc: "2.0", method: "notifications/initialized" }) + "\n");

    const listed = await send("tools/list", {});
    const registered = new Set((listed?.result?.tools ?? []).map((t) => t.name));
    evidence.catalog.registered_tool_count = registered.size;
    evidence.catalog.legacy_http_enabled = process.env.MCP_ENABLE_LEGACY_HTTP === "1";

    // The install gate is not decoration here. If the plugin in the target
    // project is not this checkout's plugin, a live call proves nothing about
    // the code under review, so the run stays catalog-only rather than
    // producing evidence that looks live and is not.
    let projectRoot = null;
    try {
      projectRoot = requireCurrentInstall();
      evidence.install_gate = { ok: true, project_root: projectRoot };
    } catch (error) {
      evidence.install_gate = { ok: false, reason: error.message.split("\n")[0] };
    }

    let editorOk = false;
    if (projectRoot !== null) {
      const probe = await send("tools/call", { name: "puerts_diagnostic", arguments: {} });
      evidence.mcp_round_trips += 1;
      let parsed = null;
      try { parsed = JSON.parse(probe?.result?.content?.[0]?.text ?? "null"); } catch { parsed = null; }
      editorOk = parsed?.success === true;
      evidence.editor = editorOk
        ? { available: true, reason: "puerts_diagnostic answered" }
        : {
          available: false,
          reason: parsed?.session_error_code
            ?? parsed?.errors?.[0]
            ?? "puerts_diagnostic did not answer",
        };
    } else {
      evidence.editor = { available: false, reason: "install gate refused; no editor was contacted" };
    }
    if (!editorOk) raise("UNPROVEN_NO_EDITOR");

    let stepNumber = 0;
    const record = (entry) => {
      stepNumber += 1;
      evidence.steps.push({ n: stepNumber, ...entry });
      const mark = entry.status === STATUS.pass ? "PASS  "
        : entry.status === STATUS.fail ? "FAIL  "
          : `${entry.status}  `;
      console.log(`  ${mark}${entry.label}${entry.detail ? ` :: ${entry.detail}` : ""}`);
      return entry;
    };

    /** Classify a tool name against the real catalog. */
    const classify = (tool) => {
      if (registered.has(tool)) return "PRESENT";
      const known = inventoryByName.get(tool);
      if (known !== undefined) return known.backend === "native_pipe" ? "PRESENT" : "LEGACY_ONLY";
      return "MISSING";
    };

    const noteMissing = (tool, state, why, request) => {
      if (evidence.missing_primitives.some((m) => m.tool === tool)) return;
      evidence.missing_primitives.push({
        tool,
        state,
        why,
        ...(request ? { proposed_schema: request } : {}),
        ...(state === "LEGACY_ONLY"
          ? { note: "Present only in the legacy HTTP catalog, which is disabled by default. Needs a native front." }
          : {}),
      });
    };

    // A tool is classified once per run. Without this a step that asks for a
    // primitive and then calls it reports the same gap twice, which inflates
    // the count the scoreboard reads.
    const classified = new Map();

    const h = {
      phase,
      projectRoot,
      registered,
      editorAvailable: editorOk,
      evidence,

      /** Does the primitive this step needs exist? Records the answer either
          way and never throws, so one run reports every gap in the domain. */
      need(tool, { why, request } = {}) {
        if (classified.has(tool)) return classified.get(tool);
        const state = classify(tool);
        classified.set(tool, state === "PRESENT");
        if (state === "PRESENT") {
          record({ label: `primitive ${tool} is registered`, tool, status: STATUS.pass });
          return true;
        }
        noteMissing(tool, state, why ?? "required by this slice", request);
        record({
          label: `primitive ${tool} is NOT usable`,
          tool,
          status: state === "LEGACY_ONLY" ? STATUS.legacy : STATUS.missing,
          detail: state === "LEGACY_ONLY"
            ? "exists only behind MCP_ENABLE_LEGACY_HTTP=1; no native front"
            : "not registered and not in the tool inventory",
          why,
        });
        raise("BLOCKED_MISSING_PRIMITIVE");
        return false;
      },

      /** A capability with no candidate tool name at all. Same effect as need()
          on an unregistered name, spelled for the case where the harness is
          naming the tool it wishes existed. */
      request(tool, why, proposedSchema) {
        return h.need(tool, { why, request: proposedSchema });
      },

      /** Call a registered tool. Returns null when the step could not run, so a
          slice reads `const r = await h.call(...); if (!r) return;` and the
          reason is already in the evidence. */
      async call(tool, args, { label, why, request } = {}) {
        const name = label ?? tool;
        if (!h.need(tool, { why, request })) {
          record({ label: name, tool, status: STATUS.skipped, detail: "its primitive is missing" });
          return null;
        }
        if (!editorOk) {
          record({ label: name, tool, status: STATUS.unproven, detail: evidence.editor.reason });
          raise("UNPROVEN_NO_EDITOR");
          return null;
        }
        evidence.mcp_round_trips += 1;
        const message = await send("tools/call", { name: tool, arguments: args });
        const text = message?.result?.content?.[0]?.text;
        let parsed = null;
        try { parsed = JSON.parse(text ?? "null"); } catch { parsed = null; }
        if (parsed === null) {
          record({ label: name, tool, status: STATUS.fail, detail: "no JSON content in the response" });
          evidence.checks.failed += 1;
          raise("PRESENT_BUT_FAILING");
          return null;
        }
        if (parsed.success !== true) {
          record({
            label: name,
            tool,
            status: STATUS.fail,
            detail: `PRESENT BUT FAILING: ${JSON.stringify(parsed.errors ?? parsed.message ?? {}).slice(0, 400)}`,
          });
          evidence.checks.failed += 1;
          raise("PRESENT_BUT_FAILING");
          return parsed;
        }
        record({ label: name, tool, status: STATUS.pass });
        evidence.checks.passed += 1;
        return parsed;
      },

      /** An assertion about a result that already came back. */
      check(condition, label, detail) {
        if (condition) {
          record({ label, status: STATUS.pass, detail });
          evidence.checks.passed += 1;
          return true;
        }
        record({ label, status: STATUS.fail, detail });
        evidence.checks.failed += 1;
        raise("PRESENT_BUT_FAILING");
        return false;
      },

      /** Something the harness deliberately did not do. PIE is the case this
          exists for: AGENTS.md reserves it for the user to ask, so a slice that
          needs runtime observation records the gap instead of starting play. */
      policy(label, detail) {
        record({ label, status: STATUS.policy, detail });
      },

      note(label, detail) {
        record({ label, status: "NOTE", detail });
      },

      /** sha256 of the asset on disk, or null when no gated project root is
          known. Bytes on disk are the only claim a rerun cannot fake. */
      fileSha(packagePath) {
        if (projectRoot === null) return null;
        const file = contentFile(projectRoot, packagePath);
        if (!existsSync(file)) return null;
        return createHash("sha256").update(readFileSync(file)).digest("hex");
      },

      /** `p4 opened` before and after, so a read-only claim is measured rather
          than asserted. Returns null when p4 is not available. */
      sourceControl() {
        if (projectRoot === null) return null;
        try {
          return execSync("p4 opened", {
            cwd: join(projectRoot, "Content"),
            encoding: "utf8",
            stdio: ["ignore", "pipe", "ignore"],
          });
        } catch { return null; }
      },

      /** Seal an artifact so the cold phase can prove it survived a restart. */
      seal({ asset_path, inspect_tool, structure_hash, node_count }) {
        evidence.sealed_artifacts.push({
          asset_path,
          inspect_tool,
          structure_hash: structure_hash ?? null,
          node_count: node_count ?? null,
          file_sha256: h.fileSha(asset_path),
        });
      },
    };

    console.log(`\n${spec.id} slice, ${phase} phase: ${spec.title}`);
    console.log(`  catalog: ${registered.size} registered tools`);
    console.log(`  editor:  ${evidence.editor.available ? "available" : `absent (${evidence.editor.reason})`}`);
    console.log("");

    if (phase === "cold") {
      const warmFile = join(evidenceDir, `slice-${spec.id}.json`);
      if (!existsSync(warmFile)) {
        record({
          label: "cold phase needs a warm run to compare against",
          status: STATUS.skipped,
          detail: `${warmFile} does not exist; run the warm phase first`,
        });
        raise("UNPROVEN_NO_EDITOR");
      } else {
        const warm = JSON.parse(readFileSync(warmFile, "utf8"));
        evidence.compared_against = { file: warmFile, warm_verdict: warm.verdict };
        if ((warm.sealed_artifacts ?? []).length === 0) {
          record({
            label: "the warm run sealed no artifact, so there is nothing to re-read",
            status: STATUS.skipped,
            detail: `warm verdict was ${warm.verdict}`,
          });
          raise(warm.verdict === "PASS" ? "UNPROVEN_NO_EDITOR" : warm.verdict);
        }
        for (const artifact of warm.sealed_artifacts ?? []) {
          const read = await h.call(artifact.inspect_tool, { asset_path: artifact.asset_path }, {
            label: `cold read of ${artifact.asset_path}`,
          });
          if (!read) continue;
          const hash = read.data?.graph?.structure_hash_sha1
            ?? read.data?.structure_hash_sha1
            ?? read.data?.member_structure_hash_sha1
            ?? null;
          h.check(
            artifact.structure_hash === null || hash === artifact.structure_hash,
            `${artifact.asset_path} carries the structure hash the warm run left`,
            `${hash} vs ${artifact.structure_hash}`,
          );
          const sha = h.fileSha(artifact.asset_path);
          h.check(
            artifact.file_sha256 === null || sha === artifact.file_sha256,
            `${artifact.asset_path} is byte-identical on disk after the restart`,
            `${sha} vs ${artifact.file_sha256}`,
          );
        }
      }
    } else {
      await warmBody(h);
    }

    evidence.verdict = worstVerdict;
    evidence.elapsed_ms = Date.now() - started;
    evidence.blocks = evidence.missing_primitives.map((m) => m.tool);

    mkdirSync(evidenceDir, { recursive: true });
    const out = join(evidenceDir, `slice-${spec.id}${phase === "cold" ? "-cold" : ""}.json`);
    writeFileSync(out, JSON.stringify(evidence, null, 2) + "\n");

    console.log(`\nverdict: ${evidence.verdict}`);
    if (evidence.missing_primitives.length > 0) {
      console.log("missing primitives this slice needs:");
      for (const m of evidence.missing_primitives) console.log(`  - ${m.tool} (${m.state}): ${m.why}`);
    }
    console.log(`checks ${evidence.checks.passed} passed, ${evidence.checks.failed} failed`);
    console.log(`evidence: ${out}`);
    process.exit(EXIT_FOR_VERDICT[evidence.verdict]);
  } catch (error) {
    console.error(`\nslice ${spec.id}: ${error.message}`);
    process.exit(1);
  } finally {
    child.kill();
  }
}
