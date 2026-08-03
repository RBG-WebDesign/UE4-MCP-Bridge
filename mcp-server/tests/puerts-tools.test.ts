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
    const tools = createPuertsTools(client);    assert(tools.length === 55, "expected all 55 PuerTS tools");
    assert(tools.some((tool) => tool.name === "puerts_behavior_tree_build"), "native Behavior Tree builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_behavior_tree_inspect"), "native Behavior Tree inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_sky_shader_create"), "native sky shader tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_blueprint_build"), "native Blueprint builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_widget_build"), "native widget builder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_graph_inspect"), "native Blueprint inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_widget_inspect"), "native widget inspector tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_widget_bind"), "native widget binder tool is missing");
    assert(tools.some((tool) => tool.name === "puerts_blueprint_member_patch"), "native Blueprint member patch tool is missing");
    for (const name of [
      "puerts_anim_blueprint_build",
      "puerts_anim_blueprint_inspect",
      "puerts_anim_montage_inspect",
      "puerts_anim_blend_space_inspect",
      "puerts_blackboard_build",
      "puerts_blackboard_inspect",
      "puerts_eqs_inspect",
      "puerts_nav_inspect",
      "puerts_nav_query",
      "puerts_ai_perception_build",
      "puerts_ai_controller_inspect",
    ]) {
      assert(tools.some((tool) => tool.name === name), `${name} is missing`);
    }
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
/** The material pair. puerts_material_inspect is the read half, with the same
    contract promises as the other three inspectors: annotated read-only, no
    transaction id, reads anywhere under /Game and /Engine, its own timeout
    budget, strict schema. puerts_material_instance_build is the only material
    WRITE the bridge has, and the test pins the two things that make it safe to
    ship: it is annotated mutating-but-not-destructive, and its schema refuses
    an asset_path outside /Game/MCPGenerated/ before anything reaches the pipe. */
