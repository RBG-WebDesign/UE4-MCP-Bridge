/**
 * Headless C++ authoring for a UE4.27 project.
 *
 * Everything here runs inside the MCP server process and touches only the
 * filesystem and UnrealBuildTool. No editor, no listener, no named pipe. That
 * is deliberate: C++ iteration happens while the editor is closed, because a
 * new UCLASS cannot be loaded without an editor restart anyway.
 *
 * Split of responsibilities:
 *   - pure text functions (renderClass, editBuildCsDependencies, editIncludes,
 *     parseBuildOutput, repairHints) take text and return text or data, so they
 *     are testable against fixtures with nothing installed.
 *   - operations that touch disk or spawn a process (writeClass, runUbt) return
 *     the standard {success, data, error} envelope.
 *
 * Design and the UE4.27 constraints being enforced: docs/CPP_AUTHORING.md.
 */

import { spawnSync } from "child_process";
import { existsSync, mkdirSync, rmSync, writeFileSync } from "fs";
import { dirname, isAbsolute, join } from "path";

// ---------------------------------------------------------------- types

export type Severity = "error" | "warning" | "note";

export interface Diagnostic {
  /** Source file, or a pseudo-file such as "LINK" / "cl", or null when the line carries none. */
  file: string | null;
  line: number | null;
  column: number | null;
  /** MSVC/UHT code such as C2065 or LNK1104. UHT messages often have none. */
  code: string | null;
  message: string;
  severity: Severity;
  raw: string;
}

export interface RepairHint {
  /** Stable identifier for the hint, not the compiler code. */
  id: string;
  diagnostic: Diagnostic | null;
  problem: string;
  fix: string;
}

export interface BuildResult {
  succeeded: boolean;
  exit_code: number | null;
  error_count: number;
  warning_count: number;
  errors: Diagnostic[];
  warnings: Diagnostic[];
  notes: Diagnostic[];
  hints: RepairHint[];
  output_tail: string[];
}

export interface ValidationIssue {
  field: string;
  message: string;
}

export interface PropertySpec {
  name: string;
  /** C++ type as written, e.g. "float", "TArray<FName>", "UStaticMeshComponent*". */
  type: string;
  /** UPROPERTY specifiers, e.g. "EditAnywhere, BlueprintReadWrite". */
  specifiers?: string;
  /** Category shorthand, appended to the specifiers as Category = "...". */
  category?: string;
  /** Constructor initialiser, emitted in the .cpp constructor body. */
  default?: string;
  comment?: string;
}

export interface FunctionSpec {
  name: string;
  /** Defaults to void. */
  return_type?: string;
  /** Parameter list as written, e.g. "float DeltaSeconds, bool bForce". */
  params?: string;
  /** UFUNCTION specifiers, e.g. "BlueprintCallable, Category = \"Door\"". */
  specifiers?: string;
  is_const?: boolean;
  is_virtual?: boolean;
  /** Body statements for the .cpp. Defaults to an empty body. */
  body?: string;
  comment?: string;
}

export interface DelegateSpec {
  name: string;
  params?: Array<{ name: string; type: string }>;
  specifiers?: string;
  comment?: string;
}

export interface ClassSpec {
  /** Class name WITHOUT the A/U prefix. */
  name: string;
  /** Owning module; supplies the <MODULE>_API export macro. */
  module: string;
  /** Key of PARENT_CLASSES, or any base when parent_include and parent_prefix are given. */
  parent?: string;
  parent_include?: string;
  parent_prefix?: "A" | "U";
  /** UCLASS() specifiers, e.g. "Blueprintable, BlueprintType". */
  class_specifiers?: string;
  /** Extra includes for the header, inserted before the .generated.h. */
  includes?: string[];
  properties?: PropertySpec[];
  functions?: FunctionSpec[];
  delegates?: DelegateSpec[];
  /** Path under Public/ and Private/ (or the module root on a flat module). */
  subdir?: string;
}

export interface RenderedClass {
  class_name: string;
  base_class: string;
  header_file: string;
  source_file: string;
  header: string;
  source: string;
  /** Modules this parent needs in Build.cs beyond Core/CoreUObject/Engine. */
  required_modules: string[];
}

// ---------------------------------------------------------------- tables

/** parent key -> [prefix, include, base class, extra Build.cs modules] */
const PARENT_CLASSES: Record<string, [("A" | "U"), string, string, string[]]> = {
  Actor: ["A", "GameFramework/Actor.h", "AActor", []],
  Pawn: ["A", "GameFramework/Pawn.h", "APawn", []],
  Character: ["A", "GameFramework/Character.h", "ACharacter", []],
  GameModeBase: ["A", "GameFramework/GameModeBase.h", "AGameModeBase", []],
  GameStateBase: ["A", "GameFramework/GameStateBase.h", "AGameStateBase", []],
  PlayerController: ["A", "GameFramework/PlayerController.h", "APlayerController", []],
  PlayerState: ["A", "GameFramework/PlayerState.h", "APlayerState", []],
  HUD: ["A", "GameFramework/HUD.h", "AHUD", []],
  AIController: ["A", "AIController.h", "AAIController", ["AIModule"]],
  ActorComponent: ["U", "Components/ActorComponent.h", "UActorComponent", []],
  SceneComponent: ["U", "Components/SceneComponent.h", "USceneComponent", []],
  GameInstance: ["U", "Engine/GameInstance.h", "UGameInstance", []],
  SaveGame: ["U", "GameFramework/SaveGame.h", "USaveGame", []],
  Object: ["U", "UObject/NoExportTypes.h", "UObject", []],
  BTTaskNode: ["U", "BehaviorTree/BTTaskNode.h", "UBTTaskNode", ["AIModule", "GameplayTasks"]],
  BTService: ["U", "BehaviorTree/BTService.h", "UBTService", ["AIModule", "GameplayTasks"]],
  BlueprintFunctionLibrary: ["U", "Kismet/BlueprintFunctionLibrary.h", "UBlueprintFunctionLibrary", []],
  UserWidget: ["U", "Blueprint/UserWidget.h", "UUserWidget", ["UMG", "Slate", "SlateCore"]],
};

