# BATCH_0_COMPLETE.md — final Batch 0 closeout and four-lane checkpoint

Companion to `INTEGRATION_ORDER.md` (§3 Batch 0, §5 checkpoints), `INTEGRATION_HISTORY_POLICY.md`,
`INTEGRATION_BRANCH_INVENTORY.md` and the four lane cards under `integration/lanes/`.

**Session date:** 2026-08-04 · **Scope:** closeout and provenance verification only — **no lane was
integrated, no production or test file was modified, `audit/` was not touched.**

---

## 1. The two Batch 0 checkpoints are different things

Batch 0 produced **two** signed checkpoints, and they are not interchangeable. Both are correct;
they mark different states.

| | A — intermediate stabilization checkpoint | B — final Batch 0 checkpoint |
|---|---|---|
| Tag | **`integration/checkpoint-batch0-20260804`** | **`integration/checkpoint-batch0-complete-20260804`** |
| Target | **`e0332214`** | **`990d6b8a`** |
| Lanes at that state | **3** — `depthcrt`, `gltf`, `ext` | **4** — the same three **plus `dxold`** |
| What it certifies | the batch's *process* — history policy, authorship, signing, range-diff reporting — plus the `NOXNA.md` cross-reference repair and the `XnbContainerFuzzTest` oracle fix, with a green full `CnaTests` | the *complete* Batch 0 content — all four lanes integrated, `dxold`'s eight Historical backends and the owner-ordered naming transition included |
| Record | `integration/BATCH_0_STABILIZATION.md` | this document |

**A is not incorrect and must not be moved, recreated, retargeted or deleted.** It is an accurate,
signed record of the state it was taken at. It predates `dxold` because `dxold` was deliberately
sequenced *after* it — `BATCH_0_STABILIZATION.md` §5 says so in its own words: *"`dxold` … is Batch
0's last lane and belongs **after** this checkpoint."*

The one thing worth stating plainly is a **wording** inconsistency inside that document, not a defect
in the tag: its §11 *"Status after this checkpoint"* opens with *"**Batch 0 complete** — 3 lanes
integrated + stabilization checkpoint taken"* while the same document, five sections earlier, names
`dxold` as the batch's remaining lane. Read in context it means *"the batch's process-validation
objective is complete"*, not *"the batch's lane set is complete"*. A banner now marks that document
as the intermediate checkpoint so the sentence cannot be quoted forward as a four-lane claim.

---

## 2. Start-gate state, measured this session

`git fetch --all --prune --tags` → **exit 0, no ref added, updated or pruned.**

| Field | Value |
|---|---|
| Repository | `git@github-openeggbert:openeggbert/cna` (single remote, `origin`) |
| Git common dir | `/rv/data/development/github.com/openeggbert/cna/.git` |
| Integration branch | `integration/post-audit-phase1` |
| Integration HEAD, local | **`990d6b8a`** — unchanged during this session |
| Integration head, `origin` | **`61bd1a1b`** — the `depthcrt` merge. The `gltf`, `ext`, stabilization and `dxold` commits are **local and unpushed** |
| Integration worktree | `/rv/data/development/github.com/openeggbert/cnaintegration` — **clean**, no `.lock`, single writer |
| Planning worktree | `/rv/data/development/github.com/openeggbert/cnaaudit` @ `feature/audit` |
| Phase-1 checkpoint | `d79214e7`, tag `cna-post-audit-remediation-phase1` — **verified ancestor** |
| Local tags | **24** — 21 `archive/preintegration/*`, `audit-2026-07-complete`, `cna-post-audit-remediation-phase1`, `integration/checkpoint-batch0-20260804` |
| Tags on `origin` | **1** — `audit-2026-07-complete`. All 21 archive tags and both checkpoint tags are **local only** |
| Adaptation branches | `adapt/depthcrt` `3cca0b19` · `adapt/ext` `c6a28036` · `adapt/dxold` `9256e606` — all retained, all worktrees clean. `gltf` needed none |
| Four pre-existing user stashes | **not listed, inspected, altered, dropped or rewritten** |

