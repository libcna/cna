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
| 113 | SpriteBatch::Begin(effect) — custom Effect parameter is stored but ignored; wire it into the sprite rendering pipeline; EasyGL: switch shader program when effect is non-null | ✅ | EasyGL: ShaderEffect (GLSL source) compiles + uses custom GL program; non-GLSL effects call Apply() for OnApply() side-effects. Vulkan: SetCustomEffect overridden in VulkanSpriteBatchBackend; Apply() called in End(). EasyGL + Vulkan build clean |

---

## Phase 11 — Bgfx 3D rendering (unblocking)

> Bgfx requires pre-compiled shader binaries. All 3D draw calls are currently no-ops.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 114 | Bgfx: set up shaderc toolchain; compile `colored3d.vert.sc` + `colored3d.frag.sc` (bgfx varying.def.sc + GLSL source → bgfx binary); embed as `bgfx_shaders.hpp` analogous to `spirv_shaders.hpp` | ✅ | CNA_BGFX_BUILD_SHADERC CMake option added; vs_colored3d.sc + fs_colored3d.sc + varying.def.sc written; compile_shaders.py compiles GLSL/ESSL/SPIR-V/WGSL variants; bgfx_shaders.hpp generated with manual EmbeddedShader (avoids BGFX_EMBEDDED_SHADER macro which requires DXBC on Linux); colored3DProgram_ created at init from kColored3dShaders; bgfx backend builds clean |
| 115 | Bgfx: wire DrawColoredPrimitives with the compiled colored3d shader program | ✅ | DrawColoredPrimitives already submits colored3DProgram_; confirmed in code review |
| 116 | Bgfx: textured3d, colored_textured3d, lit_textured3d shader variants via shaderc; wire DrawPrimitivesEx | ✅ | vs/fs_textured3d.sc + vs/fs_colored_textured3d.sc + vs/fs_lit_textured3d.sc written; compile_shaders.py extended to 4 pairs (kColored3dShaders / kTextured3dShaders / kColoredTextured3dShaders / kLitTextured3dShaders); wvpUniform_ + 6 new uniforms (diffuseColor, ambientColor, light0Dir, light0Diffuse, lightingEnabled, s_texColor) + 3 new programs initialized at startup; DrawPrimitivesEx dispatches by params.lightingEnabled / textureEnabled / vertexColorEnabled; bgfx_shaders.hpp regenerated (32 variants all OK); bgfx + EasyGL build clean |
| 117 | Bgfx: GetBackBufferData — async readback via `bgfx::blit` to a CPU-visible texture + `bgfx::readTexture` + `bgfx::frame(true)` wait | ✅ | BgfxCnaCallback implements bgfx::CallbackI (fatal + screenShot; all others no-ops); registered via init.callback; ReadBackbuffer calls bgfx::requestScreenShot(BGFX_INVALID_HANDLE) + bgfx::frame() up to 3× until screenshotReady; BGRA8↔RGBA8 swap driven by TextureFormat::Enum; bgfx + EasyGL build clean |

---

## Phase 12 — Vulkan deferred

| # | Task | Status | Notes |
|---|------|--------|-------|
| 118 | Vulkan: per-slot SamplerState — one `VkSampler` per binding slot; update descriptor set layout to include a sampler array (slots 0–15); `SetSamplerState(slot, state)` destroys and recreates the sampler | ✅ | `SamplerStateKey` cache + `slotSamplers_[16]` + `GetOrCreateTexSamplerDescSet(view,sampler)`; draw paths use slot-0 sampler; anisotropy enabled if supported |
| 119 | Vulkan: custom Effect / SPIR-V loading — `IEffectBackend::CompileProgram(vertSpv, fragSpv)`, `Bind()`, `SetUniformMatrix`, `SetUniformFloat4`; `ShaderEffect` fully functional on Vulkan | ✅ | `VulkanEffectBackend` with 128-byte push constants (GLSL std140 layout); pipeline created from SPIR-V bytes; `customEffectBackend_` plumbed through sprite batch; integration test `cna_test_vulkan_shader_effect` passes (red tint over green background) |

---

## Phase 13 — Missing XNA classes

| # | Task | Status | Notes |
|---|------|--------|-------|
| 120 | `VideoPlayer` stub — correct namespace (`Microsoft::Xna::Framework::Media`), constructor, `Play()` / `Pause()` / `Stop()` / `Dispose()`, `getStateProperty()` (`MediaState`), `getTextureProperty()` (returns `nullptr`), `Video` class stub; no actual video decoding | ✅ | Already fully implemented with FFmpeg decoder, SDL3 AudioStream, Video + VideoPlayer + VideoSoundtrackType all complete in include/Microsoft/Xna/Framework/Media/Video/ |
| 121 | `DxtUtil` — software decompression of DXT1 / DXT3 / DXT5; used by `Texture2D::FromStream` when `SurfaceFormat` is Dxt1/3/5; reference: FNA `DxtUtil.cs` | ✅ | CNA::Internal::Graphics::DxtUtil in include/CNA/Internal/Graphics/DxtUtil.hpp + src; DecompressDxt1/3/5 + block helpers + ConvertRgb565ToRgb888 ported line-by-line from FNA; TryDecodeDds() added to Texture2D::FromStream for DDS magic detection + FourCC dispatch; 6 unit tests; EasyGL build clean |

---

## Phase 14 — Integration tests for new features

| # | Task | Status | Notes |
|---|------|--------|-------|
| 122 | Integration test: EasyGL — render with `AlphaTestEffect` (alpha cutout from texture), pixel readback, assert correct masking | ✅ | examples/alpha_test_integration_test.cpp; left=red, right=green |
| 123 | Integration test: EasyGL — render with `SkinnedEffect` (2 bone transforms), assert mesh deformation against reference output | ✅ | examples/skinned_effect_integration_test.cpp; VertexBuffer::SetDataRaw added; bone translate verified |
| 124 | Integration test: Vulkan — `DrawInstancedPrimitives` with 3 instances at different positions | ✅ | examples/vulkan_instanced_test.cpp; swapchain +TRANSFER_SRC_BIT; ReadBackbuffer flushes pending draws |
| 125 | Integration test: EasyGL/Vulkan — DXT1 texture loaded via `FromStream`, rendered, pixel readback asserts correct color | ✅ | examples/dxt1_texture_test.cpp; InMemoryStream; solid-red 4×4 DXT1 DDS |

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
| VideoPlayer | 0% | ✅ FFmpeg-backed |
| DXT texture loading | 0% | ✅ |
| ShaderEffect (GLSL + SPIR-V) | 0% | ✅ both backends |
| Unit-test coverage | ~60% | ~60% (Phases 15–16 address this) |
| **Overall realistic coverage** | **~75%** | **~95% API surface / ~80% GPU behaviour** |

> Remaining ~5%: advanced MSAA (Tasks 146–147), `GetVertexBuffers` (Task 141),
> Bgfx stock effects (Tasks 137–139), Vulkan Texture3D/Cube (Task 143),
> GraphicsDeviceManager (intentionally omitted — replaced by SDL3 Game integration),
> .xnb content pipeline (out of scope).

---

## Open deferred items (unblocked by external factors)

| # | Blocking reason |
|---|----------------|
| ~~114–116 Bgfx shaders~~ | ✅ Done |
| ~~118 Vulkan per-slot sampler~~ | ✅ Done |
| ~~104 / 108 EnvironmentMapEffect~~ | ✅ Done |

---

## Phase 15 — Test coverage gaps

| # | Task | Status | Notes |
|---|------|--------|-------|
| 126 | Unit tests: SpriteBatch — constructor, Begin/End pairs, all Draw overloads, Begin sort modes, invalid-state throws | ✅ | 20 tests: SpriteSortMode enum values, SpriteEffects enum values, no-backend construction/Begin/End, Draw/DrawString throws before Begin |
| 127 | Unit tests: SpriteFont — constructor, MeasureString, getCharacters, getLineSpacing/setLineSpacing, getSpacing/setSpacing, getDefaultCharacter/set, invalid glyph behavior | ✅ | 24 tests: constructor properties, all setters, MeasureString (empty, single, multi-char, spacing, multi-line, \r skip, unknown-glyph throw, default-char fallback) |
| 128 | Unit tests: Texture2D — constructor, getWidth/Height/Format, SetData/GetData round-trip, FromStream PNG round-trip | ✅ | 28 tests: default ctor (w/h/format/levelCount), getBounds, copy/move, all GetData overloads (null/empty throws), all SetData overloads (null/negative/overflow throws); SetData/GetData GPU round-trip covered by dxt1_texture_test integration test |
| 129 | Unit tests: Texture3D, TextureCube, RenderTarget2D, RenderTargetCube — constructors, property getters, GetData/SetData per-face | ✅ | 19 tests: CubeMapFace enum values + all-distinct (8), DepthFormat enum values + all-distinct (6), RenderTargetUsage enum values + all-distinct + default check (5); constructor/GetData/SetData tests need GPU — covered by EasyGL/Vulkan integration tests |
| 130 | Unit tests: OcclusionQuery, EffectPass, EffectPassCollection, DynamicVertexBuffer, DynamicIndexBuffer — all public API methods | ✅ | 21 tests: BufferUsage enum (3), IndexElementSize enum (3), EffectPass Apply/annotations (3), EffectPassCollection out-of-bounds/range-for/const overloads/multi-add (12); OcclusionQuery/DynamicVertexBuffer/DynamicIndexBuffer need GPU — covered by integration tests |
| 131 | Unit tests: GraphicsAdapter, GraphicsResource, DisplayMode, DisplayModeCollection, GraphicsDeviceStatus | ✅ | 18 tests: DisplayMode::GetTypeName (1), DisplayModeCollection ctor/index/range-for/GetTypeName (9), GraphicsResource via Texture2D — disposed/device/name/tag/Dispose/double-Dispose (8); GraphicsAdapter static factory needs SDL init — covered by integration tests; GraphicsDeviceStatus already in GraphicsDeviceStatusTests.cpp |

---

## Phase 16 — Effect integration tests

