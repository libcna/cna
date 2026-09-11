# Lane card — `skia` (Skia CPU-raster 2D) · ✅ **INTEGRATED 2026-08-07** · merge `1381ff93` — the fourteenth lane

> **Outcome.** Merged, but only after the block that stopped the first attempt was resolved — and
> after a **second** blocking defect that the first attempt never reached. The lane adapts cleanly:
> 141 commits replayed with **zero interface drift** and its own suite at **172/172**. What it could
> not do is prove itself, because its validation has only ever been the Skia suite. Running the
> **EasyGL** principal control found `REMED-GFX-223`; building **Sokol** from these same sources
> found `REMED-GFX-225`, which meant `CNA_GRAPHICS_BACKEND=SOKOL` **did not compile at all**. Both
> are in shared code, both are fixed here, and **neither is reachable from any Skia test.**
>
> **`REMED-GFX-224`** is left open and is not a blocker: an EasyGL render target silently discards
> `SetData`, pre-existing and previously masked. It is a separate finding, deliberately not folded
> into `REMED-GFX-223`.
>
> **SKIA = a CPU-raster 2D identity, the 32nd, aliasing nothing.** Raster `SkSurface` presented
> through an SDL streaming texture; `ctest -N -L Accelerated` reports **0**. Dependency Skia
> `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b` (BSD-3-Clause), GN-built outside the tree, six static
> archives, **nothing vendored and no carried patch**. The Ganesh artifact is carried but has no
> `IGraphicsBackend` and is unreachable from backend selection — **not** a second identity. All
> three §1.1 obligations paid: an exhaustive **eleven-member** capability switch with no `default`
> arm, truthful `WireFrame=false`, and a declaration guard decided not-applicable on `stub`'s
> precedent.
>
> **Nothing was pushed. `audit/` untouched. The four user stashes are untouched. No Batch 4
> checkpoint was created; no Batch 6 checkpoint is due** — Batch 6 / Group G has four members and
> three remain.


| Field | Value |
|---|---|
| Logical lane | `skia` |
| Refs | local **and** remote `feature/skia` — both unchanged |
| Original head | `ca046f013bfd9797aab0292194e547d1caa4fef8` — unchanged locally and on `origin` |
| Archive tag | `archive/preintegration/skia-20260804` → `ca046f01` · verifies **good** · unchanged |
| Real fork point | **`a7a49e3d`** on `feature/audit` — audit-stacked; the point it shares with `direct2d`, `gdi` and `glide` |
| Own commits / files | **141 / 254** |
| Adaptation branch / head | `adapt/skia` → `a071e1e2` (worktree `cnaintegration-skia`, retained) |
| Adapted commits | **151** — 141 replayed + 7 added by the adaptation + 3 added by the stabilization |
| History class | **HISTORY CLEAN** — re-verified at the object level |
| Conflict class | **HIGH** (predicted) — **confirmed**, but smaller than the label suggests: 10 conflicted files, 6 resolutions |
| Path taken | **ADAPTATION** |
| Result | ✅ **INTEGRATED** — merge `1381ff93`, signed, `--no-ff`, merged tree byte-identical to `adapt/skia` |

---

## 1. Skia identity — what this backend actually is

`CNA_GRAPHICS_BACKEND=SKIA` is a **CPU-raster 2D** backend. It owns a raster `SkSurface`, draws
through `SkCanvas`, reads RGBA8 back, and presents through an SDL streaming texture. It is a
distinct public identity — the **32nd** — and aliases nothing.

