# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (namespace
`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend layer.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering to
one of four backends: SDL_Renderer, EasyGL (OpenGL ES 3.2 via easygl + metagl),
Vulkan, or Bgfx.

**Current phase**: Phase 7 of GRAPHICS_TASKS.md — integration tests.  
Tasks 85–87 (EasyGL pixel-readback and RenderTarget2D tests) are complete.
Task 88 (Vulkan smoke test) is partially done: the binary builds and the `--smoke`
flag was added to `demo_2d`, but the smoke run was not yet executed.
Task 89 (Bgfx smoke test) is blocked by missing pre-compiled Bgfx shaders.

**Key architectural decisions**:
- Backend is selected at compile time via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — it has a vtable pointer
  before the packed pixel data; raw `uint8_t*` casts must use a temporary buffer.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases (`bytecs`, `String`, etc.) and `System.*` stubs.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference.

---

## 2. Current status

### EasyGL backend (`cmake-build-easygl`)
- **Builds**: clean, no errors.
- **Task 85** `EasyGL_House3D_SmokeTest`: ✅ passes (3-frame smoke, ~1.3 s).
- **Task 86** `EasyGL_TexturedQuad_Readback`: ✅ passes — renders a 1×1 red texture
  full-screen, reads back centre pixel with `GetBackBufferData`, asserts R=255.
- **Task 87** `EasyGL_RenderTarget2D_Readback`: ✅ passes — clears a 64×64 RT to
  green, blits via SpriteBatch, reads back centre pixel, asserts G=255.
- 3D house demo (`cna_house3d_demo`) runs interactively.

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean after fixing `ApplyDepthStencilState` signature mismatch this session.
- `cna_demo_2d` binary exists and the `--smoke 3` flag was added this session.
- Smoke run was **not yet executed** — interrupted before completion.
- CTest entry `Vulkan_Demo2D_SmokeTest` exists in `CMakeLists.txt` but the Vulkan
  build dir has `CNA_BUILD_TESTS=OFF`, so `ctest` cannot pick it up without reconfigure.

### Bgfx backend
- No `cmake-build-bgfx` directory exists.
- Bgfx is fetched via FetchContent (large download from github.com/bkaradzic/bgfx.cmake).
- Task 65 is ⚠️: draw call submission is wired but is a silent no-op because
  `colored3DProgram_` requires pre-compiled bgfx shader binaries not present in the repo.
- `GetBackBufferData` stubs throw with a clear message (async bgfx readback not implemented).

### What does not work yet
- Vulkan textured/lit 3D pipeline (tasks 52–64, deferred).
- Bgfx actual rendering (pre-compiled shaders missing, tasks 65–67 partial).
- `Texture3D`/`TextureCube` `GetData` on GLES3 (no `glGetTexImage`).
- Unit tests (Google Test suite, tasks 8–36) not run in CI yet.

---

## 3. Recent changes

### Files modified this session

| File | Change |
|------|--------|
| `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` | `GetBackBufferData`: use tmp `uint8_t[]` buffer + `Color(r,g,b,a)` to avoid writing into `Color`'s vtable pointer |
| `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` | `ReadBackbuffer`: add `glReadBuffer(Back)` for default FB; use `currentRtHeight_` for Y-flip when RT is bound |
| `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` | Sampler filter fix: `LINEAR_MIPMAP_LINEAR` → `LINEAR` in `ApplySamplerState` (black overlay bug fix from previous session) |
| `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` | `EasyGLRenderTargetBackend::CreateResources`: bind FBO before `attach_texture_2d`; bind texture before `set_image_2d`; set LINEAR min/mag/wrap on `colorTex_` |
| `include/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.hpp` | Added `int currentRtHeight_ = 0` member |
| `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp` | Fixed `ApplyDepthStencilState` override signature (added stencil parameters to match updated base) |
| `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` | Matching implementation update (stencil params ignored with comments) |
| `examples/demo_2d/src/Game1.hpp` | Added `SetSmokeFrames(int n)` + `smokeFramesLeft_` member |
| `examples/demo_2d/src/Game1.cpp` | Added smoke countdown in `Update()` |
| `examples/demo_2d/src/Main.cpp` | Added `--smoke [N]` argument parsing |
| `CMakeLists.txt` | Added `Vulkan_Demo2D_SmokeTest` CTest entry under `VULKAN` + `CNA_BUILD_TESTS` guard |
| `GRAPHICS_TASKS.md` | Marked tasks 86 and 87 as ✅ |

### Files added this session

| File | Purpose |
|------|---------|
| `examples/easygl_textured_quad_test.cpp` | Task 86 integration test |
| `examples/easygl_render_target_test.cpp` | Task 87 integration test |

### Bugs fixed this session

1. **`Color` vtable mis-cast** — `GetBackBufferData` cast `Color*` to `uint8_t*` and
   passed it to `glReadPixels`. Because `Color` has a virtual base (`IPackedVectorT`),
   the vtable pointer occupies the first 8 bytes; `packedValue` starts at byte 8.
   `glReadPixels` wrote 4 bytes starting at byte 0 (into the vtable pointer, not the
   pixel data). Fixed with a temporary `uint8_t[]` buffer.
2. **`glReadBuffer` not called on default FB** — On EGL/GLES3, the initial read buffer
   is not guaranteed to be `GL_BACK`. `ReadBackbuffer` now explicitly calls
   `device.set_read_buffer(Back)` when reading from the default framebuffer.
3. **FBO created but not bound before attachment** — `EasyGLRenderTargetBackend::
   CreateResources` called `glFramebufferTexture2D` without first binding the FBO;
   the call was silently applied to the wrong framebuffer. Added `fbo_.bind()` before
   `attach_texture_2d`.
4. **RT texture not bound before `glTexImage2D`** — The 6-param `Texture::set_image_2d`
   overload does not call `glBindTexture` first; `colorTex_` remained uninitialized,
   making the FBO incomplete. Added explicit `colorTex_.bind()` before `set_image_2d`.
5. **RT texture incomplete when sampled** — Default GL min-filter is
   `GL_NEAREST_MIPMAP_LINEAR`; since the RT has no mipmaps, SpriteBatch sampled it as
   black. Fixed by setting `GL_LINEAR` on `colorTex_` after creation.
6. **Vulkan `ApplyDepthStencilState` signature mismatch** — Base class was extended with
   full stencil parameters; the Vulkan header still had the old 3-param signature,
   causing a compile error.

---

## 4. Current blocker / main problem

**Task 88 — Vulkan smoke test not yet verified.**

The `cna_demo_2d --smoke 3` binary was rebuilt in `cmake-build-vulkan` with the new
`--smoke` flag. The smoke run was interrupted before it could be executed.

**Exact command to run:**
```bash
cd /rv/data/development/github.com/openeggbert/cna/cmake-build-vulkan
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
echo "exit=$?"
```

**Suspected failure mode**: `LoadContent()` loads `Content/images/player.png`. If the
Content directory is missing from `cmake-build-vulkan/`, it will throw and exit non-zero.
Check with: `ls cmake-build-vulkan/Content/`.

**Affected files:**
- `cmake-build-vulkan/cna_demo_2d` — binary exists
- `cmake-build-vulkan/Content/` — may be absent
- `examples/demo_2d/src/Game1.cpp` — `LoadContent` loads assets

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **incomplete** | Task 88: Vulkan smoke test not executed after adding `--smoke` flag |
| **incomplete** | Task 89: Bgfx smoke test blocked — no build dir, task 65 draw calls are no-ops |
| **confirmed bug** | Bgfx `GetBackBufferData` always throws; async readback not implemented (task 82 stub) |
| **incomplete** | `Texture3D::GetData` / `TextureCube::GetData` on GLES3 — stub only |
| **incomplete** | Vulkan textured/lit 3D pipeline (tasks 52–64) — all deferred |
| **incomplete** | Bgfx actual rendering requires pre-compiled bgfx shader binaries |
| **needs verification** | `EasyGLRenderTargetCubeBackend::CreateResources` — likely has the same missing-bind pattern fixed in the 2D RT this session |
| **needs verification** | Vulkan `cna_demo_2d --smoke 3` exit code |
| **unknown** | Whether `CNA_BUILD_TESTS=ON` in Vulkan build breaks anything |

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
  System::*, type aliases (bytecs, String, Single, …)

metagl          ← raw GL function loader + typed enum wrappers
easygl          ← GL resource wrappers (Device, Texture, Framebuffer, Sampler, …)
```