async function materialSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  const inspectAnnotations = toolAnnotations.puerts_material_inspect;
  assert(inspectAnnotations !== undefined, "puerts_material_inspect has no annotation");
  assert(inspectAnnotations.readOnlyHint === true, "the material inspector is not annotated read-only");
  const buildAnnotations = toolAnnotations.puerts_material_instance_build;
  assert(buildAnnotations !== undefined, "puerts_material_instance_build has no annotation");
  assert(buildAnnotations.readOnlyHint === false, "the material instance builder is annotated read-only");
  assert(
    buildAnnotations.destructiveHint === false,
    "the material instance builder is annotated destructive; it writes only the parameters it was given",
  );
  assert(
    buildAnnotations.idempotentHint === true,
    "the material instance builder is not annotated idempotent; a rerun of the same spec writes nothing",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-material-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-material-${process.pid}-${Date.now()}`;
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
    const isBuild = request.tool === "material_instance_build";
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: isBuild ? "Material instance updated." : "Material inspected.",
      data: isBuild
        ? {
          asset_path: "/Game/MCPGenerated/MI_Probe",
          object_path: "/Game/MCPGenerated/MI_Probe.MI_Probe",
          created: false,
          unchanged: false,
          write_count: 1,
          compile: { resource_found: true, instance_own_resource: true, finished: true, succeeded: true, errors: [] },
          structure_hash_sha1: "0".repeat(40),
        }
        : {
          asset_path: "/Game/MCPGenerated/M_Probe",
          asset_kind: "material",
          identity_kind: "observed",
          package_dirty_before: false,
          package_dirty_after: false,
          parameter_count: 2,
          structure_hash_sha1: "0".repeat(40),
        },
      changed_assets: [], changed_actors: [], warnings: [], errors: [],
      log_output: [], transaction_id: isBuild ? "tx-material" : "",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tools = createPuertsTools(new PuerTSClient());
    const inspect = tools.find((entry) => entry.name === "puerts_material_inspect");
    assert(inspect !== undefined, "puerts_material_inspect is missing");

    const read = await inspect.handler({ asset_path: "/Game/MCPGenerated/M_Probe" });
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a valid material inspection request was rejected");
    assert(payload.transaction_id === "", "a read-only material inspection returned a transaction id");
    assert(
      Array.isArray(payload.changed_assets) && (payload.changed_assets as unknown[]).length === 0,
      "a material read reported a changed asset",
    );
    assert(received[0]?.__tool === "material_inspect", "the runtime command name is wrong");
    assert(
      typeof received[0]?.__timeout === "number" && (received[0].__timeout as number) > 5000,
      "material inspection did not get its own timeout budget",
    );

    const engineRead = await inspect.handler({ asset_path: "/Engine/BasicShapes/BasicShapeMaterial" });
    assert(
      JSON.parse(engineRead.content[0]?.text ?? "null").success === true,
      "the material inspector refused an /Engine path",
    );
    const authoringKey = await inspect.handler({ asset_path: "/Game/X/M_Y", scalars: { Roughness: 1 } });
    assert(
      JSON.parse(authoringKey.content[0]?.text ?? "null").success === false,
      "the material inspector accepted an authoring key",
    );

    const build = tools.find((entry) => entry.name === "puerts_material_instance_build");
    assert(build !== undefined, "puerts_material_instance_build is missing");
    const outside = await build.handler({ asset_path: "/Game/Elsewhere/MI_Probe", parent_path: "/Game/M_P" });
    assert(
      JSON.parse(outside.content[0]?.text ?? "null").success === false,
      "the material instance builder accepted a path outside /Game/MCPGenerated/",
    );
    const written = await build.handler({
      asset_path: "/Game/MCPGenerated/MI_Probe",
      parent_path: "/Game/MCPGenerated/M_Probe",
      scalars: { Roughness: 0.4 },
      vectors: { BaseTint: { r: 1, g: 0, b: 0 } },
      switches: { UseDetail: true },
    });
    const writtenPayload = JSON.parse(written.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(writtenPayload.success === true, "a valid material instance build was rejected");
    const sent = received[received.length - 1];
    assert(sent?.__tool === "material_instance_build", "the build runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 15000,
      "the material instance build did not get a bigger budget than a read; it blocks on a compile",
    );
    // Two reads and one write reached the pipe; the two schema rejections did not.
    assert(received.length === 3, "a rejected material request still reached the pipe");
    console.log("  PASS  material inspect and material instance build contract");

    // puerts_material_build is the master material graph write that used to be
    // a documented gap. What is pinned here is what a client can see: the
    // annotation says it replaces a graph, the /Game/MCPGenerated/ limit is
    // enforced before the pipe, the node-and-connection arrays survive the
    // JSON-text encoding an MCP client applies to structured arguments, and it
    // carries a compile-sized timeout rather than the 7 second default.
    const graphAnnotations = toolAnnotations.puerts_material_build;
    assert(graphAnnotations !== undefined, "puerts_material_build has no annotation");
    assert(graphAnnotations.readOnlyHint === false, "the material graph builder is annotated read-only");
    assert(
      graphAnnotations.destructiveHint === true,
      "the material graph builder is not annotated destructive; the spec is the whole graph and "
      + "a build aimed at an existing material replaces it",
    );
    assert(
      graphAnnotations.idempotentHint === true,
      "the material graph builder is not annotated idempotent; a rerun of the same spec converges",
    );

    const graph = tools.find((entry) => entry.name === "puerts_material_build");
    assert(graph !== undefined, "puerts_material_build is missing");
    const graphOutside = await graph.handler({ asset_path: "/Game/Elsewhere/M_Probe" });
    assert(
      JSON.parse(graphOutside.content[0]?.text ?? "null").success === false,
      "the material graph builder accepted a path outside /Game/MCPGenerated/",
    );
    const graphBuilt = await graph.handler({
      asset_path: "/Game/MCPGenerated/M_Probe",
      shading_model: "Unlit",
      parameters: [{ kind: "vector", name: "BeaconColor", default: { r: 1, g: 0.62, b: 0.16 } }],
      expressions: [{ id: "Glow", type: "Multiply" }],
      connections: [{ from: "BeaconColor", to: "Glow.A" }],
      outputs: { EmissiveColor: "Glow" },
    });
    assert(
      JSON.parse(graphBuilt.content[0]?.text ?? "null").success === true,
      "a valid material graph build was rejected",
    );
    const graphSent = received[received.length - 1];
    assert(graphSent?.__tool === "material_build", "the material build runtime command name is wrong");
    assert(
      typeof graphSent?.__timeout === "number" && (graphSent.__timeout as number) > 30000,
      "the material build did not get a compile-sized timeout budget",
    );
    for (const key of ["parameters", "expressions", "connections", "outputs"]) {
      const value = graphSent?.[key];
      const decoded = typeof value === "string" ? JSON.parse(value) : value;
      assert(
        decoded !== undefined && typeof decoded === "object",
        `${key} did not survive the MCP argument encoding as a structure`,
      );
    }
    assert(
      JSON.parse(
        (await graph.handler({ asset_path: "/Game/MCPGenerated/M_Probe", domain: "Hologram" }))
          .content[0]?.text ?? "null",
      ).success === false,
      "the material graph builder accepted a domain that does not exist in 4.27",
    );

    // puerts_texture_import is the other half: nothing else in the catalog can
    // produce a texture for a texture parameter. It generates rather than
    // imports, so the test pins that source_file is carried to the native
    // refusal rather than dropped at the client, where the caller would get an
    // unexplained schema error instead of the reason.
    const textureAnnotations = toolAnnotations.puerts_texture_import;
    assert(textureAnnotations !== undefined, "puerts_texture_import has no annotation");
    assert(textureAnnotations.readOnlyHint === false, "the texture builder is annotated read-only");
    assert(
      textureAnnotations.destructiveHint === false,
      "the texture builder is annotated destructive; it writes one asset and touches nothing else",
    );
    const texture = tools.find((entry) => entry.name === "puerts_texture_import");
    assert(texture !== undefined, "puerts_texture_import is missing");
    assert(
      JSON.parse(
        (await texture.handler({ asset_path: "/Game/Textures/T_Probe" })).content[0]?.text ?? "null",
      ).success === false,
      "the texture builder accepted a path outside /Game/MCPGenerated/",
    );
    const textureBuilt = await texture.handler({
      asset_path: "/Game/MCPGenerated/T_Probe",
      pattern: "checker",
      width: 128,
      color: { r: 1, g: 1, b: 1 },
      source_file: "C:/somewhere/grid.png",
    });
    assert(
      JSON.parse(textureBuilt.content[0]?.text ?? "null").success === true,
      "a valid texture request was rejected at the client",
    );
    const textureSent = received[received.length - 1];
    assert(textureSent?.__tool === "texture_import", "the texture runtime command name is wrong");
    assert(
      textureSent?.source_file === "C:/somewhere/grid.png",
      "source_file was dropped at the client, so the native refusal could never explain itself",
    );
    console.log("  PASS  material graph build and texture generation contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** The level and scene pair. What is checked here is the contract the two tools
    make to each other and to a client, because everything below the schema is
    C++ that no unit test can reach: scene_inspect is annotated read-only and
    returns no transaction id, scene_batch is destructive-idempotent and carries
    a batch-sized timeout, an empty operations array is refused at the client
    rather than sent, and operations survives the JSON-text encoding an MCP
    client applies to structured parameters. That last one is the failure this
    suite exists for: a batch that arrives as a string reaches Unreal as a
    string and dies in the validator with no useful message. */
async function sceneSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  assert(
    toolAnnotations.puerts_scene_inspect?.readOnlyHint === true,
    "the scene inspector is not annotated read-only",
  );
  assert(
    toolAnnotations.puerts_scene_batch?.readOnlyHint === false
      && toolAnnotations.puerts_scene_batch?.idempotentHint === true,
    "scene_batch must be mutating and idempotent: it is desired-state, so a rerun changes nothing",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-scene-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-scene-${process.pid}-${Date.now()}`;
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
      message: "Level inspected.",
      data: {
        level_path: "/Game/Maps/Probe",
        actor_count: 2,
        identity_kind: "observed",
        package_dirty_before: false,
        package_dirty_after: false,
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
    const tools = createPuertsTools(new PuerTSClient());
    const inspect = tools.find((entry) => entry.name === "puerts_scene_inspect");
    const batch = tools.find((entry) => entry.name === "puerts_scene_batch");
    assert(inspect !== undefined, "puerts_scene_inspect is missing");
    assert(batch !== undefined, "puerts_scene_batch is missing");

    const read = await inspect.handler({});
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a scene inspection with no arguments was rejected");
    assert(payload.transaction_id === "", "a read-only scene inspection returned a transaction id");
    assert(received[0]?.__tool === "scene_inspect", "the runtime command name is wrong");

    const empty = await batch.handler({ operations: [] });
    assert(
      JSON.parse(empty.content[0]?.text ?? "null").success === false,
      "a scene batch with no operations was accepted",
    );
    const unknownKey = await batch.handler({ operations: [{ op: "upsert_actor" }], save: true });
    assert(
      JSON.parse(unknownKey.content[0]?.text ?? "null").success === false,
      "scene_batch accepted a save key; it never saves, and silently ignoring one would promise a write",
    );
    assert(received.length === 1, "a rejected scene batch still reached the pipe");

    // An MCP client with no type information sends an array as JSON text.
    // decodeStructuredParams has to turn it back into an array before it
    // reaches Unreal, or the native validator sees a string.
    await batch.handler({
      operations: JSON.stringify([{ op: "delete_actor", select: { name: "Probe_0" } }]),
    });
    const sentBatch = received[1];
    assert(sentBatch?.__tool === "scene_batch", "the batch runtime command name is wrong");
    assert(
      Array.isArray(sentBatch?.operations),
      "operations arrived as text rather than an array; the structured-parameter decode is missing",
    );
    // The editor's budget is the client's minus a 2 second margin, so a 60s
    // entry in commandTimeouts reaches the pipe as 58000. Asserting 60000 here
    // asserts against a number that cannot appear on the wire; what matters is
    // that it is a batch budget and not the 7 second default, which arrives as
    // 5000.
    assert(
      typeof sentBatch?.__timeout === "number" && (sentBatch.__timeout as number) >= 50000,
      "scene_batch did not get a batch-sized timeout budget",
    );
    console.log("  PASS  scene inspect and batch contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** Level completion: the restored legacy parameters, the lighting build and the
    class-default patch.

    The first half is a REGRESSION test and is the reason this suite exists. The
    legacy actor_spawn took name, folder and scale and the legacy level_actors
    took folder_filter and include_transforms; none of the five survived the
    native migration, and nothing failed when they went, because a dropped
    parameter is only visible to a test that names it. These assertions fail if
    any of them is dropped again.

    The second half is contract only, as everywhere else in this file:
    everything below the schema is C++ no unit test can reach. What is checkable
    here is that lighting_build carries no wait parameter to promise something it
    does not do, and that class_defaults_patch's properties object survives the
    JSON-text encoding an MCP client applies to structured parameters. */
async function levelCompletionSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  assert(
    toolAnnotations.puerts_lighting_build?.readOnlyHint === false
      && toolAnnotations.puerts_lighting_build?.idempotentHint === true,
    "lighting_build must be mutating and idempotent",
  );
  assert(
    toolAnnotations.puerts_class_defaults_patch?.readOnlyHint === false
      && toolAnnotations.puerts_class_defaults_patch?.destructiveHint === false,
    "class_defaults_patch must be mutating and not destructive: it clears nothing",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-level-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-level-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      tool?: string; params?: Record<string, unknown>;
    };
    received.push({ ...request.params, __tool: request.tool });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "ok",
      data: { started: true, waited: false },
      changed_assets: [], changed_actors: [], warnings: [], errors: [],
      log_output: [], transaction_id: "",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tools = createPuertsTools(new PuerTSClient());
    const named = (name: string) => {
      const tool = tools.find((entry) => entry.name === name);
      assert(tool !== undefined, `${name} is missing`);
      return tool;
    };

    // --- the restored legacy parameters reach the runtime ------------------
    await named("puerts_spawn_actor").handler({
      class_path: "/Script/Engine.StaticMeshActor",
      name: "Pillar_NE",
      folder: "Courtyard/Structure",
      scale: { x: 1, y: 1, z: 4 },
    });
    const spawn = received[0];
    assert(spawn?.__tool === "spawn_actor", "the spawn runtime command name is wrong");
    assert(spawn?.name === "Pillar_NE", "spawn_actor dropped the legacy name parameter");
    assert(spawn?.folder === "Courtyard/Structure", "spawn_actor dropped the legacy folder parameter");
    assert(
      JSON.stringify(spawn?.scale) === JSON.stringify({ x: 1, y: 1, z: 4 }),
      "spawn_actor dropped the legacy scale parameter, or sent it as text",
    );

    await named("puerts_find_actors").handler({ folder_filter: "Courtyard", include_transforms: true });
    const find = received[1];
    assert(find?.__tool === "find_actors", "the find runtime command name is wrong");
    assert(find?.folder_filter === "Courtyard", "find_actors dropped the legacy folder_filter parameter");
    assert(find?.include_transforms === true, "find_actors dropped the legacy include_transforms parameter");

    // --- lighting_build promises nothing it does not do --------------------
    const waited = await named("puerts_lighting_build").handler({ wait_for_completion: true });
    assert(
      JSON.parse(waited.content[0]?.text ?? "null").success === false,
      "lighting_build accepted wait_for_completion; it never waits, and accepting the key would "
      + "promise a completed build",
    );
    await named("puerts_lighting_build").handler({ action: "status" });
    assert(received[2]?.__tool === "lighting_build", "the lighting runtime command name is wrong");
    assert(received[2]?.action === "status", "the lighting action did not reach the runtime");

    // --- class_defaults_patch: path limit and structured properties --------
    const outsidePath = await named("puerts_class_defaults_patch").handler({
      asset_path: "/Game/Elsewhere/BP_Guard",
      properties: { AutoPossessAI: "PlacedInWorldOrSpawned" },
    });
    assert(
      JSON.parse(outsidePath.content[0]?.text ?? "null").success === false,
      "class_defaults_patch accepted a path outside /Game/MCPGenerated/",
    );
    assert(received.length === 3, "a rejected class-defaults patch still reached the pipe");

    await named("puerts_class_defaults_patch").handler({
      asset_path: "/Game/MCPGenerated/BP_SliceGuardPawn",
      properties: JSON.stringify({ AIControllerClass: "/Game/MCPGenerated/BP_A.BP_A_C" }),
    });
    const patch = received[3];
    assert(patch?.__tool === "class_defaults_patch", "the class-defaults runtime command name is wrong");
    assert(
      typeof patch?.properties === "object" && patch?.properties !== null,
      "properties arrived as text rather than an object; the structured-parameter decode is missing",
    );
    console.log("  PASS  restored legacy parameters, lighting build and class defaults contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** The Sequencer pair. Everything below the schema is C++ no unit test can
    reach, so what is checked here is the contract the two tools make: the
    inspector is annotated read-only and returns no transaction id, the builder
    is mutating-and-idempotent and NOT destructive (it removes nothing), the
    authoring root is enforced at the client rather than at the editor, and
    bindings and tracks survive the JSON-text encoding an MCP client applies to
    structured parameters. That last one is the failure this suite exists for: a
    track list that arrives as a string reaches Unreal as a string and dies in
    the validator with no useful message. */
async function sequenceSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  assert(
    toolAnnotations.puerts_sequence_inspect?.readOnlyHint === true,
    "the sequence inspector is not annotated read-only",
  );
  assert(
    toolAnnotations.puerts_sequence_build?.readOnlyHint === false
      && toolAnnotations.puerts_sequence_build?.idempotentHint === true
      && toolAnnotations.puerts_sequence_build?.destructiveHint === false,
    "sequence_build must be mutating, idempotent and non-destructive: it is desired-state and "
    + "additive, so a rerun changes nothing and nothing is ever removed",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-sequence-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-sequence-${process.pid}-${Date.now()}`;
  await writeFile(tokenPath, "test-token", "utf8");
  process.env.MCP_PUERTS_TOKEN_PATH = tokenPath;
  process.env.MCP_PUERTS_PIPE = pipeName;
  await advertiseSession(pipeName);

  const received: Record<string, unknown>[] = [];
  const server = createServer((socket) => socket.once("data", (data: Buffer) => {
    const request = JSON.parse(data.toString("utf8")) as {
      tool?: string; params?: Record<string, unknown>;
    };
    received.push({ ...request.params, __tool: request.tool });
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: true,
      message: "Level Sequence inspected.",
      data: {
        asset_path: "/Game/MCPGenerated/LS_Probe",
        identity_kind: "observed",
        package_dirty_before: false,
        package_dirty_after: false,
        binding_count: 2,
        track_count: 3,
        key_count: 5,
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
    const tools = createPuertsTools(new PuerTSClient());
    const inspect = tools.find((entry) => entry.name === "puerts_sequence_inspect");
    const build = tools.find((entry) => entry.name === "puerts_sequence_build");
    assert(inspect !== undefined, "puerts_sequence_inspect is missing");
    assert(build !== undefined, "puerts_sequence_build is missing");

    const read = await inspect.handler({ asset_path: "/Game/MCPGenerated/LS_Probe" });
    const payload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(payload.success === true, "a sequence inspection was rejected");
    assert(payload.transaction_id === "", "a read-only sequence inspection returned a transaction id");
    assert(received[0]?.__tool === "sequence_inspect", "the runtime command name is wrong");

    const unknownKey = await build.handler({
      asset_path: "/Game/MCPGenerated/LS_Probe",
      remove_unlisted: true,
    });
    assert(
      JSON.parse(unknownKey.content[0]?.text ?? "null").success === false,
      "sequence_build accepted remove_unlisted; it removes nothing, and silently ignoring the key "
      + "would promise a prune it never performs",
    );
    const badTrackType = await build.handler({
      asset_path: "/Game/MCPGenerated/LS_Probe",
      tracks: [{ binding: "master", type: "Audio", sections: [] }],
    });
    assert(
      JSON.parse(badTrackType.content[0]?.text ?? "null").success === false,
      "sequence_build accepted an Audio track; nothing in this bridge can produce a USoundBase",
    );
    assert(received.length === 1, "a rejected sequence build still reached the pipe");

    // An MCP client with no type information sends arrays and objects as JSON
    // text. decodeStructuredParams has to turn them back before they reach
    // Unreal, or the native validator sees a string where a spec should be.
    await build.handler({
      asset_path: "/Game/MCPGenerated/LS_Probe",
      frame_rate: 30,
      playback_range: JSON.stringify({ start_frame: 0, end_frame: 90 }),
      bindings: JSON.stringify([
        { id: "cam", kind: "spawnable", name: "IntroCamera", actor_class: "/Script/CinematicCamera.CineCameraActor" },
      ]),
      tracks: JSON.stringify([{
        binding: "cam",
        type: "Transform",
        sections: [{
          start_frame: 0,
          end_frame: 90,
          keys: [{ frame: 0, value: { location: { x: 1200, y: 0, z: 300 } }, interpolation: "Cubic" }],
        }],
      }]),
    });
    const sent = received[1];
    assert(sent?.__tool === "sequence_build", "the build runtime command name is wrong");
    assert(
      Array.isArray(sent?.tracks) && Array.isArray(sent?.bindings),
      "bindings or tracks arrived as text rather than arrays; the structured-parameter decode is missing",
    );
    const sentRange = sent?.playback_range as { end_frame?: number } | undefined;
    assert(sentRange?.end_frame === 90, "playback_range was flattened on the way to the pipe");
    const sentTracks = sent?.tracks as { sections?: { keys?: unknown[] }[] }[];
    assert(
      sentTracks[0]?.sections?.[0]?.keys?.length === 1,
      "the nested key list did not survive as structure",
    );
    console.log("  PASS  sequence inspect and build contract");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

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

/** puerts_widget_bind is the write half of the bindings and variables
    widget_inspect could already read. What is pinned here is the surface a
    client sees, because every rule below it is UE4.27's and lives in C++:

    1. It is annotated mutating and idempotent, NOT destructive. remove_unlisted
       is what takes work away and it is opt-in, so the default classification
       has to say so.
    2. A request that states neither section never reaches the editor. Sending
       one would be a round trip to be told the obvious, and with
       remove_unlisted set it is the shape that means "delete everything".
    3. bindings marshals as an array of objects, not as text. A schema that let
       a string through would reach C++ as a non-array. */
async function widgetBindSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  const bindAnnotations = toolAnnotations.puerts_widget_bind;
  assert(bindAnnotations !== undefined, "puerts_widget_bind has no annotation");
  assert(bindAnnotations.readOnlyHint !== true, "the widget binder is annotated read-only");
  assert(bindAnnotations.idempotentHint === true, "the widget binder is not annotated idempotent");
  assert(
    bindAnnotations.destructiveHint !== true,
    "the widget binder is annotated destructive; remove_unlisted is opt-in and off by default",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-wbp-bind-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-wbp-bind-${process.pid}-${Date.now()}`;
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
      message: "Widget bindings applied: 2.",
      data: {
        asset_path: "/Game/MCPGenerated/WBP_Probe",
        applied_change_count: 2,
        converged: false,
        compiled: true,
        compile_status: "UpToDate",
        saved: true,
      },
      changed_assets: ["/Game/MCPGenerated/WBP_Probe.WBP_Probe"],
      changed_actors: [], warnings: [], errors: [],
      log_output: [], transaction_id: "T9",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tool = createPuertsTools(new PuerTSClient())
      .find((entry) => entry.name === "puerts_widget_bind");
    assert(tool !== undefined, "puerts_widget_bind is missing");

    const bound = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      bindings: [
        { widget: "ChargeBar", property: "Percent", source: { kind: "function", name: "GetCharge" } },
        { widget: "TitleText", property: "Text", source: { kind: "variable", name: "BeaconName" } },
      ],
      expose_as_variable: ["ToggleButton"],
    });
    assert(JSON.parse(bound.content[0]?.text ?? "null").success === true, "a valid bind spec was rejected");
    const sent = received[0];
    assert(sent?.__tool === "widget_bind", "the runtime command name is wrong");
    assert(
      typeof sent?.__timeout === "number" && (sent.__timeout as number) > 5000,
      "widget binding did not get its own timeout budget",
    );
    assert(
      Array.isArray(sent?.bindings) && (sent.bindings as unknown[]).length === 2,
      "bindings did not reach the pipe as an array of objects",
    );
    assert(
      Array.isArray(sent?.expose_as_variable),
      "expose_as_variable did not reach the pipe as an array",
    );

    const fromText = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      bindings: '[{"widget":"ChargeBar","property":"Percent","source":{"kind":"function","name":"GetCharge"}}]',
    });
    assert(
      JSON.parse(fromText.content[0]?.text ?? "null").success === true,
      "a bindings array sent as JSON text was rejected",
    );
    assert(
      Array.isArray(received[1]?.bindings),
      "the bindings text was not decoded before the pipe",
    );

    const badKind = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      bindings: [{ widget: "ChargeBar", property: "Percent", source: { kind: "delegate", name: "X" } }],
    });
    assert(
      JSON.parse(badKind.content[0]?.text ?? "null").success === false,
      "an unknown binding source kind was accepted",
    );

    const badPath = await tool.handler({
      asset_path: "/Game/UI/WBP_Probe",
      expose_as_variable: ["ChargeBar"],
    });
    assert(
      JSON.parse(badPath.content[0]?.text ?? "null").success === false,
      "a path outside /Game/MCPGenerated was accepted",
    );

    // Neither section stated. With remove_unlisted this is the request that
    // means "remove everything", so it must not reach the editor at all.
    const empty = await tool.handler({
      asset_path: "/Game/MCPGenerated/WBP_Probe",
      remove_unlisted: true,
    });
    assert(
      JSON.parse(empty.content[0]?.text ?? "null").success === false,
      "a bind request that stated no desired set was accepted",
    );
    assert(received.length === 2, "a rejected bind request still reached the editor");
    console.log("  PASS  widget bind schema, marshaling, and rejected specs");
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
    await rm(directory, { recursive: true, force: true });
    delete process.env.MCP_PUERTS_TOKEN_PATH;
    delete process.env.MCP_PUERTS_PIPE;
  }
}

