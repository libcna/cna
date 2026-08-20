# INTEGRATION_BRANCH_INVENTORY.md — authoritative dynamic lane inventory

> **Post-campaign external-history addendum (2026-08-09).** The owner subsequently completed and
> pushed the authorized MetaGL/EasyGL public-history rewrite. Current public authority is MetaGL
> `develop` `571d3a62fe166b9781ac6193d137b12ff3757620` (tree
> `a7771c5593a4ec4b71283d38523a0cde3fbf6d4b`) and EasyGL `develop`
> `0b46d35c394a9fb6aea6a85c6587894b5013da33` (tree
> `e89ff546d3782e2b32e02f4b9dc56da42c4c463a`). Those trees equal the content accepted during
> Batch 4. The external archive tags were also rewritten and are now unsigned targets with legacy
> annotations; old external SHAs and signature/tag claims below remain dated evidence of the
> integration event, not current dependency pins or current-ref assertions. No further external
> rewrite is planned. CNA's own 21 original refs/archive tags and all checkpoint history are
> unchanged. See `integration/FINAL_RECONCILIATION.md`.
>

> **Current inventory as of the 2026-08-04T14:31+02:00 fetch, against checkpoint
> `cna-post-audit-remediation-phase1` @ `d79214e7`: 21 logical integration lanes.**
>
> **Update 2026-08-09 (latest — Metal acceptance): all twenty-one lanes are integrated and zero
> remain pending.** The unchanged local and remote 99-commit `feature/metal` head
> `48928d113cb864f78d754256d2d559d914d4f1a7` remains behind its sole signed annotated archive
> (object `43f6eab8`, GPG-good). The authoritative history map accounts every original row:
> 88 signed chronological replays and 11 explained omissions. Replay range-diff is 76 `=` / 12
> `!` / 11 omitted; six post-audit commits then produce 94 signed adaptation commits at
> `e2ffe7290`. Signed `--no-ff` merge **`012b158e`** has parents `4ac696c7` and `e2ffe729`, and its
> tree is byte-identical to the adaptation.
>
> `METAL` is CNA's genuine 41st identity: direct native Objective-C++/MSL on macOS, without a
> renderer fallback. The adapted contract conservatively disables known-wrong backbuffer readback,
> MSAA, MRT, queries, custom effects, multistream and instancing. Historical macOS 14/Xcode 15.4
> evidence is 136/143 at production commit `e0f424268`; it is not adapted-source evidence. Current
> portable Metal validation is 206/206 unique and 207/207 CTest, sanitizer 206/206, OPENGLES/EasyGL
> 124+1 and 2/2, LLGL 48/48 and 3/3. No adapted Apple compile/runtime is claimed. Group G is 4/4
> and technical Batch 6 stabilization is READY. The final retake passed; signed annotated tag
> `integration/checkpoint-batch6-20260809` was created once without force with exact message
> `CNA integration Batch 6 checkpoint`. Tag object
> `8d347c933a3da3c39f22711e40e80cf7a29c4682` peels to `012b158e`; `git tag -v` exits 0 with a Good
> signature from Robert Vokac under fingerprint
> `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. It is local only and was not pushed. **Batch 6
> checkpoint status is COMPLETE**; no final campaign/`develop` readiness claim is made. See
> `integration/lanes/metal.md` and `integration/BATCH_6_STABILIZATION.md`.
>
> **Update 2026-08-09 (previous — LLGL acceptance): twenty lanes are integrated and exactly one
> remains pending: `metal`.** The unchanged 68-commit `feature/llgl` head
> `fa26e72dcda612de2a8cff814e748c7479e45836` remains behind its sole signed annotated archive.
> All 68 meaningful commits were replayed chronologically onto the Direct2D integration head,
> followed by signed stabilization commit `c74fbaeb`; signed `--no-ff` merge **`4ac696c7`** has
> parents `21b1fcd1` and `c74fbaeb`. Range-diff accounts 20 `=` and 48 `!` pairs: 47 include
> patch/context adaptation, 47 include required author cleanup, and 46 overlap. Dates/subjects are
> preserved and attribution/trailer sweeps are empty.
>
> The historical sharp-runtime i686 `__int128` observation is **classification A, non-gating**:
> the concrete route is Glide's x86 ABI probe, while CNA LLGL has no i686/Windows configure, test
> or public contract. sharp-runtime was not changed. `LLGL` is CNA's genuine 40th public identity,
> supported as LLGL OpenGL -> native OpenGL/GLX on Linux/X11 x86_64. Complete LLGL CTest is
> 145 registered / 137 passed / 8 disabled; full units are 5210/5203/7; focused ASan/UBSan 9/9,
> EasyGL controls 9/9 + 15/15, and accepted Direct2D controls 4/4. No supported-path LLGL defect
> remains. Batch 6 / Group G is 3/4; Metal was not begun and no checkpoint was created. See
> `integration/lanes/llgl.md`.
>
> **Update 2026-08-08 (previous — Direct2D acceptance): nineteen lanes are integrated and exactly
> 2 remain pending: `llgl` and `metal`.** The original owner-frozen `feature/direct2d` head remains
> unchanged at `9b17e783` behind its sole signed archive. Forty-eight chronological commits were
> replayed, followed by seven signed adaptation commits on `adapt/direct2d` at `1b740d96`. Signed
> `--no-ff` merge **`7af760be`** has parents `c805fd73` and `1b740d96`; its tree equals the
> adaptation exactly. Signed documentation-only `D2D-54` precision commit **`21b1fcd17`** is the
> current integration head. All 55 range commits, the merge, and that follow-up are GPG-good.
> Direct2D is CNA's genuine
> 39th identity: Windows-only Direct2D 1.1 drawing, with D3D11/DXGI limited to hosting and
> presentation. The x64 MinGW/Wine/Xvfb Direct2D label passes 4/4, including 19/19 unit tests;
> focused OPENGLES, GDI, and HTML DOM controls pass. No known supported-path Direct2D defect
> remains. Batch 6 / Group G is 2/4 (Skia + Direct2D integrated); LLGL and Metal were not begun,
> and no checkpoint was created. See `integration/lanes/direct2d.md`.
>
> **Update 2026-08-08 (previous — Batch 5 checkpoint retake): eighteen lanes remain integrated and
> 3 deferred lanes remain pending.** `REMED-CONTENT-007/-008`, the only mandatory blockers at the
> first decision, are DONE; bounded same-pattern finding `REMED-CONTENT-011` is also DONE. Signed
> Content test/fix commits `2d795473`/`c805fd73` move the integration head beyond the HTML DOM merge
> without adding a lane. Final containment **46/46**, relevant Content/Song/Video **116/116**,
> Glide portable **78/78**, and exact HTML DOM host **57/57** controls pass with the recorded
> sanitizer boundary. Local signed annotated tag `integration/checkpoint-batch5-20260808` (object
> `307c9ad511015c64ce55184cdf0d5ebd7b1cb575`) peels to
> `c805fd737f4321568fba378e8d1b8fe5b5270666` and verifies Good. Pending remains exactly
> `direct2d`, `llgl`, `metal`; no nineteenth lane or adaptation began and nothing was pushed.
>
> **Update 2026-08-08 (HTML DOM acceptance and first Batch 5 decision; historical): `html-dom` is
> integrated — eighteen lanes, 3 pending; Batch 5 is COMPLETE at 3 of 3.** The unchanged 55-commit original
> `claude/html-dom-cna-backend-xefzwf` head `8e4e4293` is preserved by the sole signed annotated
> `archive/preintegration/html-dom-20260804`. Forty-nine meaningful commits were replayed
> chronologically (six Canvas-only commits omitted and the HTML hunk of one mixed commit retained),
> then one signed post-audit stabilization commit produced `adapt/html-dom` @ `a32977f3`. Signed
> `--no-ff` merge **`24bf4786`** has parents `ba5fa601` and `a32977f3`; its tree equals the
> adaptation exactly. See `integration/lanes/html-dom.md`.
>
> `HTML_DOM` is CNA's thirty-eighth public identity: a genuine Emscripten-only, 2D DOM/CSS sprite
> backend using pooled `<div>` elements over the SDL browser canvas and private Canvas2D surfaces
> for bounded `RenderTarget2D` support. It is not a Canvas/WebGL/EasyGL/Software alias and has no
> fallback. Current host contracts pass **57/57** and **57/57** with linked ASan/UBSan; the
> OPENGLES/EasyGL principal control is 109 pass + 1 intentional skip, seven focused GDI controls
> exit 0 under Wine/Xvfb, Glide controls pass, and Diligent/Skia/Sokol changed capability sources
> compile. The host lacks Emscripten and Node, so no adapted-browser run is claimed; the unchanged
> implementation retains the original lane's explicitly recorded real-browser evidence.
>
> Pending: `direct2d`, `llgl`, `metal` — **3**. Technical Batch 5 stabilization passes, but the
> checkpoint is **BLOCKED**: §6 explicitly requires the still-open HIGH/P1
> `REMED-CONTENT-007/-008` path-containment findings to close before the Batch 5 checkpoint. No
> `integration/checkpoint-batch5-20260808` tag was created and no nineteenth lane began. See
> `integration/BATCH_5_STABILIZATION.md`.
>
> **Update 2026-08-08 (GDI acceptance): `gdi` is integrated — seventeen lanes, 4 pending; Batch 5 is OPEN
> at 2 of 3.** The unchanged 34-commit original `feature/gdi` head `adc9cc2a` was replayed
> chronologically onto the accepted Glide merge and followed by nine signed technical commits on
> `adapt/gdi` (**43** signed linear commits, head `625f4ad5`). Signed `--no-ff` merge
> **`ba5fa601`** has parents `677f4c59` and `625f4ad5`; its tree equals the adaptation exactly. The
> sole signed annotated `archive/preintegration/gdi-20260804` still peels to the original head.
> Range-diff maps all 34 original commits 1:1 (**18 `=`, 16 `!`**) with no metadata mismatch,
> omission, or reorder. See `integration/lanes/gdi.md`.
>
> GDI is the Windows-only private CPU Software-2D core plus classic Win32 GDI presentation through
> `HWND`, `SetDIBitsToDevice`, and `StretchDIBits`, with no fallback renderer. Its only true
> capabilities are StencilBuffer, WireFrame, and MSAA. Historical and current GDI matrices pass
> **19/19** under Wine; current evidence is x64 MinGW GCC 14/Wine 10/Xvfb, not physical Windows or
> native MSVC. PE32 allocation planners pass 12/12; native Software sanitizer/control and current
> EasyGL, DX3, Sokol, Diligent, Skia, and Glide controls pass within their recorded runtime or
> compile-only boundaries. `REMED-GFX-229` through `-233`, `REMED-BUILD-017/-018`, and GDI-054
> lifetime hardening are resolved for their automated scope; no supported-path GDI defect remains.
>
> Pending: `html-dom`, `direct2d`, `llgl`, `metal` — **4**. Batch 5 still contains HTML DOM, so **no
> Batch 5 checkpoint exists**. Next is HTML DOM.
>
> **Update 2026-08-08 (Glide acceptance): `glide` is integrated — sixteen lanes, 5 pending; Batch 5
> is OPEN at 1 of 3.** The unchanged original `feature/glide` head `2f9b47e1` was replayed as all 32 signed
> commits onto `adapt/glide`, followed by one signed stabilization commit (`e891e105`), then landed
> through signed `--no-ff` merge **`677f4c59`**. The merged tree is byte-identical to the adaptation.
> Public backend identities move token-exact **35 → 36**, adding only genuine 32-bit native-ABI
> `GLIDE`; there is no fallback renderer. Production rendering is build-only/runtime-unavailable on
> this host (no Voodoo hardware or external `glide3x.dll`); 78/78 portable tests, 13/13 shared
> contracts, the 39-export x86 fake-DLL ABI contract, i686 whole-backend syntax, 78/78 ASan/UBSan,
> and five OPENGLES pixel/state controls pass. `REMED-GFX-226`, `-227`, and `-228` are MEDIUM and
> resolved in-lane; carried findings remain unchanged. See `integration/lanes/glide.md`.
>
> Pending: `gdi`, `html-dom`, `direct2d`, `llgl`, `metal` — **5**. Batch 5 still contains `gdi`
> and `html-dom`, so **no Batch 5 checkpoint exists**. GDI has not begun. The first historical-
> baseline CMake helper violated the bounded-parallelism rule with bare `--parallel`; actual
> parallelism at or below eight remains unprovable. Reconciliation classifies this **B**, an
> accepted process deviation: the helper supplied only historical SDL/configure evidence and every
> final engineering gate was independently carried by later monitored `-j4` work. Glide remains
> technically accepted; next is GDI.
>
> **Update 2026-08-07 (latest): `skia` is integrated — fourteen lanes, 7 pending.** `skia` landed
> by adaptation (**151** signed commits on `adapt/skia`, merge `1381ff93`), CNA's **thirty-second**
> public backend identity and the first deliberately **2D-only** one: raster `SkSurface` through an
> SDL streaming texture, `ctest -N -L Accelerated` = 0, 3D capabilities refused rather than
> emulated. Zero interface drift; its own suite 172/172 both before and after adaptation.
>
> **It is the first lane to be blocked, recorded as blocked, and then merged.** The BLOCKED event is
> preserved, not rewritten. Two defects stopped it, both in **shared** code and neither reachable
> from any Skia test: `REMED-GFX-223`, where `Texture2D::ReconstructFromCache` inherited
> `RenderTarget2D`'s `gpuOnlyContent_` for an ordinary cached texture — caught by the **EasyGL**
> principal control; and `REMED-GFX-225`, where the lane's own new `ITextureCubeBackend::GetSizeEXT`
> `noexcept` virtual collided with four pre-existing accessors so that **`CNA_GRAPHICS_BACKEND=SOKOL`
> did not compile at all** — caught only by *building* the Sokol control, since no test run can find
> a compile error in a backend nobody compiled. Both fixed in-lane. `REMED-GFX-224` (EasyGL render
> targets silently discard `SetData`) is OPEN and not a blocker.
>
> Controls: EasyGL 5911/5912, Skia 172/172, row-stride 8/8, `CnaTests` under `SKIA` **124 failures
> against the fork point's 125** (8 fixed, 7 pre-classified), Sokol 37/37 + 34/34, Diligent 169/169,
> ASan+UBSan **0 + 0**. See `integration/lanes/skia.md`. Pending: `direct2d`, `gl`, `gdi`, `glide`,
> `llgl`, `html-dom`, `metal` — **7**. **No checkpoint taken:** Batch 6 / Group G has four members
> and three remain; Batch 4 (`feature/gl` alone) has not been started.
>
> **Update 2026-08-07 (later): `diligent` is integrated — thirteen lanes, 8 pending; Batch 3
> CLOSES 2 of 2.** `diligent` landed by adaptation (70 signed commits on `adapt/diligent`, merge
> `aa9f3fb5`), CNA's **thirty-first** public backend identity and the first whose native graphics
> API is a **runtime** decision rather than a build-time one. Interface drift was a single reference
> to the removed `GpuDrawParams::instanceVb`; a pre-adaptation build at the lane's own fork point
> reproduced its recorded results exactly (61/53/7/1), so the lane started from a proven baseline.
> Validation found four defects, all adaptation-owned and all fixed in-lane, and no lane-owned
> supported-path defect. See `integration/lanes/diligent.md` and
> `integration/BATCH_3_STABILIZATION.md`. Pending: `direct2d`, `gl`, `gdi`, `glide`, `llgl`,
> `skia`, `html-dom`, `metal` — **8**.
>
> **Update 2026-08-07: `sokol` is integrated — twelve lanes, 9 pending; Batch 3 opens 1 of 2.**
> `sokol` landed by adaptation (44 signed commits on `adapt/sokol`, merge `37066e45`), CNA's
> **thirtieth** public backend identity. Interface drift was two references to the removed
> `GpuDrawParams::instanceVb`; a pre-adaptation build at the lane's own fork point reproduced its
> recorded results exactly, so the lane started from a proven baseline rather than an unknown.
> Validation fixed three defects in-lane and corrected one capability report on measurement. See
> `integration/lanes/sokol.md`. Pending: `direct2d`, `gl`, `diligent`, `gdi`, `glide`, `llgl`,
> `skia`, `html-dom`, `metal` — 9. **`diligent` is next.**
>
> **Update 2026-08-06: `magnum` is integrated — eleven lanes, 10 pending; Batch 2 closes 2 of
> 2.** `magnum` landed by adaptation (19 signed commits on `adapt/magnum`, merge `e7d46c4c`)
> after its first-ever build and execution; validation fixed two defects in-lane (the
> adaptation's own guard-space error, and the `MAGNUM-65` sprite-flush ArrayView overread the
> lane had carried invisibly — ASan-caught, radeonsi-faulted). See
> `integration/lanes/magnum.md`. Pending: `direct2d`, `gl`, `diligent`, `gdi`, `glide`, `llgl`,
> `skia`, `sokol`, `html-dom`, `metal` — 10.
>
> **Update 2026-08-05 (late): `wicked` is integrated — ten lanes, 11 pending.** `wicked` landed by
> adaptation (17 signed commits on `adapt/wicked`, merge `683a00a5`) after its two HIGH blockers
> were repaired in-lane; see `integration/lanes/wicked.md` §15.
>
> **Earlier update — nine lanes integrated, 12 pending:** `depthcrt`
> (adapted, merge `61bd1a1b`), `gltf` (direct merge `722a2f5a`), `ext` (adapted `c6a28036`,
> merge `8a374b9f`), `dxold` (adapted `9256e606`, merge `990d6b8a` — closes Batch 0), `stub`
> (adapted `c29ef117`, merge `99ae7d11`), `opengles1` (adapted `b811d76d`, merge `df6b7cc6`),
> `opengl4` (adapted `3f1035de`, merge `bc29a976`), `opengl1` (adapted `91344935`, merge
> `c0876fca`) and `opengl2` (adapted `289410a6`, merge `9e6d62ed` — closes Batch 1). Pending:
> `direct2d`, `gl`, `magnum`, `wicked`, `diligent`, `gdi`, `glide`, `llgl`, `skia`, `sokol`,
> `html-dom`, `metal`. Re-derived from refs after `git fetch --all --prune --tags` on 2026-08-05
> (the Batch 1 stabilization session's fetch moved nothing); all 21 lane heads confirmed unmoved.
> The per-lane data is otherwise unchanged; see §10.1.
>
> **Do not measure integrated lanes with `git merge-base --is-ancestor`.** A direct merge keeps the
> original commit object, an adaptation replays it as a new one, so an ancestry sweep reports only
> `gltf` — the one direct merge — and undercounts the other **eight**. The authoritative instrument
> is the set of merge commits on `integration/post-audit-phase1`
> (`git log --merges d79214e7..HEAD`), cross-read against the lane cards.
>
> **N is a snapshot, not an invariant.** It is derived from Git refs immediately after
> `git fetch --all --prune --tags`. If any ref moves, N and every number below may change.
> Regenerate with §2 rather than quoting this forward.

**Supersedes** `remediation/INTEGRATION_BRANCH_INVENTORY.md` (the 2026-08-04 exit-reconciliation
snapshot, also 21 lanes). That document remains valid as a historical record; **its per-lane
`Ahead` column and its HIGH/LOW conflict classes are superseded by §4 here**, for the reason given
in §3.

---

## 1. Start-gate state

| Field | Value |
|---|---|
| Repository | `git@github-openeggbert:openeggbert/cna` (single remote, `origin`) |
| Git common dir | `/rv/data/development/github.com/openeggbert/cna/.git` |
| Session worktree | `/rv/data/development/github.com/openeggbert/cnaaudit` |
| Session branch | `feature/audit` |
| Session HEAD | `d79214e7600c0411ce912be11f8e762866be23ee` |
| `git status --short` | empty (clean) |
| Checkpoint commit | `d79214e7` — GPG **good** signature, Robert Vokac, key `FB9CE8E20AADA55F` |
| Checkpoint tag | `cna-post-audit-remediation-phase1` → tag object `8fc33512` → `d79214e7`, **annotated and GPG-signed**, verifies good |
| `REMEDIATION_EXIT.md` | `STATUS: AUTHORITATIVE — OUTCOME A, READY. Phase-1 checkpoint taken.` Blocker set **EMPTY** |
| Fetch | exit 0. Every ref reported `[up to date]`. **No ref was added, updated or pruned by this session.** |
| Integration base | `origin/develop` @ `ac3aaaeb`. The checkpoint is **779 commits ahead** of it |

**Pre-existing working-tree state in other worktrees, recorded and not touched:**

| Worktree | Branch | Pre-existing changes |
|---|---|---|
| `.../cna` | `develop` | ` M cmake/Tests/EasyGLTests.cmake`, ` M cmake/Tests/SdlRendererTests.cmake`, `?? examples/xvfb_screenshot_demo.cpp` |
| `.../cnahtmldom` | `claude/html-dom-cna-backend-xefzwf` | ` M third_party/SDL` (submodule pointer) |
| `.../easy-glrvc` | `rvc` | ` M CMakeLists.txt` (the MetaGL redirect — §7.3) |

All other worktrees are clean. Four pre-existing user stashes exist and were **not** listed
contents-wise, inspected, altered, dropped or rewritten.

`/tmp/cnaaudit-gfx098-prefix` is a **stale prunable** worktree entry (detached HEAD, not a feature
branch). It is excluded from the inventory and was **not** pruned by this session.

---

## 2. Methodology

Derived from `git for-each-ref` over `refs/heads/` and `refs/remotes/origin/` **after** a successful
fetch.

**Counted:** every ref that is not an ancestor of the integration base `origin/develop`,
deduplicated so a local branch and its remote-tracking ref pointing at the same logical work count
**once**.

**Not counted:** `develop`, `master`, `feature/audit` itself (it *is* the checkpoint base, not a
lane to integrate into it), all tags, and the stale prunable `/tmp` worktree entry.

**Deduplication actually applied.** 18 lanes exist as an identical local branch *and*
remote-tracking ref pair — one lane each. Three lanes are **remote-only**: `origin/feature/ext`,
`origin/claude/cna-magnum-gr-backend-211xsx`, `origin/claude/wicked-engine-cna-backend-5ffqzd`.
**`feature/direct2d`'s local and remote refs are now identical** (`9b17e783`); the previous snapshot
recorded local 34 ahead of remote, so it has since been pushed. No two lanes share a head.

**Verified:** `git merge-base --is-ancestor <ref> origin/develop` is non-zero for all 21 — nothing
listed is already merged.

---

## 3. The measurement correction that changes this inventory

The previous snapshot measured every lane's size and footprint as a diff against **`origin/develop`**.
That is arithmetically correct and **materially misleading**, and it was applied to only two lanes
(Magnum, Wicked) when in fact it distorts **eleven**.

Measured against the **checkpoint** instead, eleven lanes turn out to be **stacked on
`feature/audit`**, not forked from `develop`. They carry hundreds of the remediation campaign's own
commits, which the develop-relative diff attributes to the lane.

| Lane's real fork point | Commits ahead of `develop` at that point | Lanes stacked there |
|---|---|---|
| `2338b44f` | 755 | Magnum, Wicked |
| `a7a49e3d` | 707 | **skia, direct2d, gdi, glide** |
| `f5645c64` | 697 | **html-dom** |
| `1eb22c11` | 650 | **diligent, llgl, sokol** |
| `32639a13` | 554 | **gltf** |
| `ac3aaaeb` (`develop`) | 0 | depthcrt, dxold, gl, metal, opengl1, opengl2, opengl4, opengles1, stub, ext |

All five non-`develop` fork points are verified ancestors of the checkpoint and non-ancestors of
`develop`.

**What this changes, concretely.** Three examples where the develop-relative reading was wrong
enough to misdirect planning:

- **`feature/gltf`** read as "555 commits ahead, touches all three shared interfaces, **HIGH**".
  Its real contribution over the checkpoint is **one commit changing one file**, a documentation
  note (`docs: analyze glTF rendering issues`), touching **none** of the three interfaces. Its
  implementation is already in the checkpoint. It is the cheapest lane in the inventory, not one of
  the most expensive.
- **`feature/direct2d`** read as "752 ahead, all three interfaces, **HIGH**". Its real contribution
  is **48 commits over 25 files** touching **`GraphicsDevice.cpp` only**.
- **`feature/skia`** read as 848 ahead; its real contribution is **141 commits**. Still the largest
  lane, but by a factor of six less than stated.

Every number in §4 is measured from each lane's **own** merge base with the checkpoint.

---

## 4. The 21 lanes

`GD` = `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` ·
`IGB` = `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp` ·
`GC` = `include/CNA/GraphicsCapability.hpp`

Ordered by own-commit count ascending — which is close to, but not identical with, integration order
(§ `INTEGRATION_ORDER.md`).

| # | Lane | Refs | Head | Fork pt | Own | Files | Shared ifaces | Dev status | Readiness | Conflict |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **`ext`** | remote-only `origin/feature/ext` | `05ab5d3d` | `ac3aaaeb` | 1 | 1 | — | DEVELOPMENT COMPLETE | ✅ **INTEGRATED** (adapted `c6a28036`, merge `8a374b9f`) | **LOW** — one real conflict, see `integration/lanes/ext.md` |
| 2 | **`gltf`** | local + remote `feature/gltf` | `86ada7a7` | `32639a13` | 1 | 1 | — | DEVELOPMENT COMPLETE | ✅ **INTEGRATED** (direct merge `722a2f5a`) | **LOW** — confirmed, no conflict |
| 3 | **`stub`** | local + remote `feature/stub` | `a35651e8` | `ac3aaaeb` | 5 | 15 | GD | DEVELOPMENT COMPLETE | ✅ **INTEGRATED** (adapted `c29ef117`, merge `99ae7d11`) | **LOW** — one union conflict in `cmake/BackendSelection.cmake`; see `integration/lanes/stub.md` |
| 4 | **`depthcrt`** | local + remote `feature/depthcrt` | `f4804469` | `ac3aaaeb` | 6 | 14 | — | DEVELOPMENT COMPLETE | ✅ **INTEGRATED** (merge `61bd1a1b`) | **LOW** — confirmed, zero conflicts |
| 5 | `wicked` | remote-only `origin/claude/wicked-engine-cna-backend-5ffqzd` | `91d8587e` | `2338b44f` | 10 | 16 | — | BUILDS AND RUNS — `WICKED-77`/`-78`/`-79` all fixed in-lane | ✅ **INTEGRATED 2026-08-05** — adapted on `adapt/wicked` (17 signed, `97d5a644`), merge `683a00a5` | **LOW** — confirmed: 6-file registration union, **zero** probe drift; see `integration/lanes/wicked.md` §15 |
| 6 | `magnum` | remote-only `origin/claude/cna-magnum-gr-backend-211xsx` | `9b903db8` | `2338b44f` | 13 | 45 | GD IGB | BUILDS AND RUNS — current pinned Corrade/Magnum route validated | ✅ **INTEGRATED 2026-08-06** — 13 replay + 6 adaptation/stabilization commits on `adapt/magnum` (`b7fe9b24`), signed merge `e7d46c4c`; genuine 29th identity, current GL runtime and full-corpus controls; see `integration/lanes/magnum.md` | MEDIUM — registration/interface adaptation plus two validation-found defects resolved in-lane; merged tree equals adaptation |
| 7 | `opengles1` | local + remote `feature/opengles1` | `3d576da2` | `ac3aaaeb` | 26 | 23 | GD | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-05** — adapted, merge `df6b7cc6` | MEDIUM |
| 8 | **`dxold`** | local + remote `feature/dxold` | `36289bb2` | `ac3aaaeb` | 28 | 225 | — | DEVELOPMENT COMPLETE | ✅ **INTEGRATED** (adapted `9256e606`, merge `990d6b8a`) | **LOW** — confirmed; the real surface was 6 shared files already superseded at the base |
| 9 | `opengl4` | local + remote `feature/opengl4` | `c49e0ba2` | `ac3aaaeb` | 28 | 41 | GD IGB | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-05** — adapted, merge `bc29a976` | MEDIUM — confirmed: six-file registration union + 13 probe-found interface drifts; see `integration/lanes/opengl4.md` |
| 10 | **`gl`** | local + remote `feature/gl` | `f8efb9b4` | `ac3aaaeb` | 28 | 51 | GD IGB | BUILDS AND RUNS — accepted MetaGL/EasyGL chains integrated under owner authority | ✅ **INTEGRATED 2026-08-07** — 26 retained replay + 4 adaptation commits on `adapt/gl`, signed merge `0a51f864`; genuine OPENGLES/OPENGL33/WEBGL1/WEBGL2 identity family, Batch 4 checkpoint taken; see `integration/lanes/gl.md` | **CROSS-REPOSITORY** — completed owner-controlled MetaGL → EasyGL → CNA sequence; current external and shared registration contracts preserved |
| 11 | `opengl1` | local + remote `feature/opengl1` | `fc14f37b` | `ac3aaaeb` | 31 | 43 | GD IGB | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-05** — adapted, merge `c0876fca` | MEDIUM — confirmed: six-file registration union + 10 probe-found drifts + three post-fork test-contract collisions; see `integration/lanes/opengl1.md` |
| 12 | `glide` | local + remote `feature/glide` | `2f9b47e1` | `a7a49e3d` | 32 | 46 | GD IGB GC | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-08** — `adapt/glide` 33 signed commits (`e891e105`), signed merge `677f4c59`; build-only/runtime-unavailable classification; see `integration/lanes/glide.md` | **HIGH** — confirmed: 3 conflict stops, 10 file-conflict events / 9 unique files; semantic adaptation preserved current stream arrays and Texture2D authority |
| 13 | `gdi` | local + remote `feature/gdi` | `adc9cc2a` | `a7a49e3d` | 34 | 81 | GD IGB GC | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-08** — `adapt/gdi` 43 signed commits (`625f4ad5`), signed merge `ba5fa601`; x64 MinGW/Wine 19/19, no physical-Windows/MSVC claim; see `integration/lanes/gdi.md` | **HIGH** — confirmed: all three shared interfaces plus broad Software-2D/GraphicsDevice/build adaptation; current contracts authoritative |
| 14 | `sokol` | local + remote `feature/sokol` | `261ea700` | `1eb22c11` | 37 | 57 | GD | BUILDS AND RUNS — pre-adaptation baseline reproduced its own recorded results | ✅ **INTEGRATED 2026-08-07** — adapted on `adapt/sokol` (44 signed, `9fb83a99`), merge `37066e45` | MEDIUM — confirmed: nine-file registration union + **two** probe-found drifts (both `instanceVb`); see `integration/lanes/sokol.md` |
| 15 | `opengl2` | local + remote `feature/opengl2` | `77d36d9e` | `ac3aaaeb` | 40 | 63 | GD IGB GC | DEVELOPMENT COMPLETE | ✅ **INTEGRATED 2026-08-05** — adapted, merge `9e6d62ed` | **HIGH** — confirmed: six-file registration union + 14 probe drifts (two of them the lane's own interface additions) + one real production finding (flipped RT round trips, fixed in-lane); see `integration/lanes/opengl2.md` |
| 16 | **`direct2d`** | local + remote `feature/direct2d` | `9b17e783` | `a7a49e3d` | 48 | 25 | GD | historical owner freeze preserved; boundedly unfrozen for integration | ✅ **INTEGRATED 2026-08-08** — 48 replay + 7 adaptation commits on `adapt/direct2d` (`1b740d96`), signed merge `7af760be`; genuine Direct2D 1.1 identity, x64 MinGW/Wine 4/4; see `integration/lanes/direct2d.md` | MEDIUM — four explained range-diff adaptations; current shared draw/Texture2D contracts preserved |
| 17 | `html-dom` | local + remote `claude/html-dom-cna-backend-xefzwf` | `8e4e4293` | `f5645c64` | 55 | 50 | IGB GC | historical real-browser matrix recorded; current host contracts green, adapted browser rebuild unavailable without Emscripten/Node | ✅ **INTEGRATED 2026-08-08** — 49 chronological replays + one stabilization commit on `adapt/html-dom` (`a32977f3`), signed merge `24bf4786`; genuine Emscripten-only DOM/CSS identity; see `integration/lanes/html-dom.md` | **HIGH** — confirmed: 11 file-conflict resolutions across registration, shared interface/SpriteBatch, capability and tests; current contracts authoritative |
| 18 | `diligent` | local + remote `feature/diligent` | `1ab12b50` | `1eb22c11` | 65 | 56 | GD IGB | BUILDS AND RUNS — pre-adaptation baseline reproduced its own recorded results | ✅ **INTEGRATED 2026-08-07** — adapted on `adapt/diligent` (70 signed, `27f7dcef`), merge `aa9f3fb5` | MEDIUM — confirmed: the campaign's widest registration union (17 files on commit 1) + **one** probe-found drift (`instanceVb`); see `integration/lanes/diligent.md` |
| 19 | `llgl` | local + remote `feature/llgl` | `fa26e72d` | `1eb22c11` | 68 | 135 | GD | Linux/X11 x86_64 LLGL OpenGL supported; i686 record classified non-contract Glide coverage | ✅ **INTEGRATED 2026-08-09** — 68 replay + one stabilization commit on `adapt/llgl` (`c74fbaeb`), signed merge `4ac696c7`; genuine 40th identity, 145/137/0/8 dedicated, 5210/5203/7 corpus, 9/9 sanitizer; see `integration/lanes/llgl.md` | MEDIUM — 20 `=` + 48 accounted `!` range-diff pairs; current stream/lifetime/capability contracts preserved |
| 20 | `metal` | local + remote `feature/metal` | `48928d11` | `ac3aaaeb` | 99 | 59 | GD | macOS-only source-continuity contract; adapted Apple validation remains external | ✅ **INTEGRATED 2026-08-09** — 88 replay + 6 post-audit commits on `adapt/metal` (`e2ffe729`), signed merge `012b158e`; genuine 41st identity, 206/207 portable, 206/206 sanitizer, EasyGL and LLGL controls; see `integration/lanes/metal.md` | MEDIUM — 76 `=` + 12 accounted `!` + 11 mapped omissions; current streams/capabilities/lifetime preserved or conservatively narrowed |
| 21 | `skia` | local + remote `feature/skia` | `ca046f01` | `a7a49e3d` | 141 | 254 | GD IGB GC | DEVELOPMENT COMPLETE (Ganesh paused, SKIA-163) | ✅ **INTEGRATED 2026-08-07** — merge `1381ff93`, the fourteenth lane; `adapt/skia` (**151** signed, `a071e1e2`), Skia suite 172/172, **zero** probe drift. Blocked first on `REMED-GFX-223` (shared `Texture2D` cache-reconstruction state, caught by the EasyGL control) and then on `REMED-GFX-225` (its own new `GetSizeEXT` virtual broke the **SOKOL build outright**, caught by the Sokol control); both fixed here. `REMED-GFX-224` left OPEN, not a blocker. See `integration/lanes/skia.md` | **HIGH** — confirmed, but 10 conflicted files / 6 union resolutions |

**Totals:** 796 own commits across 21 lanes; **21/21 integrated, zero pending; Batch 6 checkpoint
COMPLETE**. Every original lane is 0 behind `develop`; no lane was merge-blocked by `develop`
drift.

### 4.1 Shared-interface footprint, recomputed from real fork points

| Interface | Lanes touching it | Which |
|---|---|---|
| `GraphicsDevice.cpp` | **15** | skia, direct2d, gdi, glide, llgl, diligent, sokol, magnum, metal, opengl1, opengl2, opengl4, gl, opengles1, stub |
| `IGraphicsBackend.hpp` | **10** | skia, html-dom, gdi, glide, diligent, magnum, opengl1, opengl2, opengl4, gl |
| `GraphicsCapability.hpp` | **5** | skia, html-dom, gdi, glide, opengl2 |
| **none of the three** | **5** | **gltf, wicked, dxold, depthcrt, ext** |

### 4.2 `GpuDrawParams` adaptation cost

`fc0dd2a2` (`refactor(graphics): unify ordinary and instanced stream descriptions`, 736 commits
ahead of `develop`) removed four fields — `instanceVb`, `instanceVertexOffset`, `instanceFrequency`,
`vertexBufferOffset`.

- **Magnum and Wicked contain `fc0dd2a2`** and already build against the unified representation.
- **The other 19 lanes do not**, including every lane stacked at `a7a49e3d`/`f5645c64`/`1eb22c11`/
  `32639a13` — those fork points all predate it. Being audit-stacked reduces a lane's *size*; it
  does **not** exempt it from this adaptation.
- **`depthcrt`, `dxold`, `ext`, `gltf` and `stub` reference none of the four fields anywhere in their
  changed files** — verified by grep, not assumed. Their `GpuDrawParams` adaptation cost is zero.
  *(`stub` added 2026-08-04 at the Batch 0 closeout: it had simply never been measured, not been
  measured and found costly. Measure a lane before assuming it pays.)*

---

## 5. Commit-history and attribution audit

Every lane's range from its own merge base with the checkpoint to its head was inspected for
authors, committers, GPG status, merge commits, WIP/fixup subjects, and attribution text
(`CC OK`, `Claude`, `Claude Code`, `Anthropic`, `generated by`, `authored by`, `Co-authored-by`,
`bot`, `agent`, `AI`).

**Zero merge commits and zero WIP/fixup/squash subjects across all 21 lanes.** Every lane is a
linear run of finished commits.

**Signature totals:** 592 of 796 commits carry a good GPG signature (reported `U` — good signature,
uncertified key, which is the project's normal state). ~~**204 carry no signature at all.**~~

> ### ⚠ Corrected 2026-08-04 at the Batch 0 closeout — **"204 unsigned" is wrong**
>
> The open re-derivation this table asks for below (`opengl4`, `magnum`, `wicked`) was performed —
> **for all 21 lanes**, by reading each commit object's header rather than trusting `%G?`:
>
> | | Commits |
> |---|---|
> | Total own commits, 21 lanes | **796** |
> | Maintainer **PGP** signature | **592** |
> | **SSH** signature | **204** |
> | **Genuinely unsigned** | **0** |
>
> **All 204 carry an SSH signature, and all 204 carry the identical embedded `ssh-ed25519` public
> key** — `…rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs`, the non-maintainer key first identified on
> `ext`. The count 204 is right; the classification is not.
>
> Per lane (PGP / SSH): `ext` 0/1 · `gltf` 1/0 · `stub` 5/0 · `depthcrt` 0/6 · `wicked` 0/10 ·
> `magnum` 0/13 · `opengles1` 24/2 (**re-derived at integration: the "2" are SSH-signed by a
> non-maintainer key, NOT unsigned — zero genuinely unsigned in this lane**) · `dxold` 25/3 ·
> `opengl4` 0/28 · `gl` 28/0 · `opengl1` 31/0 ·
> `glide` 32/0 · `gdi` 34/0 · `sokol` 14/23 · `opengl2` 40/0 · `direct2d` 48/0 · `html-dom` 21/34 ·
> `diligent` 28/37 · `llgl` 21/47 · `metal` 99/0 · `skia` 141/0.
>
> **No required action changes.** Policy A4 demands a maintainer GPG signature on every adapted
> commit; an SSH signature from a non-maintainer key never satisfied it — which is why `ext`,
> `depthcrt` and `dxold` were each re-signed regardless. Two descriptions do change:
>
> - the **`SIGNATURE-ONLY CLEANUP`** label is a misnomer for `html-dom`'s 17 — they are not unsigned
>   commits needing a signature *added*, they are SSH-signed commits needing a maintainer *re-sign*;
> - the `%G?` blind spot is confirmed on **every** lane it can apply to. Never derive a signature
>   class from `%G?`; read the object. Method and full figures: `integration/BATCH_0_COMPLETE.md` §6.1.

| Classification | Lanes | Detail |
|---|---|---|
| **HISTORY CLEAN** | `skia` (141), `direct2d` (48), `gdi` (34), `glide` (32), `gltf` (1), `opengl2` (40), `opengl1` (31), `stub` (5), `metal` (99) | 100 % authored **and** committed by Robert Vokac, 100 % GPG-signed, no attribution text. `metal`'s four regex hits are all references to the **filename `CLAUDE.md`**, a real tracked file — not attribution |
| **MESSAGE CLEANUP REQUIRED** | `gl` (28) | 100 % Robert-authored and signed, but five commit **bodies** carry narrative text: `"decision, not autonomous agent action"`, `"found another agent already implemented…"`, `"…(commit 14109db, co-authored by…)"` describing an *easy-gl* commit, `"reconcile with other-agent commits"`. These are prose about process, not trailers — they still must not survive into the final history |
| **AUTHOR/TRAILER CLEANUP REQUIRED — total** | `opengl4` (28/28), `depthcrt` (6/6), `ext` (1/1), `magnum` (13/13), `wicked` (10/10) | **Every** commit authored *and* committed by `Claude <noreply@anthropic.com>`, **all unsigned**, with `Co-Authored-By: Claude …` and `Claude-Session: https://claude.ai/code/…` trailers |

