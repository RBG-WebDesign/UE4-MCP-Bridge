import type { BlueprintDesiredState, BlueprintGraph, FeatureRequest, GraphNode } from "./types.js";

const constraints: FeatureRequest["constraints"] = { ue_version: "4.27", authoring_root: "/Game/MCPGenerated", networked: false, designer_editable: true };

function node(id: string, type: string, extras: Partial<GraphNode> = {}): GraphNode {
  return { id, type, functional: !["Comment", "Knot"].includes(type), ...extras };
}

function graph(name: string, kind: BlueprintGraph["kind"], nodes: GraphNode[], links: Array<[string, string]>): BlueprintGraph {
  return { name, kind, nodes, connections: links.map(([from, to]) => ({ from, from_pin: "then", to, to_pin: "execute" })) };
}

function bp(asset_path: string, parent_class = "Actor", graphs: BlueprintGraph[] = []): BlueprintDesiredState {
  return { asset_path, parent_class, components: [], variables: [], functions: [], macros: [], dispatchers: [], interfaces: [], graphs, convergence: { remove_unlisted: ["variables", "components"] } };
}

function request(feature_id: string, blueprints: BlueprintDesiredState[], required_capabilities: string[] = [], signals: FeatureRequest["architecture_signals"] = { asset_composition: true, event_wiring: true }): FeatureRequest {
  return { schema_version: 1, feature_id, intent: `${feature_id} acceptance fixture`, constraints, architecture_signals: signals, blueprints, required_capabilities };
}

const f1 = bp("/Game/MCPGenerated/BPV_F1_BasicActor", "Actor", [graph("EventGraph", "event", [node("begin", "BeginPlay", { region: "Initialize" }), node("score", "CallFunction", { params: { function: "ComputeScore" }, region: "Initialize" }), node("set_score", "VariableSet", { region: "Initialize" }), node("print", "PrintString", { region: "Report" })], [["begin", "score"], ["score", "set_score"], ["set_score", "print"]])]);
f1.components = [{ name: "Root", class: "SceneComponent" }, { name: "Mesh", class: "StaticMeshComponent", attach_to: "Root", properties: { StaticMesh: "/Engine/BasicShapes/Cube.Cube" } }];
f1.variables = [
  { name: "Enabled", type: { category: "bool" }, default: true, category: "State" }, { name: "Score", type: { category: "float" }, default: 0, category: "State" },
  { name: "Label", type: { category: "string" }, default: "Fixture", category: "Identity" }, { name: "Offset", type: { category: "struct", subcategory_object: "/Script/CoreUObject.Vector" }, category: "Identity" },
  { name: "MeshAsset", type: { category: "object", subcategory_object: "/Script/Engine.StaticMesh" }, category: "Identity" }, { name: "Values", type: { category: "float", container_type: "Array" }, category: "State" },
  { name: "Tags", type: { category: "name", container_type: "Set" }, category: "State" }, { name: "Counts", type: { category: "name", container_type: "Map", value_type: { category: "int" } }, category: "State" },
];
f1.functions = [{ name: "ComputeScore", inputs: [{ name: "Base", type: { category: "float" } }], outputs: [{ name: "Result", type: { category: "float" } }], locals: [{ name: "Multiplier", type: { category: "float" }, default: 1 }], metadata: { pure: true, const: true, category: "Scoring" } }];

const f2 = bp("/Game/MCPGenerated/BPV_F2_InteractableActor", "Actor", [graph("EventGraph", "event", [node("overlap", "ActorBeginOverlap", { region: "Interaction" }), node("valid", "Branch", { branch_depth: 1, region: "Interaction" }), node("message", "InterfaceMessage", { branch_lane: 1 }), node("dispatch", "CallDelegate", { branch_lane: 1 })], [["overlap", "valid"], ["valid", "message"], ["message", "dispatch"]])]);
f2.components = [{ name: "Root", class: "SceneComponent" }, { name: "InteractionBounds", class: "BoxComponent", attach_to: "Root" }];
f2.interfaces = ["/Game/MCPGenerated/BPI_BPV_Interactable.BPI_BPV_Interactable_C"];
f2.dispatchers = [{ name: "OnInteracted", parameters: [{ name: "Instigator", type: { category: "object" } }] }];

