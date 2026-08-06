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

const soundCueNode = z.object({
  id: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/).describe(
    "Stable UObject name used by audio_inspect and retained across convergent rebuilds.",
  ),
  type: z.enum([
    "wave_player", "mixer", "random", "modulator", "delay", "looping", "concatenator",
  ]),
  children: z.array(z.string().min(1).max(64)).optional().describe(
    "Ordered child ids. Order is meaningful for mixer, random and concatenator nodes.",
  ),
  sound_wave: z.string().optional().describe(
    "Required only for wave_player. A /Game or /Engine USoundWave object path.",
  ),
  properties: z.record(reflectedValue).optional().describe(
    "Editable UE4.27 properties by reflected name, such as PitchMin, DelayMax or bLooping.",
  ),
}).strict();

/**
 * Optimistic concurrency for a planned scene write.
 *
 * The format is checked here rather than in Unreal so a mistyped hash is a
 * schema error at the boundary, not a state_conflict from the editor: those two
 * mean opposite things to a caller, and a client that cannot tell them apart
 * will re-plan forever against a hash it is corrupting on every send.
 *
 * Only scene_structure_hash exists. Asset, Blueprint, material, sequence and
 * package preconditions each need their own hash with a defined scope and a
 * test that changes relevant and irrelevant state; declaring them here as empty
 * placeholders would advertise guards that do not run.
 */
const sceneBatchPreconditions = z.object({
  scene_structure_hash: z.string().regex(/^(sha1:)?[0-9a-f]{40}$/,
    "scene_structure_hash must be 40 lowercase hex characters, optionally prefixed \"sha1:\"",
  ).describe(
    "The value structure_hash_sha1 (puerts_scene_inspect) or pre_structure_hash "
    + "(puerts_scene_batch plan_only) returned when this batch was planned. The apply is refused with "
    + "error_code state_conflict when the level no longer hashes to it, before any transaction opens. "
    + "COVERS, per actor: object name, class path, label, folder, tags, world transform to three "
    + "decimals, attach parent and socket, and every component's name, class path, attach parent and "
    + "relative transform. DOES NOT COVER reflected property values or bounds, so a batch that only "
    + "writes properties is not protected by this hash from another property write. "
    + "Ignored when plan_only is true, because a plan is how a current hash is obtained.",
  ),
}).strict().optional().describe(
  "State the batch was planned against. Omit for the existing behavior: apply against the level as "
  + "it is now.",
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
    "Reflected properties applied to the component template, by property name, "
    + "e.g. {\"StaticMesh\": \"/Engine/BasicShapes/Cube.Cube\"}. An asset "
    + "reference is the object path as a string; null clears one. A property the "
    + "class does not have, or an asset path that does not resolve, rejects the "
    + "whole spec before the asset is touched.",
  ),
}).strict();

/** One Blueprint member variable. */
const blueprintVariable = z.object({
  name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/),
  type: z.string().describe(
    "One of bool, byte, int, int64, float, string, name, text, vector, "
    + "vector2d, rotator, transform, linearcolor; or a prefixed form such as "
    + "\"object:StaticMeshComponent\" or \"struct:/Script/Engine.HitResult\". "
    + "Full list: references/blueprint-tools.md in the unreal-engine-4-27 skill.",
  ),
  // An array is in the union only so the native side answers with the reason
  // an array default is refused, instead of the client rejecting it with a
  // union-mismatch dump that never names the rule.
  default: z.union([
    z.string(), z.number(), z.boolean(), z.null(), z.record(z.unknown()), z.array(z.unknown()),
  ]).optional().describe(
    "Value in the variable's own shape. Not accepted on an array or set variable.",
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
  "InputAction",
  "InputAxisEvent",
  "MacroInstance",
  "AddDelegate",
  "RemoveDelegate",
  "CallDelegate",
  "ClearDelegate",
  "AssignDelegate",
  "CreateDelegate",
]);

/** The symbolic operators the Operator node type accepts. Authority is
    GetSupportedOperators() in the builder; each is a verified
    UKismetMathLibrary or UKismetStringLibrary call.

    This list stays in the schema on measured grounds. The prose that used to
    surround it moved to the skill and cost nothing, but the TOKENS are what a
    caller has to spell: bench-schema-sufficiency.mjs scored first-call
    sufficiency at 87.5% before the trim and 33.3% after, and stranded ops were
    a quarter of the loss. Restoring the bare names costs ~250 bytes against the
    43 KB the prose removal saved. A list of names is not documentation, it is
    the vocabulary; the explanation of what each one does is documentation, and
    that is what lives in references/blueprint-tools.md. */
const blueprintOperators = [
  "not_bool", "and_bool", "or_bool",
  "add_float", "subtract_float", "multiply_float", "divide_float",
  "greater_float", "less_float", "greater_equal_float", "less_equal_float", "equal_float",
  "clamp_float", "lerp_float",
  "add_int", "subtract_int", "greater_int", "less_int", "equal_int",
  "make_vector", "add_vector", "multiply_vector_float",
  "append_string", "vector_to_string", "bool_to_string",
] as const;

/** Every routing key any node type reads, as one flat vocabulary.
    Which node takes which is in the reference; that mapping is the
    documentation. This is the spelling, which is not. */
const blueprintRoutingKeys = [
  "class", "function", "op", "var_name", "varName", "scope", "target_class", "value",
  "parent_class", "event_name", "parameters", "purity", "struct_type", "actor_class",
  "num_outputs", "start_index", "num_cases", "has_default", "case_values",
  "is_case_sensitive", "is_random", "loop", "start_index_from_zero", "num_inputs",
  "fkey_name", "action_name", "axis_name", "consume_input", "execute_when_paused",
  "override_parent", "macro_bp", "macro_name", "self_var_name", "delegate_owner_class",
  "delegate_name", "selected_function_name", "function_name", "text", "width", "height",
] as const;

const blueprintGraphNode = z.object({
  id: z.string().min(1).describe("Unique within this graph; connections address nodes by it."),
  type: blueprintNodeType,
  params: z.record(z.unknown()).optional().describe(
    "Routing keys the node type needs, plus pin defaults by pin name. Anything "
    + "that is not a routing key for this node type is a pin default: a number "
    + "for a float pin, true/false for a bool pin, {\"x\":0,\"y\":0,\"z\":0} for a "
    + "vector pin, an object path string for an object pin. Routing keys across "
    + "all node types: " + blueprintRoutingKeys.join(", ")
    + ". Operator op: " + blueprintOperators.join(", ")
    + ". Which node type takes which key: references/blueprint-tools.md in the "
    + "unreal-engine-4-27 skill.",
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
  "Event graph description. A connection is nodeId.pinRole on each end. \"exec\" "
  + "is direction-aware (Then on the source, Execute on the target), \"then\" is "
  + "always Then, and any other role is a literal pin name, so data pins wire "
  + "through the same array. A VariableGet, VariableSet, MakeStruct or BreakStruct "
  + "names its data pin after the variable or struct, so those roles are yours, not "
  + "from this list. Every other literal pin role: Condition, else, then_0, then_1, "
  + "then_2, self, ReturnValue, InString, A, B, X, Y, Z, Value, Min, Max, Duration, "
  + "Completed, Object, CastFailed, AsResult, InputPin, OutputPin, Default, "
  + "Pressed, Released, Key, OtherActor, \"Out 0\", \"Out 1\". Which node carries "
  + "which, and the two that cannot be wired by name: "
  + "references/blueprint-tools.md in the unreal-engine-4-27 skill.",
);

const blueprintTypeDescriptor = z.object({
  category: z.string().min(1),
}).passthrough();

const blueprintFunctionParameter = z.object({
  name: z.string().min(1),
  type: blueprintTypeDescriptor,
  default: z.string().optional(),
  rename_from: z.string().min(1).optional(),
}).strict();

const blueprintSetFunctionOperation = z.object({
  op: z.literal("set_function"),
  name: z.string().min(1),
  inputs: z.array(blueprintFunctionParameter).optional(),
  outputs: z.array(blueprintFunctionParameter).optional(),
  locals: z.array(blueprintFunctionParameter.omit({ rename_from: true })).optional(),
  metadata: z.object({
    category: z.string().optional(),
    tooltip: z.string().optional(),
    pure: z.boolean().optional(),
    const: z.boolean().optional(),
    access: z.enum(["public", "protected", "private"]).optional(),
  }).strict().optional(),
}).strict();

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
    the two alignment enums; a grid slot takes row and column. A field that
    does not apply to the actual parent slot is refused before mutation. Moving
    a subtree between parent types therefore requires updating its slot shape. */
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
    "Widget-intrinsic values by name. All types take visibility, renderOpacity "
    + "and isEnabled. A name the widget type does not support rejects the whole "
    + "spec. Per-type property lists: references/widget-tools.md in the "
    + "unreal-engine-4-27 skill.",
  ),
  slot: widgetSlot.optional(),
  children: z.array(widgetNode).max(64).optional().describe(
    "Panel types take any number, content types take at most one, leaf types "
    + "take none. Array order is tree order.",
  ),
}).strict());

/** One Blackboard key. The type vocabulary is UE4.27's nine
    UBlackboardKeyType subclasses; the native side re-validates against the
    same list, so this can only reject earlier, never let a type through.

    There is no default value here because UE4.27 has none: FBlackboardEntry
    holds a name, an instanced key type, an instance-sync flag and two
    editor-only strings, and a key's value exists only on a running
    UBlackboardComponent. */
const blackboardKey = z.object({
  name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/),
  type: z.enum(["Bool", "Int", "Float", "String", "Name", "Vector", "Rotator", "Object", "Class"]),
  base_class: z.string().optional().describe(
    "Object and Class keys only: the required base, for example "
    + "\"/Script/Engine.Actor\". Rejected on any other key type.",
  ),
  instance_synced: z.boolean().optional().describe(
    "Share this key's value across every instance of the blackboard. Default false.",
  ),
  description: z.string().optional().describe("Editor-only tooltip on the key."),
  category: z.string().optional().describe("Editor-only grouping for the key."),
}).strict();

/** One navigation query. Which position fields apply depends on kind: project
    takes point and an optional extent, path and raycast take start and end,
    random_point takes origin and radius. */
const navQuery = z.object({
  id: z.string().optional().describe("Echoed on the matching result. Defaults to q0, q1, and so on."),
  kind: z.enum(["project", "path", "raycast", "random_point"]),
  point: vector.optional(),
  start: vector.optional(),
  end: vector.optional(),
  origin: vector.optional(),
  extent: vector.optional().describe(
    "project only. Omit to use the nav data's own default query extent, which "
    + "is almost always what you want.",
  ),
  radius: z.number().optional().describe("random_point only. Must be greater than zero."),
}).strict();

/** One configured AI sense. Every field other than `sense` is a reflected
    property of the matching UAISenseConfig subclass, checked by name on the
    native side before anything is written, so a misspelled field is a refusal
    rather than a value that silently never applied. */
const perceptionSense = z.object({
  sense: z.enum(["Sight", "Hearing", "Damage", "Touch", "Team", "Prediction"]),
}).passthrough().describe(
  "All senses take MaxAge and bStartsEnabled. Sight and Hearing take further "
  + "reflected properties of their UAISenseConfig subclass; a misspelled name is a "
  + "refusal, not a silent no-op. Per-sense fields: "
  + "references/ai-input-audio-tools.md in the unreal-engine-4-27 skill.",
);

// Everything an Animation Blueprint spec holds EXCEPT asset_path, because that
// is the one field build and patch describe differently: build refuses a path
// that exists, patch refuses one that does not. One shape, not two: a copy would
// let the create schema and the edit schema drift, and the visible failure is a
// spec that builds and then cannot be patched with the same JSON.
const animBlueprintSpecFields = {
  skeleton_path: z.string().min(1).describe(
    "Object path of the target USkeleton. Required.",
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
      name: z.string().min(1),
      animation: z.string().min(1).describe("Object path of the AnimSequence this state plays."),
      looping: z.boolean().optional(),
      is_entry: z.boolean().optional().describe("Exactly one state should set this."),
    }).strict()).min(1),
    transitions: z.array(z.object({
      from: z.string().min(1).describe("A state id from states[]."),
      to: z.string().min(1).describe("A state id from states[]."),
      blend_time: z.number().optional().describe("Crossfade seconds. Default 0.2."),
      condition: z.record(z.unknown()).describe(
        "{type:\"bool_variable\", variable, value} or {type:\"time_remaining\", threshold}.",
      ),
    }).strict()),
  }).strict(),
  event_graph: z.record(z.unknown()).optional().describe(
    "Optional event graph, in the same grammar puerts_blueprint_build's graph uses.",
  ),
  save: z.boolean().optional().describe(
    "Default true. One that did not compile and read back clean is never saved.",
  ),
};