### Critical invariants

- **`Color` has a vtable pointer** (from `IPackedVectorT`): never cast `Color*` to
  `uint8_t*` for GL pixel I/O. Always use a `uint8_t[]` buffer and construct
  `Color(r,g,b,a)` per pixel.
- **`easygl::Texture::set_image_2d` (6-param overload)** does NOT call `glBindTexture`
  first. Always call `tex.bind(target)` explicitly before this overload.
- **`easygl::Framebuffer::attach_texture_2d`** calls raw `glFramebufferTexture2D` on
  the currently bound FBO. Always call `fbo.bind(target)` before attaching.
- **RT texture min-filter**: RenderTarget textures must have min-filter set to
  `GL_LINEAR` (not the default `GL_NEAREST_MIPMAP_LINEAR`) because they have no mipmaps.
- **`glReadBuffer(GL_BACK)`** must be called explicitly before `glReadPixels` on the
  default framebuffer on EGL/GLES3.
- **`currentRtHeight_`** in `EasyGLGraphicsBackend` tracks the bound RT pixel height
  for Y-flip in `ReadBackbuffer`. Kept in sync by `SetRenderTarget2D`.
- **Backend is compile-time only**: no runtime backend switching.
- **XNA namespace = XNA API only**: non-XNA extensions must be tagged `NOXNA`.

