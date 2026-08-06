#!/usr/bin/env node
/**
 * Regression test for Scripts/worktree-cleanup.mjs.
 *
 * The incident this guards: a worktree held a Windows junction aimed at the
 * main checkout, `rm -rf` followed it, and the DESTINATION was emptied. So the
 * assertion that matters is not "the worktree is gone" but "the worktree is
 * gone AND the thing it pointed at is untouched". A cleanup that deletes both
 * passes a naive test and reproduces the outage.
 *
 * Builds its own fixtures under the temp directory. No editor, no network, no
 * repository state. Runs on Windows with a real junction and on POSIX with a
 * symlink, because CI has both.
 *
 *   node Scripts/worktree-cleanup.test.mjs
 */
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import {
  existsSync, lstatSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, symlinkSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const script = join(dirname(fileURLToPath(import.meta.url)), 'worktree-cleanup.mjs');
let passed = 0;
const ok = (label) => { console.log(`  PASS  ${label}`); passed += 1; };

const run = (args, options = {}) => {
  try {
    return {
      status: 0,
      out: execFileSync(process.execPath, [script, ...args], { encoding: 'utf8', ...options }),
    };
  } catch (error) {
    return {
      status: error.status ?? 1,
      out: `${error.stdout ?? ''}${error.stderr ?? ''}`,
    };
  }
};

/** A directory junction on Windows, a directory symlink elsewhere.
    'junction' is the type that matters here: it is what AGENTS.md tells you to
    create for Plugins/Puerts, and unlike a symlink it needs no elevation, which
    is why it is everywhere in this repo's worktrees. */
function linkDirectory(linkPath, targetPath) {
  if (process.platform === 'win32') {
    symlinkSync(targetPath, linkPath, 'junction');
  } else {
    symlinkSync(targetPath, linkPath, 'dir');
  }
}

const root = mkdtempSync(join(tmpdir(), 'worktree-cleanup-'));
try {
  // ---------------------------------------------------------------- fixtures
  // precious/ stands in for the main checkout. It is OUTSIDE the worktree, and
  // nothing this script does may touch it.
  const precious = join(root, 'precious');
  mkdirSync(join(precious, 'Puerts'), { recursive: true });
  writeFileSync(join(precious, 'Puerts', 'pinned.txt'), 'the pinned bundle', 'utf8');
  writeFileSync(join(precious, 'sentinel.txt'), 'must survive', 'utf8');

  const worktree = join(root, 'worktree');
  mkdirSync(join(worktree, 'Plugins'), { recursive: true });
  mkdirSync(join(worktree, 'src'), { recursive: true });
  writeFileSync(join(worktree, 'src', 'disposable.txt'), 'ok to lose', 'utf8');

  // The junction: worktree/Plugins/Puerts -> precious/Puerts
  linkDirectory(join(worktree, 'Plugins', 'Puerts'), join(precious, 'Puerts'));

  assert.equal(lstatSync(join(worktree, 'Plugins', 'Puerts')).isSymbolicLink(), true,
    'fixture is not a reparse point; the rest of this test would prove nothing');
  assert.equal(
    readFileSync(join(worktree, 'Plugins', 'Puerts', 'pinned.txt'), 'utf8'),
    'the pinned bundle',
    'fixture link does not reach its destination',
  );
  ok('fixture: a directory junction inside the worktree reaches a destination outside it');

  // ------------------------------------------------------------------- audit
  const audit = run(['audit', worktree]);
  assert.equal(audit.status, 0);
  assert.match(audit.out, /reparse points: 1 \(1 pointing outside/,
    'audit did not report the outward link');
  assert.match(audit.out, /REFUSE: recursive deletion here would delete through/,
    'audit did not refuse recursive deletion');
  ok('audit finds the link, resolves it, and refuses recursive deletion');

  // -------------------------------------------------- delete without consent
  const dryRun = run(['delete', worktree]);
  assert.equal(dryRun.status, 0);
  assert.match(dryRun.out, /Re-run with --yes/, 'delete proceeded without --yes');
  assert.equal(existsSync(worktree), true, 'delete removed the worktree without --yes');
  ok('delete without --yes changes nothing on disk');

  // The dry run still unlinks, which is the point: prove that alone was safe.
  assert.equal(existsSync(join(precious, 'Puerts', 'pinned.txt')), true,
    'unlinking the junction destroyed its destination');
  ok('unlinking the junction leaves the destination intact');

  // ------------------------------------------------------------ full delete
  const deleted = run(['delete', worktree, '--yes']);
  assert.equal(deleted.status, 0, `delete failed: ${deleted.out}`);
  assert.match(deleted.out, /verified: 0 reparse points remain/,
    'delete did not verify the target was link-free first');

  assert.equal(existsSync(worktree), false, 'the worktree was not deleted');
  ok('the worktree directory is gone');

  // THE assertion. Everything above is setup for this one.
  assert.equal(existsSync(precious), true, 'the destination directory was deleted');
  assert.equal(readFileSync(join(precious, 'sentinel.txt'), 'utf8'), 'must survive',
    'the destination sentinel was modified or destroyed');
  assert.equal(readFileSync(join(precious, 'Puerts', 'pinned.txt'), 'utf8'), 'the pinned bundle',
    'the junction destination was deleted through the link');
  assert.deepEqual(readdirSync(join(precious, 'Puerts')), ['pinned.txt'],
    'the junction destination lost files');
  ok('the junction DESTINATION survives intact: the 2026-08-06 failure cannot recur');

  // ----------------------------------------------- refuses its own checkout
  const selfDelete = run(['delete', join(dirname(fileURLToPath(import.meta.url)), '..'), '--yes']);
  assert.equal(selfDelete.status, 1, 'the script agreed to delete its own checkout');
  assert.match(selfDelete.out, /REFUSE: .* is, or contains, this checkout/);
  ok('refuses to delete the checkout it is running from');

  // ------------------------------------------------------- unscoped restore
  const bare = run(['restore']);
  assert.equal(bare.status, 2);
  assert.match(bare.out, /A pathspec is required/);
  ok('restore with no pathspec is refused');

  for (const spec of ['.', './', '*']) {
    const unscoped = run(['restore', spec]);
    assert.equal(unscoped.status, 1, `restore accepted the unscoped pathspec ${spec}`);
    assert.match(unscoped.out, /REFUSE: .* is unscoped/);
  }
  ok('restore refuses ".", "./" and "*", the shapes that lost two files');

  // A dirty tree must block a scoped restore too, unless it is archived first.
  const dirtyRepo = mkdtempSync(join(tmpdir(), 'worktree-cleanup-repo-'));
  const git = (...args) => execFileSync('git', args, { cwd: dirtyRepo, encoding: 'utf8' });
  git('init', '--quiet');
  git('config', 'user.email', 'test@example.invalid');
  git('config', 'user.name', 'test');
  // Windows checkout would rewrite LF to CRLF on restore and fail the byte
  // comparison below for a reason that has nothing to do with what is tested.
  git('config', 'core.autocrlf', 'false');
  writeFileSync(join(dirtyRepo, 'tracked.txt'), 'committed\n', 'utf8');
  git('add', 'tracked.txt');
  git('commit', '--quiet', '-m', 'seed');
  writeFileSync(join(dirtyRepo, 'tracked.txt'), 'UNCOMMITTED WORK\n', 'utf8');

  const blocked = run(['restore', 'tracked.txt'], { cwd: dirtyRepo });
  assert.equal(blocked.status, 1, 'restore ran against a dirty tree with no archive');
  assert.match(blocked.out, /REFUSE: the working tree has uncommitted changes/);
  assert.equal(readFileSync(join(dirtyRepo, 'tracked.txt'), 'utf8'), 'UNCOMMITTED WORK\n',
    'the refused restore still overwrote the file');
  ok('a scoped restore against a dirty tree is refused, and changes nothing');

  const archived = run(['restore', 'tracked.txt', '--archive'], { cwd: dirtyRepo });
  assert.equal(archived.status, 0, `archived restore failed: ${archived.out}`);
  assert.equal(readFileSync(join(dirtyRepo, 'tracked.txt'), 'utf8'), 'committed\n',
    'the archived restore did not restore the file');
  assert.match(execFileSync('git', ['stash', 'list'], { cwd: dirtyRepo, encoding: 'utf8' }),
    /worktree-cleanup restore archive/,
    'the dirty work was not archived to a stash before the restore');
  ok('--archive stashes the dirty work first, so a restore is recoverable');

  rmSync(dirtyRepo, { recursive: true, force: true });
} finally {
  rmSync(root, { recursive: true, force: true });
}

console.log(`\n${passed} check(s) passed, 0 failed`);
