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
| Sound from a generated graph, proven by playing state | `/Game/MCPGenerated/BP_ProbeDoorV3` is the door plus five sound nodes: `CallFunction GameplayStatics.SpawnSoundAtLocation` with `Sound` as the pin default `/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess`, then `KismetSystemLibrary.IsValid` on the returned AudioComponent, a Branch, and `AudioComponent.IsPlaying` reported through `bool_to_string`. In PIE: `[BP_ProbeDoorV3_C_1] MCP_DOOR_SOUND spawned=1 playing=true`, and after the door's 1 s Delay `MCP_DOOR_SOUND after_1s component_valid=false` - the `bAutoDestroy` component is gone because the sound finished, so the log records a whole playback lifecycle rather than a void call that returned. The same PIE session logs `[LogAudio] Creating Audio Device: Id: 3`, `[LogAudioMixer] Using Audio Device Speakers (High Definition Audio Device)`, `Output buffers initialized: Frames=1024, Channels=2, Samples=2048`, so the mixer was real hardware. **Audible is not provable from here**; playing-state plus completion is the honest bar (2026-08-02, Phase F3) |
| An engine SoundWave as a graph pin default | `find_assets type SoundWave path /Engine recursive` returns 50: the editor notification cues (`CompileSuccess`, `CompileFailed`), the GamePreview set, `/Engine/EngineSounds/WhiteNoise`, and the VREditor UI bank. `ApplyPinDefault` loads an object pin's default with `LoadObject`, so the asset path string is all a `Sound` pin needs (2026-08-02) |
| Native Widget Blueprint authoring | `puerts_widget_build` created `/Game/MCPGenerated/WBP_ProbeHUD` (CanvasPanel root, TextBlock `Title`, ProgressBar `StaminaBar`, TextBlock `Readout`, each with a positioned canvas slot) in 102 ms: `widget_count 4`, `compile_status "UpToDate"`, saved to `Content/MCPGenerated/WBP_ProbeHUD.uasset`, `generated_class_path /Game/MCPGenerated/WBP_ProbeHUD.WBP_ProbeHUD_C`. Read back off the widget templates by object path: `WidgetTree.StaminaBar Percent` -> `0.41999998688697815`, `FillColorAndOpacity` -> `{r 0.1, g 0.8, b 0.4, a 1}`, `WidgetTree.Title Text` -> `"MCP_HUD_TITLE"`, `WidgetTree.Readout Justification` -> `"Right"` (2026-08-02, Phase F3) |
| Widget build convergence | The identical spec rerun answered `created false`, `Widget Blueprint updated.`, `widget_count 4`, still `UpToDate`, saved. A nested tree (CanvasPanel -> VerticalBox -> Border -> Button -> TextBlock, `widget_count 6`) built clean in one pass, so panel, content and leaf categories all attach (2026-08-02) |
| Widget tree read-back | The `tree` in the response is walked from the built `UWidgetTree`, not echoed from the request: each node reports `name`, `class`, its `slot` class, and for a `CanvasPanelSlot` the position, size and z-order taken from `LayoutData.Offsets`. `Title` read back `position {60,40} size {480,48} z_order 2`, which is the slot applier's work rather than the caller's (2026-08-02) |
| Widget build validate-before-mutate | Eight rejected specs against the unused path `/Game/MCPGenerated/WBP_ProbeReject2`, each naming the node path: `R.T: Unsupported property 'percent' on TextBlock`, `R.P: Property 'percent' has wrong type`, `R.Same: Duplicate widget name 'Same'`, `R.T: Leaf widget 'TextBlock' cannot have children`, `R.B: Content widget 'Button' can have at most 1 child, got 2`, `Root widget must be a Panel type, got 'TextBlock'`, `Unknown top-level key 'theme'`, plus an unknown slot key and a missing `tree` refused by the client schema. `find_assets` for that name returned `count 0` afterwards (2026-08-02) |
| A JSON-authored HUD on screen in PIE | `/Game/MCPGenerated/BP_ProbeHUDHost` carries a `class:UserWidget` variable defaulted to `WBP_ProbeHUD_C` and a 22-node BeginPlay graph: `WidgetBlueprintLibrary.Create` -> `IsValid` -> Branch -> `UserWidget.AddToViewport` -> `IsInViewport` printed, then Delay 1 s and `IsInViewport` again, then `UserWidget.GetCachedGeometry` -> `SlateBlueprintLibrary.GetLocalSize` -> `Conv_Vector2dToString` printed. In PIE: `MCP_HUD created=1 in_viewport=true`, `MCP_HUD after_1s in_viewport=true`, `MCP_HUD painted_size=X=1480.908 Y=1080.192`. The cached geometry is Slate's own arranged size for the root canvas, so the widget was laid out and painted at PIE viewport size rather than merely registered (2026-08-02, Phase F3) |
| Phase F3 complete in one PIE session | One `pie_start` produced, in order: `[BP_ProbeDropper_C_1] MCP_DROPPER_ALIVE`, `[BP_ProbeHUDHost_C_1] MCP_HUD created=1 in_viewport=true`, `[PIE] Play in editor total start time 0.15 seconds.`, `MCP_HUD after_1s in_viewport=true`, `MCP_HUD painted_size=X=1480.908 Y=1080.192`, `[BP_ProbeDoorV3_C_1] MCP_DOOR_OPENING panel=X=950.000 Y=0.000 Z=220.000`, `MCP_DOOR_SOUND spawned=1 playing=true`, `MCP_DOOR_OPENED panel=X=950.000 Y=0.000 Z=620.000`, `MCP_DOOR_SOUND after_1s component_valid=false`. Trigger volume, moving door, sound and HUD, all from JSON specs and three spawns, with no `Accessed None` and no Blueprint runtime error in the window. Screenshot `Saved/Screenshots/MCPBridge/phase-f3-door-sound-hud.png` (2026-08-02) |
| Unresolved graph connections fail the build | Limitation 20 closed. `blueprint_build` against the unused path `/Game/MCPGenerated/BP_ProbeConn` with `start.exec -> isv.exec` and `isv.exec -> say.exec` wired against the pure `KismetSystemLibrary.IsValid` answered `success false`, `saved false`, `graph.connection_count 1`, `connections_requested 3`, `unresolved_connections ["start.exec -> isv.exec (no input pin 'exec' on isv)", "isv.exec -> say.exec (no output pin 'exec' on isv)"]`, and one `errors[]` entry naming both pairs. The same spec with the exec chain corrected answered `success true`, `connection_count 2 of 2`, saved. The old behaviour was `compile_status "UpToDate"`, `errors []`, `saved true` (2026-08-02, Phase F4) |
| The `InputKey` node type | `UK2Node_InputKey` binds a literal `FKey` (`K2Node_InputKey.h:28`) and needs no axis or action mapping in `DefaultInput.ini`, which is why it is the one input factory now advertised. `{"type":"InputKey","params":{"fkey_name":"LeftShift"}}` builds with pins `Pressed`, `Released`, `Key` (`K2Node_InputKey.cpp:56-59`), both exec outputs wired to PrintStrings, `compile_status "UpToDate"`. An unknown key name is refused by the factory, and because the node then spawns nothing the connection that referenced it is reported: `key.Pressed -> down.exec (node 'key' spawned no node)` (2026-08-02, Phase F4) |
| A Character subclass from JSON, possessing itself | `puerts_blueprint_build` with `parent_class "Character"` builds `/Game/MCPGenerated/BP_StaminaCharacter`: 22 variables, one StaticMeshComponent, a 198-node / 246-connection graph, `compile_status "UpToDate"`, saved, 16 assets before and after a rerun. In PIE it takes control of itself with `GameplayStatics.GetPlayerController(0)` -> `Controller.Possess`, and reports `MCP_STAM_POSSESS player_controlled=true` (2026-08-02, Phase F4) |
| CharacterMovement speed at runtime | `Pawn.GetMovementComponent` (pure) into `KismetSystemLibrary.SetFloatPropertyByName(Object, PropertyName "MaxWalkSpeed", Value)`, read back through `MovementComponent.GetMaxSpeed` (pure). In PIE the log carries `maxWalkSpeed=420.0` while walking and `maxWalkSpeed=900.0` while sprinting, and the pawn's own `GetVelocity` length agrees: `speed=420.000305` and `speed=900.000122`. `BlueprintInternalUseOnly` on the setter does not block it (limitation 22) (2026-08-02, Phase F4) |
| CustomEvents called from the same graph, by a two-pass build | A `CallFunction` whose class is the Blueprint's own generated class cannot resolve on the build that creates it, because the class does not exist yet. Pass 1 declares the CustomEvents and compiles; pass 2 wires `CallFunction {class: "/Game/MCPGenerated/BP_StaminaCharacter.BP_StaminaCharacter_C", function: "MCP_ANIM_SPRINT_START"}` against the class pass 1 generated. Both passes converge on rerun. In PIE the placeholders fire on every sprint edge with their own timestamps (2026-08-02, Phase F4) |
| A HUD widget driven every tick | `UWidget::SetRenderOpacity` and `SetRenderScale` are BlueprintCallable on `UUserWidget` itself, and `GetRenderOpacity` is a pure read of the same value. The host sets opacity and X scale to `CurrentStamina / MaxStamina` each tick and reports the widget's own answer: `hudPercent=1.0`, `0.751908`, `0.256035`, `0.0`, `0.181571`, tracking `stamina=100.0`, `75.190781`, `25.603493`, `0.0`, `18.15708`. The reported number is read back off the widget, not the variable that was written (2026-08-02, Phase F4) |
| The stamina feature end to end in PIE | One session, all from JSON: `MCP_STAM_POSSESS player_controlled=true`, `MCP_STAM_HUD created=1 in_viewport=true`, `MCP_STAM t=1.004709 stamina=100.0 sprinting=false canSprint=true maxWalkSpeed=420.0 speed=420.000305 hudPercent=1.0`, `MCP_ANIM_SPRINT_START placeholder fired t=2.004796`, `MCP_STAM t=3.005499 stamina=75.190781 sprinting=true canSprint=true maxWalkSpeed=900.0 speed=900.000122`, `MCP_STAM_EMPTY stamina hit zero, sprint force-stopped`, `MCP_STAM t=7.005782 stamina=0.0 sprinting=false canSprint=false maxWalkSpeed=420.0`, `MCP_STAM t=8.008076 stamina=7.126184` (regen after the 1.5 s delay), `MCP_STAM_READY sprint re-allowed at stamina=30.032526`, `MCP_STAM t=10.010129 stamina=18.15708 sprinting=true maxWalkSpeed=900.0`. No `Accessed None` and no Blueprint runtime error in the window. Three PIE sessions, same behaviour (2026-08-02, Phase F4) |
| A non-Actor Blueprint from JSON | Limitation 23 closed. `blueprint_build` with `parent_class "/Script/Engine.SaveGame"` created `/Game/MCPGenerated/BP_StaminaSave` (one float variable `SavedStamina`, no components, no graph): `compile_status "UpToDate"`, saved, 17 assets afterwards. `/Script/CoreUObject.Object` with a variable and a CustomEvent -> PrintString graph built as `/Game/MCPGenerated/BP_ProbeDataOnly`; `/Script/Engine.ActorComponent` with a float variable built as `/Game/MCPGenerated/BP_ProbeStaminaComp`. All three were unreachable before (2026-08-02, Phase L) |
| Actor-only capability is gated by parent, not by refusing the parent | Against the unused path `/Game/MCPGenerated/BP_ProbeNonActor` with a SaveGame parent: a `components` array is refused with `Components need an Actor parent: /Script/Engine.SaveGame does not derive from Actor, and only an Actor Blueprint has a SimpleConstructionScript to hold them.`, and a `BeginPlay` or `InputKey` node with `Graph node 'bp' is of type 'BeginPlay', which needs an Actor parent: ... BeginPlay, Tick, ActorBeginOverlap, ActorEndOverlap and InputKey bind actor entry points.` `find_assets` for that name returned `count 0` afterwards, so the rejection is still before the asset exists (2026-08-02) |
| A Cast node that types itself | Limitation 26 closed. `UEdGraphPin::MakeLinkTo` moves pointers and stops; the graph editor's `TryCreateConnection` also calls `PinConnectionListChanged` on both ends, which is where `UK2Node_DynamicCast::NotifyPinConnectionListChanged` (`K2Node_DynamicCast.cpp:347`) types its `Object` pin from what it is wired to. The builder now sends both notifications. `Cast` to `/Game/MCPGenerated/BP_StaminaSave.BP_StaminaSave_C` with `Object` from `GameplayStatics.LoadGameFromSlot` compiles `UpToDate` where the same spec used to fail with `The type of Object is undetermined.` (2026-08-02, Phase L) |
| DeterminesOutputType from a pin default | The same root cause, the other half. A pin default is now announced with `PinDefaultValueChanged`, which is where `UK2Node_CallFunction::PinDefaultValueChanged` -> `FDynamicOutputHelper::ConformOutputType` (`K2Node_CallFunction.cpp:1239`) retypes the output. `GameplayStatics.CreateSaveGameObject` carries `meta=(DeterminesOutputType="SaveGameClass")` (`GameplayStatics.h:976`) and now hands back the requested subclass instead of a bare `USaveGame*` (2026-08-02) |
| The `AsResult` cast pin role | A dynamic cast names its result pin `"As"` plus the target type's **display** name (`K2Node_DynamicCast.cpp:63`), which for a Blueprint generated class is neither the asset name nor anything a caller can compute from its own spec. The connection resolver takes the role `AsResult` and asks the node through `GetCastResultPin()`. Three connections in the F4 graph use it (2026-08-02) |
| Target-scoped variable access | `VariableGet` and `VariableSet` take `scope "target"` with `target_class`; the node's member reference becomes `SetExternalMember` and it grows a `self` input pin (`UK2Node_Variable::CreatePinForSelf`, `K2Node_Variable.cpp:112`) for the object to read or write. The F4 save path wires the cast result into `VariableSet SavedStamina` on `BP_StaminaSave_C`, and the load path reads it back with `VariableGet`. A `target_class` that names no such property is refused by the factory before the node exists (2026-08-02) |
| Cross-session save and load through generated Blueprints | **A verified SaveGame persistence path, not a complete feature pipeline.** Limitation 24 closed, which was the last unmet part of Phase F4. Two PIE sessions, one slot, one value. Session one: `MCP_SAVE_PRECHECK slot_exists_at_boot=false`, `[LogStreaming] Failed to read file '.../Saved/SaveGames/MCPStamina.sav' error.`, `MCP_LOAD found=false restored=none`, then `MCP_SAVE object_valid=true wrote=true stamina_at_save=16.133768`. On disk afterwards: `Saved/SaveGames/MCPStamina.sav`, 1325 bytes. Session two, fresh state: `MCP_SAVE_PRECHECK slot_exists_at_boot=true`, `MCP_STAM t=1.009965 stamina=100.0` (the variable's own default), then `MCP_LOAD found=true restored=16.133768 stamina_now=16.133768` - the exact value the previous session wrote (2026-08-02, Phase F4) |
| Reading a Blueprint back as JSON | `puerts_graph_inspect` on `/Game/MCPGenerated/BP_ProbeConn` returns `parent_class /Script/Engine.Actor`, `compile_status "UpToDate"`, one component (`DefaultSceneRoot`, `/Script/Engine.SceneComponent`, `is_root true`), two graphs (`EventGraph` Ubergraph, `UserConstructionScript` Function), and an `EventGraph` of **3 nodes and 2 connections** - the same 3 and 2 the build that made it reported. Node types come back as the builder's own words: `BeginPlay`, `CallFunction` (`{class: /Script/Engine.KismetSystemLibrary, function: IsValid}`) and `PrintString`. Connections read `K2Node_Event_6.then -> K2Node_CallFunction_7.execute` and `K2Node_CallFunction_6.ReturnValue -> K2Node_CallFunction_7.bPrintToScreen`, each also carrying both endpoint NodeGuids and PinIds (2026-08-01) |
| Inspection is deterministic, byte for byte | Two reads of `BP_ProbeConn` with `include_pins`, nothing between them: **byte-identical payloads, 14,049 bytes**, SHA-256 `eebd873da251f489d6aa82f591b38020d1d1a1121880bec7ef0685ea5613dfd2` twice, across two separate server processes. Two reads of `BP_StaminaCharacter`: byte-identical at **847,143 bytes**, SHA-256 `29f1a9239811955bb39c1c54976a75b957fb685a23ab1f32c7c6f46ffb14672c`. No exclusion list was needed. This is not luck: every array is sorted by a stable identity (nodes by NodeGuid, pins by direction then PinId, connections by their four endpoint identities, components/variables/functions/graphs by name) and no `TMap` is iterated to produce output anywhere on the path (2026-08-01) |
| The 198-node graph reads back whole | `puerts_graph_inspect` on `/Game/MCPGenerated/BP_StaminaCharacter` reports **198 nodes and 252 connections**, against `reports/session-2026-08-02-stamina-save.json`'s build report of "198 nodes, 252 of 252 connections made". 783 pins, 37 variables, 1 component, `parent_class /Script/Engine.Character`. `unmapped_nodes` is empty and `lossy_pin_defaults` is empty, so every node in the hardest graph the builder has produced maps back to a builder node type. The histogram: Operator 72, CallFunction 46, VariableGet 24, VariableSet 18, Branch 13, PrintString 12, CustomEvent 4, InputKey 3, Cast 2, Sequence 2, BeginPlay 1, Tick 1 (2026-08-01) |
| Inspection does not write, and proves it | Six inspection calls in one editor session against two Blueprints. `package_dirty_before` and `package_dirty_after` were `false` on every call; `transaction_id` was `""` on every response and `changed_assets` empty. Across the whole editor log: `LogSavePackage` 0 lines, Blueprint compile 0 lines, `BuildBlueprintFromJSON` 0 lines, `MCP PuerTS: <tool>` transaction descriptions 0 lines, and 30 `LogBlueprintInspector` reader lines, which is exactly 6 calls times 5 readers and nothing else. `BP_ProbeConn.uasset`, `BP_ProbeDoor.uasset` and `BP_StaminaCharacter.uasset` all held their byte size, their mtime to the nanosecond and their SHA-256 across the run (2026-08-01) |
| Limitation 32's variable accumulation, measured | The inspector put a number on it without touching anything: `BP_StaminaCharacter` carries **37 member variables** where the current spec declares 23. The extra 14 are the previous session's set, which a rerun cannot remove because the variable pass is additive. Recorded here because it is the first time the drift has been counted rather than described (2026-08-01) |
| Property validate-before-mutate | Eight rejected specs against the unused path `/Game/MCPGenerated/BP_ProbeProps`, each naming component, property, and reason: unknown property name, unloadable asset path, asset of the wrong class, wrong class inside a material array (`element 0: ... is a StaticMesh, but the property holds a MaterialInterface`), a string where an array belongs, a string where a struct belongs, and an out-of-range or misspelled enumerator (`expects a EComponentMobility enumerator: Static=0, Stationary=1, Movable=2`). `find_assets` for that name returned `count 0` afterwards (2026-08-01) |

| Independent Widget Blueprint inspection | `puerts_widget_inspect` closes the last builder/inspector asymmetry: `blueprint_build`/`graph_inspect` and `behavior_tree_build`/`behavior_tree_inspect` had one, widget did not, so its only read-back was the builder's own report. Against `/Game/MCPGenerated/WBP_AtomicityGood` (CanvasPanel root, TextBlock `Title` at position 40,24 size 320,40, ProgressBar `Bar`): payload **7044 bytes**, structure hash `4C33675FF120DB53FB2E193B4B161AF3A1AD12A2`. Read-only measured rather than asserted: `transaction_id ""`, `package_dirty_before`/`after` both false, `changed_assets` empty, and the asset's SHA-256 and mtime unchanged across the reads. Two reads produced identical canonical JSON and the same structure hash. The independent read agreed with the build's own report on widget count, root name and class, child order (`["Title","Bar"]` with `child_index` matching array order), and slot geometry read off `UCanvasPanelSlot::LayoutData` rather than echoed: offsets left 40, top 24, right 320, bottom 40, plus anchors, alignment and z-order. A rerun of the same spec produced the **same structure hash**, and after an editor restart the cold-loaded asset reproduced that hash, the same widget count, and a byte-identical file (2026-08-02) |
| Widget inspection rejects cleanly | A missing path, a `/Script/UMG` path outside the two content roots, and a Blueprint asset passed to the widget inspector each return `success false`; the wrong-class case names it (`is a Blueprint, not a WidgetBlueprint`). Identity is `derived` like the BT inspector, because UE4.27 UMG widgets carry no GUID: a widget is addressed by `parent/childIndex:Class:Name`, so a rename or reorder is deliberately a different identity. Named-slot content is walked through `INamedSlotInterface`, which panel-child traversal cannot reach (2026-08-02) |

| `remove_unlisted.variables` verification status | **live_verified as of 2026-08-02.** Promoted only after the full induced-failure proof passed, which took four sessions and two ruled-out approaches. It is a sub-capability of `puerts_blueprint_build` (already live_verified) and has no separate metadata key, so the status is recorded here rather than by inventing an entry `check:inventory` would reject. Evidence: `Scripts/bp-remove-unlisted-acceptance.mjs` warm and cold, `docs/evidence/bp-remove-unlisted-*.json`. Everything the promotion rests on: convergence downward and upward, protection by ownership stamp, blocked referenced removals, forced removal with node deletion reported, plan_only read-only, and a failing build that leaves the Blueprint byte-identical to how it found it - variables, reference nodes, untouched graph nodes, file hash and package dirty state |
| Blueprint variable downward convergence (`remove_unlisted`) | Opt-in, off by default, variables scope only. Ownership is an explicit stamp, not a heuristic: `blueprint_build` writes `MCPManaged=1` metadata on every variable it declares, and removal only ever considers stamped variables, so inherited, native C++, engine-generated and human-authored variables are protected **by construction** rather than by an exclusion list, and a Blueprint authored before this change has nothing removable until a build declares its managed set. Live on `BP_ConvergeProbe` (six managed variables, graph nodes referencing two): `plan_only` returned `variables_to_remove ["DropAlsoPlain","DropPlain"]`, `blocked_removals` naming `DropReferenced` with its reference locations, and left inspection byte-identical. An unforced apply removed exactly the two unreferenced ones and left the referenced one alive with no node deleted; `force_remove_referenced` then removed it and reported every deleted node. `graph_inspect` independently confirmed the final set is exactly `["KeptA","KeptB","KeptC"]`, that `KeptA`'s reference node survived, and that BeginPlay/PrintString were untouched. Rerun removes nothing and the inspected variable set is byte-stable; after a restart the cold-loaded asset is still converged (2026-08-02) |
| Unsupported `remove_unlisted` scopes are rejected, never ignored | `components`, `functions`, `macros`, `graph_nodes` and `interfaces` set `true` each return `unsupported_scope` naming the scope; set `false` they are accepted. Silently accepting them would read as a promise to prune and quietly not do it, which is worse than refusing. An unknown scope key is also rejected (2026-08-02) |
| Defect 0 reproduced independently, on `VariableGet` | The builder's config key is `varName`; a spec using `params.variable` makes the factory return null while the build still reports `node_count 2` and `node_types ["BeginPlay","VariableGet"]`, and `graph_inspect` sees only `["BeginPlay"]`. Found while writing the `remove_unlisted` fixture: every reference assertion in it passed **vacuously** because the reference nodes did not exist. This is the same phantom-counting defect 0 records for `Cast`, now confirmed on a second node type and caught only by comparing the build's own report against the independent inspector - which is the argument for builder/inspector parity in one line (2026-08-02) |

| Truthful Blueprint build reporting | Defect 0 closed for counting. `node_count` came from `RequestedNodeTypes.Num()` and `NodeMap.Add(NodeId, SpawnedNode)` ran **even when the factory returned null**, so a refused node stayed in the count: a build reported `node_count 2` while `graph_inspect` saw one node. Now the builder reports `OutCreatedNodes`/`OutFailedNodes`, skips null nodes and nodes created in a graph other than the one requested, and the command reports `requested_node_count` / `created_node_count` / `failed_node_count` / `failed_nodes` and the connection triple, on the success **and** failure payloads. A refused node now fails the whole build: partial graph creation is not a success mode. Live: `VariableGet` with `variable` instead of `varName` returns `success false` naming the refused node and its supplied parameters, `created_node_count` excludes it, and the gate `created_node_count == independently inspected node_count` holds on the valid graph (4), the rerun (4) and MultiGate (2). `Cast` with an unresolvable target class behaves the same. No failing case left a dirty package, a file change, a source-control entry or a save prompt. `Scripts/bp-truthful-report-acceptance.mjs`, 27 of 28 checks (2026-08-02) |

## Defects and limitations (Phase L queue)

0j. **RESOLVED 2026-08-02, and every hypothesis about it was wrong.** The node
   was landing in the live graph the whole time. The diagnostic that settled it
   was the ledger's own output: `reference_nodes_removed
   ["EventGraph.K2Node_VariableGet_6"]` against `reference_nodes_restored
   ["EventGraph.K2Node_VariableGet_7"]`. Graph lookup, insertion, pin
   allocation and compile all worked; the recreated node is a NEW UObject and
   gets a new name, and the verification compared **object names**. The
   restore was correct and the report called it a failure.

   A false negative in a rollback report is not harmless: it makes a working
   restore look like data loss, which is the same class of error as the false
   positive 0g was fixed to remove, pointing the other way.

   Comparison is now by structural identity - graph name, node class,
   referenced variable, node position - with object names deliberately absent
   from it. `rollback_succeeded` is true only after that comparison passes, and
   the sabotage direction still works: a node that genuinely fails to return
   produces a mismatch and a false flag.

   Worth keeping: none of the eight hypotheses in the session goal (stale graph
   pointer, EventGraph reconstruction, node not added to Graph->Nodes, wrong
   outer, ordering, recompile removing it, missing AllocateDefaultPins) was
   the cause. Reading the evidence that already existed beat testing any of
   them.

0l. **FIXED 2026-08-02. Client discovery ended in a fallback, so "I do not know
   which editor" and "use whichever editor owns the default pipe" were the same
   code path.** `resolvePipeName` finished with
   `return "\\\\.\\pipe\\UE427PuerTSMCP"`. A missing or stale `pipe.txt` did not
   fail; it silently sent every request to whatever owned the compiled-in
   default name. With one editor open that reads as a convenience. With two it
   is a command authoring assets in a project nobody asked it to touch, and
   reporting success.

   `pipe.txt` could not have fixed it either. It carried a pipe name and
   nothing else, which is enough to reach AN editor and says nothing about
   WHICH one answered.

   Replaced by `Saved/MCPPuerTSBridge/session.json`, schema version 1, written
   by staging a temp file and moving it over the target so a reader can never
   observe a partial manifest. It carries the session id, a session nonce, the
   editor PID, the OS process creation time, project and uproject paths, the
   pipe name, the bridge commit and install-manifest hash, creation time, a
   5-second heartbeat and a shutdown state.

   The two halves are separate on purpose:

   - The **nonce** travels with every request and `AcceptCommand` refuses a
     mismatch, at the C++ safety boundary, before anything runs. It is
     regenerated on every editor start, so a client holding a previous
     session's manifest is refused rather than silently retargeted.
   - The **identity stamp** rides on every response, including rejections,
     from `BuildBaseResponse`. The client compares it to what it addressed and
     refuses the reply on a mismatch. Both directions are needed: the nonce
     stops a request reaching the wrong editor, the stamp stops an answer
     arriving from one.

   PID alone cannot establish liveness, because Windows reuses process ids, so
   the manifest records the OS process creation time from `GetProcessTimes` and
   the live editor reports its own. The heartbeat is deliberately NOT the
   liveness test: it runs on the game thread, so a long Blueprint compile stalls
   it while the editor is perfectly alive. Liveness is the PID; the heartbeat is
   context for the error.

   Every refusal is a structured code, surfaced as `session_error_code` on the
   failure envelope so a caller branches on it instead of matching English:
   `session_missing`, `session_unreadable`, `session_schema_unsupported`,
   `session_shut_down`, `session_stale`, `session_not_selected`,
   `session_project_mismatch`, `session_identity_absent`,
   `session_identity_mismatch`.

   Proven live with two UE4.27 editors open at once, `BridgeInstallTest` and
   `Tests\UE427PuerTSMCP`, in `Scripts/session-isolation-acceptance.mjs` across
   four phases. Distinct ids, pids, pipes, nonces and projects; concurrent
   diagnostics from different processes; 12 interleaved read-only requests with
   zero crossed replies; a probe actor spawned in each world absent from the
   other; a forged nonce refused BY THE EDITOR in its own words while the client
   independently refused the reply; a stale advertisement naming a dead pid
   refused before connecting; closing A leaving B serving while A is refused
   with `session_missing` and specifically NOT falling through to B; a restarted
   A issuing a new session id for the same project, with a client pinned to the
   old id refused; and no advertisement surviving either shutdown. `smoke:inspect`
   and `smoke:bt` were run against explicitly selected targets with both editors
   up and reached the right one. Evidence:
   `docs/evidence/session-isolation-{both,a-closed,a-restarted,none}.json`.

   Worth keeping: the acceptance failed twice on its own assertions before it
   passed, both times because the behaviour was right and the assertion was
   reading prose instead of a code. That is what produced `session_error_code`.

0k. **FIXED 2026-08-02 by deferral, after the restoration approach was ruled
   out by an editor crash.** The fix is not to undo the destruction, it is not
   to destroy: `BuildBlueprintFromJSONWithReport` no longer clears the existing
   graph before spawning. The replacement is built ALONGSIDE the old nodes -
   connections resolve only against the new ones, because `NodeMap` is keyed by
   spec id and the old nodes were never in it - and at the end exactly one set
   is deleted: the old graph on success, the new nodes on failure. A failing
   build is therefore non-destructive by construction, and there is nothing for
   rollback to repair.

   Same insight as the removal ledger, one level up: do not destroy until the
   thing that might fail has succeeded.

   Proven: the pre-request graph has two `VariableGet` nodes, one reading the
   removed variable (in the ledger) and one reading a variable that is never
   removed (`getA`, in no ledger). After a failing forced removal, both return -
   `graph_inspect` reports 4 nodes against 4, an identical node-type multiset,
   the variable set byte-identical, the asset file SHA-256 unchanged, no dirty
   package. The truthful-report acceptance, `smoke:inspect`, `smoke:bt` and
   `npm run verify` all still pass, so successful convergence is unchanged.

   **Sibling defect, found and fixed 2026-08-02 in the same place.** The abort
   branch was guarded by `DeferredNodesToRemove.Num() > 0`, which made
   non-destruction a property of `clear_existing_graph` rather than a property
   of failing. An additive build (`clear_existing_graph` false) has no deferred
   nodes, so the branch was skipped entirely and a failing additive build left
   its half-built nodes wired into the caller's graph. The guard is gone: a
   failed build discards what it made, whatever mode it ran in. Asserted
   directly - after a failing additive build the canonical graph hash is
   `f151d724...`, identical to the one before it, and the asset file SHA-256 is
   unchanged.

   **What the proof actually covers, and the one thing it cannot claim.** Node
   types and counts were the whole comparison until now, and they are the
   weakest thing a graph has: they match while every pin default and every link
   is gone. The fixture now carries node positions, a pin default and a data
   link alongside the exec link, and the comparison is the full
   `graph_inspect` payload with `include_pins`, split by population:

   - The three nodes the failing build never touched are the same UObjects and
     are compared byte for byte, `id`, `node_guid` and every `pin_id` included.
     Nothing about them moves.
   - The one node the ledger removed and recreated is a new UObject. Its `id`,
     `node_guid` and pin ids are regenerated by construction - measured as
     `K2Node_VariableGet_7` becoming `_8` - so it is compared by structural
     identity: type, params, position.

   A single whole-payload hash cannot express that split, which is why
   `pre_graph_hash` and `post_failure_graph_hash` differ in the evidence
   (`ce9f7f11...` against `f151d724...`) while `original_graph_preserved` is
   true and `graph_restoration_mismatches` is empty. Asserting byte-identity
   across the recreated node instead would re-add exactly the false negative
   0j was fixed to remove. Evidence:
   `docs/evidence/bp-remove-unlisted-record.json`, key `failed_build_graph`.

   **Original report, kept for the ruled-out approach.**

0k-original. **A failing build's graph rebuild is not undone by the removal rollback**
   (2026-08-02, scoped out of 0j). `clear_existing_graph` defaults true, so a
   failing request replaces the whole event graph from its own spec before the
   failure is detected. Nodes that the ledger never captured - because their
   variables were not being removed - are dropped by that rebuild and not
   restored. Measured: a pre-request graph with `getA` (reading `KeptA`, which
   survives) and `getVictim` (reading the removed variable) comes back from
   rollback with `getVictim` only.

   This is the failing build replacing the graph, not the removal rollback
   losing it, and the two should not be conflated.

   **A whole-graph snapshot was attempted and REVERTED 2026-08-02. It crashes
   the editor.** The shape looked right: clone the live event graph with
   `FEdGraphUtilities::CloneGraph` before any removal, and on failure clear the
   damaged graph and move a fresh clone's nodes back into it. It compiled, and
   the first live run took the editor down with an access violation:

   ```
   Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0xffffffffffffffff
   UE4Editor_CoreUObject!StaticDuplicateObjectEx()  UObjectGlobals.cpp:2019
   UE4Editor_UnrealEd!FEdGraphUtilities::CloneGraph()  EdGraphUtilities.cpp:254
   UE4Editor_MCPBridgePuerTS!<lambda>::operator()()  MCPPuerTSBridgeBlueprint.cpp:1420
   UE4Editor_MCPBridgePuerTS!UMCPPuerTSBridgeService::BuildBlueprintJson()
   ```

   The "asset came back with no variables" symptom recorded first was the same
   event seen from the client side: the editor was already dead. `CloneGraph`
   on a live Blueprint event graph, from inside a bridge command on the game
   thread, is not safe as written - `StaticDuplicateObjectEx` dereferenced a
   bad pointer duplicating the graph. Reverted to the last good commit rather
   than shipped. The
   likely reason is that moving cloned nodes into a live `UEdGraph` by
   `Rename` plus `AddNode` bypasses whatever the Blueprint needs to keep its
   ubergraph and skeleton consistent; a correct version probably has to go
   through `FEdGraphUtilities::CloneAndMergeGraphIn` with a real
   `FCompilerResultsLog`, or avoid the destruction entirely by deferring
   `clear_existing_graph` until the build has succeeded.

   Deferral is the better shape and is the same insight that fixed the removal
   itself: do not destroy until the thing that might fail has succeeded. Not
   attempted here.

   Until then the boundary is honest and narrow: a failed `remove_unlisted`
   restores every variable it removed and every reference node it removed, both
   verified; it does not restore graph nodes the failing build's own
   `clear_existing_graph` replacement destroyed. `remove_unlisted.variables`
   therefore stays below live_verified.

0j-original. **Reference-node restoration is captured and attempted but does not land in
   the live graph** (2026-08-02, superseded above). The removal ledger now captures each deleted
   reference node whole - class, name, position, comment, graph, variable
   reference, pin defaults and every pin link - and recreates it on the failure
   path from the ledger rather than from the incoming spec, which is the right
   shape: a request that removes a variable normally stops declaring the nodes
   that read it, so rebuilding from the spec would restore the variable and
   silently drop its graph.

   It does not yet work. The ledger reports `reference_nodes_captured 1` and
   `reference_nodes_recreated 1`, but `graph_inspect` afterwards finds one
   `VariableGet` where the pre-request graph had two, and the reference-location
   comparison reports a mismatch. Two candidate causes, neither confirmed: the
   graph resolved by name may not be the live EventGraph object the failing
   build rebuilt, and the recreated node is given a fresh object name
   (`NAME_None`), so the location string it produces cannot match the captured
   one even when the node is present.

   **The important half works.** `rollback_succeeded` reports **false** for
   exactly this reason, so the system refuses to claim a restoration it cannot
   verify. That is the property 0g was fixed to get: the dangerous failure was
   never the incomplete restore, it was reporting success while data was gone.
   Variable restoration itself is complete and verified (0g), so a failed build
   loses no variable; what is not yet restored is the graph node that read it,
   and that is reported rather than hidden.

   `remove_unlisted.variables` is therefore NOT promoted to live_verified.



0i. **RESOLVED 2026-08-02. Unknown authoring keys are now rejected.** Kept for
   the normalisation, which is the non-obvious part. A node type's accepted
   parameters are its RoutingKeys plus its own pin names, but the first attempt
   to reject on that set **regressed valid specs**: RoutingKeys are declared
   snake_case (`var_name`, `target_class`) while the registry factories read
   camelCase config (`varName`, `targetClass`), converted by
   `RegistryConfigJson`, so the valid four-node fixture failed with `varName`
   reported as unknown. Neither spelling alone is the table.

   `NormalizeParamKey` folds out underscores and case, so `var_name` and
   `varName` are one accepted name while a genuinely wrong key like `variable`
   still matches nothing. Rejection is fatal on that normalised set: the node
   is removed from the graph, reported as `unknown_parameter` with the offending
   keys and the accepted list, and the build fails.

0h. **WITHDRAWN 2026-08-02: this was a mis-diagnosis, and the behaviour is
   correct.** A connection naming an unknown node id was read as a reporting
   gap because `failed_connection_count` and `unresolved_connections` were both
   empty. They are empty because the spec is rejected by
   validate-before-mutate (`MCPPuerTSBridgeBlueprint.cpp`, the `NodeIds`
   check) BEFORE anything is created, returning a named error and no graph
   payload. Reporting counts for a graph that was never built would be the same
   lie in the other direction. The acceptance now asserts exactly that: the
   error names the unknown id, and `data.graph` is absent.



0g. **FIXED 2026-08-02 by a removal ledger, and the fix is proven by the same
   test that exposed the bug.** Kept in full because the false
   `rollback_succeeded` is the part worth remembering.

   **Fix.** A ledger is captured BEFORE any deletion: the whole
   `FBPVariableDescription` (which carries name, pin type, default value,
   category, metadata, replication settings, RepNotify function and VarGuid in
   one struct), the managed-ownership marker, and the variable's graph
   reference locations. On the failure path the transaction is cancelled, the
   ledger is replayed by re-adding each captured description and re-stamping
   its marker, the Blueprint is marked structurally modified and recompiled,
   and only THEN does the asset-creation boundary run - so its package
   dirty-state restore is the final word and the restore's own recompile does
   not leave the package dirty.

   **`rollback_succeeded` is now earned, not asserted.** It is computed by
   re-reading the asset and comparing each restored variable's pin type,
   default value, category and ownership marker against the ledger, plus every
   reference location; any difference becomes a `restoration_mismatch` and the
   flag goes false. The asset boundary's own verdict is ANDed with the
   ledger's, so `cleanup.rollback_succeeded` can no longer be true while a
   removal survived. The response carries `removal_ledger_count`,
   `variables_removed`, `variables_restored`, `reference_nodes_removed`,
   `reference_nodes_restored` and `restoration_mismatches`.

   **Proof.** `Scripts/bp-remove-unlisted-acceptance.mjs` step 8, the test that
   previously failed: a Blueprint with `KeptA/KeptB/KeptC/RollbackVictim`, a
   build declaring only the three with `remove_unlisted.variables` and
   `force_remove_referenced` plus a deliberately unresolvable connection. The
   build fails, and `RollbackVictim` is **restored** - the variable set is
   byte-identical to before the failed build, the asset file's SHA-256 is
   unchanged, no package is dirty, no cleanup errors, and a restart preserves
   the state. The whole acceptance passes warm and cold, `npm run smoke:inspect`
   and `npm run verify` pass.

   **Original report, which stood for one commit.** `blueprint_build` with `remove_unlisted.variables`
   removes the variables, then a later failure in the same command - an
   unresolved graph connection, or in principle a compile error - exits through
   `FailRolledBack`, which cancels the transaction and runs the asset rollback
   boundary. **The removed variables stay removed.** Measured: a Blueprint with
   `KeptA/KeptB/KeptC/RollbackVictim`, a failing build declaring only the three,
   `success false` - and `RollbackVictim` gone afterwards, with the asset
   otherwise unchanged.

   Two independent causes, both mine:

   - `FScopedTransaction::Cancel()` does not restore
     `FBlueprintEditorUtils::RemoveMemberVariable`. The removal was assumed to
     be transactional because it calls `Modify()`; it is not recovered by
     cancelling the scoped transaction in this path.
   - `cleanup.rollback_succeeded` reports **true** anyway, because
     `FBridgeAssetRollback` tracks created assets and package dirty state only.
     It never knew a member was removed, so it correctly reports success for
     what it tracked while the caller reads it as "nothing was lost". A
     rollback report that cannot see the destructive half of the operation is
     worse than no report.

   Deleted reference nodes under `force_remove_referenced` are not restored
   either, for the same reason.

   **Do not rely on `remove_unlisted` surviving a failed build.** It is safe on
   success and safe when the build fails BEFORE the removal pass (validation,
   unsupported scope, blocked removal); the exposure is a failure after
   removal, which the unresolved-connection lever reaches today.

   Fix, not yet implemented: capture each `FBPVariableDescription` (and the
   removed nodes) before deletion and re-add them explicitly on the failure
   path rather than trusting the transaction, then mark the Blueprint
   structurally modified and recompile so the skeleton carries them again. The
   rollback boundary should also grow a removal ledger so
   `rollback_succeeded` cannot be true while a removal survived.

   Related: **a compile failure is not reachable from the current spec
   vocabulary.** Two probes tried to force one - a node reading a
   just-removed variable, and a `Cast` with nothing wired to its `Object` pin -
   and both compiled `UpToDate`. The validator, the node factories and the
   connection resolver catch everything first. So the compile-specific branch
   of the failure path cannot be exercised from outside today; the
   unresolved-connection lever reaches the same `FailRolledBack`.



0. **Cast with a short target_class silently spawns no node, and the build
   report counts the phantom** (found 2026-08-01 by the graph_inspect
   acceptance). `{"type": "Cast", "params": {"target_class":
   "StaticMeshActor"}}` produces a build that reports success with the Cast
   in `node_count` and `node_types`, while no node exists in the graph; the
   full path `/Script/Engine.StaticMeshActor` spawns it correctly. Two
   defects: the registry Cast factory does not resolve reflected short names
   even though the variable type resolver does, and `node_count`/`node_types`
   are computed from the spec entries processed rather than the nodes actually
   added, so a factory returning null is invisible unless a connection touches
   the phantom (`node 'x' spawned no node`). Proven by
   `Scripts/graph-inspect-acceptance.mjs`, whose build-vs-read node-count
   assertion now fails loudly on any silent drop. Both fixes are builder-side
   graph mutation work and are deliberately not part of the inspector change.

0c. **FIXED 2026-08-01 for `behavior_tree_build`. Editor exit persisted
   failed-build transients.** Kept in full because the reproduction is the
   template for the other builders.

   **Fix.** `UMCPPuerTSBridgeService::BuildBehaviorTreeJson` now runs inside a
   rollback boundary (`Private/MCPBridgeAssetRollback.h`). The assets still
   have to be created before the answer is known - `FBTValidator` can only run
   against a real `UBehaviorTree` - but any response carrying errors now
   cancels the transaction and undoes the creation. The two lines that caused
   the leak were an unconditional `Tree->MarkPackageDirty()` /
   `Blackboard->MarkPackageDirty()` that ran even when `Errors.Num() > 0`.

   On failure the boundary cancels the `FScopedTransaction` **first** (its undo
   records reference objects about to be destroyed, so they must be replayed
   while those are live), then per created asset calls
   `AssetRegistry.AssetDeleted`, clears `RF_Public | RF_Standalone`, sets
   `RF_Transient`, renames it into the transient package, and marks it pending
   kill; restores each package's recorded dirty flag; and deletes any file that
   appeared and did not exist beforehand. It then re-checks all three and
   reports anything still dirty, on disk, or in the registry as a
   `cleanup_error` rather than assuming the cleanup worked. Every response,
   success or failure, carries a `cleanup` object:
   `rollback_attempted`, `rollback_succeeded`, `created_assets`,
   `removed_assets`, `dirty_packages_before`, `dirty_packages_after`,
   `files_created`, `files_removed`, `source_control_before`,
   `source_control_after`, `cleanup_errors`. Source control is read with
   `EStateCacheUsage::Use` only - asking the server would itself be the
   operation this must not perform.

   A failed save after a successful build takes the same exit: a half-written
   asset is the same hazard as a half-built one.

   **Verified** by `Scripts/bt-failure-atomicity.mjs` (fixture: the existing
   invalid-node request) and `docs/evidence/bt-failure-atomicity-*.json`. Three
   failed builds in one editor session: registry count 0 after each, no file on
   disk, `dirty_packages_after` empty, no cleanup errors, `p4 opened`
   unchanged, and the inspector - an independent reader - finds no artifact.
   The editor then closed with **no Save Content prompt** in 2.72 s and nothing
   on disk, and a restart confirmed the probe still absent. The BT acceptance's
   cold phase, whose `the failed build wrote nothing to disk (filesystem
   check)` assertion had been failing, now passes end to end, and the close
   after it - whose last step is the invalid-node build - produced no prompt
   and no file.

   **`blueprint_build` converted 2026-08-01, same boundary, no second
   implementation.** The leak was at the command boundary only: the builder
   mutates an asset it is handed, while `BuildBlueprintJson` owns the
   `CreatePackage` / `FKismetEditorUtilities::CreateBlueprint` /
   `AssetRegistry.AssetCreated` sequence and the unconditional
   `Blueprint->MarkPackageDirty()`.

   The fixture had to be chosen with care. `blueprint_build` already validates
   node types, duplicate ids, unknown node ids, component classes and parent
   classes before it creates anything (see "Blueprint build
   validate-before-mutate" above), so none of those specs reach the leak. The
   failures that do are the ones only building can find: an unresolved
   connection, a component that will not attach, a compile error. The probe is
   the cheapest of them, a connection endpoint naming a pin role that does not
   exist on a known node (`begin.nosuchpin`), which is the limitation-20 class
   of failure.

   Measured before the change, `Scripts/bp-failure-atomicity.mjs --observe`:
   the request failed and saved nothing, but the Blueprint was in the Asset
   Registry after each of three attempts and `graph_inspect` found it; on close
   the Save Content prompt appeared at 0.8 s and `BP_AtomicityProbe.uasset` was
   on disk afterwards, with "Don't Save" answered. After the change all three
   attempts leave registry count 0, nothing on disk, `dirty_packages_after`
   empty and no cleanup errors; the editor closed with **no prompt** in 3.27 s;
   a restart confirmed the probe absent. In the same session a successful build
   still compiled `UpToDate`, saved, reported `rollback_attempted false`, and
   survived the restart intact, and `npm run smoke:inspect` passes.

   **`widget_build` converted 2026-08-02, closing the shared pattern.** Widget
   raises the stakes rather than lowering them: `BuildWidgetFromJSON` creates,
   compiles **and saves** inside the library, so on the create path the
   `.uasset` is on disk before the command can judge the compile status. A
   failure after that point would leave a saved file, not merely a dirty
   package, which is why the boundary's file deletion is load-bearing here.
   The create path also gets a `TrackIfOurs` adopter: the command has no
   pointer to the asset until it loads it back, so a library failure partway
   through would otherwise leave an untracked, registered asset behind.

   **What this does and does not prove.** Every widget failure reachable from
   the current spec vocabulary is rejected BEFORE mutation (the eight in
   "Widget build validate-before-mutate" above), so unlike the Behavior Tree
   and Blueprint conversions there is **no pre-fix leak to demonstrate**. The
   boundary here is closing an exposure by construction, not a measured defect.
   `Scripts/wbp-failure-atomicity.mjs` proves what can be proven: four rejected
   specs each leave registry count 0 and no file, `p4 opened` is unchanged, a
   successful build compiles `UpToDate` with three widgets and
   `rollback_attempted false`, a rerun converges (`created false`, still one
   asset), the editor closes with no Save Content prompt in 2.75 s, and a
   restart finds the rejects absent and the good widget intact.

   A test bug worth recording, because it briefly looked like a regression: the
   first version of that script passed a bare tree instead of `{"root": ...}`.
   The client schema rejected all six calls before they reached the editor, so
   the four "rejected" specs passed for the wrong reason and the successful
   build failed. A cleanliness assertion that never reaches the code under test
   passes vacuously; the fixture has to be shown to do the thing it claims to
   reject.

   `widget_build` gained its inspector on 2026-08-02; see the Working row below.

   **Original report** (2026-08-01, by the BT live acceptance). A failed
   `behavior_tree_build` on a fresh path correctly
   saves nothing (proven by the acceptance's filesystem check), but the
   in-memory dirty transient survives, and closing the editor auto-saves it
   to disk AND opens it for `p4 add` - `BT_AcceptanceBadType(.uasset,_BB)`
   hit disk at 16:24:12, seconds after the close request. Every probe asset
   from earlier sessions (`BP_CastWireProbe`, `BP_GateProbe`, ...) shows the
   same saved-plus-opened-for-add pattern, so "unsaved probes vanish on
   restart" was wrong.

   **`Don't Save` does not stop it** (2026-08-01, clean post-reboot
   validation). Sharper reproduction: clear
   `Content/MCPGenerated/BT_AcceptanceBadType(.uasset,_BB)`, run
   `behavior-tree-acceptance.mjs --phase=cold` so its last step leaves the
   failed transient in memory, then close the editor normally and answer the
   `Save Content` prompt `Don't Save`. Both files are on disk again, stamped
   the same second as the close (observed 20:22:13). The prompt's answer is
   not what writes them; the unattended save-on-exit flow is. Consequence for
   the acceptance script: its
   `the failed build wrote nothing to disk (filesystem check)` assertion fails
   on any run that follows a close, because the previous close recreated the
   files. The other twelve cold-phase assertions pass, including
   `an unknown node type is rejected with no save`, so the build itself is
   correct and only the exit flow is at fault.

   **This is NOT the teardown hang.** That earlier claim is withdrawn: see
   defect 0f, where a close with nothing dirty, in a project outside the p4
   workspace, hung identically, and where answering `Don't Save` still left an
   unkillable process. The two are independent, and the log tells them apart -
   a 0c stall has no `LogExit` and no `MCPBridge lifecycle: shutdown begin`
   line at all, because it is a modal waiting for a human, while an 0f hang
   has the whole teardown logged and then silence. Post-0f-fix, a 0c stall
   ends the moment the prompt is answered: measured at 105.9 s parked on the
   modal, then 3.74 s to exit.

   Real fixes are builder-side (purge the transient package when a create-path
   build fails) and editor-side (suppress source-control modals for
   unattended runs); both remain out of scope and tracked here.

