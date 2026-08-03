/**
 * Unit tests for bridge_command_status.
 *
 * The tool reads two files off disk and never contacts an editor, so every
 * branch is testable here: an absent record, an unreadable one, one written by
 * something else, one left behind by a previous editor session, and a live one.
 *
 * The stale-record case is the one that matters. A leftover record looks
 * exactly like a live one and would report a command that finished before this
 * editor started, which is worse than reporting nothing.
 *
 * Run: npx tsx mcp-server/tests/status-tool.test.ts
 */

import { existsSync } from "fs";
import { createRequire } from "module";
import { dirname, join } from "path";
import { fileURLToPath } from "url";
import { createStatusTools, readStatus, statusRecordPath } from "../src/tools/status.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

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

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

const NOW = 1_700_000_000_000;
const session = JSON.stringify({ created_at: new Date(NOW - 600_000).toISOString() });

const live = JSON.stringify({
  schema_version: 1,
  kind: "bridge-command-status",
  stage: "native_call",
  tool: "blueprint_build",
  command_id: "cmd-7",
  started_at_ms: NOW - 42_000,
  updated_at_ms: NOW - 42_000,
  last_completed: { tool: "graph_inspect", command_id: "cmd-6", ok: true, duration_ms: 90, finished_at_ms: NOW - 43_000 },
});

check("a running command reports its tool, its id, and how long it has been running", () => {
  const result = readStatus(live, session, NOW);
  assert(result.success === true, "a live record should be a success");
  const data = result.data as Record<string, unknown>;
  assert(data.running === true, "running should be true during a native call");
  assert(data.tool === "blueprint_build", `tool was ${String(data.tool)}`);
  assert(data.command_id === "cmd-7", `command_id was ${String(data.command_id)}`);
  assert(data.running_ms === 42_000, `running_ms was ${String(data.running_ms)}`);
});

check("the answer states that it is a stage and that no percent exists", () => {
  const data = readStatus(live, session, NOW).data as Record<string, unknown>;
  assert(data.reports === "stage", "the tool must say it reports a stage");
  assert(data.percent_available === false, "a percent must never be advertised");
  assert(String(data.percent_unavailable_reason).includes("synchronous native call"),
    "the reason must name why no fraction is observable");
});

check("an idle record reports the last completed command and no running time", () => {
  const idle = JSON.stringify({
    kind: "bridge-command-status",
    stage: "idle",
    tool: "",
    command_id: "",
    started_at_ms: 0,
    updated_at_ms: NOW - 1_000,
    last_completed: { tool: "save", command_id: "cmd-9", ok: true, duration_ms: 101, finished_at_ms: NOW - 1_000 },
  });
  const data = readStatus(idle, session, NOW).data as Record<string, unknown>;
  assert(data.running === false, "an idle editor is not running a command");
  assert(data.running_ms === null, "an idle editor has no running time");
  assert((data.last_completed as Record<string, unknown>).command_id === "cmd-9", "the last command is carried through");
});

check("no record at all refuses and names the environment variable that turns it on", () => {
  const result = readStatus(null, session, NOW);
  assert(result.success === false, "an absent record is not an idle editor");
  assert((result.data as Record<string, unknown>).status_error_code === "status_record_absent", "wrong error code");
  assert((result.errors as string[])[0].includes("MCP_PUERTS_STATUS=1"), "the fix must be named");
});

check("an unreadable record refuses and quotes what was on disk", () => {
  const result = readStatus("{not json", session, NOW);
  assert(result.success === false, "broken JSON is not a success");
  assert((result.data as Record<string, unknown>).status_error_code === "status_record_unreadable", "wrong error code");
  assert((result.errors as string[])[0].includes("{not json"), "the actual content must be quoted");
});

