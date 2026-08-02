/**
 * Unit tests for headless C++ authoring.
 *
 * No editor, no engine, no compile. The generator and the text editors are
 * exercised against fixtures in a temp directory; the diagnostic parser is fed
 * real UBT/MSVC/UHT console text captured from failing 4.27 builds.
 *
 * Run: npx tsx mcp-server/tests/cpp-authoring.test.ts
 */

import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "fs";
import { tmpdir } from "os";
import { join } from "path";
import {
  checkHeaderRules,
  editBuildCsDependencies,
  editIncludes,
  parseBuildOutput,
  readBuildCsDependencies,
  renderClass,
  repairHints,
  resolveUbtBatch,
  validateClassSpec,
  writeClass,
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

const work = mkdtempSync(join(tmpdir(), "cpp-authoring-"));

// ------------------------------------------------------------ generator

const doorSpec: ClassSpec = {
  name: "DoorActor",
  module: "MyGame",
  parent: "Actor",
  class_specifiers: "Blueprintable",
  properties: [
    { name: "OpenAngle", type: "float", specifiers: "EditAnywhere, BlueprintReadWrite", category: "Door", default: "90.0f" },
    { name: "bLocked", type: "bool", specifiers: "EditAnywhere", category: "Door", default: "false" },
  ],
  functions: [
    { name: "Open", specifiers: "BlueprintCallable, Category = \"Door\"", body: "bLocked = false;" },
    { name: "IsOpen", return_type: "bool", specifiers: "BlueprintPure, Category = \"Door\"", is_const: true, body: "return !bLocked;" },
  ],
};

test("generated header carries the UE4.27 required shape", () => {
  const out = renderClass(doorSpec);
  assertEqual(out.class_name, "ADoorActor", "class name");
  assertEqual(out.base_class, "AActor", "base class");
  const lines = out.header.split("\n");
  assertEqual(lines[0], "#pragma once", "first line");
  assert(out.header.includes('#include "CoreMinimal.h"'), "CoreMinimal missing");
  assert(out.header.includes('#include "GameFramework/Actor.h"'), "parent include missing");
  assert(out.header.includes("class MYGAME_API ADoorActor : public AActor"), "API macro or declaration wrong");
  assert(out.header.includes("\tGENERATED_BODY()"), "GENERATED_BODY missing");
  assert(out.header.includes('UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Door")'), "UPROPERTY wrong");
  assert(out.header.includes("\tfloat OpenAngle;"), "property declaration wrong");
  assert(out.header.includes("\tbool IsOpen() const;"), "const function declaration wrong");
});

test("the .generated.h include is last", () => {
  const out = renderClass({ ...doorSpec, includes: ["Components/BoxComponent.h", "Engine/EngineTypes.h"] });
  const includes = out.header.split("\n").filter((l) => l.startsWith("#include"));
  assertEqual(includes[includes.length - 1], '#include "DoorActor.generated.h"', "generated include not last");
  assertEqual(checkHeaderRules(out.header).length, 0, "rendered header failed its own UHT rules");
});

test("generated cpp includes its header first and defines every member", () => {
  const out = renderClass(doorSpec);
  assertEqual(out.source.split("\n")[0], '#include "DoorActor.h"', "own header not first");
  assert(out.source.includes("ADoorActor::ADoorActor()"), "constructor missing");
  assert(out.source.includes("\tOpenAngle = 90.0f;"), "default not applied in constructor");
  assert(out.source.includes("void ADoorActor::Open()"), "function definition missing");
  assert(out.source.includes("bool ADoorActor::IsOpen() const"), "const function definition missing");
});

test("subdir affects the cpp include path", () => {
  const out = renderClass({ ...doorSpec, subdir: "Doors" });
  assertEqual(out.source.split("\n")[0], '#include "Doors/DoorActor.h"', "subdir include wrong");
});

test("parents pull in their Build.cs modules", () => {
  assert(renderClass({ name: "Chase", module: "MyGame", parent: "BTTaskNode" }).required_modules
    .includes("AIModule"), "AIModule not required for BTTaskNode");
  assert(renderClass({ name: "Menu", module: "MyGame", parent: "UserWidget" }).required_modules
    .includes("UMG"), "UMG not required for UserWidget");
});

test("validation rejects a name that already carries a prefix", () => {
  const issues = validateClassSpec({ name: "ADoorActor", module: "MyGame" });
  assert(issues.some((i) => i.field === "name"), `expected a name issue, got ${JSON.stringify(issues)}`);
});

test("validation rejects an unknown parent without an explicit include", () => {
  assert(validateClassSpec({ name: "Thing", module: "MyGame", parent: "AMyBase" }).length > 0, "unknown parent accepted");
  assertEqual(
    validateClassSpec({ name: "Thing", module: "MyGame", parent: "AMyBase", parent_include: "MyBase.h", parent_prefix: "A" }).length,
    0, "custom parent with include rejected");
});

test("validation rejects injection through a spec fragment", () => {
  const issues = validateClassSpec({
    name: "Thing", module: "MyGame",
    properties: [{ name: "Bad", type: "float X; public: int Y" }],
  });
  assert(issues.some((i) => i.field.endsWith(".type")), "statement injection through a type accepted");
});

test("validation rejects a UE5-only API anywhere in the spec", () => {
  const issues = validateClassSpec({
    name: "Player", module: "MyGame", parent: "Pawn",
    properties: [{ name: "InputComp", type: "UEnhancedInputComponent*" }],
  });
  assert(issues.some((i) => i.message.includes("UE4.27")), `expected a UE4.27 issue, got ${JSON.stringify(issues)}`);
});

test("validation rejects a subdir that escapes the module", () => {
  assert(validateClassSpec({ name: "Thing", module: "MyGame", subdir: "../../Engine" }).length > 0, "traversal accepted");
});

test("duplicate members are rejected", () => {
  const issues = validateClassSpec({
    name: "Thing", module: "MyGame",
    properties: [{ name: "Value", type: "float" }],
    functions: [{ name: "Value" }],
  });
  assert(issues.some((i) => i.message.includes("duplicate")), "duplicate member accepted");
});

// ------------------------------------------------------------ writing

test("writeClass uses the Public/Private split and writes both files", () => {
  const moduleRoot = join(work, "Source", "MyGame");
  mkdirSync(join(moduleRoot, "Public"), { recursive: true });
  mkdirSync(join(moduleRoot, "Private"), { recursive: true });
  const result = writeClass(doorSpec, { module_root: moduleRoot });
  assert(result.success, `write failed: ${result.error}`);
  assert(existsSync(join(moduleRoot, "Public", "DoorActor.h")), "header not written to Public");
  assert(existsSync(join(moduleRoot, "Private", "DoorActor.cpp")), "source not written to Private");
});

test("writeClass refuses to overwrite unless told to", () => {
  const moduleRoot = join(work, "Source", "MyGame");
  const again = writeClass(doorSpec, { module_root: moduleRoot });
  assert(!again.success, "overwrote an existing class silently");
  assert((again.error ?? "").includes("refusing to overwrite"), `wrong error: ${again.error}`);
  const forced = writeClass({ ...doorSpec, class_specifiers: "BlueprintType" }, { module_root: moduleRoot, overwrite: true });
  assert(forced.success, `forced overwrite failed: ${forced.error}`);
  assert(readFileSync(join(moduleRoot, "Public", "DoorActor.h"), "utf-8").includes("UCLASS(BlueprintType)"), "overwrite did not take");
});

test("writeClass writes nothing when the spec is invalid", () => {
  const moduleRoot = join(work, "Source", "MyGame");
  const result = writeClass({ name: "Bad Name", module: "MyGame" }, { module_root: moduleRoot });
  assert(!result.success, "invalid spec accepted");
  assert(!existsSync(join(moduleRoot, "Public", "Bad Name.h")), "wrote a file for an invalid spec");
});

test("dry_run reports the paths and writes nothing", () => {
  const moduleRoot = join(work, "Source", "MyGame");
  const result = writeClass({ name: "Ghost", module: "MyGame" }, { module_root: moduleRoot, dry_run: true });
  assert(result.success, `dry run failed: ${result.error}`);
  assert(!existsSync(join(moduleRoot, "Public", "Ghost.h")), "dry run wrote a file");
});

test("a flat module (no Public/Private) puts both files at the module root", () => {
  const moduleRoot = join(work, "Source", "FlatModule");
  mkdirSync(moduleRoot, { recursive: true });
  const result = writeClass({ name: "Widget", module: "FlatModule", parent: "Object" }, { module_root: moduleRoot });
  assert(result.success, `write failed: ${result.error}`);
  assert(existsSync(join(moduleRoot, "Widget.h")) && existsSync(join(moduleRoot, "Widget.cpp")), "flat layout wrong");
});

// ------------------------------------------------------------ UHT rules

test("checkHeaderRules catches a missing .generated.h", () => {
  const text = '#pragma once\n\n#include "CoreMinimal.h"\n\nUCLASS()\nclass MYGAME_API AFoo : public AActor\n{\n\tGENERATED_BODY()\n};\n';
  assert(checkHeaderRules(text).some((i) => i.message.includes("generated.h")), "missing generated.h not caught");
});

test("checkHeaderRules catches a .generated.h that is not last", () => {
  const text = '#pragma once\n\n#include "CoreMinimal.h"\n#include "Foo.generated.h"\n#include "GameFramework/Actor.h"\n\nUCLASS()\nclass MYGAME_API AFoo : public AActor\n{\n\tGENERATED_BODY()\n};\n';
  assert(checkHeaderRules(text).some((i) => i.message.includes("last include")), "misplaced generated.h not caught");
});

test("checkHeaderRules catches a missing GENERATED_BODY and a missing API macro", () => {
  const text = '#pragma once\n\n#include "CoreMinimal.h"\n#include "Foo.generated.h"\n\nUCLASS()\nclass AFoo : public AActor\n{\npublic:\n\tAFoo();\n};\n';
  const issues = checkHeaderRules(text);
  assert(issues.some((i) => i.message.includes("GENERATED_BODY")), "missing GENERATED_BODY not caught");
  assert(issues.some((i) => i.message.includes("_API")), "missing API macro not caught");
});

test("checkHeaderRules ignores a plain non-reflected header", () => {
  assertEqual(checkHeaderRules('#pragma once\n\n#include "CoreMinimal.h"\n\nstruct FPlainThing { int32 X; };\n').length, 0,
    "flagged a header with no reflected type");
});

// ------------------------------------------------------------ Build.cs

const MULTILINE_BUILD_CS = `// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MyGame : ModuleRules
{
	public MyGame(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
	}
}
`;

test("adding a dependency preserves multi-line formatting", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", add: ["AIModule"] });
  assert(out.changed, "no change reported");
  assert(out.text.includes('\t\t\t"InputCore",\n\t\t\t"AIModule"\n\t\t});'), `formatting lost:\n${out.text}`);
  assert(out.text.includes('"Slate", "SlateCore"'), "unrelated array was touched");
});

