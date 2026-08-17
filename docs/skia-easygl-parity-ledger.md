# Skia–EasyGL graphics API parity ledger

This ledger is the row-per-entry inventory required by SKIA-1. It describes the observable CNA
contract, not whether Skia exposes an OpenGL-shaped call. Entries are extracted from the eleven
public renderer/resource interfaces in `IGraphicsRenderer.hpp`, every `GraphicsCapability`, and every
public non-deleted `GraphicsDevice` method declaration. Overloads use `name/arity`; when that is
still ambiguous, `#N` is their declaration order in the audited header.

The current audited inventory contains 258 entries: 134 renderer/resource methods, eleven
`GraphicsCapability` values, and 113 public `GraphicsDevice` declarations. The number is descriptive;
the validator derives the authoritative live set from the headers.

Statuses have exact meanings:

- `implemented`: the selected raster Skia renderer covers the complete currently reachable entry;
- `bounded`: a documented 2D subset works and unsupported variants fail explicitly;
- `unsupported`: Skia reports/refuses the entry deliberately rather than silently succeeding;
- `internal`: the entry is renderer plumbing or a native-handle escape with no cross-renderer pixel
  feature to emulate; Skia implements the appropriate non-GL result.

Run `python3 scripts/validate_skia_parity_ledger.py` after changing either audited header or this
file. The validator rejects missing, stale, duplicated, malformed, or unclassified rows.

## Renderer and resource interfaces