// The spec grammar both commands inherit from the UE4.27 builder (v1). Written
// once so the two descriptions cannot disagree about what the builder accepts.
const animBlueprintSpecLimits =
  "Spec limits inherited from the builder (v1): variables are type \"bool\" only; "
  + "pipeline node types are \"StateMachine\" and \"Slot\"; every state plays one "
  + "AnimSequence; transition conditions are {type:\"bool_variable\", variable, value} or "
  + "{type:\"time_remaining\", threshold}. "
  + "Detail: references/animation-tools.md in the unreal-engine-4-27 skill.";

/** Points a tool description at the on-demand reference file that holds its
    full detail.

    A tool description is resident context: every client that lists tools pays
    for every word of it on every session, whether or not the tool is called.
    Node catalogs, operation grammars, convergence semantics and design
    rationale are documentation, and documentation belongs where it is fetched
    on demand. What stays in the schema is what a caller needs to make a VALID
    call without a lookup: the purpose, the required parameters, the hard
    constraints and the refusal behaviour. Moving more than that out trades a
    smaller catalog for extra round trips, which is the same cost in a
    different place.

    Scripts/check-schema-budget.mjs enforces the split so it cannot regrow. */
const detail = (file: string): string =>
  `Detail: references/${file} in the unreal-engine-4-27 skill.`;

/** The contract every inspector shares, stated once here and once in the skill
    rather than re-litigated in twelve descriptions. */
const readOnly =
  "READ ONLY: no transaction, nothing saved, dirty flag reported before and "
  + "after, /Game and /Engine. ";

const inspectorsRef = detail("inspectors.md");
const blueprintToolsRef = detail("blueprint-tools.md");
const widgetToolsRef = detail("widget-tools.md");
const materialToolsRef = detail("material-tools.md");
const animationToolsRef = detail("animation-tools.md");
const sceneToolsRef = detail("scene-tools.md");
const sequencerToolsRef = detail("sequencer-tools.md");
const aiToolsRef = detail("ai-input-audio-tools.md");