### Data flow for pixel readback

```
device.GetBackBufferData(&rect, &pixel, 0, 1)
  → GraphicsDevice::GetBackBufferData            [GraphicsDevice.cpp]
      uint8_t buf[w*h*4]
      → backend_->ReadBackbuffer(x, y, w, h, buf)  [EasyGLGraphicsBackend.cpp]
          if (default FB): glReadBuffer(GL_BACK)
          fbH = currentRtHeight_ > 0 ? currentRtHeight_ : getLogicalSize()
          glReadPixels(x, fbH-y-h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf)
          vertical row flip (bottom-up → top-down)
      for each pixel: data[i] = Color(buf[i*4], buf[i*4+1], buf[i*4+2], buf[i*4+3])
```

---

## 7. Useful commands

```bash
# EasyGL — build
cmake -B cmake-build-easygl -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-easygl --target CNA

# EasyGL — run integration tests (requires X11)
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-easygl -R EasyGL --output-on-failure

# EasyGL — run integration tests individually
cd cmake-build-easygl
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_test_easygl_textured_quad
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_test_easygl_render_target
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_house3d_demo --smoke 3

# Vulkan — build cna_demo_2d
cmake --build cmake-build-vulkan --target cna_demo_2d

# Vulkan — smoke test (manual, binary already built)
cd cmake-build-vulkan
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
echo "exit=$?"

# Vulkan — reconfigure with CNA_BUILD_TESTS=ON, then run ctest
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan --target cna_demo_2d
ctest --test-dir cmake-build-vulkan -R Vulkan --output-on-failure

# Bgfx — create build (requires network for FetchContent)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d 2>&1 | tail -20
cd cmake-build-bgfx
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Check Content directory exists in Vulkan build
ls cmake-build-vulkan/Content/

# Check git status
git status && git log --oneline -5
```

---

## 8. Next smallest tasks

### Task 88 — Verify Vulkan smoke test *(first priority)*

**Goal**: Confirm `cna_demo_2d --smoke 3` exits 0 under the Vulkan backend.

