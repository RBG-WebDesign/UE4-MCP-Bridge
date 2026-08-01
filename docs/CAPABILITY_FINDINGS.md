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
| JSON-authored gameplay runs in PIE | `/Game/MCPGenerated/BP_ProbeTrigger` (SceneComponent root, StaticMeshComponent pedestal, BoxComponent trigger volume, graph BeginPlay/ActorBeginOverlap/ActorEndOverlap each to a PrintString) and `/Game/MCPGenerated/BP_ProbeDropper` (one physics-simulating StaticMeshComponent) were built from JSON in 127 ms and 114 ms, both `compile_status "UpToDate"`, zero errors, saved. Spawned, then `puerts_pie_start`: the captured log holds `[LogBlueprintUserMessages] [BP_ProbeTrigger_C_4] MCP_TRIGGER_ALIVE F2-2026-08-01`, `[BP_ProbeDropper_C_2] MCP_DROPPER_ALIVE`, `[PIE] Play in editor total start time 0.145 seconds`, then `MCP_OVERLAP_ENTER`, `MCP_OVERLAP_EXIT`, `MCP_OVERLAP_ENTER`, `MCP_OVERLAP_EXIT` as the dropped rigid body fell through the volume and bounced on the pedestal. Nothing but JSON specs and spawns produced any of it (2026-08-01, Phase F2) |
| Overlap events from a generated Blueprint | `ActorBeginOverlap` and `ActorEndOverlap` build as `ReceiveActorBeginOverlap`/`ReceiveActorEndOverlap` override events and fire at runtime. Both directions observed in three separate PIE sessions (2026-08-01) |
| BoxComponent trigger volume from JSON | `{"class":"BoxComponent","name":"TriggerVolume","properties":{"BoxExtent":{"x":150,"y":150,"z":150}}}` needs no collision configuration: `UShapeComponent`'s constructor sets `OverlapAllDynamic`, and a spawned instance reads back `collisionProfileName "OverlapAllDynamic"`, `collisionEnabled "QueryOnly"`, `objectType "ECC_WorldDynamic"`, `ECR_Overlap` on all eight channels, `bGenerateOverlapEvents true`, `BoxExtent {150,150,150}` (2026-08-01) |
| Collision profile from JSON, on any primitive | `"BodyInstance": {"CollisionProfileName": "OverlapAllDynamic"}` on a `StaticMeshComponent` template applies the whole profile, not just the name: the same component read back `collisionEnabled "QueryOnly"` and `ECR_Overlap` on all eight channels, having been `BlockAllDynamic`/`QueryAndPhysics` before. `FCollisionResponse::ResponseToChannels` is `UPROPERTY(transient)`, so the responses are not written by the JSON; the Blueprint recompile round-trips the template through an archive and `UPrimitiveComponent::Serialize` calls `FBodyInstance::FixupData` -> `LoadProfileData`, which fills them in from the profile name. Proven live: with the BoxComponent moved out of the trajectory, the StaticMeshComponent alone produced `MCP_OVERLAP_ENTER` and `MCP_OVERLAP_EXIT` in PIE (2026-08-01) |
| Physics from JSON | `"BodyInstance": {"bSimulatePhysics": true}` plus `"Mobility": "Movable"` on a generated component gives a simulating rigid body: read back `bSimulatePhysics true`, `bEnableGravity true`, `Mobility "Movable"`, and in PIE the actor fell and came to rest (2026-08-01) |
| Private UPROPERTY reads and writes | `bGenerateOverlapEvents` is private on `UPrimitiveComponent` with Blueprint getter/setter. Both `read_property` and a `blueprint_build` component property reach it by name; the level's `Floor` read back `false`, a generated component read back `true` (2026-08-01) |
| PIE round trip | `pie_start` -> `get_logs` -> `pie_stop`, four times in one session, no editor restart. Editor-side calls after each stop behaved normally; `puerts_diagnostic` reported 12 actors and `is_game_thread true` afterwards (2026-08-01) |
| Blueprint member variables from JSON | `puerts_blueprint_build` takes `variables: [{name, type, default?, container?, category?}]`. `BP_ProbeDoorV2` carries `bIsOpen:bool = false`; `BP_ProbeNative` carries a nine-variable type sweep. Read back off a freshly spawned instance: `StaminaMax` 100, `OpenCount` 3, `DoorName` `"MCP door"`, `OpenOffset` `{0,0,400}`, `OpenRotation` `{pitch 0, yaw 90, roll 0}`, `PanelMesh` `StaticMesh'/Engine/BasicShapes/Cube.Cube'`, `SpawnClass` `BlueprintGeneratedClass'/Game/MCPGenerated/BP_ProbeDropper.BP_ProbeDropper_C'`, `Waypoints` `[]`, `PanelMobility` `"Movable"`. Types: bool, byte, int, int64, float, string, name, text, vector, vector2d, rotator, transform, linearcolor, `object:<class>`, `class:<class>`, `struct:<path>`, `enum:<path>`, each with `container` none/array/set (2026-08-02, mutator re-front) |
| Variable convergence and conflict rejection | An identical rerun answers `created false` per variable with the default reapplied, `compile_status "UpToDate"`. A rerun asking for `bIsOpen` as `float` is refused: `Variable 'bIsOpen' already exists as bool; the spec asks for float. Retyping is not done implicitly: it would drop every graph node that reads it.` (2026-08-02) |
| Variable validate-before-mutate | Six rejected specs against the unused path `/Game/MCPGenerated/BP_ProbeRejectV2` (unknown type `boolean`, bool default given as the string `"yes"`, vector default with the misspelled field `zz`, a default on an array variable, an object default whose asset does not load, plus an unknown `Operator` op): each named the variable and the reason, and `find_assets` for that name returned `count 0` afterwards (2026-08-02) |
| Graph node vocabulary, 26 types | `GetSupportedNodeTypes` returns BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap, PrintString, CallFunction, Operator, Delay, Branch, Sequence, Comment, Event, CustomEvent, VariableGet, VariableSet, Cast, Select, Knot, MakeStruct, BreakStruct, FormatText, SpawnActor, SwitchInt, SwitchString, MultiGate, DoOnceMultiInput. All 26 built live: 25 in one graph on `/Game/MCPGenerated/BP_ProbeNative` (25 nodes, 14 connections, zero unresolved pins, `compile_status "UpToDate"`, saved) and ActorBeginOverlap in the door graph (2026-08-02) |
| Named operators | The `Operator` node type takes `params.op` from a gated table of 25 verified UKismetMathLibrary and UKismetStringLibrary calls: not_bool, and_bool, or_bool, add/subtract/multiply/divide_float, greater/less/greater_equal/less_equal/equal_float, clamp_float, lerp_float, add/subtract/greater/less/equal_int, make_vector, add_vector, multiply_vector_float, append_string, vector_to_string, bool_to_string. An unknown name is rejected with the full list before the asset is touched (2026-08-02) |
| Latent Delay in a generated event graph | `{"type":"Delay","params":{"Duration":1.0}}` builds as a `UK2Node_CallFunction` on `UKismetSystemLibrary::Delay` and runs: in PIE the door's before and after markers are separated by the delay and both fire (2026-08-02) |
| A door that physically moves, from JSON alone | `/Game/MCPGenerated/BP_ProbeDoorV2`: SceneComponent root, Pedestal and DoorPanel StaticMeshComponents, a BoxComponent trigger with `BodyInstance.CollisionProfileName "OverlapAllDynamic"`, the variable `bIsOpen`, and a 19-node graph (ActorBeginOverlap -> VariableGet -> Operator not_bool -> Branch -> VariableSet true -> PrintString -> SceneComponent.K2_SetRelativeLocation -> Delay 1.0 s -> PrintString). PIE, with the F2 physics dropper as the triggering body: `[BP_ProbeDoorV2_C_0] MCP_DOOR_OPENING panel=X=950.000 Y=0.000 Z=220.000` then `[BP_ProbeDoorV2_C_0] MCP_DOOR_OPENED panel=X=950.000 Y=0.000 Z=620.000`. The panel's own `K2_GetComponentLocation` reports it 400 uu higher after the move, so the motion is measured by the graph rather than asserted. Exactly one opening per run: the second overlap pass that F2 recorded is swallowed by the `bIsOpen` guard, which is the variable doing its job (2026-08-02, Phase F3) |
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
8. **Mostly fixed 2026-08-02** (see "Graph node vocabulary" above). The
   vocabulary was eight types; it is now 26, and Blueprint member variables
   are reachable. What is still missing from the mutator's forty-odd factories
   and from the specs: no Timeline node of any kind, no ForLoop or
   ForEachLoop (they are macro instances, and `MacroInstance` is registered in
   `FBPNodeRegistry` but not advertised here because its `macro_bp`/`macro_name`
   pair is not documented and the engine macro library path was not verified),
   no delegate or input node types (registered, not advertised, because they
   need project input settings or a delegate property to point at), and no
   widget or audio authoring surface at all. A loop today has to be written as
   a Delay chain or a Tick with a counter variable.
