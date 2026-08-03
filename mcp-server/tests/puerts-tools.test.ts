import { mkdir, mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
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

import {
  advertiseSession as makeSession,
  TEST_SESSION_ID,
  TEST_SESSION_NONCE,
} from "./session-fixture.js";

/** The identity every mock reply stamps, kept in a module-level binding so each
    suite's server closure sees the session that suite advertised. */
let RESPONSE_SESSION: Record<string, unknown> = {};

async function advertiseSession(pipeName: string): Promise<string> {
  const fixture = await makeSession(pipeName);
  RESPONSE_SESSION = fixture.responseSession;
  return fixture.projectRoot;
}

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
  await advertiseSession(pipeName);

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
      session: RESPONSE_SESSION,
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
    assert(tools.length === 30, "expected all 30 PuerTS tools");
    assert(tools.some((tool) => tool.name === "puerts_behavior_tree_build"), "native Behavior Tree builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_behavior_tree_inspect"), "native Behavior Tree inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_sky_shader_create"), "native sky shader tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_blueprint_build"), "native Blueprint builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_widget_build"), "native widget builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_graph_inspect"), "native Blueprint inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_widget_inspect"), "native widget inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_blueprint_member_patch"), "native Blueprint member patch tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_input_mapping_info"), "native input mapping inspector is missing");
    assert(tools.some((tool) => tool.name === "puerts_input_mapping_patch"), "native input mapping patch tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_folder_visibility"), "native folder visibility tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_camera_shake"), "native camera shake tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_pie_agent_query"), "native PIE agent read tool is missing");
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
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as { params?: Record<string, unknown> };
    received.push(request.params ?? {});
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
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
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
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

    // Component template properties. This is the marshaling that gives a
    // generated StaticMeshComponent an actual mesh, so every value shape the
    // native side distinguishes has to survive the trip: an asset path as a
    // string, a list of them as an array, a struct as an object.
    const withProperties = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      components: [{
        class: "StaticMeshComponent",
        name: "DoorMesh",
        properties: {
          StaticMesh: "/Engine/BasicShapes/Cube.Cube",
          OverrideMaterials: ["/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"],
          RelativeScale3D: { x: 2, y: 2, z: 2 },
          bVisible: true,
        },
      }],
    });
    assert(
      JSON.parse(withProperties.content[0]?.text ?? "null").success === true,
      "a component property spec was rejected",
    );
    const sentComponents = received[2]?.components as { properties?: Record<string, unknown> }[] | undefined;
    const sentProperties = sentComponents?.[0]?.properties;
    assert(
      sentProperties?.StaticMesh === "/Engine/BasicShapes/Cube.Cube",
      "an asset reference did not reach the pipe as a path string",
    );
    assert(
      Array.isArray(sentProperties?.OverrideMaterials),
      "a material list did not reach the pipe as an array",
    );
    assert(
      (sentProperties?.RelativeScale3D as { x?: number } | undefined)?.x === 2,
      "a struct property was flattened before the pipe",
    );
    assert(sentProperties?.bVisible === true, "a boolean property did not reach the pipe");

    const propertiesFromText = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      components: '[{"class":"StaticMeshComponent","name":"DoorMesh",'
        + '"properties":{"StaticMesh":"/Engine/BasicShapes/Cube.Cube"}}]',
    });
    assert(
      JSON.parse(propertiesFromText.content[0]?.text ?? "null").success === true,
      "component properties sent as JSON text were rejected",
    );
    const decodedComponents = received[3]?.components as { properties?: Record<string, unknown> }[] | undefined;
    assert(
      decodedComponents?.[0]?.properties?.StaticMesh === "/Engine/BasicShapes/Cube.Cube",
      "nested component properties were not decoded before the pipe",
    );

    const badProperties = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
      components: [{ class: "StaticMeshComponent", name: "DoorMesh", properties: ["StaticMesh"] }],
    });
    assert(
      JSON.parse(badProperties.content[0]?.text ?? "null").success === false,
      "properties as an array was accepted",
    );
    assert(received.length === 4, "a malformed properties value still reached the editor");
    console.log("  PASS  Blueprint build schema, structured parameters, and rejected specs");
    console.log("  PASS  component template properties reach the pipe in their own shapes");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** puerts_widget_build advertises the widget types the C++ registry can
    actually resolve, and the tree is recursive. A client told "any object"
    writes hierarchies the builder rejects at the far end of a pipe round
    trip; these assertions pin the advertised surface, the recursion, and the
    rejections that never leave the client. */
