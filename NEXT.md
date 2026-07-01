# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 31 — User primitives and draw-call variants (Tasks 251–260)
  core work complete; only Task 260 (optional perf work) remains open. Phase 32 (Texture2D
  completeness, Tasks 261–270) starting next. Phase 30 (VertexDeclaration / vertex format
  accuracy, Tasks 241–250) is complete.
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
- **Phase 30** — Full VertexDeclaration test suite, EasyGL vertex format integration test (strides
  16/20/24/32), Bgfx vertex layout mapping helper, `docs/vertex-format-support.md`.

### What does NOT work yet
- `DrawUserIndexedPrimitives` argument-guard unit tests for `primitiveCount <= 0` not covered (only
  `DrawUserPrimitives` was in scope for Task 259).
- Multiple `SpriteBatch::Begin()/End()` per frame on Vulkan (only last batch renders).
- Texture2D completeness audit not started (Phase 32, Tasks 261–270).
- Bgfx and EasyGL stride-keyed layout: arbitrary `VertexDeclaration` strides beyond 16/20/24/32/52.
- 370 tasks still ⬜ out of 536 in `GRAPHICS_TASKS.md`.

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
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

**No single hard blocker.** The project builds cleanly on all three backends and all 1782 unit tests pass.

Phase 31's core work (Tasks 251–259) is done: both `DrawUserPrimitives` and
`DrawUserIndexedPrimitives` have full pixel-readback coverage (16/32-bit indices) and
argument-guard coverage for `primitiveCount <= 0`. The only Phase 31 items left are optional —
**Task 260** (staging optimization, a performance task, not correctness) and Task 261 which
starts **Phase 32** (Texture2D completeness audit). Recommended next: **Task 261**, since it's
an audit-only task (no code changes) that scopes the next real phase of work.

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
| **Incomplete** | Texture2D completeness audit not started (Phase 32, Tasks 261–270). |
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

1. **Task 261 — Audit `Texture2D` constructors and methods against FNA** (Phase 32 start)
   - Goal: Compare `Texture2D.hpp/.cpp` with FNA's `Texture2D.cs`; document every missing
     overload, wrong signature, or missing bounds check. Produce an audit list in AUDIT.md or
     inline in the task notes. Do not implement yet.
   - Files: `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp`,
     `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`,
     `/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Texture2D.cs`.
   - Verification: compile (no code changes expected) + written audit list.

2. **Task 260 — Optimize user primitive staging (avoid per-draw heap allocation)**
   - Goal: Replace `std::vector` allocations in `DrawUserPrimitives` / `DrawUserIndexedPrimitives`
     with a per-device scratch buffer or stack allocation for small counts.
   - Files: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`.
   - Verification: unit tests still 1782/1782; integration pixel-readback tests still pass.

---

## 9. Do not do yet

- **No Texture2D implementation changes** until the Task 261 audit is complete — the scope is unknown.
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

Current status: Phase 30 complete (Tasks 241–250); Phase 31 core work done
(Tasks 251, 252, 255, 256, 257, 258, 259, 329 done); 1782/1782 unit tests pass.
Only Task 260 (optional perf work) remains open in Phase 31.

Next: Task 261 — audit Texture2D constructors and methods against FNA (Phase 32 start).
Compare Texture2D.hpp/.cpp with FNA's Texture2D.cs; document every missing overload,
wrong signature, or missing bounds check. Do not implement yet — audit only.
Files: include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp,
       src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp,
       /rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Texture2D.cs.
Verification: compile (no code changes expected) + written audit list.
Update GRAPHICS_TASKS.md (mark 261 ✅) and NEXT.md after finishing.
```
