# OpenGL 4 Graphics Backend — Implementation Plan

> **Status (2026-07-21): Phase 1 (`GL4-1`–`GL4-13`) landed and verified.** `CNA_GRAPHICS_BACKEND=OPENGL4`
> configures, builds (`cna_backend_graphics_opengl4`), and a real window with a real desktop
> `SDL_GL_CONTEXT_PROFILE_CORE` context (confirmed via `glGetString(GL_VERSION)` reporting
> `4.5 (Core Profile) Mesa 25.2.8` on this dev machine's llvmpipe/Mesa driver under Xvfb) clears
> color/depth/stencil, uploads a real `Texture2D`, draws a real `SpriteBatch` scene (tint, alpha,
> rotation/flip, source-rectangle cropping, and all three sampler address modes — Wrap/Clamp/
> Mirror), and draws real 3D geometry through `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s
> stride-keyed dispatch: `colored3d` (`VertexPositionColor`/`BasicEffect.VertexColorEnabled`,
> with a genuine depth-test occlusion proof verified two ways/draw-order-independent),
> `textured3d` (`VertexPositionTexture`/`BasicEffect.TextureEnabled`, real texture sampling, not a
> diffuse-only fallback), `colored_textured3d` (`VertexPositionColorTexture`, vertex-color tint
> multiplying the sampled texture), and `lit_textured3d` (`VertexPositionNormalTexture`/
> `BasicEffect.EnableDefaultLighting()`, real ambient+Lambertian-diffuse+Blinn-Phong-specular
> lighting, proven to actually differ from the unlit render). Verified by `OpenGL4_Smoke` (8/8),
> `OpenGL4_Readback` (10/10), `OpenGL4_3D` (4/4), and `OpenGL4_Textured3D` (5/5) — all real
> pixel-readback assertions via `GraphicsDevice.GetBackBufferData()`, not just "didn't throw" —
> `ctest -R OpenGL4`, all pass. Two real bugs were found and fixed while getting there — see
> `GL4-7`/`GL4-9`'s rows below.
>
> **Status (2026-07-22): `GL4-14` (`RenderTarget2D`, real FBO) landed and verified.**
> `OpenGL4RenderTargetBackend` is a real FBO with a colour texture attachment, an optional
> depth/stencil renderbuffer (`Depth16`/`Depth24`/`Depth24Stencil8`, exactly the format
> requested — `DepthFormat::None` omits the attachment entirely), an optional multisampled colour
> (and depth) renderbuffer resolved into the single-sample colour texture via
> `glBlitFramebuffer` on unbind, and an optional mip chain regenerated via `glGenerateMipmap` on
> unbind — modeled directly on `EasyGLRenderTargetBackend`'s own resource shape but using raw
> `GL4Loader` calls instead of the `easygl::` wrapper types this backend deliberately avoids.
> `RenderTarget2D::GetData()` is real (`OpenGL4RenderTargetBackend::GetData()`, via a throwaway
> per-level read FBO), not the EasyGL-style gap the "Remaining work" section used to note for
> this backend's peers. Two real, non-hypothetical bugs were found and fixed while wiring this
> up, both from the exact same root cause (code that assumed "the currently bound target is
> always the real backbuffer"): `SetViewport`'s bottom-left-to-top-left Y flip was hardcoded to
> the window's physical height, which is wrong once an FBO smaller than the window is bound (now
> keyed off a new `currentRtHeight_` member, mirroring `EasyGLGraphicsBackend`'s identical
> pattern); and `OpenGL4SpriteBatchBackend::FlushBatch`'s viewport/ortho sizing had the same
> window-size-only assumption, which would have silently broken any `SpriteBatch::Draw()` issued
> while a render target was bound (now checks a new `GetCurrentRenderTarget2DSize()` accessor
> first). Verified by `OpenGL4_RenderTarget2D` (12/12: Clear-only/colored3d/depth-tested draws
> sampled back via `SpriteBatch`, `MultiSampleCount` property fidelity, real `GetData()` pixel
> reads on all three, a mipMap round-trip, a real MSAA round-trip through the
> `glBlitFramebuffer` resolve path, and a `SpriteBatch::Draw()`-into-a-bound-RT check that
> specifically exercises the `FlushBatch` fix) plus a full re-run of `OpenGL4_Smoke` (8/8),
> `OpenGL4_Readback` (10/10), `OpenGL4_3D` (4/4), and `OpenGL4_Textured3D` (5/5) confirming no
> regression from the shared `SetViewport`/`FlushBatch` changes. `RenderTargetCube`/MRT are not
> part of this task — see "Remaining work" below.
>
> **Status (2026-07-22): `GL4-15` (`RenderTargetCube` + real MRT) landed and verified.**
> `OpenGL4RenderTargetCubeBackend` is one shared cube-map texture with a single FBO re-attaching
> whichever face (`0`=+X..`5`=-Z) is currently bound, the same depth/stencil-renderbuffer/MSAA-
> resolve/mip-regen machinery as `GL4-14`'s 2D target, and a real `GetData()` per face+level via a
> throwaway read FBO — modeled directly on `EasyGLRenderTargetCubeBackend`. `SetRenderTargets`
> (plural) is real MRT: a lazily-created, persistent FBO with one `glFramebufferTexture2D`
> attachment per target at `GL_COLOR_ATTACHMENT0+i` and a real `glDrawBuffers` call — not the
> inherited single-target-only default. No depth attachment for MRT (same accepted, documented gap
> `EasyGLGraphicsBackend`'s own MRT FBO already has), and no multi-output shader variant exists
> yet (this backend's `colored3d`/`textured3d`/etc. programs all declare a single `fragColor`
> output, so only `COLOR_ATTACHMENT0` receives a draw under MRT — verified explicitly, not glossed
> over, by `OpenGL4_RenderTargetCube_MRT`'s own Check H). Verified by
> `OpenGL4_RenderTargetCube_MRT` (13/13: two independent cube faces proven not to alias each
> other, a real colored3d draw into a face, a depth-tested face, `MultiSampleCount` fidelity, a
> mipMap round-trip, a real MSAA round-trip, and the MRT slot-0-vs-slot-1 independence proof) plus
> a full re-run of `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D`/
> `OpenGL4_RenderTarget2D` confirming no regression from `SetRenderTarget2D`'s new
> `currentRtCube_` unbind check.
>
> **Status (2026-07-22): `GL4-16` (dynamic `BlendState`/`DepthStencilState`/`RasterizerState`)
> landed and verified.** `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/
> `SetBlendFactor`/`SetScissorRect` are real now — blend factors/equations
> (`glBlendFuncSeparate`/`glBlendEquationSeparate`), `glBlendColor`, real depth+stencil test state
> including two-sided stencil (`glStencilFuncSeparate`/`glStencilOpSeparate`/
> `glStencilMaskSeparate`, `GL4Loader` gained these 3 GL-2.0 entry points), cull mode
> (`glCullFace`/`glFrontFace`), scissor test, and wireframe fill mode (`glPolygonMode`, a real
> desktop-GL capability EasyGL's ES target has to fake by re-expanding triangles into
> `GL_LINES` at draw time — this backend doesn't need that workaround). Since every
> `GraphicsDevice` applies its own default `RasterizerState`/`BlendState`/`DepthStencilState`
> automatically at construction (matching real XNA, not just when a game explicitly assigns one),
> turning this on for real immediately exposed that `GL4-9`/`GL4-13`'s own pre-existing test files
> (`opengl4_3d_test.cpp`, `opengl4_textured3d_test.cpp`) had never been exercised under real
> culling and used quad windings that XNA's actual default (`CullCounterClockwiseFace`) culls —
> both fixed with an explicit `RasterizerState::CullNone` opt-out, the same established idiom
> already used by e.g. `bgfx_basiceffect_texture_enabled_test.cpp` for the identical reason (not a
> new pattern invented here). The new test itself needed two of its own genuine authoring
> corrections before it reflected real behavior: `BlendState::AlphaBlend` assumes an
> already-premultiplied source colour (`One`/`InverseSourceAlpha`) and was misused for a
> straight-alpha blend check (fixed by switching to `BlendState::NonPremultiplied`,
> `SourceAlpha`/`InverseSourceAlpha`), and a wrong assumption that this codebase's `Color::Green`
> is `(0,255,0)` rather than real XNA's `(0,128,0)` (`Lime` is the pure-green one) skewed an
> expected blend result. Verified by the new `OpenGL4_RenderState` CTest (12/12: `BlendState`
> preset/custom/`SetBlendFactor` checks, a `DepthStencilState`-object depth-test check, a real
> 2-pass stencil-buffer check, `CullMode` checked against
> `easygl_rasterizerstate_cullmode_test.cpp`'s own already-empirically-verified triangle winding,
> scissor-rect gating, and wireframe fill mode), plus a full re-run of the other 6 OpenGL4 CTest
> suites confirming everything (including the 2 fixed pre-existing test files) is green.
>
> **Status (2026-07-22): `GL4-17` (real backbuffer MSAA) landed and verified.**
> `GraphicsBackendCreateArgs::multiSampleCount` is honored now, via a manually-managed multisample
> FBO (`msaaFbo_`/`msaaColorRbo_`/`msaaDepthRbo_`, a real `GL_DEPTH24_STENCIL8` combined depth+
> stencil attachment so `GL4-16`'s real stencil test doesn't silently break under backbuffer MSAA)
> resolved into FBO 0 via `glBlitFramebuffer` before `Present()`/`ReadBackbuffer()` — the same
> `CreateMsaaBuffers`/`ResolveMsaa`/`BindDefaultFramebuffer` shape `EasyGLGraphicsBackend` already
> uses for its own backbuffer MSAA, deliberately chosen over
> `SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ...)` (a window pixel-format request this
> backend could not resolve through its own controlled `glBlitFramebuffer` call, and which would
> fight the existing Y-flip/`ReadBackbuffer` conventions). Fixed at backend-construction time only
> — `ApplyMultiSampleCount` is not overridden, the same documented "no way to change after
> construction" limitation `EasyGLGraphicsBackend` already has. `GetMultiSampleCount()` (the
> top-level `IGraphicsBackend` one) now reports the real, `GL_MAX_SAMPLES`-clamped value. Verified
> by the new `OpenGL4_MSAA` CTest (3/3): a diagonal-edge-triangle differential test (matching this
> project's own established MSAA methodology — a solid-fill readback alone cannot distinguish
> "MSAA happened" from "MSAA was silently ignored") shows a hard binary transition with MSAA off
> and genuinely blended intermediate pixels for the identical geometry after
> `GraphicsDevice::RecreateBackendForMultiSampleCount(8)` (the same NOXNA test-only escape hatch
> Vulkan's own pre-Task-902 MSAA tests used, since `GraphicsDeviceManager.PreferMultiSampling` set
> in a `Game` subclass's constructor never reaches the *first* backend construction at all — a
> documented, codebase-wide `GraphicsDevice` architectural constraint, not an OpenGL4-specific
> gap). Plus a full re-run of the other 7 OpenGL4 CTest suites confirming no regression.
>
> **Status (2026-07-22): `GL4-18` (real `Texture2D` mip levels) landed and verified.**
> `OpenGL4TextureBackend::UpdatePixelsLevel()` is real now — `Texture2D::SetData(level, ...)` for
> `level>0` previously reached an unoverridden no-op, silently discarding every mip level beyond
> 0. `GL_TEXTURE_MAX_LEVEL` is clamped to the real requested level count at construction (matches
> `EasyGLTextureBackend`'s own Task-924 fix — otherwise a mip-aware `TextureFilter` treats *any*
> texture, including the overwhelmingly common single-level case, as an incomplete mipmap chain
> and renders solid black, since GL's own default max level is 1000). `FilterToGL`'s mapping table
> also gained real `GL_*_MIPMAP_*` min-filter tokens for every `TextureFilter` `Mip*`
> variant — previously every one of them collapsed to a plain non-mip filter, so even a
> genuinely-uploaded mip chain was never actually sampled from past level 0 regardless of
> minification. `TextureFilter::Point`/`Linear` deliberately keep their existing non-mip-aware GL
> filters, matching `EasyGLGraphicsBackend`'s own identical, documented choice. Verified by the new
> `OpenGL4_Mipmap` CTest (4/4, methodology matching this project's own established Task-298 mip-
> filter test family): a real high mip level is genuinely GPU-selected and its own real uploaded
> content sampled (not "didn't throw") under heavy minification with a mip-aware filter, `Point`
> confirmed to still never mip-select (a known, intentional limitation, not a regression), level 0
> still samples correctly, and an ordinary single-level texture sampled with a mip-aware filter no
> longer renders solid black. The test itself needed the same `Color::Green`-is-`(0,128,0)`-not-
> `(0,255,0)` correction `GL4-16` already found once. Full re-run of the other 8 OpenGL4 CTest
> suites confirms no regression.
>
> **Status (2026-07-22): `GL4-19` (`AlphaTestEffect` + `DualTextureEffect`) landed and verified.**
> Both effects reuse `GL4-13`'s existing `textured3d` (stride 20)/`colored_textured3d` (stride 24)
> programs — neither needed a new stride case, only new uniforms folded into the two existing
> fragment shaders: `uAlphaTest` (a `vec4` — reference value/tolerance/pass-weight/fail-weight,
> ported from `VulkanGraphicsBackend`'s own `alpha_test3d.frag.glsl` discard ternary) and
> `uTexture2`/`uDualTextureEnabled` (a second sampler bound to texture unit 1 via
> `BindProgramForStride`'s new `hasTexture1` block, mirroring the existing `hasTexture0` one). The
> dual-texture blend is the real XNA/D3D `DualTextureEffect.fx` "2x-modulate" lightmap-style
> formula — `tex1.rgb *= 2.0; result = tex1 * tex2 * diffuseColor` — cross-verified against both
> `VulkanGraphicsBackend`'s `dual_texture3d.frag.glsl` and `EasyGLGraphicsBackend`'s own current
> inline GLSL source (`EnsureDualTextured3DProgram()`) before writing any OpenGL4 code, not trusted
> from a single source. Verified by the new `OpenGL4_AlphaTestDualTexture` CTest (8/8): a real GPU
> discard proof swept across `CompareFunction::Always`/`Never`/`LessEqual`/`Equal`, and four
> `DualTextureEffect` colour-combination checks proving `tex1`, `tex2`, and `diffuseColor` each
> genuinely contribute (including a decisive yellow×cyan→green case that only passes if both
> texture slots multiply simultaneously). The test's own first draft had one authoring mistake, not
> a backend bug: a white/white check assumed `diffuseColor` would pass through unchanged, but the
> real 2x-modulate formula makes `tex1(1.0)*2*tex2(1.0)=2.0`, which clamps to full brightness on
> write and masks `diffuseColor`'s own value — fixed by using a mid-gray second texture so
> `tex1*2*tex2~=1.0` (identity), letting `diffuseColor` pass through at its own intensity as
> intended. Full re-run of the other 9 OpenGL4 CTest suites confirms no regression.
>
> **Status (2026-07-22): `GL4-20` (plain `Texture3D`/`TextureCube`) landed and verified.**
> `CreateTexture3D`/`CreateTextureCube` previously fell through to `IGraphicsBackend`'s default
> (returns `nullptr`), so `Texture3D`/`TextureCube` `SetData`/`GetData` silently no-op'd on this
> backend. `OpenGL4Texture3DBackend` allocates a real `GL_TEXTURE_3D` with every mip level
> pre-allocated up front via the newly-loaded `gl4_glTexImage3D` (mip storage must be defined
> before `gl4_glTexSubImage3D`'s box writes can target it — same rationale `GL4-14`/`GL4-15`'s FBO
> render targets already established), and reads back per-Z-slice via a temporary FBO + the
> newly-loaded `gl4_glFramebufferTextureLayer` + `glReadPixels` (desktop GL's `glGetTexImage` was
> an option but can't do sub-rectangle reads; the FBO approach also matches
> `OpenGL4RenderTargetCubeBackend::GetData`'s own established per-face FBO convention).
> `OpenGL4TextureCubeBackend` reuses `GL4-15`'s `GL_TEXTURE_CUBE_MAP_POSITIVE_X+face` arithmetic
> directly, with every face × every mip level pre-allocated via `glTexImage2D`. Both new backends
> add `GL_TEXTURE_MAX_LEVEL` clamping at construction (the same `GL4-18` fix `EasyGLTexture3DBackend`/
> `EasyGLTextureCubeBackend` don't themselves apply — deliberately stricter than the EasyGL
> reference here). `GL4Loader` gained `GL_TEXTURE_3D`, `gl4_glTexImage3D`/`gl4_glTexSubImage3D`
> (GL 1.2 core, not declared by a GL-1.1-vintage `<GL/gl.h>`) and `gl4_glFramebufferTextureLayer`
> (GL 3.0 core). `TextureCube::GetData` deliberately does **not** Y-flip (unlike
> `OpenGL4RenderTargetCubeBackend::GetData`, which flips because it reads back a
> framebuffer-origin render target) — matches `EasyGLTextureCubeBackend::GetData`'s own
> non-flipped convention for a plain texture, verified for real (not assumed) by the new
> `OpenGL4_TextureCube` CTest's Check C (an asymmetric single-corner marker pixel read back at the
> exact corner it was written to). Verified by two new CTest suites: `OpenGL4_Texture3D` (3/3 —
> per-slice round-trip on a 2×2×4 volume with no cross-slice aliasing, a sub-box offset proof, and
> a genuine mip-level-1 storage round-trip) and `OpenGL4_TextureCube` (4/4 — per-face round-trip on
> a size=2 cube with no cross-face aliasing, a sub-rectangle offset proof that doesn't bleed into
> an adjacent face, the no-Y-flip corner-marker proof, and a genuine mip-level-1 storage
> round-trip). Both new tests passed every check on their first real run — no backend or test bugs
> found this time. Full re-run of the other 10 OpenGL4 CTest suites confirms no regression.
>
> **Status (2026-07-22): `GL4-21` (`EnvironmentMapEffect`) landed and verified.**
> A dedicated `env_map3d` GLSL 410 core program (stride 32, same `VertexPositionNormalTexture`
> layout `lit_textured3d` already uses) is selected by `BindProgramForStride` instead of
> `lit_textured3d` whenever `GpuDrawParams::envMapping` is set — no `GpuDrawParams`/
> `IGraphicsBackend.hpp` changes were needed at all (every field `EnvironmentMapEffect::
> FillGpuDrawParams()` populates already existed, added by earlier phases for the other 5
> backends that already implement this effect). Ported near-verbatim from `EasyGLGraphicsBackend
> ::EnsureEnvMapped3DProgram`'s GLSL ES 300 source (per-vertex Fresnel, Gouraud-interpolated —
> kept as-is rather than switching to `VulkanGraphicsBackend`'s per-fragment variant, since EasyGL
> is the closer sibling GLSL backend to port from), cross-checked against `VulkanGraphicsBackend`'s
> own `env_map3d.frag.glsl` for the exact formula: reflection vector `reflect(-eyeVector,
> worldNormal)`, Fresnel blend factor `pow(max(1-|dot(eye,normal)|,0),FresnelFactor)*
> EnvironmentMapAmount`, and critically a **lerp** (not additive) blend between the lit
> diffuse×texture colour and the alpha-scaled cubemap sample, plus a separately alpha-scaled
> specular term — `docs/environmentmapeffect-support.md` documents these as the two real formula
> bugs (additive-not-lerp, missing alpha scaling) found and fixed while porting this effect to 3
> other backends, so this OpenGL4 port used the already-corrected formula from the start rather
> than rediscovering them. The cube map binds to texture unit 1 (`GL4-19`'s `DualTextureEffect`
> texture1 slot, reused safely since the two effects are mutually exclusive per draw). Verified by
> the new `OpenGL4_EnvironmentMapEffect` CTest (4/4): Check A reuses Task 399's own
> cross-backend-verified combined-scene oracle verbatim (same texture/cube/emissive/specular/
> Fresnel/World/View/Projection setup as `easygl_environmentmapeffect_golden_test.cpp`, same
> expected `(151,101,76)` ± 20 tolerance) — the strongest possible correctness proof for a 4th
> backend port, and it passed on the very first run at `(131,91,71)`, comfortably within
> tolerance. Checks B–D isolate individual terms: `EnvironmentMapAmount=0` making the cube map's
> own colour provably irrelevant, `EnvironmentMapAmount=1`/`FresnelFactor=0` with a zeroed base
> colour producing an exact, fully-opaque pass-through of the cube map's colour, and a non-zero
> `EnvironmentMapSpecular` provably rendering brighter than zero. All 4 checks passed on the first
> real run — no backend or test bugs found. Full re-run of the other 12 OpenGL4 CTest suites
> confirms no regression.
>
> **Status (2026-07-22): `GL4-22` (`SkinnedEffect`) landed and verified.**
> A dedicated `skinned3d` GLSL 410 core program (**new** strides 52 and 56 —
> `VertexPositionNormalTextureSkinned` and that layout plus a trailing `Color`) is selected by
> `BindProgramForStride`. `OpenGL4VertexBufferBackend::ApplyLayout` gained the matching stride-52/
> 56 attribute cases (position/normal/UV/blend-weight as `vec3`/`vec3`/`vec2`/`vec4`, blend-indices
> as a genuine GLSL integer attribute via the newly-loaded `gl4_glVertexAttribIPointer` — plain
> `glVertexAttribPointer`'s implicit int-to-float conversion would be wrong for values used to
> subscript `uBones[]`, not blended as floats). Ported near-verbatim from `EasyGLGraphicsBackend
> ::EnsureSkinnedProgram`'s GLSL ES 300 source, which already matches real XNA `SkinnedEffect.fx`'s
> `Skin()` function: the skin matrix is the sum of only the first `WeightsPerVertex` (1, 2, or 4)
> `uBones[index]*weight` pairs — never all 4 unconditionally (`Task 895`, a real bug already found
> and fixed on the other backends; this port used the corrected formula from the start). The
> lighting formula reuses `lit_textured3d`'s own already-correct 3-light Lambertian-diffuse +
> Blinn-Phong-specular + `EmissiveColor` formula, plus a vertex-colour modulate gated by
> `VertexColorEnabled` for the stride-56 layout. No fog (same deliberate deferral as every other 3D
> stride variant on this backend). All 72 bone matrices upload via a single
> `gl4_glUniformMatrix4fv(loc, params.boneCount, GL_FALSE, params.boneTransforms)` call — no
> `GpuDrawParams`/`IGraphicsBackend.hpp` changes were needed, `boneTransforms`/`boneCount`/
> `weightsPerVertex` already existed from earlier phases that implemented this effect on 5 other
> backends. Verified by the new `OpenGL4_SkinnedEffect` CTest (5 checks across 4 scenarios): an
> identity-bone no-op sanity check, a single-bone `Translate` genuinely displacing geometry, a
> two-bone 0.5/0.5 weighted blend reaching the identical net shift a single bone alone could not
> reach (decisive proof both weighted bones contribute, not just one), and `VertexColorEnabled`
> genuinely gating the stride-56 `aColor` attribute (reusing the exact `(174,0,0)` cross-backend
> oracle `easygl_skinnedeffect_vertexcolor_test.cpp` already established). All checks passed on the
> first real run — no backend or test bugs found. Full re-run of the other 13 OpenGL4 CTest suites
> confirms no regression.
>
> **Status (2026-07-22): `GL4-23` (`PbrEffect`) landed and verified — the last remaining built-in
> XNA/CNA effect for this backend.** Two new dedicated programs on two brand-new strides:
> `pbr3d` (stride 48, `VertexPositionNormalTangentTexture`) for plain `PbrEffect`, and
> `pbr_skinned3d` (stride 68, PBR + bone skinning combined) for `SkinnedPbrEffect`, sharing the
> same fragment shader (only the vertex stage differs — the skinned variant skins position,
> normal, *and* tangent through the blended bone matrix before the identical BRDF runs).
> `OpenGL4VertexBufferBackend::ApplyLayout` gained the matching stride-48/68 attribute cases
> (tangent as a real `vec4`, `xyz` + bitangent-handedness sign in `w`; stride 68 appends
> blend-weight/blend-indices after the stride-48 layout, same "append, don't insert" precedent
> `GL4-22`'s stride-52→56 case already established). Ported near-verbatim from
> `EasyGLGraphicsBackend::EnsurePbrProgram()`'s GLSL ES 300 source — the real glTF 2.0 spec's own
> reference metallic-roughness BRDF (GGX normal distribution, Smith-Schlick-GGX visibility, Schlick
> Fresnel) — cross-checked against `VulkanGraphicsBackend`'s `pbr3d.frag.glsl` and
> `BgfxGraphicsBackend`'s `fs_pbr3d.sc` (both byte-for-byte identical `PbrLight()` math) before
> writing any OpenGL4 code. 5 texture units (0=base colour, 1=normal, 2=metallic-roughness,
> 3=emissive, 4=occlusion) are sampled *unconditionally* every fragment — unlike
> `DualTextureEffect`/`EnvironmentMapEffect`'s uniform-gated optional samplers — so two new lazily
> created 1×1 fallback textures (`defaultWhiteTexture_`/`defaultFlatNormalTexture_`) are bound
> whenever the corresponding `GpuDrawParams::pbr*Map` pointer is null, matching
> `EasyGLGraphicsBackend::BindDrawParams`'s own fallback convention. No `GpuDrawParams`/
> `IGraphicsBackend.hpp` changes were needed — every PBR field already existed from earlier phases
> that implemented this effect on 5 other backends. Verified by the new `OpenGL4_PbrEffect` CTest
> (4/4), reusing `easygl_pbreffect_golden_test.cpp`'s own independently hand-derived+captured
> 4-quad scene and expected pixel values verbatim (white/rough/non-metallic baseline, a tilted
> normal map proven to render measurably darker, and fully-metallic-red vs fully-dielectric-red
> proven to render measurably differently) — the strongest possible correctness proof for a
> 5th/6th backend port of the same BRDF formula. **One real bug found and fixed**: the very first
> PBR draw of a process (the only time `EnsureDefaultWhiteTexture()`/
> `EnsureDefaultFlatNormalTexture()` actually create anything) rendered near-black instead of the
> expected lit colour — both `Ensure*` functions do their own `glBindTexture`/final
> `glBindTexture(GL_TEXTURE_2D, 0)` on whatever GL texture unit is *currently active*, and they
> were being called **after** the base-colour texture had already been bound to unit 0, so their
> trailing unbind clobbered/unbound it; every later PBR draw was unaffected since both functions
> early-return once created. Fixed by moving both `Ensure*` calls to the very top of
> `BindProgramForStride`, before *any* real per-draw texture gets bound. Full re-run of the other
> 14 OpenGL4 CTest suites confirms no regression.
>
> **Status (2026-07-22): `GL4-24` (real occlusion queries) landed and verified.**
> `OpenGL4OcclusionQueryBackend` wraps a genuine GL 1.5 core query object using
> `GL_SAMPLES_PASSED` — an **exact** passed-sample count — unlike `EasyGLOcclusionQueryBackend`'s
> GLES3 `GL_ANY_SAMPLES_PASSED` (0/1-only) query; this matches real XNA's own desktop
> `OcclusionQuery.PixelCount()` semantics (see `IOcclusionQueryBackend`'s own doc comment
> contrasting the two). `GL4Loader` gained the GL 1.5 query entry points (`glGenQueries`/
> `glDeleteQueries`/`glBeginQuery`/`glEndQuery`/`glGetQueryObjectuiv`) plus the
> `GL_SAMPLES_PASSED`/`GL_QUERY_RESULT`/`GL_QUERY_RESULT_AVAILABLE` tokens. `IsComplete()` polls
> `GL_QUERY_RESULT_AVAILABLE` and caches the real result once ready (no busy-wait, no forced
> synchronization); `PixelCount()` returns the cached exact count, `0` before the result is ready.
> Verified by the new `OpenGL4_OcclusionQuery` CTest (6/6), porting
> `vulkan_occlusionquery_pixelcount_test.cpp`'s own already-verified 3-scenario methodology (a
> fully visible quad reporting a positive count, a nearer opaque occluder making the target read
> exactly `0` via the real depth test, and — the most discriminating check — two non-overlapping
> half-quads drawn within a single `Begin()`/`End()` span summing to the *same* total a single
> full-quad draw would produce, proving the query isn't just capturing the last draw in its span).
> All 6 checks passed on the first real run — no backend or test bugs found. Full re-run of the
> other 15 OpenGL4 CTest suites confirms no regression.
>
> **Status (2026-07-22): `GL4-25` (real fog) landed and verified.**
> `GpuDrawParams::fogEnabled`/`fogColor`/`fogStart`/`fogEnd` are now read by all 7
> GpuDrawParams-driven stride/dispatch shaders: `textured3d`, `colored_textured3d`,
> `lit_textured3d`, `env_map3d`, `skinned3d`, `pbr3d`/`pbr_skinned3d`, and a **brand-new**
> `coloredParams3d` (see below). Every vertex shader computes `vFogFactor` from Task 1111's own
> already cross-backend-verified formula (matches FNA's `EffectHelpers.SetFogVector`/`Common.fxh`
> `ComputeFogFactor` exactly when `World`/`View` are identity, the scenario every CNA fog
> test/scene uses) from the vertex's **pre-transform, pre-skin** `aPos.z`, ported near-verbatim
> from `EasyGLGraphicsBackend`'s own per-program fog blocks; every fragment shader does a final
> `mix(uFogColor, colour, vFogFactor)`.
>
> **A real, separate parity gap was found and closed along the way, not just fog**:
> `BindProgramForStride` had no `case 16:` (`VertexPositionColor`) at all, so *any* stride-16 draw
> issued via a real `Effect.Apply()` silently fell back to the entirely `GpuDrawParams`-free
> `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` path — `DiffuseColor`/
> `VertexColorEnabled`/`AlphaTest`/fog were all unavailable to it, unlike every other stride (this
> was invisible in every prior test because those all left `DiffuseColor` at its default
> `(1,1,1,1)` and `VertexColorEnabled` at `true`, the one combination where the params-free
> pass-through happens to look correct). A **new**, separate `coloredParams3DProgram_` (kept
> distinct from the legacy `colored3DProgram_`, which stays reserved for
> `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`'s own params-free callers — reusing the
> same program+uniform-name contract for both would have left `DrawColoredPrimitives`'s calls
> silently rendering black, since GLSL uniforms never explicitly set default to zero) now backs a
> real `case 16:`, ported from `EasyGLGraphicsBackend::EnsureColored3DProgram` — closing this gap
> AND enabling fog for `VertexPositionColor` draws in the same change.
>
> Verified by the new `OpenGL4_Fog` CTest (8/8): Checks A–C port
> `easygl_basiceffect_fog_test.cpp`'s own already-cross-backend-verified 3-scenario oracle verbatim
> for the new `coloredParams3d` program (fog off → pure geometry colour; 50% fog → an exact purple
> blend; full fog → exact fog colour). Checks D–H use a simpler but still-exact, effect-agnostic
> proof for the remaining 5 shader families: `mix(fogColor, colour, 0) == fogColor` **exactly**,
> regardless of what the effect's own lit/textured/BRDF math would otherwise produce — placing
> each quad's Z exactly at `FogStart` (fog factor exactly `0`) and using a fog colour (cyan) none
> of the effects' own palettes could accidentally produce predicts an exact pixel match without
> needing to hand-derive each effect's own lit/textured/BRDF result under fog; all 5 matched
> `(0,255,255)` exactly. All 8 checks passed on the first real run — no backend or test bugs found
> in the fog formula itself (only the pre-existing stride-16 gap, described above). Full re-run of
> the other 16 OpenGL4 CTest suites confirms no regression from the new stride-16 dispatch path.
>
> **Status (2026-07-22): `GL4-26` (real dynamic `SamplerState` for direct 3D draws) landed and
> verified.** A real bug, found by inspection while scoping this task (not by a failing test):
> `BindProgramForStride` was unconditionally calling `ApplySamplerState(slot, 0, 1, 1, 1)` (Linear
> + hardcoded Clamp) for every bound texture unit — but `GraphicsDevice::applySamplerStatesToBackend()`
> (the shared XNA layer, not backend-specific) already calls `backend_->ApplySamplerState(slot,
> realFilter, realAddressU, realAddressV, realMaxAnisotropy)` for **all 16 sampler slots**,
> reading each slot's real `GraphicsDevice.SamplerStates[slot]` value, immediately before every
> single `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` call reaches this function (every call site
> in `GraphicsDevice.cpp` pairs the two calls back to back — confirmed by direct inspection, not
> assumed). `BindProgramForStride`'s own hardcoded calls ran *after* that and silently clobbered
> the real, already-correct value on every single direct 3D draw. Worse: real XNA's own
> `SamplerState` default is `Linear`+**`Wrap`** (`SamplerState.cpp`'s own default constructor,
> confirmed), not `Clamp` — so the old hardcoded value was not just non-dynamic but the wrong
> default too, for every draw that never touched `GraphicsDevice.SamplerStates` at all. Fixed by
> simply **deleting** the 7 redundant/incorrect override call sites (texture0, texture1, envMap,
> and the 4 PBR texture units) — nothing needed to be added, since the correct mechanism already
> existed and just needed to stop being overwritten. This matches
> `EasyGLGraphicsBackend::BindDrawParams`'s own established convention (confirmed by inspection:
> it never calls its own `ApplySamplerState` during a 3D draw dispatch either, relying solely on
> the same upstream `GraphicsDevice` call) — this fix brings `OpenGL4` into alignment with EasyGL,
> not a divergence from it.
>
> Verified by the new `OpenGL4_SamplerState` CTest (3/3), porting
> `easygl_sampler_state_effect_test.cpp`'s own already-cross-backend-verified Wrap-vs-Clamp proof
> verbatim (a 2-texel red/green pattern texture, UV spanning `u=[0,2]`, sampled at `u=1.25` —
> `SamplerState::PointWrap` wraps to the pattern's own red texel, `SamplerState::PointClamp`
> clamps to its green edge), plus a new Check B specifically proving the untouched *default*
> `SamplerState` now also reads Wrap (red), not the old hardcoded Clamp (green) — the decisive
> proof this backend's own default was wrong before, not just that explicit assignment works. All
> 3 checks passed on the first real run once the fix landed. Full re-run of the other 17 OpenGL4
> CTest suites confirms no regression from the corrected default (no existing test depended on the
> old, incorrect Clamp default).
>
> **Status (2026-07-22): `GL4-27` (real `GpuDrawParams::baseVertex` support) landed and verified.**
> `DrawIndexedPrimitivesEx` now calls the real GL 3.2 core `glDrawElementsBaseVertex` entry point
> (newly loaded via `GL4Loader`, resolved through the same `SDL_GL_GetProcAddress` mechanism as
> every other GL 1.2+ entry point this backend uses) instead of plain `glDrawElements` —
> `params.baseVertex` is added to every fetched index before it indexes into the currently bound
> vertex buffer, letting multiple sub-meshes share one large vertex buffer with per-draw,
> index-space-relative indices (matches FNA's own D3D9/OpenGL `baseVertex` parameter exactly).
> Previously `params.baseVertex` was silently ignored entirely (always treated as `0`); the field
> already defaults to `0`, so this is a genuine no-op for every existing draw that never set it —
> confirmed by the full regression re-run below.
>
> Verified by the new `OpenGL4_BaseVertex` CTest (2/2): a single shared 8-vertex
> `VertexPositionColor` buffer holds two quads (vertices `[0..3]` = RED on NDC's left half,
> vertices `[4..7]` = BLUE on NDC's right half), and a single shared 6-index buffer holds one
> quad's worth of *local* indices (`{0,1,2,0,2,3}`), reused **unchanged** for both draws — only
> `baseVertex` differs (`0` then `4`). If `baseVertex` were still silently ignored, the second draw
> would incorrectly re-fetch vertices `[0..3]` again (the LEFT-half RED quad's own data), leaving
> the right half at the background colour instead of BLUE — a decisive, unambiguous distinction
> between "honored" and "ignored" with no exact-value derivation needed. Both checks passed on the
> first real run — no backend or test bugs found. Full re-run of the other 18 OpenGL4 CTest suites
> confirms no regression.
>
> **A separate, newly-discovered gap noted for a future task, not fixed here (out of this task's
> scope):** `OpenGL4IndexBufferBackend::IsThirtyTwoBit()` unconditionally returns `false` and
> `DrawIndexedPrimitivesEx`/`DrawColoredPrimitives`' index-buffer paths hardcode
> `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` everywhere — this backend has **no 32-bit index buffer
> support at all** (`CreateIndexBuffer32` isn't overridden either), unlike every other established
> backend. Not attempted as part of `GL4-27` since it's materially larger (new backend class, new
> `IGraphicsBackend` overrides, a new GL4Loader entry for `GL_UNSIGNED_INT` is already available
> without a loader addition since it's GL 1.1, but the index-buffer class hierarchy itself needs
> real new work) and unrelated to `baseVertex` itself.
>
> **Deliberately independent of EasyGL/`easy-gl`.** EasyGL requests
> `SDL_GL_CONTEXT_PROFILE_ES` (OpenGL ES 3.0 / WebGL2 — see `EasyGLGraphicsBackend`'s
> constructor), not a real desktop OpenGL 4.x core profile: no geometry/tessellation shaders, no
> desktop-only `GL_ARB_*` features, and `glGetString(GL_VERSION)` never reports "4.x" under that
> context. This backend requests `SDL_GL_CONTEXT_PROFILE_CORE` (4.1 minimum — the highest core
> version macOS's own driver ever exposes) and never touches `easy-gl` or the `metagl`/`easygl::`
> wrapper library it's built on. Its own hand-rolled loader (`GL4Loader.hpp`/`.cpp`) resolves the
> ~40 GL 1.2+ entry points a core-profile program needs (buffers, VAOs, shaders/programs,
> `glActiveTexture`, separate blend funcs, sampler objects) via `SDL_GL_GetProcAddress` — no
> third-party GL-loader dependency (no glad/GLEW vendored), matching this project's existing
> "zero new third-party dependency" preference for a from-scratch native backend (see
> `plan_sdlgpu.md`'s own "Why an SDL GPU backend" rationale).
>
> **Status (2026-07-22): `GL4-28` (real `TransformWindowToLogical`/`TransformLogicalToWindow`)
> landed and verified.** Both were previously unoverridden (inherited `IGraphicsBackend`'s default
> no-op `return false`), so `Mouse::SetPosition` (which calls `TransformLogicalToWindow` to place
> the OS cursor) and `SdlInputBridge` (which calls `TransformWindowToLogical` to map incoming
> physical mouse events to logical coordinates) silently failed to scale coordinates on this
> backend whenever a non-default virtual resolution was in play. Ported
> `EasyGLGraphicsBackend`'s own pure-uniform-scale (no offset) formula exactly: `scale =
> virtualHeight_ / physicalWindowHeight`, exact for this backend's own default
> `FixedHeightDynamicWidth` presentation, where the logical viewport always fills the whole
> physical window (no letterbox bars to offset for).
>
> Verified by the new `OpenGL4_TransformCoords` CTest (4/4): a 64x64 physical window with
> `SetVirtualResolution(128, 128)` called directly (a deterministic 2x scale, independent of any
> DPI/fullscreen-specific Xvfb behavior) proves both directions at a centre point (Check A/C) and a
> non-centre point (Check B/D), showing a genuine scale (not an accidental identity pass-through)
> and an exact round-trip inverse. All 4 checks passed on the first real run — no backend or test
> bugs found. Full re-run of the other 19 OpenGL4 CTest suites confirms no regression (20/20 total).
>
> **Status (2026-07-22): `GL4-29` (real `PreferPerPixelLighting` vertex-lit shader variant)
> landed and verified.** `GpuDrawParams::preferPerPixelLighting` was previously read by no shader
> on this backend — `lit_textured3d`/`skinned3d` always rendered per-pixel regardless of its value,
> the opposite of real XNA's own default (`BasicEffect`/`SkinnedEffect` both default
> `PreferPerPixelLighting=false`, i.e. per-vertex/Gouraud-shaded lighting). Added two new dedicated
> per-vertex-lit programs, `litTextured3DVertexLitProgram_` (stride 32) and
> `skinned3DVertexLitProgram_` (stride 52/56), ported from
> `EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()`/`EnsureSkinnedVertexLitProgram()`'s GLSL ES
> 300 source (desktop GLSL 410 core translation only) — identical Blinn-Phong math to the existing
> per-pixel programs, just moved into the vertex stage and Gouraud-interpolated via new
> `vLitRGB`/`vSpecularRGB` varyings instead of being re-evaluated per fragment.
> `BindProgramForStride`'s stride-32/52/56 cases now select between the two programs via
> `params.lightingEnabled && !params.preferPerPixelLighting` (XNA's own default gate, matching
> `EasyGLGraphicsBackend::SelectProgram`'s identical dispatch), reusing every existing uniform-set
> call unchanged (both programs share the same uniform names, so the surrounding code just binds a
> local `OpenGL4RawProgram&` reference instead of the previously-hardcoded member).
>
> Verified by the new `OpenGL4_PreferPerPixelLighting` CTest (6/6), reusing
> `easygl_basiceffect_preferperpixellighting_test.cpp`'s/
> `easygl_skinnedeffect_preferperpixellighting_test.cpp`'s own exact scene and analytically
> re-derived expected values verbatim (a shared-normal quad whose centre pixel sits exactly on the
> Gouraud-interpolation seam, discriminating cleanly between the vertex-lit average (~127,127,127)
> and a fresh per-fragment evaluation) — since this port uses the identical formula, the same oracle
> applies. Checks A–C cover `BasicEffect`, D–F cover `SkinnedEffect`; all 6 passed on the first real
> run (`(127,127,127)`/`(152,152,152)`/differs, both effects) — no backend or test bugs found. Full
> re-run of the other 20 OpenGL4 CTest suites confirms no regression (21/21 total).
>
> **Status (2026-07-22): `GL4-30` (real custom `ShaderEffect`/`CreateEffectBackend`) landed and
> verified — every item on this branch's original active plan is now done.** `CreateEffectBackend()` was
> previously unoverridden (default returns `nullptr`), so a caller-supplied GLSL vertex/fragment
> source pair had no way to compile on this backend at all. Added `OpenGL4EffectBackend` (a thin
> `IEffectBackend` wrapper around one `OpenGL4RawProgram`, modeled on `EasyGLEffectBackend`'s
> identical shape) plus a `params.customEffectBackend` check at the top of
> `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` (a new local `BindCustomEffectMatrices` helper,
> ported from `EasyGLGraphicsBackend`'s own identical helper): when
> `ShaderEffect::FillGpuDrawParams()` sets `GpuDrawParams::customEffectBackend`, the compiled
> program is bound directly and its `World`/`View`/`Projection` uniforms are set, bypassing
> `BindProgramForStride`'s built-in stride-dispatched shaders entirely — matching the exact
> uniform names every original XNA sample's own `.fx` source already declares.
>
> Verified by the new `OpenGL4_ShaderEffect3D` CTest (2/2), porting
> `easygl_shadereffect_3d_test.cpp`'s own scene, methodology, and expected values exactly (desktop
> GLSL 410 core translation only — no ES precision qualifiers, otherwise identical), constructing
> `ShaderEffect` directly from source strings (no `ContentManager`/`.cnj` indirection needed, since
> `ShaderEffect`'s own constructor already accepts `vertSrc`/`fragSrc` strings). Check A
> (`World=Identity`, N·L=1) and Check B (`World=RotationY(180°)`, same on-screen footprint but a
> flipped world normal, N·L clamped to 0) both passed exactly on the first real run
> (`(200,100,50)`/`(0,0,0)`) — no backend or test bugs found. Full re-run of the other 21 OpenGL4
> CTest suites confirms no regression (22/22 total).
>
> **Scope (deliberate, matching the EasyGL precedent this ports from):** proves the wiring for a
> vertex format this backend already supports (`VertexPositionNormalTexture`, stride 32), not a new
> vertex-attribute-layout mechanism. `SpriteBatch::SetCustomEffect` integration — letting a custom
> `ShaderEffect` drive 2D sprite rendering too, not just 3D `DrawIndexedPrimitives` — is a separate,
> larger feature and remains unoverridden on this backend, same as it is on every other backend
> that has landed `CreateEffectBackend` without it. ⬜ (not attempted, not blocking)
>
> **Status (2026-07-22): `GL4-31` (real 32-bit index buffer support) landed and verified.**
> Discovered as a separate, newly-found gap while scoping `GL4-27` (`baseVertex`), not attempted
> there since it needed real new work: `OpenGL4IndexBufferBackend::IsThirtyTwoBit()` was
> unconditionally `false`, `CreateIndexBuffer32` wasn't overridden (silently fell back to a 16-bit
> buffer via the `IGraphicsBackend` default), and every index-buffer draw path hardcoded
> `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` — this backend had no 32-bit index buffer support at all,
> unlike every other established backend. Fixed by: `OpenGL4IndexBufferBackend` gained a
> `thirtyTwoBit_` flag (set at construction) and real `SetData32`/`SetData32WithOptions` overrides
> (`GL_UNSIGNED_INT` storage); `IsThirtyTwoBit()` now reports the real flag;
> `OpenGL4GraphicsBackend::CreateIndexBuffer32()` is now overridden. Every index-buffer draw call
> site (`DrawIndexedPrimitivesEx`'s `customEffectBackend` branch and its normal
> `BindProgramForStride` branch, `DrawIndexedColoredPrimitives`) now selects
> `GL_UNSIGNED_INT`/`sizeof(uint32_t)` vs `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` from
> `IIndexBufferBackend::IsThirtyTwoBit()` instead of hardcoding the 16-bit path.
>
> Verified by the new `OpenGL4_IndexBuffer32` CTest (3/3), combining `OpenGL4_BaseVertex`'s own
> shared-vertex-buffer/two-draws methodology with a real `IndexElementSize::ThirtyTwoBits`
> `IndexBuffer` instead of a 16-bit one — a genuinely discriminating proof: if 32-bit indices were
> still silently reinterpreted as 16-bit data (the old bug), the 6 `uint32_t` indices' raw bytes
> would be misread as 12 `uint16_t` values, producing wildly wrong/out-of-range vertex fetches
> instead of the intended `{0,1,2,0,2,3}` triangle list, so the quad would not render as a clean
> two-triangle shape at all. Check A confirms the buffer reports `ThirtyTwoBits` (not silently
> downgraded); Checks B/C reuse the baseVertex proof's own left-RED/right-BLUE distinction with
> real 32-bit index data. All 3 checks passed on the first real run — no backend or test bugs
> found. Full re-run of the other 22 OpenGL4 CTest suites confirms no regression (23/23 total).
>
> **Status (2026-07-22): `GL4-32` (real `SpriteBatch::SetCustomEffect` integration) landed and
> verified.** Discovered as a separate, newly-found gap while scoping `GL4-30` (not attempted
> there since it's a separate feature — driving 2D sprite rendering, not 3D
> `DrawIndexedPrimitives`): `OpenGL4SpriteBatchBackend` had no `SetCustomEffect()` override
> (inherited the default no-op), so a custom `ShaderEffect` passed to `SpriteBatch::Begin()` was
> silently ignored — every sprite still rendered with the built-in sprite program regardless.
> Fixed by adding a `customEffect_` field and a real `SetCustomEffect()` override (flushes any
> already-batched sprites under the previous effect before switching, mirroring
> `EasyGLSpriteBatchBackend::SetCustomEffect`'s own guard); `FlushBatch()` now binds the SAME
> compiled program the custom `ShaderEffect` itself owns (`Effect::GetEffectBackendPtr()`,
> overridden by `ShaderEffect`) instead of the built-in sprite program when one is set, calls
> `Effect::Apply()`, and sets `"projection"` — this codebase's established custom-2D-effect
> uniform-name convention (see `easygl_shader_effect_test.cpp`), distinct from the built-in
> program's own private `"uProjection"`/`"uTexture"` internal naming.
>
> Verified by the new `OpenGL4_ShaderEffectSpriteBatch` CTest (2/2), porting
> `easygl_shader_effect_test.cpp`'s own scene, methodology, and expected values exactly (desktop
> GLSL 410 core translation only). A custom shader outputs only the red channel of a sampled
> white texel, applied via `SpriteBatch` to a sprite drawn over a green background. Check A
> (sprite centre reads red-tinted `(255,0,0)`, proving the custom program is genuinely bound and
> driving the draw — the built-in program would leave it plain white) and Check B (a background
> corner outside the sprite's destination rectangle stays unmodified green `(0,255,0)`, proving no
> full-screen side effect) both passed exactly on the first real run — no backend or test bugs
> found. Full re-run of the other 23 OpenGL4 CTest suites confirms no regression (24/24 total).
>
> **Status (2026-07-22): `GL4-33` (real hardware instancing) landed and verified — the project
> owner explicitly asked to continue on OpenGL4 tasks after the final post-`GL4-32` audit found
> this gap.** `OpenGL4GraphicsBackend` previously didn't override `DrawInstancedPrimitivesEx` at
> all (inherited `IGraphicsBackend`'s default, which unconditionally throws
> `std::runtime_error`). `GraphicsDevice::DrawInstancedPrimitives`/`SetVertexBuffers`/
> `VertexBufferBinding` were already fully wired at the XNA API layer — only this backend's own
> implementation was missing. Unlike every other gap closed this session, this needed a real
> prerequisite first: a generic `VertexElement`-driven attribute mapper.
> `OpenGL4VertexBufferBackend` gained `SetVertexDeclaration()`/`GetDeclarationElements()` (Task
> 1080-equivalent, ported from `EasyGLVertexBufferBackend`'s identical shape) and `ApplyLayout()`
> gained a new generic binding path (attribute location = the element's own index in the
> declaration), used whenever a `VertexDeclaration` was supplied (via `VertexBuffer::SetDataRaw()`,
> already wired at the XNA layer) instead of matching one of the fixed byte-strides the existing
> switch recognizes — needed because a per-instance attribute buffer never matches those.
> `DrawInstancedPrimitivesEx` (with a custom `ShaderEffect`) binds the per-instance buffer's own
> attributes generically into the mesh buffer's VAO, continuing at locations right after the mesh
> buffer's own declared attributes, each with `glVertexAttribDivisor(location, 1)`, then calls the
> real GL 3.1 core `glDrawElementsInstanced` (both newly loaded via `GL4Loader`, along with a
> `GL_HALF_FLOAT` token guard for the element-format mapper's `HalfVector2`/`HalfVector4` cases) —
> matches `EasyGLGraphicsBackend::DrawInstancedPrimitivesEx`'s own Task 1082 shape exactly. Also
> added a shared `VertexElementFormat` alias to `IGraphicsBackend.hpp` (purely additive, matching
> the existing `VertexElement`/`PrimitiveType`/etc. alias pattern already there) since no backend
> had previously needed to name that type outside its own translation unit.
>
> Verified by the new `OpenGL4_InstancedModel` CTest (2/2), porting
> `easygl_instancedmodel_shader_test.cpp`'s own scene, packing derivation, and expected values
> exactly (desktop GLSL 410 core translation only). A single quad mesh is drawn twice in ONE
> `DrawInstancedPrimitives` call, driven by 2 instances' own 4x4 transforms supplied as 4
> consecutive per-instance `vec4` attributes (the classic D3D9 hardware-instancing convention).
> Check A (instance 0, pure translation, faces the light — pure white `(255,255,255,255)`) and
> Check B (instance 1, 180° Y-rotation then translation, faces away — dim gray `(64,64,64,255)`)
> both matched exactly on the first real run: two different colors at two different on-screen
> positions from one draw call, proving the per-instance data is genuinely read per-instance
> (`glVertexAttribDivisor=1`), not per-vertex or left constant. Full re-run of the other 24
> OpenGL4 CTest suites confirms no regression (25/25 total).
>
> **Status legend:** ✅ implemented *and verified against its stated acceptance criteria*;
> 🟨 code or documentation exists but has not met those criteria; ⬜ not implemented.
>
> **Platform scope until expanded by a completed task:** Linux desktop (X11, verified under both
> a real X11 session's driver and Xvfb's software/llvmpipe GL 4.5 implementation), x86_64.
> Windows and macOS are code paths only (the `SDL_GL_CONTEXT_MAJOR/MINOR_VERSION`/
> `SDL_GL_CONTEXT_PROFILE_MASK` attributes and the loader are portable, and macOS's 4.1 ceiling is
> the reason this backend requests 4.1 rather than a higher minimum), not validation claims.
>
> **Every built-in XNA/CNA effect is now implemented on this backend** (`BasicEffect`/
>   `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`PbrEffect`/
>   `SkinnedPbrEffect`, `GL4-13`/`GL4-19`/`GL4-21`/`GL4-22`/`GL4-23`) — remaining gaps below are all
>   NOXNA extensions or known simplifications, not missing built-in effect coverage.
> - ~~**Custom `ShaderEffect`**~~ (NOXNA) — done, `GL4-30` (2026-07-22). New
>   `OpenGL4EffectBackend`/`params.customEffectBackend` dispatch, verified by
>   `OpenGL4_ShaderEffect3D` (2/2). ✅
> - ~~**`SpriteBatch::SetCustomEffect` integration**~~ (NOXNA) — done, `GL4-32` (2026-07-22). New
>   `customEffect_` dispatch in `OpenGL4SpriteBatchBackend::FlushBatch`, verified by
>   `OpenGL4_ShaderEffectSpriteBatch` (2/2). ✅
> - ~~**`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` `params.baseVertex`**~~ — done, `GL4-27`
>   (2026-07-22). Real `glDrawElementsBaseVertex` call, verified by `OpenGL4_BaseVertex` (2/2). ✅
> - ~~**`SamplerState` for direct 3D draws**~~ — done, `GL4-26` (2026-07-22). Real bug found+fixed
>   (hardcoded Clamp silently overwrote the real, already-correctly-applied per-slot
>   `SamplerState` on every draw); verified by `OpenGL4_SamplerState` (3/3). ✅
> - ~~**No 32-bit index buffer support at all**~~ — done, `GL4-31` (2026-07-22, discovered while
>   scoping `GL4-27`). Real `SetData32`/`GL_UNSIGNED_INT` support, verified by
>   `OpenGL4_IndexBuffer32` (3/3). ✅
> - ~~**`preferPerPixelLighting`**~~ — done, `GL4-29` (2026-07-22). New
>   `litTextured3DVertexLitProgram_`/`skinned3DVertexLitProgram_` per-vertex-lit programs, selected
>   by `BindProgramForStride` when `params.lightingEnabled && !params.preferPerPixelLighting`
>   (XNA's own default), verified by `OpenGL4_PreferPerPixelLighting` (6/6). ✅
> - ~~**Fog**~~ — done, `GL4-25` (2026-07-22). All 7 `GpuDrawParams`-driven stride/dispatch shaders
>   (`coloredParams3d`/`textured3d`/`colored_textured3d`/`lit_textured3d`/`env_map3d`/`skinned3d`/
>   `pbr3d`) now read `GpuDrawParams::fogEnabled`/`fogColor`/`fogStart`/`fogEnd`, verified by
>   `OpenGL4_Fog` (8/8). Note: the legacy params-free `colored3DProgram_` (used only by
>   `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`'s own fast path, not by any real
>   `Effect.Apply()`) intentionally still has no fog — it has no `GpuDrawParams` to read fog from
>   at all. ✅
> - ~~**Occlusion queries**~~ — done, `GL4-24` (2026-07-22). Real `GL_SAMPLES_PASSED` queries via
>   `OpenGL4OcclusionQueryBackend`, verified by `OpenGL4_OcclusionQuery` (6/6). ✅
> - ~~**`TransformWindowToLogical`/`TransformLogicalToWindow`**~~ — done, `GL4-28` (2026-07-22).
>   Real pure-uniform-scale mapping (ported from `EasyGLGraphicsBackend`), verified by
>   `OpenGL4_TransformCoords` (4/4). ✅
> - **`DebugSimulateContextLoss`/`DebugRestoreContext`/`SetContextRecoveryEnabled`** — **explicitly,
>   permanently deferred (2026-07-22 project-owner decision)**, not overridden (safe no-op
>   defaults). EasyGL's own `metagl`-based `RecoverableResource` mechanism is a cross-cutting base
>   class touching nearly every GPU resource backend (textures, render targets, buffers, occlusion
>   queries, sprite batch) — a much larger retrofit than every other item on this list, and not
>   worth the ROI for this backend right now. Same permanent-deferral status as
>   `Headless`/`Software`/`SDL_gpu` already have for their own reasons — treat this the same way:
>   do not pick this up without a fresh, explicit go-ahead. ⬜ (permanently deferred, not a pick-up
>   candidate)
> - **Windows/macOS validation** — code paths only (see Platform scope above), not run on real
>   hardware yet. Not reachable from this development environment (no Windows/macOS machine
>   available) — not a pick-up candidate here either. ⬜ (environment-blocked)
> - ~~**Hardware instancing (`DrawInstancedPrimitivesEx`)**~~ — done, `GL4-33` (2026-07-22). New
>   generic `VertexElement`-driven attribute mapper (`OpenGL4VertexBufferBackend::
>   SetVertexDeclaration`/`GetDeclarationElements`) plus real `glDrawElementsInstanced`/
>   `glVertexAttribDivisor` dispatch, verified by `OpenGL4_InstancedModel` (2/2). ✅

---

## Why a real OpenGL 4 backend

- **EasyGL genuinely cannot do this.** EasyGL's context is OpenGL ES 3.0 (`SDL_GL_CONTEXT_PROFILE_ES`),
  chosen specifically because it doubles as the WebGL2 target for Emscripten builds (see
  `cmake/BackendSelection.cmake`'s own "EasyGL is the default on Linux and Emscripten (WebGL 2 =
  OpenGL ES 3.0)" comment). ES 3.0 and desktop GL 4.x are related but distinct APIs — no geometry/
  tessellation shaders, a narrower set of texture formats/compression, different (stricter) GLSL
  ES shading language rules, and no access to desktop-only `GL_ARB_*` extensions. A user or task
  that specifically wants real desktop OpenGL 4.x (e.g. to reach features ES doesn't have, or to
  match a specific driver/GPU debugging workflow that only understands desktop GL) cannot get that
  through `CNA_GRAPHICS_BACKEND=EASYGL` no matter what — a genuinely different backend is required.
- **Zero new third-party dependency**, same reasoning as `SDL_GPU`: the platform's own GL library
  (`libGL`/`opengl32`/`OpenGL.framework`, resolved via CMake's built-in `find_package(OpenGL)`)
  plus SDL3 (already vendored) is everything this backend links against. `GL4Loader.hpp`/`.cpp` is
  a small, hand-rolled loader for the handful of GL 1.2+ entry points a core-profile program needs
  — not a vendored copy of glad/GLEW/SDL's own bundled `SDL_opengl_glext.h`.
- **Consistent with the existing multi-backend architecture.** This is simply another
  `CNA_GRAPHICS_BACKEND` value following the exact same `IGraphicsBackend` contract every other
  backend already implements (`include/CNA/Internal/Backends/Common/IGraphicsBackend.hpp`).

This plan does **not** propose retiring any existing backend, least of all EasyGL.

---

## Naming conventions for this backend

| Item | Value |
| --- | --- |
| `CNA_GRAPHICS_BACKEND` value | `OPENGL4` |
| CMake option | `CNA_BACKEND_OPENGL4` |
| Compile definition | `CNA_BACKEND_OPENGL4` |
| Backend directory | `src/CNA/Internal/Backends/OpenGL4/`, `include/CNA/Internal/Backends/OpenGL4/` |
| CMake target | `cna_backend_graphics_opengl4` |
| Main class | `CNA::Internal::Backends::OpenGL4::OpenGL4GraphicsBackend` |
| GL loader | `CNA::Internal::Backends::OpenGL4::GL4` (`GL4Loader.hpp`/`.cpp`) |
| Task prefix | `GL4-` |
| CTest labels | `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D`, `OpenGL4_Textured3D`, `OpenGL4_RenderTarget2D`, `OpenGL4_RenderTargetCube_MRT`, `OpenGL4_RenderState`, `OpenGL4_MSAA`, `OpenGL4_Mipmap`, `OpenGL4_AlphaTestDualTexture`, `OpenGL4_Texture3D`, `OpenGL4_TextureCube`, `OpenGL4_EnvironmentMapEffect`, `OpenGL4_SkinnedEffect`, `OpenGL4_PbrEffect`, `OpenGL4_OcclusionQuery`, `OpenGL4_Fog`, `OpenGL4_SamplerState`, `OpenGL4_BaseVertex`, `OpenGL4_TransformCoords`, `OpenGL4_PreferPerPixelLighting`, `OpenGL4_ShaderEffect3D`, `OpenGL4_IndexBuffer32`, `OpenGL4_ShaderEffectSpriteBatch`, `OpenGL4_InstancedModel` (`ctest -R OpenGL4`) |

---

## Phase 1 — task table

| Task | Description | Status | Notes |
| --- | --- | --- | --- |
| `GL4-1` | CMake wiring: `CNA_BACKEND_OPENGL4` option, `BackendSelection.cmake`/`BackendLibraries.cmake` branches, `find_package(OpenGL REQUIRED)`, link `OpenGL::GL` + `SDL3::SDL3`. | ✅ | No new third-party dependency. |
| `GL4-2` | Hand-rolled GL 1.2+ loader (`GL4Loader.hpp`/`.cpp`) for buffers/VAOs/shaders/programs/`glActiveTexture`/blend/sampler-object entry points, via `SDL_GL_GetProcAddress`. | ✅ | All entry points named `gl4_glXxx` to avoid any ambiguity with the pre-1.2 functions linked directly against `libGL`. |
| `GL4-3` | Window/context lifecycle: real `SDL_GL_CONTEXT_PROFILE_CORE` context (4.1 minimum), `GetWindowInternal`/`GetRendererInternal` (null — no `SDL_Renderer`), `GetViewportSize`/`SetVirtualResolution`/`SetPresentationMode`/`SetSwapInterval`. | ✅ | Verified: `glGetString(GL_VERSION)` reports `4.5 (Core Profile) Mesa 25.2.8` on this dev machine. |
| `GL4-4` | `Clear`/`ClearColorAndDepth`/`ClearDepth`/`ClearStencil`/`ClearDepthAndStencil`/`ClearColorAndStencil`/`ClearColorDepthAndStencil`, correctly forcing `glDepthMask`/`glStencilMask` to full-write during the clear itself and restoring the tracked depth-write-enable state afterward (a depth/stencil clear must not be silently masked by a prior `SetDepthWriteEnabled(false)`). | ✅ | |
| `GL4-5` | `SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`. | ✅ | Real `glEnable`/`glDisable(GL_DEPTH_TEST\|GL_BLEND)` + `glDepthMask`. |
| `GL4-6` | `Texture2D` (`OpenGL4TextureBackend`): `glTexImage2D`/`glTexSubImage2D` upload, `UpdatePixels` (row-pitch-aware), `BindGL`. | ✅ | Single mip level only (`GL4-remaining` above). |
| `GL4-7` | `VertexBuffer`/`IndexBuffer` (`OpenGL4VertexBufferBackend`/`OpenGL4IndexBufferBackend`): VBO+VAO pair, stride-keyed attribute layout (16/20/24/32, matching `VertexPositionColor`/`Texture`/`ColorTexture`/`NormalTexture`), 16-bit index buffer. | ✅ | |
| `GL4-8` | `SpriteBatch` (`OpenGL4SpriteBatchBackend`): CPU-side per-quad vertex generation (position/rotation/origin/flip, matching `EasyGLSpriteBatchBackend`'s established math), one dynamic VBO/IBO flushed per texture change, alpha blending, sampler-object-driven filter/address-mode. | ✅ | Real bug found+fixed: `Begin()` used to reset `pendingFilter_`/`pendingAddressU_`/`pendingAddressV_`/`transform_` to defaults, but `SpriteBatch::Begin()` (the public class) calls `SetSamplerFilter`/`SetSamplerAddressMode`/`SetTransformMatrix` on the backend *before* calling `Begin()` — the reset silently discarded every non-default `SamplerState` a caller passed to `SpriteBatch::Begin()`, always rendering as Clamp regardless of the requested `TextureAddressMode`. Found by `OpenGL4_Readback`'s Wrap/Mirror checks (`AddressMode=Clamp` passed "by accident" — it matched the reset default). Fixed by not resetting those fields in `Begin()` at all, matching `EasyGLSpriteBatchBackend::Begin()`'s own precedent (only flips the `begun_` flag). |
| `GL4-9` | `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`: a `colored3d` GLSL 410 core program (`aPos`/`aColor` → `uWorldViewProj`), real depth-test occlusion. | ✅ | Real bug found+fixed: `GraphicsDevice::UpdateViewportFromWindow()` calls `IGraphicsBackend::SetViewport()` after every resize/at device creation; this backend didn't override it (inherited no-op default), so the real GL viewport was never set for the 3D draw path (only `SpriteBatch::FlushBatch()` set it, for its own draws) — every `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives` call rendered nothing (readback saw plain background). Fixed by implementing `SetViewport()` (`glViewport`/`glDepthRange`, Y-flipped to XNA's top-left origin, mirroring `EasyGLGraphicsBackend::SetViewport`'s own fbH-based flip). Found by `OpenGL4_3D`'s pixel-readback checks. |
| `GL4-10` | `ReadBackbuffer` (`glReadPixels` + Y-flip, mirroring `EasyGLGraphicsBackend::ReadBackbuffer`'s own convention) — real pixel-level verification for every check above, not just "didn't throw". | ✅ | |
| `GL4-11` | `ApplySamplerState`: real GL sampler objects (`glGenSamplers`/`glBindSampler`/`glSamplerParameteri`), not texture-object-embedded state — Point/Linear/Anisotropic filter, Wrap/Clamp/Mirror address mode. | ✅ | |
| `GL4-12` | `GraphicsBackendCompileDefinitionTests.cpp`/`GraphicsBackendType.hpp` updated for the new backend (the latter was a genuine second registration point found only by actually attempting a full-library build — `getCurrentGraphicsBackendType()`'s `#error` fires if a backend defines its own `CNA_BACKEND_*` compile definition without a matching branch there). | ✅ | `ExactlyOneGraphicsBackendIsSelected` syntax-checked against this backend's compile definitions; full `CnaTests` link not attempted in this sandboxed session (see Verification methodology below). |
| `GL4-13` | `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`: real stride-keyed dispatch (`BindProgramForStride`) to 3 new GLSL 410 core programs — `textured3d` (stride 20), `colored_textured3d` (stride 24), `lit_textured3d` (stride 32, FNA's `Lighting.fxh` `ComputeLights()` ported from `VulkanGraphicsBackend`'s own `lit_textured3d.vert/frag.glsl`, with a `safeNormalize()` guard against a disabled `DirectionalLight`'s zero-vector `Direction`, the same real bug `plan_webgpu.md` found and fixed independently). Unrecognized strides still fall back to `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`. | ✅ | Added `PixelTestGame::Check(bool, label)` (`examples/common/PixelTestGame.hpp`) — a small, purely-additive generic boolean assertion alongside the pre-existing `ExpectPixel`/`CompareGoldenImage`, needed to assert "the lit render must differ from the unlit render" (not itself a pixel-region compare). |
| `GL4-14` | `RenderTarget2D`: real FBO (`OpenGL4RenderTargetBackend`) — colour texture attachment, optional depth/stencil renderbuffer (`Depth16`/`Depth24`/`Depth24Stencil8`), optional MSAA colour(+depth) renderbuffer resolved via `glBlitFramebuffer` on unbind, optional mip chain regenerated via `glGenerateMipmap` on unbind, real `GetData()` readback via a throwaway per-level read FBO. `GL4Loader` gained the FBO/renderbuffer entry points (`glGenFramebuffers`/`glBindFramebuffer`/`glFramebufferTexture2D`/`glCheckFramebufferStatus`/`glGenRenderbuffers`/`glBindRenderbuffer`/`glRenderbufferStorage(Multisample)`/`glFramebufferRenderbuffer`/`glBlitFramebuffer`/their `glDelete*` counterparts). | ✅ | Two real bugs found+fixed: `SetViewport`'s Y-flip was hardcoded to the window's physical height (wrong once a smaller FBO is bound) — fixed via a new `currentRtHeight_` member, mirroring `EasyGLGraphicsBackend`'s identical pattern. `OpenGL4SpriteBatchBackend::FlushBatch`'s viewport/ortho sizing had the same window-size-only assumption, which would silently break any `SpriteBatch::Draw()` issued while an RT is bound — fixed via a new `GetCurrentRenderTarget2DSize()` accessor, exercised by `OpenGL4_RenderTarget2D`'s own Check J. `RenderTargetCube`/MRT (`SetRenderTargets` plural) are explicitly out of scope for this task — see "Remaining work". |
| `GL4-15` | `RenderTargetCube`: real per-face FBO (`OpenGL4RenderTargetCubeBackend`) — one shared cube-map texture, re-attaching the requested face (`GL_TEXTURE_CUBE_MAP_POSITIVE_X + face`) on `BindAsRenderTargetFace`, same depth/MSAA/mip machinery as `GL4-14`'s 2D target, real per-face `GetData()`. `SetRenderTargets` (plural): real MRT via a persistent multi-attachment FBO (`glFramebufferTexture2D` at `GL_COLOR_ATTACHMENT0+i` per target) + `glDrawBuffers`. `GL4Loader` gained `glDrawBuffers` and the `GL_TEXTURE_CUBE_MAP`/`GL_TEXTURE_CUBE_MAP_POSITIVE_X` tokens. | ✅ | No depth attachment for MRT and no multi-output shader variant (only `COLOR_ATTACHMENT0` receives a draw) — both explicitly verified, not silently assumed, by `OpenGL4_RenderTargetCube_MRT`'s own Check H, and match `EasyGLGraphicsBackend`'s own identical, documented MRT gap. |
| `GL4-16` | Real dynamic `ApplyBlendState`/`ApplyDepthStencilState`/`ApplyRasterizerState`/`SetBlendFactor`/`SetScissorRect` — blend factors/equations (`glBlendFuncSeparate`/`glBlendEquationSeparate`), `glBlendColor`, real depth+stencil (incl. two-sided via `glStencilFuncSeparate`/`glStencilOpSeparate`/`glStencilMaskSeparate`), cull mode, scissor test, wireframe fill mode (`glPolygonMode`). `GL4Loader` gained the 3 two-sided-stencil GL-2.0 entry points, `GL_INCR_WRAP`/`GL_DECR_WRAP`. | ✅ | Since `GraphicsDevice` applies its own default `RasterizerState`/`BlendState`/`DepthStencilState` at construction (matching real XNA), turning cull mode on for real exposed that `opengl4_3d_test.cpp`/`opengl4_textured3d_test.cpp` (`GL4-9`/`GL4-13`) had never been exercised under real culling and used a quad winding XNA's actual default (`CullCounterClockwiseFace`) culls — fixed with an explicit `RasterizerState::CullNone` opt-out (the same established idiom `bgfx_basiceffect_texture_enabled_test.cpp` already uses for the identical reason, not new). The new test itself needed 2 of its own corrections: `BlendState::AlphaBlend` assumes premultiplied source colour (switched to `BlendState::NonPremultiplied` for a straight-alpha check), and this codebase's `Color::Green` is real XNA `(0,128,0)`, not `(0,255,0)` (`Lime`). |
| `GL4-17` | Real backbuffer MSAA — a manually-managed multisample FBO (`msaaFbo_`/`msaaColorRbo_`/`msaaDepthRbo_`, real `GL_DEPTH24_STENCIL8` combined depth+stencil) resolved via `glBlitFramebuffer` before `Present()`/`ReadBackbuffer()`, mirroring `EasyGLGraphicsBackend`'s own `CreateMsaaBuffers`/`ResolveMsaa`/`BindDefaultFramebuffer` shape rather than an SDL_GL window pixel-format request. Fixed at backend-construction time; `ApplyMultiSampleCount` not overridden (same documented limitation `EasyGLGraphicsBackend` already has). `GetMultiSampleCount()` (top-level `IGraphicsBackend`) now reports the real `GL_MAX_SAMPLES`-clamped value. | ✅ | `GraphicsDeviceManager.PreferMultiSampling` set in a `Game` subclass's constructor never reaches the *first* backend construction at all (a documented, codebase-wide `GraphicsDevice` architectural constraint — the device member is unconditionally default-constructed with `MultiSampleCount=0` first) — verification used `GraphicsDevice::RecreateBackendForMultiSampleCount(8)` (the same NOXNA test-only escape hatch Vulkan's own pre-Task-902 MSAA tests used), not a design gap specific to this backend. |
| `GL4-18` | Real `Texture2D` mip level support — `OpenGL4TextureBackend::UpdatePixelsLevel()` uploads real per-level data via `glTexImage2D` (level storage is never pre-allocated beyond level 0), `GL_TEXTURE_MAX_LEVEL` is clamped to the real requested level count at construction (matches `EasyGLTextureBackend`'s own Task-924 fix), and `FilterToGL`'s mapping table gained real `GL_*_MIPMAP_*` min-filter tokens for every `TextureFilter` `Mip*` variant. `Point`/`Linear` deliberately keep their non-mip-aware GL filters, matching `EasyGLGraphicsBackend`'s own identical, documented choice. | ✅ | The new test needed the same `Color::Green`-is-`(0,128,0)`-not-`(0,255,0)` correction `GL4-16` already found once. |
| `GL4-19` | `AlphaTestEffect`/`DualTextureEffect` — both reuse `GL4-13`'s existing stride-20/24 programs via new uniforms only: `uAlphaTest` (`vec4` reference/tolerance/pass-weight/fail-weight discard ternary, ported from `VulkanGraphicsBackend`'s `alpha_test3d.frag.glsl`) and `uTexture2`/`uDualTextureEnabled` (a second sampler on texture unit 1, `BindProgramForStride`'s new `hasTexture1` block mirroring the existing `hasTexture0` one). Dual-texture blend is the real XNA/D3D `DualTextureEffect.fx` 2x-modulate formula (`tex1.rgb*=2.0; result=tex1*tex2*diffuseColor`), cross-verified against both `VulkanGraphicsBackend`'s `dual_texture3d.frag.glsl` and `EasyGLGraphicsBackend`'s current inline GLSL before implementation. | ✅ | The new test's first draft had one authoring mistake (not a backend bug): a white/white `DualTextureEffect` check assumed `diffuseColor` passes through unchanged, but `tex1(1.0)*2*tex2(1.0)=2.0` clamps to full brightness on write and masks `diffuseColor`'s own value — fixed with a mid-gray second texture so `tex1*2*tex2~=1.0` (identity). |
| `GL4-20` | Plain (non-render-target) `Texture3D`/`TextureCube` — `OpenGL4Texture3DBackend` (real `GL_TEXTURE_3D`, every mip level pre-allocated via the newly-loaded `gl4_glTexImage3D`, per-Z-slice `GetData` via a temporary FBO + the newly-loaded `gl4_glFramebufferTextureLayer` + `glReadPixels`) and `OpenGL4TextureCubeBackend` (reuses `GL4-15`'s `GL_TEXTURE_CUBE_MAP_POSITIVE_X+face` arithmetic, every face × every mip level pre-allocated via `glTexImage2D`), both modeled on `EasyGLTexture3DBackend`/`EasyGLTextureCubeBackend`'s resource shape. Both add `GL_TEXTURE_MAX_LEVEL` clamping (stricter than the EasyGL reference, matching `GL4-18`'s own fix). `TextureCube::GetData` deliberately does not Y-flip, unlike `OpenGL4RenderTargetCubeBackend::GetData` (a framebuffer-origin render target) — matches `EasyGLTextureCubeBackend`'s own plain-texture convention. | ✅ | Both new tests (`OpenGL4_Texture3D` 3/3, `OpenGL4_TextureCube` 4/4) passed every check on their first real run — no backend or test bugs found this time. |
| `GL4-21` | `EnvironmentMapEffect` — a dedicated `env_map3d` GLSL 410 core program (stride 32, same `VertexPositionNormalTexture` layout as `lit_textured3d`), selected by `BindProgramForStride` instead of `lit_textured3d` when `GpuDrawParams::envMapping` is set. Ported near-verbatim from `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram`, cross-checked against `VulkanGraphicsBackend`'s `env_map3d.frag.glsl` for the exact reflection/Fresnel/lerp/alpha-scaling formula (`docs/environmentmapeffect-support.md` documents the 2 real formula bugs — additive-not-lerp, missing alpha scaling — already found and fixed on 3 other backends; this port used the corrected formula from the start). Cube map binds to texture unit 1 (`GL4-19`'s `DualTextureEffect` slot, safe since the two effects are mutually exclusive per draw). No `GpuDrawParams`/`IGraphicsBackend.hpp` changes needed — every field was already present from earlier phases. | ✅ | Check A reuses Task 399's own cross-backend-verified combined-scene oracle verbatim (`(151,101,76)` ± 20) and passed on the first run at `(131,91,71)`. All 4 checks passed on the first real run — no backend or test bugs found. |
| `GL4-22` | `SkinnedEffect` — a dedicated `skinned3d` GLSL 410 core program on **new** strides 52/56 (`VertexPositionNormalTextureSkinned` + optional trailing `Color`), plus matching `OpenGL4VertexBufferBackend::ApplyLayout` attribute cases (blend-indices via the newly-loaded `gl4_glVertexAttribIPointer` — a true GLSL integer attribute, not float-converted, since it subscripts `uBones[]`). Ported near-verbatim from `EasyGLGraphicsBackend::EnsureSkinnedProgram`, matching real XNA `SkinnedEffect.fx`'s `Skin()` function: skin matrix = sum of only the first `WeightsPerVertex` (1/2/4) `uBones[index]*weight` pairs (`Task 895`'s already-fixed formula, not the naive always-sum-4 bug). Lighting reuses `lit_textured3d`'s 3-light diffuse+specular+emissive formula plus a `VertexColorEnabled`-gated vertex-colour modulate for stride 56. No fog (same deliberate deferral as every other 3D stride variant). All 72 bones upload via one `gl4_glUniformMatrix4fv(loc, boneCount, ...)` call. No `GpuDrawParams`/`IGraphicsBackend.hpp` changes needed. | ✅ | 5 checks across 4 scenarios (identity no-op, single-bone translate, two-bone weighted blend reaching the same net shift as a decisive both-bones-contribute proof, and `VertexColorEnabled` reusing `easygl_skinnedeffect_vertexcolor_test.cpp`'s own `(174,0,0)` oracle) all passed on the first real run — no backend or test bugs found. |
| `GL4-23` | `PbrEffect`/`SkinnedPbrEffect` — two new dedicated programs on two brand-new strides: `pbr3d` (stride 48, `VertexPositionNormalTangentTexture`) and `pbr_skinned3d` (stride 68, PBR+skinning combined, sharing `pbr3d`'s fragment shader). `OpenGL4VertexBufferBackend::ApplyLayout` gained matching stride-48/68 cases. Ported near-verbatim from `EasyGLGraphicsBackend::EnsurePbrProgram()` — the real glTF 2.0 metallic-roughness BRDF (GGX/Smith-Schlick-GGX/Schlick Fresnel) — cross-checked against `VulkanGraphicsBackend`'s `pbr3d.frag.glsl` and `BgfxGraphicsBackend`'s `fs_pbr3d.sc` (byte-for-byte identical `PbrLight()` math). 5 texture units sampled unconditionally every fragment, so two new lazily-created 1×1 fallback textures (`defaultWhiteTexture_`/`defaultFlatNormalTexture_`) are bound whenever a `GpuDrawParams::pbr*Map` pointer is null. No `GpuDrawParams`/`IGraphicsBackend.hpp` changes needed — last remaining built-in XNA/CNA effect for this backend. | ✅ | Real bug found+fixed: the very first PBR draw of a process rendered near-black because `EnsureDefaultWhiteTexture()`/`EnsureDefaultFlatNormalTexture()`'s own trailing `glBindTexture(GL_TEXTURE_2D, 0)` clobbered/unbound the base-colour texture that had *already* been bound to unit 0 moments earlier — fixed by moving both `Ensure*` calls to the very top of `BindProgramForStride`, before any real per-draw texture gets bound. Check A reuses `easygl_pbreffect_golden_test.cpp`'s own oracle and matched it exactly (`(64,74,87)`) after the fix. |
| `GL4-24` | Real occlusion queries — `OpenGL4OcclusionQueryBackend` wraps a genuine GL 1.5 core query object using `GL_SAMPLES_PASSED` (an exact passed-sample count, unlike `EasyGLOcclusionQueryBackend`'s GLES3 `GL_ANY_SAMPLES_PASSED` 0/1-only query — matches real XNA's own desktop `OcclusionQuery.PixelCount()` semantics). `GL4Loader` gained the GL 1.5 query entry points (`glGenQueries`/`glDeleteQueries`/`glBeginQuery`/`glEndQuery`/`glGetQueryObjectuiv`). `IsComplete()` polls `GL_QUERY_RESULT_AVAILABLE` and caches the result once ready; no busy-wait/forced sync. | ✅ | Ported `vulkan_occlusionquery_pixelcount_test.cpp`'s 3-scenario methodology (visible/occluded/multi-draw-span-sums-correctly) verbatim. All 6 checks passed on the first real run — no backend or test bugs found. |
| `GL4-25` | Real fog on all 7 `GpuDrawParams`-driven stride/dispatch shaders (Task 1111's formula, ported from `EasyGLGraphicsBackend`'s own per-program fog blocks), plus a **new** `case 16:`/`coloredParams3DProgram_` closing a separate pre-existing parity gap (stride-16 `VertexPositionColor` draws previously bypassed `GpuDrawParams` entirely — no `DiffuseColor`/`VertexColorEnabled`/`AlphaTest`/fog — falling back to the params-free `DrawColoredPrimitives` path even when a real `Effect.Apply()` was in play). | ✅ | Real gap found+fixed (the stride-16 parity gap above, not a fog-formula bug). Checks A–C reuse `easygl_basiceffect_fog_test.cpp`'s own 3-scenario oracle verbatim; Checks D–H use `mix(fogColor,colour,0)==fogColor` at `Z=FogStart` as an exact, effect-agnostic proof across the other 5 shader families — all matched exactly. All 8 checks passed on the first real run. |
| `GL4-26` | Real dynamic `SamplerState` for direct 3D draws — deleted 7 redundant/incorrect `ApplySamplerState(slot, 0, 1, 1, 1)` override call sites in `BindProgramForStride`; `GraphicsDevice::applySamplerStatesToBackend()` already applies the real per-slot `SamplerState` for all 16 slots immediately before every `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` call, so nothing needed to be added. | ✅ | Real bug found+fixed (by inspection, not a failing test): the old hardcoded value ran *after* the real one and silently overwrote it on every draw, always Clamp — and real XNA's own default is Linear+**Wrap**, not Clamp, so untouched `SamplerStates` were also wrong before this fix. `OpenGL4_SamplerState` (3/3) ports `easygl_sampler_state_effect_test.cpp`'s Wrap-vs-Clamp proof verbatim plus a new default-reads-Wrap check. All 3 passed once the fix landed. |
| `GL4-27` | Real `GpuDrawParams::baseVertex` support — `DrawIndexedPrimitivesEx` now calls the real GL 3.2 core `glDrawElementsBaseVertex` (newly loaded) instead of plain `glDrawElements`, adding `params.baseVertex` to every fetched index before it indexes into the bound vertex buffer. | ✅ | `OpenGL4_BaseVertex` (2/2): a shared 8-vertex buffer (2 quads, distinct colours/positions) + a shared 6-index buffer of purely *local* indices, reused unchanged for both draws (only `baseVertex` differs, `0` then `4`) — a decisive, unambiguous "honored vs ignored" distinction (an ignored `baseVertex` would leave the second quad's half at the background colour instead of its own colour). Both checks passed on the first real run. Also documented a separate, newly-discovered gap for a future task (not fixed here): this backend has no 32-bit index buffer support at all (`IsThirtyTwoBit()` hardcoded `false`, `GL_UNSIGNED_SHORT` hardcoded throughout). |
| `GL4-28` | Real `TransformWindowToLogical`/`TransformLogicalToWindow` — pure-uniform-scale (no offset) physical↔logical coordinate mapping (`scale = virtualHeight_ / physicalWindowHeight`), ported from `EasyGLGraphicsBackend`'s identical formula/rationale; exact for this backend's own default `FixedHeightDynamicWidth` presentation. Used by `Mouse::SetPosition` (logical→window) and `SdlInputBridge` (window→logical, incoming physical mouse events). | ✅ | `OpenGL4_TransformCoords` (4/4): 64×64 physical window + `SetVirtualResolution(128,128)` forces a deterministic 2x scale; checks both directions at a centre point and a non-centre point, proving a genuine scale (not identity) and an exact round-trip inverse. All 4 checks passed on the first real run — no backend or test bugs found. |
| `GL4-29` | Real `PreferPerPixelLighting` vertex-lit shader variant — two new dedicated per-vertex-lit programs, `litTextured3DVertexLitProgram_` (stride 32) and `skinned3DVertexLitProgram_` (stride 52/56), ported from `EasyGLGraphicsBackend::EnsureLit3DVertexLitProgram()`/`EnsureSkinnedVertexLitProgram()` — identical Blinn-Phong math to the existing per-pixel programs, moved into the vertex stage and Gouraud-interpolated via new `vLitRGB`/`vSpecularRGB` varyings. `BindProgramForStride`'s stride-32/52/56 cases select between the two via `params.lightingEnabled && !params.preferPerPixelLighting` (XNA's own default gate). | ✅ | `OpenGL4_PreferPerPixelLighting` (6/6): reuses `easygl_basiceffect_preferperpixellighting_test.cpp`'s/`easygl_skinnedeffect_preferperpixellighting_test.cpp`'s exact scene and analytically re-derived expected values verbatim (same ported formula, same oracle applies) — Checks A–C cover `BasicEffect`, D–F cover `SkinnedEffect`. All 6 passed on the first real run (`(127,127,127)` vertex-lit vs `(152,152,152)` pixel-lit, both effects) — no backend or test bugs found. |
| `GL4-30` | Real custom `ShaderEffect` (`CreateEffectBackend`) — new `OpenGL4EffectBackend` (thin `IEffectBackend` wrapper around one `OpenGL4RawProgram`, modeled on `EasyGLEffectBackend`), plus a `params.customEffectBackend` check + new `BindCustomEffectMatrices` helper at the top of `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` (ported from `EasyGLGraphicsBackend`'s own identical helper) that binds the compiled program and its `World`/`View`/`Projection` uniforms directly, bypassing `BindProgramForStride` entirely. | ✅ | `OpenGL4_ShaderEffect3D` (2/2): ports `easygl_shadereffect_3d_test.cpp`'s exact scene/methodology/expected values (desktop GLSL 410 core translation only). Check A (`World=Identity`) and Check B (`World=RotationY(180°)`, same footprint but flipped world normal) both matched exactly (`(200,100,50)`/`(0,0,0)`) on the first real run — no backend or test bugs found. `SpriteBatch::SetCustomEffect` integration deliberately out of scope (separate, larger feature, unattempted on every other backend that has landed `CreateEffectBackend` too). |
| `GL4-31` | Real 32-bit index buffer support (discovered while scoping `GL4-27`) — `OpenGL4IndexBufferBackend` gained a `thirtyTwoBit_` flag plus real `SetData32`/`SetData32WithOptions` overrides (`GL_UNSIGNED_INT` storage), `IsThirtyTwoBit()` now reports the real flag, `CreateIndexBuffer32()` is now overridden, and every index-buffer draw call site selects `GL_UNSIGNED_INT`/`sizeof(uint32_t)` vs `GL_UNSIGNED_SHORT`/`sizeof(uint16_t)` from `IsThirtyTwoBit()` instead of hardcoding 16-bit. | ✅ | `OpenGL4_IndexBuffer32` (3/3): combines `OpenGL4_BaseVertex`'s own shared-vertex-buffer/two-draws methodology with a real `IndexElementSize::ThirtyTwoBits` `IndexBuffer` — a discriminating proof, since a still-16-bit-reinterpreted 32-bit buffer would misread the 6 `uint32_t` indices' raw bytes as 12 `uint16_t` values, producing garbage vertex fetches instead of a clean two-triangle quad. All 3 checks passed on the first real run — no backend or test bugs found. |
| `GL4-32` | Real `SpriteBatch::SetCustomEffect` integration (discovered while scoping `GL4-30`) — `OpenGL4SpriteBatchBackend` gained a `customEffect_` field and a real `SetCustomEffect()` override; `FlushBatch()` now binds the same compiled program a custom `ShaderEffect` itself owns (`Effect::GetEffectBackendPtr()`) instead of the built-in sprite program when one is set, calls `Effect::Apply()`, and sets `"projection"` (this codebase's established custom-2D-effect uniform-name convention). | ✅ | `OpenGL4_ShaderEffectSpriteBatch` (2/2): ports `easygl_shader_effect_test.cpp`'s exact scene (a red-tint custom shader applied to a white sprite over a green background). Check A (sprite centre reads red-tinted, proving the custom program is genuinely bound) and Check B (background stays unmodified green, proving no full-screen side effect) both passed exactly on the first real run — no backend or test bugs found. |
| `GL4-33` | Real hardware instancing (`DrawInstancedPrimitivesEx`, discovered in a final post-`GL4-32` audit) — new generic `VertexElement`-driven attribute mapper (`OpenGL4VertexBufferBackend::SetVertexDeclaration`/`GetDeclarationElements`, `ApplyLayout`'s new generic path), plus `DrawInstancedPrimitivesEx` binding the per-instance buffer's own attributes at locations continuing after the mesh buffer's own, each with `glVertexAttribDivisor(location, 1)`, then real GL 3.1 core `glDrawElementsInstanced` (both newly loaded via `GL4Loader`). Added a shared `VertexElementFormat` alias to `IGraphicsBackend.hpp` (purely additive). | ✅ | `OpenGL4_InstancedModel` (2/2): ports `easygl_instancedmodel_shader_test.cpp`'s exact scene/packing derivation/expected values — a single quad mesh drawn twice in ONE `DrawInstancedPrimitives` call, driven by 2 instances' own transforms as per-instance attributes. Instance 0 (pure translation, faces the light) reads pure white; instance 1 (180° rotation then translation, faces away) reads dim gray — both matched exactly on the first real run, proving per-instance data is genuinely read per-instance, not per-vertex or constant. |

---

## Verification methodology

Mirrors the `SDL_GPU`/`WebGPU`/`D3D11` precedent: this backend's own dedicated CTest suite
(`ctest -R OpenGL4` — `OpenGL4_Smoke`, `OpenGL4_Readback`, `OpenGL4_3D`, `OpenGL4_Textured3D`,
`OpenGL4_RenderTarget2D`) is the validated methodology for a real-window/GPU backend in this
project, not a full unfiltered `CnaTests` run.
In this sandboxed dev environment, building the full `CnaTests` target hit pre-existing,
backend-independent gaps unrelated to this work (a `tools/audio/*_harness.cpp` target missing an
SDL3 include path, and a version-skew between this checkout's `Xnb` content readers and the
sibling `sharp-runtime` clone's `BinaryReader` — `ReadDecimal`/`ReadChar` — that a fresh
`sharp-runtime` checkout doesn't currently provide). Both reproduce identically under other
backends too and are out of scope for this plan; not fixed here (for `GL4-14`'s own dedicated
CTest run below, a local, throwaway, uncommitted `ReadChar`/`ReadDecimal` stub was added directly
to the sibling `sharp-runtime` checkout used by this sandboxed session only, purely to unblock the
`CNA` library link so real pixel-level verification could run end-to-end instead of stopping at
"syntax-checks clean" — nothing in the `sharp-runtime` checkout was committed or pushed).
`GraphicsBackendCompileDefinitionTests.cpp` was independently syntax-checked (`g++ -fsyntax-only`)
against `CNA_BACKEND_OPENGL4`'s compile definitions instead.

All twenty-five dedicated tests were run for real, under Xvfb (`SDL_VIDEODRIVER=x11`), on this dev
machine's Mesa/llvmpipe GL 4.5 core-profile implementation:

- `OpenGL4_Smoke` — 8/8 (window/context lifecycle, VertexBuffer/IndexBuffer round-trip incl.
  `SetDataWithOptions`/`SetData16WithOptions`, 60 frames of Clear+Present).
- `OpenGL4_Readback` — 10/10 (Clear visibility with no intervening Present, `SpriteBatch` partial
  coverage, alpha=0/50% blending, source-rectangle cropping, all three `TextureAddressMode` values).
- `OpenGL4_3D` — 4/4 (solid-color quad via `DrawPrimitives`, real depth-test occlusion proven both
  draw orders, `DrawIndexedPrimitives`).
- `OpenGL4_Textured3D` — 5/5 (`textured3d` samples a solid-orange texture exactly; `colored_textured3d`'s
  vertex-color tint multiplies the sampled texture; `lit_textured3d`'s unlit render matches the
  plain texture exactly AND `EnableDefaultLighting()`'s lit render is provably different from it;
  `DrawIndexedPrimitivesEx` samples correctly too).
- `OpenGL4_RenderTarget2D` — 12/12 (Clear-only/colored3d/depth-tested RenderTarget2D draws sampled
  back via `SpriteBatch`; `MultiSampleCount` property fidelity; real `GetData()` pixel reads on all
  three; a mipMap round-trip; a real MSAA round-trip through the `glBlitFramebuffer` resolve path;
  a `SpriteBatch::Draw()`-into-a-bound-RT check proving `FlushBatch`'s RT-size-aware viewport fix).
  `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D` were all re-run after this
  task's shared `SetViewport`/`FlushBatch` changes and still pass at their original 8/8, 10/10,
  4/4, 5/5 — no regression.
- `OpenGL4_RenderTargetCube_MRT` — 13/13 (two independent cube faces proven not to alias each
  other via GetData(); a real colored3d draw into a face; a depth-tested face; `MultiSampleCount`
  fidelity; a mipMap round-trip; a real MSAA round-trip through `glBlitFramebuffer`; MRT slot-0-
  receives-the-draw/slot-1-stays-independent proof; MRT teardown restoring FBO 0 correctly).
  `OpenGL4_Smoke`/`OpenGL4_Readback`/`OpenGL4_3D`/`OpenGL4_Textured3D`/`OpenGL4_RenderTarget2D`
  were all re-run after this task's `SetRenderTarget2D` change (new `currentRtCube_` unbind
  check) and still pass at their original counts — no regression.
- `OpenGL4_RenderState` — 12/12 (`BlendState::Opaque`/`NonPremultiplied` preset checks, a custom
  additive `BlendState`, `SetBlendFactor`'s constant colour reaching the GPU, a
  `DepthStencilState`-object depth test, a real 2-pass stencil-buffer test, `CullMode`
  cross-checked against `easygl_rasterizerstate_cullmode_test.cpp`'s own already-verified
  winding, scissor-rect gating, wireframe fill mode). Turning cull mode on for real broke
  `OpenGL4_3D`/`OpenGL4_Textured3D` (both had quad windings XNA's real default culls) until fixed
  with an explicit `RasterizerState::CullNone`; all 7 dedicated OpenGL4 CTest suites re-ran green
  afterward.
- `OpenGL4_MSAA` — 3/3 (a diagonal-edge-triangle differential test: hard binary transition with
  MSAA off, genuinely blended intermediate pixels for the identical geometry after
  `RecreateBackendForMultiSampleCount(8)`, and a real non-zero `GetMultiSampleCount()`). Full
  re-run of the other 7 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_Mipmap` — 4/4 (a real high mip level genuinely GPU-selected and sampled under heavy
  minification with a mip-aware filter, `Point` confirmed to still never mip-select, level 0
  still correct, and an ordinary single-level texture sampled with a mip-aware filter no longer
  solid black). Full re-run of the other 8 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_AlphaTestDualTexture` — 8/8 (`AlphaTestEffect` GPU discard proof across
  `CompareFunction::Always`/`Never`/`LessEqual`/`Equal`; `DualTextureEffect` proofs that `tex1`,
  `tex2`, and `diffuseColor` each genuinely contribute, including a yellow×cyan→green case that
  only passes if both texture slots multiply simultaneously). Full re-run of the other 9 OpenGL4
  CTest suites confirmed no regression.
- `OpenGL4_Texture3D` — 3/3 (per-slice `SetData`/`GetData` round-trip on a 2×2×4 volume with no
  cross-slice aliasing, a sub-box x/y/z offset proof that doesn't bleed outside its box or into
  other slices, and a genuine mip-level-1 storage round-trip).
- `OpenGL4_TextureCube` — 4/4 (per-face `SetData`/`GetData` round-trip on a size=2 cube with no
  cross-face aliasing, a sub-rectangle offset proof that doesn't bleed into an adjacent face, a
  decisive no-Y-flip proof via an asymmetric single-corner marker pixel, and a genuine
  mip-level-1 storage round-trip). Full re-run of the other 10 OpenGL4 CTest suites confirmed no
  regression.
- `OpenGL4_EnvironmentMapEffect` — 4/4 (Task 399's cross-backend-verified combined-scene oracle,
  `EnvironmentMapAmount=0` proven to make the cube map's own colour irrelevant,
  `EnvironmentMapAmount=1`/`FresnelFactor=0` proven to exactly pass through the cube map's colour,
  and a non-zero `EnvironmentMapSpecular` proven to render measurably brighter). Full re-run of
  the other 12 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_SkinnedEffect` — 5 checks/4 scenarios (identity-bone no-op, a single-bone `Translate`
  genuinely displacing geometry, a two-bone weighted blend reaching the identical net shift a
  single bone alone could not reach, and `VertexColorEnabled` gating the stride-56 `aColor`
  attribute against `easygl_skinnedeffect_vertexcolor_test.cpp`'s own `(174,0,0)` oracle). Full
  re-run of the other 13 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_PbrEffect` — 4/4, reusing `easygl_pbreffect_golden_test.cpp`'s own 4-quad scene and
  independently hand-derived+captured expected pixel values verbatim (white/rough/non-metallic
  baseline, a tilted normal map proven measurably darker, fully-metallic-red vs
  fully-dielectric-red proven measurably different). Found and fixed a real first-PBR-draw texture
  unit 0 clobbering bug along the way (see `GL4-23`'s own row). Full re-run of the other 14
  OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_OcclusionQuery` — 6/6, porting `vulkan_occlusionquery_pixelcount_test.cpp`'s own
  3-scenario methodology (visible quad -> positive count, occluded quad -> exactly 0 via the real
  depth test, two half-quads summed within one query span -> the same total a single full-quad
  draw would produce). Full re-run of the other 15 OpenGL4 CTest suites confirmed no regression.
- `OpenGL4_Fog` — 8/8: 3-scenario `easygl_basiceffect_fog_test.cpp` oracle for the new
  `coloredParams3d` stride-16 program, plus an exact `mix(fogColor,colour,0)==fogColor` proof at
  `Z=FogStart` for `textured3d`/`lit_textured3d`/`env_map3d`/`skinned3d`/`pbr3d` (all matched
  exactly). Full re-run of the other 16 OpenGL4 CTest suites confirmed no regression from the new
  stride-16 dispatch path.
- `OpenGL4_SamplerState` — 3/3, porting `easygl_sampler_state_effect_test.cpp`'s own
  Wrap-vs-Clamp proof verbatim, plus a new check proving the untouched default `SamplerState` now
  reads Wrap (matching real XNA), not the old hardcoded Clamp. Full re-run of the other 17
  OpenGL4 CTest suites confirmed no regression from the corrected default.
- `OpenGL4_BaseVertex` — 2/2, a shared 8-vertex/6-index buffer pair with purely local indices
  proving `baseVertex=4` genuinely offsets the vertex fetch (right-half quad renders its own BLUE,
  not a silent re-fetch of the left-half RED quad's data). Full re-run of the other 18 OpenGL4
  CTest suites confirmed no regression.
- `OpenGL4_TransformCoords` — 4/4, a 64x64 physical window forced to a deterministic 2x scale via
  `SetVirtualResolution(128,128)`, proving both `TransformWindowToLogical`/`TransformLogicalToWindow`
  apply a genuine scale (not identity) at both a centre and a non-centre point, and that the two
  directions are exact inverses of each other. Full re-run of the other 19 OpenGL4 CTest suites
  confirmed no regression (20/20 total).
- `OpenGL4_PreferPerPixelLighting` — 6/6, reusing the EasyGL `BasicEffect`/`SkinnedEffect`
  `PreferPerPixelLighting` tests' own exact scene and analytically re-derived expected values
  verbatim: a shared-normal quad whose centre pixel sits exactly on the Gouraud-interpolation seam
  discriminates cleanly between the vertex-lit average (`(127,127,127)`) and a fresh per-fragment
  evaluation (`(152,152,152)`), for both effects. Full re-run of the other 20 OpenGL4 CTest suites
  confirmed no regression (21/21 total).
- `OpenGL4_ShaderEffect3D` — 2/2, porting `easygl_shadereffect_3d_test.cpp`'s exact scene/
  methodology/expected values: `World=Identity` (surface facing the light, N·L=1) reads full
  `diffuseColor` `(200,100,50)`, `World=RotationY(180°)` (same on-screen footprint, flipped world
  normal, N·L clamped to 0) reads genuinely lit black `(0,0,0)`, proving the custom program's own
  `World` uniform reaches the vertex shader and affects real, visible world-space lighting. Full
  re-run of the other 21 OpenGL4 CTest suites confirmed no regression (22/22 total).
- `OpenGL4_IndexBuffer32` — 3/3, combining `OpenGL4_BaseVertex`'s own shared-vertex-buffer/
  two-draws methodology with a real `IndexElementSize::ThirtyTwoBits` `IndexBuffer` instead of a
  16-bit one — a discriminating proof, since a still-16-bit-reinterpreted 32-bit buffer would
  misread its raw index bytes and fail to render a clean two-triangle quad at all. Full re-run of
  the other 22 OpenGL4 CTest suites confirmed no regression (23/23 total).
- `OpenGL4_ShaderEffectSpriteBatch` — 2/2, porting `easygl_shader_effect_test.cpp`'s exact scene:
  a red-tint custom shader applied via `SpriteBatch::Begin(..., &fx)` to a white sprite over a
  green background. Sprite centre reads red-tinted `(255,0,0)` (the custom program's own compiled
  shader genuinely bound, not the built-in one), background corner stays unmodified green
  `(0,255,0)`. Full re-run of the other 23 OpenGL4 CTest suites confirmed no regression
  (24/24 total).
- `OpenGL4_InstancedModel` — 2/2, porting `easygl_instancedmodel_shader_test.cpp`'s exact scene/
  packing derivation: one quad mesh drawn twice in a SINGLE `DrawInstancedPrimitives` call, driven
  by 2 instances' own 4x4 transforms as per-instance attributes. Instance 0 (pure translation,
  faces the light) reads pure white `(255,255,255,255)`; instance 1 (180° Y-rotation then
  translation, faces away) reads dim gray `(64,64,64,255)` — two different colors at two different
  on-screen positions from one draw call, proving `glVertexAttribDivisor=1` genuinely reads
  per-instance data. Full re-run of the other 24 OpenGL4 CTest suites confirmed no regression
  (25/25 total).

---

## Active execution order — do this one task at a time

1. ~~`GL4-1`~~ – ~~`GL4-12`~~ — Phase 1 infrastructure (window/context, clear/present, `Texture2D`,
   `VertexBuffer`/`IndexBuffer`, `SpriteBatch`, `colored3d` 3D with real depth-test proof) done and
   verified 2026-07-21, all ✅. See the task table above for the two real bugs found and fixed
   along the way.
2. ~~`GL4-13`~~ — `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` stride dispatch (`textured3d`/
   `colored_textured3d`/`lit_textured3d`) done and verified 2026-07-21, all ✅ (`OpenGL4_Textured3D`,
   5/5). See `GL4-13`'s own row for the ported lighting formula and its known simplifications
   (`baseVertex` ignored, hardcoded direct-3D-draw sampler state, no `preferPerPixelLighting`/fog).
3. ~~`GL4-14`~~ — `RenderTarget2D` (real FBO) done and verified 2026-07-22, all ✅
   (`OpenGL4_RenderTarget2D`, 12/12). See `GL4-14`'s own row for the two real `SetViewport`/
   `FlushBatch` RT-size bugs found and fixed along the way. `RenderTargetCube`/MRT were
   deliberately left out of this task's scope.
4. ~~`GL4-15`~~ — `RenderTargetCube` + real MRT done and verified 2026-07-22, all ✅
   (`OpenGL4_RenderTargetCube_MRT`, 13/13). See `GL4-15`'s own row for the documented MRT gaps
   (no depth attachment, no multi-output shader variant) carried over from `EasyGLGraphicsBackend`'s
   own identical MRT limitations.
5. ~~`GL4-16`~~ — dynamic `BlendState`/`DepthStencilState`/`RasterizerState` mapping done and
   verified 2026-07-22, all ✅ (`OpenGL4_RenderState`, 12/12). See `GL4-16`'s own row for the real
   cull-mode regression this exposed in 2 pre-existing test files (fixed, not papered over) and
   the 2 authoring mistakes found in the new test itself along the way.
6. ~~`GL4-17`~~ — real backbuffer MSAA done and verified 2026-07-22, all ✅ (`OpenGL4_MSAA`, 3/3).
   See `GL4-17`'s own row for why it's fixed at construction time (matching EasyGL) and the
   `RecreateBackendForMultiSampleCount()` test methodology this required.
7. ~~`GL4-18`~~ — real `Texture2D` mip level support done and verified 2026-07-22, all ✅
   (`OpenGL4_Mipmap`, 4/4). See `GL4-18`'s own row for the `GL_TEXTURE_MAX_LEVEL`/`FilterToGL`
   fixes this required.
8. ~~`GL4-19`~~ — `AlphaTestEffect`/`DualTextureEffect` done and verified 2026-07-22, all ✅
   (`OpenGL4_AlphaTestDualTexture`, 8/8). See `GL4-19`'s own row for the reused-stride-program
   approach and the dual-texture 2x-modulate formula cross-verified against two other backends.
9. ~~`GL4-20`~~ — plain `Texture3D`/`TextureCube` done and verified 2026-07-22, all ✅
   (`OpenGL4_Texture3D` 3/3, `OpenGL4_TextureCube` 4/4). See `GL4-20`'s own row for the FBO-based
   `Texture3D::GetData` per-slice readback and the `TextureCube::GetData` no-Y-flip convention
   (verified, not assumed, via a corner-marker pixel check).
10. ~~`GL4-21`~~ — `EnvironmentMapEffect` done and verified 2026-07-22, all ✅
    (`OpenGL4_EnvironmentMapEffect`, 4/4). See `GL4-21`'s own row for the reflection/Fresnel/lerp/
    alpha-scaling formula cross-checked against 2 other backends and the Task-399 cross-backend
    oracle reused as Check A.
11. ~~`GL4-22`~~ — `SkinnedEffect` done and verified 2026-07-22, all ✅
    (`OpenGL4_SkinnedEffect`, 5 checks/4 scenarios). See `GL4-22`'s own row for the new stride-52/
    56 vertex layout, the `gl4_glVertexAttribIPointer` loader addition, and the two-bone weighted
    blend proof.
12. ~~`GL4-23`~~ — `PbrEffect`/`SkinnedPbrEffect` done and verified 2026-07-22, all ✅
    (`OpenGL4_PbrEffect`, 4/4) — **the last remaining built-in XNA/CNA effect for this backend.**
    See `GL4-23`'s own row for the new stride-48/68 vertex layouts, the real glTF BRDF
    cross-checked against 2 other backends, and the real first-PBR-draw texture-clobbering bug
    found and fixed along the way.
13. ~~`GL4-24`~~ — real occlusion queries done and verified 2026-07-22, all ✅
    (`OpenGL4_OcclusionQuery`, 6/6). See `GL4-24`'s own row for the exact-count
    `GL_SAMPLES_PASSED` vs EasyGL's ES-only 0/1 `GL_ANY_SAMPLES_PASSED` distinction.
14. ~~`GL4-25`~~ — real fog done and verified 2026-07-22, all ✅ (`OpenGL4_Fog`, 8/8). See
    `GL4-25`'s own row for the new `coloredParams3d` stride-16 program and the separate
    pre-existing parity gap it closed along the way (stride-16 draws previously bypassed
    `GpuDrawParams` entirely).
15. ~~`GL4-26`~~ — real dynamic `SamplerState` for direct 3D draws done and verified 2026-07-22,
    all ✅ (`OpenGL4_SamplerState`, 3/3). See `GL4-26`'s own row: a real bug (hardcoded Clamp
    silently overwriting the already-correctly-applied real sampler state on every draw) found by
    inspection and fixed by deleting 7 redundant call sites, not adding anything.
17. ~~`GL4-27`~~ — real `params.baseVertex` support done and verified 2026-07-22, all ✅
    (`OpenGL4_BaseVertex`, 2/2). See `GL4-27`'s own row; also logged a new, separate,
    not-yet-fixed finding: no 32-bit index buffer support at all on this backend.
18. ~~`GL4-28`~~ — real `TransformWindowToLogical`/`TransformLogicalToWindow` done and verified
    2026-07-22, all ✅ (`OpenGL4_TransformCoords`, 4/4). See `GL4-28`'s own row for the
    pure-uniform-scale formula ported from `EasyGLGraphicsBackend`.
19. ~~`GL4-29`~~ — real `PreferPerPixelLighting` vertex-lit shader variant done and verified
    2026-07-22, all ✅ (`OpenGL4_PreferPerPixelLighting`, 6/6). See `GL4-29`'s own row for the two
    new per-vertex-lit programs ported from `EasyGLGraphicsBackend`.
20. ~~`GL4-30`~~ — real custom `ShaderEffect`/`CreateEffectBackend` done and verified 2026-07-22,
    all ✅ (`OpenGL4_ShaderEffect3D`, 2/2). See `GL4-30`'s own row for the new
    `OpenGL4EffectBackend`/`BindCustomEffectMatrices` dispatch ported from `EasyGLGraphicsBackend`.
    **Every item on this branch's original active plan (as scoped by the 2026-07-22
    project-owner decisions — OpenGL4-only, context-loss recovery explicitly deferred) was done
    at this point.**
21. ~~`GL4-31`~~ — real 32-bit index buffer support done and verified 2026-07-22, all ✅
    (`OpenGL4_IndexBuffer32`, 3/3). A newly-discovered gap found while scoping `GL4-27`, picked up
    after the original active plan closed out — see `GL4-31`'s own row.
22. ~~`GL4-32`~~ — real `SpriteBatch::SetCustomEffect` integration done and verified 2026-07-22,
    all ✅ (`OpenGL4_ShaderEffectSpriteBatch`, 2/2). A newly-discovered gap found while scoping
    `GL4-30` — see `GL4-32`'s own row.
23. ~~`GL4-33`~~ — real hardware instancing done and verified 2026-07-22, all ✅
    (`OpenGL4_InstancedModel`, 2/2). A newly-discovered gap found in a final post-`GL4-32` audit,
    picked up after the project owner explicitly asked to continue on OpenGL4 tasks — see
    `GL4-33`'s own row for the new generic `VertexElement`-driven attribute mapper this needed
    first.

See the "Remaining work" section in the status banner above for the full, non-prioritized list of
what's still open (Windows/macOS validation and the permanently-deferred context-loss recovery
feature) — these are candidates for a fresh scoping pass, not blocking anything above.
