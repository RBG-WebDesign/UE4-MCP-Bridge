import { z } from "zod";
import type { PuerTSClient } from "../puerts-client.js";
import { SessionError } from "../puerts-client.js";
import type { ToolDefinition } from "../types.js";

const target = { actor: z.string().optional(), object_path: z.string().optional() };
const vector = z.object({ x: z.number().optional(), y: z.number().optional(), z: z.number().optional() }).strict();
const rotator = z.object({ pitch: z.number().optional(), yaw: z.number().optional(), roll: z.number().optional() }).strict();
const physicsActor = z.object({
  name: z.string().min(1).max(64),
  mesh: z.string(),
  location: vector.optional(),
  rotation: rotator.optional(),
  scale: vector.optional(),
  simulate_physics: z.boolean().optional(),
  mass_kg: z.number().optional(),
  linear_damping: z.number().optional(),
  angular_damping: z.number().optional(),
}).strict();

/** The value of a reflected property. Spelling the alternatives out matters:
    an untyped schema tells a client nothing, and a client with nothing to go
    on sends a struct or an array as JSON text instead of as JSON. */
const reflectedValue = z.union([
  z.string(),
  z.number(),
  z.boolean(),
  z.null(),
  z.array(z.unknown()),
  z.record(z.unknown()),
]).describe(
  "Value in the property's own reflected shape: a number for a float, "
  + "{\"x\":0,\"y\":0,\"z\":0} for a vector, {\"pitch\":0,\"yaw\":0,\"roll\":0} for a rotator, "
  + "an array of strings for Tags.",
);

/** One SimpleConstructionScript component of a generated Blueprint. */
const blueprintComponent = z.object({
  class: z.string().describe(
    "Component class as a reflected short name (\"StaticMeshComponent\") or a "
    + "full path (\"/Script/Engine.PointLightComponent\"). Must derive from ActorComponent.",
  ),
  name: z.string().min(1).max(64),
  attach_to: z.string().optional().describe(
    "Name of a component declared earlier in this array or already on the "
    + "Blueprint. Omit to add at the root.",
  ),
  properties: z.record(reflectedValue).optional().describe(
    "Reflected properties applied to the component template, by property name: "
    + "{\"StaticMesh\": \"/Engine/BasicShapes/Cube.Cube\", "
    + "\"RelativeScale3D\": {\"x\":2,\"y\":2,\"z\":2}}. An asset reference is the "
    + "object path as a string, and an array of them for a list such as "
    + "OverrideMaterials; null clears a reference. A property name the component "
    + "class does not have, or an asset path that does not resolve, rejects the "
    + "whole spec before the asset is touched.",
  ),
}).strict();

/** One Blueprint member variable. */
const blueprintVariable = z.object({
  name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/),
  type: z.string().describe(
    "One of bool, byte, int, int64, float, string, name, text, vector, "
    + "vector2d, rotator, transform, linearcolor; or a prefixed form: "
    + "\"object:StaticMeshComponent\", \"class:/Script/Engine.Actor\", "
    + "\"struct:/Script/Engine.HitResult\", \"enum:/Script/Engine.EComponentMobility\". "
    + "A prefixed class takes a reflected short name or a full path.",
  ),
  // An array is in the union only so the native side answers with the reason
  // an array default is refused, instead of the client rejecting it with a
  // union-mismatch dump that never names the rule.
  default: z.union([
    z.string(), z.number(), z.boolean(), z.null(), z.record(z.unknown()), z.array(z.unknown()),
  ]).optional().describe(
    "Value in the variable's own shape: true for a bool, a number for a float, "
    + "{\"x\":0,\"y\":0,\"z\":0} for a vector, an object path string for an object "
    + "reference. Not accepted on an array or set variable.",
  ),
  container: z.enum(["none", "array", "set"]).optional().describe("Default none."),
  category: z.string().optional().describe("Details-panel category for the variable."),
}).strict();

/** The node types UBlueprintGraphBuilderLibrary::BuildBlueprintFromJSON can
    spawn today. The authority is GetSupportedNodeTypes() in
    Plugins/MCPBridge/Source/MCPBridgeGraphBuilder; the native command
    re-validates against it, so this enum can only reject earlier, never let a
    node type through that the builder cannot build. The first eleven have a
    dispatch case in the builder; the rest are served by the mutator's
    FBPNodeRegistry factory table, which the builder now calls. */
const blueprintNodeType = z.enum([
  "BeginPlay",
  "Tick",
  "ActorBeginOverlap",
  "ActorEndOverlap",
  "PrintString",
  "CallFunction",
  "Operator",
  "Delay",
  "Branch",
  "Sequence",
  "Comment",
  "Event",
  "CustomEvent",
  "VariableGet",
  "VariableSet",
  "Cast",
  "Select",
  "Knot",
  "MakeStruct",
  "BreakStruct",
  "FormatText",
  "SpawnActor",
  "SwitchInt",
  "SwitchString",
  "MultiGate",
  "DoOnceMultiInput",
  "InputKey",
]);

/** The symbolic operators the Operator node type accepts. Authority is
    GetSupportedOperators() in the builder; each is a verified
    UKismetMathLibrary or UKismetStringLibrary call. */
const blueprintOperators = [
  "not_bool", "and_bool", "or_bool",
  "add_float", "subtract_float", "multiply_float", "divide_float",
  "greater_float", "less_float", "greater_equal_float", "less_equal_float", "equal_float",
  "clamp_float", "lerp_float",
  "add_int", "subtract_int", "greater_int", "less_int", "equal_int",
  "make_vector", "add_vector", "multiply_vector_float",
  "append_string", "vector_to_string", "bool_to_string",
] as const;

