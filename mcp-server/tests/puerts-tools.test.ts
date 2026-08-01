import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { PuerTSClient } from "../src/puerts-client.js";
import {
  createPuertsTools,
  decodeStructuredParams,
  decodeStructuredValue,
} from "../src/tools/puerts.js";

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

async function main(): Promise<void> {
  const repoRoot = join(dirname(fileURLToPath(import.meta.url)), "..", "..");
  const startup = await readFile(join(repoRoot, "Plugins", "MCPBridge", "Content", "Python", "startup.py"), "utf8");
  assert(startup.includes('os.environ.get("MCP_ENABLE_LEGACY_HTTP") != "1"'), "legacy HTTP listener is not opt-in" );
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-client-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-test-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;

  let requestCount = 0;
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    requestCount += 1;
    const request = JSON.parse(data.toString("utf8")) as { auth?: string; tool?: string };
    assert(request.auth === "test-token", "bearer token was not forwarded");
    assert(request.tool === "find_actors", "tool name was not forwarded");
    if (requestCount > 1) {
      socket.end("{}\n");
      return;
    }
    socket.end(JSON.stringify({
      success: true,
      message: "Actors found.",
      data: { count: 0 },
      changed_assets: [],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const client = new PuerTSClient();
    const tools = createPuertsTools(client);
    assert(tools.length === 18, "expected all 18 PuerTS tools");
    assert(tools.some((tool) => tool.name === "puerts_sky_shader_create"), "native sky shader tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_blueprint_build"), "native Blueprint builder tool is missing");
    const response = await client.call("find_actors", {});
    assert(response.success && response.message === "Actors found.", "valid response was rejected");
    const actorTool = tools.find((tool) => tool.name === "puerts_find_actors");
    assert(actorTool !== undefined, "puerts_find_actors is missing");
    const failed = await actorTool.handler({});
    const failedPayload = JSON.parse(failed.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(failedPayload.success === false, "native client failure was not structured");
    assert(Array.isArray(failedPayload.errors) && Array.isArray(failedPayload.changed_assets), "failure envelope is incomplete");
    assert(failedPayload.transport === "named_pipe", "failure envelope omitted the attempted transport");
    console.log("  PASS  PuerTS registry, authenticated named-pipe client, and structured failures");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** Defect 2 of docs/CAPABILITY_FINDINGS.md: a client with no schema type
    information sends a struct or an array as JSON text, and the reflection
    lane then sees a string. These assertions pin the decode rules and prove
    the decoded structure reaches the pipe unflattened. */
async function marshalingSuite(): Promise<void> {
  assert(
    JSON.stringify(decodeStructuredValue('{"x":10,"y":20,"z":112}')) === '{"x":10,"y":20,"z":112}',
    "JSON object text was not decoded into an object",
  );
  assert(
    Array.isArray(decodeStructuredValue('["probe_a","probe_b"]')),
    "JSON array text was not decoded into an array",
  );
  assert(
    typeof decodeStructuredValue('  {"x":1}  ') === "object",
    "surrounding whitespace defeated the decode",
  );
  assert(decodeStructuredValue("ProbeRenamed") === "ProbeRenamed", "a plain string was rewritten");
  assert(decodeStructuredValue("123") === "123", "a numeric string was rewritten");
  assert(decodeStructuredValue("null") === "null", "a null literal string was rewritten");
  assert(decodeStructuredValue('{"x":') === '{"x":', "malformed JSON text was rewritten");
  assert(decodeStructuredValue('{"x":1}extra') === '{"x":1}extra', "trailing text was rewritten");
  assert(decodeStructuredValue(50000) === 50000, "a number was rewritten");
  assert(decodeStructuredValue(true) === true, "a boolean was rewritten");
  assert(decodeStructuredValue(null) === null, "null was rewritten");

  const alreadyStructured = { x: 1, y: 2, z: 3 };
  assert(
    decodeStructuredValue(alreadyStructured) === alreadyStructured,
    "an already-structured value was not passed through untouched",
  );

  const decoded = decodeStructuredParams("puerts_set_property", {
    actor: "PlayerStart",
    property: "Tags",
    value: '["probe_a","probe_b"]',
  });
  assert(Array.isArray(decoded.value) && (decoded.value as string[]).length === 2, "set_property value was not decoded");
  assert(decoded.actor === "PlayerStart", "unrelated parameters were disturbed");
  assert(
    decodeStructuredParams("puerts_find_actors", { name: "[a]" }).name === "[a]",
    "a tool with no structured parameters was rewritten",
  );
  assert(
    decodeStructuredParams("puerts_spawn_actor", { class_path: "/Game/A.A_C", location: '{"x":1,"y":2,"z":3}' })
      .location instanceof Object,
    "spawn_actor location text was not decoded",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-marshal-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-marshal-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as { params?: Record<string, unknown> };
    received.push(request.params ?? {});
    socket.end(JSON.stringify({
      success: true,
      message: "Property changed.",
      data: { property: "RelativeLocation", value: { x: 10, y: 20, z: 112 } },
      changed_assets: [],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "T1",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tools = createPuertsTools(new PuerTSClient());
    const setTool = tools.find((tool) => tool.name === "puerts_set_property");
    assert(setTool !== undefined, "puerts_set_property is missing");

    const structFromText = await setTool.handler({
      object_path: "/Temp/Untitled_1.Untitled_1:PersistentLevel.PlayerStart.CollisionCapsule",
      property: "RelativeLocation",
      value: '{"x":10,"y":20,"z":112}',
    });
    const structPayload = JSON.parse(structFromText.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(structPayload.success === true, "struct write was rejected by the schema");
    const structSent = received[0]?.value as Record<string, number> | undefined;
    assert(structSent !== undefined && structSent.x === 10 && structSent.z === 112, "struct did not reach the pipe as an object");

    await setTool.handler({ actor: "PlayerStart", property: "Tags", value: '["probe_a","probe_b"]' });
    const arraySent = received[1]?.value;
    assert(Array.isArray(arraySent) && arraySent.length === 2, "array did not reach the pipe as an array");

    await setTool.handler({ actor: "PlayerStart", property: "ActorLabel", value: "ProbeRenamed" });
    assert(received[2]?.value === "ProbeRenamed", "a string value did not survive as a string");

    await setTool.handler({
      object_path: "/Temp/Untitled_1.Untitled_1:PersistentLevel.PointLight.LightComponent0",
      property: "Intensity",
      value: 50000,
    });
    assert(received[3]?.value === 50000, "a numeric value did not survive as a number");

    const structFromObject = await setTool.handler({
      object_path: "/Temp/Untitled_1.Untitled_1:PersistentLevel.PlayerStart.CollisionCapsule",
      property: "RelativeLocation",
      value: { x: 10, y: 20, z: 112 },
    });
    assert(
      JSON.parse(structFromObject.content[0]?.text ?? "null").success === true,
      "a real JSON object was rejected by the schema",
    );
    const objectSent = received[4]?.value as Record<string, number> | undefined;
    assert(objectSent !== undefined && objectSent.y === 20, "a real JSON object was mangled on the way to the pipe");

    const missingValue = await setTool.handler({ actor: "PlayerStart", property: "Tags" });
    assert(
      JSON.parse(missingValue.content[0]?.text ?? "null").success === false,
      "set_property accepted a request with no value",
    );
    assert(received.length === 5, "the invalid request still reached the pipe");

    const readTool = tools.find((tool) => tool.name === "puerts_read_property");
    assert(readTool !== undefined, "puerts_read_property is missing");
    const read = await readTool.handler({
      object_path: "/Temp/Untitled_1.Untitled_1:PersistentLevel.PlayerStart.CollisionCapsule",
      property: "RelativeLocation",
    });
    const readPayload = JSON.parse(read.content[0]?.text ?? "null") as { data?: { value?: Record<string, number> } };
    assert(readPayload.data?.value?.z === 112, "a struct read was flattened on the way back");
    console.log("  PASS  struct and array marshaling in both directions");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** puerts_blueprint_build advertises the node types the C++ builder can
    actually spawn. A client that is told "any node type" writes graphs the
    builder silently skips, which produces a Blueprint that compiles clean and
    does nothing. These assertions pin the advertised surface and the
    validation that keeps a bad spec off the pipe. */
async function blueprintBuildSuite(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-bp-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-bp-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      success: true,
      message: "Blueprint created.",
      data: { asset_path: "/Game/MCPGenerated/BP_ProbeDoor", created: true, compile_status: "UpToDate" },
      changed_assets: ["/Game/MCPGenerated/BP_ProbeDoor.BP_ProbeDoor"],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "T2",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_blueprint_build");
    assert(tool !== undefined, "puerts_blueprint_build is missing");

    const built = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      components: [{ class: "StaticMeshComponent", name: "Mesh" }],
      graph: {
        nodes: [
          { id: "start", type: "BeginPlay" },
          { id: "print", type: "PrintString", params: { InString: "probe" } },
        ],
        connections: [{ from: "start.exec", to: "print.exec" }],
      },
    });
    assert(JSON.parse(built.content[0]?.text ?? "null").success === true, "a valid Blueprint spec was rejected");
    const sent = received[0];
    assert(Array.isArray(sent?.components), "components did not reach the pipe as an array");
    assert(
      (sent?.graph as { nodes?: unknown[] } | undefined)?.nodes?.length === 2,
      "graph did not reach the pipe as an object",
    );
    assert(sent?.__tool === "blueprint_build", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "asset authoring did not get its own timeout budget",
    );

    const structuredFromText = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      components: '[{"class":"StaticMeshComponent","name":"Mesh"}]',
      graph: '{"nodes":[{"id":"start","type":"BeginPlay"}]}',
    });
    assert(
      JSON.parse(structuredFromText.content[0]?.text ?? "null").success === true,
      "structured Blueprint parameters sent as JSON text were rejected",
    );
    assert(Array.isArray(received[1]?.components), "components text was not decoded before the pipe");

    const badNodeType = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      graph: { nodes: [{ id: "bogus", type: "TotallyNotANode" }] },
    });
    const badPayload = JSON.parse(badNodeType.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(badPayload.success === false, "an unsupported node type was accepted");
    assert(received.length === 2, "the unsupported node type still reached the editor");

    const badPath = await tool.handler({ asset_path: "/Engine/Transient" });
    assert(
      JSON.parse(badPath.content[0]?.text ?? "null").success === false,
      "a path outside /Game/MCPGenerated was accepted",
    );
    console.log("  PASS  Blueprint build schema, structured parameters, and rejected specs");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

main()
  .then(marshalingSuite)
  .then(blueprintBuildSuite)
  .catch((error: unknown) => {
    console.error(`  FAIL  ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  });
