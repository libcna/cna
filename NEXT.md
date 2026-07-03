# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 32 (Texture2D completeness, Tasks 261–270) is **fully
  complete**. Phase 33 (Texture3D/TextureCube completeness, Tasks 271–280) is in progress — Tasks
  271 (`Texture3D` audit), 272 (`TextureCube` audit), 273 (`Texture3D` partial box x/y/z upload
  tests), 274 (`Texture3D` partial box x/y/z readback tests), 275 (`TextureCube` partial rect +
  startIndex tests, all six faces), and 276 (`TextureCube` mip-level tests, all six faces — found
  and fixed a real GPU-storage allocation bug, see §3) done. Phases 30 and 31 were already complete.
  New Tasks 663 and 862 were added (unnumbered-sequence, matching existing precedent) for severe
  findings from Tasks 272 and 276 — see §3. Next up: Task 277 (verify `Texture3D` sampling in
  EasyGL stock/custom effect).
- **Key architectural decisions:**
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). EasyGL is primary (most tested).
  - The `sharp-runtime` library (sibling repo at `../sharp-runtime/`) provides all `System.*` types
    and primitive aliases (`bytecs`, `String`, etc.) used on the XNA API surface.
  - Stride-keyed vertex layout: backends currently build VAO/pipeline/layout from a map keyed
    by vertex stride. Only strides 16/20/24/32/52 work for 3D — arbitrary layouts are future work.

---

## 2. Current status

### Build status
- **All three `cmake-build-*` dirs were re-created this session.** They had been pointing at the
  sibling `../cna` repo's source tree (`CMAKE_HOME_DIRECTORY` mismatch — likely leftover from a
  copy/split of the `cna` repo into `cna_graphics`), so every build in them was actually building
  `../cna`, not this repo. Deleted and reconfigured all three (`cmake -B ... -DCNA_GRAPHICS_BACKEND=...`)
  against `cna_graphics`. If a build dir ever needs recreating again, always configure it from
  *this* repo's root, not `../cna`.
- **EasyGL (`cmake-build-debug`)** — primary, most tested: reconfigured, fully rebuilt, all EasyGL
  ctest targets pass except 2 pre-existing/unrelated (see Test status below).
- **Vulkan (`cmake-build-vulkan`)** — reconfigured against `cna_graphics`, fully rebuilt this
  session (`-j1`, required for link stability). Rebuilding surfaced and led to fixing a real
  pre-existing Vulkan bug — see §3 and `AUDIT.md` "Vulkan `TransitionImageLayout` missing a
  re-upload transition".
- **Bgfx (`cmake-build-bgfx`)** — reconfigured against `cna_graphics` (the `bgfx.cmake`
  `FetchContent` re-clone took ~32 minutes since the old `_deps` cache was deleted with the rest of
  the dir) and fully rebuilt this session. **100% ctest pass** — see Test status below.

### Test status
- **Unit tests (`CnaTests`):** EasyGL 1900/1900, Vulkan 1900/1900, Bgfx 1904/1904
  (backend-conditional test count differs slightly per build, as expected — the delta is constant
  across sessions, not a regression). All three backends verified this session.
- **EasyGL integration tests (ctest, `cmake-build-debug`):** 1971 total ctest cases registered (unit
  tests + integration tests + the `easy-gl` sibling library's own smoke tests, all share one `ctest`
  run). **1969/1971 pass.** Two failures, both pre-existing/unrelated to this session (see §5):
  `EasyGL_MRT_TwoAttachments` and `easy-gl-resource-smoke-tests`. The previously-documented
  `easygl_device_dispose_order_test` failure is **no longer reproducing** (passed cleanly); entry in
  §5 downgraded to "needs re-verification" rather than deleted, in case it's flaky.
