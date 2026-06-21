# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering
to one of four backends: SDL\_Renderer, EasyGL (OpenGL ES 3.2), Vulkan, or Bgfx.

**Current phase**: Phases 1–20 substantially done (Tasks 1–170 ✅).
Active work is Phase 21 — texture data conformance (Tasks 171–176).

**Key architectural decisions**:
- Backend selected at **compile time** via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the sole contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — vtable pointer precedes
  the packed pixel; never cast `Color*` to `uint8_t*` for GL pixel I/O.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases and `System.*` stubs.
  **DO NOT touch sharp-runtime** — a separate agent owns it.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference; all logic is ported line-by-line from there.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`) — primary backend
- **Builds**: clean.
- **Tests**: 20/20 EasyGL integration tests pass; ~1500 unit tests pass.
- Recently confirmed working:
  - SpriteBatch all overloads, SpriteEffects flip, transformMatrix translation
  - Texture2D partial-rect SetData/GetData (Task 169)
  - Texture2D startIndex/elementCount SetData/GetData (Task 170)
  - getMipBuffer(0) correctly pre-sizes the buffer after MaybeFreeCpuPixels

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- MSAA 4× (subpass resolve), per-slot SamplerState, custom SPIR-V Effect, all stock
  effects, FillMode::WireFrame — all confirmed via integration tests.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean.
- EnvironmentMapEffect and ShaderEffect not implemented (falls back / returns nullptr).
- GetBackBufferData via requestScreenShot not integration-tested.

### What does not work yet
- **Framework.Net** — 0 % (NetworkSession, PacketReader/Writer entirely absent).
- **Content pipeline (.xnb)** — 0 % (ContentManager uses custom JSON/PNG/OGG only).
- **GamerServices** — ~5 % (stubs only).
- **sRGB SurfaceFormats** — silently map to wrong GL/Vulkan internal format.
- **Mip-level SetData/GetData round-trip tests** — not yet written (Task 171).
- **TextureCube/Texture3D per-level round-trip tests** — not yet written (172–173).

---

## 3. Recent changes

| Commit | What changed |
|---|---|
| `0fd7c88` | Task 170: Texture2D startIndex/elementCount tests; fixed wrong guards in SetData and GetData (`startIndex + elementCount > w * h` → `elementCount < w * h`); getMipBuffer auto-size uses mipDim |
| `1863722` | Task 169: Texture2D partial-rect round-trip integration test; fixed getMipBuffer(0) not pre-sizing buffer after MaybeFreeCpuPixels |
| `4f00b16` | Task 168: SpriteBatch::Begin transformMatrix pixel test; fixed EasyGL FlushBatch matrix multiply order (`orthoM * transform_` → `transform_ * orthoM`) |
| `8e146e6` | Task 167: SpriteEffects FlipH/FlipV pixel integration test (400×100 viewport, PointClamp) |
| `04f0692` | Tasks 151–160, 166: SpriteBatch 6 Draw stubs + 3 DrawString(StringBuilder) stubs replaced; Begin/End guards fixed |

**Files added:**
- `examples/easygl_texture2d_partial_rect_test.cpp` (Tasks 169+170)
- `examples/easygl_transform_matrix_test.cpp` (Task 168)
- `examples/easygl_sprite_effects_test.cpp` (Task 167)
- `docs/coverage.md` — XNA 4.0 coverage report (2026-06-21)

**Files modified:**
- `src/Microsoft/Xna/Framework/Graphics/Texture2D.cpp` — getMipBuffer fix, guard fixes
- `src/Microsoft/Xna/Framework/Graphics/SpriteBatch.cpp` — stub removals, guard fixes
- `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` — matrix order fix
- `tests/Microsoft/Xna/Framework/Graphics/Texture2DTests.cpp` — 2 tests updated/added
- `CMakeLists.txt` — 3 new integration test targets

---

## 4. Current blocker / main problem

**No active blocker.** All builds clean; 20/20 EasyGL integration tests pass.

The next tasks (171–173) are round-trip tests for mip-level and TextureCube/Texture3D
data upload. These follow the same pattern as 169–170 and are not expected to reveal
new blockers, but mip-level upload in the EasyGL backend (`UpdatePixelsLevel`) has
not been exercised by any integration test yet — it may have latent bugs.

---

## 5. Known bugs and limitations

| Status | Item |
|---|---|
| **confirmed bug (fixed)** | `getMipBuffer(0)` left empty after `MaybeFreeCpuPixels`; caused UB on partial-rect SetData |
| **confirmed bug (fixed)** | SetData/GetData guard `startIndex + elementCount > w * h` rejected valid non-zero startIndex |
| **confirmed bug (fixed)** | EasyGL FlushBatch: `orthoM * transform_` applied projection before user transform |
| **incomplete** | `UpdatePixelsLevel` (mip > 0 upload) not covered by any integration test |
| **incomplete** | `TextureCube::GetData` round-trip not integration-tested |
| **incomplete** | `Texture3D::GetData` round-trip not integration-tested |
| **known limit** | EasyGL `FillMode::WireFrame` — no `glPolygonMode` on GLES3 |
| **confirmed** | `Color` has vtable pointer — never cast `Color*` to `uint8_t*` for pixel I/O |
| **unverified** | Bgfx `GetBackBufferData` via `requestScreenShot` — not integration-tested |
| **incomplete** | sRGB SurfaceFormats silently map to linear GL/Vulkan internal formats |
| **0 %** | Framework.Net (NetworkSession, PacketReader/Writer) — no headers, no stubs |
| **0 %** | XNA binary `.xnb` content pipeline |
| **~5 %** | GamerServices (Guide.Show no-op only) |

---

## 6. Architecture notes

### Module map

```
Microsoft::Xna::Framework::*            ← XNA public API (include/ + src/)
  └─ GraphicsDevice                     ← delegates to IGraphicsBackend*
       └─ IGraphicsBackend              ← include/CNA/Internal/Backends/Common/
            ├─ EasyGLGraphicsBackend    ← src/CNA/Internal/Backends/EasyGL/
            ├─ VulkanGraphicsBackend    ← src/CNA/Internal/Backends/Vulkan/
            ├─ BgfxGraphicsBackend      ← src/CNA/Internal/Backends/Bgfx/
            └─ SDLGraphicsBackend       ← src/CNA/Internal/Backends/SDL/