const blueprintGraphNode = z.object({
  id: z.string().min(1).describe("Unique within this graph; connections address nodes by it."),
  type: blueprintNodeType,
  params: z.record(z.unknown()).optional().describe(
    "Routing keys the node type needs, plus pin defaults by pin name. Routing: "
    + "CallFunction {class, function}; Operator {op} from "
    + blueprintOperators.join(", ")
    + "; VariableGet and VariableSet {var_name}, plus {scope} which is \"self\" "
    + "(default, the Blueprint's own member) or \"target\" with {target_class}, "
    + "which reads or writes the variable on another object through a self input "
    + "pin; VariableSet also takes "
    + "{value} as an alias for the pin named after the variable; Delay takes "
    + "the Duration pin; Event {parent_class, event_name}; CustomEvent "
    + "{event_name, parameters}; Cast {target_class, purity}; MakeStruct and "
    + "BreakStruct {struct_type}; SpawnActor {actor_class}; Sequence "
    + "{num_outputs}; SwitchInt {start_index, num_cases, has_default}; "
    + "SwitchString {case_values, has_default, is_case_sensitive}; MultiGate "
    + "{num_outputs, is_random, loop, start_index_from_zero}; DoOnceMultiInput "
    + "{num_inputs}; InputKey {fkey_name, consume_input, execute_when_paused, "
    + "override_parent}; Comment {text, width, height}. Everything else is a pin "
    + "default: a number for a float pin, true/false for a bool pin, "
    + "{\"x\":0,\"y\":0,\"z\":0} for a vector pin, an object path string for an "
    + "object pin.",
  ),
  x: z.number().optional(),
  y: z.number().optional(),
}).strict();

const blueprintGraph = z.object({
  nodes: z.array(blueprintGraphNode).max(200),
  connections: z.array(z.object({
    from: z.string().describe("nodeId.pinRole on the source node."),
    to: z.string().describe("nodeId.pinRole on the target node."),
  }).strict()).max(400).optional(),
}).strict().describe(
  "Event graph description. Pin roles: \"exec\" is direction-aware (Then on "
  + "the source, Execute on the target), \"then\" is always Then, and any "
  + "other role is a literal pin name, so data pins wire through the same array. "
  + "Literal names worth knowing: Branch has Condition, then, else; Sequence has "
  + "then_0, then_1 (lower case, underscore); a CallFunction has self for its "
  + "target, ReturnValue for its result and the UFUNCTION parameter name for "
  + "everything else; an Operator has A and B (or X/Y/Z, Value/Min/Max) and "
  + "ReturnValue; a VariableGet and a VariableSet both name their data pin after "
  + "the variable, and with scope \"target\" both also take self, the object the "
  + "variable lives on; Delay has Duration; Cast has Object, then, CastFailed and "
  + "\"AsResult\" for the cast result (the node's own pin name is \"As\" plus the "
  + "target type's display name, which a caller cannot compute for a Blueprint "
  + "class, so AsResult asks the node); Knot has InputPin and OutputPin; MakeStruct and "
  + "BreakStruct name their struct pin after the struct; MultiGate has \"Out 0\", "
  + "\"Out 1\"; SwitchInt has Default plus one pin per case; InputKey has Pressed, "
  + "Released and Key; ActorEndOverlap and "
  + "ActorBeginOverlap have OtherActor. DoOnceMultiInput builds its pin names "
  + "from localized text, so it spawns but cannot be wired by name here, and "
  + "SpawnActor's Spawn Transform is a by-ref pin that needs a wired input.",
);

/** The widget types FWidgetClassRegistry resolves, grouped by the child rule
    each one carries. The authority is RegisterTypes() in
    Plugins/MCPBridge/Source/MCPBridgeGraphBuilder/Private/WidgetBuilder/
    WidgetClassRegistry.cpp; the native command re-validates against it, so
    this enum can only reject earlier. */
const widgetType = z.enum([
  // Panel: 0..N children.
  "CanvasPanel", "VerticalBox", "HorizontalBox", "Overlay", "ScrollBox", "GridPanel", "WrapBox",
  // Content: 0..1 child.
  "Button", "Border", "SizeBox", "ScaleBox",
  // Leaf: no children.
  "TextBlock", "RichTextBlock", "Image", "Spacer", "ProgressBar", "Slider", "CheckBox", "EditableTextBox",
]);

/** Layout owned by the parent, not by the widget. Which fields apply depends
    on the slot class the parent creates: a canvas slot takes position, size,
    alignment, zOrder and autoSize; a box or overlay slot takes padding and
    the two alignment enums; a grid slot takes row and column. Fields that do
    not apply to the slot the parent made are ignored rather than refused,
    because the same subtree is often moved between parents. */
const widgetSlot = z.object({
  position: z.object({ x: z.number(), y: z.number() }).strict().optional(),
  size: z.object({ x: z.number(), y: z.number() }).strict().optional(),
  alignment: z.object({ x: z.number(), y: z.number() }).strict().optional(),
  padding: z.object({
    left: z.number(), top: z.number(), right: z.number(), bottom: z.number(),
  }).strict().optional(),
  zOrder: z.number().optional(),
  autoSize: z.boolean().optional(),
  row: z.number().optional(),
  column: z.number().optional(),
  rowSpan: z.number().optional(),
  columnSpan: z.number().optional(),
  horizontalAlignment: z.string().optional().describe("Left, Center, Right or Fill."),
  verticalAlignment: z.string().optional().describe("Top, Center, Bottom or Fill."),
}).strict();

/** One widget of the tree. Recursive, because a widget tree is a tree; the
    native validator enforces the per-category child counts and unique names,
    which a schema cannot express. */
interface WidgetNodeInput {
  type: z.infer<typeof widgetType>;
  name: string;
  properties?: Record<string, unknown>;
  slot?: z.infer<typeof widgetSlot>;
  children?: WidgetNodeInput[];
}

const widgetNode: z.ZodType<WidgetNodeInput> = z.lazy(() => z.object({
  type: widgetType,
  name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/).describe(
    "Unique across the whole tree. This is the widget's name in the asset, so "
    + "it is what a read-back and a BindWidget both address.",
  ),
  properties: z.record(z.unknown()).optional().describe(
    "Widget-intrinsic values by name. All types take visibility (Visible, "
    + "Hidden, Collapsed, HitTestInvisible, SelfHitTestInvisible), renderOpacity "
    + "and isEnabled. TextBlock and RichTextBlock take text and justification "
    + "(Left, Center, Right), TextBlock also color {r,g,b,a} in 0..1. ProgressBar "
    + "takes percent (0..1), fillColorAndOpacity, barFillType and isMarquee. "
    + "Slider takes value, minValue, maxValue, stepSize and orientation. CheckBox "
    + "takes isChecked. EditableTextBox takes text, hintText and isReadOnly. "
    + "ScaleBox takes stretch, stretchDirection and userSpecifiedScale. A name "
    + "the widget type does not support rejects the whole spec.",
  ),
  slot: widgetSlot.optional(),
  children: z.array(widgetNode).max(64).optional().describe(
    "Panel types take any number, content types take at most one, leaf types "
    + "take none. Array order is tree order.",
  ),
}).strict());