0d. **BT editor-graph nodes have no NodeGuid** (found 2026-08-01 on reload).
   Loading a built Behavior Tree logs "missing NodeGuid, this can cause
   deterministic cooking issues please resave package" for every editor graph
   node FBTEditorGraphSync created. Builder-side fix: assign
   `FGuid::NewGuid()` during sync, then resave. Cosmetic in the editor,
   real for deterministic cooking.

0e. **The BT builder silently drops unknown param keys** (found 2026-08-01 by
   the first use of `puerts_behavior_tree_inspect`). Node params use
   snake_case (`blackboard_key`, `wait_time`, `acceptable_radius`); a spec
   that writes `BlackboardKey` or `WaitTime` builds "successfully" with every
   key selector defaulting to SelfActor and every value at its class default.
   `FBTNodeRegistry::ApplyParams` looks up known keys and ignores the rest
   with no warning - the K3 soft-warn pattern (monolith) is the known fix.
   Builder-side work; the tool description now documents the real key names
   and points at the inspector for verification.

0f. **FIXED 2026-08-01. Editor teardown hang: MCPBridgePIEAgent cleaned up too
   late.** Kept in full, including the wrong first answer, because the earlier
   entries in this file blamed the wrong component for three sessions.

   **Symptom.** A graceful editor close destroys the window and sets the
   process exit code, and then the process never finishes exiting. It keeps
   its named pipe listening, keeps `Saved/Logs/<Project>.log` open (which is
   why a later editor writes `<Project>_2.log`), and keeps every
   `Plugins/MCPBridge/Binaries/Win64/*.dll` locked, so the project cannot be
   rebuilt. `taskkill /PID <id> /F /T` answers `There is no running instance
   of the task` and `Stop-Process -Force` silently does nothing. Only a
   reboot clears it.

   **Where it stopped.** Every graceful-close log, across both projects, ends
   on exactly the same line:

   ```
   LogExit: Object subsystem successfully closed.
   ```

   That is `StaticExit` (`Obj.cpp:4588`), bound to `FCoreDelegates::OnExit`
   and broadcast from `AppPreExit` (`LaunchEngineLoop.cpp:5805`). Everything
   after it is log-silent, which is why the window went unexamined for so
   long. The one log that ever reached `Log file closed` got there through
   `FPlatformMisc::RequestExit(1)` after an assert, which skips
   `FEngineLoop::Exit()` entirely.

   **Root cause.** `FMCPBridgePIEAgentModule::StartupModule` creates a
   `UPIEAgentRuntime`, roots it, and calls `Initialize`, which registers two
   things that outlive the object unless explicitly removed
   (`PIEAgentRuntime.cpp:97-106`):

   - a core ticker bound to it with `FTickerDelegate::CreateUObject`
   - an `FOutputDevice` (`FPIEAgentLogSink`) handed to `GLog`, whose backing
     memory that same UObject owns through a `TUniquePtr`

   Both were removed only in `ShutdownModule`, which UE4.27 calls from
   `FModuleManager::UnloadModulesAtShutdown` (`LaunchEngineLoop.cpp:4294`) -
   **after** `StaticExit` has destroyed every UObject. So from `StaticExit`
   onward, `GLog` holds a freed log sink whose `Owner` is a destroyed
   `UPIEAgentRuntime`, and the core ticker holds a delegate to the same dead
   object. The editor stops on the very line `StaticExit` emits.

   **The measurement that found it.** `Scripts/editor-shutdown-acceptance.ps1`
   builds, launches, optionally exercises the bridge, closes the window
   normally, waits for the process to disappear, and builds again. Before the
   fix:

   | Case | MCPBridge | Puerts | FJsEnv created | Close |
   |---|---|---|---|---|
   | `bare` | off | off | no | 4.1 s |
   | `plugin-off` | off | **on** | no | 4.1 s |
   | `puerts-idle` | on, inert via `-MCPPuerTSBridgeDisabled` | on | **no** | never exits |
   | `bridge-idle` | on | on | yes | never exits |

   Puerts alone is innocent, and `FJsEnv` is irrelevant: `puerts-idle` never
   creates one and still hangs. Removing only `MCPBridgePIEAgent` from
   `MCPBridge.uplugin`, with the whole rest of the bridge running and its pipe
   up, closed in 4.1 s. That is the isolation.

   **A wrong answer worth recording.** The first fix moved the *PuerTS*
   teardown to `OnEnginePreExit`, on the theory that
   `FJsEnvImpl::~FJsEnvImpl` was blocking - `StopPolling` waits on a task it
   dispatched to the game thread (`JsEnvImpl.cpp:187`, `:322`), and
   `node::FreeEnvironment` must pump libuv until the `net.createServer` pipe
   handle that `bootstrap.ts` never closes goes away. The lifecycle logging
   added at the same time disproved it in one run: **the script environment
   released in 0.003 s** and the editor hung anyway. The evidence that had
   pointed at PuerTS - three exited editors whose pipes still accepted
   connections - was a consequence of the hang, not its cause. A process that
   never finishes exiting keeps every handle it owns, pipes included.

   Two measurement mistakes are recorded here because both produced confident
   wrong answers. First, the harness treated "a window titled Unreal Editor
   exists" and the bridge's own module-startup line as readiness; both fire
   during startup, so three early runs closed editors mid-initialisation and
   one of them made a bridge-free editor look like it hung. Readiness is now
   `LogLoad: (Engine Initialization) Total time:` plus a settle. Second, those
   mid-startup closes were ignored rather than obeyed, leaving fully loaded
   editors running that then owned the configured pipe name, so a later
   read-only probe connected to the wrong editor and blocked forever. The
   probe now has a bounded read.

   **Fix.** Release from `FCoreDelegates::OnEnginePreExit`, broadcast at the
   top of `UEngine::PreExit` (`UnrealEngine.cpp:1878`), reached from
   `FEngineLoop::Exit` line 4208 - while the object system is up and the
   ticker and `GLog` are still valid. `ShutdownModule` keeps the same
   idempotent release as a fallback for a module unloaded on its own (hot
   reload, plugin disable), where `OnEnginePreExit` never fires. Applied to
   `MCPBridgePIEAgent`, which is the one that hung, and to `MCPBridgePuerTS`,
   which was releasing `FJsEnv` and calling `Service->RemoveFromRoot()` after
   `StaticExit` had closed the object subsystem - latent rather than fatal,
   but wrong for the same reason. `UMCPPuerTSBridgeService::Shutdown` now also
   deletes `Saved/MCPPuerTSBridge/pipe.txt`, so a closed editor stops
   advertising its pipe to the next client.

   **Result, 2026-08-01, BridgeInstallTest.** Five consecutive
   `bridge-idle` iterations: close 4.1 s each, `process_exited` true, zero
   locked plugin DLLs, no surviving advertisement, build ok after every one.
   `read-only` (a `diagnostic` command completed over the pipe) and `bt-smoke`
   (`npm run smoke:bt` passed) both closed in 4.1 s. `puerts-idle`, the
   smallest case that used to reproduce, now closes in 4.1 s.

   The four pipes still listed on this machine belong to editors that hung
   *before* the fix; they survive until reboot. No editor running the fix has
   left one, which is checked per iteration.

   **Verified on a clean rebooted machine, 2026-08-01.** Full record in
   `reports/shutdown-clean-validation-2026-08-01.md`. Starting from zero
   leftover processes, pipes and advertisements, with the 25 `*.zombielocked*`
   files removed and BridgeInstallTest's `PipeName` restored from the
   temporary `_fix1` suffix to the canonical `..._eb10ef4f`:

   - BridgeInstallTest built (28.7 s), launched, served a read-only
     `diagnostic` (`transaction_id ""`, so it did not transact), passed
     `npm run smoke:bt`, and closed in **4.1 s**.
   - UE427PuerTSMCP, the main test project and the source of two of the four
     original unkillable processes, built (21.9 s), launched, and closed in
     **3.78 s**.
   - Both logs run past `LogExit: Object subsystem successfully closed.` to
     `LogExit: Exiting.` and `Log file closed`. That is the whole point: no
     pre-fix graceful close ever got there.
   - Zero processes, zero pipes and zero advertisements survived either close.
   - The relink that used to fail with
     `LNK1104: cannot open file ...UE4Editor-MCPBridgePuerTS.dll` completed:
     `[3/4] UE4Editor-MCPBridgePuerTS.dll`, 18.5 s.
   - `npm run verify` exit 0: 13 suites, PuerTS pin
     (`Unreal_v1.0.9 @ 838ab762d830`, 1038 files), 206 tools frozen, smoke
     8/0/2 with the two documented skips.

   The binaries were confirmed to carry the fix by reading the built DLL's
   string table rather than trusting the build, because both test projects
   keep their own copy of the plugin and a bridge-repo edit is invisible to
   them until the installer runs or the files are copied. UE427PuerTSMCP's
   copy was still pre-fix and had to be synced first.

   Recovery for an editor already in this state: reboot, or bump
   `[MCPPuerTSBridge] PipeName` and relaunch, since pipe.txt discovery routes
   clients to the new editor automatically. A machine already carrying such
   processes can still build without rebooting: Windows refuses to overwrite
   their locked DLLs but does allow them to be RENAMED, which frees the path
   for the linker. The harness does this before its own build, never before
   the build it measures.

