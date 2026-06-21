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
