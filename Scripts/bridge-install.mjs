#!/usr/bin/env node
// Install gate for live acceptance. Answers one question before Unreal starts:
// is the plugin inside this test project the plugin in this checkout?
//
// It exists because the answer was no and nothing said so. Two UE4.27 projects
// carried an MCPBridge copy, only one was current, and a live acceptance run
// against the stale one would have passed while proving nothing about the code
// under review. Nothing compared them, so the staleness was invisible until
// someone read a file timestamp by hand.
//
// The gate is deliberately not a timestamp comparison. UBT reported "Target is
// up to date" for a project whose plugin sources were a day old, because from
// its point of view they were: it compares each project's own sources against
// its own object files and has no idea a repository exists. Content is the only
// thing that settles it.
//
//   node Scripts/bridge-install.mjs --check --project <path>
//   node Scripts/bridge-install.mjs --sync  --project <path> [--no-build]
//
// --check NEVER writes to the target project. That is the whole point of having
// a separate verb: a verification run that quietly repairs what it was asked to
// measure cannot fail, and a gate that cannot fail is not a gate.

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import {
  copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync,
} from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const MANIFEST_NAME = 'MCPBridgeInstall.json';

// Schema version of MCPBridgeInstall.json. Same rule as session.json: a manifest
// whose version this checkout does not recognise is refused, not read
// optimistically. The manifest decides two things that matter - whether the
// target is deprecated, and which DLL was built at install time - so reading an
// unknown shape means silently skipping both while reporting a pass. Bump this
// whenever a field's MEANING changes; adding a field nothing reads does not
// need a bump.
export const MANIFEST_SCHEMA_VERSION = 1;
const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
const SKIP_DIRS = new Set(['Binaries', 'Intermediate', 'Saved', '.vs']);

// The declared plugin files, split so that a stale group names itself. Native
// sources and Content/JavaScript are separate on purpose: they come from
// different producers - UBT compiles one, tsc and the PuerTS staging step
// generate the other - so either can be current while the other rots, and a
// single "plugin hash" would report that as one undifferentiated mismatch.
const GROUPS = [
  { key: 'uplugin', match: (p) => p === 'MCPBridge.uplugin' },
  { key: 'build_cs', match: (p) => p.startsWith('Source/') && p.endsWith('.Build.cs') },
  { key: 'native_source', match: (p) => p.startsWith('Source/') && !p.endsWith('.Build.cs') },
  { key: 'content_javascript', match: (p) => p.startsWith('Content/JavaScript/') },
  { key: 'content_other', match: (p) => p.startsWith('Content/') && !p.startsWith('Content/JavaScript/') },
  { key: 'resources', match: (p) => p.startsWith('Resources/') },
  // Catch-all, last. The installer copies the whole plugin directory, so a file
  // matching no named group above still has to be synced; landing here means it
  // is carried, just not called out by name in the report.
  { key: 'plugin_misc', match: () => true },
];

const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');

// skipAtRootOnly matches how Plugins/Puerts.lock.json was generated
// (Scripts/check-puerts-pin.mjs excludes Binaries/Intermediate/Saved only at the
// bundle root). Walking with a different exclusion rule than the manifest was
// written with reports pin mismatches that are an artefact of this script.
function walk(root, { skipAtRootOnly = false } = {}, out = [], prefix = '') {
  if (!existsSync(root)) return out;
  for (const entry of readdirSync(root, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      if (SKIP_DIRS.has(entry.name) && (!skipAtRootOnly || prefix === '')) continue;
      walk(join(root, entry.name), { skipAtRootOnly }, out, prefix ? `${prefix}/${entry.name}` : entry.name);
    } else if (entry.isFile()) {
      out.push(prefix ? `${prefix}/${entry.name}` : entry.name);
    }
  }
  return out;
}

function groupFiles(pluginDir) {
  const files = walk(pluginDir).sort();
  const groups = new Map(GROUPS.map((g) => [g.key, new Map()]));
  const undeclared = [];
  for (const rel of files) {
    if (rel === MANIFEST_NAME) continue; // the manifest describes the install, it is not part of it
    const group = GROUPS.find((g) => g.match(rel));
    groups.get(group.key).set(rel, sha256(readFileSync(join(pluginDir, rel))));
  }
  return { groups };
}