/**
 * UE5 APIs AGENTS.md forbids on this 4.27 target, with the replacement.
 * Matched with word boundaries so UCameraShakeBase is not mistaken for the
 * removed UCameraShake.
 */
const FORBIDDEN_UE5_API: Array<{ pattern: RegExp; token: string; replacement: string }> = [
  { pattern: /\bUEnhancedInputComponent\b|\bEnhancedInputComponent\b/, token: "EnhancedInputComponent", replacement: "UInputComponent with BindAxis / BindAction" },
  { pattern: /\bUEnhancedInputLocalPlayerSubsystem\b|\bEnhancedInputSubsystem\b/, token: "EnhancedInputSubsystem", replacement: "UInputComponent with BindAxis / BindAction" },
  { pattern: /\bUE::Tasks\b|\bTasks::Launch\b/, token: "UE::Tasks", replacement: "FAsyncTask, or FTimerManager::SetTimer" },
  { pattern: /\bMassEntity\w*\b|\bUMass\w+\b/, token: "MassAI", replacement: "BehaviorTree plus AIController" },
  { pattern: /\bUSmartObject\w*\b|\bSmartObjectSubsystem\b/, token: "SmartObjects", replacement: "manual triggers or overlap volumes" },
  { pattern: /\bUStateTree\w*\b|\bFStateTree\w*\b/, token: "StateTree", replacement: "BehaviorTree" },
  { pattern: /\bAnimNext\w*\b/, token: "AnimNext", replacement: "UAnimInstance, or Montage_Play" },
  { pattern: /\bULevelEditorSubsystem\b/, token: "LevelEditorSubsystem", replacement: "direct GEditor access" },
  { pattern: /\bUEditorUtilitySubsystem\b/, token: "EditorUtilitySubsystem", replacement: "FKismetEditorUtilities" },
  { pattern: /\bUEditorPlaySessionSubsystem\b/, token: "EditorPlaySessionSubsystem", replacement: "GEditor->RequestPlaySession" },
  { pattern: /\bUCameraShake\b/, token: "UCameraShake", replacement: "UCameraShakeBase from Camera/CameraShakeBase.h" },
  { pattern: /\bPlayCameraShake\b/, token: "PlayCameraShake", replacement: "APlayerCameraManager::StartCameraShake" },
];