| Question | Answer, measured |
|---|---|
| Which Skia path | **CPU raster.** `args.gn`: `skia_use_gl=false skia_enable_ganesh=false skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false` |
| Proven how | `ctest -N -L Accelerated` reports **0** tests in this configuration, and the startup line reports `surface=raster; samples=0; anisotropic filtering=unsupported` |
| Dependency revision | **`ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`** — `git rev-parse HEAD` of `~/deps/skia` matches `docs/skia-backend.md`'s pin exactly. Milestone 153, `canvaskit/0.41.0-1538-gebf50520d7` |
| Acquisition | GN, built outside the tree; CMake never downloads. `-DCNA_SKIA_ROOT` + `-DCNA_SKIA_BUILD_DIR` are required or configure fails |
| Artifact | 6 static archives in a `LINK_GROUP:RESCAN` (`libskia.a`, `libskcms.a`, `liballocator_{base,core,shim}.a`, `libraw_ptr.a`), 25 MB, prebuilt at `~/deps/skia-out/raster` |
| License | **BSD-3-Clause**, Google Inc. Recorded in `THIRD_PARTY_NOTICES.md` by the lane's own SKIA-159 commit |
| Vendored / patched | **Neither.** Nothing copied into the tree, no carried patch |
| Does SDL's accelerated presenter make this a GPU mode | **No.** SDL may pick an accelerated renderer to upload the finished CPU image; `docs/skia-verification-boundary.md` states the distinction and `Skia_RasterMode_Coherence` asserts the mode cannot silently change |

**The Ganesh half is carried but is not a backend.** `SkiaGaneshContext`/`SkiaGaneshSurface` and
`cmake/ThirdPartySkiaGanesh.cmake` exist, and a second GN artifact is present at
`~/deps/skia-out/ganesh` (`skia_use_gl=true skia_enable_ganesh=true`). It has **no
`IGraphicsBackend`**, is not reachable from `CNA_GRAPHICS_BACKEND=SKIA`, and requires an explicit
`CNA_SKIA_GANESH_BUILD_DIR` even to compile its probe. `Skia_Ganesh_ModeRefusal_Raster` asserts that
requesting it in a raster build refuses. **It creates no second CNA identity**, and the owner paused
that arc at SKIA-163.

---

## 2. History — re-verified at the object level

The inventory's `HISTORY CLEAN` classification survived exactly, on every axis:

| Check | Result |
|---|---|
| Own commits | **141** |
| Author **and** committer | **141/141** `Robert Vokac <robertvokac@robertvokac.com>` |
| Signature | **141/141** `%G? = U` — good PGP, uncertified key, this project's normal state |
| Prohibited trailers | **0** |
| Attribution tokens | **0** |
| Merges / WIP / fixup | **0 / 0** |
| Session narrative (multiline-aware sweep) | **1** — reworded, see below |