> **"All unsigned" is wrong for at least one lane — corrected at `ext`'s integration, 2026-08-04.**
> `ext`'s single commit `05ab5d3d` carries a `gpgsig -----BEGIN SSH SIGNATURE-----` block, SSH-signed
> with `ssh-ed25519 …rLzsfFISF4by8Q+FKz27YpkK1USsBB+mamu1QkJnbDs` — **not** the maintainer's GPG key.
> It changed nothing about the required action (an SSH signature from a foreign key is not a
> maintainer GPG signature, so the commit had to be re-signed regardless), but it does invalidate the
> method behind this row.
>
> `%G?` reported **`N`** for it, not the `E` that `INTEGRATION_HISTORY_POLICY.md` §8 predicts for an
> SSH-signed commit under an unconfigured `gpg.ssh.allowedSignersFile` — the `error:` goes to stderr
> and the status still degrades to `N`. **A `%G?` tally cannot distinguish "no signature" from "SSH
> signature that cannot be checked."** Details: `integration/lanes/ext.md` §"Correction to the
> inventory".
>
> **Re-derivation DONE 2026-08-04 at the Batch 0 closeout.** `opengl4` (28 SSH), `magnum` (13 SSH)
> and `wicked` (10 SSH) were re-checked at the object level, along with the other 18 lanes: **none
> of the 796 commits is unsigned; 204 are SSH-signed with one non-maintainer key.** The "all
> unsigned" row above is therefore wrong for **every** lane it names. See the corrected totals
> block above and `integration/BATCH_0_COMPLETE.md` §6.1.
| **AUTHOR/TRAILER CLEANUP REQUIRED — partial** | `llgl` (47/68), `diligent` (37/65), `sokol` (23/37), `html-dom` (17/55), `dxold` (3/28), `opengles1` (2/26) | Mixed histories: the listed count is `Claude <noreply@anthropic.com>`-authored and unsigned; the remainder is Robert-authored and signed |
| **SIGNATURE-ONLY CLEANUP** | `html-dom` (17 further commits) | Authored **and** committed by Robert Vokac with **no GPG signature** and no attribution text. `html-dom` is the only lane with this class: 55 commits = 21 signed + 17 Robert-unsigned + 17 Claude-unsigned |