### 2.1 Required ancestry — all six anchors verified

`d79214e7` · `e0332214` · `61bd1a1b` · `722a2f5a` · `8a374b9f` · `990d6b8a` — every one is an
ancestor of (or is) the integration HEAD.

### 2.2 Lane count, re-derived rather than quoted

Re-derived from refs after the fetch, by the §2 methodology of `INTEGRATION_BRANCH_INVENTORY.md`:

| | |
|---|---|
| Logical lanes | **21** |
| Integrated | **4** — `depthcrt`, `gltf`, `ext`, `dxold` |
| Pending | **17** |

**N is a snapshot, not an invariant.** Re-derive it after any fetch rather than quoting it forward.

---

## 3. Batch 0 shape

| | |
|---|---|
| Commits `d79214e7..990d6b8a` | **48** — 44 non-merge + **4** lane merges |
| Lane merges | exactly four, one per lane, all `--no-ff`, all signed |
| Tree growth | 5234 → **5456** files |
| Total delta | **284 files changed, +52 951, −1 951** |

```
990d6b8a  merge  dxold      2026-08-04T21:19:28+02:00
e0332214  fix    stabilization — XnbContainerFuzzTest oracle
f742341b  docs   stabilization — NOXNA cross-reference repair
8a374b9f  merge  ext        2026-08-04T17:50:07+02:00
722a2f5a  merge  gltf       2026-08-04T17:17:16+02:00
61bd1a1b  merge  depthcrt   2026-08-04T15:59:25+02:00
d79214e7  phase-1 checkpoint
```

**No fifth lane began or partially merged.** Verified two ways: no pending lane's head is an
ancestor of the integration HEAD (17 lanes swept), and the tree contains no `Backends/Stub`,
`StubGraphicsBackend` or any other pending lane's marker path. Only three `adapt/*` branches exist,
matching the three *adapted* lanes exactly (`gltf` was direct-merged and needed none).

---

## 4. Four-lane provenance — re-measured from the object database

Every row below was measured this session, not carried forward from a lane card.

| Lane | Original head | Original ref state | Archive tag | Merge base | Path | Integration merge |
|---|---|---|---|---|---|---|
| `depthcrt` | `f4804469` | local **and** remote still `f4804469` | `archive/preintegration/depthcrt-20260804` → `f4804469`, **Good signature** | `ac3aaaeb` (develop) | **ADAPTED** 6→5 | `61bd1a1b` |
| `gltf` | `86ada7a7` | local **and** remote still `86ada7a7` | `archive/preintegration/gltf-20260804` → `86ada7a7`, **Good signature** | `32639a13` (audit-stacked) | **DIRECT MERGE** — object preserved | `722a2f5a` |
| `ext` | `05ab5d3d` | remote-only, still `05ab5d3d` | `archive/preintegration/ext-20260804` → `05ab5d3d`, **Good signature** | `ac3aaaeb` (develop) | **ADAPTED** 1→1 | `8a374b9f` |
| `dxold` | `36289bb2` | local **and** remote still `36289bb2` | `archive/preintegration/dxold-20260804` → `36289bb2`, **Good signature** | `ac3aaaeb` (develop) | **ADAPTED** 28→35 | `990d6b8a` |

**No original branch history was rewritten. All four archive tags are unchanged and verify good.**

### 4.1 Each merge's tree delta is exactly its own lane's scope

| Merge | Range | Files changed |
|---|---|---|
| `61bd1a1b` `depthcrt` | `d79214e7..61bd1a1b` | **13** |
| `722a2f5a` `gltf` | `61bd1a1b..722a2f5a` | **1** — `gltfissues.md` |
| `8a374b9f` `ext` | `722a2f5a..8a374b9f` | **1** — `NOXNA.md` |
| `990d6b8a` `dxold` | `e0332214..990d6b8a` | **266** — 153 `examples/`, 16 `scripts/`, 13 `cmake/`, 11 each `include/`/`docs/`/`dx2-spike/`, 9 `src/`, 8 `tests/`, the six other `dx*-spike/` dirs, and 18 root-level files (12 `plan_*.md`, `README.md`, `CMakeLists.txt`, `CLAUDE.md`, `NOXNA.md`, `.gitignore`, `plans/plan.md`) |

