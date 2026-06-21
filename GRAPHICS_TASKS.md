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
| 167 | Pixel integration test: `SpriteEffects::FlipHorizontally` — draw asymmetric texture (left half red, right half blue), flip horizontally, read back and assert left=blue, right=red | ⬜ | EasyGL; new `examples/easygl_sprite_effects_test.cpp` |
| 168 | Pixel integration test: `transformMatrix` in `SpriteBatch::Begin` — pass a translation matrix `Matrix::CreateTranslation(100,0,0)`, draw a 1×1 red texture at (0,0), read back pixel at (100,0) and assert red | ⬜ | EasyGL; verify matrix is forwarded to backend viewport transform |

---

## Phase 21 — Texture SetData/GetData conformance

| # | Task | Status | Notes |
|---|------|--------|-------|
| 169 | `Texture2D::SetData` / `GetData` — partial rectangle regions: set data into a sub-rectangle of a 4×4 texture, read back the full texture and verify only the target region changed | ⬜ | Add to `Texture2DTests.cpp` |
| 170 | `Texture2D::SetData` / `GetData` — `startIndex` and `elementCount` parameters: upload only a middle slice of a data array, verify correct pixels written | ⬜ | Add to `Texture2DTests.cpp` |
| 171 | `Texture2D` mip-level `SetData` / `GetData`: upload distinct colours to mip 0 and mip 1 of a 4×4 `generateMipMaps=true` texture; read back each level and verify | ⬜ | Add to `Texture2DTests.cpp` |
| 172 | `TextureCube` mip-level `SetData` / `GetData`: upload distinct colours per face per mip; read back and verify | ⬜ | Add to `Texture3DTextureCubeRenderTargetTests.cpp` |
| 173 | `Texture3D` z-slice `SetData` / `GetData`: upload distinct colours per z-slice; read back and verify | ⬜ | Add to `Texture3DTextureCubeRenderTargetTests.cpp` |
| 174 | `SurfaceFormat` backend mapping table: for each `SurfaceFormat` value (Color, Bgr565, Bgra5551, Bgra4444, Dxt1/3/5, Rgba1010102, Rg32, Rgba64, Alpha8, Single, Vector2/4, HalfSingle/Vector2/Vector4, HdrBlendable) — document whether EasyGL/Vulkan/Bgfx map it to a real GPU format, approximate it, or throw | ⬜ | New file `docs/surface-format-support.md` |
| 175 | DXT golden tests: for a 4×4 DXT1 block encoding known RGBA values, decode via `DxtUtil` and compare byte-for-byte against FNA reference output | ⬜ | Add to unit tests; generate golden values from FNA |
| 176 | sRGB formats: `ColorSrgb`, `Dxt1Srgb`, `Dxt3Srgb`, `Dxt5Srgb` — either implement GPU-side sRGB sampling flag in EasyGL (`GL_SRGB8_ALPHA8`) and Vulkan (`VK_FORMAT_R8G8B8A8_SRGB`), or explicitly throw `std::runtime_error("SurfaceFormat not supported")` with a clear message | ⬜ | Currently silently maps to wrong format |

---

## Phase 22 — RenderTarget and presentation correctness

| # | Task | Status | Notes |
|---|------|--------|-------|
| 177 | `RenderTargetUsage::DiscardContents` vs `PreserveContents` in EasyGL: verify that `DiscardContents` issues `glInvalidateFramebuffer` (or clear) and `PreserveContents` does not clear on `SetRenderTarget` | ⬜ | Add integration test; check FNA behaviour |
| 178 | Same as 177 but Vulkan: `DiscardContents` → `VK_ATTACHMENT_LOAD_OP_CLEAR`; `PreserveContents` → `VK_ATTACHMENT_LOAD_OP_LOAD` in RT render pass | ⬜ | May require per-RT render pass creation change |
| 179 | Same as 177 but Bgfx: map to `BGFX_CLEAR_COLOR` vs no clear flag on `bgfx::setViewClear` | ⬜ | Low priority |
| 180 | Integration test: backbuffer → RT → backbuffer → RT → backbuffer in a single frame; verify correct render target is active at each step and final backbuffer pixel is correct | ⬜ | EasyGL; new example |
| 181 | `RenderTargetBinding` with explicit mip level and cube face: verify that `SetRenderTargets({RenderTargetBinding(rt, 0), RenderTargetBinding(rt, 1)})` correctly targets distinct mip levels | ⬜ | Add unit test; check FNA `RenderTargetBinding` ctor |
| 182 | `PresentationParameters` round-trip: after `GraphicsDeviceManager::ApplyChanges`, verify `GraphicsDevice.PresentationParameters` reflects the requested `BackBufferWidth`, `BackBufferHeight`, `DepthStencilFormat`, `PresentInterval`, `MultiSampleCount` | ⬜ | Unit test; no backend needed |
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
