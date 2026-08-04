import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { BLUEPRINT_PRODUCTION_FIXTURES } from "../src/blueprint-production/fixtures.js";
import { diffDesiredState, planFeature } from "../src/blueprint-production/production-planner.js";
import { graphComplexityWarnings, lintGraph, planGraphLayout } from "../src/blueprint-production/layout.js";
import { DEFAULT_POLICY } from "../src/blueprint-production/planner.js";
import type { BlueprintDesiredState, BlueprintGraph } from "../src/blueprint-production/types.js";
import { createBlueprintProductionTools } from "../src/tools/blueprint-production.js";
import { toolAnnotations } from "../src/annotations.js";

interface TestCase { name: string; fn: () => void | Promise<void>; }
const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: TestCase["fn"]): void { tests.push({ name, fn }); }
function assert(condition: boolean, message: string): asserts condition { if (!condition) throw new Error(`Assertion failed: ${message}`); }

test("manifest and generated fixture library agree", async () => {
  const here = dirname(fileURLToPath(import.meta.url));
  const manifest = JSON.parse(await readFile(join(here, "fixtures", "blueprint-production", "fixture-manifest.json"), "utf8")) as { fixtures: Array<{ id: string }> };
  assert(BLUEPRINT_PRODUCTION_FIXTURES.length === 9, "eight primitive fixtures plus one capstone");
  assert(JSON.stringify(manifest.fixtures.map((item) => item.id)) === JSON.stringify(BLUEPRINT_PRODUCTION_FIXTURES.map((item) => item.feature_id)), "fixture IDs stay reconciled");
});

test("all fixtures produce byte-stable plans with no raw intent in commands", () => {
  for (const fixture of BLUEPRINT_PRODUCTION_FIXTURES) {
    const first = planFeature(fixture);
    const second = planFeature(fixture);
    assert(JSON.stringify(first) === JSON.stringify(second), `${fixture.feature_id} plan is stable`);
    assert(first.plan_hash_sha1 === second.plan_hash_sha1 && /^[a-f0-9]{40}$/.test(first.plan_hash_sha1), `${fixture.feature_id} hash is stable`);
    assert(!JSON.stringify(first.command_plan).includes(fixture.intent), `${fixture.feature_id} intent does not reach mutation parameters`);
  }
});

test("command plans use only canonical commands and force production checks", () => {
  const commands = new Set(["puerts_blueprint_build", "puerts_blueprint_member_patch", "puerts_blueprint_graph_patch", "puerts_graph_inspect"]);
  for (const fixture of BLUEPRINT_PRODUCTION_FIXTURES) for (const entry of planFeature(fixture).command_plan) {
    assert(commands.has(entry.command), `${entry.command} is canonical`);
    if (entry.command !== "puerts_graph_inspect") {
      assert(entry.params.compile === true, `${entry.command} compiles`);
      assert(entry.params.save === true, `${entry.command} saves`);
    }
    if (entry.command === "puerts_blueprint_member_patch" || entry.command === "puerts_blueprint_graph_patch") assert(entry.params.verify === true, `${entry.command} verifies`);
  }
});

test("dependency and operation order are stable", () => {
  const plan = planFeature(BLUEPRINT_PRODUCTION_FIXTURES.find((item) => item.feature_id === "f6_inheritance")!);
  assert(plan.dependency_order[0].endsWith("BaseInteractable"), "parent asset precedes child");
  assert(plan.command_plan.every((entry, index) => entry.order === index + 1), "command order is contiguous");
  for (const path of plan.dependency_order) {
    const phases = plan.command_plan.filter((entry) => entry.asset_path === path).map((entry) => entry.phase);
    assert(phases[0] === "build" && phases.at(-1) === "inspect", `${path} builds before proof read`);
  }
});

test("architecture partitioner outputs explicit decision structures", () => {
  const planF1 = planFeature(BLUEPRINT_PRODUCTION_FIXTURES[0]);
  assert(planF1.architecture.selected === "blueprint", "F1 is Blueprint composition");
  assert(planF1.architecture.build_order.length === 6, "build order has 6 phases");

  const capstone = planFeature(BLUEPRINT_PRODUCTION_FIXTURES[8]);
  assert(capstone.architecture.selected === "hybrid", "physics capstone is hybrid");
  assert(capstone.architecture.cpp_responsibilities.length > 0, "cpp responsibilities assigned");
  assert(capstone.architecture.blueprint_responsibilities.length > 0, "blueprint responsibilities assigned");
});