Nothing outside the four lanes' declared scope entered the branch.

---

## 5. `dxold` — verified independently this session

### 5.1 Identity

| Check | Result |
|---|---|
| Source head | `36289bb2` — `feature/dxold` local **and** `origin/feature/dxold`, both unmoved |
| Archive tag | `archive/preintegration/dxold-20260804` → `36289bb2`, annotated, **Good signature** from `FB9CE8E20AADA55F`, tag body records lane/ref/head/merge-base/checkpoint |
| Merge base | `ac3aaaeb` — matches the tag's own recorded merge base exactly |
| Original inventory | **28** commits in `ac3aaaeb..36289bb2`, **0 merges** |
| Adapted range | **35** commits in `e0332214..9256e606`, **0 merges** |
| Merge | `990d6b8a` — parents `e0332214` + `9256e606`, signed `U`, merged tree **byte-identical** to `adapt/dxold`'s tree (`1ddf0d6d`) |
| Signatures | **35/35 `U`**, all authored **and** committed by `Robert Vokac <robertvokac@robertvokac.com>` |
| Attribution | zero prohibited tokens in the adapted range or the merge metadata |

### 5.2 Losslessness — the central claim, verified where it is made

The lane card's claim is that the **replay boundary** is byte-identical, with every subsequent
transformation isolated in its own reviewed commit. Tested at both points:

| Comparison | Result |
|---|---|
| 210 lane-added files, `36289bb2` vs **`c0cad202`** (the 28th replayed original) | **210 / 210 byte-identical · 0 differing · 0 missing** |
| the same 210 files, `36289bb2` vs **`9256e606`** (adapted head) | 120 identical · 90 differing · **0 missing** |

The 90 differences at the adapted head are the *intended* downstream effect of the seven commits
that follow the replay — the interface adaptation, the two owner-ordered renames and the four
completion fixes. **The claim holds exactly where the card makes it**, and no file was lost at
either point.

### 5.3 Naming transition — executed and complete

| Check | Result |
|---|---|
| `CNA_GRAPHICS_BACKEND` values, checkpoint | 14 — …`ASCII`, **`DX3`** (free-direct-backed), `D3D9`, `SDL_GPU` |
| `CNA_GRAPHICS_BACKEND` values, HEAD | 22 — …`ASCII`, **`FREEDIRECT`**, `D3D9`, **`DX1 DX2 DX3 DX5 DX6 DX7 DX8 D3D10`**, `SDL_GPU` |
| `GraphicsBackendType` enumerators | 14 → 22; added `Dx1 Dx2 Dx5 Dx6 Dx7 Dx8 D3D10 FreeDirect`, with `Dx3` retained and re-pointed at the real DirectX 3 backend |
| Live `DX30` selector, option or enumerator | **none** — `CNA_BACKEND_DX30`, `STREQUAL "DX30"` and `"DX30"` all return zero hits |
| Residual `DX30` tokens | comments, docs, spike records and historical task IDs only (`DX30-0a`…`DX30-0e`, `DX30-83`), deliberately preserved as historical identifiers |

**Public backend delta: exactly +8.** `FREEDIRECT` is the **renamed** free-direct backend, not a
ninth addition — 14 → 22 is +8 whether counted from the CMake selector list or the enum. The
DirectX 3 generation is now covered by two distinct public implementations: `DX3` (real
Microsoft/Wine) and `FREEDIRECT` (reimplementation library).

### 5.4 Validation — confirmed, not re-run

The current integration HEAD **is** the tested merge (`990d6b8a`), and `adapt/dxold`'s tree is
byte-identical to it, so the recorded matrix was confirmed from surviving evidence rather than
re-executed.

