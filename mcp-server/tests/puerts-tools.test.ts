import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { PuerTSClient } from "../src/puerts-client.js";
import { createPuertsTools } from "../src/tools/puerts.js";

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
    assert(tools.length === 17, "expected all 17 PuerTS tools");
    assert(tools.some((tool) => tool.name === "puerts_sky_shader_create"), "native sky shader tool is missing");
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

main().catch((error: unknown) => {
  console.error(`  FAIL  ${error instanceof Error ? error.message : String(error)}`);
  process.exit(1);
});