**187 commits are `Claude <noreply@anthropic.com>`-authored across 11 lanes.** None may survive into
the final integrated history as author, committer, co-author or trailer. See
`INTEGRATION_HISTORY_POLICY.md`.

**Ref names are not attribution.** `claude/html-dom-cna-backend-xefzwf`,
`origin/claude/cna-magnum-gr-backend-211xsx` and `origin/claude/wicked-engine-cna-backend-5ffqzd`
are the literal names of the refs. They are recorded as factual provenance identifiers and stay.

---

## 6. `feature/direct2d` — historical owner freeze, now integrated

| Field | Value |
|---|---|
| Local ref | `refs/heads/feature/direct2d` @ **`9b17e783e74e87a3f23b9cc47bd3c7cd6dad9d81`** |
| Remote ref | `refs/remotes/origin/feature/direct2d` @ **the same `9b17e783`** — local and remote agree |
| Worktree | `/rv/data/development/github.com/openeggbert/cnadirect2d`, clean |
| Merge base with checkpoint | `a7a49e3d` (707 ahead of `develop`) · with `develop`: `ac3aaaeb` |
| Own commits / files | **48 / 25** · ahead of `develop` 755, behind 0 |
| Tip | `fix(Task D2D-87): flush pending drawing before RenderTarget2D SetData` |
| Tip timestamp | **2026-08-04T14:12:27+02:00** — before this session's start gate (14:31), and unchanged across every re-check during the session |
| Shared interfaces | **`GraphicsDevice.cpp` only** — not `IGraphicsBackend.hpp`, not `GraphicsCapability.hpp` |
| History | **CLEAN** — 48/48 Robert-authored, committed and GPG-signed; no attribution text; no merges; no WIP |
| Archive tag | `archive/preintegration/direct2d-20260804` → `9b17e783`, annotated, GPG-signed, verifies good, **local only** |

