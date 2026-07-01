# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 32 — Texture2D completeness (Tasks 261–270), in progress.
  Phase 31 (User primitives and draw-call variants, Tasks 251–260) and Phase 30 (VertexDeclaration
  / vertex format accuracy, Tasks 241–250) are both fully complete.
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
- **EasyGL (`cmake-build-debug`)** — primary, most tested: builds clean.
- **Vulkan (`cmake-build-vulkan`)** — builds clean; requires single-threaded link (`-j1`) for
  stability.
- **Bgfx (`cmake-build-bgfx`)** — builds when configured.

### Test status
- **Unit tests (`CnaTests`, EasyGL build):** 1813/1813 pass (last verified this session).
- **EasyGL integration tests:** ~63 test executables registered in CMake; the vast majority pass.
  Two pre-existing failures: `easygl_device_dispose_order_test` (root cause unknown) and one
  pixel-readback test tied to the SpriteBatch multi-Begin/End bug (see §5).
- **Vulkan integration tests:** 13 registered; 11/13 historically pass.
- **Bgfx:** `Bgfx_VertexFormatMapping` test passes (47/47 mapping checks); no pixel-readback for
  Bgfx (backend has no readback API), so Bgfx coverage is smoke-only.

### What currently works
- Full unit-test coverage for `DrawUserPrimitives` / `DrawUserIndexedPrimitives` (all typed +
  `VertexDeclaration` overloads), including argument-guard and pixel-readback verification.