0b. **MultiGate ignores num_outputs** (found 2026-08-01 by the same
   acceptance). `{"type": "MultiGate", "params": {"num_outputs": 4}}` builds a
   MultiGate with the default 2 exec outputs and no warning; the identical key
   on Sequence is honored (probe: Sequence 4, MultiGate 2). Builder-side
   registry config work, same lane as defect 0.

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
   need project input settings or a delegate property to point at), and
   `CreateWidget`, which is registered but was not needed once a raw
   `CallFunction` on `WidgetBlueprintLibrary.Create` proved to compile
   (limitation 22). A loop today has to be written as a Delay chain or a Tick
   with a counter variable. The "no widget or audio authoring surface at all"
   this entry used to end with is wrong and was closed on 2026-08-02: audio
   needed no new vocabulary, and widgets have their own tool now. See the two
   Fixed sections below.
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
20. **FIXED 2026-08-02, Phase F4. Kept for the record.**
    **A graph connection that cannot be resolved is a log line, not an error.**
    `BuildBlueprintFromJSON` writes
    `BuildBlueprintFromJSON: Could not resolve pins for connection A -> B` to
    the editor log and carries on, and `blueprint_build` still answers
    `compile_status "UpToDate"`, `errors []`, `saved true`. The response's
    `connection_count` is the number of connections **requested**, not the
    number made. Repro, and how this was found: the first
    `BP_ProbeDoorV3` build wired `brSnd.exec -> playing.exec` and
    `playing.exec -> printS.exec` against a pure node, both were dropped, and
    the build reported complete success with `connection_count 34`; the fixed
    spec reports 33. A caller has to read `log_output` for
    `Could not resolve pins` to know its graph is whole. The cheapest fix is to
    return the unresolved connections in `errors`, which would also make the
    spec fail before it is saved.
