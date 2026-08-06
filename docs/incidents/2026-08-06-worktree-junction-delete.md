# Incident: recursive delete followed junctions into the main checkout

**Date:** 2026-08-06
**Severity:** data loss, partially unrecoverable
**Unrecoverable loss:** uncommitted edits to two files

## What was lost

| Path | State | Recovered |
|---|---|---|
| `docs/CAPABILITY_FINDINGS.md` | uncommitted edits | **No** |
| `docs/playbooks/README.md` | uncommitted edits | **No** |
| `mcp-server/` (101 tracked files) | committed at `8a2a978` | Yes, from git |
| `Plugins/Puerts` (1038 files, gitignored) | pinned vendored bundle | Yes, from `BridgeInstallTest` |
| `mcp-server/node_modules`, `puerts-runtime/node_modules` | regenerable | Yes, `npm install` |

The two doc files were modified in the working tree and never staged, so no blob
was ever written to the object store. `git fsck --lost-found` finds nothing for
them. They are gone from this repository. The only remaining avenues are outside
git: Windows File History or Previous Versions on `D:\`, or an editor's local
history.

## What happened

The task was to clear 44 stale worktrees that `npm run doctor` had been warning
about. The audit before deleting was thorough about the right thing and blind to
the wrong one.

1. `git worktree remove --force` was run over all 44. It succeeded for 18 and
   reported `is not a working tree` for 26, whose registrations were already
   stale. Git's registry was then clean, but 27 directories remained on disk,
   totalling 230 MB.
2. Those 27 were audited for content: every file was hashed and checked against
   the object store. 233 files were not in it, and all 233 were gitignored build
   output (`Plugins/MCPBridge/Content/JavaScript`, `__pycache__`) plus one
   packaged release zip, which was moved to `Releases/` rather than deleted.
   The audit's conclusion, that deleting destroyed no content, was correct as far
   as it went.
3. `rm -rf _bridge_worktrees` was run. Each of those worktrees contained a
   Windows **junction** at `Plugins/Puerts` aimed at the main checkout, created
   exactly as `AGENTS.md` instructs. The recursive delete walked through them and
   emptied the destination: `mcp-server/`, `Plugins/Puerts`, and both workspace
   `node_modules` in `D:\Unreal Projects\UE4_Bridge`.
4. Recovery of the tracked files used `git checkout -- .`. That restored the 101
   deleted files and also reverted the two doc files, which held unrelated
   uncommitted work that had been deliberately left out of the commit earlier in
   the same session.

Two distinct mistakes. The first destroyed files outside the delete target. The
second destroyed files the first had not touched.

## Why the audit did not catch it

The content audit walked the orphaned directories and hashed what it found. It
skipped `node_modules` and a few build directories by name, and it never
descended into a reparse point, so it never saw `Plugins/Puerts` as content.
That was the correct walking behaviour and it produced a report with a hole in
it: files reachable only through a link were absent from the inventory, so
"nothing unique here" was true of what was inventoried and false of what was on
disk.

A junction is hard to notice. It is not a POSIX symlink, Explorer draws it as an
ordinary folder, `du` bills the destination's bytes to the link, and the earlier
per-worktree audit had already returned "no unique files" for these 26 because
`git ls-files` errored in a directory with no `.git` and the script recorded an
empty list on error. Three separate signals each looked fine on its own.

`AGENTS.md` documented the junctions the whole time, in the prerequisites
section, as the fix for a worktree missing `Plugins/Puerts`. It was read as setup
instructions rather than as a hazard for teardown.

## Recovery, in order

1. `git checkout -- .` restored the 101 tracked files. **This step caused the
   second loss.** The correct command was `git checkout -- mcp-server`.
2. `Plugins/Puerts` was restored by copying from
   `D:\Unreal Projects\BridgeInstallTest\Plugins\Puerts`, which had a real copy
   rather than a link. `npm run check:puerts` confirmed it matched the pin
   (`Unreal_v1.0.9 @ 838ab762d830`, 1038 files).
3. `npm install` restored both workspace `node_modules`.
4. `npm run verify` exited 0 and `npm run doctor` reported 10 ok, 0 errors.

The worktree cleanup itself was verified lossless for everything git tracked: all
55 local branches sat at byte-identical commits before and after, compared with
`git for-each-ref`. Removing a worktree never touches the object store.

## Prevention

**The hard rule, now in `AGENTS.md`:** a worktree can contain Windows junctions
into the main checkout. Before deleting a worktree directory, detect and remove
reparse points without following them. Never run a recursive delete until that
check passes.

`Scripts/worktree-cleanup.mjs` implements it:

```bash
node Scripts/worktree-cleanup.mjs audit  <dir>          # report, refuse, change nothing
node Scripts/worktree-cleanup.mjs delete <dir> --yes    # unlink, verify, then delete
node Scripts/worktree-cleanup.mjs restore <pathspec>    # scoped restore, dirty tree blocked
```

It walks without descending through links, resolves each destination and prints
it, removes a junction with a link-only operation (`rmdir` on Windows, never a
recursive delete), re-scans to prove zero reparse points remain, and only then
deletes. It refuses to delete the checkout it runs from. Its `restore` refuses
`.`, `./` and `*`, and refuses any restore against a dirty tree unless
`--archive` stashes the dirty work first.

`Scripts/worktree-cleanup.test.mjs` is the regression test. It builds a
disposable worktree containing a real junction to a destination outside it, runs
the cleanup, and asserts both halves: the worktree is gone **and** the
destination's files are byte-identical. A cleanup that deleted both would pass a
naive "the directory is gone" test and reproduce this outage. It runs in
`npm run test:editor-free`.

## What this changes about auditing

An inventory that skips links is not an inventory of the disk. When an audit
declines to follow something, the report has to say so, because the reader will
otherwise treat "nothing found" as "nothing there". The content audit here should
have listed every reparse point it refused to descend into, and that omission is
the reason a correct-looking report preceded a destructive command.
