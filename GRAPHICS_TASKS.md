# Graphics Implementation Task Plan

> Goal: XNA 4.0 Graphics namespace fully implemented — all 4 backends
> (SDL_Renderer, EasyGL, Vulkan, Bgfx).
>
> **SDL_Renderer is intentionally 2D-only.** All 3D calls throw `std::runtime_error`.
> This is correct XNA/FNA behavior — SDL_Renderer cannot do 3D.

---

## Legend

| Symbol | Meaning |
|--------|---------|
| ⬜ | Not started |
| 🔄 | In progress |
| ✅ | Done |
| ⛔ | Blocked / deferred |
| ℹ️  | Known limitation (not a bug) |
| ⚠️  | Partial / no-op |

---

## Completed work summary (Phases 1–8)

All 100 original tasks addressed.

| Phase | Scope | Status |
|-------|-------|--------|
| 1 | API/Doxygen audit — all Graphics headers | ✅ complete |
| 2 | Unit tests — Viewport, Rect, States, Vertices, PackedVector, Effects, SpriteFont, Model, OcclusionQuery, Exceptions | ✅ complete |
| 3 | SDL_Renderer — SamplerState filter, ScissorRect, RenderTarget2D, BlendState, 3D throws | ✅ complete |
| 4 | EasyGL — ScissorRect, stencil, per-slot sampler, MRT, RenderTargetCube, Texture3D/Cube GetData, BlendFactor, ReferenceStencil | ✅ complete |
| 5 | Vulkan — OcclusionQuery, ScissorRect, Texture3D, TextureCube, RenderTargetCube, MRT, BlendFactor, SPIR-V shaders (stride 16/20/24/32), DrawPrimitivesEx textured+lit | ✅ complete |
| 6 | Bgfx — VB/IB, BlendState, DepthStencil, RasterizerState, SamplerState, ScissorRect, RenderTarget2D/Cube, MRT, OcclusionQuery, Texture3D/Cube, BlendFactor, full stencil | ✅ complete |
| 7 | Integration tests — EasyGL house3D, EasyGL readback, EasyGL RT, Vulkan smoke, Bgfx smoke | ✅ complete |
| 8 | IGraphicsBackend interface additions — SetScissorRect, SetBlendFactor, SetReferenceStencil, SetRenderTargets, IRenderTargetCubeBackend, IEffectBackend | ✅ complete |

---

## Phase 9 — Effect system generalization (critical gap)