test("layout is left to right, grid-snapped, generates layout hash, and is stable", () => {
  const plan = planFeature(BLUEPRINT_PRODUCTION_FIXTURES[6]);
  const layout = plan.layout_plans[0].graph;
  const positions = new Map(layout.nodes.map((node) => [node.id, node]));
  for (const edge of layout.connections) assert(positions.get(edge.to)!.x >= positions.get(edge.from)!.x, `${edge.from} flows left to right`);
  assert(new Set(layout.nodes.map((node) => node.y)).size > 1, "branch lanes have distinct vertical positions");
  assert(layout.comments.length >= 3, "logical regions create stable comments");
  assert(layout.reroutes.length > 0, "only long cross-lane connections receive reroutes");
  assert(typeof layout.layout_hash_sha1 === "string" && layout.layout_hash_sha1.length === 40, "layout generates SHA-1 hash");
  assert(layout.nodes.every((n) => n.x % 16 === 0 && n.y % 16 === 0), "coordinates snap to 16px grid");
});

test("complexity policy and graph linter emit machine-readable findings", () => {
  const nodes = Array.from({ length: 26 }, (_, index) => ({ id: `n${index}`, type: "CallFunction", repeat_key: index < 3 ? "repeat" : undefined, branch_depth: index === 25 ? 4 : 0, loop_expected_iterations: index === 24 ? 101 : undefined }));
  const graph: BlueprintGraph = { name: "Heavy", kind: "event", nodes, connections: [] };
  const codes = new Set(graphComplexityWarnings("/Game/MCPGenerated/BPV_Heavy", graph, DEFAULT_POLICY).map((warning) => warning.code));
  for (const code of ["FUNCTIONAL_NODE_LIMIT", "BRANCH_DEPTH_LIMIT", "REPEATED_PATTERN", "LARGE_LOOP"]) assert(codes.has(code), `${code} emitted`);

  const posGraph = planGraphLayout(graph, DEFAULT_POLICY);
  const linterFindings = lintGraph("/Game/MCPGenerated/BPV_Heavy", posGraph, DEFAULT_POLICY);
  assert(linterFindings.some((f) => f.code === "FUNCTIONAL_NODE_LIMIT"), "linter detects node limit");
  assert(linterFindings.some((f) => f.code === "COMPLEX_EVENT_GRAPH"), "linter detects complex event graph");
});

test("telekinetic capstone fixture produces deterministic hybrid plan", () => {
  const capstoneReq = BLUEPRINT_PRODUCTION_FIXTURES[8];
  const plan = planFeature(capstoneReq);
  assert(plan.feature_id === "capstone_physics_pickup_throw", "capstone plan ID matches");
  assert(plan.architecture.selected === "hybrid", "capstone partitions to hybrid");
  assert(plan.blueprints.length === 2, "capstone defines 2 blueprints");
  assert(plan.blueprints.some((bp) => bp.asset_path.endsWith("BPV_Capstone_Character")), "character blueprint included");
  assert(plan.blueprints.some((bp) => bp.asset_path.endsWith("BPV_Capstone_PhysicsObject")), "physics object blueprint included");
  assert(plan.layout_plans.length >= 1, "layout plans generated for capstone blueprints");
});

test("unsupported requests remain non-executable and proof gaps stay visible", () => {
  for (const fixture of BLUEPRINT_PRODUCTION_FIXTURES) {
    const plan = planFeature(fixture);
    assert(plan.executable === false, `${fixture.feature_id} cannot be green before reload and save proof`);
    assert(plan.unsupported.some((item) => item.capability === "package_reload"), "reload gap is explicit");
    assert(plan.unsupported.some((item) => item.capability === "independent_save_confirmation"), "save proof gap is explicit");
    assert(plan.verification_plan.some((step) => step.kind === "reload" && !step.available), "proof plan marks reload unavailable");
  }
  assert(planFeature(BLUEPRINT_PRODUCTION_FIXTURES[1]).unsupported.some((item) => item.capability === "node:InterfaceMessage"), "F2 reports interface message node gap");
});

