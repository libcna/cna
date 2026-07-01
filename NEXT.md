# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 32 — Texture2D completeness (Tasks 261–270) in progress
  (Tasks 261, 262, 263, 264, 266 done). Phase 31 (Tasks 251–260) core work complete; only Task
  260 (optional perf work) remains open. Phase 30 (VertexDeclaration / vertex format accuracy,
  Tasks 241–250) is complete.
- **Key architectural decisions:**
  - Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`
    (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). EasyGL is primary (most tested).
  - The `sharp-runtime` library (sibling repo at `../sharp-runtime/`) provides all `System.*` types
    and primitive aliases (`bytecs`, `String`, etc.) used on the XNA API surface.
  - Stride-keyed vertex layout: backends currently build VAO/pipeline/layout from a map keyed
    by vertex stride. Only strides 16/20/24/32/52 work for 3D — arbitrary layouts are future work.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`) — primary
- **Builds:** clean.
- **Unit tests (`CnaTests`):** 1808/1808 pass.
- **Integration tests:** ~62 EasyGL integration test executables registered in CMake; most pass.
  Two pre-existing failures: `easygl_device_dispose_order_test` (root cause unknown) and one
  pixel-readback test related to the SpriteBatch multiple-Begin/End bug (see Known Bugs).

### Vulkan backend (`cmake-build-vulkan`)
- **Builds:** clean (single-threaded `-j1` required for link stability).
- **Vulkan integration tests:** 13 registered; 11/11 historically pass. Latest:
  `vulkan_scissor_test` (Task 329, 4/4 pixel checks).

### Bgfx backend (`cmake-build-bgfx`)
- Builds when configured. `Bgfx_VertexFormatMapping` test (Task 249): 47/47 mapping checks pass.
  No pixel-readback for Bgfx (no readback API).

### Recently implemented (Phase 31, Tasks 251–259, 329)
- **Task 251** — `DrawUserPrimitives` FNA audit: fixed silent-return bug in 4 typed overloads;
  added VertexDeclaration overload; exposed `PrimitiveVerts()` as public NOXNA static; 12/12 unit tests.
- **Task 252** — `DrawUserIndexedPrimitives` FNA audit: fixed silent-return bug in all 8 typed
  overloads (VPC/VPT/VPCT/VPNT × 16-bit/32-bit); added `primitiveCount` validation; added 2 new
  VertexDeclaration overloads (16-bit + 32-bit) matching FNA's second generic overloads; 15/15 unit tests.
- **Task 255** — `DrawUserPrimitives<VertexPositionColor>` pixel-readback: EasyGL integration test;
  vertexOffset=0 and vertexOffset=1; centre=(255,0,0) 2/2 PASS.
- **Task 256** — `DrawUserPrimitives` custom `VertexDeclaration` pixel-readback: custom 16-byte
  `MyVertex` struct; vertexOffset=0 and vertexOffset=1; centre=(255,0,0) 2/2 PASS.
- **Task 257** — `DrawUserIndexedPrimitives<VertexPositionColor>` (16-bit indices) pixel-readback:
  EasyGL integration test; vertexOffset=0/indexOffset=0 and vertexOffset=1/indexOffset=1 sub-tests;
  centre=(255,0,0) 2/2 PASS.
- **Task 258** — `DrawUserIndexedPrimitives<VertexPositionColor>` (32-bit indices) pixel-readback:
  EasyGL integration test; vertexOffset=0/indexOffset=0 and vertexOffset=1/indexOffset=1 sub-tests;
  centre=(255,0,0) 2/2 PASS.
- **Task 259** — `DrawUserPrimitives` argument-guard tests: `primitiveCount <= 0` throws
  `System::ArgumentOutOfRangeException` for all 5 overloads (VPC/VPT/VPCT/VPNT + VertexDeclaration),
  zero and negative counts; 10/10 new unit tests.
