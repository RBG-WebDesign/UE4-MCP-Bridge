/**
 * Blueprint member and graph editing tools.
 *
 * Bridges to the C++ UBlueprintInspectorLibrary (read) and
 * UBlueprintMutatorLibrary (write) via the Python listener. Pin connections
 * are validated by UEdGraphSchema_K2::CanCreateConnection in C++ before
 * linking. Every mutation compiles and saves the Blueprint; on compile
 * failure the tool returns success=false with the compiler errors.
 */

import { z } from "zod";
import { UnrealClient } from "../unreal-client.js";
import type { ToolDefinition } from "../types.js";

// TypeDescriptor: matches FBPTypeDescriptor JSON in C++.
const typeDescriptorSchema = z.object({
  category: z.string().describe(
    "Pin category: bool, byte, int, int64, float, name, string, text, " +
    "object, class, struct, enum (matches UEdGraphSchema_K2 pin categories)."),
  sub_category: z.string().optional().describe("Pin sub-category where applicable."),
  sub_category_object: z.string().optional().describe(
    "Object path for object/class/struct/enum types, e.g. /Script/Engine.Actor or /Script/CoreUObject.Vector."),
  container_type: z.enum(["none", "array", "set", "map"]).optional()
    .describe("Container type. Default none."),
}).describe("Blueprint type descriptor");

const paramListSchema = z.array(z.object({
  name: z.string(),
  type: typeDescriptorSchema,
})).describe("Parameter list: [{name, type}]");

const positionSchema = z.tuple([z.number(), z.number()])
  .describe("Graph position [x, y] in Slate coordinates.");

const blueprintPath = z.string().describe(
  "Content path of the Blueprint asset, e.g. /Game/Blueprints/BP_Door.");