/** Header -> modules that must appear in Build.cs for it to resolve. */
const INCLUDE_TO_MODULE: Array<[RegExp, string[]]> = [
  [/^AIController\.h$|^BehaviorTree\//, ["AIModule", "GameplayTasks"]],
  [/^Blueprint\/UserWidget\.h$|^Components\/(Button|TextBlock|CanvasPanel|Image)\.h$/, ["UMG", "Slate", "SlateCore"]],
  [/^Widgets\/|^SlateBasics\.h$|^SlateCore/, ["Slate", "SlateCore"]],
  [/^Niagara/, ["Niagara"]],
  [/^NavigationSystem\.h$|^NavAreas\/|^NavMesh\//, ["NavigationSystem"]],
  [/^GameplayTag/, ["GameplayTags"]],
  [/^AbilitySystem|^GameplayAbilit/, ["GameplayAbilities", "GameplayTags", "GameplayTasks"]],
  [/^MoviePlayer\.h$/, ["MoviePlayer"]],
  [/^MediaPlayer\.h$|^MediaSource\.h$/, ["MediaAssets"]],
  [/^Http\.h$/, ["HTTP"]],
  [/^Json\.h$|^Dom\/JsonObject\.h$/, ["Json", "JsonUtilities"]],
  [/^PhysicsEngine\//, ["PhysicsCore"]],
];

const CPP_KEYWORDS = new Set([
  "class", "struct", "enum", "union", "template", "typename", "namespace", "public",
  "private", "protected", "virtual", "const", "static", "void", "int", "float",
  "double", "bool", "char", "return", "if", "else", "for", "while", "switch",
  "case", "break", "continue", "new", "delete", "this", "operator", "friend",
  "inline", "explicit", "typedef", "using", "auto", "true", "false", "nullptr",
]);

const IDENTIFIER = /^[A-Za-z_][A-Za-z0-9_]*$/;

// ---------------------------------------------------------------- validation

/** Any forbidden UE5 API token present in the text, with its 4.27 replacement. */
export function findForbiddenUe5Api(text: string): Array<{ token: string; replacement: string }> {
  const found: Array<{ token: string; replacement: string }> = [];
  for (const entry of FORBIDDEN_UE5_API) {
    if (entry.pattern.test(text)) found.push({ token: entry.token, replacement: entry.replacement });
  }
  return found;
}

function checkIdentifier(value: string, field: string, issues: ValidationIssue[]): void {
  if (!IDENTIFIER.test(value)) {
    issues.push({ field, message: `"${value}" is not a C++ identifier` });
  } else if (CPP_KEYWORDS.has(value)) {
    issues.push({ field, message: `"${value}" is a C++ keyword` });
  }
}

/** Fragments that would let a spec field escape its position in the template. */
function checkFragment(value: string, field: string, issues: ValidationIssue[]): void {
  if (/[\r\n]/.test(value)) issues.push({ field, message: "must be a single line" });
  if (value.includes(";")) issues.push({ field, message: "must not contain ';'" });
  if (/\/\*|\*\/|\/\//.test(value)) issues.push({ field, message: "must not contain a comment" });
}

export function validateClassSpec(spec: ClassSpec): ValidationIssue[] {
  const issues: ValidationIssue[] = [];

  checkIdentifier(spec.name ?? "", "name", issues);
  if (/^[AUFES][A-Z]/.test(spec.name ?? "")) {
    issues.push({
      field: "name",
      message: `"${spec.name}" already carries a UE type prefix; pass the bare name, the prefix is derived from the parent`,
    });
  }
  checkIdentifier(spec.module ?? "", "module", issues);

  const parentKey = spec.parent ?? "Actor";
  const known = PARENT_CLASSES[parentKey];
  if (!known) {
    if (!spec.parent_include || !spec.parent_prefix) {
      issues.push({
        field: "parent",
        message:
          `unknown parent "${parentKey}"; pass parent_include and parent_prefix to use a custom base. ` +
          `Known: ${Object.keys(PARENT_CLASSES).sort().join(", ")}`,
      });
    } else {
      checkFragment(spec.parent_include, "parent_include", issues);
    }
  }

  if (spec.subdir) {
    if (isAbsolute(spec.subdir) || spec.subdir.includes("..")) {
      issues.push({ field: "subdir", message: "must be a relative path inside the module, with no '..'" });
    }
  }

  const seen = new Set<string>();
  for (const [index, prop] of (spec.properties ?? []).entries()) {
    const field = `properties[${index}]`;
    checkIdentifier(prop.name ?? "", `${field}.name`, issues);
    if (seen.has(prop.name)) issues.push({ field: `${field}.name`, message: `duplicate member "${prop.name}"` });
    seen.add(prop.name);
    if (!prop.type || !prop.type.trim()) issues.push({ field: `${field}.type`, message: "type is required" });
    else checkFragment(prop.type, `${field}.type`, issues);
    if (prop.specifiers) checkFragment(prop.specifiers, `${field}.specifiers`, issues);
    if (prop.default) checkFragment(prop.default, `${field}.default`, issues);
  }

  for (const [index, fn] of (spec.functions ?? []).entries()) {
    const field = `functions[${index}]`;
    checkIdentifier(fn.name ?? "", `${field}.name`, issues);
    if (seen.has(fn.name)) issues.push({ field: `${field}.name`, message: `duplicate member "${fn.name}"` });
    seen.add(fn.name);
    if (fn.return_type) checkFragment(fn.return_type, `${field}.return_type`, issues);
    if (fn.params) checkFragment(fn.params, `${field}.params`, issues);
    if (fn.specifiers) checkFragment(fn.specifiers, `${field}.specifiers`, issues);
  }

  for (const [index, del] of (spec.delegates ?? []).entries()) {
    const field = `delegates[${index}]`;
    checkIdentifier(del.name ?? "", `${field}.name`, issues);
    if (seen.has(del.name)) issues.push({ field: `${field}.name`, message: `duplicate member "${del.name}"` });
    seen.add(del.name);
    if (del.specifiers) checkFragment(del.specifiers, `${field}.specifiers`, issues);
  }

  for (const [index, inc] of (spec.includes ?? []).entries()) {
    checkFragment(inc, `includes[${index}]`, issues);
    if (inc.endsWith(".generated.h")) {
      issues.push({
        field: `includes[${index}]`,
        message: "the .generated.h include is emitted last by the generator; do not pass it",
      });
    }
  }

  // Whole-spec scan last, so a UE5 API smuggled in through a type or a function
  // body is refused even though every field is individually well formed.
  const blob = JSON.stringify(spec);
  for (const hit of findForbiddenUe5Api(blob)) {
    issues.push({ field: "spec", message: `${hit.token} does not exist in UE4.27; use ${hit.replacement}` });
  }

  return issues;
}

// ---------------------------------------------------------------- generation

function joinSpecifiers(specifiers: string | undefined, category: string | undefined): string {
  const parts: string[] = [];
  if (specifiers && specifiers.trim()) parts.push(specifiers.trim());
  if (category && category.trim()) parts.push(`Category = "${category.trim()}"`);
  return parts.join(", ");
}

/** Render a header/source pair. Assumes the spec already validated clean. */
export function renderClass(spec: ClassSpec): RenderedClass {
  const parentKey = spec.parent ?? "Actor";
  const known = PARENT_CLASSES[parentKey];
  const prefix = known ? known[0] : (spec.parent_prefix ?? "U");
  const parentInclude = known ? known[1] : (spec.parent_include ?? "");
  const base = known ? known[2] : parentKey;
  const requiredModules = known ? [...known[3]] : [];

  const className = `${prefix}${spec.name}`;
  const api = `${spec.module.toUpperCase()}_API`;
  const subdir = (spec.subdir ?? "").replace(/\\/g, "/").replace(/\/+$/, "");
  const headerRel = subdir ? `${subdir}/${spec.name}.h` : `${spec.name}.h`;

  for (const inc of [parentInclude, ...(spec.includes ?? [])]) {
    for (const [pattern, modules] of INCLUDE_TO_MODULE) {
      if (pattern.test(inc)) for (const m of modules) if (!requiredModules.includes(m)) requiredModules.push(m);
    }
  }

  const headerLines: string[] = ["#pragma once", "", '#include "CoreMinimal.h"'];
  if (parentInclude) headerLines.push(`#include "${parentInclude}"`);
  for (const inc of spec.includes ?? []) headerLines.push(`#include "${inc}"`);
  // Always last. UHT reads the include list positionally and rejects anything
  // after the generated header.
  headerLines.push(`#include "${spec.name}.generated.h"`, "");

  for (const del of spec.delegates ?? []) {
    if (del.comment) headerLines.push(`/** ${del.comment} */`);
    if (del.specifiers) headerLines.push(`UDELEGATE(${del.specifiers})`);
    const count = del.params?.length ?? 0;
    const suffixMap: Record<number, string> = { 0: "", 1: "_OneParam", 2: "_TwoParams", 3: "_ThreeParams", 4: "_FourParams" };
    const suffix = suffixMap[count] ?? `_${count}Params`;
    const paramsList = (del.params ?? []).map((p) => `${p.type}, ${p.name}`).join(", ");
    const macroBody = paramsList ? `${del.name}, ${paramsList}` : del.name;
    headerLines.push(`DECLARE_DYNAMIC_MULTICAST_DELEGATE${suffix}(${macroBody});`, "");
  }

  headerLines.push(`UCLASS(${(spec.class_specifiers ?? "").trim()})`);
  headerLines.push(`class ${api} ${className} : public ${base}`);
  headerLines.push("{");
  headerLines.push("\tGENERATED_BODY()");
  headerLines.push("");
  headerLines.push("public:");
  headerLines.push(`\t${className}();`);

  for (const prop of spec.properties ?? []) {
    headerLines.push("");
    if (prop.comment) headerLines.push(`\t/** ${prop.comment} */`);
    headerLines.push(`\tUPROPERTY(${joinSpecifiers(prop.specifiers, prop.category)})`);
    headerLines.push(`\t${prop.type.trim()} ${prop.name};`);
  }

  for (const fn of spec.functions ?? []) {
    headerLines.push("");
    if (fn.comment) headerLines.push(`\t/** ${fn.comment} */`);
    headerLines.push(`\tUFUNCTION(${(fn.specifiers ?? "").trim()})`);
    const virtualKeyword = fn.is_virtual ? "virtual " : "";
    const constKeyword = fn.is_const ? " const" : "";
    headerLines.push(
      `\t${virtualKeyword}${(fn.return_type ?? "void").trim()} ${fn.name}(${(fn.params ?? "").trim()})${constKeyword};`
    );
  }

  headerLines.push("};", "");

  const sourceLines: string[] = [`#include "${headerRel}"`, ""];
  sourceLines.push(`${className}::${className}()`, "{");
  if (prefix === "A") sourceLines.push("\tPrimaryActorTick.bCanEverTick = false;");
  for (const prop of spec.properties ?? []) {
    if (prop.default && prop.default.trim()) sourceLines.push(`\t${prop.name} = ${prop.default.trim()};`);
  }
  sourceLines.push("}");

  for (const fn of spec.functions ?? []) {
    sourceLines.push("");
    const constKeyword = fn.is_const ? " const" : "";
    sourceLines.push(
      `${(fn.return_type ?? "void").trim()} ${className}::${fn.name}(${(fn.params ?? "").trim()})${constKeyword}`
    );
    sourceLines.push("{");
    for (const line of (fn.body ?? "").split("\n")) {
      if (line.trim()) sourceLines.push(`\t${line.trim()}`);
    }
    sourceLines.push("}");
  }
  sourceLines.push("");

  return {
    class_name: className,
    base_class: base,
    header_file: `${spec.name}.h`,
    source_file: `${spec.name}.cpp`,
    header: headerLines.join("\n"),
    source: sourceLines.join("\n"),
    required_modules: requiredModules,
  };
}

export interface WriteClassOptions {
  /** Source/<Module> directory. Public/Private split is used when both exist. */
  module_root: string;
  overwrite?: boolean;
  /** Render and validate, report the paths, write nothing. */
  dry_run?: boolean;
}

export interface Envelope<T> {
  success: boolean;
  data: T;
  error?: string;
}

/**
 * Validate, render, then write both files or neither.
 *
 * Refuses to overwrite unless overwrite is set. If the second write fails, any
 * file this call created is removed, so a half-written class never reaches UHT.
 */
export function writeClass(spec: ClassSpec, options: WriteClassOptions): Envelope<Record<string, unknown>> {
  const issues = validateClassSpec(spec);
  if (issues.length > 0) {
    return {
      success: false,
      data: { issues },
      error: `class spec rejected: ${issues.map((i) => `${i.field}: ${i.message}`).join("; ")}`,
    };
  }

  const rendered = renderClass(spec);
  // The rendered text is checked as well as the spec. A spec can be clean and
  // the output still break a UHT rule if the template is wrong, and finding
  // that out from a compile is the slow way.
  const headerIssues = checkHeaderRules(rendered.header);
  if (headerIssues.length > 0) {
    return {
      success: false,
      data: { issues: headerIssues, header: rendered.header },
      error: `generated header violates UHT rules: ${headerIssues.map((i) => `${i.field}: ${i.message}`).join("; ")}`,
    };
  }

  const moduleRoot = options.module_root;
  if (!existsSync(moduleRoot)) {
    return { success: false, data: {}, error: `module directory not found: ${moduleRoot}` };
  }

  const subdir = (spec.subdir ?? "").replace(/\\/g, "/").replace(/\/+$/, "");
  const split = existsSync(join(moduleRoot, "Public")) && existsSync(join(moduleRoot, "Private"));
  const headerDir = split ? join(moduleRoot, "Public", subdir) : join(moduleRoot, subdir);
  const sourceDir = split ? join(moduleRoot, "Private", subdir) : join(moduleRoot, subdir);
  const headerPath = join(headerDir, rendered.header_file);
  const sourcePath = join(sourceDir, rendered.source_file);

  const existing = [headerPath, sourcePath].filter((p) => existsSync(p));
  if (existing.length > 0 && !options.overwrite) {
    return {
      success: false,
      data: { existing_files: existing },
      error: `refusing to overwrite ${existing.join(", ")}; pass overwrite to replace`,
    };
  }

  const result = {
    class_name: rendered.class_name,
    base_class: rendered.base_class,
    module: spec.module,
    header: headerPath,
    source: sourcePath,
    required_modules: rendered.required_modules,
    overwrote: existing,
    next_step:
      "Add required_modules to <Module>.Build.cs if missing, then build. " +
      "New classes need an editor restart to load.",
  };
  if (options.dry_run) return { success: true, data: { ...result, dry_run: true } };

  const created: string[] = [];
  try {
    for (const [path, text] of [[headerPath, rendered.header], [sourcePath, rendered.source]] as const) {
      mkdirSync(dirname(path), { recursive: true });
      const isNew = !existsSync(path);
      writeFileSync(path, text, { encoding: "utf-8" });
      if (isNew) created.push(path);
    }
  } catch (error) {
    for (const path of created) {
      try { rmSync(path); } catch { /* best effort rollback */ }
    }
    return {
      success: false,
      data: { rolled_back: created },
      error: `write failed, ${created.length} partial file(s) removed: ${(error as Error).message}`,
    };
  }
  return { success: true, data: result };
}

// ---------------------------------------------------------------- Build.cs

export type DependencyKind = "Public" | "Private";

export interface BuildCsEditResult {
  text: string;
  changed: boolean;
  added: string[];
  removed: string[];
  already_present: string[];
  not_present: string[];
  error?: string;
}

interface ArrayBlock {
  /** Index just after the opening brace. */
  innerStart: number;
  /** Index of the closing brace. */
  innerEnd: number;
  inner: string;
}

function findDependencyArray(text: string, kind: DependencyKind): ArrayBlock | null {
  const head = new RegExp(`${kind}DependencyModuleNames\\s*\\.\\s*AddRange\\s*\\(\\s*new\\s+string\\s*\\[\\s*\\]\\s*\\{`);
  const match = head.exec(text);
  if (!match) return null;
  const innerStart = match.index + match[0].length;
  let depth = 1;
  for (let i = innerStart; i < text.length; i++) {
    if (text[i] === "{") depth++;
    else if (text[i] === "}") {
      depth--;
      if (depth === 0) return { innerStart, innerEnd: i, inner: text.slice(innerStart, i) };
    }
  }
  return null;
}

function quotedEntries(inner: string): Array<{ name: string; start: number; end: number }> {
  const out: Array<{ name: string; start: number; end: number }> = [];
  const re = /"([^"]*)"/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(inner)) !== null) {
    out.push({ name: m[1], start: m.index, end: m.index + m[0].length });
  }
  return out;
}

function lineIndent(text: string, index: number): string {
  const start = text.lastIndexOf("\n", index) + 1;
  const match = /^[ \t]*/.exec(text.slice(start, index));
  return match ? match[0] : "";
}

/**
 * Add or remove module dependencies in a Build.cs, in place, idempotently.
 *
 * Edits the existing array surgically rather than reformatting it, so comments,
 * indentation and trailing commas inside the array survive untouched. Adding a
 * module that is already listed (through AddRange or a bare .Add) is a no-op,
 * and removing one that is absent is a no-op, so the same call can be replayed.
 */
export function editBuildCsDependencies(
  text: string,
  options: { kind: DependencyKind; add?: string[]; remove?: string[] }
): BuildCsEditResult {
  const result: BuildCsEditResult = {
    text, changed: false, added: [], removed: [], already_present: [], not_present: [],
  };
  const add = options.add ?? [];
  const remove = options.remove ?? [];

  const bad = [...add, ...remove].find((m) => !IDENTIFIER.test(m));
  if (bad !== undefined) {
    return { ...result, error: `"${bad}" is not a valid module name` };
  }

  for (const name of remove) {
    const block = findDependencyArray(result.text, options.kind);
    if (!block) {
      // Nothing to remove from. Not an error: the desired state already holds.
      result.not_present.push(name);
      continue;
    }
    const entries = quotedEntries(block.inner);
    const at = entries.findIndex((e) => e.name === name);
    if (at < 0) { result.not_present.push(name); continue; }

    let cutStart = entries[at].start;
    let cutEnd = entries[at].end;
    if (at + 1 < entries.length) cutEnd = entries[at + 1].start;       // takes the following comma
    else if (at > 0) cutStart = entries[at - 1].end;                    // takes the preceding comma
    // Cutting entry-start to next-entry-start (or previous-entry-end to
    // entry-end for the last one) takes exactly one separator and one line's
    // worth of whitespace with it, so no blank line is left behind.
    const inner = block.inner.slice(0, cutStart) + block.inner.slice(cutEnd);
    result.text = result.text.slice(0, block.innerStart) + inner + result.text.slice(block.innerEnd);
    result.removed.push(name);
    result.changed = true;
  }

  for (const name of add) {
    const block = findDependencyArray(result.text, options.kind);
    if (!block) {
      return {
        ...result,
        error:
          `no ${options.kind}DependencyModuleNames.AddRange(new string[] { ... }) block found; ` +
          "refusing to guess where the list lives",
      };
    }
    const entries = quotedEntries(block.inner);
    const bareAdd = new RegExp(`${options.kind}DependencyModuleNames\\s*\\.\\s*Add\\s*\\(\\s*"${name}"`);
    if (entries.some((e) => e.name === name) || bareAdd.test(result.text)) {
      result.already_present.push(name);
      continue;
    }

    let inner: string;
    if (entries.length === 0) {
      inner = block.inner.trim() === "" ? ` "${name}" ` : `${block.inner} "${name}" `;
    } else {
      const last = entries[entries.length - 1];
      const tail = block.inner.slice(last.end);
      const hasTrailingComma = /^\s*,/.test(tail);
      const insertAt = hasTrailingComma ? last.end + tail.indexOf(",") + 1 : last.end;
      const multiline = block.inner.includes("\n");
      const indent = multiline ? lineIndent(block.inner, last.start) : "";
      const insertion = multiline
        ? (hasTrailingComma ? `\n${indent}"${name}",` : `,\n${indent}"${name}"`)
        : (hasTrailingComma ? ` "${name}",` : `, "${name}"`);
      inner = block.inner.slice(0, insertAt) + insertion + block.inner.slice(insertAt);
    }
    result.text = result.text.slice(0, block.innerStart) + inner + result.text.slice(block.innerEnd);
    result.added.push(name);
    result.changed = true;
  }

  return result;
}

/** Module names currently listed for one dependency kind. */
export function readBuildCsDependencies(text: string, kind: DependencyKind): string[] {
  const block = findDependencyArray(text, kind);
  const fromArray = block ? quotedEntries(block.inner).map((e) => e.name) : [];
  const bare = [...text.matchAll(new RegExp(`${kind}DependencyModuleNames\\s*\\.\\s*Add\\s*\\(\\s*"([^"]+)"`, "g"))]
    .map((m) => m[1]);
  return [...new Set([...fromArray, ...bare])];
}

// ---------------------------------------------------------------- includes

export interface IncludeEditResult {
  text: string;
  changed: boolean;
  added: string[];
  removed: string[];
  already_present: string[];
  not_present: string[];
  /** True when a .generated.h had to be moved back to last position. */
  generated_reordered: boolean;
}

const INCLUDE_LINE = /^[ \t]*#include[ \t]+["<]([^">]+)[">].*$/;

/**
 * Add or remove #include lines idempotently, keeping the .generated.h last.
 *
 * The generated header is not an ordinary include: UHT requires it to be the
 * final one in the file, so every insertion goes above it and a stray include
 * below it is repaired by moving the generated line back to the end.
 */
export function editIncludes(
  text: string,
  options: { add?: string[]; remove?: string[] }
): IncludeEditResult {
  const add = options.add ?? [];
  const remove = options.remove ?? [];
  const result: IncludeEditResult = {
    text, changed: false, added: [], removed: [], already_present: [], not_present: [],
    generated_reordered: false,
  };

  let lines = text.split("\n");
  const pathOf = (line: string): string | null => {
    const m = INCLUDE_LINE.exec(line);
    return m ? m[1] : null;
  };

  for (const path of remove) {
    const at = lines.findIndex((l) => pathOf(l) === path);
    if (at < 0) { result.not_present.push(path); continue; }
    lines.splice(at, 1);
    result.removed.push(path);
    result.changed = true;
  }

  for (const path of add) {
    if (lines.some((l) => pathOf(l) === path)) { result.already_present.push(path); continue; }
    const generatedAt = lines.findIndex((l) => (pathOf(l) ?? "").endsWith(".generated.h"));
    let insertAt: number;
    if (generatedAt >= 0) {
      insertAt = generatedAt;
    } else {
      let lastInclude = -1;
      for (let i = 0; i < lines.length; i++) if (pathOf(lines[i]) !== null) lastInclude = i;
      if (lastInclude >= 0) insertAt = lastInclude + 1;
      else {
        const pragma = lines.findIndex((l) => /^[ \t]*#pragma\s+once/.test(l));
        insertAt = pragma >= 0 ? pragma + 1 : 0;
        if (lines[insertAt] !== undefined && lines[insertAt].trim() !== "") {
          lines.splice(insertAt, 0, "");
          insertAt += 1;
        } else if (pragma >= 0) {
          insertAt += 1;
        }
      }
    }
    lines.splice(insertAt, 0, `#include "${path}"`);
    result.added.push(path);
    result.changed = true;
  }

  // Convergence: whatever the file looked like on the way in, the generated
  // header ends up last.
  const generatedAt = lines.findIndex((l) => (pathOf(l) ?? "").endsWith(".generated.h"));
  if (generatedAt >= 0) {
    let lastInclude = generatedAt;
    for (let i = generatedAt + 1; i < lines.length; i++) if (pathOf(lines[i]) !== null) lastInclude = i;
    if (lastInclude > generatedAt) {
      const [generated] = lines.splice(generatedAt, 1);
      lines.splice(lastInclude, 0, generated);
      result.generated_reordered = true;
      result.changed = true;
    }
  }

  result.text = lines.join("\n");
  return result;
}

// ---------------------------------------------------------------- diagnostics

// Order matters: the file(line) forms must be tried before the bare forms, or
// a Windows path's drive-letter colon gets mistaken for the "LINK : " separator.
const DIAG_PATTERNS: Array<{ re: RegExp; kind: "msvc" | "uht" | "bare-file" | "bare" | "ubt" }> = [
  // D:\Proj\Foo.cpp(120): error C2065: 'Bar': undeclared identifier
  // D:\Proj\Foo.h(12,5): warning C4996: '...'
  { kind: "msvc", re: /^\s*(?<file>.+?)\((?<line>\d+)(?:,(?<column>\d+))?\)\s*:\s*(?<severity>fatal error|error|warning|note)\s+(?<code>[A-Z]+\d+)\s*:\s*(?<message>.*)$/i },
  // UHT: D:\Proj\Foo.h(9): Error: Missing '#include "Foo.generated.h"'
  { kind: "uht", re: /^\s*(?<file>.+?)\((?<line>\d+)(?:,(?<column>\d+))?\)\s*:\s*(?<severity>Error|Warning)\s*:\s*(?<message>.*)$/ },
  // LINK : fatal error LNK1104: cannot open file '...dll'   /   cl : Command line warning D9002
  { kind: "bare-file", re: /^\s*(?<file>[A-Za-z0-9_.\-]+)\s+:\s*(?:Command line\s+)?(?<severity>fatal error|error|warning)\s+(?<code>[A-Z]+\d+)\s*:\s*(?<message>.*)$/i },
  // warning C4996: 'UEditorEngine::RequestPlaySession': Use the overload...
  { kind: "bare", re: /^\s*(?<severity>fatal error|error|warning|note)\s+(?<code>[A-Z]+\d+)\s*:\s*(?<message>.*)$/i },
  // UBT's own failures: ERROR: Failed to produce item: ...
  { kind: "ubt", re: /^\s*(?<severity>ERROR|WARNING)\s*:\s*(?<message>.*)$/ },
];

function normaliseSeverity(raw: string): Severity {
  const lower = raw.toLowerCase();
  if (lower === "note") return "note";
  if (lower.startsWith("warn")) return "warning";
  return "error";
}

/** Parse UBT / MSVC / UHT console output into structured diagnostics. */
export function parseBuildOutput(output: string, exitCode: number | null): BuildResult {
  const lines = output.split(/\r?\n/);
  const diagnostics: Diagnostic[] = [];
  const seen = new Set<string>();

  for (const raw of lines) {
    if (!raw.trim()) continue;
    for (const { re, kind } of DIAG_PATTERNS) {
      const m = re.exec(raw);
      if (!m || !m.groups) continue;
      const g = m.groups;
      // "Total build time" style lines that happen to contain a colon must not
      // reach here; every pattern demands a severity word, so they do not.
      const diagnostic: Diagnostic = {
        file: g.file ? g.file.trim() : null,
        line: g.line ? Number(g.line) : null,
        column: g.column ? Number(g.column) : null,
        code: g.code ?? null,
        message: (g.message ?? "").trim(),
        severity: normaliseSeverity(g.severity ?? "error"),
        raw: raw.trimEnd(),
      };
      if (kind === "ubt" && !diagnostic.message) break;
      const key = `${diagnostic.file}|${diagnostic.line}|${diagnostic.code}|${diagnostic.message}`;
      if (!seen.has(key)) {
        seen.add(key);
        diagnostics.push(diagnostic);
      }
      break;
    }
  }

  const errors = diagnostics.filter((d) => d.severity === "error");
  const warnings = diagnostics.filter((d) => d.severity === "warning");
  const notes = diagnostics.filter((d) => d.severity === "note");
  return {
    succeeded: exitCode === 0 && errors.length === 0,
    exit_code: exitCode,
    error_count: errors.length,
    warning_count: warnings.length,
    errors,
    warnings,
    notes,
    hints: repairHints(diagnostics),
    output_tail: lines.filter((l) => l.trim()).slice(-20),
  };
}

// ---------------------------------------------------------------- repair hints

function includeFromMessage(message: string): string | null {
  const m = /Cannot open include file:\s*'([^']+)'/i.exec(message)
    ?? /#include\s+["<]([^">]+)[">]/.exec(message);
  return m ? m[1] : null;
}

