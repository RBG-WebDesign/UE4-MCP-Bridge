# Headless C++ authoring

How the bridge writes, edits, and compiles UE4.27 C++ with the editor closed.

Implementation: `mcp-server/src/cpp-authoring.ts`.
Tests: `mcp-server/tests/cpp-authoring.test.ts` (no editor, no engine, no compile).

## Why headless

A new UCLASS cannot be loaded by a running editor. The editor has to restart
before it sees the class, so the entire authoring loop - generate, edit, run
UHT, build, read diagnostics, repair - is work that happens while the editor is
closed. Routing it through the editor would add a dependency that buys nothing
and breaks the one case that matters.

Everything in this module therefore touches only the filesystem and
UnrealBuildTool. It is the same reason `engine_source_*` is server-local.

## Operations

| Operation | Input | Output |
|---|---|---|
| `validateClassSpec` | class spec | list of issues, empty when the spec is writable |
| `renderClass` | class spec | header text, source text, required Build.cs modules |
| `writeClass` | class spec, module root | `{success, data, error}`; both files or neither |
| `checkHeaderRules` | header text | UHT rule violations, found without running UHT |
| `editBuildCsDependencies` | Build.cs text, add/remove lists | edited text, what changed |
| `readBuildCsDependencies` | Build.cs text | the modules currently listed |
| `editIncludes` | file text, add/remove lists | edited text, `.generated.h` forced last |
| `parseBuildOutput` | UBT console text, exit code | structured diagnostics plus repair hints |
| `repairHints` | diagnostics | the edit to make, not a restatement of the error |
| `runUbt` | project file, target | `{success, data, error}` with parsed diagnostics |

Pure text functions return plain values. The two operations with side effects,
`writeClass` and `runUbt`, return the standard `{success, data, error}` envelope.

## What makes an edit safe

Four properties, all of them tested.

**Idempotent.** Applying the same edit twice produces the same bytes as applying
it once. Adding a module already in `PublicDependencyModuleNames` reports
`already_present` and changes nothing. Adding an `#include` that is already
there changes nothing. This is what lets a caller re-run a recipe without
diffing first.

**Convergent.** The call states the desired end state, not a patch to apply.
`editIncludes` moves a misplaced `.generated.h` back to last even when the call
asked for nothing else, because "the generated header is last" is part of the
end state rather than an operation someone requested.

**Failure-atomic.** `writeClass` validates the spec, renders both files, checks
the rendered header against the UHT rules, and only then writes. If the second
write fails, any file the call created is deleted, so a half-written class never
reaches UHT. `editBuildCsDependencies` returns the original text unchanged when
it refuses.

**It refuses rather than guesses.** Explicitly:

- Overwriting an existing `.h` or `.cpp` without `overwrite: true`.
- A class name that already carries a UE prefix (`ADoorActor`): the prefix is
  derived from the parent, and accepting both spellings would produce
  `AADoorActor`.
- An unknown parent class with no `parent_include` and `parent_prefix`.
- A `subdir` that is absolute or contains `..`.
- Any spec fragment carrying `;`, a newline, or a comment, which would let a
  type or parameter list inject statements into the generated file.
- Duplicate member names.
- Any UE5-only API from the AGENTS.md table, anywhere in the spec.
- A module name that is not a bare identifier.
- A `Build.cs` with no `<Kind>DependencyModuleNames.AddRange(new string[] { ... })`
  block. There is no safe place to invent one, so the call reports why and stops.

## How a generated class is validated before it is written

Three gates, in order. Nothing is written until all three pass.

1. **Spec validation.** Identifiers, parent resolution, path containment,
   fragment sanity, duplicate members, and a whole-spec scan for forbidden UE5
   APIs. The scan runs over the serialised spec, so a UE5 type smuggled in
   through a property type or a function body is caught even though every field
   is individually well formed.
2. **Rendered-header validation.** `checkHeaderRules` runs on the text that is
   about to be written. It catches template bugs, not just caller bugs, and
   finding those out from a compile is the slow way.
3. **Filesystem check.** Both target paths are resolved and tested for existence
   before either is opened.

`checkHeaderRules` is not a UHT replacement. It covers the failures that account
for nearly every "my new class will not compile" report and it runs with the
engine absent. A header that fails it always fails real UHT; a header that
passes it can still fail real UHT.

## UE4.27 constraints being enforced

These are properties of UnrealHeaderTool, not style preferences. UHT parses the
header as text before the compiler ever sees it, and it is positional.

**`.generated.h` is included last.** UHT generates that header from the
declarations that precede it and rejects any `#include` after it. The generator
always emits it last; `editIncludes` inserts above it and moves it back to the
end if something else lands below; `checkHeaderRules` reports both a missing one
on a reflected type and a misplaced one.

**`#pragma once` at the top.** A reflected header included twice produces
duplicate generated bodies.

**`UCLASS()` sits immediately above `class`.** Nothing may come between the
macro and the declaration, not even a comment line. Same for `USTRUCT()`,
`UENUM()`, and `UINTERFACE()`.

**`GENERATED_BODY()` is the first line inside the class body**, before any
access specifier. It expands to a block that sets its own access, which is why
it goes first and why the generator emits `public:` after it rather than before.
`GENERATED_UCLASS_BODY()` is the older form and requires a
`const FObjectInitializer&` constructor; the generator does not emit it.