test("adding a dependency is idempotent", () => {
  const once = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", add: ["AIModule"] });
  const twice = editBuildCsDependencies(once.text, { kind: "Public", add: ["AIModule"] });
  assert(!twice.changed, "second add changed the file");
  assertEqual(twice.text, once.text, "text drifted on replay");
  assertEqual(twice.already_present[0], "AIModule", "already_present not reported");
});

test("adding to a single-line array keeps it on one line", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Private", add: ["UMG"] });
  assert(out.text.includes('new string[] { "Slate", "SlateCore", "UMG" }'), `single-line formatting lost:\n${out.text}`);
});

test("removing a middle dependency leaves no blank line or stray comma", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", remove: ["CoreUObject"] });
  assert(out.changed, "no change reported");
  assert(out.text.includes('\t\t\t"Core",\n\t\t\t"Engine",\n\t\t\t"InputCore"\n'), `removal formatting wrong:\n${out.text}`);
  assert(!out.text.includes("CoreUObject"), "entry still present");
});

test("removing the last dependency drops the preceding comma", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", remove: ["InputCore"] });
  assert(out.text.includes('\t\t\t"Engine"\n\t\t});'), `trailing comma left behind:\n${out.text}`);
});

test("removing an absent dependency is a no-op, not an error", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", remove: ["Niagara"] });
  assert(!out.changed, "reported a change");
  assertEqual(out.error, undefined, "reported an error");
  assertEqual(out.not_present[0], "Niagara", "not_present not reported");
});

