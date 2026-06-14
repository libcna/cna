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
Tasks 85–89 are all complete. Phase 7 is done.

**Key architectural decisions**:
- Backend selected at compile time via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — vtable pointer sits before
  the packed pixel data; never cast `Color*` to `uint8_t*` for GL pixel I/O.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases and `System.*` stubs.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference.

---

## 2. Current status

### EasyGL backend (`cmake-build-easygl`)
- **Builds**: clean.
- **Task 85** `EasyGL_House3D_SmokeTest`: ✅
- **Task 86** `EasyGL_TexturedQuad_Readback`: ✅ — 1×1 red texture, `GetBackBufferData`, asserts R=255.
- **Task 87** `EasyGL_RenderTarget2D_Readback`: ✅ — 64×64 RT cleared green, blitted via SpriteBatch, asserts G=255.
- `cna_house3d_demo` runs interactively.

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- **Task 88** `Vulkan_Demo2D_SmokeTest`: ✅ — `--smoke 3` exits 0 on AMD Radeon 780M (RADV PHOENIX).
- CTest entry in `CMakeLists.txt`; reconfigure with `-DCNA_BUILD_TESTS=ON` to run via ctest.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean after fixing stencil op macro names, `bgfx::setBlendFactor` (no such API →
  stored as `blendFactorPacked_`), `PointListEXT` (not in XNA 4.0 → removed), and
  `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` (internal header → replaced with local constant 8).
- **Task 89** `Bgfx_Demo2D_SmokeTest`: ✅ — `--smoke 3` exits 0 (OpenGL 2.1 fallback renderer).
- Task 65 ⚠️: draw calls are no-ops — needs pre-compiled bgfx shader binaries (shaderc).
- `GetBackBufferData` stub throws (async bgfx readback not implemented).

### What does not work yet
- Vulkan textured/lit 3D pipeline (tasks 52–64, deferred).
- Bgfx actual rendering — pre-compiled shaders (shaderc) required; task 65 draw calls are no-ops.
- `Texture3D`/`TextureCube` `GetData` on GLES3 (no `glGetTexImage`).

---

## 3. Last commits

**`a63475e`** — `fix+test(EasyGL/Tasks 85-88): pixel readback, RT, Vulkan smoke setup`
- `GetBackBufferData`: tmp buffer to avoid `Color` vtable pointer mis-cast.
- `ReadBackbuffer`: explicit `glReadBuffer(Back)`; `currentRtHeight_` for Y-flip.
- `EasyGLRenderTargetBackend::CreateResources`: bind FBO before attach; bind texture before `set_image_2d`; `GL_LINEAR` filter.
- Vulkan `ApplyDepthStencilState` signature fixed; `demo_2d`: `--smoke N` flag.

**HEAD** (after a63475e) — Tasks 88+89: Vulkan+Bgfx smoke tests ✅
- Bgfx: fixed stencil op macros (`_INCRWRAP`/`_DECRWRAP` → `_INCR`/`_DECR`), `bgfx::setBlendFactor` API, `PointListEXT`, `BGFX_CONFIG_MAX_FRAME_BUFFER_ATTACHMENTS` internal header.
- `Bgfx_Demo2D_SmokeTest` CTest entry added; Vulkan smoke test confirmed passing.

---

## 4. Current blocker

**None** — Phase 7 (integration tests, tasks 85–89) is complete.

Next work is Phase 8 or user-directed tasks (see section 8).

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **confirmed bug** | Bgfx `GetBackBufferData` always throws (async readback not implemented) |
| **incomplete** | `Texture3D::GetData` / `TextureCube::GetData` on GLES3 — stub only |
| **incomplete** | Vulkan textured/lit 3D pipeline (tasks 52–64) — deferred |
| **needs verification** | `EasyGLRenderTargetCubeBackend::CreateResources` — likely has the same missing FBO bind + texture parameter bugs fixed in the 2D RT (`a63475e`) |

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

