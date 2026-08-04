# INTEGRATION_BRANCH_INVENTORY.md — dynamic pending-branch inventory

> **Current inventory as of `099b03c0` (2026-08-04): 19 logical pending integration branches/lanes.**
>
> This is a **snapshot derived from Git refs**, not an invariant. It will change. Do not quote the
> number without its commit and date. Regenerate it (§2) rather than carrying it forward.

---

## 1. Why this file exists

`plan_postaudit.md` §10 recorded "the nineteen integration branches" on 2026-08-03. That number is
still 19 — but **the membership has changed**, and treating "19" as a fixed invariant hid the change:

| Recorded 2026-08-03 | State at `099b03c0` |
|---|---|
| `origin/claude/diligent-engine-backend-cna-01cs23` | **ref no longer exists** → `feature/diligent` |
| `origin/claude/llgl-gr-backend-cna-3orpwo` | **ref no longer exists** → `feature/llgl` |
| `origin/claude/sokol-gfx-cna-backend-8s3oo8` | **ref no longer exists** → `feature/sokol` |
| `origin/claude/noxna-graphics-api-extension-lihfjk` | **ref no longer exists** → `origin/feature/ext` |

Four of nineteen refs were renamed or promoted. The count coinciding at 19 is exactly the kind of
false stability a fixed number produces. The mapping above is **inferred from matching profiles**
(base, commit count, touched-file signature), not from a rename record — see §2's caveat.

---

## 2. Methodology

Derived from `git for-each-ref` over `refs/heads/` and `refs/remotes/origin/` at `099b03c0`.

**Counted:** every branch that is not an ancestor of `develop`, deduplicated so a local branch and
its remote tracking ref pointing at the same logical work count **once**.

**Not counted:** `develop`, `master`, the remediation branch `feature/audit` itself, archive tags,
already-merged branches, and the prunable stale worktree `/tmp/cnaaudit-gfx098-prefix` (detached
HEAD, not a feature branch).

**Verified:** no ref in the list is an ancestor of `develop` — nothing here is already merged.

**Caveat on the rename mapping in §1:** the four old `origin/claude/*` refs are gone, so no
`git` rename record survives. The mapping is inferred from identical merge bases and matching
touched-file signatures (notably `origin/feature/ext` matching the old
`noxna-graphics-api-extension` profile exactly: 1 commit, touching none of the three shared
interface files). Treat it as a strong inference, not a certainty.

---

## 3. The 19 lanes

All 19 share the same merge base: **`ac3aaaeb`**, the current `develop` head.

`GD` = touches `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` ·
`IGB` = touches `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` ·
`GC` = touches `include/CNA/GraphicsCapability.hpp`

| # | Lane | Head | Ahead | Last commit | GD | IGB | GC | State | Conflict class |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `feature/skia` | `ca046f01` | 848 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 2 | `claude/html-dom-cna-backend-xefzwf` | `8e4e4293` | 752 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 3 | `feature/gdi` | `adc9cc2a` | 741 | 2026-08-03 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 4 | `feature/glide` | `2f9b47e1` | 739 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 5 | `feature/direct2d` | `f11f2560` | 738 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 6 | `feature/llgl` | `65327813` | 716 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 7 | `feature/diligent` | `1ab12b50` | 715 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 8 | `feature/sokol` | `261ea700` | 687 | 2026-08-04 | ✔ | ✔ | ✔ | **active development** | **HIGH** |
| 9 | `feature/gltf` | `86ada7a7` | 555 | 2026-07-28 | ✔ | ✔ | ✔ | settled | **HIGH** |
| 10 | `feature/metal` | `48928d11` | 99 | 2026-07-21 | ✔ | — | — | settled, **unbuilt (no Mac)** | MEDIUM |
| 11 | `feature/opengl2` | `77d36d9e` | 40 | 2026-07-20 | ✔ | ✔ | ✔ | integration-ready | **HIGH** |
| 12 | `feature/opengl1` | `fc14f37b` | 31 | 2026-07-20 | ✔ | ✔ | — | integration-ready | MEDIUM |
| 13 | `feature/opengl4` | `c49e0ba2` | 28 | 2026-07-22 | ✔ | ✔ | — | integration-ready | MEDIUM |
| 14 | **`feature/gl`** | `f8efb9b4` | 28 | 2026-07-20 | ✔ | ✔ | — | **cross-repository — see §4** | MEDIUM + **external** |
| 15 | `feature/dxold` | `36289bb2` | 28 | 2026-07-21 | — | — | — | integration-ready | **LOW** |
| 16 | `feature/opengles1` | `3d576da2` | 26 | 2026-07-22 | ✔ | — | — | integration-ready | MEDIUM |
| 17 | `feature/depthcrt` | `f4804469` | 6 | 2026-07-22 | — | — | — | integration-ready | **LOW** |
| 18 | `feature/stub` | `a35651e8` | 5 | 2026-07-19 | ✔ | — | — | integration-ready | MEDIUM |
| 19 | `origin/feature/ext` | `05ab5d3d` | 1 | 2026-07-20 | — | — | — | integration-ready, **remote-only** | **LOW** |

