# Large build-artifact commit in git history — analysis

**Status:** documented only, not fixed. History rewrite is intentionally deferred to the repo
owner (see "Recommended remediation" below) — nothing in this document changes any existing
commit.

## How this was found

While pushing the isolated `feature/renderer-opengles2` branch (base commit
`4c93f185c7006c526493e15ce625165c308f646a`), GitHub emitted a push-time warning:

```
remote: warning: File cmake-build-easygl/libCNA.a is 52.34 MB; this is larger than GitHub's
recommended maximum file size of 50.00 MB
remote: warning: GH001: Large files detected. You may want to try Git Large File Storage
```

The push itself succeeded (GitHub warns, it does not reject, below the hard 100 MB limit). The
warning surfaced because `feature/renderer-opengles2` was a brand-new ref; the offending objects
are old history, not something introduced by that branch or by the OPENGLES2 work.

## The incident

Two consecutive commits, same author, ~2 minutes apart, on **2026-06-07**:

| Commit | Time | Subject |
|---|---|---|
| `c9f05a687dd98f21b1a19a7043e2fecbddbd4201` | 2026-06-07T11:48:00+02:00 | "Implement SpriteFont glyph data model and SpriteBatch::DrawString" |
| `71c88d5bd64a29d7e2f50bc1c0adf85531457bc2` | 2026-06-07T11:49:49+02:00 | "Stop tracking cmake-build-easygl build artifacts" |

`c9f05a687` is a normal SpriteFont/DrawString feature commit that **also** accidentally swept in
the entire `cmake-build-easygl/` build output directory (CMake cache, compiler-ABI probe
binaries, dependency files, and every compiled `.o`/`.a`) — almost certainly a `git add -A`/
`git add .` run from inside a dirty working tree with no `.gitignore` entry for that build
directory yet.

`71c88d5bd`, the very next commit, removed the whole tree from tracking again and added a
`.gitignore` line for it. Its own message is explicit about the mistake: *"The previous commit
accidentally added the entire cmake-build-easygl/ tree (701 generated files incl. a 52 MB
libCNA.a). Add it to .gitignore and remove it from version control."*

**The problem:** git history is append-only. Deleting the files in a later commit removes them
from the working tree and from `HEAD`'s tree, but the blobs themselves remain in the repository's
object database forever (reachable from `c9f05a687`), inflating clone/fetch size indefinitely
unless the history itself is rewritten.

## Quantification

Full accidental tree, measured directly (`git ls-tree -r -l c9f05a687 -- cmake-build-easygl/`):

- **701 files, 160,890,338 bytes (153.4 MB) total**, added and then reverted in two commits.