| Gate | Evidence |
|---|---|
| Eight dedicated suites total **137** | **Independently re-derived**: `ctest -N -L <backend>` across the eight build trees returns DX1 10, DX2 19, DX3 19, DX5 19, DX6 20, DX7 20, DX8 20, D3D10 10 = **137**. `CTestCostData.txt` — written by every real run and untouched by `-N` — records per-test timings for exactly those same 137 |
| Zero failures in seven of the eight | `LastTestsFailed.log` — written only when a run has failures — is **absent** in `cmake-build-dx2/dx3/dx5/dx6/dx7/dx8/d3d10` |
| DX1 | its `LastTestsFailed.log` was **stale** (20:36, naming `Dx1_No3D` at full-registry index 5629) and predates its own final run's cost data (21:01). Rather than infer, the suite was **re-run live this session on the merged tree**: **10/10 passed, `Dx1_No3D` green**, 27.0 s under Wine on `:99` |
| FREEDIRECT 19/20 | `cmake-build-freedirect/Testing/Temporary/LastTestsFailed.log` names exactly **one** test — `FreeDirect_SpriteBatch` — the declared, control-proven pre-existing residual |
| EasyGL principal baseline | 5906 passed / 6 skipped / 0 failed — the Batch 0 stabilization baseline exactly |
| D3D9 modern-control cross-build | exit 0 |
| New remediation ticket | **none created**, and none warranted |

> **One caveat, stated because it was caused by this session.** The verification used `ctest -N` to
> re-derive the suite sizes, and `-N` **overwrites** `Testing/Temporary/LastTest.log`. The prior
> sessions' verbatim run logs in those nine build trees are therefore gone. `CTestCostData.txt` and
> `LastTestsFailed.log` are not touched by `-N` and survived, which is what the table above rests
> on — together with the live DX1 re-run. No tracked file, no provenance object and no test source
> was affected.

---

## 6. History and signature audit — `d79214e7..HEAD`

| Check | Result |
|---|---|
| Commits in range | **48** |
| Signatures | **48 / 48 `U`** — good signature, uncertified key, this project's normal pass state. Zero `N`, zero `E` |
| Adapted commits signed | 5 `depthcrt` + 1 `ext` + 35 `dxold` = **41 / 41** |
| Integration merges signed | **4 / 4** |
| Directly merged original (`gltf` `86ada7a7`) signed | **yes** |
| Stabilization commits signed | **2 / 2** |
| Distinct authors | **1** — `Robert Vokac <robertvokac@robertvokac.com>` (48/48) |
| Distinct committers | **1** — the same identity (48/48) |
| Trailers | **none anywhere in the range** |
| Attribution sweep (`INTEGRATION_HISTORY_POLICY.md` §8) | **one** hit: the subject `docs(CLAUDE.md): document build-directory convention …`. This is the **tracked filename**, explicitly non-violating per §2.1. Its body was read in full and contains no attribution, no process narration and no session identifier |
| Prohibited AI attribution | **zero** |
| `audit/` modified in range | **0 files** |
| `git diff --check`, working tree and over the range | **clean** |

### 6.1 A campaign-wide signature correction — "204 unsigned" is wrong

`INTEGRATION_BRANCH_INVENTORY.md` §5 records *"592 of 796 commits carry a good GPG signature … **204
carry no signature at all**"*, and flags `opengl4` (28), `magnum` (13) and `wicked` (10) as still
needing object-level re-derivation. That re-derivation was performed here — **for all 21 lanes, not
just the three** — by reading each commit object's header rather than relying on `%G?`:

| | Commits |
|---|---|
| Total own commits, 21 lanes | **796** |
| Maintainer **PGP** signature | **592** |
| **SSH** signature | **204** |
| **Genuinely unsigned** | **0** |

**All 204 carry an SSH signature, and all 204 carry the identical embedded `ssh-ed25519` public
key** — `…rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs`, the non-maintainer key first identified on
the `ext` lane. The count 204 is right; its classification is not.

