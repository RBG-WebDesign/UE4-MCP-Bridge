#!/usr/bin/env node
// Safe deletion of a git worktree directory, and safe restoration afterwards.
//
// WHY THIS EXISTS
//
// On 2026-08-06 a plain `rm -rf` over 27 orphaned worktree directories emptied
// the MAIN checkout: mcp-server/, Plugins/Puerts and both workspace
// node_modules. AGENTS.md tells you to link the pinned PuerTS bundle into each
// worktree with a Windows junction, so every worktree held a reparse point
// aimed at the main tree, and a recursive delete walked straight through it.
// The follow-up `git checkout -- .` then reverted two files of unrelated
// uncommitted work. See docs/incidents/2026-08-06-worktree-junction-delete.md.
//
// A junction is invisible to the thing that usually saves you. It is not a
// symlink in the POSIX sense, `du` reports the destination's size as if it were
// local, and Explorer shows it as an ordinary folder. The only reliable signal
// is lstat's reparse bit, which is what this checks.
//
//   node Scripts/worktree-cleanup.mjs audit   <dir>            report only
//   node Scripts/worktree-cleanup.mjs unlink  <dir>            remove links only
//   node Scripts/worktree-cleanup.mjs delete  <dir> [--yes]    unlink, verify, delete
//   node Scripts/worktree-cleanup.mjs restore <pathspec...>    scoped git restore
//
// Exit codes: 0 clean, 1 refused, 2 bad usage.

import { execFileSync } from 'node:child_process';
import {
  existsSync, lstatSync, readdirSync, readlinkSync, realpathSync, rmSync, rmdirSync, unlinkSync,
} from 'node:fs';
import { isAbsolute, join, relative, resolve } from 'node:path';

const [, , rawCommand, ...rest] = process.argv;
const command = rawCommand ?? 'help';
const flags = new Set(rest.filter((a) => a.startsWith('--')));
const positional = rest.filter((a) => !a.startsWith('--'));

const die = (code, message) => { console.error(message); process.exit(code); };
const norm = (p) => resolve(p).replace(/\\/g, '/').toLowerCase();

/** Is `child` inside `parent`? Path text only: both are already resolved, and
    resolving through the link is exactly what we must not do. */
function isInside(parent, child) {
  const rel = relative(resolve(parent), resolve(child));
  return rel !== '' && !rel.startsWith('..') && !isAbsolute(rel);
}

/** Every reparse point under `root`, found WITHOUT following any of them.
    readdir with withFileTypes reports a junction as a symbolic link, and the
    walk stops at one rather than descending: descending is the bug. */
function findReparsePoints(root) {
  const found = [];
  const walk = (dir) => {
    let entries;
    try { entries = readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const entry of entries) {
      const full = join(dir, entry.name);
      let stat;
      try { stat = lstatSync(full); } catch { continue; }
      if (stat.isSymbolicLink()) {
        let target = '(unreadable)';
        let resolved = null;
        try { target = readlinkSync(full); } catch { /* keep the placeholder */ }
        try { resolved = realpathSync(full); } catch { /* dangling link */ }
        found.push({ path: full, target, resolved, dangling: resolved === null });
        continue; // never descend through a link
      }
      if (stat.isDirectory()) walk(full);
    }
  };
  walk(root);
  return found;
}

/** Remove the LINK, never its destination.
    On Windows a directory junction is removed with rmdir, which unlinks it and
    leaves the target alone; unlink is for file links. Trying both in that order
    is what keeps this correct on Windows and POSIX without branching on
    process.platform. */
function removeLinkOnly(linkPath) {
  try { rmdirSync(linkPath); return true; } catch { /* try unlink */ }
  try { unlinkSync(linkPath); return true; } catch { return false; }
}

function describe(points, root) {
  for (const point of points) {
    const outside = point.resolved !== null && !isInside(root, point.resolved);
    const marker = point.dangling ? 'DANGLING' : outside ? 'OUTSIDE  ' : 'inside   ';
    console.log(`  ${marker}  ${point.path}`);
    console.log(`             -> ${point.target}`);
    if (point.resolved && point.resolved !== point.target) {
      console.log(`             =  ${point.resolved}`);
    }
  }
}

function auditDir(dir) {
  if (!existsSync(dir)) die(2, `no such directory: ${dir}`);
  const root = resolve(dir);
  const points = findReparsePoints(root);
  const outside = points.filter((p) => p.resolved !== null && !isInside(root, p.resolved));

  console.log(`target: ${root}`);
  console.log(`reparse points: ${points.length} (${outside.length} pointing outside the worktree)\n`);
  if (points.length) describe(points, root);
  return { root, points, outside };
}