21. **A const `BlueprintCallable` UFUNCTION is a pure K2 node with no exec
    pins.** UHT promotes a const BlueprintCallable function that returns a value
    to `FUNC_BlueprintPure`, so `UAudioComponent::IsPlaying`
    (`AudioComponent.h:505-506`) and `UWidget::GetCachedGeometry`
    (`Widget.h:696-697`) are declared `UFUNCTION(BlueprintCallable)` and still
    have no `exec`/`then`. Wiring exec to one of them hits limitation 20 and
    disappears. Read the declaration for `const`, not for the macro. Both were
    wired as pure nodes in the F3 graphs and worked.
22. `BlueprintInternalUseOnly` does **not** stop a directly spawned
    `UK2Node_CallFunction`. `UWidgetBlueprintLibrary::Create` carries
    `meta=(BlueprintInternalUseOnly="true")`, which only makes
    `UEdGraphSchema_K2::CanUserKismetCallFunction` (`EdGraphSchema_K2.cpp:932`)
    answer false and keep the function out of the palette; a node built with
    `SetFromFunction` compiles `UpToDate` and runs. `BP_ProbeHUDHost` creates
    its widget that way. `FBPNodeRegistry` does register a `CreateWidget`
    factory for `UK2Node_CreateWidget` (`BPNodeFactory.cpp:325`, config key
    `widgetClass`), but it stays unadvertised alongside the delegate, input and
    macro factories, and was not needed. **Input is no longer in that list**:
    `InputKey` is advertised as of 2026-08-02, because it is the one input
    factory that needs nothing from project settings.