**Owner-confirmed FROZEN.** The project owner has stated no further development will be performed on
this branch. The head above was determined from Git in this session, not carried forward from the
previous inventory — which recorded `6cd6ad06`; exactly three further commits (`701ea9e2`,
`09411e77`, `9b17e783`, the D2D-85/86/87 sequence) landed after that snapshot and before the
freeze.

**Frozen is not complete, and the branch's own record says so.** `plans/plan_direct2d.md` at
`9b17e783` contains **128 `D2D-*` task rows: 32 `✅`, 35 `🟨`, and 61 `⬜`**. Because both yellow
and blank are incomplete under the plan's own rule, the exact frozen count is **96 incomplete**,
not the stale 88 previously repeated here. Its own banner reads:
*"Audit z 2026-08-03 vyvrátil předchozí závěr, že zbývají pouze nativní validační brány. Bylo
nalezeno 100 konkrétních otevřených úkolů `D2D-34` až `D2D-133`."*

| Question | Answer |
|---|---|
| Completion report on the branch? | **No.** `plans/plan_direct2d.md` + `docs/direct2d-backend.md` record an explicitly open backlog |
| Build/test evidence? | **Partial.** `Direct2D_Smoke`, `Direct2D_2DParity`, `Direct2D_Lifetime` passed 3/3 in 21.56 s under Wine on Xvfb (2026-08-03, MinGW). The plan itself states this run does **not** cover the found defects, and that `CnaTests.exe` unit tests are not in the `Direct2D` label |
| Known unrelated blocker | `CnaTests.exe` fails to link on missing `enet/enet.h` even with `CNA_ENABLE_NET=OFF` — recorded on the branch as not-Direct2D-owned |
| Historical status before authorization | **Merely frozen.** Readiness **NOT READY** |
| Dependencies | None on another lane. Windows/Wine/MinGW toolchain for its test gate |
| Common-interface conflicts | `GraphicsDevice.cpp` only — narrower than previously recorded |
| Expected adaptation cost | Moderate: 48 commits, 25 files, clean signed history (no rewrite needed), plus the standard `GpuDrawParams` adaptation |

