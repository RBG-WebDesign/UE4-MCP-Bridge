#!/usr/bin/env node
/**
 * Regression test for Scripts/check-git-hygiene.mjs.
 *
 * The checker's first draft reported 48 errors, ~40 of which were one fact:
 * two commits shared as ancestors by every lane branch. A checker whose output
 * is mostly noise gets ignored, which is worse than no checker, so the
 * deduplication is the behaviour most worth pinning.
 *
 * Four cases, per the review that asked for this:
 *   1. duplicate commit reporting  - one finding per commit, not per branch
 *   2. superseded commits          - removed from the report entirely
 *   3. truly unique commits        - reported, and NOT hidden by the above
 *   4. no novel content            - branch reported as redundant
 *
 * Plus the protected-branch severity split, which is what lets the checker
 * reach zero errors while work is deliberately parked on a checkpoint branch.
 *
 * Builds a throwaway repository under the temp directory. No network, no
 * editor, no dependence on this repository's own branch state, which matters:
 * a test that asserted against the real repo would pass or fail based on
 * whatever branches happen to exist today.
 *
 *   node Scripts/check-git-hygiene.test.mjs
 */
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const script = join(dirname(fileURLToPath(import.meta.url)), 'check-git-hygiene.mjs');
let passed = 0;
const ok = (label) => { console.log(`  PASS  ${label}`); passed += 1; };

const repo = mkdtempSync(join(tmpdir(), 'git-hygiene-'));
const git = (...args) => execFileSync('git', args, { cwd: repo, encoding: 'utf8' }).trim();
const commit = (file, body, message) => {
  writeFileSync(join(repo, file), body, 'utf8');
  git('add', file);
  git('commit', '--quiet', '-m', message);
  return git('rev-parse', 'HEAD');
};

/** Run the checker against the fixture repo. It exits 1 when errors exist, so
    the failure path is expected and its stdout still has to be read. */
function runChecker() {
  try {
    return { status: 0, out: execFileSync(process.execPath, [script, '--json'], { cwd: repo, encoding: 'utf8' }) };
  } catch (error) {
    return { status: error.status ?? 1, out: (error.stdout ?? '').toString() };
  }
}
const report = () => {
  const { out } = runChecker();
  return JSON.parse(out);
};
const findingsOf = (json, check) => json.findings.filter((f) => f.check === check);

