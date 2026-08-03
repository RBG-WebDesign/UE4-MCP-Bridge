#!/usr/bin/env node
// Verifies that the local PuerTS bundle at Plugins/Puerts matches the pinned
// checksum manifest at Plugins/Puerts.lock.json. The bundle is ignored by Git,
// so this lock file is the only recoverable record of what the bundle must
// contain: upstream tag Unreal_v1.0.9, one deliberate Build.cs modification,
// and the nodejs_16 third-party package.
//
// Usage:
//   node Scripts/check-puerts-pin.mjs            verify, skip if bundle absent
//   node Scripts/check-puerts-pin.mjs --strict   verify, fail if bundle absent
//   node Scripts/check-puerts-pin.mjs --bundle <dir>   verify a bundle that is
//                                                not at Plugins/Puerts
//   node Scripts/check-puerts-pin.mjs --write    regenerate the lock file from
//                                                the current bundle (only after
//                                                a deliberate, reviewed change)

import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync, readdirSync, statSync, existsSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const lockPath = join(repoRoot, 'Plugins', 'Puerts.lock.json');

const EXCLUDED_DIRS = new Set(['Binaries', 'Intermediate', 'Saved']);

const args = process.argv.slice(2);
const strict = args.includes('--strict');
const write = args.includes('--write');
// --bundle <dir> verifies a bundle that is not at Plugins/Puerts: a worktree,
// or the tree Scripts/package-mcp-bridge.ps1 is about to vendor into a release.
const bundleArg = args.indexOf('--bundle');
const bundleDir = bundleArg >= 0 ? args[bundleArg + 1] : join(repoRoot, 'Plugins', 'Puerts');

function listFiles(root) {
  const out = [];
  const visit = (dir) => {
    for (const entry of readdirSync(dir, { withFileTypes: true })) {
      const full = join(dir, entry.name);
      if (entry.isDirectory()) {
        if (dir === root && EXCLUDED_DIRS.has(entry.name)) continue;
        visit(full);
      } else if (entry.isFile()) {
        out.push(relative(root, full).replaceAll('\\', '/'));
      }
    }
  };
  visit(root);
  return out.sort();
}

function sha256(path) {
  return createHash('sha256').update(readFileSync(path)).digest('hex');
}

function computeManifestFiles() {
  const files = {};
  for (const rel of listFiles(bundleDir)) {
    files[rel] = sha256(join(bundleDir, rel));
  }
  return files;
}

if (!existsSync(join(bundleDir, 'Puerts.uplugin'))) {
  const message = `PuerTS bundle not found at ${bundleDir}.`;
  if (strict) {
    console.error(`FAIL: ${message} The installer requires the pinned bundle. See docs/PUERTS.md.`);
    process.exit(1);
  }
  console.warn(`SKIP: ${message} Pin check skipped (pass --strict to require it).`);
  process.exit(0);
}

if (write) {
  const files = computeManifestFiles();
  const lock = {
    upstream: {
      repository: 'https://github.com/Tencent/puerts',
      tag: 'Unreal_v1.0.9',
      commit: '838ab762d83021c0407f13120f4004dcaf70cffe',
      subtree: 'unreal/Puerts',
    },
    verified: {
      date: '2026-07-31',
      method:
        'Full-tree comparison against a sparse clone of the upstream tag, CRLF-normalized. ' +
        '945 of 947 upstream files byte-identical; see local_modifications and missing_from_upstream.',
    },
    local_modifications: [
      'Source/JsEnv/JsEnv.Build.cs: line "private bool UseNodejs = false;" changed to "= true;" (Node.js backend instead of plain V8)',
    ],
    local_additions:
      'ThirdParty/nodejs_16/** (93 files): separately distributed PuerTS Node.js 16 dependency package (libuv 1.43 headers, V8/cppgc headers, Win64 libnode.dll and libnode.lib)',
    missing_from_upstream: [
      'Content/JavaScript/react-umg/index.js (React-UMG deliberately not installed)',
      'ThirdParty/Libnode_APL.xml',
    ],
    excluded_directories: [...EXCLUDED_DIRS],
    file_count: Object.keys(files).length,
    files,
  };
  writeFileSync(lockPath, JSON.stringify(lock, null, 2) + '\n');
  console.log(`Wrote ${lockPath} with ${lock.file_count} file hashes.`);
  process.exit(0);
}

if (!existsSync(lockPath)) {
  console.error(`FAIL: ${lockPath} does not exist. Generate it with --write after reviewing the bundle.`);
  process.exit(1);
}

const lock = JSON.parse(readFileSync(lockPath, 'utf8'));
const actual = computeManifestFiles();
const expected = lock.files;

const missing = Object.keys(expected).filter((f) => !(f in actual));
const added = Object.keys(actual).filter((f) => !(f in expected));
const changed = Object.keys(expected).filter((f) => f in actual && actual[f] !== expected[f]);

if (missing.length || added.length || changed.length) {
  console.error(`FAIL: Plugins/Puerts does not match Puerts.lock.json (pinned ${lock.upstream.tag} @ ${lock.upstream.commit.slice(0, 12)}).`);
  for (const f of missing.slice(0, 20)) console.error(`  missing: ${f}`);
  for (const f of added.slice(0, 20)) console.error(`  added:   ${f}`);
  for (const f of changed.slice(0, 20)) console.error(`  changed: ${f}`);
  const shown = Math.min(missing.length, 20) + Math.min(added.length, 20) + Math.min(changed.length, 20);
  const total = missing.length + added.length + changed.length;
  if (total > shown) console.error(`  ...and ${total - shown} more.`);
  console.error('If this change is deliberate, review it and regenerate with --write.');
  process.exit(1);
}

console.log(`OK: Plugins/Puerts matches the pin (${lock.upstream.tag} @ ${lock.upstream.commit.slice(0, 12)}, ${lock.file_count} files).`);
