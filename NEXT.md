# NEXT.md — CNA Project Handoff

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`),
built on SDL3 with a pluggable 3D graphics backend layer (EasyGL/OpenGL ES 3.2, Vulkan, Bgfx,
SDL_Renderer). It is a framework/runtime — not a game — designed so that XNA/FNA game code can be
ported to C++ with minimal API-surface changes.

- **Main goal:** Full XNA 4.0 API coverage with pixel-accurate behavior, backed by unit and
  pixel-readback integration tests.
- **Current development phase:** Phase 31 — User primitives and draw-call variants (Tasks 251–260).
  Phase 30 (VertexDeclaration / vertex format accuracy, Tasks 241–250) is complete.
- **Key architectural decision:** Backend selection is compile-time via `CNA_GRAPHICS_BACKEND`
  (`EASYGL` | `VULKAN` | `BGFX` | `SDL_RENDERER`). The EasyGL backend is primary (most tested).
  The `sharp-runtime` library (sibling repo) provides all `System.*` types and primitive aliases
  (`bytecs`, `String`, etc.) that the XNA API surface depends on.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`) — primary
- **Builds:** clean.
- **Unit tests (`CnaTests`):** 1757/1757 pass.
- **Integration tests:** ~60 EasyGL integration test executables registered in CMake; most pass.
  Two pre-existing failures: `easygl_device_dispose_order_test` and one pixel-readback test
  related to the SpriteBatch multiple-Begin/End bug (see Known Bugs).

### Vulkan backend (`cmake-build-vulkan`)
- **Builds:** clean (single-threaded `-j1` required for link stability).
- **Vulkan integration tests:** 13 registered; 11/11 historically pass. Latest additions:
  `vulkan_scissor_test` (Task 329, 4/4 pixel checks).

### Bgfx backend (`cmake-build-bgfx`)
- Builds when configured. `Bgfx_VertexFormatMapping` test (Task 249): 47/47 mapping checks pass.
  No pixel-readback for Bgfx (no readback API).

### Recently implemented (Phases 30–31, Tasks 241–252, 329)
- Full VertexDeclaration test suite (compact formats, multi-channel UV, unusual offsets, tangent/binormal).
- EasyGL vertex format integration test covering strides 16/20/24/32 with pixel readback.
- Bgfx vertex layout mapping helper (`BgfxVertexFormatHelper.hpp`) for all 12 VEF + 13 VEU values.
- `docs/vertex-format-support.md` — per-backend table for all formats and usages.
- Vulkan scissor test (`vulkan_scissor_test.cpp`) — ScissorTestEnable + ScissorRectangle pixel readback.
- `DrawUserPrimitives` audit (Task 251): fixed silent-return bug in 4 typed overloads (now throw on
  missing effect); added VertexDeclaration-based overload matching FNA's second generic overload;
  exposed `GraphicsDevice::PrimitiveVerts()` as public NOXNA static; 12/12 unit tests.
- `DrawUserIndexedPrimitives` audit (Task 252): fixed silent-return bug in all 8 typed overloads (now
  throw on missing effect + validate primitiveCount); added VertexDeclaration overloads for 16-bit and
  32-bit indices matching FNA's second generic overloads; 15/15 unit tests.
- `DrawUserPrimitives` VPC pixel-readback (Task 255): EasyGL integration test — full-NDC red quad via
  typed VPC overload; tests vertexOffset=0 and vertexOffset=1; centre=(255,0,0) 2/2 PASS.

### What does NOT work yet
- Multiple `SpriteBatch::Begin()/End()` per frame on Vulkan (only last batch renders).
- `DrawUserIndexedPrimitives` typed overloads have not been audited against FNA (Task 252).
- No pixel-readback tests for `DrawUserPrimitives` (Tasks 255–256 pending).
- No pixel-readback tests for `DrawUserIndexedPrimitives` (Tasks 257–258 pending).
- Bgfx backend lacks pixel readback — integration tests there are smoke-only.
- 376 tasks still ⬜ out of 539 in `GRAPHICS_TASKS.md`.

---

## 3. Recent changes

