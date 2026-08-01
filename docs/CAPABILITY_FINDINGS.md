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
6. `puerts_blueprint_build` cannot set component properties, so a generated
   StaticMeshComponent has no mesh and the spawned actor renders as a bare
   gizmo. `UBlueprintGraphBuilderLibrary::SetComponentProperty` already exists
   to back a `properties` field, but read it before wiring it up: despite the
   `JsonValue` parameter name it calls `FProperty::ImportText`, so it wants
   Unreal text format (`(X=200,Y=200,Z=200)`), not JSON. Routing it through
   `SetObjectPropertyJson`/`FJsonObjectConverter` instead would match the
   marshaling the rest of the lane now uses. Blocks F1/F2 content that needs a
   visible mesh.
7. The Blueprint builder's own node vocabulary is eight types (BeginPlay,
   ActorBeginOverlap, ActorEndOverlap, PrintString, CallFunction, Branch,
   Sequence, Comment). `docs/superpowers/specs/` documents eleven passes
   including Delay, VariableGet/Set, ForLoop and ForEachLoop, and the separate
   `BlueprintMutator` subsystem registers roughly forty node factories, but
   none of that reached `BuildBlueprintFromJSON` in this repository. Variables,
   loops, and timers are therefore not reachable from `puerts_blueprint_build`
   yet. `UBlueprintMutatorLibrary` is the richer surface to re-front next.

## Fixed

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
