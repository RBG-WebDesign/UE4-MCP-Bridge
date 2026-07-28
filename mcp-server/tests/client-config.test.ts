/**
 * Unit tests for UnrealClient endpoint resolution.
 *
 * No mock server and no UE4: these only exercise how host, port, and timeout
 * are chosen from the environment and from explicit options.
 */

import { UnrealClient, resolveClientDefaults } from "../src/unreal-client.js";

interface TestCase { name: string; fn: () => void; }
const tests: TestCase[] = [];
let passed = 0;
let failed = 0;

function test(name: string, fn: () => void): void {
  tests.push({ name, fn });
}

function assert(condition: boolean, message: string): void {
  if (!condition) throw new Error(`Assertion failed: ${message}`);
}

const ENV_KEYS = ["UNREAL_BRIDGE_HOST", "UNREAL_BRIDGE_PORT", "UNREAL_BRIDGE_TIMEOUT_MS"];

function withEnv(values: Record<string, string | undefined>, fn: () => void): void {
  const saved: Record<string, string | undefined> = {};
  for (const key of ENV_KEYS) saved[key] = process.env[key];
  try {
    for (const key of ENV_KEYS) delete process.env[key];
    for (const [key, value] of Object.entries(values)) {
      if (value !== undefined) process.env[key] = value;
    }
    fn();
  } finally {
    for (const key of ENV_KEYS) {
      if (saved[key] === undefined) delete process.env[key];
      else process.env[key] = saved[key];
    }
  }
}

test("defaults to localhost:8080 with no environment set", () => {
  withEnv({}, () => {
    const defaults = resolveClientDefaults();
    assert(defaults.host === "localhost", `host was ${defaults.host}`);
    assert(defaults.port === 8080, `port was ${defaults.port}`);
    assert(new UnrealClient().endpoint === "http://localhost:8080", "endpoint wrong");
  });
});

test("reads host and port from the environment", () => {
  withEnv({ UNREAL_BRIDGE_HOST: "100.64.0.7", UNREAL_BRIDGE_PORT: "9090" }, () => {
    assert(new UnrealClient().endpoint === "http://100.64.0.7:9090",
      "tunnel endpoint not picked up");
  });
});

test("trims surrounding whitespace", () => {
  withEnv({ UNREAL_BRIDGE_HOST: "  desktop.tail1234.ts.net  " }, () => {
    assert(new UnrealClient().endpoint === "http://desktop.tail1234.ts.net:8080",
      "whitespace not trimmed");
  });
});

test("an empty host falls back rather than producing http://:8080", () => {
  withEnv({ UNREAL_BRIDGE_HOST: "   " }, () => {
    assert(new UnrealClient().endpoint === "http://localhost:8080", "empty host not handled");
  });
});

test("an unexpanded placeholder falls back instead of becoming a hostname", () => {
  // A .mcp.json using ${VAR:-default} that the client never expanded would
  // otherwise be used verbatim as a DNS name.
  withEnv({ UNREAL_BRIDGE_HOST: "${UNREAL_BRIDGE_HOST:-localhost}" }, () => {
    assert(new UnrealClient().endpoint === "http://localhost:8080",
      "placeholder was treated as a hostname");
  });
});

test("a malformed port falls back to 8080", () => {
  for (const bad of ["not-a-number", "0", "70000", "-1", "80.5"]) {
    withEnv({ UNREAL_BRIDGE_PORT: bad }, () => {
      const defaults = resolveClientDefaults();
      assert(defaults.port === 8080, `port ${bad} produced ${defaults.port}`);
    });
  }
});

test("a valid edge-of-range port is accepted", () => {
  withEnv({ UNREAL_BRIDGE_PORT: "65535" }, () => {
    assert(resolveClientDefaults().port === 65535, "65535 rejected");
  });
  withEnv({ UNREAL_BRIDGE_PORT: "1" }, () => {
    assert(resolveClientDefaults().port === 1, "1 rejected");
  });
});

test("timeout is configurable and clamped", () => {
  withEnv({ UNREAL_BRIDGE_TIMEOUT_MS: "600000" }, () => {
    assert(resolveClientDefaults().timeout === 600000, "timeout not read");
  });
  withEnv({ UNREAL_BRIDGE_TIMEOUT_MS: "10" }, () => {
    assert(resolveClientDefaults().timeout === 320000, "below-range timeout not rejected");
  });
});

test("explicit options beat the environment", () => {
  withEnv({ UNREAL_BRIDGE_HOST: "from-env", UNREAL_BRIDGE_PORT: "1234" }, () => {
    const client = new UnrealClient({ host: "explicit", port: 4321 });
    assert(client.endpoint === "http://explicit:4321", "explicit options were overridden");
  });
});

test("existing callers passing only a port still work", () => {
  // The unit tests construct clients as new UnrealClient({ port: TEST_PORT }).
  withEnv({}, () => {
    const client = new UnrealClient({ port: 18775 });
    assert(client.endpoint === "http://localhost:18775", "port-only construction broke");
  });
});

test("a failed request names the endpoint it tried", async () => {
  await withEnvAsync({ UNREAL_BRIDGE_HOST: "127.0.0.1", UNREAL_BRIDGE_PORT: "9" }, async () => {
    const result = await new UnrealClient({ timeout: 2000 }).sendCommand("ping");
    assert(result.success === false, "expected failure against a dead port");
    assert(String(result.error).includes("127.0.0.1:9"),
      `error did not name the endpoint: ${result.error}`);
  });
});

// Async variant of withEnv for the one test that awaits.
async function withEnvAsync(
  values: Record<string, string | undefined>, fn: () => Promise<void>
): Promise<void> {
  const saved: Record<string, string | undefined> = {};
  for (const key of ENV_KEYS) saved[key] = process.env[key];
  try {
    for (const key of ENV_KEYS) delete process.env[key];
    for (const [key, value] of Object.entries(values)) {
      if (value !== undefined) process.env[key] = value;
    }
    await fn();
  } finally {
    for (const key of ENV_KEYS) {
      if (saved[key] === undefined) delete process.env[key];
      else process.env[key] = saved[key];
    }
  }
}

async function main(): Promise<void> {
  for (const t of tests) {
    try {
      await t.fn();
      passed += 1;
      console.log(`  PASS  ${t.name}`);
    } catch (err) {
      failed += 1;
      console.error(`  FAIL  ${t.name}`);
      console.error(`        ${(err as Error).message}`);
    }
  }
  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed > 0 ? 1 : 0);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
