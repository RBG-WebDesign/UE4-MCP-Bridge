# Capability findings

Live probe results against the UE427PuerTSMCP editor. Each entry states what
was observed, with the reproduction. Phase P of docs/MASTERY_PLAN_2026-07-31.md
maintains this file; Phase L consumes it.

## Working

| Capability | Evidence (2026-07-31) |
|---|---|
| Object-reference reads | `read_property PlayerStart RootComponent` returns the component path string |
| Enum reads | `Mobility` returns the numeric value (0). No name; enhancement candidate |
| Empty array reads | `Tags` returns `[]` |
| Blueprint class spawn | `spawn_actor` with `/Game/MCPAcceptance/BP_TestActor.BP_TestActor_C` spawned and transacted, 12.3 ms |
| call_function with qualified names | `Actor.SetActorLabel ["ProbeRenamed"]` succeeded; `Actor.GetActorLocation` returns a proper `{x,y,z}` |
| Scalar property writes | `LightComponent0.Intensity` 5000 to 50000 and back via undo, verified by read-back |
| Targeted undo | `puerts_undo` with a transaction id reverted exactly that transaction |
| Full lifecycle | spawn, screenshot, modify, undo, delete, verify-gone exercised end to end |
| Struct reads | `read_property object_path .../PlayerStart.CollisionCapsule RelativeLocation` returns `{"x":0,"y":0,"z":112.00068664550781}`; `RelativeRotation` returns `{"pitch":0,"yaw":0,"roll":0}` (2026-08-01, Phase L) |
| Struct writes | `set_property` the same target to `{"x":10,"y":20,"z":112}` returned success with the reflection read-back `{"x":10,"y":20,"z":112}`; `puerts_undo` on its transaction restored `{"x":0,"y":0,"z":112.00068664550781}` (2026-08-01) |
| Array writes and non-empty array reads | `set_property actor PlayerStart Tags ["probe_a","probe_b"]` succeeded, read back `["probe_a","probe_b"]`, undo restored `[]` (2026-08-01) |
| Arbitrary struct writes | `set_property LightComponent0.LightColor {"r":20,"g":40,"b":60,"a":255}` succeeded and read back, proving the write path is no longer limited to the three hand-coded vector and rotator cases (2026-08-01) |
| Native Blueprint authoring | `puerts_blueprint_build` created `/Game/MCPGenerated/BP_ProbeDoor` (parent Actor, one StaticMeshComponent, BeginPlay to PrintString) in 91 ms: `compile_status "UpToDate"`, zero errors, saved to `Content/MCPGenerated/BP_ProbeDoor.uasset`. `spawn_actor /Game/MCPGenerated/BP_ProbeDoor.BP_ProbeDoor_C` then spawned it. Screenshot `Saved/Screenshots/MCPBridge/phase-l3-bp-probedoor.png` (2026-08-01, Phase L) |
| Blueprint build idempotency | The identical spec rerun answered `created false`, component `created false`, one asset in `/Game/MCPGenerated` afterwards, still `UpToDate` (2026-08-01) |
| Blueprint build validate-before-mutate | Six rejected specs against the unused path `/Game/MCPGenerated/BP_ProbeNative` (duplicate node id, connection to an unknown node id, missing component class, component class that is not an ActorComponent, parent class that is not an Actor, `attach_to` naming nothing): each returned `success false` with the exact reason, and `find_assets` for that name returned `count 0`, so no half-built asset was left (2026-08-01) |
| Component hierarchies | A components-only build (no graph) created `BP_ProbeNative` with `PointLightComponent Glow` attached to `SceneComponent Pivot`, compiled `UpToDate` (2026-08-01) |
| Component template properties | `puerts_blueprint_build` components take a `properties` object applied to the SCS template. `BP_ProbeDoor` rebuilt with `DoorMesh` = `StaticMesh /Engine/BasicShapes/Cube.Cube`, `OverrideMaterials ["/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"]`, `RelativeScale3D {2,2,2}`, `Mobility "Movable"`, and `SkyPanel` = the same cube with `OverrideMaterials ["/Game/MCPGenerated/M_NativeAuroraSky.M_NativeAuroraSky"]`: 7 properties applied, `compile_status "UpToDate"`, saved, 177 ms. Read back off the spawned instance: `StaticMesh'/Engine/BasicShapes/Cube.Cube'`, `["Material'/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'"]`, `"Movable"` (2026-08-01, Phase F1) |
| Game-asset references from a generated Blueprint | The `SkyPanel` material above is `/Game/MCPGenerated/M_NativeAuroraSky`, a Game asset an earlier native run authored, so the reference path is not limited to `/Engine` content: `read_property SkyPanel OverrideMaterials` returns `["Material'/Game/MCPGenerated/M_NativeAuroraSky.M_NativeAuroraSky'"]` (2026-08-01) |
| Phase F1 visible content | Spawned `/Game/MCPGenerated/BP_ProbeDoor.BP_ProbeDoor_C` and captured `Saved/Screenshots/MCPBridge/phase-f1-component-properties.png` (2085x1138). The frame shows a large light-grey cube with the engine basic-shape material standing on the dark checkered floor with its own cast shadow, and a smaller cube beside it rendering the aurora material as magenta and cyan waves on dark violet, both inside the orange selection outline. Solid shaded geometry, not the bare gizmo of the Phase L run (2026-08-01) |
| Property convergence on rerun | An identical rerun answered `created false` for the asset and both components with the same seven properties applied, `find_assets /Game/MCPGenerated` still `count 3`, `find_actors BP_ProbeDoor` still `count 1`. A rerun with changed values (mesh `Cylinder`, material `WorldGridMaterial`, scale z 3) applied them: a freshly spawned instance read back `StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'`, `WorldGridMaterial`, `z 3`, evidenced side by side in `Saved/Screenshots/MCPBridge/phase-f1-convergence.png` (2026-08-01) |
| Property validate-before-mutate | Eight rejected specs against the unused path `/Game/MCPGenerated/BP_ProbeProps`, each naming component, property, and reason: unknown property name, unloadable asset path, asset of the wrong class, wrong class inside a material array (`element 0: ... is a StaticMesh, but the property holds a MaterialInterface`), a string where an array belongs, a string where a struct belongs, and an out-of-range or misspelled enumerator (`expects a EComponentMobility enumerator: Static=0, Stationary=1, Movable=2`). `find_assets` for that name returned `count 0` afterwards (2026-08-01) |