async function widgetBuildSuite(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-widget-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-widget-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Widget Blueprint created.",
      data: { asset_path: "/Game/MCPGenerated/WBP_Probe", created: true, compile_status: "UpToDate" },
      changed_assets: ["/Game/MCPGenerated/WBP_Probe.WBP_Probe"],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "T3",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_widget_build");
    assert(tool !== undefined, "puerts_widget_build is missing");

    const built = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      tree: {
        root: {
          type: "CanvasPanel",
          name: "RootCanvas",
          children: [
            {
              type: "TextBlock",
              name: "Label",
              properties: { text: "MCP", color: { r: 1, g: 1, b: 1, a: 1 } },
              slot: { position: { x: 60, y: 40 }, size: { x: 400, y: 48 }, zOrder: 1 },
            },
            {
              type: "ProgressBar",
              name: "Bar",
              properties: { percent: 0.42 },
              slot: { position: { x: 60, y: 96 }, size: { x: 400, y: 24 } },
            },
          ],
        },
      },
    });
    assert(JSON.parse(built.content[0]?.text ?? "null").success === true, "a valid widget tree was rejected");
    const sent = received[0];
    assert(sent?.__tool === "widget_build", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "widget authoring did not get its own timeout budget",
    );
    const sentRoot = (sent?.tree as { root?: { children?: unknown[] } } | undefined)?.root;
    assert(sentRoot?.children?.length === 2, "the tree did not reach the pipe as a nested object");

    // Three levels deep, so the recursion is exercised past the one the flat
    // shape would also satisfy.
    const nested = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      tree: {
        root: {
          type: "CanvasPanel",
          name: "RootCanvas",
          children: [{
            type: "VerticalBox",
            name: "Column",
            children: [{
              type: "Border",
              name: "Frame",
              children: [{ type: "TextBlock", name: "Deep", properties: { text: "deep" } }],
            }],
          }],
        },
      },
    });
    assert(JSON.parse(nested.content[0]?.text ?? "null").success === true, "a three-level tree was rejected");

    const fromText = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      tree: '{"root":{"type":"CanvasPanel","name":"RootCanvas"}}',
    });
    assert(
      JSON.parse(fromText.content[0]?.text ?? "null").success === true,
      "a widget tree sent as JSON text was rejected",
    );
    assert(
      ((received[2]?.tree as { root?: { name?: string } } | undefined)?.root)?.name === "RootCanvas",
      "the tree text was not decoded before the pipe",
    );

    const badType = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      tree: { root: { type: "NotAWidget", name: "RootCanvas" } },
    });
    assert(
      JSON.parse(badType.content[0]?.text ?? "null").success === false,
      "an unsupported widget type was accepted",
    );
    assert(received.length === 3, "the unsupported widget type still reached the editor");

    const badPath = await tool.handler({
      asset_path: "/Game/UI/WBP_Probe",
      tree: { root: { type: "CanvasPanel", name: "RootCanvas" } },
    });
    assert(
      JSON.parse(badPath.content[0]?.text ?? "null").success === false,
      "a path outside /Game/MCPGenerated was accepted",
    );

    const noTree = await tool.handler({ asset_path: "/Game/MCPGenerated/WBP_Probe" });
    assert(JSON.parse(noTree.content[0]?.text ?? "null").success === false, "a spec with no tree was accepted");
    assert(received.length === 3, "a malformed widget spec still reached the editor");
    console.log("  PASS  widget build schema, recursive tree marshaling, and rejected specs");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** Limitation 20 of docs/CAPABILITY_FINDINGS.md, closed: an unresolvable
    graph connection used to be a log line, and the build still answered
    compile_status UpToDate, errors [], saved true, with connection_count
    reporting the number of connections *requested*. That is the worst failure
    shape available - a caller cannot tell a graph with a hole in it from a
    working one. The native command now counts the links actually made and
    fails the build on a shortfall. These assertions pin the response contract
    that failure arrives in, and the description that advertises it: an MCP
    client only ever sees the envelope, so if the envelope stops carrying the
    shortfall the fix is invisible again. */