**Bounded unfreeze and integration, 2026-08-08.** The owner explicitly authorized only the
integration and stabilization of the existing Direct2D lane. The original ref remained frozen and
unchanged. `adapt/direct2d` replayed all 48 commits onto `c805fd73`, added seven signed commits, and
reached `1b740d96`; signed `--no-ff` merge `7af760be` is the nineteenth lane merge. The final tree
preserves current shared `GraphicsDevice`/`IGraphicsBackend`/Texture2D semantics, exposes the
genuine 39th backend identity, and has no known supported-path Direct2D defect. Exact provenance,
freeze-reason classification, capability and test matrices, resolved findings, and external gates
are in `integration/lanes/direct2d.md`.

The immutable-fact rule still applies: do not move or rewrite `feature/direct2d` or its sole archive
tag. Integration happened through the separate adaptation history.

---

## 7. `feature/gl` — cross-repository lane

> **INTEGRATED 2026-08-07 — Batch 4 CLOSED.** Merge `0a51f8647`; MetaGL develop `c964e736`;
> EasyGL develop `9b831dee`; EasyGL rvc archive tag `archive/preintegration/easygl-rvc-20260807`
> created (the §7.5 gap is closed). **Correction to §7.1's "no attribution cleanup is required":
> wrong at the object level** — 15/16 MetaGL and 5/5 EasyGL completed commits carry
> `Co-authored-by: Junie` trailers, so both develop integrations were performed as
> trailer-stripped replays (trees byte-identical; as-authored heads preserved on their branches
> and archive tags; owner-scoped range cleanliness — see `integration/lanes/gl.md`).