Per lane (PGP / SSH): `ext` 0/1 · `gltf` 1/0 · `stub` 5/0 · `depthcrt` 0/6 · `wicked` 0/10 ·
`magnum` 0/13 · `opengles1` 24/2 · `dxold` 25/3 · `opengl4` 0/28 · `gl` 28/0 · `opengl1` 31/0 ·
`glide` 32/0 · `gdi` 34/0 · `sokol` 14/23 · `opengl2` 40/0 · `direct2d` 48/0 · `html-dom` 21/34 ·
`diligent` 28/37 · `llgl` 21/47 · `metal` 99/0 · `skia` 141/0.

**No required action changes.** Policy A4 demands a maintainer GPG signature on every adapted
commit; an SSH signature from a non-maintainer key never satisfied it, which is why `ext`, `depthcrt`
and `dxold` were all re-signed regardless. What changes is the description:

- the **`SIGNATURE-ONLY CLEANUP`** class label is a misnomer for `html-dom`'s 17 — they are not
  unsigned commits needing a signature added, they are SSH-signed commits needing a maintainer
  re-sign, exactly like the `AUTHOR/TRAILER` class;
- the `%G?` blind spot is now confirmed on **every** lane it could apply to, not two. Never derive a
  signature class from `%G?`; read the object.

---

## 7. Corrections recorded rather than silently applied

| Where | Statement | Correction |
|---|---|---|
| `990d6b8a` merge body | *"as **32** signed commits on `adapt/dxold`"* | The adapted range is **35** (measured). The 32 excludes the three validation-driven completion fixes, and its inline enumeration accounts for 31. **The merge object is deliberately left unmodified** — it is signed, it is the batch's closing object, and amending published integration history is forbidden. The authoritative count is 35, recorded on the lane card and here |
| `integration/lanes/dxold.md` §Adaptation record | enumerates the 35 by name but lists only **34** — `55e1269f`, `8a1e801e`, `dd4806f0`, `acb085a8`, `d6a9bd32`, `9256e606` and the 28 originals | **`618afbcf`** (`fix(FREEDIRECT): rename the one compound CTest label the FREEDIRECT rename missed`) was missing from both the enumeration and the mapping table. A lane card is living integration documentation, so this is **corrected in place** |
| `integration/BATCH_0_STABILIZATION.md` §11 | *"Batch 0 complete — 3 lanes integrated"* | Scoped and banner-marked as the **intermediate** checkpoint (§1). The tag itself is correct and untouched |

**A false positive, checked and dismissed.** `docs/graphics-backend-feature-matrix.md` lists none of
the eight new backends, which looks like a `dxold` documentation gap. It is not: the file is
**byte-identical across `develop`, the checkpoint and HEAD**, and its own title scopes it to the
*established* backends (SDL_Renderer, EasyGL, Vulkan, Bgfx, D3D9, D3D11, D3D12), explicitly
excluding experimental and niche ones. It never covered the old `DX3`/free-direct backend either.
No finding.

---

## 8. Carried forward unchanged

### 8.1 `REMED-CONTENT-007` / `REMED-CONTENT-008`

**OPEN · HIGH / P1 · not touched by this session and not fixable by it.**

- **`REMED-CONTENT-007`** — `SongContentTypeReader.cpp` and `VideoContentTypeReader.cpp` each define
  a private `ResolveRelativeFilePath()` with no containment check, fed by the `.xnb`'s own embedded
  filename. `include/CNA/Internal/PathContainment.hpp` exists and is unused by either.
- **`REMED-CONTENT-008`** — `ContentManager.cpp` makes zero calls to `IsDisallowedAbsolutePath` /
  `ResolveContainedPath`, while joining eight manifest-supplied path fields onto the content root raw.

**Non-blocking for Batch 0, re-checked rather than waved through:** none of the four integrated lanes
touches any file either finding lives in — `dxold`'s 266 paths contain no `ContentManager.cpp`,
`ContentReader.cpp`, `SongContentTypeReader.cpp`, `VideoContentTypeReader.cpp` or `PathContainment.hpp`.

**Required before any public security-clean claim or release** (`INTEGRATION_ORDER.md` §5, §6).
Recommended placement remains a **parallel safety lane during Batch 0–1**: the fix is mechanical,
touches `Content/` only, and `Content/` is touched by none of the 21 lanes.

### 8.2 `feature/direct2d`