9. `physics_build` bodies cannot fire overlap events, so they are not a usable
   mover for a trigger probe. `AStaticMeshActor`'s constructor calls
   `StaticMeshComponent->SetGenerateOverlapEvents(false)`
   (`StaticMeshActor.cpp:33`), and `physics_build` spawns `AStaticMeshActor`,
   so its bodies fail the both-sides test in
   `UPrimitiveComponent::CanComponentsGenerateOverlap`. Repro: `physics_build`
   one simulating cube at `{700,0,1100}` above the same trigger,
   `read_property .../MCP_F2_PhysCube.StaticMeshComponent0
   bGenerateOverlapEvents` -> `false`; PIE, and `physics_observe` shows it at
   rest on the trigger's pedestal at `z 170.00003`, so it passed straight
   through the volume, while the log after that run's `MCP_TRIGGER_ALIVE`
   contains no `MCP_OVERLAP_*` line at all. The same read on the level's own
   `Floor` returns `false` too: every `AStaticMeshActor` is like this. The
   overlapping body has to be a generated Blueprint, whose
   `StaticMeshComponent` keeps the `UPrimitiveComponent` default of `true`.
10. `physics_observe` only iterates `AStaticMeshActor`
    (`MCPPuerTSBridgePhysics.cpp:279`). A JSON-authored Blueprint actor with a
    simulating `StaticMeshComponent` is invisible to it: with only
    `BP_ProbeDropper_C_0` simulating in the PIE world, `physics_observe` with
    no filter returned `{"world":"pie","count":0,"actors":[]}`. So the tool
    that reads runtime transforms cannot see the bodies the authoring tool
    creates, and PIE-time position evidence for a generated actor has to come
    from log output instead.