- **Vulkan integration tests (ctest, `cmake-build-vulkan`):** 1853 total ctest cases. **1852/1853
  pass** — improved from the previously-documented "11/13" baseline for the 13 Vulkan-specific
  integration tests (now 12/13; only `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails, a
  pre-existing depth-bias-precision issue, not investigated further — see §5).
- **Bgfx (ctest, `cmake-build-bgfx`):** 1847 total ctest cases. **1847/1847 pass (100%)**, including
  `Bgfx_Demo2D_SmokeTest`, `Bgfx_RenderTargetUsage`, and `Bgfx_VertexFormatMapping` (47/47 mapping
  checks). No pixel-readback for Bgfx (backend has no readback API), so Bgfx coverage is smoke-only,
  but everything that exists passes cleanly.

### What currently works
- Full unit-test coverage for `DrawUserPrimitives` / `DrawUserIndexedPrimitives` (all typed +
  `VertexDeclaration` overloads), including argument-guard and pixel-readback verification.
- `Texture2D`: constructors, `SetData`/`GetData` (with bounds checking on both), `FromStream`
  (PNG/JPEG/BMP/DDS-DXT1/3/5, plus a resize/crop overload), `SaveAsPng`/`SaveAsJpeg` (both
  round-trip verified), `LevelCount` (verified against FNA's mip-level formula), CPU shadow-storage
  lifetime under `SetContextRecoveryEnabled` (audited and pinned by tests, Task 270).
- Recently implemented (this session, chronological):
  - Pixel-readback tests for `DrawUserIndexedPrimitives` with 16-bit and 32-bit indices.
  - Argument-guard tests for `DrawUserPrimitives` (`primitiveCount <= 0` throws).
  - A full FNA-conformance audit of `Texture2D` (see `AUDIT.md`), which found and led to fixing:
    two heap-buffer-overflow bugs in `SetData`, two missing `NOXNA` tags, a missing `FromStream`
    overload, and a hardcoded JPEG quality value.
  - Round-trip verification of `Texture2D::FromStream`/`SaveAsPng`/`SaveAsJpeg` and `LevelCount`.
  - A performance fix replacing 22 per-draw-call heap allocations in
    `DrawUserPrimitives`/`DrawUserIndexedPrimitives` with two reusable scratch buffers.
  - Task 270: audited `cpuPixels_`/`extraMipLevels_` shadow-buffer lifetime; confirmed `GetData`
    permanently throws after the level-0 shadow is freed (no GPU readback fallback exists); found
    and fixed a real bug where a partial `SetData(level,rect,...)` after the shadow was freed could
    silently zero out the rest of the GPU texture — now throws instead. See `AUDIT.md`,
    "Texture2D CPU shadow storage".
  - `DrawUserIndexedPrimitives` argument-guard tests: `primitiveCount<=0` throws
    `ArgumentOutOfRangeException` for all 8 typed + 2 `VertexDeclaration` overloads (Task 252 had
    added the guard code but never the tests). Also found and fixed the untyped raw-`void*`
    overload, which had no guard at all until now.
  - Tasks 268/269: verified NPOT textures work end-to-end on EasyGL (new pixel-readback test);
    found and fixed 2 real `SpriteBatch` bugs on EasyGL — `SamplerState` (`Filter`/`AddressU`/
    `AddressV`) was never applied to sprite rendering at all (only SDL_Renderer's `Filter` worked),
    and UVs were hard-clamped to `[0,1]`, making `TextureAddressMode::Wrap`/`Mirror` structurally
    unreachable. Both fixed; see `AUDIT.md` "NPOT textures and SpriteBatch edge sampling".
  - Unrelated build-blocking fix: `StorageDevice.cpp`'s `SelectorResult`/`ContainerResult` didn't
    implement 2 new pure-virtual members that a concurrent `sharp-runtime` commit added to
    `System::IAsyncResult` (`getAsyncStateProperty`/`getAsyncWaitHandleProperty`) — fixed to match.
  - Rebuilt/verified Vulkan and Bgfx (only reconfigured earlier this session). Found and fixed a
    real pre-existing Vulkan bug: `TransitionImageLayout` was missing the
    `SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL` barrier `UpdatePixels` needs to re-upload a
    texture after its first `SetData` — every second-or-later `SetData` call threw on Vulkan. Fixed.
  - Task 265 (closes Phase 32): FNA's real `GetData<T>` doesn't bounds-check `rect` at the managed
    level at all (CNA's existing check is a deliberate safety extra, not a literal-parity gap).
    Found and fixed a real, symmetric bug instead: neither `GetData` overload validated
    `startIndex < 0` (the equivalent `SetData` overloads already did) — an OOB read from
    `cpuPixels_` (3-arg) or an OOB write into the caller's array (5-arg). Fixed both.
  - Task 271 (starts Phase 33): audited `Texture3D` against FNA and found/fixed 3 real bugs —
    `LevelCount` hardcoded to 1 regardless of `mipMap`; `SetData`/`GetData` had almost no input
    validation at all (null-data crash, negative-`elementCount` huge-allocation crash, negative-
    `startIndex` OOB read/write, no box-bounds check); `Dispose(bool)` was never overridden, so the
    GPU resource was never released on explicit `Dispose()`. Added `Texture3DTests.cpp` (31 new
    tests) — previously `Texture3D` had zero dedicated unit tests. See `AUDIT.md` "Texture3D
    detailed audit".
  - Task 272: audited `TextureCube` against FNA and found the *exact same 3 bug classes* as Task
    271 (confirming a systemic pattern, not one-off mistakes) plus 2 `TextureCube`-specific bugs —
    a missing `SetData`/`GetData(face,data,startIndex,elementCount)` overload, and a `rect==nullptr`
    -at-`level>0` bug that ignored `level` entirely (always used the full face `Size` instead of
    `Size>>level`, unlike `Texture2D`/`Texture3D`'s `mipDim()` pattern). All 5 fixed. Also found
    `DDSFromStreamEXT` is a **non-functional stub** — it ignores its `stream` argument and silently
    returns a blank 1×1 texture; documented as a severe finding (new Task 663), not fixed (a real
    DDS-cube-parser implementation is a substantial feature, out of this audit's guard-fixing
    scope). Added `TextureCubeTests.cpp` (27 new tests). See `AUDIT.md` "TextureCube detailed audit".
- Known working examples: `examples/dxt1_texture_test.cpp` (DDS/DXT1 decode via `FromStream`),
  `examples/easygl_texture2d_partial_rect_test.cpp` and `easygl_texture2d_mip_test.cpp`
  (`SetData`/`GetData` round trips), the DrawUserPrimitives/DrawUserIndexedPrimitives pixel-readback
  tests (Tasks 255–258), `vulkan_scissor_test` (Task 329),
  `easygl_npot_texture_test.cpp` / `easygl_texture_address_mode_test.cpp` (Tasks 268/269),
  `easygl_texture3d_slices_test.cpp` (Task 173, per-slice SetData/GetData round trip), and
  `easygl_texturecube_faces_test.cpp` (Task 172, per-face SetData/GetData round trip).

### What does NOT work yet
- `Texture2D` is still missing `SetDataPointerEXT`, `GetDataPointerEXT`, `TextureDataFromStreamEXT`,
  and `DDSFromStreamEXT`; `SurfaceFormat` support is effectively Color-only (`ValidateFormat`
  throws for every other format) — both found in the Task 261 audit, neither yet addressed.
- Multiple `SpriteBatch::Begin()`/`End()` calls per frame on Vulkan only renders the last batch.
- `SpriteBatch`'s `SamplerState` (`Filter`/`AddressU`/`AddressV`) is still a no-op on **Vulkan and
  Bgfx** — only EasyGL was fixed this session (Task 269); those two backends' `ISpriteBatchBackend`
  implementations don't override `SetSamplerFilter`/`SetSamplerAddressMode` at all.
- EasyGL and Bgfx stride-keyed vertex layout only supports strides 16/20/24/32/52.
- `Texture2D::extraMipLevels_` (mip levels >0 CPU shadow) is never freed regardless of
  `SetContextRecoveryEnabled` — only the level-0 shadow (`cpuPixels_`) participates in the
  RAM-saving optimization (Task 270 audit finding; documented, not fixed — see `AUDIT.md`).
- EasyGL's `Texture3D` backend ignores `mipMap` and `SurfaceFormat` entirely — always creates a
  single-level `Rgba8` 3D texture regardless of what the caller requested (Task 271 audit finding;
  the `LevelCount` *property* is now correct, but the GPU texture itself still has no real mip
  chain). Feeds a later Phase 33 task, not fixed now. `TextureCube`'s EasyGL backend likely has the
  same limitation (not separately re-verified — Task 272 audit note).
- **`TextureCube::DDSFromStreamEXT` does not work at all** — it's a silent stub that ignores the
  input stream and always returns a blank 1×1 texture (Task 272 finding; new Task 663 tracks a
  real implementation). This is more severe than the other "missing API surface" items above,
  since it fails silently rather than loudly (compiles, runs, returns a plausible-looking object).
- 358 tasks are still unchecked (⬜) out of 537 total in `GRAPHICS_TASKS.md` (Task 272 completed,
  and a new Task 663 was added ⬜ this session for the `DDSFromStreamEXT` finding — net unchanged
  from before Task 272, since one task closed and one was added).

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
| 276 | `EasyGLGraphicsBackend.cpp`, `examples/easygl_texturecube_mip_test.cpp` (new), `CMakeLists.txt`, `AUDIT.md`, `GRAPHICS_TASKS.md` | **Found and fixed a real bug.** New mip round-trip test (all 6 faces × 3 levels) initially failed: `EasyGLTextureCubeBackend`'s constructor only allocated GPU storage for level 0 (`set_image_2d`, no level loop), so `SetData`'s `set_sub_image_2d` (`glTexSubImage2D`) writes to level 1+ silently went nowhere (that call requires the level to already be defined). Fixed by pre-allocating every mip level for every face in the constructor. All 126 checks now pass. Same bug shape flagged (not fixed here) for `Texture3D`'s identical single-level-only pattern — tracked as new Task 862. 1973/1975 EasyGL ctest (same 2 pre-existing failures). |
| 275 | `examples/easygl_texturecube_partial_rect_test.cpp` (new), `CMakeLists.txt`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Task 172 already covered whole-face round-trip for all 6 faces (simple 2-arg overload); `TextureCubeTests.cpp`'s rect-based/startIndex overload coverage was argument-guards only. New test closes that gap with real pixel verification: off-centre 2×2 rect per face (no cross-face bleed), `SetData`/`GetData` `startIndex` with real data. All pass, no bug found. |
| 274 | `examples/easygl_texture3d_partial_box_readback_test.cpp` (new), `CMakeLists.txt`, `AUDIT.md`, `GRAPHICS_TASKS.md` | `GetData` box readback with a per-voxel-unique colour (so axis swaps are detectable, unlike Task 273's binary split): asymmetric off-origin box, `startIndex`, far-corner box. All pass, no bug found. Also confirmed FNA itself never validates `elementCount` against box volume — CNA's matching lack of that check is faithful behavior, not a gap. |
| 273 | `examples/easygl_texture3d_partial_box_test.cpp` (new), `CMakeLists.txt`, `AUDIT.md`, `GRAPHICS_TASKS.md` | `SetData` box upload with genuine x/y/z sub-regions (Task 173's existing test only varied z across full width/height): asymmetric off-origin box, single-voxel box, far-corner box. All pass, no bug found — confirms arbitrary 3D sub-region upload already worked correctly. |
| 272 | `TextureCube.hpp/.cpp`, `TextureCubeTests.cpp` (new), `AUDIT.md`, `GRAPHICS_TASKS.md` | Audited `TextureCube` against FNA (`TextureCube.cs`) and found the *same 3 bug classes* as Task 271's `Texture3D` audit (hardcoded `LevelCount`, missing `SetData`/`GetData` guards, missing `Dispose(bool)`), confirming a systemic pattern — all fixed identically. Plus 2 `TextureCube`-specific bugs: (4) the `SetData`/`GetData(face,data,startIndex,elementCount)` overload was missing from the API entirely (FNA has 3 arities, CNA had 2) — added, delegating to the 6-arg overload like FNA's own overloads do; (5) `rect==nullptr` at `level>0` ignored `level` completely, always using the full face `Size` instead of `Size>>level` (unlike `Texture2D`/`Texture3D`'s `mipDim()` pattern) — fixed with the same `mipDim()` helper, pinned by 2 regression tests proving a level-1-sized call now succeeds and a level-0-sized one at level 1 is now correctly rejected. Also added a rect-bounds check to both `SetData`/`GetData` (FNA has neither for `TextureCube`, unlike `Texture3D`'s `GetData`-only check — extends C++ safety consistently). **Also found: `DDSFromStreamEXT` is a non-functional stub** — `return TextureCube(device, 1, false, SurfaceFormat::Color);`, ignoring `stream` entirely, always silently returning a blank 1×1 texture. Documented as a severe finding (fails silently, not loudly) and tracked as new Task 663 — not fixed here (a real DDS-cube-parser is a substantial feature, out of this audit's guard-fixing scope). 27 new unit tests (`TextureCubeTests.cpp` — previously zero dedicated TextureCube unit tests existed). 1900/1900 (EasyGL/Vulkan) / 1904/1904 (Bgfx) unit tests; 1969/1971 EasyGL ctest (same 2 pre-existing failures); existing `easygl_texturecube_faces_test.cpp` (Task 172) still 24/24 pixel checks pass. See `AUDIT.md` "TextureCube detailed audit". |
| 271 | `Texture3D.hpp/.cpp`, `Texture3DTests.cpp` (new), `AUDIT.md`, `GRAPHICS_TASKS.md` | Starts Phase 33. Audited `Texture3D` against FNA (`Texture3D.cs`) and found/fixed 3 real bugs, mirroring the Texture2D Tasks 261/265/266 pattern: (1) `LevelCount` was hardcoded to 1, ignoring `mipMap` — now computes `CalculateMipLevels(width,height)`, matching FNA; (2) `SetData`/`GetData` had almost no input validation at all — null `data` caused a segfault, negative `elementCount` risked a huge-allocation crash (unsigned cast), negative `startIndex` caused an OOB read (`SetData`) or write (`GetData`), and the 10-arg box overloads had no bounds check at all (FNA's own `GetData` has one, `SetData` doesn't — added it to both, extending C++ safety beyond literal FNA parity, matching Texture2D's established precedent); (3) `Dispose(bool)` was never overridden, so `backend_` (and the GPU texture) was never released on explicit `Dispose()` — fixed, mirroring `Texture2D`'s pattern exactly (`using GraphicsResource::Dispose;` + override). Also refactored the 2-arg/3-arg `SetData`/`GetData` to delegate to the 10-arg overload instead of duplicating logic, matching how FNA's own overloads delegate. 31 new unit tests (`Texture3DTests.cpp` — previously zero dedicated Texture3D unit tests existed). 1873/1873 (EasyGL/Vulkan) / 1877/1877 (Bgfx) unit tests; 1942/1944 EasyGL ctest (same 2 pre-existing failures). Documented, not fixed: EasyGL's `Texture3D` backend ignores `mipMap`/`SurfaceFormat` entirely. See `AUDIT.md` "Texture3D detailed audit". |
| 265 | `Texture2D.hpp/.cpp`, `Texture2DTests.cpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Closes Phase 32. FNA's real `GetData<T>` doesn't bounds-check `rect` at the managed level (delegates to native `FNA3D_GetTextureData2D`) — CNA's existing rect-bounds check is a deliberate safety extra, not a literal-parity gap. Found and fixed a real, symmetric bug instead: neither `GetData` overload validated `startIndex < 0` (the equivalent `SetData` overloads already do) — negative `startIndex` caused an OOB read from `cpuPixels_` (3-arg overload) or an OOB write into the caller's array (5-arg overload). Fixed both, mirroring `SetData`'s guard exactly. 2 new unit tests; 1842/1842 pass on EasyGL/Vulkan/Bgfx; 1911/1913 EasyGL ctest (same 2 pre-existing failures). |
| — | `VulkanGraphicsBackend.cpp`, `AUDIT.md` | Rebuilt + fully verified `cmake-build-vulkan` and `cmake-build-bgfx` (both had only been reconfigured, not rebuilt, earlier this session). Found and fixed a real pre-existing Vulkan bug while doing so: `TransitionImageLayout` was missing the `SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL` barrier case that `VulkanTextureBackend::UpdatePixels` needs to re-upload a texture after its first upload — every `SetData` call after the first threw `"Vulkan: unsupported image layout transition"` on any Vulkan-backed texture. Added the missing case (symmetric with the existing reverse-direction case). Vulkan: `CnaTests` 1840/1840 (was 1838/1840), ctest 1852/1853 (only `Vulkan_DepthBias`'s `DepthBias=-1e6` sub-case fails, pre-existing/unrelated — up from the documented "11/13" baseline). Bgfx: `CnaTests` 1844/1844, ctest 1847/1847 (100%). See `AUDIT.md` "Vulkan `TransitionImageLayout` missing a re-upload transition". |
| 268/269 | `IGraphicsBackend.hpp`, `SpriteBatch.cpp`, `EasyGLGraphicsBackend.hpp/.cpp`, `CMakeLists.txt`, `examples/easygl_npot_texture_test.cpp` (new), `examples/easygl_texture_address_mode_test.cpp` (new), `AUDIT.md`, `GRAPHICS_TASKS.md` | Task 268: verified NPOT textures (3×5) upload and GPU-sample correctly end-to-end on EasyGL (new pixel-readback test, 5/5 rows correct); no POT/NPOT branching exists in any backend. Task 269: found and fixed 2 real bugs — (1) `SpriteBatch::Begin()`'s `SamplerState` had zero effect on EasyGL/Vulkan/Bgfx (`AddressU`/`AddressV` never read at all; `Filter` only worked on SDL_Renderer) — added `ISpriteBatchBackend::SetSamplerAddressMode`, `Begin()` now always resolves+applies `SamplerState` (default `LinearClamp`, matching FNA) via EasyGL's existing `ApplySamplerState`; (2) EasyGL's SpriteBatch UV math hard-clamped to `[0,1]`, making `Wrap`/`Mirror` unreachable — removed (FNA never clamps). New `EasyGL_TextureAddressMode` test proves `PointWrap` vs `PointClamp` now sample distinctly. EasyGL only — Vulkan/Bgfx SpriteBatch SamplerState remains a no-op (documented gap). Also fixed an unrelated build break in `StorageDevice.cpp` (`sharp-runtime`'s `IAsyncResult` gained 2 new pure-virtual members in a concurrent commit). 1840/1840 unit tests still pass; 1909/1911 ctest (2 pre-existing, unrelated failures — see §5). |
| — | `GraphicsDevice.cpp`, `DrawUserIndexedPrimitivesTests.cpp`, `GRAPHICS_TASKS.md` | Added the `primitiveCount<=0` argument-guard unit tests for `DrawUserIndexedPrimitives` that Task 252 claimed but never landed (mirrors Task 259). All 8 typed + 2 `VertexDeclaration` overloads, zero and negative counts. Found and fixed a real gap: the untyped raw-`void*` overload had no guard at all — added `ArgumentOutOfRangeException::ThrowIfNegativeOrZero`, matching the other 10 overloads. 22 new unit tests; 1840/1840 total pass. |
| 270 | `Texture2D.hpp/.cpp`, `Texture2DTests.cpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Audited `cpuPixels_`/`extraMipLevels_` shadow-buffer retention vs. `SetContextRecoveryEnabled`. Confirmed `GetData` throws permanently once the level-0 shadow is freed (no GPU readback fallback in `ITextureBackend`). Found and fixed a real bug: partial `SetData(level,rect,...)` after the shadow was freed resurrected a zero-filled buffer and re-uploaded it whole, silently zeroing already-uploaded GPU pixels outside the rect — now throws `std::runtime_error` instead; the level-0 branch also now calls `MaybeFreeCpuPixels()` so partial updates no longer defeat the RAM-saving feature. `extraMipLevels_` is never freed — documented as an open gap, not fixed. 5 new unit tests; 1818/1818 total pass. |
| 260 | `GraphicsDevice.hpp/.cpp`, `GRAPHICS_TASKS.md` | Added 2 per-device scratch buffers (`userVertexScratch_`, `userIndexScratch_`), replacing 22 per-call `std::vector` heap allocations across all `DrawUserPrimitives`/`DrawUserIndexedPrimitives` typed + `VertexDeclaration` overloads. 1813/1813 unit tests + 8/8 pixel-readback checks (Tasks 255–258) still pass. Closes Phase 31. |
| 267 | `Texture2DTests.cpp` (extended), `GRAPHICS_TASKS.md` | `LevelCount` verification: 2-arg ctor / `mipMap=false` always 1; `mipMap=true` matches FNA's `CalculateMipLevels` for square/non-square/non-power-of-two sizes. 5 new unit tests. |
| — | `Texture2D.hpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Tagged the 2 `assetName` constructors `NOXNA` (Task 261 audit finding: neither is part of the FNA/XNA API). No behavior change. |
| 264 | `Texture2D.cpp`, `Texture2DTests.cpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | `SaveAsJpeg` round-trip verification (tolerance-based; confirmed alpha is dropped, since JPEG has no alpha channel). Fixed hardcoded JPEG quality=100 by adding `FNA_GRAPHICS_JPEG_SAVE_QUALITY` env-var support. 8 new unit tests. |
| 263 | `Texture2DTests.cpp` (extended), `GRAPHICS_TASKS.md` | `SaveAsPng` round-trip verification: error guards, multi-pixel + alpha exact match, non-square size, save-time resize, filename overload via temp file. 6 new unit tests. |
| 262 | `Texture2D.hpp/.cpp`, `Texture2DTests.cpp`, `docs/texture-stream-formats.md` (new), `AUDIT.md`, `GRAPHICS_TASKS.md` | Verified PNG/JPEG/BMP `FromStream` decoding via round-trip tests. Added the missing `FromStream(device, stream, width, height, zoom)` overload, matching FNA3D's resize/crop semantics. 5 new unit tests. |
| 266 | `Texture2D.cpp`, `Texture2DTests.cpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Fixed both Task 261 heap-buffer-overflow bugs: `SetData(level, rect, ...)` now bounds-checks `rect` against the mip level (mirrors `GetData`); `SetData(Color*, elementCount)` now validates the buffer size before building the `ImageData`. 7 new unit tests. |
| 261 | `AUDIT.md` (extended), `GRAPHICS_TASKS.md` | Detailed `Texture2D` vs. FNA audit (no code changes) — found the 2 OOB bugs above, 2 missing `NOXNA` tags, 4 missing EXT methods, and Color-only format support. |
| 259 | `DrawUserPrimitivesTests.cpp` (extended), `GRAPHICS_TASKS.md` | Argument-guard tests: `primitiveCount<=0` throws `ArgumentOutOfRangeException` for all 5 `DrawUserPrimitives` overloads. 10 new unit tests. |
| 258 | `examples/easygl_draw_user_indexed_primitives_32_test.cpp` (new), `CMakeLists.txt` | `DrawUserIndexedPrimitives` pixel-readback with 32-bit indices; 2/2 PASS. |
| 257 | `examples/easygl_draw_user_indexed_primitives_vpc_test.cpp` (new), `CMakeLists.txt` | `DrawUserIndexedPrimitives` pixel-readback with 16-bit indices; 2/2 PASS. |

Earlier history (Tasks 241–256, 329, and Phase 30) is in `GRAPHICS_TASKS.md`; not repeated here to
keep this section current and scannable.

---

## 4. Current blocker / main problem

**There is no hard blocker.** The build is clean and all 1900 (EasyGL/Vulkan) / 1904 (Bgfx) unit
tests pass on all three backends. Three pre-existing, unrelated ctest failures are documented in §5
(`EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`, `Vulkan_DepthBias`'s `-1e6` sub-case).
None block normal development; each needs its own dedicated investigation. Separately, `TextureCube::
DDSFromStreamEXT` is a confirmed non-functional silent stub (Task 272 finding, tracked as new Task
663) — not a build/test blocker, but worth flagging prominently since it fails silently. Also,
`Texture3D`'s mip levels >0 are suspected broken via the same silent-failure GPU-storage-allocation
bug that Task 276 found and fixed for `TextureCube` (tracked as new Task 862, not yet reproduced
with a test) — likely-but-unconfirmed, not a build/test blocker either.

The closest thing to an open problem beyond that is that the Task 261 `Texture2D` audit still has
two unresolved (non-urgent) findings, documented in `AUDIT.md` under "Texture2D detailed audit":
- Missing `SetDataPointerEXT` / `GetDataPointerEXT` / `TextureDataFromStreamEXT` /
  `DDSFromStreamEXT` methods.
- `SurfaceFormat` support is effectively Color-only (`Texture::ValidateFormat` throws for any
  other format), so `Texture2D` cannot represent compressed or packed pixel formats natively.

Neither is a crash or correctness bug — both are missing API surface, not memory-unsafe. No
failing command or failing test is associated with either.

---

## 5. Known bugs and limitations

| Status | Issue |
|--------|-------|
| **Confirmed bug** | `SpriteBatch` with multiple `Begin()`/`End()` calls per frame on Vulkan: only the last batch renders. Workaround: merge all sprite draws into one Begin/End. |
| **Confirmed bug, newly found** | `EasyGL_MRT_TwoAttachments` ctest (Task 145, `examples/easygl_mrt_test.cpp`) fails deterministically (4/4 runs): `SetRenderTargets` with 2 attachments renders the correct colour to attachment 0 but attachment 1 stays black instead of the expected colour. Not caused by this session (no file touched here relates to FBO/render-target code) — surfaced only because this was the first full `ctest` run in a while. Needs dedicated investigation; do not fix opportunistically. |
| **Confirmed bug, newly found, out-of-repo** | `easy-gl-resource-smoke-tests` (sibling `easy-gl` repo) aborts deterministically (3/3 runs) on an `assert(g_state.last_active_texture == 0x84C0)` failure in `test_texture_upload_sets_unpack_alignment_wrap_and_unit0_binding` (`easy-gl/tests/smoke/SmokeResourceTests.cpp:336`). `easy-gl`'s `src/Texture.cpp` hasn't changed since 2026-06-27 and has no uncommitted changes — unrelated to this session. Out of scope: would need investigation in the sibling repo. |
| **Needs re-verification** | `easygl_device_dispose_order_test` — previously documented as failing with an unknown root cause; **passed cleanly** in this session's full `ctest` run (`EasyGL_DeviceDisposeOrder ... Passed`). Possibly flaky, possibly already fixed incidentally. Downgraded from "confirmed failing" until re-checked a few more times. |
| **Confirmed bug, pre-existing** | `Vulkan_DepthBias` ctest: the `DepthBias=-1e6` sub-case fails (`got=(255,0,0) expected GREEN`); the other 3/4 sub-cases (DepthBias=0, SlopeScale=0, SlopeScale=-2000) pass. Surfaced by fully rebuilding/retesting Vulkan this session (previously only "11/13 historically pass" was documented, without naming which 2). Likely a depth-bias scale/precision mismatch for very large bias values — not investigated further; out of scope for Tasks 268/269/270. |
| **Incomplete** | `SpriteBatch`'s `SamplerState` (`Filter`/`AddressU`/`AddressV`) is a no-op on Vulkan and Bgfx — only EasyGL was fixed (Task 269); their `ISpriteBatchBackend` implementations don't override `SetSamplerFilter`/`SetSamplerAddressMode` at all. |
| **Incomplete** | EasyGL and Bgfx stride-keyed vertex layout supports only strides 16/20/24/32/52; other `VertexDeclaration` layouts silently select the wrong VAO/pipeline. |
| **Incomplete** | Vulkan backend: `Tangent` and `Binormal` `VertexElementUsage` values are not mapped (no Vulkan semantic equivalent). |
| **Incomplete** | `VertexElementUsage` Depth/Fog/PointSize/Sample/TessellateFactor are unsupported in all 3D backends (no-op, or return `bgfx::Attrib::Count`). |
| **Incomplete** | SDL_Renderer backend: `CreateVertexBuffer` always throws `ThrowNo3D`; no 3D support at all. |
| **Incomplete** | Bgfx backend has no pixel-readback API, so its integration tests are smoke-only. |
| **Incomplete** | `Texture2D` missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and Color-only `SurfaceFormat` support (Task 261 audit; see `AUDIT.md`). |
| **Incomplete** | `Texture2D::extraMipLevels_` (mip levels >0 CPU shadow) is never freed regardless of `SetContextRecoveryEnabled(false)` — only the level-0 shadow participates in the RAM-saving optimization (Task 270 audit finding). |
| **Confirmed bug, severe, silent failure** | `TextureCube::DDSFromStreamEXT` is a non-functional stub — it ignores its `stream` parameter entirely and always returns a blank 1×1 `Color` texture, regardless of what DDS cube-map data (if any) is passed. It compiles, runs without throwing, and returns a plausible-looking `TextureCube`, so callers get silently wrong data instead of a loud failure. Task 272 finding; tracked as new `GRAPHICS_TASKS.md` Task 663 (needs DDS header parsing + per-face/per-level DXT decode + 6×levelCount `SetData` calls — a real feature, not a guard fix). |
| **Incomplete** | EasyGL's `Texture3D` backend ignores `mipMap`/`SurfaceFormat` entirely (Task 271 finding); `TextureCube`'s EasyGL backend likely has the same limitation (not separately re-verified — Task 272 audit note, feeds Task 278). |
| **By design (documented Task 270)** | `Texture2D::GetData` (level 0) permanently throws `std::runtime_error` after the first full upload once `SetContextRecoveryEnabled(false)` has been called — CNA has no GPU pixel-readback path, unlike FNA's real `GetData<T>`, which always reads back from the GPU. |
| **Risky assumption** | The new user-primitive scratch buffers (Task 260) never shrink — memory stays at the high-water mark for the life of the `GraphicsDevice`. Acceptable for typical usage, but worth knowing if a game does one enormous user-primitive draw and never again. |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, `IVertexBuffer`, etc. |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via EasyGL wrapper |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | Uses `VulkanVertexFormatHelper.hpp` for per-format mapping |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | Uses `BgfxVertexFormatHelper.hpp`; no readback |
| CNA utilities | `include/CNA/`, `src/CNA/` | NOXNA helpers, logging, math |
| sharp-runtime | `../sharp-runtime/` (sibling repo) | `System.*` types, primitive aliases |

### Critical invariants (do not break these)

- **`NOXNA` macro** tags every non-XNA extension in public headers — required for any new
  CNA-only public method/constructor.
- **C# properties** → `getXProperty()` / `setXProperty()` convention (never public fields on the
  XNA API surface).
- **`static readonly`** (C#) → `static const` member in `.hpp` + definition in `.cpp`.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) must
  be used on XNA API surfaces — never raw `uint8_t`, `float`, `std::string` directly.
- **Backend selection is compile-time** — no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout:** EasyGL, Vulkan, and Bgfx build VAO/pipeline/layout from a map
  keyed by vertex stride; only strides 16/20/24/32/52 work correctly for 3D. Do not assume
  arbitrary strides work.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */`.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` required at the top of every `.hpp`/`.cpp`.
- **Effect must be applied before any draw call** — all `DrawUser*` overloads throw
  `std::runtime_error` when `currentEffect_` is null.
- **`GraphicsDevice::userVertexScratch_` / `userIndexScratch_`** are shared, growable,
  non-shrinking scratch buffers used by all `DrawUserPrimitives`/`DrawUserIndexedPrimitives`
  overloads (Task 260). They must never be resized *down*, and must not be reentered
  (no nested/concurrent draw calls reusing them mid-write).

### Data flow (indexed user primitives draw call)

```
Game calls GraphicsDevice::DrawUserIndexedPrimitives(type, vertices, vOffset, numVerts, indices, iOffset, primCount)
  → validates backend_ non-null (silent return if null)
  → validates currentEffect_ non-null (throws if null)
  → validates primCount > 0 (throws ArgumentOutOfRangeException)
  → computes index count via IndexCountForPrimitives(type, primCount)
  → packs vertex data into a typed GpuV* struct, backed by the reusable userVertexScratch_ buffer
  → creates transient IVertexBuffer + IIndexBuffer via backend_->CreateVertexBuffer/CreateIndexBuffer16/32
  → calls backend_->DrawIndexedPrimitivesEx(*vb, *ib, world, view, proj, type, primCount, gpuParams)
  → backend maps vertex stride → VAO/pipeline/layout → GPU draw
```

### FNA reference

Authoritative behavioral reference: `/rv/data/library/github.com/FNA-XNA/FNA/src`.
When CNA diverges from FNA intentionally, document it with an inline `//` comment in the `.cpp`.

---

## 7. Useful commands

```bash
# Configure (EasyGL — primary)
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Configure (Vulkan)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON

# Configure (Bgfx)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BUILD_TESTS=ON

# Build CNA library (EasyGL)
cmake --build cmake-build-debug --target CNA -j$(nproc)

# Build all unit tests (EasyGL)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run unit tests
/rv/data/development/github.com/openeggbert/cna_graphics/cmake-build-debug/CnaTests

# Run a specific test suite
/rv/data/development/github.com/openeggbert/cna_graphics/cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Build Vulkan (single-threaded to avoid linker races)
cmake --build cmake-build-vulkan --target CNA -j1

# Run a specific EasyGL integration test (headless, needs an X server on :0)
DISPLAY=:0 /rv/data/development/github.com/openeggbert/cna_graphics/cmake-build-debug/cna_test_easygl_draw_user_primitives_vpc

# CTest (all registered tests, EasyGL build)
cd cmake-build-debug && ctest --output-on-failure
```

There is no known reproducible failing command right now (see §4).

---

## 8. Next smallest tasks

In priority order:

1. **Task 663 — implement `TextureCube::DDSFromStreamEXT` for real** (highest priority: silent
   failure, not just missing coverage)
   - Goal: replace the current stub (`return TextureCube(device, 1, false, SurfaceFormat::Color);`,
     which ignores `stream` entirely) with a real DDS cube-map parser: header parsing (magic,
     size, mip levels, `isCube` flag — throw if the DDS isn't actually a cube map, matching FNA's
     `FormatException`), reusing `Texture2D.cpp`'s private `TryDecodeDds`/`DxtUtil::
     DecompressDxt1/3/5` decode helpers, then 6 faces × `levelCount` `SetData` calls reading
     sequential per-face-per-level blocks from the stream (mirrors `TextureCube.cs:314-405`).
   - Files: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp`,
     `tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp`, possibly a small refactor to
     expose `Texture2D.cpp`'s DDS-decode helper for reuse (or a duplicate — check size/complexity
     before deciding, matching this project's "small duplication over premature shared utility"
     convention already used for `CalculateMipLevels`/`mipDim`).
   - Verification: new unit test(s) round-tripping a hand-built or real DDS cube-map file; confirm
     `isCube=false` DDS input throws instead of silently succeeding.

2. **Extend the Task 269 `SpriteBatch` `SamplerState` fix to Vulkan and Bgfx** (optional follow-up)
   - Goal: `VulkanSpriteBatchBackend`/the Bgfx equivalent still don't override
     `SetSamplerFilter`/`SetSamplerAddressMode` — `SamplerState` passed to `SpriteBatch::Begin()`
     is silently ignored on those two backends (EasyGL was fixed this session). Same fix shape:
     store pending filter/address values, apply via each backend's existing sampler mechanism
     (`VkSampler` cache for Vulkan, `BGFX_SAMPLER_*` flags for Bgfx) at draw/flush time.
   - Files: `include/CNA/Internal/Backends/Vulkan/…`, `src/CNA/Internal/Backends/Vulkan/…` and/or
     the Bgfx equivalents.
   - Verification: a Vulkan/Bgfx pixel-readback test analogous to `EasyGL_TextureAddressMode`, if
     readback infrastructure exists for that backend (Bgfx has none — smoke-test only there).

3. **Task 273/274 — `Texture3D` partial box upload/readback tests** (continues Phase 33)
   - Goal: dedicated x/y/z sub-region `SetData`/`GetData` tests beyond the existing full-slice
     round trip (`easygl_texture3d_slices_test.cpp`) — partial boxes within a single slice, boxes
     spanning multiple slices, and boxes at non-zero mip levels (Task 271 added the guards but not
     this positive-path coverage).
   - Files: `tests/Microsoft/Xna/Framework/Graphics/Texture3DTests.cpp` and/or a new EasyGL
     integration test.
   - Verification: new tests pass on all 3 backends.

---

## 9. Do not do yet

- **No broad `Texture2D` rewrite** — Phase 32 (Tasks 261–270) is fully complete (see `AUDIT.md`).
  Two lower-priority findings from the Task 261 audit remain open (missing EXT statics, Color-only
  format support) but have no owning phase/task anymore — treat any future work on them as its own
  small, scoped task, not a bundled rewrite.
- **No further changes to the user-primitive scratch buffers (Task 260)** without re-running the
  Tasks 255–258 pixel-readback tests — they are shared, growable, non-shrinking state on
  `GraphicsDevice`; any change to their lifetime or sizing logic needs the same regression pass.
- **No refactor of the stride-keyed vertex layout system** — it is load-bearing for all 3D tests;
  changes need their own dedicated phase with full regression testing.
- **No changes to the Bgfx backend draw path** — pixel readback is unavailable there, so
  correctness cannot be verified.
- **No SpriteBatch Vulkan multi-batch fix** until the root cause is isolated — a wrong fix could
  break single-batch rendering silently.
- **No API renames or namespace moves** — XNA API names are frozen by spec.
- **No mass Doxygen cleanup passes** — add Doxygen only when touching a file for another reason.
- **No new sharp-runtime types** unless a concrete CNA task requires them.
- **No broad `GetData`/`SetData` rewrite** — the scope for further changes should come from a
  dedicated audit, not ad-hoc cleanup.
- **No opportunistic fix of `EasyGL_MRT_TwoAttachments`** (newly found this session) — it needs its
  own root-cause investigation (FBO/render-target attachment code), not a guess bundled into an
  unrelated task.
- **No investigation of the `easy-gl-resource-smoke-tests` failure** as part of a CNA task — it's in
  the sibling `easy-gl` repo; if it needs fixing, that's a separate `easy-gl` session's work.
- **No further UV-clamp-style changes to `EasyGLSpriteBatchBackend::Draw()`** without re-running the
  full sprite pixel-readback suite (`EasyGL_TexturedQuad`, `EasyGL_SpriteEffects_Flip`,
  `EasyGL_TransformMatrix_Translation`, `EasyGL_NpotTexture`, `EasyGL_TextureAddressMode`) — this is
  the single most heavily-used 2D rendering path in the engine.
- **No opportunistic fix of `Vulkan_DepthBias`'s `DepthBias=-1e6` failure** (newly found this
  session) — needs its own root-cause investigation (depth-bias scale/precision), not a guess
  bundled into an unrelated task.