| Task | Files | Change |
|------|-------|--------|
| 255 | `examples/easygl_draw_user_primitives_vpc_test.cpp` (new), `CMakeLists.txt` | DrawUserPrimitives VPC pixel-readback; offset=0 + offset=1; centre=(255,0,0) 2/2 PASS |
| 252 | `GraphicsDevice.hpp/.cpp`, `DrawUserIndexedPrimitivesTests.cpp` (new) | Fixed 8 typed overloads silent-return bug; added VertexDeclaration+16-bit and VertexDeclaration+32-bit overloads; primitiveCount validation; 15/15 tests |
| 251 | `GraphicsDevice.hpp/.cpp`, `DrawUserPrimitivesTests.cpp` | Fixed 4 typed overloads to throw on missing effect; added VertexDeclaration overload; exposed `PrimitiveVerts()` public static; 12/12 tests |
| 329 | `examples/vulkan_scissor_test.cpp`, `CMakeLists.txt` | 2-frame scissor pixel-readback test (no scissor → both quadrants red; scissor top-left 32×32 → inside red, outside green) |
| 250 | `docs/vertex-format-support.md` | Per-backend tables for all 12 VEF + 13 VEU values; stride-keyed layout limitation documented |
| 249 | `include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp`, `examples/bgfx_vertex_format_test.cpp` | Bgfx mapping helper + 47-check mapping test + 4 VertexBuffer smoke tests |
| 247 | `examples/easygl_vertex_formats_test.cpp` | EasyGL strides 16/20/24/32 pixel-readback test |
| 241–246 | `tests/Microsoft/Xna/Framework/Graphics/VertexDeclarationTests.cpp` | Vertex declaration audit: compact format, multi-channel UV, unusual offset, tangent/binormal, Equals/Hash/ToString |

---

## 4. Current blocker / main problem

**No single hard blocker** at this moment. The project is healthy and builds cleanly.

**No single hard blocker** at this moment. The project is healthy and builds cleanly.

**No single hard blocker.** The project is healthy and builds cleanly.

The next natural task is Task 256: pixel-readback test for `DrawUserPrimitives` using the
explicit `VertexDeclaration` overload (raw vertex data + custom struct). This exercises the
same backend path as Task 255 but via the new VD overload added in Task 251.

---

## 5. Known bugs and limitations

| Status | Issue |
|--------|-------|
| **Confirmed bug** | `SpriteBatch` multiple `Begin()/End()` per frame on Vulkan: only the last batch renders. Workaround: merge all sprite draws into one Begin/End. |
| **Fixed** | `DrawUserIndexedPrimitives` typed overloads (8 overloads) silent-return bug — fixed in Task 252; all now throw on missing effect. |
| **Incomplete** | `DrawUserPrimitives` and `DrawUserIndexedPrimitives`: no pixel-readback integration tests yet (Tasks 255–258). |
| **Incomplete** | Bgfx backend: stride-keyed layout only covers strides 16/20/24/32/52; arbitrary VertexDeclaration layouts not supported. |
| **Incomplete** | EasyGL backend: same stride-keyed limitation as Bgfx. |
| **Incomplete** | Vulkan backend: Tangent and Binormal `VertexElementUsage` values are not mapped (no Vulkan semantic equivalent). |
| **Incomplete** | VertexElementUsage Depth/Fog/PointSize/Sample/TessellateFactor: unsupported in all 3D backends; currently return `bgfx::Attrib::Count` / no-op. |
| **Incomplete** | Texture2D completeness audit not started (Phase 32, Tasks 261–270). |
| **Needs verification** | `easygl_device_dispose_order_test` pre-existing failure — root cause unknown. |
| **Incomplete** | SDL_Renderer backend: `CreateVertexBuffer` always throws `ThrowNo3D`. No 3D support at all. |

---

## 6. Architecture notes

### Main modules

| Layer | Location | Notes |
|-------|----------|-------|
| XNA public API | `include/Microsoft/Xna/Framework/…` | Must match XNA 4.0 / FNA exactly |
| Backend contracts | `include/CNA/Internal/Backends/Common/` | `IGraphicsBackend`, `IVertexBuffer`, etc. |
| EasyGL backend | `src/CNA/Internal/Backends/EasyGL/` | Primary; OpenGL ES 3.2 via EasyGL wrapper |
| Vulkan backend | `src/CNA/Internal/Backends/Vulkan/` | Uses VulkanVertexFormatHelper.hpp for per-format mapping |
| Bgfx backend | `src/CNA/Internal/Backends/Bgfx/` | Uses BgfxVertexFormatHelper.hpp; no readback |
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
  a map keyed by vertex stride. This means only strides 16/20/24/32/52 work correctly for 3D.
  Arbitrary `VertexDeclaration` layouts are a future task.