const specs = [
  ["puerts_diagnostic", "diagnostic", "Prove the in-process PuerTS context, game thread, named-pipe transport, and actor-query timing.", z.object({ actor_limit: z.number().optional() }).strict()],
  ["puerts_find_assets", "find_assets", "Find UE4.27 assets by path, type, or name.", z.object({ path: z.string().optional(), type: z.string().optional(), name: z.string().optional(), recursive: z.boolean().optional(), limit: z.number().optional() }).strict()],
  ["puerts_find_actors", "find_actors", "Find actors in the current editor level.", z.object({ name: z.string().optional(), type: z.string().optional(), limit: z.number().optional() }).strict()],
  ["puerts_read_property", "read_property", "Read an Unreal reflected property.", z.object({ ...target, property: z.string() }).strict()],
  ["puerts_set_property", "set_property", "Set an approved Unreal reflected property in a transaction.", z.object({ ...target, property: z.string(), value: reflectedValue }).strict()],
  ["puerts_call_function", "call_function", "Call a native-approved Unreal function.", z.object({ actor: z.string(), function: z.string(), arguments: z.array(z.unknown()).optional() }).strict()],
  ["puerts_spawn_actor", "spawn_actor", "Spawn an actor in a transaction.", z.object({ class_path: z.string(), location: vector.optional(), rotation: rotator.optional() }).strict()],
  ["puerts_delete_actor", "delete_actor", "Delete an actor in a transaction. Requires confirm=true.", z.object({ actor: z.string(), confirm: z.literal(true) }).strict()],
  ["puerts_sky_shader_create", "sky_shader_create", "Create an animated native HLSL aurora sky material and apply it to a sky sphere in one transaction.", z.object({ asset_path: z.string().optional(), sky_actor: z.string().optional() }).strict()],
  ["puerts_blueprint_build", "blueprint_build",
    "Create or update a compiled Blueprint asset from one JSON spec: parent class, "
    + "components with their template properties, and an event graph. A component property "
    + "takes the value in its own reflected shape, and an asset reference as an object path "
    + "string, so a StaticMeshComponent can be given a mesh and materials. Member variables "
    + "are declared the same way and are reachable from the graph through VariableGet and "
    + "VariableSet nodes. Supported graph node types are BeginPlay, Tick, ActorBeginOverlap, "
    + "ActorEndOverlap, PrintString, CallFunction, Operator, Delay, Branch, Sequence, "
    + "Comment, Event, CustomEvent, VariableGet, VariableSet, Cast, Select, Knot, MakeStruct, "
    + "BreakStruct, FormatText, SpawnActor, SwitchInt, SwitchString, MultiGate, "
    + "DoOnceMultiInput and InputKey; anything else is rejected before the asset is touched. "
    + "InputKey binds a literal FKey (params.fkey_name, for example \"LeftShift\") and needs no "
    + "project input mapping, which is why it is the one input node type advertised. Rerunning the "
    + "same spec converges: the asset is loaded rather than duplicated, an existing component "
    + "or variable of the same name and type is left alone, and the event graph is rebuilt "
    + "from the spec. A component or variable that exists under a different type is an error, "
    + "never a silent retype. "
    + "The parent class is any class Unreal allows a Blueprint of, including UObject, "
    + "SaveGame and ActorComponent, so a data-only Blueprint is authorable here. "
    + "Actor-only capability is gated rather than the parent being refused: with a "
    + "non-Actor parent the components array and the BeginPlay, Tick, ActorBeginOverlap, "
    + "ActorEndOverlap and InputKey node types are rejected by name before the asset "
    + "exists, and variables plus the parent-neutral node types build normally. "
    + "A VariableGet or VariableSet takes scope \"target\" with target_class to read or write "
    + "the variable on another object through its self pin, and a Cast's result is addressed "
    + "by the pin role AsResult. Those two are what a save/load round trip needs. "
    + "Every graph connection must resolve to real pins: the response's graph.connection_count "
    + "is the number of links actually made, and any shortfall against the number requested "
    + "fails the build with the dropped pairs named in errors and in "
    + "graph.unresolved_connections, so a graph with a hole in it is never saved. A pure node "
    + "has no exec pins, and a const BlueprintCallable UFUNCTION that returns a value is pure. "
    + "Assets are limited to /Game/MCPGenerated/. Response reports compile status with "
    + "compiler errors and warnings; the asset is only saved when it built clean.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native "
        + "command enforces the same limit; this rejects earlier, at the client.",
      ),
      parent_class: z.string().optional().describe(
        "Parent class as a reflected short name (\"Character\") or a full path "
        + "(\"/Script/Engine.SaveGame\"). Any class Unreal allows a Blueprint of, "
        + "which includes non-Actor classes; components and the actor event node "
        + "types then need an Actor parent. Defaults to Actor.",
      ),
      components: z.array(blueprintComponent).max(64).optional(),
      variables: z.array(blueprintVariable).max(64).optional().describe(
        "Blueprint member variables, converged the same way components are.",
      ),
      graph: blueprintGraph.optional(),
      compile: z.boolean().optional().describe("Default true."),
      save: z.boolean().optional().describe("Default true. A build with errors is never saved."),
      clear_existing_graph: z.boolean().optional().describe(
        "Default true: the event graph is replaced by the spec rather than appended to.",
      ),
      remove_unlisted: z.object({
        variables: z.boolean().optional(),
        components: z.boolean().optional(),
        functions: z.boolean().optional(),
        macros: z.boolean().optional(),
        graph_nodes: z.boolean().optional(),
        interfaces: z.boolean().optional(),
      }).strict().optional().describe(
        "Opt-in downward convergence, off by default: nothing is ever removed unless a scope "
        + "is set true. Only 'variables' is implemented; any other scope set true is REJECTED "
        + "with an unsupported_scope error rather than ignored, so a caller is never told a "
        + "prune happened when it did not. Variable removal only ever considers variables this "
        + "builder previously declared (stamped MCPManaged): inherited variables, native C++ "
        + "UPROPERTYs, engine-generated variables and anything a human added carry no stamp and "
        + "are reported as protected_variables instead. A variable referenced by graph nodes is "
        + "blocked unless force_remove_referenced is also set.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Return the convergence plan and change nothing: current_variables, "
        + "desired_variables, variables_to_add/update/remove, protected_variables, "
        + "referenced_variables, blocked_removals and expected_change_count. Read-only - it "
        + "returns before the mutation section, so no asset is created and no package is "
        + "dirtied even when the asset does not exist yet.",
      ),
      force_remove_referenced: z.boolean().optional().describe(
        "Default false. Allow removal of a managed variable that graph nodes reference, "
        + "deleting those nodes with it. Every deleted node is reported in "
        + "convergence.removed_reference_nodes.",
      ),
    }).strict()],
  ["puerts_blueprint_graph_patch", "blueprint_graph_patch",
    "Change selected existing Blueprint graph state without rebuilding or clearing the graph. "
    + "blueprint_build is desired-state: it takes a whole graph and makes the asset match, which "
    + "means changing one pin default on a graph of forty nodes requires restating all forty "
    + "correctly or losing what was not mentioned. This is the other shape. operations is an "
    + "ordered batch of add_node, update_node, remove_node, set_pin_default, connect_pins, "
    + "disconnect_pins and move_node. Every node is addressed by a selector that must resolve to "
    + "exactly one node: node_guid, or a structural combination of type, node_class, var_name, "
    + "function and position, or new_id to name a node added earlier in the same batch. A bare "
    + "object id is REFUSED, because a UObject name changes whenever a node is recreated and a "
    + "patch that targets by it edits whatever holds the name afterwards. A selector matching "
    + "nothing, or more than one node, is a refusal and not a guess, and the whole batch resolves "
    + "before anything mutates. plan_only is read-only and reports matched_nodes, "
    + "unmatched_selectors, ambiguous_selectors, the per-kind change lists and "
    + "expected_change_count; predicted_structure_hash is given only for a no-op batch, where it "
    + "is the current hash by definition, and prediction_unavailable_reason says so otherwise. "
    + "Applying runs in one transaction, compiles, reads the asset back independently, verifies "
    + "the change is really present, and saves only after that passes; any failure or mismatch "
    + "rolls the whole batch back and leaves the graph as it was found. Rerunning the same patch "
    + "is a no-op that reports converged and does not save. Adds no node types of its own: "
    + "add_node accepts the same vocabulary blueprint_build does.",
    z.object({
      asset_path: z.string(),
      graph: z.string().optional(),
      operations: z.array(z.record(z.unknown())).min(1),
      plan_only: z.boolean().optional(),
      compile: z.boolean().optional(),
      save: z.boolean().optional(),
      verify: z.boolean().optional(),
    }).strict()],
  ["puerts_blueprint_member_patch", "blueprint_member_patch",
    "Change a Blueprint's MEMBERS incrementally: variables, functions, interfaces, event "
    + "dispatchers and components. puerts_blueprint_graph_patch owns nodes and pins and cannot "
    + "reach any of these; puerts_blueprint_build reaches them only by restating the whole asset. "
    + "operations is an ordered batch of add_variable, remove_variable, set_variable_default, "
    + "add_function, remove_function, add_interface, remove_interface, add_event_dispatcher, "
    + "remove_event_dispatcher, remove_component and rename_component. "
    + "The whole batch is resolved and classified before the first mutation runs, because each "
    + "underlying mutator entry point recompiles the Blueprint: an unloadable interface path, a "
    + "default value the variable's type cannot hold, a rename onto a name already taken, a "
    + "remove_variable naming an inherited or native property, or two operations on the same "
    + "member in one batch are all refusals that mutate nothing. There is no change-variable-type "
    + "primitive, so add_variable against an existing variable of a different type is refused by "
    + "name rather than silently accepted. "
    + "An operation whose result is already present is reported in unchanged_operations and not "
    + "repeated, so a rerun applies nothing, reports converged and leaves no dirty package. "
    + "plan_only is read-only and returns operations_to_apply, unchanged_operations, "
    + "expected_change_count and pre_member_hash; predicted_member_hash is given only for a no-op "
    + "batch, where it is the current hash by definition. "
    + "Applying runs in one transaction, compiles, re-reads every operation's own condition from "
    + "the asset rather than trusting the mutator's report, and saves only after that passes; any "
    + "failure rolls the whole batch back, and whether the rollback actually restored the members "
    + "is decided by reading them again and reported as rollback_succeeded. "
    + "pre_member_hash and post_member_hash are the same hash puerts_graph_inspect returns as "
    + "member_structure_hash_sha1, so a caller can verify a patch against an independent read.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The Blueprint must "
        + "already exist: patching never creates one.",
      ),
      operations: z.array(z.record(z.unknown())).min(1).describe(
        "Ordered batch. Each entry is {op, ...}: "
        + "{op:\"add_variable\", name, type:{category:\"float\"}, default?, category?}, "
        + "{op:\"remove_variable\", name}, "
        + "{op:\"set_variable_default\", name, default}, "
        + "{op:\"add_function\", name, inputs?:[{name,type}], outputs?:[{name,type}]}, "
        + "{op:\"remove_function\", name}, "
        + "{op:\"add_interface\", path}, {op:\"remove_interface\", path}, "
        + "{op:\"add_event_dispatcher\", name, parameters?:[{name,type}]}, "
        + "{op:\"remove_event_dispatcher\", name}, "
        + "{op:\"remove_component\", name}, {op:\"rename_component\", from, to}. "
        + "type is the same Type Descriptor puerts_graph_inspect reports for a variable, and a "
        + "partial descriptor matches a variable whose reported type agrees on the fields given.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Classify the batch and change nothing.",
      ),
      compile: z.boolean().optional().describe(
        "Default true. A Blueprint that does not compile after the batch is rolled back, never saved.",
      ),
      save: z.boolean().optional().describe(
        "Default true. A batch that applied nothing does not save either way.",
      ),
      verify: z.boolean().optional().describe(
        "Default true. Re-read each operation's condition from the asset after applying. Turning "
        + "this off removes the only check that distinguishes a change from a report of one.",
      ),
    }).strict()],
  ["puerts_widget_build", "widget_build",
    "Create or replace a compiled UMG Widget Blueprint from one JSON widget tree. The tree "
    + "is a hierarchy of typed, named widgets: each carries optional widget-intrinsic "
    + "properties and an optional slot object holding the layout its parent owns. The whole "
    + "tree is validated before any asset is touched, so an unsupported widget type, a "
    + "duplicate name, a property the type does not have, or a child under a leaf widget is "
    + "a rejection rather than a half-built asset. Rerunning a spec converges by replacing "
    + "the tree of the asset already at that path; unlike a Blueprint's components and "
    + "variables, a widget tree has no per-widget identity to merge against, so the spec is "
    + "the whole tree. Assets are limited to /Game/MCPGenerated/. The response reports the "
    + "hierarchy read back out of the built asset, not the request, along with "
    + "generated_class_path, which is the class a graph needs to create the widget at "
    + "runtime.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native "
        + "command enforces the same limit; this rejects earlier, at the client.",
      ),
      tree: z.object({
        root: widgetNode.describe("The root widget. Its own slot is ignored: it has no parent."),
      }).passthrough().describe(
        "The widget tree. Only root is required; the native builder also accepts an "
        + "animations array, which is not schema-checked here.",
      ),
      save: z.boolean().optional().describe("Default true. A tree that did not compile is never saved."),
    }).strict()],
  ["puerts_graph_inspect", "graph_inspect",
    "Read an existing Blueprint back as machine-readable JSON: parent class, "
    + "SimpleConstructionScript components, member variables, implemented interfaces, "
    + "user functions, the graph list, and one graph described in the shape "
    + "puerts_blueprint_build writes. This is the inverse direction of the builder and it "
    + "is READ ONLY: no transaction is opened, the response carries no transaction id, "
    + "nothing is compiled or saved, and the asset's package dirty flag is read before and "
    + "after the work and reported as package_dirty_before / package_dirty_after so a "
    + "caller can verify that reading did not write. "
    + "Each graph node reports its builder node type (BeginPlay, CallFunction, Operator, "
    + "VariableSet and the rest of the 26-word vocabulary), or null with node_class when "
    + "this builder has no word for it, in which case the node is also listed under "
    + "graph.unmapped_nodes. params holds the routing keys the node type keeps as fields "
    + "plus every input pin default the caller authored, in the JSON shape a spec writes "
    + "it; a struct pin default other than vector, rotator or linear color is reported as "
    + "its raw pin text and named in graph.lossy_pin_defaults. "
    + "Node identity is OBSERVED, not authored: a node is addressed by its object name "
    + "(id) and its NodeGuid, because the id a build spec wrote is not persisted on the "
    + "node, so an inspected node cannot be matched back to the spec line that made it. "
    + "Every array is canonically ordered - nodes by NodeGuid, pins by direction then "
    + "PinId, connections by their endpoint identities, components, variables, functions "
    + "and graphs by name - so two reads of an unchanged Blueprint produce the same "
    + "content. Object key order is the JSON serializer's and is not canonical; hash a "
    + "key-sorted form if you are comparing runs byte for byte. "
    + "Reading is allowed anywhere under /Game and /Engine, unlike authoring, which is "
    + "limited to /Game/MCPGenerated/.",
    z.object({
      asset_path: z.string().describe(
        "The Blueprint, as a package path (\"/Game/MCPGenerated/BP_ProbeDoor\") or as the "
        + "object path other tools hand back (\"/Game/MCPGenerated/BP_ProbeDoor.BP_ProbeDoor\"). "
        + "Limited to /Game and /Engine.",
      ),
      graph_name: z.string().optional().describe(
        "Which graph to describe. Omit for the event graph. Any name from the response's "
        + "graphs array works, including function and macro graphs.",
      ),
      include_pins: z.boolean().optional().describe(
        "Default false. Attach every pin of every node with its PinId, name, direction, "
        + "resolved type, all three default slots (value, object, text), the node's own "
        + "autogenerated default, and its links. Roughly ten times the payload on a large "
        + "graph, so it is off unless a caller needs pin-level detail.",
      ),
    }).strict()],
  ["puerts_behavior_tree_build", "behavior_tree_build",
    "Create or update a UE4.27 BehaviorTree asset with its Blackboard from one JSON spec, in "
    + "one transaction: blackboard keys, blackboard assignment, and the full node graph, then "
    + "compile-free save (Behavior Trees have no compile step). The graph replaces the tree's "
    + "root only when every node builds, so a failed spec leaves an existing tree untouched, "
    + "and a rerun of the same spec converges. Node structure: {id, type, name?, params?, "
    + "children?, decorators?, services?}. Composites: Selector, Sequence, SimpleParallel. "
    + "Tasks: MoveTo, Wait, WaitBlackboardTime, RotateToFaceBBEntry, PlayAnimation, MakeNoise, "
    + "RunBehavior, PlaySound, FinishWithResult, SetTagCooldown. Decorators: Blackboard, "
    + "ForceSuccess, Loop, TimeLimit, Cooldown, CompareBBEntries, IsAtLocation, DoesPathExist, "
    + "TagCooldown, ConditionalLoop, KeepInCone, IsBBEntryOfClass. Services: DefaultFocus, "
    + "RunEQS. Unknown types are rejected before the asset is touched. params keys are snake_case (blackboard_key, wait_time, acceptable_radius, random_deviation); an unknown params key is currently dropped without a warning, so verify with puerts_behavior_tree_inspect. params values are "
    + "strings (\"5.0\", \"TargetActor\"); a params key naming a blackboard key is validated "
    + "against the keys that exist. The response reports the keys actually on the blackboard "
    + "asset, read back rather than echoed.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "BehaviorTree package path under /Game/MCPGenerated/, no asset-name suffix.",
      ),
      blackboard_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).optional().describe(
        "Blackboard asset path under /Game/MCPGenerated/. Defaults to <asset_path>_BB. "
        + "Point several trees at one path to share a blackboard.",
      ),
      keys: z.array(z.object({
        name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/),
        type: z.string().describe("Bool, Int, Float, String, Name, Vector, Rotator, Object, or Class."),
        base_class: z.string().optional().describe(
          "For Object and Class keys: the required base, e.g. \"/Script/Engine.Actor\".",
        ),
      }).strict()).max(64).optional().describe(
        "Blackboard keys. Existing keys with the same name are left alone (idempotent).",
      ),
      root: z.record(z.unknown()).describe(
        "The root node of the tree, usually a composite. Required: a Behavior Tree without "
        + "nodes does nothing in PIE.",
      ),
      save: z.boolean().optional().describe("Default true. A tree that did not build cleanly is never saved."),
    }).strict()],
  ["puerts_behavior_tree_inspect", "behavior_tree_inspect",
    "Read an existing UE4.27 BehaviorTree and its Blackboard back as machine-readable JSON. "
    + "The read half of puerts_behavior_tree_build, and READ ONLY: no transaction is opened, "
    + "nothing is compiled or saved, and the package dirty flag is reported before and after "
    + "the read (package_dirty_before / package_dirty_after) so the claim is checkable. "
    + "Returns the tree class, blackboard path, the root composite as a nested structure "
    + "(kind: composite/task/decorator/service), child_index order, decorators attached to "
    + "the child they guard, root_decorators, per-node class_path and name, editor-visible "
    + "node properties, blackboard keys referenced by nodes (from FBlackboardKeySelector "
    + "fields), the blackboard's own key names and types, counts per kind, unsupported_fields "
    + "for anything reflection could not express, and structure_hash_sha1 - a canonical hash "
    + "of identity/class/name/keys in traversal order, so two reads of an unchanged tree "
    + "compare by hash. Node identity is DERIVED (identity_kind: \"derived\"): UE4.27 BT "
    + "nodes carry no GUIDs, so a node is addressed by its traversal path "
    + "(parent/childIndex:Class:Name); a renamed or reordered node is a different identity "
    + "on purpose. Unknown node classes are reported with their class_path and properties "
    + "rather than dropped. Reading is allowed anywhere under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The BehaviorTree, as a package path (\"/Game/MCPGenerated/BT_Patrol\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_widget_inspect", "widget_inspect",
    "Read an existing UE4.27 Widget Blueprint back as machine-readable JSON. "
    + "The independent read half of puerts_widget_build, and READ ONLY: no transaction is "
    + "opened, nothing is compiled or saved, and the package dirty flag is reported before "
    + "and after the read (package_dirty_before / package_dirty_after) so the claim is "
    + "checkable. Returns parent_class, generated_class_path, the root widget as a nested "
    + "hierarchy with child_index order, and per widget: name, class, class_path, "
    + "is_variable, editable properties (text values, visibility, enabled state and "
    + "everything else reflection can express), and its slot. A CanvasPanelSlot reports "
    + "anchors, offsets, alignment, z_order and auto_size, plus the position/size pair "
    + "puerts_widget_build's own report uses so the two can be compared field for field. "
    + "named_slots carries content held by INamedSlotInterface hosts, which is NOT "
    + "reachable through panel children. Also returns exposed variables, bindings, "
    + "animations, unsupported_fields for anything reflection could not express, and "
    + "structure_hash_sha1 - a canonical hash of identity/class/name/variable-flag in "
    + "traversal order, so two reads of an unchanged widget compare by hash and a text "
    + "edit does not read as a reshape. Widget identity is DERIVED "
    + "(identity_kind: \"derived\"): UE4.27 UMG widgets carry no GUIDs, so a widget is "
    + "addressed by its traversal path (parent/childIndex:Class:Name); a renamed or "
    + "reordered widget is a different identity on purpose. Reading is allowed anywhere "
    + "under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The Widget Blueprint, as a package path (\"/Game/MCPGenerated/WBP_HUD\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blueprint_inspect", "anim_blueprint_inspect",
    "Read an existing UE4.27 Animation Blueprint back as machine-readable JSON. "
    + "The read half of puerts_anim_blueprint_build, and READ ONLY: no transaction is opened, "
    + "nothing is compiled or saved, and the package dirty flag is reported before and after "
    + "the read (package_dirty_before / package_dirty_after) so the claim is checkable. "
    + "Returns target_skeleton, generated_class_path, parent_class, blueprint_status (the "
    + "STORED compile status, since reading never compiles), anim_graphs with every node and "
    + "its editable properties, state_machines with entry_state, states (and conduits) and "
    + "their inner pose graphs, transitions keyed by from->to with crossfade_duration, "
    + "priority_order, bidirectional, blend_mode and logic_type, and per transition a "
    + "condition object whose kind is \"rule_graph\", \"automatic_time_remaining\" or "
    + "\"none\" - an automatic rule has no rule graph at all, so reporting only the graph "
    + "would make it look like a missing condition. Also returns cached_poses with the nodes "
    + "that read each cache, blend_nodes (blend spaces, blend lists, layered bone blends, "
    + "two-way blends), member variables, counts, unsupported_fields for anything reflection "
    + "could not express, and structure_hash_sha1 - a canonical hash of identity, class, "
    + "state and transition edges, so two reads of an unchanged asset compare by hash and "
    + "retuning a blend time does not read as a reshape. Asset references (a state's "
    + "AnimSequence, a blend space, a slot name) arrive inside each node's properties. Node "
    + "identity is DERIVED (identity_kind: \"derived\"): the id a build spec wrote is not "
    + "persisted on the node, so a node is addressed by its traversal path. Reading is "
    + "allowed anywhere under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The Animation Blueprint, as a package path (\"/Game/MCPGenerated/ABP_Hero\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_montage_inspect", "anim_montage_inspect",
    "Read an existing UE4.27 Animation Montage back as machine-readable JSON. READ ONLY, on "
    + "the same terms as the other inspectors: no transaction, no compile, no save, and the "
    + "package dirty flag reported before and after. Returns sequence_length, the skeleton, "
    + "sync_group, blend in/out times with blend_out_trigger_time and enable_auto_blend_out, "
    + "sections IN MONTAGE ORDER with their next_section chain (order is meaningful here and "
    + "is deliberately not sorted), slot tracks with the animation segments inside them and "
    + "each segment's source asset and play rate, notifies with trigger_time, "
    + "end_trigger_time, duration, notify and notify-state classes, is_state and "
    + "is_branching_point, and structure_hash_sha1. Notifies are read explicitly rather than "
    + "by reflection because UE4.27 declares them UPROPERTY() with no Edit flag, so a "
    + "property walk would silently drop them. "
    + "There is deliberately NO montage write tool: sections and notifies are "
    + "FAnimLinkableElement values that must be re-linked against slot segments, and UE4.27 "
    + "exposes no atomic operation for rebuilding the next_section chain, so a half-applied "
    + "edit would leave a montage that plays the wrong thing. Reading is allowed anywhere "
    + "under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The Animation Montage, as a package path (\"/Game/Anim/AM_Attack\") or the object "
        + "path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blend_space_inspect", "anim_blend_space_inspect",
    "Read an existing UE4.27 Blend Space, Blend Space 1D or Aim Offset back as machine-readable "
    + "JSON. READ ONLY, on the same terms as the other inspectors: no transaction, no compile, "
    + "no save, and the package dirty flag reported before and after. Returns the target "
    + "skeleton, blend_space_class (which is what tells 1D from 2D, since UE4.27 exposes no "
    + "dimension count), all three axes with display_name, min, max and grid_divisions, and "
    + "every sample with its animation, x/y/z position and rate_scale. Samples are SORTED by "
    + "position and animation rather than reported in array order, because a blend space's "
    + "array order carries no meaning, so structure_hash_sha1 is stable across two reads of an "
    + "unchanged asset. A sample with no animation is reported as a warning rather than a silent "
    + "row: it contributes nothing at runtime. "
    + "There is deliberately no blend space write tool: UE4.27 rebuilds the triangulation from "
    + "the sample set, so a partly applied sample edit leaves a space that interpolates wrong "
    + "rather than one that fails, and there is no atomic sample-set replacement to wrap. "
    + "Reading is allowed anywhere under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The Blend Space, as a package path (\"/Game/Anim/BS_Locomotion\") or the object path "
        + "other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blueprint_build", "anim_blueprint_build",
    "Create a NEW UE4.27 Animation Blueprint from one JSON spec, in one transaction: member "
    + "variables, the anim graph pipeline, a state machine with its states and transitions, "
    + "and an optional event graph. The asset is compiled and the compile result is RETURNED "
    + "(compile_status, plus compiler errors and warnings) rather than assumed, and before "
    + "the asset is saved it is read back through puerts_anim_blueprint_inspect and every "
    + "requested state and transition must be present. A failure at any point cancels the "
    + "transaction and rolls the creation back: the response carries a cleanup object naming "
    + "what was created, what was removed, and whether any package is still dirty. "
    + "CREATE-ONLY, and this is the one thing to know before calling it: the command REFUSES "
    + "when an asset already exists at asset_path, so a rerun of the same spec is a refusal "
    + "and not a no-op. The underlying UE4.27 builder's rebuild path clears nothing, so "
    + "running it over an existing graph would append a second state machine rather than "
    + "converge, and restoring the previous contents of a pre-existing asset is not "
    + "something the rollback boundary can do. There is no anim_blueprint_patch for the same "
    + "reason. To change an existing Animation Blueprint, build to a new path. "
    + "Spec limits inherited from the builder (v1): variables are type \"bool\" only; "
    + "pipeline node types are \"StateMachine\" and \"Slot\"; every state plays one "
    + "AnimSequence; transition conditions are {type:\"bool_variable\", variable, value} or "
    + "{type:\"time_remaining\", threshold}, and a time_remaining condition sets UE4.27's "
    + "automatic remaining-time rule, which uses the transition's blend_time as the trigger "
    + "offset because 4.27 exposes no separate trigger time.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "AnimBlueprint package path under /Game/MCPGenerated/, no asset-name suffix. Must not "
        + "already exist.",
      ),
      skeleton_path: z.string().min(1).describe(
        "Object path of the target USkeleton, e.g. \"/Game/Characters/Hero_Skeleton\". "
        + "Required: an Animation Blueprint has no meaning without one.",
      ),
      variables: z.array(z.object({
        name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/),
        type: z.literal("bool").describe("bool is the only type the UE4.27 builder supports today."),
        default: z.string().optional().describe("\"true\" or \"false\". Defaults to \"false\"."),
      }).strict()).max(64).optional().describe(
        "Member variables the transition rules read. Existing names are left alone.",
      ),
      anim_graph: z.object({
        pipeline: z.array(z.object({
          id: z.string().min(1),
          type: z.string().describe("\"StateMachine\" or \"Slot\"."),
          name: z.string().min(1).describe("For a Slot node this is the slot name montages play into."),
        }).strict()).min(1),
      }).strict().describe(
        "The pose pipeline, wired in array order and terminating at the graph's Root node.",
      ),
      state_machine: z.object({
        states: z.array(z.object({
          id: z.string().min(1).describe("Referenced by transitions. Not persisted on the node."),
          name: z.string().min(1).describe("The state name the inspector reports back."),
          animation: z.string().min(1).describe("Object path of the AnimSequence this state plays."),
          looping: z.boolean().optional(),
          is_entry: z.boolean().optional().describe("Exactly one state should set this."),
        }).strict()).min(1),
        transitions: z.array(z.object({
          from: z.string().min(1).describe("A state id from states[]."),
          to: z.string().min(1).describe("A state id from states[]."),
          blend_time: z.number().optional().describe("Crossfade duration in seconds. Default 0.2."),
          condition: z.record(z.unknown()).describe(
            "{type:\"bool_variable\", variable, value} or {type:\"time_remaining\", threshold}.",
          ),
        }).strict()),
      }).strict(),
      event_graph: z.record(z.unknown()).optional().describe(
        "Optional event graph, in the same grammar puerts_blueprint_build's graph uses.",
      ),
      save: z.boolean().optional().describe(
        "Default true. An Animation Blueprint that did not compile and read back clean is never saved.",
      ),
    }).strict()],
  ["puerts_physics_build", "physics_build", "Build a validated static-mesh rigid-body scene in one transaction.", z.object({ actors: z.array(physicsActor).min(1).max(200) }).strict()],
  ["puerts_physics_observe", "physics_observe", "Read rigid-body transforms and velocities from the editor or PIE world.", z.object({ actors: z.array(z.string()).max(200).optional() }).strict()],
  ["puerts_viewport_screenshot", "viewport_screenshot", "Fit requested actors and save a PNG of the active editor viewport.", z.object({ actors: z.array(z.string()).max(200).optional(), filename: z.string().optional() }).strict()],
  ["puerts_save", "save", "Save approved project assets and the current level.", z.object({ assets: z.array(z.string()).optional(), level_path: z.string().optional() }).strict()],
  ["puerts_pie_start", "pie_start", "Request Play In Editor start.", z.object({}).strict()],
  ["puerts_pie_stop", "pie_stop", "Request Play In Editor stop.", z.object({}).strict()],
  ["puerts_get_logs", "get_logs", "Read captured Unreal logs and command output.", z.object({ maximum_lines: z.number().optional() }).strict()],
  ["puerts_undo", "undo", "Undo the exact last PuerTS transaction.", z.object({ transaction_id: z.string() }).strict()],
] as const;