SharpRuntime    ← /rv/data/development/github.com/openeggbert/sharp-runtime
metagl          ← raw GL function loader + typed enum wrappers
easygl          ← GL resource wrappers (Device, Texture, Framebuffer, Sampler, …)
```

### Texture CPU-side shadow copy invariant

`Texture2D` maintains a CPU-side shadow in `cpuPixels_` (shared_ptr).
After any constructor or 2-arg `SetData`, `MaybeFreeCpuPixels()` is called:
- if `contextRecoveryEnabled_ == false` (default) → `cpuPixels_` is freed.
- The 5-arg `SetData(level, rect, data, start, count)` does NOT call
  `MaybeFreeCpuPixels`, so the shadow survives across chained calls.
- `getMipBuffer(0)` now auto-sizes to `mipDim(width,0) * mipDim(height,0) * 4`
  when the buffer is empty, so a re-created buffer is always safe to write into.

### EasyGL sprite batch matrix convention

`FlushBatch` computes: `combined = transform_ * orthoM` (XNA row-major order).
`transform_` is the user matrix from `SpriteBatch::Begin`; `orthoM` is the
screen-space orthographic projection. Applying `orthoM` first was a bug (fixed).

### Vulkan pipeline key encoding

All 3D pipeline creation functions encode `drawMsaa` as the last bool argument.
The MSAA render pass (`renderPassMsaa_`) uses 3 attachments and 3 clear values;
the non-MSAA path uses 2. The active render pass is selected per-frame in
`RecordCommandBuffer`.

### Critical invariants

- **`Color` has a vtable pointer** — use `uint8_t[]` + `Color(r,g,b,a)` for pixel I/O.
- **Vulkan build: `-j1`** — race condition in SPIR-V header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.
- **FNA is authoritative** — do not deviate from FNA logic without a `//` comment.

---

## 7. Useful commands

```bash
# EasyGL — configure + build + all integration tests
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

# EasyGL — unit tests only
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug --exclude-regex EasyGL --output-on-failure

# Vulkan — build (use -j1 to avoid SPIR-V race)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan -j1
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-vulkan -R Vulkan --output-on-failure

# Bgfx — build + smoke test
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d
cd cmake-build-bgfx && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Run one specific integration test
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_texture2d_partial_rect

# Bgfx — recompile 3D shaders (run from repo root)
python3 src/CNA/Internal/Backends/Bgfx/shaders/compile_shaders.py \
    cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc \
    cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src
```

---

## 8. Next smallest tasks