- `Texture2D`: constructors, `SetData`/`GetData` (with bounds checking on both), `FromStream`
  (PNG/JPEG/BMP/DDS-DXT1/3/5, plus a resize/crop overload), `SaveAsPng`/`SaveAsJpeg` (both
  round-trip verified), `LevelCount` (verified against FNA's mip-level formula).
- Recently implemented (this session, chronological):
  - Pixel-readback tests for `DrawUserIndexedPrimitives` with 16-bit and 32-bit indices.
  - Argument-guard tests for `DrawUserPrimitives` (`primitiveCount <= 0` throws).
  - A full FNA-conformance audit of `Texture2D` (see `AUDIT.md`), which found and led to fixing:
    two heap-buffer-overflow bugs in `SetData`, two missing `NOXNA` tags, a missing `FromStream`
    overload, and a hardcoded JPEG quality value.
  - Round-trip verification of `Texture2D::FromStream`/`SaveAsPng`/`SaveAsJpeg` and `LevelCount`.
  - A performance fix replacing 22 per-draw-call heap allocations in
    `DrawUserPrimitives`/`DrawUserIndexedPrimitives` with two reusable scratch buffers.
- Known working examples: `examples/dxt1_texture_test.cpp` (DDS/DXT1 decode via `FromStream`),
  `examples/easygl_texture2d_partial_rect_test.cpp` and `easygl_texture2d_mip_test.cpp`
  (`SetData`/`GetData` round trips), the DrawUserPrimitives/DrawUserIndexedPrimitives pixel-readback
  tests (Tasks 255–258), and `vulkan_scissor_test` (Task 329).

### What does NOT work yet
- `Texture2D` is still missing `SetDataPointerEXT`, `GetDataPointerEXT`, `TextureDataFromStreamEXT`,
  and `DDSFromStreamEXT`; `SurfaceFormat` support is effectively Color-only (`ValidateFormat`
  throws for every other format) — both found in the Task 261 audit, neither yet addressed.
- `DrawUserIndexedPrimitives` has no argument-guard unit tests for `primitiveCount <= 0` (only
  `DrawUserPrimitives` was covered).
- Multiple `SpriteBatch::Begin()`/`End()` calls per frame on Vulkan only renders the last batch.
- EasyGL and Bgfx stride-keyed vertex layout only supports strides 16/20/24/32/52.
- Non-power-of-two texture support (Task 268) and edge-sampling clamp/wrap verification
  (Task 269) have not been started.
- 363 tasks are still unchecked (⬜) out of 536 total in `GRAPHICS_TASKS.md`.

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
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

**There is no hard blocker.** The build is clean and all 1813 unit tests pass.

The closest thing to an open problem is that the Task 261 `Texture2D` audit still has two
unresolved (non-urgent) findings, documented in `AUDIT.md` under "Texture2D detailed audit":
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
| **Needs verification** | `easygl_device_dispose_order_test` fails; root cause unknown (pre-existing, not introduced this session). |
| **Incomplete** | `DrawUserIndexedPrimitives` has no argument-guard unit tests for `primitiveCount <= 0` (only `DrawUserPrimitives` is covered). |
| **Incomplete** | EasyGL and Bgfx stride-keyed vertex layout supports only strides 16/20/24/32/52; other `VertexDeclaration` layouts silently select the wrong VAO/pipeline. |
| **Incomplete** | Vulkan backend: `Tangent` and `Binormal` `VertexElementUsage` values are not mapped (no Vulkan semantic equivalent). |
| **Incomplete** | `VertexElementUsage` Depth/Fog/PointSize/Sample/TessellateFactor are unsupported in all 3D backends (no-op, or return `bgfx::Attrib::Count`). |
| **Incomplete** | SDL_Renderer backend: `CreateVertexBuffer` always throws `ThrowNo3D`; no 3D support at all. |
| **Incomplete** | Bgfx backend has no pixel-readback API, so its integration tests are smoke-only. |
| **Incomplete** | `Texture2D` missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and Color-only `SurfaceFormat` support (Task 261 audit; see `AUDIT.md`). |
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
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/CnaTests

# Run a specific test suite
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/CnaTests --gtest_filter="Texture2DTest.*"

# Build Vulkan (single-threaded to avoid linker races)
cmake --build cmake-build-vulkan --target CNA -j1

# Run a specific EasyGL integration test (headless, needs an X server on :0)
DISPLAY=:0 /rv/data/development/github.com/openeggbert/cna/cmake-build-debug/cna_test_easygl_draw_user_primitives_vpc

# CTest (all registered tests, EasyGL build)
cd cmake-build-debug && ctest --output-on-failure
```

There is no known reproducible failing command right now (see §4).

---

## 8. Next smallest tasks

In priority order:

1. **Task 270 — Add CPU-side shadow storage only where required for `GetData`; document cost**
   - Goal: Audit where `cpuPixels_` / mip-level shadow buffers are retained vs. freed
     (`MaybeFreeCpuPixels`, `contextRecoveryEnabled_`), confirm `GetData` still works correctly
     when context recovery is disabled, and document the memory-cost tradeoff.
   - Files: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`, likely a new `docs/` note.
   - Verification: existing + new unit tests pass; no unexpected memory growth.

2. **Add `DrawUserIndexedPrimitives` argument-guard unit tests**
   - Goal: Mirror Task 259's coverage (which only tested `DrawUserPrimitives`) for
     `DrawUserIndexedPrimitives`: `primitiveCount <= 0` should throw
     `System::ArgumentOutOfRangeException` for all 8 typed overloads + 2 `VertexDeclaration`
     overloads.
   - Files: `tests/Microsoft/Xna/Framework/Graphics/DrawUserIndexedPrimitivesTests.cpp`.
   - Verification: `CnaTests --gtest_filter="DrawUserIndexedPrimitives*"` → all pass.

3. **Task 268/269 — Non-power-of-two textures and edge sampling** (larger, needs backend work)
   - Goal: Verify NPOT textures (e.g. 3×5, 7×11) work across all backends, and that texture
     sampling at edges respects clamp/wrap `TextureAddressMode`. Likely needs pixel-readback
     integration tests, not just unit tests.
   - Files: TBD after scoping; likely new `examples/` integration tests.
   - Verification: pixel-readback tests pass on EasyGL (primary backend).

---

## 9. Do not do yet

- **No broad `Texture2D` rewrite** — the Task 261 audit is complete (see `AUDIT.md`); the two
  memory-safety bugs, the `FromStream`/`SaveAsPng`/`SaveAsJpeg` verification, the `NOXNA` tag fix,
  and `LevelCount` verification are all done. The two remaining findings (missing EXT statics,
  Color-only format support) should land incrementally per Phase 32 task, not as one large
  refactor.
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

---

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task.
Do not refactor unrelated code. Make one small, verified improvement.
Run the relevant build/test command before declaring the task done.
Update NEXT.md after finishing.

Current status: Phase 30 and Phase 31 fully complete. Phase 32 in progress
(Tasks 261, 262, 263, 264, 266, 267 done, plus a NOXNA tag fix). 1813/1813 unit tests pass.

Next task: Task 270 — add CPU-side shadow storage only where required for GetData; document cost.
Audit where cpuPixels_ / mip-level shadow buffers are retained vs. freed (MaybeFreeCpuPixels,
contextRecoveryEnabled_), confirm GetData still works correctly when context recovery is
disabled, and document the memory-cost tradeoff.
Files: src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp, likely a new docs/ note.
Verification: existing + new unit tests pass; no unexpected memory growth.
Update GRAPHICS_TASKS.md (mark 270 ✅) and NEXT.md after finishing.
```