/** One native pipe tool: its public MCP name, the runtime command it sends,
    and the schema every caller (direct or aliased) must satisfy. */
export interface NativeToolSpec {
  readonly name: string;
  readonly command: string;
  readonly description: string;
  readonly inputSchema: z.ZodType;
}

export const nativeToolSpecs: readonly NativeToolSpec[] = specs.map(
  ([name, command, description, inputSchema]) => ({ name, command, description, inputSchema }),
);

const specsByName = new Map(nativeToolSpecs.map((spec) => [spec.name, spec]));

/** Look up a native tool by its public puerts_* name. Throws on an unknown
    name so a mistyped alias target fails at construction, not at call time. */
export function nativeToolSpec(name: string): NativeToolSpec {
  const spec = specsByName.get(name);
  if (spec === undefined) throw new Error(`Unknown native tool: ${name}`);
  return spec;
}

/** The failure envelope the native lane returns instead of throwing, so a
    caller always sees the same response shape. */
export function nativeFailureEnvelope(
  errors: string[],
  message = "Native PuerTS command failed.",
): Record<string, unknown> {
  return {
    success: false,
    message,
    data: {},
    changed_assets: [],
    changed_actors: [],
    warnings: [],
    errors,
    log_output: [],
    transaction_id: "",
    transport: "named_pipe",
  };
}

