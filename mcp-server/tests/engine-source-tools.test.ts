/**
 * Unit tests for engine source navigation tools.
 *
 * Builds a fake engine source tree in a temp directory and points
 * UE_ENGINE_ROOT at it, so no UE4 install is required. Covers symbol
 * declaration search, regex search, scope/module filtering, cpp
 * inclusion, result caps, file reads, and path traversal rejection.
 *
 * Run: npx tsx mcp-server/tests/engine-source-tools.test.ts
 */

import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { dirname, join } from "path";
import { createEngineSourceTools } from "../src/tools/engine-source.js";
import type { ToolDefinition } from "../src/types.js";

let engineRoot: string;
let tools: ToolDefinition[];
let toolMap: Map<string, ToolDefinition>;

// ---- Test runner ----

interface TestCase {
  name: string;
  fn: () => Promise<void>;
}

const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => Promise<void>): void {
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

function assertIncludes(str: string, substr: string, label: string): void {
  if (!str.includes(substr)) {
    throw new Error(`${label}: expected "${str}" to include "${substr}"`);
  }
}

async function callTool(name: string, params: Record<string, unknown>): Promise<Record<string, unknown>> {
  const tool = toolMap.get(name);
  if (!tool) throw new Error(`Tool not found: ${name}`);
  const result = await tool.handler(params);
  return JSON.parse(result.content[0].text) as Record<string, unknown>;
}

interface Match {
  file: string;
  line: number;
  text: string;
  context: string[];
  include_hint?: string;
}

function matchesOf(result: Record<string, unknown>): Match[] {
  const data = result.data as Record<string, unknown>;
  return data.matches as Match[];
}

// ---- Setup & Teardown ----

function writeEngineFile(relPath: string, content: string): void {
  const full = join(engineRoot, relPath);
  mkdirSync(dirname(full), { recursive: true });
  writeFileSync(full, content, "utf-8");
}

function setup(): void {
  engineRoot = mkdtempSync(join(tmpdir(), "ue-engine-src-test-"));

  writeEngineFile(join("Engine", "Source", "Runtime", "Engine", "Public", "UnrealClient.h"), [
    "// Copyright Epic Games, Inc. All Rights Reserved.",
    "",
    "#pragma once",
    "",
    "/**",
    " * A screenshot request.",
    " */",
    "struct ENGINE_API FScreenshotRequest",
    "{",
    "\tstatic void RequestScreenshot(bool bInShowUI);",
    "\tstatic void RequestScreenshot(const FString& InFilename, bool bInShowUI, bool bAddFilenameSuffix);",
    "\tstatic void Reset();",
    "};",
  ].join("\n"));

  writeEngineFile(join("Engine", "Source", "Runtime", "Core", "Public", "HAL", "Runnable.h"), [
    "#pragma once",
    "",
    "class CORE_API FRunnable",
    "{",
    "public:",
    "\tvirtual bool Init() { return true; }",
    "\tvirtual uint32 Run() = 0;",
    "\tvirtual void Stop() {}",
    "};",
  ].join("\n"));

  writeEngineFile(join("Engine", "Source", "Editor", "UnrealEd", "Public", "FileHelpers.h"), [
    "#pragma once",
    "",
    "class UNREALED_API FEditorFileUtils",
    "{",
    "public:",
    "\tUNREALED_API static bool SaveCurrentLevel();",
    "};",
  ].join("\n"));

  writeEngineFile(join("Engine", "Source", "Runtime", "Engine", "Private", "UnrealClientImpl.cpp"), [
    "#include \"UnrealClient.h\"",
    "",
    "void FScreenshotRequest::Reset()",
    "{",
    "}",
  ].join("\n"));

  writeEngineFile(join("Engine", "Plugins", "FX", "GlitchFX", "Source", "GlitchFX", "Public", "GlitchThing.h"), [
    "#pragma once",
    "",
    "class GLITCHFX_API FGlitchThing",
    "{",
    "};",
  ].join("\n"));

  // Must be skipped during walks: generated code inside Intermediate.
  writeEngineFile(join("Engine", "Plugins", "FX", "GlitchFX", "Intermediate", "Build", "GlitchThing.gen.h"), [
    "class GLITCHFX_API FGlitchThing",
    "{",
    "};",
  ].join("\n"));

  const repeated: string[] = ["#pragma once", ""];
  for (let i = 0; i < 30; i++) repeated.push(`// FOO_REPEATED marker ${i}`);
  writeEngineFile(join("Engine", "Source", "Runtime", "Engine", "Public", "Repeats.h"), repeated.join("\n"));

  process.env.UE_ENGINE_ROOT = engineRoot;
  tools = createEngineSourceTools();
  toolMap = new Map(tools.map((t) => [t.name, t]));
}

function teardown(): void {
  delete process.env.UE_ENGINE_ROOT;
  rmSync(engineRoot, { recursive: true, force: true });
}

// ---- Tests ----

test("registers engine_source_search and engine_source_read", async () => {
  assertEqual(tools.length, 2, "tool count");
  assert(toolMap.has("engine_source_search"), "engine_source_search registered");
  assert(toolMap.has("engine_source_read"), "engine_source_read registered");
});

test("symbol search finds a struct declaration with include hint", async () => {
  const result = await callTool("engine_source_search", {
    symbol: "FScreenshotRequest",
    scope: "Runtime",
  });
  assertEqual(result.success, true, "success");
  const matches = matchesOf(result);
  assertEqual(matches.length, 1, "match count");
  assertEqual(matches[0].file, "Source/Runtime/Engine/Public/UnrealClient.h", "file");
  assertEqual(matches[0].line, 8, "line");
  assertIncludes(matches[0].text, "struct ENGINE_API FScreenshotRequest", "matched text");
  assertEqual(matches[0].include_hint, "UnrealClient.h", "include hint");
});

test("symbol search derives nested include hints", async () => {
  const result = await callTool("engine_source_search", {
    symbol: "FRunnable",
    scope: "Runtime",
  });
  const matches = matchesOf(result);
  assertEqual(matches.length, 1, "match count");
  assertEqual(matches[0].include_hint, "HAL/Runnable.h", "include hint");
});

test("pattern search with scope and module filter", async () => {
  const result = await callTool("engine_source_search", {
    pattern: "SaveCurrentLevel",
    scope: "Editor",
    module: "UnrealEd",
  });
  assertEqual(result.success, true, "success");
  const matches = matchesOf(result);
  assertEqual(matches.length, 1, "match count");
  assertEqual(matches[0].include_hint, "FileHelpers.h", "include hint");
});

test("module filter matches module directories, not the install root", async () => {
  // Every absolute path under the install contains an "Engine" segment
  // (<root>/Engine/...). A module filter of "Engine" must only match files
  // under a module directory named Engine, so the Core header stays out.
  const result = await callTool("engine_source_search", {
    pattern: "CORE_API FRunnable",
    scope: "Runtime",
    module: "Engine",
  });
  assertEqual(result.success, true, "success");
  assertEqual(matchesOf(result).length, 0, "Core header excluded by Engine module filter");

  const scoped = await callTool("engine_source_search", {
    pattern: "CORE_API FRunnable",
    scope: "Runtime",
    module: "Core",
  });
  assertEqual(matchesOf(scoped).length, 1, "Core module filter finds it");
});

test("cpp files are excluded by default and included with include_cpp", async () => {
  const noCpp = await callTool("engine_source_search", {
    pattern: "FScreenshotRequest::Reset",
    scope: "Runtime",
  });
  assertEqual(matchesOf(noCpp).length, 0, "match count without include_cpp");

  const withCpp = await callTool("engine_source_search", {
    pattern: "FScreenshotRequest::Reset",
    scope: "Runtime",
    include_cpp: true,
  });
  const matches = matchesOf(withCpp);
  assertEqual(matches.length, 1, "match count with include_cpp");
  assertEqual(matches[0].include_hint, undefined, "no include hint for Private files");
});

test("plugins scope skips Intermediate directories", async () => {
  const result = await callTool("engine_source_search", {
    symbol: "FGlitchThing",
    scope: "Plugins",
  });
  const matches = matchesOf(result);
  assertEqual(matches.length, 1, "only the Public header matches");
  assertIncludes(matches[0].file, "Public/GlitchThing.h", "match is the real header");
});

test("max_results caps output and sets truncated", async () => {
  const result = await callTool("engine_source_search", {
    pattern: "FOO_REPEATED",
    scope: "Runtime",
    max_results: 5,
  });
  const data = result.data as Record<string, unknown>;
  assertEqual(matchesOf(result).length, 5, "capped match count");
  assertEqual(data.truncated, true, "truncated flag");
});

test("symbol search falls back to plain references with a note", async () => {
  const result = await callTool("engine_source_search", {
    symbol: "RequestScreenshot",
    scope: "Runtime",
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assert(matchesOf(result).length >= 1, "found plain references");
  assertIncludes(String(data.note), "No declaration matched", "fallback note");
});

test("unknown symbol returns success with zero matches", async () => {
  const result = await callTool("engine_source_search", {
    symbol: "FDoesNotExistAnywhere",
    scope: "All",
  });
  assertEqual(result.success, true, "success");
  assertEqual(matchesOf(result).length, 0, "match count");
});

test("requires exactly one of symbol or pattern", async () => {
  const both = await callTool("engine_source_search", {
    symbol: "FRunnable",
    pattern: "FRunnable",
  });
  assertEqual(both.success, false, "both provided fails");
  assertIncludes(String(both.error), "exactly one", "error message");

  const neither = await callTool("engine_source_search", {});
  assertEqual(neither.success, false, "neither provided fails");
});

test("engine_source_read returns a numbered slice", async () => {
  const result = await callTool("engine_source_read", {
    file: "Source/Runtime/Engine/Public/UnrealClient.h",
    start_line: 8,
    end_line: 10,
  });
  assertEqual(result.success, true, "success");
  const data = result.data as Record<string, unknown>;
  assertEqual(data.start_line, 8, "start line");
  assertEqual(data.end_line, 10, "end line");
  assertIncludes(String(data.text), "8\tstruct ENGINE_API FScreenshotRequest", "numbered content");
});

test("engine_source_read rejects path traversal", async () => {
  const result = await callTool("engine_source_read", {
    file: "../outside.txt",
  });
  assertEqual(result.success, false, "traversal fails");
  assertIncludes(String(result.error), "outside", "error mentions escape");
});

test("engine_source_read reports missing files", async () => {
  const result = await callTool("engine_source_read", {
    file: "Source/Runtime/Engine/Public/Nope.h",
  });
  assertEqual(result.success, false, "missing file fails");
  assertIncludes(String(result.error), "File not found", "error message");
});

test("invalid UE_ENGINE_ROOT fails loudly instead of falling through", async () => {
  const original = process.env.UE_ENGINE_ROOT;
  process.env.UE_ENGINE_ROOT = join(engineRoot, "does-not-exist");
  try {
    const result = await callTool("engine_source_search", {
      symbol: "FRunnable",
    });
    assertEqual(result.success, false, "bad root fails");
    assertIncludes(String(result.error), "UE_ENGINE_ROOT", "error names the env var");
  } finally {
    process.env.UE_ENGINE_ROOT = original;
  }
});

// ---- Run ----

async function run(): Promise<void> {
  console.log("engine-source-tools tests\n");
  setup();
  try {
    for (const t of tests) {
      try {
        await t.fn();
        passed += 1;
        console.log(`  PASS  ${t.name}`);
      } catch (err) {
        failed += 1;
        console.error(`  FAIL  ${t.name}`);
        console.error(`        ${(err as Error).message}`);
      }
    }
  } finally {
    teardown();
  }
  console.log(`\n${passed} passed, ${failed} failed`);
  if (failed > 0) process.exit(1);
}

run().catch((err) => {
  console.error(err);
  process.exit(1);
});
