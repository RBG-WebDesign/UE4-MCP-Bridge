import * as UE from "ue";

export type JsonPrimitive = string | number | boolean | null;
export type JsonValue = JsonPrimitive | JsonObject | JsonValue[];
export interface JsonObject {
  [key: string]: JsonValue;
}

/**
 * Every permission the runtime grants, and the ONLY definition of the set.
 *
 * The union used to be written here and the runtime's grant list written
 * separately in registry.ts, and the two drifted: project.config.read,
 * project.config.write and pie.observe were added to the union alone, so
 * ToolRegistry.execute refused input_mapping_info, input_mapping_patch,
 * folder_visibility and pie_agent_query outright with "Missing permission" -
 * four registered tools that could not be called at all. Deriving the type
 * from the array removes the second list rather than fixing today's copy of
 * it.
 */
export const allPermissions = [
  "assets.read",
  "assets.write",
  "assets.delete",
  "actors.read",
  "actors.spawn",
  "actors.delete",
  "reflection.read",
  "reflection.write",
  "functions.call",
  "level.save",
  "editor.pie",
  // Ini-backed project and editor configuration: input mappings
  // (Config/DefaultInput.ini) and Content Browser folder visibility
  // (Config/FolderVisibility.ini). Separate from assets.* because these write
  // config, not content, and separate read from write so a client can be
  // allowed to inspect the input bindings without being allowed to rebind them.
  "project.config.read",
  "project.config.write",
  // Reading a running PIE session: the world snapshot, an operation's status,
  // an in-engine condition check. Distinct from editor.pie, which starts and
  // stops the session, because observing is not controlling.
  "pie.observe",
  // The asynchronous job API. Its own permission because none of the others
  // describes it: job_cancel kills a render process and unwinds a running
  // Lightmass or Recast build, which is neither an asset write nor a
  // reflection write, and a manifest that called it "assets.write" would be
  // wrong about what the tool can do.
  "jobs.control",
  "logs.read",
  "viewport.capture",
  "transactions.undo",
] as const;

export type Permission = typeof allPermissions[number];


export interface CommandRequest {
  readonly id: string;
  readonly tool: string;
  readonly params: JsonObject;
  readonly timeout_ms?: number;
  readonly transaction_id: string;
}

export interface CommandResponse {
  success: boolean;
  message: string;
  /** Stable, branchable reason for a refusal. Absent on success and on the
      failures that have not been given a code yet: a client must treat a
      missing code as "read the message", never as "no error". The full code
      list and the migration of the ~250 free-text native refusals is the
      separate step; this field exists so quarantine can ship with one. */
  error_code?: string;
  /** Durations this process measured, in milliseconds. Never timestamps: the
      server and the editor are separate processes and their clocks are not
      comparable, so each layer reports how long IT took and the server adds its
      own. Merged into the envelope by CompleteCommand, which has to name the
      field explicitly or it is dropped. */
  timings_ms?: JsonObject;
  data: JsonValue;
  changed_assets: string[];
  changed_actors: string[];
  warnings: string[];
  errors: string[];
  log_output: string[];
  transaction_id: string;
}

export interface ToolContext {
  readonly bridge: UE.MCPPuerTSBridgeService;
  readonly transactionId: string;
}

/**
 * A tool the runtime will execute.
 *
 * There is deliberately no schema here. This registry used to carry an
 * inputSchema and an outputSchema per tool, and they were a second, thinner,
 * hand-maintained copy of the zod schemas in mcp-server/src/tools/puerts.ts.
 * They validated nothing: ToolRegistry.execute never read them, and their only
 * consumer was a manifest() method with no callers anywhere in the repository.
 * What they did do was drift - four tools declared `number` where the enforced
 * contract is an integer - so they were documentation that could be wrong about
 * the contract while looking authoritative next to the code that runs it.
 *
 * The single source for a tool's input contract is the zod schema at the MCP
 * boundary. It is validated before anything is sent, it is what tools/list
 * publishes, and the native C++ re-checks every value it acts on. Permissions
 * and the timeout stay here because execute() actually uses them.
 */
export interface ToolDefinition {
  readonly name: string;
  readonly permissions: readonly Permission[];
  readonly executionTimeoutMs: number;
  execute(context: ToolContext, input: JsonObject): Promise<CommandResponse>;
}

export function response(success: boolean, message: string, data: JsonValue = {}): CommandResponse {
  return {
    success,
    message,
    data,
    changed_assets: [],
    changed_actors: [],
    warnings: [],
    errors: [],
    log_output: [],
    transaction_id: "",
  };
}

export function requireString(input: JsonObject, key: string): string {
  const value = input[key];
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(key + " must be a non-empty string");
  }
  return value;
}

export function optionalString(input: JsonObject, key: string): string | undefined {
  const value = input[key];
  if (value === undefined || value === null || value === "") {
    return undefined;
  }
  if (typeof value !== "string") {
    throw new Error(key + " must be a string");
  }
  return value;
}

export function optionalBoolean(input: JsonObject, key: string, fallback: boolean): boolean {
  const value = input[key];
  if (value === undefined) {
    return fallback;
  }
  if (typeof value !== "boolean") {
    throw new Error(key + " must be a boolean");
  }
  return value;
}

export function optionalNumber(input: JsonObject, key: string, fallback: number): number {
  const value = input[key];
  if (value === undefined) {
    return fallback;
  }
  if (typeof value !== "number" || !Number.isFinite(value)) {
    throw new Error(key + " must be a finite number");
  }
  return value;
}

export function requireObject(input: JsonObject, key: string): JsonObject {
  const value = input[key];
  if (value === null || Array.isArray(value) || typeof value !== "object") {
    throw new Error(key + " must be an object");
  }
  return value;
}