/** Parameters that carry an object or an array. An MCP client that has no
    type information for a parameter sends structured input as JSON text, and
    that text reaches Unreal reflection as a string: a struct write dies in the
    runtime validator, an array write reaches C++ as a non-array. Listing the
    parameters here, one line per tool, keeps that decision reviewable. */
const structuredParameters: Readonly<Record<string, readonly string[]>> = {
  puerts_set_property: ["value"],
  puerts_call_function: ["arguments"],
  puerts_spawn_actor: ["location", "rotation"],
  puerts_physics_build: ["actors"],
  puerts_physics_observe: ["actors"],
  puerts_viewport_screenshot: ["actors"],
  puerts_save: ["assets"],
  puerts_blueprint_build: ["components", "variables", "graph"],
  puerts_widget_build: ["tree"],
  puerts_behavior_tree_build: ["keys", "root"],
  puerts_anim_blueprint_build: ["variables", "anim_graph", "state_machine", "event_graph"],
};

/** Round-trip budget per tool, in milliseconds. Absent means the 7 second
    default in PuerTSClient. Asset authoring blocks the game thread for as
    long as Unreal needs to compile and save, and a client-side timeout while
    the editor is still working reports a failure for work that succeeds. */
const commandTimeouts: Readonly<Record<string, number>> = {
  puerts_blueprint_build: 30000,
  // Every mutator entry point recompiles the Blueprint, so a batch costs one
  // compile per applied operation. 30s is a realistic ceiling for a single
  // build and too tight for a ten-operation member batch.
  puerts_blueprint_member_patch: 60000,
  puerts_widget_build: 30000,
  puerts_behavior_tree_build: 30000,
  puerts_behavior_tree_inspect: 15000,
  puerts_widget_inspect: 15000,
  // An Animation Blueprint build compiles at least twice inside the builder
  // (once after variables, once at the end), then compiles again for the report
  // and reads the whole asset back before saving. 30s is a realistic ceiling for
  // one Blueprint compile and too tight for four passes over a state machine.
  puerts_anim_blueprint_build: 60000,
  puerts_anim_blueprint_inspect: 15000,
  puerts_anim_montage_inspect: 15000,
  puerts_anim_blend_space_inspect: 15000,
  // Reading is cheaper than building, but a 200-node graph with include_pins
  // is a large serialization on the game thread and the 7 second default is
  // close enough to it to report a failure for work that succeeds.
  puerts_graph_inspect: 15000,
};

