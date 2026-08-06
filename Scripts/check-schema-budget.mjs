#!/usr/bin/env node
// Enforces the tool-schema size budget.
//
// A tool description and its inputSchema are RESIDENT context: a client that
// lists tools pays for every word on every session, whether or not the tool is
// ever called. That is the real cost of an MCP server, and it grows silently,
// one well-meaning paragraph at a time. This catalog was 178 KB before the
// split; the descriptions were design documents.
//
// The rule the split enforces: a schema carries what a caller needs to make a
// VALID call without a lookup (purpose, required parameters, hard constraints,
// refusal behaviour). Node catalogs, operation grammars, convergence semantics
// and rationale live in skills/unreal-engine-4-27/references/, which is fetched
// on demand. Trimming past that point is not a win: it trades a smaller catalog
// for extra lookup round trips, or for invalid arguments.
//
// Usage:
//   node Scripts/check-schema-budget.mjs            verify
//   node Scripts/check-schema-budget.mjs --report   print the ranked table
//
// Requires mcp-server/dist (run npm run build first).

import { existsSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const distDir = join(repoRoot, 'mcp-server', 'dist');
const report = process.argv.includes('--report');

// Budgets in bytes of serialized JSON. The catalog is ~135 KB today, so the
// total carries about 5 KB of slack: enough for an honest new tool, not enough
// to absorb a wave of them without someone deciding that is what they want.
//
// The two budgets guard different failures and that split is deliberate.
// TOTAL catches tool-count sprawl, where every tool is reasonable and the sum
// is not. DESCRIPTION catches prose regrowth, which is the failure that
// actually happened here: no single description looked unreasonable while it
// was being written.
const TOTAL_BUDGET = 140_000;
const PER_TOOL_BUDGET = 6_000;
const DESCRIPTION_BUDGET = 1_200;

// Tools whose size is schema STRUCTURE, not prose. Nothing to move to the
// skill: the nesting is what validates the input, and validation at a trust
// boundary is not negotiable. Listed here so an exception is visible and has to
// be argued for, rather than silently raising the budget for everyone.
const STRUCTURAL_EXEMPTIONS = new Map([
  ['blueprint_production_plan', 'Deeply nested FeatureRequest validation; zero prose in the schema.'],
  ['puerts_blueprint_build', 'Component, variable and graph node sub-schemas; prose already moved.'],
]);

if (!existsSync(join(distDir, 'index.js'))) {
  console.error('FAIL: mcp-server/dist is missing. Run npm run build first.');
  process.exit(1);
}

// Ask the built server over stdio, exactly as a client does. The budget is
// about what tools/list actually ships, so measure that rather than a
// reconstruction of it that can drift.
const listTools = () => new Promise((resolve, reject) => {
  const server = spawn(process.execPath, [join(distDir, 'index.js')], {
    stdio: ['pipe', 'pipe', 'ignore'],
  });
  const timer = setTimeout(() => {
    server.kill();
    reject(new Error('server did not answer tools/list within 30s'));
  }, 30_000);
  const send = (message) => server.stdin.write(`${JSON.stringify(message)}\n`);

  let buffer = '';
  server.stdout.on('data', (chunk) => {
    buffer += chunk;
    const lines = buffer.split('\n');
    buffer = lines.pop() ?? '';
    for (const line of lines) {
      if (!line.trim()) continue;
      let message;
      try { message = JSON.parse(line); } catch { continue; }
      if (message.id === 1) {
        send({ jsonrpc: '2.0', id: 2, method: 'tools/list', params: {} });
      } else if (message.id === 2) {
        clearTimeout(timer);
        server.kill();
        resolve(message.result.tools);
      }
    }
  });
  server.on('error', reject);

  send({
    jsonrpc: '2.0',
    id: 1,
    method: 'initialize',
    params: {
      protocolVersion: '2024-11-05',
      capabilities: {},
      clientInfo: { name: 'schema-budget', version: '1' },
    },
  });
});

const tools = await listTools();

const rows = tools.map((tool) => ({
  name: tool.name,
  total: JSON.stringify(tool).length,
  description: (tool.description ?? '').length,
})).sort((a, b) => b.total - a.total);

const total = rows.reduce((sum, row) => sum + row.total, 0);

if (report) {
  console.log(`${rows.length} tools, ${total} bytes (~${Math.round(total / 4)} tokens)\n`);
  console.log('  bytes   desc  tool');
  for (const row of rows) {
    console.log(
      `${String(row.total).padStart(7)} ${String(row.description).padStart(6)}  ${row.name}`,
    );
  }
  process.exit(0);
}

const failures = [];

if (total > TOTAL_BUDGET) {
  failures.push(
    `catalog is ${total} bytes, over the ${TOTAL_BUDGET} budget by ${total - TOTAL_BUDGET}`,
  );
}

for (const row of rows) {
  const exemption = STRUCTURAL_EXEMPTIONS.get(row.name);
  if (row.total > PER_TOOL_BUDGET && !exemption) {
    failures.push(
      `${row.name}: ${row.total} bytes, over the ${PER_TOOL_BUDGET} per-tool budget. `
      + 'Move the catalogs and semantics into skills/unreal-engine-4-27/references/, '
      + 'or add a STRUCTURAL_EXEMPTIONS entry saying why the size is validation.',
    );
  }
  // The description budget has no exemptions. Prose is always movable.
  if (row.description > DESCRIPTION_BUDGET) {
    failures.push(
      `${row.name}: description is ${row.description} chars, over ${DESCRIPTION_BUDGET}. `
      + 'Keep purpose, constraints and refusal behaviour; move the rest to the skill.',
    );
  }
}

if (failures.length) {
  console.error('FAIL: tool schema budget exceeded.');
  for (const failure of failures) console.error(`  ${failure}`);
  console.error('\nRun with --report to see the ranked table.');
  process.exit(1);
}

console.log(
  `OK: ${rows.length} tools, ${total} bytes (~${Math.round(total / 4)} tokens), `
  + `under the ${TOTAL_BUDGET} budget. Largest: ${rows[0].name} at ${rows[0].total}.`,
);