test("desired versus observed diff emits minimal repair scopes", () => {
  const desired = BLUEPRINT_PRODUCTION_FIXTURES[0].blueprints[0];
  const matching = { asset_path: desired.asset_path, parent_class: desired.parent_class, components: desired.components, variables: desired.variables, functions: desired.functions, dispatchers: desired.dispatchers, interfaces: desired.interfaces, graphs: desired.graphs };
  assert(diffDesiredState(desired, matching).length === 0, "matching observation needs no repair");
  const repairs = diffDesiredState(desired, { ...matching, parent_class: "Pawn", variables: [] });
  assert(repairs.length === 2, "only changed scopes are repaired");
  assert(repairs.some((repair) => repair.path === "parent_class" && repair.command === "puerts_blueprint_build"), "parent repair uses builder");
  assert(repairs.some((repair) => repair.path === "variables" && repair.command === "puerts_blueprint_member_patch"), "member repair uses member patch");
  const missing = diffDesiredState(desired as BlueprintDesiredState, undefined);
  assert(missing.length === 1 && missing[0].scope === "asset", "missing asset uses one build repair");
});

test("schema boundary rejects unsafe paths, duplicate node IDs, and dangling graph references", () => {
  const unsafe = structuredClone(BLUEPRINT_PRODUCTION_FIXTURES[0]);
  unsafe.blueprints[0].asset_path = "/Game/Outside/BP_Bad";
  let refused = false;
  try { planFeature(unsafe); } catch { refused = true; }
  assert(refused, "unsafe authoring path refused before planning");

  const dupNode = structuredClone(BLUEPRINT_PRODUCTION_FIXTURES[0]);
  dupNode.blueprints[0].graphs[0].nodes.push({ id: "begin", type: "PrintString" });
  refused = false;
  try { planFeature(dupNode); } catch { refused = true; }
  assert(refused, "duplicate node ID refused before planning");

  const dangling = structuredClone(BLUEPRINT_PRODUCTION_FIXTURES[0]);
  dangling.blueprints[0].graphs[0].connections.push({ from: "missing", from_pin: "then", to: "print", to_pin: "execute" });
  refused = false;
  try { planFeature(dangling); } catch { refused = true; }
  assert(refused, "dangling node reference refused before planning");
});

test("blueprint production tool is one classified server-local planner", () => {
  const tools = createBlueprintProductionTools();
  assert(tools.length === 1, "factory exposes one tool");
  assert(tools[0].name === "blueprint_production_plan", "tool has the canonical planner name");
  assert(toolAnnotations.blueprint_production_plan?.readOnlyHint === true, "planner is centrally classified read-only");
});

test("blueprint production tool returns a plan and missing-asset repairs", async () => {
  const tool = createBlueprintProductionTools()[0];
  const result = await tool.handler({ request: BLUEPRINT_PRODUCTION_FIXTURES[0] });
  const payload = JSON.parse(result.content[0].text) as { plan: { feature_id: string }; repairs: Array<{ scope: string }> };
  assert(payload.plan.feature_id === "f1_basic_actor", "tool returns the requested deterministic plan");
  assert(payload.repairs.length === 1 && payload.repairs[0].scope === "asset", "missing observation produces one asset repair");
});

test("blueprint production tool calculates per-asset observed repairs", async () => {
  const fixture = BLUEPRINT_PRODUCTION_FIXTURES[0];
  const desired = fixture.blueprints[0];
  const observed = {
    asset_path: desired.asset_path,
    parent_class: desired.parent_class,
    components: desired.components,
    variables: desired.variables,
    functions: desired.functions,
    dispatchers: desired.dispatchers,
    interfaces: desired.interfaces,
    graphs: desired.graphs,
  };
  const result = await createBlueprintProductionTools()[0].handler({ request: fixture, observed_blueprints: [observed] });
  const payload = JSON.parse(result.content[0].text) as { repairs: unknown[] };
  assert(payload.repairs.length === 0, "matching independent observation needs no repair");
});

test("blueprint production tool strictly rejects unknown input fields", async () => {
  let refused = false;
  try {
    await createBlueprintProductionTools()[0].handler({ request: BLUEPRINT_PRODUCTION_FIXTURES[0], unexpected: true });
  } catch {
    refused = true;
  }
  assert(refused, "unknown top-level fields are rejected by Zod before planning");
});

async function run(): Promise<void> {
  for (const item of tests) {
    try { await item.fn(); passed += 1; console.log(`  PASS  ${item.name}`); }
    catch (error) { failed += 1; console.error(`  FAIL  ${item.name}`); console.error(`        ${(error as Error).message}`); }
  }
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run();
