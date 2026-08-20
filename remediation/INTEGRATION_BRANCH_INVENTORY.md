# INTEGRATION_BRANCH_INVENTORY.md — dynamic pending-branch inventory

> ## ⚠ SUPERSEDED — HISTORICAL SNAPSHOT
>
> **Superseded on 2026-08-04 by `integration/INTEGRATION_BRANCH_INVENTORY.md`**, re-derived after a
> fresh fetch against the finalized checkpoint `d79214e7`. Read that document for current data.
>
> **What this snapshot got right and keeps:** the lane count (21), the deduplication method, the
> `feature/gl` three-repository chain, and the audit-stacked re-classification of Magnum and Wicked.
>
> **What is superseded:** every per-lane `Ahead` figure and every HIGH/LOW conflict class in §3.
> They were measured against `origin/develop`, which attributes the remediation campaign's own
> commits to the lane. Re-measured from each lane's real merge base with the checkpoint, **eleven**
> lanes are audit-stacked, not two — `feature/gltf` contributes **1** commit rather than 555,
> `feature/direct2d` **48** rather than 752, `feature/skia` **141** rather than 848. See
> `integration/INTEGRATION_BRANCH_INVENTORY.md` §3.
>
> **Also superseded:** §5's description of `feature/direct2d` as *"the final actively-developed
> lane"* with a moving head. The owner has since **frozen it at `9b17e783`**.
>
> The counts below are **retained deliberately as the snapshot they were**, per the rule that
> obsolete current-count claims are labelled rather than erased.

---

> **Current inventory as of the 2026-08-04 exit-reconciliation fetch (checkpoint candidate on
> `feature/audit` @ `765335f5`): 21 logical pending integration lanes.**
>
> This is a **snapshot derived from Git refs after `git fetch --all --prune --tags`**, not an
> invariant. **N may change before integration begins** — `feature/direct2d` moved twice while this
> document was being written (§5). Do not quote the number without its fetch date and commit.
> Regenerate it (§2) rather than carrying it forward.

**Supersedes** the "19 logical lanes at `099b03c0`" snapshot. That figure was derived **without a
fetch** and is retained below only as a labelled historical entry (§7).

---

## 1. What changed against the previous snapshot

| Change | Detail |
|---|---|
| **19 → 21 lanes** | The two remote-only lanes the un-fetched search could not see are now counted: `origin/claude/cna-magnum-gr-backend-211xsx`, `origin/claude/wicked-engine-cna-backend-5ffqzd` |
| **Magnum and Wicked are re-classified** | They are **not** develop-forked backend lanes. Both fork from **`feature/audit` @ `2338b44f7`**, carry 755 of the remediation branch's own commits, and add only **13** (Magnum) and **10** (Wicked) commits of their own. See §4 — this changes their adaptation cost and their integration order |
| **`feature/direct2d` head moved during the session** | `f6edeb7c` → `6cd6ad06`, last commit `2026-08-04T11:27:08Z`, ~2 minutes before the inventory was derived. Live evidence it is still developing (§5) |
| **`feature/gl` cross-repository chain re-verified after a fresh fetch** | MetaGL and EasyGL heads unchanged; one factual correction to where the MetaGL redirect lives (§6.1) |
| **`feature/llgl` head moved** | `65327813` (716 ahead) → `fa26e72d` (718 ahead) |

---

## 2. Methodology