**OWNER-FROZEN at `9b17e783`** · **FROZEN INCOMPLETE / EXPERIMENTAL** · corrected exact recount:
96 of 128 `D2D-*` tasks incomplete (the original Batch 0 prose said 88)
carry no completion mark · no further development · **not automatically integration-ready** · does
**not** block any other lane. Local and remote refs agree; `archive/preintegration/direct2d-20260804`
verifies good. If the head is ever observed at anything other than `9b17e783`, stop and report the
movement rather than silently updating the frozen reference.

### 8.3 `feature/gl`

MetaGL development **complete**; EasyGL development **complete**. Outstanding, in order
(`INTEGRATION_BRANCH_INVENTORY.md` §7.4):

1. restore/remove the temporary EasyGL `rvc` `CMakeLists.txt` redirect — throwaway **local build
   configuration**, use `git restore`/`git checkout --`, **never `git stash`**;
2. create the missing signed archive tag for the completed EasyGL `rvc` head — the campaign's one
   remaining provenance gap, blocked only by step 1;
3. MetaGL `feature/followup-audit` → MetaGL `develop`;
4. EasyGL `rvc` → EasyGL `develop`;
5. CNA `feature/gl` updated to both `develop` revisions (**`GLB-38`**);
6. CNA `feature/gl` integrated.

**Steps 3–5 are owner-only** and that decision has not been lifted.

**EasyGL is internal and hidden — a support library, never a user-selectable CNA backend.** The
public backends `feature/gl` supplies are exactly **four**:

| # | Public backend |
|---|---|
| 1 | **OpenGL ES 3** |
| 2 | **OpenGL 3** |
| 3 | **WebGL 1** |
| 4 | **WebGL 2** |

Do not count or expose EasyGL as a fifth.

### 8.4 `dxold` residuals

| Residual | Classification |
|---|---|
| `FreeDirect_SpriteBatch` (zero-alpha, rotation-by-pi) | **checkpoint residual** — control-proven on pre-rename binaries; the rotation half is a real production defect on the FreeDirect backend, previously recorded, deliberately not fixed by the lane |
| DX1 full-`CnaTests`-under-Wine reds | **documented structural boundary** of a 2D-only backend, the same class as FREEDIRECT/SDL_RENDERER/ASCII at the base; `docs/dx1-backend.md` §7a carries the breakdown. The dedicated 10-test suite is DX1's declared gate and is green |
| Sanitizer coverage of the eight new backends | **absent by platform** — MinGW/Wine targets, the same classification phase 1 applied to D3D9/D3D11/D3D12. Post-adaptation the lane's native footprint is identity-only plus tests |

---

## 9. Completion gates

| Gate | Result |
|---|---|
| `git fetch --all --prune --tags` | ✅ exit 0, nothing moved |
| Four lanes integrated, verified from objects | ✅ §4 |
| `dxold` provenance complete and lossless | ✅ §5, 210/210 at the replay boundary |
| All signatures good | ✅ 48/48 `U` |
| Zero prohibited attribution | ✅ §6 |
| Prior integration history unchanged | ✅ six ancestry anchors verified; no rebase, amend, reset or force-update |
| `integration/checkpoint-batch0-20260804` unchanged | ✅ still → `e0332214` |
| Two checkpoints documented distinctly | ✅ §1 |
| Dynamic lane count refreshed | ✅ 21 / 4 / 17 |
| Exactly four lanes; no fifth began | ✅ §3 |
| No production, test or `audit/` change | ✅ documentation only |
| `git diff --check` | ✅ clean |
| Worktrees clean | ✅ all |
| Nothing pushed | ✅ |

---

## 10. Next — Batch 1 first lane

**Selected: `stub`.** The proposal was re-derived from refs rather than assumed; see
`INTEGRATION_ORDER.md` §3 *Batch 1* for the measured comparison against `opengles1`, `opengl4`,
`opengl1` and `opengl2`, the reasons, the adaptation scope and the model recommendation.

**Batch 1 has not begun.** No branch was created, no commit adapted, no lane worktree made.