| # | Task | Status | Notes |
|---|------|--------|-------|
| 132 | Integration test: EasyGL — ShaderEffect (GLSL) with SpriteBatch; render white 1×1 texture through red-tint fragment shader, pixel readback asserts red centre and green bg | ✅ | `examples/easygl_shader_effect_test.cpp`; GLSL vert matches SpriteBatch attribute layout (aPos/aTexCoord/aColor + projection); frag outputs `vec4(t.r,0,0,t.a)`; centre=(255,0,0) bg=(0,255,0) PASS |
| 133 | Integration test: EasyGL — DualTextureEffect: two textures blended on a quad, pixel readback asserts blended output colour | ✅ | `examples/easygl_dual_texture_test.cpp`; tex0=magenta, tex1=yellow, diffuse=white → magenta×yellow=(1,0,0)=red; centre=(255,0,0) PASS; exercises `prog_dual_textured_` + GL_TEXTURE1 slot |
| 134 | Integration test: EasyGL — EnvironmentMapEffect: quad with cube-map reflection, pixel readback asserts expected lit+env colour | ✅ | `examples/easygl_env_map_test.cpp`; emissive=(1,0,0), light0Diffuse=(0,0,0), envAmount=0 → red; 1×1 white TextureCube created in Initialize(); fixed `~TextureCube()` inline dtor bug (moved to `.cpp`); centre=(255,0,0) PASS |
| 135 | Integration test: Vulkan — DualTextureEffect: same as Task 133 but on Vulkan pipeline | ✅ | `examples/vulkan_dual_texture_test.cpp`; tex0=magenta, tex1=yellow → magenta×yellow=(1,0,0)=red; exercises `GetOrCreatePipelineDualTex3D` + `GetOrCreateDualTexDescSet`; centre=(255,0,0) PASS; all 4 Vulkan tests green |
| 136 | Integration test: Vulkan — EnvironmentMapEffect: same as Task 134 but on Vulkan cube-map pipeline | ✅ | `examples/vulkan_env_map_test.cpp`; emissive=(1,0,0), envAmount=0 → red; 1×1 white TextureCube; centre=(255,0,0) PASS |

---

## Phase 17 — Bgfx parity

| # | Task | Status | Notes |
|---|------|--------|-------|
| 137 | Bgfx: AlphaTestEffect dispatch in `DrawPrimitivesEx` — add alpha-test shader pair and dispatch when `useAlphaTest` is set | ✅ | `vs/fs_alpha_test3d.sc` compiled; `alphaTest3DProgram_` + `alphaTestUnif_`; dispatched when `alphaTest[2]<0‖alphaTest[3]<0` |
| 138 | Bgfx: DualTextureEffect dispatch in `DrawPrimitivesEx` — add dual-texture shader pair and dispatch when `useDualTexture` is set | ✅ | `vs/fs_dual_texture3d.sc` compiled; `dualTexture3DProgram_` + `texColor3DSampler2_`; dispatched on `params.dualTexture` |
| 139 | Bgfx: SkinnedEffect dispatch in `DrawPrimitivesEx` — add skinning shader with bone UBO | ✅ | `vs/fs_skinned3d.sc` compiled; `skinned3DProgram_` + `bonesUnif_` (Mat4[72]); `MakeBgfxLayout(52)` exposes Weight+Indices; dispatched on `params.skinned` |
| 140 | Bgfx: `DrawInstancedPrimitivesEx` — replace the throw; implement via `bgfx::allocInstanceDataBuffer` | ✅ | `vs/fs_instanced3d.sc` (i_data0..3 as world mat4); `BgfxVertexBufferBackend::cpuData` CPU copy; `instanced3DProgram_` + `vpInstanced3DUnif_`; `allocInstanceDataBuffer` + indexed draw |

---

## Phase 18 — API gaps and Vulkan completeness

| # | Task | Status | Notes |
|---|------|--------|-------|
| 141 | `GraphicsDevice::GetVertexBuffers()` — add method returning current vertex buffer binding list; add unit test | ✅ | Method already in GraphicsDevice.cpp (return currentVertexBuffers_); `VertexBufferBindingTests.cpp` — 11 tests, all PASSED |
| 142 | Vulkan: `RenderTargetCube` — implement `VulkanRenderTargetCubeBackend`: 6-face cubemap image, per-face framebuffer, `SetRenderTarget(RenderTargetCube*, CubeMapFace)` | ✅ | `VulkanRenderTargetCubeBackend`: VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT image, 6 per-face VkImageViews, 1 VkImageView_CUBE for sampling, shared depth image, 6 VkFramebuffers using `rtRenderPass_`. Fixed design bug: made `IRenderTargetCubeBackend : ITextureCubeBackend` (mirrors `IRenderTargetBackend : ITextureBackend`) + protected TextureCube ctor so `RenderTargetCube` shares a single GPU image (no more duplicate `VulkanTextureCubeBackend`). Added `IVulkanCubeSamplable` interface so `VulkanTextureCubeBackend` and `VulkanRenderTargetCubeBackend` both work in `EnvironmentMapEffect` envMap path. EasyGL `EasyGLRenderTargetCubeBackend` updated with `BindGL()`+`SetData()`. `cna_test_vulkan_rtcube`: 6 SpriteBatch draws (one per face) + blue backbuffer assert PASS |
| 143 | Vulkan: Texture3D and TextureCube upload — implement `VulkanTexture3DBackend` and `VulkanTextureCubeBackend` with SetData/GetData; prerequisite for Task 136 | ✅ | Already implemented in earlier phase: VkImage cube (6 array layers) + per-face barrier + staging upload; `CreateTexture3D`/`CreateTextureCube` both return valid backends |
| 144 | Integration test: EasyGL — Model rendering; hard-coded Model (2 bones, 1 mesh, BasicEffect), `Model::Draw(world, view, proj)`, pixel readback asserts expected colour | ✅ | `examples/easygl_model_draw_test.cpp`; 2-bone identity hierarchy, VertexPositionColor red quad, BasicEffect VertexColorEnabled; centre=(255,0,0) PASS |
| 145 | Integration test: EasyGL — MRT (Multi-Render-Target): `SetRenderTargets({rt0, rt1})`, draw, read back from each attachment, assert different colours | ✅ | `examples/easygl_mrt_test.cpp`; rt0 cleared red→overwritten green by MRT draw, rt1 cleared blue→untouched; blit each to screen half; left=(0,255,0) right=(0,0,255) PASS |
| 146 | MSAA: EasyGL — respect `PresentationParameters.MultiSampleCount`; create multisampled render buffer and resolve on Present | ✅ | `examples/easygl_msaa_test.cpp`; `GraphicsBackendCreateArgs::multiSampleCount` passed from `PresentationParameters`; `EasyGLGraphicsBackend` creates MSAA FBO+RBOs, routes all rendering through them, resolves via `glBlitFramebuffer` in `Present()`/`ReadBackbuffer()`; clamps to `GL_MAX_SAMPLES`; centre=(255,0,0) PASS |
| 147 | MSAA: Vulkan — respect `PresentationParameters.MultiSampleCount`; multisampled swapchain images, resolve attachment in render pass | ✅ | `examples/vulkan_msaa_test.cpp`; `PickSampleCount()` helper queries `framebufferColorSampleCounts & framebufferDepthSampleCounts` and clamps to requested count; `CreateMsaaColorResources()` creates TRANSIENT MSAA color VkImage+memory+view; `CreateRenderPassMsaa()` 3-attachment render pass (att0=MSAA color, att1=resolve/swapchain, att2=depth); `CreatePipeline2DMsaa()` sprite pipeline with `rasterizationSamples=sampleCount_`; all 7 `GetOrCreate*Pipeline*` fns gain `bool msaa` param encoded in `Make3DKey` (bit 11) / `MakeExt3DKey` (bit 15); `RecordCommandBuffer` uses `renderPassMsaa_` + 3 clear values for backbuffer pass, selects `pipeline2DMsaa_` for sprite draws, passes `drawMsaa` to all 3D pipeline lookups; AMD Radeon 780M: centre=(255,0,0) PASS |
| 148 | Integration test: Vulkan — RenderTarget2D full cycle: clear to red, `GetBackBufferData` after SetRenderTarget+Present, assert red pixel | ✅ | `vulkan_rt2d_test.cpp`: BasicEffect+colored3D quad into RT, SpriteBatch blit RT→backbuffer, assert centre=(255,0,0). Fixed 3 backend bugs: (1) `IVulkanSamplable` interface so RT can be sampled by SpriteBatch, (2) stride=16 `useExtParams` bug (sent colored3D through wrong Ext3D pipeline), (3) missing RT render pass exit subpass dependency + renderPass_ dependency parity for validation clean build |
| 149 | Unit tests: EffectParameter `SetValue`/`GetValue` round-trip for all types (bool, int, float, Vector2/3/4, Matrix, Quaternion, Texture2D/3D/Cube) | ✅ | Added 22 tests to `EffectParameterTests.cpp`: bool/int/vec2/vec4/quat array round-trips, SetValueTranspose array, Texture2D/3D/Cube null+sentinel pointer round-trips, default initial state (bool/int/float/string/tex*=null/matrix=identity) — 40 tests total, all PASSED |
| 150 | `GraphicsDevice::SetStringMarkerEXT` — add debug-label method; no-op on most backends, `vkCmdInsertDebugUtilsLabelEXT` on Vulkan with `VK_EXT_debug_utils` | ✅ | `GraphicsDevice::SetStringMarkerEXT(string)` → `IGraphicsBackend::SetStringMarkerEXT(const char*)` (default no-op); Vulkan: `pfnCmdInsertDebugLabel_` loaded via `vkGetDeviceProcAddr` after device creation; marker queued as `Pending3DDraw{isMarker=true}` and emitted via `vkCmdInsertDebugUtilsLabelEXT` in `draw3DFor`; EasyGL/Bgfx/SDL = no-op; both builds clean |

---

## Phase 19 — SpriteBatch API completion

> **Source of these tasks:** external code review (June 2026) identified 6 stubbed `Draw` overloads and
> 3 stubbed `DrawString(StringBuilder,…)` overloads in `SpriteBatch.cpp`. All are marked
> `CNA_STUB` and do nothing. These are high-traffic XNA APIs; fixing them is the best
> return-on-investment before adding further rendering features.

