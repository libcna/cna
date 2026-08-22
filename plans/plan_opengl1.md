# CNA OPENGL1 Backend Plan

## Scope
`OPENGL1` is a genuinely independent, native legacy desktop OpenGL backend. It MUST NOT depend on EasyGL and MUST NOT route rendering through SDL_Renderer, SDL_GPU, bgfx, or the modern EasyGL backend. SDL is used only for the window and OpenGL context.

Target platforms: Linux and Windows desktop compatibility-profile drivers. The backend intentionally targets the OpenGL 1.x fixed-function programming model and requests an OpenGL 1.1 context. Modern drivers may expose a newer compatibility context while preserving these APIs.

## Implemented foundation
- Independent `CNA_GRAPHICS_BACKEND=OPENGL1` selection and `CNA_BACKEND_OPENGL1` compile definition.
- SDL `SDL_WINDOW_OPENGL` window integration and direct `SDL_GL_CreateContext`/swap path.
- Direct legacy OpenGL API usage; zero EasyGL dependency.
- RGBA8 `Texture2D` creation/update/binding, with automatic mipmap generation
  (`glGenerateMipmap`/`GL_GENERATE_MIPMAP`/CPU box-filter fallback -- see item 6).
- `ARB_texture_cube_map` `TextureCube` creation/update, and `EnvironmentMapEffect`'s fixed-function
  reflection-mapping subset (see item 5).
- Fixed-function `SpriteBatch` using textured quads, tint, source/destination rectangles, rotation, origin and sprite flipping.
- CPU-backed vertex/index buffers with 16-bit and 32-bit indices (no GL buffer objects at all --
  `DrawInternal`/`SpriteBatch::Draw` read straight from CPU-side data every draw call via
  immediate-mode `glVertex3f`/`glTexCoord2f`/`glColor4f`, which also means these two resource
  types are inherently immune to context loss, nothing to recreate).
- Non-indexed/indexed 3D primitive rendering for triangle list/strip, line list/strip and points.
- Known CNA vertex layouts: position+color, position+texture, position+color+texture, position+normal+texture.
- World/View/Projection through the legacy projection/model-view matrix stacks.
- Depth testing/writes, stencil, culling, wireframe, scissor, viewport and polygon offset.
- Fixed-function texture mapping (including the full 9-value XNA `TextureFilter` -> GL min/mag
  mapping, item 6), vertex colors correctly combined with `BasicEffect`'s material
  `DiffuseColor`/`EmissiveColor` (item 4), one-light directional lighting with real `GL_EMISSION`
  material support, linear fog and coarse alpha testing through `GpuDrawParams`.
