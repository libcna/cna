# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 32 — Texture2D completeness (Tasks 261–270) has started
  (Task 261 audit done). Phase 31 (Tasks 251–260) core work complete; only Task 260 (optional
  perf work) remains open. Phase 30 (VertexDeclaration / vertex format accuracy, Tasks 241–250)
  is complete.
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
- **Unit tests (`CnaTests`):** 1782/1782 pass.
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
  Found **2 confirmed memory-safety bugs**: (1) `SetData(int level, const Rectangle*, ...)` has no
  bounds check that `rect` fits inside the mip level, unlike the equivalent `GetData` overload —
  an out-of-range `Rectangle` causes a heap buffer **overflow write**; (2) the simple
  `SetData(const Color*, int elementCount)` builds an `ImageData` claiming full texture dimensions
  over an undersized pixel buffer when `elementCount < width*height`, causing an out-of-bounds
  **read** in the EasyGL backend's `set_image_2d` call. Also found: 2 constructors missing `NOXNA`
  tags (`Texture2D(assetName)`, `Texture2D(assetName, device)` — not in the FNA API surface);
  missing `FromStream(w,h,zoom)` overload; missing `SetDataPointerEXT`/`GetDataPointerEXT`/
  `TextureDataFromStreamEXT`/`DDSFromStreamEXT`; `SurfaceFormat` support is effectively Color-only
  (`ValidateFormat` throws for everything else). Full writeup in `AUDIT.md` under
  "Texture2D detailed audit (Task 261, Phase 32)".
- **Phase 30** — Full VertexDeclaration test suite, EasyGL vertex format integration test (strides
  16/20/24/32), Bgfx vertex layout mapping helper, `docs/vertex-format-support.md`.

### What does NOT work yet
- **The 2 memory-safety bugs found in Task 261 are not yet fixed** — audit was scoped to
  documentation only. Recommend a fast-follow fix task before continuing deeper into Phase 32.
- `DrawUserIndexedPrimitives` argument-guard unit tests for `primitiveCount <= 0` not covered (only
  `DrawUserPrimitives` was in scope for Task 259).
- Multiple `SpriteBatch::Begin()/End()` per frame on Vulkan (only last batch renders).
- Bgfx and EasyGL stride-keyed layout: arbitrary `VertexDeclaration` strides beyond 16/20/24/32/52.
- 369 tasks still ⬜ out of 536 in `GRAPHICS_TASKS.md`.

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
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

**Two confirmed memory-safety bugs**, found by the Task 261 audit but deliberately left unfixed
(the task was scoped to audit only). Both are reachable from the public `Texture2D` API:

1. `Texture2D::SetData(int level, const Rectangle* rect, ...)` has no bounds check that `rect`
   fits inside the mip level — an out-of-range rect causes a **heap buffer overflow write**.
2. `Texture2D::SetData(const Color* data, int elementCount)` with `elementCount < width*height`
   causes a **heap buffer overflow read** in the EasyGL backend.

Recommend fixing both **before** proceeding deeper into Phase 32 (Tasks 262–270 build on top of
`SetData`/`GetData`, so shipping more Texture2D work on top of known OOB bugs compounds the risk).
See `AUDIT.md` → "Texture2D detailed audit" findings #1–#2 for exact line numbers and repro logic.

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
| **Confirmed bug** | `Texture2D::SetData(int level, const Rectangle* rect, ...)` — no bounds check that `rect` fits inside the mip level; out-of-range rect causes a heap buffer overflow **write**. Found in Task 261 audit, not yet fixed. |
| **Confirmed bug** | `Texture2D::SetData(const Color* data, int elementCount)` with `elementCount < width*height` — heap buffer overflow **read** in the EasyGL backend. Found in Task 261 audit, not yet fixed. |
| **Incomplete** | SDL_Renderer backend: `CreateVertexBuffer` always throws `ThrowNo3D`. No 3D support at all. |
| **Incomplete** | Bgfx backend lacks pixel readback — integration tests there are smoke-only. |

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

1. **Task 266 — Fix the 2 confirmed OOB bugs from the Task 261 audit** (fast-follow; this is
   exactly the bounds-checking gap Task 266 already anticipated in `GRAPHICS_TASKS.md`)
   - Goal: (a) Add rect-bounds validation to `Texture2D::SetData(int level, const Rectangle*, ...)`
     mirroring the check already present in `GetData` (`Texture2D.cpp:317`) — throw
     `std::out_of_range` when `x < 0 || y < 0 || x + w > levelW || y + h > levelH`.
     (b) In `SetData(const Color* data, int elementCount)`, validate `elementCount >= width*height`
     before building the `ImageData` (throw, matching FNA's `ArgumentOutOfRangeException` for
     insufficient data), or size `img.width`/`img.height` to match the actual data provided.
   - Files: `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`,
     `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` (add regression tests for both).
   - Verification: new unit tests covering out-of-range rects and undersized elementCount both
     throw instead of corrupting memory; `CnaTests` still all pass; run under ASan/valgrind if
     available to confirm the OOB is gone.

2. **Task 262 — Verify `Texture2D::FromStream` supports PNG/JPG/BMP formats**
   - Goal: Document actual supported formats (SDL_image codecs) and add the missing
     `FromStream(GraphicsDevice&, Stream&, int width, int height, bool zoom)` overload found
     missing in the Task 261 audit.
   - Files: `Texture2D.hpp/.cpp`.
   - Verification: unit/integration test round-tripping a resized load.

3. **Task 260 — Optimize user primitive staging (avoid per-draw heap allocation)**
   - Goal: Replace `std::vector` allocations in `DrawUserPrimitives` / `DrawUserIndexedPrimitives`
     with a per-device scratch buffer or stack allocation for small counts.
   - Files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`.
   - Verification: unit tests still 1782/1782; integration pixel-readback tests still pass.

---

## 9. Do not do yet

- **No broad Texture2D rewrite** — the Task 261 audit is complete and scoped the work (see
  `AUDIT.md`), but fixes should land incrementally per Phase 32 task (266 first — the 2 OOB bugs),
  not as one large refactor.
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
Phase 32 started (Task 261 audit done). 1782/1782 unit tests pass — no code changed by Task 261.

Task 261 found 2 CONFIRMED memory-safety bugs in Texture2D::SetData (see AUDIT.md "Texture2D
detailed audit"): (1) SetData(level, rect, ...) has no bounds check that rect fits the mip level
(GetData has this check, SetData doesn't) — OOB heap write. (2) SetData(Color*, elementCount) with
elementCount < width*height builds an ImageData with mismatched dimensions vs buffer size — OOB
heap read in the EasyGL backend's set_image_2d call.

Next: Task 266 — fix both OOB bugs before continuing Phase 32.
(a) Add the missing rect-bounds check to SetData(int level, const Rectangle*, const Color*, int, int)
    mirroring GetData's existing check at Texture2D.cpp:317: throw std::out_of_range when
    x<0 || y<0 || x+w>levelW || y+h>levelH.
(b) In SetData(const Color* data, int elementCount), validate elementCount >= width*height before
    building the ImageData; throw std::out_of_range (matching the level-based overload's exception
    type) if insufficient.
Files: src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp,
       tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp (add regression tests for both).
Verification: new unit tests for out-of-range rect and undersized elementCount both throw;
CnaTests still 1782+/1782+ pass.
Update GRAPHICS_TASKS.md (mark 266 ✅) and NEXT.md after finishing.
```
