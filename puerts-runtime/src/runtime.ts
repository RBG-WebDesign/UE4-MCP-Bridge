import * as UE from "ue";
import { JsonObject, JsonValue, response, CommandResponse } from "./types";


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

/** The shape a thrown tool error becomes. `data` is what a caller gets when the
    failing command has structure to report as well as a sentence: the same
    envelope, the same message, the same error text, and a body instead of an
    empty object. */
/**
 * A refusal with a stable, branchable code.
 *
 * The runtime is where most client-visible refusals actually originate, not
 * C++. The path and confirmation gates are checked here AND in the native
 * service, and this copy runs first, so a client asking for /Engine sees this
 * message and never reaches the native one. Coding only the C++ side would
 * therefore have produced codes almost nobody receives.
 */
export class RefusedError extends Error {
  constructor(readonly code: string, message: string) {
    super(message);
    this.name = "RefusedError";
  }
}

/** Refuse with a code. Reads at the call site like the `throw new Error` it replaces. */
export function refuse(code: string, message: string): never {
  throw new RefusedError(code, message);
}

export function commandFailure(error: unknown, data: JsonValue = {}): CommandResponse {
  const message = error instanceof Error ? error.message : String(error);
  const result = response(false, "Command failed.", data);
  if (error instanceof RefusedError) { result.error_code = error.code; }
  result.errors.push(message);
  return result;
}