### 3.1 Overlap with the post-audit shared-interface changes

Recomputed from refs this session — **all three previously recorded figures still hold**:

- **0 of 19** contain `fc0dd2a2` (`refactor(graphics): unify ordinary and instanced stream descriptions`) — every branch predates the unified representation;
- **16 of 19** modify `GraphicsDevice.cpp`;
- **13 of 19** modify `IGraphicsBackend.hpp`;
- **10 of 19** modify `GraphicsCapability.hpp`;
- exactly **3** touch none of the three: `feature/depthcrt`, `feature/dxold`, `origin/feature/ext`.

Every one therefore still carries the **old** `GpuDrawParams` shape with the four fields
`REMED-GFX-202` removed (`instanceVb`, `instanceVertexOffset`, `instanceFrequency`,
`vertexBufferOffset`). That is a **mechanical adaptation cost**, not a defect and not a ticket.
The per-branch adaptation checklist is `plan_postaudit.md` §10.

### 3.2 Suggested integration order

1. **`feature/depthcrt`, `feature/dxold`, `origin/feature/ext`** — touch none of the three shared
   interfaces. Lowest risk, and they validate the process cheaply.
2. **`feature/stub`, `feature/opengles1`, `feature/opengl1`, `feature/opengl4`, `feature/opengl2`** —
   small, settled, single shared-interface surface.
3. **`feature/gl`** — only after its external chain (§4) completes. Nothing else gates on it.
4. **`feature/metal`** — settled but unbuilt; integrate for source continuity, not verification.
5. **The eight active-development lanes** (`skia`, `html-dom`, `gdi`, `glide`, `direct2d`, `llgl`,
   `diligent`, `sokol`) — **last, and not while still moving.** Each is 687–848 commits ahead and
   touches all three shared interfaces. Integrating a branch that is still being developed converts
   one adaptation into a recurring one.

**`feature/gltf` is the exception to the "active last" rule**: it is content-pipeline work rather
than a backend, so it can be sequenced with group 2 despite its size, provided its `GpuDrawParams`
adaptation is done once.

---

## 4. `feature/gl` — cross-repository integration lane

The required EasyGL and MetaGL implementation work is **complete**. These are **not** unfinished
feature developments. What is outstanding is purely that their completed branches have not been
merged into their repositories' `develop` branches, so CNA `feature/gl` currently builds against
**non-`develop` revisions**.

### 4.1 The evidence

| Repository | Completed branch | Head | Ahead of `develop` | Merged? | `develop` head |
|---|---|---|---|---|---|
| **MetaGL** (`meta-gl`) | **`feature/followup-audit`** | `d5bc155` (2026-07-19) | **16 commits** | **NO** | `d51fcd7` |
| **EasyGL** (`easy-gl`) | **`rvc`** | `b52f671` (2026-07-19) | **5 commits** | **NO** | `62c0a24` |

**Exact branch names matter.** EasyGL's branch is `rvc` — no `feature/` prefix. MetaGL's is
`feature/followup-audit`, checked out at the sibling path `meta-gl-followup-audit`.

The dependency chain, read from the build files rather than assumed:

- `cnagl feature/gl` → `cmake/BackendSelection.cmake:157` requires `../easy-glrvc`, described in its
  own `FATAL_ERROR` text as "a separate git checkout (**branch `rvc` of easy-gl**)". Line 136 marks
  it `GLB-7 TEMPORARY` and points at `GLB-38` for the switch back to `../easy-gl`.