test("a trailing comma in the array survives an add", () => {
  const source = 'PublicDependencyModuleNames.AddRange(new string[]\n\t\t{\n\t\t\t"Core",\n\t\t\t"Engine",\n\t\t});\n';
  const out = editBuildCsDependencies(source, { kind: "Public", add: ["UMG"] });
  assert(out.text.includes('\t\t\t"Engine",\n\t\t\t"UMG",\n'), `trailing-comma style lost:\n${out.text}`);
});

test("a bare .Add call counts as already present", () => {
  const source = MULTILINE_BUILD_CS.replace(
    "PrivateDependencyModuleNames.AddRange",
    'PublicDependencyModuleNames.Add("Niagara");\n\t\tPrivateDependencyModuleNames.AddRange');
  const out = editBuildCsDependencies(source, { kind: "Public", add: ["Niagara"] });
  assert(!out.changed, "added a module that a .Add call already declares");
});

test("a Build.cs with no AddRange block is refused, not guessed at", () => {
  const out = editBuildCsDependencies("public class X : ModuleRules { }", { kind: "Public", add: ["Engine"] });
  assert(!out.changed, "changed a file with no dependency array");
  assert((out.error ?? "").includes("refusing to guess"), `wrong error: ${out.error}`);
});

test("an invalid module name is refused", () => {
  const out = editBuildCsDependencies(MULTILINE_BUILD_CS, { kind: "Public", add: ['Engine", "Evil'] });
  assert((out.error ?? "").includes("not a valid module name"), `injection accepted: ${out.error}`);
  assertEqual(out.text, MULTILINE_BUILD_CS, "text changed despite the error");
});