/** Decode a JSON-encoded object or array back into the structure it encodes.
    Anything else is returned untouched, so a string property value stays a
    string even when it happens to be valid JSON for a number or a quoted
    string. Only text that both opens and closes as an object or an array is
    considered. */
export function decodeStructuredValue(value: unknown): unknown {
  if (typeof value !== "string") return value;
  const text = value.trim();
  const looksStructured = (text.startsWith("{") && text.endsWith("}"))
    || (text.startsWith("[") && text.endsWith("]"));
  if (!looksStructured) return value;
  try {
    const parsed: unknown = JSON.parse(text);
    return parsed !== null && typeof parsed === "object" ? parsed : value;
  } catch {
    return value;
  }
}

/** Apply decodeStructuredValue to the structured parameters of one tool. */
export function decodeStructuredParams(
  toolName: string,
  params: Record<string, unknown>,
): Record<string, unknown> {
  const keys = structuredParameters[toolName];
  if (keys === undefined) return params;
  const decoded: Record<string, unknown> = { ...params };
  for (const key of keys) {
    if (key in decoded) decoded[key] = decodeStructuredValue(decoded[key]);
  }
  return decoded;
}

/** The single execution path for every native command. Both the puerts_*
    tools and the compatibility aliases go through this, so an alias cannot
    drift from the tool it fronts. */
