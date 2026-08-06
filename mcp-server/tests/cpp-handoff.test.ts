/**
 * Acceptance test suite for the C++ Generation Handoff Phase.
 *
 * Validates:
 * 1. C++ source and header rendering for UTelekineticComponent from Feature IR.
 * 2. UHT header rules, macro placement, and include ordering compliance.
 * 3. Feature planning for hybrid C++ component + Blueprint character child.
 * 4. Idempotency (identical plan execution produces 100% stable SHA1 hashes).
 * 5. Rollback and diff repair emission for missing or corrupt components.
 *
 * Run: npx tsx mcp-server/tests/cpp-handoff.test.ts
 */

import { BLUEPRINT_PRODUCTION_FIXTURES } from "../src/blueprint-production/fixtures.js";
import { diffDesiredState, planFeature } from "../src/blueprint-production/production-planner.js";
import {
  checkHeaderRules,
  renderClass,
  validateClassSpec,
  type ClassSpec,
} from "../src/cpp-authoring.js";

let passed = 0;
let failed = 0;
const failures: string[] = [];

function test(name: string, fn: () => void): void {
  try {
    fn();
    passed++;
    console.log(`  PASS  ${name}`);
  } catch (error) {
    failed++;
    failures.push(`${name}: ${(error as Error).message}`);
    console.log(`  FAIL  ${name}`);
    console.log(`        ${(error as Error).message}`);
  }
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

function assertEqual<T>(actual: T, expected: T, label: string): void {
  if (actual !== expected) {
    throw new Error(`${label}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
  }
}

console.log("\n--- C++ Generation Handoff Acceptance Suite ---");

const telekineticSpec: ClassSpec = {
  name: "TelekineticComponent",
  module: "BlueprintProductionFixture",
  parent: "ActorComponent",
  class_specifiers: "ClassGroup=(Custom), meta=(BlueprintSpawnableComponent)",
  properties: [
    { name: "GrabDistance", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "1200.0f" },
    { name: "HoldDistance", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "300.0f" },
    { name: "ImpulseStrength", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Telekinesis", default: "1800.0f" },
    { name: "bIsHolding", type: "bool", specifiers: "VisibleAnywhere, BlueprintReadOnly", category: "Telekinesis", default: "false" },
  ],
  functions: [
    { name: "GrabTarget", return_type: "bool", params: "UPrimitiveComponent* TargetComponent, FVector TargetLocation", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = true; return true;" },
    { name: "ReleaseTarget", return_type: "void", params: "", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = false;" },
    { name: "ThrowTarget", return_type: "void", params: "FVector Direction", specifiers: "BlueprintCallable, Category = \"Telekinesis\"", body: "bIsHolding = false;" },
    { name: "IsHoldingTarget", return_type: "bool", params: "", specifiers: "BlueprintPure, Category = \"Telekinesis\"", is_const: true, body: "return bIsHolding;" },
  ],
  delegates: [
    { name: "FOnTelekineticStateChanged", params: [{ name: "bHolding", type: "bool" }], comment: "Fired when physics component is acquired or released" },
  ],
};

test("validateClassSpec accepts valid UTelekineticComponent spec", () => {
  const issues = validateClassSpec(telekineticSpec);
  assertEqual(issues.length, 0, "no validation issues");
});

test("renderClass outputs clean UE4.27 header and source for UTelekineticComponent", () => {
  const rendered = renderClass(telekineticSpec);
  assertEqual(rendered.class_name, "UTelekineticComponent", "derived class name");
  assertEqual(rendered.base_class, "UActorComponent", "base class");
  
  const headerLines = rendered.header.split("\n");
  assertEqual(headerLines[0], "#pragma once", "header starts with #pragma once");
  assert(rendered.header.includes('#include "CoreMinimal.h"'), "includes CoreMinimal.h");
  assert(rendered.header.includes('#include "Components/ActorComponent.h"'), "includes parent component header");
  assert(rendered.header.includes('#include "TelekineticComponent.generated.h"'), "includes generated header");
  
  // Verify generated header is strictly the last include
  const includes = headerLines.filter((l) => l.startsWith("#include"));
  assertEqual(includes[includes.length - 1], '#include "TelekineticComponent.generated.h"', "generated include is last");
  
  // Verify delegate macro placement
  assert(rendered.header.includes("DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTelekineticStateChanged, bool, bHolding);"), "emits dynamic multicast delegate macro");
  
  // Verify UCLASS and member declarations
  assert(rendered.header.includes("class BLUEPRINTPRODUCTIONFIXTURE_API UTelekineticComponent : public UActorComponent"), "class export declaration");
  assert(rendered.header.includes("UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = \"Telekinesis\")"), "UPROPERTY specifiers");
  assert(rendered.header.includes("UFUNCTION(BlueprintCallable, Category = \"Telekinesis\")"), "UFUNCTION specifiers");
  assert(rendered.header.includes("bool IsHoldingTarget() const;"), "const pure query function");

  // Verify constructor & body implementation in source
  assert(rendered.source.includes("UTelekineticComponent::UTelekineticComponent()"), "constructor definition");
  assert(rendered.source.includes("GrabDistance = 1200.0f;"), "constructor default initialization");
  assert(rendered.source.includes("bIsHolding = true; return true;"), "GrabTarget method body");
});

test("checkHeaderRules validates generated UTelekineticComponent header against UHT rules", () => {
  const rendered = renderClass(telekineticSpec);
  const issues = checkHeaderRules(rendered.header);
  assertEqual(issues.length, 0, "header meets all UHT header rules");
});

test("feature planner generates hybrid architecture for telekinetic_component_handoff fixture", () => {
  const fixture = BLUEPRINT_PRODUCTION_FIXTURES.find((f) => f.feature_id === "telekinetic_component_handoff");
  assert(Boolean(fixture), "fixture telekinetic_component_handoff exists");
  
  const plan = planFeature(fixture!);
  assertEqual(plan.architecture.selected, "hybrid", "architecture selection is hybrid");
  assertEqual(plan.cpp.classes.length, 1, "contains 1 native C++ class plan");
  assertEqual(plan.cpp.classes[0].name, "UTelekineticComponent", "class name is UTelekineticComponent");
  
  assert(plan.architecture.build_order.includes("Generate C++ source code"), "build order includes C++ source generation");
  assert(plan.architecture.build_order.includes("Compile editor target once"), "build order includes editor target build");
  assert(plan.architecture.build_order.includes("Create Blueprint child assets"), "build order includes Blueprint child creation");
});

test("planFeature exhibits 100% idempotency for telekinetic handoff plan", () => {
  const fixture = BLUEPRINT_PRODUCTION_FIXTURES.find((f) => f.feature_id === "telekinetic_component_handoff")!;
  const plan1 = planFeature(fixture);
  const plan2 = planFeature(fixture);
  
  assertEqual(plan1.plan_hash_sha1, plan2.plan_hash_sha1, "SHA1 plan hashes are identical across runs");
  assertEqual(JSON.stringify(plan1.command_plan), JSON.stringify(plan2.command_plan), "command plans are byte-identical");
});

test("diffDesiredState identifies missing telekinetic Blueprint child and emits build repair", () => {
  const fixture = BLUEPRINT_PRODUCTION_FIXTURES.find((f) => f.feature_id === "telekinetic_component_handoff")!;
  const desired = fixture.blueprints[0];
  
  const repairs = diffDesiredState(desired, undefined);
  assertEqual(repairs.length, 1, "emits 1 repair operation for unbuilt asset");
  assertEqual(repairs[0].command, "puerts_blueprint_build", "repair command is puerts_blueprint_build");
  assertEqual(repairs[0].asset_path, "/Game/MCPGenerated/BP_TelekineticCharacter", "repair target asset path");
});

if (failed > 0) {
  console.log(`\nResults: ${passed} passed, ${failed} failed.`);
  for (const err of failures) console.log(` - ${err}`);
  process.exit(1);
} else {
  console.log(`\nResults: ${passed} passed, 0 failed.`);
}