test("readBuildCsDependencies sees both array entries and bare adds", () => {
  const deps = readBuildCsDependencies(MULTILINE_BUILD_CS, "Public");
  assertEqual(deps.join(","), "Core,CoreUObject,Engine,InputCore", "public dependencies wrong");
  assertEqual(readBuildCsDependencies(MULTILINE_BUILD_CS, "Private").join(","), "Slate,SlateCore", "private dependencies wrong");
});

test("the Build.cs editor round-trips through a real file", () => {
  const path = join(work, "MyGame.Build.cs");
  writeFileSync(path, MULTILINE_BUILD_CS, "utf-8");
  const added = editBuildCsDependencies(readFileSync(path, "utf-8"), { kind: "Public", add: ["AIModule", "UMG"] });
  writeFileSync(path, added.text, "utf-8");
  const back = editBuildCsDependencies(readFileSync(path, "utf-8"), { kind: "Public", remove: ["AIModule", "UMG"] });
  assertEqual(back.text, MULTILINE_BUILD_CS, "add then remove did not restore the original text");
});

// ------------------------------------------------------------ includes

const HEADER_FIXTURE = `#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DoorActor.generated.h"

UCLASS()
class MYGAME_API ADoorActor : public AActor
{
	GENERATED_BODY()
};
`;

test("an added include lands above the .generated.h", () => {
  const out = editIncludes(HEADER_FIXTURE, { add: ["Components/BoxComponent.h"] });
  assert(out.changed, "no change reported");
  const includes = out.text.split("\n").filter((l) => l.startsWith("#include"));
  assertEqual(includes[2], '#include "Components/BoxComponent.h"', "insert position wrong");
  assertEqual(includes[3], '#include "DoorActor.generated.h"', "generated include no longer last");
});

test("adding an include is idempotent", () => {
  const once = editIncludes(HEADER_FIXTURE, { add: ["Components/BoxComponent.h"] });
  const twice = editIncludes(once.text, { add: ["Components/BoxComponent.h"] });
  assert(!twice.changed, "second add changed the file");
  assertEqual(twice.text, once.text, "text drifted on replay");
});