The two largest individual blobs (a history-wide scan for blobs over 5 MB, across everything
reachable from the `4c93f185c…` base commit, found no other large-blob incident besides these
two — see "Scope of this analysis" for what that scan does and doesn't cover):

| Blob | Path | Size |
|---|---|---|
| `6bb6f236dbb74a5d8af3c122983a1450045a4acb` | `cmake-build-easygl/libCNA.a` | 54,883,144 bytes (52.34 MB) — the one GitHub warned about |
| (not individually re-hashed; see ls-tree output) | `cmake-build-easygl/SHARP_RUNTIME/libSHARP_RUNTIME.a` | 15,510,090 bytes (14.79 MB) |

The remaining ~86 MB is spread across the other 699 files: object files (`*.o`, several hundred
KB–low MB each), `compiler_depend.internal`/`compiler_depend.make` (37,120 and 38,732 lines of
generated dependency data respectively), `CMakeConfigureLog.yaml`, `CMakeCache.txt`, and a handful
of small compiler-ABI-probe binaries (`CMakeCCompilerId`/`CMakeCXXCompilerId`, ~16 KB each).

## Which branches/refs carry this history

Directly confirmed: `c9f05a687` is reachable from `feature/renderer-opengles2`
(`git branch --all --contains c9f05a687` → `feature/renderer-opengles2` +
`remotes/origin/feature/renderer-opengles2`), and — since that branch is exactly
`4c93f185c7006c526493e15ce625165c308f646a` plus 5 new, fully-inspected OPENGLES2 commits with no
other parents — `c9f05a687` must be an ancestor of the base commit itself, not of anything added
by that branch.

Strong inference, not directly re-verified in this sandbox (see limitation below): the base
commit `4c93f185c…` is one commit behind `origin/develop`'s tip and was confirmed earlier in this
session to be an ancestor of `origin/develop`, and `claude/cna-opengles2-renderer-opnd05` sits at
that same develop tip. A June 2026 commit that is an ancestor of the August 2026
"pre-renderer-expansion" base is, for all practical purposes, baked into every mainline branch —
`develop`, `claude/cna-opengles2-renderer-opnd05`, `feature/renderer-opengles2`, and any other
branch cut from develop after 2026-06-07.

**Limitation:** this sandbox has a shallow clone. `git merge-base --is-ancestor c9f05a687
origin/develop` reported "not an ancestor" locally, but the local `origin/develop` ref only goes
back 50 commits (oldest visible: `bcd06479d`, a much later "Phase-3 physical layout" refactor) —
the shallow boundary sits well after June 2026, so that plumbing check cannot see far enough back
to give a definitive answer either way from this environment. Treat "affects develop and the session
branch" as a well-supported inference from the graph structure above, not a directly-executed
`merge-base` proof on those two refs specifically. A full (unshallowed) clone would let you
confirm this with one command: `git merge-base --is-ancestor c9f05a687 origin/develop`.

### Verification against a full clone (2026-08-11, eleven-lane integration)

That command was run against a full clone during the `11branches` integration campaign. The
document's **conclusion is confirmed, but its commit SHAs do not identify the reachable copy**:

- `git merge-base --is-ancestor c9f05a687 develop` reports **not an ancestor**, and
  `git branch -a --contains c9f05a687` matches **no ref at all** — in a full clone that commit is
  an unreachable object, not part of any published branch.
- The incident is nevertheless genuinely baked into `develop`'s history under a **different commit
  pair**: `77cf76302fdff59f11b96d1c582fa3cafa48c156` ("Implement SpriteFont glyph data model and
  SpriteBatch::DrawString", 2026-06-07T11:48:00+02:00) and `e43a4a99b` ("Stop tracking
  cmake-build-easygl build artifacts"). `77cf76302` **is** an ancestor of `develop`.
- `77cf76302` and `c9f05a687` share the **identical tree** `9ddae2e0ab8757b5edd114ca692d9e678bce8158`
  and differ only in parent, i.e. they are the same content on two lineages — the shallow clone this
  analysis was written in saw the other one.
- The quantification is unchanged and re-measured on the reachable commit:
  `git ls-tree -r -l 77cf76302 -- cmake-build-easygl/` → **701 files, 160,890,338 bytes (153.4 MB)**.

So the remediation section below still applies; the paths to feed `git filter-repo` / BFG are the
same, and the commit to start reasoning from is `77cf76302`, not `c9f05a687`. Nothing was rewritten
here either — this note records a verification only.

## Why it won't recur

`.gitignore` already has a catch-all `cmake-build-*/` pattern (line 2) in addition to the many
renderer-specific entries added over time (`cmake-build-debug/*`, `cmake-build-easygl/*`, etc.).
No further gitignore change is needed to prevent a repeat of this specific mistake.

## Recommended remediation (for the owner to execute — not done here)

Standard tools for this exact situation, in order of preference:

1. **`git filter-repo`** (the tool the git project itself now recommends over `filter-branch`) —
   e.g. `git filter-repo --path cmake-build-easygl --invert-paths` run against a fresh mirror
   clone, to strip that path from every commit across every branch/tag in one pass.
2. **BFG Repo-Cleaner** — simpler CLI for exactly this case
   (`bfg --delete-folders cmake-build-easygl`), also operates on a mirror clone.
3. `git filter-branch` — the built-in tool; works, but is documented by git itself as slow and
   easy to misuse compared to the two options above; not recommended as a first choice.

All three rewrite commit hashes for every commit from `c9f05a687` forward, on every branch/tag
that contains it — which, per the section above, is expected to be effectively all of them. That
means:

- Every existing clone (including this sandbox's, and anyone else's local checkout) becomes
  stale and needs to be re-cloned or hard-reset to the rewritten history after the rewrite.
- Any open PRs/branches based on pre-rewrite commits will need to be rebased onto the new history.
- A `--force` push of every rewritten ref is required (the repo's own CLAUDE.md/harness rules
  normally forbid history rewrites and force pushes — this is the documented, deliberate
  exception the owner asked for).
- After rewriting, `git reflog expire --expire=now --all` + `git gc --prune=now --aggressive` (on
  the server-side/bare copy, or instruct GitHub support / re-push a pruned mirror) is needed to
  actually reclaim the storage — rewriting history alone doesn't delete the now-unreferenced
  objects until they're garbage-collected.

## Scope of this analysis

- This document covers only the specific incident GitHub's push warning surfaced
  (`cmake-build-easygl/`, the `c9f05a687` / `71c88d5bd` pair).
- The "any other large blobs?" scan was a `git rev-list --objects` + `git cat-file --batch-check`
  sweep (>5 MB threshold) over everything reachable from the `4c93f185c…` base commit in this
  sandbox's shallow clone — it found nothing beyond the two blobs listed above, but it is bounded
  by this clone's shallow fetch depth, not a full-lifetime audit of the repository's entire
  history back to its first commit. A complete audit would need the same scan run against a full
  (unshallowed) clone or GitHub's own repository-size tooling.
- No files were modified, no history was rewritten, and nothing beyond this markdown file was
  committed as part of this analysis.