/**
 * Turn diagnostics into actionable repairs.
 *
 * Every hint names the edit to make, not the symptom. Hints are deduplicated by
 * id plus subject so a header included from thirty translation units produces
 * one instruction, not thirty.
 */
export function repairHints(diagnostics: Diagnostic[]): RepairHint[] {
  const hints: RepairHint[] = [];
  const seen = new Set<string>();
  const push = (key: string, hint: RepairHint): void => {
    if (seen.has(key)) return;
    seen.add(key);
    hints.push(hint);
  };

  for (const d of diagnostics) {
    const message = d.message;
    const where = d.file ? `${d.file}${d.line ? `(${d.line})` : ""}` : "the build";

    // 1. Missing or misplaced .generated.h include.
    if (/\.generated\.h/i.test(message)) {
      const last = /last header|must be the last|Expected .*\.generated\.h to be/i.test(message);
      push(`generated:${d.file}`, {
        id: last ? "generated_include_not_last" : "generated_include_missing",
        diagnostic: d,
        problem: last
          ? `${where}: the .generated.h include is not the last include in the header`
          : `${where}: the header declares a UCLASS but does not include its .generated.h`,
        fix: last
          ? 'Move #include "<Name>.generated.h" below every other #include. UHT reads the include list positionally and fails on anything after it.'
          : 'Add #include "<Name>.generated.h" as the LAST include in the header, after CoreMinimal.h and the parent header.',
      });
      continue;
    }

    // 2. GENERATED_BODY missing from a reflected type.
    if (/GENERATED_BODY|GENERATED_UCLASS_BODY|GENERATED_USTRUCT_BODY/i.test(message)) {
      push(`generated_body:${d.file}`, {
        id: "generated_body_missing",
        diagnostic: d,
        problem: `${where}: the UCLASS/USTRUCT body macro is missing or misplaced`,
        fix: "Put GENERATED_BODY() on the first line inside the type body, before any access specifier. UCLASS()/USTRUCT() must sit immediately above the class keyword with nothing between them.",
      });
      continue;
    }

    // 3. Missing module dependency, seen as an unresolvable include or an
    //    unresolved external at link.
    const include = includeFromMessage(message);
    if (d.code === "C1083" && include) {
      const modules = INCLUDE_TO_MODULE.find(([pattern]) => pattern.test(include))?.[1];
      push(`module:${include}`, {
        id: "module_dependency_missing",
        diagnostic: d,
        problem: `${where}: '${include}' is not on the include path`,
        fix: modules
          ? `Add ${modules.join(", ")} to PublicDependencyModuleNames (or Private) in <Module>.Build.cs, then rebuild.`
          : `Add the module that owns '${include}' to PublicDependencyModuleNames in <Module>.Build.cs. If the header is in your own module, check the Public/Private split: only Public headers are visible to other modules.`,
      });
      continue;
    }
    if (d.code === "LNK2019" || d.code === "LNK2001") {
      push("unresolved_external", {
        id: "module_dependency_missing",
        diagnostic: d,
        problem: `${where}: unresolved external symbol; the declaration was found but nothing links it`,
        fix: "The owning module compiles but is not linked: add it to PublicDependencyModuleNames (or Private) in <Module>.Build.cs. If the symbol is yours, check the class carries the <MODULE>_API export macro.",
      });
      continue;
    }

    // 4. A UE5-only API that AGENTS.md forbids on this 4.27 target.
    const forbidden = findForbiddenUe5Api(message);
    if (forbidden.length > 0) {
      for (const hit of forbidden) {
        push(`ue5:${hit.token}`, {
          id: "ue5_only_api",
          diagnostic: d,
          problem: `${where}: ${hit.token} does not exist in UE4.27`,
          fix: `Replace ${hit.token} with ${hit.replacement}.`,
        });
      }
      continue;
    }

    // 5. The editor holds the DLL. Compile diagnostics above are still valid.
    if (d.code === "LNK1104") {
      push("locked_dll", {
        id: "output_locked",
        diagnostic: d,
        problem: `${where}: the linker cannot open its output file`,
        fix: "The editor is almost certainly running and holding that DLL. Close the editor and rebuild. Compile errors reported before this point are real and worth fixing first.",
      });
      continue;
    }

    // 6. Deprecation. Common enough on 4.27 editor code to be worth naming.
    if (d.code === "C4996") {
      push(`deprecated:${message.slice(0, 60)}`, {
        id: "deprecated_api",
        diagnostic: d,
        problem: `${where}: deprecated API`,
        fix: `Follow the deprecation text exactly: ${message}`,
      });
    }
  }

  return hints;
}