23. **FIXED 2026-08-02, Phase L. Kept for the record.**
    **`blueprint_build` refuses any parent class that is not an Actor.**
    `MCPPuerTSBridgeBlueprint.cpp:375` answered
    `Parent class must derive from Actor: /Script/Engine.SaveGame does not.`
    The engine did not require this: `FKismetEditorUtilities::CanCreateBlueprintOfClass`
    was checked separately on the next line and allows `USaveGame`,
    `UActorComponent` and plain `UObject`. Three consequences met in one chunk:
    no SaveGame subclass (limitation 24); no ActorComponent subclass, so a
    stamina **component** on a pawn was not an available design and the feature
    had to be a Character subclass; and no data-only Blueprint of any kind. The
    fix is the one this entry proposed: the parent check is now the engine's
    own, and the actor-only parts of the spec are gated instead. See the two
    Working rows above and the Fixed section below.
24. **FIXED 2026-08-02, Phase F4. Kept for the record.**
    **A save/load round trip cannot carry a value.** Three doors, all shut.
    `UGameplayStatics::CreateSaveGameObject` refuses the base class by design:
    `if (*SaveGameClass && (*SaveGameClass != USaveGame::StaticClass()))`
    (`GameplayStatics.cpp:2075`), so `/Script/Engine.SaveGame` on the class pin
    yields nothing to save. `USaveGame` itself declares no property, so even a
    live instance would have nowhere to put a float. `SaveDataToSlot` and
    `LoadDataFromSlot` (`GameplayStatics.h:996`, `:1044`), which take a raw byte
    array and would sidestep the class entirely, are **not** `UFUNCTION`s and so
    are unreachable from a graph. With limitation 23 blocking the subclass, the
    value half of the requirement is unreachable today. Observed:
    `MCP_SAVE object_valid=false wrote=false stamina_at_save=0.0`,
    `MCP_LOAD object_valid=false`, and no `Saved/SaveGames` directory on disk
    after three PIE sessions. What *is* proven is the call surface and the
    negative: `SaveGameToSlot` answers false rather than throwing, and
    `MCP_SAVE_PRECHECK slot_exists_at_boot=false` reads the slot at BeginPlay.
    Fixing 23 fixes this. It did: with the SaveGame subclass authorable, none
    of the three doors is on the path any more, and the round trip is proven
    across two PIE sessions. See "Cross-session save and load through generated
    Blueprints" above.
