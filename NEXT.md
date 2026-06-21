# NEXT.md — CNA handoff document

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (namespace
`Microsoft::Xna::Framework`) built on SDL3 with a pluggable graphics backend layer.
It is a framework/runtime, not a game.

**Main goal**: let C++ applications use the XNA 4.0 API while delegating rendering to
one of four backends: SDL_Renderer, EasyGL (OpenGL ES 3.2 via easygl + metagl),
Vulkan, or Bgfx.

**Current phase**: GRAPHICS_TASKS.md Phases 1–15 complete (Tasks 101–131 ✅),
Tasks 132–136 complete (EasyGL/Vulkan ShaderEffect/DualTextureEffect/EnvironmentMapEffect),
Tasks 137–145 complete (Bgfx parity, GetVertexBuffers, Model.Draw integration test, MRT integration test).
**Phases 1–18 complete (Tasks 1–150 all ✅). Phases 19–25 added (Tasks 151–200) based on June 2026 external code review. Phase 19 is next: SpriteBatch stub removal (Tasks 151–160).**

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
- Phase 11 (Tasks 114–116): full 3D shader suite compiled and wired:
  - 4 shader pairs compiled (colored3d, textured3d, colored_textured3d, lit_textured3d) — GLSL/ESSL/SPIR-V/WGSL × 32 variants
  - `compile_shaders.py` grouped into `SHADER_PAIRS`; generates 4 separate `bgfx::EmbeddedShader` arrays
  - `bgfx/platform.h` removed (not shipped by bgfx.cmake; `PlatformData` is in `bgfx/bgfx.h`)
  - 3 new programs + 6 new uniforms initialized at startup; destroyed in destructor
  - `DrawPrimitivesEx` dispatches to lit / coloredTextured / textured / colored program
- Tasks 114–117 all complete. Phase 11 fully done.

### Bgfx shaderc paths (needed to recompile shaders)
```
shaderc binary:   cmake-build-bgfx/_deps/bgfx_cmake-build/cmake/bgfx/shaderc
bgfx include dir: cmake-build-bgfx/_deps/bgfx_cmake-src/bgfx/src
```

---

## 3. Last commits

**HEAD** — Task 119: Vulkan custom Effect / SPIR-V — `VulkanEffectBackend` with GLSL std140-aligned
128-byte push constants; push constant offsets fixed (GLSL pads `vec2` to 16 before `mat4`);
`cna_test_vulkan_shader_effect` passes (red tint over green background).

**prev** — Task 118: Vulkan per-slot SamplerState — `SamplerStateKey` cache, `slotSamplers_[16]`,
`GetOrCreateTexSamplerDescSet`, anisotropy support, draw paths use slot-specific sampler.

**prev** — Tasks 122–125: Integration tests — AlphaTestEffect cutout, SkinnedEffect bone
deformation, Vulkan DrawInstancedPrimitives ×3, DXT1 FromStream readback.

**prev** — Tasks 115–121: Bgfx textured/lit shaders + DrawPrimitivesEx, GetBackBufferData callback,
VideoPlayer (confirmed complete), DxtUtil + Texture2D::FromStream DDS decoding.

**`16db778`** — Tasks 115–116: Bgfx textured/lit 3D shaders + DrawPrimitivesEx dispatch.

**`e37355f`** — docs: NEXT.md handoff for Tasks 101–114.

---

## 4. Current state of GRAPHICS_TASKS.md

| Phase | Range | Status |
|-------|-------|--------|
| 1–8 | Tasks 1–100 | ✅ all complete |
| 9 — Effect system | Tasks 101–109 | ✅ all complete |
| 10 — Draw features | Tasks 110–113 | ✅ all complete |
| 11 — Bgfx 3D shaders | Tasks 114–117 | ✅ all complete |
| 12 — Vulkan deferred | Tasks 118–119 | ✅ all complete |
| 13 — Missing XNA classes | Tasks 120–121 | ✅ all complete |
| 14 — Integration tests | Tasks 122–125 | ✅ all complete |
| 15 — Unit test gaps | Tasks 126–131 | ✅ all complete |
| 16 — Effect integration | Tasks 132–135 | ✅ done (136 blocked on Task 143) |

---

## 5. Known bugs and limitations

| Status | Item |
|--------|------|
| **unverified** | Bgfx `GetBackBufferData` implemented via `requestScreenShot` callback + up to 3× `bgfx::frame()`; correct in single-threaded bgfx mode — not integration-tested yet |
| ✅ **done** | Vulkan per-slot SamplerState (Task 118 — `slotSamplers_[16]`, `samplerCache_`, `GetOrCreateTexSamplerDescSet`) |
| ✅ **done** | Vulkan custom Effect / SPIR-V loading (Task 119 — `VulkanEffectBackend`, 128-byte push constants with GLSL std140 alignment, `cna_test_vulkan_shader_effect` passes) |
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

Tasks 151–200 added across Phases 19–25 based on June 2026 external code review.
Recommended order:

| Priority | # | Why first |
|----------|---|-----------|
| **High** | 151–156 | SpriteBatch `Draw` stub removal — 6 no-op overloads, trivial one-liners |
| **High** | 157–159 | `DrawString(StringBuilder,…)` stubs — convert via `StringBuilder::ToString()` |
| **High** | 160 | Unit tests for all 9 newly implemented overloads |
| **High** | 161–166 | SpriteBatch sort-mode and guard tests (logic already present, just needs tests) |
| **High** | 167–168 | SpriteEffects flip + transformMatrix pixel integration tests |
| **High** | 169–173 | Texture SetData/GetData: partial rect, startIndex, mip levels, cube faces, 3D slices |
| Medium | 174–176 | SurfaceFormat support table, DXT golden tests, sRGB handling |
| Medium | 177–183 | RenderTargetUsage, device reset events, PresentationParameters round-trip |
| Medium | 184–190 | Effect.Clone, EffectParameter guards, BasicEffect + AlphaTestEffect pixel tests |
| Medium | 191–196 | Stock effects backend parity (DualTexture, EnvMap, Skinned, fog, EnableDefaultLighting) |
| Low | 197–200 | PackedVector golden values, edge-case tests, docs update |

---

## 9. Resume prompt

```
Read NEXT.md first. Open only the files needed for the first task.
Do not refactor unrelated code. Do not expand scope.

Current status: GRAPHICS_TASKS.md phases 1–15 complete + Task 132 complete (EasyGL ShaderEffect GLSL test).
Tasks 126–150 added (Phases 15–18) — Phase 16 in progress; Task 136 blocked on Task 143 (Vulkan TextureCube upload). Next unblocked: 137 (Bgfx AlphaTestEffect) or 141 (GetVertexBuffers).
DO NOT touch sharp-runtime — another agent is working on it.

Update NEXT.md after each task.
```
