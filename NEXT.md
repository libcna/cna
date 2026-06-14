# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (namespace
`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend layer.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering to
one of four backends: SDL_Renderer, EasyGL (OpenGL ES 3.2 via easygl + metagl),
Vulkan, or Bgfx.

**Current phase**: GRAPHICS_TASKS.md Phases 1–11 complete (Tasks 101–114 ✅).
Phases 12–14 (Tasks 115–125) remain.

**Key architectural decisions**:
- Backend selected at compile time via `CNA_GRAPHICS_BACKEND` CMake option.
- `IGraphicsBackend` is the contract between the XNA API layer and any backend.
- `Color` inherits `IPackedVectorT<UInt32>` (virtual base) — vtable pointer sits before
  the packed pixel data; never cast `Color*` to `uint8_t*` for GL pixel I/O.
- SharpRuntime (`/rv/data/development/github.com/openeggbert/sharp-runtime`) provides
  .NET primitive type aliases and `System.*` stubs.
- FNA source at `/rv/data/library/github.com/FNA-XNA/FNA/src` is the authoritative
  XNA 4.0 API reference.
- **DO NOT touch sharp-runtime** — another agent is working on it.

---

## 2. Current status

### EasyGL backend (`cmake-build-debug`)
- **Builds**: clean.
- Effect system fully generalized (Phase 9, Tasks 101–105):
  - `currentEffect_` changed to `Effect*`; `FillGpuDrawParams()` dispatches polymorphically.
  - `AlphaTestEffect`: GLSL alpha-discard uniform (`uAlphaTest[4]`) in all 4 fragment shaders.
  - `DualTextureEffect`: stride=20 dual-sampler pipeline (`prog_dual_textured_`).
  - `EnvironmentMapEffect`: stride=32 cube-map pipeline (`prog_env_mapped_`).
  - `SkinnedEffect`: 72-bone UBO skinning shader (`prog_skinned_`).
- Phase 10 (Tasks 110–113): non-zero vertex/index offsets work; `SpriteBatch::Begin(effect)`
  wires custom Effect into sprite pipeline.
- `FillMode::WireFrame` not supported on GLES3 (no `glPolygonMode`) — known limit.

### Vulkan backend (`cmake-build-vulkan`)
- **Builds**: clean.
- Phase 9 (Tasks 106–109): AlphaTest / DualTexture / EnvironmentMap / Skinned SPIR-V
  shader pairs + dedicated pipeline layouts.
- Phase 10 (Tasks 110–113):
  - `vertexStart` / `startIndex` / `baseVertex` properly forwarded.
  - True GPU instancing via `VK_VERTEX_INPUT_RATE_INSTANCE` + `GetOrCreatePipelineInstanced3D`.
  - `FillMode::WireFrame` → `fillModeNonSolid` GPU feature + `VK_POLYGON_MODE_LINE`; all 7
    pipeline functions encode wireframe in `Make3DKey` / `MakeExt3DKey`.
  - `SpriteBatch::Begin(effect)` stores effect pointer; `End()` calls `Apply()` before flush.
- Build: use `-j1` to avoid race condition in SPIR-V header generation.

### Bgfx backend (`cmake-build-bgfx`)
- **Builds**: clean.
- Phase 11 (Task 114): shaderc toolchain set up; `colored3d` shaders compiled and embedded:
  - `src/CNA/Internal/Backends/Bgfx/shaders/vs_colored3d.sc` + `fs_colored3d.sc` + `varying.def.sc`
  - `compile_shaders.py` produces GLSL/ESSL/SPIR-V/WGSL variants → `bgfx_shaders.hpp`
  - Manual `kColored3dShaders[]` struct (avoids `BGFX_EMBEDDED_SHADER` macro which requires DXBC on Linux)
  - `colored3DProgram_` created at init from `kColored3dShaders`
  - `DrawColoredPrimitives` submits `colored3DProgram_` (Task 115 effectively done)
- **Tasks 116–117** remain: textured/lit 3D shaders and `GetBackBufferData` readback.
- Smoke test (`--smoke 3`) exits 0.

### Bgfx shaderc paths (needed to recompile shaders)
```
shaderc binary:   cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc
bgfx include dir: cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src
```

---

## 3. Last commits

**`9effffc`** — Tasks 112–114: FillMode::WireFrame (Vulkan), SpriteBatch custom effect wiring,
Bgfx shaderc toolchain + colored3d shaders embedded.

**`6de93fa`** — Tasks 101–112: Effect system generalization (Phase 9), all 4 effect GPU variants
on EasyGL + Vulkan, non-zero draw offsets, true Vulkan GPU instancing.

---

## 4. Current state of GRAPHICS_TASKS.md

| Phase | Range | Status |
|-------|-------|--------|
| 1–8 | Tasks 1–100 | ✅ all complete |
| 9 — Effect system | Tasks 101–109 | ✅ all complete |
| 10 — Draw features | Tasks 110–113 | ✅ all complete |
| 11 — Bgfx 3D shaders | Task 114 | ✅; Tasks 115–117 ⬜ |
| 12 — Vulkan deferred | Tasks 118–119 | ⬜ |
| 13 — Missing XNA classes | Tasks 120–121 | ⬜ |
| 14 — Integration tests | Tasks 122–125 | ⬜ |

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **confirmed bug** | Bgfx `GetBackBufferData` always throws (async readback not implemented — Task 117) |
| **incomplete** | Bgfx `DrawPrimitivesEx` textured/lit — needs Tasks 116 shaders |
| **incomplete** | Vulkan per-slot SamplerState (Task 118 deferred — requires descriptor set refactor) |
| **incomplete** | Vulkan custom Effect / SPIR-V loading (Task 119 deferred) |
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

| Stride | Vertex type                 | Shader pair              | Push constant use       |
|--------|-----------------------------|--------------------------|-------------------------|
| 16     | VertexPositionColor         | colored3d                | MVP only (first 64B)    |
| 20     | VertexPositionTexture       | textured3d               | MVP + texture flag      |
| 24     | VertexPositionColorTexture  | colored_textured3d       | MVP + vcEnabled flag    |
| 32     | VertexPositionNormalTexture | lit_textured3d           | MVP + lighting (128B)   |
| 20/24/32 | above + alpha test       | alpha_test3d             | MVP + alphaTest vec4    |
| 20     | VertexPositionTexture       | dual_texture3d           | MVP + dual sampler      |
| 32     | VertexPositionNormalTexture | env_map3d                | MVP + world + FS UBO    |
| 52     | VertexPositionNormalTextureSkinned | skinned3d         | MVP + bone palette UBO  |
| 16/32  | any                         | instanced3d              | VK_VERTEX_INPUT_RATE_INSTANCE binding=1 |

Push constant layout (128 bytes = 32 floats):
- [0..15]  = MVP matrix
- [16..19] = diffuseColor (vec4)
- [20..22] = ambientColor (vec3)
- [23]     = lightingEnabled
- [24..26] = light0Dir (vec3)
- [27]     = textureEnabled
- [28..30] = light0Diffuse (vec3)
- [31]     = vertexColorEnabled

### Bgfx embedded shader pattern

`BGFX_EMBEDDED_SHADER` macro activates DXBC on Linux (`BGFX_PLATFORM_SUPPORTS_DXBC`
includes `BX_PLATFORM_LINUX`) but shaderc can't produce DXBC without D3D4Linux.
Solution: build the `bgfx::EmbeddedShader` struct manually with only GL/GLES/Vulkan/WebGPU/Noop
entries + `RendererType::Count` sentinel. `createEmbeddedShader` terminates at Count sentinel.

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
- **Vulkan build: `-j1`** — build with `-j1` to avoid race condition in shader header generation.
- **Backend is compile-time only** — no runtime switching.
- **XNA namespace = XNA API only** — non-XNA extensions tagged `NOXNA`.

---

## 7. Useful commands

```bash
# EasyGL — build + integration tests
cmake -B cmake-build-debug -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug --target CNA
DISPLAY=:0 SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-debug -R EasyGL --output-on-failure