check("a record written by something else refuses rather than being interpreted", () => {
  const result = readStatus(JSON.stringify({ kind: "something-else", stage: "native_call" }), session, NOW);
  assert(result.success === false, "a foreign record is not a status record");
  assert((result.data as Record<string, unknown>).status_error_code === "status_record_malformed", "wrong error code");
});

check("a record older than the editor session is reported as stale, never served as current", () => {
  const newerSession = JSON.stringify({ created_at: new Date(NOW - 10_000).toISOString() });
  const result = readStatus(live, newerSession, NOW);
  assert(result.success === false, "a leftover record must not be reported as a running command");
  assert((result.data as Record<string, unknown>).status_error_code === "status_record_from_previous_session", "wrong error code");
});

check("an unreadable session manifest does not block a good record", () => {
  const result = readStatus(live, "not json", NOW);
  assert(result.success === true, "the record is still readable when the manifest is not");
});

check("the tool is registered, read-only in shape, and takes no arguments", () => {
  const tools = createStatusTools();
  assert(tools.length === 1, `expected one tool, got ${tools.length}`);
  assert(tools[0].name === "bridge_command_status", `wrong name: ${tools[0].name}`);
  assert(tools[0].description.includes("SERVER LOCAL"), "the description must say it does not contact the editor");
  assert(Object.keys((tools[0].inputSchema as { shape?: object }).shape ?? {}).length === 0, "the tool takes no arguments");
});

check("the record path sits beside the session manifest for the addressed project", () => {
  const path = statusRecordPath({ MCP_UNREAL_PROJECT_ROOT: "C:\\Proj" } as NodeJS.ProcessEnv);
  assert(path.includes("Saved") && path.includes("MCPPuerTSBridge") && path.endsWith("status.json"), path);
  assert(path.startsWith("C:\\Proj"), path);
});

// The one check that catches writer/reader drift. Everything above feeds the
// reader a record this test wrote, which proves the reader agrees with the
// test, not with the editor. statusRecord() is the function the PuerTS runtime
// actually calls, so this reads its real compiled output and puts it through
// the real reader. Skipped rather than failed when the runtime has not been
// built, because `npm test` can be run alone; `npm run verify` builds first.
check("what the runtime writes is what the reader accepts", () => {
  const emitted = join(__dirname, "..", "..", "Plugins", "MCPBridge", "Content", "JavaScript", "status.js");
  if (!existsSync(emitted)) {
    console.log("        SKIP: run npm run build first; the runtime is not compiled");
    return;
  }
  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const runtime = require(emitted) as {
    statusRecord: (
      stage: string,
      active: { tool: string; command_id: string; started_at_ms: number } | null,
      last: unknown,
      nowMs: number,
    ) => Record<string, unknown>;
  };

  const running = runtime.statusRecord(
    "native_call",
    { tool: "blueprint_build", command_id: "cmd-11", started_at_ms: NOW - 5_000 },
    null,
    NOW - 5_000,
  );
  const readBack = readStatus(JSON.stringify(running), session, NOW);
  assert(readBack.success === true, `the reader rejected the runtime's own record: ${JSON.stringify(readBack.errors)}`);
  const data = readBack.data as Record<string, unknown>;
  assert(data.running === true, "the runtime's native_call record must read back as running");
  assert(data.tool === "blueprint_build", `tool was ${String(data.tool)}`);
  assert(data.running_ms === 5_000, `running_ms was ${String(data.running_ms)}`);

  const idle = runtime.statusRecord("idle", null, null, NOW);
  const idleBack = readStatus(JSON.stringify(idle), session, NOW).data as Record<string, unknown>;
  assert(idleBack.running === false, "the runtime's idle record must read back as not running");

  // The stage/percent contract, asserted against the writer rather than the
  // reader: a percent field appearing on the record is the failure this whole
  // design exists to prevent.
  assert(!("percent" in running) && !("progress" in running),
    `the runtime record must carry no percent: ${Object.keys(running).join(", ")}`);
  assert(running.stage === "native_call", `stage was ${String(running.stage)}`);
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