- **Doxygen required** on every public `.hpp` member: full `/** @brief … @param … @return */` block.
- **SPDX header** `// SPDX-License-Identifier: MS-PL` required at top of every `.hpp` and `.cpp`.

### Data flow (3D draw call)

```
Game calls GraphicsDevice::DrawUserPrimitives(...)
  → validates effect applied
  → calls VertexCountForUserPrimitives() / PrimitiveVerts()
  → copies vertex data into transient IVertexBuffer via backend_->CreateVertexBuffer()
  → calls backend_->DrawPrimitivesEx(*vb, world, view, proj, type, count, gpuParams)
  → backend maps VertexDeclaration stride → VAO/pipeline/layout
  → GPU draw
```

### FNA reference

The authoritative behavioral reference is at `/rv/data/library/github.com/FNA-XNA/FNA/src`.
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

# Build all tests (EasyGL)
cmake --build cmake-build-debug --target CnaTests -j$(nproc)

# Run unit tests
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/CnaTests

# Run specific test suite
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/CnaTests --gtest_filter="PrimitiveVertsTest.*"

# Build Vulkan (single-threaded to avoid linker races)
cmake --build cmake-build-vulkan --target CNA -j1

# Run a specific integration test (headless, EasyGL)
/rv/data/development/github.com/openeggbert/cna/cmake-build-debug/cna_test_easygl_vertex_formats

# CTest (all registered tests, EasyGL build)
cd cmake-build-debug && ctest --output-on-failure
```

---

## 8. Next smallest tasks

In priority order:

1. **Task 252 — Audit `DrawUserIndexedPrimitives` overloads against FNA**
   - Goal: same audit as Task 251 — check all 8 typed overloads for the silent-return bug; check
     whether a VertexDeclaration + raw-pointer overload is missing; fix and add unit tests.
   - Files: `include/Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp`,
     `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`,
     `tests/Microsoft/Xna/Framework/Graphics/DrawUserIndexedPrimitivesTests.cpp` (new).
   - Verification: `cmake --build cmake-build-debug --target CnaTests -j$(nproc)` → all pass.

2. **Task 255 — Pixel-readback test for `DrawUserPrimitives` with `VertexPositionColor`**
   - Goal: EasyGL integration test: draw a colored triangle via `DrawUserPrimitives<VertexPositionColor>`,
     read back a pixel from the centre, assert color matches.
   - Files: `examples/easygl_draw_user_primitives_vpc_test.cpp` (new), `CMakeLists.txt`.
   - Verification: build + run test executable → exit 0.

3. **Task 256 — Pixel-readback test for `DrawUserPrimitives` with custom `VertexDeclaration`**
   - Goal: same as Task 255 but using the new VertexDeclaration-based overload with a custom struct.
   - Files: `examples/easygl_draw_user_primitives_custom_test.cpp` (new), `CMakeLists.txt`.
   - Verification: build + run test executable → exit 0.

4. **Task 259 — Validate user primitive arrays for null / invalid offsets / invalid count**
   - Goal: unit tests verifying that `DrawUserPrimitives` throws `std::invalid_argument` or
     `System::ArgumentOutOfRangeException` for null data, negative offset, negative count.
   - Files: `tests/Microsoft/Xna/Framework/Graphics/DrawUserPrimitivesTests.cpp` (extend).
   - Verification: `CnaTests --gtest_filter="DrawUserPrimitivesValidation.*"`.

5. **Task 261 — Audit every `Texture2D` constructor and method against FNA** (Phase 32 start)
   - Goal: compare `Texture2D.hpp/.cpp` with FNA's `Texture2D.cs`; document missing overloads,
     wrong signatures, missing bounds checks.
   - Files: `include/Microsoft/Xna/Framework/Graphics/Texture2D.hpp`,
     `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp`.
   - Verification: compile + produce a written audit list.

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

Current status: Phase 30 complete (Tasks 241–250); Phase 31 in progress (Tasks 251, 252, 255 done);
Task 329 complete; 1772/1772 unit tests pass.

Next: Task 256 — pixel-readback test for DrawUserPrimitives via explicit VertexDeclaration overload.
Use a custom packed struct + VertexDeclaration; call DrawUserPrimitives(type, data, offset, count, vd).
Files: examples/easygl_draw_user_primitives_custom_test.cpp (new), CMakeLists.txt.
Build: cmake --build cmake-build-debug --target cna_test_easygl_draw_user_primitives_custom
Update GRAPHICS_TASKS.md (mark 256 ✅) and NEXT.md after finishing.
```