# Vulkan — build (use -j1 to avoid race in SPIR-V header generation)
cmake -B cmake-build-vulkan -DCNA_GRAPHICS_BACKEND=VULKAN
cmake --build cmake-build-vulkan --target cna_demo_2d -j1
cd cmake-build-vulkan && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Bgfx — build + smoke test
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX
cmake --build cmake-build-bgfx --target cna_demo_2d
cd cmake-build-bgfx && DISPLAY=:0 SDL_VIDEODRIVER=x11 ./cna_demo_2d --smoke 3

# Bgfx — recompile 3D shaders (run from repo root)
python3 src/CNA/Internal/Backends/Bgfx/shaders/compile_shaders.py \
    cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc \
    cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src

# Bgfx — build shaderc (one-time, ~10 min)
cmake -B cmake-build-bgfx -DCNA_GRAPHICS_BACKEND=BGFX -DCNA_BGFX_BUILD_SHADERC=ON
cmake --build cmake-build-bgfx --target shaderc
```

---

## 8. Next tasks (ordered by priority)

| # | Task | Notes |
|---|------|-------|
| 115 | Bgfx: wire `DrawColoredPrimitives` with `colored3DProgram_` | Likely already done — `DrawColoredPrimitives` submits the program; verify and mark ✅ |
| 116 | Bgfx: `textured3d`, `colored_textured3d`, `lit_textured3d` shader variants via shaderc; wire `DrawPrimitivesEx` | Add new `.sc` files; extend `compile_shaders.py` and `bgfx_shaders.hpp` |
| 117 | Bgfx: `GetBackBufferData` — async readback via `bgfx::blit` + `bgfx::readTexture` + `bgfx::frame(true)` | Currently always throws |
| 118 | Vulkan: per-slot SamplerState — one `VkSampler` per binding slot (0–15) | Large descriptor set refactor |
| 119 | Vulkan: custom Effect / SPIR-V loading — `IEffectBackend::CompileProgram(vertSpv, fragSpv)` | `ShaderEffect` fully functional on Vulkan |
| 120 | `VideoPlayer` stub — `Microsoft::Xna::Framework::Media` namespace; `Play/Pause/Stop/Dispose`; no actual decoding | API completeness stub |
| 121 | `DxtUtil` — software DXT1/3/5 decompression; used by `Texture2D::FromStream` | Reference: FNA `DxtUtil.cs` |
| 122 | Integration test: EasyGL — `AlphaTestEffect` alpha cutout + pixel readback | Requires Task 102 ✅ |
| 123 | Integration test: EasyGL — `SkinnedEffect` 2-bone transform + mesh deformation | Requires Task 105 ✅ |
| 124 | Integration test: Vulkan — `DrawInstancedPrimitives` 3 instances at different positions | Requires Task 111 ✅ |
| 125 | Integration test: EasyGL/Vulkan — DXT1 texture via `FromStream`, pixel readback | Requires Task 121 |

---

## 9. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: GRAPHICS_TASKS.md Tasks 101–114 complete. Next tasks are 115–125.
DO NOT touch sharp-runtime — another agent is working on it.

Update NEXT.md after each task.
```