25. **A named child widget of a created UUserWidget cannot be reached from
    another Blueprint.** `UUserWidget::GetWidgetFromName` (`UserWidget.h:1090`)
    and `GetRootWidget` (`:1084`) carry no `UFUNCTION`; `UWidgetTree::FindWidget`
    (`WidgetTree.h:30`) carries none either, and `WidgetTree` is a plain
    `UPROPERTY(Transient)` with no Blueprint access. `UPanelWidget::GetChildAt`
    **is** BlueprintCallable (`PanelWidget.h:36`) but needs a `UPanelWidget` you
    have no way to obtain. The normal UMG answer, a variable on the generated
    widget class, is out because the builder's `VariableGet` is self-scope only
    (`BPNodeFactory.cpp:181`, `scope '%s' not supported in v1 (only 'self')`).
    So `ProgressBar.SetPercent` and `TextBlock.SetText` are not reachable from a
    host graph, and the F4 HUD drives `UWidget::SetRenderOpacity` /
    `SetRenderScale` on the user widget itself instead, reading the value back
    with `GetRenderOpacity`. Three possible fixes, cheapest first: advertise a
    non-self `VariableGet` scope (`FMemberReference::SetExternalMember` already
    exists), give `widget_build` a graph, or add a narrow native
    `GetWidgetByName` helper.
26. **FIXED 2026-08-02, Phase L. Kept for the record, and the diagnosis was
    exactly right.**
    **The `Cast` node type cannot be typed by this builder.**
    `UK2Node_DynamicCast::AllocateDefaultPins` creates its `Object` pin as
    `PC_Wildcard` and resolves the type in `NotifyPinConnectionListChanged`,
    which `UEdGraphPin::MakeLinkTo` never calls. Repro: `Cast` to
    `/Script/Engine.Pawn` with `Object` wired from a `CallFunction`, the
    connection is made and counted, and the compile fails with
    `The type of  Object  is undetermined.  Connect something to  Cast To Pawn  to imply a specific type.`
    The same root cause hits `meta=(DeterminesOutputType=...)`: writing
    `Pin->DefaultObject` on `GameplayStatics.GetActorOfClass`'s `ActorClass`
    fires no `PinDefaultValueChanged`, so its `ReturnValue` stays a wildcard and
    the identical error appears one node downstream. Anything the builder wires
    that depends on a pin-change notification is in this class. The fix is one
    `NotifyPinConnectionListChanged` after `MakeLinkTo`, plus
    `PinDefaultValueChanged` after `ApplyPinDefault`. That is what landed,
    through the public entry points `UEdGraphNode::PinConnectionListChanged`
    (which `UK2Node` overrides to clear a connected input pin's literal and then
    call `NotifyPinConnectionListChanged`) and `PinDefaultValueChanged`, on both
    ends of every connection and after every pin default. See the Fixed section
    below.
25b. **`VariableGet` and `VariableSet` are self-scope only.** Recorded inside
    limitation 25 rather than on its own, and closed with it half-open:
    `scope "target"` with `target_class` now exists and is what the save/load
    round trip uses. It does **not** close 25 itself, because a named child
    widget of a created `UUserWidget` is still not reachable: the widget's own
    generated class has no member variable for it that a host graph could name.
27. **There is no Self node, so "this actor" cannot be used as a value.**
    `UK2Node_Self` has no factory in `FBPNodeRegistry`. An unconnected `self`
    pin on a member function is the blueprint's self and covers the target case,
    but a *parameter* that wants this actor has nothing to wire. The F4 graph
    reaches it the long way round: `Pawn.GetMovementComponent` off the implicit
    self pin, then `PawnMovementComponent.GetPawnOwner`, which hands the pawn
    back already typed and so also dodges limitation 26. Worth a `Self` factory.
28. **A pin default that names no pin is still only a log warning.**
    `ApplyParamsAsPinDefaults` (`BlueprintGraphBuilderLibrary.cpp:483`) logs
    `node '%s' param '%s' %s` and carries on, so a misspelled pin name or a
    value of the wrong shape leaves the pin at its own default and the build
    reports success. This is the same silent-failure class limitation 20 was,
    and after F4 it is the last one left in `blueprint_build`. The fix has the
    same shape: collect the failures and fail the build with them named.
29. **A build that fails after the asset is created leaves an unsaved package
    behind.** Validate-before-mutate covers everything checkable before
    creation; a connection shortfall and a compile error are found afterwards,
    so the package exists in memory, `find_assets` reports it, and the disk does
    not have it. Repro: a rejected `InputKey` spec against
    `/Game/MCPGenerated/BP_ProbeInputBad` left `find_assets` `count 1` with no
    `BP_ProbeInputBad.uasset` in `Content/MCPGenerated`, and the entry was gone
    after the next editor restart. Harmless but confusing: unlike the
    pre-validation rejections, `count 0` is not the signal that a build failed.
30. **One editor crash, not reproduced.** `EXCEPTION_ACCESS_VIOLATION reading
    address 0xffffffffffffffff` during a `blueprint_build` that rebuilt the
    198-node F4 graph immediately after the previous command had **created** a
    new `StaticMeshComponent` on the same generated Character Blueprint and
    recompiled it. The editor log ends after the previous command's save; dump
    in `Saved/Crashes/UE4CC-Windows-971305C54CF3274B001FF4AA7117327B_0000`. The
    same command with the same spec, after an editor restart with the component
    already on disk, succeeded and has since run four more times. So the suspect
    is add-component-then-rebuild-large-graph inside one editor session, and it
    is one sighting, not a reproduction.