// ------------------------------------------------------------------ commands

if (command === 'audit') {
  if (!positional[0]) die(2, 'usage: worktree-cleanup.mjs audit <dir>');
  const { outside } = auditDir(positional[0]);
  console.log(outside.length
    ? '\nREFUSE: recursive deletion here would delete through the links above.'
    : '\nNo outward links. Recursive deletion would stay inside the target.');
  process.exit(0);
}

if (command === 'unlink' || command === 'delete') {
  if (!positional[0]) die(2, `usage: worktree-cleanup.mjs ${command} <dir> [--yes]`);
  const { root, points } = auditDir(positional[0]);

  // Never operate on the repository this script lives in.
  const repoRoot = resolve(join(import.meta.dirname ?? '.', '..'));
  if (norm(root) === norm(repoRoot) || isInside(root, repoRoot)) {
    die(1, `\nREFUSE: ${root} is, or contains, this checkout (${repoRoot}).`);
  }

  let removed = 0;
  for (const point of points) {
    if (removeLinkOnly(point.path)) {
      removed += 1;
      console.log(`  unlinked  ${point.path}`);
      if (point.resolved && !existsSync(point.resolved)) {
        die(1, `\nSTOP: removing the link destroyed its destination ${point.resolved}. `
          + 'This should be impossible; do not continue.');
      }
    } else {
      die(1, `\nREFUSE: could not remove the link ${point.path}. Nothing else was touched.`);
    }
  }
  if (points.length) console.log(`\nremoved ${removed} reparse point(s); every destination still exists`);

  // Requirement 5: prove it, do not assume it.
  const remaining = findReparsePoints(root);
  if (remaining.length) {
    console.log('\nreparse points still present:');
    describe(remaining, root);
    die(1, 'REFUSE: the target still holds reparse points. Not deleting.');
  }
  console.log('verified: 0 reparse points remain under the target');

  if (command === 'unlink') process.exit(0);

  if (!flags.has('--yes')) {
    console.log(`\nWould delete ${root}. Re-run with --yes to proceed.`);
    process.exit(0);
  }
  rmSync(root, { recursive: true, force: true });
  console.log(`deleted ${root}`);
  process.exit(0);
}

if (command === 'restore') {
  // Requirement 8: an unscoped restore is the second half of the incident.
  const UNSCOPED = new Set(['.', './', '*', ':/', '']);
  if (positional.length === 0) {
    die(2, 'usage: worktree-cleanup.mjs restore <pathspec...>\n'
      + 'A pathspec is required. `git checkout -- .` and `git restore .` revert '
      + 'unrelated uncommitted work and are exactly what lost two files on 2026-08-06.');
  }
  for (const spec of positional) {
    if (UNSCOPED.has(spec)) {
      die(1, `REFUSE: "${spec}" is unscoped. Name the paths you mean to restore.`);
    }
  }

  // Requirement 7: a clean or archived tree before anything is overwritten.
  const dirty = execFileSync('git', ['status', '--porcelain'], { encoding: 'utf8' })
    .split(/\r?\n/).filter(Boolean)
    .filter((line) => !line.startsWith('??'));

  if (dirty.length && !flags.has('--archive')) {
    console.error('REFUSE: the working tree has uncommitted changes:\n');
    for (const line of dirty) console.error(`  ${line}`);
    console.error('\nA restore overwrites these without a copy anywhere. Either commit them,'
      + '\nor re-run with --archive to snapshot them to a git stash first.');
    process.exit(1);
  }
  if (dirty.length) {
    const label = `worktree-cleanup restore archive ${positional.join(' ')}`;
    execFileSync('git', ['stash', 'push', '--include-untracked', '-m', label], { stdio: 'inherit' });
    console.log('archived the dirty tree to a stash; recover it with `git stash pop`');
  }

  execFileSync('git', ['checkout', '--', ...positional], { stdio: 'inherit' });
  console.log(`restored: ${positional.join(', ')}`);
  process.exit(0);
}

die(2, `usage:
  worktree-cleanup.mjs audit   <dir>
  worktree-cleanup.mjs unlink  <dir>
  worktree-cleanup.mjs delete  <dir> [--yes]
  worktree-cleanup.mjs restore <pathspec...> [--archive]`);