// ---------------------------------------------------------------- UBT

export interface UbtOptions {
  /** Absolute path to the .uproject. */
  project_file: string;
  /** Build target, e.g. MyProjectEditor. */
  target: string;
  platform?: string;
  configuration?: string;
  /** Defaults to UE_ENGINE_ROOT, then D:/UE/UE_4.27. */
  engine_root?: string;
  extra_args?: string[];
  timeout_ms?: number;
}

/** The Build.bat UBT is invoked through, resolved but not executed. */
export function resolveUbtBatch(engineRoot?: string): string {
  const root = engineRoot || process.env.UE_ENGINE_ROOT || "D:/UE/UE_4.27";
  return join(root, "Engine", "Build", "BatchFiles", "Build.bat");
}

/**
 * Run UnrealBuildTool and return parsed diagnostics.
 *
 * Invoked through cmd.exe on purpose. Node refuses to spawn a .bat directly
 * (EINVAL) since the batch-argument-injection fix, and UBT's entry point on
 * Windows is Build.bat. Same approach as buildTarget in Scripts/bridge-install.mjs;
 * do not add a second way of doing this.
 */
export function runUbt(options: UbtOptions): Envelope<BuildResult | Record<string, never>> {
  const batch = resolveUbtBatch(options.engine_root);
  if (!existsSync(batch)) {
    return { success: false, data: {}, error: `UnrealBuildTool not found at ${batch}. Set UE_ENGINE_ROOT` };
  }
  if (!existsSync(options.project_file)) {
    return { success: false, data: {}, error: `project file not found: ${options.project_file}` };
  }

  const args = [
    "/c", batch,
    options.target,
    options.platform ?? "Win64",
    options.configuration ?? "Development",
    options.project_file,
    "-WaitMutex",
    "-NoHotReload",
    ...(options.extra_args ?? []),
  ];
  const run = spawnSync("cmd.exe", args, {
    encoding: "utf-8",
    timeout: options.timeout_ms ?? 30 * 60 * 1000,
    maxBuffer: 64 * 1024 * 1024,
    windowsHide: true,
  });
  if (run.error) {
    return { success: false, data: {}, error: `failed to run UBT: ${run.error.message}` };
  }
  const output = `${run.stdout ?? ""}\n${run.stderr ?? ""}`;
  const parsed = parseBuildOutput(output, run.status);
  return { success: parsed.succeeded, data: parsed, error: parsed.succeeded ? undefined : "build failed" };
}