const f3 = bp("/Game/MCPGenerated/BPV_F3_HybridSwitch", "/Script/BlueprintProductionFixture.BPVHybridSwitchBase", [graph("EventGraph", "event", [node("visual", "Event", { region: "Visual extension" }), node("audio", "CallFunction", { region: "Visual extension" })], [["visual", "audio"]])]);
f3.components = [{ name: "Mesh", class: "StaticMeshComponent" }, { name: "Audio", class: "AudioComponent" }];

const f4 = bp("/Game/MCPGenerated/BPV_F4_StatefulDoor", "Actor", [graph("EventGraph", "event", [node("overlap", "ActorBeginOverlap"), node("can_open", "CallFunction"), node("branch", "Branch", { branch_depth: 1 }), node("open", "CallFunction", { branch_lane: 1 })], [["overlap", "can_open"], ["can_open", "branch"], ["branch", "open"]])]);
f4.components = [{ name: "Frame", class: "StaticMeshComponent" }, { name: "DoorPanel", class: "StaticMeshComponent", attach_to: "Frame" }, { name: "Trigger", class: "BoxComponent", attach_to: "Frame" }];
f4.variables = ["bLocked", "bOpen", "OpenOffset", "OpenDuration", "KeyId"].map((name) => ({ name, type: { category: name.startsWith("b") ? "bool" : name === "KeyId" ? "name" : "float" }, category: "Door" }));
f4.functions = ["CanOpen", "OpenDoor", "CloseDoor"].map((name) => ({ name }));
f4.dispatchers = [{ name: "OnDoorStateChanged", parameters: [{ name: "bOpen", type: { category: "bool" } }] }];

const f5 = bp("/Game/MCPGenerated/BPV_F5_InventoryComponent", "ActorComponent", [graph("AddItem", "function", [node("entry", "Event"), node("loop", "ForEachLoop", { loop_expected_iterations: 250, region: "Inventory scan" }), node("changed", "CallDelegate")], [["entry", "loop"], ["loop", "changed"]])]);
f5.variables = [{ name: "Items", type: { category: "struct", container_type: "Array" } }, { name: "Tags", type: { category: "name", container_type: "Set" } }, { name: "Counts", type: { category: "name", container_type: "Map", value_type: { category: "int" } } }];
f5.functions = ["AddItem", "RemoveItem", "ContainsItem", "GetItemCount"].map((name) => ({ name }));
f5.dispatchers = [{ name: "OnInventoryChanged" }];

const f6base = bp("/Game/MCPGenerated/BPV_F6_BaseInteractable", "Actor", [graph("Activate", "function", [node("entry", "Event"), node("extension", "CustomEvent")], [["entry", "extension"]])]);
f6base.functions = [{ name: "Activate", inputs: [{ name: "Instigator", type: { category: "object" } }] }];
const f6child = bp("/Game/MCPGenerated/BPV_F6_ChildInteractable", "/Game/MCPGenerated/BPV_F6_BaseInteractable.BPV_F6_BaseInteractable_C", [graph("Activate", "function", [node("entry", "Event"), node("parent", "CallFunction")], [["entry", "parent"]])]);
f6child.depends_on = [f6base.asset_path];
f6child.functions = [{ name: "Activate", inputs: [{ name: "ActivationInstigator", rename_from: "Instigator", type: { category: "object" } }] }];

const cleanupNodes = [node("start", "BeginPlay", { region: "Start" }), ...Array.from({ length: 6 }, (_, index) => node(`repeat_${index}`, "CallFunction", { repeat_key: "validate_then_apply", branch_lane: index % 2, region: index < 3 ? "Validate" : "Apply" })), node("finish", "PrintString", { branch_lane: 1, region: "Finish" })];
const f7 = bp("/Game/MCPGenerated/BPV_F7_GraphCleanup", "Actor", [graph("EventGraph", "event", cleanupNodes, [["start", "repeat_0"], ["repeat_0", "repeat_1"], ["repeat_1", "repeat_2"], ["repeat_2", "repeat_3"], ["repeat_3", "repeat_4"], ["repeat_4", "repeat_5"], ["repeat_0", "finish"]])]);
f7.graphs[0].connections.push({ from: "start", from_pin: "then", to: "repeat_5", to_pin: "execute" });
const f8 = bp("/Game/MCPGenerated/BPV_F8_Rollback", "Actor", [graph("EventGraph", "event", [node("begin", "BeginPlay"), node("valid", "PrintString")], [["begin", "valid"]])]);