### 7.0 What `feature/gl` actually adds to CNA's public surface

**EasyGL is internal and hidden.** It is a support library, not a CNA backend a user selects.

The public backends supplied by this lane are exactly **four**:

| # | Public backend |
|---|---|
| 1 | **OpenGL ES 3** |
| 2 | **OpenGL 3** |
| 3 | **WebGL 1** |
| 4 | **WebGL 2** |

`easy-gl` and `meta-gl` are the layers those four are built on, and both appear in §7.1 only because
their repositories gate the lane. **Do not count or expose EasyGL as an additional public CNA
backend**, in this document, in capability matrices, or in any release description.

**MetaGL and EasyGL implementation development for this lane is complete.** Neither project needs
further feature implementation here. What is outstanding is that their completed histories have not
been adapted into their repositories' `develop` branches, so CNA `feature/gl` builds against
non-`develop` revisions.

### 7.1 The three repositories

| Repository | Path | Fetch | `develop` | Completed branch | Head | Ahead | Behind | Merged? |
|---|---|---|---|---|---|---|---|---|
| **MetaGL** | `.../meta-gl` (+ linked worktree `.../meta-gl-followup-audit`) | exit 0, no ref change | `d51fcd7f` | **`feature/followup-audit`** | **`d5bc155f`** | **16** | 0 | **NO** |
| **EasyGL** | `.../easy-gl` (+ linked worktree `.../easy-glrvc`) | exit 0, no ref change | `62c0a248` | **`rvc`** | **`b52f671379c0fe6d71d8c091ee4334c348beec8e`** | **5** | 0 | **NO** |
| **CNA** | `.../cnaaudit` | exit 0 | — | `feature/gl` | `f8efb9b4` | 28 | 0 | **NO** |

Both external repositories' completed histories are **100 % Robert Vokac-authored and GPG-signed**
(MetaGL 16/16 good, EasyGL 5/5 good) — **no attribution cleanup is required in either**. MetaGL also
carries a `v0.2.0` tag; EasyGL has no tags.

Both `meta-gl` and `meta-gl-followup-audit` worktrees are **clean**. `easy-gl` is clean;
**`easy-glrvc` is not** — see §7.3.

### 7.2 The dependency chain, read from the build files

`cmake/BackendSelection.cmake` at `feature/gl` @ `f8efb9b4`:

- line 157 `if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easy-glrvc/CMakeLists.txt")` → hard
  `FATAL_ERROR` naming *"a separate git checkout (branch `rvc` of easy-gl)"*