- **Task 329** — Vulkan scissor test: `ScissorTestEnable` + `ScissorRectangle` pixel readback; 4/4 PASS.
- **Task 261** (Phase 32 start) — Full `Texture2D` audit vs FNA (audit only, zero code changes).
  Found 2 confirmed memory-safety bugs (fixed in Task 266) plus: 2 constructors missing `NOXNA`
  tags; a missing `FromStream(w,h,zoom)` overload (added in Task 262); missing
  `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`;
  `SurfaceFormat` support is effectively Color-only (`ValidateFormat` throws for everything else).
  Full writeup in `AUDIT.md` under "Texture2D detailed audit (Task 261, Phase 32)".
- **Task 266** — Fixed both OOB bugs found in Task 261: (1) `SetData(int level, const Rectangle*,
  ...)` now throws `std::out_of_range` when `rect` exceeds the mip level's bounds, mirroring
  `GetData`'s existing check (was a heap buffer **overflow write**). (2)
  `SetData(const Color*, int elementCount)` now throws `std::out_of_range` when
  `elementCount < width*height` instead of building a size-mismatched `ImageData` (was a heap
  buffer **overflow read** in the EasyGL backend). 7 new unit tests (5 rect-bounds +
  2 buffer-size).
- **Task 262** — Verified `Texture2D::FromStream` decodes PNG (lossless), JPEG (lossy,
  tolerance-checked), and BMP (hand-built, exact) via round-trip tests; DDS/DXT1/3/5 already
  worked (Task 125). Documented in `docs/texture-stream-formats.md`. Added the missing
  `FromStream(GraphicsDevice&, Stream&, int width, int height, bool zoom)` overload, matching
  FNA3D's resize (fit, preserves aspect ratio) / crop (zoom, exact target size) semantics via
  `SDL_CreateSurfaceFrom` + `SDL_BlitSurfaceScaled`. 5 new unit tests.
- **Task 263** — Verified `Texture2D::SaveAsPng`: null-stream/no-CPU-pixels error guards; a
  multi-pixel round-trip with a semi-transparent pixel (exact match on all 4 channels for all 4
  pixels — catches spatial/transposition bugs a single-solid-colour test would miss); non-square
  size (3x5); the save-time resize path (`SaveAsPng(stream, targetW, targetH)` scaling the
  output); and the filename-based `NOXNA` overload via a real temp file. 6 new unit tests.
- **Task 264** — Verified `Texture2D::SaveAsJpeg`: same coverage as Task 263 but with a colour
  tolerance (JPEG is lossy) instead of exact match, plus a dedicated test confirming a
  semi-transparent source decodes back fully opaque (JPEG has no alpha channel). **Also fixed**
  the Task 261 audit finding that `SaveAsJpeg` hardcoded quality=100: added a
  `GetJpegSaveQuality()` helper that reads `FNA_GRAPHICS_JPEG_SAVE_QUALITY`, matching FNA exactly.
  8 new unit tests; 1808/1808 total pass.
- **Phase 30** — Full VertexDeclaration test suite, EasyGL vertex format integration test (strides
  16/20/24/32), Bgfx vertex layout mapping helper, `docs/vertex-format-support.md`.

### What does NOT work yet
- `Texture2D`: missing `NOXNA` tags on 2 assetName constructors, missing
  `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`, and
  Color-only format support — all found in Task 261, still open (see `AUDIT.md`).
- `DrawUserIndexedPrimitives` argument-guard unit tests for `primitiveCount <= 0` not covered (only
  `DrawUserPrimitives` was in scope for Task 259).