**Files likely involved**:
- `cmake-build-vulkan/cna_demo_2d` (binary, already built)
- Possibly `cmake-build-vulkan/Content/` (check it exists)

**Verification**:
```bash
cd cmake-build-vulkan
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
echo "exit=$?"
```

If exit≠0: check for missing `Content/` directory or audio init errors in stderr.
If `Content/` is missing: symlink or copy from `examples/demo_2d/Content`.

After confirmed pass: update `GRAPHICS_TASKS.md` task 88 to ✅.

---

### Task 88b — Wire CTest for Vulkan smoke test

**Goal**: Make `ctest -R Vulkan_Demo2D_SmokeTest` work.

**Files involved**: reconfigure `cmake-build-vulkan` with `-DCNA_BUILD_TESTS=ON`

**Verification**:
```bash
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan --target cna_demo_2d
ctest --test-dir cmake-build-vulkan -R Vulkan_Demo2D_SmokeTest --output-on-failure
```

---

### Task 89 — Bgfx smoke test

**Goal**: Create a Bgfx build, run `cna_demo_2d --smoke 3`, verify no crash.
Actual rendering will be blank — that is acceptable for a smoke test.

**Files involved**:
- Requires network (FetchContent: bgfx.cmake)
- `CMakeLists.txt` — add `Bgfx_Demo2D_SmokeTest` CTest entry (same pattern as Vulkan)

**Verification**:
```bash
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d 2>&1 | tail -10
cd cmake-build-bgfx
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
echo "exit=$?"
```

---

### Task — Verify `EasyGLRenderTargetCubeBackend` FBO setup

**Goal**: Check whether `EasyGLRenderTargetCubeBackend::CreateResources` has the same
missing-bind bugs that were fixed in the 2D RT this session.

**Files involved**:
- `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (~line 428)

**What to check**: Does `CreateResources` call `fbo_.bind()` before any FBO attach
operations? Does the cube texture have LINEAR min-filter set?

---

### Task — Unit test regression guard for `GetBackBufferData` vtable fix

**Goal**: Add a Google Test that calls `GetBackBufferData` after a colored clear and
asserts the correct color is returned. Prevents regression of the vtable mis-cast bug.

**Files involved**: `tests/Microsoft/Xna/Framework/Graphics/` (new test file)

**Verification**:
```bash
ctest --test-dir cmake-build-easygl -R GetBackBufferData --output-on-failure
```

---

## 9. Do not do yet

- **No Vulkan 3D textured/lit pipeline** (tasks 52–64) — requires SPIR-V shader
  variants; large block of work, deferred by design.
- **No Bgfx shader compilation setup** — bgfx requires platform-specific pre-compiled
  binaries via `shaderc`; do not attempt to wire up a shader build pipeline.
- **No refactoring of `IGraphicsBackend`** — signature changes break all four backends
  simultaneously; only add new virtual methods with default no-op implementations.
- **No changes to `Color` memory layout** — it is a packed XNA type with a virtual
  base; work around the vtable at call sites, do not redesign the class.
- **No mass `easygl` / `metagl` API changes** — these are third-party libraries;
  prefer wrappers.
- **No new XNA API classes** until the current integration test phase (tasks 88, 89)
  is complete.
- **No Google Test suite reorganization** — run existing tests before restructuring.
- **No Bgfx build without verifying network access** — FetchContent downloads from
  GitHub; confirm connectivity first.

---

## 10. Resume prompt

```
Read NEXT.md first. Then open only the files listed for the first task below.
Do not refactor unrelated code. Do not expand scope.

First task: verify Task 88 — Vulkan smoke test.

Steps:
1. Check Content exists: ls cmake-build-vulkan/Content/
2. Run: cd cmake-build-vulkan && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
3. If exit=0: mark task 88 ✅ in GRAPHICS_TASKS.md, update NEXT.md, proceed to Task 89.
4. If exit≠0: diagnose with stderr output; likely fix is Content dir or audio init.
   Fix the smallest possible thing and re-run.

After finishing: update NEXT.md with the new status and the next task.
```