const groupHash = (files) =>
  sha256([...files.entries()].sort(([a], [b]) => (a < b ? -1 : 1)).map(([f, h]) => `${f}:${h}`).join('\n'));

function diffGroup(want, have) {
  const problems = [];
  for (const [file, hash] of want) {
    if (!have.has(file)) problems.push(`missing: ${file}`);
    else if (have.get(file) !== hash) problems.push(`differs: ${file}`);
  }
  for (const file of have.keys()) if (!want.has(file)) problems.push(`extra: ${file}`);
  return problems;
}

// The pinned bundle, verified where it actually has to be correct: inside the
// target project. Scripts/check-puerts-pin.mjs verifies the repository copy and
// npm run verify runs it; neither says anything about what a test project is
// loading, and the editor loads the target's copy.
function puertsState(root) {
  const lockPath = join(repoRoot, 'Plugins', 'Puerts.lock.json');
  if (!existsSync(lockPath)) return { pin: 'no lock file', problems: ['Plugins/Puerts.lock.json is missing'] };
  const lock = JSON.parse(readFileSync(lockPath, 'utf8'));
  const pin = `${lock.upstream?.tag ?? 'unknown'}@${(lock.upstream?.commit ?? '').slice(0, 12)}`;
  const bundle = join(root, 'Plugins', 'Puerts');
  if (!existsSync(bundle)) return { pin, problems: [`PuerTS bundle is missing at ${bundle}`], problem_count: 1 };
  const want = new Map(Object.entries(lock.files ?? {}));
  const have = new Map(walk(bundle, { skipAtRootOnly: true })
    .map((rel) => [rel, sha256(readFileSync(join(bundle, rel)))]));

  // A pinned file that is absent or altered is fatal: the editor may then load
  // something other than the verified bundle, which is the whole reason the pin
  // exists. An EXTRA file is a different claim and gets a different answer.
  //
  // Measured on Tests/UE427PuerTSMCP: all 1038 pinned files byte-identical, plus
  // 63 files the repository bundle does not carry - ThirdParty/Libnode_APL.xml
  // and Android static libraries under ThirdParty/nodejs_16/lib. Those are
  // upstream's own files from the same tag; that bundle is simply a more
  // complete extraction. They cannot change what the pinned files do on a Win64
  // editor, and the only way to make them go away is deleting 63 files out of
  // someone's project directory, which a verification gate has no business
  // doing on its own. So they are named and counted, not treated as a violation.
  //
  // The exception is where an extra file WOULD be loaded. Content/ holds the
  // JavaScript the runtime executes and Binaries/ holds the DLLs the editor
  // links; an unpinned file in either is fatal, wherever it came from.
  const missingOrAltered = [];
  for (const [file, hash] of want) {
    if (!have.has(file)) missingOrAltered.push(`missing: ${file}`);
    else if (have.get(file) !== hash) missingOrAltered.push(`altered: ${file}`);
  }
  const extra = [...have.keys()].filter((f) => !want.has(f));
  const loadable = extra.filter((f) => f.startsWith('Content/') || f.startsWith('Binaries/'));
  const problems = [
    ...missingOrAltered,
    ...loadable.map((f) => `unpinned file in a loaded directory: ${f}`),
  ];
  return {
    pin,
    file_count: want.size,
    extra_inert_files: extra.length,
    extra_inert_sample: extra.slice(0, 5),
    problems: problems.slice(0, 20),
    problem_count: problems.length,
  };
}

function dllHashes(pluginDir) {
  const dir = join(pluginDir, 'Binaries', 'Win64');
  if (!existsSync(dir)) return {};
  const out = {};
  for (const name of readdirSync(dir).sort()) {
    if (!name.startsWith('UE4Editor-MCPBridge') || !name.endsWith('.dll')) continue;
    out[name] = sha256(readFileSync(join(dir, name)));
  }
  return out;
}

function newestSourceMtime(pluginDir) {
  let newest = 0;
  let newestFile = null;
  for (const rel of walk(pluginDir)) {
    if (!rel.startsWith('Source/')) continue;
    const m = statSync(join(pluginDir, rel)).mtimeMs;
    if (m > newest) { newest = m; newestFile = rel; }
  }
  return { mtime: newest, file: newestFile };
}