/** The animation lane's three tools. Two contracts are tested here that the
    other builders do not have:

    1. puerts_anim_blueprint_build is annotated mutating and NOT idempotent, on
       purpose. It creates a new Animation Blueprint and refuses one that
       exists, so a rerun fails rather than converging, and an idempotent hint
       would tell a caller the opposite.
    2. A rolled-back build must report success:false and claim no changed
       assets. A build that rolled back and still named a changed asset would
       send a caller to save something that no longer exists. */
async function animationSuite(): Promise<void> {
  const { toolAnnotations } = await import("../src/annotations.js");
  for (const name of [
    "puerts_anim_blueprint_inspect",
    "puerts_anim_montage_inspect",
    "puerts_anim_blend_space_inspect",
  ]) {
    const annotations = toolAnnotations[name];
    assert(annotations !== undefined, `${name} has no annotation`);
    assert(annotations.readOnlyHint === true, `${name} is not annotated read-only`);
  }
  const buildAnnotations = toolAnnotations.puerts_anim_blueprint_build;
  assert(buildAnnotations !== undefined, "puerts_anim_blueprint_build has no annotation");
  assert(buildAnnotations.readOnlyHint === false, "the Animation Blueprint builder is annotated read-only");
  assert(
    buildAnnotations.idempotentHint === false,
    "the Animation Blueprint builder is annotated idempotent, but it refuses an existing asset",
  );
  assert(
    buildAnnotations.destructiveHint === false,
    "the Animation Blueprint builder is annotated destructive, but it never touches an existing asset",
  );

  const directory = await mkdtemp(join(tmpdir(), "ue4-puerts-anim-"));
  const tokenPath = join(directory, "token.txt");
  const pipeName = `\\\\.\\pipe\\ue4-puerts-anim-${process.pid}-${Date.now()}`;
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
    // A build aimed at ABP_Broken answers as a rolled-back failure, so both
    // halves of the envelope are covered by one listener.
    const broken = request.tool === "anim_blueprint_build"
      && String(request.params?.asset_path ?? "").endsWith("ABP_Broken");
    socket.end(JSON.stringify({
      session: RESPONSE_SESSION,
      success: !broken,
      message: broken ? "Animation Blueprint build reported errors and was rolled back." : "ok",
      data: broken
        ? { asset_path: "/Game/MCPGenerated/ABP_Broken", created: false, compile_status: "RolledBack" }
        : {
          asset_path: "/Game/MCPGenerated/ABP_Hero",
          object_path: "/Game/MCPGenerated/ABP_Hero.ABP_Hero",
          identity_kind: "derived",
          package_dirty_before: false,
          package_dirty_after: false,
          structure_hash_sha1: "0".repeat(40),
        },
      changed_assets: broken || request.tool !== "anim_blueprint_build"
        ? [] : ["/Game/MCPGenerated/ABP_Hero.ABP_Hero"],
      changed_actors: [],
      warnings: [],
      errors: broken ? ["state 'Walk' is missing after read-back"] : [],
      log_output: [],
      transaction_id: broken || request.tool !== "anim_blueprint_build" ? "" : "tx-anim-1",
    }) + "\n");
  }));

  try {
    await new Promise<void>((resolve, reject) => {
      server.once("error", reject);
      server.listen(pipeName, resolve);
    });
    const tools = createPuertsTools(new PuerTSClient());
    const inspect = tools.find((entry) => entry.name === "puerts_anim_blueprint_inspect");
    const montage = tools.find((entry) => entry.name === "puerts_anim_montage_inspect");
    const build = tools.find((entry) => entry.name === "puerts_anim_blueprint_build");
    assert(inspect !== undefined && montage !== undefined && build !== undefined, "an animation tool is missing");

    const read = await inspect.handler({ asset_path: "/Game/MCPGenerated/ABP_Hero" });
    const readPayload = JSON.parse(read.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(readPayload.success === true, "a valid Animation Blueprint inspection was rejected");
    assert(readPayload.transaction_id === "", "a read-only Animation Blueprint inspection returned a transaction id");
    assert(
      Array.isArray(readPayload.changed_assets) && (readPayload.changed_assets as unknown[]).length === 0,
      "an Animation Blueprint read reported a changed asset",
    );
    assert(received[0]?.__tool === "anim_blueprint_inspect", "the inspector runtime command name is wrong");
    assert(
      typeof received[0]?.__timeout === "number" && (received[0].__timeout as number) > 5000,
      "Animation Blueprint inspection did not get its own timeout budget",
    );

    // Reading is wider than authoring: an engine-shipped AnimBP is readable.
    const engineRead = await inspect.handler({ asset_path: "/Engine/Anim/ABP_Thing" });
    assert(
      JSON.parse(engineRead.content[0]?.text ?? "null").success === true,
      "the Animation Blueprint inspector refused an /Engine path",
    );

    const montageRead = await montage.handler({ asset_path: "/Game/Anim/AM_Attack" });
    assert(
      JSON.parse(montageRead.content[0]?.text ?? "null").success === true,
      "a valid Animation Montage inspection was rejected",
    );
    assert(received[2]?.__tool === "anim_montage_inspect", "the montage runtime command name is wrong");

    const built = await build.handler({
      asset_path: "/Game/MCPGenerated/ABP_Hero",
      skeleton_path: "/Game/Characters/Hero_Skeleton",
      anim_graph: { pipeline: [{ id: "sm", type: "StateMachine", name: "Locomotion" }] },
      state_machine: {
        states: [{ id: "idle", name: "Idle", animation: "/Game/Anim/Idle", is_entry: true }],
        transitions: [],
      },
    });
    const builtPayload = JSON.parse(built.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(builtPayload.success === true, "a valid Animation Blueprint build was rejected");
    assert(received[3]?.__tool === "anim_blueprint_build", "the builder runtime command name is wrong");
    assert(
      typeof received[3]?.__timeout === "number" && (received[3].__timeout as number) > 30000,
      "the Animation Blueprint build did not get a compile-sized timeout budget",
    );

    const rolledBack = await build.handler({
      asset_path: "/Game/MCPGenerated/ABP_Broken",
      skeleton_path: "/Game/Characters/Hero_Skeleton",
      anim_graph: { pipeline: [{ id: "sm", type: "StateMachine", name: "Locomotion" }] },
      state_machine: {
        states: [{ id: "walk", name: "Walk", animation: "/Game/Anim/Walk", is_entry: true }],
        transitions: [],
      },
    });
    const rolledBackPayload = JSON.parse(rolledBack.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(rolledBackPayload.success === false, "a rolled-back Animation Blueprint build reported success");
    assert(
      Array.isArray(rolledBackPayload.changed_assets)
      && (rolledBackPayload.changed_assets as unknown[]).length === 0,
      "a rolled-back Animation Blueprint build still named a changed asset",
    );

    const blendSpace = tools.find((entry) => entry.name === "puerts_anim_blend_space_inspect");
    assert(blendSpace !== undefined, "the Blend Space inspector is missing");
    const blendRead = await blendSpace.handler({ asset_path: "/Game/Anim/BS_Locomotion" });
    const blendPayload = JSON.parse(blendRead.content[0]?.text ?? "null") as Record<string, unknown>;
    assert(blendPayload.success === true, "a valid Blend Space inspection was rejected");
    assert(blendPayload.transaction_id === "", "a read-only Blend Space inspection returned a transaction id");
    assert(
      received[received.length - 1]?.__tool === "anim_blend_space_inspect",
      "the Blend Space runtime command name is wrong",
    );

    const sentSoFar = received.length;
    const outsideRoot = await build.handler({
      asset_path: "/Game/Elsewhere/ABP_Hero",
      skeleton_path: "/Game/Characters/Hero_Skeleton",
      anim_graph: { pipeline: [] },
      state_machine: { states: [], transitions: [] },
    });
    assert(
      JSON.parse(outsideRoot.content[0]?.text ?? "null").success === false,
      "the Animation Blueprint builder accepted a path outside /Game/MCPGenerated/",
    );
    const noSkeleton = await build.handler({
      asset_path: "/Game/MCPGenerated/ABP_Hero",
      anim_graph: { pipeline: [] },
      state_machine: { states: [], transitions: [] },
    });
    assert(
      JSON.parse(noSkeleton.content[0]?.text ?? "null").success === false,
      "the Animation Blueprint builder accepted a spec with no skeleton_path",
    );
    const authoringKey = await inspect.handler({
      asset_path: "/Game/MCPGenerated/ABP_Hero",
      state_machine: { states: [] },
    });
    assert(
      JSON.parse(authoringKey.content[0]?.text ?? "null").success === false,
      "the Animation Blueprint inspector accepted an authoring key",
    );
    assert(received.length === sentSoFar, "a rejected animation request still reached the pipe");
    console.log("  PASS  animation: three read-only inspectors, create-only build, rollback envelope");
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
  .then(widgetBindSuite)
  .then(animationSuite)
  .then(materialSuite)
  .then(sceneSuite)
  .then(levelCompletionSuite)
  .then(sequenceSuite)
  .then(connectionContractSuite)
  .then(nonActorParentSuite)
  .then(graphInspectSuite)
  .then(discoverySuite)
  .catch((error: unknown) => {
    console.error(`  FAIL  ${error instanceof Error ? error.message : String(error)}`);
    process.exit(1);
  });