31. **Nothing in the catalog can press a key.** `InputKey` nodes compile and
    bind, and possession is proven, but there is no input-simulation tool, so an
    input-driven feature cannot be exercised through its input during an
    automated PIE run. The F4 character therefore carries an auto-drive branch
    on Tick that sets the same `bSprintHeld` variable the `LeftShift` node sets,
    and the save and load events fire from the same timeline rather than from
    their `K` and `L` keys. The input path is proven to *build and bind*; it is
    not proven to *fire*, and that is not claimed.
32. **A graph spec is the whole graph, and a Blueprint's variables are
    additive.** `clear_existing_graph` defaults true, so the only way to add a
    node to an existing generated Blueprint is to resend every node it already
    had; there is no node identity to merge against. Variables are the
    opposite: a rerun adds and updates, and a variable the spec stopped
    mentioning stays on the asset forever. Both bit this chunk. Extending the
    F4 character with save and load meant regenerating its whole event graph
    from a fresh generator, and `BP_StaminaCharacter` now carries the previous
    session's 22 variables alongside the current spec's 23, with the overlap
    shared. Neither is wrong, but together they mean a generated Blueprint
    accumulates dead members while its graph cannot be patched. The next
    primitive is node upsert plus a declared-set variable pass; both are
    deliberately out of this chunk.
    **Half open as of 2026-08-01.** The read half landed:
    `puerts_graph_inspect` returns the whole graph, and it put a number on the
    drift this entry described - `BP_StaminaCharacter` carries **37** member
    variables where its current spec declares 23. The write half is untouched
    and is the next capability. Its blocker is named in the Fixed section
    above: a spec's `id` is not persisted on the node, so there is still
    nothing to merge against. An authored node identity that survives save,
    load and recompile is the prerequisite, not the upsert algorithm.
33. **The 200-node cap in `blueprint_build`'s schema is a real ceiling for one
    feature.** The F4 graph is 198 nodes at 252 connections, and roughly a
    quarter of that is string composition: every logged value costs one
    `Conv_*ToString` and about two `Concat_StrStr` nodes, because there is no
    string-literal node and no proven `FormatText` route. A feature that wants
    one more reported number has to give one up. Raising the cap is not the fix
    on its own; a `FormatText` node with typed argument pins would cut the
    logging cost by two thirds.
34. **Moving a Character by writing its capsule's `RelativeLocation` leaves it
    able to fall through the floor.** `set_property` on
    `BP_StaminaCharacter_C_1.CollisionCylinder RelativeLocation` reported
    success and read back the new value, and in the next PIE session the
    character's own graph reported `z=29.4` and then `z=-801`, `z=-2626`,
    `z=-5430`, falling at terminal velocity while its X drifted, i.e. it went
    through the ground rather than standing on it. The same actor class spawned
    fresh with `spawn_actor` at the same place reported `z=110.149994` on every
    tick of a twelve-second run. Deleting and respawning is the reliable move;
    the property write is not. Not diagnosed further.

## Pending capabilities (tracked, deliberately not started)

- **Behavior Tree inspection** - RESOLVED 2026-08-01 evening.
  `puerts_behavior_tree_inspect` landed (InspectBehaviorTreeJson, donor
  references recorded in the implementation notes), passed its two-phase live
  acceptance including an editor restart, and its independent spec comparison
  promoted `puerts_behavior_tree_build` to live_verified. On its FIRST use the
  inspector caught a real defect the builder's own report never showed: the
  acceptance spec's CamelCase param keys were silently dropped (see 0e).
  Evidence: docs/evidence/behavior-tree-acceptance-2026-08-01.txt.

## Unknown (tracked, not explained)

None open. The one entry that was here is resolved below.

## Resolved Unknowns

**`actor_count_total 0` for a full level is a startup race, not a bad
first-call path** (was the tracked Unknown; reproduced and resolved
2026-08-01).

What was recorded: `puerts_diagnostic` answered `actor_count_total 0` once, in
a level that had 12 actors, on the first call after an editor start, with no
repro. Two candidate readings were written down - a real race between editor
startup and the actor query, or a first-call code path that answers before the
world is attached.

It reproduced on the first `puerts_diagnostic` after the editor restart in this
chunk, at no extra cost, and the follow-up call settled it:

- first call: `actor_count_total 0`, `actor_count_measured 0`,
  `json_snapshot_bytes 13`, `native_actor_query_ms 0.0215`,
  `is_game_thread true`, `service_address 000001C4BFA1F380`
- next call, same session, same service address:
  `actor_count_total 12`, `json_snapshot_bytes 1158`,
  `native_actor_query_ms 0.0141`, `is_game_thread true`

**It is the race, and the second reading is refuted.** A first-call path that
answered before the world was attached would have to be a different path, and
there is only one: the same service address, the same query, on the game
thread, in both calls, with the second answering correctly milliseconds later.
The query genuinely ran and genuinely found nothing, because at that instant
the editor world held no actors yet.

The window is real and reachable: the bridge's named pipe is up and accepting
authenticated commands before the map has finished loading. The editor log
puts `MCP PuerTS named pipe ready with 20 approved tools` well before
`[LogLoad] (Engine Initialization) Total time: 43.06 seconds`, and both of the
calls above landed between them.

Not fixed, because the fix is a design decision rather than a bug repair, and
this chunk was scoped to inspection. The honest options, cheapest first: have
`diagnostic` report a `world_ready` flag alongside the count so zero-with-no-
world is distinguishable from zero-with-empty-level; or hold the pipe closed
until the editor's initial map load completes, which trades a clear signal for
a longer startup during which the bridge is simply absent. **The caller-facing
lesson stands either way: a `0` from the first call after an editor start is
not evidence of an empty level. Call twice.**

## Fixed

**Read-only Blueprint graph inspection** (`puerts_graph_inspect`; landed
2026-08-01). The inverse direction of `blueprint_build`, and the first half of
limitation 32: a graph cannot be patched before it can be read. Patching itself
is deliberately **not** in this change.

Most of it was already written. `UBlueprintInspectorLibrary` and its readers -
`ListSCSNodes`, `ListVariables`, `ListGraphs`, `ListFunctions`,
`ListInterfaces`, plus `FBPGraphReader` and `FBPNodeSerializer` - have been
compiled into `MCPBridgeGraphBuilder` all along with no caller, exactly as the
widget builder was. **That is twice now.** Read the module before writing a
subsystem for it.

Four decisions:

- **The reverse type map lives next to the forward one.**
  `UBlueprintGraphBuilderLibrary::GetNodeTypeForNode` is the mirror of the
  dispatch chain in `BuildBlueprintFromJSON`, in the same file, deliberately a
  chain rather than a table: `UK2Node_CustomEvent` derives from
  `UK2Node_Event` and `UK2Node_MultiGate` from `UK2Node_ExecutionSequence`, so
  asking the base first reports both as their base and would rebuild the wrong
  node. A mapping table in the command layer would have been a second place to
  forget a node type; here, a type added to one side and not the other shows up
  immediately as an inspected node whose `type` is null, listed under
  `graph.unmapped_nodes` with the K2 class it could not name.
- **Node identity is observed, and says so.** A node is addressed by its object
  name and its `NodeGuid`. The `id` a build spec wrote is **not persisted
  anywhere on the node**, so an inspected node cannot be matched back to the
  spec line that made it. Synthesising a plausible-looking `id` would have
  hidden exactly the gap that has to be closed next.
- **Read-only is measured, not asserted.** The command is kept out of
  `IsToolMutating`, so no transaction is opened and the response carries no
  transaction id; nothing on the path calls `Modify`, `MarkPackageDirty` or a
  compile; and the package's own dirty flag is read before and after the work
  and returned as `package_dirty_before` / `package_dirty_after`. An annotation
  is a promise, and this one is checkable by the caller.
- **Every array is canonically ordered.** Unreal's array order is an
  implementation detail that a reconstruct, a paste or a load can permute, so
  nodes sort by `NodeGuid`, pins by direction then `PinId`, connections by
  their four endpoint identities, and components, variables, functions and
  graphs by name. JSON *object key* order is `FJsonObject`'s `TMap` and is not
  canonical; a caller comparing runs byte for byte should sort keys first. In
  practice both test payloads came back byte-identical without that step.

Two gaps recorded rather than papered over. `MakeStruct`, `BreakStruct`,
`SpawnActor`, `Select`, `Knot` and `FormatText` hold their configuration in
their pins rather than in a `UPROPERTY`, so they report pin defaults and no
routing params. And a struct pin default other than vector, rotator or linear
color is reported as its raw pin text and named in `graph.lossy_pin_defaults`,
because only those three are written in the comma form the reader can invert.
Neither fired on any graph tested, including the 198-node one.

Three fidelity holes in the pre-existing pin serializer were closed on the way:
it reported `DefaultValue` alone, so every object-pin asset reference and every
`FText` literal in a graph read back as empty, and there was no
`AutogeneratedDefaultValue` to tell an authored default from the node's own.

**A Blueprint no longer has to be an Actor** (was limitation 23; fixed
2026-08-02, Phase L). `BuildBlueprintJson` dropped its own
`IsChildOf(AActor::StaticClass())` check and kept the engine's,
`FKismetEditorUtilities::CanCreateBlueprintOfClass`, which was already on the
next line and allows `USaveGame`, `UActorComponent` and plain `UObject`. What
is genuinely Actor-only is gated per capability instead: a `components` array
needs a SimpleConstructionScript, and `BeginPlay`, `Tick`, `ActorBeginOverlap`,
`ActorEndOverlap` and `InputKey` bind AActor entry points. Both rejections name
the node or the array and the parent class, and both fire before the asset
exists.

Two decisions:

- **Gate the capability, not the parent.** The alternative, an allowlist of
  permitted parent classes, would have needed updating for every base class
  anyone ever wants and would still have said nothing useful about *why*. Five
  node types and one array is the whole actual dependency, and it is written
  down in one predicate, `IsActorOnlyNodeType`.
- **A non-Actor Blueprint still gets a graph.** `FKismetEditorUtilities::CreateBlueprint`
  makes an ubergraph for any `BPTYPE_Normal` Blueprint, so a UObject parent
  takes CustomEvents, CallFunction, Cast, variables and flow. Only the actor
  entry points are missing, which is the truth rather than a restriction.

**Pin-change notifications, so a wildcard pin types itself** (was limitation 26;
fixed 2026-08-02, Phase L). `UEdGraphPin::MakeLinkTo` moves two pointers and
stops. `UEdGraphSchema_K2::TryCreateConnection`, which is what the graph editor
runs, also calls `PinConnectionListChanged` on both ends, and that is where a
node that types a pin from what it is wired to does the work:
`UK2Node_DynamicCast::NotifyPinConnectionListChanged` (`K2Node_DynamicCast.cpp:347`)
promotes its `Object` pin out of `PC_Wildcard`, and
`UK2Node_CallFunction::NotifyPinConnectionListChanged` conforms a
`DeterminesOutputType` output. The same gap existed for pin defaults:
`UK2Node_CallFunction::PinDefaultValueChanged` (`K2Node_CallFunction.cpp:1239`)
is what retypes an output from a class picker. The builder now sends both.

Three decisions:

- **Use the public entry points, not the K2 ones.** `PinConnectionListChanged`
  on `UEdGraphNode` is what the schema calls; `UK2Node`'s override resets a
  connected input pin's autogenerated default before forwarding to
  `NotifyPinConnectionListChanged`. Calling the inner one directly would have
  skipped the literal clearing the editor does.
- **Notify both ends, guarded.** A notification can destroy a pin
  (`bIsBeadFunction` suicide, orphan-pin removal), so each call is behind
  `!Pin->IsPendingKill()`, and connections re-resolve their pins by name each
  iteration rather than caching pointers.