// Binaries is in SKIP_DIRS, so this cannot go through walk(). It did, and the
// rule below silently never fired: newest DLL mtime was always 0 and the guard
// short-circuited. The gate's own acceptance caught it by breaking a case that
// only this rule covers.
function newestDllMtime(pluginDir) {
  const dir = join(pluginDir, 'Binaries', 'Win64');
  if (!existsSync(dir)) return { mtime: 0, file: null };
  let newest = 0;
  let newestFile = null;
  for (const name of readdirSync(dir)) {
    if (!name.endsWith('.dll')) continue;
    const m = statSync(join(dir, name)).mtimeMs;
    if (m > newest) { newest = m; newestFile = `Binaries/Win64/${name}`; }
  }
  return { mtime: newest, file: newestFile };
}

// The catalog the in-editor runtime actually exposes, read out of the installed
// JavaScript rather than the repository's. A hash match already proves the file
// is the right one; this proves the right file exposes the tools the MCP server
// advertises, which is the property a caller depends on and the one that breaks
// silently when only one lane is rebuilt.
function catalogNames(registryJs) {
  return [...registryJs.matchAll(/\{\s*name:\s*"([a-z0-9_]+)"/g)].map((m) => m[1]).sort();
}

function expectedCatalog() {
  const puertsTools = join(repoRoot, 'mcp-server', 'dist', 'tools', 'puerts.js');
  if (!existsSync(puertsTools)) return null;
  // Each MCP adapter is declared as ["puerts_<mcp name>", "<native command>", ...].
  // The native command is the second element and is what the registry keys on.
  const text = readFileSync(puertsTools, 'utf8');
  return [...text.matchAll(/\["puerts_[a-z0-9_]+",\s*"([a-z0-9_]+)"/g)].map((m) => m[1]).sort();
}

function gitState() {
  const git = (args) => {
    try {
      return execFileSync('git', args, { cwd: repoRoot, encoding: 'utf8' }).trim();
    } catch { return ''; }
  };
  const status = git(['status', '--porcelain']);
  return {
    commit: git(['rev-parse', 'HEAD']) || 'unknown',
    branch: git(['rev-parse', '--abbrev-ref', 'HEAD']) || 'unknown',
    dirty: status.length > 0,
    dirty_files: status ? status.split('\n').map((l) => l.trim()).slice(0, 40) : [],
  };
}

function resolveProject(explicit) {
  const root = explicit || process.env.MCP_UNREAL_PROJECT_ROOT;
  if (!root) throw new Error('Pass --project <path> or set MCP_UNREAL_PROJECT_ROOT');
  if (!existsSync(join(root, 'Plugins', 'MCPBridge'))) {
    throw new Error(`No MCPBridge plugin at ${join(root, 'Plugins', 'MCPBridge')}`);
  }
  return root;
}

/**
 * Compare a target project's installed plugin against this checkout.
 * Reads only. Returns { ok, problems, report } and never touches the project.
 */
export function checkInstall(projectRoot) {
  const root = resolveProject(projectRoot);
  const pluginDir = join(root, 'Plugins', 'MCPBridge');
  const manifestPath = join(pluginDir, MANIFEST_NAME);
  const problems = [];
  const stale = [];

  const manifest = existsSync(manifestPath) ? JSON.parse(readFileSync(manifestPath, 'utf8')) : null;
  if (!manifest) {
    problems.push(
      `${root} has no ${MANIFEST_NAME}: it is an unmanaged plugin copy of unknown provenance. ` +
      'Run npm run install:sync -- --project <path> to make it a verified target, or stop using it for acceptance.');
  }
  // Version before content. Everything below that reads a manifest FIELD is
  // gated on this, because a field read out of a shape this checkout does not
  // know is a guess, and the two fields in question (status, dll_hashes) are
  // both cases where guessing means passing an install that should be refused.
  const found = manifest?.schema_version;
  const manifestTrusted = manifest !== null && found === MANIFEST_SCHEMA_VERSION;
  if (manifest && !manifestTrusted) {
    if (!Number.isInteger(found)) {
      problems.push(
        `manifest_schema: ${MANIFEST_NAME} in ${root} has no integer schema_version, so it was written ` +
        'before the manifest was versioned. Its fields are not read. Re-run ' +
        'npm run install:sync -- --project <path> to rewrite it.');
    } else if (found < MANIFEST_SCHEMA_VERSION) {
      problems.push(
        `manifest_schema: ${MANIFEST_NAME} in ${root} is schema version ${found}; this checkout writes ` +
        `${MANIFEST_SCHEMA_VERSION}. Re-run npm run install:sync -- --project <path>, which rewrites the ` +
        'manifest at the current version.');
    } else {
      problems.push(
        `manifest_schema: ${MANIFEST_NAME} in ${root} is schema version ${found}, which is NEWER than the ` +
        `${MANIFEST_SCHEMA_VERSION} this checkout understands. The target was installed from a newer bridge ` +
        'checkout; syncing from here would move it backwards. Update this checkout instead.');
    }
  }
  if (manifestTrusted && manifest.status === 'deprecated') {
    problems.push(`${root} is marked deprecated in its manifest and is blocked from acceptance: ${manifest.deprecated_reason ?? 'no reason recorded'}`);
  }

  // Repository against install, computed live from both sides. The manifest is
  // provenance, not evidence: comparing the install to what the manifest CLAIMS
  // it contains would pass for any install whose manifest was written from it.
  const want = groupFiles(join(repoRoot, 'Plugins', 'MCPBridge'));
  const have = groupFiles(pluginDir);
  const report = { project: root, repository: repoRoot, groups: {} };
  for (const { key } of GROUPS) {
    const diffs = diffGroup(want.groups.get(key), have.groups.get(key));
    report.groups[key] = {
      files: want.groups.get(key).size,
      repository_hash: groupHash(want.groups.get(key)),
      installed_hash: groupHash(have.groups.get(key)),
      differences: diffs.slice(0, 20),
      difference_count: diffs.length,
    };
    if (diffs.length) {
      stale.push(key);
      problems.push(`${key}: ${diffs.length} file(s) differ from the repository [${diffs.slice(0, 5).join('; ')}${diffs.length > 5 ? '; ...' : ''}]`);
    }
  }

  const puerts = puertsState(root);
  report.puerts = puerts;
  if (puerts.problems.length) {
    problems.push(`puerts_pin: the installed bundle violates Plugins/Puerts.lock.json (${puerts.problem_count} file(s)) [${puerts.problems.slice(0, 3).join('; ')}]`);
  }

  // The DLL, bound to its sources by having been built during sync from exactly
  // the files the manifest recorded. Two ways it goes stale and both are caught:
  // the binary is swapped (hash moves off the recorded one) or the sources were
  // synced without rebuilding (sources end up newer than the binary).
  const dlls = dllHashes(pluginDir);
  const recordedDlls = manifestTrusted ? manifest.dll_hashes : null;
  report.dll = { installed: dlls, recorded: recordedDlls ?? null };
  if (Object.keys(dlls).length === 0) {
    problems.push('dll: no UE4Editor-MCPBridge*.dll in Binaries/Win64; the target project has never been built');
  } else if (recordedDlls) {
    for (const [name, hash] of Object.entries(recordedDlls)) {
      if (!dlls[name]) problems.push(`dll: ${name} recorded at install is missing`);
      else if (dlls[name] !== hash) problems.push(`dll: ${name} is not the binary built at install time`);
    }
  }
  const newestSource = newestSourceMtime(pluginDir);
  const newestDll = newestDllMtime(pluginDir);
  report.dll.newest_source = newestSource.file;
  report.dll.newest_dll = newestDll.file;
  if (newestSource.mtime && newestDll.mtime && newestSource.mtime > newestDll.mtime) {
    problems.push(`dll: ${newestSource.file} is newer than every built DLL; the project was synced but not rebuilt`);
  }

  const registryPath = join(pluginDir, 'Content', 'JavaScript', 'registry.js');
  if (!existsSync(registryPath)) {
    problems.push('content_javascript: registry.js is missing from the installed Content/JavaScript');
  } else {
    const installed = catalogNames(readFileSync(registryPath, 'utf8'));
    const expected = expectedCatalog();
    report.catalog = { installed_count: installed.length, expected_count: expected?.length ?? null };
    if (!expected) {
      problems.push('catalog: mcp-server/dist is not built, so the expected catalog is unknown. Run npm run build');
    } else {
      const missing = expected.filter((n) => !installed.includes(n));
      const extra = installed.filter((n) => !expected.includes(n));
      report.catalog.missing = missing;
      report.catalog.extra = extra;
      if (missing.length || extra.length) {
        problems.push(`catalog: the installed registry does not expose the MCP server's native catalog (missing: ${missing.join(', ') || 'none'}; extra: ${extra.join(', ') || 'none'})`);
      }
    }
  }

  report.stale_groups = stale;
  report.manifest = manifest;
  report.manifest_schema = { found: found ?? null, supported: MANIFEST_SCHEMA_VERSION, trusted: manifestTrusted };
  return { ok: problems.length === 0, problems, report };
}

function buildTarget(root) {
  const uproject = readdirSync(root).find((f) => f.endsWith('.uproject'));
  if (!uproject) throw new Error(`No .uproject in ${root}`);
  const target = `${uproject.replace(/\.uproject$/, '')}Editor`;
  const batch = join(process.env.UE_ENGINE_ROOT || 'D:/UE/UE_4.27', 'Engine', 'Build', 'BatchFiles', 'Build.bat');
  if (!existsSync(batch)) throw new Error(`UBT not found at ${batch}. Set UE_ENGINE_ROOT`);
  console.log(`  building ${target} Win64 Development`);
  // Through cmd.exe: Node refuses to spawn a .bat directly (EINVAL) since the
  // batch-argument-injection fix, and UBT's entry point is Build.bat.
  execFileSync('cmd.exe',
    ['/c', batch, target, 'Win64', 'Development', join(root, uproject), '-WaitMutex', '-NoHotReload'],
    { stdio: 'inherit' });
  return target;
}

export function syncInstall(projectRoot, { build = true } = {}) {
  const root = resolveProject(projectRoot);
  const pluginDir = join(root, 'Plugins', 'MCPBridge');
  const sourceDir = join(repoRoot, 'Plugins', 'MCPBridge');
  const want = groupFiles(sourceDir);

  // Copy only what actually differs. Not an optimisation: copyFileSync stamps
  // the destination with the current time whether or not the bytes changed, so
  // a blanket copy makes every installed source newer than a DLL that UBT then
  // correctly declines to rebuild, and the "synced but not rebuilt" rule fires
  // on an install that is perfectly current.
  const installedNow = groupFiles(pluginDir);
  let copied = 0;
  const perGroup = {};
  for (const { key } of GROUPS) {
    const files = want.groups.get(key);
    const present = installedNow.groups.get(key);
    perGroup[key] = files.size;
    for (const [rel, hash] of files) {
      if (present.get(rel) === hash) continue;
      const dest = join(pluginDir, rel);
      mkdirSync(dirname(dest), { recursive: true });
      copyFileSync(join(sourceDir, rel), dest);
      copied++;
    }
  }
  // Only declared files are copied, so a file deleted from the repository would
  // linger in the target and still be loaded. Removing it is the point of
  // reporting it, but deleting from a project directory is not something a sync
  // should do silently, so it is named and left.
  const have = groupFiles(pluginDir);
  const orphans = [];
  for (const { key } of GROUPS) {
    for (const rel of have.groups.get(key).keys()) {
      if (!want.groups.get(key).has(rel)) orphans.push(rel);
    }
  }
  console.log(`  copied ${copied} changed file(s) of ${Object.values(perGroup).reduce((a, b) => a + b, 0)} declared: ${Object.entries(perGroup).map(([k, v]) => `${k}=${v}`).join(' ')}`);
  if (orphans.length) {
    console.log(`  WARNING  ${orphans.length} installed file(s) are not in the repository and were left in place:`);
    for (const o of orphans.slice(0, 10)) console.log(`             ${o}`);
    console.log('           Delete them by hand if they are stale; sync will not remove project files.');
  }

  if (build) buildTarget(root);
  else console.log('  --no-build: the DLL is NOT rebuilt, so install:check will reject this install until it is');

  const git = gitState();
  const manifest = {
    schema_version: MANIFEST_SCHEMA_VERSION,
    generated_by: 'Scripts/bridge-install.mjs',
    status: 'active',
    installed_at: new Date().toISOString(),
    source_repository: repoRoot.replaceAll('\\', '/'),
    target_project: root.replaceAll('\\', '/'),
    bridge_commit: git.commit,
    bridge_branch: git.branch,
    bridge_tree_dirty: git.dirty,
    bridge_dirty_files: git.dirty_files,
    puerts_pin: puertsState(root).pin,
    group_hashes: Object.fromEntries(GROUPS.map(({ key }) => [key, groupHash(want.groups.get(key))])),
    group_file_counts: perGroup,
    dll_hashes: dllHashes(pluginDir),
    orphaned_files: orphans,
  };
  writeFileSync(join(pluginDir, MANIFEST_NAME), JSON.stringify(manifest, null, 2) + '\n');
  console.log(`  wrote ${join(pluginDir, MANIFEST_NAME)}`);
  return manifest;
}

/**
 * The one line a live acceptance script needs: resolve the target project and
 * refuse to go near the editor unless its plugin is this checkout's plugin.
 * Set MCP_SKIP_INSTALL_CHECK=1 only to debug the gate itself; it is announced
 * loudly because a skipped gate that nobody notices is the failure mode this
 * whole file exists to remove.
 */
export function requireCurrentInstall() {
  const root = process.env.MCP_UNREAL_PROJECT_ROOT;
  if (!root) throw new Error('Set MCP_UNREAL_PROJECT_ROOT to the test project root');
  if (process.env.MCP_SKIP_INSTALL_CHECK === '1') {
    console.log(`  WARNING  MCP_SKIP_INSTALL_CHECK=1: the install gate is OFF for ${root}.`);
    console.log('           Results prove nothing about this checkout unless the install is current.');
    return root;
  }
  assertInstallCurrent(root);
  return root;
}

/** Gate for a live acceptance script. Throws with the reasons if the install is stale. */
export function assertInstallCurrent(projectRoot) {
  const { ok, problems } = checkInstall(projectRoot);
  if (ok) return;
  throw new Error(
    `The MCPBridge install in ${projectRoot} does not match this checkout, so a live run would prove ` +
    `nothing about the code under review:\n  - ${problems.join('\n  - ')}\n` +
    'Run: npm run install:sync -- --project <path>');
}

// pathToFileURL, not a hand-built `file://${path}`: import.meta.url
// percent-encodes the spaces in "D:\Unreal Projects", so the hand-built form
// never matched on Windows and only the endsWith() fallback below it was ever
// doing the work. That fallback fired on ANY argv[1] ending in the same
// basename, so it is gone too.
if (process.argv[1]
  && pathToFileURL(process.argv[1]).href.toLowerCase() === import.meta.url.toLowerCase()) {
  const argv = process.argv.slice(2);
  const at = argv.indexOf('--project');
  const project = at >= 0 ? argv[at + 1] : undefined;
  const wantSync = argv.includes('--sync');

  try {
    if (wantSync) {
      console.log('bridge install sync');
      syncInstall(project, { build: !argv.includes('--no-build') });
      console.log('\nre-checking after sync');
      const { ok, problems, report } = checkInstall(project);
      for (const p of problems) console.log(`  FAIL  ${p}`);
      if (ok) console.log(`  PASS  install matches the repository at ${report.manifest.bridge_commit.slice(0, 7)}`);
      process.exit(ok ? 0 : 1);
    }
    const { ok, problems, report } = checkInstall(project);
    console.log(`bridge install check\n  repository: ${report.repository}\n  project:    ${report.project}`);
    for (const { key } of GROUPS) {
      const g = report.groups[key];
      console.log(`  ${g.difference_count ? 'STALE' : ' OK  '}  ${key.padEnd(19)} ${g.files} file(s)  ${g.repository_hash.slice(0, 12)}`);
    }
    // Gated on trusted, not on presence: an untrusted manifest is refused whole
    // by checkInstall, and printing a field out of one is the same guess it
    // refuses to make. A pre-versioning manifest has no bridge_commit at all, so
    // reading it here threw and the manifest_schema refusal below never printed.
    if (report.manifest && report.manifest_schema.trusted) {
      console.log(`  installed from ${report.manifest.bridge_commit.slice(0, 7)}${report.manifest.bridge_tree_dirty ? ' (dirty tree)' : ''} at ${report.manifest.installed_at}`);
    }
    for (const p of problems) console.log(`  FAIL  ${p}`);
    // No blanket remedy here. Every problem states its own fix, and one of them
    // is "update this checkout, do NOT sync" for a target installed from a newer
    // bridge. A summary line telling everyone to sync contradicted that refusal
    // on the one path where syncing moves the target backwards.
    console.log(ok ? '\ninstall is current' : `\n${problems.length} problem(s); each FAIL above names its fix`);
    process.exit(ok ? 0 : 1);
  } catch (error) {
    console.error(`bridge install: ${error.message}`);
    process.exit(1);
  }
}