const capstoneGraph: BlueprintGraph = {
  name: "EventGraph",
  kind: "event",
  nodes: [
    node("interact", "InputKey", { params: { fkey_name: "E" }, region: "Pickup or drop" }),
    node("is_holding", "VariableGet", { params: { var_name: "bHolding" }, region: "Pickup or drop" }),
    node("toggle", "Branch", { branch_depth: 1, region: "Pickup or drop" }),
    node("drop_handle", "VariableGet", { params: { var_name: "PhysicsHandle" }, branch_lane: -1, region: "Drop" }),
    node("drop", "CallFunction", { params: { class: "PhysicsHandleComponent", function: "ReleaseComponent" }, branch_lane: -1, region: "Drop" }),
    node("drop_clear_component", "VariableSet", { params: { var_name: "HeldComponent", value: null }, branch_lane: -1, region: "Drop" }),
    node("drop_clear_state", "VariableSet", { params: { var_name: "bHolding", value: false }, branch_lane: -1, region: "Drop" }),
    node("camera", "VariableGet", { params: { var_name: "Camera" }, branch_lane: 1, region: "Acquire" }),
    node("camera_location", "CallFunction", { params: { class: "SceneComponent", function: "K2_GetComponentLocation" }, branch_lane: 1, region: "Acquire" }),
    node("camera_forward", "CallFunction", { params: { class: "SceneComponent", function: "GetForwardVector" }, branch_lane: 1, region: "Acquire" }),
    node("grab_distance", "VariableGet", { params: { var_name: "GrabDistance" }, branch_lane: 1, region: "Acquire" }),
    node("trace_offset", "Operator", { params: { op: "multiply_vector_float" }, branch_lane: 1, region: "Acquire" }),
    node("trace_end", "Operator", { params: { op: "add_vector" }, branch_lane: 1, region: "Acquire" }),
    node("trace", "CallFunction", { params: { class: "KismetSystemLibrary", function: "LineTraceSingle", bTraceComplex: false, bIgnoreSelf: true }, branch_lane: 1, region: "Acquire" }),
    node("hit", "Branch", { branch_depth: 2, branch_lane: 1, region: "Acquire" }),
    node("break_hit", "CallFunction", { params: { class: "GameplayStatics", function: "BreakHitResult" }, branch_lane: 1, region: "Acquire" }),
    node("grab_handle", "VariableGet", { params: { var_name: "PhysicsHandle" }, branch_lane: 1, region: "Grab" }),
    node("grab", "CallFunction", { params: { class: "PhysicsHandleComponent", function: "GrabComponentAtLocation", InBoneName: "None" }, branch_lane: 1, region: "Grab" }),
    node("store_component", "VariableSet", { params: { var_name: "HeldComponent" }, branch_lane: 1, region: "Grab" }),
    node("store_state", "VariableSet", { params: { var_name: "bHolding", value: true }, branch_lane: 1, region: "Grab" }),
    node("tick", "Tick", { branch_lane: 3, region: "Hold target" }),
    node("tick_state", "VariableGet", { params: { var_name: "bHolding" }, branch_lane: 3, region: "Hold target" }),
    node("tick_gate", "Branch", { branch_depth: 1, branch_lane: 3, region: "Hold target" }),
    node("hold_distance", "VariableGet", { params: { var_name: "HoldDistance" }, branch_lane: 3, region: "Hold target" }),
    node("hold_offset", "Operator", { params: { op: "multiply_vector_float" }, branch_lane: 3, region: "Hold target" }),
    node("hold_end", "Operator", { params: { op: "add_vector" }, branch_lane: 3, region: "Hold target" }),
    node("target_handle", "VariableGet", { params: { var_name: "PhysicsHandle" }, branch_lane: 3, region: "Hold target" }),
    node("set_target", "CallFunction", { params: { class: "PhysicsHandleComponent", function: "SetTargetLocation" }, branch_lane: 3, region: "Hold target" }),
    node("throw_input", "InputKey", { params: { fkey_name: "LeftMouseButton" }, branch_lane: 5, region: "Throw" }),
    node("throw_state", "VariableGet", { params: { var_name: "bHolding" }, branch_lane: 5, region: "Throw" }),
    node("throw_gate", "Branch", { branch_depth: 1, branch_lane: 5, region: "Throw" }),
    node("held", "VariableGet", { params: { var_name: "HeldComponent" }, branch_lane: 5, region: "Throw" }),
    node("impulse", "CallFunction", { params: { class: "PrimitiveComponent", function: "AddImpulse", Impulse: { x: 1800, y: 0, z: 180 }, bVelChange: true }, branch_lane: 5, region: "Throw" }),
    node("throw_handle", "VariableGet", { params: { var_name: "PhysicsHandle" }, branch_lane: 5, region: "Throw" }),
    node("throw_release", "CallFunction", { params: { class: "PhysicsHandleComponent", function: "ReleaseComponent" }, branch_lane: 5, region: "Throw" }),
    node("throw_clear_component", "VariableSet", { params: { var_name: "HeldComponent", value: null }, branch_lane: 5, region: "Throw" }),
    node("throw_clear_state", "VariableSet", { params: { var_name: "bHolding", value: false }, branch_lane: 5, region: "Throw" }),
  ],
  connections: [
    { from: "interact", from_pin: "Pressed", to: "toggle", to_pin: "exec" },
    { from: "is_holding", from_pin: "bHolding", to: "toggle", to_pin: "Condition" },
    { from: "toggle", from_pin: "then", to: "drop", to_pin: "exec" },
    { from: "drop_handle", from_pin: "PhysicsHandle", to: "drop", to_pin: "self" },
    { from: "drop", from_pin: "then", to: "drop_clear_component", to_pin: "exec" },
    { from: "drop_clear_component", from_pin: "then", to: "drop_clear_state", to_pin: "exec" },
    { from: "toggle", from_pin: "else", to: "trace", to_pin: "exec" },
    { from: "camera", from_pin: "Camera", to: "camera_location", to_pin: "self" },
    { from: "camera", from_pin: "Camera", to: "camera_forward", to_pin: "self" },
    { from: "camera_forward", from_pin: "ReturnValue", to: "trace_offset", to_pin: "A" },
    { from: "grab_distance", from_pin: "GrabDistance", to: "trace_offset", to_pin: "B" },
    { from: "camera_location", from_pin: "ReturnValue", to: "trace_end", to_pin: "A" },
    { from: "trace_offset", from_pin: "ReturnValue", to: "trace_end", to_pin: "B" },
    { from: "camera_location", from_pin: "ReturnValue", to: "trace", to_pin: "Start" },
    { from: "trace_end", from_pin: "ReturnValue", to: "trace", to_pin: "End" },
    { from: "trace", from_pin: "then", to: "hit", to_pin: "exec" },
    { from: "trace", from_pin: "ReturnValue", to: "hit", to_pin: "Condition" },
    { from: "trace", from_pin: "OutHit", to: "break_hit", to_pin: "Hit" },
    { from: "hit", from_pin: "then", to: "grab", to_pin: "exec" },
    { from: "grab_handle", from_pin: "PhysicsHandle", to: "grab", to_pin: "self" },
    { from: "break_hit", from_pin: "HitComponent", to: "grab", to_pin: "Component" },
    { from: "break_hit", from_pin: "Location", to: "grab", to_pin: "GrabLocation" },
    { from: "grab", from_pin: "then", to: "store_component", to_pin: "exec" },
    { from: "break_hit", from_pin: "HitComponent", to: "store_component", to_pin: "HeldComponent" },
    { from: "store_component", from_pin: "then", to: "store_state", to_pin: "exec" },
    { from: "tick", from_pin: "then", to: "tick_gate", to_pin: "exec" },
    { from: "tick_state", from_pin: "bHolding", to: "tick_gate", to_pin: "Condition" },
    { from: "camera_forward", from_pin: "ReturnValue", to: "hold_offset", to_pin: "A" },
    { from: "hold_distance", from_pin: "HoldDistance", to: "hold_offset", to_pin: "B" },
    { from: "camera_location", from_pin: "ReturnValue", to: "hold_end", to_pin: "A" },
    { from: "hold_offset", from_pin: "ReturnValue", to: "hold_end", to_pin: "B" },
    { from: "tick_gate", from_pin: "then", to: "set_target", to_pin: "exec" },
    { from: "target_handle", from_pin: "PhysicsHandle", to: "set_target", to_pin: "self" },
    { from: "hold_end", from_pin: "ReturnValue", to: "set_target", to_pin: "NewLocation" },
    { from: "throw_input", from_pin: "Pressed", to: "throw_gate", to_pin: "exec" },
    { from: "throw_state", from_pin: "bHolding", to: "throw_gate", to_pin: "Condition" },
    { from: "throw_gate", from_pin: "then", to: "impulse", to_pin: "exec" },
    { from: "held", from_pin: "HeldComponent", to: "impulse", to_pin: "self" },
    { from: "impulse", from_pin: "then", to: "throw_release", to_pin: "exec" },
    { from: "throw_handle", from_pin: "PhysicsHandle", to: "throw_release", to_pin: "self" },
    { from: "throw_release", from_pin: "then", to: "throw_clear_component", to_pin: "exec" },
    { from: "throw_clear_component", from_pin: "then", to: "throw_clear_state", to_pin: "exec" },
  ],
};
const capCharacter = bp("/Game/MCPGenerated/BPV_Capstone_Character", "Character", [capstoneGraph]);
capCharacter.components = [{ class: "CameraComponent", name: "Camera", properties: { RelativeLocation: { x: 0, y: 0, z: 64 } } }, { class: "PhysicsHandleComponent", name: "PhysicsHandle" }];
capCharacter.variables = [
  { name: "GrabDistance", type: { category: "float" }, default: 1200, category: "Physics Pickup" },
  { name: "HoldDistance", type: { category: "float" }, default: 300, category: "Physics Pickup" },
  { name: "HeldComponent", type: { category: "object", subcategory_object: "/Script/Engine.PrimitiveComponent" }, default: null, category: "Physics Pickup" },
  { name: "bHolding", type: { category: "bool" }, default: false, category: "Physics Pickup" },
];
const capObject = bp("/Game/MCPGenerated/BPV_Capstone_PhysicsObject");
capObject.components = [{ class: "StaticMeshComponent", name: "PhysicsBody", properties: { StaticMesh: "/Engine/BasicShapes/Cube.Cube", Mobility: "Movable", BodyInstance: { bSimulatePhysics: true } } }];