- line 167 `add_subdirectory(../easy-glrvc easy-gl)`
- lines 134–136 mark this `GLB-7 TEMPORARY` and name **`GLB-38`** as the task that switches back to
  `../easy-gl`

So CNA `feature/gl` depends on **EasyGL `rvc`**, by path, today.

### 7.3 Where the MetaGL redirect actually lives — verified, not assumed

`easy-glrvc`'s **committed** `CMakeLists.txt` line 17 reads `add_subdirectory(../meta-gl meta-gl)` —
MetaGL **`develop`**, identical to what `easy-gl` `develop` uses. The redirect to the follow-up audit
branch exists **only as an uncommitted working-tree modification**:

```diff
-add_subdirectory(../meta-gl meta-gl)
+#add_subdirectory(../meta-gl meta-gl)
+add_subdirectory(../meta-gl-followup-audit meta-gl)
```

The committed `rvc` branch therefore already targets MetaGL `develop`; only the **working
configuration currently in use** builds against `meta-gl-followup-audit`. The MetaGL adaptation is
still required, because that working configuration is what `feature/gl` is actually built against
today.

### 7.4 Mandatory integration sequence

1. **Restore / remove the temporary `easy-glrvc` `CMakeLists.txt` redirect** so that worktree is
   clean. This is throwaway local build configuration, not work — see §7.5. Use `git restore` /
   `git checkout --`; **never `git stash`** (§`INTEGRATION_HISTORY_POLICY.md` §6).
2. Preserve the completed EasyGL `rvc` head with a signed archive tag. Blocked only by step 1.
3. Preserve the original MetaGL completed head — **already done**:
   `archive/preintegration/metagl-followup-audit-20260804` → `d5bc155f`, signed, verifies good.
4. Integrate the completed MetaGL `feature/followup-audit` (16 commits) into MetaGL `develop`.
5. Integrate the completed EasyGL `rvc` (5 commits) into EasyGL `develop`.
6. Validate **both** `develop` branches — MetaGL `develop` standalone, then EasyGL `develop` against
   the new MetaGL `develop`.
7. Update CNA `feature/gl` to those `develop` revisions — this is **`GLB-38`**: repoint
   `cmake/BackendSelection.cmake` from `../easy-glrvc` to `../easy-gl`.
8. Build and test CNA `feature/gl`.
9. Integrate CNA `feature/gl` into the integration branch.

**None of these merges may be performed autonomously.** `plans/plan_glbackends.md` records the same
constraint independently: *"`GLB-38` … Decided: leave to the project owner — do not attempt to
merge/push between repos autonomously."*

### 7.5 The one archive tag deliberately not created

**EasyGL `rvc` @ `b52f671379c0fe6d71d8c091ee4334c348beec8e` has no archive tag.** Phase 2's stated
precondition for the external repositories is *"only where the completed head is unambiguous **and
the worktree is clean**"*. The head is unambiguous; the `easy-glrvc` worktree is not clean (§7.3).

The precondition was respected rather than reinterpreted. **This is a deliberate omission, not an
oversight**, and it is the one open provenance gap left by this session.

**Correction to the earlier provenance wording (2026-08-04).** An earlier draft framed the
uncommitted `easy-glrvc` `CMakeLists.txt` redirect as an open *decision* — "discard or commit". That
was wrong, and it overstated what is blocked. Classified accurately:

- The redirect is **temporary local build configuration**, pointing the build at a sibling worktree
  while that worktree's branch was still being developed.
- It is **not feature work.** Nothing in it is an implementation change.
- It is **not provenance that must be preserved.** There is nothing here to archive, adapt, replay
  or record in a mapping table. It carries no authorship worth keeping and no technical content that
  would be lost.
- It therefore **does not need an owner decision.** Restoring the committed file is the correct and
  only action, and it is reversible.

What follows is that the missing archive tag is blocked on a **one-line cleanup**, not on a judgment
call: restore the committed `CMakeLists.txt` so the worktree is clean, then create the tag. Both are
step 1 and step 2 of §7.4.

The **owner-only** constraint remains exactly where `plans/plan_glbackends.md` puts it — on the
cross-repository *merges* (§7.4 steps 4–6) and on `GLB-38`. It was never about this file.

---

## 8. Magnum and Wicked Engine

> **Both are now INTEGRATED** — `wicked` 2026-08-05 (merge `683a00a5`,
> `integration/lanes/wicked.md`) and `magnum` 2026-08-06 (merge `e7d46c4c`,
> `integration/lanes/magnum.md`). The rows below are the pre-integration snapshot; the lane cards
> are authoritative. The "behind the checkpoint: 24" figure was a behind-count, not a work count,
> and had grown to 230/248 by the time each lane was adapted.

Both re-derived from the refreshed refs. **Both heads are unchanged** from the previous snapshot.

