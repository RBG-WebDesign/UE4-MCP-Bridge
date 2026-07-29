# Publishing to the public remote

**Publishing is paused as of 2026-07-29.** Nothing has been pushed since
`0a825fb` (2026-07-06). Read this before changing that.

## Why a plain push is unsafe

`https://github.com/RBG-WebDesign/UE4_Bridge.git` is public and is the bridge
only. But the local history predates the boundary between bridge and game: game
source is in commits reachable from local `main`, even though those paths are
untracked at HEAD.

Git publishes reachable history, not just the working tree. Pushing a
pre-boundary branch publishes game source that no path listing would warn you
about.

Local `main` is roughly 111 commits ahead of `origin/main` and is effectively a
different project. The consolidation branch is 23 commits ahead. Neither is
publishable as-is.

## The procedure

Build a sanitized branch rooted at `origin/main` and apply the final bridge tree
as a single clean commit. That publishes the *content* without the *history*.

```bash
cd "D:/Unreal Projects/UE4_Bridge"

# 1. Confirm what the remote actually has. Do not assume.
git ls-remote origin main

# 2. Fetch and branch from the published commit, not from anything local.
git fetch origin
git switch -c release/<version> origin/main

# 3. Bring the bridge tree across as content, without merging history.
#    Use the consolidation branch as the source of truth.
git checkout consolidate/unify-bridge-2026-07-29 -- \
  mcp-server Plugins/MCPBridge docs Scripts clients agents skills \
  AGENTS.md CLAUDE.md GEMINI.md README.md README_PROMPTBRUSH.md \
  .mcp.json package.json package-lock.json

# 4. Prove the result is clean before committing.
npm run check:layout        # fails on any tracked game path
npm run verify              # build + unit tests + smoke

# 5. Confirm no game content is staged, by inspection not by faith.
git diff --cached --name-only | grep -Ei '^(Content|Source|Config|Saved)/|\.uasset$|\.umap$|\.uproject$' \
  && echo "STOP: game content staged" || echo "clean"

# 6. Commit and push.
git commit -m "Release <version>: <summary>"
git push origin release/<version>
```

Open a pull request from `release/<version>` rather than pushing to `main`
directly. A local fast-forward once skipped GitHub's merge commit and left local
and remote `main` divergent, which is how two "main" branches started drifting in
the first place.

## Guards

Enable the pre-push hook in every clone:

```bash
git config core.hooksPath .githooks
```

It blocks a push that carries a tracked game path. It is per-clone configuration,
so a fresh clone has no protection until you run it.

`npm run check:layout` fails on any tracked `Content/`, `Source/`, `Config/`,
`Saved/`, `.uasset` or `.umap`. Run it before every release commit.

## Version

The server's version tracks `VersionName` in `MCPBridge.uplugin`, currently
`0.4.0`. The companion distribution repo at
`https://github.com/RBG-WebDesign/MCPBridge-Server` needs a synced release
whenever `mcp-server/` changes materially.

## Before the next release

The consolidation added four plugin modules' worth of C++, the engine-source
tools, the PIE agent, and the cloth optimizer. `MCPBridge.uplugin` is still at
`0.4.0` and its `Version` integer at `4`. Bump both, and update
`docs/FAB_LISTING.md`, before publishing any of it.