11. No component-level collision function surface. `SetCollisionProfileName`
    is a `UFUNCTION`, and `blueprint_build` component properties are reflected
    properties only: `{"SetCollisionProfileName": "OverlapAllDynamic"}` and the
    top-level `{"CollisionProfileName": "OverlapAllDynamic"}` are both rejected
    with `StaticMeshComponent has no reflected property by that name.` The
    working spelling is the nested struct, `{"BodyInstance":
    {"CollisionProfileName": "OverlapAllDynamic"}}`, which is not discoverable
    from the schema and is worth a description example.
12. `get_logs` has no cursor. `GetRecentLogs` calls `Since(0, N)`: the ring
    holds 2000 lines, a read returns at most the last 500, and there is no
    `since`/marker parameter, so consecutive reads return overlapping windows
    that a caller has to de-duplicate itself. A PIE start/stop cycle costs
    roughly 70 lines (290 -> 346 -> 416 -> 489 across four cycles in one
    session), so about seven more cycles push a given run's lines out of a
    500-line read. Anchor on content (the last `MCP_TRIGGER_ALIVE`) rather than
    on line counts.
13. `pie_start` takes no options: its schema is `{}`. No map override, no
    player count, no simulate-versus-play, no dedicated server, no run-for-N-
    seconds. It also returns before the session exists ("Play In Editor start
    requested." at 33-107 ms, while `[PIE] Play in editor total start time
    0.145 seconds` appears later), so the caller has to sleep and then poll
    `get_logs`. `pie_stop` is deferred the same way: two `pie_stop` calls in
    one batch gave `success` then `No Play In Editor session is active or
    queued.`
14. During PIE the native allowlist admits only `pie_stop`, `get_logs` and
    `physics_observe` (`MCPPuerTSBridgeService.cpp:257`). Everything else,
    including `viewport_screenshot`, `spawn_actor` and `read_property`, returns
    `Editor operations are blocked during Play In Editor. Stop PIE first.` So
    there is no screenshot of the running game and no runtime property read;
    a generated Blueprint has to report its own state through `PrintString`.
15. Intermittent, twice observed, not reproduced. (a) `delete_actor
    BP_ProbeTrigger_C_0` answered `Actor not found.` in the request right after
    a `blueprint_build` that recompiled the actor's class, while
    `find_actors` in the next request of the same batch still listed
    `BP_ProbeTrigger_C_0`; a retry deleted it. (b) `find_actors {"name":
    "BP_Probe"}` returned `count 0` in the request right after two deletes and
    two spawns in the same batch, while the spawns had returned
    `BP_ProbeTrigger_C_3` and `BP_ProbeDropper_C_2` and the same query in the
    next server session listed both. Targeted repros of each (spawn then find;
    recompile then delete) both passed, so the trigger is not identified. Treat
    a single miss as worth one retry, not as proof the actor is gone.
    Three more sightings on 2026-08-02, all `read_property` returning
    `Actor not found: BP_ProbeNative_C_0` for one read out of nine against the
    same actor in the same batch, at a different position each run, while the
    other eight succeeded. Still not reproduced on demand. A separate lesson
    from chasing it: `spawn_actor` returns the assigned name, and after a
    delete the next spawn of the same class is `_C_1`, not `_C_0`. Use the
    returned name; do not compute it.
16. A Blueprint variable's default is a string, and it is NOT read back by
    `FProperty::ImportText`. `FBlueprintEditorUtils::PropertyValueFromString_Direct`
    special-cases four structs (`BlueprintEditorUtils.cpp:8983-9015`):
    `FVector` and `FRotator` go through `FDefaultValueHelper::ParseVector` /
    `ParseRotator`, `FTransform` and `FLinearColor` through their own
    `InitFromString`. The two halves disagree, and the asymmetry is not
    symmetric between them either: `ParseRotator` falls back to
    `FRotator::InitFromString` and so accepts `P= Y= R=`, while `ParseVector`
    has no such fallback and rejects `X=0.000 Y=0.000 Z=400.000` with
    `Can't parse default value` at compile time. The builder writes the comma
    triple for vector and rotator and the type's own `ToString` for transform
    and linear color, and writes the CDO through `PropertyValueFromString` so
    only one parser is ever involved. Anything reaching for `ImportText` on a
    Blueprint variable default will hit this.
17. `UScriptStruct::ExportText` is a delta export (`Class.cpp:2916`). Called
    with `Defaults == Value` it emits `()` for any struct that has no native
    `ExportTextItem`, so the value silently vanishes. `FVector` has one and
    looked correct; `FRotator` has none and read back as all zeroes. Pass a
    null `Defaults`.
18. `viewport_screenshot` fits the requested actors but from a fixed distance
    that depends on how many were requested: the door and the dropper together
    (1080 uu apart vertically) framed the whole level and left the door a few
    dozen pixels tall, while the door alone framed usefully. For readable
    evidence, request the one actor that matters. Filed alongside limitation 3.
19. An array or set variable takes no `default` in the build spec. The value
    would have to be Unreal's array import text, which is a second grammar for
    a caller to get right for no gain; entries are set from the graph instead.
    Rejected by name with that reason.

## Fixed

**Blueprint variables and the mutator node registry** (was limitation 8; fixed
2026-08-02). `puerts_blueprint_build` now takes `variables` and its graph
vocabulary is 26 node types instead of eight.

Two decisions shaped this:

- **The registry is consulted, not copied.** `BuildBlueprintFromJSON` keeps a
  local dispatch case for the eleven types where the builder wants control of
  the shape (the three actor events, Tick, PrintString, CallFunction, Operator,
  Delay, Branch, Sequence, Comment) and asks `FBPNodeRegistry::Find` for
  everything else, passing the node's `params` as the factory's ConfigJson with
  snake_case keys translated in one table. `GetSupportedNodeTypes` builds its
  second half by asking the registry whether a factory is actually registered,
  so a factory that is renamed or removed takes its node type out of the MCP
  schema instead of leaving a type that builds nothing. The enum in
  `mcp-server/src/tools/puerts.ts` and the dispatch cannot diverge without the
  native side rejecting first.
- **Math is a gated table, not a free function call.** Every operator name maps
  to a `UKismetMathLibrary` or `UKismetStringLibrary` function that was checked
  against the 4.27 headers, and an unknown name is refused with the whole list.
  The same functions stay reachable through a raw `CallFunction` node for
  anything the table does not cover; the named form exists so the vocabulary is
  documented and reviewable in one place.

Variables converge like components: same name and same pin type is a no-op with
the default reapplied, a different type is refused rather than retyped, because
retyping drops every graph node that reads the variable. Defaults are checked
against the JSON type rather than coerced from it, which is what caught
`{"type": "bool", "default": "yes"}` being accepted as `true`
(`FJsonValue::TryGetBool` answers yes to `"yes"`), and struct defaults now
reject a key that names no field of the struct, which is limitation 7 closed for
this path.

Two engine gotchas cost a build cycle each and are written up as limitations 16
and 17: the variable-default parser is not `ImportText`, and
`UScriptStruct::ExportText` elides everything when its Defaults pointer equals
its Value pointer.

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

Map/set/FText/FName reads; sky_shader_create rerun behavior; find_assets
path/name filters; undo stack depth; two-editor pipe isolation. Overlap against
a player pawn (nothing in the default game mode moves on its own, and there is
no input-simulation tool in the native catalog, so the only self-propelled
overlap source proven so far is a JSON-authored physics body).
