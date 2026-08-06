#!/usr/bin/env node
// Measures whether a caller can author a VALID first call from the tool schema
// alone, without opening a skill reference.
//
// This is the direct test of the risk the schema trim created. Moving prose out
// of a description costs nothing: nobody needed the paragraph to spell a
// parameter. Moving a TOKEN out is different. If a caller has to write
// params.op = "add_int" and "add_int" appears nowhere in the schema, the call is
// a lookup or a guess, and a guess is an invalid first call. Byte reduction that
// buys extra round trips is not a win, it is the same cost in a different place.
//
// It is a static, editor-free, model-free proxy for first-call validity, and it
// is honest about being one: it measures whether the token was DISCOVERABLE, not
// whether a given model would have found it. What it cannot measure is
// completion rate, total tool calls and repair calls, which need a live agent
// driving a live editor. Those stay a live-run measurement.
//
// The corpus is every call this repository already proves correct: the seven
// vertical slices and the acceptance scripts. Those payloads ran against a real
// editor and passed, so every token in them is one a real task actually needed.
//
//   node Scripts/bench-schema-sufficiency.mjs
//   node Scripts/bench-schema-sufficiency.mjs --json out.json
//   node Scripts/bench-schema-sufficiency.mjs --baseline before.json
//
// Requires mcp-server/dist (run npm run build first).

import { existsSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { spawn } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const distDir = join(repoRoot, 'mcp-server', 'dist');
const referenceDir = join(repoRoot, 'skills', 'unreal-engine-4-27', 'references');

const argOf = (flag) => {
  const index = process.argv.indexOf(flag);
  return index === -1 ? null : process.argv[index + 1];
};
const jsonOut = argOf('--json');
const baselinePath = argOf('--baseline');

if (!existsSync(join(distDir, 'index.js'))) {
  console.error('FAIL: mcp-server/dist is missing. Run npm run build first.');
  process.exit(1);
}

// ---------------------------------------------------------------- the corpus

/** Files whose payloads are known-good: they ran against a real UE4.27 editor
    and passed. A token that appears here is one a real task needed. */
function corpusFiles() {
  const scripts = readdirSync(join(repoRoot, 'Scripts'))
    .filter((f) => /^slice-[a-z]+\.mjs$/.test(f) || /-acceptance\.mjs$/.test(f))
    .map((f) => join(repoRoot, 'Scripts', f));
  return scripts.filter((f) => existsSync(f));
}

/** Names the author of the corpus INVENTED rather than looked up: their own
 *  variable names, node ids, component and asset labels.
 *
 *  These matter because two documented rules make an invented name show up in a
 *  vocabulary position. A VariableGet names its data pin after the variable, and
 *  a MakeStruct after the struct. So "IsLit" appears as a pin role in a proven
 *  call while being nobody's vocabulary: the caller needed the RULE, which is
 *  documented, not the token. Counting those as undocumented would inflate the
 *  score with names no schema could ever have carried, and a benchmark with
 *  false positives is worse than no benchmark. */
function authoredNames(source) {
  const names = new Set();
  for (const match of source.matchAll(/\b(?:id|name|var_name|varName|label|to|from)\s*:\s*["']([A-Za-z_][A-Za-z0-9_]*)["']/g)) {
    names.add(match[1]);
  }
  return names;
}

/** Token classes, each tied to the exact schema position a caller writes it in.
 *
 *  Node types are deliberately NOT harvested: they are a zod enum, so they are
 *  in the schema by construction and cannot become reference-only. The classes
 *  below are the free-form positions, where the schema accepts z.record or
 *  z.string and the vocabulary lives only in whatever text we wrote. Those are
 *  the ones a trim can silently strand. */
function harvest(files) {
  const tokens = new Map(); // token -> { token, kind, files:Set }
  const add = (token, kind, file) => {
    const key = `${kind}:${token}`;
    const entry = tokens.get(key) ?? { token, kind, files: new Set() };
    entry.files.add(file.replace(`${repoRoot}\\`, '').replace(`${repoRoot}/`, ''));
    tokens.set(key, entry);
  };

  // Deliberate nonsense: the acceptance scripts send these to prove a refusal
  // names the bad pin. A token that is SUPPOSED to be unknown is not a
  // documentation gap.
  const isNegativeFixture = (token) => /^no_?such/i.test(token);

  // Blank the INSIDE of every string literal. Prose in a PrintString default
  // ("vertical slice: gameplay") otherwise reads as a params key named "slice".
  // Only the key scan uses this view; the value scans need the real contents.
  const blankStrings = (text) => text.replace(/"[^"\n]*"|'[^'\n]*'/g, (literal) =>
    literal[0] + ' '.repeat(literal.length - 2) + literal[0]);

  // A graph node literal: an object carrying id, type and params. Matching a
  // bare `params:` anywhere swept in harness option bags (timeout_ms,
  // wait_for_completion), which are a slice script's own arguments and not a
  // tool vocabulary at all.
  const NODE_PARAMS = /\{[^{}]*\bid:\s*["'][^"']*["'][^{}]*\btype:\s*["']([^"']*)["'][^{}]*\bparams:\s*\{([^{}]*(?:\{[^{}]*\}[^{}]*)*)\}/g;

  // A CallFunction node's params beyond class and function are the UFUNCTION's
  // OWN parameter names, drawn from the whole reflected engine. No bridge schema
  // could ever carry that list, and the schema already states the rule that
  // sends a caller to unreal-api or engine_source_search for it. Scoring
  // WidgetType as a documentation gap would be scoring the bridge for not
  // inlining Unreal.
  const ENGINE_SUPPLIED = new Set(['CallFunction']);
  const BRIDGE_KEYS_ON_ENGINE_NODES = new Set(['class', 'function']);

  for (const file of files) {
    const source = readFileSync(file, 'utf8');
    const authored = authoredNames(source);
    const skip = (token) => authored.has(token) || isNegativeFixture(token);

    // One pass over the RAW source, so the node type and the op values survive.
    // Only the params body is blanked, and only for the key scan.
    for (const node of source.matchAll(NODE_PARAMS)) {
      const [, nodeType, body] = node;
      for (const op of body.matchAll(/\bop:\s*["']([a-z0-9_]+)["']/g)) {
        add(op[1], 'operator-op', file);
      }
      for (const key of blankStrings(body).matchAll(/(?:^|[,{\s])([A-Za-z_][A-Za-z0-9_]*)\s*:/g)) {
        if (key[1] === 'op' || skip(key[1])) continue;
        if (ENGINE_SUPPLIED.has(nodeType) && !BRIDGE_KEYS_ON_ENGINE_NODES.has(key[1])) continue;
        add(key[1], 'params-key', file);
      }
    }

    // Connection endpoints: "nodeId.PinRole". The pin role half is the token.
    for (const link of source.matchAll(/\b(?:from|to):\s*["'][A-Za-z0-9_]+\.([A-Za-z_][A-Za-z0-9_ ]*)["']/g)) {
      if (skip(link[1])) continue;
      add(link[1], 'pin-role', file);
    }
  }
  return [...tokens.values()];
}

// ------------------------------------------------------- what the schema says

function listTools() {
  return new Promise((resolve, reject) => {
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
        if (message.id === 1) send({ jsonrpc: '2.0', id: 2, method: 'tools/list', params: {} });
        else if (message.id === 2) {
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
        clientInfo: { name: 'bench-schema-sufficiency', version: '1' },
      },
    });
  });
}

function referenceText() {
  if (!existsSync(referenceDir)) return '';
  return readdirSync(referenceDir)
    .filter((f) => f.endsWith('.md'))
    .map((f) => readFileSync(join(referenceDir, f), 'utf8'))
    .join('\n');
}

// A token counts as present only on a word boundary. Substring matching would
// score "add_int" as found because "add_interface" happens to contain it, which
// is exactly the kind of false pass that makes a benchmark worse than none.
const contains = (haystack, token) =>
  new RegExp(`(?<![A-Za-z0-9_])${token.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}(?![A-Za-z0-9_])`)
    .test(haystack);

const tools = await listTools();
const schemaText = JSON.stringify(tools);
const referenceCorpus = referenceText();
const corpus = corpusFiles();
const tokens = harvest(corpus);

for (const entry of tokens) {
  entry.inSchema = contains(schemaText, entry.token);
  entry.inReference = contains(referenceCorpus, entry.token);
  entry.verdict = entry.inSchema ? 'schema'
    : entry.inReference ? 'reference-only'
      : 'undocumented';
}

const byVerdict = (verdict) => tokens.filter((t) => t.verdict === verdict);
const schemaTokens = byVerdict('schema');
const referenceOnly = byVerdict('reference-only');
const undocumented = byVerdict('undocumented');
const sufficiency = tokens.length === 0 ? 1 : schemaTokens.length / tokens.length;

const report = {
  corpus_files: corpus.length,
  schema_bytes: schemaText.length,
  tokens_total: tokens.length,
  tokens_in_schema: schemaTokens.length,
  tokens_reference_only: referenceOnly.length,
  tokens_undocumented: undocumented.length,
  first_call_sufficiency: Number(sufficiency.toFixed(4)),
  reference_only: referenceOnly
    .map((t) => ({ token: t.token, kind: t.kind, used_by: [...t.files].sort() }))
    .sort((a, b) => a.kind.localeCompare(b.kind) || a.token.localeCompare(b.token)),
  undocumented: undocumented
    .map((t) => ({ token: t.token, kind: t.kind, used_by: [...t.files].sort() }))
    .sort((a, b) => a.token.localeCompare(b.token)),
};

console.log('schema sufficiency: can a valid first call be authored from the schema alone?\n');
console.log(`  corpus            ${report.corpus_files} proven call sites`);
console.log(`  schema            ${report.schema_bytes} bytes (~${Math.round(report.schema_bytes / 4)} tokens)`);
console.log(`  caller tokens     ${report.tokens_total}`);
console.log(`    in schema       ${report.tokens_in_schema}`);
console.log(`    reference-only  ${report.tokens_reference_only}   <- a lookup, or a guess`);
console.log(`    undocumented    ${report.tokens_undocumented}   <- a guess, in either version`);
console.log(`  sufficiency       ${(sufficiency * 100).toFixed(1)}%\n`);

if (referenceOnly.length) {
  console.log('  reference-only tokens, by kind:');
  let kind = null;
  for (const entry of report.reference_only) {
    if (entry.kind !== kind) { kind = entry.kind; console.log(`    ${kind}`); }
    console.log(`      ${entry.token}`);
  }
  console.log('');
}
if (undocumented.length) {
  console.log('  UNDOCUMENTED (neither schema nor reference):');
  for (const entry of report.undocumented) {
    console.log(`    ${entry.token} (${entry.kind}) used by ${entry.used_by.join(', ')}`);
  }
  console.log('');
}

if (baselinePath) {
  const before = JSON.parse(readFileSync(baselinePath, 'utf8'));
  const delta = (now, then) => {
    const d = now - then;
    return `${d >= 0 ? '+' : ''}${d}`;
  };
  console.log('  against baseline:');
  console.log(`    schema bytes     ${before.schema_bytes} -> ${report.schema_bytes} (${delta(report.schema_bytes, before.schema_bytes)})`);
  console.log(`    in schema        ${before.tokens_in_schema} -> ${report.tokens_in_schema} (${delta(report.tokens_in_schema, before.tokens_in_schema)})`);
  console.log(`    reference-only   ${before.tokens_reference_only} -> ${report.tokens_reference_only} (${delta(report.tokens_reference_only, before.tokens_reference_only)})`);
  console.log(`    sufficiency      ${(before.first_call_sufficiency * 100).toFixed(1)}% -> ${(sufficiency * 100).toFixed(1)}%`);
  console.log('');
}

if (jsonOut) {
  writeFileSync(jsonOut, `${JSON.stringify(report, null, 2)}\n`);
  console.log(`  wrote ${jsonOut}\n`);
}

// An undocumented token is a real defect either way: a caller has nowhere to
// learn it. Reference-only is a judgement call and is reported, not failed.
process.exit(undocumented.length > 0 ? 1 : 0);