- Multiple `SpriteBatch::Begin()/End()` per frame on Vulkan (only last batch renders).
- Bgfx and EasyGL stride-keyed layout: arbitrary `VertexDeclaration` strides beyond 16/20/24/32/52.
- 365 tasks still ⬜ out of 536 in `GRAPHICS_TASKS.md`.

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
| 264 | `Texture2D.cpp`, `Texture2DTests.cpp` (extended), `AUDIT.md`, `GRAPHICS_TASKS.md` | SaveAsJpeg round-trip verification (tolerance-based, alpha-drop confirmed); fixed hardcoded JPEG quality=100 by adding FNA_GRAPHICS_JPEG_SAVE_QUALITY env-var support. 8 new unit tests; 1808/1808 total pass. |
| 263 | `Texture2DTests.cpp` (extended), `GRAPHICS_TASKS.md` | SaveAsPng round-trip verification: error guards, multi-pixel+alpha exact match, non-square size, save-time resize, filename overload via temp file. 6 new unit tests; 1800/1800 total pass. |
| 262 | `Texture2D.hpp/.cpp`, `Texture2DTests.cpp`, `docs/texture-stream-formats.md` (new), `AUDIT.md`, `GRAPHICS_TASKS.md` | Verified PNG/JPEG/BMP FromStream decoding (round-trip tests); added missing FromStream(device,stream,width,height,zoom) overload matching FNA3D resize/crop semantics. 5 new unit tests; 1794/1794 total pass. |
| 266 | `Texture2D.cpp`, `Texture2DTests.cpp`, `AUDIT.md`, `GRAPHICS_TASKS.md` | Fixed both Task 261 OOB bugs: SetData(level,rect,...) rect-bounds check (mirrors GetData); SetData(Color*,elementCount) buffer-size check. 7 new unit tests; 1789/1789 total pass. |
| 261 | `AUDIT.md` (extended), `GRAPHICS_TASKS.md` | Detailed Texture2D vs FNA audit; found 2 OOB memory bugs (SetData rect-bounds write, SetData(Color*,int) size-mismatch read), 2 missing NOXNA tags, 4 missing methods/overloads, Color-only format support. No code changed. |
| 259 | `DrawUserPrimitivesTests.cpp` (extended), `GRAPHICS_TASKS.md` | Added argument-guard tests: primitiveCount<=0 throws ArgumentOutOfRangeException for all 5 DrawUserPrimitives overloads (VPC/VPT/VPCT/VPNT + VD); 10/10 new unit tests; 1782/1782 total pass |
| 258 | `examples/easygl_draw_user_indexed_primitives_32_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` | DrawUserIndexedPrimitives VPC (32-bit indices) pixel-readback; vertexOffset=0/indexOffset=0 + vertexOffset=1/indexOffset=1; centre=(255,0,0) 2/2 PASS; 1772/1772 unit tests still pass |
| 257 | `examples/easygl_draw_user_indexed_primitives_vpc_test.cpp` (new), `CMakeLists.txt`, `GRAPHICS_TASKS.md` | DrawUserIndexedPrimitives VPC (16-bit indices) pixel-readback; vertexOffset=0/indexOffset=0 + vertexOffset=1/indexOffset=1; centre=(255,0,0) 2/2 PASS; 1772/1772 unit tests still pass |
| 256 | `examples/easygl_draw_user_primitives_custom_test.cpp` (new), `CMakeLists.txt` | DrawUserPrimitives custom VD pixel-readback; custom MyVertex + VD overload; offset=0 + offset=1; 2/2 PASS |
| 255 | `examples/easygl_draw_user_primitives_vpc_test.cpp` (new), `CMakeLists.txt` | DrawUserPrimitives VPC pixel-readback; offset=0 + offset=1; centre=(255,0,0) 2/2 PASS |
| 252 | `GraphicsDevice.hpp/.cpp`, `DrawUserIndexedPrimitivesTests.cpp` (new) | Fixed 8 typed overloads silent-return bug; added 2 VD overloads; primitiveCount validation; 15/15 unit tests |
| 251 | `GraphicsDevice.hpp/.cpp`, `DrawUserPrimitivesTests.cpp` (new) | Fixed 4 typed overloads; VD overload; `PrimitiveVerts()` public static; 12/12 tests |
| 329 | `examples/vulkan_scissor_test.cpp`, `CMakeLists.txt` | Vulkan scissor pixel-readback; 4/4 PASS |

---

## 4. Current blocker / main problem

**No single hard blocker.** The 2 memory-safety bugs found by the Task 261 audit are fixed
(Task 266); `FromStream` format support + the missing resize/crop overload are done (Task 262);
`SaveAsPng` and `SaveAsJpeg` are both round-trip verified (Tasks 263–264), and the hardcoded JPEG
quality is fixed. All 1808 unit tests pass.