**One body carried a quoted section title.** `f8baa00f` cited *`NEXT_skia.md`'s "Attempted this
session" note*. It is a pointer to a real heading in a real tracked file, not prose about how the
work was produced — but the phrase is exactly what the campaign strips, so the citation was
minimally reworded to name the section by its task ID (`NEXT_skia.md`'s SKIA-163 note), which is
also more durable. The technical body is untouched.

`NEXT_skia.md`'s own internal headings were **deliberately left alone**. That file is the lane's
continuity document and its "Completed in this session: SKIA-NN" structure is its own convention
across all 141 commits; the attribution policy is scoped to commit metadata, and rewriting a
carried document's forty headings would be a large unrelated edit to the lane's provenance.

### 2.1 `range-diff` — 130 of 141 byte-identical, 0 dropped

`git range-diff archive/preintegration/skia-20260804...adapt/skia`, 853 lines, on the lane card's
own evidence path (`build-probe/skia-rangediff.txt`).

| Class | Count |
|---|---|
| `=` byte-identical | **130** |
| `!` changed | **11** |
| `<` dropped | **0** |

Of the 11: **6** are the conflict resolutions below, **1** is the reworded citation, and **4** are
context-only — the added and removed lines are identical and only the surrounding lines the head
has since changed differ. One of those four is worth naming: `1b6396c0`'s `GraphicsCapability.hpp`
edit ends the enum at `Texture3D`, and at the head two members follow it, so the three-way merge
correctly kept the trailing comma.

---

## 3. Content — the compile probe found **zero** drift

`cmake --build … --target CNA cna_backend_graphics_skia` against the current shared interfaces:
**exit 0, zero errors**, first time.

This is the **third** lane after `wicked` and `magnum` to need no interface adaptation at all, and
for the same structural reason: it forked at `a7a49e3d`, **707 commits ahead of `develop`**, which
is past the entire stale-fork set. Measured, not assumed:

| Post-fork change | Cost to this lane |
|---|---|
| `fc0dd2a2` `GpuDrawParams` unification (4 removed fields) | **zero** — no hunk of the lane's diff touches any reference |
| `SetVertexDeclaration` / `SetRenderTargets` pure virtual | zero — this backend has no vertex pipeline; both predate the fork |
| `GetData`/`SetData` `void → bool` on four interfaces | zero — already present at the fork |
| `CreateRenderTargetCube`'s `preserveContents` | zero — already present |
| `ApplyBlendState`'s `BlendWriteState` | zero — already present |
| FNA fog vector | zero — no fog path |

**The drift surface was 26 files; the conflict surface was 10.** Six resolutions across five stops
in a 141-commit `cherry-pick`, every one a union:

| # | File | Resolution |
|---|---|---|
| 1 | `cmake/BackendSelection.cmake` | Registration union **#11**. All 31 existing identities kept token-exact, `SKIA` added as the **32nd** |
| 2 | `tests/CNA/GraphicsBackendTypeTests.cpp` | The head refactored this into `ExpectedNameFor()`; the lane's inline switch is superseded. Took HEAD, added the `Skia` arm — now 32 arms |
| 3 | `examples/texture2d_getdata_transfer_range_test.cpp` | The lane's `MipUpload → MipPolicy` three-value refactor kept; the head's `DX3 → FREEDIRECT` rename kept. 15 identities |
| 4 | `tests/…/GraphicsBackendCompileDefinitionTests.cpp` | Kept the head's `SOKOL` identity test, appended the lane's `SKIA` one |
| 5 | `CLAUDE.md`, `README.md`, `docs/README.md`, `docs/graphics-backend-feature-matrix.md` | Documentation unions. The lane's one non-Skia edit — dropping the now-stale "fifth backend" from the `WEBGPU` bullet, wrong once 26 more landed — was kept |
| 6 | `IGraphicsBackend.hpp`, `TextureCube.hpp` | Kept the head's post-fork `ShareCpuPixels` hook **and** per-face `cpuPixels_` array; kept the lane's `GetSizeEXT` and `shared_ptr` backend ownership |

---

## 4. §1.1 obligations — all three answered explicitly

| Obligation | Decision |
|---|---|
| **`REMED-GFX-201`/`-202`** exhaustive capability reporting | **Paid.** `SupportsCapability` was `return capability == Texture3D` — truthful for the nine members that existed when it was written, accidental for the two added since. Now an exhaustive **eleven-member** `switch` with **no `default` arm**, so a twelfth member is a `-Wswitch` diagnostic rather than an unchosen answer. `Skia_GraphicsCapability` asserts all eleven |
| **`REMED-GFX-209`** truthful `WireFrame` | **`false`**, and the refusal half is satisfied *more strongly* than the rule requires: `Ensure3DSupported()` rejects every 3D draw before any vertex input is inspected, so no polygon topology can reach a raster queue to be filled solid |
| **`REMED-GFX-DECL-GUARD`** draw-time declaration fidelity | **Not applicable — decided, not waived.** The guard exists for a backend that infers native input elements from a byte stride. This one has no native vertex layout and no draw route that consumes one; `docs/stub-backend.md`'s precedent for a backend with no vertex pipeline applies unchanged |

The release-gate validator was **strengthened**, not relaxed, to keep enforcing this: it now
brace-matches the real function body, accepts either the equality or the switch shape, and
additionally rejects a `default:` arm or a missing case.

**`CustomEffects` is a deliberate `false`.** The narrow opt-in `CNA_SKIA_SKSL_V1` /
`CNA_SKIA_SKSL_MESH_V1` ABI genuinely works, but a `true` would promise the arbitrary-`Effect`
support this backend rejects for ordinary GLSL. Under-reporting a bounded extension is the honest
direction, and `Skia_Effect_Boundary` pins it to observed behaviour rather than leaving it a claim.

---

## 5. Capability table — every answer measured

| Capability | Skia | Basis |
|---|---|---|
| `ThreeD` | **false** | Every 3D route refuses through `Ensure3DSupported()`; `Skia_3D_Refusal` |
| `DepthStencilBuffer` | **false** | No attachment. `DepthStencilState::None` accepted only as the *absence* of one |
| `MultiSampleAntiAliasing` | **false** | Raster `SkSurface` owns zero samples; requests 0/1/2/4/oversized all apply and report 0 |
| `MultipleRenderTargets` | **false** | `SkCanvas` has one colour result; replay emulation evaluated and rejected. `Skia_MRT_Rejection` |
| `AnisotropicFiltering` | **false** | Exact complete-Linear fallback; no native feature claimed |
| `WireFrame` | **false** | No polygon fill mode and no draw route for one |
| `OcclusionQuery` | **false** | Raster final pixels cannot distinguish positive from zero coverage |
| `CustomEffects` | **false** | Deliberate under-report of the bounded SkSL ABI — see §4 |
| `Texture3D` | **true** | The **only** true. Bounded CPU transfer/readback storage only; never shader sampling |
| `MultiStreamVertexInput` | **false** | New since the fork. No vertex-stream pipeline to split |
| `Instancing` | **false** | New since the fork. The instanced route refuses rather than drawing one instance |

**2D/3D contract: 2D-only, and that boundary is preserved.** Nothing was emulated with unrelated
CPU drawing to make a 3D test pass.

---

## 6. Texture / row-stride audit — clean, and independently derived

The units were named rather than assumed: **texels**, **bytes/texel**, **row bytes**
(`width × bytes/texel`, CNA's own packed pitch), **stride** (the source or destination buffer's row
pitch), **slices**, **mip levels**.

New `Skia_Texture_RowStride` (`examples/skia_texture_row_stride_test.cpp`), **8/8**:

- 13×7 gives level-0 rows of **13, 26 and 52 bytes** for `Alpha8`, `Bgr565` and `Color` — none 4-,
  8- or 16-aligned for the narrow formats — and level 1 is 6×3, whose **6/12/24-byte** rows are a
  *different* misalignment, making the two levels an independent pair rather than one check twice;
- the pattern varies with both axes, so a one-texel row shear cannot alias into a whole-image offset;
- a sub-rectangle starting mid-row proves the transfer keeps **its own** pitch, not the level's;
- a fourth leg draws 13-wide 1:1 and compares **all 91 backbuffer texels**, because an exact
  `GetData` can pass from the CPU shadow while the sampling image Skia builds over that same storage
  carries a wrong `rowBytes` — a defect that renders wrong and reads back right.

**Written from Skia's own storage model, deliberately not adapted from Wicked's staging logic.**
This storage is CNA-owned and tightly packed; a mapped GPU allocation has a driver-chosen pitch. The
two cannot share a correctness argument, so `WICKED-79`'s reasoning was not reused — and the code
confirms it: `UpdatePixels` validates `stride >= width * bytesPerTexel` and copies row-by-row into a
packed chain, honouring source padding without ever adopting it.

---

## 7. Validation

| Instrument | Pre-adaptation (fork point) | Adapted | Reading |
|---|---|---|---|
| Skia suite | **171 / 171**, 0 failed, 73.4 s | **172 / 172**, 0 failed, 67.8 s | Reproduced its own recorded count exactly, then kept it with one added test |
| `ctest -N -L Accelerated` | **0** | **0** | Raster-only claim holds |
| Six source audits | 6/6 | 6/6 | Ledger 258 entries, matrix 351, release gate 11/11 capabilities |
| `CnaTests` under `SKIA` | 5699 · 5564 · **125** failed · 21 skipped | **5746 · 5611 · 124 failed · 23 skipped** | See §7.1 — **better than the baseline**, and set-differenced rather than compared as a total |
| `CnaTests` under `EASYGL` (principal control) | — | **5912 registered · 5911 passed · 1 failed · 6 skipped** | The one failure is `easy-gl-resource-smoke-tests`, a sibling-repo binary with **zero** CNA symbols, linked before the fix was first built. External |
| `SOKOL` dedicated suite | — | **37/37**, 19.2 s | Matches its own recorded figure. Only reachable after `REMED-GFX-225` |
| `SOKOL` shared `Texture2D`/cache control | — | **34/34** | |
| `DILIGENT` focused `Texture2D`/cache/cube/render-target control | — | **169/169** | |
| ASan / UBSan (`address,undefined`, `SKIA`) | — | **0 ASan errors · 0 UBSan runtime errors** | 233 tests. All 15 382 736 leaked bytes are three `libGLX_mesa` blocks with no CNA/Skia/SDL frame |

**The lane started from a proven baseline.** Like `sokol` and `diligent`, and unlike `wicked` and
`magnum`, a build at its own fork point reproduced its recorded results exactly *before* any
adaptation, so everything measured afterwards is attributable.

### 7.1 The corpus delta, classified

The previous session recorded `131` and wrote that the honest expected figure was `125` **and
unmeasured**, because the capability fix landed after that run. It is now measured: **124**, one
*better* than the fork point. Compared as a set, not as a total — **8 fixed, 7 new**:

**Fixed (8).** Five `GraphicsDeviceCapabilityTest` rows repaired by giving the shared capability
contract its 2D-only arm — these asserted answers only a 3D-capable backend can give, and this one
truthfully gives the other. Plus **three content-path tests `REMED-GFX-223` repaired**:
`CnjCacheIsolationTest.SidecarLoadedFirstDoesNotCorruptLaterNativeLoad`,
`CnjEffectTest.LoadsRealCnjFixture` and `CnjStockEffectTest.CustomGlslEffectStillWorks`. All three
were failing **at the fork point too** — the defect was latent in `feature/skia` from the start and
had never been attributed, because a `CnaTests`-under-`SKIA` failure was assumed to be the 2D-only
class.

**New (7).** Exactly the `2 + 5` in `OrdinaryDrawMultiStreamTest` / `InstancedDrawMultiStreamTest` —
`REMED-GFX-201`/`-202` shared tests that construct a live `VertexBuffer`, which this backend refuses
by design. **Not fixed, and recorded as the pre-existing 2D-only class:** those files have no arm
for any 2D-only identity, and `SdlGraphicsBackend`/`CanvasGraphicsBackend` both `return false` for
every capability, so `SDL_RENDERER`, `CANVAS`, `ASCII` and `FREEDIRECT` fail them identically.
Verified by reading their overrides. The refusal itself is already pinned by `Skia_3D_Refusal`.

`125 − 8 + 7 = 124`. **Zero new supported-path failures.**

> **Run `CnaTests` from the worktree root.** `MediaLibraryTestFixture` redirects `MediaLibraryPaths`
> at the *relative* paths `tests/assets/media/{music,pictures}`. From a build directory those
> resolve to nothing, the library comes up empty, and `ObjectGraphIsInternallyConsistent` segfaults
> at test 4952. `ctest` sets the directory correctly; a bare `./CnaTests` does not. Confirmed not a
> defect by reverting the fix, by excluding the new tests and by running the EasyGL binary — all
> three crash identically, and the suite is **84/84** from the right directory.

---

## 8. The two blocking defects, and how they were resolved

The EasyGL control is the campaign's principal instrument, and it had **never been run against this
lane** — its own validation has always been the Skia suite. Running it, and then building a *third*
backend from these sources, found **two defects in shared code, neither reachable from any Skia
test**. Both are fixed here; both are recorded in `plans/plan_postaudit.md` §20 and §22.

### Fixed and verified during adaptation

`IsColorTransferFormatEXT` narrowed the `Color*` `SetData`/`GetData` overloads to
`SurfaceFormat::Color` alone on every non-Skia backend, withdrawing the working `ColorSrgbEXT` route
`MouseCursor::FromTexture2D` uses. It now preserves the pre-existing any-4-byte-format rule off
Skia, and `MouseCursorTest` is 14/14 again. Separately, `bb8e6430`: the three-way merge had cleanly,
silently reverted `REMED-GFX-222`'s own fixture update, because the lane rewrote the same region — a
conflict resolved correctly by content and wrong by meaning, exposed only by running the suite.

### `REMED-GFX-223` — a cache hit claimed to be a render target · **RESOLVED** `9dbdd4cf`

`Texture2D::gpuOnlyContent_` carries two different claims:

| | Claim | Consequences |
|---|---|---|
| **B** (weak) | an absent CPU shadow is normal here — fall back to the backend rather than throwing | `GetData` reads the backend **only when there is no shadow** |
| **A** (strong) | the live backend is the sole authority; the shadow is never trusted | `GetData` prefers the backend always; `SetData` drops the shadow and updates the existing backend **in place** |

At the head the flag only ever meant **B**: both of its readers (`Texture2D.cpp:434` and `:509` at
`aa9f3fb5`) sit *inside* an `if (!cpuPixels_ || cpuPixels_->empty())` branch.
`Texture2D::ReconstructFromCache` borrows the protected constructor `RenderTarget2D` owns, and so
inherits `gpuOnlyContent_ = true` for an ordinary content texture whose very next statement installs
a CPU shadow. It only ever needed **B**, so the mislabel was inert.

This lane promoted the flag to **A**, correctly, for real render targets — at which point every one
of A's consequences became a false statement about a cache hit.

**First incorrect state transition:** `gpuOnlyContent_ = true` at `Texture2D.cpp:356`, executed on
behalf of `ReconstructFromCache` (`:2703`), producing `gpuOnlyContent_ == true && cpuPixels_ != nullptr`.

**The previous session's diagnosis was right but incomplete.** It identified the mislabel and the
`GetData` reordering. It did not identify that `SetData(const Color*, int)`'s new in-place branch
writes through a backend that `ContentManager`'s weak texture cache **shares** with every other
wrapper reconstructed from the same entry — the CNB-33 aliasing `CnjCacheIsolationTests` exists to
pin, whose own header comment predicted it verbatim ("if a future change to `SetData` ever starts
mutating in place instead of reassigning, these tests will catch it"). It also keeps the cache
entry's `weak_ptr` alive, turning what the head resolves as a *miss* into a *hit on mutated pixels*.
**Clearing the flag alone makes both tests green and leaves that aliasing live.** The repair is
therefore two-sided:

1. `ReconstructFromCache` clears `gpuOnlyContent_`.
2. the in-place backend update in `SetData(const Color*, int)` is gated on `gpuOnlyContent_` — the
   branch exists solely so a render target does not have its backend swapped for an ordinary texture
   backend, leaving `RenderTarget2D`'s cached `IRenderTargetBackend` view dangling.

31 insertions / 17 deletions across two files. Relative to the head, ordinary textures are
behaviourally identical, render targets keep this lane's improvement, and a cache hit whose shadow
has legitimately expired now raises the ordinary-`Texture2D` refusal — the contract
`ContextRecoveryTest` already documents for every other plain texture.

Traced on both trees with `CNA_TEXTURE_TRANSFER_TRACE=1`, same test, same display, same fixture:

| Tree | `GetData` provenance | Result |
|---|---|---|
| head `aa9f3fb5` (EASYGL) | all `source=cpuPixels_` | passes |
| this lane, pre-fix (EASYGL) | 1 failed `source=backend` readback in test 1, 2 in test 2 | throws |
| this lane, post-fix (EASYGL) | 3 × `source=cpuPixels_`, **zero** backend readbacks | passes |

**Cardinality:** the fix *removes* GPU readback attempts on the cache-hit path and adds none. It
adds no draw, frame, synchronization, wait or cache entry. Ordinary `SetData` returns to the head's
one-texture-per-full-upload allocation; extending the in-place optimization to ordinary textures is
deliberately **not** done, because making it safe needs the weak cache invalidated or made
copy-on-write — a design change, not a bug fix.

### `REMED-GFX-225` — this lane's new `GetSizeEXT` virtual broke four backends · **RESOLVED** `8bd8bc09`

`SKIA-149` added `[[nodiscard]] virtual int GetSizeEXT() const noexcept` to `ITextureCubeBackend`.
The head's interface had **no** `GetSizeEXT` at all — but four concrete cube backends already carried
a same-named non-virtual accessor **without** `noexcept`: `SokolTextureCubeBackend`,
`D3D11TextureCubeBackend`, `D3D12TextureCubeBackend`, `D3D9TextureCubeBackend`. All four derive from
the interface, so the new virtual silently turned each into an override with a **looser exception
specification** — a hard compile error.

**`CNA_GRAPHICS_BACKEND=SOKOL` did not build at all**, failing at 47 % in
`cna_backend_graphics_sokol`. No Skia test and no EasyGL control can reach it: EasyGL's cube backend
has no `GetSizeEXT`, so the collision does not exist there. It took building a **third** backend from
these sources — the cross-backend control whose precedent `integration/lanes/diligent.md` set.

Fixed with `noexcept override` on all four. `D3D11`/`D3D12`/`D3D9` are corrected **by inspection and
not compiled**; none builds on this Linux host.

**Sweep for the same class.** The lane adds six virtuals to the shared interface and gives three
interfaces an `enable_shared_from_this` base. Every backend header was grepped for members named
after each of the six: **only `GetSizeEXT` collides**. No backend already used
`enable_shared_from_this`, and no class inherits two of the three interfaces, so no ambiguous base.
No existing virtual signature was removed or changed, and **none of the six new virtuals is pure**,
so no backend is forced to implement anything. Three of ~32 backends were actually compiled
(`SKIA`, `EASYGL`, `SOKOL`) plus `DILIGENT`; the rest rest on that static analysis, which is stated
rather than implied.

---

## 9. New findings

| ID | Severity | Status |
|---|---|---|
| **`REMED-GFX-223`** | HIGH | ✅ **RESOLVED** — `9dbdd4cf`, 13 regression tests |
| **`REMED-GFX-224`** | MEDIUM | ⬜ **OPEN — not a blocker.** An EasyGL render target silently discards `SetData`, because `ITextureBackend::UpdatePixels` is a defaulted no-op that `EasyGLRenderTargetBackend` never overrides. Pre-existing; masked at the head by `SetData` replacing a render target's backend and leaving a shadow `GetData` read first. No test, example or documented contract depends on the round trip, and the partial-rectangle overload always went to the same no-op |
| **`REMED-GFX-225`** | HIGH | ✅ **RESOLVED** — `8bd8bc09` |

The lane's own pre-existing open set is unchanged and is **not** a blocker: `SKIA-163` partial and
`SKIA-164`–`SKIA-170` open, all confined to the unadvertised Ganesh/MSAA/anisotropy/MRT successor
scope behind `docs/skia-release-gate.md`. The advertised SKIA-1–114 baseline is closed and signed.

`REMED-CONTENT-007`/`-008` re-checked for this lane per `INTEGRATION_ORDER.md` §6: its one
*production* content-reader change (`Texture2DContentTypeReader.cpp`) contains **zero** `ReadString`,
`fs::`, path or resolution tokens — it adds mip-count and compressed-byte-count validation only.
**Not a blocker**, confirmed by re-reading the diff rather than by citing the earlier inspection.

---

## 10. Commits

**151 on `adapt/skia`, 141 replayed + 7 added by the adaptation + 3 added by this stabilization.**
Every one reports *Good signature* from `255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`; `%G?` is `U`
only because that key carries no local owner-trust assignment — the integration branch's own
accepted commits report identically. **Zero attribution, zero trailers, zero merges.**
`git diff --check` clean over the whole range and in the worktree.

| Added commit | Subject |
|---|---|
| `37e8fc0a` | `feat(skia): report every GraphicsCapability member exhaustively` |
| `f1dbb737` | `test(skia): classify the API and EasyGL registrations added since the fork` |
| `bb8e6430` | `fix(skia): restore REMED-GFX-222's accepted null vertex-buffer bindings` |
| `5ec67f24` | `test(skia): add a bounded row-stride oracle at deliberately unaligned widths` |
| `59be655f` | `test(skia): give the shared capability contract its 2D-only arm` |
| `df6c8e27` | `docs(skia): name the operand after the 3D refusal prefix` |
| `75b1b903` | `fix(REMED-GFX-222 sibling): stop narrowing the Color* overloads off Skia` |
| `9dbdd4cf` | `fix(REMED-GFX-223): confine gpuOnlyContent_ to real render targets` |
| `8bd8bc09` | `fix(REMED-GFX-225): conform the four cube backends to the new GetSizeEXT virtual` |
| `a071e1e2` | `docs(skia): record REMED-GFX-223's resolution and the two findings it uncovered` |

`bb8e6430` is worth keeping in mind for future lanes: the three-way merge resolved
`easygl_device_validation_test.cpp` cleanly **by content** and silently reverted `REMED-GFX-222`'s
own fixture update, because the lane had rewritten the same region for its Skia arm. Only running
the suite exposed it. A conflict can be resolved correctly line-by-line and still be wrong by
meaning.

---

## 11. Merge

| Field | Value |
|---|---|
| Merge commit | **`1381ff93`** — signed, `--no-ff`, parents `aa9f3fb5` + `a071e1e2` |
| Merged tree | **byte-identical to `adapt/skia`** |
| Conflicts | none — `adapt/skia` already contained the integration head |
| Integration HEAD | `aa9f3fb5` → **`1381ff93`** |
| Lane count | 13 integrated / 8 pending → **14 integrated / 7 pending** |
| `feature/skia` | `ca046f01` — **unchanged** |
| `archive/preintegration/skia-20260804` | → `ca046f01` — **unchanged**, verifies good |
| Pushed | **no** |
| Checkpoint | **none.** Batch 6 / Group G has **four** members — `direct2d`, `llgl`, `metal`, `skia` — and three remain. No Batch 4 checkpoint either: Batch 4 is `feature/gl` alone and has not been started |

**What this lane cost, and what it bought.** It is the first lane to be blocked, recorded as
blocked, and then merged — the BLOCKED event is preserved above, not rewritten. Its real lesson is
the one the first attempt half-learned and this one finished: *a backend-local `#ifdef` is not the
only way a lane reaches other backends, and a lane's own suite cannot find what it cannot reach.*
`REMED-GFX-223` needed the **EasyGL** control to surface; `REMED-GFX-225` needed a **Sokol build**,
and no amount of test-running would have found it, because the failure was a compile error in a
backend nobody had compiled. Run the principal control before believing a lane — and build a third
backend before believing a shared-interface change.

**Retained:** worktree `cnaintegration-skia`, build trees `cmake-build-skia`, `-easygl`, `-sokol`,
`-diligent` and `-asan` (objects reclaimed, binary kept), and the pinned `~/deps/skia` +
`~/deps/skia-out/raster` artifacts. `cmake-build-skia-pre` was deleted with the owner's approval
once its historical 171/171 baseline was recorded.