- **`AsResult` rather than a computed pin name.** A cast names its result pin
  `"As"` plus the target type's *display* name, which for
  `BP_StaminaSave_C` is not derivable from the spec. The connection resolver
  takes the role `AsResult` and asks `GetCastResultPin()`.

**Target-scoped variable access** (fixed 2026-08-02, Phase L).
`FBPNodeFactory::CreateVariableGet` / `CreateVariableSet` took `scope` and
refused anything but `self`. They now take `scope "target"` with `targetClass`,
call `FMemberReference::SetExternalMember`, and the node grows a `self` input
pin for the object to act on. The class is loaded and the property is looked up
before the node is created, so a misspelled variable name is refused by the
factory rather than surfacing later as a connection to a pin that does not
exist.

**Unresolved graph connections fail the build** (was limitation 20; fixed
2026-08-02, Phase F4). `UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSONWithReport`
counts the links `MakeLinkTo` actually created and returns one entry per dropped
connection; `BuildBlueprintJson` compares that count against the number the spec
asked for and, on any shortfall, adds an error naming every dropped pair. An
error means the asset is not saved, which the command already enforced.
`graph.connection_count` is now the number **made**, with `connections_requested`
and `unresolved_connections` beside it.

Three decisions:

- **The count is the contract, not the log.** The builder already wrote
  `Could not resolve pins for connection A -> B` to the editor log, and had done
  since it was written. Nothing consumed it. Counting made against requested is
  what turns a log line into a failure, and it is one integer.
- **Each drop says why.** An endpoint that does not read `nodeId.pinRole`, a node
  id that spawned nothing, and a pin role that names no pin of that direction are
  three different mistakes with three different fixes, so the entry names which.
  The third is the common one and its usual cause is limitation 21, so the error
  text says that too.
- **The Blueprint-callable entry point keeps its old signature.** The reporting
  overload is plain C++; `BuildBlueprintFromJSON` forwards to it and discards the
  report, exactly as it behaved before.

It paid for itself inside the same session. The F4 graph's first build wired
`bpSeq.then_0 -> ownSelf.exec` against `UActorComponent::GetOwner`, which is
`const` and therefore pure (limitation 21). Before this change that build would
have answered `compile_status "UpToDate"`, `errors []`, `saved true` with the
character's possession chain quietly unwired, and the failure would have
surfaced later as "the pawn is never possessed", with nothing pointing at the
cause.

**The `InputKey` node type** (part of limitation 8's unadvertised input family;
advertised 2026-08-02). Eleven of the twelve input factories in
`BPNodeFactory_Input.cpp` need a project input mapping to point at: an
`InputAction` node naming an action `DefaultInput.ini` does not declare compiles
clean and never fires, which is why the whole family stayed unadvertised.
`UK2Node_InputKey` is the exception. It binds a literal `FKey` and the factory
rejects a name `EKeys` does not know, so the failure is at build time rather
than at play time. Advertising it cost four lines: the type in
`RegistryNodeTypes()`, its four config keys in the routing-key set so they are
not misread as pin defaults, and the enum entry in `mcp-server/src/tools/puerts.ts`.

## Fixed (earlier)

**Widget authoring** (was the second half of limitation 8's "no widget and no
audio authoring surface at all"; fixed 2026-08-02). `puerts_widget_build` takes
a JSON widget tree and answers with a compiled, saved `UWidgetBlueprint`.

The surprise is that almost none of this was new code. The design spec
`docs/superpowers/specs/2026-03-18-widget-blueprint-builder-design.md` is marked
"design complete, implementation not started", and the plan repeated that. The
implementation is in fact fully present in `MCPBridgeGraphBuilder`: 1840 lines
across `WidgetBlueprintBuilderLibrary.cpp` and eleven `WidgetBuilder/` files,
compiled into the module every build, with 18 widget types rather than the
spec's 10 and a widget-animation pass the spec never mentions. It had no caller.
So this is the same re-front the Blueprint builder got, not a new subsystem:
`UMCPPuerTSBridgeService::BuildWidgetJson` (156 lines) plus the runtime command
and the MCP schema. **Read the source before believing a spec's status line.**

Three decisions shaped the front:

- **The library owns the grammar; the command owns the contract.** Widget types,
  child-count rules per category, property names and their JSON types all stay
  in `FWidgetClassRegistry` and `FWidgetBlueprintValidator`. The command adds
  the `/Game/MCPGenerated/` limit, a `ValidateWidgetJSON` pass before anything
  is touched, the create-versus-rebuild decision, and the read-back.
- **The response is read back from the asset, not echoed from the request.**
  `DescribeWidget` walks the live `UWidgetTree` from `RootWidget` through
  `UPanelWidget::GetChildAt`, and reports a `UCanvasPanelSlot`'s position and
  size out of `LayoutData.Offsets`. Slot layout is applied by a different code
  path than widget construction, so a tree built with every slot silently at the
  origin would otherwise read as a success.
- **A widget tree converges as a whole, not per widget.** Components and
  variables have stable names to merge against; a widget tree has no identity
  that survives a caller reordering or renaming a node, so a rerun replaces the
  tree of the asset already at that path. That is why the tool is annotated
  `destructiveIdempotent` rather than merely idempotent, and why the description
  says so.

`UWidgetBlueprintFactory` through `FAssetToolsModule` is the creation path the
spec called for and it works unchanged in 4.27; `FKismetEditorUtilities::CompileBlueprint`
compiles a `UWidgetBlueprint` with no special casing. `MCPBridgePuerTS` gained
`UMG` and `UMGEditor` as private dependencies for the read-back only.

**Sound** needed no new code at all. `CallFunction` has never been gated to a
function list - it takes any class and any reflected function - and
`ApplyPinDefault` already loads object pins by asset path, so
`GameplayStatics.SpawnSoundAtLocation` with a `/Engine` SoundWave on its `Sound`
pin was reachable from the 26-type vocabulary as it stood. What the earlier
session read as "no audio authoring surface" was a vocabulary that already
covered it. The work was picking a proof: `SpawnSoundAtLocation` returns the
`UAudioComponent`, and `PlaySoundAtLocation` returns void, so only the former
can be checked afterwards.

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
path/name filters; undo stack depth. Two-editor pipe isolation is no longer on
this list: it was tested on 2026-08-02 with both editors open and is finding 0l.
Overlap against
a player pawn (nothing in the default game mode moves on its own, and there is
no input-simulation tool in the native catalog, so the only self-propelled
overlap source proven so far is a JSON-authored physics body).

## Finding 0m: install:sync leaves stale UHT generated code when a source file goes away

Confirmed 2026-08-02 by the integration lead, on a live rebuild of
`D:/Unreal Projects/BridgeInstallTest`.

`Scripts/bridge-install.mjs --sync` copies declared files into the target and
then builds. It deliberately does not delete project files, and says so. What
it also does not do is invalidate Unreal Header Tool's generated code, and that
combination has a failure mode with a misleading error.

Sequence that produced it:

1. A source file (`MCPPuerTSBridgeBlueprintMember.cpp`) and a header
   declaration for `PatchBlueprintMembersJson` were present in the target.
2. UHT generated `MCPPuerTSBridgeService.gen.cpp` and
   `MCPPuerTSBridgeService.generated.h` carrying a reflection thunk for that
   `UFUNCTION`.
3. `--sync` restored the header from the repository, where the declaration does
   not exist, and the `.cpp` was removed by hand as the sync output instructs.
4. UHT did not regenerate, because the restored header is not newer than the
   generated file. The stale thunk survived.
5. The build failed with `LNK2019: unresolved external symbol
   PatchBlueprintMembersJson ... referenced in function
   execPatchBlueprintMembersJson`, then `LNK1120`.

The error names a symbol that appears in NEITHER the repository header nor the
target header. Both were checked and both are clean. Grepping
`Intermediate/Build/Win64/UE4Editor/Inc/` is what actually locates it. Someone
reading only the linker output would look for a missing implementation of a
function nobody declared.

Fix that worked: delete the plugin's `Intermediate/` (145 files, all
regenerable build output) and rebuild. A warm incremental build took 1.7
seconds; this one is a full plugin rebuild.

Why this is a bridge finding and not a one-off: the whole point of the install
gate is that a live run proves something about the code under review. This is a
case where the sources match the repository, `install:check` is satisfied on
content, and the BINARY still contains a reflection entry for a command that no
longer exists. Content equality is not build equality.

Open, not yet fixed. Two candidate fixes, neither implemented:

- `--sync` removes `Intermediate/Build/.../Inc/<module>/` for any module whose
  declared header set changed, which is cheap and targeted.
- `install:check` compares the installed registry catalog against the built
  DLL's exported reflection, not just against the source tree. That is the
  check that would have caught this before the build rather than during it.

Related: the same run surfaced `extra:` files in `native_source` (the two
orphan-installed files above), which `install:check` DID catch and refuse. The
content gate works. The build-artifact gate does not exist.

## Finding 0n: three gaps that make a live fixture impossible to reset

Found by lane H, 2026-08-02, while making the member_patch acceptance
deterministic. Recorded together because they are one practical problem: there
is no way to return a live editor's asset to a known state.

1. **No delete-asset primitive exists in the catalog.** Nothing in the 209
   registrations deletes an asset. `puerts_delete_actor` deletes a level actor,
   which is a different thing.
2. **`blueprint_build`'s `remove_unlisted` rejects the `components` scope as
   unsupported.** So there is no downward convergence on components: a build can
   add a component and cannot take one away.
3. **Unlinking the `.uasset` does not reseed a live editor.** The package is
   already loaded, so the next build finds the in-memory object and the file on
   disk is irrelevant.

Together these mean a fixture cannot be restored in place while the editor is
running, which is why the previous member_patch acceptance was order dependent
and why two live runs of it disagreed.

Lane H's workaround is sound and does not need any of the three fixed: build the
fixture at a fresh path per run (`BP_MemberProbe_<runId>`) and assert the path
did not exist by requiring `graph_inspect` to fail on it first. Determinism
comes from never reusing a path, not from cleaning one up.

Worth fixing anyway, because the workaround only helps tests. A caller
authoring real content needs (1) and (2) to converge downward at all, and the
absence of (2) means `remove_unlisted` is convergent for graphs and not for
components, which is a surprising asymmetry in a command whose whole contract is
convergence.

## Finding 0o: ImportText returning non-null is not a type check

Confirmed by lane H, 2026-08-02, by reading engine source. This is the defect
behind the integrator's first live member_patch run.

`CompareVariableDefault` (`MCPPuerTSBridgeBlueprintMember.cpp:220`) asks whether
a type can hold a value by testing `FProperty::ImportText` for a non-null
return. For a floating point property that is not a test:
`FNumericProperty::ImportText_Internal`
(`Runtime/CoreUObject/Private/UObject/PropertyNumeric.cpp:113`) advances past
`[+-.0-9]`, consumes nothing at all for `"not a number"`, calls
`SetNumericPropertyValueFromString`, and returns non-null.

So the value imports as `0.0`, is classified `Different` rather than
`Unavailable`, and is applied. The applier (`BPVariableOps.cpp:168`) uses the
same non-test, and verification re-reads through the same comparator, which
agrees with itself. `0.0` is written, verified, and saved.

`FIntProperty` does reject, through `UEnum::ParseEnum` returning `INDEX_NONE`,
so an int control passing beside a float failing localises it precisely.

Fix: require the whole buffer consumed, not merely a non-null return. Accept
only when `End != nullptr && *End == '\0'`. It belongs in one shared helper on
`UBlueprintMutatorLibrary` beside `JsonDefaultToImportText`, because THREE call
sites carry the identical non-test: the member validator, `SetVariableDefault`,
and `add_variable`'s default path.

Assigned to lane G with the build lock. The acceptance check stays red until it
lands, on purpose, so the fix has a failing test to turn green.

The general lesson is the one this file keeps recording: a verifier that shares
its comparator with the writer cannot catch the writer being wrong. The member
hash read back through `graph_inspect` is a genuinely independent check; the
default-value comparison was not.