test("removing an include is idempotent and absence is not an error", () => {
  const out = editIncludes(HEADER_FIXTURE, { remove: ["GameFramework/Actor.h"] });
  assert(!out.text.includes("GameFramework/Actor.h"), "include not removed");
  const again = editIncludes(out.text, { remove: ["GameFramework/Actor.h"] });
  assert(!again.changed, "second remove changed the file");
  assertEqual(again.not_present[0], "GameFramework/Actor.h", "not_present not reported");
});

test("a misplaced .generated.h is moved back to last", () => {
  const broken = HEADER_FIXTURE.replace(
    '#include "DoorActor.generated.h"',
    '#include "DoorActor.generated.h"\n#include "Engine/EngineTypes.h"');
  const out = editIncludes(broken, {});
  assert(out.generated_reordered, "reorder not reported");
  const includes = out.text.split("\n").filter((l) => l.startsWith("#include"));
  assertEqual(includes[includes.length - 1], '#include "DoorActor.generated.h"', "generated include still misplaced");
  assertEqual(checkHeaderRules(out.text).length, 0, "repaired header still violates the UHT rules");
});

test("an include is added to a cpp with no pragma once", () => {
  const out = editIncludes('#include "DoorActor.h"\n\nvoid F() {}\n', { add: ["Engine/World.h"] });
  assertEqual(out.text.split("\n")[1], '#include "Engine/World.h"', "insert position wrong");
});

test("an include is added to a header that has none", () => {
  const out = editIncludes("#pragma once\n\nstruct FThing {};\n", { add: ["CoreMinimal.h"] });
  assert(out.text.includes('#pragma once\n\n#include "CoreMinimal.h"'), `insert position wrong:\n${out.text}`);
});

// ------------------------------------------------------------ diagnostics

// Real UBT console output from a failed 4.27 editor-target build.
const REAL_BUILD_OUTPUT = `Running UnrealBuildTool: dotnet "..\\..\\Engine\\Binaries\\DotNET\\UnrealBuildTool.exe" MyGameEditor Win64 Development -Project="D:\\Projects\\MyGame\\MyGame.uproject" -WaitMutex -NoHotReload
Building MyGameEditor...
Using Visual Studio 2019 14.29.30154 toolchain and Windows 10.0.18362.0 SDK.
Determining max actions to execute in parallel (12 physical cores, 24 logical cores)
Parsing headers for MyGameEditor
D:\\Projects\\MyGame\\Source\\MyGame\\Public\\DoorActor.h(9): Error: Missing '#include "DoorActor.generated.h"'
D:\\Projects\\MyGame\\Source\\MyGame\\Public\\LockComponent.h(14): Error: Expected LockComponent.generated.h to be the last header included
Reflection code generated for MyGameEditor in 6.7 seconds
[1/12] Compile DoorActor.cpp
D:\\Projects\\MyGame\\Source\\MyGame\\Private\\DoorActor.cpp(41): warning C4996: 'UEditorEngine::RequestPlaySession': Use the overload taking a FRequestPlaySessionParams. Please update your code to the new API before upgrading to the next release, otherwise your project will no longer compile.
D:\\Projects\\MyGame\\Source\\MyGame\\Private\\DoorActor.cpp(52): error C2065: 'UEnhancedInputComponent': undeclared identifier
D:\\Projects\\MyGame\\Source\\MyGame\\Private\\DoorActor.cpp(53,17): error C2039: 'Foo': is not a member of 'ADoorActor'
D:\\Projects\\MyGame\\Source\\MyGame\\Public\\PatrolTask.h(4): fatal error C1083: Cannot open include file: 'BehaviorTree/BTTaskNode.h': No such file or directory
[7/12] Link UE4Editor-MyGame.dll
DoorActor.cpp.obj : error LNK2019: unresolved external symbol "public: void __cdecl ADoorActor::Missing(void)" referenced in function main
LINK : fatal error LNK1104: cannot open file 'D:\\Projects\\MyGame\\Binaries\\Win64\\UE4Editor-MyGame.dll'
ERROR: UBT ERROR: Failed to produce item: D:\\Projects\\MyGame\\Binaries\\Win64\\UE4Editor-MyGame.dll
Total build time: 41.55 seconds (Parallel executor: 0.00 seconds)
`;

