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

export function objectIdentity(object: UE.Object): JsonObject {
  return {
    name: object.GetName(),
    path: object.GetPathName(),
    class_name: object.GetClass().GetName(),
  };
}

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

export function toJsonValue(value: unknown, depth = 0, visited = new Set<object>()): JsonValue {
  if (value === null || typeof value === "string" || typeof value === "number" || typeof value === "boolean") {
    return value;
  }
  if (depth >= 5) {
    return "[maximum depth]";
  }
  if (Array.isArray(value)) {
    return value.slice(0, 500).map((entry: unknown) => toJsonValue(entry, depth + 1, visited));
  }
  if (typeof value === "object") {
    if (visited.has(value)) {
      return "[circular]";
    }
    visited.add(value);
    const candidate = value as Readonly<Record<string, unknown>>;
    if (typeof candidate.GetPathName === "function" && typeof candidate.GetName === "function") {
      const unrealObject = value as UE.Object;
      return objectIdentity(unrealObject);
    }
    const output: JsonObject = {};
    for (const key of Object.keys(candidate).slice(0, 200)) {
      const entry = candidate[key];
      if (typeof entry !== "function" && entry !== undefined) {
        output[key] = toJsonValue(entry, depth + 1, visited);
      }
    }
    return output;
  }
  return String(value);
}

export function vectorFrom(input: JsonObject | undefined): UE.Vector {
  if (input === undefined) {
    return new UE.Vector(0, 0, 0);
  }
  return new UE.Vector(numberField(input, "x", 0), numberField(input, "y", 0), numberField(input, "z", 0));
}

export function rotatorFrom(input: JsonObject | undefined): UE.Rotator {
  if (input === undefined) {
    return new UE.Rotator(0, 0, 0);
  }
  return new UE.Rotator(
    numberField(input, "pitch", 0),
    numberField(input, "yaw", 0),
    numberField(input, "roll", 0),
  );
}

export function numberField(input: JsonObject, key: string, fallback: number): number {
  const value = input[key];
  if (value === undefined) {
    return fallback;
  }
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(key + " must be a finite number");
  }
  return value;
}

export function optionalObject(input: JsonObject, key: string): JsonObject | undefined {
  const value = input[key];
  if (value === undefined || value === null) {
    return undefined;
  }
  if (typeof value !== "object" || Array.isArray(value)) {
    throw new Error(key + " must be an object");
  }
  return value;
}

export function stringArray(input: JsonObject, key: string): string[] {
  const value = input[key];
  if (value === undefined) {
    return [];
  }
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