**`<MODULE>_API` on the class.** Without the export macro the class compiles but
nothing outside its module can link against it, which surfaces later as an
`LNK2019` that looks nothing like its cause. The macro name is the module name
uppercased.

**`UPROPERTY` and `UFUNCTION` are on the line directly above the member.**
`UPROPERTY` cannot annotate a local, a static, or a `TSharedPtr`. `UFUNCTION`
must appear on the declaration in the header, never on the definition in the
`.cpp`.

**The `.cpp` includes its own header first**, at the path relative to the
module's include root, which is why `subdir` changes the include and not just
the file location.

**No UE5 APIs.** The forbidden list mirrors the AGENTS.md table:
`EnhancedInputComponent`, `EnhancedInputSubsystem`, `UE::Tasks`, MassAI,
SmartObjects, StateTree, AnimNext, `LevelEditorSubsystem`,
`EditorUtilitySubsystem`, `EditorPlaySessionSubsystem`. Also `UCameraShake` and
`PlayCameraShake`: this 4.27.2 build carries the UE5-transitional
`UCameraShakeBase` with `StartCameraShake()`, so the older names are the ones
that do not exist here. Matching is word-boundary anchored, so `UCameraShakeBase`
is not mistaken for the removed `UCameraShake`.

## Build.cs editing

The array is edited surgically rather than reparsed and reprinted. Comments,
indentation, and trailing-comma style inside the array survive, because a
dependency edit that reformats the file produces a diff nobody can review.

Multi-line and single-line array forms are both handled and each keeps its own
shape. A bare `PublicDependencyModuleNames.Add("X")` counts as present for
idempotency, but additions always go into the `AddRange` array.

Removal takes exactly one separator with the entry: the following comma for any
entry but the last, the preceding comma for the last. Add-then-remove restores
the original bytes, which the tests assert directly.

## Running UBT

`runUbt` invokes `Engine\Build\BatchFiles\Build.bat` **through `cmd.exe`**.

Node refuses to spawn a `.bat` directly since the batch-argument-injection fix
and fails with `EINVAL`. UBT's Windows entry point is a `.bat`. The working
reference is `buildTarget` in `Scripts/bridge-install.mjs`; this module uses the
same approach on purpose. Do not add a second way of doing it.

Engine root resolution: explicit `engine_root`, then `UE_ENGINE_ROOT`, then
`D:/UE/UE_4.27`. The batch file's absence is reported before anything is spawned.

Building the editor target while the editor is running compiles normally and
then fails at link with `LNK1104` on the locked DLL. The compile diagnostics
from that run are still valid and are the reason to do it; the hint says so.

## Diagnostics

Every line is matched against an ordered pattern list. Order matters: the
`file(line)` forms are tried before the bare `LINK : ` form, or a Windows path's
drive-letter colon gets read as the pseudo-file separator.

Handled forms:

```
D:\Proj\Foo.cpp(52): error C2065: 'X': undeclared identifier
D:\Proj\Foo.cpp(53,17): error C2039: 'Foo': is not a member of 'ADoorActor'
D:\Proj\Foo.h(9): Error: Missing '#include "Foo.generated.h"'      (UHT, no code)
DoorActor.cpp.obj : error LNK2019: unresolved external symbol ...
LINK : fatal error LNK1104: cannot open file '...UE4Editor-MyGame.dll'
warning C4996: 'UEditorEngine::RequestPlaySession': Use the overload ...
ERROR: UBT ERROR: Failed to produce item: ...
```

Each becomes `{file, line, column, code, message, severity, raw}`, with `null`
where the line carries no such field. Duplicates are collapsed. Progress output
never matches, because every pattern requires a severity word.

A build is successful only when the exit code is zero **and** no error
diagnostic was parsed.

## Repair hints

A hint names the edit to make. It is not a restatement of the error.

| id | Triggered by | Fix it gives |
|---|---|---|
| `generated_include_missing` | UHT message naming `.generated.h` | add it as the last include |
| `generated_include_not_last` | UHT "last header included" | move it below every other include |
| `generated_body_missing` | any message naming `GENERATED_BODY` | first line inside the body, before any access specifier |
| `module_dependency_missing` | `C1083` on a known engine header, or `LNK2019`/`LNK2001` | the specific modules to add to `Build.cs`, from a header-to-module table |
| `ue5_only_api` | a forbidden UE5 token inside any diagnostic message | the 4.27 replacement |
| `output_locked` | `LNK1104` | the editor is holding the DLL; earlier compile errors are still real |
| `deprecated_api` | `C4996` | the compiler's own deprecation instruction, carried through verbatim |

Hints are deduplicated by cause, so a header included from thirty translation
units produces one instruction rather than thirty.

## Not covered

- No MCP tool registration. This wave ships the library and its tests; wiring it
  into the tool catalog touches integration-owned files.
- No real UHT invocation. `checkHeaderRules` is a text check, not UHT.
- `runUbt` has no unit test that spawns a build. The cmd.exe invocation is
  copied from a known-working reference and the parser is tested against real
  captured output, but the two have not been proven end to end in this lane.
- No USTRUCT, UENUM, or UINTERFACE generation. Only UCLASS.
- No module creation: a new module needs a `.Build.cs`, a `.uproject` entry, and
  `IMPLEMENT_MODULE`, and none of that is generated here.