| Field | Magnum | Wicked Engine |
|---|---|---|
| Exact ref | `refs/remotes/origin/claude/cna-magnum-gr-backend-211xsx` | `refs/remotes/origin/claude/wicked-engine-cna-backend-5ffqzd` |
| Head | `9b903db8cf16988e3fbc955a429bab6c6a5b191e` | `91d8587e9a1a760c3275713f15f65bfafa387082` |
| Local ref | none — **remote-only** | none — **remote-only** |
| Real fork point | **`2338b44f`** on `feature/audit` (755 ahead of `develop`) | **the same `2338b44f`** |
| Own commits / files | **13 / 45** | **10 / 16** |
| **Behind the checkpoint** | **24 commits** | **24 commits** |
| Ahead of `develop` | 768 (755 of them the campaign's own) | 765 (same 755) |
| Contains `fc0dd2a2` | **yes** | **yes** |
| Shared interfaces | `GraphicsDevice.cpp`, `IGraphicsBackend.hpp` | **none of the three** |
| Tip | `docs(plan_magnum): record the verified baseline and the decisions behind it` | `feat(plans/plan_wicked.md WICKED-32/31/58/28): buffer hazards, upload stalls, multi-stream, mips` |
| Last commit (UTC) | 2026-08-04T09:40:54Z | 2026-08-04T09:18:36Z |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED (total)** — 13/13 Claude-authored, 13/13 unsigned | **AUTHOR/TRAILER CLEANUP REQUIRED (total)** — 10/10 Claude-authored, 10/10 unsigned |
| Plan file / namespace | `plans/plan_magnum.md` / `MAGNUM-*` | `plans/plan_wicked.md` / `WICKED-*` |
| External dependency | `cmake/ThirdPartyMagnum.cmake` (Magnum/Corrade) | `cmake/ThirdPartyWicked.cmake` + `cmake/patches/wicked-sdl3-platform.patch` |
| Archive tag | `archive/preintegration/magnum-20260804`, signed | `archive/preintegration/wicked-20260804`, signed |

**The previous snapshot's audit-stacked re-classification is re-verified and holds.** The correction
this inventory adds is that it was **never specific to these two** — nine further lanes have the same
shape (§3).

**Each needs a 24-commit rebase onto the checkpoint** before anything else; they were cut from
`feature/audit` one day before the `WEBGPU-115` and `GFX-215/216/217/218/219` work landed. (The
previous snapshot said 22; the checkpoint has since advanced by two commits.)

**What is still NOT established.** Neither branch was checked out, built, tested, merged, rebased or
cherry-picked by this session. **Development status is UNKNOWN for both.** A run of feature-shaped
commit subjects and a tip reading "record the verified baseline" is *development* evidence, not
*integration-readiness* evidence. Both are recorded **NEEDS VALIDATION**, and their low interface
footprint does not upgrade that.

---

## 9. Archive tags created by this session

All **22** are annotated, GPG-signed, verified good, and **local only — none pushed**.

Pattern: `archive/preintegration/<lane-slug>-<YYYYMMDD>`. No prior `archive/*` convention existed;
the repository's two pre-existing tags are `audit-2026-07-complete` (lightweight) and
`cna-post-audit-remediation-phase1` (annotated, signed).

| Tag | Target | Own |
|---|---|---|
| `archive/preintegration/skia-20260804` | `ca046f01` | 141 |
| `archive/preintegration/metal-20260804` | `48928d11` | 99 |
| `archive/preintegration/llgl-20260804` | `fa26e72d` | 68 |
| `archive/preintegration/diligent-20260804` | `1ab12b50` | 65 |
| `archive/preintegration/html-dom-20260804` | `8e4e4293` | 55 |
| `archive/preintegration/direct2d-20260804` | `9b17e783` | 48 |
| `archive/preintegration/opengl2-20260804` | `77d36d9e` | 40 |
| `archive/preintegration/sokol-20260804` | `261ea700` | 37 |
| `archive/preintegration/gdi-20260804` | `adc9cc2a` | 34 |
| `archive/preintegration/glide-20260804` | `2f9b47e1` | 32 |
| `archive/preintegration/opengl1-20260804` | `fc14f37b` | 31 |
| `archive/preintegration/opengl4-20260804` | `c49e0ba2` | 28 |
| `archive/preintegration/gl-20260804` | `f8efb9b4` | 28 |
| `archive/preintegration/dxold-20260804` | `36289bb2` | 28 |
| `archive/preintegration/opengles1-20260804` | `3d576da2` | 26 |
| `archive/preintegration/magnum-20260804` | `9b903db8` | 13 |
| `archive/preintegration/wicked-20260804` | `91d8587e` | 10 |
| `archive/preintegration/depthcrt-20260804` | `f4804469` | 6 |
| `archive/preintegration/stub-20260804` | `a35651e8` | 5 |
| `archive/preintegration/gltf-20260804` | `86ada7a7` | 1 |
| `archive/preintegration/ext-20260804` | `05ab5d3d` | 1 |
| `archive/preintegration/metagl-followup-audit-20260804` *(in `meta-gl`)* | `d5bc155f` | 16 |

**Not created:** EasyGL `rvc` — §7.5.

---

## 10. Integration base

| Field | Value |
|---|---|
| Integration branch | **`integration/post-audit-phase1`** |
| Created from | tag `cna-post-audit-remediation-phase1` |
| Head | **`990d6b8a`** — the `dxold` merge, which closes Batch 0. Below it: the two Batch 0 stabilization commits (`f742341b`, `e0332214`, integrating no lane) and the `ext`, `gltf`, `depthcrt` merges |
| Head on `origin` | **`61bd1a1b`** — the `depthcrt` merge. Everything above it is **local and unpushed** |
| Checkpoint tag is an ancestor | yes — together with `e0332214` and all four lane merges |
| Batch 0 checkpoint tags | **two, both signed, both local**: intermediate `integration/checkpoint-batch0-20260804` → `e0332214` (3 lanes) and final `integration/checkpoint-batch0-complete-20260804` → `990d6b8a` (4 lanes). Neither may be moved. See `integration/BATCH_0_COMPLETE.md` §1 |
| Worktree | **`/rv/data/development/github.com/openeggbert/cnaintegration`** — sibling development path, not `/tmp` |
| Worktree status | clean |
| Feature lanes integrated | **4 of 21 — `depthcrt`** (adapted, merge `61bd1a1b`), **`gltf`** (direct merge `722a2f5a`), **`ext`** (adapted, merge `8a374b9f`) and **`dxold`** (adapted, merge `990d6b8a`), all 2026-08-04 |
| Feature lanes pending | **17** — re-derived 2026-08-04 after fetch, not carried forward |
| Writing agents | one at a time; no other agent holds it |

### 10.1 Lane completion cannot be inferred from ancestry

The campaign now uses **both** integration paths, and they leave different traces:

| Path | Lane | Original head an ancestor of the integration branch? |
|---|---|---|
| **Adaptation** — commits replayed as new objects | `depthcrt` (`f4804469` → `88244b3a`…`3cca0b19`) | **No**, and correctly so |
| **Direct merge** — original object preserved | `gltf` (`86ada7a7`) | **Yes** |

A sweep of `git merge-base --is-ancestor <lane-head> integration/post-audit-phase1` over all 21 lanes
therefore returns **1**, not 2. That is a property of the measurement, not of the campaign. Read lane
completion from the lane cards and `INTEGRATION_ORDER.md` §3; use ancestry only to confirm a
*direct-merged* lane.

### 10.1 Integrated lanes

| Lane | Adapted commits | Merge commit | Original head | Archive tag | Record |
|---|---|---|---|---|---|
| `depthcrt` | 5 (`88244b3a` … `3cca0b19`), all signed, all Robert-authored | `61bd1a1b`, signed, `--no-ff` | `f4804469` | `archive/preintegration/depthcrt-20260804` — **unchanged** | `integration/lanes/depthcrt.md` |
| `gltf` | **none — direct merge**, `86ada7a7` preserved as the same object | `722a2f5a`, signed, `--no-ff` | `86ada7a7` | `archive/preintegration/gltf-20260804` — **unchanged** | `integration/lanes/gltf.md` |
| `ext` | 1 (`c6a28036`), signed, Robert-authored, trailers stripped | `8a374b9f`, signed, `--no-ff` | `05ab5d3d` | `archive/preintegration/ext-20260804` — **unchanged** | `integration/lanes/ext.md` |
| `dxold` | 35 (`bc2e0de3` … `9256e606`), all signed, all Robert-authored; incl. the owner-ordered FREEDIRECT/DX3 naming transition | `990d6b8a`, signed, `--no-ff` | `36289bb2` | `archive/preintegration/dxold-20260804` — **unchanged** | `integration/lanes/dxold.md` |
| `stub` | 7 (`383931ce` … `c29ef117`), all signed, all Robert-authored | `99ae7d11`, signed, `--no-ff` | `a35651e8` | `archive/preintegration/stub-20260804` — **unchanged** | `integration/lanes/stub.md` |
| `opengles1` | 31 (`ec51bf6b` … `b811d76d`), all signed, all Robert-authored | `df6b7cc6`, signed, `--no-ff` | `3d576da2` | `archive/preintegration/opengles1-20260804` — **unchanged** | `integration/lanes/opengles1.md` |
| `opengl4` | 28 (… `3f1035de`), all signed, all Robert-authored; 4 NEXT.md session-status commits OMITTED with justification | `bc29a976`, signed, `--no-ff` | `c49e0ba2` | `archive/preintegration/opengl4-20260804` — **unchanged** | `integration/lanes/opengl4.md` |
| `opengl1` | 37 (`8a560460` … `91344935`), all signed, all Robert-authored; all 31 originals TRANSFERRED | `c0876fca`, signed, `--no-ff` | `fc14f37b` | `archive/preintegration/opengl1-20260804` — **unchanged** | `integration/lanes/opengl1.md` |

**Pending lanes: 17** after `depthcrt`, `gltf`, `ext` and `dxold`. The count is a snapshot of the
same 21-lane derivation (re-derived post-merge: 21 logical lanes, 4 integrated); it is not an
invariant and must be re-derived after a fetch (§2) rather than quoted forward.

> **`dxold`'s 3 `Claude`-authored commits were SSH-signed, not unsigned** — the same `%G?` blind
> spot `ext` documented, now confirmed on a second lane. `opengl4` (28), `magnum` (13) and
> `wicked` (10) still need object-level re-derivation before their rows are quoted.
>
> **Public backend delta of `dxold`: +8** (DX1, DX2, DX3-real, DX5, DX6, DX7, DX8, D3D10).
> **FREEDIRECT is the renamed free-direct backend** (formerly `DX3`) — a rename, not an addition;
> the DirectX 3 generation is now covered by two distinct public implementations.

> **Re-derived 2026-08-04 by the Batch 0 stabilization checkpoint**, independently rather than
> carried forward: after `git fetch --all --prune --tags`, enumerating every branch across local and
> remote refs excluding `develop`, `master`, `feature/audit`, `integration/*` and `adapt/*` yields
> **21 logical lanes — 3 integrated, 18 pending**. The fetch moved nothing. See
> `integration/BATCH_0_STABILIZATION.md` §1.

**Batch 0 has now covered all three principal history classes** — a reconstructed multi-commit lane
(`depthcrt`), a clean direct-merge lane (`gltf`), and a single-commit metadata-cleanup lane (`ext`).
The one remaining Batch-0 lane is `dxold`.

**Push state — corrected 2026-08-04, measured with `git ls-remote`.** The earlier blanket claim
"nothing has been pushed" is **no longer true** and is superseded:

| Ref | On `origin`? |
|---|---|
| `refs/heads/feature/audit` | **pushed** @ `047c254a` |
| `refs/heads/integration/post-audit-phase1` | **pushed** @ `61bd1a1b` (the `depthcrt` merge) |
| `refs/heads/adapt/depthcrt` | **pushed** @ `3cca0b19` |
| **All 21 CNA archive tags** | **not pushed** — still local only, as claimed |
| The `gltf` merge `722a2f5a` and the documentation commit that records it | **not pushed** |

All three branch pushes carry the reflog stamp `update by push` at **2026-08-04 16:54:40 +0200** —
after the `depthcrt` lane card was written, which is why that card's "Nothing was pushed" was
accurate when written and is retained as the historical record it is. The push was not performed by
the `gltf` integration session, which pushed nothing.

**No original branch history was rewritten. `refs/heads/feature/depthcrt` is byte-for-byte where the
bootstrap left it.**