The remaining Task 261 findings are lower-severity and still open: 2 constructors missing `NOXNA`
tags, missing EXT statics (`SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/
`DDSFromStreamEXT`), and Color-only `SurfaceFormat` support. None of these are memory-unsafe —
they're missing-API gaps — so they can be picked up incrementally per Phase 32 task without
urgency. The `NOXNA` tag fix is the smallest/cheapest remaining item.

---

## 5. Known bugs and limitations

| Status | Issue |
|--------|-------|
| **Confirmed bug** | `SpriteBatch` multiple `Begin()/End()` per frame on Vulkan: only the last batch renders. Workaround: merge all sprite draws into one Begin/End. |
| **Needs verification** | `easygl_device_dispose_order_test` pre-existing failure — root cause unknown. |
| **Incomplete** | `DrawUserIndexedPrimitives` argument-guard unit tests for `primitiveCount <= 0` not covered (Task 259 only covered `DrawUserPrimitives`). |
| **Incomplete** | EasyGL and Bgfx backends: stride-keyed vertex layout supports only strides 16/20/24/32/52; arbitrary `VertexDeclaration` layouts silently use wrong VAO/pipeline. |
| **Incomplete** | Vulkan backend: `Tangent` and `Binormal` `VertexElementUsage` values not mapped (no Vulkan semantic equivalent). |
| **Incomplete** | `VertexElementUsage` Depth/Fog/PointSize/Sample/TessellateFactor: unsupported in all 3D backends; currently no-op or return `bgfx::Attrib::Count`. |
| **Incomplete** | SDL_Renderer backend: `CreateVertexBuffer` always throws `ThrowNo3D`. No 3D support at all. |
| **Incomplete** | Bgfx backend lacks pixel readback — integration tests there are smoke-only. |
| **Incomplete** | `Texture2D`: 2 constructors missing `NOXNA` tags; missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT`; `SurfaceFormat` support is effectively Color-only. Found in Task 261 audit (see `AUDIT.md`), not yet addressed — not memory-unsafe, just missing API surface. |

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

### Critical invariants

- **`NOXNA` macro** tags every non-XNA extension in public headers.
- **C# properties** → `getXProperty()` / `setXProperty()` convention (never public fields for XNA API).
- **Static readonly** → `static const` in `.hpp` + definition in `.cpp`.
- **Type aliases** from `SharpRuntime/SharpRuntimeHelper.hpp` (`bytecs`, `Single`, `String`, …) must
  be used on XNA API surfaces — never raw `uint8_t`, `float`, `std::string` directly.
- **Backend selection** is compile-time: no runtime branch between backends in the same binary.
- **Stride-keyed vertex layout:** EasyGL, Vulkan, and Bgfx currently build VAO/pipeline/layout from
  a map keyed by vertex stride. Only strides 16/20/24/32/52 work correctly for 3D.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */` block.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` required at top of every `.hpp` and `.cpp`.
- **Effect must be applied before any draw call** — all `DrawUser*` overloads now throw
  `std::runtime_error` when `currentEffect_` is null (fixed in Tasks 251–252).

### Data flow (indexed user primitives draw call)

```
Game calls GraphicsDevice::DrawUserIndexedPrimitives(type, vertices, vOffset, numVerts, indices, iOffset, primCount)
  → validates backend_ non-null (silent return if null)
  → validates currentEffect_ non-null (throws if null)
  → validates primCount > 0 (throws ArgumentOutOfRangeException)
  → computes index count via IndexCountForPrimitives(type, primCount)
  → packs vertex data into typed GpuV* struct
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
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/CnaTests --gtest_filter="DrawUserIndexedPrimitives*"

# Build Vulkan (single-threaded to avoid linker races)
cmake --build cmake-build-vulkan --target CNA -j1

# Run a specific EasyGL integration test (headless)
DISPLAY=:0 /rv/data/development/github.com/openeggbert/cna/cmake-build-debug/cna_test_easygl_draw_user_primitives_vpc

# CTest (all registered tests, EasyGL build)
cd cmake-build-debug && ctest --output-on-failure
```

---

## 8. Next smallest tasks

In priority order:

1. **Add missing `NOXNA` tags** (small, mechanical, from the Task 261 audit)
   - Goal: Tag `Texture2D(const std::string& assetName)` and
     `Texture2D(const std::string& assetName, GraphicsDevice&)` with `NOXNA`, matching the
     project's own precedent (`SoundEffect(const std::string&)` is already `NOXNA`).
   - Files: `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp`.
   - Verification: compiles; no behavior change (NOXNA is a marker macro only).

2. **Task 267 — Verify `LevelCount` behavior for mipmapped and non-mipmapped textures**
   - Goal: Unit tests confirming `getLevelCountProperty()` matches FNA's `CalculateMipLevels`
     formula for various width/height combinations, and that the non-mipmapped constructor
     always yields `LevelCount == 1`.
   - Files: `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp`.
   - Verification: new unit tests pass.

3. **Task 260 — Optimize user primitive staging (avoid per-draw heap allocation)**
   - Goal: Replace `std::vector` allocations in `DrawUserPrimitives` / `DrawUserIndexedPrimitives`
     with a per-device scratch buffer or stack allocation for small counts.
   - Files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`.
   - Verification: unit tests still 1808/1808; integration pixel-readback tests still pass.

4. **Task 268/269 — Non-power-of-two textures and edge sampling** (larger, needs backend work)
   - Goal: Verify NPOT textures (e.g. 3x5, 7x11) work across all backends, and that texture
     sampling at edges respects clamp/wrap `TextureAddressMode`. Likely needs pixel-readback
     integration tests, not just unit tests.
   - Files: TBD after scoping; likely `examples/` integration tests.
   - Verification: pixel-readback tests pass on EasyGL (primary backend).

---

## 9. Do not do yet

- **No broad Texture2D rewrite** — the Task 261 audit is complete (see `AUDIT.md`); the 2 OOB
  bugs (Task 266), FromStream format support/resize overload (Task 262), and SaveAsPng/SaveAsJpeg
  verification (Tasks 263–264) are done; remaining findings (missing NOXNA tags, missing EXT
  statics, Color-only format support) should land incrementally per Phase 32 task, not as one
  large refactor.
- **No refactor of the stride-keyed vertex layout system** — it is load-bearing for all 3D tests;
  changes need their own dedicated phase with full regression testing.
- **No changes to the Bgfx backend draw path** — pixel readback is unavailable there, so correctness
  cannot be verified.
- **No SpriteBatch Vulkan multi-batch fix** until the root cause is isolated — a wrong fix could
  break single-batch rendering silently.
- **No API renames or namespace moves** — XNA API names are frozen by spec.
- **No mass Doxygen cleanup passes** — add Doxygen only when touching a file for another reason.
- **No new sharp-runtime types** unless a concrete CNA task requires them.
- **No broad `GetData`/`SetData` rewrite** — wait for Phase 32 audit to determine actual scope.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope beyond the task.

Current status: Phase 30 complete; Phase 31 core work done (Tasks 251,252,255,256,257,258,259,329);
Phase 32 in progress (Tasks 261, 262, 263, 264, 266 done). 1808/1808 unit tests pass.

Tasks 261 (audit), 266 (OOB-bug fixes), 262 (FromStream format verification + resize/crop
overload), 263 (SaveAsPng round-trip verification), and 264 (SaveAsJpeg round-trip verification +
JPEG quality env-var fix) are all closed. Remaining Task 261 findings are missing-API gaps, not
memory-safety bugs (see AUDIT.md "Texture2D detailed audit"):
- 2 constructors missing NOXNA tags (Texture2D(assetName), Texture2D(assetName, device)).
- Missing SetDataPointerEXT/GetDataPointerEXT/TextureDataFromStreamEXT/DDSFromStreamEXT.
- SurfaceFormat support is effectively Color-only (ValidateFormat throws for everything else).

Next: tag Texture2D(const std::string& assetName) and Texture2D(const std::string& assetName,
GraphicsDevice&) with NOXNA, matching the project's own precedent (SoundEffect(const std::string&)
is already NOXNA). Purely mechanical — no behavior change. Then Task 267 (verify LevelCount
behavior) or Task 260 (optional perf work).
Files: include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp.
Verification: compiles; CnaTests still all pass.
Update GRAPHICS_TASKS.md and NEXT.md after finishing.
```
