#!/usr/bin/env node
// Repository hygiene as a checked invariant rather than a cleanup task.
//
// This exists because two cleanups in one week were archaeology. 44 worktrees
// had accumulated silently until one of them ate the main checkout; local main
// had drifted 237 commits from origin/main without anyone deciding it should.
// Neither was a hard problem. Both were invisible until they were expensive.
//
// The steady state this enforces is deliberately boring:
//
//     main, clean tree, one worktree, no stale branches, main == origin/main
//
// Anything else should be temporary, named, and reported.
//
//   node Scripts/check-git-hygiene.mjs          verify
//   node Scripts/check-git-hygiene.mjs --json   machine-readable
//
// Exit 0 clean or warnings only, 1 on any error.

import { execFileSync } from 'node:child_process';
import { existsSync, lstatSync, readFileSync, readdirSync, readlinkSync, realpathSync } from 'node:fs';
import { dirname, isAbsolute, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// The repository being CHECKED is the one the command runs in, not the one the
// script lives in. Deriving it from import.meta.url made the checker audit
// UE4_Bridge no matter where it was invoked, which is wrong for a worktree and
// untestable against a fixture. Falls back to the script's own repo when the
// working directory is not inside a git tree.
const repoRoot = (() => {
  try {
    return execFileSync('git', ['rev-parse', '--show-toplevel'], {
      cwd: process.cwd(), encoding: 'utf8',
    }).trim();
  } catch {
    return join(dirname(fileURLToPath(import.meta.url)), '..');
  }
})();
const asJson = process.argv.includes('--json');

const git = (args, options = {}) => {
  try {
    return execFileSync('git', args, {
      cwd: repoRoot, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, ...options,
    }).trim();
  } catch (error) {
    return { error: (error.stderr || error.message).toString().trim() };
  }
};
const lines = (value) => (typeof value === 'string' ? value.split(/\r?\n/).filter(Boolean) : []);

// ------------------------------------------------------------------- policy

const BRANCH_LIMITS = { ok: 5, warn: 10 };      // >warn is an error
const BRANCH_STALE_DAYS = 30;
const UNTRACKED_STALE_DAYS = 14;
const CHECKPOINT_MAX_DAYS = 30;

/** Branches exempt from the "must be represented on main" rule.
    A checkpoint is exempt only while it still holds unique commits AND is
    inside its window: an expired checkpoint that duplicates main is just a
    stale branch with a respectable name. */
const PROTECTED = [
  /^main$/,
  /^main-local-/,
  /^checkpoint\//,
];
const isProtected = (name) => PROTECTED.some((pattern) => pattern.test(name));

/**
 * Commits that are NOT on main by patch-id but whose content demonstrably is.
 *
 * The lane workflow replayed work rather than merging it, so a commit can be
 * fully superseded while sharing no patch-id with anything on main. Without
 * this list, both entries below are reported against every one of ~40 lane
 * branches that share them as ancestors: one fact, forty errors. A checker
 * whose output is mostly noise gets ignored, which is worse than no checker.
 *
 * An entry needs evidence, not a hunch. Both of these were confirmed on
 * 2026-08-06 by checking that every file they touch exists on origin/main and
 * that the feature is live (puerts_graph_inspect was called against a running
 * editor; the non-Actor parent rule has a test asserting it).
 */
const SUPERSEDED = new Map([
  ['d134cfe98f29f0f4a094bafc86ce841bdb3df91c',
    'Stamina save/load. Superseded: non-Actor parent support and the save round '
    + 'trip are on main, verified 2026-08-06 by file presence plus the parent-gate test.'],
  ['4d9be287e8a4a86c6e40ccd1fb02b43eb2581d78',
    'Read-only Blueprint graph inspection. Superseded: puerts_graph_inspect is '
    + 'live on main, verified 2026-08-06 by calling it against a running editor.'],
  ['6de750ba5c96d893c2e0825d1a43518d6fad8a67',
    'WIP FP-4 component convergence and FP-5 level commands. Superseded: every '
    + 'file it touches is on main, remove_unlisted carries the components scope, '
    + 'and the level commands shipped. Verified 2026-08-06 by file presence plus '
    + 'schema inspection of main.'],
]);

const findings = [];
const add = (level, check, message, fix) => findings.push({ level, check, message, fix });
const error = (...args) => add('error', ...args);
const warn = (...args) => add('warn', ...args);

const days = (unixSeconds) => Math.floor((Date.now() / 1000 - unixSeconds) / 86400);

// --------------------------------------------------------- 2. main vs origin

const hasOrigin = lines(git(['remote'])).includes('origin');
const currentBranch = git(['rev-parse', '--abbrev-ref', 'HEAD']);

if (!hasOrigin) {
  warn('divergence', 'no origin remote; divergence cannot be checked');
} else if (typeof git(['rev-parse', '--verify', '--quiet', 'refs/heads/main']) !== 'string') {
  warn('divergence', 'no local main branch');
} else {
  const counts = git(['rev-list', '--left-right', '--count', 'main...origin/main']);
  const [ahead, behind] = lines(counts)[0]?.split(/\s+/).map(Number) ?? [0, 0];
  if (ahead > 0 && behind > 0) {
    error('divergence',
      `main and origin/main have BOTH diverged: ${ahead} local-only, ${behind} remote-only`,
      'Do not pull, merge, reset or rebase automatically. Classify the unique commits '
      + 'first: `git cherry origin/main main` shows which are already upstream by patch-id.');
  } else if (ahead > 0) {
    warn('divergence', `main is ${ahead} commit(s) ahead of origin/main`, 'Push, or explain why not.');
  } else if (behind > 0) {
    warn('divergence', `main is ${behind} commit(s) behind origin/main`, 'git pull --ff-only');
  }
}

// ------------------------------------------- 3,4,5,6. branch count and health

const branchRows = lines(git([
  'for-each-ref', '--format=%(refname:short)\t%(committerdate:unix)', 'refs/heads/',
])).map((row) => {
  const [name, date] = row.split('\t');
  return { name, age: days(Number(date)) };
});

const count = branchRows.length;
if (count > BRANCH_LIMITS.warn) {
  error('branch-count', `${count} local branches (policy: <=${BRANCH_LIMITS.ok} ok, <=${BRANCH_LIMITS.warn} warn)`,
    'Delete branches whose work is on main. `--json` lists which are redundant.');
} else if (count > BRANCH_LIMITS.ok) {
  warn('branch-count', `${count} local branches (policy: <=${BRANCH_LIMITS.ok})`);
}

const mergedIntoMain = new Set(lines(git(['branch', '--merged', 'main', '--format=%(refname:short)'])));
const redundant = [];
const unique = [];
// Group unrepresented work by COMMIT, not by branch. Forty branches sharing one
// ancestor is one problem, and reporting it forty times buries the branch that
// genuinely has something.
const unrepresented = new Map(); // sha -> { subject, branches[] }

for (const branch of branchRows) {
  if (branch.name === 'main') continue;

  // Patch-id equivalence, not ancestry: this repo integrates lanes by replaying
  // commits, so a branch can be fully represented on main while sharing no
  // commit ids with it. Ancestry alone would call every such branch unique.
  const cherry = git(['cherry', '-v', 'main', branch.name]);
  const novelCommits = typeof cherry === 'string'
    ? lines(cherry).filter((line) => line.startsWith('+')).map((line) => {
      const [, sha, ...subject] = line.split(' ');
      return { sha, subject: subject.join(' ') };
    }).filter(({ sha }) => !SUPERSEDED.has(sha))
    : null;
  const novel = novelCommits?.length ?? null;

  for (const commit of novelCommits ?? []) {
    const entry = unrepresented.get(commit.sha) ?? { subject: commit.subject, branches: [] };
    entry.branches.push(branch.name);
    unrepresented.set(commit.sha, entry);
  }

  if (novel === 0) {
    redundant.push(branch.name);
    if (mergedIntoMain.has(branch.name)) {
      warn('merged-branch', `${branch.name} is merged into main and still exists locally`,
        `git branch -d ${branch.name}`);
    } else {
      warn('redundant-branch',
        `${branch.name} holds nothing novel to main (all commits upstream by patch-id, or superseded)`,
        `git branch -D ${branch.name}`);
    }
  } else if (novel !== null && !isProtected(branch.name)) {
    unique.push({ name: branch.name, novel, age: branch.age });
  }

  if (branch.age > BRANCH_STALE_DAYS && branch.name !== 'main') {
    warn('stale-branch', `${branch.name} last committed ${branch.age} days ago`);
  }
  if (/^checkpoint\//.test(branch.name) && branch.age > CHECKPOINT_MAX_DAYS && novel === 0) {
    error('expired-checkpoint',
      `${branch.name} is ${branch.age} days old and holds nothing unique`,
      `Checkpoints expire at ${CHECKPOINT_MAX_DAYS} days unless they hold unique commits. `
      + `git branch -D ${branch.name}`);
  }
}

// One finding per distinct commit, naming every branch that carries it.
//
// Severity depends on WHERE it lives. A commit held only by protected branches
// is the declared intended state: that is what a checkpoint branch is for, and
// erroring on it would leave a permanent failure with no honest resolution
// short of falsely marking it superseded. A commit on an ordinary branch is
// different: nothing is preserving it on purpose, so it is an error.
for (const [sha, entry] of unrepresented) {
  const carriers = entry.branches.length;
  const shown = carriers > 3
    ? `${entry.branches.slice(0, 3).join(', ')}, ...`
    : entry.branches.join(', ');
  const onlyProtected = entry.branches.every(isProtected);
  const message = `${sha.slice(0, 8)} "${entry.subject}" is on ${carriers} branch(es) and not on main (${shown})`;

  if (onlyProtected) {
    warn('preserved-commit', `${message} - held only by protected branches, as intended`,
      'Kept deliberately. Merge it or mark it SUPERSEDED when the branch retires.');
  } else {
    error('unrepresented-commit', message,
      'Merge it, or if its content is already on main under a different patch, add it to '
      + 'SUPERSEDED with the evidence that made you sure. If it should simply be preserved, '
      + 'move it to a checkpoint/* branch and delete the ordinary one.');
  }
}

// --------------------------------------- 7. remote-tracking refs gone upstream

if (hasOrigin) {
  const pruneDry = git(['remote', 'prune', '--dry-run', 'origin']);
  const stale = typeof pruneDry === 'string'
    ? lines(pruneDry).filter((line) => /would prune/.test(line)).length
    : 0;
  if (stale > 0) {
    warn('stale-remote-refs',
      `${stale} remote-tracking ref(s) point at branches deleted on origin`,
      'Report only. Do NOT prune here: for this repo those refs are the only surviving '
      + 'copies of some lane commits. Delete individually after confirming the work is on main.');
  }
}

// ------------------------------------------------- 8,9. worktrees and reparse

const worktreePaths = lines(git(['worktree', 'list', '--porcelain']))
  .filter((line) => line.startsWith('worktree '))
  .map((line) => line.slice(9));
const extra = worktreePaths.filter((p) => resolve(p).toLowerCase() !== resolve(repoRoot).toLowerCase());

const manifestPath = join(repoRoot, '.worktrees.json');
let manifest = [];
if (existsSync(manifestPath)) {
  try { manifest = JSON.parse(readFileSync(manifestPath, 'utf8')).worktrees ?? []; } catch {
    error('worktree-manifest', '.worktrees.json is not valid JSON');
  }
}

for (const path of extra) {
  const entry = manifest.find((m) => resolve(repoRoot, m.path).toLowerCase() === resolve(path).toLowerCase());
  if (!entry) {
    error('undeclared-worktree', `${path} is not in .worktrees.json`,
      'Every extra worktree needs {path, purpose, branch, created, expires}. '
      + 'Remove it with: node Scripts/worktree-cleanup.mjs delete <dir> --yes');
    continue;
  }
  if (entry.expires && new Date(entry.expires).getTime() < Date.now()) {
    error('expired-worktree', `${path} expired on ${entry.expires} (purpose: ${entry.purpose})`,
      'node Scripts/worktree-cleanup.mjs delete <dir> --yes');
  }
}

// A reparse point inside a worktree is the 2026-08-06 failure waiting to happen.
const isInside = (parent, child) => {
  const rel = relative(resolve(parent), resolve(child));
  return rel !== '' && !rel.startsWith('..') && !isAbsolute(rel);
};
const SKIP = new Set(['node_modules', '.git', 'Binaries', 'Intermediate', 'DerivedDataCache', 'Saved']);
function scanLinks(root, out = []) {
  let entries;
  try { entries = readdirSync(root, { withFileTypes: true }); } catch { return out; }
  for (const entry of entries) {
    const full = join(root, entry.name);
    let stat;
    try { stat = lstatSync(full); } catch { continue; }
    if (stat.isSymbolicLink()) {
      let target = '(unreadable)';
      try { target = realpathSync(full); } catch { try { target = readlinkSync(full); } catch { /* keep */ } }
      out.push({ path: full, target });
      continue;
    }
    if (stat.isDirectory() && !SKIP.has(entry.name)) scanLinks(full, out);
  }
  return out;
}

for (const path of extra) {
  if (!existsSync(path)) continue;
  const outward = scanLinks(path).filter((link) => !isInside(path, link.target));
  if (outward.length) {
    error('unsafe-worktree-links',
      `${path} holds ${outward.length} reparse point(s) aimed OUTSIDE it, e.g. ${outward[0].path} -> ${outward[0].target}`,
      'A recursive delete would destroy the destination. Use '
      + 'node Scripts/worktree-cleanup.mjs delete <dir> --yes, which unlinks first.');
  }
}

// ------------------------------------------------- 10. long-lived untracked

const untracked = lines(git(['ls-files', '--others', '--exclude-standard']));
for (const file of untracked) {
  const full = join(repoRoot, file);
  let stat;
  try { stat = lstatSync(full); } catch { continue; }
  const age = days(stat.mtimeMs / 1000);
  if (age > UNTRACKED_STALE_DAYS) {
    warn('stale-untracked', `${file} has been untracked for ${age} days`,
      'Commit it, ignore it, or delete it. An untracked file survives no backup and no clone.');
  }
}

// ------------------------------------------------------------------- report

const errors = findings.filter((f) => f.level === 'error');
const warnings = findings.filter((f) => f.level === 'warn');

if (asJson) {
  console.log(JSON.stringify({
    branch: currentBranch,
    branch_count: count,
    redundant_branches: redundant,
    unique_branches: unique,
    worktrees: worktreePaths.length,
    findings,
  }, null, 2));
} else {
  console.log('git hygiene\n');
  console.log(`  branch      ${currentBranch}`);
  console.log(`  branches    ${count} local`);
  console.log(`  worktrees   ${worktreePaths.length}`);
  console.log(`  untracked   ${untracked.length}\n`);
  for (const finding of findings) {
    console.log(`${finding.level === 'error' ? ' ERROR' : '  WARN'}  ${finding.check}: ${finding.message}`);
    if (finding.fix) console.log(`        fix: ${finding.fix}`);
  }
  if (!findings.length) console.log('  clean: main == origin/main, no stale branches, one worktree.');
  console.log(`\n${errors.length} error(s), ${warnings.length} warning(s)`);
  if (redundant.length > 3) {
    console.log(`\n${redundant.length} branches hold nothing novel to main. Delete them with:`);
    console.log(`  git branch -D ${redundant.slice(0, 4).join(' ')} ...`);
    console.log('  (--json lists all of them under redundant_branches)');
  }
}

process.exit(errors.length ? 1 : 0);