Derived from `git for-each-ref` over `refs/heads/` and `refs/remotes/origin/` **after**
`git fetch --all --prune --tags` (exit 0; one configured remote,
`origin` → `git@github-openeggbert:openeggbert/cna`; no refs added, updated or pruned by this
session's fetch).

**Integration base:** `origin/develop` @ `ac3aaaeb`.

**Counted:** every ref that is not an ancestor of the integration base, deduplicated so a local
branch and its remote-tracking ref pointing at the same logical work count **once**.

**Not counted:** `develop`, `master`, the remediation branch `feature/audit` itself (it *is* the
checkpoint base, not a lane to integrate into it), archive tags, already-merged branches, and the
prunable stale worktree `/tmp/cnaaudit-gfx098-prefix` (detached HEAD, not a feature branch).

**Verified:** `git merge-base --is-ancestor <ref> origin/develop` returns non-zero for all 21 —
nothing in the list is already merged. No two lanes share a head.

**Deduplication actually applied.** 18 lanes exist as a local branch *and* an identical
remote-tracking ref; they are one lane each. `feature/direct2d` is one lane whose **local ref is
ahead of its remote** (`6cd6ad06` local vs `e341758f` on `origin`) — still one lane, recorded with
both refs. Three lanes are **remote-only**: `origin/feature/ext`, and the two `claude/*` lanes.

**Measurement caveat that mattered.** Diffing Magnum and Wicked against `origin/develop` reports
them touching all three shared interfaces. That is an artifact of the 755 **remediation** commits
they carry, not of their own work. Their true footprint must be measured from their real fork point
`2338b44f7` — §4 does that, and the answer is materially different.

---

## 3. The 21 lanes

`GD` = touches `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` ·
`IGB` = touches `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` ·
`GC` = touches `include/CNA/GraphicsCapability.hpp`

Rows 1–19 share merge base **`ac3aaaeb`** (the integration base) and `Ahead` is measured from it.
Rows 20–21 are measured from **`2338b44f7`** on `feature/audit` — see §4.

| # | Lane | Head | Ahead | Last commit (UTC) | GD | IGB | GC | State | Conflict class |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `feature/skia` | `ca046f01` | 848 | 2026-08-04T05:20 | ✔ | ✔ | ✔ | settled (paused by owner decision, SKIA-163) | **HIGH** |
| 2 | `feature/direct2d` | `6cd6ad06` | 752 | **2026-08-04T11:27** | ✔ | ✔ | ✔ | **ACTIVE — final lane still developing** (§5) | **HIGH** |
| 3 | `claude/html-dom-cna-backend-xefzwf` | `8e4e4293` | 752 | 2026-08-04T03:07 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 4 | `feature/gdi` | `adc9cc2a` | 741 | 2026-08-03T20:35 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 5 | `feature/glide` | `2f9b47e1` | 739 | 2026-08-04T04:40 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 6 | `feature/llgl` | `fa26e72d` | 718 | 2026-08-04T09:43 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 7 | `feature/diligent` | `1ab12b50` | 715 | 2026-08-04T04:35 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 8 | `feature/sokol` | `261ea700` | 687 | 2026-08-04T05:10 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 9 | `feature/gltf` | `86ada7a7` | 555 | 2026-07-28T18:28 | ✔ | ✔ | ✔ | settled (content pipeline, not a backend) | **HIGH** |
| 10 | `feature/metal` | `48928d11` | 99 | 2026-07-21 | ✔ | — | — | settled, **unbuilt (no Mac)** | MEDIUM |
| 11 | `feature/opengl2` | `77d36d9e` | 40 | 2026-07-20 | ✔ | ✔ | ✔ | integration-ready | **HIGH** |
| 12 | `feature/opengl1` | `fc14f37b` | 31 | 2026-07-20 | ✔ | ✔ | — | integration-ready | MEDIUM |
| 13 | `feature/opengl4` | `c49e0ba2` | 28 | 2026-07-22 | ✔ | ✔ | — | integration-ready | MEDIUM |
| 14 | **`feature/gl`** | `f8efb9b4` | 28 | 2026-07-20 | ✔ | ✔ | — | **cross-repository — see §6** | MEDIUM + **external** |
| 15 | `feature/dxold` | `36289bb2` | 28 | 2026-07-21 | — | — | — | integration-ready | **LOW** |
| 16 | `feature/opengles1` | `3d576da2` | 26 | 2026-07-22 | ✔ | — | — | integration-ready | MEDIUM |
| 17 | `feature/depthcrt` | `f4804469` | 6 | 2026-07-22 | — | — | — | integration-ready | **LOW** |
| 18 | `feature/stub` | `a35651e8` | 5 | 2026-07-19 | ✔ | — | — | integration-ready | MEDIUM |
| 19 | `origin/feature/ext` | `05ab5d3d` | 1 | 2026-07-20 | — | — | — | integration-ready, **remote-only** | **LOW** |
| 20 | `origin/claude/cna-magnum-gr-backend-211xsx` | `9b903db8` | **13** † | 2026-08-04T09:40 | ✔ | ✔ | — | **audit-stacked**, remote-only (§4) | MEDIUM |
| 21 | `origin/claude/wicked-engine-cna-backend-5ffqzd` | `91d8587e` | **10** † | 2026-08-04T09:18 | — | — | — | **audit-stacked**, remote-only (§4) | **LOW** |

† Ahead of `feature/audit` @ `2338b44f7`, **not** of `develop`. Measured from `develop` they read 768
and 765, but 755 of those commits are the remediation campaign's own.

### 3.1 Overlap with the post-audit shared-interface changes

Recomputed from refs this session:

- **19 of 21 do not contain `fc0dd2a2`** (`refactor(graphics): unify ordinary and instanced stream
  descriptions`). **Magnum and Wicked do contain it** — they are the only two lanes already on the
  unified `GpuDrawParams` representation, because they branched off `feature/audit` after it landed.
- Of the 19 develop-forked lanes: **16** modify `GraphicsDevice.cpp`, **13** modify
  `IGraphicsBackend.hpp`, **10** modify `GraphicsCapability.hpp`; exactly **3** touch none of the
  three (`feature/depthcrt`, `feature/dxold`, `origin/feature/ext`).

Those 19 therefore each carry the **old** `GpuDrawParams` shape with the four fields `REMED-GFX-202`
removed (`instanceVb`, `instanceVertexOffset`, `instanceFrequency`, `vertexBufferOffset`). That is a
**mechanical adaptation cost**, not a defect and not a ticket. Per-branch checklist:
`plans/plan_postaudit.md` §10.

**Magnum and Wicked do not carry that cost.** They already build against the unified representation.

### 3.2 Suggested integration order

1. **`feature/depthcrt`, `feature/dxold`, `origin/feature/ext`** — touch none of the three shared
   interfaces. Lowest risk, and they validate the process cheaply.
2. **`feature/stub`, `feature/opengles1`, `feature/opengl1`, `feature/opengl4`, `feature/opengl2`** —
   small, settled, single shared-interface surface.
3. **`origin/claude/wicked-engine-cna-backend-5ffqzd`, then
   `origin/claude/cna-magnum-gr-backend-211xsx`** — 10 and 13 commits on top of the checkpoint base,
   already on the unified representation. Wicked touches **none** of the three shared interfaces from
   its fork point; Magnum touches `GraphicsDevice.cpp` and `IGraphicsBackend.hpp`. These are the
   cheapest large-value lanes precisely because they are stacked on the remediation work — but see
   §4 for the 22-commit rebase each needs first, and for what is *not* established about them.
4. **`feature/gl`** — only after its external chain (§6) completes. Nothing else gates on it.
5. **`feature/metal`** — settled but unbuilt; integrate for source continuity, not verification.
6. **The eight large develop-forked lanes** (`skia`, `direct2d`, `html-dom`, `gdi`, `glide`, `llgl`,
   `diligent`, `sokol`) and `feature/gltf` — **last.** Each is 555–848 commits ahead and touches all
   three shared interfaces. **`feature/direct2d` last of all, and not before it is frozen** (§5).

**`feature/gltf` may be sequenced earlier** with group 2 despite its size if desired: it is
content-pipeline work rather than a backend, provided its `GpuDrawParams` adaptation is done once.

---

## 4. Magnum and Wicked Engine — audit-stacked lanes

Both were re-derived from the current fetch. Both were previously recorded as develop-forked lanes
768 and 765 commits ahead; that is arithmetically true against `develop` and **materially
misleading**, so it is corrected here.

| Field | Magnum | Wicked Engine |
|---|---|---|
| Exact ref | `refs/remotes/origin/claude/cna-magnum-gr-backend-211xsx` | `refs/remotes/origin/claude/wicked-engine-cna-backend-5ffqzd` |
| Head | `9b903db8cf16988e3fbc955a429bab6c6a5b191e` | `91d8587e9a1a760c3275713f15f65bfafa387082` |
| **Real fork point** | **`2338b44f7` on `feature/audit`** (2026-08-03, `docs(remediation): complete REMED-GFX-212`) | **the same `2338b44f7`** |
| Merge base with `develop` | `ac3aaaeb` | `ac3aaaeb` |
| Ahead / behind `develop` | 768 / 0 | 765 / 0 |
| **Own commits** (not in `feature/audit`) | **13** | **10** |
| **Behind `feature/audit`** | **22 commits** | **22 commits** |
| Files changed from fork point | 45 | 16 |
| Touches `GraphicsDevice.cpp` / `IGraphicsBackend.hpp` / `GraphicsCapability.hpp` | ✔ / ✔ / — | — / — / — |
| Contains `fc0dd2a2` | **yes** | **yes** |
| Tip subject | `docs(plan_magnum): record the verified baseline and the decisions behind it` | `feat(plans/plan_wicked.md WICKED-32/31/58/28): buffer hazards, upload stalls, multi-stream, mips` |
| Last commit (UTC) | 2026-08-04T09:40:54Z | 2026-08-04T09:18:36Z |
| Plan file | `plans/plan_magnum.md` | `plans/plan_wicked.md` |
| Task namespace | `MAGNUM-*` | `WICKED-*` |
| External dependency | `cmake/ThirdPartyMagnum.cmake` (Magnum/Corrade) | `cmake/ThirdPartyWicked.cmake` + `cmake/patches/wicked-sdl3-platform.patch` |

**What this means for integration.** Each needs a **22-commit rebase onto the checkpoint base**
before anything else — they were cut from `feature/audit` one day before the WEBGPU-115 and
GFX-215/216/217/218/219 work landed. After that rebase their adaptation is small: Wicked touches none
of the three shared interfaces, Magnum touches two.

**What is NOT established.** Neither branch was checked out, built, tested, merged, rebased or
cherry-picked by this session. **Nothing here says either is complete or integration-ready.** A run
of feature-shaped commit subjects and a tip that says "record the verified baseline" is
*development* evidence, not *integration-readiness* evidence — establishing the latter needs the
per-branch adaptation checklist and a build, neither of which this reconciliation ran.
Development-complete and integration-ready are **different claims**; only the weaker one has any
support here, and even that is inference from commit subjects.

The user states Direct2D is now the only lane still actively developed. That is **consistent** with
these two: both stopped ~2 hours before Direct2D's latest commit and neither has moved since.
Consistency is not proof of completion.

The ref names contain the literal token `claude` because that is what the refs are called. That is a
factual ref name, not a contributor attribution, and it stays.

---

## 5. `feature/direct2d` — the final actively-developed lane

| Field | Value |
|---|---|
| Local ref | `refs/heads/feature/direct2d` @ **`6cd6ad06`** |
| Remote ref | `refs/remotes/origin/feature/direct2d` @ **`e341758f`** (718 ahead) — **local is 34 commits ahead of the remote** |
| Worktree | `/rv/data/development/github.com/openeggbert/cnadirect2d` |
| Merge base with `develop` | `ac3aaaeb` |
| Ahead / behind `develop` | **752 / 0** |
| Tip subject | `test(Task D2D-83/84): verify CPU-fallback sampling against the D2D-81 oracle` |
| Last commit (UTC) | **2026-08-04T11:27:08Z** |
| Plan file | `plans/plan_direct2d.md` (also touches `plans/plan_sdlgpu.md`) |
| Shared interfaces | `GraphicsDevice.cpp`, `IGraphicsBackend.hpp`, `GraphicsCapability.hpp` — all three |

**Git evidence that it is still moving, recorded rather than assumed.** The head observed at the
start of this session was `f6edeb7c`; by the time the inventory was derived it was `6cd6ad06`, dated
**~2 minutes earlier**. Its last six commits (`677e7e41` … `6cd6ad06`) all landed on 2026-08-04
between 10:30 and 11:27 UTC, on sequential `D2D-78` … `D2D-84` task IDs. It is the newest commit in
the entire 21-lane inventory.

**It is not called finished.** The branch's own record does not declare completion and the user has
not confirmed a final head. Per the user's statement it is expected to finish soon; until it does,
its head is a moving target.

**This does not invalidate the checkpoint.** The checkpoint is the *base onto which branches are
later adapted*, so it can be taken while a lane is still moving. **Actual branch integration should
wait until Direct2D is frozen at a known head** — integrating a moving branch converts one
adaptation into a recurring one.

---

## 6. `feature/gl` — cross-repository integration lane

The required EasyGL and MetaGL implementation work is **complete**. These are **not** unfinished
feature developments. What is outstanding is purely that their completed branches have not been
merged into their repositories' `develop` branches, so CNA `feature/gl` currently builds against
**non-`develop` revisions**.

### 6.1 The evidence — re-verified after a fresh fetch in each repository

`git fetch --all --prune --tags` was run read-only in `easy-gl`, `meta-gl` and
`meta-gl-followup-audit` (all three working trees clean; all exit 0; no ref changes). **`easy-glrvc`
was deliberately not fetched** — its working tree carries a pre-existing uncommitted modification, so
it fell outside the "clean tree" precondition. It shares the `easy-gl` remote, which was refreshed.

| Repository | Completed branch | Head | Ahead of `develop` | Behind | Merged? | `develop` head |
|---|---|---|---|---|---|---|
| **MetaGL** (`meta-gl`) | **`feature/followup-audit`** | `d5bc155` (2026-07-19) | **16 commits** | 0 | **NO** | `d51fcd7` (2026-07-18) |
| **EasyGL** (`easy-gl`) | **`rvc`** | `b52f671` (2026-07-19) | **5 commits** | 0 | **NO** | `62c0a24` (2026-07-19) |

All four heads are **unchanged** from the previous snapshot — the fetch added nothing.

**Exact branch names matter.** EasyGL's branch is `rvc` — no `feature/` prefix. MetaGL's is
`feature/followup-audit`, checked out at the sibling path `meta-gl-followup-audit`.

The dependency chain, read from the build files rather than assumed:

- `cnagl feature/gl` → `cmake/BackendSelection.cmake:157` requires `../easy-glrvc`, described in its
  own `FATAL_ERROR` text as "a separate git checkout (**branch `rvc` of easy-gl**)". Line 134 marks
  it `GLB-7 TEMPORARY` and points at `GLB-38` for the switch back to `../easy-gl`.
- **Correction to the previous record.** The MetaGL redirect is **not** in `easy-glrvc`'s committed
  history. `git show HEAD:CMakeLists.txt` in `easy-glrvc` reads `add_subdirectory(../meta-gl meta-gl)`
  — MetaGL `develop`, the same line `easy-gl` `develop` uses. The redirect
  (`#add_subdirectory(../meta-gl meta-gl)` + `add_subdirectory(../meta-gl-followup-audit meta-gl)`)
  exists **only as an uncommitted working-tree modification** to that file. The previously recorded
  "the redirect exists at both levels" was describing the working tree, not the branch.

So the *committed* `rvc` branch already targets MetaGL `develop`; only the **working configuration
currently in use** builds against `meta-gl-followup-audit`. The MetaGL merge is still required,
because that working configuration is what `feature/gl` is actually built against today.

### 6.2 Mandatory order

1. Identify the completed MetaGL branch and head — **`feature/followup-audit` @ `d5bc155`**.
2. **Preserve that head for provenance** (archive tag, per §8).
3. Adapt/merge it into MetaGL `develop`.
4. Validate MetaGL `develop`.
5. Identify the completed EasyGL branch and head — **`rvc` @ `b52f671`**.
6. **Preserve that head for provenance.**
7. Adapt/merge it into EasyGL `develop`. **Reconcile `easy-glrvc`'s uncommitted `CMakeLists.txt`
   modification first** — decide whether the `meta-gl-followup-audit` redirect is discarded (it
   should be, once step 3 lands) or committed.
8. Validate EasyGL `develop`, including its MetaGL dependency.
9. Update CNA `feature/gl` to the resulting EasyGL and MetaGL `develop` revisions — this is
   `GLB-38`: repoint `cmake/BackendSelection.cmake` from `../easy-glrvc` back to `../easy-gl`.
10. Build and test CNA `feature/gl`.
11. Integrate CNA `feature/gl` into the CNA integration branch.

**None of these merges was performed in this session, and none may be performed autonomously.**
`plans/plan_glbackends.md` records the same constraint independently: *"`GLB-38` … **Decided: leave to the
project owner — do not attempt to merge/push between repos autonomously.** (Reconfirmed 2026-07-20
morning.)"*

---

## 7. Historical snapshot — 19 lanes at `099b03c0` (labelled, superseded)

Retained for provenance. **Do not quote as current.** Derived 2026-08-04 from local refs **without a
fetch**, which is why it could not see the two `claude/*` remote lanes. Its four rename mappings
(`diligent-engine-backend-cna-01cs23` → `feature/diligent`, `llgl-gr-backend-cna-3orpwo` →
`feature/llgl`, `sokol-gfx-cna-backend-8s3oo8` → `feature/sokol`,
`noxna-graphics-api-extension-lihfjk` → `origin/feature/ext`) were inferred from matching base,
commit count and touched-file signature, not from a rename record, and remain strong inferences
rather than certainties.

**The method defect worth keeping.** That snapshot searched only refs already present locally and
never refreshed them. A branch inventory that does not fetch first cannot be authoritative — the
count is not the lesson, the missing fetch is.

---

## 8. Commit-history policy for integration — **mandatory**

Applies to all future integration of CNA, EasyGL and MetaGL branches.

### 8.1 Provenance

- **Original branch heads must be preserved** through archive tags or equivalent immutable,
  non-destructive references **before any adaptation begins**.
- **Do not destructively rewrite the only copy of an original branch history.** Ever.
- Produce a **range-diff** (or equivalent provenance report) between the original and adapted
  history for each integrated branch.

### 8.2 Clean commit messages

The final integrated `develop` history **must not contain AI attribution**. Remove from adapted
commit messages and trailers:

- `CC OK`
- `authored by Claude` / `generated by Claude`
- `Claude Code`
- `Co-authored-by: Claude`
- `Anthropic`
- any AI / bot / agent status or contributor text

**Claude must not appear** as author, as committer attribution added by the integration process, as
co-author, or as a contributor trailer in the final adapted history.

**Actual original Git ref names containing `claude`** (`claude/html-dom-cna-backend-xefzwf`,
`claude/cna-magnum-gr-backend-211xsx`, `claude/wicked-engine-cna-backend-5ffqzd`) **remain valid
provenance identifiers.** A ref name is not a contributor attribution; do not confuse the two.

### 8.3 Form

- Clean technical commit messages only.
- Logical commit grouping.
- **All adapted commits GPG-signed.**

### 8.4 Entry conditions for the first adaptation branch

1. The signed phase-1 checkpoint tag exists (`REMEDIATION_EXIT.md` §8).
2. `feature/direct2d` is **frozen at a known head** confirmed by the project owner (§5).
3. The archive tag for the lane being adapted has been created and pushed.
4. The lane's `GpuDrawParams` adaptation (§3.1) is planned per `plans/plan_postaudit.md` §10.

**No branch merge, rebase, cherry-pick, history rewrite or archive-tag creation was performed during
this reconciliation, and none is authorized by it.**