try {
  git('init', '--quiet', '-b', 'main');
  git('config', 'user.email', 'test@example.invalid');
  git('config', 'user.name', 'test');
  git('config', 'core.autocrlf', 'false');

  commit('base.txt', 'base\n', 'seed');

  // --------------------------------------------------------------- case 4
  // A branch whose commits are all on main by patch-id. Created by committing
  // the SAME change on both sides, which is how the lane workflow produced
  // branches that shared no commit ids with main while holding nothing new.
  git('branch', 'redundant-branch');
  const onMain = commit('shared.txt', 'shared content\n', 'add shared');
  git('checkout', '--quiet', 'redundant-branch');
  commit('shared.txt', 'shared content\n', 'add shared (replayed)');
  git('checkout', '--quiet', 'main');

  assert.notEqual(git('rev-parse', 'redundant-branch'), onMain,
    'fixture is wrong: the replayed commit must have a different sha');

  let json = report();
  assert.equal(findingsOf(json, 'redundant-branch').length, 1,
    'a branch with no novel content should be reported redundant exactly once');
  assert.ok(json.redundant_branches.includes('redundant-branch'));
  assert.equal(findingsOf(json, 'unrepresented-commit').length, 0,
    'a patch-identical commit must not be reported as unrepresented');
  ok('case 4: a branch whose commits are all upstream by patch-id is redundant, not unrepresented');

  // --------------------------------------------------------------- case 3
  // A genuinely unique commit on one ordinary branch.
  git('checkout', '--quiet', '-b', 'unique-branch');
  const uniqueSha = commit('only-here.txt', 'novel\n', 'genuinely unique work');
  git('checkout', '--quiet', 'main');

  json = report();
  const unrepresented = findingsOf(json, 'unrepresented-commit');
  assert.equal(unrepresented.length, 1, 'the unique commit should produce exactly one error');
  assert.equal(unrepresented[0].level, 'error');
  assert.ok(unrepresented[0].message.includes(uniqueSha.slice(0, 8)),
    'the finding must name the commit, not just the branch');
  ok('case 3: a truly unique commit on an ordinary branch is one error naming the commit');

  // --------------------------------------------------------------- case 1
  // The regression that mattered. Ten branches sharing one unmerged ancestor
  // must be ONE finding that names them, not ten findings.
  for (let index = 0; index < 10; index += 1) {
    git('branch', `carrier-${index}`, 'unique-branch');
  }
  json = report();
  const deduped = findingsOf(json, 'unrepresented-commit');
  assert.equal(deduped.length, 1,
    `11 branches share one unmerged commit; expected 1 finding, got ${deduped.length}`);
  assert.ok(/on 11 branch\(es\)/.test(deduped[0].message),
    `the single finding must report the carrier count, got: ${deduped[0].message}`);
  ok('case 1: one unmerged commit on 11 branches is ONE finding, not 11');

  // --------------------------------------------------------------- case 2
  // Superseded removes the commit from the report entirely, and with it the
  // only reason those branches were not redundant.
  const source = execFileSync(process.execPath, ['-e',
    `process.stdout.write(require('fs').readFileSync(${JSON.stringify(script)}, 'utf8'))`,
  ], { encoding: 'utf8' });
  const patched = source.replace(
    'const SUPERSEDED = new Map([',
    `const SUPERSEDED = new Map([\n  [${JSON.stringify(uniqueSha)}, 'test fixture'],`,
  );
  assert.notEqual(patched, source, 'could not inject a SUPERSEDED entry; the anchor moved');
  const patchedPath = join(repo, 'patched-checker.mjs');
  writeFileSync(patchedPath, patched, 'utf8');

  let patchedJson;
  try {
    patchedJson = JSON.parse(execFileSync(process.execPath, [patchedPath, '--json'],
      { cwd: repo, encoding: 'utf8' }));
  } catch (error) {
    patchedJson = JSON.parse((error.stdout ?? '').toString());
  }
  assert.equal(findingsOf(patchedJson, 'unrepresented-commit').length, 0,
    'a superseded commit must not be reported as unrepresented');
  assert.ok(patchedJson.redundant_branches.includes('unique-branch'),
    'with its only novel commit superseded, the branch becomes redundant');
  ok('case 2: a superseded commit leaves the report, and its branch becomes redundant');

  // ------------------------------------------------- protected severity split
  // The same commit, parked on a checkpoint branch instead, is the declared
  // intended state and must warn rather than error. Without this the checker
  // can never reach zero errors while work is deliberately preserved.
  for (let index = 0; index < 10; index += 1) git('branch', '-D', `carrier-${index}`);
  git('branch', '-m', 'unique-branch', 'checkpoint/parked');

  json = report();
  assert.equal(findingsOf(json, 'unrepresented-commit').length, 0,
    'a commit held only by a protected branch must not be an error');
  const preserved = findingsOf(json, 'preserved-commit');
  assert.equal(preserved.length, 1, 'it should be reported as preserved instead');
  assert.equal(preserved[0].level, 'warn');
  ok('protected split: a commit parked only on checkpoint/* warns instead of erroring');

  // ------------------------------------------------------------ branch count
  for (let index = 0; index < 12; index += 1) git('branch', `filler-${index}`);
  json = report();
  const counted = findingsOf(json, 'branch-count');
  assert.equal(counted.length, 1);
  assert.equal(counted[0].level, 'error', 'more than 10 local branches must fail');
  ok('branch count above the policy limit is an error');

  assert.equal(runChecker().status, 1, 'the checker must exit nonzero while errors exist');
  ok('exit status is 1 when any error is present');
} finally {
  rmSync(repo, { recursive: true, force: true });
}

console.log(`\n${passed} check(s) passed, 0 failed`);