const specs = [
  ["puerts_diagnostic", "diagnostic", "Prove the in-process PuerTS context, game thread, named-pipe transport, and actor-query timing.", z.object({ actor_limit: z.number().optional() }).strict()],
  ["puerts_find_assets", "find_assets", "Find UE4.27 assets by path, type, or name.", z.object({ path: z.string().optional(), type: z.string().optional(), name: z.string().optional(), recursive: z.boolean().optional(), limit: z.number().optional() }).strict()],
  ["puerts_delete_asset", "delete_asset",
    "Permanently delete one asset under /Game. confirm=true is mandatory. By default UE4.27 "
    + "checks both disk and memory references and refuses a referenced asset. force=true uses "
    + "the engine's force-delete path, which can null references, dirty referencer packages, "
    + "remove Blueprint instances, and close asset editors holding affected objects. The response "
    + "lists disk referencers and independently verifies both Asset Registry and package-file "
    + "absence. Deleting an already absent asset is a successful no-op. This operation is not "
    + "transacted and cannot be undone; make a source-control checkpoint first.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/[A-Za-z0-9_]+(?:\/[A-Za-z0-9_]+)*(?:\.[A-Za-z0-9_]+)?$/),
      confirm: z.literal(true),
      force: z.boolean().optional().describe(
        "Default false. True may break references and requires the same explicit confirmation.",
      ),
    }).strict()],
  ["puerts_find_actors", "find_actors",
    "Find actors in the current editor level. name and type are case-insensitive SUBSTRING "
    + "matches, not wildcards. "
    + "folder_filter and include_transforms are the legacy level_actors parameters, restored: "
    + "either one reads the level through the same snapshot puerts_scene_inspect reports, so the "
    + "answer cannot disagree with the inspector, and both are absent by default so the ordinary "
    + "read stays the cheap actor iteration it always was. "
    + "For bounds, components, attachment or a structure hash, use puerts_scene_inspect: this tool "
    + "is the quick lookup, not the level inspector.",
    z.object({
      name: z.string().optional().describe("Object name or label substring."),
      type: z.string().optional().describe("Class name substring."),
      folder_filter: z.string().optional().describe(
        "World Outliner folder PREFIX, the same match the legacy level_actors made: "
        + "\"Courtyard\" takes \"Courtyard/Lighting\" and does not take \"OldCourtyard\".",
      ),
      include_transforms: z.boolean().optional().describe(
        "Default false. Add each actor's world location, rotation and scale under transform. "
        + "location is reported either way.",
      ),
      limit: z.number().optional(),
    }).strict()],
  ["puerts_read_property", "read_property", "Read an Unreal reflected property.", z.object({ ...target, property: z.string() }).strict()],
  ["puerts_set_property", "set_property", "Set an approved Unreal reflected property in a transaction.", z.object({ ...target, property: z.string(), value: reflectedValue }).strict()],
  ["puerts_call_function", "call_function", "Call a native-approved Unreal function.", z.object({ actor: z.string(), function: z.string(), arguments: z.array(z.unknown()).optional() }).strict()],
  ["puerts_spawn_actor", "spawn_actor",
    "Spawn one actor in a transaction. "
    + "name, folder and scale are the legacy actor_spawn parameters, restored: the spawn is "
    + "finished through the same desired-state path puerts_scene_batch uses, inside the one "
    + "transaction this command opens, so a label that already belongs to another actor is a "
    + "refusal and the whole spawn is rolled back rather than left half-applied. "
    + "For more than one actor, or to change an actor that already exists, use "
    + "puerts_scene_batch: it takes the whole scene in one call and this one takes one actor.",
    z.object({
      class_path: z.string(),
      location: vector.optional(),
      rotation: rotator.optional(),
      scale: vector.optional().describe("World scale. Omitted means unit scale."),
      name: z.string().optional().describe(
        "Actor LABEL for the spawned actor, the same thing legacy actor_spawn's name set. Refused "
        + "when another actor in the level already carries it.",
      ),
      folder: z.string().optional().describe("World Outliner folder path, such as \"Courtyard/Lighting\"."),
    }).strict()],
  ["puerts_delete_actor", "delete_actor", "Delete an actor in a transaction. Requires confirm=true.", z.object({ actor: z.string(), confirm: z.literal(true) }).strict()],
  ["puerts_sky_shader_create", "sky_shader_create", "Create an animated native HLSL aurora sky material and apply it to a sky sphere in one transaction.", z.object({ asset_path: z.string().optional(), sky_actor: z.string().optional() }).strict()],
  ["puerts_blueprint_build", "blueprint_build",
    "Create or update a compiled Blueprint asset from one JSON spec: parent class, components "
    + "with their template properties, member variables, and an event graph. Desired-state and "
    + "convergent: a rerun loads the asset rather than duplicating it, and a member that exists "
    + "under a different type is an error, never a silent retype. Nothing is removed unless "
    + "remove_unlisted names a scope. An unsupported node type is rejected before the asset is "
    + "touched. Every connection must resolve to real pins: graph.connection_count is the number "
    + "actually made, and any shortfall against the number requested fails the build with the "
    + "dropped pairs named in graph.unresolved_connections, so a graph with a hole in it is never "
    + "saved. Assets are limited to /Game/MCPGenerated/. Response reports compile status with "
    + "compiler errors and warnings; the asset is only saved when it built clean. "
    + blueprintToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native "
        + "command enforces the same limit; this rejects earlier, at the client.",
      ),
      parent_class: z.string().optional().describe(
        "Reflected short name (\"Character\") or full path (\"/Script/Engine.SaveGame\"). Any "
        + "class Unreal allows a Blueprint of, including non-Actor. Components and the actor "
        + "event node types require an Actor parent. Defaults to Actor.",
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
        "Opt-in downward convergence, off by default. Only 'variables' and 'components' are "
        + "implemented; any other true scope is rejected. Removal is limited to members this "
        + "builder declared.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Return the convergence plan and change nothing.",
      ),
      force_remove_referenced: z.boolean().optional().describe(
        "Default false. Allow removal of a managed variable that graph nodes reference, "
        + "deleting those nodes with it.",
      ),
    }).strict()],
  ["puerts_blueprint_graph_patch", "blueprint_graph_patch",
    "Change selected nodes and pins on an existing Blueprint graph without rebuilding it, when "
    + "restating the whole graph through blueprint_build would be the wrong shape. operations is "
    + "an ordered batch of add_node, update_node, remove_node, set_pin_default, connect_pins, "
    + "disconnect_pins, move_node, set_node_enabled and break_pin_links. Every node is addressed "
    + "by a selector that must resolve to exactly one node; a bare object id is refused, and a "
    + "selector matching nothing or more than one node is a refusal, not a guess. The whole batch "
    + "resolves before anything mutates, applies in one transaction, and rolls back entirely on "
    + "any failure or read-back mismatch. add_node accepts the same node vocabulary "
    + "blueprint_build does. "
    + blueprintToolsRef,
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
    "Change a Blueprint's MEMBERS incrementally: variables, functions, macros, interfaces, event "
    + "dispatchers and components. puerts_blueprint_graph_patch owns nodes and pins and cannot "
    + "reach any of these; puerts_blueprint_build reaches them only by restating the whole asset. "
    + "operations is an ordered batch, resolved and classified before the first mutation because "
    + "each underlying mutator recompiles the Blueprint. There is no change-variable-type "
    + "primitive. An operation already satisfied is reported in unchanged_operations and not "
    + "repeated, so a rerun applies nothing. Applying runs in one transaction, compiles, re-reads "
    + "every operation's condition from the asset, and rolls the whole batch back on any failure. "
    + blueprintToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The Blueprint must "
        + "already exist: patching never creates one.",
      ),
      operations: z.array(z.record(z.unknown())).min(1).superRefine((operations, context) => {
        operations.forEach((operation, index) => {
          if (operation.op !== "set_function") return;
          const parsed = blueprintSetFunctionOperation.safeParse(operation);
          if (parsed.success) return;
          for (const issue of parsed.error.issues) {
            context.addIssue({ ...issue, path: [index, ...issue.path] });
          }
        });
      }).describe(
        "Ordered batch of {op, ...} entries. op is one of add_variable, remove_variable, "
        + "rename_variable, set_variable_default, set_variable_metadata, add_function, "
        + "set_function, remove_function, rename_function, add_macro, set_macro, remove_macro, "
        + "rename_macro, add_interface, remove_interface, add_event_dispatcher, "
        + "remove_event_dispatcher, remove_component, rename_component, reparent_component. "
        + "type is the same Type Descriptor puerts_graph_inspect reports for a variable. "
        + "Per-op fields: " + blueprintToolsRef,
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
    "Create or replace a compiled UMG Widget Blueprint from one JSON widget tree of typed, "
    + "named widgets, each with optional intrinsic properties and an optional slot object "
    + "holding the layout its parent owns. The spec is the WHOLE tree: a widget tree has no "
    + "per-widget identity to merge against, so a rerun replaces it rather than merging. The "
    + "tree is validated before any asset is touched. Assets are limited to /Game/MCPGenerated/. "
    + "The response reports the hierarchy read back out of the built asset, plus "
    + "generated_class_path. "
    + widgetToolsRef,
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
  ["puerts_widget_bind", "widget_bind",
    "Bind widget properties to Blueprint functions or variables, and expose widgets as members "
    + "of the generated class. This is what makes a UMG widget show live data rather than the "
    + "constants puerts_widget_build gave it. Desired state: an identical rerun applies nothing "
    + "and reports converged. The source function or variable must already exist on the Widget "
    + "Blueprint, and a property binding may only target a pure (BlueprintPure or const) "
    + "function. Every entry is validated by UE4.27's own binding validator before anything is "
    + "written, and a failed compile restores the previous bindings. Binding a widget implies "
    + "exposing it. Verify with puerts_widget_inspect. "
    + widgetToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path of an existing Widget Blueprint under /Game/MCPGenerated/, no asset-name "
        + "suffix. widget_bind never creates an asset.",
      ),
      bindings: z.array(z.object({
        widget: z.string().min(1).describe("Widget name in the tree, as widget_build spelled it."),
        property: z.string().min(1).describe(
          "The property to bind, without the \"Delegate\" suffix: Percent on a ProgressBar, Text "
          + "on a TextBlock, Visibility, IsEnabled, ToolTipText. An unbindable name is refused "
          + "with the bindable ones on that widget class listed.",
        ),
        source: z.object({
          kind: z.enum(["function", "variable"]).describe(
            "function binds to a member function (pure only, for a property binding); variable "
            + "binds to a member variable.",
          ),
          name: z.string().min(1).describe("The function or variable name on the Widget Blueprint."),
        }).strict(),
      }).strict()).optional().describe(
        "One entry per (widget, property). UE4.27 allows a property to be bound once, so two "
        + "entries for the same pair are refused.",
      ),
      expose_as_variable: z.array(z.string().min(1)).optional().describe(
        "Widget names to publish as members of the generated class, which is what BindWidget and "
        + "a graph reference by name both need. Widgets named in bindings are added automatically.",
      ),
      remove_unlisted: z.boolean().optional().describe(
        "Default false. Drop bindings not listed and clear the variable flag on widgets not "
        + "listed, within the stated sections only. Clearing a variable flag breaks every graph "
        + "reference to that widget.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Report what would change and write nothing.",
      ),
      save: z.boolean().optional().describe(
        "Default true. Nothing is saved when nothing changed or the compile failed.",
      ),
    }).strict().refine(
      (spec) => spec.bindings !== undefined || spec.expose_as_variable !== undefined,
      // Both sections optional, neither present is not. A request that states no
      // desired state has nothing to converge on, and with remove_unlisted it is
      // the shape that means "take everything away", so it is refused at the
      // client rather than sent to the editor to be told the same thing.
      { message: "widget_bind needs bindings, expose_as_variable, or both." },
    )],
  ["puerts_graph_inspect", "graph_inspect",
    "Read an existing Blueprint back as machine-readable JSON: parent class, "
    + "SimpleConstructionScript components, member variables, implemented interfaces, user "
    + "functions, the graph list, and one graph described in the shape "
    + "puerts_blueprint_build writes. The independent read half of the builder. A node the "
    + "builder has no word for reports a null type with node_class and is listed under "
    + "graph.unmapped_nodes. Node identity is OBSERVED (object name plus NodeGuid), not the "
    + "id a build spec wrote, so an inspected node cannot be matched back to the spec line "
    + "that made it. Also returns member_structure_hash_sha1, the hash "
    + "puerts_blueprint_member_patch verifies against. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Blueprint, as a package path or the object path other tools hand back. Limited to "
        + "/Game and /Engine.",
      ),
      graph_name: z.string().optional().describe(
        "Which graph to describe. Omit for the event graph. Any name from the response's "
        + "graphs array works, including function and macro graphs.",
      ),
      include_pins: z.boolean().optional().describe(
        "Default false. Attach every pin of every node with its PinId, name, direction, type, "
        + "defaults and links. Roughly ten times the payload on a large graph.",
      ),
    }).strict()],
  ["puerts_behavior_tree_build", "behavior_tree_build",
    "Create or update a UE4.27 BehaviorTree asset with its Blackboard from one JSON spec, in one "
    + "transaction: blackboard keys, blackboard assignment, and the full node graph, then a "
    + "compile-free save. The graph replaces the tree's root only when every node builds, so a "
    + "failed spec leaves an existing tree untouched and a rerun converges. Node structure: "
    + "{id, type, name?, params?, children?, decorators?, services?}. Unknown types are rejected "
    + "before the asset is touched. params keys are snake_case and values are strings; an unknown "
    + "params key is currently DROPPED WITHOUT A WARNING, so verify with "
    + "puerts_behavior_tree_inspect. Keys here are add-only; use puerts_blackboard_build to "
    + "update or remove one. Supported node types: "
    + aiToolsRef,
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
        + "nodes does nothing in PIE. Node shape is {id, type, name?, params?, children?, "
        + "decorators?, services?}. params keys across all node types: blackboard_key, "
        + "wait_time, acceptable_radius, random_deviation, BTAsset, allow_strafe, "
        + "observe_blackboard_value, notify_observer, time_limit, cooldown_time. Which node "
        + "type takes which, and the full type list: "
        + "references/ai-input-audio-tools.md in the unreal-engine-4-27 skill.",
      ),
      save: z.boolean().optional().describe("Default true. A tree that did not build cleanly is never saved."),
    }).strict()],
  ["puerts_behavior_tree_inspect", "behavior_tree_inspect",
    "Read an existing UE4.27 BehaviorTree and its Blackboard back as machine-readable JSON: the "
    + "tree class, blackboard path, the root composite as a nested structure, per-node class and "
    + "properties, referenced blackboard keys, counts per kind, unsupported_fields, and "
    + "structure_hash_sha1. The read half of puerts_behavior_tree_build, and READ ONLY: no "
    + "transaction, nothing compiled or saved, package dirty flag reported before and after. Node "
    + "identity is DERIVED from the traversal path, so a renamed or reordered node is a different "
    + "identity on purpose. Reading is allowed anywhere under /Game and /Engine. "
    + aiToolsRef,
    z.object({
      asset_path: z.string().describe(
        "The BehaviorTree, as a package path (\"/Game/MCPGenerated/BT_Patrol\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_blackboard_build", "blackboard_build",
    "Reconcile a whole UE4.27 Blackboard asset from one desired-state spec: keys with their "
    + "types, per-key instance sync, editor description and category, and the parent blackboard. "
    + "puerts_behavior_tree_build creates a blackboard and ADDS keys; this command owns the "
    + "blackboard as an asset in its own right and can UPDATE a key, REMOVE one and set the "
    + "parent. There is no key default value, because UE4.27 has none: a key's value exists only "
    + "on a running UBlackboardComponent. A key that already exists under a DIFFERENT type is an "
    + "error naming both types, never a silent retype. remove_unlisted is opt-in and off by "
    + "default. Applying runs in one transaction and reads every key back off the asset rather "
    + "than trusting the write. Assets are limited to /Game/MCPGenerated/. "
    + aiToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Blackboard package path under /Game/MCPGenerated/, no asset-name suffix.",
      ),
      parent_path: z.string().optional().describe(
        "A Blackboard to inherit keys from, anywhere under /Game or /Engine. Keys the parent "
        + "chain declares cannot be redeclared here.",
      ),
      keys: z.array(blackboardKey).max(128).optional(),
      remove_unlisted: z.boolean().optional().describe(
        "Default false. Remove keys this asset declares that the spec does not name. Inherited "
        + "keys are never removed: they are not this asset's to remove.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Classify the change and write nothing.",
      ),
      save: z.boolean().optional().describe("Default true. A build that failed verification is never saved."),
    }).strict()],
  ["puerts_blackboard_inspect", "blackboard_inspect",
    "Read a UE4.27 Blackboard back as machine-readable JSON: every key with its type, base class, "
    + "instance-sync flag, editor description and category, an inherited flag, the parent chain, "
    + "the asset's own UBlackboardData::IsValid verdict, and structure_hash_sha1. The read half of "
    + "puerts_blackboard_build, and READ ONLY: no transaction, nothing saved, package dirty flag "
    + "reported before and after. Key identity is AUTHORED: a key's name IS what every "
    + "FBlackboardKeySelector binds to, so renaming a key is a different key. key_count is what "
    + "this asset declares; total_key_count includes the parent chain. Reading is allowed anywhere "
    + "under /Game and /Engine. "
    + aiToolsRef,
    z.object({
      asset_path: z.string().describe(
        "The Blackboard, as a package path (\"/Game/MCPGenerated/BB_Guard\") or the object path "
        + "other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_eqs_inspect", "eqs_inspect",
    "Read a UE4.27 Environment Query (UEnvQuery) back as machine-readable JSON: query name, "
    + "options in evaluation order, each option's generator class with its properties, and each "
    + "test with its class and every scoring and filtering property, plus unsupported_fields and "
    + "structure_hash_sha1. Options and tests are addressed by index, because neither carries a "
    + "GUID. THERE IS NO eqs_build, and that is a decision rather than a gap: UE4.27 rebuilds "
    + "Options from the editor graph, so a written Options array would be silently wiped the next "
    + "time a human opened the asset. Author the query by hand and run it from a Behavior Tree "
    + "with the RunEQS service, which puerts_behavior_tree_build supports. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Environment Query, as a package path or the object path other tools hand back. "
        + "Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_nav_inspect", "nav_inspect",
    "Read the editor world's navigation configuration as machine-readable JSON: whether a "
    + "navigation system exists, how many build tasks remain, every ANavigationData actor with "
    + "its agent config and generation settings, the supported agents, every NavMeshBoundsVolume "
    + "and NavModifierVolume with its world-space box and area class, and the bounds the "
    + "navigation system actually REGISTERED. The registered bounds are deliberately a separate "
    + "list from the volumes, because a volume in an unloaded or hidden sublevel is present as an "
    + "actor and absent from the registered bounds, which is the usual reason a level that looks "
    + "like it has a navmesh does not. READ ONLY: no rebuild, nothing spawned, nothing dirtied. "
    + "Reads the editor world, so it refuses during Play In Editor. "
    + inspectorsRef,
    z.object({}).strict()],
  ["puerts_nav_query", "nav_query",
    "Answer a batch of navigation queries against the editor world's navmesh in one round trip. "
    + "Kinds are project (snap a point onto the navmesh), path (reachability, length and cost), "
    + "raycast (walk in a straight line) and random_point. reachable is true only for "
    + "ENavigationQueryResult::Success, and the raw result word is also returned, because a "
    + "missing path and an unanswerable query are different problems. The whole batch is "
    + "validated before the first query runs, so a bad entry is a refusal rather than partial "
    + "answers. The response warns when the navmesh is still building, and that random_point is "
    + "not deterministic. READ ONLY: every kind is a const query on UNavigationSystemV1. "
    + "Requires a navigation system; run puerts_nav_inspect first if this refuses. "
    + inspectorsRef,
    z.object({
      queries: z.array(navQuery).min(1).max(100),
    }).strict()],
  ["puerts_nav_build", "nav_build",
    "Rebuild the editor world's navigation so a placed NavMeshBoundsVolume produces an actual "
    + "navmesh, then read the world back with the same function puerts_nav_inspect uses so the "
    + "answer is not the build's own claim. Mutating and deliberately NOT transactional: navmesh "
    + "tiles are derived data the transaction buffer does not record, and the recovery from a bad "
    + "build is another build. wait defaults to FALSE and returns immediately; poll "
    + "puerts_nav_inspect until remaining_build_tasks is zero before running puerts_nav_query, "
    + "because a partial navmesh answers queries wrongly rather than refusing them. Every "
    + "precondition is refused by name before any generator runs. plan_only runs every "
    + "precondition and no generator. "
    + sceneToolsRef,
    z.object({
      wait: z.boolean().optional().describe(
        "Block until the build finishes (default false). True is the only path that spawns a "
        + "missing RecastNavMesh, but it can outlast the 30 second pipe deadline on a large level.",
      ),
      plan_only: z.boolean().optional().describe(
        "Check every precondition and rebuild nothing. Read-only.",
      ),
    }).strict()],
  ["puerts_audio_build", "audio_build",
    "Create or reconcile a UE4.27 Sound Cue as one desired-state node tree under "
    + "/Game/MCPGenerated. Supports Wave Player, Mixer, Random, Modulator, Delay, Looping and "
    + "Concatenator nodes, ordered child links and reflected editable properties. first_node and "
    + "children are the playable truth; the native command calls LinkGraphNodesFromSoundNodes to "
    + "derive the editor graph after authoring. The whole request is validated before mutation, "
    + "including node ids, types, arity, references, reachability, cycles, wave assets and property "
    + "conversion. Existing cues must be saved and clean so a package-file snapshot can restore a "
    + "failed replacement. New cues use the shared asset rollback boundary. Every successful write "
    + "is independently read back through audio_inspect before save. plan_only mutates nothing, and "
    + "a repeated matching request is a no-op. Does not start PIE or play audio.",
    z.object({
      asset_path: z.string().describe(
        "Sound Cue package path under /Game/MCPGenerated, without an object suffix.",
      ),
      first_node: z.string().min(1).max(64).describe("Id of the playable root node."),
      nodes: z.array(soundCueNode).min(1).max(100),
      properties: z.record(reflectedValue).optional().describe(
        "Editable USoundCue properties by reflected name.",
      ),
      plan_only: z.boolean().optional(),
      save: z.boolean().optional().describe("Save after verified read-back. Defaults true."),
    }).strict()],
  ["puerts_audio_inspect", "audio_inspect",
    "Read a Sound Cue or a Sound Wave back as machine-readable JSON: the common USoundBase "
    + "fields and every EditAnywhere property by reflected name, so a Sound Wave's NumChannels, "
    + "SampleRate, SoundGroup, CompressionQuality, bLooping and bStreaming come back without a "
    + "field list that would go stale. For a Sound Cue it walks the whole node graph from "
    + "FirstNode. A cue with no FirstNode is warned about, and nodes unreachable from FirstNode "
    + "are counted as orphans: both are invisible from either list on its own. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Sound Cue or Sound Wave, as a package path or the object path other tools hand back. "
        + "Any USoundBase is accepted; a Sound Class, Sound Mix or Attenuation asset is a "
        + "different type and is refused by name.",
      ),
    }).strict()],
  ["puerts_cloth_inspect", "cloth_inspect",
    "Read a UE4.27 skeletal mesh's cloth setup as machine-readable JSON: every render section "
    + "with its cloth binding, every clothing asset with its GUID, topology hash and Physics "
    + "Asset, per-LOD vertex and triangle counts, the authoring masks, and the full NvCloth "
    + "config. A straight pass-through of UClothOptimizerLibrary::InspectClothAsset, the same "
    + "function the module's in-editor panel reads. THE READ HALF ONLY, deliberately: the "
    + "module's writers mutate cloth PAINT, authored data a re-run cannot regenerate, and they "
    + "ship when a failed apply provably restores the mask (write_unsupported_reason). A mesh "
    + "with no clothing assets is answered with a warning, not refused. "
    + readOnly
    + inspectorsRef,
    z.object({
      skeletal_mesh: z.string().describe(
        "The skeletal mesh, as a package path or the object path other tools hand back.",
      ),
    }).strict()],
  ["puerts_ai_perception_build", "ai_perception_build",
    "Reconcile the whole AIPerceptionComponent configuration on an existing AIController Blueprint "
    + "in one call: which senses it has, each sense's properties, and the dominant sense. The "
    + "component is created if it is missing. Desired-state, because a perception config is only "
    + "coherent as a whole: a listed sense is replaced wholesale, and a dominant_sense that senses "
    + "does not configure is refused by name. Every field other than `sense` is checked against the "
    + "config class by reflected name BEFORE anything is written. The Blueprint must already exist "
    + "and derive from AIController; create it with puerts_blueprint_build first. remove_unlisted "
    + "is opt-in and off by default, and a satisfied rerun costs no compile and no save. "
    + "Perception configures WHAT a controller can sense, not what it does about it: handling a "
    + "stimulus needs an OnPerceptionUpdated binding in the controller graph, which "
    + "puerts_blueprint_build authors. Assets are limited to /Game/MCPGenerated/. "
    + aiToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "An existing AIController Blueprint under /Game/MCPGenerated/, no asset-name suffix.",
      ),
      component_name: z.string().min(1).max(64).regex(/^[A-Za-z0-9_]+$/).optional().describe(
        "Default \"AIPerception\". An existing component of that name that is not an "
        + "AIPerceptionComponent is a refusal, never a replacement.",
      ),
      senses: z.array(perceptionSense).max(6).optional(),
      dominant_sense: z.enum(["Sight", "Hearing", "Damage", "Touch", "Team", "Prediction", "None"])
        .optional().describe(
          "Must also appear in senses. Default None.",
        ),
      remove_unlisted: z.boolean().optional().describe(
        "Default false. Remove configured senses the spec does not name.",
      ),
      plan_only: z.boolean().optional().describe("Default false. Classify the change and write nothing."),
      compile: z.boolean().optional().describe("Default true. A Blueprint that did not compile is rolled back."),
      save: z.boolean().optional().describe("Default true."),
    }).strict()],
  ["puerts_ai_controller_inspect", "ai_controller_inspect",
    "Read an AIController Blueprint back as machine-readable JSON: parent class, generated class "
    + "path, every AIPerceptionComponent the Blueprint declares with each configured sense, the "
    + "dominant sense class, and every RunBehaviorTree call site in its graphs. The independent "
    + "read half of puerts_ai_perception_build. The controller-to-BT wiring is reported as CALL "
    + "SITES because UE4.27 has no data-driven field pointing a controller at a tree. A call site "
    + "wired from a variable "
    + "resolves to nothing at edit time and is listed under dynamic_behavior_tree_call_sites rather "
    + "than guessed at. Only components this Blueprint declares are visible; one inherited from a "
    + "native C++ parent lives on the parent CDO, which this reader does not walk. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The controller Blueprint, as a package path or the object path other tools hand back. "
        + "Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_widget_inspect", "widget_inspect",
    "Read an existing UE4.27 Widget Blueprint back as machine-readable JSON: parent_class, "
    + "generated_class_path, the root widget as a nested hierarchy with child_index order, and "
    + "per widget its name, class, is_variable, editable properties and slot. named_slots "
    + "carries content held by INamedSlotInterface hosts, which is NOT reachable through panel "
    + "children. Also returns exposed variables, bindings, animations, unsupported_fields and "
    + "structure_hash_sha1, which excludes text so an edit does not read as a reshape. The "
    + "independent read half of puerts_widget_build. Widget identity is DERIVED from the "
    + "traversal path, since UMG widgets carry no GUIDs. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Widget Blueprint, as a package path (\"/Game/MCPGenerated/WBP_HUD\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blueprint_inspect", "anim_blueprint_inspect",
    "Read an existing UE4.27 Animation Blueprint back as machine-readable JSON: target_skeleton, "
    + "generated_class_path, parent_class, the STORED blueprint_status, anim_graphs with every "
    + "node and its editable properties, state_machines with their states, conduits and inner "
    + "pose graphs, transitions keyed by from->to with their condition kind, cached_poses, "
    + "blend_nodes, member variables, counts, unsupported_fields and structure_hash_sha1. The "
    + "read half of puerts_anim_blueprint_build, and READ ONLY: no transaction, nothing compiled "
    + "or saved, package dirty flag reported before and after. Node identity is DERIVED from the "
    + "traversal path, not the id a build spec wrote. Reading is allowed anywhere under /Game and "
    + "/Engine. "
    + animationToolsRef,
    z.object({
      asset_path: z.string().describe(
        "The Animation Blueprint, as a package path (\"/Game/MCPGenerated/ABP_Hero\") or the "
        + "object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_montage_inspect", "anim_montage_inspect",
    "Read an existing UE4.27 Animation Montage back as machine-readable JSON: sequence_length, "
    + "the skeleton, sync_group, blend in/out times, sections IN MONTAGE ORDER with their "
    + "next_section chain, slot tracks with their animation segments, notifies with their trigger "
    + "and state flags, and structure_hash_sha1. READ ONLY, on the same terms as the other "
    + "inspectors. There is deliberately NO montage write tool: UE4.27 exposes no atomic operation "
    + "for rebuilding the next_section chain, so a half-applied edit would leave a montage that "
    + "plays the wrong thing. Reading is allowed anywhere under /Game and /Engine.",
    z.object({
      asset_path: z.string().describe(
        "The Animation Montage, as a package path (\"/Game/Anim/AM_Attack\") or the object "
        + "path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blend_space_inspect", "anim_blend_space_inspect",
    "Read an existing UE4.27 Blend Space, Blend Space 1D or Aim Offset back as machine-readable "
    + "JSON: the target skeleton, blend_space_class (which is what tells 1D from 2D, since "
    + "UE4.27 exposes no dimension count), all three axes, and every sample with its animation, "
    + "position and rate_scale. Samples are SORTED by position and animation rather than reported "
    + "in array order. A sample with no animation is reported as a warning rather than a silent "
    + "row: it contributes nothing at runtime. "
    + "puerts_anim_blend_space_build creates new Blend Spaces; there is no update-in-place "
    + "counterpart, because UE4.27 offers no atomic sample-set replacement. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Blend Space, as a package path (\"/Game/Anim/BS_Locomotion\") or the object path "
        + "other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_anim_blend_space_build", "anim_blend_space_build",
    "Create a NEW UE4.27 Blend Space 1D from one JSON spec: target skeleton, one axis and a "
    + "sample list. CREATE-ONLY: it refuses outright when asset_path already names an asset, "
    + "because UE4.27 rebuilds a blend space's whole triangulation from its sample set and offers "
    + "no atomic way to replace an existing one's samples. The result is kept only if every "
    + "sample validates AND an independent puerts_anim_blend_space_inspect read agrees with the "
    + "request; anything else rolls the asset back. Positions must fall within [min, max] and no "
    + "two samples may share a position, both refused before any asset is touched.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\//).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. Must not already exist.",
      ),
      skeleton_path: z.string().describe(
        "Object path of the target USkeleton, e.g. \"/Game/Characters/Hero_Skeleton\".",
      ),
      axis: z.object({
        name: z.string().describe("Display name for the axis, e.g. \"Speed\"."),
        min: z.number().describe("Minimum axis value."),
        max: z.number().describe("Maximum axis value. Must be greater than min."),
        grid_divisions: z.number().int().min(1).optional().describe(
          "Number of grid divisions for this axis. Default 4.",
        ),
      }).strict().describe("The blend space's single axis (1D builder)."),
      samples: z.array(z.object({
        animation: z.string().describe(
          "Object path of the AnimSequence this sample plays. Must target the same skeleton.",
        ),
        position: z.number().describe(
          "Where this sample sits on the axis, within [axis.min, axis.max]. No two samples "
          + "may share a position.",
        ),
        rate_scale: z.number().positive().optional().describe(
          "Playback rate multiplier for this sample. Default 1.0.",
        ),
      }).strict()).min(1).max(64).describe("1 to 64 sample points."),
      plan_only: z.boolean().optional().describe(
        "Default false. Validate everything and report would_create without touching anything.",
      ),
      save: z.boolean().optional().describe("Default true."),
    }).strict()],
  ["puerts_anim_blueprint_build", "anim_blueprint_build",
    "Create a NEW UE4.27 Animation Blueprint from one JSON spec, in one transaction: member "
    + "variables, the anim graph pipeline, a state machine with its states and transitions, and "
    + "an optional event graph. The compile result is RETURNED rather than assumed, and the asset "
    + "is read back through puerts_anim_blueprint_inspect before it is saved. CREATE-ONLY: it "
    + "REFUSES when an asset already exists at asset_path, so a rerun is a refusal and not a "
    + "no-op. Use puerts_anim_blueprint_patch, which takes this same spec, to change one that "
    + "exists. "
    + animBlueprintSpecLimits,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "AnimBlueprint package path under /Game/MCPGenerated/, no asset-name suffix. Must not "
        + "already exist.",
      ),
      ...animBlueprintSpecFields,
    }).strict()],
  ["puerts_anim_blueprint_patch", "anim_blueprint_patch",
    "Replace the generated contents of an Animation Blueprint that ALREADY EXISTS, from the same "
    + "JSON spec puerts_anim_blueprint_build takes. Build refuses an occupied path, this refuses "
    + "an empty one. CONVERGENT: the AnimGraph is cleared before repopulating, so the same spec "
    + "run twice leaves the same states and transitions; structure_hash_sha1 is NOT promised "
    + "stable across a rerun. PRECONDITION, a refusal rather than a warning: the asset must be "
    + "SAVED with no unsaved changes, because the rollback boundary here is the .uasset on disk. "
    + "Failure-atomic: nothing is written until the compile and the read-back have both passed. "
    + "Restoring from disk clears the editor's undo history (restore.undo_history_cleared). "
    + animBlueprintSpecLimits,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "AnimBlueprint package path under /Game/MCPGenerated/, no asset-name suffix. Must "
        + "already exist, and must be saved with no unsaved changes.",
      ),
      ...animBlueprintSpecFields,
    }).strict()],
  ["puerts_material_inspect", "material_inspect",
    "Read an existing UE4.27 Material or Material Instance back as machine-readable JSON. One "
    + "tool answers for both kinds because a caller holding an asset path usually does not know "
    + "which it has; asset_kind says which one answered and the field names are the same either "
    + "way. Always returns parameters, with type, effective value and (for an instance) whether "
    + "it overrides rather than inherits. A master material also returns its expression nodes, "
    + "their connections and material_inputs. structure_hash_sha1 excludes parameter VALUES on "
    + "purpose, so retinting an instance does not read as a reshape. Expression identity is "
    + "OBSERVED. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Material or Material Instance, as a package path (\"/Game/MCPGenerated/M_Rock\") "
        + "or the object path other tools hand back. Limited to /Game and /Engine.",
      ),
    }).strict()],
  ["puerts_material_instance_build", "material_instance_build",
    "Create or update a UMaterialInstanceConstant and set its scalar, vector, texture and "
    + "static switch parameters from one desired-state spec. Convergent: a parameter already at "
    + "the requested value and already overridden is reported unchanged, so a second run dirties "
    + "nothing. Every parameter is resolved against the parent and validated before the asset is "
    + "created or touched, so a name the parent does not publish is refused with the closest "
    + "matching names. Saves only after an independent read-back agrees with every requested "
    + "value. The parent material is authored by puerts_material_build; a texture comes from "
    + "puerts_texture_import. Assets are limited to /Game/MCPGenerated/. "
    + materialToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native "
        + "command enforces the same limit; this rejects earlier, at the client.",
      ),
      parent_path: z.string().optional().describe(
        "The Material or Material Instance to inherit from, under /Game or /Engine. "
        + "Required when the instance does not exist yet; on an existing instance, "
        + "supplying a different one reparents it.",
      ),
      scalars: z.record(z.number()).optional().describe(
        "Scalar parameter name to value, e.g. {\"Roughness\": 0.4}.",
      ),
      vectors: z.record(z.object({
        r: z.number(), g: z.number(), b: z.number(), a: z.number().optional(),
      }).strict()).optional().describe(
        "Vector parameter name to linear color. a defaults to 1.",
      ),
      textures: z.record(z.string()).optional().describe(
        "Texture parameter name to texture asset path. A path that loads no texture is "
        + "refused before anything is written.",
      ),
      switches: z.record(z.boolean()).optional().describe(
        "Static switch parameter name to true or false.",
      ),
      clear_unlisted: z.boolean().optional().describe(
        "Default false. True makes the spec the whole desired state: unmentioned overrides are "
        + "dropped. unlisted_overrides is reported either way.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Answer with the plan and open no transaction.",
      ),
      save: z.boolean().optional().describe(
        "Default true. An instance whose read-back disagreed with the request is never saved.",
      ),
    }).strict()],
  ["puerts_material_build", "material_build",
    "Author a UMaterial graph from one desired-state spec: expression nodes, named scalar, "
    + "vector, texture and static switch parameters, the links between the nodes, and the links "
    + "into BaseColor, EmissiveColor, Roughness, Normal and the rest. This is the parent that "
    + "puerts_material_instance_build then overrides. The spec is the WHOLE graph: a build aimed "
    + "at an existing material replaces the graph it had, which is what makes a rerun converge. "
    + "Runs in one transaction, reports the compile result in the response, and saves only after "
    + "an independent puerts_material_inspect read-back agrees that every requested link, output "
    + "and parameter landed. Node ids are yours and expression names are the engine's, so the "
    + "response maps each spec id to the expression_id the inspector will report. Assets are "
    + "limited to /Game/MCPGenerated/. "
    + materialToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. The native command "
        + "enforces the same limit; this rejects earlier, at the client.",
      ),
      domain: z.enum([
        "Surface", "DeferredDecal", "LightFunction", "Volume", "PostProcess", "UI",
      ]).optional().describe("Default Surface."),
      blend_mode: z.enum([
        "Opaque", "Masked", "Translucent", "Additive", "Modulate", "AlphaComposite", "AlphaHoldout",
      ]).optional().describe("Default Opaque."),
      shading_model: z.enum([
        "Unlit", "DefaultLit", "Subsurface", "PreintegratedSkin", "ClearCoat",
        "SubsurfaceProfile", "TwoSidedFoliage", "Hair", "Cloth", "Eye",
      ]).optional().describe("Default DefaultLit. Unlit is the one an emissive-only material wants."),
      two_sided: z.boolean().optional().describe("Default false."),
      parameters: z.array(z.object({
        kind: z.enum(["scalar", "vector", "texture", "switch"]),
        name: z.string().min(1).describe(
          "The parameter name a material instance will override. Also the node's id unless id "
          + "is given, so outputs and connections can name it directly.",
        ),
        id: z.string().optional().describe("Node id, if it should differ from name."),
        default: z.union([
          z.number(),
          z.boolean(),
          z.string(),
          z.object({ r: z.number(), g: z.number(), b: z.number(), a: z.number().optional() }).strict(),
        ]).optional().describe(
          "A number for scalar, {r,g,b,a} for vector, true/false for switch, and a texture asset "
          + "path for texture. REQUIRED for texture: a texture sample with no texture does not "
          + "compile, and puerts_texture_import can generate one.",
        ),
        group: z.string().optional().describe("Parameter group shown in the instance editor."),
        x: z.number().optional(),
        y: z.number().optional(),
      }).strict()).optional().describe(
        "Named parameter nodes. Sugar over expressions, sharing one id namespace with them.",
      ),
      expressions: z.array(z.object({
        id: z.string().min(1).describe("Your id for this node, referenced by connections and outputs."),
        type: z.string().min(1).describe(
          "Expression class, short or full: \"Multiply\", \"Constant3Vector\", \"TextureSample\", "
          + "or \"MaterialExpressionMultiply\". Must resolve to a concrete UMaterialExpression "
          + "subclass; anything else is refused.",
        ),
        params: z.record(z.unknown()).optional().describe(
          "Property name to value on that node, e.g. {\"ConstA\": 2.0} on a Multiply. An unknown "
          + "property is refused with the node's editable property names.",
        ),
        x: z.number().optional(),
        y: z.number().optional(),
      }).strict()).optional(),
      connections: z.array(z.object({
        from: z.string().min(1).describe("\"nodeId\" for its first output, or \"nodeId.RGB\"."),
        to: z.string().min(1).describe("\"nodeId.InputName\", e.g. \"Glow.A\". A wrong name is "
          + "refused with the node's real input names."),
      }).strict()).optional(),
      outputs: z.record(z.string()).optional().describe(
        "Material property to node id: {\"EmissiveColor\": \"Glow\"}. Accepts the same names "
        + "puerts_material_inspect reports under material_inputs.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. True validates the whole spec and answers with the planned nodes without "
        + "creating or touching an asset.",
      ),
      save: z.boolean().optional().describe(
        "Default true. A material whose read-back or compile disagreed is never saved.",
      ),
    }).strict()],
  ["puerts_texture_import", "texture_import",
    "Generate a UTexture2D asset so a texture parameter has something to point at. GENERATED, "
    + "not imported from disk: a solid colour or a checker is reproducible from the spec, which "
    + "is what makes it convergent. source_file is REFUSED with a reason rather than quietly "
    + "ignored; use pattern, color and color_b. The write is transacted and the source pixels are "
    + "compared byte for byte before anything is saved. Assets are limited to /Game/MCPGenerated/. "
    + materialToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix.",
      ),
      pattern: z.enum(["solid", "checker"]).optional().describe("Default solid."),
      width: z.number().int().optional().describe("Power of two, 4 to 2048. Default 256."),
      height: z.number().int().optional().describe(
        "Power of two, 4 to 2048. Defaults to width.",
      ),
      checker_size: z.number().int().optional().describe(
        "Checker square edge in pixels. Default 32. Ignored for a solid texture.",
      ),
      color: z.object({
        r: z.number(), g: z.number(), b: z.number(), a: z.number().optional(),
      }).strict().optional().describe(
        "Linear colour 0..1. The solid colour, or the first checker colour. Default white.",
      ),
      color_b: z.object({
        r: z.number(), g: z.number(), b: z.number(), a: z.number().optional(),
      }).strict().optional().describe("The second checker colour. Default dark grey."),
      srgb: z.boolean().optional().describe(
        "Defaults from compression: true for Default, false for Normalmap, Masks and Grayscale.",
      ),
      compression: z.enum(["Default", "Normalmap", "Masks", "Grayscale"]).optional(),
      source_file: z.string().optional().describe(
        "Accepted by the schema only so the refusal explains itself. File import is not "
        + "implemented.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Answer with the plan and open no transaction.",
      ),
      save: z.boolean().optional().describe("Default true."),
    }).strict()],
  ["puerts_scene_inspect", "scene_inspect",
    "Read the editor's current level back as machine-readable JSON: every actor with its object "
    + "name, label, class, world transform, folder, tags, bounds, attachment and components, plus "
    + "every PlayerStart and a canonical structure_hash_sha1. The read half of puerts_scene_batch, "
    + "and READ ONLY: no transaction, nothing saved, and the level package dirty flag reported "
    + "before and after. Actor identity is the object name, not the label. Transforms are WORLD "
    + "transforms, the same space puerts_scene_batch writes. The hash always covers the WHOLE "
    + "level, never the filtered view. level_path is an assertion, not a target: it refuses on a "
    + "mismatch rather than loading a level. "
    + sceneToolsRef,
    z.object({
      level_path: z.string().optional().describe(
        "The level you believe is loaded (\"/Game/Maps/Test\"). Refuses on a mismatch. Omit to "
        + "read whatever the editor has open.",
      ),
      actors: z.array(z.string()).max(500).optional().describe(
        "Object names, labels or object paths. Narrows what is reported; never narrows the hash.",
      ),
      include_components: z.boolean().optional().describe(
        "Default true. Attach each actor's components with their class, attach parent, relative "
        + "transform and mobility.",
      ),
      include_properties: z.array(z.string()).max(32).optional().describe(
        "Reflected property names to read on every reported actor, returned under properties. A "
        + "property the actor does not have is absent rather than null, because null would say it "
        + "exists and holds nothing.",
      ),
    }).strict()],
  ["puerts_scene_batch", "scene_batch",
    "Apply a desired-state description of many actors to the editor's current level in ONE "
    + "transaction: spawn, modify, delete, reparent, set folders, tags and reflected properties on "
    + "actors and on the components they already have. A scene that would take fifty "
    + "puerts_spawn_actor and puerts_set_property calls is one call. Lights, post-process volumes, "
    + "trigger volumes, blocking volumes and nav mesh bounds volumes are all upsert_actor with a "
    + "class path and properties; there is no separate lighting or volume tool. A selector matching "
    + "more than one actor is a refusal that names the matches, never a guess. The AGENTS.md "
    + "PlayerStart rule is enforced automatically: a trigger volume containing a PlayerStart "
    + "refuses the whole batch. Any failure rolls back and re-hashes the level to decide "
    + "rollback_succeeded. IT NEVER SAVES: the level is left dirty and puerts_save is what writes "
    + "it to disk. "
    + sceneToolsRef,
    z.object({
      level_path: z.string().optional().describe(
        "The level you believe is loaded. Refuses on a mismatch; never loads a level.",
      ),
      operations: z.array(z.record(z.unknown())).min(1).max(500).describe(
        "Ordered batch. "
        + "{op:\"upsert_actor\", select?:{name|path|label}, label?, class?, location?, rotation?, "
        + "scale?, folder?, tags?, attach_to?, properties?, components?} and "
        + "{op:\"delete_actor\", select:{name|path|label}}. class is an actor class path, "
        + "required when the operation has to spawn. Transforms are WORLD space; an omitted "
        + "component keeps its current value. properties is {propertyName: value} on the actor "
        + "and components is {componentName: {propertyName: value}} on components the actor "
        + "ALREADY has. Every property write is gated by the same AllowedWritableProperties "
        + "allowlist puerts_set_property uses. Allowed class roots and per-field rules: "
        + sceneToolsRef,
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Classify the batch and change nothing.",
      ),
      verify: z.boolean().optional().describe(
        "Default true. Re-read every operation's own condition from the level after applying. "
        + "Off removes the only check that distinguishes a change from a report of one.",
      ),
      preconditions: sceneBatchPreconditions,
    }).strict()],
  ["puerts_lighting_build", "lighting_build",
    "Build lighting for the level the editor has open, so placed lights are baked rather than "
    + "preview-only. IT DOES NOT WAIT FOR THE BUILD: action=\"start\" starts it and returns with "
    + "waited:false. Poll with action=\"status\" until build_running is false before "
    + "screenshotting or saving, and read lighting_unbuilt_objects to see whether the level still "
    + "needs a rebuild. started is read back from the editor rather than assumed, because UE4.27 "
    + "swallows a refused build. Refused during Play In Editor, and there is no cancel. "
    + sceneToolsRef,
    z.object({
      action: z.enum(["start", "status"]).optional().describe("Default start. status changes nothing."),
      quality: z.enum(["Preview", "Medium", "High", "Production"]).optional().describe(
        "Default Preview, which is the quality to use when the point is to see the scene lit at "
        + "all. Production is minutes to hours and is a deliberate choice, not a default.",
      ),
      only_current_level: z.boolean().optional().describe(
        "Default true: build the current level rather than every loaded sublevel.",
      ),
      level_path: z.string().optional().describe(
        "The level you believe is loaded. Refuses on a mismatch; never loads a level.",
      ),
    }).strict()],
  ["puerts_job_status", "job_status",
    "Report the state of a long job the editor started (puerts_lighting_build, puerts_nav_build, "
    + "puerts_sequence_render_start), or list every job it holds. It reports a STAGE and live "
    + "counters, never a percent: no UE4.27 entry point on these paths reports a completion "
    + "fraction. state is running, succeeded, failed or cancelled, and every answer carries "
    + "cancel_supported and cancel_effect because cancellation is not uniform. A job id from a "
    + "previous editor session answers job_lost_editor_restarted rather than being reported as "
    + "still running. Call with no job_id to list every job. "
    + sceneToolsRef,
    z.object({
      job_id: z.string().optional().describe(
        "The id a *_start command returned. Omit to list every job this editor holds.",
      ),
    }).strict()],
  ["puerts_job_result", "job_result",
    "Collect the finished output of a job, ONCE. The result is kept in the editor beside the job "
    + "record, so a client that lost the start command's response can still read what it "
    + "answered. Refuses with job_still_running if the job has not finished (poll "
    + "puerts_job_status first) and job_result_consumed on a second call. The answer carries the "
    + "job record and start_result, the body the starting command returned. "
    + sceneToolsRef,
    z.object({
      job_id: z.string().describe("The id a *_start command returned."),
    }).strict()],
  ["puerts_job_cancel", "job_cancel",
    "Ask a running job to stop. CANCELLATION IS NOT UNIFORM AND THIS TOOL DOES NOT PRETEND IT IS. "
    + "Every answer reports cancel_effect for that job: \"immediate\" only for a sequence render, "
    + "\"deferred\" for a lighting or navigation build. A deferred cancel does NOT interrupt an "
    + "engine call already in progress, and stopped_now says which of the two happened. It "
    + "refuses rather than lying: job_not_running, cancel_unsupported, cancel_target_gone. "
    + "Nothing is rolled back: a cancelled navmesh is partial and a cancelled render leaves the "
    + "frames it already wrote. Work that blocks the game thread inside one engine call cannot be "
    + "cancelled at all, which is why puerts_nav_build with wait:true returns no job_id. "
    + inspectorsRef,
    z.object({
      job_id: z.string().describe("The id a *_start command returned."),
    }).strict()],
  ["puerts_sequence_render_start", "sequence_render_start",
    "Render a ULevelSequence to image files and return a job_id. NOTHING IS RENDERED WHEN THIS "
    + "RETURNS: it spawns a second UE process and answers. Poll puerts_job_status until state is "
    + "no longer \"running\", then collect with puerts_job_result. IT REFUSES ON AN UNSAVED LEVEL "
    + "OR AN UNSAVED SEQUENCE, by name, because the render process reads both from disk and would "
    + "otherwise render the previous version and exit successfully. Save with puerts_save first. "
    + "Refused during Play In Editor. output_directory must be inside the project. This is the one "
    + "job in the bridge that puerts_job_cancel can stop immediately. "
    + sequencerToolsRef,
    z.object({
      asset_path: z.string().describe("The ULevelSequence to render, under /Game."),
      output_directory: z.string().optional().describe(
        "Relative paths resolve against the project directory; anything outside the project is "
        + "refused. Default Saved/MCPRenders/<sequence name>.",
      ),
      format: z.enum(["png", "jpg", "bmp", "exr"]).optional().describe(
        "Default png. AVI is unavailable in the installed engine.",
      ),
      output_format: z.string().optional().describe(
        "The filename format string, e.g. \"{world}_{frame}\". Default is the engine's own.",
      ),
      resolution_x: z.number().int().optional().describe("Default 1280. Clamped to 16..7680."),
      resolution_y: z.number().int().optional().describe("Default 720. Clamped to 16..7680."),
      frame_rate: z.number().int().optional().describe(
        "Override the capture frame rate. Defaults to the sequence's own display rate.",
      ),
      warm_up_frames: z.number().int().optional().describe(
        "Frames played before capture starts, to let particles and post processing settle. "
        + "Default 0.",
      ),
      overwrite_existing: z.boolean().optional().describe(
        "Default true. Overwrites frames already in the output directory.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Run every refusal and start no process.",
      ),
    }).strict()],
  ["puerts_project_settings_maps", "project_settings_maps",
    "Set the project default game map, editor startup map, and global game mode. Values are "
    + "validated before mutation, applied to UGameMapsSettings, and persisted to the project's "
    + "Config/DefaultEngine.ini. Map values must name existing saved /Game maps. The game mode "
    + "must resolve to a GameModeBase class. At least one field is required.",
    z.object({
      game_default_map: z.string().regex(/^\/Game\//).optional(),
      editor_startup_map: z.string().regex(/^\/Game\//).optional(),
      global_default_game_mode: z.string().optional(),
    }).strict().refine(
      (value) => value.game_default_map !== undefined
        || value.editor_startup_map !== undefined
        || value.global_default_game_mode !== undefined,
      { message: "At least one project setting is required." },
    )],
  ["puerts_project_settings_patch", "project_settings_patch",
    "Set any config property on any Project Settings page and persist it to that page's "
    + "Default*.ini. section names the settings class behind the page, such as "
    + "\"GameMapsSettings\" or \"RendererSettings\"; the engine picks the target ini from the "
    + "class itself, so no path is given here. Read current values first with "
    + "puerts_read_property against the CDO path, e.g. "
    + "\"/Script/EngineSettings.Default__GameMapsSettings\". A property that is not marked config "
    + "is REFUSED rather than set, because it would change in memory and silently vanish on the "
    + "next editor start. Convergent. Verification reads the ini back off disk; on any "
    + "disagreement both the CDO and the previous ini bytes are restored. Containers are "
    + "confirmed present but not compared element by element "
    + "(persisted_but_not_value_compared). This edits project config, so editor undo does not "
    + "take it back. puerts_project_settings_maps stays the better call for maps and game mode.",
    z.object({
      section: z.string().describe(
        "The settings class behind the page, such as \"GameMapsSettings\". The full "
        + "\"/Script/EngineSettings.GameMapsSettings\" path and a leading U are both accepted.",
      ),
      properties: z.record(z.unknown()).describe(
        "{propertyName: value}, each value in the property's own reflected shape. Names are the "
        + "reflected C++ names, not the UI labels: \"bUseBorderlessWindow\", not \"Use Borderless "
        + "Window\". engine_source_search finds the reflected name behind a label.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Report properties_to_apply and unchanged_properties, and write nothing.",
      ),
    }).strict()],
  ["puerts_project_package_start", "project_package_start",
    "Start an asynchronous UE4.27 game-project cook, stage and package through a fixed UAT "
    + "BuildCookRun child process. This returns a job_id immediately and never waits on UAT. "
    + "Poll puerts_job_status, then collect with puerts_job_result. The served .uproject and "
    + "RunUAT path are derived by native code and cannot be supplied by the caller. Only Win64 "
    + "and Development or Shipping are accepted. Every map must be an existing saved /Game map, "
    + "all dirty content is refused, and output_directory must remain inside Project/Saved. "
    + "Cancellation kills only the owned process tree and leaves partial output. This packages "
    + "the game project; it does not invoke the unsupported UE4.27 BuildPlugin binary flow.",
    z.object({
      maps: z.array(z.string().regex(/^\/Game\//)).min(1).max(32),
      target: z.literal("Win64").optional(),
      configuration: z.enum(["Development", "Shipping"]).optional(),
      output_directory: z.string().optional(),
      plan_only: z.boolean().optional(),
    }).strict()],
  ["puerts_class_defaults_patch", "class_defaults_patch",
    "Set INHERITED class-default (CDO) values on a generated Blueprint as desired state: "
    + "AIControllerClass and AutoPossessAI on a pawn, and anything else on the bridge's "
    + "writable-property allowlist. A variable the Blueprint ITSELF declares is refused here and "
    + "pointed at puerts_blueprint_member_patch. The transaction is deliberately NOT the rollback, "
    + "because a class default object is not RF_Transactional: this command snapshots and restores "
    + "instead, and reports transaction_covers_cdo. When that is false, editor undo will NOT take "
    + "the change back. Convergent, and an unknown property name is answered with the closest names "
    + "on the class. It writes the CLASS default: actors already placed keep the value they were "
    + "placed with, and puerts_scene_batch is what changes those. "
    + blueprintToolsRef,
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(\/[A-Za-z0-9_]+)*$/).describe(
        "Package path under /Game/MCPGenerated/, no asset-name suffix. Patching never creates a "
        + "Blueprint; author it with puerts_blueprint_build first.",
      ),
      properties: z.record(z.unknown()).describe(
        "{propertyName: value} applied to the generated class default object, each value in its own "
        + "reflected shape. A class reference is a class object path ending in _C, such as "
        + "\"/Game/MCPGenerated/BP_Guard.BP_Guard_C\"; an enum is its entry name, such as "
        + "\"PlacedInWorldOrSpawned\". Every write is gated by the same AllowedWritableProperties "
        + "allowlist puerts_set_property uses.",
      ),
      plan_only: z.boolean().optional().describe(
        "Default false. Return properties_to_apply, unchanged_properties and expected_change_count, "
        + "and change nothing.",
      ),
      verify: z.boolean().optional().describe(
        "Default true. Read every patched value back off the CDO after writing it. Turning this off "
        + "removes the only check that distinguishes a change from a report of one.",
      ),
      save: z.boolean().optional().describe(
        "Default true. The save happens only after the read-back agrees with every requested value.",
      ),
    }).strict()],
  ["puerts_input_mapping_info", "input_mapping_info",
    "Read the project's input action and axis mappings. The independent read half of "
    + "puerts_input_mapping_patch, and READ ONLY: no transaction is opened, nothing is written, "
    + "and the native side touches only const accessors on UInputSettings. "
    + "Both arrays are canonically sorted, so two reads of an unchanged project return the same "
    + "content, and mapping_hash_sha1 is the same digest the patch command reports as "
    + "pre_mapping_hash_sha1 / post_mapping_hash_sha1 - so a patch can be verified against a read "
    + "that did not perform it. The hash always covers the WHOLE mapping set, never the filtered "
    + "view, and action_total / axis_total say how much a filtered read did not show. "
    + "Key names are FKey string form: W, SpaceBar, LeftMouseButton, MouseX, Gamepad_LeftTrigger. "
    + "Filters are exact matches, not substrings.",
    z.object({
      action_name: z.string().optional().describe("Exact action name to filter, such as PF_Pause."),
      axis_name: z.string().optional().describe("Exact axis name to filter, such as MoveForward."),
      key: z.string().optional().describe("Exact FKey name to filter, such as Escape."),
    }).strict()],
  ["puerts_input_mapping_patch", "input_mapping_patch",
    "Reconcile the project's input mappings against a desired set, in one call. The whole desired "
    + "set travels once, is classified against the mappings that exist before anything is "
    + "written, and a binding already present is reported in unchanged_operations rather than "
    + "reapplied. preset expands into the same actions and axes a caller could write by hand; "
    + "explicit entries are appended to the preset's. remove_actions and remove_axes name mappings "
    + "to delete: {name} alone removes every key bound to that name, {name, key} removes one. "
    + "remove_unlisted is refused outright when neither actions nor axes is given. An unresolvable "
    + "key name is refused by name before any mapping is written. These are ini writes "
    + "(Config/DefaultInput.ini), so there is no UE4 transaction: the boundary is a snapshot and "
    + "rollback_succeeded reports whether a re-read matches the pre-patch hash. "
    + aiToolsRef,
    z.object({
      preset: z.enum(["first_person", "third_person", "top_down", "tank"]).optional().describe(
        "A standard control scheme, expanded client-side into actions and axes.",
      ),
      actions: z.array(z.object({
        name: z.string().min(1).describe("Action name, e.g. Jump."),
        key: z.string().min(1).describe("FKey name, e.g. SpaceBar."),
        shift: z.boolean().optional(),
        ctrl: z.boolean().optional(),
        alt: z.boolean().optional(),
        cmd: z.boolean().optional(),
      }).strict()).max(200).optional(),
      axes: z.array(z.object({
        name: z.string().min(1).describe("Axis name, e.g. MoveForward."),
        key: z.string().min(1).describe("FKey name, e.g. W or MouseX."),
        scale: z.number().optional().describe("Default 1.0. Use -1.0 for the opposite direction."),
      }).strict()).max(200).optional(),
      remove_actions: z.array(z.object({
        name: z.string().min(1),
        key: z.string().optional().describe("Omit to remove every key bound to this action."),
      }).strict()).max(200).optional(),
      remove_axes: z.array(z.object({
        name: z.string().min(1),
        key: z.string().optional().describe("Omit to remove every key bound to this axis."),
      }).strict()).max(200).optional(),
      remove_unlisted: z.boolean().optional().describe(
        "Default false. Prune existing mappings not in the desired set, within the stated halves only.",
      ),
      plan_only: z.boolean().optional().describe("Default false. Classify the batch and write nothing."),
    }).strict()],
  ["puerts_folder_visibility", "folder_visibility",
    "Hide or show Content Browser folders, or read the hidden set. Display-only: a hidden folder "
    + "stays on disk, referenced and cookable, and only the browser stops showing it. The list "
    + "persists in Config/FolderVisibility.ini and is re-applied at editor startup. hidden is the "
    + "whole desired set and the command converges onto it; hide and show are the delta form. The "
    + "two shapes are mutually exclusive and a request carrying both is refused. Calling with none "
    + "of the three is the read. The response is always read back from the editor, never echoed. "
    + "Editor view state, not asset state, so there is no transaction: the reverse of any change "
    + "is another call. /Game itself cannot be hidden.",
    z.object({
      hidden: z.array(z.string()).max(200).optional().describe(
        "The complete set of folders that should be hidden. Mutually exclusive with hide/show.",
      ),
      hide: z.array(z.string()).max(200).optional().describe("Folders to hide, e.g. /Game/HorrorEngine."),
      show: z.array(z.string()).max(200).optional().describe("Folders to unhide."),
      plan_only: z.boolean().optional().describe("Default false. Report the plan and change nothing."),
    }).strict()],
  ["puerts_camera_shake", "camera_shake",
    "Play a camera shake on the running PIE session's player camera, through the full "
    + "PIE World -> PlayerController(0) -> PlayerCameraManager chain. Runtime effect only: no "
    + "asset is touched, nothing is persisted, and there is nothing to undo. "
    + "Refused with a named reason when PIE is not running, rather than reporting a shake nobody "
    + "could have seen. shake_class is the path to a CameraShake class; a Blueprint's runtime "
    + "class path usually ends in _C. "
    + "UE4.27 note: this build uses UCameraShakeBase with StartCameraShake(), not the older "
    + "UCameraShake / PlayCameraShake names.",
    z.object({
      shake_class: z.string().describe(
        "CameraShake class path, e.g. /Game/CameraShakes/CS_Explosion.CS_Explosion_C.",
      ),
      scale: z.number().positive().optional().describe(
        "Intensity multiplier, default 1.0. Must be greater than zero: a zero-amplitude shake "
        + "cannot be told apart from one that did not play.",
      ),
    }).strict()],
  ["puerts_pie_agent_query", "pie_agent_query",
    "The READ-ONLY half of the PIE gameplay agent: observe, read_property, status, expect. "
    + "op=observe captures the running session - pawn transform and velocity, nearby actors with "
    + "physics and collision state, on-screen widgets, player counters, and the Output Log tail. "
    + "op=read_property returns one reflected variable from one uniquely matched PIE actor. "
    + "op=status polls an asynchronous agent operation; operation_id 0 means the latest. "
    + "op=expect starts an in-engine check of declarative conditions and returns its operation_id; "
    + "it does not block until the conditions pass, so poll op=status for the verdict. "
    + "The write half (move_to, look_at, press, record_start, record_stop, replay) is NOT here. "
    + "AGENTS.md requires the user to ask before any pie_agent_* tool runs. Every op needs a live "
    + "PIE session and refuses cleanly without one. Nothing is transacted.",
    z.object({
      op: z.enum(["observe", "read_property", "status", "expect"]),
      radius: z.number().positive().optional().describe("observe: actor scan radius around the pawn in uu (default 1500)."),
      class_filter: z.string().optional().describe("observe: only include actors whose class name contains this text."),
      log_lines: z.number().int().min(0).optional().describe("observe: Output Log tail length (default 20)."),
      actor: z.string().optional().describe("read_property: exact actor name, label, path, or unique name or class substring."),
      property: z.string().optional().describe("read_property: reflected variable or state name to read."),
      operation_id: z.number().int().nonnegative().optional().describe("status: which operation to poll; 0 means the latest."),
      conditions: z.array(z.string()).min(1).optional().describe("expect: declarative actor_count, counter and log regex assertions."),
      within_seconds: z.number().positive().optional().describe("expect: deadline in seconds (default 5)."),
    }).strict()],
  ["puerts_pie_agent_control", "pie_agent_control",
    "Drive the existing PIE session through the native gameplay agent. Runtime-only: no asset is "
    + "changed and this tool never starts PIE. op=move_to and op=replay return operation_id for "
    + "polling through puerts_pie_agent_query op=status. op=look_at, press, key_state, axis_state, "
    + "clear_axes, record_start and record_stop apply immediately. Every op refuses cleanly when "
    + "its runtime preconditions are missing. AGENTS.md requires explicit user approval before "
    + "running this tool.",
    z.object({
      op: z.enum([
        "move_to", "look_at", "press", "key_state", "axis_state", "clear_axes",
        "record_start", "record_stop", "replay",
      ]),
      location: z.tuple([z.number(), z.number(), z.number()]).optional(),
      actor: z.string().min(1).optional(),
      timeout: z.number().positive().optional(),
      acceptance_radius: z.number().positive().optional(),
      action: z.string().min(1).optional(),
      key: z.string().min(1).optional(),
      keys: z.array(z.string().min(1)).min(1).max(32).optional(),
      hold_seconds: z.number().nonnegative().optional(),
      pressed: z.boolean().optional(),
      axis: z.string().min(1).optional(),
      value: z.number().min(-1).max(1).optional(),
      events: z.array(z.string()).max(64).optional(),
      class_filters: z.array(z.string()).max(64).optional(),
      log_categories: z.array(z.string()).max(64).optional(),
      max_events: z.number().int().min(1).max(65536).optional(),
      script: z.array(z.record(z.unknown())).max(1000).optional(),
      seed: z.number().int().optional(),
      budget_seconds: z.number().positive().optional(),
      export: z.boolean().optional(),
    }).strict()],
  ["puerts_sequence_inspect", "sequence_inspect",
    "Read a UE4.27 ULevelSequence back as machine-readable JSON: display rate, tick resolution, "
    + "playback range, every possessable and spawnable binding, every master and object track "
    + "with its sections, and every keyframe on every channel with its time, value and "
    + "interpolation. The independent read half of puerts_sequence_build. Binding identity is "
    + "OBSERVED (the FGuid UMovieScene stores), stable across renames and restarts. Frames are "
    + "reported in DISPLAY-RATE frames, the numbers puerts_sequence_build takes, with raw ticks "
    + "beside them. structure_hash_sha1 covers keyframe VALUES as well as structure, because for "
    + "a sequence a key value is the content. A possessable that resolves to no level actor "
    + "reports resolved false rather than being omitted. "
    + readOnly
    + inspectorsRef,
    z.object({
      asset_path: z.string().describe(
        "The Level Sequence, as a package path (\"/Game/MCPGenerated/LS_Intro\") or the object "
        + "path other tools hand back. Limited to /Game and /Engine.",
      ),
      include_keys: z.boolean().optional().describe(
        "Default true. False reports sections and channels with key_count only. It narrows what is "
        + "REPORTED and never what is hashed, because a hash of a filtered read is a hash of the "
        + "request.",
      ),
    }).strict()],
  ["puerts_sequence_build", "sequence_build",
    "Create or update a UE4.27 ULevelSequence from one desired-state spec: display rate, playback "
    + "range, actor bindings, tracks, sections and keyframes, in ONE transaction. CONVERGENT: "
    + "every binding, track, section and key is an upsert keyed on its own identity, so a rerun "
    + "of an identical spec writes nothing. ADDITIVE: it never removes anything it did not write "
    + "in this call, and there is no remove_unlisted. Failure-atomic, with rollback_succeeded "
    + "decided by re-hashing rather than by trusting the undo. Frames are DISPLAY-RATE frames "
    + "throughout, so a spec written against what Sequencer shows lands where it reads. Authoring "
    + "is limited to /Game/MCPGenerated/. "
    + sequencerToolsRef,
    z.object({
      asset_path: z.string().describe(
        "Package path of the Level Sequence to create or update, under /Game/MCPGenerated/.",
      ),
      frame_rate: z.number().positive().max(240).optional().describe(
        "Display rate in fps. Default 30 on creation; omitted keeps an existing sequence's rate. "
        + "Changing it does NOT move existing keys, and the response warns when it changed one.",
      ),
      playback_range: z.object({
        start_frame: z.number().int(),
        end_frame: z.number().int(),
      }).strict().optional().describe(
        "Playback range in display-rate frames, end exclusive, the same convention Sequencer uses.",
      ),
      bindings: z.array(z.object({
        id: z.string().min(1).max(64).describe(
          "Your own id for this binding, used by tracks in this same spec. Not stored in the "
          + "asset; the response maps it to the FGuid under binding_guids.",
        ),
        kind: z.enum(["possessable", "spawnable"]),
        name: z.string().min(1).max(128).describe(
          "Display name in Sequencer. This IS the convergence key together with kind: a rerun "
          + "finds the binding by name rather than making a second one.",
        ),
        actor_label: z.string().optional().describe(
          "possessable only. The label of an actor in the current level. A label matching no "
          + "actor, or more than one, is a refusal that names the matches.",
        ),
        actor_class: z.string().optional().describe(
          "spawnable only. Actor class path, limited to /Game/, /Script/Engine. and "
          + "/Script/CinematicCamera..",
        ),
      }).strict()).max(100).optional(),
      tracks: z.array(z.object({
        binding: z.string().min(1).describe(
          "A binding id declared in this spec, or \"master\" for a master track. A Camera track is "
          + "master-only.",
        ),
        type: z.enum(["Transform", "Float", "Bool", "Visibility", "Camera"]).describe(
          "Float and Bool need property. Visibility animates bHidden, so value true means HIDDEN. "
          + "Event and Audio tracks are refused by name.",
        ),
        property: z.string().optional().describe(
          "Float and Bool only. The property path Sequencer animates, e.g. \"Light.Intensity\". "
          + "Part of the track's identity.",
        ),
        sections: z.array(z.object({
          start_frame: z.number().int(),
          end_frame: z.number().int(),
          row_index: z.number().int().min(0).max(32).optional(),
          keys: z.array(z.object({
            frame: z.number().int(),
            value: z.union([
              z.number(),
              z.boolean(),
              z.string(),
              z.object({
                location: vector.optional(),
                rotation: rotator.optional(),
                scale: vector.optional(),
              }).strict(),
            ]).describe(
              "Float: a number. Bool and Visibility: true or false. Transform: "
              + "{location, rotation, scale}, any subset. Camera: the binding id to cut to.",
            ),
            interpolation: z.enum(["Linear", "Cubic", "Constant"]).optional().describe(
              "Default Cubic on float channels. Ignored on bool and camera-cut channels, with a "
              + "warning rather than a silent drop.",
            ),
          }).strict()).max(2000),
        }).strict()).max(100),
      }).strict()).max(100).optional(),
      plan_only: z.boolean().optional().describe(
        "Default false. Classify the spec against the sequence that exists, report "
        + "operations_to_apply, unchanged_operations, expected_change_count and "
        + "pre_structure_hash, and write nothing.",
      ),
      save: z.boolean().optional().describe(
        "Default true. A sequence whose read-back disagreed with the spec is never saved. A "
        + "converged rerun writes nothing to disk and says so under saved false.",
      ),
    }).strict()],
  ["puerts_physics_build", "physics_build", "Build a validated static-mesh rigid-body scene in one transaction.", z.object({ actors: z.array(physicsActor).min(1).max(200) }).strict()],
  ["puerts_physics_observe", "physics_observe", "Read rigid-body transforms and velocities from the editor or PIE world.", z.object({ actors: z.array(z.string()).max(200).optional() }).strict()],
  ["puerts_viewport_screenshot", "viewport_screenshot", "Fit requested actors and save a PNG of the active editor viewport.", z.object({ actors: z.array(z.string()).max(200).optional(), filename: z.string().optional() }).strict()],
  ["puerts_save", "save", "Save approved project assets and the current level.", z.object({ assets: z.array(z.string()).optional(), level_path: z.string().optional() }).strict()],
  ["puerts_level_create", "level_create",
    "Create, save and load a new project map. Refuses an existing target, a non-map template and "
    + "any dirty map or content package before switching levels.",
    z.object({
      level_path: z.string().min(1).describe("New map package path under /Game/, for example /Game/Maps/MyMap."),
      template_path: z.string().min(1).optional().describe("Existing project map package path to copy from."),
    }).strict()],
  ["puerts_level_load", "level_load",
    "Load an existing project map. Refuses non-map packages and refuses to switch while any map "
    + "or content package is dirty. Loading the already-current map is a read-only no-op.",
    z.object({ level_path: z.string().min(1).describe("Existing map package path under /Game/.") }).strict()],
  ["puerts_level_save", "level_save",
    "Save the current level and, when save_all is true, every dirty project package. Preserves "
    + "the legacy level_save schema.",
    z.object({ save_all: z.boolean().optional().describe("Also save all dirty map and content packages. Default false.") }).strict()],
  ["puerts_pie_start", "pie_start", "Request Play In Editor start.", z.object({}).strict()],
  ["puerts_pie_stop", "pie_stop", "Request Play In Editor stop.", z.object({}).strict()],
  ["puerts_get_logs", "get_logs", "Read captured Unreal logs and command output.", z.object({ maximum_lines: z.number().optional() }).strict()],
  ["puerts_undo", "undo", "Undo the exact last PuerTS transaction.", z.object({ transaction_id: z.string() }).strict()],
  ["puerts_anim_montage_build", "anim_montage_build",
    "Create or update a UE4.27 Animation Montage from one desired-state spec. The native writer validates slots, segments, sections and blend fields, independently inspects the result and restores the package on failure. Non-empty notifies are refused because UE4.27 has no atomic notify relink operation.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/[A-Za-z0-9_]+(?:\/[A-Za-z0-9_]+)*(?:\.[A-Za-z0-9_]+)?$/),
      skeleton: z.string().min(1),
      slots: z.array(z.object({
        slot_name: z.string().min(1),
        segments: z.array(z.object({
          animation: z.string().min(1),
          start_time: z.number().finite().optional(),
          start_position: z.number().finite().optional(),
          end_position: z.number().finite().optional(),
          play_rate: z.number().finite().optional(),
          loop_count: z.number().finite().optional(),
        }).strict()).min(1),
      }).strict()).min(1),
      sections: z.array(z.object({
        name: z.string().min(1),
        start_time: z.number().finite().optional(),
        next_section: z.string().optional(),
      }).strict()).min(1),
      notifies: z.array(z.unknown()).optional(),
      blend: z.object({
        blend_in_time: z.number().finite().optional(),
        blend_out_time: z.number().finite().optional(),
        blend_out_trigger_time: z.number().finite().optional(),
        enable_auto_blend_out: z.boolean().optional(),
      }).strict().optional(),
      sync_group: z.string().optional(),
      plan_only: z.boolean().optional(),
      save: z.boolean().optional(),
    }).strict()],
  ["puerts_sequence_event_track_build", "sequence_event_track_build",
    "Add or converge a director Blueprint event endpoint and event keys on an existing UE4.27 Level Sequence. The native command compiles the contained director Blueprint, verifies every requested key, saves when requested and restores the package on failure.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(?:\/[A-Za-z0-9_]+)*(?:\.[A-Za-z0-9_]+)?$/),
      binding_id: z.string().optional(),
      endpoint_name: z.string().min(1).max(64).regex(/^[A-Za-z_][A-Za-z0-9_]*$/),
      events: z.array(z.object({ frame: z.number().int() }).strict()).min(1).max(1024),
      call_in_editor: z.boolean().optional(),
      plan_only: z.boolean().optional(),
      save: z.boolean().optional(),
    }).strict()],
  ["puerts_data_table_build", "data_table_build",
    "Create or reconcile a UE4.27 DataTable from complete desired row JSON. The row struct and rows are validated on a transient table before mutation. Existing clean assets use package-file rollback, new assets use creation rollback, and UDataTable::GetTableAsJSON must match before save. Assets are limited to /Game/MCPGenerated/.",
    z.object({
      asset_path: z.string().regex(/^\/Game\/MCPGenerated\/[A-Za-z0-9_]+(?:\/[A-Za-z0-9_]+)*$/),
      row_struct: z.string().min(1).optional().describe(
        "ScriptStruct path. Required when creating; on update it may be omitted or must match.",
      ),
      rows_json: z.union([z.string().min(1), z.array(z.record(z.unknown()))]).optional().describe(
        "Complete desired rows as DataTable JSON text or an array. Omit for an empty table.",
      ),
      plan_only: z.boolean().optional().describe("Validate and report create/change state without writing."),
      save: z.boolean().optional().describe("Save after verified export read-back. Defaults true."),
    }).strict()],
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
  puerts_spawn_actor: ["location", "rotation", "scale"],
  puerts_class_defaults_patch: ["properties"],
  puerts_physics_build: ["actors"],
  puerts_physics_observe: ["actors"],
  puerts_viewport_screenshot: ["actors"],
  puerts_save: ["assets"],
  puerts_blueprint_build: ["components", "variables", "graph"],
  puerts_widget_build: ["tree"],
  puerts_sequence_build: ["playback_range", "bindings", "tracks"],
  puerts_widget_bind: ["bindings", "expose_as_variable"],
  puerts_input_mapping_patch: ["actions", "axes", "remove_actions", "remove_axes"],
  puerts_folder_visibility: ["hidden", "hide", "show"],
  puerts_pie_agent_query: ["conditions"],
  puerts_pie_agent_control: ["location", "keys", "events", "class_filters", "log_categories", "script"],
  puerts_scene_inspect: ["actors", "include_properties"],
  puerts_scene_batch: ["operations", "preconditions"],
  puerts_material_instance_build: ["scalars", "vectors", "textures", "switches"],
  puerts_material_build: ["parameters", "expressions", "connections", "outputs"],
  puerts_texture_import: ["color", "color_b"],
  puerts_behavior_tree_build: ["keys", "root"],
  puerts_anim_blueprint_build: ["variables", "anim_graph", "state_machine", "event_graph"],
  puerts_anim_blueprint_patch: ["variables", "anim_graph", "state_machine", "event_graph"],
  puerts_blackboard_build: ["keys"],
  puerts_nav_query: ["queries"],
  puerts_ai_perception_build: ["senses"],
  puerts_anim_montage_build: ["slots", "sections", "notifies", "blend"],
  puerts_sequence_event_track_build: ["events"],
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
  puerts_sequence_build: 30000,
  puerts_sequence_inspect: 15000,
  // Applies the bindings, then compiles the Widget Blueprint, and compiles it a
  // second time if it has to restore them. Two compiles is the ceiling worth
  // budgeting for.
  puerts_widget_bind: 30000,
  puerts_input_mapping_patch: 15000,
  puerts_scene_batch: 60000,
  puerts_scene_inspect: 15000,
  // Not a wait on the build: this budget covers the scene gather and the
  // Lightmass export that starting one costs. The build itself outlives the
  // call by design, which is what action="status" is for.
  puerts_lighting_build: 60000,
  // The job commands read a small in-memory record and make one live query.
  // They are left on the 7 second default deliberately: a job command that
  // needed longer would mean the game thread was busy, and the answer to that
  // is bridge_command_status, not a bigger budget here.
  // Loads the sequence asset, serializes a capture manifest and spawns a
  // process. It does NOT wait for the render, so this covers the asset load.
  puerts_sequence_render_start: 20000,
  puerts_class_defaults_patch: 20000,
  // folder_filter or include_transforms routes the read through the scene
  // snapshot, which walks the level twice; the 7 second default is close enough
  // to that on a large level to report a failure for work that succeeds.
  puerts_find_actors: 15000,
  // The spawn finishes through a one-operation scene batch when name, folder or
  // scale is given, and that batch verifies by reading the level back.
  puerts_project_settings_maps: 10000,
  // One CDO write plus one ini rewrite and read-back. No asset, no compile.
  puerts_project_settings_patch: 10000,
  puerts_project_package_start: 20000,
  puerts_spawn_actor: 15000,
  puerts_material_inspect: 15000,
  // A static switch change recompiles the instance's shader permutation, and
  // the command blocks on that compile rather than reporting "requested".
  puerts_material_instance_build: 30000,
  // A master material build blocks on a full shader compile, not one instance
  // permutation, and FinishCompilation is called on purpose so the response can
  // report a compile result instead of "requested".
  puerts_material_build: 90000,
  // Generation is cheap; the cost is PostEditChange building the platform mip
  // chain and running the compressor, which a 2048 square can spend seconds in.
  puerts_texture_import: 30000,
  puerts_behavior_tree_build: 30000,
  puerts_behavior_tree_inspect: 15000,
  puerts_widget_inspect: 15000,
  // An Animation Blueprint build compiles at least twice inside the builder
  // (once after variables, once at the end), then compiles again for the report
  // and reads the whole asset back before saving. 30s is a realistic ceiling for
  // one Blueprint compile and too tight for four passes over a state machine.
  puerts_anim_blueprint_build: 60000,
  // Everything the build path does, plus an inspect before the mutation and,
  // on the failure path only, a package reload. 90s rather than 60s because a
  // reload reinstances every object of the generated class.
  puerts_anim_blueprint_patch: 90000,
  puerts_anim_blueprint_inspect: 15000,
  puerts_anim_montage_inspect: 15000,
  puerts_anim_blend_space_inspect: 15000,
  // AddSample per sample plus one ValidateSampleData() triangulation rebuild,
  // then an independent anim_blend_space_inspect read-back before save. Same
  // budget as audio_build for a comparably small single-asset builder.
  puerts_anim_blend_space_build: 30000,
  puerts_anim_montage_build: 60000,
  puerts_sequence_event_track_build: 30000,
  puerts_blackboard_build: 20000,
  puerts_blackboard_inspect: 10000,
  puerts_eqs_inspect: 10000,
  // Reads the whole editor world twice over with TActorIterator. On a large
  // level that is well past the 7 second default.
  puerts_nav_inspect: 20000,
  // A hundred path queries on a large navmesh is real pathfinding work, done
  // synchronously on the game thread.
  puerts_nav_query: 20000,
  // The editor-side pipe deadline clamps at 30s, so this is the ceiling and not
  // a budget: wait: true blocks the game thread inside
  // UNavigationSystemV1::Build and nothing on either side can interrupt it. The
  // default wait: false answers in well under a second.
  puerts_nav_build: 30000,
  // Compiles the Blueprint, so it costs what any Blueprint compile costs.
  puerts_ai_perception_build: 30000,
  puerts_ai_controller_inspect: 15000,
  // A cue graph walk plus reflected properties on every node. Small next to a
  // Blueprint, but the 7 second default is close enough to a large cue on a
  // cold asset load to report a failure for work that succeeds.
  puerts_audio_build: 30000,
  puerts_audio_inspect: 15000,
  // Walks every LOD's render sections and every clothing asset's physical mesh.
  // A character mesh with several cloth LODs is a much larger read than a
  // Blueprint graph.
  puerts_cloth_inspect: 25000,
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
  // Durations from THIS process's monotonic clock, merged with the ones the
  // editor reported from its own. Nothing here is compared against an editor
  // timestamp: server_validation and server_total are wholly measured on this
  // side, and the editor's numbers are carried through untouched. That is what
  // makes the breakdown usable without the two clocks agreeing.
  const startedAt = performance.now();
  let validatedAt = startedAt;
  try {
    const parsed = spec.inputSchema.parse(
      decodeStructuredParams(spec.name, params),
    ) as Record<string, unknown>;
    validatedAt = performance.now();
    const result = await client.call(
      spec.command,
      parsed,
      commandTimeouts[spec.name],
    ) as unknown as Record<string, unknown>;
    const timings = (result.timings_ms ?? {}) as Record<string, unknown>;
    result.timings_ms = {
      ...timings,
      server_validation: validatedAt - startedAt,
      // Round trip as the server sees it: validation, pipe, editor, and the
      // wait behind whatever the editor was already doing. server_total minus
      // the editor's own runtime_total is the transport and scheduling cost.
      server_total: performance.now() - startedAt,
    };
    return result;
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

/**
 * Tools withheld from discovery, keyed by the puerts_* name.
 *
 * The DISCOVERY half of the quarantine. The runtime keeps the matching half in
 * puerts-runtime/src/registry.ts (quarantinedTools, keyed by native command
 * name) and refuses execution there, so removing a tool from tools/list is not
 * load-bearing on its own: it only stops a current client from choosing one.
 *
 * The two lists live on opposite sides of a process boundary and cannot import
 * each other - the runtime imports "ue", which exists only inside the editor.
 * puerts-tools.test.ts parses the runtime file and asserts the two agree, which
 * is the boundary test that replaces the generation that is not practical here.
 *
 * The specs stay in nativeToolSpecs: executeNativeCommand and the compatibility
 * aliases both resolve through it, and dropping the entry would make a stale
 * alias fail with a schema error instead of the quarantine reason.
 */
export const QUARANTINED_TOOLS: ReadonlySet<string> = new Set([
  "puerts_level_load",
  "puerts_level_create",
]);

export function createPuertsTools(client: PuerTSClient): ToolDefinition[] {
  return nativeToolSpecs
    .filter((spec) => !QUARANTINED_TOOLS.has(spec.name))
    .map((spec) => ({
      name: spec.name,
      description: `[PRIMARY NATIVE IPC] ${spec.description}`,
      inputSchema: spec.inputSchema,
      handler: async (params: Record<string, unknown>) => {
        const result = await executeNativeCommand(client, spec, params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    }));
}