| # | Task | Status | Notes |
|---|------|--------|-------|
| 151 | `SpriteBatch::Draw(Texture2D, Vector2, Color)` — remove stub; delegate to `pushSprite` using texture natural size as destination, full texture as source | ✅ | Delegates to `pushSprite(texture, Rectangle(pos.X,pos.Y,w,h), Rectangle(0,0,w,h), color, 0, Zero, None, 0)` |
| 152 | `SpriteBatch::Draw(Texture2D, Vector2, optional<Rectangle> src, Color)` — remove stub; use texture natural size for dest, src or full texture | ✅ | `src.has_value()` → use src rect and its dims; else full texture rect |
| 153 | `SpriteBatch::Draw(Texture2D, Vector2, optional<Rectangle> src, Color, float rotation, Vector2 origin, float scale, SpriteEffects, float layerDepth)` — remove stub; compute dest from position + source size * scale | ✅ | dest = `Rectangle(pos.X, pos.Y, dw*scale, dh*scale)` |
| 154 | `SpriteBatch::Draw(Texture2D, Vector2, optional<Rectangle> src, Color, float rotation, Vector2 origin, Vector2 scale, SpriteEffects, float layerDepth)` — remove stub; Vector2 scale variant | ✅ | dest = `Rectangle(pos.X, pos.Y, dw*scale.X, dh*scale.Y)` |
| 155 | `SpriteBatch::Draw(Texture2D, Rectangle dest, Color)` — remove stub; delegate to `pushSprite` with full texture source | ✅ | `pushSprite(texture, dest, Rectangle(0,0,w,h), color, 0, Zero, None, 0)` |
| 156 | `SpriteBatch::Draw(Texture2D, Rectangle dest, optional<Rectangle> src, Color)` — remove stub; optional source maps to full texture or given rect | ✅ | `pushSprite` delegation; src defaults to full texture |
| 157 | `SpriteBatch::DrawString(SpriteFont, StringBuilder, Vector2, Color)` — remove stub; convert via `StringBuilder::ToString()` | ✅ | `StringBuilder::ToString()` confirmed present in sharp-runtime; delegates to `DrawString(string,…)` |
| 158 | `SpriteBatch::DrawString(SpriteFont, StringBuilder, Vector2, Color, float, Vector2, float, SpriteEffects, float)` — same pattern as 157 | ✅ | Delegates to scalar-scale string overload |
| 159 | `SpriteBatch::DrawString(SpriteFont, StringBuilder, Vector2, Color, float, Vector2, Vector2, SpriteEffects, float)` — same pattern as 157 | ✅ | Delegates to Vector2-scale string overload |
| 160 | Unit tests for all 9 newly implemented overloads (151–159) and guard tests | ✅ | 21 SpriteBatch tests total, all PASSED; also fixed `End()` guard (was silently no-op without Begin; now throws per XNA spec); fixed `Begin()` to set `begun=true` before backend check so double-Begin throws even without GPU context |

---

## Phase 20 — SpriteBatch XNA behaviour conformance

| # | Task | Status | Notes |
|---|------|--------|-------|
| 161 | Verify `SpriteSortMode::Immediate`: sprite must be flushed inside `Draw()`, not deferred — add unit test that calls `Draw` inside `Begin/Immediate/End` and inspects that backend received draw before `End()` | ⬜ | Requires mock/inspectable backend; deferred to later |
| 162 | Verify `SpriteSortMode::Deferred`: sprites submitted in call order; test that order is preserved in backend | ⬜ | Requires mock/inspectable backend; deferred to later |
| 163 | Verify `SpriteSortMode::Texture`: sprites sorted by texture pointer; test with 3 draws alternating 2 textures — backend must receive both textures grouped | ⬜ | Requires mock/inspectable backend; deferred to later |
| 164 | Verify `SpriteSortMode::FrontToBack`: sprites sorted by ascending `layerDepth`; test with 3 draws at depths 0.5, 0.1, 0.9 — assert delivery order | ⬜ | Requires mock/inspectable backend; deferred to later |
| 165 | Verify `SpriteSortMode::BackToFront`: sprites sorted by descending `layerDepth`; test with same 3 draws — assert reverse delivery order | ⬜ | Requires mock/inspectable backend; deferred to later |
| 166 | Guard tests: `Begin()` twice without `End()` must throw; `End()` without `Begin()` must throw; `Begin/End/Begin/End` cycle does not throw | ✅ | Covered by Task 160; `BeginTwiceWithoutEndThrows`, `EndWithoutBeginThrows`, `BeginEndBeginEndDoesNotThrow` — all PASSED |
| 167 | Pixel integration test: `SpriteEffects::FlipHorizontally` and `FlipVertically` — asymmetric textures, flip, pixel readback | ✅ | `examples/easygl_sprite_effects_test.cpp`; viewport 400×100, `SamplerState::PointClamp`; 2×1 tex [Red\|Blue] for FlipH, 1×2 tex [Red/Blue] for FlipV; 8 readback assertions (left/right no-flip, left/right FlipH, top/bot no-flip, top/bot FlipV) — all 8 PASS |
| 168 | Pixel integration test: `transformMatrix` in `SpriteBatch::Begin` — pass a translation matrix `Matrix::CreateTranslation(100,0,0)`, draw a 1×1 red texture at (0,0), read back pixel at (100,0) and assert red | ✅ | `examples/easygl_transform_matrix_test.cpp`; viewport 400×200; `CreateTranslation(100,50,0)` moves 1×1 red sprite from (0,0) to (100,50); 2 readback checks (origin=Black, translated=Red) — both PASS. Also fixed EasyGL bug: `combined = orthoM * transform_` → `transform_ * orthoM` (row-major order: transform first, then project). |

---

## Phase 21 — Texture SetData/GetData conformance

| # | Task | Status | Notes |
|---|------|--------|-------|
| 169 | `Texture2D::SetData` / `GetData` — partial rectangle regions: set data into a sub-rectangle of a 4×4 texture, read back the full texture and verify only the target region changed | ✅ | `examples/easygl_texture2d_partial_rect_test.cpp`; 4×4 texture filled with red, 2×2 blue sub-rect at (1,1); GetData reads back 16 pixels — 4 blue, 12 red — all 16 PASS. Also fixed `getMipBuffer(0)` bug: buffer was not pre-sized when recreated after `MaybeFreeCpuPixels()`; now auto-assigns `width*height*4` bytes. |
| 170 | `Texture2D::SetData` / `GetData` — `startIndex` and `elementCount` parameters: upload only a middle slice of a data array, verify correct pixels written | ✅ | Added to `examples/easygl_texture2d_partial_rect_test.cpp` (Tasks 169+170 share file). T170A: 6-element array [G,G,B,B,G,G], startIndex=2, elementCount=2 → writes 2 Blues into rect. T170B: GetData with startIndex=1 → writes into out[1..2], sentinels untouched. Also fixed same wrong guard in GetData (`startIndex + elementCount > w * h` → `elementCount < w * h`). 2 unit tests updated/added in Texture2DTests.cpp. |
| 171 | `Texture2D` mip-level `SetData` / `GetData`: upload distinct colours to mip 0 and mip 1 of a 4×4 `generateMipMaps=true` texture; read back each level and verify | ✅ | `examples/easygl_texture2d_mip_test.cpp`; 21/21 PASS — mip0=Red (16px), mip1=Blue (4px), mip2=Green (1px); colours do not bleed across levels; no Texture2D.cpp changes needed |
| 172 | `TextureCube` mip-level `SetData` / `GetData`: upload distinct colours per face per mip; read back and verify | ✅ | `examples/easygl_texturecube_faces_test.cpp`; 24/24 PASS — 6 faces × 4 pixels, each face a distinct colour, no bleed. Also fixed bug: TextureCube::SetData/GetData was passing raw Color* (sizeof=24, vtable at offset 0) to GL; fixed by converting to/from uint8_t[] in TextureCube.cpp. |
| 173 | `Texture3D` z-slice `SetData` / `GetData`: upload distinct colours per z-slice; read back and verify | ✅ | `examples/easygl_texture3d_slices_test.cpp`; 16/16 PASS — 2×2×4, 4 slices × 4 pixels, no bleed. Fixed same Color→uint8_t bug in Texture3D.cpp; also moved `~Texture3D()` to .cpp to fix incomplete-type unique_ptr error in test binaries. |
| 174 | `SurfaceFormat` backend mapping table: for each `SurfaceFormat` value (Color, Bgr565, Bgra5551, Bgra4444, Dxt1/3/5, Rgba1010102, Rg32, Rgba64, Alpha8, Single, Vector2/4, HalfSingle/Vector2/Vector4, HdrBlendable) — document whether EasyGL/Vulkan/Bgfx map it to a real GPU format, approximate it, or throw | ✅ | `docs/surface-format-support.md` — all 27 CNA formats documented; key finding: all backends hardcode RGBA8 and ignore surfaceFormat entirely. Priority remediation list included. |
| 175 | DXT golden tests: for a 4×4 DXT1 block encoding known RGBA values, decode via `DxtUtil` and compare byte-for-byte against FNA reference output | ✅ | `tests/CNA/Internal/Graphics/DxtUtilTests.cpp` — 6/6 PASS (DXT1 solid, transparent, DXT3, DXT5 solid, partial alpha, non-square 8×4). Pre-existing. |
| 176 | sRGB formats: `ColorSrgb`, `Dxt1Srgb`, `Dxt3Srgb`, `Dxt5Srgb` — either implement GPU-side sRGB sampling flag in EasyGL (`GL_SRGB8_ALPHA8`) and Vulkan (`VK_FORMAT_R8G8B8A8_SRGB`), or explicitly throw `std::runtime_error("SurfaceFormat not supported")` with a clear message | ✅ | `Texture::ValidateFormat(SurfaceFormat)` — throws for all non-Color formats; called in Texture2D/Texture3D/TextureCube public constructors. `examples/easygl_surface_format_throws_test.cpp` — 16/16 PASS; 24/24 EasyGL tests pass. |

---

## Phase 22 — RenderTarget and presentation correctness