// ---------------------------------------------------------------- UHT rules

/**
 * The UHT rules a reflected header must satisfy, checked as text.
 *
 * This is not a UHT replacement: it catches the four failures that account for
 * nearly every "my new class will not compile" report, before UBT is ever
 * started, and it runs with the editor closed and the engine absent. A header
 * that passes here can still fail real UHT; a header that fails here always
 * fails real UHT.
 */
export function checkHeaderRules(text: string): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const lines = text.split("\n");
  const includes = lines
    .map((l, i) => ({ path: INCLUDE_LINE.exec(l)?.[1], index: i }))
    .filter((e): e is { path: string; index: number } => e.path !== undefined);
  const generated = includes.filter((e) => e.path.endsWith(".generated.h"));
  const reflected = /^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(/m.test(text);

  if (reflected && generated.length === 0) {
    issues.push({ field: "includes", message: 'a reflected type needs #include "<Name>.generated.h"' });
  }
  if (generated.length > 1) {
    issues.push({ field: "includes", message: "more than one .generated.h include" });
  }
  if (generated.length === 1 && includes[includes.length - 1].index !== generated[0].index) {
    issues.push({ field: "includes", message: "the .generated.h include must be the last include in the file" });
  }
  if (reflected && !/^\s*#pragma\s+once/m.test(text)) {
    issues.push({ field: "header", message: "missing #pragma once" });
  }

  // UCLASS() must sit immediately above the class keyword, and GENERATED_BODY()
  // must be the first thing inside the body.
  for (const [index, line] of lines.entries()) {
    if (!/^\s*UCLASS\s*\(/.test(line)) continue;
    const next = (lines[index + 1] ?? "").trim();
    if (!/^class\b/.test(next)) {
      issues.push({ field: `line ${index + 2}`, message: "UCLASS() must be directly above the class declaration" });
      continue;
    }
    const open = lines.findIndex((l, i) => i > index && /^\s*\{/.test(l));
    const body = open >= 0 ? (lines[open + 1] ?? "").trim() : "";
    if (!/^GENERATED_BODY\s*\(\s*\)|^GENERATED_UCLASS_BODY\s*\(\s*\)/.test(body)) {
      issues.push({
        field: `line ${open + 2}`,
        message: "GENERATED_BODY() must be the first line inside the class body",
      });
    }
    if (!/\b[A-Z0-9_]+_API\b/.test(next)) {
      issues.push({
        field: `line ${index + 2}`,
        message: "the class has no <MODULE>_API export macro; other modules will fail to link against it",
      });
    }
  }

  for (const hit of findForbiddenUe5Api(text)) {
    issues.push({ field: "header", message: `${hit.token} does not exist in UE4.27; use ${hit.replacement}` });
  }
  return issues;
}