## Defects and limitations (Phase L queue)

1. call_function requires QUALIFIED names (`Actor.GetActorLocation`); the bare
   name fails with `Function is not approved.` Undocumented. Default allowlist
   is 3 functions and each approved function also needs a hand-written native
   executor (`Approved function has no native executor`, service line ~735).
2. Failed and read-only commands still emit transaction ids and undo warnings.
   Read-only calls should not transact.
3. viewport_screenshot rejects full actor paths other tools return; matches
   short names only (task chip filed).
4. Default writable-property allowlist is 8 entries (Actor.bHidden/Tags/
   ActorLabel, SceneComponent.RelativeLocation/Rotation/Scale3D,
   LightComponentBase.Intensity/LightColor). Configurable via
   `[MCPPuerTSBridge]` ini keys `AllowedFunctions` / `AllowedWritableProperties`;
   neither surface is documented in docs/PUERTS.md.
5. No native map-load tool: the native catalog cannot open a different level.
   Blocks titled-map save probes and Phase F work on a persistent map.
6. Changing a component template does not reach instances already in the
   level. After a rerun changed `DoorMesh` to a Cylinder with
   `WorldGridMaterial` at scale z 3, the actor spawned before that build still
   read `Cube`, `BasicShapeMaterial`, z 2, while an actor spawned after it read
   the new values. The compile does not re-run construction on existing
   instances, so a probe actor has to be respawned to show a template change.
   Cheap to live with; worth knowing before reading a stale viewport as a
   failed write.
7. A struct property written from an object with a misspelled field
   (`{"xx": 2}` for a vector) is accepted and silently leaves the field at its
   previous value. `FJsonObjectConverter` ignores JSON keys that match no
   property. The value shape is checked; individual struct field names are not.
8. The Blueprint builder's own node vocabulary is eight types (BeginPlay,
   ActorBeginOverlap, ActorEndOverlap, PrintString, CallFunction, Branch,
   Sequence, Comment). `docs/superpowers/specs/` documents eleven passes
   including Delay, VariableGet/Set, ForLoop and ForEachLoop, and the separate
   `BlueprintMutator` subsystem registers roughly forty node factories, but
   none of that reached `BuildBlueprintFromJSON` in this repository. Variables,
   loops, and timers are therefore not reachable from `puerts_blueprint_build`
   yet. `UBlueprintMutatorLibrary` is the richer surface to re-front next.