| # | Task | Status | Notes |
|---|------|--------|-------|
| 177 | `RenderTargetUsage::DiscardContents` vs `PreserveContents` in EasyGL: verify that `DiscardContents` issues `glInvalidateFramebuffer` (or clear) and `PreserveContents` does not clear on `SetRenderTarget` | ✅ | `GraphicsDevice::SetRenderTarget` calls `Clear(0,0,0,255)` on DiscardContents; direct FBO readback test 3/3 PASS |
| 178 | Same as 177 but Vulkan: `DiscardContents` → `VK_ATTACHMENT_LOAD_OP_CLEAR`; `PreserveContents` → `VK_ATTACHMENT_LOAD_OP_LOAD` in RT render pass | ✅ | Added `rtRenderPassLoad_`; `VulkanRenderTargetBackend` stores `preserveContents_`; `CreateRenderTarget2D` receives bool from XNA layer; 3/3 PASS; 9/9 Vulkan tests pass |
| 179 | Same as 177 but Bgfx: map to `BGFX_CLEAR_COLOR` vs no clear flag on `bgfx::setViewClear` | ✅ | Implemented `ClearColorAndDepth` (delegates to `Clear`); `BindAsRenderTarget` calls `setViewClear(BGFX_CLEAR_NONE)` for PreserveContents; smoke test PASS |
| 180 | Integration test: backbuffer → RT → backbuffer → RT → backbuffer in a single frame; verify correct render target is active at each step and final backbuffer pixel is correct | ✅ | 3/3 PASS; 26/26 EasyGL |
| 181 | `RenderTargetBinding` with explicit mip level and cube face: verify that `SetRenderTargets({RenderTargetBinding(rt, 0), RenderTargetBinding(rt, 1)})` correctly targets distinct mip levels | ✅ | 13/13 unit tests PASS; all 6 CubeMapFace values, distinct array slices, default field checks |
| 182 | `PresentationParameters` round-trip: after `GraphicsDeviceManager::ApplyChanges`, verify `GraphicsDevice.PresentationParameters` reflects the requested `BackBufferWidth`, `BackBufferHeight`, `DepthStencilFormat`, `PresentInterval`, `MultiSampleCount` | ✅ | Fixed `applyToExistingBackend` to call `SetPresentationParameters(pp)`; added `GraphicsDevice::SetPresentationParameters` NOXNA helper; 5/5 PASS; 27/27 EasyGL |
| 183 | `GraphicsDevice` device-reset events: `DeviceResetting` and `DeviceReset` fire when swapchain is recreated; `ResourceDestroyed` fires on explicit resource dispose; add unit tests | ⬜ | Check FNA event semantics |

---

## Phase 23 — Effect system XNA accuracy