- Context-loss resource recreation registry (`OpenGL1ResourceRegistry`/`IOpenGL1Recoverable`,
  independent of EasyGL's own), wired up for `Texture2D`, `TextureCube` and `RenderTarget2D`
  (see item 8).

## Intentional OpenGL 1.x limitations
- No GLSL/custom `ShaderEffect` pipeline in the strict OPENGL1 backend.
- No PBR shaders, programmable per-pixel lighting, GPU skinning, modern environment-map shaders (Fresnel
  edge-weighting/`EnvironmentMapSpecular` -- item 5), or arbitrary custom vertex declarations.
- No MRT (`SetRenderTargets()` falls back to the shared `IGraphicsBackend` single-target default).
- No instancing.
- RenderTarget2D is implemented via `ARB_framebuffer_object`/core (>=3.0), detected at runtime (`OpenGL1Capabilities::framebufferObject`); `CreateRenderTarget2D()` returns nullptr on a driver without it. `EXT_framebuffer_object` (the older, narrower extension) is not supported. `RenderTarget2D` MSAA is supported (item 25, via its own explicit `glRenderbufferStorageMultisample`/`glBlitFramebuffer` resolve -- a separate mechanism from item 22's window-visual-based backbuffer MSAA, which does not extend to FBO-based render targets).
- `DualTextureEffect` is implemented via `ARB_multitexture`/core (>=1.3), detected at runtime (`OpenGL1Capabilities::multitexture`) with the entry points (`glActiveTexture`/`glMultiTexCoord2f`) loaded through `SDL_GL_GetProcAddress`; a strict 1.1 driver silently falls back to texture unit 0 only (`texture1` ignored, matching every other textured draw).
- Anisotropic filtering requires `GL_EXT_texture_filter_anisotropic`, detected at runtime (`OpenGL1Capabilities::anisotropicFiltering`, `GraphicsCapability::AnisotropicFiltering`); silently falls back to no anisotropy (clamped to 1.0x) when the driver lacks it.
- `GraphicsProfile.Reach`/`.HiDef` numeric ceilings (texture/cube/volume size, format whitelist,
  MRT count) are not enforced -- matches every non-D3D9 CNA backend, per `plan_dx9.md`'s own
  "Divergence 3" decision (item 12).

## Next implementation phases
1. ~~Add runtime GL version/extension discovery~~ **Done**: `OpenGL1Capabilities`/`DetectOpenGL1Capabilities()` (`OpenGL1Capabilities.hpp`/`.cpp`) query `GL_VERSION`/`GL_EXTENSIONS` directly (no loader library, no EasyGL dependency) once per context, right after `SDL_GL_MakeCurrent`, and expose version + `framebufferObject`/`multitexture`/`textureCubeMap`/`generateMipmap`/`anisotropicFiltering` flags for phases 2/3/5/6 below to consult. First real consumer wired end-to-end: `GraphicsCapability::AnisotropicFiltering` now reports the real detected value instead of a hardcoded `false`, and `ApplySamplerState()` actually sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` when `TextureFilter::Anisotropic` is requested and the extension is present (verified against the real driver, including over-cap clamping and reset-to-1.0 on a non-anisotropic filter, by `OpenGL1_Anisotropic_GlState`).
2. ~~Add `ARB_framebuffer_object` RenderTarget2D support~~ **Done**: `OpenGL1RenderTargetBackend` (`OpenGL1RenderTargetBackend.hpp`/`.cpp`) creates a color texture + optional depth/stencil renderbuffer + FBO via ARB/core-3.0 entry points loaded through `SDL_GL_GetProcAddress` (`TryLoadOpenGL1FramebufferObjectFunctions()`); `CreateRenderTarget2D()` returns nullptr when `OpenGL1Capabilities::framebufferObject` is false or the FBO ends up incomplete (strict capability fallback, no partial/broken render target). Found and fixed three real bugs while getting this to round-trip correctly (render into a target, sample it back as a texture) -- see the "Bugs found" section below.
3. ~~Add `ARB_multitexture`/OpenGL 1.3 dual-texture fixed-function path~~ **Done**: `DrawInternal` reproduces FNA's real `DualTextureEffect.fx` formula (`(texture0.rgb*2)*texture1.rgb*diffuseColor.rgb`, alpha `texture0.a*texture1.a*diffuseColor.a`, both units sampling the same per-vertex UV -- DualTextureEffect has no second texcoord channel either) via `GL_COMBINE` texture-environment chaining: unit 0 modulates `texture0` by `GL_PRIMARY_COLOR` with `GL_RGB_SCALE=2`, unit 1 modulates `GL_PREVIOUS` by `texture1`. `SpriteBatch::Begin()` defensively resets unit 1 + unit 0's `GL_TEXTURE_ENV_MODE`, since a prior dual-textured 3D draw's state would otherwise bleed into 2D sprite rendering (`glPushAttrib`/`glPopAttrib` save/restore whatever state existed at `Begin()`/`End()`, they don't reset it to a clean baseline). Verified against the real driver by `OpenGL1_DualTextureEffect_Doubling` (reused from EasyGL's own Task 383 test), including the `*2` doubling factor specifically (a naive `texture0*texture1` multiply without it is a distinct, discriminable wrong answer).
4. ~~Add fixed-function texture environment/combine mappings for the XNA effects that can be represented honestly~~ **Done**: `DrawInternal`'s stride==16 (`VertexPositionColor`) and stride==24 (`VertexPositionColorTexture`) vertex paths used the per-vertex color alone via `glColor4ub`, silently ignoring `GpuDrawParams::diffuseColor` -- any `BasicEffect.VertexColorEnabled` draw with a non-default `DiffuseColor`/`EmissiveColor` (XNA's real formula multiplies vertex color BY the material's `(DiffuseColor+EmissiveColor)*Alpha`, per `BasicEffect::FillGpuDrawParams`) silently dropped the material tint. Fixed with a new `emitVertexColor` lambda that multiplies the unpacked vertex color by `params->diffuseColor` component-wise before calling `glColor4f`, matching the stride==20 fix from the initial bug pass. Verified against the real driver by `OpenGL1_BasicEffect_Combined` (reused verbatim from EasyGL's own Task 370 capstone test), which asserts the exact multiplicative formula `TextureColor x VertexColor x (DiffuseColor+EmissiveColor)` across 4 independently-colored texels -- pixel-exact match within tolerance.
5. ~~Add cube maps where `ARB_texture_cube_map` exists and implement the subset of EnvironmentMapEffect representable by texture coordinate generation/combine~~ **Done**: `OpenGL1TextureCubeBackend` (raw `GL_TEXTURE_CUBE_MAP`/`GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z`, no extra function loading needed -- those are core-1.3 enum values, unlike FBO/multitexture's entry points) backs `CreateTextureCube()`, gated on `OpenGL1Capabilities::textureCubeMap` (strict capability fallback: returns nullptr when absent). `DrawInternal` blends it into a `VertexPositionNormalTexture` (stride 32) draw via texture unit 1's `GL_REFLECTION_MAP` texture-coordinate generation (`GL_TEXTURE_GEN_S/T/R`), `GL_INTERPOLATE`d against unit 0's lit/textured result through `GL_COMBINE` with `GL_CONSTANT.a=EnvironmentMapAmount` -- exactly FNA's real `mix(baseColor,envColor,Amount)` blend (see `docs/environmentmapeffect-support.md`'s own Task 394 for that formula). Unit 1's texgen/cube-map-enable state is explicitly reset on every draw that is *not* itself env-mapped (`resetUnit1EnvGen()`), since per-unit GL enables persist across `DrawInternal` calls with no push/pop bracketing them, unlike `SpriteBatch::Begin()`/`End()`. Deliberately NOT attempted: Fresnel edge-weighting and `EnvironmentMapSpecular`'s alpha-scaled specular term, both inherently per-pixel/view-angle-dependent and not expressible with fixed-function texture combiners -- an honest, documented limitation, not a silent wrong answer. Found and fixed one more real bug while wiring this up: the lit (stride 32) path never set `GL_EMISSION`, silently dropping `EnvironmentMapEffect`'s (and `BasicEffect`'s) `EmissiveColor`/ambient-light contribution entirely -- see the "Bugs found" section below. Verified against the real driver by `OpenGL1_EnvironmentMapEffect`, pixel-exact on the first attempt (`Amount=0`/`1`/`0.5` against a solid-color cube map, isolating the blend formula from reflection-vector face selection).
6. ~~Add mipmap generation via available extensions or CPU fallback~~ **Done**: `OpenGL1TextureBackend` regenerates every mip level whenever level 0 is (re)uploaded and `ImageData::mipLevels>1` was requested, in priority order: an explicit `glGenerateMipmap()` call (loaded via `SDL_GL_GetProcAddress`, part of the same `ARB_framebuffer_object`/core-3.0 family as phase 2's FBO support, works on core-profile drivers too) > the older `GL_GENERATE_MIPMAP`/`SGIS_generate_mipmap` texture parameter (set before the level-0 upload, driver regenerates automatically as a side effect -- `OpenGL1Capabilities::generateMipmap`, phase 1) > a CPU 2x2 box-filter fallback (`GenerateMipsCPU`) for a driver with neither mechanism (expected unreachable on any real hardware from this decade, but implemented per the plan text rather than just assuming the extension exists). `OpenGL1TextureBackend::HasMips()` tracks whether generation actually succeeded. Also extended `ApplySamplerState`'s `TextureFilter`->GL min/mag mapping from a flat 2-way (Point vs. everything-else-is-Linear) split to the full, correct 9-value mapping (matching `EasyGLGraphicsBackend`'s own already-established mapping) -- without this, generated mip levels would never actually be sampled (`GL_TEXTURE_MIN_FILTER` never requested a `_MIPMAP_` variant before this phase), making mip generation alone untestable/pointless dead infrastructure. Verified against the real driver by `OpenGL1_MipmapGeneration`: a single-pixel checkerboard texture (whose 2x2 box average is EXACTLY uniform gray at every level beyond level 0, regardless of driver-specific averaging/LOD-rounding details) reads as sharply aliased near-white at native size but reads as exact `(128,128,128)` mid-gray once minified 4x with `TextureFilter.LinearMipPoint` -- proof the mip chain was both generated and actually sampled, not just present-but-unused. **Adjacent finding, same day, fixed as a follow-up**: implementing mip-aware filtering surfaced 3 real, pre-existing SamplerState bugs by tracing the exact call order -- `ApplySamplerState` fired once per draw for every sampler slot BEFORE `DrawInternal`/`SpriteBatch::Draw` actually bound the corresponding texture (`GraphicsDevice::applySamplerStatesToBackend()` fires before `DrawPrimitivesEx()`), so its `glTexParameteri` calls landed on whatever texture happened to be bound from the *previous* draw call rather than the one about to be sampled; `ApplySamplerState`'s own `slot` parameter was ignored entirely (every slot's filter/address-mode wrote to whichever texture unit was currently active, not the requested slot, so a `DualTextureEffect` draw's second texture never received its own `SamplerState` at all); and `SpriteBatch`'s own `SetSamplerAddressMode()` was stored but never actually applied. **All 3 fixed same day**: `ApplySamplerState` now only caches per-slot parameters (`OpenGL1GraphicsBackend::samplerSlot_[2]`); a new `ApplySamplerFilterAndWrap(slot,hasMips)` applies them at the ACTUAL bind site inside `DrawInternal`, right after each `BindGL()` call, on the correct active unit, using that specific texture's own `HasMips()`; `SpriteBatch::Draw()` (which already bound-then-applied correctly, just never for wrap mode) now also sets `GL_TEXTURE_WRAP_S/T` from its own `u_`/`v_` and uses the same mip-aware min/mag mapping. Verified via mutation testing (temporarily reverted to the old buggy `ApplySamplerState`, confirmed the new test fails with the exact predicted wrong values, then restored and reconfirmed the exact predicted correct values) by `OpenGL1_SamplerState_BindOrder`: a first-ever texture draw requesting the non-default `AddressMode.Clamp` (chosen specifically because the default `AddressMode.Wrap` coincidentally masks the ordering bug -- an early, wrong version of this test using `Wrap` passed even against the deliberately-reintroduced bug, which is what caught that the first test design was flawed) reads the correct clamped texel; a `DualTextureEffect` draw with deliberately DIFFERENT `AddressMode`s on slot 0 vs slot 1 produces the precomputed brightness value only the correct per-slot pairing predicts (buggy pairing predicts ~1, correct predicts ~78 -- unambiguously distinct).
7. ~~Add readback~~ **Done**: `ReadBackbuffer` implemented (`glReadPixels` off `GL_BACK`). Render-target readback is still N/A (blocked on item 2, RenderTarget2D).
8. ~~Add context-loss resource recreation registry, separate from EasyGL~~ **Done**: `OpenGL1ContextRecovery.hpp`/`.cpp` define `IOpenGL1Recoverable`/`OpenGL1ResourceRegistry` -- a from-scratch implementation of the same concept EasyGL's own `::easygl::RecoverableResource`/`ResourceRegistry` establish, but with zero dependency on that library or its `metagl` context-event plumbing. `OpenGL1GraphicsBackend::DebugSimulateContextLoss()`/`DebugRestoreContext()` perform one atomic destroy+recreate cycle (matching every other desktop backend's own semantics -- there is no genuine asynchronous lost/restored pair on desktop the way WebGL has): notify-lost, destroy+recreate the SDL GL context, re-detect capabilities/reload extension entry points exactly as the constructor does, re-establish base GL state, notify-restored. `SetContextRecoveryEnabled(false)` stops future `Create*` calls from registering with the registry, matching the documented `IGraphicsBackend` contract. Two resource types are wired up: `OpenGL1TextureBackend` (via the pre-existing `ITextureBackend::ShareCpuPixels()` hook -- re-uploads from the SAME CPU pixel buffer `Texture2D` itself keeps, no duplicate copy) and `OpenGL1RenderTargetBackend` (rebuilds an empty FBO/color-texture/depth-renderbuffer of the same size/format -- content is GPU-produced, not restorable, matching real XNA/FNA `RenderTarget2D` semantics after a device reset without `RenderTargetUsage.PreserveContents`). `OpenGL1VertexBufferBackend`/`OpenGL1IndexBufferBackend`/`OpenGL1SpriteBatchBackend` need no recovery machinery at all -- they hold zero live GL resources (`DrawInternal`'s immediate-mode `glVertex3f`/`glTexCoord2f`/`glColor4f` calls read straight from CPU-side buffers every draw, there is no VBO/VAO). **Follow-up, closed 2026-07-20 with explicit user go-ahead**: `OpenGL1TextureCubeBackend` is now also wired into the registry. The blocker was real (`ITextureCubeBackend` had no `ShareCpuPixels()`-equivalent hook), so closing it required the cross-cutting `IGraphicsBackend.hpp` interface change this phase originally deferred: `ITextureCubeBackend::ShareCpuPixels(int face, std::shared_ptr<std::vector<uint8_t>>)` was added with a default no-op body, so every other backend (EasyGL, Vulkan, Bgfx, D3D9/11/12, WebGPU, SdlGpu, SDL_Renderer, Canvas, DX3, Headless, Software) stays source-compatible unchanged -- confirmed by rebuilding both `CNA_GRAPHICS_BACKEND=OPENGL1` and `=EASYGL` clean, and `CnaTests`' full 46-case `TextureCubeTest` suite passing unchanged on both. `TextureCube` itself gained a level-0-only per-face CPU shadow (`cpuPixels_[6]`, lazily created on first `SetData()` per face, mutated in place for subsequent writes to the same face -- same scope and shared_ptr-aliasing convention `Texture2D::cpuPixels_` already established) that it shares with the backend via the new hook. `OpenGL1TextureCubeBackend::Build()`/`RecreateGLResource()` mirror `OpenGL1RenderTargetBackend`'s established constructor/recreate-share-one-method pattern; `RegenerateMips()` reuses the same 3-tier `glGenerateMipmap`/`GL_GENERATE_MIPMAP`/CPU-box-filter priority `OpenGL1TextureBackend` already established (item 6), with `glGenerateMipmap(GL_TEXTURE_CUBE_MAP)` regenerating all 6 faces in one call unlike the per-face CPU fallback. A face that was never `SetData()`-ed keeps a null `cpuPixels_[face]` and rebuilds as an empty pre-allocated level, same as a brand-new cube map -- confirmed not to crash. Verified against the real driver by `OpenGL1_ContextLoss`'s extended TextureCube section, mutation-tested (reverted `ShareCpuPixels` to a no-op, confirmed the face-content check fails; restored, reconfirmed it passes with the exact original color). **Real bug found and fixed later the same night, by an independent adversarial review of this phase's own diff**: `DebugSimulateContextLoss()` never re-bound `currentRt_`'s rebuilt FBO after `registry_.NotifyContextRestored()` -- a brand-new GL context always defaults to the backbuffer (FBO 0) regardless of what was bound before the loss, so if a `RenderTarget2D` was the ACTIVE render target at the moment of loss, every draw immediately after recovery silently landed on the backbuffer instead (while `SetViewport`/`SetScissorRect` kept computing against the RT's own dimensions, since `currentRt_` was still non-null -- a viewport/scissor mismatch on top of the wrong target). `OpenGL1_ContextLoss`'s original RT check couldn't have caught this: it called `SetRenderTarget(nullptr)` *before* triggering the loss (missing the at-risk window entirely) and only checked "no C++ exception was thrown" rather than reading back real pixel content -- `glBindFramebuffer`/`glClear`/`glBindTexture` don't throw for "silently bound to the wrong target". Fixed with `if(currentRt_)currentRt_->BindAsRenderTarget();` right after `NotifyContextRestored()`. The test was rewritten to keep the RT bound *through* the simulated loss and to read the RT's own FBO directly via `RenderTarget2D::GetData()` (bypassing the backbuffer entirely, avoiding a real intermediate bug in the test itself: sampling the RT indirectly through `SpriteBatch` onto the backbuffer couldn't distinguish "the RT is genuinely blue" from "the backbuffer happened to already be blue" -- an early rewrite attempt using that approach passed even against the deliberately-reintroduced bug). Both the fix and the test were confirmed via mutation testing: revert to the buggy behavior, rebuild, confirm the test fails; restore, rebuild, reconfirm it passes.
9. ~~Add Linux X11/Xvfb smoke tests~~ **Done** (Linux half): `tests/opengl1/README.md`'s 8 priority scenarios are wired to real CTest registrations (`cmake/Tests/OpenGL1Tests.cmake`), 8/8 passing under Xvfb/X11+Mesa llvmpipe. Windows GitHub Actions compile/smoke jobs still open.
10. ~~Add visual golden-image tests shared with Software/EasyGL for the supported fixed-function subset~~ **Done**: reuses the EXACT same checked-in reference PNGs EasyGL's own golden-image suite (`examples/golden/*.png`) uses -- no new images -- via the shared, backend-agnostic `PixelTestGame::CompareGoldenImage()` helper (`examples/common/PixelTestGame.hpp`, real public `Game`/`GraphicsDevice`/`Texture2D` API only), same reuse precedent Vulkan's own golden-image tests already established (`VulkanTests.cmake`). Software's own golden-image suite does not exist yet (only EasyGL/Vulkan do); "shared with Software/EasyGL" is honored in spirit -- OPENGL1 reuses the same PNGs any future Software golden suite would too. 9 of EasyGL's golden scenes reused verbatim, all pixel-matching the checked-in reference on the first attempt despite OPENGL1 being a completely different (legacy fixed-function/immediate-mode) rasterizer from EasyGL's shader-based one: the smoke canary (flat clear), `BasicEffect` (textured+vertex-color+diffuse/emissive), `SpriteBatch` rotation, texture-filter linear blending, `BlendState.Additive`, `DualTextureEffect`, `RasterizerState.CullMode`, `DepthStencilState` write-enable, and `AlphaTestEffect` (`CompareFunction::Greater` with alpha clearly above the reference -- within the "coarse approximation" subset `glAlphaFunc(GL_GEQUAL,...)` can honestly reproduce). Every reused scene was deliberately chosen for a flat, edge-free 8x8 sample region (constant UV, no antialiased boundary in the checked region) -- this is what makes cross-rasterizer reuse safe without a wider tolerance. Deliberately excluded: `PbrEffect`/`SkinnedEffect`/`SkinnedPbrEffect` (GLSL-shader-only, no fixed-function equivalent -- this backend's own design rule) and `EnvironmentMapEffect`'s golden scene (exercises Fresnel/specular, phase 5's own documented unimplemented limitation -- a guaranteed mismatch, not a useful test).
11. ~~Add explicit `GraphicsCapability` reporting so unsupported shader-era features return false instead of over-reporting support~~ **Done** (audit, no source change needed): `GraphicsDevice::SupportsCapability()`'s existing OPENGL1 truth table was already correct after phase 1's anisotropic fix -- `ThreeD`/`DepthStencilBuffer`/`WireFrame` true, `AnisotropicFiltering` tracks the real runtime-detected extension, and `MultiSampleAntiAliasing`/`MultipleRenderTargets`/`OcclusionQuery`/`CustomEffects` correctly report false (none of these are implemented by this backend). Locked in by `OpenGL1_GraphicsCapability`, which cross-checks every value against independent evidence rather than trusting the flag alone: `AnisotropicFiltering` against a direct `glGetString(GL_EXTENSIONS)` scan, `OcclusionQuery=false` against a real `OcclusionQuery` object that never completes, `WireFrame=true` against an actual `glGetIntegerv(GL_POLYGON_MODE)` readback.
12. ~~Audit XNA Reach-profile behavior against what the fixed-function pipeline can reproduce exactly~~ **Done**: confirmed `GraphicsAdapter::IsProfileSupported()` correctly returns `true` unconditionally for OPENGL1 too, matching `plan_dx9.md`'s own documented project-wide "Divergence 3" decision (`docs/d3d9-backend.md`: D3D9 is the *only* backend with a real `D3DCAPS9` to consult for genuine per-profile enforcement -- "on the other backends it is genuinely unfixable-in-principle... this plan forbids faking it there"). OPENGL1 correctly does not attempt to fake `Reach`/`HiDef` numeric-ceiling enforcement (texture size, cube size, format whitelist), same as every other non-D3D9 backend. Functional characterization of what OPENGL1's fixed-function pipeline can/cannot reproduce from real XNA `GraphicsProfile.Reach`'s guaranteed feature set: `BasicEffect`/`AlphaTestEffect`/`DualTextureEffect` -- full coverage; `EnvironmentMapEffect` -- reflection-only subset (phase 5, no Fresnel/specular); `SkinnedEffect` and any custom (non-stock) `Effect` -- entirely unsupported (`CreateEffectBackend()` inherited `nullptr` default, matching this backend's own "no GLSL/custom ShaderEffect pipeline" design rule -- Reach itself permits some vs_2_0/ps_2_0 custom shaders, so this is a real, deliberate, documented OPENGL1 gap versus Reach, not a coincidental match); MRT -- `SetRenderTargets()`'s inherited single-target-only default happens to coincide with Reach's own `MaxRenderTargets=1` ceiling, though not by deliberate profile-awareness; volume/3D textures -- unsupported (no `CreateTexture3D()` override), which also happens to coincide with Reach's own `MaxVolumeExtent=0` (3D textures are HiDef-only in real XNA); `RenderTargetCube` -- unsupported (no `CreateRenderTargetCube()` override), an unconditional gap regardless of profile; occlusion queries -- unsupported, and this one is a **genuine Reach-relevant gap, not coincidental** (`docs/d3d9-backend.md` lists occlusion queries among the features real XNA guarantees are present at the `Reach` floor itself, not `HiDef`-gated) -- already flagged by phase 11's `OpenGL1_GraphicsCapability` test. NPOT textures -- no restriction enforced at all (whatever size is given goes straight to `glTexImage2D`), which is *more* permissive than what `Reach` XNA content could actually rely on, the same "unfixable, forbidden to fake" situation as texture-size ceilings. Hardware instancing -- not applicable, no custom-shader Effect pipeline exists to instance through. **Two real, previously-latent bugs found and fixed while actually running the full, unfiltered `CnaTests` suite under `CNA_GRAPHICS_BACKEND=OPENGL1` for the first time ever** (this audit's own empirical step, not just documentation): `tests/.../GraphicsDeviceCapabilityTests.cpp` hardcoded EasyGL-only expected values for `MultipleRenderTargets`/`OcclusionQuery`/`CustomEffects`/`WireFrame` (its own header comment already admitted "only ever builds against a fully 3D-capable backend (EasyGL by default)" -- OPENGL1 is a second, legitimately-different, equally-honest 3D-capable backend that comment did not anticipate), and `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` check never had a `CNA_BACKEND_OPENGL1` branch (the exact same gap class as a documented, already-fixed D3D9 bug sitting in that same file). Both fixed with `#ifdef CNA_BACKEND_OPENGL1` branches; `CnaTests` reconfirmed green for the affected suites on OPENGL1 (33/33) and regression-checked on EasyGL (9/9, zero behavior change). The other 126 `CnaTests` failures seen in this same run are pre-existing, environment-specific (Media/Video/Sound/Xnb content-fixture tests needing files/codecs not present in this sandbox) and unrelated to OPENGL1 or this audit.

## EasyGL parity gap (found 2026-07-20)

Systematic method-by-method diff of `EasyGLGraphicsBackend` against `OpenGL1GraphicsBackend`,
requested explicitly by the project owner ("najdi ti co easy gl backend umi a opengl1 backend
neumi" -- find what EasyGL can do that OPENGL1 can't). Confirmed the existing "Intentional OpenGL
1.x limitations" section already correctly covers the shader/PBR/skinning/MRT/instancing/
RenderTargetCube/custom-VertexDeclaration class of gaps (real, not revisited here). The following
11 gaps are genuinely expressible with real legacy/period-compatible fixed-function OpenGL (no
shader, no modern-only extension) and were simply never implemented yet:

13. ~~Add virtual-resolution/presentation-mode scaling (`GetViewportSize()`/`EffectiveWidth()`/
    `EffectiveHeight()` returning the raw physical window size instead of a dynamically-recomputed
    logical size; `SetPresentationMode()` a no-op; `TransformWindowToLogical`/
    `TransformLogicalToWindow` never overridden) via the same `FixedHeightDynamicWidth`
    aspect-correct recomputation EasyGL's own `getLogicalSize()` already does.~~
    **Done, deliberately narrower scope than the item's own original wording** (found while
    implementing): `GetViewportSize()` was NOT changed -- `GraphicsDevice::UpdateViewportFromWindow()`
    (shared, backend-agnostic) feeds `GetViewportSize()`'s return value DIRECTLY into the real
    `glViewport()` call as well as the public `GraphicsDevice.Viewport` property; making it return a
    recomputed LOGICAL size instead of the real physical size would silently break rendering (GL
    would only draw into a sub-rectangle of the actual window) unless paired with a full
    intermediate-render-target-then-scale-blit compositing pipeline -- a much larger undertaking
    that would push OPENGL1 toward "a second modern rendering pipeline", against this backend's own
    design rule. `EffectiveWidth()`/`EffectiveHeight()` (`OpenGL1SpriteBatchBackend::Begin()`'s own
    `glOrtho` range -- the ONLY consumer, entirely separate from the real `glViewport` call) are
    what actually needed the recomputation, and are the only thing changed: now call a new private
    `ComputeLogicalSize()`, which applies `logicalH = preferredH` (fixed), `logicalW =
    round(physicalW * preferredH / physicalH)` -- matching EasyGL's own `getLogicalSize()` --
    **only** when `presentationMode_ == FixedHeightDynamicWidth` (the default). Every other mode
    (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`) keeps today's original, unrecomputed
    behavior: `Stretch` and `NativeBackBuffer` are honestly already correct that way (`Stretch`
    means "don't preserve aspect", which is exactly what an unscaled logical size mapped via
    `glOrtho` onto a differently-shaped physical viewport already produces; `NativeBackBuffer` means
    "no scaling" by definition); true `Letterbox`/`Overscan` (real bars/cropping) would need the
    actual `glViewport` sub-rectangle adjusted, not just this `SpriteBatch`-facing logical size --
    a documented, intentional gap, not attempted here. `SetPresentationMode(int)` now stores the
    mode (was a pure no-op) in a new `presentationMode_` member, initialized from
    `GraphicsBackendCreateArgs::presentationMode` in the constructor (previously never read at all).
    `TransformWindowToLogical`/`TransformLogicalToWindow` are now overridden using the same
    recomputed logical size: a single uniform `logicalH/physicalH` scale with no offset (the
    logical canvas fills the window exactly under `FixedHeightDynamicWidth`, by construction, so
    there are no letterbox bars to account for) -- matching `Mouse::logical_to_window`'s own
    documented comment for EasyGL's identical model ("a uniform height-scale with no offset ...
    exact for its FixedHeightDynamicWidth model"). Both return the shared interface's own default
    (`false`, "window==logical") for every other presentation mode.

    New test `OpenGL1_PresentationMode` (`examples/opengl1_presentationmode_test.cpp`) constructs
    at 320x240 (4:3), confirms a baseline stripe drawn at logical X=160 lands at physical X=160
    (unresized sanity), then genuinely resizes the real SDL window to 480x480 (a real aspect
    change, 4:3 to 1:1) via `SDL_SetWindowSize`+`SDL_SyncWindow` (found empirically that
    `SDL_SetWindowSize` alone is asynchronous even under this sandbox's Xvfb -- `SDL_GetWindowSizeInPixels`
    read stale pre-resize values immediately afterward without `SDL_SyncWindow`+`SDL_PumpEvents`,
    confirmed via an explicit sanity check in the test itself before trusting the resize).
    Post-resize, a stripe drawn at logical X=120 (half of the CORRECTLY recomputed
    `round(480*240/480)=240`) must land at physical X=~240 (the real centre) -- the pre-existing
    bug (`EffectiveWidth()` stuck at the original 320) would instead place it at physical X=~180, a
    clearly distinguishable 60-pixel difference, matched exactly on the first attempt.
    `TransformWindowToLogical(240,240)`/`TransformLogicalToWindow(120,120)` are checked as an exact
    inverse pair against the same 0.5 scale. Mutation-tested three ways, independently: (1)
    reverting `ComputeLogicalSize()` to always return the stuck construction-time values reproduces
    the predicted stripe position exactly (physical X=180, only that one check fails); (2)
    reverting `TransformWindowToLogical` alone to the shared no-op default reproduces the predicted
    failure (`ok=false`) in exactly its own two checks, leaving every other check -- including the
    structurally-identical but independent `TransformLogicalToWindow` -- still passing, confirming
    clean, isolated discrimination with no coincidental cross-check masking. Full
    `ctest -R "OpenGL1_"` regression sweep: 36/36 passed after this change (confirming the
    FixedHeightDynamicWidth recomputation is a mathematical no-op, and therefore non-regressing,
    for every existing test's construction-time window size, none of which resize their window).
14. ~~Add `BasicEffect.DirectionalLight1`/`DirectionalLight2` via `GL_LIGHT1`/`GL_LIGHT2` (`DrawInternal`
    only ever configures `GL_LIGHT0`, despite `GpuDrawParams` already carrying all 3 lights' data).~~
    **Done**: `DrawInternal`'s lit path now also configures `GL_LIGHT1`/`GL_LIGHT2` from
    `GpuDrawParams::light1Dir`/`light1Diffuse`/`light2Dir`/`light2Diffuse`, the exact same
    `glEnable`/`glLightfv(GL_DIFFUSE)`/`glLightfv(GL_POSITION)` pattern `GL_LIGHT0` already used.
    Both are unconditionally enabled whenever the lit path runs -- safe because
    `FillGpuDrawParams` already zeroes a disabled light's diffuse color (mirroring FNA's own
    `DirectionalLight.Enabled` setter), so a disabled light1/light2 contributes nothing, matching
    every other lit-path field's own "zeroed when disabled" convention; no extra per-light enable
    branching needed. New test `OpenGL1_DirectionalLight12`
    (`examples/opengl1_directionallight12_test.cpp`) explicitly disables `DirectionalLight0` (so
    the test cannot pass via the already-working light0 path alone), sets `DirectionalLight1`=red
    and `DirectionalLight2`=blue both shining straight at a camera-facing quad, and asserts the
    result is exactly magenta `(255,0,255)` -- matched exactly on the first attempt. Mutation-
    tested: removing the new `GL_LIGHT1`/`GL_LIGHT2` configuration reproduces the predicted failure
    (pure black `(0,0,0)`, both lights silently dropped); restoring reconfirms `(255,0,255)`. Full
    `ctest -R "OpenGL1_"` regression sweep: 30/30 passed after this change.
15. ~~Add `BasicEffect` specular highlights via `GL_SPECULAR` material/light state (`GpuDrawParams::
    specularColor`/`specularPower`/`light0-2Specular` are never read by `DrawInternal` at all).~~
    **Done**: the lit path now sets `glMaterialfv(GL_SPECULAR)`/`glMaterialf(GL_SHININESS)` from
    `GpuDrawParams::specularColor`/`specularPower` (clamped to GL's `[0,128]` `GL_SHININESS`
    range) and `glLightfv(GL_LIGHTn,GL_SPECULAR)` for all three lights from `light0-2Specular`,
    plus `glLightModeli(GL_LIGHT_MODEL_COLOR_CONTROL,GL_SEPARATE_SPECULAR_COLOR)` (core GL 1.2) so
    the specular term is added AFTER texture modulation instead of being folded into it (matching
    `BasicEffect.fx`'s own `texture*(ambient+diffuse) + specular` formula) and
    `glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER,GL_TRUE)` so GL computes the eye vector from the
    real eye-space viewer position (available for free since `MODELVIEW` already holds
    `View*World` whenever lights/vertices are specified) instead of an infinite viewer along -Z --
    GL's own automatic equivalent of `GpuDrawParams::eyePositionWorld`'s explicit half-vector
    term, so no separate read of that field is needed for this fixed-function path. New test
    `OpenGL1_Specular` (`examples/opengl1_specular_test.cpp`) isolates the specular term
    completely (material `DiffuseColor`/`AmbientLightColor`/`EmissiveColor` and the light's own
    `DiffuseColor` all black), places a small quad away from the origin along -Z with
    `View`/`World=Identity` so the local-viewer eye position and the light direction are both
    close to the surface normal (the classic near-maximal Blinn-Phong configuration, avoiding a
    razor-thin highlight that would be fragile to assert against), and asserts
    `SpecularColor=black` reads pure black (sanity) while `SpecularColor=white` reads a strong,
    real highlight (`R=236` on the first attempt). Mutation-tested: removing the material
    `GL_SPECULAR`/`GL_SHININESS`/`GL_LIGHT_MODEL_*` configuration reproduces the predicted failure
    (`R=0`, specular silently dropped even with the light's own specular color still configured);
    restoring reconfirms `R=236`. Full `ctest -R "OpenGL1_"` regression sweep: 31/31 passed after
    this change.
16. ~~Add `TextureAddressMode.Mirror` via `GL_MIRRORED_REPEAT` (core GL 1.4) -- currently silently
    treated as `Clamp` in both the 3D path and `SpriteBatch::Draw`.~~
    **Done**: new shared `WrapMode(int)` helper maps XNA's raw `TextureAddressMode` ordinals
    (`Wrap=0`→`GL_REPEAT`, `Mirror=2`→`GL_MIRRORED_REPEAT`, else→`GL_CLAMP_TO_EDGE`), replacing the
    old binary `==0?GL_REPEAT:GL_CLAMP_TO_EDGE` at both call sites (`ApplySamplerFilterAndWrap`,
    the 3D path; `OpenGL1SpriteBatchBackend::Draw`). No capability gate needed --
    `GL_MIRRORED_REPEAT` is core GL 1.4 (2002), unconditionally available the same way
    `GL_CLAMP_TO_EDGE` (core 1.2) already is here. New test
    `OpenGL1_TextureAddressMode_Mirror` (`examples/opengl1_textureaddressmode_mirror_test.cpp`)
    samples a 2x1 red/blue texture across `U=0..2` (two repeats) with `AddressU=Mirror` and
    `TextureFilter.Point` (crisp texel reads). The three wrap modes are only distinguishable in
    the second repeat: `U=1.25` reads blue under both the correct `Mirror` behavior AND the
    pre-existing `Clamp` bug (coincidental, given this texel layout), but `U=1.75` reads red only
    under genuine `Mirror` -- blue under both `Clamp` (bug) and plain `Wrap`. Matched the predicted
    values exactly on the first attempt (`U=0.25` red, `U=0.75` blue, `U=1.25` blue, `U=1.75` red).
    Mutation-tested: reverting `WrapMode` to the old binary mapping reproduces the predicted
    failure (`U=1.75` reads blue instead of red, while `U=1.25` still coincidentally passes,
    confirming `U=1.75` is genuinely the load-bearing check and not `U=1.25`); restoring
    reconfirms all 4 points. Full `ctest -R "OpenGL1_"` regression sweep: 32/32 passed after this
    change.
17. ~~Add constant blend color via `glBlendColor`/`GL_CONSTANT_COLOR` (core GL 1.4/`EXT_blend_color`)
    -- `SetBlendFactor()` is a no-op and `Blend.BlendFactor`/`InverseBlendFactor` currently map to
    `GL_ONE`/`GL_ZERO` instead, meaning this isn't a degraded case, it's a silently WRONG color.~~
18. ~~Add blend-equation-beyond-add via `glBlendEquationSeparate` (core GL 1.4/`EXT_blend_subtract`/
    `EXT_blend_minmax`) -- `ApplyBlendState()` drops `colorBlendFunc`/`alphaBlendFunc` entirely,
    always implicit `GL_FUNC_ADD`.~~
19. ~~Add separate alpha blend factors via `glBlendFuncSeparate` (core GL 1.4/`EXT_blend_func_separate`)
    -- `ApplyBlendState()` currently reuses the color factors for alpha too.~~

    **Items 17/18/19 Done together** (2026-07-20): all three are core GL 1.4 (2002) and were
    implemented as one change, gated on a single new `OpenGL1Capabilities::extendedBlend` flag
    (`coreAtLeast(1,4)`) and one loader (`TryLoadBlendFunctions()`, loading `glBlendColor`/
    `glBlendFuncSeparate`/`glBlendEquationSeparate` via `SDL_GL_GetProcAddress`, same locally-named-
    typedef pattern the multitexture/mipmap loaders already use). `ApplyBlendState()` now calls
    `glBlendFuncSeparate`+`glBlendEquationSeparate` when `extendedBlend` is available, falling back
    to the old single-value `glBlendFunc` (implicit `GL_FUNC_ADD`, color factors reused for alpha)
    on a driver that genuinely lacks it. `SetBlendFactor()` now calls `glBlendColor` when available.
    `BlendF()`'s indices 10/11 (`Blend.BlendFactor`/`InverseBlendFactor`) now map to
    `GL_CONSTANT_COLOR`/`GL_ONE_MINUS_CONSTANT_COLOR` when `extendedBlend` is true (the old
    `GL_ONE`/`GL_ZERO` mapping stays as the fallback for when it isn't, since those enum tokens are
    themselves core-1.4-gated and invalid to pass to `glBlendFunc` on an older driver).

    Three new tests, one per item, all using the 3D draw path (`BasicEffect`+`DrawUserPrimitives`)
    rather than `SpriteBatch` -- found while writing item 17's test that
    `OpenGL1SpriteBatchBackend::Begin()` hardcodes its own `glBlendFunc(GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA)` independent of whatever `BlendState` a game passes to
    `SpriteBatch.Begin()` -- a separate, pre-existing limitation (SpriteBatch ignores a custom
    `BlendState`'s actual factors/equation beyond standard alpha blending) that would otherwise
    mask all three of these fixes; confirmed empirically (an earlier version of the item 17 test
    using `SpriteBatch` always read back the SpriteBatch-hardcoded blend result regardless of
    `BlendFactor`). Flagged here, not fixed -- out of scope for these 3 blend-state items.
    - `OpenGL1_BlendFactor`: draws solid white over black with `ColorSourceBlend=BlendFactor`,
      `ColorDestinationBlend=Zero`, `BlendFactor=(128,0,0,255)` -- correct result `(128,0,0)`,
      matched exactly on the first attempt; the pre-existing bug would give white `(255,255,255)`
      (`BlendFactor` silently treated as `Blend.One`). Mutation-tested (reverting `SetBlendFactor`
      to a no-op): reproduces a distinct third wrong answer, `(0,0,0)` (GL's never-set default
      constant color), confirming the check catches this specific fix independently of `BlendF`'s
      own mapping.
    - `OpenGL1_BlendEquation`: draws an opaque destination `(200,50,50)` then a second quad
      `(50,20,20)` with `ColorBlendFunction=ReverseSubtract`, `ColorSourceBlend=ColorDestinationBlend
      =One` -- correct result `(150,30,30)` (`dest-src`), matched exactly on the first attempt;
      mutation-tested (dropping `colorBlendFunc`/`alphaBlendFunc`): reproduces the predicted
      always-`Add` result `(250,70,70)` (`dest+src`) exactly.
    - `OpenGL1_BlendAlphaFactor`: uses a `RenderTarget2D` (not the backbuffer, for a genuine
      alpha-channel readback via `RenderTarget2D::GetData()`'s real `glReadPixels` against the
      FBO) cleared to alpha=100, then draws a full quad with alpha=200 using
      `ColorSourceBlend=One`/`ColorDestinationBlend=Zero` (color: source passes through, a sanity
      check that the split is genuine) but `AlphaSourceBlend=Zero`/`AlphaDestinationBlend=One`
      (alpha: destination explicitly preserved) -- correct result alpha=100, matched exactly on
      the first attempt; mutation-tested (alpha reusing color's `One`/`Zero` factors): reproduces
      the predicted wrong value alpha=200 (source alpha, following color's own formula by
      mistake) exactly.

    Full `ctest -R "OpenGL1_"` regression sweep: 35/35 passed after these changes.
20. ~~Add a runtime `SetSwapInterval()` override (`SDL_GL_SetSwapInterval`) -- currently only the
    construction-time value ever reaches SDL; changing vsync mid-session silently does nothing.~~
    **Done**: `OpenGL1GraphicsBackend::SetSwapInterval(int)` now calls `SDL_GL_SetSwapInterval(interval)`
    directly, overriding the `IGraphicsBackend` no-op default. New test `OpenGL1_SwapInterval`
    (`examples/opengl1_swapinterval_test.cpp`) verifies against the real driver via
    `SDL_GL_GetSwapInterval()`, not just "no exception thrown": calls `SetSwapInterval(0)` then asserts
    the driver reports `0`, then `SetSwapInterval(1)` and asserts `1` -- the second call proves this is a
    genuine runtime change and not a value that merely happened to already match construction time.
    Mutation-tested: reverting the override to a no-op reproduces the predicted failure
    (`SDL_GL_GetSwapInterval=1` after `SetSwapInterval(0)`, i.e. the driver's prior value was never
    touched); restoring the fix reproduces the predicted correct values (`0` then `1`). Full
    `ctest -R "OpenGL1_"` regression sweep: 27/27 passed after this change.
21. ~~Add `RenderTarget2D` mip-chain generation on unbind, reusing the existing `Texture2D` mip
    machinery (`glGenerateMipmap`/`GL_GENERATE_MIPMAP`/CPU box-filter, phase 6) -- `CreateRenderTarget2D`
    currently silently drops its own `mipMap` parameter (unnamed `bool`).~~
    **Done**: `CreateRenderTarget2D` now forwards `mipMap` into `OpenGL1RenderTargetBackend`, which
    calls `glGenerateMipmap(GL_TEXTURE_2D)` on `UnbindAsRenderTarget()` every time the target stops
    being active, following FNA3D's own `OPENGL_ResolveTarget` mechanism (the whole chain is always
    regenerated wholesale from level 0 -- games never render into non-zero mip levels directly).
    Unlike `Texture2D` there is no CPU-side pixel buffer to box-filter from as a last-resort
    fallback (render target content is GPU-produced), so this is a single-tier fallback: gated
    purely on `glGenerateMipmap` being loadable, which is expected always true in practice since
    it's part of the same `ARB_framebuffer_object`/core-3.0 entry-point family `RenderTarget2D`
    support already requires (`TryLoadOpenGL1FramebufferObjectFunctions()` now loads it alongside
    the other FBO entry points, but does not gate `loaded_` on it, matching the same honest-failure
    shape `OpenGL1TextureBackend::RegenerateMips()` already uses when neither mechanism is
    present). `OpenGL1RenderTargetBackend::HasMips()` reports whether the last unbind actually
    produced a complete chain.

    **Adjacent finding**: fixing generation alone was not sufficient to make the chain usable.
    Every 3D/SpriteBatch texture-sampling call site (`DrawInternal`'s `texture0`/`texture1`
    binding, `OpenGL1SpriteBatchBackend::Draw`) decided mip-awareness via
    `dynamic_cast<const OpenGL1TextureBackend*>`, which is always `nullptr` for an
    `OpenGL1RenderTargetBackend` -- a different class entirely -- silently forcing `mip=false` for
    every render-target sample regardless of whether a real chain existed. Fixed by centralizing
    all three call sites behind a new `HasMipsFor(const ITextureBackend*)` helper that also checks
    `OpenGL1RenderTargetBackend::HasMips()`.

    **Test-design finding**: a first version of the regression test filled the render target with
    a single solid color and sampled it back minified with `TextureFilter::Anisotropic` (the same
    probe EasyGL's own Task 336 fix, `examples/easygl_rendertarget2d_mip_test.cpp`, uses) -- this
    passed even with mip regeneration mutated out entirely, a false pass, for two compounding
    reasons: (1) a solid color reads back identically at every mip level regardless of whether
    real levels exist, since `HasMipsFor()`'s own gating correctly falls back to a plain
    non-mipmap `GL_LINEAR` filter when `HasMips()` is `false`, and a flat color survives that
    fallback unchanged; (2) a second attempt using a checkerboard pattern (mirroring
    `OpenGL1_MipmapGeneration`'s already-proven discriminator) still produced a coincidental exact
    mid-gray even with generation mutated out, because the specific quad/viewport geometry chosen
    mapped the read-back pixel's center to EXACTLY a texel-grid corner where a checkerboard's
    diagonal 2x2 neighborhood (2 white + 2 black) blends to exact gray under plain bilinear
    filtering ALONE, with no mip chain involved at all -- the same "default value masks bug" class
    of trap this project was already burned by once in phase 6's SamplerState finding. Fixed by
    choosing a minification amount whose read-back pixel lands solidly inside a single checker
    block (not on a block-boundary corner), so an unmipped level-0-only sample reads a pure,
    unblended block color, unambiguously far from gray. New test `OpenGL1_RenderTarget2D_Mip`
    (`examples/opengl1_rendertarget2d_mip_test.cpp`) mutation-tested against both fixes
    independently: reverting `UnbindAsRenderTarget()`'s generation call reproduces the predicted
    failure (minified sample reads pure white `(255,255,255)` instead of mid-gray); separately
    forcing `HasMipsFor()` to always return `false` (generation intact) reproduces the identical
    failure, confirming the sampling-path fix is independently load-bearing, not just the
    generation call. Restoring both fixes reconfirms the predicted correct values (native: sharp
    aliased white `(255,255,255)`; minified: exact mid-gray `(128,128,128)`). Full
    `ctest -R "OpenGL1_"` regression sweep: 28/28 passed after this change.
22. ~~Add backbuffer multisampling via `SDL_GL_MULTISAMPLEBUFFERS`/`SAMPLES` context attributes +
    `GL_MULTISAMPLE` (`WGL`/`GLX_ARB_multisample`, ratified 1998, same era as `ARB_multitexture`
    already used here) -- `multiSampleCount` is currently read by nothing, `SupportsCapability
    (MultiSampleAntiAliasing)` hardcoded false. Backbuffer-only; `RenderTarget2D` MSAA (EasyGL's own
    manual offscreen-FBO resolve) correctly stays out of scope -- genuinely needs modern extensions.~~
    **Done**: `GraphicsDevice.cpp` now requests `SDL_GL_MULTISAMPLEBUFFERS`/`SDL_GL_MULTISAMPLESAMPLES`
    before `SDL_CreateWindow()` for `CNA_BACKEND_OPENGL1` (same GLX-visual-fixed-at-window-creation-
    time block the depth/stencil fix already lives in), when `PresentationParameters.MultiSampleCount
    > 1`. `OpenGL1GraphicsBackend::DetectMultiSampleCount()` (called from the constructor and from
    `DebugSimulateContextLoss()`) reads back whatever the driver GENUINELY granted via
    `SDL_GL_GetAttribute` -- not just echoing the request, since GLX can silently clamp/refuse it --
    and calls `glEnable(GL_MULTISAMPLE)` when real. `GetMultiSampleCount()` now returns the honest
    applied count; `SupportsCapability(MultiSampleAntiAliasing)` now returns `multiSampleCount_>1`
    instead of hardcoded `false`. `ApplyMultiSampleCount()` is deliberately NOT overridden -- the
    `IGraphicsBackend` default (echo `GetMultiSampleCount()` back unchanged) is already the honest
    answer, since a GLX window visual cannot be reconfigured post-construction, matching EasyGL's
    own established behavior for the identical reason (`examples/easygl_msaa_change_test.cpp`'s own
    comments).

    **Scoping finding, found while writing the regression test**: `GraphicsDeviceManager.
    PreferMultiSampling` -- the common `Game`-based idiom every other OPENGL1 test in this suite
    uses -- cannot actually reach this fix. `Game::GraphicsDevice_` (`Game.hpp`) is a plain VALUE
    member, constructed with a fully-default `PresentationParameters` (`MultiSampleCount=0`) as
    part of `Game`'s own base-class construction -- this happens before a derived `Game` subclass's
    constructor body (where `GraphicsDeviceManager` is constructed and `PreferMultiSampling` is set)
    ever runs. By the time `GraphicsDeviceManager::CreateDevice()` later tries to push its computed
    `MultiSampleCount` into the ALREADY-CONSTRUCTED device via `Reset()`/`ApplyMultiSampleCount()`,
    the window (and its GLX visual) already exists without a multisample buffer --
    `RecreateBackendForMultiSampleCount()` only recreates the GL context, not the window itself, so
    a window-visual-based mechanism genuinely cannot be reconfigured in place. This is a pre-existing
    `Game`/`GraphicsDeviceManager` construction-order constraint (not OPENGL1-specific, not
    introduced by this change, and well outside this item's scope to fix). What DOES work: a
    `PresentationParameters` with `MultiSampleCount` already set BEFORE being passed to
    `GraphicsDevice`'s own `(adapter, profile, presentationParameters)` constructor directly (a
    real, valid XNA construction path independent of `Game`/`GraphicsDeviceManager`, following the
    same direct-construction pattern `examples/d3d12_smoke_test.cpp`'s own windowless-device test
    already established) -- the new `OpenGL1_MSAA` test uses this path.

    A second, smaller finding along the way: `GraphicsDevice::createBackend()` (the path used by
    the INITIAL device construction) never writes the backend's honestly-applied `MultiSampleCount`
    back into `PresentationParameters` the way `GraphicsDevice::Reset()`/
    `RecreateBackendForMultiSampleCount()` does via `ApplyMultiSampleCount()` -- so
    `PresentationParameters.MultiSampleCount` read right after initial construction is only ever an
    echo of what was requested, not proof of what was granted. `IGraphicsBackend::
    GetMultiSampleCount()` (reachable via the NOXNA `GraphicsDevice::GetBackend()`) is the only
    authoritative source in that case; the test uses it as its primary check rather than the PP
    echo. Also a pre-existing, cross-backend `GraphicsDevice`-level gap, out of scope here.

    New test `OpenGL1_MSAA` (`examples/opengl1_msaa_test.cpp`) asserts `GetMultiSampleCount()==4`
    (the exact requested count) and `SupportsCapability(MultiSampleAntiAliasing)==true`, both
    mutation-tested independently (reverting `DetectMultiSampleCount()` to always report 0, and
    separately reverting just the `SupportsCapability` mapping to hardcoded `false` with detection
    intact) -- both reproduce the predicted failure, confirming each fix is independently
    load-bearing. The test also draws a triangle whose hypotenuse is exactly the line `x+y=65` in
    screen-pixel space (passing exactly through pixel (32,32)'s continuous center) and prints
    (diagnostic only, not asserted) whether the read-back edge shows genuine per-sample coverage
    blending. It does not on this sandbox: confirmed via a minimal, CNA-independent standalone
    SDL3+OpenGL program that even raw `glReadPixels` from a driver-confirmed 4x multisample GLX
    visual (`GL_SAMPLE_BUFFERS=1`, `GL_SAMPLES=4`, `GL_MULTISAMPLE` enabled, zero GL errors -- all
    independently verified) reads back perfectly binary on this sandbox's Mesa llvmpipe software
    rasterizer under Xvfb -- a real environment/driver limitation in resolve-on-read for a
    window-system multisample framebuffer, not a bug in this fix. Everything actually within
    OPENGL1's own control (the request reaching the driver, the capability correctly reporting it)
    is independently, empirically verified above; real hardware/drivers are expected to resolve
    correctly, matching this repo's existing precedent of trusting driver-reported capabilities
    that this sandbox's software rasterizer cannot itself fully exercise. Full
    `ctest -R "OpenGL1_"` regression sweep: 29/29 passed after this change.
23. ~~Add real occlusion queries via `ARB_occlusion_query`/core GL 1.5 (`glGenQueries`/
    `glBeginQuery(GL_SAMPLES_PASSED)`/`glEndQuery`/`glGetQueryObjectuiv`, ratified 2001/core 2003,
    same `SDL_GL_GetProcAddress` loading pattern already used for `ARB_framebuffer_object`) --
    genuinely pre-shader-era and fixed-function-orthogonal, not a "second modern OpenGL backend"
    concern any more than the existing FBO-based `RenderTarget2D` support already is. Supersedes
    phase 11/12's "no native modern occlusion-query guarantee" framing, which predates this
    finding.~~ **Done**: new `OpenGL1OcclusionQueryBackend` (`OpenGL1OcclusionQueryBackend.hpp`/
    `.cpp`) implements `IOcclusionQueryBackend` via `glGenQueries`/`glBeginQuery(GL_SAMPLES_
    PASSED)`/`glEndQuery`/`glGetQueryObjectiv(GL_QUERY_RESULT_AVAILABLE)`/`glGetQueryObjectuiv
    (GL_QUERY_RESULT)`, entry points loaded via `TryLoadOpenGL1OcclusionQueryFunctions()`
    (`SDL_GL_GetProcAddress`, same pattern as `TryLoadOpenGL1FramebufferObjectFunctions()`), gated
    on a new `OpenGL1Capabilities::occlusionQuery` flag (`coreAtLeast(1,5) ||
    GL_ARB_occlusion_query`). `OpenGL1GraphicsBackend::CreateOcclusionQuery()` returns nullptr when
    unavailable (the documented `IGraphicsBackend` contract, matching `CreateRenderTarget2D`'s own
    capability-gated fallback); `SupportsCapability(OcclusionQuery)` now returns the real detected
    value instead of hardcoded `false`. Uses `GL_SAMPLES_PASSED` (not `GL_ANY_SAMPLES_PASSED`) --
    on a non-multisampled target this is a real, exact visible-sample count, more XNA-faithful
    than EasyGL's own GLES3-constrained implementation (`IOcclusionQueryBackend`'s own doc comment:
    "On OpenGL ES 3.0 (EasyGL) ... 0 or 1"). No context-loss recovery: an occlusion query is an
    ephemeral, single-use GPU measurement with no persistent content to restore, unlike
    `Texture2D`/`TextureCube`/`RenderTarget2D` -- documented as an intentional scope decision, not
    an oversight.

    `OpenGL1_GraphicsCapability` (`examples/opengl1_graphics_capability_test.cpp`) extended with a
    real, functionally meaningful verification, not just "some query object exists": cross-checks
    `SupportsCapability(OcclusionQuery)` against an independent raw `GL_VERSION`/`GL_EXTENSIONS`
    scan (same style as the file's own pre-existing `AnisotropicFiltering` check), then draws a
    full-viewport quad with nothing else present (must report `PixelCount()` close to the real
    viewport pixel area -- got exactly `512` for a `32x16` viewport) and the SAME quad fully
    covered by a nearer opaque occluder with real depth testing enabled (must report
    `PixelCount()==0` -- real `GL_LESS_EQUAL` depth-test rejection, not a placeholder). Found while
    writing this: `Matrix::CreateOrthographicOffCenter`'s `M33`/`M43` map world Z to clip-space Z
    with a NEGATIVE slope for near/far arguments `(-1,1)` -- a LARGER world Z is NEARER (passes
    `LessEqual`) under this convention, the opposite of the more common "larger Z = farther"
    intuition; confirmed empirically (occluder at world Z=0.9 in front of a target at Z=0.5
    correctly occludes; the reverse assignment did not). Mutation-tested both fixes independently:
    reverting `OpenGL1Capabilities::occlusionQuery` detection to always `false` reproduces the
    predicted `SupportsCapability` mismatch (test still structurally passes via its own honest
    degraded-behavior branch, but the capability cross-check itself correctly fails); separately
    hardcoding `OpenGL1OcclusionQueryBackend::PixelCount()` to always return a nonzero placeholder
    (999) with detection intact reproduces the predicted failure (occluded case reads `999` instead
    of `0`) -- confirming both the capability detection and the real `GL_SAMPLES_PASSED` mechanism
    are independently load-bearing. The shared `GraphicsDeviceCapabilityTests.cpp`
    (`CnaTests`)'s own `CNA_BACKEND_OPENGL1`-gated `SupportsOcclusionQuery` expectation (previously
    `EXPECT_FALSE`, a phase-12 finding predating this item) is now `EXPECT_TRUE` unconditionally --
    OcclusionQuery is no longer one of the capabilities that legitimately differs between OPENGL1
    and EasyGL, both real, 3D-capable backends. Full `ctest -R "OpenGL1_"` regression sweep: 29/29
    passed after this change; `CnaTests --gtest_filter="GraphicsDeviceCapabilityTest.*"`: 8/8
    passed under `CNA_GRAPHICS_BACKEND=OPENGL1`.

This closed item 23. Items 13-19 (virtual-resolution/presentation-mode scaling, `BasicEffect`
`DirectionalLight1`/`DirectionalLight2`, `BasicEffect` specular highlights,
`TextureAddressMode.Mirror`, constant blend color, blend equation beyond add, separate alpha blend
factors) were completed the same day in the same session -- see each item's own entry above for
its full writeup. **All 11 items in the EasyGL parity list (13-23) found 2026-07-20 are now
Done.**

## Further improvements beyond EasyGL parity (2026-07-20)

With the EasyGL parity list closed, the project owner asked what else could realistically be
improved without turning OPENGL1 into "a second modern OpenGL backend" (this backend's own design
rule). Of the "Intentional OpenGL 1.x limitations" list, two are genuinely addable with zero new
shader/programmable-pipeline dependency, reusing machinery this backend already has:

24. ~~Add `RenderTargetCube` by combining the existing FBO (`OpenGL1RenderTargetBackend`, item 2)
    and cube-map (`OpenGL1TextureCubeBackend`, item 5) machinery -- `CreateRenderTargetCube()`
    currently inherits the `IGraphicsBackend` `nullptr` default.~~ **Done**: new
    `OpenGL1RenderTargetCubeBackend` (`OpenGL1RenderTargetBackend.hpp`/`.cpp`, same translation
    unit as the 2D render target -- they already share the FBO loader/`glGenerateMipmap_` symbol)
    uses ONE reusable FBO whose color attachment is re-pointed at the requested face
    (`GL_TEXTURE_CUBE_MAP_POSITIVE_X+face`) on every `BindAsRenderTargetFace()` call, not six
    separate FBOs -- the standard pattern. One depth/stencil renderbuffer is shared across all six
    faces (only one is ever the active draw target at a time). `GetData()` reads back via
    `glGetTexImage` directly on the cube texture object (not `glReadPixels` against the bound FBO)
    -- a cube face's rendered content is retrievable straight from the texture object regardless
    of which FBO/attachment point last wrote it, the same mechanism `OpenGL1TextureCubeBackend::
    GetData()` already uses for CPU-uploaded content -- but still needs the same row-flip
    `OpenGL1RenderTargetBackend::GetData()` applies (GPU-rasterized content is bottom-up, unlike a
    CPU-uploaded face's top-down convention). Mip-chain regeneration on unbind reuses item 21's
    exact mechanism (`glGenerateMipmap(GL_TEXTURE_CUBE_MAP)` regenerates all 6 faces in one call).
    Requires both `framebufferObject` and `textureCubeMap` capabilities; `CreateRenderTargetCube()`
    returns nullptr when either is absent. `multiSampleCount` is accepted but ignored -- out of
    scope for this item (item 25 addresses `RenderTarget2D` MSAA specifically, not six separate
    cube-face resolve targets).

    **A cube-face target needed its own tracked state, separate from `currentRt_`**: unlike
    `SetRenderTarget2D()`'s single `IRenderTargetBackend* currentRt_`, a bound cube face has no
    natural home in that 2D-only-typed pointer, so `EffectiveWidth()`/`EffectiveHeight()`/
    `SetViewport()`/`SetScissorRect()` (which all branch on `currentRt_` for the active target's
    real size) would have had no way to see a cube face's size at all. Added `currentCubeRt_`/
    `currentCubeFace_` members and a new `SetRenderTargetCubeFace()` override (default
    implementation is call-through-only, no state tracking); `SetRenderTarget2D()` and
    `SetRenderTargetCubeFace()` now each clear the OTHER's stale pointer when switching between
    the two kinds, mirroring `SetRenderTarget2D()`'s own pre-existing "unbind whatever was active
    before" symmetry. `DebugSimulateContextLoss()`'s rebind-what-was-active fix (item 8) is
    extended to the cube case too (`currentCubeFace_` tracks WHICH face, so recovery re-attaches
    the correct one, not just any).

    New test `OpenGL1_RenderTargetCube` (`examples/opengl1_rendertargetcube_test.cpp`) renders two
    DIFFERENT solid colors into two DIFFERENT faces of the same target (+X red, +Y blue), unbinds,
    and reads both back independently via `RenderTargetCube::GetData()` -- a genuine GPU readback,
    not a CPU shadow -- confirming each face kept its own distinct content (not just "something got
    painted somewhere", which a single-face test could not rule out an all-faces-aliased bug). A
    third, never-rendered face is checked to have picked up neither color, confirming `Build()`'s
    per-face pre-allocation didn't leak content across faces. Also exercises
    `DebugSimulateContextLoss()` while a face is genuinely bound and active, matching the
    established item-8 rigor for `RenderTarget2D`/`TextureCube`. All 4 checks matched their
    predicted values exactly on the first attempt. Mutation-tested two ways, independently: (1)
    `BindAsRenderTargetFace()` mutated to always attach face 0 regardless of the requested face --
    reproduces the predicted failure exactly (+X reads blue, last-write-wins since both draws
    landed on the same actual face; +Y reads black, since the real +Y attachment point was never
    written; the never-rendered -X face correctly stays unaffected; the context-loss check also
    fails, since its own draw also silently landed on face 0). (2) `CreateRenderTargetCube()`
    mutated to always return `nullptr` -- reproduces the predicted graceful-degradation failure
    (every face reads back `(0,0,0)`, no crash, matching `RenderTargetCube`'s own documented
    null-backend tolerance). Restoring each mutation independently reconfirmed all 4 checks pass.
    Full `ctest -R "OpenGL1_"` regression sweep: 37/37 passed after this change.

    **Unrelated environment note, encountered while mutation-testing this item**: a full `CNA`
    library rebuild transiently failed with `ContentReader has no member named ReadChar/
    ReadDecimal` (Xnb content-reader code, unrelated to graphics/OPENGL1) on one attempt, then
    succeeded cleanly on retry with no source changes -- confirmed by compiling the affected file
    in isolation (succeeded immediately) and by `ContentReader.hpp` being byte-identical between
    `feature/opengl1` and `develop`. A transient incremental-build/dependency-scan glitch, not a
    real defect in either this item's own changes or the pre-existing Xnb code; flagged here in
    case it recurs, not fixed (out of scope).

25. ~~Add `RenderTarget2D` MSAA via `glRenderbufferStorageMultisample`/`glBlitFramebuffer` (core GL
    3.0, or the older `EXT_framebuffer_multisample`/`EXT_framebuffer_blit` pair) -- the same
    "driver from ~2005+ already has this" tier `RenderTarget2D`/mipmap-generation/item-24 above
    already rely on. `GetMultiSampleCount()` currently hardcoded 0; `multiSampleCount` constructor
    argument accepted but ignored.~~ **Done.**

    `glRenderbufferStorageMultisample_`/`glBlitFramebuffer_` are loaded by the SAME
    `TryLoadOpenGL1FramebufferObjectFunctions()` loader the required FBO functions already use --
    unlike `glGenerateMipmap` (item 21, a genuinely separate, independently-existing extension
    family), these two are part of the SAME `ARB_framebuffer_object`/core-3.0 entry-point family
    the mandatory FBO functions already are. Not required for `loaded_`, so a driver that somehow
    lacks just these two keeps plain single-sample `RenderTarget2D` working, only losing MSAA.

    `OpenGL1RenderTargetBackend` gained a SEPARATE `msaaFbo_`/`msaaColorRbo_`/`msaaDepthRbo_`
    triplet (built by the new `BuildMsaa()`, called right after the existing single-sample
    `Build()` in both the constructor and `RecreateGLResource()`), alongside the pre-existing
    single-sample `fbo_`/`colorTex_`/`depthRbo_` triplet. `BindAsRenderTarget()` now targets
    `msaaFbo_` when `multiSampleCount_ > 1` (all rendering goes to the multisample buffers);
    `UnbindAsRenderTarget()` resolves via `glBlitFramebuffer_(..., GL_COLOR_BUFFER_BIT,
    GL_NEAREST)` BEFORE the existing mip-chain regeneration (mips must be generated from the
    just-resolved image, not stale/undefined multisample content); `GetData()` is unchanged --
    always reads the resolved single-sample `fbo_`. `BuildMsaa()` is best-effort and NOT fatal on
    failure: an incomplete/unsupported MSAA framebuffer just leaves `multiSampleCount_` at 0 and
    every draw/resolve call correctly falls back to the plain single-sample path, matching FNA3D's
    own "real, device-clamped value, possibly 0" `MultiSampleCount` semantics rather than
    throwing. Requested sample count is clamped to a queried `GL_MAX_SAMPLES`. New
    `GetMultiSampleCount()` override returns the real, applied `multiSampleCount_` (was
    previously the interface's hardcoded-0 default). `CreateRenderTarget2D()`'s
    `multiSampleCount` parameter, previously accepted but silently dropped, is now forwarded.

    New test `OpenGL1_RenderTarget2D_MSAA` (`examples/opengl1_rendertarget2d_msaa_test.cpp`)
    creates a `RenderTarget2D` with `preferredMultiSampleCount=4` and checks: (1)
    `MultiSampleCount` reflects a real, genuinely-applied value, not silently 0 (the pre-existing
    bug); (2) a solid-color fill survives the render+resolve pipeline intact (a broken MSAA FBO or
    resolve blit could easily produce black/garbage even for trivial full-coverage content); (3)
    an edge-crossing triangle produces a genuinely blended (neither fully-covered nor uncovered)
    pixel at the diagonal edge, proving `glBlitFramebuffer_` performed a real multisample resolve
    rather than a degenerate copy. Unlike item 22's OWN backbuffer-MSAA test (downgraded to
    diagnostic-only after a standalone SDL+GL probe proved even a driver-confirmed multisample GLX
    *window* visual reads back perfectly binary on this sandbox's Mesa llvmpipe software
    rasterizer -- a real environment limitation in implicit resolve-on-read), this render-target
    path uses this backend's OWN EXPLICIT `glBlitFramebuffer_` resolve rather than any
    driver-implicit resolve, and empirically DOES produce a real, exactly-mid-value blended pixel
    (confirmed: `R=128` exactly, neither the fully-covered `R=255` nor the uncovered `R=0`) --
    reliable enough to assert here as a real hard check, not just diagnostic. All 3 checks matched
    their predicted values on the first attempt.

    Mutation-tested two ways, independently: (1) `BindAsRenderTarget()` mutated to always bind
    `fbo_` regardless of `multiSampleCount_` (simulating "rendering never actually reaches the
    MSAA buffers") -- reproduces the predicted failure exactly (resolved centre reads `(0,0,0)`
    instead of `(0,200,0)`, since the resolve blit overwrites the correctly-drawn single-sample
    content with garbage from the never-written `msaaFbo_`; the antialiasing check also fails,
    uniformly `r=0` with no blended value; `1/3 PASS`). (2) `BuildMsaa()` mutated to leave
    `multiSampleCount_ = 0` even after a fully successful build (simulating "MSAA build succeeds
    but its sample count is never reported") -- produces a DIFFERENT, complementary failure
    signature: check 1 fails (`MultiSampleCount=0`), check 2 still PASSES (since `multiSampleCount_`
    also drives `BindAsRenderTarget()`'s own branch, so the single-sample fallback path still
    renders correctly), check 3 fails (no antialiasing possible from single-sample rendering);
    `1/3 PASS`. Restoring each mutation independently reconfirmed all 3 checks pass with the exact
    predicted correct values (`MultiSampleCount=4`, resolved centre `(0,200,0)`, edge `r=128` at
    the blend pixel). Full `ctest -R "OpenGL1_"` regression sweep: 38/38 passed after this change.

## Bugs found while adding test coverage (2026-07-19)

Fixed alongside the initial `void*`/`SDL_GLContext` build error, `ApplyRasterizerState`'s
inverted `GL_FRONT`/`GL_BACK` mapping, and `SpriteBatch`'s double-applied origin offset (all
found and fixed the same day the backend was first run — see git log): the stride==20
(`VertexPositionTexture`) vertex path in `DrawInternal` ignored `GpuDrawParams::diffuseColor`
entirely (hardcoded white), and the window's GL context requested `SDL_GL_STENCIL_SIZE=8` too
late (after `SDL_CreateWindow`) to take effect on X11/GLX, silently producing a 0-bit stencil
buffer and making every `DepthStencilState.StencilEnable` a no-op. See
`tests/opengl1/README.md`'s "Bugs found and fixed" section for the full detail on each.

Three more found while implementing and round-trip-testing RenderTarget2D (same day):
- `OpenGL1SpriteBatchBackend::Begin()` never disabled `GL_CULL_FACE` -- real XNA `SpriteBatch`
  is never subject to 3D face culling, but a game's `RasterizerState.CullMode` (left over from
  its own 3D rendering) could silently make sprites disappear depending on winding. Latent on
  the default backbuffer (the fixed quad winding happened to survive the default
  `CullCounterClockwiseFace` state) but became directly observable once render-target rendering
  needed a genuinely different code path to render into.
- `GL_CLAMP` (not `GL_CLAMP_TO_EDGE`) was used for texture wrap mode everywhere (texture
  creation, `ApplySamplerState`, the new RT color texture) -- `GL_CLAMP`'s border-color
  blending (toward transparent black) kicks in for `GL_LINEAR`-filtered samples anywhere near a
  texture edge, which for a very small texture (e.g. a 1x1 solid-color texture, a common
  pattern) is effectively everywhere except the exact texel centre. A general texture-fidelity
  bug, not RT-specific, but only surfaced once a test sampled off-centre. Fixed everywhere.
- A render target's color texture is written by the GPU rasterizer, whose row 0 is the BOTTOM
  of what was drawn into it (standard GL framebuffer convention) -- the opposite of every
  CPU-uploaded texture's row 0 (this project's own convention: the TOP, since images upload
  top-down). `OpenGL1RenderTargetBackend::GetData()`'s own row-flip already corrects this for
  CPU readback, but that flip does *not* apply when the render target is instead sampled
  directly as a texture (drawing it via `SpriteBatch`, or binding it as `BasicEffect.Texture`)
  -- confirmed the two paths are genuinely independent by testing them separately. Fixed with a
  V-coordinate swap for `SpriteBatch::Draw()` (checked via `dynamic_cast<const
  IRenderTargetBackend*>`) and an equivalent `GL_TEXTURE` matrix flip in `DrawInternal` for the
  3D path (texcoords there are baked into the vertex buffer, not computed per-draw).

One more found while implementing `EnvironmentMapEffect`'s cube-map support (same day):
- `DrawInternal`'s lit (stride 32, `VertexPositionNormalTexture`) path enabled `GL_COLOR_MATERIAL`
  with `GL_AMBIENT_AND_DIFFUSE` but never set `GL_EMISSION` at all -- any lit draw's
  `EmissiveColor` (`BasicEffect`) or combined `EmissiveColor`+`AmbientLightColor*DiffuseColor`
  (`EnvironmentMapEffect`, which does not populate `GpuDrawParams::ambientColor` the way
  `BasicEffect` does -- it pre-combines everything into `emissiveColor` instead) was silently
  dropped from the rendered color entirely. Real XNA/GL fixed-function lighting sums material
  emission flatly regardless of any light being on or off. Fixed with
  `glMaterialfv(GL_FRONT_AND_BACK,GL_EMISSION,...)` from `GpuDrawParams::emissiveColor` alongside
  the existing ambient/diffuse material setup. Caught while building `OpenGL1_EnvironmentMapEffect`
  -- without this fix the test's "pure base color" (`Amount=0`) case would have read back as black
  instead of the expected `AmbientLightColor*DiffuseColor`.

## Status: OPENGL-CUBE-1 — XNA Clear ignores draw masks (2026-08-22)

The cna-template cube progressively collapsed while its five-second SpriteBatch banner was
visible, then recovered as soon as the banner disappeared. The banner left
`DepthBufferWriteEnable=false`; this backend passed the state straight through to `glClear`, and
OpenGL consequently refused to clear depth on the next frame. FNA3D explicitly saves, opens and
restores the scissor plus color/depth/stencil write masks around every clear because XNA
`GraphicsDevice.Clear` is independent of draw state. OPENGL1 now follows that contract for all six
clear combinations. The extended `OpenGL1_DepthStencilState_WriteEnable_Golden` test seeds near
depth, clears to far while depth writes are disabled, then proves both that the clear occurred and
that the disabled mask was restored. Direct Xvfb run: all three checks pass. The rebuilt
cna-template OPENGL1 executable was runtime-inspected at 0.4, 1.6 and 2.8 seconds; the textured
cube remains complete throughout the overlapping translucent banner.

## Design rule
Do not turn OPENGL1 into a second modern OpenGL backend. Features should be implemented with true legacy/fixed-function OpenGL or well-defined period-compatible extensions. Shader-dependent XNA/NOXNA features should remain unsupported rather than secretly delegating to EasyGL.