All following the same pattern: EasyGL integration test in `examples/`, registered
in `CMakeLists.txt`, GRAPHICS\_TASKS.md marked ✅, NEXT.md updated.

### Task 171 — Texture2D mip-level SetData/GetData
**Goal**: Verify that mip level 1 of a 4×4 mipMap texture can be written and read
back independently of mip 0.

**Design**:
- `Texture2D tex(dev, 4, 4, /*mipMap=*/true, SurfaceFormat::Color)` → 3 mip levels.
- `SetData(0, nullptr, red16, 0, 16)` → mip 0 all Red.
- `SetData(1, nullptr, blue4, 0, 4)` → mip 1 (2×2) all Blue.
- `GetData(0, nullptr, rb0, 0, 16)` → verify 16 Red.
- `GetData(1, nullptr, rb1, 0, 4)` → verify 4 Blue.

**Files**: `examples/easygl_texture2d_mip_test.cpp`, `CMakeLists.txt`,
`GRAPHICS_TASKS.md`.

**Verification**: `DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_texture2d_mip`

### Task 172 — TextureCube per-face per-mip SetData/GetData
**Goal**: Write distinct colours to each of the 6 cube faces (mip 0) and read back.

**Design**:
- `TextureCube cube(dev, 4, /*mipMap=*/false, SurfaceFormat::Color)`.
- `SetData(CubeMapFace::PositiveX, 0, nullptr, red16, 0, 16)` etc. — 6 different colours.
- `GetData` each face; verify each returns the correct colour.

**Files**: `examples/easygl_texturecube_faces_test.cpp`, `CMakeLists.txt`,
`GRAPHICS_TASKS.md`.

**Verification**: `DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_texturecube_faces`

### Task 173 — Texture3D z-slice SetData/GetData
**Goal**: Write distinct colours to each z-slice of a 2×2×4 Texture3D; read back.

**Design**:
- `Texture3D tex(dev, 2, 2, 4, false, SurfaceFormat::Color)`.
- `SetData(0, 0, 0, 2, 2, z, z+1, data4, 0, 4)` for each z in 0..3.
- `GetData` and verify per-slice colour.

**Files**: `examples/easygl_texture3d_slices_test.cpp`, `CMakeLists.txt`,
`GRAPHICS_TASKS.md`.

**Verification**: `DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cmake-build-debug/cna_test_easygl_texture3d_slices`

### Task 174 — SurfaceFormat backend mapping table
**Goal**: Document per-format support for EasyGL/Vulkan/Bgfx.

**Files**: New `docs/surface-format-support.md`. Read each backend's
`CreateTexture` / `GetInternalFormat` to enumerate what is mapped, approximated,
or missing.

**Verification**: File exists and is accurate against source.

### Task 175 — DXT1 golden decode test
**Goal**: Unit-test `DxtUtil::DecompressDxt1Block` against known reference values.

**Files**: `tests/Microsoft/Xna/Framework/Graphics/DxtUtilTests.cpp` (new).

**Verification**: `ctest --test-dir cmake-build-debug -R DxtUtil`

---

## 9. Do not do yet

- **Do not touch sharp-runtime** — another agent owns it; any change there will conflict.
- **Do not implement Framework.Net** — out of scope for current phase.
- **Do not add .xnb content pipeline** — custom descriptor format is the current contract.
- **Do not refactor IGraphicsBackend** — changing the interface breaks all 4 backends at once.
- **Do not change the `Color` memory layout** — packed ABGR order is relied on by all backends.
- **Do not convert integration tests to unit tests without a mock device** — there is no
  fake GraphicsDevice; integration tests using `Game` + EasyGL are the established pattern.
- **Do not work on Tasks 177–200** until 171–176 are done — the texture conformance
  track should be completed before starting RenderTarget/Effect tracks.
- **Do not start Tasks 201–500** (deep conformance, golden-image, FNA harness) until
  Phase 21 (Tasks 171–176) is fully complete.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: Tasks 1–170 complete. Next unstarted: Task 171 (Texture2D mip-level
SetData/GetData round-trip integration test). Follow the same pattern as Task 169–170:
create examples/easygl_texture2d_mip_test.cpp, register in CMakeLists.txt, build,
run under DISPLAY=:0 SDL_VIDEODRIVER=x11, verify PASS, update GRAPHICS_TASKS.md and
NEXT.md, commit and push.

DO NOT touch sharp-runtime — another agent is working on it.
```