### Critical invariants

- **`Color` has a vtable pointer** — never cast `Color*` to `uint8_t*` for pixel I/O;
  use a `uint8_t[]` buffer and construct `Color(r,g,b,a)` per pixel.
- **`easygl::Texture::set_image_2d` (6-param)** — does NOT bind the texture; always call
  `tex.bind(target)` first.
- **`easygl::Framebuffer::attach_texture_2d`** — operates on the currently bound FBO;
  always call `fbo.bind(target)` before attaching.
- **RT texture min-filter** — must be `GL_LINEAR`; default `GL_NEAREST_MIPMAP_LINEAR`
  makes the RT texture-incomplete (no mipmaps).
- **`glReadBuffer(GL_BACK)`** — must be called explicitly before `glReadPixels` on the
  default FB on EGL/GLES3.
- **`currentRtHeight_`** in `EasyGLGraphicsBackend` — tracks bound RT height for Y-flip
  in `ReadBackbuffer`; kept in sync by `SetRenderTarget2D`.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.

### Pixel readback data flow

```
device.GetBackBufferData(&rect, &pixel, 0, 1)
  → GraphicsDevice::GetBackBufferData
      uint8_t buf[w*h*4]
      → backend_->ReadBackbuffer(x, y, w, h, buf)
          if (default FB): glReadBuffer(GL_BACK)
          fbH = currentRtHeight_ > 0 ? currentRtHeight_ : getLogicalSize()
          glReadPixels(x, fbH-y-h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf)
          vertical row flip
      data[i] = Color(buf[i*4], buf[i*4+1], buf[i*4+2], buf[i*4+3])
```

---

## 7. Useful commands

```bash
# EasyGL — build + integration tests
cmake -B cmake-build-easygl -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-easygl --target CNA
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-easygl -R EasyGL --output-on-failure

# EasyGL — individual tests
cd cmake-build-easygl
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_test_easygl_textured_quad
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_test_easygl_render_target
DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_house3d_demo --smoke 3

# Vulkan — smoke test (Task 88)
ls cmake-build-vulkan/Content/
cd cmake-build-vulkan && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Vulkan — reconfigure with CNA_BUILD_TESTS=ON for ctest
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-vulkan --target cna_demo_2d
ctest --test-dir cmake-build-vulkan -R Vulkan_Demo2D_SmokeTest --output-on-failure

# Bgfx — create build (requires network)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d
cd cmake-build-bgfx && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
```

---

## 8. Next tasks (ordered)

### 1. Verify `EasyGLRenderTargetCubeBackend` FBO setup

Check `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` ~line 428.
Confirm `fbo_.bind()` is called before any attachment and that cube texture has
`GL_LINEAR` min-filter set. Fix if the same bugs are present.

### 2. Unit test regression guard for `GetBackBufferData`

Add a Google Test that calls `GetBackBufferData` after a colored clear and asserts
the returned `Color` is correct. Prevents regression of the vtable mis-cast bug.
File: `tests/Microsoft/Xna/Framework/Graphics/GraphicsDeviceTest.cpp` (new or extend).

---

## 9. Do not do yet

- No Vulkan 3D textured/lit pipeline (tasks 52–64) — SPIR-V work, deferred by design.
- No Bgfx shader compilation (`shaderc`) — requires platform-specific binaries.
- No `IGraphicsBackend` signature refactoring — breaks all four backends at once.
- No changes to `Color` memory layout — work around the vtable at call sites.
- No mass `easygl`/`metagl` API changes — third-party libraries.
- No new XNA API classes until tasks 88 and 89 are closed.

---

## 10. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Phase 7 (tasks 85–89) is complete. Await user direction for the next task.
Candidates (in order): verify EasyGLRenderTargetCubeBackend FBO setup,
add GetBackBufferData regression test.

Update NEXT.md after each task.
```