async function connectionContractSuite(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-conn-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-conn-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  // The shortfall envelope the native command produces: two of the four
  // requested connections were dropped against a pure node, so the build
  // failed and nothing was saved.
  const unresolved = [
    "brSnd.then -> playing.exec (no input pin 'exec' on playing)",
    "playing.then -> printS.exec (no output pin 'then' on playing)",
  ];
  const server = createServer((socket) => socket.once("data", () => {
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: false,
      message: "Blueprint build reported errors.",
      data: {
        asset_path: "/Game/MCPGenerated/BP_ProbeDoorV3",
        created: false,
        compile_status: "UpToDate",
        saved: false,
        graph: {
          requested: true,
          cleared_existing: true,
          node_count: 31,
          connection_count: 2,
          connections_requested: 4,
          unresolved_connections: unresolved,
          node_types: [],
        },
      },
      changed_assets: [],
      changed_actors: [],
      warnings: ["Save was skipped: the Blueprint did not build cleanly."],
      errors: [`2 of 4 graph connection(s) could not be wired and were dropped: ${unresolved.join(", ")}.`],
      log_output: [],
      transaction_id: "T4",
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
      asset_path: "/Game/MCPGenerated/BP_ProbeDoorV3",
      graph: {
        nodes: [
          { id: "brSnd", type: "Branch" },
          { id: "playing", type: "CallFunction", params: { class: "AudioComponent", function: "IsPlaying" } },
          { id: "printS", type: "PrintString" },
        ],
        connections: [{ from: "brSnd.then", to: "playing.exec" }],
      },
    });
    const payload = JSON.parse(built.content[0]?.text ?? "null") as {
      success?: boolean;
      errors?: string[];
      warnings?: string[];
      data?: { saved?: boolean; compile_status?: string; graph?: Record<string, unknown> };
    };

    assert(payload.success === false, "a dropped connection did not fail the build");
    assert(
      payload.data?.compile_status === "UpToDate",
      "the fixture no longer models the failure that matters: a clean compile with a broken graph",
    );
    assert(payload.data?.saved === false, "a build with dropped connections was still saved");

    const errorText = (payload.errors ?? []).join(" ");
    for (const pair of unresolved) {
      assert(errorText.includes(pair), `errors[] does not name the dropped connection: ${pair}`);
    }

    const graph = payload.data?.graph ?? {};
    assert(graph.connection_count === 2, "connection_count is not the number of links actually made");
    assert(graph.connections_requested === 4, "the requested connection count is not reported");
    assert(
      Array.isArray(graph.unresolved_connections) && graph.unresolved_connections.length === 2,
      "the dropped pairs are not reported in the graph result",
    );
    assert(
      (graph.connections_requested as number) - (graph.connection_count as number)
        === (graph.unresolved_connections as string[]).length,
      "the shortfall and the dropped-pair list disagree",
    );

    assert(
      tool.description.includes("connection_count")
        && tool.description.includes("actually made")
        && tool.description.includes("fails the build"),
      "the tool no longer advertises that an unresolved connection fails the build",
    );
    console.log("  PASS  an unresolved graph connection fails the build and names the dropped pairs");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** A Blueprint no longer has to be an Actor.

    blueprint_build used to refuse any parent that did not derive from AActor,
    which removed the SaveGame subclass, the ActorComponent subclass and every
    data-only Blueprint at once for a reason the engine does not share. The
    client is not the place that gate lives, so what is pinned here is the
    surface a caller reads and the shapes that have to survive the trip: a
    non-Actor parent_class, a target-scoped variable node with its owning
    class, and the AsResult cast pin role, none of which the schema may
    flatten or reject on its own. */
async function nonActorParentSuite(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-parent-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-parent-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as { params?: Record<string, unknown> };
    received.push(request.params ?? {});
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Blueprint created.",
      data: {
        asset_path: "/Game/MCPGenerated/BP_StaminaSave",
        parent_class: "/Script/Engine.SaveGame",
        created: true,
        compile_status: "UpToDate",
        saved: true,
      },
      changed_assets: [],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "T5",
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

    const saveGame = await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_StaminaSave",
      parent_class: "/Script/Engine.SaveGame",
      variables: [{ name: "SavedStamina", type: "float", default: 0 }],
    });
    assert(
      JSON.parse(saveGame.content[0]?.text ?? "null").success === true,
      "a SaveGame parent was rejected by the client",
    );
    assert(
      received[0]?.parent_class === "/Script/Engine.SaveGame",
      "a non-Actor parent_class did not reach the pipe",
    );
    assert(
      (received[0]?.variables as { name?: string }[] | undefined)?.[0]?.name === "SavedStamina",
      "the variable of a data-only Blueprint did not reach the pipe",
    );

    // The two graph shapes a save/load round trip needs: a variable node
    // scoped to another object, and a cast result addressed by role rather
    // than by the display-name-derived pin name a caller cannot compute.
    await tool.handler({
      asset_path: "/Game/MCPGenerated/BP_StaminaCharacter",
      parent_class: "Character",
      graph: {
        nodes: [
          { id: "load", type: "CallFunction", params: { class: "GameplayStatics", function: "LoadGameFromSlot" } },
          { id: "cast", type: "Cast", params: { target_class: "/Game/MCPGenerated/BP_StaminaSave.BP_StaminaSave_C" } },
          {
            id: "read",
            type: "VariableGet",
            params: {
              var_name: "SavedStamina",
              scope: "target",
              target_class: "/Game/MCPGenerated/BP_StaminaSave.BP_StaminaSave_C",
            },
          },
        ],
        connections: [
          { from: "load.ReturnValue", to: "cast.Object" },
          { from: "cast.AsResult", to: "read.self" },
        ],
      },
    });
    const graph = received[1]?.graph as {
      nodes?: { params?: Record<string, unknown> }[];
      connections?: { from?: string; to?: string }[];
    } | undefined;
    assert(
      graph?.nodes?.[2]?.params?.scope === "target",
      "a target-scoped variable node lost its scope before the pipe",
    );
    assert(
      graph?.nodes?.[2]?.params?.target_class === "/Game/MCPGenerated/BP_StaminaSave.BP_StaminaSave_C",
      "a target-scoped variable node lost its owning class before the pipe",
    );
    assert(
      graph?.connections?.[1]?.from === "cast.AsResult",
      "the AsResult cast pin role did not reach the pipe",
    );

    assert(
      tool.description.includes("SaveGame")
        && tool.description.includes("non-Actor parent")
        && tool.description.includes("AsResult")
        && tool.description.includes("\"target\""),
      "the tool no longer advertises non-Actor parents, AsResult, or target-scoped variables",
    );
    console.log("  PASS  a non-Actor parent, target-scoped variables and the AsResult pin role");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** puerts_graph_inspect is the read-only inverse of the builder. The three
    things a read has to promise, and that a client can get wrong long before
    the pipe answers: it is annotated read-only and carries no transaction id,
    it reaches any Blueprint under /Game or /Engine rather than only the
    authoring root, and its optional pin detail is off unless asked for. */
async function graphInspectSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  const inspectAnnotations = toolAnnotations.puerts_graph_inspect;
  assert(inspectAnnotations !== undefined, "puerts_graph_inspect has no annotation");
  assert(inspectAnnotations.readOnlyHint === true, "the inspector is not annotated read-only");
  assert(inspectAnnotations.destructiveHint === false, "the inspector is annotated destructive");

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-inspect-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-inspect-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Blueprint inspected.",
      data: {
        asset_path: "/Game/MCPGenerated/BP_ProbeDoor",
        package_dirty_before: false,
        package_dirty_after: false,
        graph: { name: "EventGraph", node_count: 3, connection_count: 2 },
      },
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
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_graph_inspect");
    assert(tool !== undefined, "puerts_graph_inspect is missing");

    const read = await tool.handler({ asset_path: "/Game/MCPGenerated/BP_ProbeDoor" });
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a valid inspection request was rejected");
    // Read-only is not a comment: a response that carried a transaction id
    // would mean the native side had opened one.
    assert(payload.transaction_id === "", "a read-only inspection returned a transaction id");
    assert(
      Array.isArray(payload.changed_assets) && (payload.changed_assets as unknown[]).length === 0,
      "a read reported a changed asset",
    );
    const sent = received[0];
    assert(sent?.__tool === "graph_inspect", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "inspection did not get its own timeout budget",
    );

    // Reading is allowed outside the authoring root. Authoring is limited to
    // /Game/MCPGenerated/; refusing to read anywhere else would make the
    // inspector useless on a Blueprint somebody else wrote.
    const engineRead = await tool.handler({ asset_path: "/Engine/Some/BP_Thing", graph_name: "EventGraph" });
    assert(
      JSON.parse(engineRead.content[0]?.text ?? "null").success === true,
      "the inspector refused an /Engine path",
    );
    assert(received[1]?.graph_name === "EventGraph", "graph_name did not reach the pipe");

    // Pin detail is opt-in, and the schema is strict about everything else.
    const withPins = await tool.handler({ asset_path: "/Game/X/BP_Y", include_pins: true });
    assert(JSON.parse(withPins.content[0]?.text ?? "null").success === true, "include_pins was rejected");
    assert(received[2]?.include_pins === true, "include_pins did not reach the pipe");

    const missingPath = await tool.handler({ graph_name: "EventGraph" });
    assert(
      JSON.parse(missingPath.content[0]?.text ?? "null").success === false,
      "an inspection with no asset_path was accepted",
    );
    const unknownKey = await tool.handler({ asset_path: "/Game/X/BP_Y", clear_existing_graph: true });
    assert(
      JSON.parse(unknownKey.content[0]?.text ?? "null").success === false,
      "the inspector accepted an authoring key, so a mutating spec could be sent to a read",
    );
    assert(received.length === 3, "a rejected request still reached the pipe");
    console.log("  PASS  read-only Blueprint inspection contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** Zero-config discovery: with only MCP_UNREAL_PROJECT_ROOT set, the client
    finds both the token and the pipe name the editor advertised under
    Saved/MCPPuerTSBridge/. This is the contract that lets .mcp.json carry no
    machine-specific pipe or token path. */
async function discoverySuite(): Promise<void> {
  const projectRoot = await mkdtemp(join(tmpdir(), "ue4-puerts-discovery-"));
  const bridgeDir = join(projectRoot, "Saved", "MCPPuerTSBridge");
  await mkdir(bridgeDir, { recursive: true });
  const pipeName = `\\\\.\\pipe\\ue4-puerts-discovery-${process.pid}-${Date.now()}`;
  await writeFile(join(bridgeDir, "token.txt"), "discovered-token", "utf8");
  // pipe.txt is still written by the editor and is deliberately NOT sufficient
  // any more: it names a pipe without saying which editor owns it. It is seeded
  // here with the WRONG name on purpose, so this suite fails loudly if the client
  // ever falls back to it instead of resolving the session manifest.
  await writeFile(join(bridgeDir, "pipe.txt"), "\\\\.\\pipe\\wrong-editor\n", "utf8");
  const session = {
    schema_version: 1,
    session_id: TEST_SESSION_ID,
    session_nonce: TEST_SESSION_NONCE,
    editor_pid: process.pid,
    process_start_time: "2026-08-02T00:00:00.000Z",
    project_path: projectRoot,
    uproject_path: join(projectRoot, "Discovery.uproject"),
    pipe_name: pipeName,
    bridge_commit: "0000000",
    installed_manifest_hash: "0".repeat(40),
    created_at: "2026-08-02T00:00:00.000Z",
    last_heartbeat_at: new Date().toISOString(),
    shutdown_state: "running",
  };
  await writeFile(join(bridgeDir, "session.json"), JSON.stringify(session), "utf8");
  RESPONSE_SESSION = {
    session_id: TEST_SESSION_ID,
    editor_pid: process.pid,
    process_start_time: session.process_start_time,
    project_path: projectRoot,
    uproject_path: session.uproject_path,
    pipe_name: pipeName,
  };
  delete process.env.MCP_PUERTS_PIPE;
  delete process.env.MCP_PUERTS_TOKEN_PATH;
  delete process.env.MCP_PUERTS_TOKEN;
  process.env.MCP_UNREAL_PROJECT_ROOT = projectRoot;

  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as
      { auth?: string; session_nonce?: string; expect_session_id?: string };
    assert(request.auth === "discovered-token", "the token was not discovered from the project root");
    assert(request.session_nonce === TEST_SESSION_NONCE, "the session nonce was not sent with the request");
    assert(request.expect_session_id === TEST_SESSION_ID, "the addressed session id was not sent with the request");
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
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
    const response = await new PuerTSClient().call("find_actors", {});
    assert(response.success, "a call with discovery-resolved pipe and token failed");
    console.log("  PASS  session, pipe and token discovery from MCP_UNREAL_PROJECT_ROOT alone");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(projectRoot, { recursive: true, force: true });
    delete process.env.MCP_UNREAL_PROJECT_ROOT;
  }
}

/** puerts_behavior_tree_build sends its structured spec through the pipe and
    rejects what the native side would reject, before the pipe is touched. */
async function behaviorTreeBuildSuite(): Promise<void> {
  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-bt-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-bt-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Behavior Tree created.",
      data: {
        asset_path: "/Game/MCPGenerated/BT_Probe",
        blackboard_path: "/Game/MCPGenerated/BT_Probe_BB",
        created: true,
        has_root: true,
        saved: true,
      },
      changed_assets: ["/Game/MCPGenerated/BT_Probe.BT_Probe"],
      changed_actors: [],
      warnings: [],
      errors: [],
      log_output: [],
      transaction_id: "T7",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_behavior_tree_build");
    assert(tool !== undefined, "puerts_behavior_tree_build is missing");

    const built = await tool.handler({
      asset_path: "/Game/MCPGenerated/BT_Probe",
      keys: [{ name: "TargetActor", type: "Object", base_class: "/Script/Engine.Actor" }],
      root: {
        id: "root", type: "Selector", children: [
          { id: "wait", type: "Wait", params: { wait_time: "2.0" } },
        ],
      },
    });
    assert(JSON.parse(built.content[0]?.text ?? "null").success === true, "a valid Behavior Tree spec was rejected");
    const sent = received[0];
    assert(sent?.__tool === "behavior_tree_build", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "asset authoring did not get its own timeout budget",
    );
    const sentRoot = sent?.root as { type?: string; children?: unknown[] } | undefined;
    assert(sentRoot?.type === "Selector" && sentRoot?.children?.length === 1,
      "root did not reach the pipe as a structure");
    assert(Array.isArray(sent?.keys) && (sent.keys as unknown[]).length === 1,
      "keys did not reach the pipe as an array");

    const missingRoot = await tool.handler({ asset_path: "/Game/MCPGenerated/BT_Probe" });
    assert(
      JSON.parse(missingRoot.content[0]?.text ?? "null").success === false,
      "a spec without root was accepted",
    );
    const badPath = await tool.handler({ asset_path: "/Game/Elsewhere/BT_X", root: { id: "r", type: "Selector" } });
    assert(
      JSON.parse(badPath.content[0]?.text ?? "null").success === false,
      "a path outside /Game/MCPGenerated was accepted",
    );
    assert(received.length === 1, "a rejected request still reached the pipe");
    console.log("  PASS  Behavior Tree build schema and structured spec marshaling");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** puerts_behavior_tree_inspect is the read half of the BT builder. Same
    contract promises as graph_inspect: annotated read-only, no transaction id,
    reads anywhere under /Game and /Engine, and a strict schema that rejects
    authoring keys so a mutating spec cannot be sent to a read. */
async function behaviorTreeInspectSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  const inspectAnnotations = toolAnnotations.puerts_behavior_tree_inspect;
  assert(inspectAnnotations !== undefined, "puerts_behavior_tree_inspect has no annotation");
  assert(inspectAnnotations.readOnlyHint === true, "the BT inspector is not annotated read-only");

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-bt-inspect-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-bt-inspect-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      params?: Record<string, unknown>;
      tool?: string;
      timeout_ms?: number;
    };
    received.push({ ...(request.params ?? {}), __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Behavior Tree inspected.",
      data: {
        asset_path: "/Game/MCPGenerated/BT_Patrol",
        blackboard_path: "/Game/MCPGenerated/BT_Patrol_BB.BT_Patrol_BB",
        package_dirty_before: false,
        package_dirty_after: false,
        identity_kind: "derived",
        structure_hash_sha1: "ABC123",
        root: { id: "root", kind: "composite", children: [] },
      },
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
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_behavior_tree_inspect");
    assert(tool !== undefined, "puerts_behavior_tree_inspect is missing");

    const read = await tool.handler({ asset_path: "/Game/MCPGenerated/BT_Patrol" });
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a valid inspection request was rejected");
    assert(payload.transaction_id === "", "a read-only BT inspection returned a transaction id");
    assert(
      Array.isArray(payload.changed_assets) && (payload.changed_assets as unknown[]).length === 0,
      "a BT read reported a changed asset",
    );
    const sent = received[0];
    assert(sent?.__tool === "behavior_tree_inspect", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "BT inspection did not get its own timeout budget",
    );

    const engineRead = await tool.handler({ asset_path: "/Engine/Some/BT_Thing" });
    assert(
      JSON.parse(engineRead.content[0]?.text ?? "null").success === true,
      "the BT inspector refused an /Engine path",
    );

    const missingPath = await tool.handler({});
    assert(
      JSON.parse(missingPath.content[0]?.text ?? "null").success === false,
      "an inspection with no asset_path was accepted",
    );
    const authoringKey = await tool.handler({ asset_path: "/Game/X/BT_Y", root: { id: "r" } });
    assert(
      JSON.parse(authoringKey.content[0]?.text ?? "null").success === false,
      "the BT inspector accepted an authoring key",
    );
    assert(received.length === 2, "a rejected request still reached the pipe");
    console.log("  PASS  read-only Behavior Tree inspection contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** puerts_widget_inspect is the read half of the widget builder. Same contract
    promises as graph_inspect and behavior_tree_inspect: annotated read-only, no
    transaction id, reads anywhere under /Game and /Engine, its own timeout
    budget, and a strict schema that rejects authoring keys so a mutating spec
    cannot be sent to a read. */
async function widgetInspectSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  const inspectAnnotations = toolAnnotations.puerts_widget_inspect;
  assert(inspectAnnotations !== undefined, "puerts_widget_inspect has no annotation");
  assert(inspectAnnotations.readOnlyHint === true, "the widget inspector is not annotated read-only");

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-wbp-inspect-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-wbp-inspect-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      tool?: string; params?: Record<string, unknown>; timeout_ms?: number;
    };
    received.push({ ...request.params, __tool: request.tool, __timeout: request.timeout_ms });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Widget Blueprint inspected.",
      data: {
        asset_path: "/Game/MCPGenerated/WBP_Probe",
        identity_kind: "derived",
        package_dirty_before: false,
        package_dirty_after: false,
        widget_count: 3,
        structure_hash_sha1: "0".repeat(40),
      },
      changed_assets: [], changed_actors: [], warnings: [], errors: [],
      log_output: [], transaction_id: "",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_widget_inspect");
    assert(tool !== undefined, "puerts_widget_inspect is missing");

    const read = await tool.handler({ asset_path: "/Game/MCPGenerated/WBP_Probe" });
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a valid widget inspection request was rejected");
    assert(payload.transaction_id === "", "a read-only widget inspection returned a transaction id");
    assert(
      Array.isArray(payload.changed_assets) && (payload.changed_assets as unknown[]).length === 0,
      "a widget read reported a changed asset",
    );
    const sent = received[0];
    assert(sent?.__tool === "widget_inspect", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "widget inspection did not get its own timeout budget",
    );

    const engineRead = await tool.handler({ asset_path: "/Engine/Some/WBP_Thing" });
    assert(
      JSON.parse(engineRead.content[0]?.text ?? "null").success === true,
      "the widget inspector refused an /Engine path",
    );

    const missingPath = await tool.handler({});
    assert(
      JSON.parse(missingPath.content[0]?.text ?? "null").success === false,
      "a widget inspection with no asset_path was accepted",
    );
    const authoringKey = await tool.handler({ asset_path: "/Game/X/WBP_Y", tree: { root: {} } });
    assert(
      JSON.parse(authoringKey.content[0]?.text ?? "null").success === false,
      "the widget inspector accepted an authoring key",
    );
    assert(received.length === 2, "a rejected widget request still reached the pipe");
    console.log("  PASS  read-only Widget Blueprint inspection contract");
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
  .then(behaviorTreeBuildSuite)
  .then(behaviorTreeInspectSuite)
  .then(widgetBuildSuite)
  .then(widgetInspectSuite)
  .then(connectionContractSuite)
  .then(nonActorParentSuite)
  .then(graphInspectSuite)
  .then(discoverySuite)
  .catch((error: unknown) => {
    console.error(`  FAIL  ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  });