- `easy-glrvc/CMakeLists.txt:17-18` → `#add_subdirectory(../meta-gl meta-gl)` is **commented out** and
  replaced by `add_subdirectory(../meta-gl-followup-audit meta-gl)`.
- `easy-gl` `develop`'s own `CMakeLists.txt:17` uses `../meta-gl` — i.e. MetaGL `develop`.

So the temporary redirect exists at **both** levels, and it unwinds bottom-up.

### 4.2 Mandatory order

1. Identify the completed MetaGL branch and head — **`feature/followup-audit` @ `d5bc155`**.
2. **Preserve that head for provenance** (archive tag, per §6).
3. Adapt/merge it into MetaGL `develop`.
4. Validate MetaGL `develop`.
5. Identify the completed EasyGL branch and head — **`rvc` @ `b52f671`**.
6. **Preserve that head for provenance.**
7. Adapt/merge it into EasyGL `develop`.
8. Validate EasyGL `develop`, including its MetaGL dependency.
9. Update CNA `feature/gl` to the resulting EasyGL and MetaGL `develop` revisions — this is
   `GLB-38`: repoint `cmake/BackendSelection.cmake` from `../easy-glrvc` back to `../easy-gl`, and
   `easy-gl`'s own `CMakeLists.txt` from `../meta-gl-followup-audit` back to `../meta-gl`.
10. Build and test CNA `feature/gl`.
11. Integrate CNA `feature/gl` into the CNA integration branch.

**None of these merges was performed in this session, and none may be performed autonomously.**
`plan_glbackends.md` records the same constraint independently: *"`GLB-38` … **Decided: leave to the
project owner — do not attempt to merge/push between repos autonomously.** (Reconfirmed 2026-07-20
morning.)"*

**Note:** the `easy-glrvc` checkout has an uncommitted local modification to `CMakeLists.txt`. It
belongs to another repository and was left untouched; whoever performs step 7 must reconcile it
first.

---

## 5. Magnum and Wicked Engine

**No Git evidence exists for either, in this repository or the surrounding workspace, as of
`099b03c0`.** Searched:

- all local and remote refs — no matching branch;
- all worktrees — none;
- every `.md` planning document outside `audit/` — no mention;
- the whole repository tree — the single textual hit is an unrelated token inside vendored
  `third_party/SDL/src/gpu/d3d12/SDL_gpu_d3d12.c`;
- the sibling workspace `/rv/data/development/github.com/openeggbert/` — no `*magnum*` or `*wicked*`
  directory;
- the workspace backend catalogues `40backend.md`, `newcnagraphicsbackends.txt`, `cnabackends.txt` —
  no entry in any of them.

**Therefore:** neither is counted in the 19, and no branch name, head or completion status is
recorded for either — inventing one would be worse than recording the absence.

**Action for the project owner:** if Magnum or Wicked Engine work exists, it lives outside this
workspace and must be pointed at explicitly before it can be inventoried or sequenced. Until then
they are, at most, **prospective future lanes** — not pending integration branches.

---

## 6. Commit-history policy for integration — **mandatory**

Applies to all future integration of CNA, EasyGL and MetaGL branches.

### 6.1 Provenance

- **Original branch heads must be preserved** through archive tags or equivalent non-destructive
  references before any adaptation begins.
- **Do not rewrite the original archived branch heads.** Ever.
- Produce a **range-diff** (or equivalent provenance report) between the original and adapted
  history for each integrated branch.

### 6.2 Clean commit messages

The final integrated commits **must not contain AI attribution**. Remove from adapted commit
messages and trailers:

- `CC OK`
- `authored by Claude`
- `generated by Claude`
- `Claude Code`
- `Co-authored-by: Claude`
- `Anthropic`
- any AI / bot / agent status text

**Claude must not appear** as author, as committer attribution added by the integration process, as
co-author, or as a contributor trailer in the final adapted history.

### 6.3 Form

- Clean technical commit messages only.
- Logical commit grouping.
- **All adapted commits GPG-signed.**

No branch-history cleanup was performed during this reconciliation, and none is authorized by it.
