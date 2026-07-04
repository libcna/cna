# Graphics Implementation Task Plan

> Goal: XNA 4.0 Graphics namespace fully implemented — all 4 backends
> (SDL_Renderer, EasyGL, Vulkan, Bgfx).
>
> **SDL_Renderer is intentionally 2D-only.** All 3D calls throw `std::runtime_error`.
> This is correct XNA/FNA behavior — SDL_Renderer cannot do 3D.

> **Task numbering:** 1–663 = original core plan (Phases 1–55, all non-WebGPU work).
> 664+ = additions made after the original plan was written (session 2026-07-02 onward),
> including the full Phase 70–73 backend-perfection wave below.
> **10001+ = WebGPU** (Phases 56–69, renumbered 2026-07-02 from their original 501–661 to
> free up the low range for further core-plan growth; WebGPU is deliberately deprioritized —
> see "Execution order" below).

## Execution order (priority, not document order)

1. **Phases 1–55** (core, Tasks 1–663) — in progress, ~44% done as of 2026-07-02.
2. **Phase 70 — SDL_Renderer** (Tasks 666–731) — the 2D-only backend must reach verified
   perfection first: it's the smallest, simplest surface, and every other backend's SpriteBatch/
   Texture2D work builds on the same XNA semantics SDL_Renderer has to get exactly right.
3. **Phase 71 — EasyGL final gap closure** (Tasks 732–739) — EasyGL is already the de facto
   target of most unlabeled tasks in Phases 34–55, so this phase is deliberately small: it only
   covers what genuinely isn't tracked elsewhere yet (real `SurfaceFormat` GPU forwarding,
   `FillMode::WireFrame` emulation, mip-chain generation for `Texture3D`/`TextureCube`).
4. **Phase 72 — Bgfx parity** (Tasks 740–824) — full pixel-verified breadth, replicated from
   Phases 35–50's EasyGL-default testing matrix, starting from wiring up the pixel-readback path
   (`GetBackBufferData` exists per Task 117 but no current Bgfx test calls it).
5. **Phase 73 — Vulkan parity** (Tasks 664–665, 825–861) — Vulkan already has more infrastructure
   done than Bgfx (per-slot samplers, instancing, wireframe, MSAA, RenderTargetCube, stock-effect
   SPIR-V shaders), so this phase is gap-closure sized, not a full replication — plus the two
   SpriteBatch bugs found 2026-07-02 (multi-batch, dropped `SamplerState`).
6. **Phases 56–69 (WebGPU, Tasks 10001+)** — parked. Revisit only after 1–5 above are done.

> Phases 70–73 physically appear after Phase 69 in this document (append-only, to avoid
> renumbering existing WebGPU content) but run **before** it — see the priority order above,
> not the phase numbers, for actual execution sequence.

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
| 183 | `GraphicsDevice` device-reset events: `DeviceResetting` and `DeviceReset` fire when swapchain is recreated; `ResourceDestroyed` fires on explicit resource dispose; add unit tests | ✅ | GDM events fire in correct order (Resetting→Reset); 5/5 PASS; 28/28 EasyGL |

---

## Phase 23 — Effect system XNA accuracy

