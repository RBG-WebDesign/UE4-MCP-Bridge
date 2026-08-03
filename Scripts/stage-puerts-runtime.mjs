#!/usr/bin/env node
// Stages the PuerTS JavaScript support files from the pinned bundle into the
// MCPBridge plugin Content. Plugins/MCPBridge/Content/JavaScript is generated
// output and is not tracked: the root *.js files come from the puerts-runtime
// tsc build, and the puerts/ subtree is a verbatim copy of
// Plugins/Puerts/Content/JavaScript/puerts, whose contents are pinned by
// Plugins/Puerts.lock.json.
//
// The staged copy is verified against the lock file, not against the source
// tree, so --check works on a machine that has no 369 MB bundle. That is what
// Scripts/package-mcp-bridge.ps1 calls before it will produce a release.
//
// Usage:
//   node Scripts/stage-puerts-runtime.mjs           copy the puerts/ subtree
//   node Scripts/stage-puerts-runtime.mjs --check   verify the staged copy
//   node Scripts/stage-puerts-runtime.mjs --bundle <dir>       bundle elsewhere
//   node Scripts/stage-puerts-runtime.mjs --check --plugin <dir>  plugin copy
//                                                   elsewhere (a release stage)
//   node Scripts/stage-puerts-runtime.mjs --allow-missing-bundle
//                                                   server-only checkout: warn
//                                                   instead of failing

import { createHash } from 'node:crypto';
import { cpSync, existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');

const args = process.argv.slice(2);
const check = args.includes('--check');
const allowMissing = args.includes('--allow-missing-bundle');
const bundleArg = args.indexOf('--bundle');
const bundleDir = bundleArg >= 0 ? args[bundleArg + 1] : join(repoRoot, 'Plugins', 'Puerts');
// --plugin <dir> checks a plugin tree that is not this checkout's: what
// package-mcp-bridge.ps1 points at the copy it has staged for a release.
const pluginArg = args.indexOf('--plugin');
const pluginDir = pluginArg >= 0 ? args[pluginArg + 1] : join(repoRoot, 'Plugins', 'MCPBridge');

const source = join(bundleDir, 'Content', 'JavaScript', 'puerts');
const destination = join(pluginDir, 'Content', 'JavaScript', 'puerts');
const lockPath = join(repoRoot, 'Plugins', 'Puerts.lock.json');

const LOCK_PREFIX = 'Content/JavaScript/puerts/';

function lockedFiles() {
  if (!existsSync(lockPath)) {
    console.error(`FAIL: ${lockPath} does not exist, so there is nothing to verify the staged copy against.`);
    process.exit(1);
  }
  const lock = JSON.parse(readFileSync(lockPath, 'utf8'));
  const want = new Map();
  for (const [file, hash] of Object.entries(lock.files ?? {})) {
    if (file.startsWith(LOCK_PREFIX)) want.set(file.slice(LOCK_PREFIX.length), hash);
  }
  if (want.size === 0) {
    console.error(`FAIL: ${lockPath} lists no ${LOCK_PREFIX}* files, so the pin cannot be checked.`);
    process.exit(1);
  }
  return { want, pin: `${lock.upstream.tag} @ ${lock.upstream.commit.slice(0, 12)}` };
}

function hashTree(root) {
  const out = new Map();
  const visit = (dir) => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) visit(full);
      else if (entry.isFile()) {
        out.set(
          relative(root, full).replaceAll('\\', '/'),
          createHash('sha256').update(readFileSync(full)).digest('hex'),
        );
      }
    }
  };
  visit(root);
  return out;
}

/** Compare the staged copy against the lock file. Returns a list of problems. */
function verifyStaged(want) {
  if (!existsSync(destination)) return [`missing: ${destination}`];
  const have = hashTree(destination);
  const problems = [];
  for (const [file, hash] of want) {
    if (!have.has(file)) problems.push(`missing: puerts/${file}`);
    else if (have.get(file) !== hash) problems.push(`differs from the pin: puerts/${file}`);
  }
  for (const file of have.keys()) {
    if (!want.has(file)) problems.push(`not in the pin: puerts/${file}`);
  }
  return problems;
}

function report(problems, pin, want) {
  if (problems.length === 0) {
    console.log(`OK: staged puerts support files match the pin (${pin}, ${want.size} files).`);
    return;
  }
  console.error(`FAIL: ${destination} does not match the pin (${pin}).`);
  for (const p of problems.slice(0, 20)) console.error(`  ${p}`);
  if (problems.length > 20) console.error(`  ...and ${problems.length - 20} more.`);
  console.error(`Re-stage with: node Scripts/stage-puerts-runtime.mjs --bundle <path to Plugins/Puerts>`);
  process.exit(1);
}

const { want, pin } = lockedFiles();

if (check) {
  report(verifyStaged(want), pin, want);
  process.exit(0);
}

if (!existsSync(source)) {
  // No bundle. Either the staged copy is already correct, or this checkout
  // cannot produce a loadable plugin and must say so: exiting 0 here is how an
  // incomplete Content/JavaScript used to reach a release zip unnoticed.
  const problems = verifyStaged(want);
  if (problems.length === 0) {
    console.log(`OK: pinned bundle not present at ${bundleDir}, but the staged copy already matches the pin (${pin}).`);
    process.exit(0);
  }
  const message = `pinned bundle support files not found at ${source}, and the staged copy is incomplete.`;
  if (allowMissing) {
    console.warn(`WARN: ${message} The MCP server builds without them; the in-editor PuerTS runtime does not, ` +
      'and Scripts/package-mcp-bridge.ps1 will refuse to package this tree.');
    process.exit(0);
  }
  console.error(`FAIL: ${message}`);
  for (const p of problems.slice(0, 5)) console.error(`  ${p}`);
  console.error('Obtain the pinned bundle (see docs/PUERTS.md), or pass --allow-missing-bundle for a ' +
    'server-only checkout that will not be packaged.');
  process.exit(1);
}

cpSync(source, destination, { recursive: true, force: true });
report(verifyStaged(want), pin, want);