export const BLUEPRINT_PRODUCTION_FIXTURES: FeatureRequest[] = [
  request("f1_basic_actor", [f1]),
  request("f2_interaction_system", [f2], ["interface_asset_creation", "interface_message", "delegate_nodes"]),
  { ...request("f3_cpp_hybrid_actor", [f3], ["cpp_generation"], { shared_base_class: true, strict_runtime_tests: true, asset_composition: true }), cpp: { classes: [{ name: "ABPVHybridSwitchBase", header: "Source/BlueprintProductionFixture/BPVHybridSwitchBase.h", source: "Source/BlueprintProductionFixture/BPVHybridSwitchBase.cpp", responsibility: "tested switch state" }] } },
  request("f4_stateful_door", [f4]),
  request("f5_inventory_component", [f5], ["user_defined_types", "loop_nodes", "delegate_nodes"], { large_data_transform: true, designer_settings: true }),
  request("f6_inheritance", [f6child, f6base], ["parent_change"]),
  request("f7_graph_cleanup", [f7]),
  request("f8_failure_rollback", [f8], ["rollback_fixture_injection"]),
  request("capstone_physics_pickup_throw", [capObject, capCharacter], ["interface_message", "delegate_nodes"], { reusable_runtime_system: true, performance_sensitive: true, asset_composition: true, designer_settings: true, event_wiring: true }),
];