test("the parser handles MSVC errors with and without a column", () => {
  const result = parseBuildOutput(REAL_BUILD_OUTPUT, 6);
  const c2065 = result.errors.find((d) => d.code === "C2065");
  assert(c2065 !== undefined, "C2065 not parsed");
  assertEqual(c2065!.file, "D:\\Projects\\MyGame\\Source\\MyGame\\Private\\DoorActor.cpp", "file wrong");
  assertEqual(c2065!.line, 52, "line wrong");
  assertEqual(c2065!.column, null, "column should be absent");
  assertEqual(c2065!.severity, "error", "severity wrong");
  assertEqual(c2065!.message, "'UEnhancedInputComponent': undeclared identifier", "message wrong");

  const c2039 = result.errors.find((d) => d.code === "C2039");
  assertEqual(c2039!.line, 53, "line wrong");
  assertEqual(c2039!.column, 17, "column not parsed");
});

test("the parser handles the C4996 deprecation warning", () => {
  const result = parseBuildOutput(REAL_BUILD_OUTPUT, 6);
  const warning = result.warnings.find((d) => d.code === "C4996");
  assert(warning !== undefined, "C4996 not parsed");
  assertEqual(warning!.severity, "warning", "severity wrong");
  assertEqual(warning!.line, 41, "line wrong");
  assert(warning!.message.startsWith("'UEditorEngine::RequestPlaySession': Use the overload"), `message wrong: ${warning!.message}`);
});

test("the parser handles a bare compiler warning with no file prefix", () => {
  const result = parseBuildOutput(
    "warning C4996: 'UEditorEngine::RequestPlaySession': Use the overload taking a FRequestPlaySessionParams.", 0);
  assertEqual(result.warning_count, 1, "bare warning not parsed");
  assertEqual(result.warnings[0].file, null, "file should be null");
  assertEqual(result.warnings[0].line, null, "line should be null");
  assertEqual(result.warnings[0].code, "C4996", "code wrong");
});

test("the parser handles the LINK fatal error", () => {
  const result = parseBuildOutput(REAL_BUILD_OUTPUT, 6);
  const lnk = result.errors.find((d) => d.code === "LNK1104");
  assert(lnk !== undefined, "LNK1104 not parsed");
  assertEqual(lnk!.file, "LINK", "pseudo-file wrong");
  assertEqual(lnk!.line, null, "line should be null");
  assert(lnk!.message.startsWith("cannot open file"), `message wrong: ${lnk!.message}`);
  assert(result.errors.some((d) => d.code === "LNK2019"), "LNK2019 not parsed");
});

test("the parser handles UHT errors, which carry no code", () => {
  const result = parseBuildOutput(REAL_BUILD_OUTPUT, 6);
  const uht = result.errors.filter((d) => d.code === null && d.message.includes("generated.h"));
  assertEqual(uht.length, 2, `expected 2 UHT errors, got ${JSON.stringify(uht)}`);
  assertEqual(uht[0].line, 9, "UHT line wrong");
  assertEqual(uht[0].file, "D:\\Projects\\MyGame\\Source\\MyGame\\Public\\DoorActor.h", "UHT file wrong");
});

test("the parser ignores ordinary progress output", () => {
  const result = parseBuildOutput(REAL_BUILD_OUTPUT, 6);
  const noise = [...result.errors, ...result.warnings].filter((d) =>
    d.message.includes("Total build time") || d.message.includes("Compile DoorActor"));
  assertEqual(noise.length, 0, `progress lines parsed as diagnostics: ${JSON.stringify(noise)}`);
  assert(!result.succeeded, "a failing build reported success");
  assertEqual(result.exit_code, 6, "exit code not carried");
});

test("a clean build with a nonzero exit code is still a failure", () => {
  assertEqual(parseBuildOutput("Total build time: 3.0 seconds\n", 1).succeeded, false, "nonzero exit reported as success");
  assertEqual(parseBuildOutput("Total build time: 3.0 seconds\n", 0).succeeded, true, "clean build reported as failure");
});

// ------------------------------------------------------------ repair hints