- **No rushed `TextureCube::DDSFromStreamEXT` implementation (Task 663) without real test fixtures**
  — a "looks plausible" DDS-cube parser that isn't actually verified against a real or carefully
  hand-built DDS cube-map file would just trade one silent-failure stub for a differently-silent
  bug. Build the test fixture(s) first, then the implementation, then verify against them —
  don't mark Task 663 done on the strength of "it compiles and doesn't throw."

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task.
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md after finishing.

Current status: Phase 30, Phase 31, and Phase 32 (Tasks 261-270) are all fully complete. Phase 33
(Texture3D/TextureCube completeness, Tasks 271-280) is in progress: Tasks 271 (Texture3D audit) and
272 (TextureCube audit) are done. All three cmake-build-* dirs (EasyGL, Vulkan, Bgfx) are
reconfigured against cna_graphics AND fully rebuilt/retested this session: EasyGL 1969/1971 ctest,
Vulkan 1852/1853 ctest, Bgfx 1847/1847 (100%) — remaining failures are pre-existing/unrelated, see
NEXT.md §5. Also fixed several real pre-existing bugs found along the way: a Vulkan
TransitionImageLayout gap; a Texture2D::GetData startIndex<0 guard gap; in Texture3D (Task 271), a
hardcoded LevelCount, missing SetData/GetData input validation, and a missing Dispose(bool)
override; and in TextureCube (Task 272), the identical 3 bug classes as Texture3D (confirming a
systemic pattern) plus 2 TextureCube-specific bugs: a missing SetData/GetData(face,data,startIndex,
elementCount) overload, and a rect==nullptr-at-level>0 bug that ignored the mip level entirely.
Task 272 also found TextureCube::DDSFromStreamEXT is a non-functional silent stub — documented as
a severe finding, tracked as new Task 663, deliberately NOT fixed in the audit pass (it's a
substantial feature, not a guard fix).

Next task: Task 663 — implement TextureCube::DDSFromStreamEXT for real (highest priority: this is a
silent-failure bug, more urgent than ordinary missing coverage). The current implementation ignores
its `stream` argument entirely and always returns a blank 1x1 texture. Needs: DDS header parsing
(magic, size, mip levels, isCube flag - throw if not actually a cube map, matching FNA's
FormatException), reusing Texture2D.cpp's private TryDecodeDds/DxtUtil::DecompressDxt1/3/5 decode
helpers, then 6 faces x levelCount SetData calls reading sequential per-face-per-level blocks from
the stream (mirrors TextureCube.cs:314-405). Build real DDS-cube-map test fixture(s) BEFORE writing
the implementation — do not mark this done on "compiles and doesn't throw" alone, since that's
exactly how the current stub looks superficially fine.
Files: src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp,
tests/Microsoft/Xna/Framework/Graphics/TextureCubeTests.cpp.
Verification: new unit test(s) round-tripping a real/hand-built DDS cube-map file; confirm
isCube=false DDS input throws instead of silently succeeding.
Update GRAPHICS_TASKS.md and NEXT.md after finishing.
```