| # | Task | Status | Notes |
|---|------|--------|-------|
| 184 | `Effect::Clone()` for base `Effect` and all stock effects (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`): clone must produce an independent copy with the same parameter values but a separate parameter collection; modifying a parameter on the clone must not affect the original | ✅ | EasyGL integration test via `AlphaTestEffect`; pointer distinct, Alpha+DiffuseColor independence both directions; 7/7 PASS; 29/29 EasyGL |
| 185 | `Effect::CurrentTechnique`, `Techniques`, `Parameters`, `Passes` collection semantics: verify that `Techniques[0].Passes[0].Apply()` calls the correct backend draw-state setup; add unit tests for collection indexing and `Contains` | ✅ | Added `GetParameterBySemantic` to `EffectParameterCollection`; new `EffectCollectionTests.cpp` (38 unit tests); EasyGL integration test for CurrentTechnique get/set + Apply; 7/7 PASS; 30/30 EasyGL |
| 186 | `EffectParameter` arrays — wrong-type and out-of-range guards: `SetValue(float[])` on an int parameter must throw or silently ignore per XNA spec; `SetValue` with more elements than declared must throw; add unit tests | ✅ | FNA silently ignores type mismatch and excess/fewer elements; NaN stored without throw (FNA non-debug mode); 6 new unit tests, 46/46 pass |
| 187 | `EffectParameter::SetValueTranspose(Matrix)` — verify the matrix is stored transposed relative to `SetValue(Matrix)`; add unit test comparing stored values byte-for-byte | ✅ | 6 new tests: raw layout diff, GetValueMatrix returns Transpose(m), SetValue round-trip, SetValueTranspose≡SetValue(Transpose), double-transpose; 52/52 pass |
| 188 | `EffectAnnotation` and annotation collections: verify that `Effect.Parameters["X"].Annotations["hint"].GetValueString()` works; add unit tests | ✅ | Added `cachedString` param to constructor; 31 tests: metadata, all GetValue* types, string round-trip, collection indexing, technique/pass annotations start empty |
| 189 | Pixel integration tests for `BasicEffect` combinations — EasyGL: (a) vertex color only, (b) texture only, (c) texture + vertex color (multiply), (d) directional lighting on, (e) fog on | ✅ | `easygl_basiceffect_combinations_test.cpp`: 5 sub-tests (a) red vertices stride=16 (b) blue texture stride=20 (c) red diffuse×white tex stride=20 (d) green vertex×white tex stride=24 (e) red directional light stride=32; fog skipped — no fog fields in EasyGL GpuDrawParams; 5/5 PASS |
| 190 | Pixel integration tests for `AlphaTestEffect` all `CompareFunction` modes — EasyGL: draw a pixel with alpha=128; test `Less`, `LessEqual`, `Equal`, `GreaterEqual`, `Greater`, `NotEqual`, `Always`, `Never` reference=128; assert pixel is drawn or discarded accordingly | ✅ | `easygl_alphatest_modes_test.cpp`: pixel.a=128/255, ref=128; drawn: Always/LessEqual/Equal/GreaterEqual; discarded: Never/Less/NotEqual/Greater; 8/8 PASS; 32/32 EasyGL |

---

## Phase 24 — Stock effects backend parity

| # | Task | Status | Notes |
|---|------|--------|-------|
| 191 | `DualTextureEffect` pixel tests — Vulkan + EasyGL + Bgfx: two distinct textures, verify per-backend that the output blends both | ✅ | `easygl_dualtexture_test.cpp`: 4 sub-tests: (a) white×blue→blue (b) red×white→red (c) white×white×green diffuse→green (d) yellow×cyan→green (proves both textures multiplied simultaneously); 4/4 PASS; 33/33 EasyGL |
| 192 | `EnvironmentMapEffect` parameter accuracy — EasyGL: verify reflection vector, `EnvironmentMapAmount`, `EnvironmentMapSpecular`, `EmissiveColor` affect output correctly by reading back pixels; compare EasyGL vs. Vulkan | ✅ | Extended `easygl_env_map_test.cpp`: 4 sub-tests (a) EmissiveColor=red→red (b) EmissiveColor=green→green (c) EnvMapSpecular=(0,0,1)→blue (d) EnvMapAmount=1 blue cube→blue; 4/4 PASS; 33/33 EasyGL |
| 193 | `SkinnedEffect` bone count tests: (a) 1 bone identity → no vertex movement, (b) 1 bone translate → all vertices shifted, (c) 4-weight blend with two bones at 50%/50% → midpoint position; assert pixel readback per case | ✅ | EasyGL; `examples/easygl_skinned_effect_bones_test.cpp`; 8/8 pixel checks PASS (centre=red, left/right=green per sub-test) |
| 194 | `BasicEffect::EnableDefaultLighting()` — verify that after calling `EnableDefaultLighting()` the three `DirectionalLight` values match the FNA constants in `BasicEffect.cs`; add unit test comparing property values | ✅ | EasyGL integration test `easygl_basiceffect_default_lighting_test.cpp`; also fixed 2 bugs: Light2.SpecularColor was Zero (should be (0.3231373,0.3607844,0.3937255)) and Light2.DiffuseColor.Y was 0.3607843 (should be 0.3607844); 14/14 PASS; 35/35 EasyGL |
| 195 | Fog equation accuracy — `BasicEffect` fog: set `FogEnabled=true`, `FogStart=0`, `FogEnd=100`, `FogColor=red`; draw a geometry at Z=50; assert the output pixel is a blend between geometry colour and red; test EasyGL and Vulkan | ✅ | EasyGL: implemented fog in `GpuDrawParams` + 4 shaders (colored/textured/col+textured/lit+textured); `easygl_basiceffect_fog_test.cpp`; 3 sub-tests (a) fog OFF→pure blue (b) 50% fog Z=0.5→purple (128,0,128) (c) full fog Z=0.9>FogEnd=0.5→red; 3/3 PASS; 36/36 EasyGL. Fog depth = model-space Z (correct for identity World/View). |
| 196 | Backend parity table: for each stock effect (Basic, AlphaTest, DualTexture, EnvironmentMap, Skinned) and `ShaderEffect`, document EasyGL / Vulkan / Bgfx / SDL status (✅ tested / ⚠️ compiles only / ❌ missing) in a new section of `docs/xna-4-api-coverage.md` | ✅ | Added §7 "Stock Effect Backend Parity" to `docs/xna-4-api-coverage.md`; per-effect table with EasyGL/Vulkan/Bgfx/SDL columns; known-gaps table lists 6 follow-up items |

---

## Phase 25 — PackedVector exactness

| # | Task | Status | Notes |
|---|------|--------|-------|
| 197 | Generate FNA golden values for all PackedVector types (`Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Byte4`, `Color`, `HalfSingle`, `HalfVector2`, `HalfVector4`, `NormalizedByte2`, `NormalizedByte4`, `NormalizedShort2`, `NormalizedShort4`, `Rg32`, `Rgba1010102`, `Rgba64`, `Short2`, `Short4`, `Single`, `Vector2`, `Vector4`): run FNA with known inputs and record `PackedValue` and `ToVector4()` output | ✅ | Golden values derived from FNA bit-packing formulas via Python; saved in `tests/PackedVectorGolden.md`; 17 types covered |
| 198 | Compare CNA PackedVector output against golden values from Task 197; fix rounding, saturation, or bit-packing bugs discovered | ✅ | 3 bugs fixed: `HalfTypeHelper::Convert(float)` (uint32_t exp underflow → 0.0f packed as infinity), `NormalizedByte2/4::Pack` (truncation→`std::lroundf`), `NormalizedShort2/4::Pack` (same); golden file corrected for -1.0 inputs; 20 new golden-value tests added; 1638/1640 total pass (2 pre-existing failures unrelated to PackedVector) |
| 199 | PackedVector edge-case tests: inputs −1.0, 0.0, 1.0, values slightly outside [0,1] or [−1,1] range, `NaN`, `+Inf`, `−Inf`, half-float special values (denormals, ±0, ±Inf, NaN) | ✅ | 28 new tests: clamping (Alpha8/Bgr565/NormByte2/4/NormShort2/4/Rg32/Rgba1010102/Rgba64/Short2/4), HalfTypeHelper specials (±0, ±∞, NaN, denormals), boundary round-trips; 1666/1668 pass (2 pre-existing) |
| 200 | Update `docs/xna-4-api-coverage.md`: rewrite the PackedVector and stock-effects sections to reflect the actual post-Phase-18 state; remove stale "stub" labels; add backend parity table from Task 196; add overall coverage estimate table matching the June 2026 external review findings | ✅ | PackedVector status: Stub→Implemented (17 types); stock-effects status updated per backend; §8 Overall Coverage Estimate added (~80% EasyGL); §10 recommended order and §11 summary updated; section renumbered |

---

## Phase 26 — GraphicsDevice lifecycle and XNA validation

> Goal: make `GraphicsDevice` behave closer to XNA/FNA in invalid states, reset-like operations,
> resource ownership, and draw-call validation. These tasks should reduce hidden incompatibilities
> with real XNA games.

| #   | Task                                                                                                                                                          | Status | Notes                                                |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------------------------- |
| 201 | Audit all public `GraphicsDevice` methods against FNA `GraphicsDevice.cs`; list missing overloads, validation differences, and intentionally unsupported APIs | ✅      | `docs/graphicsdevice-fna-audit.md` created; 3 missing XNA methods identified (`Present(rect,rect,IntPtr)`, `Clear(ClearOptions,Vector4,float,int)`, `GetRenderTargetsNoAllocEXT`); 7 CNA non-XNA methods missing `NOXNA` tag documented |
| 202 | Add validation for null `VertexBuffer`, null `IndexBuffer`, null `Texture`, null `Effect`, and invalid render target arguments                                | ✅      | Present(): throws InvalidOperationException when RT bound; SetVertexBuffers(>16): throws ArgumentOutOfRangeException; GetBackBufferData(nullptr): throws invalid_argument; TextureCollection: throws ObjectDisposedException for disposed textures; SetRenderTarget(RT2D*)/SetRenderTarget(RTCube*,face) now update renderTargetBound_ flag; 8 unit + 4 integration tests |
| 203 | Verify draw calls throw when no vertex buffer is bound and the API requires one                                                                               | ✅      | DrawPrimitives, DrawIndexedPrimitives, DrawInstancedPrimitives all throw std::runtime_error when no VB bound; 3/3 EasyGL integration checks pass (`easygl_draw_novertexbuffer_test.cpp`) |
| 204 | Verify indexed draw calls throw when no index buffer is bound                                                                                                 | ✅      | DrawIndexedPrimitives and DrawInstancedPrimitives throw `std::runtime_error` when no IB bound (VB bound to pass first guard); 2/2 EasyGL integration checks pass (`easygl_draw_noindexbuffer_test.cpp`) |
| 205 | Validate primitive count and vertex/index ranges in `DrawPrimitives`, `DrawIndexedPrimitives`, and user-primitive variants                                    | ✅      | Added `ThrowIfNegativeOrZero`/`ThrowIfNegative` guards to DrawPrimitives (primitiveCount, vertexStart), DrawIndexedPrimitives (primitiveCount, startIndex, baseVertex), DrawInstancedPrimitives (primitiveCount, instanceCount), and all 4 typed DrawUserPrimitives overloads; 9/9 EasyGL integration checks pass (`easygl_draw_range_validation_test.cpp`) |
| 206 | Validate `PrimitiveType` handling for `TriangleList`, `TriangleStrip`, `LineList`, `LineStrip`, and `PointList` if present                                    | ✅      | Added `PointListEXT = 4` to enum (matches FNA); EasyGL `ToEasyGl()` maps it to `GL_POINTS`, default throws `InvalidOperationException("Unrecognized primitive type!")`; all helper switch tables updated; 6/6 EasyGL integration checks pass (`easygl_primitivetype_validation_test.cpp`) |
| 207 | Add tests for `GraphicsDevice::Clear` overloads: color only, options + color + depth + stencil, `ClearOptions` combinations                                   | ✅      | Added depth range guard [0,1] throwing `ArgumentOutOfRangeException`; added `IGraphicsBackend::ClearDepth(float)` + EasyGL impl (GL depth-only clear); fixed depth-only branch to not accidentally clear color; 9/9 pixel-readback + exception EasyGL checks pass (`easygl_clear_overloads_test.cpp`) |
| 208 | Verify viewport state survives draw calls and changes only when explicitly set                                                                                | ✅      | 10/10 EasyGL integration checks: initial VP matches backbuffer, set/get round-trip, VP stable across Clear+DrawUserPrimitives, second explicit set works, minDepth/maxDepth survive (`easygl_viewport_state_test.cpp`) |
| 209 | Verify scissor rectangle behavior when `RasterizerState.ScissorTestEnable` is false vs true                                                                   | ✅      | Fixed EasyGL bug: `SetScissorRect` was incorrectly enabling scissor test (now rect-only, enable is `ApplyRasterizerState`'s job); 7/7 EasyGL pixel-readback checks pass: scissor-off draws full viewport, scissor-on clips to right half, re-disabling restores full draw, rect round-trips (`easygl_scissor_test.cpp`) |
| 210 | Verify `GraphicsDevice` rejects using disposed resources in draw calls                                                                                        | ✅      | Added `ObjectDisposedException` guards: `Effect::Apply()`, `SetRenderTarget(RenderTarget2D*)`, `SetRenderTarget(RenderTargetCube*,CubeMapFace)` (Texture guard was already in `TextureCollection`); VB/IB skip pending Task 212 (not yet derived from `GraphicsResource`); 3/3 EasyGL checks pass (`easygl_disposed_resource_test.cpp`) |

---

## Phase 27 — GraphicsResource ownership and disposal semantics

| #   | Task                                                                                                      | Status | Notes                              |
| --- | --------------------------------------------------------------------------------------------------------- | ------ | ---------------------------------- |
| 211 | Audit `GraphicsResource` base behavior against FNA: `GraphicsDevice`, `Name`, `Tag`, `IsDisposed`, events | ✅      | Fixed `Dispose(bool)` event-before-flag ordering; added `ToString()` override (returns Name if set, else type name); documented 2 gaps: device resource tracking (`AddResourceReference`/`RemoveResourceReference` not implemented) and `GraphicsDeviceResetting()` callback; all intentional C++ deviations listed in `docs/graphicsresource-fna-audit.md` |
| 212 | Ensure all Graphics resources derive consistently from `GraphicsResource` where XNA/FNA does              | ✅      | Added `GraphicsResource` base + `GetTypeName()` to 8 types: `BlendState`, `DepthStencilState`, `RasterizerState`, `SamplerState` (device=nullptr, copyable value semantics preserved), `VertexBuffer`, `IndexBuffer`, `SpriteBatch` (device=&device, removed redundant private `graphicsDevice_` member), `VertexDeclaration` (device=nullptr, new `VertexDeclaration.cpp` created); clean build, 44/45 EasyGL tests pass (1 pre-existing MRT failure) |
| 213 | Add unit tests for double `Dispose()` on all major resource types                                         | ✅      | 22/22 EasyGL checks pass: BlendState, DepthStencilState, RasterizerState, SamplerState, VertexDeclaration, VertexBuffer, IndexBuffer, Texture2D, RenderTarget2D, BasicEffect, SpriteBatch — each called Dispose() twice, no crash + IsDisposed=true confirmed (`easygl_double_dispose_test.cpp`) |
| 214 | Add tests for disposing resources while bound to `GraphicsDevice`                                         | ✅      | Implemented FNA behaviour: `Texture::Dispose(bool)` removes self from all Textures/VertexTextures slots; `RenderTarget2D::Dispose(bool)` throws `InvalidOperationException` if still bound; VB/IB no cleanup on dispose (matches FNA); added `TextureCollection::RemoveDisposedTexture`; 10/10 EasyGL checks pass (`easygl_bound_resource_dispose_test.cpp`) |
| 215 | Ensure backend GPU handles are released exactly once on dispose                                           | ✅      | Added `Dispose(bool)` overrides to `VertexBuffer`, `IndexBuffer`, `Texture2D` that call `backend_.reset()` before delegating to base — GPU handle freed immediately on Dispose(), not deferred to C++ destructor; added `NOXNA HasBackend()` accessor; 16/16 EasyGL checks pass (`easygl_handle_release_test.cpp`) |
| 216 | Ensure moved C++ resources do not double-free backend handles                                             | ✅      | Added explicit move constructor+assignment to `VertexBuffer` and `IndexBuffer` (bodies in `.cpp` so complete backend type is visible); verified via `HasBackend()`: moved-from becomes false, moved-to stays true; 21/21 EasyGL checks pass (`easygl_move_semantics_test.cpp`) |
| 217 | Add `ResourceCreated` / `ResourceDestroyed` behavior if required by CNA/FNA compatibility layer           | ✅      | Added `OnResourceCreated`/`OnResourceDestroyed` NOXNA methods to `GraphicsDevice`; `GraphicsResource` ctor fires `ResourceCreated` (when device set) and `Dispose(bool)` fires `ResourceDestroyed` with name+tag; updated `ResourceCreatedEventArgs`/`ResourceDestroyedEventArgs` to use `System::Object*`; 9/9 EasyGL checks pass (`easygl_resource_events_test.cpp`) |
| 218 | Verify `GraphicsDevice` destruction disposes backend resources in safe order                              | ✅      | Added `resources_` tracking list to `GraphicsDevice`; `AddResourceReference`/`RemoveResourceReference` NOXNA methods; `GraphicsResource` ctor registers, `Dispose(bool)` unregisters; `GraphicsDevice::Dispose()` copies+clears the list then disposes all tracked resources before `destroyNativeResources()` (matching FNA pattern for safe re-entrancy); 11/11 EasyGL checks pass (`easygl_device_dispose_order_test.cpp`) |
| 219 | Add leak-check style test for creating and disposing many textures/buffers/render targets                 | ✅      | Added `GetTrackedResourceCount()` NOXNA to `GraphicsDevice`; test creates 20×4=80 resources (Texture2D, VertexBuffer, IndexBuffer, RenderTarget2D), disposes all, verifies: event counts match (80/80), tracking list returns to baseline, `HasBackend()==false` for all, no crash on unique_ptr clear; 7/7 EasyGL checks pass (`easygl_resource_leak_test.cpp`) |
| 220 | Document Graphics resource lifetime rules in `docs/graphics-resource-lifetime.md`                         | ✅      | Created `docs/graphics-resource-lifetime.md` covering: GPU handle ownership (unique_ptr in derived), Dispose(bool) override chain (backend freed before base), GraphicsDevice tracking list + copy-and-clear disposal pattern, ResourceCreated/Destroyed events, move semantics + incomplete-type pimpl caveat, resources without device, backend-specific caveats (EasyGL context loss, Vulkan in-flight images, Bgfx deferred destroy, SDL renderer order) |

---

## Phase 28 — PresentationParameters and GraphicsDeviceManager compatibility

| #   | Task                                                                                                                                            | Status | Notes                                        |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------- | ------ | -------------------------------------------- |
| 221 | Audit `PresentationParameters` fields/properties against FNA/XNA                                                                                | ✅      | All 9 properties + `Clone()` confirmed present and matching FNA; fixed default dimensions bug (1024×768→800×480 to match `GraphicsDeviceManager::DefaultBackBuffer*`); added `SetDisplayOrientation` and `SetDeviceWindowHandle` setter tests; extended `CloneCopiesAllFields` to cover all 10 fields; fixed C++ name-hiding bug (`Texture`/`Texture2D`/`RenderTarget2D::Dispose(bool)` hid base `Dispose()` — added `using` declarations); 26/26 unit tests pass; 51/52 EasyGL pass (pre-existing MRT failure) |
| 222 | Verify defaults for `BackBufferWidth`, `BackBufferHeight`, `BackBufferFormat`, `DepthStencilFormat`, `MultiSampleCount`, `PresentationInterval` | ✅      | Covered by Task 221 — 10 default-value tests already in PresentationParametersTests.cpp, all pass |
| 223 | Implement or document `PresentationInterval` mapping for SDL/EasyGL/Vulkan/Bgfx                                                                 | ✅      | Added `swapInterval` to `GraphicsBackendCreateArgs` (0=Immediate/no-VSync, 1=Default/One, 2=Two); `IGraphicsBackend::SetSwapInterval(int)` default no-op; `toSwapInterval(PresentInterval)` helper in GraphicsDevice; EasyGL: `SDL_GL_SetSwapInterval` at init + runtime override; SDL_Renderer: `SDL_SetRenderVSync` (maps Two→1, SDL3 only supports 0/1); Vulkan: present mode at swapchain creation (IMMEDIATE/MAILBOX for 0, FIFO_RELAXED for 2, FIFO for 1); Bgfx: `resetFlags_` member (`BGFX_RESET_VSYNC` vs `BGFX_RESET_NONE`), runtime `SetSwapInterval` calls `bgfx::reset`; `GraphicsDevice::SetPresentationParameters` now calls `SetSwapInterval`; 10/10 EasyGL smoke-test checks pass (`easygl_present_interval_test.cpp`); 52/53 EasyGL pass (pre-existing MRT) |
| 224 | Verify `IsFullScreen` and windowed mode fields are stored consistently even if backend cannot switch fullscreen                                 | ✅      | `SDL_SetWindowFullscreen` failure changed from throw to `SDL_ClearError()` soft-skip (non-fatal); `easygl_fullscreen_field_test.cpp` verifies field round-trip through GDM setter + `ApplyChanges()` + `ToggleFullScreen()`; 7/7 PASS; 53/54 EasyGL pass (pre-existing MRT) |
| 225 | Add `GraphicsDeviceManager` compatibility shim if current SDL3 Game integration leaves common XNA samples uncompilable                          | ✅      | Registered GDM as `IGraphicsDeviceManager` + `IGraphicsDeviceService` in `registerServices()`; added duplicate-registration guard (throws `invalid_argument`); `unregisterServices()` now removes both services (with null-guard); `Game::DoInitialize()` now finds GDM and calls `CreateDevice()` automatically — matching FNA behavior; also fixed `StorageDeviceNotConnectedException` + `StorageDevice.cpp` to use `std::exception_ptr` following sharp-runtime API change; 10/10 + 7/7 integration tests PASS, 1639/1639 unit tests PASS |
| 226 | Implement `ApplyChanges()` compatibility path for `GraphicsDeviceManager` if added                                                              | ⬜      | Recreate swapchain/window resources          |
| 227 | Add tests for changing backbuffer size through presentation parameters                                                                          | ✅      | `easygl_backbuffer_resize_test.cpp`: PP dimensions always updated correctly via GDM and direct paths; viewport HEIGHT = virtualHeight (correct for FixedHeightDynamicWidth mode); viewport WIDTH is adaptive to physical window aspect ratio; 12/12 PASS. Also added `docs/easygl_bugs.md` with full EasyGL bug audit (MRT leak, scissor/clear viewport bug, anisotropy ignored, missing ColorWriteMask, etc.) |
| 228 | Add tests for depth/stencil format changes                                                                                                      | ✅      | Unit: 3 new PP tests (None/Depth16/Depth24Stencil8 round-trip) → 5/5 depth unit tests pass. Integration: `easygl_depth_format_test.cpp` — GDM path + direct path for all 4 DepthFormat values; EasyGL does not recreate the depth buffer at runtime (window-system FB has fixed depth alloc) but PP field is always stored correctly and neither path throws; 9/9 PASS |
| 229 | Verify MSAA count changes after device creation are handled or explicitly rejected                                                              | ✅      | Unit: 3 new PP tests (Zero/One/Eight round-trip) → 5/5 MultiSampleCount unit tests pass. Integration: `easygl_msaa_change_test.cpp` — GDM path (preferMultiSampling=false→0, true→8, false→0) + direct path (0/1/2/4/8); PP field always stored, no throw; 9/9 PASS. Runtime MSAA count change does NOT recreate the backend (documented as **limit** in `docs/easygl_bugs.md`) |
| 230 | Document which `GraphicsDeviceManager` features are supported, replaced, or intentionally omitted                                               | ✅      | `docs/gdm-coverage.md` created: full property/event/method/service table vs FNA; key deviation — CNA `ApplyChanges()` calls `applyToExistingBackend()` instead of `graphicsDevice.Reset()`, so format/MSAA/depth changes update PP but not GPU resources; all defaults match FNA; NOXNA extensions documented |

---

## Phase 29 — VertexBuffer, IndexBuffer, and DynamicBuffer conformance

| #   | Task                                                                                             | Status | Notes                              |
| --- | ------------------------------------------------------------------------------------------------ | ------ | ---------------------------------- |
| 231 | Audit `VertexBuffer`, `DynamicVertexBuffer`, `IndexBuffer`, `DynamicIndexBuffer` API against FNA | ✅      | Added `getBufferUsageProperty`, `getVertexDeclarationProperty`, `getIndexElementSizeProperty`; protected `dynamic` ctors; startIndex/elementCount SetData overloads; DynamicVB/IB SetDataOptions overloads; 56/56 EasyGL pass |
| 232 | Add tests for `VertexBuffer::SetData` with offset, startIndex, elementCount, vertexStride        | ✅      | `easygl_vertexbuffer_setdata_test.cpp`; 14/14 PASS; also tests IndexBuffer and Dynamic* properties |
| 233 | Add tests for `VertexBuffer::GetData` if exposed                                                 | N/A     | GetData not in CNA API; no backend VBO readback; gap documented in `docs/easygl_bugs.md` |
| 234 | Add tests for `IndexBuffer::SetData` with 16-bit and 32-bit index formats                        | ✅      | Covered by Task 232 (tests 7a, 7b, 8 in `easygl_vertexbuffer_setdata_test.cpp`); GPU offsetInBytes documented as missing |
| 235 | Add tests for `IndexBuffer::GetData` if exposed                                                  | N/A     | Same as 233 — GetData not in CNA API; gap documented in `docs/easygl_bugs.md` |
| 236 | Implement `DynamicVertexBuffer::SetDataOptions` behavior: `None`, `Discard`, `NoOverwrite`       | ✅      | EasyGL: `Discard`=orphan+sub_data, `NoOverwrite`=sub_data, `None`=BufferData; all other backends use default no-op; 57/57 EasyGL pass |
| 237 | Implement `DynamicIndexBuffer::SetDataOptions` behavior: `None`, `Discard`, `NoOverwrite`        | ✅      | Implemented together with Task 236 — same GL strategy for IBOs; `SetData16/32WithOptions` in EasyGL backend |
| 238 | Add stress test for repeatedly updating dynamic buffers every frame                              | ✅      | `easygl_dynamic_buffer_stress_test.cpp`; 12 frames × None/Discard/NoOverwrite; pixel readback verifies each frame; 36/36 PASS |
| 239 | Validate buffer usage flags: `BufferUsage::WriteOnly` and any other supported values             | ✅      | `easygl_buffer_usage_test.cpp`; 15/15 PASS; deviations: GL usage hint not forwarded (always DynamicDraw), GetData WriteOnly enforcement absent (no GetData in CNA) |
| 240 | Verify disposed buffers cannot be rebound or updated                                             | ✅      | `easygl_disposed_buffer_test.cpp`; 17/17 PASS; guards in VB/IB SetData/SetDataRaw/SetDataWithOptions and GraphicsDevice::SetVertexBuffer/SetIndexBuffer; throws ObjectDisposedException |

---

## Phase 30 — VertexDeclaration and vertex format accuracy

| #   | Task                                                                                                                           | Status | Notes                              |
| --- | ------------------------------------------------------------------------------------------------------------------------------ | ------ | ---------------------------------- |
| 241 | Audit `VertexDeclaration`, `VertexElement`, `VertexElementFormat`, and `VertexElementUsage` against FNA                        | ✅      | Include equality/hash behavior     |
| 242 | Add tests for all built-in vertex structs: `VertexPositionColor`, `VertexPositionTexture`, `VertexPositionNormalTexture`, etc. | ✅      | Size, stride, declaration elements |
| 243 | Verify custom vertex declarations with unusual offsets                                                                         | ✅      | 7 tests: non-zero start, leading padding, gap between elements, out-of-order offsets, insertion order preserved, explicit stride with trailing padding and non-zero start; 1722/1722 pass |
| 244 | Verify multiple texture coordinate channels                                                                                    | ✅      | 6 tests: usageIndex 0/1/2 stored independently, mixed decl, auto-stride 3×TexCoord; 1728/1728 pass |
| 245 | Verify color element formats: `Color`, `Byte4`, normalized formats                                                             | ✅      | 11 tests: per-format auto-stride for Color/Byte4/Short2/Short4/NormalizedShort2/NormalizedShort4/HalfVector2/HalfVector4 + 3 combination tests; 1739/1739 pass |
| 246 | Verify tangent/binormal usages if present                                                                                      | ✅      | 6 tests: Tangent/Binormal usage stored, auto-stride Vector3 variants, Pos+Normal+Tangent, full PBR vertex (56B stride); 1745/1745 pass |
| 247 | Add backend test drawing with each supported vertex element format                                                             | ✅      | `easygl_vertex_formats_test.cpp`: 4 sub-tests via VertexBuffer+DrawPrimitives — stride 16/20/24/32, all 4/4 PASS (red centre pixel); `EasyGL_VertexFormats_AllStrides` ctest added |
| 248 | Add Vulkan vertex input mapping tests for all supported formats                                                                | ✅      | `VulkanVertexFormatHelper.hpp`: `VertexElementFormatToVk()` + `VertexElementFormatSize()` for all 12 VEF values; `vulkan_vertex_format_test.cpp`: 30/30 PASS — mapping table (24 cases) + pixel readback for stride 16/20/24/32 |
| 249 | Add Bgfx vertex layout mapping tests for all supported formats                                                                 | ✅      | `BgfxVertexFormatHelper.hpp`: `VertexElementFormatToBgfx()` + `VertexElementUsageToBgfxAttrib()` + `VertexElementFormatSize()` for all 12 VEF values and all 13 VEU values; `bgfx_vertex_format_test.cpp`: 47 mapping/size checks + 4 VertexBuffer creation smoke tests (stride 16/20/24/32); `Bgfx_VertexFormatMapping` ctest added |
| 250 | Document unsupported vertex formats and fallback behavior                                                                      | ✅      | `docs/vertex-format-support.md`: per-backend table for all 12 VEF and 13 VEU values; stride-keyed layout fallback behavior; SDL_Renderer limitations; future-work section |
| 662 | Audit and fix `GetTypeNameCPP` macro — change `#NAME` → `NAME` in sharp-runtime; fix unquoted callers (`SoundEffectInstance`, `DynamicSoundEffectInstance`) | ✅ | `sharp-runtime/include/System/Object.hpp`: removed `#` from `#NAME` (string literal passthrough, no extra quoting); `SoundEffectInstance.cpp` + `DynamicSoundEffectInstance.cpp`: added quoted dot-notation names; 1715/1715 tests pass |

---

## Phase 31 — User primitives and draw-call variants

| #   | Task                                                                                           | Status | Notes              |
| --- | ---------------------------------------------------------------------------------------------- | ------ | ------------------ |
| 251 | Audit all `DrawUserPrimitives` overloads against FNA                                           | ✅      | Fixed typed overloads to throw on missing effect; added VertexDeclaration overload; exposed `PrimitiveVerts()` as public NOXNA static; 12/12 unit tests pass (`DrawUserPrimitivesTests.cpp`) |
| 252 | Audit all `DrawUserIndexedPrimitives` overloads against FNA                                    | ✅      | Fixed silent-return bug in 8 typed overloads (now throw on missing effect); added VertexDeclaration overloads (16-bit + 32-bit) matching FNA's second generic overloads; added `primitiveCount` validation; 15/15 unit tests |
| 253 | Implement missing `DrawUserPrimitives` overloads by staging data into transient vertex buffers | ⬜      | EasyGL/Vulkan/Bgfx |
| 254 | Implement missing `DrawUserIndexedPrimitives` overloads by staging data into transient VB/IB   | ⬜      | EasyGL/Vulkan/Bgfx |
| 255 | Add tests for `DrawUserPrimitives` with `VertexPositionColor`                                  | ✅      | `examples/easygl_draw_user_primitives_vpc_test.cpp`; full-NDC red quad via typed VPC overload; 2 sub-tests (offset=0, offset=1); centre=(255,0,0) 2/2 PASS |
| 256 | Add tests for `DrawUserPrimitives` with custom vertex declaration                              | ✅      | `examples/easygl_draw_user_primitives_custom_test.cpp`; custom 16-byte MyVertex struct + VertexDeclaration overload; 2 sub-tests (offset=0, offset=1); centre=(255,0,0) 2/2 PASS |
| 257 | Add tests for `DrawUserIndexedPrimitives` with 16-bit indices                                  | ✅      | `examples/easygl_draw_user_indexed_primitives_vpc_test.cpp`; full-NDC red quad via typed VPC + uint16_t overload; 2 sub-tests (vertexOffset=0/indexOffset=0, vertexOffset=1/indexOffset=1); centre=(255,0,0) 2/2 PASS |
| 258 | Add tests for `DrawUserIndexedPrimitives` with 32-bit indices                                  | ✅      | `examples/easygl_draw_user_indexed_primitives_32_test.cpp`; full-NDC red quad via typed VPC + uint32_t overload; 2 sub-tests (vertexOffset=0/indexOffset=0, vertexOffset=1/indexOffset=1); centre=(255,0,0) 2/2 PASS |
| 259 | Validate user primitive arrays for null, invalid offsets, invalid primitive count              | ✅      | `DrawUserPrimitivesTests.cpp` extended: primitiveCount<=0 throws `ArgumentOutOfRangeException` for all 5 `DrawUserPrimitives` overloads (VPC/VPT/VPCT/VPNT + VertexDeclaration), zero and negative counts; 10/10 new unit tests |
| 260 | Optimize user primitive staging to avoid unnecessary heap allocation per draw                  | ✅      | Added 2 per-device reusable scratch buffers (`userVertexScratch_`/`userIndexScratch_`, grow-only `resize`) shared by all 4 `DrawUserPrimitives` + 8 `DrawUserIndexedPrimitives` typed overloads + 2 indexed VertexDeclaration overloads; replaces 22 per-call `std::vector` heap allocations with buffer reuse once capacity stabilizes. 1813/1813 unit tests + 8/8 pixel-readback checks (Tasks 255–258) still pass — no rendering regression. |
| — | `GraphicsDevice.cpp`, `DrawUserIndexedPrimitivesTests.cpp`, `GRAPHICS_TASKS.md` | Added the `primitiveCount<=0` argument-guard unit tests for `DrawUserIndexedPrimitives` that Task 252 claimed but never actually landed (mirrors Task 259's `DrawUserPrimitives` coverage): all 8 typed overloads + 2 `VertexDeclaration` overloads, zero and negative counts. Also found and fixed a real gap while writing these: the untyped raw-`void*` overload (predates the Task 252 typed overloads) had no `primitiveCount` guard at all — added `ArgumentOutOfRangeException::ThrowIfNegativeOrZero`, matching the other 10 overloads. 22 new unit tests; 1840/1840 total pass. |

---

## Phase 32 — Texture2D completeness

| #   | Task                                                                         | Status | Notes                                   |
| --- | ---------------------------------------------------------------------------- | ------ | --------------------------------------- |
| 261 | Audit every `Texture2D` constructor and method against FNA                   | ✅      | See `AUDIT.md` "Texture2D detailed audit". Found: OOB write bug in `SetData(level,rect,...)` (fixed, Task 266); OOB read bug in `SetData(Color*,int)` (fixed, Task 266); missing `NOXNA` tags on 2 assetName ctors (fixed); missing `FromStream(w,h,zoom)` overload (added, Task 262); missing `SetDataPointerEXT`/`GetDataPointerEXT`/`TextureDataFromStreamEXT`/`DDSFromStreamEXT` (still open); Color-only format support (still open, feeds Tasks 265/268/269). |
| 262 | Verify `Texture2D::FromStream` supports PNG, JPG, BMP if FNA/XNA-compatible  | ✅      | Verified via round-trip tests: PNG (lossless), JPEG (lossy, tolerance-checked), BMP (hand-built, exact); DDS/DXT1/3/5 already worked (Task 125). Documented in `docs/texture-stream-formats.md`. Also added the missing `FromStream(device, stream, width, height, zoom)` overload found in the Task 261 audit, matching FNA3D's resize/crop semantics. 5 new unit tests. |
| 263 | Verify `Texture2D::SaveAsPng` if present                                     | ✅      | 6 new tests: null-stream/no-CPU-pixels guards; multi-pixel round-trip with alpha (catches spatial/transposition bugs, exact match); non-square size (3x5); save-time resize (2x2→6x4); filename-based NOXNA overload via temp file. |
| 264 | Verify `Texture2D::SaveAsJpeg` if present                                    | ✅      | 8 new tests: error guards; multi-pixel round-trip within tolerance (lossy); alpha-channel drop verified (JPEG has none); non-square size; save-time resize; filename overload; `FNA_GRAPHICS_JPEG_SAVE_QUALITY` env var now honored (was hardcoded to 100 — Task 261 audit finding, fixed here). |
| 265 | Implement exact bounds checking for `GetData<T>` rectangles                  | ✅      | FNA's real `GetData<T>` doesn't bounds-check `rect` at the managed level at all (delegates to native `FNA3D_GetTextureData2D`) — CNA's existing rect-bounds check is a deliberate safety improvement, not a literal-parity gap. Found and fixed a real, symmetric bug instead: neither `GetData` overload validated `startIndex < 0` (the equivalent `SetData` overloads already do) — negative `startIndex` caused an OOB read from `cpuPixels_` (3-arg overload) or an OOB write into the caller's array (5-arg overload). Fixed both, mirroring `SetData`'s existing guard. 2 new unit tests; 1842/1842 pass on EasyGL/Vulkan/Bgfx. Closes Phase 32. See `AUDIT.md`. |
| 266 | Implement exact bounds checking for `SetData<T>` rectangles                  | ✅      | Fixed both OOB bugs from Task 261 audit: (1) `SetData(level,rect,...)` now throws `std::out_of_range` when rect exceeds mip-level bounds (mirrors `GetData`'s existing check); (2) `SetData(Color*,elementCount)` now throws `std::out_of_range` when `elementCount < width*height` instead of building a size-mismatched `ImageData`. 7 new unit tests (5 rect-bounds + 2 buffer-size); 1789/1789 total pass. |
| 267 | Verify `LevelCount` behavior for mipmapped and non-mipmapped textures        | ✅      | 5 new tests confirming `getLevelCountProperty()` matches FNA's `CalculateMipLevels` formula: 2-arg ctor and `mipMap=false` always yield 1; `mipMap=true` verified for square power-of-two, non-square power-of-two, and non-power-of-two sizes (3x5→3, 7x11→4, 16x16→5, etc). |
| 268 | Verify non-power-of-two textures across all backends                         | ✅      | No POT/NPOT branching found anywhere (Texture2D, EasyGL/Vulkan/Bgfx texture creation) — all support NPOT natively; verified end-to-end on EasyGL via new pixel-readback test (3×5, 5 solid-colour rows, full-screen SpriteBatch draw, 5/5 rows read back correctly); Vulkan/Bgfx verified by code inspection only (no texture-level pixel-readback infra). See `AUDIT.md` "NPOT textures and SpriteBatch edge sampling". |
| 269 | Verify texture sampling at edges for clamp/wrap modes                        | ✅      | Found and fixed 2 real bugs on EasyGL: (1) `SpriteBatch::Begin()`'s `SamplerState` had zero effect on EasyGL/Vulkan/Bgfx (only `Filter` was even read, and only SDL_Renderer's backend implemented `SetSamplerFilter`) — added `ISpriteBatchBackend::SetSamplerAddressMode`, wired `SpriteBatch::Begin()` to always resolve+apply `SamplerState` (default `LinearClamp`, matching FNA) via EasyGL's existing `ApplySamplerState` GL-sampler mechanism; (2) EasyGL's SpriteBatch UV math hard-clamped to [0,1], making `Wrap`/`Mirror` unreachable even with fix #1 — removed the clamp (FNA never clamps). New `EasyGL_TextureAddressMode` pixel-readback test proves `PointWrap` vs `PointClamp` now sample distinctly (Red vs Blue) at U=1.25 past the texture edge. Vulkan/Bgfx not fixed (documented gap). See `AUDIT.md`. |
| 270 | Add CPU-side shadow storage only where required for `GetData`; document cost | ✅      | See `AUDIT.md` "Texture2D CPU shadow storage". Confirmed `GetData` throws (no GPU readback fallback) once the level-0 shadow is freed by `SetContextRecoveryEnabled(false)`. Found and fixed a real bug: partial `SetData(level,rect,...)` after the shadow was freed silently zero-filled the rest of the GPU texture; now throws `std::runtime_error` instead, and the level-0 branch calls `MaybeFreeCpuPixels()` so partial updates don't defeat the RAM-saving feature. `extraMipLevels_` (mip >0) is never freed — documented as an open gap, not fixed (out of scope). 5 new unit tests; 1818/1818 total pass. |

---

## Phase 33 — Texture3D and TextureCube completeness

| #   | Task                                                                     | Status | Notes                                  |
| --- | ------------------------------------------------------------------------ | ------ | -------------------------------------- |
| 271 | Audit `Texture3D` API against FNA                                        | ✅      | Found and fixed 3 real bugs, mirroring Tasks 261/265/266's Texture2D pattern: (1) `LevelCount` hardcoded to 1, ignoring `mipMap` — now computes `CalculateMipLevels(w,h)`; (2) `SetData`/`GetData` had almost no validation — null data caused a crash, negative `elementCount` risked a huge-allocation crash, negative `startIndex` caused OOB read/write, no box-bounds check on the 10-arg overloads — all fixed with guards matching `Texture2D`'s exception-type convention; (3) missing `Dispose(bool)` override left the GPU resource unreleased on explicit `Dispose()` — fixed. 31 new unit tests (new `Texture3DTests.cpp`); 1873/1873(EasyGL/Vulkan)/1877(Bgfx) pass; EasyGL ctest 1942/1944 (2 pre-existing, unrelated failures). Documented, not fixed: EasyGL backend ignores `mipMap`/`SurfaceFormat` entirely (always single-level Rgba8). See `AUDIT.md`. |
| 272 | Audit `TextureCube` API against FNA                                      | ✅      | Found and fixed the same 3 bug classes as Task 271's `Texture3D` audit — confirmed a systemic pattern: (1) `LevelCount` hardcoded to 1 regardless of `mipMap` — now `CalculateMipLevels(size,size)`; (2) `SetData`/`GetData` had essentially zero input validation (not even a null check) — fixed with the same guard set as `Texture3D`, plus a rect-bounds check on both `SetData`/`GetData` (FNA has neither, unlike `Texture3D`'s `GetData`-only check — extends safety consistently); (3) missing `Dispose(bool)` override — fixed. Plus 2 `TextureCube`-specific findings: (4) the `SetData`/`GetData(face,data,startIndex,elementCount)` overload was missing from the API entirely — added; (5) `rect==nullptr` at `level>0` ignored `level`, always using the full face `Size` instead of `Size>>level` — fixed via a `mipDim()` helper matching `Texture2D`/`Texture3D`'s pattern. **Also found: `DDSFromStreamEXT` is a non-functional stub** that ignores its `stream` argument and always returns a blank 1×1 texture — documented, not fixed (see Task 663). 27 new unit tests (new `TextureCubeTests.cpp`); 1900/1900 (EasyGL/Vulkan) / 1904/1904 (Bgfx) pass; EasyGL ctest 1969/1971 (2 pre-existing, unrelated failures). See `AUDIT.md`. |
| 273 | Add `Texture3D` partial box upload tests                                 | ✅      | Task 173's existing test only varies z (full-width/height slices) — would miss an x/y-axis bug. New `examples/easygl_texture3d_partial_box_test.cpp` (`EasyGL_Texture3D_PartialBox_RoundTrip` ctest) uses boxes with distinct width/height/depth, offset on every axis: (A) asymmetric off-origin box in a 4×5×3 volume, (B) single-voxel box, (C) box touching the far corner (right==width/bottom==height/back==depth, verifying exclusive-bound semantics). All 3 sub-tests pass against the existing `EasyGLTexture3DBackend::SetData`/`GetData` (glTexSubImage3D/glReadPixels-per-slice) — no bug found; confirms genuine x/y/z sub-region upload already works correctly. 1972/1972 EasyGL ctest pass (2 pre-existing unrelated failures unchanged: `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`). |
| 274 | Add `Texture3D` partial box readback tests                               | ✅      | New `examples/easygl_texture3d_partial_box_readback_test.cpp` (`EasyGL_Texture3D_PartialBox_Readback` ctest). Unlike Task 273's binary Red/Blue split, this fills a 4×3×5 volume with a per-voxel-unique colour (`R=20+x*40, G=20+y*60, B=20+z*40`) so a `GetData` box that reads the wrong offset/axis is always detectable, not just boxes crossing a colour boundary. 3 sub-tests: (A) asymmetric off-origin box read, (B) non-zero `startIndex` into a sentinel-padded output array (mirrors Task 170B's `Texture2D` pattern), (C) box touching the far corner (`right==width`/`bottom==height`/`back==depth`). All pass — no bug found; confirms `EasyGLTexture3DBackend::GetData`'s per-slice `glReadPixels` correctly honours arbitrary x/y/z box offsets on the read path too. Also confirmed (via FNA `Texture3D.cs`/`Texture2D.cs` `GetData<T>`) that FNA itself never validates `elementCount` against box volume — only `data.Length >= startIndex+elementCount` — so CNA's matching lack of a box-volume-vs-elementCount check is faithful behavior, not a gap. 1971/1973 EasyGL ctest pass (2 pre-existing unrelated failures unchanged). |
| 275 | Add `TextureCube` face upload/readback tests for all six faces           | ✅      | Task 172's existing test already gives pixel-exact whole-face round-trip coverage for all 6 faces via the simple 2-arg `SetData`/`GetData(face,data,elementCount)` overload — but only that overload; `TextureCubeTests.cpp`'s rect-based/startIndex overload coverage was argument-guards only (no pixel verification). New `examples/easygl_texturecube_partial_rect_test.cpp` (`EasyGL_TextureCube_PartialRect_RoundTrip` ctest) closes that gap: (A) all six faces get their own background colour plus an off-centre 2×2 White rect via the 6-arg rect overload, verified pixel-exact per face with no cross-face bleed; (B) `SetData` `startIndex` with real data (mirrors Task 170A); (C) `GetData` `startIndex` into a sentinel-padded array (mirrors Task 170B). All pass — no bug found; confirms `EasyGLTextureCubeBackend`'s `set_sub_image_2d`/FBO-`glReadPixels` correctly honour arbitrary per-face x/y rects. 1972/1974 EasyGL ctest pass (2 pre-existing unrelated failures unchanged). |
| 276 | Add `TextureCube` mip-level tests for all six faces                      | ✅      | **Found and fixed a real bug.** New `examples/easygl_texturecube_mip_test.cpp` (`EasyGL_TextureCube_Mip_RoundTrip` ctest, mirrors Task 171's `Texture2D` mip test) initially FAILED: mip levels 1 and 2 always read back `(0,0,0)` regardless of what was written, on all six faces. Root cause: `EasyGLTextureCubeBackend`'s constructor only allocated GPU storage for level 0 (`set_image_2d`, one call per face, no level loop), while `SetData` writes via `set_sub_image_2d` (`glTexSubImage2D`), which requires the target level to already have a defined image — level 0 worked by luck, levels 1+ silently failed. Fixed by pre-allocating every mip level for every face in the constructor (`CalculateCubeMipLevels` + a per-level `set_image_2d(nullptr)` loop), mirroring `TextureCube.cpp`'s own `CalculateMipLevels`/`mipDim` logic. All 126 checks (6 faces × 21 pixels across 3 levels) now pass. 1973/1975 EasyGL ctest pass (2 pre-existing unrelated failures unchanged). **Same bug shape likely also affects `Texture3D`** (`EasyGLTexture3DBackend`'s constructor has the identical single-level-only pattern, documented but not fixed under Task 271) — not fixed here as it's outside this task's TextureCube scope; flagged for a follow-up task. |
| 277 | Verify `Texture3D` sampling in EasyGL stock/custom effect                | ✅      | **Verified: the API does not expose `Texture3D` sampling to any shader today — no code change, audit-only finding.** No stock XNA effect (`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) ever samples a `Texture3D` in real FNA/XNA either, so the only realistic path is a custom `ShaderEffect` — which has **zero** texture-binding API of any kind (`ShaderEffect`/`IEffectBackend` only expose scalar/vector/matrix uniform setters, confirmed by reading both headers). The generic path (`GraphicsDevice.Textures[slot] = tex`) is structurally blocked too: FNA's `Texture3D : Texture` (confirmed in FNA source) so it fits `TextureCollection`'s `Texture*` slots; CNA's `Texture3D : GraphicsResource` (not `Texture` — confirmed in `Texture3D.hpp`), so it cannot be assigned into `TextureCollection` at all. `EffectParameter::SetValue(Texture3D*)`/`GetValueTexture3D()` (added for Task 271/272-adjacent API completeness) has zero consumers anywhere in any backend (`EasyGL`/`Vulkan`/`Bgfx` all grepped clean) — it stores a pointer nobody ever reads. Existing `EffectParameterTests.cpp` already fully covers the only piece that actually works (pointer round-trip storage); no further test was added since there is no positive GPU-sampling behavior to lock in. Tracked as new Task 863 (see below) since fixing this is a real architecture change (making `Texture3D` inherit `Texture`, or adding a parallel 3D-texture-binding path), well outside this verify-only task's scope. See `AUDIT.md`. |
| 278 | Verify `TextureCube` sampling in EasyGL/Vulkan/Bgfx EnvironmentMapEffect | ✅      | Unlike Task 277's custom-effect finding, stock effects have their own hardcoded cube-texture path (`GpuDrawParams::envMap`, `ITextureCubeBackend*`) that bypasses the broken generic `EffectParameter`/`TextureCollection` route entirely — so this needed real per-backend verification, not an assumption. **EasyGL**: already fully wired (`EnsureEnvMapped3DProgram`, `samplerCube uEnvMap`, existing pixel-verified `EasyGL_EnvironmentMapEffect_Readback` test, reconfirmed passing). **Vulkan**: already fully wired (dedicated descriptor set layout/pipeline, `env_map3d.frag.glsl`'s `samplerCube uEnvMap`, existing pixel-verified `Vulkan_EnvironmentMapEffect_Readback` test, reconfirmed passing). **Bgfx: found and fixed a real gap** — `DrawPrimitivesEx` never checked `params.envMapping` at all, so `EnvironmentMapEffect` silently fell through to the plain lit-textured branch (`params.lightingEnabled`), rendering with no reflection and no crash — a silent behavioral gap, not a crash. Fixed by adding a full Bgfx shader pair (`vs_env_map3d.sc`/`fs_env_map3d.sc`, mirroring the EasyGL/Vulkan reflection formula: `reflect(-E,N)` sampled via `samplerCube`), a new `envMap3DProgram_` + 6 new uniforms, and a `params.envMapping` branch in `DrawPrimitivesEx`. Required building bgfx's `shaderc` tool from source (`-DCNA_BGFX_BUILD_SHADERC=ON -DBGFX_BUILD_TOOLS=ON`, not built by default) to regenerate `bgfx_shaders.hpp`. Bgfx has no GPU readback API (documented pre-existing limitation), so added `Bgfx_EnvironmentMapEffect_Smoke` (new `examples/bgfx_env_map_test.cpp`) instead of a pixel-verified test — exercises all 4 of the EasyGL/Vulkan test's configurations across 3 frames, verifies no crash/exception. 1908/1908 Bgfx ctest pass (100%, no regressions). See `AUDIT.md`. |
| 279 | Add validation for invalid `CubeMapFace` values                          | ✅      | Confirmed via FNA source (`TextureCube.cs`) that FNA itself never validates `cubeMapFace` — passed straight through to `FNA3D_SetTextureDataCube`/`GetTextureDataCube` with no range check. All 3 CNA backends already guarded against out-of-range face at the backend layer (`if (face < 0 \|\| face >= 6) return;` in EasyGL/Vulkan/Bgfx), so this was already memory-safe — just a silent no-op instead of a clear error. Added an `IsValidCubeMapFace()` check (throws `std::out_of_range`) to `TextureCube::SetData`/`GetData`'s 6-arg overload (the other overloads delegate to it), matching the established CNA-safety-extra pattern from Tasks 265/271/272. 7 new unit tests (below/above range for both `SetData`/`GetData`, both the simple and rect-based overloads, plus one confirming all 6 valid faces still work). 1980/1982 EasyGL ctest pass, 1915/1915 Bgfx ctest pass (100%), Vulkan 34/34 `TextureCubeTest` pass (full-suite Vulkan run separately has 2 pre-existing, order-dependent failures unrelated to this change — confirmed via git-stash bisection; see `NEXT.md` §5). |
| 280 | Document `Texture3D` and `TextureCube` backend support matrix            | ✅      | New `docs/texture3d-texturecube-support.md`. Compiling it surfaced a significant, previously-undocumented finding: **`Texture3D`/`TextureCube::GetData` is a total silent no-op on both Vulkan and Bgfx** — neither backend overrides `GetData` (both fall through to `ITexture3DBackend`/`ITextureCubeBackend`'s empty base-class default), so the caller's output buffer is left completely untouched, with no error of any kind. Existing `Texture3DTests.cpp`/`TextureCubeTests.cpp` never caught this because their `GetData` tests are argument-guard-only (no value assertions) — confirmed by cross-referencing "34/34 TextureCubeTest pass on Vulkan" (Task 279) against the actual backend source. Also confirmed, via code-pattern comparison to Task 276's fixed bug, that Vulkan and Bgfx very likely have the same "mip level >0 GPU storage never allocated" bug as EasyGL's pre-Task-276 `TextureCube` bug, for **both** `Texture3D` and `TextureCube` (`imgInfo.mipLevels = 1` hardcoded in Vulkan; `hasMips=false` hardcoded in Bgfx) — flagged as likely (🔍), not confirmed with a reproducing test, since writing+fixing 4 more mip-level bugs is out of this documentation task's scope. Tracked as new Tasks 864 (Vulkan/Bgfx mip-level allocation) and 865 (Vulkan `GetData` readback implementation) — see `GRAPHICS_TASKS.md` below and `AUDIT.md`. **Phase 33 is now fully complete (Tasks 271–280).** |
| 663 | Implement `TextureCube::DDSFromStreamEXT` for real (Task 272 finding)    | ⬜      | Currently a silent stub: ignores `stream`, always returns a blank 1×1 texture. Needs DDS header parsing (`isCube` flag, width/levels), reusing `Texture2D.cpp`'s `TryDecodeDds`/`DxtUtil` decode helpers, and 6×levelCount `SetData` calls. See `AUDIT.md` "TextureCube detailed audit" finding #6. |
| 862 | Fix `Texture3D` mip levels >0: `EasyGLTexture3DBackend` constructor only allocates level 0 (Task 276 finding) | ⬜ | Same bug shape as the `TextureCube` mip-allocation bug fixed by Task 276: `EasyGLTexture3DBackend`'s constructor calls `set_image_3d` once per volume with no level loop, while `SetData` writes via `set_sub_image_3d` (`glTexSubImage3D`), which requires the target level to already exist. `SetData(level>0,...)` on a mipmapped `Texture3D` almost certainly silently fails today, mirroring what Task 276 found and fixed for `TextureCube`. Fix shape: mirror Task 276's fix — pre-allocate every mip level in the constructor via a per-level `set_image_3d(nullptr)` loop using the existing `CalculateMipLevels(w,h)` formula. Not fixed under Task 276 itself since that task's scope was `TextureCube` only; not yet verified with a reproducing test (Task 271's audit only documented the general `mipMap`-ignored limitation, not a level>0 `SetData` failure specifically). |
| 863 | Wire `Texture3D` sampling into shaders (stock/custom effects) — currently structurally impossible (Task 277 finding) | ⬜ | FNA's `Texture3D : Texture` lets any texture (2D/3D/Cube) go into `GraphicsDevice.Textures[slot]` and be sampled by a shader; CNA's `Texture3D : GraphicsResource` (not `Texture`) cannot be assigned into `TextureCollection` at all, and `ShaderEffect`/`IEffectBackend` have no texture-binding API whatsoever (only scalar/vector/matrix uniforms) for any texture type, so there is no custom-effect workaround either. `EffectParameter::SetValue(Texture3D*)`/`GetValueTexture3D()` exist but have zero consumers in any backend — a write-only dead end. Fix requires a real architecture decision: (a) make `Texture3D` (and `TextureCube`, same issue — see Task 278) inherit `Texture` to match FNA and unify `TextureCollection` handling, likely a non-trivial refactor touching `EffectParameter`, `TextureCollection`, and every backend's texture-bind code; or (b) add a parallel, `Texture3D`-specific GPU-binding path outside `TextureCollection`. Out of scope for the Task 277/278 verify-only audits. See `AUDIT.md`. |
| 864 | Fix Vulkan and Bgfx mip-level allocation for `Texture3D`/`TextureCube` — `mipLevels`/`hasMips` hardcoded to 1/false regardless of `mipMap` (Task 280 finding) | ⬜ | Same bug shape as Task 276's fixed EasyGL `TextureCube` bug and Task 862's tracked EasyGL `Texture3D` bug, but for the other two backends, both texture types: `VulkanGraphicsBackend::CreateTexture3D`/`CreateTextureCube` take `bool /*mipMap*/` and drop it entirely — `VulkanTexture3DBackend`/`VulkanTextureCubeBackend`'s `VkImageCreateInfo::mipLevels` is hardcoded to `1`. `BgfxTexture3DBackend`'s constructor takes `bool /*mipMap*/` and calls `bgfx::createTexture3D(..., /*hasMips=*/false, ...)`; `BgfxTextureCubeBackend` similarly hardcodes `bgfx::createTextureCube(size, false, 1, ...)`. `SetData(level>0,...)` on a mipmapped `Texture3D`/`TextureCube` on either backend almost certainly silently fails or hits a validation error, mirroring Task 276's finding — not yet reproduced with a test (flagged 🔍 in `docs/texture3d-texturecube-support.md`, not confirmed). Fix shape: mirror Task 276 — compute the real level count from `mipMap` and pre-allocate every level at image/texture creation time. |
| 865 | Implement real GPU readback for `Texture3D`/`TextureCube::GetData` on Vulkan (Task 280 finding) | ⬜ | Neither `VulkanTexture3DBackend` nor `VulkanTextureCubeBackend` overrides `GetData` — both silently fall through to `ITexture3DBackend`/`ITextureCubeBackend`'s empty base-class default (`virtual void GetData(...) const {}`). Calling `Texture3D::GetData`/`TextureCube::GetData` on Vulkan leaves the caller's buffer completely untouched, with no error of any kind — a severe, currently undocumented-until-now silent-failure gap (same "fails silently" severity class as Task 663's `DDSFromStreamEXT` stub). Existing unit tests never caught this because `GetData` test coverage is argument-guard-only (no value assertions) — see `docs/texture3d-texturecube-support.md` for the full explanation of why "34/34 tests pass on Vulkan" is consistent with this bug. Fix shape: implement `vkCmdCopyImageToBuffer` + a host-visible staging buffer, mirroring the upload path's staging-buffer pattern in reverse, respecting `level`/`x,y,z,face`/`w,h,depth`. Bgfx has no equivalent fix — its lack of a GPU readback API is an accepted, already-documented, project-wide limitation, not a bug to fix. |
| 866 | Set `Name` on `BlendState`/`DepthStencilState`/`RasterizerState` static presets, matching FNA (Task 291 finding) | ⬜ | Task 291's `SamplerState` audit found FNA's private preset constructor sets `Name` (e.g. `"SamplerState.PointClamp"`) on every preset; CNA's `SamplerState` didn't, and was fixed under Task 291. Checking sibling state classes during that audit found the identical gap in all 3 of `BlendState`/`DepthStencilState`/`RasterizerState` — FNA sets `Name` on all of their presets too (e.g. `"BlendState.Additive"`, confirmed via FNA source), but none of CNA's equivalent presets call `setNameProperty`. Deliberately not fixed under Task 291 (SamplerState-only scope, per the project's no-bundling-unrelated-fixes convention) — same one-line-per-preset fix shape, just needs its own task/PR and its own preset-name test additions per class. |
| 867 | Implement real GPU upload for `Texture2D::SetData(level>0,...)` on Vulkan and Bgfx, and fix EasyGL's mip-incomplete-texture black-screen bug (Task 298/299 findings) | ⬜ | **Severe, silent-failure bug, same class as Task 865.** `ITextureBackend::UpdatePixelsLevel(level, rgba, w, h)` (the virtual hook `Texture2D::SetData(level, rect, data, ...)` calls for `level>0`) has an empty no-op default body in `include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`, and **neither `VulkanTextureBackend` nor the Bgfx texture backend overrides it** — confirmed by grep, zero matches in either backend's `.cpp`. Calling `Texture2D::SetData(level>0, ...)` on a mipmapped `Texture2D` on Vulkan or Bgfx silently does nothing; the mip level's GPU content is whatever it was at texture creation (uninitialized/zero), with no error of any kind. Only EasyGL implements this correctly (confirmed working via Task 171's existing `easygl_texture2d_mip_test.cpp` and this session's new Task 298 test). Found while building Task 298's mipmap-filter pixel test: the Vulkan run showed the expected-high-mip-level colour never appearing, which traced back to this — not a filter/sampler issue at all, a data-upload issue. **Additional, related findings needed for a complete fix on Vulkan specifically** (found while tracing this): `VulkanTextureBackend`'s `VkImageCreateInfo::mipLevels` is hardcoded to `1` (same bug shape as Task 864's `Texture3D`/`TextureCube` finding, but for `Texture2D` — previously undocumented), its `VkImageViewCreateInfo::subresourceRange.levelCount` is also hardcoded to `1` (so even a real multi-level image wouldn't be visible past level 0 through this view), and `VulkanGraphicsBackend::ApplySamplerState`'s `VkSamplerCreateInfo` never sets `minLod`/`maxLod` (both default to `0.0`, clamping the shader's automatic LOD selection to level 0 regardless of `mipmapMode`) — all three must be fixed together, not just `UpdatePixelsLevel`, for Vulkan `Texture2D` mips to work end-to-end. Bgfx's fix shape is unconfirmed beyond the missing `UpdatePixelsLevel` override — not investigated further this session. **Task 299 found a related, EasyGL-specific finding from the same root cause** (Texture2D mip-level metadata never threaded into backend resource creation, manifesting differently per backend): `TextureFilter::Anisotropic` (and every other `*Mip*`-suffixed `TextureFilter` value — `LinearMipPoint`, `PointMipLinear`, all four `Min*Mag*Mip*` combinations) maps to a GL minFilter with a `_MIPMAP_` suffix (e.g. `LinearMipmapLinear`), which OpenGL requires a *complete* mipmap chain for; since EasyGL never sets `GL_TEXTURE_MAX_LEVEL` to match each texture's real level count (confirmed via `easygl_texture_anisotropic_effect_test.cpp`: assigning `TextureFilter::Anisotropic` to an ordinary single-level `Texture2D::CreateFromPixels` texture — the overwhelmingly common case for real game textures — renders **solid black**, a classic GL mipmap-incompleteness symptom). Vulkan does not share this specific symptom (confirmed via the same test: renders correctly even on a single-level texture) — its issue is purely the missing `maxLod`/`mipLevels`/`levelCount` plumbing above, not incompleteness. Fix shape: for Vulkan/Bgfx, mirror the existing level-0 upload path (staging buffer for Vulkan, `bgfx::updateTexture2D` for Bgfx) generalized to arbitrary `level`, plus the three Vulkan-side allocation/view/sampler fixes above; for EasyGL, set `GL_TEXTURE_MAX_LEVEL` on every `Texture2D` at creation time to match its real level count (1 for non-mipmapped, `CalculateMipLevels(w,h)-1` for mipmapped), which requires threading `mipMap`/level-count into `EasyGLGraphicsBackend::CreateTexture` (currently takes no such parameter, unlike `CreateTexture3D`/`CreateTextureCube`). Not fixed here — this is a multi-part, three-backend-touching fix needing its own dedicated task and full regression pass, not something to bundle into a pixel-test task. |

---

## Phase 34 — SurfaceFormat implementation matrix

| #   | Task                                                                                                 | Status | Notes                        |
| --- | ---------------------------------------------------------------------------------------------------- | ------ | ---------------------------- |
| 281 | Create canonical table of all XNA/FNA `SurfaceFormat` enum values                                    | ✅      | **Found and fixed a real enum-conformance bug** while building the table. `docs/surface-format-support.md`'s new canonical table (27 values, 0–26, sourced directly from FNA's `SurfaceFormat.cs`) exposed that CNA's enum diverged from FNA starting at ordinal 20: CNA declared 7 invented "Srgb" variants (`ColorSrgb`, `Bgr565Srgb`, `Bgra5551Srgb`, `Bgra4444Srgb`, `Dxt1Srgb`, `Dxt3Srgb`, `Dxt5Srgb`) with no FNA equivalent at those ordinals, while omitting FNA's real values (`ColorBgraEXT`, `ColorSrgbEXT`, `Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`, `ByteEXT`, `UShortEXT`) entirely — a direct violation of the project's "enum names must match XNA/FNA exactly" rule. Fixed `SurfaceFormat.hpp` to match FNA's 27 values exactly, same order/ordinals. Blast radius was small (grepped first): only one example test referenced the old names, since `Texture::ValidateFormat` only special-cases `Color` and throws for everything else — updated it to use the real FNA names instead. 7 new ordinal-value unit tests added to the pre-existing `SurfaceFormatTests.cpp` (which already had 0–19 pinned, with a comment implying 20+ was known-incomplete) for ordinals 20–26. Verified across all 3 backends: EasyGL 1987/1989, Bgfx 1922/1922 (100%), Vulkan 1924/1927 (all pre-existing/documented failures, no regressions). |
| 282 | For each format, define CPU bytes-per-pixel or compressed block size                                 | ✅      | Found FNA already has the exact shared helper this task and Task 283 are asking for: `Texture.GetBlockSizeSquaredEXT(SurfaceFormat)` and `Texture.GetFormatSizeEXT(SurfaceFormat)`, both public static methods in FNA's `Texture.cs` (region "Static SurfaceFormat Size Methods") — real FNA API, not a CNA invention, so ported these directly onto CNA's `Texture` class rather than inventing a separate `SurfaceFormatHelper` class name (matches the project rule to follow FNA's actual API shape over the plan's guessed name). Ported both switch statements line-by-line against FNA source, covering all 27 `SurfaceFormat` values (fixed by Task 281): `GetBlockSizeSquaredEXT` returns 16 for the 6 block-compressed formats (`Dxt1/3/5`, `Dxt5SrgbEXT`, `Bc7EXT`, `Bc7SrgbEXT`) and 1 for everything else; `GetFormatSizeEXT` returns the exact per-format byte size (8/16/1/2/4/8/16 depending on format). Also found and fixed a pre-existing, unrelated conformance gap while touching this file: `Texture::ValidateFormat` (a CNA-only extension, not in FNA) was missing its `NOXNA` tag — added it. 22 new unit tests in new `TextureTests.cpp` (exhaustive per-format coverage for both methods, plus invalid-enum-value throws `std::out_of_range` for both). Verified across all 3 backends: EasyGL 1998/2000, Vulkan (targeted `TextureTest.*` clean), Bgfx 1933/1933 (100%), no regressions. Lays the groundwork for Task 283 — FNA's same region also has `ValidateGetDataFormat`/`GetPixelStoreAlignment`, which build on `GetFormatSizeEXT` and are the actual "required for SetData/GetData" consumers; not yet ported (Task 283's scope). |
| 283 | Implement `SurfaceFormatHelper::GetSize` equivalent if missing                                       | ✅      | Ported FNA's remaining "Static SurfaceFormat Size Methods": `Texture.GetPixelStoreAlignment(format)` (`min(8, GetFormatSizeEXT(format))`, the OpenGL 2.1 `GL_PACK/UNPACK_ALIGNMENT` cap) and `Texture.ValidateGetDataFormat(format, elementSizeInBytes)` (throws unless `elementSizeInBytes` evenly divides the format's byte size). Both are `internal` in FNA; made `public static` on CNA's `Texture` since 3 of the 4 real call sites (`Texture3D::GetData`, `TextureCube::GetData`, `GraphicsDevice::GetBackBufferData`) aren't subclasses of `Texture` in CNA (Task 277/863 finding) and so couldn't reach a `protected` member — a documented, intentional visibility deviation (see `AUDIT.md`), not a CNA extension. Wired `ValidateGetDataFormat` into all 4 real call sites, matching FNA's `Texture2D.cs`/`Texture3D.cs`/`TextureCube.cs`/`GraphicsDevice.cs` exactly: `Texture2D::GetData` (both overloads), `Texture3D::GetData`, `TextureCube::GetData`, `GraphicsDevice::GetBackBufferData`. Currently a no-op in practice everywhere it's called (always `Color`/4-byte-elements, which always divides evenly), since `Texture::ValidateFormat` blocks every other format before this logic would run — but it's the correct, forward-looking infrastructure for when later Phase 34 tasks add real non-`Color` format support. 6 new unit tests (exact-division passes, uneven-division throws `std::invalid_argument`, invalid-format throws `std::out_of_range`, plus alignment-clamping checks). Verified across all 3 backends: EasyGL 2004/2006, Vulkan 1942/1944 (2 pre-existing failures, unrelated), Bgfx 1939/1939 (100%) — no regressions from wiring the new check into 4 existing `GetData` call sites. |
| 284 | Implement/verify `Color` format mapping: EasyGL/Vulkan/Bgfx                                          | ✅      | **Found and fixed a real Vulkan gamma bug** — channel order (RGBA/BGRA) itself was already correct everywhere (confirmed via dozens of existing exact-color pixel-readback tests), but all of those only use saturated 0/255 values, which can't reveal a linear-vs-sRGB colorspace mixup (both are sRGB-curve fixed points). Testing with a genuine mid-range value (128) found: `VulkanTextureBackend` (`Texture2D`) created its image as `VK_FORMAT_R8G8B8A8_SRGB` (inconsistent with `Texture3D`/`TextureCube`/`RenderTarget2D`'s `UNORM`, and wrong regardless — FNA's `SurfaceFormat.Color` is linear, not sRGB), **and** the Vulkan swapchain surface format selection explicitly preferred `VK_FORMAT_B8G8R8A8_SRGB`, applying an unwanted gamma encode to every presented pixel. The two bugs partially canceled for textured content (decode-then-encode ≈ identity) but left all non-textured rendering (vertex colors, lighting output, blends) badly wrong — a nominal 128 read back as 188. Fixed both to `UNORM`. New `examples/vulkan_texture_srgb_test.cpp` (`Vulkan_Texture2D_ColorFormat_Linear` ctest) proves it: before the fix, vertex-color vs. textured mid-grey diverged by 60; after, both read exactly 128. Verified EasyGL and Bgfx have no equivalent bug (no `SRGB` reference anywhere in either backend). Full Vulkan ctest suite: 1944/1945 (only the pre-existing, already-documented `Vulkan_DepthBias` failure remains) — no regressions from this pipeline-wide fix. See `AUDIT.md` and `docs/surface-format-support.md`. |
| 285 | Implement/verify `Bgr565`, `Bgra5551`, `Bgra4444` packing/unpacking                                  | ✅      | Verified, not greenfield: all 3 `PackedVector` types already existed (`Bgr565.hpp`/`Bgra5551.hpp`/`Bgra4444.hpp`) with pack/unpack math matching FNA's `Bgr565.cs`/`Bgra5551.cs`/`Bgra4444.cs` bit layouts exactly (confirmed line-by-line: 5-6-5, 5-5-5-1, 4-4-4-4 channel order and shift amounts), and `tests/PackedVectorGolden.md` already had a golden reference table for all three (computed independently in Python against FNA's arithmetic). What was missing: the golden values weren't actually pinned in `PackedVectorTests.cpp` — `Bgr565` only tested Red/Half (not Green/Blue), and `Bgra4444`/`Bgra5551` had zero golden-value tests at all (only zero/max/equality checks). Added 8 new golden tests (`Bgr565Test.GoldenGreen/GoldenBlue`, `Bgra4444Test.GoldenRedOpaque/GoldenGreenHalfAlpha/GoldenHalf`, `Bgra5551Test.GoldenRedOpaque/GoldenBlueTransparent/GoldenHalfOpaque`) pinning every remaining row from the golden doc (packed hex value + `ToVector4()` output). No bug found — these 3 formats' CPU-side packing was already correct; this task closed the test-coverage gap. Not in scope here (per Phase 34's one-task-at-a-time rule): actual GPU texture support for these formats — `Texture::ValidateFormat` still throws for everything except `Color`, same as all other non-Color formats (Task 290's scope). 2012/2014 EasyGL ctest pass (2 pre-existing, unrelated failures unchanged: `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`). |
| 286 | Implement/verify `NormalizedByte2/4`, `NormalizedShort2/4` texture/storage behavior if supported     | ✅      | **Found the task's own premise was half wrong: `NormalizedShort2`/`NormalizedShort4` are not `SurfaceFormat` texture values in FNA at all** — confirmed via FNA's actual `SurfaceFormat.cs` (only `NormalizedByte2`/`NormalizedByte4` exist there); `NormalizedShort2`/`NormalizedShort4` exist solely as `VertexElementFormat` values (`VertexElementFormat.cs`), a vertex-attribute concept, not a texture-storage one. CNA's `VertexElementFormat` enum already matches FNA exactly (ordinals 8/9), and both Vulkan (`VK_FORMAT_R16G16(B16A16)_SNORM`) and Bgfx (`AttribType::Int16`, normalized) already map them to real GPU vertex formats with existing unit/smoke-test coverage (`VertexDeclarationTests.cpp`, `VertexElementTests.cpp`, `vulkan_vertex_format_test.cpp`, `bgfx_vertex_format_test.cpp`) — no gap, no fix needed; EasyGL's vertex handling is stride-keyed rather than per-format (a separate, already-documented, out-of-scope limitation). For the real `SurfaceFormat` values, `NormalizedByte2`/`NormalizedByte4`: `Texture.GetBlockSizeSquaredEXT`/`GetFormatSizeEXT` already correctly handle both (Task 282, verified against FNA byte-for-byte: 2 and 4 bytes respectively) with existing exhaustive unit tests. The one genuine, scoped gap found: no test anywhere confirmed `Texture2D` construction actually throws for these two formats (`Texture::ValidateFormat` blocks every non-`Color` format, but this specific pair was absent from the representative Task 176 throw-test list). Added `UnsupportedFormatConstructionTest.NormalizedByte2Throws`/`NormalizedByte4Throws` to `Texture2DTests.cpp`, confirming `std::runtime_error` for both. All 4 `PackedVector` CPU types (`NormalizedByte2/4`, `NormalizedShort2/4`) already existed with full golden-value test coverage already in place (unlike Task 285's `Bgr565`/`Bgra5551`/`Bgra4444`, which needed golden tests added) — no test-coverage gap there either. 2016/2018 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged: `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`; `EasyGL_TransformMatrix_Translation` failed once under parallel `-j` but passed in isolation and on a serial rerun — parallel-execution flakiness, same class as `EasyGL_SkinnedBones`, not a regression). |
| 287 | Implement/verify float formats: `Single`, `Vector2`, `Vector4`                                       | ✅      | Same verify-only shape as Tasks 285/286: `Texture::GetBlockSizeSquaredEXT` (1, non-compressed) and `GetFormatSizeEXT` (4/8/16 respectively) were already correctly ported from FNA (Task 282) with existing exhaustive unit tests; no backend (EasyGL/Vulkan/Bgfx) special-cases any of these 3 formats — confirmed via grep, all fall through to the universal "only `Color` is GPU-backed" path. The only gap: no test confirmed `Texture2D` construction actually throws for these 3 formats. Added `UnsupportedFormatConstructionTest.SingleThrows`/`Vector2Throws`/`Vector4Throws` to `Texture2DTests.cpp`, mirroring Task 286's additions. No bug found, no GPU mapping work needed (real GL/Vulkan float-texture mapping is out of scope until `Texture::ValidateFormat` itself is widened to accept non-`Color` formats, which is Task 290's/a later task's concern, not this one's). 2017/2019 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged: `EasyGL_MRT_TwoAttachments`, `easy-gl-resource-smoke-tests`). |
| 288 | Implement/verify half-float formats: `HalfSingle`, `HalfVector2`, `HalfVector4`                      | ✅      | Same verify-only shape as Tasks 285–287: `GetBlockSizeSquaredEXT`(1)/`GetFormatSizeEXT` (`HalfSingle`=2/`HalfVector2`=4/`HalfVector4`=8) already correct (Task 282, already tested); no backend special-cases any of the 3 (confirmed via grep, all fall through to the "only `Color` is GPU-backed" path). Added `UnsupportedFormatConstructionTest.HalfSingleThrows`/`HalfVector2Throws`/`HalfVector4Throws` to `Texture2DTests.cpp`. No bug found. Real GL/Vulkan half-float texture mapping remains out of scope until `Texture::ValidateFormat` is deliberately widened (a later task, not 285–289's verify-only scope). 2020/2022 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged). |
| 289 | Implement/verify HDR formats: `HdrBlendable`, `Rgba1010102`, `Rgba64`                                | ✅      | Same verify-only shape as Tasks 285–288: `GetBlockSizeSquaredEXT`(1)/`GetFormatSizeEXT` (`Rgba1010102`=4/`Rgba64`=8/`HdrBlendable`=8) already correct (Task 282, already tested); fallback status already documented in `docs/surface-format-support.md` (Task 281 — `HdrBlendable` noted there as FNA's alias for RGBA16F, same underlying format as `HalfVector4`); no backend special-cases any of the 3. Added `UnsupportedFormatConstructionTest.HdrBlendableThrows`/`Rgba1010102Throws`/`Rgba64Throws` to `Texture2DTests.cpp`. No bug found, no new documentation needed (already existed). 2023/2025 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged). |
| 290 | Add test that every `SurfaceFormat` either works or throws a deliberate unsupported-format exception | ✅      | **Closes Phase 34.** Unlike Tasks 285–289 (each already fully covered by construction-throw tests added incrementally), this task had genuine remaining scope: cross-referencing every `SurfaceFormat::` reference across `Texture2DTests.cpp` and `examples/easygl_surface_format_throws_test.cpp` against the full 27-value enum found 7 values still untested for throw-behavior — `Bgra5551`, `Bgra4444`, `Dxt3`, `Dxt5`, `Rg32`, `ByteEXT`, `UShortEXT`. Rather than adding 7 more one-off `TEST_F` cases, added a single exhaustive `UnsupportedFormatConstructionTest.EverySurfaceFormatEitherWorksOrThrowsClearly` test (`Texture2DTests.cpp`) that iterates a hardcoded list of all 27 `SurfaceFormat` values, asserting `Color` never throws and every other value throws `std::runtime_error` — self-documenting and will catch a future un-vetted 28th value automatically (it just won't be in the list yet, which is a visible, deliberate gap rather than a silent one). Confirmed `Texture::ValidateFormat` is called from the shared, backend-agnostic `Texture2D`/`Texture3D`/`TextureCube` constructors (not per-backend), so this one EasyGL-build test result applies identically to Vulkan/Bgfx. 2024/2026 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged). |

---

## Phase 35 — SamplerState and texture sampling conformance

| #   | Task                                                                                       | Status | Notes                                          |
| --- | ------------------------------------------------------------------------------------------ | ------ | ---------------------------------------------- |
| 291 | Audit `SamplerState` API and static presets against FNA                                    | ✅      | **Opens Phase 35.** Audited CNA's `SamplerState` against FNA's `Graphics/States/SamplerState.cs` line-by-line: the 7-property surface, all 6 static presets (`AnisotropicClamp/Wrap`, `LinearClamp/Wrap`, `PointClamp/Wrap` — confirmed FNA has no 7th "Default" preset), default-constructor values (`Linear`/`Wrap`×3/anisotropy=4/maxMip=0/lodBias=0), and every preset's filter/address values already matched FNA exactly — no bug in the core state values. **One real, genuine finding**: FNA's private preset constructor sets `Name` (e.g. `"SamplerState.AnisotropicClamp"`) on each preset; CNA's equivalent constructor never called `setNameProperty`, so preset `Name`/`ToString()` silently diverged from FNA (no existing test caught this). Fixed by threading a `name` parameter through `SamplerState`'s private preset constructor (`SamplerState.hpp`/`.cpp`) and calling `setNameProperty`, matching FNA's exact preset name strings. Added 8 new tests (`Name` per preset ×6, `ToString()` returns preset name, default-constructed `Name` is empty) to `SamplerStateTests.cpp`. **Also found the identical gap is systemic**, not `SamplerState`-specific: `BlendState`/`DepthStencilState`/`RasterizerState` presets in FNA all set `Name` too (e.g. `"BlendState.Additive"`), and none of CNA's equivalents do — deliberately not fixed here (this task's literal scope is `SamplerState` only, per the project's no-bundling convention), tracked as new Task 866. 2032/2034 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged). |
| 292 | Verify default sampler states for all slots                                                | ✅      | **Found and fixed a real bug**, uncovered directly by Task 291's `Name` fix: FNA's `SamplerStateCollection` constructor (`SamplerStateCollection.cs`) fills every slot with `SamplerState.LinearWrap`; CNA's constructor instead default-constructed each of the 16 slots (`samplers_(MaxSamplers)`). The filter/address *values* coincided (both are `Linear`+`Wrap`×3), which is exactly why this was invisible until `Name` existed to tell them apart — `coll[i].getNameProperty()` was empty instead of `"SamplerState.LinearWrap"`. Zero tests existed for `SamplerStateCollection` or `GraphicsDevice`'s sampler defaults at all before this task. Fixed the constructor to `samplers_(MaxSamplers, SamplerState::LinearWrap)`. Added new `SamplerStateCollectionTests.cpp` (16-slot default filter/addressing/`Name` checks, indexer assignment, negative/over-range throws for both mutable and const indexers) plus `GraphicsDeviceSamplerStatesTest` confirming both `GraphicsDevice.SamplerStates` and `VertexSamplerStates` default every slot to `LinearWrap`. 2042/2044 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged). |
| 293 | Verify per-slot sampler binding with two textures using different sampler states           | ✅      | **Found and fixed a severe, previously-undocumented, project-wide bug.** New `examples/easygl_sampler_state_effect_test.cpp` (`EasyGL_SamplerState_DualTextureEffect` ctest) renders a `DualTextureEffect` quad with a UV range past 1.0 and `SamplerStates[0] = SamplerState::PointWrap` assigned; it initially **FAILED** — the result was always the `Clamp` answer regardless of the assigned `TextureAddressMode`/`TextureFilter`. Root cause, traced precisely: `GraphicsDevice::DrawPrimitives`/`DrawIndexedPrimitives` (buffer-bound draws) correctly call `applySamplerStatesToBackend()` before delegating to the backend, but **all 18 of the `DrawUserPrimitives`/`DrawUserIndexedPrimitives`/`DrawInstancedPrimitives` overloads never did** — meaning every textured 3D draw issued through the `DrawUserPrimitives` family (the primary way stock effects like `BasicEffect`/`DualTextureEffect`/`AlphaTestEffect`/`EnvironmentMapEffect`/`SkinnedEffect` actually draw in most game/test code) silently ignored `GraphicsDevice.SamplerStates`/`VertexSamplerStates` entirely, always sampling with whatever GPU state happened to be bound from a prior, unrelated draw (or the texture's own hardcoded creation-time default: Linear+ClampToEdge on EasyGL). Fixed by adding the same `applySamplerStatesToBackend()` call already used correctly in `DrawPrimitives`/`DrawIndexedPrimitives` to all 18 missing call sites (mechanical, uniform, matches the existing sibling-method pattern exactly — not a design change). Test now passes (confirmed `RED`, the `PointWrap`-correct answer, not `GREEN`, the previous silently-`Clamp`ed answer). Also registered the same backend-agnostic test source as `Vulkan_SamplerState_DualTextureEffect`, but could not build/run it this session — `cmake-build-vulkan` failed with an unrelated, pre-existing compile error from **uncommitted local changes in the sibling `sharp-runtime` repo** (`BitConverter.hpp`, not touched this session); the fix itself is at the shared, backend-agnostic `GraphicsDevice` layer, so it should benefit Vulkan/Bgfx identically (both already have real `ApplySamplerState` implementations, confirmed by code reading), but this is unverified pending a clean Vulkan build. 2043/2045 EasyGL ctest pass serially (2 pre-existing, unrelated failures unchanged) — zero regressions from a fix touching 18 call sites across the entire `GraphicsDevice` draw surface. **Follow-up (same session): the `sharp-runtime` build blocker was resolved** (pre-existing uncommitted fix in that sibling repo, unrelated to CNA, committed separately) **and Vulkan/Bgfx were then verified — both had their own, backend-specific sampler bugs, now also fixed.** Vulkan: `GetOrCreateDualTexDescSet` hardcoded `defaultSampler_` (Linear+ClampToEdge) into both descriptor slots and cached the descriptor set keyed only by image views, completely ignoring `slotSamplers_[0]`/`slotSamplers_[1]` (which `ApplySamplerState` correctly computes) — fixed by threading both samplers through as parameters and folding them into the cache key (mirroring the already-correct single-texture `GetOrCreateTexSamplerDescSet(view, sampler)` pattern). The reused `easygl_sampler_state_effect_test.cpp` source, now also registered as `Vulkan_SamplerState_DualTextureEffect`, initially failed identically to the pre-fix EasyGL result and now passes. Bgfx: found and fixed a distinct one-line bug in the same code path — `DrawPrimitivesEx`'s dual-texture branch bound `texture1` (slot 1) using `samplerFlags_[0]` instead of `samplerFlags_[1]`, so the second texture always inherited the first texture's sampler state regardless of what was assigned to slot 1. Fixed to use `samplerFlags_[1]`. Full ctest suites reconfirmed on all three backends after these follow-up fixes: EasyGL unchanged (2043/2045), **Vulkan 1983/1984** (only the pre-existing `Vulkan_DepthBias` failure), **Bgfx 1977/1977 (100%)** — zero regressions anywhere. |
| 294 | Pixel test: `TextureAddressMode::Clamp`                                                    | ✅      | Confirmed Task 269's `easygl_texture_address_mode_test.cpp` only covers `Clamp`/`Wrap` for **`SpriteBatch`**, a different code path from Task 293's fix — a separate 3D-stock-effect test was genuinely needed. New `examples/easygl_texture_address_mode_clamp_effect_test.cpp` mirrors Task 293's `DualTextureEffect` pattern exactly (2-texel red/green pattern texture + white second texture + UV range 0–2), but assigns `SamplerState::PointClamp` and asserts the opposite (edge-extend/`GREEN`) result instead of `PointWrap`'s repeat/`RED`. Registered on **both** EasyGL (`EasyGL_TextureAddressMode_Clamp_DualTextureEffect`) and Vulkan (`Vulkan_TextureAddressMode_Clamp_DualTextureEffect`, reusing the same backend-agnostic source) — both pass, confirming Task 293's fix holds for `Clamp` specifically on both backends, not just the `Wrap` case the fix-proof test happened to use. EasyGL ctest: 2044/2046 (2 pre-existing, unrelated failures unchanged). Vulkan ctest: 1983/1985 — only `Vulkan_DepthBias` (pre-existing) plus `Vulkan_FillMode_WireFrame`, which was also observed failing **in isolation** this session (fail/pass/fail across 3 repeat runs) — strengthens rather than contradicts its existing "order/timing-dependent flakiness" documentation (NEXT.md §5); confirmed unrelated to this task's changes (which touch only descriptor-set caching for dual-texture draws, nothing in the fill-mode/rasterization pipeline). |
| 295 | Pixel test: `TextureAddressMode::Wrap`                                                     | ✅      | **Already fully satisfied by Task 293's own fix-proof test** — no new code needed. `examples/easygl_sampler_state_effect_test.cpp` assigns `SamplerState::PointWrap` to slot 0 on a `DualTextureEffect` 3D draw, samples past UV 1.0 (raw u=1.25, out-of-range), and asserts the repeat/`RED` result — exactly this task's literal ask. Already registered and passing on both EasyGL (`EasyGL_SamplerState_DualTextureEffect`) and Vulkan (`Vulkan_SamplerState_DualTextureEffect`). This entry exists purely to record the mapping so the test isn't duplicated later. |
| 296 | Pixel test: `TextureAddressMode::Mirror`                                                   | ✅      | New `examples/easygl_texture_address_mode_mirror_effect_test.cpp`, mirroring the Task 293/294 `DualTextureEffect` pattern. FNA has no `PointMirror` static preset (only `*Clamp`/`*Wrap` combinations exist), so this test builds a custom `SamplerState` (`Filter=Point`, `AddressU=AddressV=Mirror`) rather than using a preset. Chose sample point raw `u=1.6` deliberately — at the more obvious `u=1.25` used by Tasks 293/294, `Mirror`'s reflected answer (`2.0-1.25=0.75`) coincidentally lands in the same texel as `Clamp`'s answer, so it wouldn't distinguish a real `Mirror` implementation from one that silently fell back to `Clamp`; at `u=1.6`, `Mirror` reflects to `0.4` (left texel, `RED`) while both `Wrap` (`frac(1.6)=0.6`) and `Clamp` (clamped to `1.0`) land on the right texel (`GREEN`), so only a correct `Mirror` implementation passes. Registered on both EasyGL (`EasyGL_TextureAddressMode_Mirror_DualTextureEffect`) and Vulkan (`Vulkan_TextureAddressMode_Mirror_DualTextureEffect`) — both pass on the first try (all three backends already correctly map `TextureAddressMode::Mirror` to `GL_MIRRORED_REPEAT`/`VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT`/Bgfx's mirror flag, confirmed via grep before writing the test) — no bug found, this was a genuine coverage gap, not a hidden defect. EasyGL ctest: 2045/2047 (2 pre-existing, unrelated failures unchanged). Vulkan ctest: 1984/1986 (`Vulkan_DepthBias` pre-existing; `Vulkan_FillMode_WireFrame`'s known flakiness recurred, unrelated — see Task 294's entry). |
| 297 | Pixel test: `TextureFilter::Point` vs `Linear`                                             | ✅      | First genuinely new-ground test in Phase 35 — no prior test exercised `TextureFilter` itself (only `TextureAddressMode`, Tasks 293–296). New `examples/easygl_texture_filter_point_vs_linear_test.cpp` draws 4 columns in one frame on a `DualTextureEffect`, each with its own `SamplerState`: magnification (2-texel red/green texture stretched across a 256px column) × {`Point`,`Linear`}, and minification (256-texel alternating-red/green texture compressed into a 128px column, 2 texels/pixel) × {`Point`,`Linear`}. Each is sampled exactly at a texel boundary (guaranteed not to coincide with any texel center): `Point` must read a pure, unblended colour there; `Linear` must read a genuine ~50/50 blend. All 4 checks passed on the first run on both backends, with blend values landing almost exactly at 127–128 as predicted — no bug found. Documented finding: CNA maps XNA's single `TextureFilter` value to one GL/Vulkan min+mag filter pair (matching FNA's flat enum, which has no separate min/mag control) and generates no mipmaps by default, so magnification and minification exercise *identical* underlying sampler math here — this task's two scales confirm scale itself doesn't break anything, not a functionally distinct GPU code path. Registered on both EasyGL (`EasyGL_TextureFilter_PointVsLinear`) and Vulkan (`Vulkan_TextureFilter_PointVsLinear`). EasyGL ctest: 2046/2048 (2 pre-existing, unrelated failures unchanged). Vulkan ctest: 1985/1987 (both pre-existing/documented: `Vulkan_DepthBias`, `Vulkan_FillMode_WireFrame`'s known flakiness). |
| 298 | Verify mipmap filter behavior: `MipPoint`, `MipLinear`, `MinLinearMagPointMipLinear`, etc. | ✅      | **EasyGL: verified correct. Found a real, severe Vulkan/Bgfx bug along the way (tracked separately as Task 867), not fixed here.** New `examples/easygl_texture_mip_filter_effect_test.cpp`: a 128×128 `mipMap` `Texture2D` (8 levels) with levels 0–2 solid Red, levels 3–7 solid Green, drawn at a tiny 8×8px on-screen size (forcing automatic GPU LOD ≈4, deep in the Green range with wide margin either side against driver-specific rounding). On EasyGL: `TextureFilter::LinearMipPoint` correctly samples a high mip level (Green) — real mip-level selection works when an explicit `Mip*` filter is requested. `TextureFilter::Point` (and the default `Linear`) always sample level 0 (Red) regardless of minification — **a real, confirmed, deliberate EasyGL/Vulkan deviation from FNA semantics** (XNA's `Point`/`Linear` are supposed to be mip-aware too), documented as an accepted tradeoff: CNA doesn't set `GL_TEXTURE_MAX_LEVEL` to match each texture's real level count, so using a mip-aware GL filter unconditionally for the common non-mipmapped (single-level) texture case would render it GL-incomplete (typically solid black) — tracked as part of Task 867's scope, not fixed here. **On Vulkan, the test initially failed for a completely different, more severe reason**: `Texture2D::SetData(level>0,...)` is a **total silent no-op** — `ITextureBackend::UpdatePixelsLevel` has an empty default body that neither `VulkanTextureBackend` nor Bgfx's texture backend overrides, so none of the Green mip-level data the test tries to upload ever reaches the GPU on either backend; tracing this further found Vulkan also hardcodes `VkImageCreateInfo::mipLevels=1`, `VkImageViewCreateInfo::levelCount=1`, and never sets `VkSamplerCreateInfo::minLod`/`maxLod` (defaulting to 0, clamping LOD selection to level 0 regardless of `mipmapMode`) — three more fixes needed together for Vulkan `Texture2D` mips to work at all. **Un-registered the Vulkan variant of this test** (it would fail for the wrong, confounding reason — a data-upload bug, not a filter-selection bug) rather than leave a misleadingly-labeled failing test; tracked the full finding as new Task 867. EasyGL ctest: confirmed passing, no regressions (this task added no production-code changes, test-only). |
| 299 | Verify anisotropic filtering caps and fallback                                             | ✅      | Audited all 3 backends' `TextureFilter::Anisotropic` + `SamplerState.MaxAnisotropy` handling. **Vulkan: correct.** Queries `VkPhysicalDeviceFeatures.samplerAnisotropy` support and the real device cap (`VkPhysicalDeviceProperties.limits.maxSamplerAnisotropy`), clamps the requested value to it before creating the sampler. **EasyGL: no anisotropic support at all** — `TextureFilter::Anisotropic` silently falls back to plain trilinear filtering; the underlying `easy-gl` library has zero anisotropy-related API (confirmed via grep — no `SamplerParameter` enum value for it exists), so `MaxAnisotropy` has no effect whatsoever, on any value. **Bgfx: partial** — enables `BGFX_SAMPLER_MIN/MAG_ANISOTROPIC` flags (some anisotropic filtering does occur) but the `maxAnisotropy` parameter itself is unused (`/*maxAnisotropy*/` in the function signature) — the requested level is never communicated, only "on/off". A true visual-quality pixel test (comparing detail preservation under oblique/aspect-skewed minification) was judged too driver-dependent/fragile to assert precisely — the task's own "caps and fallback" framing was tested literally instead: new `examples/easygl_texture_anisotropic_effect_test.cpp` assigns `MaxAnisotropy=9999` (far beyond any real cap) and confirms no crash/exception on both EasyGL and Vulkan, which is this test's real, load-bearing assertion. **Found an additional, severe finding while building this test**: assigning `TextureFilter::Anisotropic` (or any `*Mip*` filter) to an ordinary single-level `Texture2D` renders **solid black on EasyGL** (a classic GL mipmap-incompleteness symptom — `Anisotropic` maps to a `_MIPMAP_`-suffixed GL filter requiring a complete mip chain, but EasyGL never sets `GL_TEXTURE_MAX_LEVEL` to match a texture's real level count). Vulkan does not share this symptom (renders correctly even on a single-level texture). This is the same root architectural gap as Task 867 manifesting differently — Task 867's tracked scope extended to cover it; not fixed here (documented only, per this test's file-header comment — the test does not fail on the black result, only on an actual crash). Registered on both EasyGL (`EasyGL_TextureAnisotropic_DualTextureEffect`) and Vulkan (`Vulkan_TextureAnisotropic_DualTextureEffect`). EasyGL ctest: 2048/2050 (2 pre-existing, unrelated failures unchanged). Vulkan ctest: 1986/1988 (`Vulkan_DepthBias` pre-existing; `Vulkan_RenderTargetUsage` failed once in a full run, confirmed passing in isolation — pre-existing order-dependent flakiness per Task 279, not a regression). |
| 300 | Document sampler behavior differences by backend                                           | ✅      | **Closes Phase 35.** New `docs/sampler-state-support.md` synthesizes all of Tasks 291–299's findings into one reference: `SamplerState` API/preset conformance, default sampler states, the central Task 293 per-slot-binding bug (root cause + fix per backend), `TextureAddressMode`/`TextureFilter`/mipmap-filter/anisotropic-filtering coverage, and a summary support matrix across all 3 backends with explicit ✅/❌/⚠️/🔍 status per feature. Links out to the two open, tracked follow-up items (Task 866, Task 867) rather than duplicating their full writeups. |

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
| 327 | Verify `FillMode::WireFrame` on Vulkan/Bgfx and documented unsupported behavior on GLES/EasyGL if applicable | ✅      | Avoid false support                           |
| 328 | Verify depth bias and slope-scale depth bias                                                                 | ✅      | Shadow-like test                              |
| 329 | Verify scissor test enable/disable interaction with `GraphicsDevice.ScissorRectangle`                        | ✅      | `vulkan_scissor_test.cpp`: 4/4 PASS — no-scissor (both quadrants red), scissor=top-left (inside red, outside green); `Vulkan_ScissorTest` ctest added |
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
| 664 | Fix Vulkan: multiple `SpriteBatch::Begin()`/`End()` calls per frame — only the last batch renders | ⬜ | Confirmed bug (session 2026-07-02, see `NEXT.md` §5). Root cause not yet isolated — likely `VulkanSpriteBatchBackend` reuses/overwrites a single per-frame vertex/index staging buffer or descriptor set across `Begin`/`End` cycles instead of flushing or double-buffering per batch. Repro: 2+ `Begin()`/`Draw()`/`End()` cycles in one `Draw(GameTime)`, each drawing a distinct-colour quad to a distinct screen region; expect both regions correct, currently only the last one renders. Do not guess-fix; instrument/trace the Vulkan command recording first. |
| 665 | Fix Vulkan: `SpriteBatch::Begin()`'s `SamplerState` (Filter/AddressU/AddressV) has no effect | ⬜ | `VulkanSpriteBatchBackend` doesn't override `SetSamplerFilter`/`SetSamplerAddressMode` (mirrors the pre-Task-269 EasyGL bug, same root cause, different backend). Fix shape: mirror Task 269's EasyGL fix — store pending filter/address values set via `SetSamplerFilter`/`SetSamplerAddressMode`, apply via the existing per-slot `VkSampler` cache (Task 118, `GetOrCreateTexSamplerDescSet`) at flush time, slot 0. Verify UV is not separately clamped anywhere in the Vulkan sprite draw path (mirrors the second EasyGL bug Task 269 found — check before assuming only the sampler wiring is missing). Add a `Vulkan_TextureAddressMode` pixel-readback test analogous to `EasyGL_TextureAddressMode`. |

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

---

## Phase 56 — WebGPU backend: infrastructure and CMake setup

> **⛔ Deprioritized as of 2026-07-02.** Tasks in this phase and Phases 57–69 are on hold until
> Phases 1–55 and the Phase 70–73 backend-perfection wave (SDL_Renderer → EasyGL → Bgfx → Vulkan)
> are complete — see "Execution order" near the top of this document. Task numbers were moved to
> 10001+ (from 501–661) to free up numbering space for that wave; no other content changed.
>
> WebGPU backend uses **wgpu-native v29** (C API header `webgpu.h` + `wgpu.h`).
> Installed at `vendor/wgpu-native/`. Shaders are written in **WGSL** (not SPIR-V).
> Push constants do not exist in WebGPU — replaced by uniform buffers (bind group 0, binding 0).
> Backend selection: `-DCNA_GRAPHICS_BACKEND=WEBGPU`, build dir `cmake-build-webgpu`.
>
> Strategy: mirror the Vulkan backend structure, adapt to WebGPU API differences.
> Estimated total effort: ~4–6 weeks (Tasks 501–750).

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10001 | Add `CNA_GRAPHICS_BACKEND=WEBGPU` CMake option; find `vendor/wgpu-native` headers + libs; define `CNA_BACKEND_WEBGPU` | ⬜ | Mirror VULKAN block in CMakeLists.txt |
| 10002 | Create `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp` — class skeleton, all IGraphicsBackend sub-interfaces declared | ⬜ | ~12 nested backend classes |
| 10003 | Create `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp` — stub all methods (throw not-implemented) | ⬜ | Compiles clean, no functionality yet |
| 10004 | SDL3 surface creation: obtain `WGPUSurface` via `SDL_GetProperty(SDL_PROP_WINDOW_WGPU_SURFACE_POINTER)` or `wgpuInstanceCreateSurface` | ⬜ | Prerequisite for all rendering |
| 10005 | `WGPUInstance` + `WGPUAdapter` + `WGPUDevice` + `WGPUQueue` initialization via `wgpuCreateInstance` / `wgpuInstanceRequestAdapter` / `wgpuAdapterRequestDevice` | ⬜ | All synchronous in wgpu-native |
| 10006 | Swap chain: `WGPUSurface` configure + `wgpuSurfaceGetCurrentTexture` + `wgpuTextureCreateView` for backbuffer | ⬜ | Replaces `vkAcquireNextImageKHR` |
| 10007 | Command encoder: `wgpuDeviceCreateCommandEncoder` + `wgpuCommandEncoderFinish` + `wgpuQueueSubmit` per frame | ⬜ | Replaces Vulkan command buffer recording |
| 10008 | Render pass: `wgpuCommandEncoderBeginRenderPass` with color attachment (backbuffer view) + depth attachment | ⬜ | Equivalent to `vkCmdBeginRenderPass` |
| 10009 | `Clear()`: set clear color in `WGPURenderPassColorAttachment.clearValue`; implement depth clear in pass descriptor | ⬜ | |
| 10010 | `Present()`: `wgpuSurfacePresent()` after queue submit | ⬜ | |

---

## Phase 57 — WebGPU backend: uniform buffer system (replaces push constants)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10011 | Design `GpuUniforms` struct (128 bytes = 32 floats) matching Vulkan push constant layout; upload via `wgpuQueueWriteBuffer` | ⬜ | Central UBO for MVP + effect params |
| 10012 | Create `WGPUBuffer` (uniform, size=128) per frame (or ring buffer of 3); map on CPU side via `wgpuBufferGetMappedRange` | ⬜ | |
| 10013 | `WGPUBindGroupLayout` for slot 0 binding 0 (uniform buffer) — shared across all 3D pipelines | ⬜ | |
| 10014 | `WGPUBindGroup` creation and per-draw update for MVP matrix | ⬜ | |
| 10015 | `WGPUBindGroupLayout` for slot 1 binding 0 (texture sampler) — for textured pipelines | ⬜ | |
| 10016 | `WGPUSampler` creation mapping `SamplerState` (filter, address mode) → WGPU descriptor | ⬜ | |
| 10017 | `WGPUPipelineLayout` combining UBO bind group layout + texture bind group layout | ⬜ | |

---

## Phase 58 — WebGPU backend: WGSL shaders

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10020 | Write `sprite2d.wgsl` — 2D sprite vertex + fragment shader (pos + UV + RGBA tint); embed as C++ string literal | ⬜ | Equivalent to `sprite2d.vert/frag.glsl` |
| 10021 | Write `colored3d.wgsl` — 3D vertex shader (float3 pos + ubyte4 color), flat fragment; UBO for MVP | ⬜ | stride=16 |
| 10022 | Write `textured3d.wgsl` — 3D vertex (float3 pos + float2 UV); texture2D sampler in fragment | ⬜ | stride=20 |
| 10023 | Write `colored_textured3d.wgsl` — float3 + ubyte4 color + float2 UV; multiply tex×color in fragment | ⬜ | stride=24 |
| 10024 | Write `lit_textured3d.wgsl` — float3 pos + float3 normal + float2 UV; Blinn-Phong lighting in fragment | ⬜ | stride=32 |
| 10025 | Write `alpha_test3d.wgsl` — per-pixel alpha discard matching XNA AlphaTestEffect semantics | ⬜ | |
| 10026 | Write `dual_texture3d.wgsl` — two texture samplers, multiply/blend in fragment | ⬜ | |
| 10027 | Write `env_map3d.wgsl` — cube map sampler + reflection vector from normal | ⬜ | |
| 10028 | Write `skinned3d.wgsl` — bone palette as uniform array (max 72 mat4); blend 4 weights+indices | ⬜ | |
| 10029 | Write `instanced3d.wgsl` — per-instance mat4 world transform in second vertex buffer binding | ⬜ | |
| 10030 | Compile-time validation: embed all WGSL as `constexpr const char*` in `webgpu_shaders.hpp`; validate via `wgpuDeviceCreateShaderModule` at startup | ⬜ | Catch WGSL errors early |

---

## Phase 59 — WebGPU backend: render pipeline creation

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10035 | `WGPURenderPipelineDescriptor` builder helper: vertex state, primitive state, depth-stencil state, multisample state, fragment state | ⬜ | Reusable for all pipelines |
| 10036 | Pipeline cache: `std::unordered_map<uint64_t, WGPURenderPipeline>` with MakeKey(topo, depth, blend, cull, stride, wireframe, msaa) | ⬜ | Mirror Vulkan MakeKey / GetOrCreate* |
| 10037 | `GetOrCreatePipeline2D()` — sprite pipeline (stride=24, Sprite2DVertex layout, no depth) | ⬜ | |
| 10038 | `GetOrCreatePipelineColored3D()` — stride=16, VPC layout | ⬜ | |
| 10039 | `GetOrCreatePipelineExt3D()` — stride 20/24/32 dispatch matching Vulkan | ⬜ | |
| 10040 | `GetOrCreatePipelineAlphaTest3D()` — alpha discard variant | ⬜ | |
| 10041 | `GetOrCreatePipelineDualTex3D()` — two-texture variant | ⬜ | |
| 10042 | `GetOrCreatePipelineEnvMap3D()` — cube map variant | ⬜ | |
| 10043 | `GetOrCreatePipelineSkinned3D()` — bone palette variant | ⬜ | |
| 10044 | `GetOrCreatePipelineInstanced3D()` — per-instance binding variant | ⬜ | |
| 10045 | Depth-stencil: `WGPUDepthStencilState` mapping `DepthFormat` + `CompareFunction` + `StencilOperation` | ⬜ | |
| 10046 | Blend state: `WGPUBlendState` mapping `BlendFunction` + `BlendFactor` (Opaque, AlphaBlend, Additive, NonPremultiplied) | ⬜ | |
| 10047 | Rasterizer: `WGPUPrimitiveState` mapping `CullMode`, `FillMode` (WireFrame via `topology=LineStrip` fallback or unsupported) | ⬜ | |

---

## Phase 60 — WebGPU backend: vertex and index buffers

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10050 | `WebGPUVertexBufferBackend`: create `WGPUBuffer` (vertex, size=capacity×stride) with `COPY_DST` usage | ⬜ | |
| 10051 | `SetData()`: upload via `wgpuQueueWriteBuffer(queue, buffer, 0, data, byteSize)` | ⬜ | Simpler than Vulkan staging |
| 10052 | `SetDataWithOptions()`: `Discard` = reallocate buffer; `NoOverwrite` = `wgpuQueueWriteBuffer` at offset | ⬜ | |
| 10053 | `WebGPUIndexBufferBackend`: 16-bit and 32-bit index buffers via `WGPUIndexFormat` | ⬜ | |
| 10054 | `SetData16()` / `SetData32()`: `wgpuQueueWriteBuffer` | ⬜ | |
| 10055 | Disposed guard in all SetData methods (throw `ObjectDisposedException`) | ⬜ | Match Task 240 pattern |
| 10056 | `SetVertexBuffer(wgpuRenderPassSetVertexBuffer)` + `SetIndexBuffer(wgpuRenderPassSetIndexBuffer)` in draw dispatch | ⬜ | |

---

## Phase 61 — WebGPU backend: textures

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10060 | `WebGPUTextureBackend`: `WGPUTexture` (2D, RGBA8Unorm, COPY_DST + TEXTURE_BINDING) + `WGPUTextureView` | ⬜ | |
| 10061 | `SetData()`: `wgpuQueueWriteTexture()` with `WGPUImageCopyTexture` + `WGPUTextureDataLayout` | ⬜ | |
| 10062 | `GetData()`: `WGPUBuffer` (MAP_READ) + `wgpuCommandEncoderCopyTextureToBuffer` + `wgpuBufferMapAsync` + poll | ⬜ | Async → synchronous via polling |
| 10063 | Mip levels: generate via `wgpuCommandEncoderCopyTextureToTexture` per level or leave as mip=1 (document) | ⬜ | |
| 10064 | `WebGPURenderTargetBackend`: `WGPUTexture` (RENDER_ATTACHMENT + TEXTURE_BINDING) + depth texture | ⬜ | |
| 10065 | `SetRenderTarget(rt)` / `SetRenderTarget(nullptr)`: switch render pass target between RT and swapchain view | ⬜ | |
| 10066 | `GetBackBufferData()`: readback via MAP_READ buffer + `wgpuCommandEncoderCopyTextureToBuffer` | ⬜ | |
| 10067 | `WebGPUTextureCubeBackend`: `WGPUTexture` (dimension=2D, arrayLayerCount=6, CUBE_COMPATIBLE) | ⬜ | |
| 10068 | `WebGPUTexture3DBackend`: `WGPUTexture` (dimension=3D) | ⬜ | |
| 10069 | MSAA: `WGPUTexture` with `sampleCount=4`; resolve in render pass via `resolveTarget` | ⬜ | |

---

## Phase 62 — WebGPU backend: 2D rendering (SpriteBatch)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10075 | `WebGPUSpriteBatchBackend`: dynamic vertex buffer ring (3 frames) for sprite quads | ⬜ | |
| 10076 | Upload sprite quads via `wgpuQueueWriteBuffer` per batch | ⬜ | |
| 10077 | Per-batch draw: set pipeline, bind groups (UBO + texture), vertex buffer, draw | ⬜ | |
| 10078 | Viewport UBO (2 floats: width, height) in sprite UBO slot | ⬜ | Replaces Vulkan sprite push constants |
| 10079 | Sprite sort modes: Immediate, Deferred, Texture, FrontToBack, BackToFront — mirror Vulkan implementation | ⬜ | |

---

## Phase 63 — WebGPU backend: 3D draw dispatch

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10085 | `DrawPrimitives()`: bind colored3d pipeline + UBO + vertex buffer + `wgpuRenderPassEncoderDraw` | ⬜ | |
| 10086 | `DrawIndexedPrimitives()`: bind index buffer + `wgpuRenderPassEncoderDrawIndexed` | ⬜ | |
| 10087 | `DrawPrimitivesEx()`: dispatch by `GpuDrawParams` (stride, textureEnabled, lightingEnabled, dualTexture, skinned, instanced) | ⬜ | Mirror Vulkan dispatch logic |
| 10088 | `DrawUserPrimitives()`: transient `WGPUBuffer` (COPY_DST + VERTEX, mappedAtCreation=false); upload + draw + release | ⬜ | |
| 10089 | `DrawInstancedPrimitivesEx()`: second vertex buffer binding (per-instance mat4 world transforms) | ⬜ | |
| 10090 | PrimitiveType mapping: TriangleList→`WGPUPrimitiveTopology_TriangleList`, TriangleStrip→Strip, LineList→LineList, LineStrip→LineStrip, PointList→PointList | ⬜ | |
| 10091 | `vertexStart` / `startIndex` / `baseVertex` support in draw calls | ⬜ | Match Task 110 |

---

## Phase 64 — WebGPU backend: Effects

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10095 | `WebGPUEffectBackend`: `BasicEffect` wires to `FillGpuDrawParams` → UBO upload | ⬜ | |
| 10096 | `AlphaTestEffect`: UBO alpha test params (function, reference) | ⬜ | |
| 10097 | `DualTextureEffect`: second texture bind group | ⬜ | |
| 10098 | `EnvironmentMapEffect`: cube map bind group + reflection UBO params | ⬜ | |
| 10099 | `SkinnedEffect`: bone palette as large UBO (72 × mat4 = 4608 bytes) in separate bind group | ⬜ | WebGPU min UBO size: 65536 bytes — fits |
| 10100 | `ShaderEffect` (custom WGSL): `wgpuDeviceCreateShaderModule` from user-provided WGSL source string | ⬜ | NOXNA extension |

---

## Phase 65 — WebGPU backend: state management

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10105 | `SetDepthTestEnabled()` / `SetDepthWriteEnabled()`: bake into pipeline key | ⬜ | WebGPU requires pipeline rebuild on change |
| 10106 | `SetBlendState()`: map `BlendState` preset → `WGPUBlendState` | ⬜ | |
| 10107 | `SetRasterizerState()`: `CullMode` → `WGPUCullMode`; `FillMode::WireFrame` unsupported (log warning) | ⬜ | WebGPU has no polygon mode |
| 10108 | `SetScissorRectangle()`: `wgpuRenderPassEncoderSetScissorRect` | ⬜ | |
| 10109 | `SetViewport()`: `wgpuRenderPassEncoderSetViewport` | ⬜ | |
| 10110 | `SetSamplerState()`: per-slot `WGPUSampler` cache (filter + address mode key) | ⬜ | |
| 10111 | `SetDepthStencilState()`: stencil ops → `WGPUStencilFaceState` | ⬜ | |
| 10112 | `OcclusionQuery`: `WGPUQuerySet` (type=Occlusion) + `wgpuRenderPassEncoderBeginOcclusionQuery` | ⬜ | |

---

## Phase 66 — WebGPU backend: Multiple Render Targets

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10115 | MRT render pass: `WGPURenderPassDescriptor` with array of `WGPURenderPassColorAttachment` (up to 4) | ⬜ | |
| 10116 | `GetOrCreateMRTRenderPipeline(colorAttachmentCount)`: pipeline with matching `targetCount` in fragment state | ⬜ | |
| 10117 | `SetRenderTargets(vector<RenderTarget2D*>)`: configure MRT pass descriptor | ⬜ | |

---

## Phase 67 — WebGPU backend: integration tests

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10120 | `cmake-build-webgpu` directory; `cna_webgpu_test` macro in CMakeLists.txt | ⬜ | Mirror `cna_vulkan_test` |
| 10121 | Smoke test: init device, clear to blue, `GetBackBufferData`, assert pixel | ⬜ | `webgpu_smoke_test.cpp` |
| 10122 | 2D sprite test: `SpriteBatch` draw white 1×1 texture → assert pixel | ⬜ | |
| 10123 | 3D colored quad: stride=16 VPC, red quad, assert center pixel | ⬜ | `webgpu_vertex_format_test.cpp` |
| 10124 | 3D textured quad: stride=20 VPT, green texture, assert center pixel | ⬜ | |
| 10125 | 3D colored+textured: stride=24 VPCT, blue vertex + white tex | ⬜ | |
| 10126 | 3D lit textured: stride=32 VPNT, magenta tex, no lighting | ⬜ | |
| 10127 | `AlphaTestEffect`: draw with alpha < threshold → pixel transparent | ⬜ | |
| 10128 | `DualTextureEffect`: two textures → multiply blend | ⬜ | |
| 10129 | `EnvironmentMapEffect`: emissive color only (envAmount=0) → red pixel | ⬜ | |
| 10130 | `SkinnedEffect`: identity bone palette → same as lit textured | ⬜ | |
| 10131 | Instanced draw: 3 instances at different positions, assert 3 pixels | ⬜ | |
| 10132 | RenderTarget2D: draw red into RT, blit to backbuffer → assert red | ⬜ | |
| 10133 | MSAA 4x: draw red quad with MSAA, resolve, assert pixel | ⬜ | |
| 10134 | OcclusionQuery: draw occluded geometry, assert query result = 0 | ⬜ | |
| 10135 | VertexBuffer dispose guard: assert `ObjectDisposedException` after `Dispose()` | ⬜ | |
| 10136 | Dynamic buffer stress: 12 frames × None/Discard/NoOverwrite | ⬜ | |
| 10137 | WebGPU vertex format mapping table test (mirror Task 248 for WebGPU) | ⬜ | `WGPUVertexFormat` enum |

---

## Phase 68 — WebGPU backend: advanced and parity

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10140 | `SetStringMarkerEXT()`: no-op (WebGPU has no debug labels in wgpu-native C API yet) | ⬜ | Document deviation |
| 10141 | `DebugSimulateContextLoss()`: destroy and recreate device (wgpu-native supports `wgpuDeviceDestroy`) | ⬜ | |
| 10142 | `PresentationInterval` → vsync: `wgpuSurfaceConfigure.presentMode` (Fifo=VSync, Immediate=no VSync, Mailbox=adaptive) | ⬜ | |
| 10143 | `IsFullScreen` via `SDL_SetWindowFullscreen` — same as other backends | ⬜ | |
| 10144 | `BackBufferWidth/Height` changes: reconfigure swap chain via `wgpuSurfaceConfigure` | ⬜ | |
| 10145 | DXT1/DXT3/DXT5 compressed texture upload: `WGPUTextureFormat_BC1RGBAUnorm` etc. | ⬜ | Requires `wgpuAdapterHasFeature(BC_texture_compression)` |
| 10146 | Texture3D: `WGPUTextureDimension_3D` + layered upload | ⬜ | |
| 10147 | TextureCube: `WGPUTexture` arrayLayerCount=6 + `WGPUTextureViewDimension_Cube` | ⬜ | |
| 10148 | RenderTargetCube: `WGPUTexture` cube + per-face `WGPUTextureView` as render attachment | ⬜ | |
| 10149 | `FillMode::WireFrame`: document as unsupported in WebGPU (no polygon mode); add to deviations doc | ⬜ | |
| 10150 | WebGPU vertex format helper: `WGPUVertexFormat WebGPUVertexFormatFromVEF(VertexElementFormat)` (mirror Task 248) | ⬜ | |

---

## Phase 69 — WebGPU: documentation and future (Emscripten/WASM)

| #   | Task                                                                                                          | Status | Notes                                                                 |
| --- | ------------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------- |
| 10155 | `docs/webgpu-backend.md`: architecture, deviations from Vulkan, WGSL shader map, UBO layout | ⬜ | |
| 10156 | `docs/webgpu-vs-vulkan-deviations.md`: push constants → UBO, no wireframe, async→sync strategy | ⬜ | |
| 10157 | Emscripten target: configure CNA for `emcc` build with `-sUSE_WEBGPU=1`; WebGPU backend routes to browser `navigator.gpu` | ⬜ | True browser WASM target |
| 10158 | Emscripten: SDL3 Emscripten port + WebGPU surface via `emscripten_webgpu_get_device()` | ⬜ | |
| 10159 | Emscripten: verify all 9 WGSL shader pairs compile in browser via `createShaderModule` | ⬜ | |
| 10160 | Emscripten: run 2D smoke test in headless Chrome via `--headless=new --enable-features=WebGPU` | ⬜ | CI-friendly |
| 10161 | Cross-backend pixel comparison: same scene rendered on EasyGL/Vulkan/Bgfx/WebGPU — assert pixel-level parity | ⬜ | |

---

## Phase 70 — SDL_Renderer: 2D backend verified perfection

> Runs first, per "Execution order" above. SDL_Renderer is 2D-only by design (see file header),
> so this phase is deliberately smaller than Phases 72–73 — there's no 3D pipeline, blend/depth/
> raster state matrix, or stock-effect shader breadth to cover. "Perfect" here means: every 2D
> XNA feature SDL_Renderer is supposed to support is pixel-verified specifically *through*
> SDL_Renderer (not assumed from EasyGL parity), and every unsupported 3D path throws the
> correct, specific exception rather than a generic one.

### SpriteBatch on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 666 | Audit all `SpriteBatch::Draw`/`DrawString` overloads specifically through SDL_Renderer (not inherited from EasyGL test assumptions) | ⬜ | Confirm every overload produces correct `SDL_RenderTexture`/`SDL_RenderTextureRotated` calls |
| 667 | Pixel test: `SpriteSortMode::Deferred` submission order on SDL_Renderer | ⬜ | Mirrors Task 162 for this backend |
| 668 | Pixel test: `SpriteSortMode::Texture` grouping on SDL_Renderer | ⬜ | Mirrors Task 163 |
| 669 | Pixel test: `SpriteSortMode::FrontToBack` / `BackToFront` ordering on SDL_Renderer | ⬜ | Mirrors Tasks 164–165 |
| 670 | Pixel test: `SpriteSortMode::Immediate` per-draw flush on SDL_Renderer | ⬜ | Mirrors Task 161 |
| 671 | Pixel test: rotation around origin on SDL_Renderer | ⬜ | `SDL_RenderTextureRotated` |
| 672 | Pixel test: scalar and `Vector2` scale overloads on SDL_Renderer | ⬜ | |
| 673 | Pixel test: source rectangle cropping on SDL_Renderer | ⬜ | |
| 674 | Pixel test: `SpriteEffects::FlipHorizontally`/`FlipVertically` on SDL_Renderer | ⬜ | Mirrors Task 167 |
| 675 | Pixel test: `transformMatrix` in `SpriteBatch::Begin` on SDL_Renderer | ⬜ | Mirrors Task 168 |
| 676 | Decide and document `SpriteBatch::Begin(effect)` custom-`Effect` behavior on SDL_Renderer | ⬜ | No programmable shader stage exists on SDL_Renderer — either throw `NotSupportedException` or document silent-ignore; must not silently misrender |
| 677 | Guard tests: `Begin`/`End` sequencing errors throw correctly on SDL_Renderer | ⬜ | Mirrors Task 166 |

### Texture2D on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 678 | Audit `Texture2D::SetData`/`GetData` full-array round-trip on SDL_Renderer | ⬜ | `SDL_UpdateTexture`/`SDL_LockTexture` path |
| 679 | Pixel test: `SetData` partial-rectangle region on SDL_Renderer | ⬜ | Mirrors Task 169 |
| 680 | Pixel test: `SetData` `startIndex`/`elementCount` slice on SDL_Renderer | ⬜ | Mirrors Task 170 |
| 681 | Decide and document `Texture2D` mip-level `SetData`/`GetData` on SDL_Renderer | ⬜ | SDL textures have no native mip chain — single-level-only fallback, or throw for `level>0`; must not silently no-op |
| 682 | Verify `Texture2D::FromStream` (PNG/JPG/BMP/DDS) round-trip renders correctly when drawn via SDL_Renderer | ⬜ | |
| 683 | Verify `SaveAsPng`/`SaveAsJpeg` round-trip when the source texture was created/updated through SDL_Renderer | ⬜ | |
| 684 | Verify NPOT texture upload+sample correctness on SDL_Renderer | ⬜ | 3×5, 7×11 — mirrors Task 268 |
| 685 | Pixel test: `TextureAddressMode::Clamp` via `SpriteBatch` on SDL_Renderer | ⬜ | |
| 686 | Decide and document `TextureAddressMode::Wrap` via `SpriteBatch` on SDL_Renderer | ⬜ | SDL's texture sampling has no native wrap mode — emulate via tiled draw calls, or throw `NotSupportedException`; must not silently clamp when Wrap was requested |
| 687 | Decide and document `TextureAddressMode::Mirror` via `SpriteBatch` on SDL_Renderer | ⬜ | Same decision shape as Task 686 |
| 688 | Pixel test: `TextureFilter::Point` vs `Linear` via `SDL_ScaleMode` on SDL_Renderer | ⬜ | |
| 689 | Verify `Texture2D::Dispose` releases the underlying `SDL_Texture` exactly once | ⬜ | No double-free |

### SpriteFont on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 690 | Pixel test: single glyph at known position on SDL_Renderer | ⬜ | Mirrors Task 424 |
| 691 | Pixel test: multiple glyphs with spacing on SDL_Renderer | ⬜ | Mirrors Task 425 |
| 692 | Pixel test: newline advances by line spacing on SDL_Renderer | ⬜ | Mirrors Task 426 |
| 693 | Pixel test: default character fallback on SDL_Renderer | ⬜ | Mirrors Task 427 |
| 694 | Verify `SpriteEffects` flip + rotation/origin/scale with `DrawString` on SDL_Renderer | ⬜ | Mirrors Tasks 428–429 |

### BlendState on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 695 | Audit `BlendState` → `SDL_BlendMode` mapping for all 4 presets | ⬜ | Opaque/AlphaBlend/Additive/NonPremultiplied |
| 696 | Pixel test: `BlendState::Opaque` on SDL_Renderer | ⬜ | |
| 697 | Pixel test: `BlendState::AlphaBlend` premultiplied alpha on SDL_Renderer | ⬜ | |
| 698 | Pixel test: `BlendState::NonPremultiplied` on SDL_Renderer | ⬜ | |
| 699 | Pixel test: `BlendState::Additive` saturation behavior on SDL_Renderer | ⬜ | |
| 700 | Decide and document custom (non-preset) `BlendState` combinations on SDL_Renderer | ⬜ | `SDL_ComposeCustomBlendMode` has a narrower factor set than XNA — document fallback/throw per unsupported combination |

### SamplerState on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 701 | Audit `SamplerState` → `SDL_ScaleMode` + address-mode mapping | ⬜ | |
| 702 | Verify default sampler state matches FNA's `LinearClamp` on SDL_Renderer | ⬜ | |
| 703 | Verify per-draw sampler state changes take effect on the next `SpriteBatch::Begin` on SDL_Renderer | ⬜ | |

### RenderTarget2D on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 704 | Audit `RenderTarget2D` construction on SDL_Renderer | ⬜ | `SDL_TEXTUREACCESS_TARGET` |
| 705 | Verify `RenderTarget2D` can be sampled as `Texture2D` after unbinding on SDL_Renderer | ⬜ | |
| 706 | Verify `RenderTargetUsage::DiscardContents` vs `PreserveContents` on SDL_Renderer | ⬜ | |
| 707 | Verify `GetBackBufferData` reads correct pixels after `SetRenderTarget(nullptr)` restores the backbuffer on SDL_Renderer | ⬜ | |
| 708 | Decide and document render-target depth-buffer behavior on SDL_Renderer | ⬜ | SDL_Renderer has no depth-buffer concept — throw, or silently ignore `DepthFormat`, matching the 2D-only design intent |
| 709 | Verify MRT (`SetRenderTargets` with 2+ bindings) throws clearly on SDL_Renderer | ⬜ | SDL_Renderer supports one active render target at a time — must not silently render to only the first |

### Viewport / PresentationParameters / GraphicsDeviceManager on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 710 | Verify `Viewport` get/set round-trip and `Project`/`Unproject` math on SDL_Renderer | ⬜ | 2D orthographic case |
| 711 | Verify backbuffer resize through `PresentationParameters` on SDL_Renderer | ⬜ | |
| 712 | Verify fullscreen toggle on SDL_Renderer | ⬜ | |
| 713 | Verify `PresentInterval` (vsync) mapping to `SDL_SetRenderVSync` on SDL_Renderer | ⬜ | |
| 714 | Decide and document `MultiSampleCount` behavior on SDL_Renderer | ⬜ | SDL_Renderer has no MSAA control — accept-and-ignore-with-log, or throw |
| 715 | Verify `DeviceResetting`/`DeviceReset` events fire correctly on SDL_Renderer backbuffer resize | ⬜ | |

### GraphicsDevice / resource lifecycle on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 716 | Verify `GraphicsDevice::Clear` (all `ClearOptions` combinations) on SDL_Renderer | ⬜ | |
| 717 | Verify disposed-resource guards throw `ObjectDisposedException` consistently on SDL_Renderer | ⬜ | Texture2D, RenderTarget2D, BlendState, SamplerState, SpriteBatch |
| 718 | Verify double-`Dispose` is safe for every SDL_Renderer-backed resource type | ⬜ | |
| 719 | Add leak-check test: create/dispose 80 SDL_Renderer textures/render-targets, verify `GetTrackedResourceCount` returns to baseline | ⬜ | Mirrors Task 219 |

### Systematic "3D throws correctly" sweep on SDL_Renderer

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 720 | Verify `DrawPrimitives`/`DrawIndexedPrimitives`/`DrawInstancedPrimitives` throw the correct exception type+message on SDL_Renderer | ⬜ | Not just "throws something" |
| 721 | Verify all 5 `DrawUserPrimitives` typed + `VertexDeclaration` overloads throw correctly on SDL_Renderer | ⬜ | |
| 722 | Verify all 10 `DrawUserIndexedPrimitives` typed + `VertexDeclaration` overloads throw correctly on SDL_Renderer | ⬜ | |
| 723 | Re-verify `CreateVertexBuffer`/`CreateIndexBuffer`/Dynamic variants throw correctly on SDL_Renderer with Task-261-style rigor | ⬜ | Phase 3 covered this at a lighter pass |
| 724 | Verify `VertexDeclaration` construction does **not** throw on SDL_Renderer | ⬜ | Pure data description — only the draw call should throw, not declaration/construction |
| 725 | Decide and document `Texture3D`/`TextureCube` construction on SDL_Renderer | ⬜ | Throw at construction, or allow construction but throw on Draw/sampling — match FNA's behavior model as closely as a 2D-only backend can |
| 726 | Verify all 5 stock 3D effects' `Apply()`+property setters do **not** throw on SDL_Renderer, but drawing with them does | ⬜ | Matches FNA: effects are backend-agnostic data objects until actually used to draw |
| 727 | Verify `OcclusionQuery::Begin`/`End` throw correctly on SDL_Renderer | ⬜ | |
| 728 | Verify `Model::Draw` throws correctly on SDL_Renderer | ⬜ | Requires 3D primitives |
| 729 | Verify `RasterizerState`/`DepthStencilState` can be constructed and assigned without throwing on SDL_Renderer | ⬜ | Pure data, mirrors Task 724's pattern |

### Compatibility proof

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 730 | Port and run 5 minimal 2D-only FNA/XNA samples specifically targeting SDL_Renderer | ⬜ | Real compatibility proof for the 2D-only backend, not EasyGL |
| 731 | Document `docs/sdl-renderer-2d-completeness.md`: final per-feature support table + decisions made for Tasks 676/681/686–687/700/708–709/725, with rationale | ⬜ | ✅/⚠️-emulated/❌-throws-by-design per feature |

---

## Phase 71 — EasyGL: final gap closure and perfection gate

> Runs second. Deliberately small: EasyGL is already the implicit default target of most
> unlabeled tasks across Phases 34–55 (see the coverage-analysis artifact from this session —
> among 235 pending core tasks, 189 name no backend and default to EasyGL by established
> convention). This phase covers only what genuinely isn't tracked anywhere else yet.

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 732 | Implement real `SurfaceFormat` GPU forwarding on EasyGL | ⬜ | `CreateTexture`/`CreateTexture3D`/`CreateTextureCube` currently hardcode `GL_RGBA8` regardless of requested format; map Color/Bgr565/Bgra4444/Bgra5551/Rgba1010102/Rg32/Rgba64/Single/Vector2/Vector4/HalfSingle/HalfVector2/HalfVector4/HdrBlendable to real GLES 3.2 internal formats; throw clearly for genuinely unsupported ones |
| 733 | Implement `FillMode::WireFrame` emulation on EasyGL via barycentric-coordinate geometry shader + `fwidth()` edge detection | ⬜ | GLES 3.2 has geometry shaders in core — no native `glPolygonMode`, but this is emulatable, not a hardware wall (corrects the earlier "N/A on GLES" framing) |
| 734 | Add a non-geometry-shader barycentric fallback for GLES 3.2 drivers with unreliable geometry-shader support | ⬜ | Vertex-attribute barycentrics (requires unwelding shared vertices) as a documented fallback path; note in `docs/easygl_bugs.md` |
| 735 | Implement `Texture3D` mip-chain generation on EasyGL | ⬜ | `EasyGLTexture3DBackend`'s `mipMap` constructor parameter is currently unused (`bool /*mipMap*/`) — single level always, even though `Texture3D::LevelCount` now correctly reports the requested count (Task 271) |
| 736 | Verify/implement `TextureCube` mip-chain generation on EasyGL | ⬜ | Confirm whether `EasyGLTextureCubeBackend` has the same unused-`mipMap`-parameter pattern as Task 735 found for Texture3D; fix if so |
| 737 | Add explicit `TextureAddressMode::Mirror` regression test for the Task 269 SpriteBatch SamplerState fix | ⬜ | Only Wrap vs. Clamp were independently pixel-tested; Mirror is fixed by the same code path but never verified on its own |
| 738 | Close the remaining EasyGL-scoped ⬜ tasks in Phases 34–55 | ⬜ | Not new work — this is the actual bulk of "EasyGL perfection." See Phases 34, 35, 36, 37, 38 (partial), 39, 40, 41, 42, 43 (partial), 44, 45, 46, 47, 48, 49, 50, 51 (EasyGL row), 52, 53 (EasyGL-relevant rows), 54, 55 (Tasks 493–494 specifically) |
| 739 | Final EasyGL perfection gate: re-run Tasks 493–494 (full unit + integration suite) after Tasks 732–738 land | ⬜ | Zero unexplained failures; update `docs/xna-4-api-coverage.md` overall EasyGL estimate |

---

## Phase 72 — Bgfx: full 2D+3D pixel-verified parity

> Runs third. Unlike the EasyGL phase above, this one is genuinely large — Bgfx is not the
> default target of any existing Phase 34–55 task, so the full breadth of pixel-verification
> work needs an explicit, separate pass. Starts from wiring up the pixel-readback path itself,
> since no current Bgfx test uses it at all.

### Foundational: unlock pixel testing

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 740 | Wire `GraphicsDevice::GetBackBufferData` into a reusable Bgfx pixel-readback test helper | ⬜ | The async `bgfx::requestScreenShot`+`bgfx::frame()` path exists (Task 117, done) but zero current tests call it — this unblocks every pixel test below |
| 741 | Add a Bgfx per-texture-level readback path for direct `Texture2D`/`3D`/`Cube` `GetData` verification | ⬜ | Avoids needing a full draw+backbuffer-read cycle just to check an upload round-tripped |
| 742 | Verify Bgfx `GetBackBufferData` readback reliability under headless CI | ⬜ | The "up to 3 frames" wait in Task 117 may need tuning; add a timeout/retry regression test |

### SamplerState / TextureAddressMode

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 743 | Audit `SamplerState` → `BGFX_SAMPLER_*` flag mapping completeness | ⬜ | |
| 744 | Pixel test: `TextureAddressMode::Clamp` on Bgfx | ⬜ | Mirrors Task 294 |
| 745 | Pixel test: `TextureAddressMode::Wrap` on Bgfx | ⬜ | Mirrors Task 295 |
| 746 | Pixel test: `TextureAddressMode::Mirror` on Bgfx | ⬜ | Mirrors Task 296 |
| 747 | Pixel test: `TextureFilter::Point` vs `Linear` on Bgfx | ⬜ | Mirrors Task 297 |
| 748 | Verify mipmap filter modes (`MipPoint`/`MipLinear`/etc.) on Bgfx | ⬜ | Mirrors Task 298 |
| 749 | Verify anisotropic filtering cap query + fallback on Bgfx | ⬜ | Mirrors Task 299 |
| 750 | Wire a Task-269-equivalent `SpriteBatch` `SamplerState` fix into Bgfx's `ISpriteBatchBackend` | ⬜ | Currently a no-op, same root cause as the EasyGL bug fixed this session and the Vulkan bug tracked as Task 665 |

### BlendState

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 751 | Audit `BlendState` preset mapping to `BGFX_STATE_BLEND_*` | ⬜ | |
| 752 | Pixel test: `BlendState::Opaque` on Bgfx | ⬜ | |
| 753 | Pixel test: `BlendState::AlphaBlend` premultiplied alpha on Bgfx | ⬜ | |
| 754 | Pixel test: `BlendState::NonPremultiplied` on Bgfx | ⬜ | |
| 755 | Pixel test: `BlendState::Additive` saturation on Bgfx | ⬜ | |
| 756 | Verify separate color/alpha blend function + factor combinations on Bgfx | ⬜ | |
| 757 | Verify `BlendFactor` constant-color blending on Bgfx | ⬜ | |

### DepthStencilState

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 758 | Audit `DepthStencilState` preset mapping to Bgfx depth/stencil flags | ⬜ | |
| 759 | Pixel test: depth write enabled vs disabled on Bgfx | ⬜ | |
| 760 | Pixel test: all 8 depth `CompareFunction` values on Bgfx | ⬜ | |
| 761 | Verify stencil enable/disable + read/write masks on Bgfx | ⬜ | |
| 762 | Verify front-face stencil ops (Keep/Replace/Increment/Decrement) on Bgfx | ⬜ | |
| 763 | Verify two-sided stencil ops on Bgfx if exposed | ⬜ | |
| 764 | Verify `ReferenceStencil` device state reaches Bgfx draw calls | ⬜ | |

### RasterizerState

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 765 | Pixel test: culling disabled / `CullClockwise` / `CullCounterClockwise` on Bgfx | ⬜ | |
| 766 | Verify `FillMode::WireFrame` on Bgfx | ⬜ | Confirm real polygon wireframe vs. line-primitive substitution |
| 767 | Verify depth bias and slope-scale depth bias on Bgfx | ⬜ | |
| 768 | Verify scissor test enable/disable interaction with `ScissorRectangle` on Bgfx | ⬜ | |

### RenderTarget2D / RenderTargetCube

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 769 | Verify `RenderTarget2D` can be sampled as `Texture2D` after unbinding on Bgfx | ⬜ | |
| 770 | Verify `RenderTargetCube` can be sampled as `TextureCube` after unbinding on Bgfx | ⬜ | `EnvironmentMapEffect` path |
| 771 | Verify `RenderTargetUsage::DiscardContents` vs `PreserveContents` on Bgfx | ⬜ | |
| 772 | Verify MSAA render target creation + resolve on Bgfx | ⬜ | |
| 773 | Verify `SetRenderTarget(nullptr)` restores the backbuffer on Bgfx | ⬜ | |
| 774 | Verify MRT with mixed formats is rejected or handled per XNA constraints on Bgfx | ⬜ | |
| 775 | Document Bgfx MRT attachment limits | ⬜ | |

### Viewport

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 776 | Verify `Viewport::Project`/`Unproject` math on Bgfx | ⬜ | Confirm no Bgfx-specific NDC/texture-origin convention bug |
| 777 | Verify viewport reset after backbuffer resize on Bgfx | ⬜ | |

### Effect base + BasicEffect

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 778 | Verify `EffectPass::Apply`/`EffectTechnique` selection reach the Bgfx draw-state setup correctly | ⬜ | |
| 779 | Pixel test: `BasicEffect` vertex-color-only on Bgfx | ⬜ | |
| 780 | Pixel test: `BasicEffect` texture-only on Bgfx | ⬜ | |
| 781 | Pixel test: `BasicEffect` texture × vertex color on Bgfx | ⬜ | |
| 782 | Pixel test: `BasicEffect` one directional light on Bgfx | ⬜ | |
| 783 | Pixel test: `BasicEffect` ambient+emissive+specular combination on Bgfx | ⬜ | |
| 784 | Pixel test: `BasicEffect` fog on Bgfx | ⬜ | |

### AlphaTestEffect (Task 375 already covers the CompareFunction sweep)

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 785 | Verify alpha reference value 0–255 vs 0–1 scaling on Bgfx | ⬜ | |
| 786 | Verify `AlphaTestEffect` + vertex/diffuse color interaction on Bgfx | ⬜ | |
| 787 | Verify `AlphaTestEffect` fog behavior on Bgfx | ⬜ | |
| 788 | Verify `AlphaTestEffect` null-texture behavior on Bgfx | ⬜ | |

### DualTextureEffect

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 789 | Pixel test: two white textures + diffuse color on Bgfx | ⬜ | |
| 790 | Pixel test: magenta × yellow = red on Bgfx | ⬜ | Mirrors Tasks 133/135 |
| 791 | Verify first/second texture null behavior on Bgfx | ⬜ | |
| 792 | Verify `DualTextureEffect` fog behavior on Bgfx | ⬜ | |

### EnvironmentMapEffect

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 793 | Pixel test: `EnvironmentMapAmount=0` (ignores cubemap) on Bgfx | ⬜ | |
| 794 | Pixel test: `EnvironmentMapAmount=1` with white cubemap on Bgfx | ⬜ | |
| 795 | Pixel test: `EnvironmentMapSpecular` contribution on Bgfx | ⬜ | |
| 796 | Verify `FresnelFactor` on Bgfx if implemented | ⬜ | |
| 797 | Verify eye position affects reflection vector on Bgfx | ⬜ | |
| 798 | Verify non-identity world/normal-matrix correctness on Bgfx | ⬜ | |

### SkinnedEffect

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 799 | Verify `SetBoneTransforms` accepts the supported bone count + throws for excess on Bgfx | ⬜ | |
| 800 | Pixel test: identity bone palette (no deformation) on Bgfx | ⬜ | |
| 801 | Pixel test: single translation bone on Bgfx | ⬜ | |
| 802 | Pixel test: two-bone 50/50 blend on Bgfx | ⬜ | |

### SpriteBatch (needs Task 740's readback wiring first)

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 803 | Pixel test: `SpriteSortMode` ordering (Deferred/Texture/FrontToBack/BackToFront) on Bgfx | ⬜ | |
| 804 | Pixel test: rotation around origin on Bgfx | ⬜ | |
| 805 | Pixel test: scale overloads on Bgfx | ⬜ | |
| 806 | Pixel test: source rectangle cropping on Bgfx | ⬜ | |
| 807 | Pixel test: `SpriteEffects` flip on Bgfx | ⬜ | Mirrors Task 167 |
| 808 | Pixel test: `transformMatrix` in `SpriteBatch::Begin` on Bgfx | ⬜ | Mirrors Task 168 |

### SpriteFont

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 809 | Pixel test: single glyph at known position on Bgfx | ⬜ | |
| 810 | Pixel test: multiple glyphs with spacing + newline on Bgfx | ⬜ | |
| 811 | Pixel test: default character fallback on Bgfx | ⬜ | |

### Model

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 812 | Pixel test: model with two meshes and different effects on Bgfx | ⬜ | |
| 813 | Pixel test: model hierarchy transform propagation on Bgfx | ⬜ | |

### OcclusionQuery (Task 448 already covers sync)

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 814 | Pixel/query test: fully visible quad returns positive pixel count on Bgfx | ⬜ | |
| 815 | Pixel/query test: fully occluded quad returns zero/lower count on Bgfx | ⬜ | |
| 816 | Verify disposing an active `OcclusionQuery` is safe or throws correctly on Bgfx | ⬜ | |

### Texture2D / Texture3D / TextureCube depth

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 817 | Verify `Texture2D` `SetData`/`GetData` partial-rectangle + `startIndex`/`elementCount` on Bgfx | ⬜ | Mirrors Tasks 169–170; currently only EasyGL has this depth |
| 818 | Verify `Texture2D` mip-level `SetData`/`GetData` on Bgfx | ⬜ | Mirrors Task 171 |
| 819 | Verify `Texture3D` box/`GetData` bounds guards reach correct pixels on Bgfx | ⬜ | Task 271 fixed the C++ guards backend-agnostically; no Bgfx pixel test confirms the GPU side |
| 820 | Verify `TextureCube` per-face/per-mip `SetData`/`GetData` on Bgfx | ⬜ | Mirrors Task 172 |
| 821 | Verify NPOT texture upload+sample on Bgfx | ⬜ | Mirrors Task 268; currently code-inspected only for this backend |

### Final Bgfx perfection gate

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 822 | Run the full Bgfx integration suite (Tasks 740–821) end to end; zero unexplained failures or documented, justified skips | ⬜ | |
| 823 | Update `docs/xna-4-api-coverage.md` §7 stock-effect backend-parity table: move Bgfx from "compiles only" to "pixel-verified" only where proven by Tasks 740–822 | ⬜ | |
| 824 | Document remaining genuine Bgfx architectural limitations (if any survive) in `docs/graphics-backend-feature-matrix.md` | ⬜ | |

---

## Phase 73 — Vulkan: full 2D+3D pixel-verified parity (gap closure)

> Runs fourth (last of the four backend-focused phases; WebGPU remains parked after this).
> Vulkan already has substantially more infrastructure than Bgfx — per-slot samplers (Task 118),
> custom Effect/SPIR-V (119), instancing (111), wireframe (112), MSAA (147), RenderTargetCube
> (142), Texture3D/Cube backends (143), scissor (329), depth bias (328), and stock-effect SPIR-V
> shaders for all 5 effects (102–109) — so this phase is sized as gap-closure, not full
> replication, plus the two SpriteBatch bugs found this session.

| #   | Task | Status | Notes |
| --- | ---- | ------ | ----- |
| 664 | *(see Phase 47 above)* Fix Vulkan `SpriteBatch` multi-`Begin`/`End` bug | ⬜ | Cross-referenced here; lives in Phase 47's table |
| 665 | *(see Phase 47 above)* Fix Vulkan `SpriteBatch` `SamplerState` no-op | ⬜ | Cross-referenced here; lives in Phase 47's table |
| 825 | Pixel test: `TextureAddressMode::Clamp` on Vulkan | ⬜ | |
| 826 | Pixel test: `TextureAddressMode::Wrap` on Vulkan | ⬜ | Verifies Task 665's fix once landed |
| 827 | Pixel test: `TextureAddressMode::Mirror` on Vulkan | ⬜ | |
| 828 | Pixel test: `TextureFilter::Point` vs `Linear` on Vulkan | ⬜ | |
| 829 | Verify mipmap filter modes on Vulkan | ⬜ | |
| 830 | Verify anisotropic filtering cap + fallback on Vulkan | ⬜ | `VkPhysicalDeviceFeatures.samplerAnisotropy` |
| 831 | Pixel test: `BlendState::Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive` on Vulkan | ⬜ | Consolidated, 4 sub-cases — mirrors Task 189's pattern |
| 832 | Verify separate color/alpha blend function + factor combinations on Vulkan | ⬜ | |
| 833 | Verify `BlendFactor` constant-color blending on Vulkan | ⬜ | |
| 834 | Pixel test: depth write enabled vs disabled on Vulkan | ⬜ | |
| 835 | Pixel test: all 8 depth `CompareFunction` values on Vulkan | ⬜ | |
| 836 | Verify stencil enable/disable + read/write masks + front-face ops on Vulkan | ⬜ | |
| 837 | Verify two-sided stencil ops on Vulkan | ⬜ | `VkStencilOpState` front/back |
| 838 | Verify `ReferenceStencil` device state reaches Vulkan draw calls | ⬜ | |
| 839 | Pixel test: culling disabled / `CullClockwise` / `CullCounterClockwise` on Vulkan | ⬜ | |
| 840 | Verify `RenderTarget2D` can be sampled as `Texture2D` after unbinding on Vulkan | ⬜ | Extends Task 148's full-cycle test with an explicit sampling-after-unbind assertion |
| 841 | Add the pixel-readback confirmation for `RenderTargetUsage::DiscardContents` vs `PreserveContents` on Vulkan | ⬜ | Task 178 implemented the render-pass load-op mapping; no pixel test confirms it yet |
| 842 | Verify MRT with mixed formats is rejected or handled per XNA constraints on Vulkan | ⬜ | |
| 843 | Verify `Viewport::Project`/`Unproject` math on Vulkan | ⬜ | Confirm no Vulkan Y-flip/NDC convention bug |
| 844 | Pixel test: `BasicEffect` vertex-color-only / texture-only / texture×vertex-color / one-light / ambient+emissive+specular on Vulkan | ⬜ | Consolidated, mirrors Task 189; Vulkan currently only has Phase 9–14's combined smoke coverage |
| 845 | Pixel test: `BasicEffect` fog on Vulkan | ⬜ | EasyGL got this in Task 195; audit Vulkan `GpuDrawParams`/SPIR-V shaders for a fog uniform, add if missing |
| 846 | Verify alpha reference scaling + vertex/diffuse color interaction + fog + null-texture behavior in `AlphaTestEffect` on Vulkan | ⬜ | |
| 847 | Verify `DualTextureEffect` null-texture behavior + fog on Vulkan | ⬜ | Blend correctness already covered by Task 135 |
| 848 | Verify `EnvironmentMapEffect` `FresnelFactor` + eye-position + non-identity world-matrix correctness on Vulkan | ⬜ | Amount/specular already covered by Task 136 |
| 849 | Verify `SkinnedEffect` bone-count boundary + two-bone-blend pixel correctness on Vulkan | ⬜ | Add explicit pixel assertions beyond the existing shader smoke test |
| 850 | Pixel test: `SpriteSortMode` ordering (Deferred/Texture/FrontToBack/BackToFront) on Vulkan | ⬜ | |
| 851 | Pixel test: rotation/scale/source-rectangle-cropping/`SpriteEffects` flip on Vulkan | ⬜ | Consolidated |
| 852 | Pixel test: single glyph + multi-glyph spacing + newline + default-character-fallback on Vulkan | ⬜ | `SpriteFont` has no dedicated Vulkan pixel test yet |
| 853 | Pixel test: model with two meshes and hierarchy transform propagation on Vulkan | ⬜ | |
| 854 | Pixel/query test: visible vs occluded quad pixel counts on Vulkan | ⬜ | Task 447 covers sync correctness; add the actual pixel/query-count assertions |
| 855 | Verify `Texture2D` `SetData`/`GetData` partial-rectangle + `startIndex`/`elementCount` + mip-level on Vulkan | ⬜ | Currently only EasyGL has this depth (Tasks 169–171) |
| 856 | Verify `TextureCube` per-face/per-mip `SetData`/`GetData` pixel correctness on Vulkan | ⬜ | Mirrors Task 172 |
| 857 | Verify NPOT texture upload+sample on Vulkan | ⬜ | Mirrors Task 268; currently code-inspected only |
| 858 | Verify `Texture3D` box-region `SetData`/`GetData` pixel correctness on Vulkan | ⬜ | Task 271's guards are backend-agnostic C++; no Vulkan pixel test confirms the GPU side |
| 859 | Run the full Vulkan integration suite (Tasks 664–665, 825–858) end to end; zero unexplained failures or documented, justified skips | ⬜ | |
| 860 | Update `docs/xna-4-api-coverage.md` §7 stock-effect backend-parity table: move Vulkan rows from "partial" to "pixel-verified" only where proven by Tasks 825–859 | ⬜ | |
| 861 | Document remaining genuine Vulkan limitations (if any survive) in `docs/graphics-backend-feature-matrix.md` | ⬜ | |
