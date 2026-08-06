/**
 * Routing-policy tests: the sequencing rules, and proof the checker catches
 * what it claims to.
 *
 * Every fixture is a transcript of tool calls a session could plausibly make
 * for a representative request. Half are safe and must produce no violation;
 * half break exactly one rule and must produce exactly that one. Testing both
 * directions is the point: a checker that flags everything is as useless as one
 * that flags nothing, and only the negative fixtures can tell them apart.
 *
 * No model runs here. These assert the rules, so that a model-in-the-loop
 * harness has something to assert AGAINST later.
 */

import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { checkRouting, type Transcript } from "../src/routing-policy.js";
import { createPuertsTools, QUARANTINED_TOOLS } from "../src/tools/puerts.js";
import { PuerTSClient } from "../src/puerts-client.js";

let passed = 0;
let failed = 0;
function check(name: string, fn: () => void): void {
  try { fn(); passed += 1; console.log(`  PASS  ${name}`); }
  catch (error) { failed += 1; console.error(`  FAIL  ${name}\n        ${(error as Error).message}`); }
}
function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

const listed = createPuertsTools(new PuerTSClient()).map((tool) => tool.name);
const rulesOf = (t: Transcript): string[] =>
  checkRouting(t, QUARANTINED_TOOLS).map((v) => v.rule).sort();

/** "Move the cube up by 200." Read, plan, apply against what was read. */
const safeSceneEdit: Transcript = {
  toolsListed: listed,
  calls: [
    { tool: "puerts_scene_inspect", arguments: {} },
    { tool: "puerts_scene_batch", arguments: { plan_only: true, operations: [{ op: "upsert_actor" }] } },
    { tool: "puerts_scene_batch", arguments: {
      operations: [{ op: "upsert_actor" }],
      preconditions: { scene_structure_hash: "6428f5ad0d71a7e4bbfe28bdb85ec93f6a789519" },
    } },
    { tool: "puerts_viewport_screenshot", arguments: {} },
  ],
};

/** "Delete the old props." Destructive, confirmed, after a read. */
const safeDelete: Transcript = {
  toolsListed: listed,
  calls: [
    { tool: "puerts_find_actors", arguments: { name: "Prop" } },
    { tool: "puerts_delete_actor", arguments: { actor: "Prop_3", confirm: true } },
  ],
};

/** "Test the door in play mode." PIE, but the user asked for it. */
const safePie: Transcript = {
  toolsListed: listed,
  calls: [
    { tool: "puerts_scene_inspect", arguments: {} },
    { tool: "puerts_pie_start", arguments: {}, userApproved: true },
    { tool: "puerts_pie_stop", arguments: {}, userApproved: true },
  ],
};

check("a read-plan-apply scene edit passes every rule", () => {
  assert(rulesOf(safeSceneEdit).length === 0, `unexpected: ${rulesOf(safeSceneEdit).join(", ")}`);
});
check("a confirmed delete after a read passes", () => {
  assert(rulesOf(safeDelete).length === 0, `unexpected: ${rulesOf(safeDelete).join(", ")}`);
});
check("approved PIE passes", () => {
  assert(rulesOf(safePie).length === 0, `unexpected: ${rulesOf(safePie).join(", ")}`);
});

check("a write with no prior read or plan is caught", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_scene_batch", arguments: { operations: [{ op: "upsert_actor" }] } },
  ] };
  assert(rulesOf(t).join() === "write-before-inspection", `got ${rulesOf(t).join(", ")}`);
});

check("a destructive call without confirm is caught", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_find_actors", arguments: {} },
    { tool: "puerts_delete_asset", arguments: { asset_path: "/Game/X" } },
  ] };
  assert(rulesOf(t).join() === "destructive-without-confirm", `got ${rulesOf(t).join(", ")}`);
});

check("PIE without the user asking is caught", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_scene_inspect", arguments: {} },
    { tool: "puerts_pie_start", arguments: {} },
  ] };
  assert(rulesOf(t).join() === "pie-without-approval", `got ${rulesOf(t).join(", ")}`);
});

check("an apply carrying a hash nothing produced is caught", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_scene_batch", arguments: {
      operations: [{ op: "upsert_actor" }],
      preconditions: { scene_structure_hash: "6428f5ad0d71a7e4bbfe28bdb85ec93f6a789519" },
    } },
  ] };
  // Both fire, and both are true of this transcript: it wrote blind AND it
  // invented a precondition. Asserting the pair rather than one keeps the test
  // honest about what the checker actually reports.
  assert(rulesOf(t).join() === "precondition-not-observed,write-before-inspection", `got ${rulesOf(t).join(", ")}`);
});

check("calling a quarantined tool is caught even though it is not offered", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_level_load", arguments: { level_path: "/Game/Maps/X" } },
  ] };
  assert(rulesOf(t).join() === "quarantined-tool-called", `got ${rulesOf(t).join(", ")}`);
});

check("a discovery list containing a quarantined tool is caught", () => {
  const t: Transcript = { toolsListed: [...listed, "puerts_level_create"], calls: [] };
  assert(rulesOf(t).join() === "quarantined-tool-offered", `got ${rulesOf(t).join(", ")}`);
});

check("the real tools/list is clean under the discovery rule", () => {
  const t: Transcript = { toolsListed: listed, calls: [] };
  assert(rulesOf(t).length === 0, `the server offers a quarantined tool: ${rulesOf(t).join(", ")}`);
});

check("a violation names the call position, not just the tool", () => {
  const t: Transcript = { toolsListed: listed, calls: [
    { tool: "puerts_scene_inspect", arguments: {} },
    { tool: "puerts_scene_inspect", arguments: {} },
    { tool: "puerts_delete_actor", arguments: { actor: "X" } },
  ] };
  const violations = checkRouting(t, QUARANTINED_TOOLS);
  assert(violations.length === 1 && violations[0].callIndex === 2,
    `expected one violation at index 2, got ${JSON.stringify(violations)}`);
});

// --- the rules are STATED, not only checkable -------------------------------
// The checker proves a violation is detectable. It cannot make a model behave.
// What closes the other half of that gap is that every client is told the rule
// in the first place - so these assert that each enforced rule appears in the
// steering text a client actually receives. A rule that is checked but never
// stated punishes a client for not guessing; one stated but never checked is a
// paragraph nobody has to obey.

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
const serverSource = readFileSync(join(repoRoot, "mcp-server", "src", "index.ts"), "utf-8");
const skill = readFileSync(
  join(repoRoot, "skills", "unreal-engine-4-27", "SKILL.md"), "utf-8");

const STATED_RULES: Array<[string, RegExp]> = [
  ["write-before-inspection", /inspect before you mutate/i],
  ["plan-before-destructive", /plan_only/i],
  ["destructive-without-confirm", /confirm=true/i],
  ["pie-without-approval", /pie_start/i],
  ["quarantined-tool-called", /absent from tools\/list|capabilities\.unavailable/i],
];

for (const [rule, pattern] of STATED_RULES) {
  check(`the server instructions state the rule behind ${rule}`, () => {
    const instructions = serverSource.slice(
      serverSource.indexOf("instructions: ["), serverSource.indexOf("].join("));
    assert(pattern.test(instructions),
      `no client without the skill installed is told about ${rule}`);
  });
}

check("the skill states the PIE approval rule too", () => {
  assert(/pie_start/.test(skill) && /ask/i.test(skill),
    "the skill must carry the PIE rule as well as the server instructions");
});

console.log(`\n${passed} passed, ${failed} failed`);
if (failed > 0) process.exit(1);