> **Root cause:** `GraphicsDevice::currentEffect_` is typed `BasicEffect*`.
> `BuildGpuDrawParams()` takes `const BasicEffect*`.
> `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, and `SkinnedEffect`
> all inherit from `Effect`, not from `BasicEffect` — they cannot be passed to `DrawPrimitivesEx`.
> Their `OnApply()` fully updates `EffectParameter` objects, but GPU output is zero.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 101 | Generalize Effect dispatch — change `currentEffect_` to `Effect*`; add `virtual void FillGpuDrawParams(GpuDrawParams&) const` to `Effect` base (empty default); `BasicEffect` overrides with current `BuildGpuDrawParams` logic; all `DrawPrimitivesEx` calls replaced by `currentEffect_->FillGpuDrawParams(p)`; `Effect::Apply()` now calls `SetCurrentEffect(this)` so all effect subclasses auto-register | ✅ | EasyGL + Vulkan build clean; smoke tests pass |
| 102 | EasyGL: AlphaTestEffect GPU shader variant — add `alphaTest[4]` to GpuDrawParams (default `{0,0,1,1}` = Always pass); `AlphaTestEffect::FillGpuDrawParams` computes X/Y/Z/W from alphaFunction+referenceAlpha; all 4 EasyGL fragment shaders include `uAlphaTest` uniform + `discard` logic; `BindDrawParams` uploads it; smoke test passes | ✅ | EasyGL + Vulkan build clean |
| 103 | EasyGL: DualTextureEffect GPU shader variant — `texture1`+`dualTexture` added to GpuDrawParams; `DualTextureEffect::FillGpuDrawParams` implemented; `prog_dual_textured_` (stride=20, two samplers) added to EasyGL; `BindDrawParams` activates GL_TEXTURE1 via `metagl::glActiveTexture`; builds clean | ✅ | EasyGL + Vulkan build clean |
| 104 | EasyGL: EnvironmentMapEffect GPU shader variant — `envMap`/`envMapAmount`/`envMapSpecular`/`emissiveColor`/`eyePositionWorld`/`envMapping` added to GpuDrawParams; `BindGL()` on `ITextureCubeBackend`; `EnvironmentMapEffect::FillGpuDrawParams` implemented; `prog_env_mapped_` (stride=32, cube map unit 1, reflect(-E,N)) added to EasyGL; `DebugSimulateContextLoss` updated; builds clean | ✅ | EasyGL + Vulkan build clean |
| 105 | EasyGL: SkinnedEffect GPU shader variant — bone transform array (max 72 mat4) as uniform block; skinning vertex shader (4 weights + indices); `SetBoneTransforms` upload to backend | ✅ | EasyGL + Vulkan build clean; 1333 tests pass |
| 106 | Vulkan: AlphaTestEffect SPIR-V shader pair + pipeline | ✅ | alpha_test3d.{vert,frag}.glsl; pipelineLayoutAlphaTest3D_; UV remapped to loc=1 for stride 20/24/32; EasyGL-compatible discard logic; EasyGL + Vulkan build clean; 1333 tests pass |
| 107 | Vulkan: DualTextureEffect SPIR-V shader pair + pipeline | ✅ | dual_texture3d.frag.glsl; descriptorSetLayout2Tex_ (2 samplers); pipelineLayoutDualTex3D_; dualTexDescSet cached by (view0,view1); EasyGL + Vulkan build clean |
| 108 | Vulkan: EnvironmentMapEffect SPIR-V shader pair + pipeline | ✅ | env_map3d.{vert,frag}.glsl; UBO ring buffer per frame for FS params; descriptorSetLayoutEnvMap_ (2 samplers + dynamic UBO); pipelineLayoutEnvMap3D_ (128-byte PC: mvp+world); defaultWhiteCubeView_ fallback; EasyGL + Vulkan build clean |
| 109 | Vulkan: SkinnedEffect SPIR-V shader pair + pipeline (bone data via storage buffer or large push constant block) | ✅ | skinned3d.{vert,frag}.glsl; bone palette in UNIFORM_BUFFER_DYNAMIC (4608 bytes/draw, 32 draws/frame ring buffer); descriptorSetLayoutSkinned_ (sampler2D + UBO_dynamic); stride=52 pipeline; EasyGL + Vulkan build clean |

---

## Phase 10 — Missing draw call features

| # | Task | Status | Notes |
|---|------|--------|-------|
| 110 | Support non-zero `vertexStart` / `startIndex` / `baseVertex` in `DrawPrimitives` / `DrawIndexedPrimitives` — EasyGL: `glDrawArrays(offset, …)` / `glDrawElementsBaseVertex`; Vulkan: `firstVertex`/`firstIndex` params; Bgfx: offset param; remove the current throw | ✅ | EasyGL: vertexStart→draw_arrays first; startIndex→byte offset; baseVertex→glDrawElementsBaseVertex. Vulkan: VB/IB copy offsets; baseVertex→vkCmdDrawIndexed vertexOffset. EasyGL + Vulkan build clean. |
| 111 | Vulkan: true GPU instancing — `VK_VERTEX_INPUT_RATE_INSTANCE` binding; per-instance VBO layout; `DrawInstancedPrimitivesEx` calls `vkCmdDrawIndexed` with correct `instanceCount` | ✅ | instanced3d.vert.glsl: binding=0 VERTEX (pos), binding=1 INSTANCE (mat4); frame3DInstVB_ ring buffer; GetOrCreatePipelineInstanced3D; GpuDrawParams.instanceVb; GraphicsDevice finds per-instance binding from currentVertexBuffers_; vkCmdDrawIndexed with draw.instanceCount; EasyGL + Vulkan build clean |
| 112 | Vulkan: FillMode::WireFrame — query and enable `VkPhysicalDeviceFeatures.fillModeNonSolid` at device creation; `ApplyRasterizerState` maps `FillMode::WireFrame` to `VK_POLYGON_MODE_LINE` | ✅ | fillModeNonSolidSupported_ queried at device creation; ApplyRasterizerState sets fillModeWireframe_; all 7 pipeline creation functions pass wireframe to MakeExt3DKey/Make3DKey and use ternary polygonMode; all 5 draw dispatch sites set d.wireframe = fillModeWireframe_; EasyGL + Vulkan build clean |
| 113 | SpriteBatch::Begin(effect) — custom Effect parameter is stored but ignored; wire it into the sprite rendering pipeline; EasyGL: switch shader program when effect is non-null | ⬜ | |

---

## Phase 11 — Bgfx 3D rendering (unblocking)

> Bgfx requires pre-compiled shader binaries. All 3D draw calls are currently no-ops.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 114 | Bgfx: set up shaderc toolchain; compile `colored3d.vert.sc` + `colored3d.frag.sc` (bgfx varying.def.sc + GLSL source → bgfx binary); embed as `bgfx_shaders.hpp` analogous to `spirv_shaders.hpp` | ⬜ | Prerequisite for 115–116 |
| 115 | Bgfx: wire DrawColoredPrimitives with the compiled colored3d shader program | ⬜ | Requires 114 |
| 116 | Bgfx: textured3d, colored_textured3d, lit_textured3d shader variants via shaderc; wire DrawPrimitivesEx | ⬜ | Requires 114 |
| 117 | Bgfx: GetBackBufferData — async readback via `bgfx::blit` to a CPU-visible texture + `bgfx::readTexture` + `bgfx::frame(true)` wait | ⬜ | Currently always throws |

---

## Phase 12 — Vulkan deferred

| # | Task | Status | Notes |
|---|------|--------|-------|
| 118 | Vulkan: per-slot SamplerState — one `VkSampler` per binding slot; update descriptor set layout to include a sampler array (slots 0–15); `SetSamplerState(slot, state)` destroys and recreates the sampler | ⬜ | Requires substantial descriptor set redesign |
| 119 | Vulkan: custom Effect / SPIR-V loading — `IEffectBackend::CompileProgram(vertSpv, fragSpv)`, `Bind()`, `SetUniformMatrix`, `SetUniformFloat4`; `ShaderEffect` fully functional on Vulkan | ⬜ | |

---

## Phase 13 — Missing XNA classes

| # | Task | Status | Notes |
|---|------|--------|-------|
| 120 | `VideoPlayer` stub — correct namespace (`Microsoft::Xna::Framework::Media`), constructor, `Play()` / `Pause()` / `Stop()` / `Dispose()`, `getStateProperty()` (`MediaState`), `getTextureProperty()` (returns `nullptr`), `Video` class stub; no actual video decoding | ⬜ | Full decoding is out of scope; stub for API completeness |
| 121 | `DxtUtil` — software decompression of DXT1 / DXT3 / DXT5; used by `Texture2D::FromStream` when `SurfaceFormat` is Dxt1/3/5; reference: FNA `DxtUtil.cs` | ⬜ | Required for loading DXT textures without a GPU decompression extension |

---

## Phase 14 — Integration tests for new features

| # | Task | Status | Notes |
|---|------|--------|-------|
| 122 | Integration test: EasyGL — render with `AlphaTestEffect` (alpha cutout from texture), pixel readback, assert correct masking | ⬜ | Requires 102 |
| 123 | Integration test: EasyGL — render with `SkinnedEffect` (2 bone transforms), assert mesh deformation against reference output | ⬜ | Requires 105 |
| 124 | Integration test: Vulkan — `DrawInstancedPrimitives` with 3 instances at different positions | ⬜ | Requires 111 |
| 125 | Integration test: EasyGL/Vulkan — DXT1 texture loaded via `FromStream`, rendered, pixel readback asserts correct color | ⬜ | Requires 121 |

---

## XNA 4.0 Graphics API coverage

| Area | Current | After Phase 9–14 |
|------|---------|------------------|
| Enums, state objects, value types | ~100% | 100% |
| SpriteBatch / 2D pipeline | ~100% | 100% |
| Texture2D, VertexBuffer, IndexBuffer | ~100% | 100% |
| GraphicsDevice API surface | ~90% (non-zero offsets throw) | ~98% |
| BasicEffect → GPU (EasyGL) | ~95% | 95% |
| BasicEffect → GPU (Vulkan) | ~80% | 80% |
| BasicEffect → GPU (Bgfx) | ~0% visible | ~80% |
| AlphaTestEffect GPU | ~0% | ~85% |
| DualTextureEffect GPU | ~0% | ~85% |
| EnvironmentMapEffect GPU | ~0% | ~75% |
| SkinnedEffect GPU | ~0% | ~85% |
| SpriteBatch custom Effect | ~0% | ~80% |
| Render targets (2D, Cube, MRT) | ~90% | 90% |
| FillMode::WireFrame | ℹ️ EasyGL N/A (GLES3); ❌ Vulkan | ℹ️ EasyGL N/A; ✅ Vulkan |
| VideoPlayer | 0% | stub |
| DXT texture loading | 0% | ✅ |
| **Overall realistic coverage** | **~75%** | **~95%** |

> Remaining ~5%: advanced MSAA, GraphicsDeviceManager (intentionally omitted —
> replaced by SDL3 Game integration), .xnb content pipeline (out of scope),
> and platform-specific Bgfx renderer quirks.

---

## Open deferred items (unblocked by external factors)

| # | Blocking reason |
|---|----------------|
| 114–116 Bgfx shaders | Requires bgfx shaderc binaries and build system setup |
| 118 Vulkan per-slot sampler | Large descriptor set refactor across the entire Vulkan backend |
| 104 / 108 EnvironmentMapEffect | Cube map as input texture + reflection shader is complex |