export function createBlueprintGraphTools(client: UnrealClient): ToolDefinition[] {
  return [
    {
      name: "blueprint_inspect",
      description:
        "Read Blueprint structure: graphs, functions, variables, macros, interfaces, " +
        "event dispatchers, components (SCS), nodes in a graph with pins and links, " +
        "single node detail, or node search. Use before editing to resolve graph names, " +
        "node GUIDs, and pin names.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        action: z.enum([
          "graphs", "functions", "variables", "macros", "interfaces",
          "event_dispatchers", "components", "nodes", "node_detail", "find_nodes",
        ]).describe("What to inspect."),
        graph_name: z.string().optional().describe("Required for actions 'nodes' and 'node_detail'."),
        node_guid: z.string().optional().describe("Required for action 'node_detail'."),
        query: z.record(z.unknown()).optional().describe(
          "For 'find_nodes': {node_class?, calls_function?, references_variable?, references_asset?, graph_name?}."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_inspect", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_add_variable",
      description: "Add a member variable to a Blueprint, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Variable name."),
        type: typeDescriptorSchema,
        default_value: z.unknown().optional().describe("Optional default value (JSON-serializable)."),
        category: z.string().optional().describe("Editor category for organization."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_add_variable", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_remove_variable",
      description: "Remove a member variable from a Blueprint, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Variable name to remove."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_remove_variable", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_set_variable_default",
      description: "Set a member variable's default value, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Variable name."),
        default_value: z.unknown().describe("New default value (JSON-serializable)."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_set_variable_default", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_add_function",
      description:
        "Create a user function graph on a Blueprint with typed inputs/outputs, " +
        "then compile and save. Returns the function graph name.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Function name."),
        inputs: paramListSchema.optional(),
        outputs: paramListSchema.optional(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_add_function", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_remove_function",
      description: "Remove a user function graph by name, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Function name to remove."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_remove_function", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_add_event_dispatcher",
      description: "Create an event dispatcher (multicast delegate) with an optional parameter signature, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Dispatcher name."),
        signature: z.object({ parameters: paramListSchema }).optional()
          .describe("Delegate signature. Default: no parameters."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_add_event_dispatcher", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_remove_event_dispatcher",
      description: "Remove an event dispatcher by name, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        name: z.string().describe("Dispatcher name to remove."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_remove_event_dispatcher", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_add_interface",
      description: "Implement a Blueprint Interface on a Blueprint, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        interface_class: z.string().describe(
          "Interface class path: /Script/Module.Name for native, /Game/Path/BPI_X.BPI_X_C for Blueprint interfaces."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_add_interface", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_remove_interface",
      description: "Remove an interface implementation (deletes its generated function graphs), then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        interface_class: z.string().describe("Interface class path."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_remove_interface", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_component_remove",
      description: "Remove a component (SCS node) from a Blueprint; children re-parent to the removed node's parent. Compiles and saves.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        component_name: z.string().describe("Component variable name."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_component_remove", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_component_rename",
      description: "Rename a component (SCS node) on a Blueprint, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        old_name: z.string(),
        new_name: z.string(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_component_rename", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_node_add",
      description:
        "Add a node to a Blueprint graph. Returns the new node's GUID for pin wiring. " +
        "node_type is a registered factory: CallFunction, Event, CustomEvent, VariableGet, " +
        "VariableSet, Branch, Sequence, Knot, Comment, Cast, CreateWidget, SpawnActor, " +
        "MacroInstance, MakeStruct, BreakStruct, Select, FormatText, SwitchEnum, SwitchInt, " +
        "SwitchName, SwitchString, MultiGate, DoOnceMultiInput, InputAction, InputAxisEvent, " +
        "InputKey, AddDelegate, RemoveDelegate, CallDelegate, and more. config keys are " +
        "camelCase and depend on node_type: CallFunction {targetClass, functionName}; " +
        "Event {parentClass, eventName}; CustomEvent {eventName, parameters?}; " +
        "VariableGet/VariableSet {varName, scope?}; Cast {targetClass, purity?}; " +
        "SpawnActor {actorClass}; CreateWidget {widgetClass}; MakeStruct/BreakStruct " +
        "{structType}; Comment {text}; MacroInstance {macroBP, macroName}. " +
        "Class paths use /Script/Module.Class form. Compiles and saves.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string().describe("Target graph, e.g. EventGraph or a function name."),
        node_type: z.string().describe("Registered node factory name."),
        position: positionSchema.optional(),
        config: z.record(z.unknown()).optional().describe("Node-type-specific configuration."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_node_add", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_node_delete",
      description: "Delete a node by GUID (links broken first), then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string(),
        node_guid: z.string(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_node_delete", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_node_move",
      description: "Move a node to a new graph position, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string(),
        node_guid: z.string(),
        position: positionSchema,
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_node_move", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_node_set_enabled",
      description: "Enable or disable a node by GUID, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string(),
        node_guid: z.string(),
        enabled: z.boolean(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_node_set_enabled", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_pins_connect",
      description:
        "Connect two pins between nodes. The connection is validated by " +
        "UEdGraphSchema_K2::CanCreateConnection before linking - incompatible " +
        "types fail cleanly with no graph change. Compiles and saves on success.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string(),
        source_node_guid: z.string().describe("GUID of the output-side node."),
        source_pin: z.string().describe("Output pin name (e.g. 'then', 'ReturnValue')."),
        target_node_guid: z.string().describe("GUID of the input-side node."),
        target_pin: z.string().describe("Input pin name (e.g. 'execute', 'Condition')."),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_pins_connect", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
    {
      name: "blueprint_pins_break",
      description: "Break all links on a single pin of a node, then compile and save.",
      inputSchema: z.object({
        blueprint_path: blueprintPath,
        graph_name: z.string(),
        node_guid: z.string(),
        pin_name: z.string(),
      }),
      handler: async (params) => {
        const result = await client.sendCommand("blueprint_pins_break", params);
        return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
      },
    },
  ];
}
