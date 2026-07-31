#!/usr/bin/env node
// Stages the PuerTS JavaScript support files from the pinned bundle into the
// MCPBridge plugin Content. Plugins/MCPBridge/Content/JavaScript is generated
// output and is not tracked: the root *.js files come from the puerts-runtime
// tsc build, and the puerts/ subtree is a verbatim copy of
// Plugins/Puerts/Content/JavaScript/puerts, whose contents are pinned by
// Plugins/Puerts.lock.json.
//
// Usage:
//   node Scripts/stage-puerts-runtime.mjs           copy the puerts/ subtree
//   node Scripts/stage-puerts-runtime.mjs --check   verify the copy matches

import { createHash } from 'node:crypto';
import { cpSync, existsSync, readFileSync, readdirSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const source = join(repoRoot, 'Plugins', 'Puerts', 'Content', 'JavaScript', 'puerts');
const destination = join(repoRoot, 'Plugins', 'MCPBridge', 'Content', 'JavaScript', 'puerts');

const check = process.argv.includes('--check');

if (!existsSync(source)) {
  console.warn(`SKIP: pinned bundle support files not found at ${source}. ` +
    'The server builds without them; the in-editor runtime does not.');
  process.exit(0);
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

if (check) {
  if (!existsSync(destination)) {
    console.error(`FAIL: ${destination} is missing. Run npm run build.`);
    process.exit(1);
  }
  const want = hashTree(source);
  const have = hashTree(destination);
  const problems = [];
  for (const [file, hash] of want) {
    if (!have.has(file)) problems.push(`missing: puerts/${file}`);
    else if (have.get(file) !== hash) problems.push(`differs from bundle: puerts/${file}`);
  }
  for (const file of have.keys()) {
    if (!want.has(file)) problems.push(`not in bundle: puerts/${file}`);
  }
  if (problems.length) {
    console.error('FAIL: staged puerts support files do not match the pinned bundle.');
    for (const p of problems.slice(0, 20)) console.error(`  ${p}`);
    console.error('Re-stage with: node Scripts/stage-puerts-runtime.mjs');
    process.exit(1);
  }
  console.log(`OK: staged puerts support files match the pinned bundle (${want.size} files).`);
  process.exit(0);
}

cpSync(source, destination, { recursive: true, force: true });
console.log(`Staged ${hashTree(destination).size} puerts support files into Plugins/MCPBridge/Content/JavaScript/puerts.`);
