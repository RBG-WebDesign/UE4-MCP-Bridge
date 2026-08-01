import * as UE from "ue";
import { JsonObject, JsonSchema, JsonValue, response, CommandResponse } from "./types";

export const outputSchema: JsonSchema = {
  type: "object",
  properties: {
    success: { type: "boolean" },
    message: { type: "string" },
    data: {},
    changed_assets: { type: "array", items: { type: "string" } },
    changed_actors: { type: "array", items: { type: "string" } },
    warnings: { type: "array", items: { type: "string" } },
    errors: { type: "array", items: { type: "string" } },
    log_output: { type: "array", items: { type: "string" } },
    transaction_id: { type: "string" },
  },
  required: [
    "success", "message", "changed_assets", "changed_actors", "warnings",
    "errors", "log_output", "transaction_id",
  ],
  additionalProperties: true,
};

export function findActor(bridge: UE.MCPPuerTSBridgeService, nameOrPath: string): UE.Actor {
  const actor = bridge.FindLevelActor(nameOrPath);
  if (actor === undefined || actor === null) {
    throw new Error("Actor not found: " + nameOrPath);
  }
  return actor;
}

export function resolveObject(bridge: UE.MCPPuerTSBridgeService, input: JsonObject): UE.Object {
  const actor = input.actor;
  if (typeof actor === "string" && actor.length > 0) {
    return findActor(bridge, actor);
  }
  const objectPath = input.object_path;
  if (typeof objectPath !== "string" || objectPath.length === 0) {
    throw new Error("actor or object_path is required");
  }
  const found = bridge.FindObjectByPath(objectPath);
  if (found === undefined || found === null) {
    throw new Error("Object not found: " + objectPath);
  }
  return found;
}

export function safeActorLabel(actor: UE.Actor): string {
  try {
    return actor.GetActorLabel();
  } catch {
    return actor.GetName();
  }
}

export function optionalObject(input: JsonObject, key: string): JsonObject | undefined {
  const raw = input[key];
  if (raw === undefined || raw === null) {
    return undefined;
  }
  const value = decodeStructuredValue(raw);
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(key + " must be an object");
  }
  return value;
}

/** Decode a JSON-encoded object or array back into the structure it encodes.
    The MCP server does this at the client boundary, where the mangling starts;
    this is the last gate before Unreal reflection, and it keeps the native
    lane correct for any client or server build that still sends structured
    input as text. Anything that is not text opening and closing as an object
    or an array is returned untouched, so a string property stays a string. */
export function decodeStructuredValue(value: JsonValue): JsonValue {
  if (typeof value !== "string") {
    return value;
  }
  const text = value.trim();
  const looksStructured = (text.startsWith("{") && text.endsWith("}"))
    || (text.startsWith("[") && text.endsWith("]"));
  if (!looksStructured) {
    return value;
  }
  try {
    const parsed = JSON.parse(text) as JsonValue;
    return parsed !== null && typeof parsed === "object" ? parsed : value;
  } catch {
    return value;
  }
}

export function objectArray(input: JsonObject, key: string): JsonObject[] {
  const raw = input[key];
  if (raw === undefined || raw === null) {
    return [];
  }
  const value = decodeStructuredValue(raw);
  const isObjectEntry = (entry: JsonValue): boolean =>
    entry !== null && typeof entry === "object" && !Array.isArray(entry);
  if (!Array.isArray(value) || !value.every(isObjectEntry)) {
    throw new Error(key + " must be an array of objects");
  }
  return value as JsonObject[];
}

export function stringArray(input: JsonObject, key: string): string[] {
  const raw = input[key];
  if (raw === undefined) {
    return [];
  }
  const value = decodeStructuredValue(raw);
  if (!Array.isArray(value) || !value.every((entry: JsonValue) => typeof entry === "string")) {
    throw new Error(key + " must be an array of strings");
  }
  return value as string[];
}

export function commandFailure(error: unknown): CommandResponse {
  const message = error instanceof Error ? error.message : String(error);
  const result = response(false, "Command failed.");
  result.errors.push(message);
  return result;
}
