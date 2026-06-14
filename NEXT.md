# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (namespace
`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend layer.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering to
one of four backends: SDL_Renderer, EasyGL (OpenGL ES 3.2 via easygl + metagl),
Vulkan, or Bgfx.

**Current phase**: All 100 GRAPHICS_TASKS.md tasks addressed.
Three tasks remain deferred by design (true GPU instancing, per-slot Vulkan samplers,
custom SPIR-V Effect loading).

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

### EasyGL backend (`cmake-build-debug`)
- **Builds**: clean.
- Tasks 42–51, 85–87 all complete (MRT, RenderTargetCube, Texture3D/Cube GetData,
  scissor, stencil, sampler, BlendFactor, smoke + readback integration tests).
- `cna_house3d_demo` runs interactively.

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- Tasks 52–63, 88 all complete.
  - Textured/lit 3D pipeline: 6 SPIR-V shaders (textured3d, colored_textured3d,
    lit_textured3d) via 128-byte push constant layout. `GetOrCreatePipelineExt3D`
    covers strides 20, 24, 32. `FillExtPushConst` fills MVP + lighting params.
  - `VulkanRTSource` abstract base unifies `currentRT_` for 2D RTs and cube face proxies.
  - `VulkanMRTProxy`: N-color render pass + combined framebuffer for MRT.
  - `VulkanRenderTargetCubeBackend`: 6-layer cube-compatible VkImage + per-face VkFramebuffers.
  - Smoke test (`--smoke 3`) exits 0 on AMD Radeon 780M (RADV PHOENIX).
- Deferred: Task 55 true instancing, Task 58 per-slot samplers, Task 64 custom Effect.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean.
- **Task 89** `Bgfx_Demo2D_SmokeTest`: ✅ — `--smoke 3` exits 0 (OpenGL 2.1 fallback renderer).
- Tasks 65–67 ⚠️: draw calls are no-ops — needs pre-compiled bgfx shader binaries (shaderc).
- `GetBackBufferData` stub throws (async bgfx readback not implemented).

### What does not work yet
- Vulkan true GPU instancing (Task 55 falls back to single-instance draw).
- Vulkan per-slot SamplerState (Task 58 deferred — requires descriptor set refactor).
- Vulkan custom Effect/SPIR-V loading (Task 64 deferred).
- Bgfx actual 3D rendering — pre-compiled shaders (shaderc) required.
- `Texture3D`/`TextureCube` `GetData` on GLES3 (no `glGetTexImage`).

---

## 3. Last commits

**`abc9068`** — Tasks 65, 80-84: Bgfx 3D vertex/index buffers + full stencil + MRT + effect stubs.

**`4130035`** — Task 45: EasyGL MRT via glDrawBuffers + GetColorGLHandle.

**`29f577f`** — Tasks 47, 48: Texture3D + TextureCube GPU backends.

**`a63475e`** — Tasks 85-88: pixel readback, RT, Vulkan smoke setup.

**HEAD** — Tasks 52–55, 61–62: Vulkan textured+lit 3D pipeline (SPIR-V), RenderTargetCube,
MRT, and smoke test verification.

---

## 4. Current blocker

**None** — all 100 GRAPHICS_TASKS.md tasks are addressed.

Remaining deferred items:
- Task 55: true Vulkan GPU instancing (requires instanced pipeline + per-instance VBO layout)
- Task 58: Vulkan per-slot SamplerState (requires descriptor set-per-slot redesign)
- Task 64: Vulkan custom Effect (SPIR-V loading via IEffectBackend)

Next work is user-directed.

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **confirmed bug** | Bgfx `GetBackBufferData` always throws (async readback not implemented) |
| **incomplete** | Vulkan `DrawInstancedPrimitivesEx` falls back to single-instance (Task 55 deferred) |
| **incomplete** | Vulkan per-slot SamplerState (Task 58 deferred) |
| **incomplete** | Vulkan custom Effect / SPIR-V loading (Task 64 deferred) |
| **incomplete** | Bgfx 3D draw calls are no-ops (pre-compiled bgfx shaders required) |
| **known limit** | EasyGL `FillMode::WireFrame` — no `glPolygonMode` on GLES3 |
| **invariant** | `Color` has vtable pointer — never cast `Color*` to `uint8_t*` for pixel I/O |

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

### Vulkan 3D pipeline overview

| Stride | Vertex type                | Shader pair             | Push constant use       |
|--------|----------------------------|-------------------------|-------------------------|
| 16     | VertexPositionColor        | colored3d vert/frag     | MVP only (first 64B)    |
| 20     | VertexPositionTexture      | textured3d vert/frag    | MVP + texture flag      |
| 24     | VertexPositionColorTexture | colored_textured3d      | MVP + vcEnabled flag    |
| 32     | VertexPositionNormalTexture| lit_textured3d          | MVP + lighting (128B)   |

Push constant layout (128 bytes = 32 floats):
- [0..15]  = MVP matrix
- [16..19] = diffuseColor (vec4)
- [20..22] = ambientColor (vec3)
- [23]     = lightingEnabled
- [24..26] = light0Dir (vec3)
- [27]     = textureEnabled
- [28..30] = light0Diffuse (vec3)
- [31]     = vertexColorEnabled

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
- **Vulkan build: `j1`** — build with `-j1` to avoid race condition in shader header generation.

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

# Vulkan — build (use -j1 to avoid race in SPIR-V header generation)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN
cmake --build cmake-build-vulkan --target cna_demo_2d -j1

# Vulkan — smoke test (must run from cmake-build-vulkan/)
cd cmake-build-vulkan && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Bgfx — smoke test
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d
cd cmake-build-bgfx && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3
```

---

## 8. Next tasks (ordered)

All 100 GRAPHICS_TASKS.md tasks addressed. Await user direction.

Candidates if continuing graphics work:
1. **Task 55 true instancing** — add `VK_VERTEX_INPUT_RATE_INSTANCE` binding + instanced pipeline
2. **Task 58 per-slot samplers** — per-descriptor-set sampler state for slots 0–15
3. **Task 64 custom Effect** — load arbitrary SPIR-V via `IEffectBackend`
4. **Bgfx shaders** — compile bgfx shaders with shaderc to unblock Tasks 65–67

---

## 9. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

All 100 GRAPHICS_TASKS.md tasks are addressed. Await user direction.
Do NOT touch sharp-runtime — another agent is working on it.

Update NEXT.md after each task.
```
