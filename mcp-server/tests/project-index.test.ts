/**
 * Project intelligence index tests. No editor, no named pipe.
 *
 * Pass 1 and 2 point the indexer at a real UE4.27 project with the MCPBridge
 * plugin installed, read-only: the store is redirected into a temp directory
 * so nothing outside this repo is written.
 *
 * Pass 3 copies real source out of that project into a temp fixture and
 * mutates it there, which is how the incremental assertions can touch a file
 * without editing the real project.
 *
 * The project path comes from PROJECT_INDEX_TEST_PROJECT, falling back to a
 * local dev default that will not exist on another machine or in CI. When
 * neither exists, this file SKIPs (exit 0) rather than crash: it is one file
 * out of the chain `npm test` runs, and AGENTS.md documents that chain as
 * needing no UE4 install. It did, silently, until this hardcoded a personal
 * path and broke that guarantee for everyone else.
 *
 * Run: npx tsx mcp-server/tests/project-index.test.ts
 *      PROJECT_INDEX_TEST_PROJECT="D:\Path\To\Project" npx tsx mcp-server/tests/project-index.test.ts
 */

import { appendFileSync, cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, utimesSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";
import {
  PROJECT_INDEX_SCHEMA_VERSION,
  findSymbols,
  indexProject,
  listContentAssets,
  listGameplayTags,
  listInputMappings,
  listModules,
  type ProjectIndex,
} from "../src/project-index.js";

const REAL_PROJECT = process.env.PROJECT_INDEX_TEST_PROJECT || "D:\\Unreal Projects\\BridgeInstallTest";
const REAL_PROJECT_FIXTURE_PATH = join(
  REAL_PROJECT, "Plugins", "MCPBridge", "Source", "MCPBridgeGraphBuilder", "Public");

// ---- Test runner ----

interface TestCase {
  name: string;
  fn: () => void;
}

const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => void): void {
  tests.push({ name, fn });
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

function assertEqual<T>(actual: T, expected: T, label: string): void {
  if (actual !== expected) {
    throw new Error(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

// ---- Shared state ----

let workDir: string;
let realStore: string;
let fixtureRoot: string;

function setup(): void {
  workDir = mkdtempSync(join(tmpdir(), "project-index-"));
  realStore = join(workDir, "real-store.json");

  // Fixture built from real source so the symbols are real, but mutable
  // because it is a copy in a temp directory.
  fixtureRoot = join(workDir, "FixtureProject");
  const fixtureModule = join(fixtureRoot, "Plugins", "MCPBridge", "Source", "MCPBridgeGraphBuilder");
  mkdirSync(fixtureModule, { recursive: true });
  cpSync(
    join(REAL_PROJECT, "Plugins", "MCPBridge", "Source", "MCPBridgeGraphBuilder", "Public"),
    join(fixtureModule, "Public"),
    { recursive: true }
  );
  cpSync(
    join(REAL_PROJECT, "Plugins", "MCPBridge", "Source", "MCPBridgeGraphBuilder", "MCPBridgeGraphBuilder.Build.cs"),
    join(fixtureModule, "MCPBridgeGraphBuilder.Build.cs")
  );

  const configDir = join(fixtureRoot, "Config");
  mkdirSync(configDir, { recursive: true });
  writeFileSync(
    join(configDir, "DefaultInput.ini"),
    [
      "[/Script/Engine.InputSettings]",
      '+ActionMappings=(ActionName="Jump",bShift=False,Key=SpaceBar)',
      '+ActionMappings=(ActionName="Fire",bShift=False,Key=LeftMouseButton)',
      '+AxisMappings=(AxisName="MoveForward",Scale=1.000000,Key=W)',
      '+AxisMappings=(AxisName="MoveForward",Scale=-1.000000,Key=S)',
      "",
    ].join("\n"),
    "utf-8"
  );
  writeFileSync(
    join(configDir, "DefaultGameplayTags.ini"),
    [
      "[/Script/GameplayTags.GameplayTagsSettings]",
      '+GameplayTagList=(Tag="Ability.Dash",DevComment="dash")',
      '+GameplayTagList=(Tag="Ability.Jump",DevComment="")',
      "",
    ].join("\n"),
    "utf-8"
  );

  const contentDir = join(fixtureRoot, "Content", "Maps");
  mkdirSync(contentDir, { recursive: true });
  writeFileSync(join(contentDir, "TestArena.umap"), "not a real package", "utf-8");
}

function teardown(): void {
  rmSync(workDir, { recursive: true, force: true });
}

function indexReal(): { index: ProjectIndex; indexed: number; skipped: number; scanned: number } {
  const result = indexProject(REAL_PROJECT, { storePath: realStore });
  assert(result.success, `indexProject failed: ${result.error ?? "unknown"}`);
  if (!result.data) throw new Error("indexProject returned no data");
  return {
    index: result.data.index,
    indexed: result.data.stats.indexed,
    skipped: result.data.stats.skipped,
    scanned: result.data.stats.scanned,
  };
}

// ---- Pass 1: real project, first index ----

test("real project exists and is indexable", () => {
  assert(existsSync(REAL_PROJECT), `test project not found at ${REAL_PROJECT}`);
  const first = indexReal();
  assert(first.scanned > 50, `expected a substantial scan, got ${first.scanned} files`);
  assertEqual(first.skipped, 0, "first run should skip nothing");
  assertEqual(first.indexed, first.scanned, "first run should index every scanned file");
  assertEqual(first.index.schemaVersion, PROJECT_INDEX_SCHEMA_VERSION, "schema version");
  assert(existsSync(realStore), "store file was written");
});

test("real C++ symbols are found with module and base class", () => {
  const { index } = indexReal();
  const hits = findSymbols(index, "UBlueprintGraphBuilderLibrary");
  assert(hits.length > 0, "UBlueprintGraphBuilderLibrary not found");
  const decl = hits.find((h) => h.symbol.kind === "class");
  assert(decl !== undefined, "no class declaration for UBlueprintGraphBuilderLibrary");
  if (!decl) return;
  assertEqual(decl.symbol.reflected, true, "UCLASS should mark the symbol reflected");
  assertEqual(decl.symbol.base, "UBlueprintFunctionLibrary", "base class");
  assertEqual(decl.symbol.module, "MCPBridgeGraphBuilder", "owning module");
  assert(decl.file.endsWith("BlueprintGraphBuilderLibrary.h"), `unexpected file ${decl.file}`);

  const funcs = findSymbols(index, "BuildBlueprintFromJSON");
  assert(
    funcs.some((h) => h.symbol.kind === "function" && h.symbol.reflected),
    "expected a reflected UFUNCTION named BuildBlueprintFromJSON"
  );
});

test("real Build.cs module dependencies are captured", () => {
  const { index } = indexReal();
  const graphBuilder = listModules(index).find((m) => m.name === "MCPBridgeGraphBuilder");
  assert(graphBuilder !== undefined, "MCPBridgeGraphBuilder module not indexed");
  if (!graphBuilder) return;
  assert(graphBuilder.publicDependencies.includes("CoreUObject"), "public dependency CoreUObject");
  assert(graphBuilder.privateDependencies.includes("KismetCompiler"), "private dependency KismetCompiler");
  assert(graphBuilder.privateDependencies.includes("InputCore"), "private dependency InputCore");
  assert(listModules(index).length >= 5, "expected several modules across project and plugins");
});

test("real content inventory finds maps by package path", () => {
  const { index } = indexReal();
  const assets = listContentAssets(index);
  const map = assets.find((a) => a.packagePath === "/Game/MCPTests/ConsolidatedMilestone");
  assert(map !== undefined, "ConsolidatedMilestone map not in the content inventory");
  assertEqual(map?.assetKind, "map", "map asset kind");
  assert(
    assets.some((a) => a.packagePath === "/Game/MCPGenerated/BP_TruthProbe"),
    "BP_TruthProbe uasset not in the content inventory"
  );
});

// ---- Pass 2: second run is a no-op ----

test("second run over an unchanged project re-indexes nothing", () => {
  const first = indexReal();
  const second = indexReal();
  assertEqual(second.indexed, 0, "second run indexed count");
  assertEqual(second.skipped, second.scanned, "second run should skip every file");
  assertEqual(second.scanned, first.scanned, "scan size should be stable");
});

// ---- Pass 3: incremental behaviour on a mutable copy ----

test("fixture parses input mappings and gameplay tags from Config", () => {
  const result = indexProject(fixtureRoot, { storePath: join(workDir, "fixture.json") });
  assert(result.success, `fixture index failed: ${result.error ?? ""}`);
  if (!result.data) return;
  const mappings = listInputMappings(result.data.index);
  assertEqual(mappings.filter((m) => m.kind === "action").length, 2, "action mapping count");
  const forward = mappings.filter((m) => m.kind === "axis" && m.name === "MoveForward");
  assertEqual(forward.length, 2, "MoveForward axis mapping count");
  assertEqual(forward[0].key, "W", "first MoveForward key");
  assertEqual(forward[1].scale, -1, "second MoveForward scale");
  const tags = listGameplayTags(result.data.index).map((t) => t.tag);
  assert(tags.includes("Ability.Dash"), "Ability.Dash tag");
  assert(tags.includes("Ability.Jump"), "Ability.Jump tag");
});

test("touching one file re-indexes exactly that file, with the reason recorded", () => {
  const store = join(workDir, "incremental.json");
  const first = indexProject(fixtureRoot, { storePath: store });
  assert(first.success && first.data !== null, "fixture first index");
  if (!first.data) return;

  const noop = indexProject(fixtureRoot, { storePath: store });
  assertEqual(noop.data?.stats.indexed, 0, "fixture second run should be a no-op");

  const touched = join(
    fixtureRoot,
    "Plugins", "MCPBridge", "Source", "MCPBridgeGraphBuilder", "Public", "BlueprintGraphBuilderLibrary.h"
  );
  const future = new Date(Date.now() + 10_000);
  utimesSync(touched, future, future);

  const third = indexProject(fixtureRoot, { storePath: store });
  assertEqual(third.data?.stats.indexed, 1, "exactly one file re-indexed");
  assertEqual(third.data?.stats.reasons.mtime_changed, 1, "mtime_changed reason count");
  const rel = "Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Public/BlueprintGraphBuilderLibrary.h";
  assertEqual(third.data?.index.files[rel].invalidatedBy, "mtime_changed", "recorded invalidation reason");
});

test("a size change invalidates, and a deleted file is reported as removed", () => {
  const store = join(workDir, "size.json");
  indexProject(fixtureRoot, { storePath: store });

  const grown = join(fixtureRoot, "Config", "DefaultInput.ini");
  appendFileSync(grown, '+ActionMappings=(ActionName="Crouch",Key=LeftControl)\n', "utf-8");
  const after = indexProject(fixtureRoot, { storePath: store });
  assertEqual(after.data?.stats.indexed, 1, "exactly one file re-indexed after growth");
  assertEqual(after.data?.stats.reasons.size_changed, 1, "size_changed reason count");
  assertEqual(listInputMappings(after.data!.index).filter((m) => m.name === "Crouch").length, 1, "new mapping picked up");

  const doomed = join(fixtureRoot, "Content", "Maps", "TestArena.umap");
  rmSync(doomed);
  const removedRun = indexProject(fixtureRoot, { storePath: store });
  assertEqual(removedRun.data?.stats.removed.length, 1, "one removed file");
  assertEqual(removedRun.data?.stats.removed[0], "Content/Maps/TestArena.umap", "removed path");
});

test("a schema version bump forces a full re-index", () => {
  const store = join(workDir, "schema.json");
  const first = indexProject(fixtureRoot, { storePath: store });
  const total = first.data?.stats.scanned ?? 0;
  assert(total > 0, "fixture scanned something");

  const stale = JSON.parse(readFileSync(store, "utf-8")) as ProjectIndex;
  stale.schemaVersion = PROJECT_INDEX_SCHEMA_VERSION - 1;
  writeFileSync(store, JSON.stringify(stale), "utf-8");

  const after = indexProject(fixtureRoot, { storePath: store });
  assertEqual(after.data?.stats.indexed, total, "every file re-indexed after a schema bump");
  assertEqual(after.data?.stats.reasons.schema_version_changed, total, "schema_version_changed reason count");
});

test("a missing project root fails with an error instead of throwing", () => {
  const result = indexProject(join(workDir, "does-not-exist"), { write: false });
  assertEqual(result.success, false, "success flag");
  assertEqual(result.data, null, "data");
  assert((result.error ?? "").includes("does not exist"), "error message names the problem");
});

// ---- Run ----

if (!existsSync(REAL_PROJECT_FIXTURE_PATH)) {
  console.log("Running project index tests...\n");
  console.log(`  SKIP  entire file - no project with the MCPBridge plugin at ${REAL_PROJECT}`);
  console.log(`        Set PROJECT_INDEX_TEST_PROJECT to a real UE4.27 project to run these.`);
  process.exit(0);
}

setup();
console.log("Running project index tests...\n");
for (const t of tests) {
  try {
    t.fn();
    console.log(`  PASS  ${t.name}`);
    passed++;
  } catch (err) {
    console.log(`  FAIL  ${t.name}`);
    console.log(`        ${err instanceof Error ? err.message : String(err)}`);
    failed++;
  }
}
teardown();
console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