export async function executeNativeCommand(
  client: PuerTSClient,
  spec: NativeToolSpec,
  params: Record<string, unknown>,
): Promise<Record<string, unknown>> {
  try {
    const parsed = spec.inputSchema.parse(
      decodeStructuredParams(spec.name, params),
    ) as Record<string, unknown>;
    return await client.call(
      spec.command,
      parsed,
      commandTimeouts[spec.name],
    ) as unknown as Record<string, unknown>;
  } catch (error: unknown) {
    const envelope = nativeFailureEnvelope([error instanceof Error ? error.message : String(error)]);
    // A refusal to address an editor is not the same kind of failure as a
    // command that ran and failed, and a caller has to be able to tell them
    // apart without pattern-matching English. The code says which check
    // refused; the detail carries the editor's own words when the editor was
    // the one that refused, which a prose message would otherwise discard.
    if (error instanceof SessionError) {
      envelope.session_error_code = error.code;
      envelope.session_error_detail = error.detail;
    }
    return envelope;
  }
}

export function createPuertsTools(client: PuerTSClient): ToolDefinition[] {
  return nativeToolSpecs.map((spec) => ({
    name: spec.name,
    description: `[PRIMARY NATIVE IPC] ${spec.description}`,
    inputSchema: spec.inputSchema,
    handler: async (params: Record<string, unknown>) => {
      const result = await executeNativeCommand(client, spec, params);
      return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
    },
  }));
}