## Fixed

**Component properties on generated Blueprints** (was limitation 6; fixed
2026-08-01). `puerts_blueprint_build` components now take a `properties` object
applied to the SCS component template, so a generated StaticMeshComponent has a
mesh and materials instead of rendering as a bare gizmo.

`UBlueprintGraphBuilderLibrary::SetComponentProperty` was not used. Despite its
`JsonValue` parameter name it calls `FProperty::ImportText`, which wants Unreal
text format, and it discards the parse result. The new path marshals through
`FJsonObjectConverter`, the same call `ReadObjectPropertyJson` and
`SetObjectPropertyJson` use, with two shapes resolved before the converter sees
them:

- **UObject references load explicitly.** The converter's own route for a
  `UObject*` from a string is `FProperty::ImportText`, and
  `FObjectPropertyBase::ImportText_Internal` ignores its own `bOk`: it sets the
  property to null and still returns a non-null buffer, so the converter reports
  success. An asset path that does not resolve would apply nothing and say it
  worked. The object is loaded with `LoadObject` and checked against the
  property's class instead, and arrays of object references element by element,
  which is how `OverrideMaterials` gets its material.
- **The JSON value's shape is checked against the property category.** The same
  discarded-`ImportText` fallback catches every other type: `"RelativeScale3D":
  "big"` was accepted and silently wrote nothing, and `"Mobility": 7.5` wrote an
  out-of-range enumerator that then tripped an engine ensure inside
  `USceneComponent::PostEditChangeProperty`. A struct now requires an object, an
  array requires an array, a number requires a number, an enum requires a real
  enumerator by name or value. Both were found by probing, after the first
  build; both are rejected before the asset is touched.

Property checks run in the existing validate-before-mutate pass, so a bad
property rejects the whole spec rather than leaving a Blueprint whose mesh is
quietly null. The writable-property allowlist that governs `set_property` on
level actors is deliberately not consulted here: it is eight entries long and
would reject `StaticMesh` outright. The boundary for this tool is the
`/Game/MCPGenerated/` asset-path limit it already enforced.

**Struct and array marshaling, both directions** (was defects 1 and 2; fixed
2026-08-01, commit on bridge/native-consolidation-2026-07-31). Two independent
causes, one per direction:

- READ. The `object_path` branch of `read_property` walked the property in
  TypeScript and serialized it with `Object.keys`. A PuerTS struct wrapper
  exposes its fields as prototype accessors and owns no enumerable keys, so
  every struct flattened to `{}`. A trace build confirmed it: the value was a
  live `/Script/CoreUObject.Vector` whose `own_keys` was `[]` while `.X` read
  `112.00068664550781`. Fixed by routing both target kinds through the native
  `UMCPPuerTSBridgeService::ReadObjectPropertyJson`, which uses
  `FJsonObjectConverter::UPropertyToJsonValue` - the same call the actor branch
  already used, which is why `Tags` and object references had always worked.
- WRITE. `puerts_set_property` published `value` with the empty JSON Schema
  `{}` (from `z.unknown()`). With no type information a client sends structured
  input as JSON text, and the same trace build caught it arriving as
  `kind=string json="{\"x\":10,\"y\":20,\"z\":112}"`. That string failed the
  runtime object validator, and on the actor branch it reached C++ as a string,
  which is the reported
  `LogJson: JsonValueToUProperty - Attempted to import TArray from non-array JSON key`.
  Fixed by publishing a real union schema for `value` and decoding
  JSON-encoded objects and arrays at the MCP server boundary, with the same
  guard at the runtime's last gate before reflection. The write itself now goes
  through native `SetObjectPropertyJson`/`FJsonObjectConverter`, so any
  reflected type works rather than the three hand-coded vector and rotator
  property names.

## Untested

Map/set/FText/FName reads; sky_shader_create rerun behavior;
physics_build/observe; pie_start/stop round trip; get_logs bounds; find_assets
path/name filters; undo stack depth; two-editor pipe isolation.