| Entry | EasyGL behavior and test surface | Skia result or plan | Status | Evidence or follow-up |
|---|---|---|---|---|
| `IVertexBufferRenderer::SetData/3` | Uploads a GL vertex stream; 3D buffer tests. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95 |
| `IVertexBufferRenderer::SetDataWithOptions/4` | Uploads with streaming hint; dynamic-buffer tests. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95 |
| `IVertexBufferRenderer::SetVertexDeclaration/1` | Configures GL vertex layout; declaration tests. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95 |
| `IVertexBufferRenderer::GetVertexCount/0` | Reports uploaded vertex capacity. | Resource creation is rejected first. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::SetData16/2` | Uploads 16-bit GL indices; index tests. | No raster 3D index pipeline. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::SetData32/2` | Uploads 32-bit GL indices; index tests. | No raster 3D index pipeline. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::SetData16WithOptions/3` | Streams 16-bit indices; dynamic-buffer tests. | No raster 3D index pipeline. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::SetData32WithOptions/3` | Streams 32-bit indices; dynamic-buffer tests. | No raster 3D index pipeline. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::GetIndexCount/0` | Reports uploaded index capacity. | Resource creation is rejected first. | `unsupported` | SKIA-95 |
| `IIndexBufferRenderer::IsThirtyTwoBit/0` | Selects GL index element type. | Resource creation is rejected first. | `unsupported` | SKIA-95 |
| `IOcclusionQueryRenderer::Begin/0` | Begins a GL samples-passed query; query tests. | Refusal object throws the stable no-3D diagnostic. | `unsupported` | SKIA-102, SKIA-104–105 |
| `IOcclusionQueryRenderer::End/0` | Ends the active GL query. | Refusal object throws the stable no-3D diagnostic. | `unsupported` | SKIA-102, SKIA-104–105 |
| `IOcclusionQueryRenderer::IsComplete/0` | Polls GL query availability. | Refusal object safely returns false without blocking. | `unsupported` | SKIA-102, SKIA-104–105 |
| `IOcclusionQueryRenderer::PixelCount/0` | Returns EasyGL's boolean samples-passed result. | Refusal object returns zero; no unsound framebuffer-derived count is invented. | `unsupported` | SKIA-102, SKIA-104–105 |
| `ITextureCubeRenderer::SetData/8` | Uploads one GL cube-face region; cube transfer tests. | Exact bounded CPU face/mip/rectangle storage. | `implemented` | SKIA-80–84; shared 56-check write audit |
| `ITextureCubeRenderer::GetData/8` | Reads one cube-face region; cube readback tests. | Exact bounded CPU readback; no fabricated pixels. | `implemented` | SKIA-80–84; shared 56-check read audit |
| `ITextureCubeRenderer::BindGL/0` | Binds the cube GL target. | No GL/native cube handle exists. | `unsupported` | SKIA-80–84 |
| `ITextureCubeRenderer::GetSizeEXT/0` | N/A -- SKIA-149 CNAEXT extension; the shared base default returns 0 for every other renderer. | Reports the bound cube map's stored square face dimension so `cnaSampleCubeEXT` can size its per-draw CPU face read. | `implemented` | SKIA-149; `Skia_CubeVolume_Effect_Binding` |
| `ITextureCubeRenderer::ShareCpuPixels/2` | Hands the renderer a reference to `TextureCube::cpuPixels_[face]` so a GL-style renderer can restore level 0 after a context loss; only OPENGL1 implements it. | Keeps the shared no-op default. Cube storage here is CPU-owned and outlives presenter reconstruction unchanged, so there is no lost GPU image to restore and no second shadow to keep coherent. | `internal` | SKIA-80–84, SKIA-110; `Skia_ContextRecovery`, `Skia_ResourceBudget` |
| `ITexture3DRenderer::SetData/9` | Uploads a GL volume region; Texture3D tests. | Exact bounded CPU mip/sub-volume storage. | `implemented` | SKIA-82–84; shared 56-check write audit |
| `ITexture3DRenderer::GetData/9` | Reads a GL volume region; Texture3D tests. | Exact bounded CPU readback; no fabricated voxels. | `implemented` | SKIA-82–84; shared 56-check read audit |
| `ITexture3DRenderer::BindGL/0` | Binds the GL volume target. | No GL/native volume handle exists. | `unsupported` | SKIA-82–84 |
| `ITexture3DRenderer::GetDimensionsEXT/3` | N/A -- SKIA-149 CNAEXT extension; the shared base default sets all three out-params to 0 for every other renderer. | Reports the bound volume's stored width/height/depth so `cnaSampleVolumeEXT` can size its per-draw CPU voxel read and grid-atlas packing. | `implemented` | SKIA-149; `Skia_CubeVolume_Effect_Binding` |
| `ITextureRenderer::GetWidth/0` | Reports GL texture width; Texture2D tests. | Reports the CPU image width. | `implemented` | SKIA-22, SKIA-26 |
| `ITextureRenderer::GetHeight/0` | Reports GL texture height; Texture2D tests. | Reports the CPU image height. | `implemented` | SKIA-22, SKIA-26 |
| `ITextureRenderer::GetNativeTexture/0` | EasyGL exposes no SDL texture. | Returns null; Skia owns an `SkImage`. | `internal` | SKIA-22 |
| `ITextureRenderer::UpdatePixels/2` | Replaces level zero with a GL upload. | Rebuilds both alpha-labelled CPU images and regenerates only dirty unauthored descendants. | `implemented` | SKIA-22–24, SKIA-128; `Skia_Texture2D_MipGeneration` |
| `ITextureRenderer::UpdatePixelsLevel/4` | Uploads a requested GL mip level. | Textures upload every valid level into stable checked CNA storage with authored barriers; targets upload the addressed stable surface and eagerly regenerate its suffix. Invalid levels/dimensions reject before mutation. | `implemented` | SKIA-27, SKIA-125–132; `Skia_Texture2D_MipGeneration`, `Skia_RenderTarget2D_MipGeneration` |
| `ITextureRenderer::HasDefinedMipLevel/1` | EasyGL has no separate initialized-level query; caller-authored CPU shadows define ordinary mip content. | Reports every allocated CNA texture or target mip as deterministic readable storage, allowing renderer-defined levels without a duplicate public shadow. | `internal` | SKIA-128, SKIA-131; `Skia_Texture2D_MipGeneration`, `Skia_RenderTarget2D_MipStorage` |
| `ITextureRenderer::BindGL/0` | Binds EasyGL's native texture. | Intentional no-op; SpriteBatch samples `SkImage`. | `internal` | SKIA-22, SKIA-32 |
| `ITextureRenderer::ShareCpuPixels/1` | Shares restoration shadow with EasyGL. | Common texture shadow already owns the bytes. | `internal` | SKIA-23, SKIA-28 |
| `ITextureRenderer::GetData/7` | Reads GL target pixels when no CPU shadow exists. | Exact full/partial readback for every Texture2D and RenderTarget2D mip; a valid target read is a one-shot resolve barrier for every dirty descendant and common staging never masks live target bytes. | `implemented` | SKIA-23–24, SKIA-62, SKIA-127, SKIA-131–132; `Skia_Texture2D_MipTransfer`, `Skia_RenderTarget2D_MipGeneration` |
| `IRenderTargetRenderer::BindAsRenderTarget/0` | Binds an EasyGL FBO. | Selection is owned by checked Skia target binding. | `implemented` | SKIA-61, SKIA-69 |
| `IRenderTargetRenderer::UnbindAsRenderTarget/0` | Resolves/unbinds the EasyGL FBO. | Binding restores the raster backbuffer. | `implemented` | SKIA-61, SKIA-69 |
| `IRenderTargetRenderer::GetColorGLHandle/0` | Returns the FBO color texture name. | Returns zero; sampling uses snapshots. | `internal` | SKIA-63, SKIA-74 |
| `IRenderTargetRenderer::GetMultiSampleCount/0` | Reports device-clamped target samples. | Reports zero; real MSAA requests reject. | `bounded` | SKIA-73, SKIA-76 |
| `IRenderTargetRenderer::HasRealDepthBuffer/1` | Reports the actual GL depth attachment. | Always false for raster targets. | `unsupported` | SKIA-67 |
| `IRenderTargetCubeRenderer::GetSize/0` | Reports GL cube-face size. | Reports the exact raster face extent. | `implemented` | SKIA-85–86; shared properties contract |
| `IRenderTargetCubeRenderer::BindAsRenderTargetFace/1` | Selects one cube FBO face. | Selects one of six stable level-zero SkSurfaces. | `implemented` | SKIA-85–86; plural binding contract |
| `IRenderTargetCubeRenderer::UnbindAsRenderTarget/0` | Resolves/unbinds a cube face. | Generates dirty face mips and restores the backbuffer. | `implemented` | SKIA-85–86; mip/readback contracts |
| `IRenderTargetCubeRenderer::GetGLHandle/0` | Returns the cube texture name. | Returns zero; cube sampling remains unavailable. | `unsupported` | SKIA-85–86; `skia-texture-storage.md` |
| `IRenderTargetCubeRenderer::GetMultiSampleCount/0` | Reports cube-face sample count. | Reports the honestly applied raster count of zero. | `bounded` | SKIA-85–86; policy/usage contracts |
| `IRenderTargetCubeRenderer::HasRealDepthBuffer/1` | Reports cube depth attachment. | Always false; requested depth remains metadata. | `unsupported` | SKIA-85–86; direct policy test |
| `IRenderTargetCubeRenderer::SetData/8` | Uploads one rendered cube-face region where supported. | Exact bounded face/mip/rectangle upload. | `implemented` | SKIA-85–86; shared GetData contract |
| `IEffectRenderer::CompileProgram/2` | Compiles GLSL vertex/fragment programs; shader tests. | Exact `CNA_SKIA_SKSL_V1` marker compiles one bounded SkSL 100 SpriteBatch shader ABI; arbitrary GLSL remains unsupported. | `bounded` | SKIA-89–92; `Skia_SkSL_Effect_Prototype` |
| `IEffectRenderer::Bind/0` | Binds an EasyGL program. | Valid tagged runtime effect accepts Apply/Bind; SpriteBatch composes it per draw. | `bounded` | SKIA-91–92 |
| `IEffectRenderer::Unbind/0` | Restores EasyGL program state. | Clears adapter binding state; stock SpriteBatch selection remains explicit. | `bounded` | SKIA-91–92 |
| `IEffectRenderer::IsValid/0` | Reports GL link success. | Reports tagged SkSL compile plus reserved-ABI validation. | `bounded` | SKIA-91–92 |
| `IEffectRenderer::GetCompileError/0` | Exposes GL compile/link diagnostics. | Retains compiler, source-limit, and reflected-ABI diagnostics for tagged effects. | `bounded` | SKIA-91–92 |
| `IEffectRenderer::SetUniformFloat/2` | Sets a scalar GL uniform. | Exact reflected non-array float write in tagged SkSL. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformInt/2` | Sets an integer GL uniform. | Exact reflected 32-bit int write in tagged SkSL. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformVec2/3` | Sets a vec2 GL uniform. | Exact reflected float2 write in tagged SkSL. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformVec3/4` | Sets a vec3 GL uniform. | Exact reflected float3 write in tagged SkSL. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformVec4/5` | Sets a vec4 GL uniform. | Exact reflected float4 write; reserved cnaTint rejects public writes. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformMat4/2` | Sets a matrix GL uniform. | Exact column-major reflected float4x4 write. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformFloatArray/3` | Sets a GL scalar array. | Exact reflected float-array count/data write. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::SetUniformVec2Array/3` | Sets a GL vec2 array. | Exact reflected float2-array count/data write. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::BindTexture/2` | Binds an extra 2D sampler. | Units 1–7 weakly bind declared cnaTexture1–7 children; draw snapshots current live pixels. | `bounded` | SKIA-92; `Skia_SkSL_UniformTexture` |
| `IEffectRenderer::BindTextureCube/2` | Binds an extra cube sampler. | Binds unit 1 for the bounded `cnaSampleCubeEXT` extension when the effect declares all six `cnaCubeFace0`-`5` children; rejects unit≠1, undeclared children, null, or an expired renderer. | `bounded` | SKIA-144–151; `Skia_CubeVolume_Effect_Binding`, `Skia_CubeVolume_Sampling_Oracle` |
| `IEffectRenderer::BindTexture3D/2` | Binds an extra volume sampler. | Binds unit 1 for the bounded `cnaSampleVolumeEXT` extension when the effect declares the `cnaVolumeAtlas0` child; rejects unit≠1, undeclared children, null, an expired renderer, or a padded atlas exceeding the 256 MiB budget. | `bounded` | SKIA-144–151; `Skia_CubeVolume_Effect_Binding`, `Skia_CubeVolume_Sampling_Oracle` |
| `ISpriteBatchRenderer::Begin/0` | Starts EasyGL sprite submission. | Starts checked immediate canvas session. | `implemented` | SKIA-31 |
| `ISpriteBatchRenderer::End/0` | Flushes/ends sprite submission. | Ends checked canvas session. | `implemented` | SKIA-31 |
| `ISpriteBatchRenderer::SetTransformMatrix/1` | Applies sprite transform in GL shader. | Applies equivalent SkCanvas transform. | `implemented` | SKIA-35 |
| `ISpriteBatchRenderer::SetCustomEffect/1` | Selects a custom EasyGL effect. | Null/exact stock SpriteEffect use built-in paint; valid tagged SkSL uses runtime shader; stock 3D effects identify the missing primitive route and other custom effects reject. | `bounded` | SKIA-89–94; both SkSL tests; `Skia_SpriteEffect_Alias`; `Skia_StockEffect_Boundary` |
| `ISpriteBatchRenderer::SetSamplerFilter/1` | Selects GL point/linear/mip filtering. | All nine ordinals preserve independent min/mag/mip components over the complete Texture2D chain; Anisotropic is exactly the complete Linear fallback and remains capability-false. | `bounded` | SKIA-43, SKIA-70, SKIA-78–79, SKIA-129; `Skia_MipSampling_Raster`, `Skia_Sampler_MipmapFilterPolicy` |
| `ISpriteBatchRenderer::SetSamplerAddressMode/2` | Selects GL clamp/wrap/mirror axes. | Both axes implemented in Skia shader. | `implemented` | SKIA-44–46 |
| `ISpriteBatchRenderer::Draw/3` | Draws a texture at point position. | Direct canvas image draw. | `implemented` | SKIA-32 |
| `ISpriteBatchRenderer::Draw/4` | Draws destination/source/tint rectangles. | Direct canvas image draw. | `implemented` | SKIA-32–33 |
| `ISpriteBatchRenderer::Draw/8` | Draws full transformed sprite contract. | Rotation/origin/effects/depth path covered. | `implemented` | SKIA-34–39 |
| `ISpriteBatchRenderer::DrawMeshEXT/7` | N/A -- SKIA-157 CNAEXT extension; the shared base default throws for every other renderer. | Draws a triangle-list `SkVertices` mesh through a bound `CNA_SKIA_SKSL_MESH_V1` effect's shader, composed with the active transform exactly like an ordinary sprite draw; requires `SpriteSortMode::Immediate` (does not join the deferred sort/batch queue) and throws if a declared texture child was never bound. | `bounded` | SKIA-144–157; `Skia_MeshEffect_PublicApi` |
| `IGraphicsRenderer::Clear/4` | Clears current GL framebuffer color. | Clears active raster surface. | `implemented` | SKIA-13, SKIA-61 |
| `IGraphicsRenderer::Present/0` | Swaps EasyGL window buffers. | Uploads raster snapshot to SDL presenter. | `implemented` | SKIA-7, SKIA-13 |
| `IGraphicsRenderer::GetViewportSize/2` | Reports EasyGL logical target size. | Reports active raster logical size. | `implemented` | SKIA-13, SKIA-61 |
| `IGraphicsRenderer::GetDefaultViewportRect/4` | Reports the PHYSICAL rectangle a GL/GPU renderer must program after a resize or presentation-mode change; OPENGL2 is the reference override. | Keeps the shared base result -- origin plus the logical size -- and that is the correct answer here, not an unimplemented one: this renderer has no GPU viewport to program, and Letterbox/Overscan/Stretch scale and centre through SDL's own logical presentation while the raster surface retains the requested dimensions. | `implemented` | SKIA-13–14, SKIA-71–72; `Skia_PresentationModes`, `Skia_Contract_ViewportResetAfterResize` |
| `IGraphicsRenderer::SetVirtualResolution/2` | Updates EasyGL logical projection/presentation. | Reallocates/maps raster presentation. | `implemented` | SKIA-13–14 |
| `IGraphicsRenderer::SetPresentationMode/1` | Applies EasyGL presentation mapping. | All five mappings are pixel-tested. | `implemented` | SKIA-13–14 |
| `IGraphicsRenderer::SetSwapInterval/1` | Applies GL swap interval. | Applies SDL presenter interval. | `implemented` | SKIA-15 |
| `IGraphicsRenderer::ApplyMultiSampleCount/1` | Reconfigures EasyGL backbuffer MSAA. | Raster requests 0/1/2/4/4096 all write back actual zero; no samples are fabricated. | `unsupported` | SKIA-76–77; `Skia_RenderTarget2D_MsaaPolicy` |
| `IGraphicsRenderer::UpdatePresentationFormatEXT/3` | EasyGL keeps its historical format policy. | Raster format is fixed RGBA8888; fullscreen is separate. | `bounded` | SKIA-8, SKIA-17 |
| `IGraphicsRenderer::GetMultiSampleCount/0` | Reports EasyGL actual backbuffer samples. | Reports zero. | `unsupported` | SKIA-17, SKIA-76 |
| `IGraphicsRenderer::TransformWindowToLogical/4` | Converts through EasyGL presentation mapping. | Uses SDL's DPI-aware renderer mapping. | `implemented` | SKIA-14, SKIA-72 |
| `IGraphicsRenderer::TransformLogicalToWindow/4` | Converts inverse EasyGL presentation mapping. | Uses SDL's DPI-aware inverse mapping. | `implemented` | SKIA-14, SKIA-72 |
| `IGraphicsRenderer::CreateTexture/1` | Allocates an EasyGL 2D texture. | Allocates the complete checked zeroed 2D mip chain and level-zero alpha-labelled Skia images; every level transfers, generates, and samples through bounded zero-copy raster views. | `bounded` | SKIA-22–30, SKIA-125–129 |
| `IGraphicsRenderer::CreateSpriteBatch/0` | Allocates EasyGL sprite renderer state. | Allocates checked SkCanvas adapter. | `implemented` | SKIA-31–40 |
| `IGraphicsRenderer::ReadBackbuffer/5` | Reads GL framebuffer top-left RGBA. | Exact active-surface RGBA8 readback. | `implemented` | SKIA-7, SKIA-62 |
| `IGraphicsRenderer::CreateOcclusionQuery/0` | Creates GL samples-passed query. | Creates a refusal object: safe false/zero properties; Begin/End throw after SKIA-104 disproves raster emulation. | `unsupported` | SKIA-102, SKIA-104–105 |
| `IGraphicsRenderer::CreateTexture3D/5` | Allocates GL volume texture/mips. | Creates bounded CPU voxel/mip storage only. | `bounded` | SKIA-82–84; `Skia_TextureStorage_Policy` |
| `IGraphicsRenderer::CreateTextureCube/3` | Allocates GL cube texture/mips. | Creates six bounded CPU faces/mips only. | `bounded` | SKIA-80–84; `Skia_TextureStorage_Policy` |
| `IGraphicsRenderer::CreateRenderTarget2D/6` | Allocates GL FBO with requested attachments. | Color targets own a bindable level-zero surface; mip requests add complete stable per-level surfaces, exact shadows and deterministic resolve generation. Depth and MSAA remain bounded refusals. Superseded as the live construction path by `CreateRenderTarget2DEXT`; kept for the shared interface's default-forwarding base case. | `bounded` | SKIA-61–79, SKIA-131–132; `Skia_RenderTarget2D_MipGeneration` |
| `IGraphicsRenderer::CreateRenderTarget2DEXT/7` | N/A -- SKIA-142 CNAEXT extension carrying an explicit `SurfaceFormat`. | The live Skia render-target construction path. Promotes the 13 formats FNA itself reports renderable (matching real XNA/FNA renderability, not Skia's own raster capability); every other format is refused before allocation. Depth and MSAA remain bounded refusals, matching `CreateRenderTarget2D`. | `bounded` | SKIA-142; `Skia_RenderTarget2D_FormatSupport` |
| `IGraphicsRenderer::SetRenderTarget2D/1` | Binds/unbinds one EasyGL FBO and resolves mips when leaving it. | Binds checked raster target/backbuffer and resolves dirty descendants once; foreign/cross-device requests reject before changing or resolving the active target. | `implemented` | SKIA-61, SKIA-69, SKIA-132; `Skia_RenderTarget2D_MipGeneration` |
| `IGraphicsRenderer::CreateRenderTargetCube/5` | Allocates cube-face FBO target. | Creates bounded six-face raster/mip storage; no depth/MSAA/sampler. | `bounded` | SKIA-85–86; four public contracts |
| `IGraphicsRenderer::CreateEffectRenderer/2` | Compiles arbitrary EasyGL GLSL. | Untagged strings return null; exact `CNA_SKIA_SKSL_V1` constructs the bounded fragment-only SkSL adapter. | `bounded` | SKIA-89–92; `docs/skia-effects.md`; `Skia_SkSL_Effect_Prototype` |
| `IGraphicsRenderer::SetRenderTargetCubeFace/2` | Binds selected cube face. | Binds one checked raster face and resets target-local state. | `implemented` | SKIA-85–86; plural binding contract |
| `IGraphicsRenderer::SetRenderTargets/2` | Binds normalized GL MRT set. | Empty/one 2D/one cube face work; 2–4 targets reject atomically because SkCanvas cannot express distinct slot outputs. | `bounded` | SKIA-68, SKIA-85–88; `Skia_MRT_Rejection` |
| `IGraphicsRenderer::ApplyBlendState/7` | Maps full EasyGL blend/write state. | All 714,025 valid factor/function tuples and target-0 masks draw; invalid raw selectors, target-1/2/3 masks, and non-default sample masks reject atomically. | `bounded` | SKIA-47–57, SKIA-119–124 |
| `IGraphicsRenderer::ApplyDepthStencilState/16` | Applies complete GL depth/stencil state. | Disabled None is valid 2D state; any active depth/write/stencil mode rejects. | `unsupported` | SKIA-97–98, SKIA-102 |
| `IGraphicsRenderer::ApplyRasterizerState/5` | Applies GL cull/fill/scissor/bias. | 2D solid/scissor only; wireframe rejects. | `bounded` | SKIA-41, SKIA-58 |
| `IGraphicsRenderer::ApplySamplerState/5` | Applies per-slot GL filter/address/anisotropy. | Sprite Texture2D slot implements all min/mag/mip combinations and addressing; Anisotropic is a complete-Linear fallback, not an advertised device feature. | `bounded` | SKIA-43–46, SKIA-78–79, SKIA-129 |
| `IGraphicsRenderer::SetBlendFactor/4` | Sets GL constant blend color. | Rebuilds the active generated blender transactionally with the live RGBA constant. | `implemented` | SKIA-120–124 |
| `IGraphicsRenderer::SetReferenceStencil/1` | Updates GL stencil reference. | Zero accompanies disabled 2D state; nonzero rejects. | `unsupported` | SKIA-98, SKIA-102 |
| `IGraphicsRenderer::SetScissorRect/4` | Updates GL scissor. | Updates active top-left Skia clip state. | `implemented` | SKIA-41, SKIA-59 |
| `IGraphicsRenderer::SetViewport/6` | Updates GL viewport and depth range. | 2D rectangle works; depth range has no effect. | `bounded` | SKIA-42, SKIA-97 |
| `IGraphicsRenderer::SupportsDepthStencil/0` | True for EasyGL depth/stencil surfaces. | False for raster backbuffer. | `unsupported` | SKIA-67, SKIA-97 |
| `IGraphicsRenderer::Ensure3DSupported/1` | Default boundary is a no-op on 3D renderers. | Public draw/model calls fail before consuming input or state. | `internal` | SKIA-102; `Skia_3D_Refusal` |
| `IGraphicsRenderer::ClearColorAndDepth/5` | Clears EasyGL color and depth. | Rejects because depth cannot be honored. | `unsupported` | SKIA-67, SKIA-97 |
| `IGraphicsRenderer::ClearDepth/1` | Clears EasyGL depth only. | Rejects because no depth buffer exists. | `unsupported` | SKIA-67, SKIA-97 |
| `IGraphicsRenderer::ClearStencil/1` | Clears EasyGL stencil only. | Rejects because no stencil buffer exists. | `unsupported` | SKIA-67, SKIA-98 |
| `IGraphicsRenderer::ClearDepthAndStencil/2` | Clears both EasyGL attachments. | Rejects because neither exists. | `unsupported` | SKIA-67, SKIA-98 |
| `IGraphicsRenderer::ClearColorAndStencil/5` | Clears EasyGL color/stencil. | Rejects rather than partially clearing color. | `unsupported` | SKIA-67, SKIA-98 |
| `IGraphicsRenderer::ClearColorDepthAndStencil/6` | Clears all EasyGL attachments. | Rejects rather than claiming absent attachments. | `unsupported` | SKIA-67, SKIA-98 |
| `IGraphicsRenderer::SetDepthTestEnabled/1` | Toggles GL depth test. | Explicit 3D refusal. | `unsupported` | SKIA-97 |
| `IGraphicsRenderer::SetBlendEnabled/1` | Toggles GL blend stage. | Toggles 2D source replacement/composition. | `implemented` | SKIA-50 |
| `IGraphicsRenderer::SetDepthWriteEnabled/1` | Toggles GL depth writes. | Explicit 3D refusal. | `unsupported` | SKIA-97 |
| `IGraphicsRenderer::CreateVertexBuffer/1` | Creates an EasyGL VBO/VAO. | Throws actionable no-3D error. | `unsupported` | SKIA-95 |
| `IGraphicsRenderer::CreateIndexBuffer16/1` | Creates 16-bit EasyGL IBO. | Throws actionable no-3D error. | `unsupported` | SKIA-95 |
| `IGraphicsRenderer::CreateIndexBuffer32/1` | Creates 32-bit EasyGL IBO. | Explicitly names and rejects the 32-bit route. | `unsupported` | SKIA-95, SKIA-102 |
| `IGraphicsRenderer::DrawColoredPrimitives/6` | Draws EasyGL colored vertices. | Throws actionable no-3D error. | `unsupported` | SKIA-96 |
| `IGraphicsRenderer::DrawIndexedColoredPrimitives/7` | Draws indexed EasyGL colored vertices. | Throws actionable no-3D error. | `unsupported` | SKIA-96 |
| `IGraphicsRenderer::DrawPrimitivesEx/7` | Draws stock/custom EasyGL 3D parameters. | Explicit stable no-3D primitive refusal. | `unsupported` | SKIA-96, SKIA-99–102 |
| `IGraphicsRenderer::DrawIndexedPrimitivesEx/8` | Draws indexed stock/custom EasyGL 3D. | Explicit stable no-3D indexed refusal. | `unsupported` | SKIA-96, SKIA-99–102 |
| `IGraphicsRenderer::DrawInstancedPrimitivesEx/9` | Draws hardware-instanced EasyGL geometry. | Explicit stable no-3D instanced refusal. | `unsupported` | SKIA-102–103 |
| `IGraphicsRenderer::GetMaxVertexStreams/0` | REMED-GFX-201: how many per-vertex bindings of the same input rate the renderer can express natively. | No vertex pipeline exists, so there is no binding ceiling to report; every draw route refuses through `Ensure3DSupported()` before a binding is read. | `unsupported` | SKIA-95, SKIA-102–103; `Skia_3D_Refusal` |
| `IGraphicsRenderer::SetContextRecoveryEnabled/1` | Controls EasyGL CPU restoration shadows. | Raster resources stay CPU-owned across presenter rebuild. | `internal` | SKIA-16, SKIA-28 |
| `IGraphicsRenderer::SupportsCapability/1` | Reports EasyGL compile/device features. | True only for storage-only Texture3D; GPU/3D entries false. | `implemented` | SKIA-17, SKIA-84, capability rows below |
| `IGraphicsRenderer::GetMaxTextureDimension/0` | Reports GL maximum texture axis. | Reports bounded raster maximum. | `implemented` | SKIA-26 |
| `IGraphicsRenderer::SetStringMarkerEXT/1` | Inserts GL debug marker where available. | Intentional no-op without GPU stream. | `internal` | SKIA-60 |
| `IGraphicsRenderer::DebugSimulateContextLoss/0` | Drives EasyGL recovery seam. | Rebuilds SDL presenter, retains raster resources. | `implemented` | SKIA-16, SKIA-65, SKIA-110; 64-cycle `Skia_ResourceBudget` |
| `IGraphicsRenderer::DebugRestoreContext/0` | Drives EasyGL restore seam. | Rebuilds SDL presenter, retains raster resources. | `implemented` | SKIA-16, SKIA-65 |
| `IGraphicsRenderer::RegisterForWindow/2` | Common SDL window-to-renderer registry. | Same checked registry path. | `internal` | SKIA-12, SKIA-18 |
| `IGraphicsRenderer::UnregisterForWindow/1` | Removes common window registry entry. | Transactional teardown and rollback. | `internal` | SKIA-12 |
| `IGraphicsRenderer::GetForWindow/1` | Resolves common renderer for input/presentation. | Returns only the live registered Skia renderer. | `internal` | SKIA-12, SKIA-18 |

## Capability values

| Entry | EasyGL behavior and test surface | Skia result or plan | Status | Evidence or follow-up |
|---|---|---|---|---|
| `GraphicsCapability::ThreeD` | EasyGL advertises its GL primitive pipeline. | Raster renderer reports false. | `unsupported` | SKIA-95–103 |
| `GraphicsCapability::DepthStencilBuffer` | EasyGL advertises real depth/stencil storage. | Raster renderer reports false. | `unsupported` | SKIA-67, SKIA-97–98 |
| `GraphicsCapability::MultiSampleAntiAliasing` | EasyGL reports probed MSAA support. | Raster renderer reports false and zero samples. | `unsupported` | SKIA-76–77 |
| `GraphicsCapability::MultipleRenderTargets` | EasyGL advertises normalized MRT binding. | Raster renderer reports false; single-colour replay cannot implement distinct MRT slot outputs. | `unsupported` | SKIA-87–88; `Skia_MRT_Rejection` |
| `GraphicsCapability::AnisotropicFiltering` | EasyGL reports device anisotropy. | Raster renderer reports false. | `unsupported` | SKIA-78–79 |
| `GraphicsCapability::WireFrame` | EasyGL supports GL line polygon mode where available. | Raster renderer reports false and rejects wireframe. | `unsupported` | SKIA-58, SKIA-97 |
| `GraphicsCapability::OcclusionQuery` | EasyGL reports query API availability. | Raster renderer reports false; final-pixel differences cannot observe samples passed. | `unsupported` | SKIA-104–105 |
| `GraphicsCapability::CustomEffects` | EasyGL compiles custom GLSL effects. | False: the explicit bounded SkSL SpriteBatch extension is not arbitrary GLSL compatibility. | `unsupported` | SKIA-89–94 |
| `GraphicsCapability::Texture3D` | EasyGL exposes GL volume textures. | True for persistent CPU transfer/readback storage only; no sampling claim. | `bounded` | SKIA-82–84; `Skia_GraphicsCapability` |
| `GraphicsCapability::MultiStreamVertexInput` | REMED-GFX-201: EasyGL reports whether it can re-slot one declaration across several bindings of the same input rate. | Raster renderer reports false; there is no vertex-stream pipeline to split. | `unsupported` | SKIA-95, SKIA-102–103; `Skia_GraphicsCapability` |
| `GraphicsCapability::Instancing` | EasyGL reports driver-granted hardware instancing. | Raster renderer reports false; the instanced draw route refuses rather than drawing one instance. | `unsupported` | SKIA-102–103; `Skia_GraphicsCapability`, `Skia_3D_Refusal` |

## Public GraphicsDevice calls

| Entry | EasyGL behavior and test surface | Skia result or plan | Status | Evidence or follow-up |
|---|---|---|---|---|
| `GraphicsDevice::GraphicsDevice/0` | Common device creates the selected EasyGL renderer/window. | Creates the selected Skia renderer transactionally. | `implemented` | SKIA-10–12 |
| `GraphicsDevice::GraphicsDevice/3` | Creates EasyGL with adapter/profile/parameters. | Same common parameters select raster presentation. | `implemented` | SKIA-10–15 |
| `GraphicsDevice::getIsDisposedProperty/0` | Common lifetime state around EasyGL teardown. | Same resource-first Skia teardown state. | `implemented` | SKIA-12, SKIA-29 |
| `GraphicsDevice::getGraphicsDeviceStatusProperty/0` | Reflects renderer device events. | Remains Normal across synchronous presenter recovery. | `implemented` | SKIA-16, SKIA-110; 64-cycle `Skia_ResourceBudget` |
| `GraphicsDevice::getAdapterProperty/0` | Common adapter metadata used by EasyGL. | Same common adapter metadata. | `implemented` | SKIA-10 |
| `GraphicsDevice::getGraphicsProfileProperty/0` | Common profile selected for EasyGL validation. | Same public profile metadata; capabilities remain truthful. | `implemented` | SKIA-10, SKIA-17 |
| `GraphicsDevice::getPresentationParametersProperty/0#1` | Mutable common presentation state. | Reflects applied Skia logical/present interval values. | `implemented` | SKIA-13–15 |
| `GraphicsDevice::getPresentationParametersProperty/0#2` | Const common presentation state. | Reflects applied Skia logical/present interval values. | `implemented` | SKIA-13–15 |
| `GraphicsDevice::getDisplayModeProperty/0` | Adapter/fullscreen dimensions around EasyGL. | Reports common adapter or Skia logical fullscreen size. | `implemented` | SKIA-8, SKIA-13 |
| `GraphicsDevice::getTexturesProperty/0` | Common pixel-shader texture slots feed EasyGL. | Slots exist; only 2D SpriteBatch sampling is supported. | `bounded` | SKIA-22, SKIA-32, SKIA-96 |
| `GraphicsDevice::getSamplerStatesProperty/0` | Common sampler slots apply to EasyGL. | Point/linear and address modes are bounded to 2D. | `bounded` | SKIA-43–46, SKIA-79 |
| `GraphicsDevice::getVertexTexturesProperty/0` | Common vertex-sampler slots feed EasyGL 3D. | No raster vertex shader pipeline. | `unsupported` | SKIA-95–103 |
| `GraphicsDevice::getVertexSamplerStatesProperty/0` | Common vertex sampler state feeds EasyGL 3D. | No raster vertex shader pipeline. | `unsupported` | SKIA-95–103 |
| `GraphicsDevice::getBlendStateProperty/0#1` | Mutable cached EasyGL blend state. | Returns the cached state for every valid 2D factor/function tuple; sample/MRT write fields retain the documented raster boundary. | `bounded` | SKIA-47–57, SKIA-119–124 |
| `GraphicsDevice::setBlendStateProperty/1` | Applies complete EasyGL blend/write state. | All valid selector tuples, independent RGB/alpha equations, constants, and target-zero write masks work; unsupported sample/MRT fields reject. | `bounded` | SKIA-47–57, SKIA-119–124 |
| `GraphicsDevice::getBlendStateProperty/0#2` | Const cached EasyGL blend state. | Returns the common cached Skia state. | `implemented` | SKIA-47–57 |
| `GraphicsDevice::getDepthStencilStateProperty/0#1` | Mutable cached EasyGL depth/stencil state. | State can be inspected but has no raster attachment. | `unsupported` | SKIA-97–98 |
| `GraphicsDevice::setDepthStencilStateProperty/1` | Applies full EasyGL depth/stencil state. | None remains a 2D no-op; active modes reject without changing cache. | `unsupported` | SKIA-97–98, SKIA-102 |
| `GraphicsDevice::getDepthStencilStateProperty/0#2` | Const cached EasyGL depth/stencil state. | Cache remains common metadata only. | `unsupported` | SKIA-97–98 |
| `GraphicsDevice::getRasterizerStateProperty/0#1` | Mutable cached EasyGL rasterizer state. | Returns cached 2D-bounded state. | `bounded` | SKIA-41, SKIA-58 |
| `GraphicsDevice::setRasterizerStateProperty/1` | Applies EasyGL cull/fill/scissor/bias. | Solid/scissor work; wireframe rejects; 3D fields inert. | `bounded` | SKIA-41, SKIA-58 |
| `GraphicsDevice::getRasterizerStateProperty/0#2` | Const cached EasyGL rasterizer state. | Returns cached 2D-bounded state. | `bounded` | SKIA-41, SKIA-58 |
| `GraphicsDevice::getScissorRectangleProperty/0` | Returns common EasyGL scissor rectangle. | Returns the active top-left 2D rectangle. | `implemented` | SKIA-41, SKIA-59 |
| `GraphicsDevice::setScissorRectangleProperty/1` | Applies EasyGL scissor immediately. | Applies checked Skia raster clip state. | `implemented` | SKIA-41, SKIA-59 |
| `GraphicsDevice::getViewportProperty/0` | Returns common EasyGL viewport/depth range. | Rectangle is exact; depth range is metadata. | `bounded` | SKIA-42, SKIA-97 |
| `GraphicsDevice::setViewportProperty/1` | Applies EasyGL viewport/depth range. | Applies 2D placement/clip; no depth interpretation. | `bounded` | SKIA-42, SKIA-97 |
| `GraphicsDevice::getBlendFactorProperty/0` | Returns EasyGL constant blend color. | Returns the cached constant consumed by generated BlendFactor/InverseBlendFactor routes. | `implemented` | SKIA-120–124 |
| `GraphicsDevice::setBlendFactorProperty/1` | Applies GL constant blend color. | Applies live RGBA updates transactionally to generated blend routes. | `implemented` | SKIA-120–124 |
| `GraphicsDevice::getMultiSampleMaskProperty/0` | Returns EasyGL coverage mask. | Returns common cache on a zero-sample raster. | `unsupported` | SKIA-56, SKIA-76 |
| `GraphicsDevice::setMultiSampleMaskProperty/1` | Stores EasyGL coverage mask for draws. | Non-default draw use rejects; no sample mask is invented. | `unsupported` | SKIA-56, SKIA-76 |
| `GraphicsDevice::getReferenceStencilProperty/0` | Returns EasyGL stencil reference. | Returns common cache without raster stencil. | `unsupported` | SKIA-98 |
| `GraphicsDevice::setReferenceStencilProperty/1` | Applies EasyGL stencil reference. | Nonzero rejects before committing the public cache. | `unsupported` | SKIA-98, SKIA-102 |
| `GraphicsDevice::getIndicesProperty/0` | Returns current EasyGL index buffer. | No Skia index buffer can be created. | `unsupported` | SKIA-95 |
| `GraphicsDevice::setIndicesProperty/1` | Binds EasyGL index buffer. | Common binding cannot produce a Skia 3D draw. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::Clear/1` | Clears EasyGL target to `Color`. | Clears active raster surface exactly. | `implemented` | SKIA-13, SKIA-61 |
| `GraphicsDevice::Clear/4#1` | Float RGBA overload clears EasyGL color. | Float RGBA clears active raster surface. | `implemented` | SKIA-13 |
| `GraphicsDevice::Clear/4#2` | Options/color/depth/stencil clears real EasyGL attachments. | Masks absent depth/stencil and clears color if requested. | `bounded` | SKIA-67 |
| `GraphicsDevice::Clear/2` | Color/depth overload clears EasyGL attachments. | Clears color; absent raster depth is masked. | `bounded` | SKIA-67 |
| `GraphicsDevice::Present/0` | Presents EasyGL unless a target is bound. | Presents SDL-uploaded raster under same guard. | `implemented` | SKIA-7, SKIA-13, SKIA-61 |
| `GraphicsDevice::Reset/0` | Reapplies current EasyGL presentation state. | Reapplies Skia size/mode/interval transactionally. | `implemented` | SKIA-8, SKIA-13–16 |
| `GraphicsDevice::Reset/1` | Applies new EasyGL presentation parameters. | Applies bounded raster parameters; MSAA remains zero. | `bounded` | SKIA-8, SKIA-13–17 |
| `GraphicsDevice::Reset/2#1` | Reference-adapter reset forwards to EasyGL. | Same common path with Skia rollback guarantees. | `bounded` | SKIA-8, SKIA-12–16 |
| `GraphicsDevice::Reset/2#2` | Optional-adapter reset is the EasyGL implementation route. | Same common path with Skia rollback guarantees. | `bounded` | SKIA-8, SKIA-12–16 |
| `GraphicsDevice::Dispose/0` | Disposes resources before EasyGL renderer/window. | Same ordering with checked weak Skia resources. | `implemented` | SKIA-12, SKIA-18, SKIA-29 |
| `GraphicsDevice::GetBackBufferData/2` | Reads complete EasyGL backbuffer to colors. | Reads exact active raster surface. | `implemented` | SKIA-23, SKIA-62 |
| `GraphicsDevice::GetBackBufferData/3` | Reads complete EasyGL backbuffer with destination offset. | Same validated common conversion from RGBA8. | `implemented` | SKIA-23, SKIA-62 |
| `GraphicsDevice::GetBackBufferData/4` | Reads EasyGL rectangle with destination range. | Same top-left rectangular raster readback. | `implemented` | SKIA-23, SKIA-62 |
| `GraphicsDevice::SetRenderTarget/1` | Binds one EasyGL RenderTarget2D or backbuffer. | Binds checked raster target or backbuffer. | `implemented` | SKIA-61, SKIA-69 |
| `GraphicsDevice::SetRenderTarget/2` | Binds EasyGL cube target face. | Binds an exact six-surface raster cube face. | `implemented` | SKIA-85–86; shared readback contract |
| `GraphicsDevice::SetRenderTargets/1` | Validates and binds EasyGL MRT/cube sets. | Empty/one 2D/one cube work; MRT rejects before state, clear, or draw side effects. | `bounded` | SKIA-68, SKIA-85–88; `Skia_MRT_Rejection` |
| `GraphicsDevice::GetRenderTargets/0` | Returns common active EasyGL binding vector. | Returns empty/one 2D/cube-face binding accurately. | `bounded` | SKIA-61, SKIA-68, SKIA-85–86 |
| `GraphicsDevice::SetVertexBuffer/1` | Binds EasyGL vertex buffer slot zero. | No raster vertex buffer exists. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::SetVertexBuffer/2` | Binds EasyGL vertex buffer with offset. | No raster vertex buffer exists. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::SetVertexBuffers/1` | Binds multiple EasyGL vertex streams/instances. | Common cache exists; Skia draws reject. | `unsupported` | SKIA-95, SKIA-103 |
| `GraphicsDevice::GetVertexBuffers/0` | Returns common EasyGL vertex bindings. | No usable raster vertex bindings. | `unsupported` | SKIA-95 |
| `GraphicsDevice::SetIndexBuffer/1` | Binds EasyGL index buffer. | No raster index buffer exists. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::GetVertexBuffer/0` | Returns EasyGL slot-zero binding. | No usable raster vertex buffer. | `unsupported` | SKIA-95 |
| `GraphicsDevice::GetIndexBuffer/0` | Returns EasyGL index binding. | No usable raster index buffer. | `unsupported` | SKIA-95 |
| `GraphicsDevice::DrawPrimitives/3` | Draws bound EasyGL non-indexed geometry. | Rejects at the public Skia guard before binding validation. | `unsupported` | SKIA-96, SKIA-102 |
| `GraphicsDevice::DrawIndexedPrimitives/6` | Draws bound EasyGL indexed geometry. | Rejects at the public Skia guard before binding validation. | `unsupported` | SKIA-96, SKIA-102 |
| `GraphicsDevice::DrawInstancedPrimitives/7` | Draws EasyGL instanced indexed geometry. | Rejects at the public Skia guard before binding validation. | `unsupported` | SKIA-102–103 |
| `GraphicsDevice::DrawUserPrimitives/4#1` | Draws raw default-layout EasyGL vertices. | Rejects before inspecting or packing caller input. | `unsupported` | SKIA-95–96, SKIA-102 |
| `GraphicsDevice::DrawUserPrimitives/5#1` | Draws raw explicitly declared EasyGL vertices. | No raster declared-vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/5#2` | Draws declared `VertexPositionColor` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/5#3` | Draws declared `VertexPositionTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/5#4` | Draws declared `VertexPositionColorTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/5#5` | Draws declared `VertexPositionNormalTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/4#2` | Draws `VertexPositionColor` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/4#3` | Draws `VertexPositionColorTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/4#4` | Draws `VertexPositionTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserPrimitives/4#5` | Draws `VertexPositionNormalTexture` through EasyGL. | No raster 3D vertex pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#1` | Draws raw default-layout EasyGL indexed data. | Rejects before inspecting or packing caller input. | `unsupported` | SKIA-95–96, SKIA-102 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#2` | Draws 16-bit indexed `VertexPositionColor`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#3` | Draws 16-bit indexed `VertexPositionColorTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#4` | Draws 16-bit indexed `VertexPositionTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#5` | Draws 16-bit indexed `VertexPositionNormalTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#6` | Draws 32-bit indexed `VertexPositionColor`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#7` | Draws 32-bit indexed `VertexPositionColorTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#8` | Draws 32-bit indexed `VertexPositionTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/7#9` | Draws 32-bit indexed `VertexPositionNormalTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#1` | Draws raw declared EasyGL data with 16-bit indices. | No raster declared-index pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#2` | Draws declared 16-bit `VertexPositionColor`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#3` | Draws declared 16-bit `VertexPositionTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#4` | Draws declared 16-bit `VertexPositionColorTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#5` | Draws declared 16-bit `VertexPositionNormalTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#6` | Draws raw declared EasyGL data with 32-bit indices. | No raster declared-index pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#7` | Draws declared 32-bit `VertexPositionColor`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#8` | Draws declared 32-bit `VertexPositionTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#9` | Draws declared 32-bit `VertexPositionColorTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::DrawUserIndexedPrimitives/8#10` | Draws declared 32-bit `VertexPositionNormalTexture`. | No raster 3D indexed pipeline. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::PrimitiveVerts/2` | Common topology helper used by EasyGL validation. | Renderer-neutral helper remains exact. | `implemented` | Shared primitive tests |
| `GraphicsDevice::OnResourceCreated/1` | Raises common event for EasyGL resources. | Raises the same event for Skia resources. | `internal` | SKIA-22, SKIA-61 |
| `GraphicsDevice::OnResourceDestroyed/2` | Raises common EasyGL destruction event. | Raises the same event for Skia resources. | `internal` | SKIA-29, SKIA-69 |
| `GraphicsDevice::AddResourceReference/1` | Tracks EasyGL resources before renderer teardown. | Tracks Skia resources under the same owner. | `internal` | SKIA-12, SKIA-29 |
| `GraphicsDevice::RemoveResourceReference/1` | Untracks disposed EasyGL resources. | Untracks disposed Skia resources safely. | `internal` | SKIA-29, SKIA-69 |
| `GraphicsDevice::GetTrackedResourceCount/0` | Debug count for common resource lifetime. | Same count covers Skia wrappers. | `internal` | SKIA-29, SKIA-74 |
| `GraphicsDevice::SetDepthTestEnabled/1` | Toggles EasyGL depth testing. | Throws actionable no-3D error. | `unsupported` | SKIA-97 |
| `GraphicsDevice::SetBlendEnabled/1` | Toggles EasyGL blending. | Implements raster replacement/composition toggle. | `implemented` | SKIA-50 |
| `GraphicsDevice::SetDepthWriteEnabled/1` | Toggles EasyGL depth writes. | Throws actionable no-3D error. | `unsupported` | SKIA-97 |
| `GraphicsDevice::SetGraphicsProfileEXT/1` | Common profile handoff before EasyGL reset. | Same common profile metadata handoff. | `internal` | SKIA-10, SKIA-17 |
| `GraphicsDevice::SetContextRecoveryEnabled/1` | Controls EasyGL restoration shadows. | Forwards no-op policy for CPU-owned raster resources. | `internal` | SKIA-16, SKIA-28 |
| `GraphicsDevice::SetStringMarkerEXT/1` | Forwards optional EasyGL GPU marker. | Safe no-op without raster GPU command stream. | `internal` | SKIA-60 |
| `GraphicsDevice::GetRenderer/0` | Exposes selected EasyGL renderer internally. | Exposes selected Skia renderer internally. | `internal` | SKIA-10–11 |
| `GraphicsDevice::GetGraphicsRendererType/0` | Reports compile-time EasyGL type. | Reports `GraphicsRendererType::Skia`. | `implemented` | SKIA-11 |
| `GraphicsDevice::GetGraphicsRendererName/0` | Reports compile-time `EASYGL`. | Reports compile-time `SKIA`. | `implemented` | SKIA-11 |
| `GraphicsDevice::SupportsCapability/1` | Forwards EasyGL capability/device query. | Forwards true only for storage-only Texture3D. | `implemented` | SKIA-17, SKIA-84, capability rows above |
| `GraphicsDevice::GetMaxTextureDimension/0` | Forwards EasyGL hardware limit. | Returns enforced raster axis limit. | `implemented` | SKIA-26 |
| `GraphicsDevice::SetCurrentEffect/1` | Stores stock/custom effect for EasyGL 3D draws. | Common pointer can be stored; raster 3D draws reject. | `unsupported` | SKIA-89–103 |
| `GraphicsDevice::Indices/0` | Alias returning EasyGL index binding. | No usable raster index buffer. | `unsupported` | SKIA-95 |
| `GraphicsDevice::Indices/1` | Alias binding EasyGL index buffer. | No usable raster index buffer. | `unsupported` | SKIA-95–96 |
| `GraphicsDevice::GetTypeName/0` | Common runtime type identity. | Renderer-independent type identity. | `internal` | Shared object tests |
| `GraphicsDevice::SetPresentationParameters/1` | Stores/forwards EasyGL interval parameters. | Stores/forwards Skia raster interval parameters. | `implemented` | SKIA-13–15 |
| `GraphicsDevice::RecreateRendererForMultiSampleCount/1` | Test seam recreates EasyGL with requested samples. | Recreates raster renderer but remains zero-sample. | `unsupported` | SKIA-76–77 |