test("a missing .generated.h produces the include hint", () => {
  const hints = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints;
  const hint = hints.find((h) => h.id === "generated_include_missing");
  assert(hint !== undefined, `missing generated.h hint absent: ${JSON.stringify(hints.map((h) => h.id))}`);
  assert(hint!.fix.includes("LAST include"), `fix does not say where to put it: ${hint!.fix}`);
});

test("a misplaced .generated.h produces the ordering hint", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints.find((h) => h.id === "generated_include_not_last");
  assert(hint !== undefined, "ordering hint absent");
  assert(hint!.fix.includes("below every other"), `fix wrong: ${hint!.fix}`);
});

test("a missing GENERATED_BODY produces its hint", () => {
  const hints = repairHints(parseBuildOutput(
    "D:\\Projects\\MyGame\\Source\\MyGame\\Public\\DoorActor.h(12): Error: Expected a GENERATED_BODY() macro in class ADoorActor", 6).errors);
  const hint = hints.find((h) => h.id === "generated_body_missing");
  assert(hint !== undefined, `GENERATED_BODY hint absent: ${JSON.stringify(hints)}`);
  assert(hint!.fix.includes("first line inside the type body"), `fix wrong: ${hint!.fix}`);
});

test("an unresolvable engine include names the module to add to Build.cs", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints
    .find((h) => h.id === "module_dependency_missing" && h.problem.includes("BTTaskNode"));
  assert(hint !== undefined, "module dependency hint absent");
  assert(hint!.fix.includes("AIModule") && hint!.fix.includes("Build.cs"), `fix wrong: ${hint!.fix}`);
});

test("an unresolved external also points at Build.cs", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints
    .find((h) => h.id === "module_dependency_missing" && h.problem.includes("unresolved external"));
  assert(hint !== undefined, "unresolved external hint absent");
  assert(hint!.fix.includes("Build.cs") && hint!.fix.includes("_API"), `fix wrong: ${hint!.fix}`);
});

test("a UE5-only API in a compiler error names the 4.27 replacement", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints.find((h) => h.id === "ue5_only_api");
  assert(hint !== undefined, "UE5 API hint absent");
  assert(hint!.problem.includes("EnhancedInputComponent"), `problem wrong: ${hint!.problem}`);
  assert(hint!.fix.includes("BindAxis"), `fix does not name the 4.27 replacement: ${hint!.fix}`);
});

test("a locked output DLL is explained as a running editor", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints.find((h) => h.id === "output_locked");
  assert(hint !== undefined, "locked DLL hint absent");
  assert(hint!.fix.includes("editor"), `fix wrong: ${hint!.fix}`);
});

test("the C4996 deprecation carries the compiler's own instruction", () => {
  const hint = parseBuildOutput(REAL_BUILD_OUTPUT, 6).hints.find((h) => h.id === "deprecated_api");
  assert(hint !== undefined, "deprecation hint absent");
  assert(hint!.fix.includes("FRequestPlaySessionParams"), `fix dropped the instruction: ${hint!.fix}`);
});

test("hints are deduplicated across repeated diagnostics", () => {
  const repeated = Array(30).fill(
    "D:\\P\\A.cpp(1): fatal error C1083: Cannot open include file: 'AIController.h': No such file or directory").join("\n");
  const hints = parseBuildOutput(repeated, 6).hints;
  assertEqual(hints.length, 1, `expected 1 deduplicated hint, got ${hints.length}`);
});

test("a clean build produces no hints", () => {
  assertEqual(parseBuildOutput("Build succeeded.\nTotal build time: 3.0 seconds\n", 0).hints.length, 0, "hints on a clean build");
});

// ------------------------------------------------------------ UBT invocation

test("the UBT entry point resolves to Build.bat under the engine root", () => {
  const batch = resolveUbtBatch("D:/UE/UE_4.27");
  assert(batch.endsWith(join("Engine", "Build", "BatchFiles", "Build.bat")), `wrong entry point: ${batch}`);
});

// ------------------------------------------------------------ summary

rmSync(work, { recursive: true, force: true });

console.log(`\ncpp authoring: ${passed} passed, ${failed} failed`);
if (failed > 0) {
  for (const f of failures) console.log(`  ${f}`);
  process.exit(1);
}