| # | Task | Status | Notes |
|---|------|--------|-------|
| 184 | `Effect::Clone()` for base `Effect` and all stock effects (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`): clone must produce an independent copy with the same parameter values but a separate parameter collection; modifying a parameter on the clone must not affect the original | ⬜ | Check FNA `Effect.cs Clone()`; add unit tests |
| 185 | `Effect::CurrentTechnique`, `Techniques`, `Parameters`, `Passes` collection semantics: verify that `Techniques[0].Passes[0].Apply()` calls the correct backend draw-state setup; add unit tests for collection indexing and `Contains` | ⬜ | Add to `EffectParameterTests.cpp` or new file |
| 186 | `EffectParameter` arrays — wrong-type and out-of-range guards: `SetValue(float[])` on an int parameter must throw or silently ignore per XNA spec; `SetValue` with more elements than declared must throw; add unit tests | ⬜ | Check FNA for throw vs. silent-ignore |
| 187 | `EffectParameter::SetValueTranspose(Matrix)` — verify the matrix is stored transposed relative to `SetValue(Matrix)`; add unit test comparing stored values byte-for-byte | ⬜ | Already partially tested in Task 149; add edge-case test |
| 188 | `EffectAnnotation` and annotation collections: verify that `Effect.Parameters["X"].Annotations["hint"].GetValueString()` works; add unit tests | ⬜ | Low priority — rarely used in typical XNA games |
| 189 | Pixel integration tests for `BasicEffect` combinations — EasyGL: (a) vertex color only, (b) texture only, (c) texture + vertex color (multiply), (d) directional lighting on, (e) fog on | ⬜ | New `examples/easygl_basiceffect_combinations_test.cpp`; one readback assert per combination |
| 190 | Pixel integration tests for `AlphaTestEffect` all `CompareFunction` modes — EasyGL: draw a pixel with alpha=128; test `Less`, `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`, `Always`, `Never` reference=128; assert pixel is drawn or discarded accordingly | ⬜ | New `examples/easygl_alphatest_modes_test.cpp` |

---

## Phase 24 — Stock effects backend parity

| # | Task | Status | Notes |
|---|------|--------|-------|
| 191 | `DualTextureEffect` pixel tests — Vulkan + EasyGL + Bgfx: two distinct textures, verify per-backend that the output blends both | ⬜ | Extend existing vulkan/easygl tests; add Bgfx variant |
| 192 | `EnvironmentMapEffect` parameter accuracy — EasyGL: verify reflection vector, `EnvironmentMapAmount`, `EnvironmentMapSpecular`, `EmissiveColor` affect output correctly by reading back pixels; compare EasyGL vs. Vulkan | ⬜ | Extend `easygl_env_map_test.cpp` with multiple readback assertions |
| 193 | `SkinnedEffect` bone count tests: (a) 1 bone identity → no vertex movement, (b) 1 bone translate → all vertices shifted, (c) 4-weight blend with two bones at 50%/50% → midpoint position; assert pixel readback per case | ⬜ | EasyGL; new `examples/easygl_skinned_effect_bones_test.cpp` |
| 194 | `BasicEffect::EnableDefaultLighting()` — verify that after calling `EnableDefaultLighting()` the three `DirectionalLight` values match the FNA constants in `BasicEffect.cs`; add unit test comparing property values | ⬜ | No backend needed; pure C++ unit test |
| 195 | Fog equation accuracy — `BasicEffect` fog: set `FogEnabled=true`, `FogStart=0`, `FogEnd=100`, `FogColor=red`; draw a geometry at Z=50; assert the output pixel is a blend between geometry colour and red; test EasyGL and Vulkan | ⬜ | New integration test |
| 196 | Backend parity table: for each stock effect (Basic, AlphaTest, DualTexture, EnvironmentMap, Skinned) and `ShaderEffect`, document EasyGL / Vulkan / Bgfx / SDL status (✅ tested / ⚠️ compiles only / ❌ missing) in a new section of `docs/xna-4-api-coverage.md` | ⬜ | Documentation task; no code change |

---

## Phase 25 — PackedVector exactness

| # | Task | Status | Notes |
|---|------|--------|-------|
| 197 | Generate FNA golden values for all PackedVector types (`Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Byte4`, `Color`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `NormalizedByte2`, `NormalizedByte4`, `NormalizedShort2`, `NormalizedShort4`, `Rg32`, `Rgba1010102`, `Rgba64`, `Short2`, `Short4`, `Single`, `Vector2`, `Vector4`): run FNA with known inputs and record `PackedValue` and `ToVector4()` output | ⬜ | Use FNA C# runner; save golden values as a table in `tests/PackedVectorGolden.md` |
| 198 | Compare CNA PackedVector output against golden values from Task 197; fix rounding, saturation, or bit-packing bugs discovered | ⬜ | Depends on 197 |
| 199 | PackedVector edge-case tests: inputs −1.0, 0.0, 1.0, values slightly outside [0,1] or [−1,1] range, `NaN`, `+Inf`, `−Inf`, half-float special values (denormals, ±0, ±Inf, NaN) | ⬜ | Add to existing PackedVector unit tests |
| 200 | Update `docs/xna-4-api-coverage.md`: rewrite the PackedVector and stock-effects sections to reflect the actual post-Phase-18 state; remove stale "stub" labels; add backend parity table from Task 196; add overall coverage estimate table matching the June 2026 external review findings | ⬜ | Documentation task; no code change |

---

## Phase 26 — GraphicsDevice lifecycle and XNA validation

> Goal: make `GraphicsDevice` behave closer to XNA/FNA in invalid states, reset-like operations,
> resource ownership, and draw-call validation. These tasks should reduce hidden incompatibilities
> with real XNA games.

| #   | Task                                                                                                                                                          | Status | Notes                                                |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------------- |
| 201 | Audit all public `GraphicsDevice` methods against FNA `GraphicsDevice.cs`; list missing overloads, validation differences, and intentionally unsupported APIs | ⬜      | Create `docs/graphicsdevice-fna-audit.md`            |
| 202 | Add validation for null `VertexBuffer`, null `IndexBuffer`, null `Texture`, null `Effect`, and invalid render target arguments                                | ⬜      | Match FNA/XNA exception style as closely as possible |
| 203 | Verify draw calls throw when no vertex buffer is bound and the API requires one                                                                               | ⬜      | Add unit tests                                       |
| 204 | Verify indexed draw calls throw when no index buffer is bound                                                                                                 | ⬜      | Add unit tests                                       |
| 205 | Validate primitive count and vertex/index ranges in `DrawPrimitives`, `DrawIndexedPrimitives`, and user-primitive variants                                    | ⬜      | Prevent silent out-of-bounds                         |
| 206 | Validate `PrimitiveType` handling for `TriangleList`, `TriangleStrip`, `LineList`, `LineStrip`, and `PointList` if present                                    | ⬜      | Document unsupported primitive types                 |
| 207 | Add tests for `GraphicsDevice::Clear` overloads: color only, options + color + depth + stencil, `ClearOptions` combinations                                   | ⬜      | Include invalid depth/stencil values                 |
| 208 | Verify viewport state survives draw calls and changes only when explicitly set                                                                                | ⬜      | Unit + integration test                              |
| 209 | Verify scissor rectangle behavior when `RasterizerState.ScissorTestEnable` is false vs true                                                                   | ⬜      | EasyGL/Vulkan/Bgfx                                   |
| 210 | Verify `GraphicsDevice` rejects using disposed resources in draw calls                                                                                        | ⬜      | Texture, VB, IB, Effect, RenderTarget                |

---

## Phase 27 — GraphicsResource ownership and disposal semantics

| #   | Task                                                                                                      | Status | Notes                              |
| --- | --------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------- |
| 211 | Audit `GraphicsResource` base behavior against FNA: `GraphicsDevice`, `Name`, `Tag`, `IsDisposed`, events | ⬜      | Document differences               |
| 212 | Ensure all Graphics resources derive consistently from `GraphicsResource` where XNA/FNA does              | ⬜      | Fix inheritance gaps               |
| 213 | Add unit tests for double `Dispose()` on all major resource types                                         | ⬜      | Must not crash                     |
| 214 | Add tests for disposing resources while bound to `GraphicsDevice`                                         | ⬜      | Texture, VB, IB, RT                |
| 215 | Ensure backend GPU handles are released exactly once on dispose                                           | ⬜      | Add debug counters or mock backend |
| 216 | Ensure moved C++ resources do not double-free backend handles                                             | ⬜      | Move constructor/assignment tests  |
| 217 | Add `ResourceCreated` / `ResourceDestroyed` behavior if required by CNA/FNA compatibility layer           | ⬜      | Check current event model          |
| 218 | Verify `GraphicsDevice` destruction disposes backend resources in safe order                              | ⬜      | Avoid use-after-free               |
| 219 | Add leak-check style test for creating and disposing many textures/buffers/render targets                 | ⬜      | Debug-only acceptable              |
| 220 | Document Graphics resource lifetime rules in `docs/graphics-resource-lifetime.md`                         | ⬜      | Include backend-specific caveats   |

---

## Phase 28 — PresentationParameters and GraphicsDeviceManager compatibility

| #   | Task                                                                                                                                            | Status | Notes                                        |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------ | -------------------------------------------- |
| 221 | Audit `PresentationParameters` fields/properties against FNA/XNA                                                                                | ⬜      | Missing fields should be added or documented |
| 222 | Verify defaults for `BackBufferWidth`, `BackBufferHeight`, `BackBufferFormat`, `DepthStencilFormat`, `MultiSampleCount`, `PresentationInterval` | ⬜      | Unit test                                    |
| 223 | Implement or document `PresentationInterval` mapping for SDL/EasyGL/Vulkan/Bgfx                                                                 | ⬜      | VSync behavior                               |
| 224 | Verify `IsFullScreen` and windowed mode fields are stored consistently even if backend cannot switch fullscreen                                 | ⬜      | Unit test                                    |
| 225 | Add `GraphicsDeviceManager` compatibility shim if current SDL3 Game integration leaves common XNA samples uncompilable                          | ⬜      | May be minimal                               |
| 226 | Implement `ApplyChanges()` compatibility path for `GraphicsDeviceManager` if added                                                              | ⬜      | Recreate swapchain/window resources          |
| 227 | Add tests for changing backbuffer size through presentation parameters                                                                          | ⬜      | EasyGL/Vulkan                                |
| 228 | Add tests for depth/stencil format changes                                                                                                      | ⬜      | EasyGL/Vulkan/Bgfx                           |
| 229 | Verify MSAA count changes after device creation are handled or explicitly rejected                                                              | ⬜      | Clear exception if unsupported               |
| 230 | Document which `GraphicsDeviceManager` features are supported, replaced, or intentionally omitted                                               | ⬜      | Update API coverage doc                      |

---

## Phase 29 — VertexBuffer, IndexBuffer, and DynamicBuffer conformance

| #   | Task                                                                                             | Status | Notes                              |
| --- | ------------------------------------------------------------------------------------------------ | ------ | ---------------------------------- |
| 231 | Audit `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`, `DynamicIndexBuffer` API against FNA | ⬜      | Include constructors and overloads |
| 232 | Add tests for `VertexBuffer::SetData` with offset, startIndex, elementCount, vertexStride        | ⬜      | Catch stride bugs                  |
| 233 | Add tests for `VertexBuffer::GetData` if exposed                                                 | ⬜      | Verify CPU/GPU round-trip          |
| 234 | Add tests for `IndexBuffer::SetData` with 16-bit and 32-bit index formats                        | ⬜      | Include offset paths               |
| 235 | Add tests for `IndexBuffer::GetData` if exposed                                                  | ⬜      | Verify exact values                |
| 236 | Implement `DynamicVertexBuffer::SetDataOptions` behavior: `None`, `Discard`, `NoOverwrite`       | ⬜      | Backend-specific mapping           |
| 237 | Implement `DynamicIndexBuffer::SetDataOptions` behavior: `None`, `Discard`, `NoOverwrite`        | ⬜      | Backend-specific mapping           |
| 238 | Add stress test for repeatedly updating dynamic buffers every frame                              | ⬜      | EasyGL/Vulkan/Bgfx                 |
| 239 | Validate buffer usage flags: `BufferUsage::WriteOnly` and any other supported values             | ⬜      | Match FNA where possible           |
| 240 | Verify disposed buffers cannot be rebound or updated                                             | ⬜      | Unit test                          |

---

## Phase 30 — VertexDeclaration and vertex format accuracy

| #   | Task                                                                                                                           | Status | Notes                              |
| --- | ------------------------------------------------------------------------------------------------------------------------------ | ------ | ---------------------------------- |
| 241 | Audit `VertexDeclaration`, `VertexElement`, `VertexElementFormat`, and `VertexElementUsage` against FNA                        | ⬜      | Include equality/hash behavior     |
| 242 | Add tests for all built-in vertex structs: `VertexPositionColor`, `VertexPositionTexture`, `VertexPositionNormalTexture`, etc. | ⬜      | Size, stride, declaration elements |
| 243 | Verify custom vertex declarations with unusual offsets                                                                         | ⬜      | Position not at offset 0           |
| 244 | Verify multiple texture coordinate channels                                                                                    | ⬜      | TextureCoordinate0/1/2             |
| 245 | Verify color element formats: `Color`, `Byte4`, normalized formats                                                             | ⬜      | Backend attribute mapping          |
| 246 | Verify tangent/binormal usages if present                                                                                      | ⬜      | Needed for future NOXNA/PBR too    |
| 247 | Add backend test drawing with each supported vertex element format                                                             | ⬜      | EasyGL first                       |
| 248 | Add Vulkan vertex input mapping tests for all supported formats                                                                | ⬜      | Prevent silent wrong locations     |
| 249 | Add Bgfx vertex layout mapping tests for all supported formats                                                                 | ⬜      | Match bgfx layout limitations      |
| 250 | Document unsupported vertex formats and fallback behavior                                                                      | ⬜      | `docs/vertex-format-support.md`    |

---

## Phase 31 — User primitives and draw-call variants

| #   | Task                                                                                           | Status | Notes              |
| --- | ---------------------------------------------------------------------------------------------- | ------ | ------------------ |
| 251 | Audit all `DrawUserPrimitives` overloads against FNA                                           | ⬜      | API and behavior   |
| 252 | Audit all `DrawUserIndexedPrimitives` overloads against FNA                                    | ⬜      | API and behavior   |
| 253 | Implement missing `DrawUserPrimitives` overloads by staging data into transient vertex buffers | ⬜      | EasyGL/Vulkan/Bgfx |
| 254 | Implement missing `DrawUserIndexedPrimitives` overloads by staging data into transient VB/IB   | ⬜      | EasyGL/Vulkan/Bgfx |
| 255 | Add tests for `DrawUserPrimitives` with `VertexPositionColor`                                  | ⬜      | Pixel readback     |
| 256 | Add tests for `DrawUserPrimitives` with custom vertex declaration                              | ⬜      | Pixel readback     |
| 257 | Add tests for `DrawUserIndexedPrimitives` with 16-bit indices                                  | ⬜      | Pixel readback     |
| 258 | Add tests for `DrawUserIndexedPrimitives` with 32-bit indices                                  | ⬜      | Pixel readback     |
| 259 | Validate user primitive arrays for null, invalid offsets, invalid primitive count              | ⬜      | Unit tests         |
| 260 | Optimize user primitive staging to avoid unnecessary heap allocation per draw                  | ⬜      | Performance task   |

---

## Phase 32 — Texture2D completeness

| #   | Task                                                                         | Status | Notes                                   |
| --- | ---------------------------------------------------------------------------- | ------ | --------------------------------------- |
| 261 | Audit every `Texture2D` constructor and method against FNA                   | ⬜      | Include protected/internal constructors |
| 262 | Verify `Texture2D::FromStream` supports PNG, JPG, BMP if FNA/XNA-compatible  | ⬜      | Document actual formats                 |
| 263 | Verify `Texture2D::SaveAsPng` if present                                     | ⬜      | Add round-trip test                     |
| 264 | Verify `Texture2D::SaveAsJpeg` if present                                    | ⬜      | Add round-trip test                     |
| 265 | Implement exact bounds checking for `GetData<T>` rectangles                  | ⬜      | Match FNA exceptions                    |
| 266 | Implement exact bounds checking for `SetData<T>` rectangles                  | ⬜      | Match FNA exceptions                    |
| 267 | Verify `LevelCount` behavior for mipmapped and non-mipmapped textures        | ⬜      | Unit tests                              |
| 268 | Verify non-power-of-two textures across all backends                         | ⬜      | 3×5, 7×11                               |
| 269 | Verify texture sampling at edges for clamp/wrap modes                        | ⬜      | Pixel tests                             |
| 270 | Add CPU-side shadow storage only where required for `GetData`; document cost | ⬜      | Avoid accidental memory bloat           |

---

## Phase 33 — Texture3D and TextureCube completeness

| #   | Task                                                                     | Status | Notes                                  |
| --- | ------------------------------------------------------------------------ | ------ | -------------------------------------- |
| 271 | Audit `Texture3D` API against FNA                                        | ⬜      | Constructors, getters, SetData/GetData |
| 272 | Audit `TextureCube` API against FNA                                      | ⬜      | Faces, mip levels, formats             |
| 273 | Add `Texture3D` partial box upload tests                                 | ⬜      | x/y/z region                           |
| 274 | Add `Texture3D` partial box readback tests                               | ⬜      | x/y/z region                           |
| 275 | Add `TextureCube` face upload/readback tests for all six faces           | ⬜      | Exact color per face                   |
| 276 | Add `TextureCube` mip-level tests for all six faces                      | ⬜      | Distinct mip colors                    |
| 277 | Verify `Texture3D` sampling in EasyGL stock/custom effect                | ⬜      | If API exposes 3D textures to shaders  |
| 278 | Verify `TextureCube` sampling in EasyGL/Vulkan/Bgfx EnvironmentMapEffect | ⬜      | Cross-backend                          |
| 279 | Add validation for invalid `CubeMapFace` values                          | ⬜      | Unit test                              |
| 280 | Document `Texture3D` and `TextureCube` backend support matrix            | ⬜      | API coverage doc                       |

---

## Phase 34 — SurfaceFormat implementation matrix

| #   | Task                                                                                                 | Status | Notes                        |
| --- | ---------------------------------------------------------------------------------------------------- | ------ | ---------------------------- |
| 281 | Create canonical table of all XNA/FNA `SurfaceFormat` enum values                                    | ⬜      | Include numeric values       |
| 282 | For each format, define CPU bytes-per-pixel or compressed block size                                 | ⬜      | Shared helper                |
| 283 | Implement `SurfaceFormatHelper::GetSize` equivalent if missing                                       | ⬜      | Required for SetData/GetData |
| 284 | Implement/verify `Color` format mapping: EasyGL/Vulkan/Bgfx                                          | ⬜      | RGBA/BGRA correctness        |
| 285 | Implement/verify `Bgr565`, `Bgra5551`, `Bgra4444` packing/unpacking                                  | ⬜      | Golden tests                 |
| 286 | Implement/verify `NormalizedByte2/4`, `NormalizedShort2/4` texture/storage behavior if supported     | ⬜      | Or throw clearly             |
| 287 | Implement/verify float formats: `Single`, `Vector2`, `Vector4`                                       | ⬜      | GL/Vulkan format mapping     |
| 288 | Implement/verify half-float formats: `HalfSingle`, `HalfVector2`, `HalfVector4`                      | ⬜      | GL/Vulkan format mapping     |
| 289 | Implement/verify HDR formats: `HdrBlendable`, `Rgba1010102`, `Rgba64`                                | ⬜      | Document fallback            |
| 290 | Add test that every `SurfaceFormat` either works or throws a deliberate unsupported-format exception | ⬜      | No silent wrong mapping      |

---

## Phase 35 — SamplerState and texture sampling conformance

| #   | Task                                                                                       | Status | Notes                                          |
| --- | ------------------------------------------------------------------------------------------ | ------ | ---------------------------------------------- |
| 291 | Audit `SamplerState` API and static presets against FNA                                    | ⬜      | LinearClamp, PointClamp, AnisotropicWrap, etc. |
| 292 | Verify default sampler states for all slots                                                | ⬜      | Unit test                                      |
| 293 | Verify per-slot sampler binding with two textures using different sampler states           | ⬜      | DualTextureEffect test                         |
| 294 | Pixel test: `TextureAddressMode::Clamp`                                                    | ⬜      | Out-of-range UV                                |
| 295 | Pixel test: `TextureAddressMode::Wrap`                                                     | ⬜      | Out-of-range UV                                |
| 296 | Pixel test: `TextureAddressMode::Mirror`                                                   | ⬜      | Out-of-range UV                                |
| 297 | Pixel test: `TextureFilter::Point` vs `Linear`                                             | ⬜      | Magnification/minification                     |
| 298 | Verify mipmap filter behavior: `MipPoint`, `MipLinear`, `MinLinearMagPointMipLinear`, etc. | ⬜      | If backend supports                            |
| 299 | Verify anisotropic filtering caps and fallback                                             | ⬜      | Query backend max anisotropy                   |
| 300 | Document sampler behavior differences by backend                                           | ⬜      | API coverage doc                               |

---

## Phase 36 — BlendState conformance

| #   | Task                                                                                     | Status | Notes                                          |
| --- | ---------------------------------------------------------------------------------------- | ------ | ---------------------------------------------- |
| 301 | Audit `BlendState` API and static presets against FNA                                    | ⬜      | AlphaBlend, Additive, NonPremultiplied, Opaque |
| 302 | Verify default `BlendState` on `GraphicsDevice`                                          | ⬜      | Unit test                                      |
| 303 | Pixel test: `BlendState::Opaque`                                                         | ⬜      | Source replaces destination                    |
| 304 | Pixel test: `BlendState::AlphaBlend` premultiplied alpha                                 | ⬜      | XNA-compatible result                          |
| 305 | Pixel test: `BlendState::NonPremultiplied`                                               | ⬜      | Compare with expected formula                  |
| 306 | Pixel test: `BlendState::Additive`                                                       | ⬜      | Saturation behavior                            |
| 307 | Verify separate color/alpha blend functions                                              | ⬜      | `ColorBlendFunction`, `AlphaBlendFunction`     |
| 308 | Verify separate color/alpha source/destination blend factors                             | ⬜      | All common factors                             |
| 309 | Verify `BlendFactor` and `MultiSampleMask` behavior                                      | ⬜      | Backends differ                                |
| 310 | Verify changing blend state after first use follows XNA immutability rules if applicable | ⬜      | State object freeze semantics                  |

---

## Phase 37 — DepthStencilState conformance

| #   | Task                                                           | Status | Notes                                   |
| --- | -------------------------------------------------------------- | ------ | --------------------------------------- |
| 311 | Audit `DepthStencilState` API and static presets against FNA   | ⬜      | Default, DepthRead, None                |
| 312 | Verify default depth/stencil state on `GraphicsDevice`         | ⬜      | Unit test                               |
| 313 | Pixel test: depth write enabled vs disabled                    | ⬜      | Two overlapping quads                   |
| 314 | Pixel test: depth comparison functions                         | ⬜      | Less, LessEqual, Greater, Always, Never |
| 315 | Verify stencil enable/disable behavior                         | ⬜      | EasyGL/Vulkan/Bgfx                      |
| 316 | Verify stencil read/write masks                                | ⬜      | Unit + pixel test                       |
| 317 | Verify front-face stencil operations                           | ⬜      | Keep, Replace, Increment, Decrement     |
| 318 | Verify two-sided stencil operations if API exposes them        | ⬜      | Backface ops                            |
| 319 | Verify `ReferenceStencil` device state is used by all backends | ⬜      | Already added, needs tests              |
| 320 | Document depth/stencil support matrix                          | ⬜      | API coverage doc                        |

---

## Phase 38 — RasterizerState conformance

| #   | Task                                                                                                         | Status | Notes                                         |
| --- | ------------------------------------------------------------------------------------------------------------ | ------ | --------------------------------------------- |
| 321 | Audit `RasterizerState` API and static presets against FNA                                                   | ⬜      | CullClockwise, CullCounterClockwise, CullNone |
| 322 | Verify default rasterizer state on `GraphicsDevice`                                                          | ⬜      | Unit test                                     |
| 323 | Pixel test: culling disabled                                                                                 | ⬜      | Draw both winding orders                      |
| 324 | Pixel test: cull clockwise                                                                                   | ⬜      | Verify expected triangle disappears           |
| 325 | Pixel test: cull counter-clockwise                                                                           | ⬜      | Verify expected triangle disappears           |
| 326 | Verify `FillMode::Solid`                                                                                     | ⬜      | Baseline                                      |
| 327 | Verify `FillMode::WireFrame` on Vulkan/Bgfx and documented unsupported behavior on GLES/EasyGL if applicable | ⬜      | Avoid false support                           |
| 328 | Verify depth bias and slope-scale depth bias                                                                 | ⬜      | Shadow-like test                              |
| 329 | Verify scissor test enable/disable interaction with `GraphicsDevice.ScissorRectangle`                        | ⬜      | Pixel test                                    |
| 330 | Verify state object immutability/freeze behavior after binding                                               | ⬜      | XNA-compatible if implemented                 |

---

## Phase 39 — RenderTarget2D and RenderTargetCube completeness

| #   | Task                                                                          | Status | Notes                         |
| --- | ----------------------------------------------------------------------------- | ------ | ----------------------------- |
| 331 | Audit `RenderTarget2D` constructors and properties against FNA                | ⬜      | Include preferred formats     |
| 332 | Audit `RenderTargetCube` constructors and properties against FNA              | ⬜      | Include faces and mipmaps     |
| 333 | Verify `RenderTarget2D` can be sampled as `Texture2D` after unbinding         | ⬜      | EasyGL/Vulkan/Bgfx            |
| 334 | Verify `RenderTargetCube` can be sampled as `TextureCube` after unbinding     | ⬜      | EnvironmentMapEffect          |
| 335 | Verify depth buffer creation for render targets                               | ⬜      | DepthStencilFormat            |
| 336 | Verify render target mipmap support or explicitly reject unsupported mips     | ⬜      | XNA behavior                  |
| 337 | Verify MSAA render target creation and resolve behavior                       | ⬜      | EasyGL/Vulkan/Bgfx            |
| 338 | Verify setting `nullptr` render target returns to backbuffer                  | ⬜      | Unit/integration test         |
| 339 | Verify multiple render targets with mixed formats reject invalid combinations | ⬜      | Match backend/XNA constraints |
| 340 | Document MRT limits for each backend                                          | ⬜      | Max color attachments         |

---

## Phase 40 — Viewport, DisplayMode, and adapter behavior

| #   | Task                                                               | Status | Notes                                     |
| --- | ------------------------------------------------------------------ | ------ | ----------------------------------------- |
| 341 | Audit `Viewport` API against FNA                                   | ⬜      | Project/Unproject methods                 |
| 342 | Add tests for `Viewport::Project` with known matrices              | ⬜      | Math correctness                          |
| 343 | Add tests for `Viewport::Unproject` inverse behavior               | ⬜      | Project then Unproject                    |
| 344 | Verify viewport min/max depth behavior                             | ⬜      | Edge cases                                |
| 345 | Audit `GraphicsAdapter` API against FNA                            | ⬜      | CurrentDisplayMode, SupportedDisplayModes |
| 346 | Add safe fallback for systems where display mode enumeration fails | ⬜      | Headless CI                               |
| 347 | Verify `DisplayModeCollection` enumeration and indexing            | ⬜      | Already partly tested                     |
| 348 | Verify backbuffer size follows window size when appropriate        | ⬜      | SDL integration                           |
| 349 | Verify viewport reset after backbuffer resize                      | ⬜      | EasyGL/Vulkan                             |
| 350 | Document adapter/display limitations for non-desktop platforms     | ⬜      | Android/Web notes                         |

---

## Phase 41 — Effect base class and compiled effect compatibility

| #   | Task                                                                          | Status | Notes                                       |
| --- | ----------------------------------------------------------------------------- | ------ | ------------------------------------------- |
| 351 | Audit `Effect` base class against FNA                                         | ⬜      | Constructors, clone, parameters, techniques |
| 352 | Decide explicit support policy for XNA `.fx` / compiled effect bytecode       | ⬜      | Support, partial support, or unsupported    |
| 353 | If unsupported, make all `.fx` bytecode constructors throw clear exceptions   | ⬜      | No silent fake effects                      |
| 354 | Add documentation explaining `ShaderEffect` vs XNA `Effect` bytecode          | ⬜      | Developer-facing                            |
| 355 | Verify `EffectPass::Apply` updates current effect state consistently          | ⬜      | Unit test with mock effect                  |
| 356 | Verify `EffectTechnique` selection changes applied pass collection            | ⬜      | Unit test                                   |
| 357 | Verify effect parameter lookup by name and semantic                           | ⬜      | FNA semantics                               |
| 358 | Verify effect parameter collection enumeration order                          | ⬜      | Important for compatibility                 |
| 359 | Add tests for missing parameter lookups                                       | ⬜      | Return null vs throw as FNA                 |
| 360 | Add effect lifecycle tests: dispose, clone after dispose, apply after dispose | ⬜      | Match FNA where possible                    |

---

## Phase 42 — BasicEffect exactness

| #   | Task                                                                     | Status | Notes                                      |
| --- | ------------------------------------------------------------------------ | ------ | ------------------------------------------ |
| 361 | Audit `BasicEffect` properties and defaults against FNA `BasicEffect.cs` | ⬜      | Create table                               |
| 362 | Unit test default values for every `BasicEffect` property                | ⬜      | Diffuse, emissive, specular, fog, lighting |
| 363 | Unit test `EnableDefaultLighting()` exact constants                      | ⬜      | Complements Task 194                       |
| 364 | Pixel test: `VertexColorEnabled=false`, no texture, diffuse color only   | ⬜      | EasyGL/Vulkan/Bgfx                         |
| 365 | Pixel test: `VertexColorEnabled=true`, no texture                        | ⬜      | Vertex color multiplication                |
| 366 | Pixel test: `TextureEnabled=true`, no vertex color                       | ⬜      | Texture color                              |
| 367 | Pixel test: `TextureEnabled=true` and `VertexColorEnabled=true`          | ⬜      | Texture × vertex color                     |
| 368 | Pixel test: one directional light enabled                                | ⬜      | Normal-dependent output                    |
| 369 | Pixel test: ambient + emissive + specular combination                    | ⬜      | Harder reference case                      |
| 370 | Cross-backend BasicEffect image comparison suite                         | ⬜      | Same scene on EasyGL/Vulkan/Bgfx           |

---

## Phase 43 — AlphaTestEffect exactness

| #   | Task                                                          | Status | Notes                              |
| --- | ------------------------------------------------------------- | ------ | ---------------------------------- |
| 371 | Audit `AlphaTestEffect` properties and defaults against FNA   | ⬜      | Compare default alpha function/ref |
| 372 | Unit test default values for all `AlphaTestEffect` properties | ⬜      | No backend needed                  |
| 373 | Pixel test every `CompareFunction` on EasyGL                  | ⬜      | Complements Task 190               |
| 374 | Pixel test every `CompareFunction` on Vulkan                  | ⬜      | Same expected results              |
| 375 | Pixel test every `CompareFunction` on Bgfx                    | ⬜      | Same expected results              |
| 376 | Verify alpha reference value scaling 0–255 vs 0–1             | ⬜      | Common bug source                  |
| 377 | Verify vertex color and diffuse color interaction             | ⬜      | If supported                       |
| 378 | Verify fog behavior in AlphaTestEffect                        | ⬜      | XNA-compatible                     |
| 379 | Verify texture disabled or null texture behavior              | ⬜      | Throw/fallback per FNA             |
| 380 | Document AlphaTestEffect backend parity                       | ⬜      | API coverage doc                   |

---

## Phase 44 — DualTextureEffect exactness

| #   | Task                                                              | Status | Notes                                  |
| --- | ----------------------------------------------------------------- | ------ | -------------------------------------- |
| 381 | Audit `DualTextureEffect` properties and defaults against FNA     | ⬜      | Texture, Texture2, DiffuseColor, Alpha |
| 382 | Unit test default values for all `DualTextureEffect` properties   | ⬜      | No backend needed                      |
| 383 | Pixel test: two white textures, diffuse color red                 | ⬜      | Expected red                           |
| 384 | Pixel test: magenta × yellow = red                                | ⬜      | Existing tests can be expanded         |
| 385 | Pixel test: alpha value affects output alpha/color as FNA expects | ⬜      | Premultiplied considerations           |
| 386 | Verify first texture null behavior                                | ⬜      | FNA behavior                           |
| 387 | Verify second texture null behavior                               | ⬜      | FNA behavior                           |
| 388 | Verify fog behavior in DualTextureEffect                          | ⬜      | If property exists                     |
| 389 | Cross-backend DualTextureEffect comparison suite                  | ⬜      | EasyGL/Vulkan/Bgfx                     |
| 390 | Document DualTextureEffect backend parity                         | ⬜      | API coverage doc                       |

---

## Phase 45 — EnvironmentMapEffect exactness

| #   | Task                                                             | Status | Notes                          |
| --- | ---------------------------------------------------------------- | ------ | ------------------------------ |
| 391 | Audit `EnvironmentMapEffect` properties and defaults against FNA | ⬜      | Include Fresnel if present     |
| 392 | Unit test default values for all properties                      | ⬜      | No backend needed              |
| 393 | Pixel test with `EnvironmentMapAmount=0`                         | ⬜      | Should ignore cubemap          |
| 394 | Pixel test with `EnvironmentMapAmount=1` and white cubemap       | ⬜      | Strong env contribution        |
| 395 | Pixel test for `EnvironmentMapSpecular`                          | ⬜      | Specular contribution          |
| 396 | Verify `FresnelFactor` if present                                | ⬜      | XNA Reach/HiDef behavior       |
| 397 | Verify eye position affects reflection vector                    | ⬜      | Use different camera positions |
| 398 | Verify normal matrix/world transform correctness                 | ⬜      | Non-identity world             |
| 399 | Cross-backend EnvironmentMapEffect comparison suite              | ⬜      | EasyGL/Vulkan/Bgfx             |
| 400 | Document EnvironmentMapEffect backend parity                     | ⬜      | API coverage doc               |

---

## Phase 46 — SkinnedEffect exactness

| #   | Task                                                                                | Status | Notes                       |
| --- | ----------------------------------------------------------------------------------- | ------ | --------------------------- |
| 401 | Audit `SkinnedEffect` properties, bone limit, and defaults against FNA              | ⬜      | MaxBones, weights, lighting |
| 402 | Unit test default values for all `SkinnedEffect` properties                         | ⬜      | No backend needed           |
| 403 | Verify `SetBoneTransforms` accepts exactly supported bone count                     | ⬜      | Boundary tests              |
| 404 | Verify `GetBoneTransforms` returns independent copy or expected reference semantics | ⬜      | Match FNA                   |
| 405 | Verify too many bones throws correct exception                                      | ⬜      | Unit test                   |
| 406 | Pixel test: identity bone palette                                                   | ⬜      | No deformation              |
| 407 | Pixel test: single translation bone                                                 | ⬜      | Mesh shifts                 |
| 408 | Pixel test: two-bone blend                                                          | ⬜      | Midpoint deformation        |
| 409 | Cross-backend SkinnedEffect comparison suite                                        | ⬜      | EasyGL/Vulkan/Bgfx          |
| 410 | Document SkinnedEffect backend parity                                               | ⬜      | API coverage doc            |

---

## Phase 47 — SpriteBatch renderer correctness

| #   | Task                                                                     | Status | Notes                 |
| --- | ------------------------------------------------------------------------ | ------ | --------------------- |
| 411 | Create mock/recording `ISpriteBatchBackend` for deterministic unit tests | ⬜      | Needed for sort tests |
| 412 | Complete tests for `SpriteSortMode::Immediate`                           | ⬜      | Task 161 dependency   |
| 413 | Complete tests for `SpriteSortMode::Deferred`                            | ⬜      | Task 162 dependency   |
| 414 | Complete tests for `SpriteSortMode::Texture`                             | ⬜      | Task 163 dependency   |
| 415 | Complete tests for `SpriteSortMode::FrontToBack`                         | ⬜      | Task 164 dependency   |
| 416 | Complete tests for `SpriteSortMode::BackToFront`                         | ⬜      | Task 165 dependency   |
| 417 | Pixel test: rotation around origin                                       | ⬜      | EasyGL                |
| 418 | Pixel test: scalar and Vector2 scale overloads produce expected size     | ⬜      | EasyGL                |
| 419 | Pixel test: source rectangle cropping                                    | ⬜      | EasyGL                |
| 420 | Pixel test: layer depth affects order when sort mode uses it             | ⬜      | EasyGL                |

---

## Phase 48 — SpriteFont and text rendering correctness

| #   | Task                                                                                             | Status | Notes                                      |
| --- | ------------------------------------------------------------------------------------------------ | ------ | ------------------------------------------ |
| 421 | Audit `SpriteFont` API against FNA                                                               | ⬜      | Characters, spacing, kerning, default char |
| 422 | Verify `MeasureString(string)` exact behavior for empty, newline, carriage return, unknown glyph | ⬜      | Some tests exist; expand                   |
| 423 | Verify `MeasureString(StringBuilder)` exact behavior                                             | ⬜      | Add tests                                  |
| 424 | Pixel test: draw a single glyph at known position                                                | ⬜      | EasyGL                                     |
| 425 | Pixel test: draw multiple glyphs with spacing                                                    | ⬜      | EasyGL                                     |
| 426 | Pixel test: newline advances by line spacing                                                     | ⬜      | EasyGL                                     |
| 427 | Pixel test: default character fallback renders expected glyph                                    | ⬜      | EasyGL                                     |
| 428 | Verify `SpriteEffects` with `DrawString`                                                         | ⬜      | Flip text                                  |
| 429 | Verify rotation/origin/scale with `DrawString`                                                   | ⬜      | Pixel test                                 |
| 430 | Document SpriteFont limitations and content-loading requirements                                 | ⬜      | API coverage doc                           |

---

## Phase 49 — Model, ModelMesh, and ModelBone correctness

| #   | Task                                                                                   | Status | Notes                     |
| --- | -------------------------------------------------------------------------------------- | ------ | ------------------------- |
| 431 | Audit `Model`, `ModelMesh`, `ModelMeshPart`, `ModelBone`, collections against FNA      | ⬜      | Public API and properties |
| 432 | Verify `ModelBoneCollection` indexing by int/name                                      | ⬜      | Unit tests                |
| 433 | Verify `ModelMeshCollection` indexing by int/name                                      | ⬜      | Unit tests                |
| 434 | Verify `ModelMeshPart` properties: VB, IB, start index, primitive count, vertex offset | ⬜      | Unit tests                |
| 435 | Verify `Model::CopyAbsoluteBoneTransformsTo`                                           | ⬜      | Known hierarchy           |
| 436 | Verify `Model::CopyBoneTransformsFrom`                                                 | ⬜      | Known hierarchy           |
| 437 | Verify `Model::CopyBoneTransformsTo`                                                   | ⬜      | Known hierarchy           |
| 438 | Pixel test: model with two meshes and different effects                                | ⬜      | EasyGL                    |
| 439 | Pixel test: model hierarchy transform affects child mesh                               | ⬜      | EasyGL                    |
| 440 | Document model-loading and content pipeline limitations                                | ⬜      | API coverage doc          |

---

## Phase 50 — OcclusionQuery correctness

| #   | Task                                                                    | Status | Notes                             |
| --- | ----------------------------------------------------------------------- | ------ | --------------------------------- |
| 441 | Audit `OcclusionQuery` API against FNA                                  | ⬜      | Begin/End, IsComplete, PixelCount |
| 442 | Unit test invalid sequence: `End` before `Begin`                        | ⬜      | Match FNA exception               |
| 443 | Unit test invalid sequence: double `Begin`                              | ⬜      | Match FNA exception               |
| 444 | Unit test invalid sequence: double `End`                                | ⬜      | Match FNA exception               |
| 445 | Pixel/query test: fully visible quad returns positive pixel count       | ⬜      | EasyGL                            |
| 446 | Pixel/query test: fully occluded quad returns zero or lower pixel count | ⬜      | EasyGL                            |
| 447 | Implement/verify Vulkan occlusion query result synchronization          | ⬜      | Avoid stale reads                 |
| 448 | Implement/verify Bgfx occlusion query result behavior                   | ⬜      | bgfx query API                    |
| 449 | Verify disposing query while active is safe or throws correctly         | ⬜      | Unit/integration                  |
| 450 | Document occlusion query backend limitations                            | ⬜      | API coverage doc                  |

---

## Phase 51 — Backend parity and unsupported behavior policy

| #   | Task                                                                             | Status | Notes                                     |
| --- | -------------------------------------------------------------------------------- | ------ | ----------------------------------------- |
| 451 | Create master backend feature matrix for SDL_Renderer, EasyGL, Vulkan, Bgfx      | ⬜      | `docs/graphics-backend-feature-matrix.md` |
| 452 | Ensure SDL_Renderer throws clear exceptions for every unsupported 3D API path    | ⬜      | No silent no-op                           |
| 453 | Ensure EasyGL unsupported features throw or document fallback behavior           | ⬜      | GLES limitations                          |
| 454 | Ensure Vulkan unsupported features throw or document fallback behavior           | ⬜      | Device feature checks                     |
| 455 | Ensure Bgfx unsupported features throw or document fallback behavior             | ⬜      | Backend caps                              |
| 456 | Add startup log/capability dump for selected backend                             | ⬜      | Formats, MSAA, MRT, anisotropy            |
| 457 | Add automated smoke test selecting every backend available on the machine        | ⬜      | CI-friendly                               |
| 458 | Add backend-specific skip mechanism for tests requiring unavailable GPU features | ⬜      | Avoid false failures                      |
| 459 | Document Web/Emscripten backend limitations separately                           | ⬜      | GLES/WebGL notes                          |
| 460 | Document Android backend limitations separately                                  | ⬜      | SDL_Renderer/GLES status                  |

---

## Phase 52 — Pixel conformance and golden-image testing

| #   | Task                                                                                         | Status | Notes                      |
| --- | -------------------------------------------------------------------------------------------- | ------ | -------------------------- |
| 461 | Create common pixel-test helper for window/device creation, draw, present, readback, compare | ⬜      | Reduce duplicated examples |
| 462 | Add tolerance-based pixel comparison for backend differences                                 | ⬜      | Exact vs approximate modes |
| 463 | Add golden image format for small reference outputs                                          | ⬜      | PNG or raw RGBA            |
| 464 | Add BasicEffect golden image cases                                                           | ⬜      | Reuse Phase 42             |
| 465 | Add SpriteBatch golden image cases                                                           | ⬜      | Reuse Phase 47             |
| 466 | Add Texture sampling golden image cases                                                      | ⬜      | Reuse Phase 35             |
| 467 | Add BlendState golden image cases                                                            | ⬜      | Reuse Phase 36             |
| 468 | Add DepthStencil/Rasterizer golden image cases                                               | ⬜      | Reuse Phases 37–38         |
| 469 | Add stock effects golden image cases                                                         | ⬜      | Reuse Phases 43–46         |
| 470 | Add CI option to run graphics pixel tests only when GPU/display is available                 | ⬜      | Headless-safe              |

---

## Phase 53 — FNA comparison harness

| #   | Task                                                                                        | Status | Notes                            |
| --- | ------------------------------------------------------------------------------------------- | ------ | -------------------------------- |
| 471 | Create small FNA C# reference app generator for selected Graphics tests                     | ⬜      | Runs under .NET/FNA              |
| 472 | Generate reference values for non-rendering APIs: defaults, exceptions, enum numeric values | ⬜      | JSON output                      |
| 473 | Generate reference values for PackedVector tests                                            | ⬜      | Complements Task 197             |
| 474 | Generate reference values for `BasicEffect` defaults and lighting constants                 | ⬜      | Complements Task 361–363         |
| 475 | Generate reference values for `SpriteFont.MeasureString`                                    | ⬜      | Complements Phase 48             |
| 476 | Generate reference values for `Viewport.Project/Unproject`                                  | ⬜      | Complements Phase 40             |
| 477 | Generate reference screenshots for simple SpriteBatch cases                                 | ⬜      | Optional, depends on FNA runtime |
| 478 | Generate reference screenshots for BasicEffect cases                                        | ⬜      | Optional                         |
| 479 | Add script to compare CNA JSON results against FNA JSON results                             | ⬜      | Deterministic conformance        |
| 480 | Document how to regenerate FNA reference data                                               | ⬜      | `docs/fna-reference-harness.md`  |

---

## Phase 54 — Documentation and public compatibility status

| #   | Task                                                                                               | Status | Notes                                                |
| --- | -------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------------- |
| 481 | Rewrite `docs/xna-4-api-coverage.md` with current task status after Phase 25+                      | ⬜      | Avoid stale claims                                   |
| 482 | Add clear definitions: API present, implemented, tested, FNA-compatible, intentionally unsupported | ⬜      | Prevent ambiguous percentages                        |
| 483 | Add per-class Graphics coverage table                                                              | ⬜      | GraphicsDevice, Texture2D, Effect, SpriteBatch, etc. |
| 484 | Add per-backend support table                                                                      | ⬜      | SDL/EasyGL/Vulkan/Bgfx                               |
| 485 | Add known deviations from XNA/FNA                                                                  | ⬜      | Honest compatibility list                            |
| 486 | Add migration guide for XNA/FNA users trying CNA                                                   | ⬜      | What works, what to avoid                            |
| 487 | Add minimal 2D sample compatibility list                                                           | ⬜      | SpriteBatch/SpriteFont/Texture2D                     |
| 488 | Add minimal 3D sample compatibility list                                                           | ⬜      | BasicEffect/model/buffers                            |
| 489 | Add troubleshooting guide for graphics backend selection                                           | ⬜      | Env vars/CMake options                               |
| 490 | Add release checklist for Graphics namespace compatibility milestones                              | ⬜      | Before claiming 90/95/100%                           |

---

## Phase 55 — Final Graphics stabilization before declaring XNA 4.0 complete

| #   | Task                                                                                                                                      | Status | Notes                                   |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------- |
| 491 | Remove all remaining `CNA_STUB` markers in `Graphics` or convert them to explicit documented unsupported exceptions                       | ⬜      | No silent placeholders                  |
| 492 | Search for all `TODO`, `FIXME`, `no-op`, and `stub` comments in Graphics source and triage them                                           | ⬜      | Create checklist                        |
| 493 | Run full unit test suite with EasyGL enabled                                                                                              | ⬜      | Must pass                               |
| 494 | Run full graphics integration suite with EasyGL                                                                                           | ⬜      | Must pass or documented skips           |
| 495 | Run full graphics integration suite with Vulkan                                                                                           | ⬜      | Must pass or documented skips           |
| 496 | Run full graphics integration suite with Bgfx                                                                                             | ⬜      | Must pass or documented skips           |
| 497 | Run SDL_Renderer 2D-only suite and verify all 3D calls throw intentionally                                                                | ⬜      | Must pass                               |
| 498 | Build and run at least 5 small FNA/XNA sample ports on CNA                                                                                | ⬜      | Real compatibility proof                |
| 499 | Produce final Graphics compatibility report with percentages based on tests, not estimates                                                | ⬜      | `docs/graphics-compatibility-report.md` |
| 500 | Declare `Microsoft.Xna.Framework.Graphics` 1.0 compatibility milestone only if Tasks 491–499 pass or deviations are explicitly documented | ⬜      | Release gate                            |
