# PuerTS Unreal Engine Manual

## Complete page-by-page technical digest

**Source:** Official PuerTS English Unreal Engine documentation  
**Documentation root:** <https://puerts.github.io/en/docs/puerts/unreal/install/>  
**Access date:** 2026-07-31  
**Coverage:** 13 English Unreal Engine manual pages

> This document is an original technical digest. It does not reproduce the official manual verbatim. It summarizes every English Unreal manual page listed in the official manual structure. Use the source links for the original wording, complete examples, and images.

## Scope

This digest covers these pages:

1. [PuerTS Unreal Engine User Manual](https://puerts.github.io/en/docs/puerts/unreal/manual/)
2. [Install PuerTS](https://puerts.github.io/en/docs/puerts/unreal/install/)
3. [Setup Development Environment](https://puerts.github.io/en/docs/puerts/unreal/dev_environment/)
4. [Getting Started](https://puerts.github.io/en/docs/puerts/unreal/getting_started/)
5. [Starting a New JavaScript Virtual Machine](https://puerts.github.io/en/docs/puerts/unreal/start_a_virtual_machine/)
6. [Debugging](https://puerts.github.io/en/docs/puerts/unreal/vscode_debug/)
7. [Interacting With C++ From TypeScript](https://puerts.github.io/en/docs/puerts/unreal/script_call_uclass/)
8. [Template-Based Static Binding](https://puerts.github.io/en/docs/puerts/unreal/template_binding/)
9. [Engine or Pure C++ Calling TypeScript](https://puerts.github.io/en/docs/puerts/unreal/engine_call_script/)
10. [Automatic Binding Mode](https://puerts.github.io/en/docs/puerts/unreal/uclass_extends/)
11. [Blueprint Mixin](https://puerts.github.io/en/docs/puerts/unreal/mixin/)
12. [FAQ](https://puerts.github.io/en/docs/puerts/unreal/faq/)
13. [Official PuerTS Demos](https://puerts.github.io/en/docs/puerts/unreal/demos/)

This digest excludes the Unity manual, Chinese translations, repository issues, and third-party guides.

---

# 1. PuerTS Unreal Engine User Manual

Source: <https://puerts.github.io/en/docs/puerts/unreal/manual/>

## Core purpose

PuerTS puts a JavaScript virtual machine inside Unreal Engine. It lets TypeScript or JavaScript call Unreal Engine APIs. It also lets Unreal Engine and C++ call TypeScript.

PuerTS does not replace Unreal Engine. It defines an interaction layer between TypeScript and the engine.

The manual presents two main API paths:

1. **Reflected Unreal APIs**
   - PuerTS imports reflected APIs by default.
   - This includes APIs exposed through `UCLASS`, `UFUNCTION`, `UPROPERTY`, `USTRUCT`, and `UENUM`.
   - Blueprint-accessible APIs are generally available from TypeScript.

2. **Non-reflected C++ APIs**
   - PuerTS cannot discover ordinary C++ APIs through Unreal reflection.
   - Developers can expose these APIs through reflection wrappers.
   - Developers can also use template-based static binding.

## Main development model

A developer should first determine how a task works in C++ or Blueprint. The TypeScript implementation then calls the same Unreal APIs.

The practical model is:

```text
TypeScript or JavaScript
        |
        v
PuerTS interop
        |
        +--> Unreal reflection
        +--> Static C++ bindings
        +--> Delegates and JsObject bridges
        |
        v
Unreal Engine
```

## Manual structure

The manual connects installation, TypeScript setup, virtual-machine startup, API interaction, debugging, automatic binding, mixins, demos, and troubleshooting.

## Important boundary

A generated declaration does not make a private C++ API available. PuerTS still needs a reflected API or a static binding.

---

# 2. Install PuerTS

Source: <https://puerts.github.io/en/docs/puerts/unreal/install/>

## Source installation

The source installation flow is:

1. Clone the PuerTS repository.
2. Copy the `unreal/Puerts` directory into the project `Plugins` directory.
3. Install a JavaScript backend.
4. Compile the Unreal project.

Expected project layout:

```text
YourProject/
  Plugins/
    Puerts/
```

## Release-package installation

The release flow is:

1. Open the PuerTS release page.
2. Select a package that matches the Unreal Engine version.
3. Extract the package into `YourProject/Plugins`.

Unreal release names use an `Unreal_v...` prefix.

## JavaScript backends

The manual describes three backend choices.

### V8

Use V8 for a standard ECMAScript runtime.

The installation requires a matching V8 package under:

```text
YourProject/Plugins/Puerts/ThirdParty
```

The selected V8 version must match the `UseV8Version` setting in:

```text
Puerts/Source/JsEnv/JsEnv.Build.cs
```

### Node.js

The manual recommends the Node.js backend when npm package support is important.

The Node.js backend files go under the PuerTS `ThirdParty` directory. The build setting must enable Node.js:

```text
UseNodeJs = true
```

Node.js increases package size but gives scripts access to more Node and npm functionality.

### QuickJS

QuickJS targets smaller deployments.

The QuickJS backend files go under the PuerTS `ThirdParty` directory. The build setting must enable QuickJS.

QuickJS does not provide the same Node.js ecosystem.

## Blueprint-only projects

PuerTS contains C++ source. A Blueprint-only project does not automatically compile C++ plugins.

The manual recommends one of these actions:

- Add a C++ class to convert the project into a C++ project.
- Build the engine with PuerTS included.
- Generate project files and compile the plugin from an IDE.

## macOS quarantine issue

macOS can block downloaded dynamic libraries. The manual provides an `xattr` command to remove the quarantine flag from PuerTS libraries.

## UE4.27 relevance

UE4.27 falls into the documented UE4.25-and-newer backend range. The selected PuerTS release, backend binary, and build flags must still match the pinned project configuration.

---

# 3. Setup Development Environment

Source: <https://puerts.github.io/en/docs/puerts/unreal/dev_environment/>

## TypeScript workflow

PuerTS executes JavaScript. The recommended authoring workflow uses TypeScript and compiles it to JavaScript.

The setup requires:

- Node.js
- A local TypeScript package
- A `tsconfig.json`
- Generated PuerTS declaration files

A basic setup installs TypeScript with npm and compiles with:

```text
npx tsc
```

## `tsconfig.json`

The manual uses a project-level `tsconfig.json`.

Key compiler settings include:

- A modern JavaScript target.
- CommonJS modules.
- Source maps.
- Optional decorator support.
- Type roots for generated declarations and npm types.
- An output directory under `Content/JavaScript`.
- An include path for the project TypeScript source.

Conceptual layout:

```text
YourProject/
  tsconfig.json
  TypeScript/
  Content/
    JavaScript/
  Typing/
```

A project can use another layout, but `typeRoots`, `include`, and `outDir` must match it.

## Type safety

The generated `.d.ts` files describe the Unreal API available to TypeScript.

The declaration generation step provides:

- Class names.
- Function signatures.
- Properties.
- Structs.
- Enums.
- Delegates.
- Blueprint-generated types.

The manual tells developers to regenerate declarations after relevant C++ changes.

## Generated-file policy

Generated declarations should not contain manual changes. A later generation can replace them.

A reusable plugin should document:

- The generator command.
- The input project.
- The output directory.
- Whether Git tracks the files.
- How to detect stale declarations.

---

# 4. Getting Started

Source: <https://puerts.github.io/en/docs/puerts/unreal/getting_started/>

## Two execution modes

PuerTS supports two main execution modes. They can exist in the same project, but their virtual machines remain isolated.

### Automatic binding mode

Automatic binding mode starts a default virtual machine through the PuerTS module.

It supports:

- TypeScript classes that extend Unreal classes.
- Proxy Blueprint generation.
- Incremental TypeScript compilation.
- Hot reload.
- Event overrides.
- Input events.
- RPC declarations.

The setup command runs from the PuerTS plugin directory:

```text
node enable_puerts_module.js
```

The class file name, class name, and default export must match.

Example naming rule:

```text
File: TS_Player.ts
Class: TS_Player
Default export: TS_Player
```

### Manually started virtual machines

A native C++ owner creates an `FJsEnv` instance and starts a JavaScript entry module.

This mode suits:

- One-time scripts.
- A project script entry point.
- A long-lived orchestration process.
- A separate tool or service runtime.

The native owner must keep the `FJsEnv` alive for as long as scripts need it.

## Isolation

Each `FJsEnv` is a separate virtual machine. Separate instances do not share normal JavaScript memory or global state.

## Selection guidance

Use automatic binding when Unreal must instantiate TypeScript-backed Unreal classes.

Use a manual virtual machine when C++ owns the script runtime and controls its lifetime.

---

# 5. Starting a New JavaScript Virtual Machine

Source: <https://puerts.github.io/en/docs/puerts/unreal/start_a_virtual_machine/>

## `FJsEnv`

`puerts::FJsEnv` owns a JavaScript virtual-machine environment.

It controls:

- Script startup.
- Module loading.
- Logging.
- Debugger configuration.
- Memory associated with the runtime.
- Arguments passed into the entry module.

## One-time script

A native object can create `FJsEnv` and call `Start` during an Unreal event, such as `BeginPlay`.

The entry name points to a compiled JavaScript file.

## Long-lived entry point

A `UGameInstance` can own an `FJsEnv`.

A common lifecycle is:

```text
UGameInstance::OnStart
  -> create FJsEnv
  -> start Entry.js

UGameInstance::Shutdown
  -> reset FJsEnv
```

This pattern gives the runtime a clear owner and shutdown point.

## Passing Unreal objects to TypeScript

`Start` can receive named arguments.

The manual describes the arguments as name and `UObject*` pairs. TypeScript reads them through `argv`.

Important rules:

- Passed values must be `UObject` instances or subclasses.
- Names act as map keys.
- Imported TypeScript modules can read the same `argv`.
- The native owner controls object validity.

## Main methods

The page highlights these `FJsEnv` capabilities:

- `Start`: Start the virtual machine and execute an entry module.
- `WaitDebugger`: Wait for a debugger attachment.
- `CurrentStackTrace`: Get the current JavaScript stack trace.

## Module path

The default module loader uses `Content/JavaScript` as the script root unless the runtime receives another loader configuration.

## Multiple virtual machines

Multiple `FJsEnv` instances are isolated. Data must cross through an explicit native or external bridge.

## Lifetime warning

Do not create a short-lived local `FJsEnv` for a long-running script. The runtime can stop when the native smart pointer leaves scope.

---

# 6. Debugging

Source: <https://puerts.github.io/en/docs/puerts/unreal/vscode_debug/>

## Automatic binding debug settings

Automatic binding mode uses PuerTS project settings.

The relevant settings include:

- Debug enable.
- Debug port.
- Wait for debugger.
- Wait timeout.

These settings control the default PuerTS runtime.

## Manual virtual-machine debugging

A manually created `FJsEnv` can receive:

- A module loader.
- A logger.
- A debugger port.

The runtime can call `WaitDebugger` before it starts the entry script.

## VS Code configuration

The manual uses a VS Code `launch.json` with Node attach configurations.

A configuration needs:

- `"type": "node"`
- `"request": "attach"`
- A port that matches PuerTS.

The manual uses separate example ports for manual mode and automatic binding mode.

## Early breakpoints

Breakpoints in startup code can be missed when the runtime starts before the debugger attaches.

Use a wait setting or call `WaitDebugger` before `Start`.

## Apparent editor freeze

The editor can appear frozen when PuerTS waits for a debugger. This is expected behavior.

Disable the wait setting in `Config/DefaultPuerts.ini` when no debugger will attach.

## Background CPU throttling

Unreal Editor can reduce CPU use when it is not the active window. This can make debugging from VS Code appear extremely slow.

The manual recommends disabling the editor preference named `Use Less CPU when in Background` during debugging.

---

# 7. Interacting With C++ From TypeScript

Source: <https://puerts.github.io/en/docs/puerts/unreal/script_call_uclass/>

## Reflection test

The first question is whether Unreal reflects the API.

PuerTS recognizes APIs marked with:

- `UCLASS`
- `UFUNCTION`
- `UPROPERTY`
- `USTRUCT`
- `UENUM`

These APIs become available through the generated `ue` declarations.

## Calling reflected objects

A native owner can pass a UObject into the JavaScript environment through `argv`.

TypeScript can then:

- Read and write reflected properties.
- Call reflected member functions.
- Call reflected static functions.
- Access the object world.
- Load reflected classes.
- Spawn actors through reflected engine helpers.

## Loading Blueprint classes

The manual loads a generated Blueprint class through a class path.

Blueprint-generated class paths usually end in `_C`.

The general pattern is:

```text
/Game/Folder/Asset.Asset_C
```

TypeScript can then use reflected spawn helpers with the loaded class.

## Type names

PuerTS TypeScript names omit Unreal’s common native prefixes.

Examples:

```text
FVector -> UE.Vector
AActor  -> UE.Actor
UObject -> UE.Object
```

## Containers

PuerTS provides constructors for reflected Unreal containers.

Documented helpers include:

- `UE.NewArray`
- `UE.NewSet`
- `UE.NewMap`

The caller supplies a supported element type.

Examples of type categories include:

- Built-in integers.
- Strings.
- UObject classes.
- UStruct types.

These are Unreal-backed containers. They are not ordinary JavaScript arrays, sets, or maps.

## Array buffers

A native function can receive PuerTS byte data through an Unreal array-buffer type.

TypeScript can pass a typed array such as `Uint8Array`.

## Mixin path

TypeScript can override selected reflected behavior through Blueprint mixins.

## Non-reflected APIs

PuerTS cannot call a non-reflected C++ API through the `ue` module automatically.

Use template-based static binding or add a reflected wrapper.

---

# 8. Template-Based Static Binding

Source: <https://puerts.github.io/en/docs/puerts/unreal/template_binding/>

## Purpose

Static binding exposes non-reflected Unreal or ordinary C++ APIs to TypeScript.

This path is useful for:

- Public C++ APIs without Unreal reflection.
- Plain C++ classes.
- Performance-sensitive interfaces.
- Stable native service layers.
- Editor APIs that should not become Blueprint APIs.

## Build dependency

The native module must depend on `JsEnv` in its `Build.cs`.

## Unreal class binding

The binding code declares each used Unreal type and registers methods through `puerts::DefineClass`.

The documented concepts include:

- `UsingUClass`
- `DefineClass`
- `Method`
- `Function`
- `Property`
- `Variable`
- `Register`

## Plain C++ class binding

Plain C++ types use the `cpp` TypeScript module.

The binding can expose:

- Static functions.
- Member functions.
- Static variables.
- Member variables.
- Constructors.
- Inheritance.

## Member and static functions

Use these declaration methods:

- `.Method(...)` for a member function.
- `.Function(...)` for a static function.

Function references use helpers such as:

- `MakeFunction`
- `MakeCheckFunction`
- `SelectFunction`
- `MakeOverload`
- `CombineOverloads`

Overload helpers select or combine C++ overloads.

## Member and static data

Use these declaration methods:

- `.Property(...)` for member data.
- `.Variable(...)` for static data.

Reference helpers include:

- `MakeProperty`
- `MakePropertyByGetterSetter`
- `MakeVariable`

A getter and setter pair can expose data without exposing the native field directly.

## Constructors

Static binding can expose:

- A default constructor.
- A constructor with specified argument types.
- Several overloaded constructors.

Constructor helpers include `MakeConstructor` and `CombineConstructors`.

## Inheritance

A bound child type can declare its native base with `.Extends<BaseType>()`.

The base type must also have a binding.

## Regeneration requirement

After a binding change:

1. Compile the C++ module.
2. Restart Unreal Editor when required.
3. Regenerate TypeScript declarations.

## Design guidance

Expose stable batch APIs instead of many small native calls.

For an MCP bridge, static binding is a good path for native services that PuerTS workflows must call directly.

---

# 9. Engine or Pure C++ Calling TypeScript

Source: <https://puerts.github.io/en/docs/puerts/unreal/engine_call_script/>

## Dynamic delegates

C++ can invoke TypeScript through Unreal dynamic delegates.

The manual covers:

- Dynamic single-cast delegates.
- Dynamic delegates with parameters.
- Dynamic delegates with return values.
- Dynamic multicast delegates.

A delegate property must be reflected for TypeScript to bind to it.

## TypeScript delegate operations

TypeScript can:

- Bind a function.
- Bind an inline callback.
- Add multicast listeners.
- Return values.
- Read and update reference parameters.

PuerTS helpers for reference values include:

- `$unref`
- `$set`

## TypeScript-created delegates

TypeScript can create a delegate object and pass it into C++.

Two lifetime models are documented:

1. **Owner-backed delegate**
   - `toDelegate` associates the delegate with a UObject owner.

2. **Manual-release delegate**
   - `toManualReleaseDelegate` creates a delegate without an owner.
   - `releaseManualReleaseDelegate` must release it.
   - Failing to release it can leak the delegate.

## `FJsObject`

C++ can receive a JavaScript object through `FJsObject`.

Native code can:

- Read object fields.
- Write object fields.
- Invoke a JavaScript function object.
- Return native results.

This path supports flexible data exchange, but it has less static type safety than reflected structs or fixed request schemas.

## `std::function`

Static-bound C++ APIs can receive JavaScript callbacks as `std::function`.

The JavaScript function can receive native arguments and return a value.

## MCPBridge implication

Dynamic delegates suit Unreal events and callbacks.

`FJsObject` suits flexible internal calls, but a public MCP boundary should still validate a fixed JSON schema.

---

# 10. Automatic Binding Mode

Source: <https://puerts.github.io/en/docs/puerts/unreal/uclass_extends/>

## Purpose

Automatic binding lets a TypeScript class extend an Unreal class.

PuerTS uses a default runtime and generates an Unreal-visible proxy class.

The mode supports:

- Proxy Blueprint generation.
- Incremental compilation.
- Hot reload.
- Unreal event overrides.
- Input events.
- TypeScript-defined Unreal members.
- RPC metadata.

## Setup

Close Unreal Editor before setup.

Run this command from the PuerTS plugin directory:

```text
node enable_puerts_module.js
```

The setup installs editor-side dependencies and updates required project files.

## File and class format

A TypeScript class must:

- Extend an Unreal class.
- Use a matching file name.
- Use a matching class name.
- Export that class as the default export.

## Constructor behavior

Automatic binding uses a method named `Constructor`, not the normal TypeScript `constructor`.

This method maps to Unreal object construction behavior.

Constructor rules include:

- Call construction-only Unreal APIs in `Constructor`.
- Create default subobjects there.
- Set native default properties there.
- Do not create persistent JavaScript closures or similar JavaScript resources there.
- TypeScript initialization can override editor defaults for supported native fields.
- TypeScript-only fields do not use Unreal initialization.

## Supported data types

The page documents Unreal-compatible TypeScript field and parameter types.

The main categories are:

- `void`
- `boolean`
- `number`
- `bigint`
- `string`
- Unreal object classes
- Unreal structs
- Unreal classes
- Unreal arrays, sets, and maps
- Delegates
- Pointer and subclass wrapper types

TypeScript has fewer numeric names than C++. PuerTS annotations identify the required native type.

## Type annotations

Documented annotations include:

- `@cpp:text` for `FText`.
- `@cpp:name` for `FName`.
- `@cpp:int` for an integer type.
- `@cpp:byte` for a byte type.
- `@no-blueprint` for a TypeScript-only field or method.

## Decorator setup

Set this TypeScript compiler option:

```json
{
  "experimentalDecorators": true
}
```

PuerTS decorator declarations provide Unreal-style class, function, property, and RPC metadata.

## Class flags and metadata

The manual documents class controls such as:

- Blueprint visibility and Blueprint base-class use.
- Abstract or const classes.
- Deprecation.
- Component wrapper behavior.
- Hidden categories and functions.
- Advanced display.
- Experimental and early-access markers.
- Tooltips and display names.
- Blueprint-spawnable components.
- Tick restrictions.
- Hidden Blueprint overrides.
- Restricted function libraries.
- World-context display.
- Async proxy exposure.
- Blueprint thread safety.
- Hierarchy support.

## Function flags and metadata

The manual documents function controls such as:

- `BlueprintImplementableEvent`
- `BlueprintNativeEvent`
- `SealedEvent`
- `Exec`
- `BlueprintPure`
- `BlueprintCallable`
- `BlueprintAuthorityOnly`
- `BlueprintCosmetic`
- `CallInEditor`
- Category, tooltip, compact-title, and keyword metadata

## Property flags

The manual documents many property controls. Main groups include:

### Persistence and lifetime

- Config and global config.
- Transient and duplicate-transient behavior.
- Save-game serialization.
- Text export and serialization controls.

### Replication

- Replicated.
- Replicated with notification.
- Not replicated.

### Editor visibility

- Edit anywhere.
- Edit on instances.
- Edit on defaults.
- Visible anywhere.
- Visible on instances.
- Visible on defaults.
- Simple and advanced display.

### Blueprint access

- Blueprint read-only.
- Blueprint read-write.
- Blueprint getter and setter.
- Blueprint-assignable delegates.
- Blueprint-callable delegates.
- Authority-only delegates.

### Object and component behavior

- Instanced.
- Export.
- Asset-registry searchable.
- Fixed-size editing.
- Non-transactional.

## Property metadata

The page provides a large metadata catalog.

Important groups include:

- Allowed and disallowed classes.
- Abstract and Blueprint-only class filtering.
- Clamp and UI numeric ranges.
- Asset bundles.
- Content and file path behavior.
- Property order and thumbnails.
- Edit conditions.
- Spawn exposure.
- Engine and plugin content visibility.
- Viewport edit widgets.
- Required interfaces.
- Multi-line and secret text fields.
- Relative paths.
- Tree views.
- Array summary titles.
- Dynamic option lists.
- Bitmasks.

## RPC decorators

RPC flags cover:

- Networked functions.
- Reliable functions.
- Multicast calls.
- Server calls.
- Client calls.

Property flags cover network replication and replication notifications.

Replication conditions include:

- Initial only.
- Owner only.
- Skip owner.
- Simulated actors.
- Autonomous actors.
- Physics-related conditions.
- Replay-related conditions.
- Custom conditions.
- Never replicate.

A replicated property with notification needs the corresponding `OnRep_...` method.

## Automatic-binding boundary

Automatic binding is not the same as direct private-engine control.

It creates Unreal-visible proxy classes and redirects supported calls into the default PuerTS runtime.

---

# 11. Blueprint Mixin

Source: <https://puerts.github.io/en/docs/puerts/unreal/mixin/>

## Purpose

Mixin combines TypeScript behavior with an existing Unreal C++ or Blueprint class.

It supports:

- Overriding selected Unreal or Blueprint functions.
- Adding TypeScript methods and fields.
- Creating a derived generated class.
- Applying behavior to an existing class.

## C++ class mixin

The base C++ class must expose overridable functions through Unreal reflection.

For native functions, supported override points require:

- `BlueprintNativeEvent`, or
- `BlueprintImplementableEvent`

The TypeScript class implements an interface that matches the Unreal class.

`Puerts.blueprint.mixin` creates or applies the combined type.

## Blueprint class mixin

TypeScript can load a Blueprint-generated class, convert it to a JavaScript-interpreted type, and apply a mixin.

The generated class can then be used for actor spawning.

## Configuration

The documented mixin configuration includes:

- `objectTakeByNative`
- `inherit`
- `generatedClass`

### `objectTakeByNative`

When enabled, Unreal owns the object-side garbage-collection relationship.

### `inherit`

When enabled, PuerTS creates a new generated class instead of changing behavior globally.

### `generatedClass`

This can supply a generated class to the mixin operation.

## Calling base behavior

The page shows a prototype-chain pattern that permits `super` calls from the TypeScript override.

This needs careful testing because the runtime prototype chain must match the Unreal class relationship.

## Override limit

Blueprint-defined functions and events are supported.

Native C++ functions require a Blueprint event specifier. An arbitrary native virtual C++ method is not automatically a mixin override point.

---

# 12. FAQ

Source: <https://puerts.github.io/en/docs/puerts/unreal/faq/>

## Windows zero-length allocation warning

Unreal’s allocation behavior can return `nullptr` for a zero-length `std::nothrow` array allocation.

V8 can interpret this as an out-of-memory failure.

PuerTS detects and patches this condition. The warning is informational.

## Missing extension functions in automatic binding

The PuerTS module can start before later modules register extension methods.

After all modules load, call:

```text
IPuertsModule::Get().InitExtensionMethodsMap();
```

This rebuilds the extension-method map.

## Wait-for-debugger hang

The wait option intentionally blocks startup until a debugger connects.

Disable it in:

```text
Config/DefaultPuerts.ini
```

when no debugger will attach.

## Unexpected `StaticClass`

A TypeScript-generated class does not behave like a native C++ class with its own normal static method.

The FAQ recommends loading the generated Blueprint class by path with `UE.Class.Load`.

## macOS V8 verification error

Remove the quarantine attribute from the downloaded V8 dynamic library.

## Blueprint-only compile failure

Generate native project files and compile the C++ plugin. Blueprint-only projects do not automatically build source plugins.

## Missing fields after packaging

Editor and runtime handling of `FName` case can produce a field-name mismatch.

Use consistent capitalization across Blueprint fields, generated declarations, and script access.

## Illegal-thread construction in UE5

The FAQ connects this error with asynchronous loading.

Its listed workaround disables the async loading thread. This item targets UE5 and needs separate evaluation for UE4.27.

## Invalid object exceptions

PuerTS throws when script code calls an invalidated Unreal object proxy.

The recommended approach is:

- Release references during level or world transitions.
- Avoid long-lived stale references.
- Use exception handling only for non-critical cases.

## Garbage collection ownership

PuerTS creates a JavaScript proxy for an Unreal object.

Two ownership models exist:

1. JavaScript proxy retains the Unreal object.
2. Unreal retains the JavaScript proxy relationship.

Unreal owns the proxy relationship in cases such as:

- A TypeScript class that inherits an Unreal class.
- A mixin with native ownership.
- The deprecated `makeUClass` path.

Unreal can still force object destruction during a transition even when JavaScript holds a proxy.

## `Puerts_UserObjectRetainer`

This name indicates that the JavaScript proxy still retains the Unreal object.

V8 uses generational garbage collection. Old objects may remain until a later full scan.

The FAQ lists APIs that can hint or force V8 garbage collection. Forced full collection is slow and should stay a test or diagnostic operation.

## Scripts missing from packaged builds

JavaScript files are not Unreal assets.

Add `Content/JavaScript` to the packaging setting for additional non-asset directories.

## TypeScript version limits

Automatic class inheritance uses the TypeScript compiler bundled under PuerTS editor script directories.

The FAQ lists older TypeScript versions as known stable or tested and says newer versions above its stated limit are unsupported.

A pinned UE4.27 integration should preserve its tested TypeScript version.

## Incremental `ue_bp.d.ts` problems

Blueprint declarations use incremental generation.

When the declaration state is stale, run:

```text
Puerts.Gen FULL
```

## Missing proxy Blueprint generation

Troubleshooting commands include:

```text
puerts ls
puerts ls TsTestActor
puerts compile <file-id>
```

Check:

- Whether PuerTS commands initialized.
- Whether the file is inside the TypeScript project.
- Whether the file, class, and export names match.
- Whether the file has been processed as a Blueprint class.

## Syntax errors in `ue_bp.d.ts`

Invalid Blueprint path or member characters can create invalid TypeScript syntax.

Possible responses include:

- Blacklist a small number of invalid Blueprints.
- Generate declarations only for a clean content path.

Example:

```text
Puerts.Gen PATH=/Game/StarterContent
```

## Maximum call-stack errors

An intermittent stack error can indicate unsafe multi-threaded access to `FJsEnv`.

The FAQ recommends a `THREAD_SAFE` build macro for V8.

QuickJS does not support the same multi-threaded access.

A consistent stack error can instead indicate JavaScript recursion.

---

# 13. Official PuerTS Demos

Source: <https://puerts.github.io/en/docs/puerts/unreal/demos/>

## Demo setup

The page recommends:

1. Download a selected demo project.
2. Install PuerTS through the standard installation process.
3. Prefer the Node.js backend for the demo environment.

## Current demonstration categories

The page links examples for:

- Manual virtual-machine startup.
- Automatic binding.
- VS Code debugging.
- C++ calling TypeScript.
- Blueprint and C++ mixins.
- Static binding for non-reflected C++ APIs.

## Legacy demo project

The legacy virtual-machine project uses a native game-instance entry point.

The selected TypeScript example can be changed through the native startup source.

## Inherited-class demo

The FPS demo shows the automatic class-inheritance system.

The default virtual machine owns TypeScript-backed engine classes.

Any separately created virtual machine remains isolated.

## Interaction examples

The listed examples cover:

- TypeScript and Unreal calls.
- Unreal containers.
- Asynchronous Blueprint loading.
- Wrapping latent behavior with `async` and `await`.
- UMG loading and event binding.
- Mixins.
- Plain C++ static bindings.

## Default virtual-machine arguments

The demos note that the automatic default virtual machine does not receive a manually supplied `GameInstance` argument unless the integration adds it.

Code that expects `argv.getByName("GameInstance")` must account for the selected runtime mode.

## Editor extensions

PuerTS can create Unreal Editor extensions.

The listed demos include:

- Menus.
- Toolbars.
- Dropdown actions.
- Context menus.
- Command-line extensions.
- Optional immediate-mode GUI.
- Node.js API use.

This area is important for an MCP-controlled Unreal Editor because it shows PuerTS can coordinate editor-facing tools.

---

# Consolidated PuerTS capability model

## PuerTS can directly access

PuerTS can directly use reflected Unreal APIs that appear in the generated declarations.

This generally includes:

- Blueprint-callable functions.
- Reflected properties.
- Reflected structs.
- Reflected enums.
- Reflected delegates.
- Blueprint-generated classes.
- Public reflected editor APIs.

## PuerTS needs a native binding for

PuerTS needs a static binding or wrapper for:

- Non-reflected C++ APIs.
- Plain C++ classes.
- Native helpers without Blueprint exposure.
- Stable high-performance batch operations.
- Private engine implementation.
- APIs with unsupported parameter or return serialization.

## PuerTS can orchestrate

PuerTS is suitable for:

- Multi-step editor workflows.
- Feature assembly.
- Project inspection.
- Actor and component configuration.
- Request normalization.
- Calling native builders.
- Test coordination.
- Result aggregation.
- Repair loops.
- JSON reporting.

## Native C++ remains preferable for

Native C++ remains preferable for:

- Blueprint graph mutation.
- Package creation and saving.
- AssetTools integration.
- Transactions and undo.
- PIE control.
- UnrealBuildTool integration.
- Large loops.
- Thread-sensitive work.
- Private or editor-only internals.
- Stable low-level bridge primitives.

---

# Generated declaration files

The manual’s declaration workflow produces the type information that TypeScript uses.

A typical set includes:

```text
index.d.ts
puerts.d.ts
puerts_decorators.d.ts
ue.d.ts
ue_bp.d.ts
```

## General roles

### `puerts.d.ts`

Defines PuerTS runtime helpers, delegate types, Unreal container interfaces, references, and pointer wrappers.

### `puerts_decorators.d.ts`

Defines automatic-binding decorators and Unreal metadata names.

### `ue.d.ts`

Describes reflected C++ and engine types available in the current Unreal project.

### `ue_bp.d.ts`

Describes Blueprint-generated types discovered during generation.

This file is project-specific and can include engine, plugin, and game assets.

### `index.d.ts`

References the generated declaration set.

## Important limit

A declaration proves that the generator found a type signature. It does not prove:

- Runtime calls succeed.
- An object is valid.
- A property serializer supports every value.
- An editor mutation saves correctly.
- A Blueprint graph can be modified.
- A function works on the current thread.
- A package includes the required script.

Each important operation still needs a runtime test.

---

# Recommended UE4.27 MCPBridge interpretation

## Use one public capability surface

Keep old MCP tool names as compatible public capabilities.

Route them to:

- PuerTS workflows.
- Native named-pipe commands.
- Existing C++ builders.
- Build or commandlet processes.
- Explicit legacy adapters during migration.

## Use PuerTS for orchestration

PuerTS should:

- Inspect the project.
- Normalize requests.
- Select native commands.
- Batch related operations.
- Track progress.
- Validate results.
- Produce structured reports.

## Use existing native builders

Do not rewrite existing C++ Blueprint, behavior-tree, animation, material, or asset builders in TypeScript.

Expose them through a named-pipe command. Let PuerTS coordinate them.

## Use generated declarations as a capability index

Compare each old MCP tool with:

- `ue.d.ts`
- `ue_bp.d.ts`
- Existing native builders
- Static binding declarations
- Runtime tests

Classify each tool as:

```text
direct_reflection
puerts_workflow
existing_native_builder
new_native_wrapper
build_process
legacy_only
unsupported
```

## Regenerate per target project

A reusable plugin cannot depend on one game’s `ue_bp.d.ts`.

Generate declarations for each installed project. Keep the plugin’s stable TypeScript API separate from project-specific Blueprint declarations.

---

# Command reference

## Installation and setup

```text
node enable_puerts_module.js
npx tsc
```

## Declaration generation and repair

```text
Puerts.Gen FULL
Puerts.Gen PATH=/Game/StarterContent
```

## Automatic-binding diagnostics

```text
puerts ls
puerts ls TsTestActor
puerts compile <file-id>
```

## Packaging requirement

Add this project directory to Unreal’s additional non-asset package directories:

```text
Content/JavaScript
```

---

# Coverage checklist

| Page | Covered | Main topics |
|---|---:|---|
| User Manual | Yes | Purpose, reflection, static binding, interaction model |
| Install | Yes | Source install, releases, V8, Node.js, QuickJS |
| Development Environment | Yes | TypeScript, `tsconfig.json`, declarations |
| Getting Started | Yes | Automatic mode and manual VMs |
| Start a Virtual Machine | Yes | `FJsEnv`, lifecycle, arguments, isolation |
| Debugging | Yes | Ports, VS Code, wait behavior, CPU throttling |
| TypeScript Calls C++ | Yes | Reflection, containers, Blueprint class loading |
| Static Binding | Yes | Plain C++, functions, fields, overloads, constructors |
| C++ Calls TypeScript | Yes | Delegates, references, `FJsObject`, `std::function` |
| Automatic Binding | Yes | Proxy classes, constructors, decorators, RPC |
| Blueprint Mixin | Yes | C++ and Blueprint mixins, ownership, override limits |
| FAQ | Yes | Packaging, GC, invalid objects, declarations, threads |
| Demos | Yes | Runtime, binding, UI, async, editor-extension examples |

# End of digest
