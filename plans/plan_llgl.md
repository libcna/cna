# LLGL Graphics Backend — Implementation Plan

> **Authoritative post-audit integration disposition (2026-08-09).** This block supersedes stale
> status cells and broad capability claims in the historical diary below while preserving that
> diary as the original lane record. The supported public contract is one backend identity,
> `LLGL`, routed through LLGL `Release-v0.04b`
> (`1e78d8fa497f5cab76b231ba13f4d6249dac0e7e`) to its OpenGL RenderSystem and native OpenGL/GLX on
> Linux/X11 x86_64. OpenGL is a required build module and the only automatic/runtime-supported
> renderer. Explicit Vulkan selection now rejects: the module remains compile-covered, but native
> validation exposed descriptor/layout and teardown violations at the pinned revision. Null is
> explicit diagnostics only. Wayland, Windows and i686 are not part of the CNA LLGL contract.
>
> The recorded i686 MinGW `__int128` failure is **classification A, non-gating**. Its concrete route
> is Glide's x86 ABI probe; no historical LLGL i686 configure, test, platform claim or public option
> exists. The first diagnostic is `sharp-runtime/include/System/Int128.hpp:31:9: error: expected
> unqualified-id before '__int128'` from `i686-w64-mingw32-g++`. Owner disposition: preserve the
> historical record, do not modify sharp-runtime, and validate LLGL on its truthful native x86_64
> X11 route.
>
> **Supported-path disposition:** `LLGL-48` and `LLGL-52` are resolved and have gating runtime
> oracles. `LLGL-53` is closed by measured capability narrowing: custom viewport/depth/render-target
> controls pass, while non-zero depth bias and stencil reject deterministically. `LLGL-54` keeps
> proven 2-4-slot `RenderTarget2D` MRT; cube-face and mip-mapped MRT compositions reject.
> `LLGL-55` is satisfied for the supported OpenGL/Xvfb route with `CNA_ENABLE_NET=OFF`; its proposed
> Vulkan-only lane is superseded by the explicit unsupported boundary. `LLGL-56` retains X11-only
> scope; sanitizer allocations rooted in pinned LLGL/SDL/Mesa GLX visual selection are external.
> `LLGL-38` remains non-gating external/hardware coverage, not an unresolved supported-path defect.
>
> Two independent post-audit findings were added and resolved. `LLGL-57` synchronized the LLGL
> swap-chain extent during virtual-resolution/reset and first readback, fixing constructor-size
> pixels leaking into the first resized frame. `LLGL-58` supplies a valid address for LLGL 0.04b's
> zero-count clear-value copy in both deferred render-pass routes, eliminating the CNA-reachable
> UBSan report without patching the dependency. No shader source or generated shader artifact was
> changed.
>
> The authoritative stream-array architecture is preserved. One geometry stream supports
> `VertexOffset`, `vertexStart`, `startIndex` and `baseVertex`; multistream, instance-frequency and
> instanced combinations reject. Texture2D/readback, RenderTarget2D, MRT, stock BasicEffect paths,
> SpriteBatch custom effects, Texture3D transfer, occlusion, wireframe where reported, additive
> blending and PBR are covered. Plain TextureCube is transfer-only; cube sampling and
> RenderTargetCube reject. Back-buffer MSAA, stencil, constant blend factor and non-zero depth bias
> are not advertised.

> **Audit update (2026-08-03, revision `212cb62c`): functionally broad, but not yet production-ready
> or at unqualified EasyGL parity.** The current backend builds with both LLGL Vulkan and OpenGL
> modules and the core OpenGL pixel tests (`Smoke`, `2D`, `TextureReadback`, `Presentation`, `3D`,
> `BasicEffect`, `RenderTarget`, `MRT`, `MultiSampleMask`) pass on Xvfb/llvmpipe. The audit also
> confirmed several correctness bugs which are already partly described in `known_bugs.md`: replay
> grouped by target can violate public call order, a queued draw can observe a later mutation of the
> same vertex/index buffer, the 3D pipeline key drops blend/write-mask state, custom Vulkan shaders
> with more than one descriptor set can crash during pipeline creation, and stock effects with more
> than one texture do not retain per-slot sampler state. Phase LLGL-8 below is the remediation gate;
> completing an earlier implementation/audit phase must not be interpreted as closing these bugs.
>
> **Verification limits of this audit:** the Vulkan module compiled, but the available Xvfb server
> did not expose DRI3, so Vulkan presentation failed before backend rendering with
> `VK_ERROR_SURFACE_LOST_KHR`. This is an infrastructure limitation, not counted as a renderer
> regression. OpenGL-only execution additionally reproduced the known back-buffer Y-offset problem:
> `Llgl_Deferred_Viewport` passed 37/39 checks and `Llgl_Deferred_Scissor` 43/47; the failing cases
> switch between a render target and a non-zero-Y viewport/scissor on the back buffer. The complete
> `CnaTests` target is currently also blocked independently of LLGL because `CNA_ENABLE_NET=OFF`
> still compiles ENet tests and cannot find `enet/enet.h` (tracked by LLGL-55 as test-infrastructure
> hardening, not as a graphics defect).
>
> **Status (2026-07-31): the 2D baseline is implemented and verified against real GPU pixels on
> BOTH renderer modules.** `CNA_GRAPHICS_BACKEND=LLGL` configures and builds
> (`cna_backend_graphics_llgl`), and on this environment's virtual display (Xvfb + Mesa lavapipe
> for Vulkan, llvmpipe for OpenGL) a real SDL window, a real `LLGL::RenderSystem`, and a real swap
> chain clear and present 60 frames, upload a real `Texture2D`, and draw a real `SpriteBatch` scene
> whose pixels are read back and asserted: quadrant orientation, tint multiplication,
> `SpriteEffects::FlipHorizontally`, and `BlendState::NonPremultiplied` alpha blending. Four CTests,
> all green: `Llgl_Smoke` and `Llgl_Smoke_OpenGL` (8/8 each), `Llgl_2D` and `Llgl_2D_OpenGL`
> (10/10 pixel checks each).
>
> **`LLGL-17` — "the OpenGL module clears but draws nothing" — is fixed** (2026-07-31, same day it
> was filed). The cause was CNA's own shader-language selection, not LLGL: a modern OpenGL module
> reports *both* GLSL and SPIR-V (desktop GL ingests SPIR-V through `GL_ARB_gl_spirv`), the
> selection checked SPIR-V first, and GL accepted the Vulkan-targeted SPIR-V far enough to
> rasterize geometry from the position attribute while every other attribute and the uniform block
> read as zero. GLSL is now preferred wherever a module offers it. The regression that let this
> survive is closed too: the OpenGL module now has its own CTest registrations rather than being
> exercised only by whatever the default preference happened to pick.
>
> **The colour-only 3D path is implemented and pixel-verified too** (`LLGL-24`, 2026-07-31):
> `VertexDeclaration` translation, per-layout vertex shaders, a keyed pipeline cache, real vertex
> and index buffer draws with `vertexStart`/`startIndex`/`baseVertex` honoured, depth test and
> depth write, cull mode, and fill mode — `Llgl_3D` and `Llgl_3D_OpenGL`, 12/12 checks each. The
> **`BasicEffect`'s textured, tinted, fogged, alpha-tested AND LIT paths are all done** (`LLGL-25`,
> closed): one texture, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog, `AlphaTestEffect`,
> and per-pixel directional lighting, textured or untextured (ambient, up to three lights, specular,
> `EmissiveColor`) — `Llgl_BasicEffect`/`Llgl_Lighting` and their `_OpenGL` twins, 13/13 and 10/10
> checks each. `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`, and cube/
> volume textures were also open as of this paragraph's original writing but are done now -- see
> their own paragraphs below. `SkinnedPbrEffect` (`PbrEffect` + skinning combined) is done too --
> see its own paragraph below as well. Every stock effect this backend originally scoped is now
> implemented.
> The originally scoped stock-effect families are present, but the 2026-08-03 audit disproved the
> older claim that every unsupported or incomplete path fails explicitly: several state, cache and
> deferred-resource gaps are silent today. See the current gap list and Phase LLGL-8.
>
> **`RenderTarget2D` is implemented too** (`LLGL-26`, 2026-07-31): draw into it, unbind back to the
> swap chain, sample it back onto the screen with `SpriteBatch`, draw the 3D path (`BasicEffect` +
> depth test) into it, and `GetData()` straight off the colour attachment — `Llgl_RenderTarget`/
> `_OpenGL`, 9/9 on both modules. `RenderTargetCube` was open as of this paragraph's original
> writing but is done now, and so is MRT -- see their own paragraphs below. **MSAA render targets
> and mip-mapped render targets are done too** (`LLGL-26` follow-ups, 2026-08-01) — see their own
> dedicated paragraphs below. Every item this row ever scoped for `RenderTarget2D` is now done.
>
> **Occlusion queries are implemented too** (`LLGL-28`, 2026-07-31): `LLGL::QueryHeap`-backed,
> answering synchronously (a submit-and-wait forced on first `IsComplete()`/`PixelCount()` call,
> not genuine async polling) — `Llgl_OcclusionQuery`/`_OpenGL`, 6/6 on both modules.
>
> **Custom `ShaderEffect`s are implemented too** (`LLGL-27`, 2026-07-31), scoped to `SpriteBatch`
> draws (the fixed sprite vertex layout, not an arbitrary `VertexDeclaration`) — a real runtime
> GLSL→SPIR-V compile via `libshaderc` when the Vulkan module is loaded, GLSL handed to LLGL
> directly on the OpenGL module — `Llgl_ShaderEffect`/`_OpenGL`, 6/6 on both modules. Every item in
> `plan_llgl.md`'s original phase-5 scope (`LLGL-26`/`27`/`28`) is now implemented in at least its
> initial, documented cut (`RenderTargetCube`, MRT, MSAA render targets, and mip-mapped render
> targets too, as of later passes -- see their own paragraphs below). No concrete follow-up remains
> open in this row's own original scope.
>
> **Lighting without a texture is implemented too** (`LLGL-31`, 2026-07-31): a fourth vertex/fragment
> shader pair (`lit_colored3d`/`lit_untextured3d`) covers a lit, untextured-but-coloured draw —
> reusing `primitiveLayout_` (no `colorMap`/`samplerState` binding) rather than
> `primitiveTexturedLayout_`. The one remaining refusal is narrower and still real: a lit,
> untextured draw with **no vertex colours either** still throws by name (`AcquirePrimitiveVertexShader`'s
> own "an untextured draw needs vertex colours" error), matching the pre-existing unlit-untextured
> restriction rather than introducing a new asymmetric capability. `Llgl_Lighting`/`_OpenGL` grew
> from 8/8 to 10/10 checks on both modules.
>
> **A real window resize is proven too** (`LLGL-29`, 2026-07-31): grow, shrink, and a post-resize
> `Letterbox` presentation rect recompute, driven through `GraphicsDeviceManager.ApplyChanges()`
> exactly like a real game — `Llgl_Resize`/`_OpenGL`, 8/8 on both modules. Found and fixed two real
> timing issues no earlier test could reach (both only ever used the default 800x480 size, so a
> resize away from it never happened): `SDL_SetWindowSize()`'s asynchronous completion under X11
> (fixed with `SDL_SyncWindow()` before the post-resize `Present()`) and `ReadBackbuffer()`
> addressing logical, not window, coordinates (a letterbox bar cannot be named through it at all).
>
> **MSAA is proven too** (`LLGL-23`, 2026-07-31): a genuinely antialiased diagonal edge, not just a
> bookkeeping check — `Llgl_Msaa`/`_OpenGL`, 7/7 on Vulkan (with two checks `[SKIP]`ped on OpenGL,
> see below). Found a real, non-test constraint along the way: this backend applies
> `MultiSampleCount` only at swap-chain CONSTRUCTION time, and since a `Game`'s single,
> eagerly-constructed `GraphicsDevice` is always built with default (`MultiSampleCount=0`)
> `PresentationParameters` before any `GraphicsDeviceManager` preference can reach it, MSAA can
> never actually be enabled through the ordinary `Game` + `ApplyChanges()` flow on this backend —
> the test constructs two raw `GraphicsDevice` objects directly instead, matching
> `dx3_resize_transaction_test.cpp`'s own established pattern. Also module-dependent like
> `WireFrame`: the Vulkan module (lavapipe) honours the requested sample count on this environment,
> the OpenGL module (llvmpipe/GLX) does not at any count tried — a real driver constraint the test
> detects and reports `[SKIP]` for rather than papering over.
>
> **MSAA render targets are implemented too** (`LLGL-26` follow-up, 2026-08-01): `CreateRenderTarget2D`'s
> `multiSampleCount` parameter is real now, not ignored — a genuinely antialiased diagonal edge
> resolved into a `RenderTarget2D`'s own colour texture, not just a bookkeeping check —
> `Llgl_Msaa_RenderTarget`/`_OpenGL`, 7/7 on both modules (unlike back-buffer MSAA, this module
> applies a real sample count on BOTH modules for a render target). Unlike the swap chain (only
> ever honoured at `GraphicsDevice` construction time), a `RenderTarget2D`'s `MultiSampleCount` is
> read at its own construction, so no raw-`GraphicsDevice`-construction workaround is needed here —
> an ordinary `PixelTestGame` device suffices. Uses LLGL's anonymous (textureless) multisampled
> colour attachment pattern, mirroring the existing anonymous depth attachment: leaving
> `RenderTargetDescriptor::colorAttachments[0].texture` null (format-only) with `samples > 1` makes
> LLGL allocate an internal MSAA buffer automatically, resolving into an explicit
> `resolveAttachments[0]` (the target's own single-sample colour texture) at the end of each render
> pass — confirmed by reading `VKRenderTarget.cpp`/`GLRenderTarget.cpp` directly before writing any
> code, rather than assumed. The applied sample count is read back via `renderTarget->GetSamples()`
> after creation (LLGL silently reduces an unsupported request rather than failing), stored on the
> new `LlglRenderTargetBackend::GetSampleCount()`/`GetMultiSampleCount()` (1 internally means "no
> MSAA"; `GetMultiSampleCount()` reports 0 in that case, matching this backend's own established
> back-buffer convention). **One real, non-obvious bug found and fixed**: LLGL builds a graphics
> pipeline against ONE fixed sample count baked in at pipeline-creation time
> (`GraphicsPipelineDescriptor::rasterizer.multiSampleEnabled` plus its `renderPass`'s own sample
> count) — every 3D/sprite/custom-effect pipeline in this backend was hardcoding
> `swapChain_->GetSamples()`/`swapChain_->GetRenderPass()` regardless of what target was actually
> bound at draw time, a latent bug invisible before real per-render-target MSAA existed (every
> render target was previously single-sample, matching the swap chain's own usual default). Once a
> `RenderTarget2D` could request a real sample count higher than the swap chain's own, this
> mismatch meant the pipeline was built `multiSampleEnabled=false` even while drawing into a
> genuinely multisampled framebuffer — the lavapipe software Vulkan module accepted this silently
> rather than erroring, rasterizing single-sample coverage into every sample of the MS image (no
> crash, no validation error, just zero actual antialiasing — `GetMultiSampleCount()` still
> correctly reported the real applied count, which is what made this hard to spot from the
> bookkeeping checks alone). Fixed with two new accessors mirroring the already-existing
> `GetPrimaryRenderPassEXT()`/`GetActiveColorAttachmentCountEXT()` pattern —
> `LlglBoundRenderTarget::GetSampleCount()` (1 for cube faces/MRT binds, which do not support MSAA
> yet) and `LlglGraphicsBackend::GetPrimarySampleCountEXT()` (the bound target's own, or the swap
> chain's when nothing is bound) — used everywhere `multiSampleEnabled`/`renderPass` were built, and
> folded into `MakeBlendPipelineKey`'s own cache key (a single-sample pipeline is not
> interchangeable with a multisampled one even when every other field matches) so a pipeline built
> for one sample count is never handed back for another. Diagnosed by adding a temporary debug
> printf of the raw edge-pixel values before and after the fix, not by guessing.
>
> **Mip-mapped render targets are implemented too** (`LLGL-26` follow-up, 2026-08-01): a
> `mipMap=true` `RenderTarget2D`'s colour texture is allocated with a REAL mip chain
> (`RenderTarget2D.LevelCount`'s own `CalculateMipLevels(w, h)` formula, computed identically here
> and matching the Vulkan backend's own `CalculateVulkanRTMipLevels`), and every level 1.. is
> genuinely regenerated from level 0's just-rendered content after each render pass this target
> appears in — `Llgl_RenderTarget2D_Mip`/`_OpenGL`, 8/8 on both modules, adapted from
> `examples/vulkan_rendertarget2d_mip_test.cpp`'s own 7:1 asymmetric-split technique but reading
> mip content back directly via `GetData(level)` (now real) instead of forcing GPU automatic LOD
> selection through an extreme on-screen minification draw — more direct and less fragile under a
> software rasterizer. **Architecture**: the render-target ATTACHMENT itself still only ever binds
> level 0 (`LLGL::AttachmentDescriptor`'s own `mipLevel` default), and `RecordAndSubmitFrame()`/
> `CaptureBackbuffer()` call `LLGL::CommandBuffer::GenerateMips()` on the colour texture right after
> `EndRenderPass()` for any bucket whose target wants it — the LLGL equivalent of the Vulkan
> backend's own `vkCmdBlitImage` cascade (`VulkanTargetPassEXT::MaybeGenerateMips`) and of EasyGL's
> `glGenerateMipmap`-on-unbind, but a single built-in LLGL call instead of a hand-rolled blit loop.
> Knowing WHICH texture to regenerate at bucket-replay time needed a new "capture at queue time"
> field (`FrameCommand::mipRegenColorTexture`, mirroring `target`/`projectionBuffer`'s own existing
> pattern) rather than a live lookup from the bound target, because a `RenderTarget2D` can be
> destroyed before the frame that references it is replayed — `LlglBoundRenderTarget::
> GetMipRegenColorTextureEXT()`/`LlglGraphicsBackend::GetActiveMipRegenColorTextureEXT()` mirror the
> already-existing `GetSampleCount()`/`GetPrimarySampleCountEXT()` pair from the MSAA render target
> paragraph above, and `FindMipRegenColorTextureEXT()` reads it back off any one command in a
> bucket once that bucket's render pass ends. `GetData(level)` was a hard `level != 0` refusal
> before this — now real for any `level` inside `[0, LevelCount)`, rejecting (throwing
> `System::NotSupportedException` through the shared `Texture2D::GetData` layer) anything outside
> it, verified by the test's own out-of-range checks on both a mip-mapped and a plain target. MRT
> binds and `RenderTargetCube` faces do not support mip-mapping yet (`GetMipRegenColorTextureEXT()`
> defaults to null for both) — no test in this project asked for either, and see `RenderTargetCube`'s
> own paragraph above for why `mipMap` is still ignored there specifically. Worked cleanly with all
> 8 checks passing on the very first run, on both modules.
>
> **`DualTextureEffect` is implemented too** (`LLGL-25`, 2026-07-31), the first of the four
> remaining `BasicEffect`-family stock effects — `Llgl_DualTexture`/`_OpenGL`, 3/3 on both modules.
> Reuses the plain textured/coloured-textured vertex shader as-is (only the fragment shader and
> pipeline layout are new) since DualTextureEffect samples its second texture through the SAME UV
> as the first. **A real, pre-existing correctness bug was found and fixed along the way, affecting
> every colour-carrying unlit/lit 3D shader, not just this one**: `VertexColorEnabled` was never
> actually read anywhere in this backend — a vertex buffer that merely CARRIED a colour attribute
> had it multiplied into the tint unconditionally, regardless of what the effect asked for.
> `llgl_basiceffect_test.cpp`'s own pre-existing Check D was silently relying on this bug. Fixed
> with a new uniform gate (`vertexColorEnabledPad`/`ambientColorLighting.w`, depending on the
> shader) and by growing the shared unlit uniform block from 128 to 144 bytes — which meant EVERY
> unlit 3D shader needed the same field added, whether or not it reads it, because OpenGL requires
> an identically laid-out named uniform block across every stage linked into one program.
>
> **`GraphicsDevice::DrawUserPrimitives()`'s typed overloads work now too** (`LLGL-32`, 2026-07-31)
> — a second real, pre-existing gap found by the same `DualTextureEffect` work (its own cross-backend
> test, `dualtextureeffect_vertexcolor_test.cpp`, could not even build a vertex buffer on this
> backend before this fix). Every typed overload routes through a raw, declaration-less
> `CreateVertexBuffer(int)` + `SetData`, so the fix infers the vertex layout from the upload
> stride's four possible sizes (16/20/24/32 bytes) instead — the same technique the Vulkan
> backend's own `MakeExt3DKey()` already uses for these exact stream sizes. Two pre-existing,
> cross-backend tests (`dualtextureeffect_vertexcolor_test.cpp`, stride 24;
> `graphicsdevice_default_state_occlusion_test.cpp`, stride 16) are now registered verbatim,
> unmodified, and pass on both modules.
>
> **Cube textures are implemented too** (`LLGL-26`, 2026-07-31): a new `LlglTextureCubeBackend`
> backing `CreateTextureCube` — one `LLGL::TextureType::TextureCube` resource, 6 array layers, the
> project's own face order already matching the Vulkan/GL cube-image convention LLGL itself uses,
> so `face` maps straight to `baseArrayLayer` with no remapping. Worked cleanly on the first build
> and test run: the full `CnaTests` regression baseline under this backend dropped from 18 failures
> to just 4, all pre-existing and unrelated (MRT, and three EasyGL-dialect-GLSL-fixture failures —
> see `LLGL-22`'s own updated entry). `EnvironmentMapEffect` itself — actually sampling a cube map
> from a 3D shader for reflections, not just creating and uploading one — remains its own separate,
> still-open item under `LLGL-25`.
>
> **Volume (`Texture3D`) textures are implemented too** (`LLGL-26`, 2026-07-31): a new
> `LlglTexture3DBackend` backing `CreateTexture3D` — one `LLGL::TextureType::Texture3D` resource,
> box-region `SetData`/`GetData` via `LLGL::TextureRegion`'s real 3D `offset`/`extent` (no
> array-layer indexing needed, unlike cube textures, since this uses genuine depth). Mip level count
> is computed from `(width, height)` **only** — depth does not participate, matching FNA's own
> `Texture3D` constructor and mirroring the Vulkan backend's own
> `CalculateVulkanTexture3DMipLevels` precedent exactly, confirmed by reading that code before
> writing this one rather than by naive analogy to the cube-texture formula (which would have been
> wrong). `SupportsCapability(CNA::GraphicsCapability::Texture3D)` now returns `true`. Worked
> cleanly on the first build and test run: all 39 `Texture3DTest` cases pass (previously skipped for
> lack of backend support), dropping the full `CnaTests` regression baseline's failure count to the
> same 4 pre-existing, unrelated failures as after cube textures (see `LLGL-22`'s own updated
> entry). No dedicated `Llgl_*` pixel test was added, for the same reasoning as cube textures:
> `Texture3DTest` already exercises real `SetData`/`GetData` round-trips against the real LLGL
> backend and swap chain this `CnaTests` build uses.
>
> **`EnvironmentMapEffect` is implemented too** (`LLGL-25`, 2026-07-31): unlike every other stock
> effect here, it gets its own dedicated vertex shader, fragment shader, pipeline layout and
> 84-float `EnvMapParams` uniform buffer pool (`env_map3d.{vert,frag}.glsl`/`.gl.{vert,frag}.glsl`,
> `primitiveEnvMapLayout_`, `envMapUniformBuffers_`) rather than reusing the shared 100-float
> `Transform` block, since its field set (Fresnel factor, environment map amount/specular) does not
> fit it and its shader pair is never linked with any other shader in this backend. The formula is
> transliterated directly from the Vulkan backend's own already-correct `env_map3d.frag.glsl`
> (itself the product of 3 previously-found-and-fixed formula bugs, see
> `docs/environmentmapeffect-support.md`) rather than re-derived. **One real bug found while adding
> it**: the fog blend's `mix()` argument order was copied from the Vulkan-standalone backend's own
> OPPOSITE `vFogFactor` convention while the vertex shader's `vFogFactor` computation was correctly
> copied from this backend's own (opposite) convention — every env-map draw with fog disabled (the
> default) rendered fully in `fogColor` (black) instead of its real colour. Fixed by swapping the
> `mix()` argument order to match this backend's own established fog convention (see
> `lit_textured3d.frag.glsl`). Reuses the shared, cross-backend
> `examples/environmentmapeffect_alphascaledlerp_test.cpp` verbatim (`Llgl_EnvironmentMapEffect_
> AlphaScaledLerp`, 2/2). **Not tested on the OpenGL module in this environment**: `CreateTextureCube`
> aborts there with `hasCubeTextures not supported` — a genuine, pre-existing GLX/llvmpipe
> limitation discovered while adding this test, not a regression; no `_OpenGL` CTest variant is
> registered for this reason. See `LLGL-25 (EnvironmentMapEffect)`'s own row for the full detail.
>
> **`SkinnedEffect` is implemented too** (`LLGL-25`, 2026-07-31): GPU vertex skinning, up to 4 bone
> weight/index pairs blended per vertex, gated at runtime by `WeightsPerVertex` rather than a
> compile-time-unrolled per-bone-count shader the way real XNA compiles 9 permutations of. Like
> `EnvironmentMapEffect`, gets its own dedicated vertex/fragment shader pair and pipeline layout
> (`skinned3d.{vert,frag}.glsl`/`.gl.` flavours, `primitiveSkinnedLayout_`) rather than the shared
> `Transform` block — plus a SECOND, separate 4608-byte `BoneBlock` buffer pool
> (`skinnedBoneBuffers_`) for the 72-`mat4` bone array, kept apart from the small per-draw
> parameter buffer pool, mirroring the Vulkan backend's own `BoneBlock`/`FogParams` UBO split. Two
> real, independent gaps closed to make this possible (found by reading the code, not by a failing
> test): `MapVertexUsage()` had no cases for `VertexElementUsage::BlendWeight`/`BlendIndices` at
> all (the real-`VertexDeclaration` attribute path), and `ResolveVertexAttributes()`'s
> declaration-less stride-inference switch (`LLGL-32`) had no case for stride 52
> (`VertexPositionNormalTextureSkinned`'s own GPU-packed size) — every test drives `SkinnedEffect`
> through exactly that path, so this second gap would have thrown without the fix. Two tests
> ported from the Vulkan backend's own fully backend-agnostic sources (`Llgl_SkinnedEffect_
> IdentityBones`/`Llgl_SkinnedEffect_TwoBoneBlend`, 1/1 each on **both** modules — unlike
> `EnvironmentMapEffect`, no cube texture is needed, so the OpenGL module's `hasCubeTextures`
> limitation does not apply here). Worked cleanly on the first build and test run — no bugs found.
> See `LLGL-25 (SkinnedEffect)`'s own row for the full detail.
>
> **`RenderTargetCube` is implemented too** (`LLGL-26`, 2026-07-31): ONE shared 6-layer
> `LLGL::TextureType::TextureCube` colour texture plus ONE shared depth/stencil texture (matching
> FNA's own `RenderTargetCube`, one depth buffer for the whole cube, not one per face), and 6
> `LLGL::RenderTarget`s built once at construction, each attaching the shared colour texture at a
> different `arrayLayer`. A new `LlglBoundRenderTarget` common interface lets
> `currentRenderTargetBackend_` point at either a plain `RenderTarget2D` or one cube face without
> any queue/replay code needing to know which — `GroupFrameCommandsByTargetEXT` already groups
> purely by `LLGL::RenderTarget*` pointer identity, so 6 distinct per-face pointers just work,
> zero changes needed there. **One real bug, found by reasoning before it reached a test**:
> `EnvironmentMapEffect`'s cube-texture resolution was a hard `dynamic_cast` that would have
> silently failed to sample a `RenderTargetCube` — the entire real-time-reflection use case this
> feature exists for — fixed with a new `ResolveSampledTextureCube()` helper. A new dedicated
> `examples/llgl_rendertargetcube_test.cpp`, `Llgl_RenderTargetCube`, 9/9, passed cleanly on the
> first run: construction, 6 independent per-face `Clear`+`GetData()` round trips, back-buffer
> independence, and sampling the result through `EnvironmentMapEffect`. `preserveContents`/
> `mipMap`/`multiSampleCount` are ignored in this first cut, matching `RenderTarget2D`'s own
> already-accepted scope. See `LLGL-26`'s own row for the full detail, including this project's
> elaborate shared cross-backend `RenderTargetCube` oracles (`rendertargetcube_usage_test.cpp`,
> `rendertargetcube_getdata_contract_test.cpp`, `rendertargetcube_msaa_face_test.cpp`) that are
> NOT yet wired up for LLGL — left as a documented follow-up rather than guessed at.
>
> **Multiple Render Targets (MRT) are implemented too** (`LLGL-26` follow-up, 2026-07-31), scoped
> to a deliberately narrower first cut than this project's other MRT-capable backends: 2-4
> `RenderTarget2D` slots only (mixing in a `RenderTargetCube` face is refused by name), bound and
> written by a custom multi-output `ShaderEffect` drawn through `SpriteBatch` only -- a 3D
> colour-only draw (`DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`) while an MRT set is bound throws
> by name too, since no stock effect family in this backend declares more than one fragment output
> and real XNA MRT is only meaningfully useful through a custom Effect anyway. A new
> `LlglMRTBinding` combines the N bound targets' own (borrowed, still owned by them) colour
> textures plus a fresh anonymous depth attachment into ONE `LLGL::RenderTarget`, owned by
> `LlglGraphicsBackend` itself (`currentMrtBinding_`) rather than by any XNA-visible object, since
> an MRT bind has no owning XNA object of its own. `GetPrimaryRenderPassEXT()` now returns the
> CURRENTLY bound target's own render pass (a real, pre-existing `LLGL::RenderTarget::GetRenderPass()`
> accessor) instead of always the swap chain's, and `MakeBlendPipelineKey`/
> `FillCurrentBlendAndRasterStateEXT` both grew a `colorAttachmentCount` parameter so a cached
> pipeline's blend-target count and render pass match whatever is actually bound -- both
> `AcquireSpritePipeline` and `LlglEffectBackend::AcquirePipeline` thread it through. Per-slot
> `ColorWriteChannels1..3` are not supported: `FillCurrentBlendAndRasterStateEXT` fills every
> active slot identically to slot 0's own write mask, a documented simplification rather than a
> silent gap. `GroupFrameCommandsByTargetEXT` needed zero changes -- it already groups purely by
> `LLGL::RenderTarget*` pointer identity, so the MRT bind's own combined pointer just works. A new
> `examples/llgl_mrt_test.cpp`, `Llgl_MRT`/`_OpenGL` (unlike `RenderTargetCube`, plain
> `RenderTarget2D` slots work on both modules), 9/9, passed cleanly on the first run: a real
> 2-output custom `ShaderEffect` writes two DIFFERENT values to two simultaneously bound targets
> from the SAME draw call, the 3D-draw-during-MRT refusal, back-buffer isolation, and that an
> ordinary single-target draw still works correctly once the MRT bind ends.
> `GraphicsCapability::MultipleRenderTargets` now reports `true`.

---

## Why an LLGL backend

Every other CNA backend names a native graphics API (`VULKAN`, `D3D11`, `EASYGL`, …) or a
platform-provided abstraction (`SDL_GPU`, `BGFX`, `WEBGPU`). LLGL is a thin, hand-written C++
abstraction over OpenGL / Vulkan / Direct3D 11 / Direct3D 12 / Metal, with an API shaped very much
like modern explicit APIs (command buffers, pipeline state objects, pipeline layouts, resource
heaps) but far smaller than bgfx.

That makes it interesting to CNA for two distinct reasons:

1. **A second, independent multi-API abstraction** to compare against bgfx and SDL_gpu. Where those
   two hide their backend choice almost entirely, LLGL exposes it as a named module loaded at
   runtime, which lets one CNA build genuinely switch native API without recompiling.
2. **Reach on platforms CNA does not otherwise cover well** — LLGL's Metal and Direct3D 12 modules
   are first-class, so this backend is the natural future home for a macOS/iOS target that does not
   go through MoltenVK.

The cost is that CNA now depends on someone else's abstraction of an abstraction: a defect can live
in CNA, in LLGL, or in the native driver, and telling them apart takes a spike outside CNA (this
plan's own `LLGL-17` investigation did exactly that).

---

## Design decisions

| # | Decision | Rationale |
| --- | --- | --- |
| 1 | **The renderer module is chosen at runtime, not at CMake time.** `Detail::ResolveRendererModule()` walks a preference list and loads the first module that works, caching the answer for the process. | This is the one thing LLGL offers that no other CNA backend does. Making it a compile-time choice would throw it away. |
| 2 | **Default preference is Vulkan, then OpenGL.** Overridable with `CNA_LLGL_RENDERER=auto\|opengl\|vulkan\|null`. | Chosen by the project owner. Note that LLGL itself marks its Vulkan module experimental while its OpenGL module is the mature one — the preference deliberately does not follow LLGL's own maturity ranking. |
| 3 | **The Null module is never selected automatically.** It is compiled in and reachable only through an explicit `CNA_LLGL_RENDERER=null`. | A renderer that accepts every command and draws nothing would turn "no usable GPU" into a silent black screen — the exact class of fabricated success this project's backends must never produce. |
| 4 | **CNA keeps owning the window; LLGL renders into it.** `LlglSdlSurface` adapts the existing SDL3 window to `LLGL::Surface`; LLGL's own `Window`/`Display` layer is never used to create one. | The window belongs to `GraphicsDevice`, is shared with SDL input/event handling, and exists before any backend does. |
| 5 | **Only the OpenGL module gets `SDL_WINDOW_OPENGL`.** The Vulkan module builds its surface from the native window handle and needs no SDL flag. | SDL refuses to create a window that is both `SDL_WINDOW_OPENGL` and `SDL_WINDOW_VULKAN`, so the flag has to follow the runtime module decision — which is why `ResolveRendererModule()` caches: `GraphicsDevice` asks before the window exists and the backend asks after. |
| 6 | **X11 only, for now.** A Wayland SDL window is refused with a clear error naming `SDL_VIDEODRIVER=x11`. | LLGL 0.04b compiles Wayland support only when explicitly enabled, and this integration does not enable it. Refusing beats handing LLGL a handle it cannot present to. |
| 7 | **The X11 visual is reported to LLGL, not left for LLGL to choose.** | A GLX context created for a visual other than the drawable's cannot be made current. The SDL window already committed to a visual when it was created, so it is the only one that can work. |
| 8 | **Both shader flavours are checked in, and the choice is made from the module's reported shading language, GLSL first.** Vulkan gets SPIR-V words, OpenGL gets GLSL source. | A build needs no shader toolchain — same discipline as the Bgfx and SDL_GPU backends' generated headers. The GLSL-first order is load-bearing, not cosmetic: a modern OpenGL module reports SPIR-V too, and accepting it there silently breaks every binding (see `LLGL-17`). |
| 9 | **The whole frame is buffered on the CPU and recorded at `Present()`.** Clears and draws first enter one logical command list; replay currently groups them by render-target identity. | LLGL forbids buffer uploads inside a render pass. Deferral keeps uploads outside render passes, but target-identity bucketing does **not** preserve call order across target transitions and lets later buffer mutations affect earlier queued draws. LLGL-45 and LLGL-46 must replace those two unsafe consequences without losing the upload constraint. |
| 10 | **Sprite geometry is baked into window pixels on the CPU; the GPU viewport stays at the full window.** Letterboxing lives in the geometry, XNA's sub-`Viewport` clipping in the scissor. | Keeps one projection constant for a whole frame, which is what makes decision 9 cheap. |
| 11 | **Clip space is treated as Y-up on every module.** | LLGL submits Vulkan viewports with a negated height, flipping Vulkan's natively Y-down clip space to match OpenGL's. `RenderingCapabilities::screenOrigin` describes viewport/scissor *rectangle* space, not clip space — keying the projection's Y sign off it renders the scene upside down (found by reading back real pixels, see `LLGL-13`). |
| 12 | **`LLGL_ENABLE_EXCEPTIONS=ON`.** | LLGL's `LLGL_TRAP` aborts the process outright when exceptions are off. CNA reports backend failures as exceptions everywhere else, so an abort would replace a reportable failure with a crash. |
| 13 | **`CXX_EXTENSIONS ON` for LLGL's own targets only.** | LLGL's `LLGL_VA_ARGS` macro is built on the `, ## __VA_ARGS__` GNU extension; with CNA's `CMAKE_CXX_EXTENSIONS OFF` inherited, LLGL does not compile at all. |
| 14 | **The LLGL archives are linked as a link group.** | The core archive and its modules genuinely reference each other, and CMake orders the modules ahead of the core. |

---

## Naming conventions for this backend

| Thing | Convention |
| --- | --- |
| Task prefix | `LLGL-` |
| CMake option value | `CNA_GRAPHICS_BACKEND=LLGL` |
| Compile definition | `CNA_BACKEND_LLGL` (plus `CNA_LLGL_HAS_OPENGL`/`_VULKAN`/`_NULL`) |
| Backend target | `cna_backend_graphics_llgl` |
| Source directory | `src/CNA/Internal/Backends/Llgl/` |
| C++ namespace | `CNA::Internal::Backends::Llgl` |
| Class prefix | `Llgl` (e.g. `LlglTextureBackend`) |
| Build directory | `cmake-build-llgl/` |

---

## Phase LLGL-1 — Infrastructure and CMake integration

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-1 | Add `LLGL` as a valid `CNA_GRAPHICS_BACKEND` value: the `CACHE STRING`/`STRINGS` list, the `option(CNA_BACKEND_LLGL ...)` declaration, and the mutual-exclusion list. | ✅ | Done 2026-07-31. `tests/.../GraphicsBackendCompileDefinitionTests.cpp`'s `ExactlyOneGraphicsBackendIsSelected` counter and `CNA::GraphicsBackendType` were updated in the same change — both would otherwise be wrong under this backend. |
| LLGL-2 | Add the selection branch setting `BACKEND_DIR`/`BACKEND_TARGET`/`CNA_BACKEND_LLGL`, and `cmake/ThirdPartyLLGL.cmake` with `cna_configure_llgl()`. | ✅ | Done 2026-07-31. Pinned FetchContent tag `Release-v0.04b`, with `CNA_LLGL_ROOT` for an existing checkout. Vulkan module defaults to whatever `find_package(Vulkan QUIET)` reports, so a host without a Vulkan SDK still configures. |
| LLGL-3 | Link the backend target against the LLGL module archives. | ✅ | Done 2026-07-31 — see design decision 14. Found empirically as "undefined reference to `LLGL::ModuleOpenGL::AllocRenderSystem`". |
| LLGL-4 | Runtime renderer selection (`LlglRendererSelection.hpp/.cpp`): module enum, name mapping, compiled-in query, env override parsing, preference resolution, cached probe. | ✅ | Done 2026-07-31. Covered by 5 unit tests in `GraphicsBackendCompileDefinitionTests.cpp` (default preference never contains Null, override parsing, invalid-value rejection, module names, which module needs a GL window). |
| LLGL-5 | `GraphicsDevice::getBackendWindowFlags()` follows the runtime module decision. | ✅ | Done 2026-07-31 — see design decision 5. |

---

## Phase LLGL-2 — Surface, device and swap chain

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-6 | `LlglSdlSurface`: SDL3 window → `LLGL::Surface`, including the real X11 visual and a clear error for a non-X11 driver. | ✅ | Done 2026-07-31. Xlib is confined to that one translation unit and its macros (`None`, `Status`, …) undefined immediately, so no XNA header ever sees them. |
| LLGL-7 | Load the render system, create the swap chain (24-bit depth, 8-bit stencil, requested sample count), command buffer and queue. | ✅ | Done 2026-07-31. `CNA_LLGL_DEBUG=1` additionally turns on LLGL's own debug layer — which is what found `LLGL-12`'s bind-flag defect within seconds. |
| LLGL-8 | Presentation: virtual resolution, all five `CnaPresentationMode` policies, swap interval, window↔logical coordinate transforms, resize. | ✅ | Done 2026-07-31. Verified for `FixedHeightDynamicWidth` (the default) by `Llgl_Smoke`; the other four modes share one code path and are not separately pixel-verified yet. |
| LLGL-9 | First end-to-end proof: real window, 60 frames of clear + present, clean exit. | ✅ | Verified 2026-07-31 — `examples/llgl_smoke_test.cpp` / `Llgl_Smoke`, 8/8 checks on Vulkan (lavapipe) under Xvfb. |

---

## Phase LLGL-3 — 2D vertical slice

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-10 | Sprite shaders in both flavours plus `compile_shaders.py`, which compiles the Vulkan flavour to SPIR-V and embeds the OpenGL flavour verbatim into one generated header. | ✅ | Done 2026-07-31. `--check` mode fails if the checked-in header is stale. |
| LLGL-11 | `Texture2D`: creation from `ImageData`, `SetData` per level, GPU readback for `GetData`. | ✅ | Done 2026-07-31. Readback verified indirectly through the back-buffer path; a direct `Texture2D::GetData()` round-trip test is still owed (`LLGL-19`). |
| LLGL-12 | `SpriteBatch`: pipeline layout, per-blend-state pipeline cache, per-sampler-state sampler cache, quad building with rotation/origin/flip, frame recording and submission. | ✅ | Done 2026-07-31. Two real defects were found by asserting pixels rather than absence of exceptions: the readback texture lacked `CopyDst`/`CopySrc` (so the "cleared to black" check was passing against a zero-initialised buffer, not against the frame), and the staging texture must take the swap chain's own colour format or a B8G8R8A8 swap chain hands back byte-swapped pixels. |
| LLGL-13 | Correct orientation. | ✅ | Done 2026-07-31 — design decision 11. The first implementation keyed the projection's Y sign off `screenOrigin` and rendered every sprite vertically mirrored; caught by the quadrant check in `Llgl_2D`, not by inspection. |
| LLGL-14 | Blend state, blend factor, sampler state (complete min/mag/mip triples for all nine `TextureFilter` values), scissor, viewport. | ✅ | Done 2026-07-31. `SetBlendFactor` is only emitted when the blend state actually uses `BlendFactor`/`InverseBlendFactor` — see `LLGL-18`. |
| LLGL-15 | Back-buffer readback (`ReadBackbuffer`), used by `GraphicsDevice::GetBackBufferData` and every pixel test. | ✅ | Done 2026-07-31. The whole back buffer is captured once per frame and every region served from that capture: the swap chain's render pass loads its colour attachment as `Undefined`, so re-entering it for a second copy would read discarded content. |
| LLGL-16 | Pixel-asserted 2D proof. | ✅ | Verified 2026-07-31 — `examples/llgl_2d_test.cpp` / `Llgl_2D`, 10/10 checks on Vulkan (lavapipe) under Xvfb. |

---

## Phase LLGL-4 — Known gaps and open questions

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-17 | **The OpenGL module clears but draws nothing.** | ✅ | Filed and fixed 2026-07-31. **The cause was CNA's, not LLGL's.** A standalone LLGL-only spike narrowed it down step by step: a quad with an identity matrix rendered in the right place (so position, pipeline and render pass were fine) while `texCoord`, `color` and the uniform block all read as zero; explicit `layout(location=)`/`layout(binding=)` qualifiers changed nothing; a `ResourceHeap` instead of individual `SetResource` bindings changed nothing; and a fragment shader hardcoded to output magenta still rendered black — which is what finally ruled out the binding path entirely. Instrumenting LLGL's own `GLLegacyShader::CompileShaderSource` showed it was never called: LLGL's GL core profile advertises `ShadingLanguage::SPIRV` (`GL_ARB_gl_spirv`) alongside GLSL, and this backend's selection checked SPIR-V first, so the OpenGL module was being handed SPIR-V compiled for Vulkan's binding model. Fixed by preferring GLSL wherever a module offers it. Both flavours are now pixel-verified: `Llgl_2D_OpenGL` 10/10, `Llgl_2D` (Vulkan) 10/10. |
| LLGL-18 | `SetBlendFactor` on OpenGL hits `ErrUnsupportedGLProc: glBlendColor` on this environment's context. | ✅ | Fixed 2026-07-31 by requesting dynamic blend-factor state, and emitting the call, only when the blend state genuinely references `Blend::BlendFactor`/`InverseBlendFactor` — correct, cheaper, and it keeps the overwhelming majority of blend states off a proc some GL tables genuinely lack. **A first pass documented this workaround before implementing it**; the gap surfaced immediately once the OpenGL module actually drew and hit the unimplemented path. A game that really uses `Blend::BlendFactor` on such a driver still fails loudly, with LLGL's own error. |
| LLGL-19 | Byte-exact texture upload/readback round-trip test. | ✅ | Done 2026-07-31 — `examples/llgl_texture_readback_test.cpp`, `Llgl_TextureReadback` (+ `_OpenGL`), 6/6 on both modules. Deliberately drives `ITextureBackend` directly rather than `Texture2D::GetData()`: the public API answers from its own CPU pixel shadow whenever it has one, so a test written against it would pass without the GPU being asked anything. Covers full-surface and sub-rectangle reads, `UpdatePixels`, per-level `UpdatePixelsLevel` (level 0 keeps its own different content), and the two refusals — an undersized destination buffer and a level the texture does not have — because a backend that half-filled a buffer and returned true would hand the shared layer fabricated pixels. |
| LLGL-20 | Pixel-verify all five presentation modes. | ✅ | Done 2026-07-31 — `examples/llgl_presentation_test.cpp`, `Llgl_Presentation` (+ `_OpenGL`), 6/6 on both modules. An 800x480 window with a 100x100 canvas (a deliberately different aspect ratio, since a matching one makes Letterbox, Overscan and Stretch indistinguishable): each mode draws a full-canvas sprite and the test asserts both where it appears and where it does not — a letterbox bar still at the clear colour is what separates "fitted" from "stretched over everything". Also covers the logical↔window transform round-trip. **This test found and closed a real limitation**: `ReadBackbuffer` used to throw outright for any non-1:1 presentation, so no pixel test could run under a scaled canvas at all. It now resolves a scaled logical pixel to the window pixel at the centre of the block it covers — nearest-neighbour, deliberately not an average, so every value returned is a colour the frame genuinely contained. A real window resize is proven too now (`LLGL-29`). |
| LLGL-21 | `BlendState.MultiSampleMask` and the per-MRT colour write masks for slots 1..3. | 🟨 | **`ColorWriteChannels1..3` are implemented now** (2026-08-01): `colorWriteChannels_` grew from one `int` to a 4-element array, `ApplyBlendState`'s already-passed `BlendWriteState::colorWriteChannels[4]` (the shared `GraphicsDevice.cpp` layer already forwarded all four slots -- only this backend was dropping slots 1..3 on the floor) is stored per slot, and `FillCurrentBlendAndRasterStateEXT`'s loop reads `colorWriteChannels_[slot]` instead of one shared value; `MakeBlendPipelineKey` folds all four into its cache key. **One real bug found and fixed, not a driver limitation**: LLGL only reads `blend.targets[i]` PER ATTACHMENT when `GraphicsPipelineDescriptor::blend.independentBlendEnabled` is explicitly `true` -- otherwise it silently reuses `targets[0]` for every attachment regardless of what `targets[1..3]` were set to (confirmed by reading `VKGraphicsPSO.cpp`'s own `CreateColorBlendState`: `desc.targets[desc.independentBlendEnabled ? i : 0]`, and `GLBlendState.cpp`'s identical `if (desc.independentBlendEnabled)` branch). This backend never set that flag, so the per-slot masks it now computes would have silently done nothing the moment more than one attachment was bound. Fixed with `pipelineDesc.blend.independentBlendEnabled = (clampedCount > 1)`. Diagnosed by extending `Llgl_MRT` with a genuine 2-draw masking test, seeing slot 1's masked write land anyway, and reading LLGL's own Vulkan source rather than guessing. **Module-dependent once the real bug was fixed**: on this environment (Xvfb + Mesa) the Vulkan module (lavapipe) genuinely masks slot 1 (a real per-attachment `VkPipelineColorBlendAttachmentState.colorWriteMask`); the OpenGL module (llvmpipe via GLX) does not -- slot 1 still reads back the unmasked value, meaning `glColorMaski`'s own per-draw-buffer masking is not honoured by this environment's GL driver, a real GL constraint (not a CNA defect) `Llgl_MRT`'s own new check detects and reports `[SKIP]` for on that module, matching the existing `WireFrame`/back-buffer-MSAA precedent, rather than failing or papering over it. `Llgl_MRT`/`_OpenGL` grew from 9/9 to 11/11 checks (Vulkan) / 10 pass + 1 `[SKIP]` (OpenGL). **`MultiSampleMask` is still deliberately not applied**: LLGL's sample mask lives in the blend descriptor and would multiply the pipeline cache with no real use on this backend yet -- the one remaining item in this row's own scope. |
| LLGL-22 | Full `CnaTests` regression baseline under `-DCNA_GRAPHICS_BACKEND=LLGL`. | ✅ | **Down to 3 failures out of 5698 as of 2026-07-31 (5688 passed, 7 skipped)**, after MRT (`LLGL-26` follow-up) closed `GraphicsDeviceCapabilityTest.SupportsMultipleRenderTargets` and fixed `GraphicsDeviceValidationTest.SetRenderTargets_FourTargets_DoesNotThrow` (moved out of the single-target-backend `#if` branch alongside `SDL_RENDERER`/`ASCII`/`DX3`, now that LLGL genuinely supports a 4-slot MRT bind). The remaining 3 are all pre-existing, documented, and unrelated: `CnjEffectTest.LoadsRealCnjFixture`/`CnjStockEffectTest.CustomGlslEffectStillWorks` (real content fixtures use EasyGL-dialect GLSL, incompatible with the `libshaderc`-compiled SPIR-V this backend's custom `ShaderEffect` path needs — `LLGL-27`'s own custom-effect scope was never meant to cover arbitrary pre-authored GLSL dialects), and `XnbContainerFuzzTest.MutatedRealModelFixtureNeverCrashesAndOnlyFailsCleanly` (the same EasyGL-dialect-fixture class of failure, surfaced through a mutated/fuzzed model instead of a Cnj effect). Earlier history for reference: before MRT the baseline was 5687 passed / 7 skipped / 4 failed; after cube texture support it was 5649 passed / 45 skipped / 4 failed; after `LLGL-25`'s full scope (texturing through lighting) it was 5635 passed / 45 skipped / 18 failed; original baseline before any 3D/effect work was 5606 passed / 70 skipped / 22 failed of 5698. `GraphicsDeviceValidationTest`'s `SetRenderTargets` expectations were fixed (not tolerated) once `LLGL-26` gave this backend real `RenderTarget2D`/MRT support, following the same registration-gap precedent as `ASCII-5`/`DX3-86`. |
| LLGL-31 | Lighting without a texture. | ✅ | Done 2026-07-31. A fourth vertex/fragment shader pair, `lit_colored3d.{vert,gl.vert}.glsl`/`lit_untextured3d.{frag,gl.frag}.glsl`, mirrors `lit_textured3d`/`lit_colored_textured3d` minus the texture coordinate attribute/`colorMap` sample -- reuses `primitiveLayout_` (no texture/sampler bindings), never `primitiveTexturedLayout_`. `AcquirePrimitiveVertexShader`'s `lit && textured` / `lit && hasColor` / `textured` / `hasColor` branch chain and `AcquirePrimitivePipeline`'s fragment-shader ternary needed no changes to the attribute-trimming loop or `pipelineLayout` selection -- both were already generic over this case. Still requires vertex colours, matching the pre-existing unlit-untextured restriction: a lit, untextured, colourless draw still throws "an untextured draw needs vertex colours" by name. `examples/llgl_lighting_test.cpp` Check G rewritten from "expect a throw" into a positive pixel test (a hand-declared `VertexPositionNormalColor` local struct, since no stock XNA vertex type combines Normal and Color) verifying a lit white surface and a red-vertex-colour tint; Check H keeps the still-real "no colour, no texture" refusal. `Llgl_Lighting`/`_OpenGL` grew from 8/8 to 10/10 on both modules. |
| LLGL-30 | `FillMode::WireFrame` on the Vulkan module. | ⬜ | Works and is pixel-verified on the OpenGL module; LLGL's Vulkan module does not enable the device feature a line polygon mode needs and draws nothing at all, so the backend refuses the request there rather than presenting an empty frame. `SupportsCapability(WireFrame)` answers per module, which made this the first capability in the project whose answer is a runtime fact -- `GraphicsDeviceCapabilityTest.DoesNotSupportWireFrame` was adjusted accordingly. |
| LLGL-32 | `GraphicsDevice::DrawUserPrimitives()`'s typed overloads (`VertexPositionColor`/`VertexPositionTexture`/`VertexPositionColorTexture`/`VertexPositionNormalTexture`, ...) all throw on this backend. | ✅ | Done 2026-07-31. Found while implementing `LLGL-25`'s `DualTextureEffect` (which is why the dedicated `examples/llgl_dualtexture_test.cpp` was originally written instead of reusing the pre-existing, cross-backend `examples/dualtextureeffect_vertexcolor_test.cpp` — now BOTH are registered, exercising two different upload paths to the same feature). Every typed `DrawUserPrimitives` overload calls `IGraphicsBackend::CreateVertexBuffer(int count)` (count-only, no `VertexDeclaration`) and then a raw byte `SetData`, so `LlglVertexBufferBackend::GetVertexAttributes()` came back empty and `QueuePrimitives()` refused the draw by name. **Fix**: `LlglVertexBufferBackend::ResolveVertexAttributes()` already had a stride-based fallback for the no-declaration case, but it only ever recognised stride 16 (`VertexPositionColor`) -- extended to all four GPU-packed stream sizes `GraphicsDevice.cpp`'s own `DrawUserPrimitives` overloads produce (`CNA::Internal::Graphics::Position{Color,Texture,ColorTexture,NormalTexture}Stream`, 16/20/24/32 bytes, each a distinct size so the stride alone identifies which one was used), mirroring the SAME "infer the vertex format from the upload stride" precedent the Vulkan backend's own `MakeExt3DKey()` already relies on for these exact same stream sizes. `examples/dualtextureeffect_vertexcolor_test.cpp` (stride 24, `Llgl_DualTextureEffect_VertexColor`) and `examples/graphicsdevice_default_state_occlusion_test.cpp` (stride 16, `Llgl_GraphicsDevice_DefaultStateOcclusion`) — both pre-existing, cross-backend shared sources, verbatim reuse, no LLGL-specific code — are now registered and pass on both modules, 2/2 and 2/2 (+ `_OpenGL`). |
| LLGL-29 | Real window resize (`ResizeBuffers` and the presentation rect following it). | ✅ | Done 2026-07-31 — `examples/llgl_resize_test.cpp`, `Llgl_Resize` (+ `_OpenGL`), 8/8 on both modules. Drives a real resize the way a game actually does it (`GraphicsDeviceManager.PreferredBackBufferWidth/Height` + `ApplyChanges()`, which resizes the real SDL window through `GameWindow::EndScreenDeviceChange`), grows past the original size, shrinks below it, and re-applies a square virtual canvas with `Letterbox` mode afterward -- covering `GetViewportSize()` following the resize, a pixel only reachable in the newly grown area, a read at the old (now out-of-range) bounds throwing rather than returning garbage, and the letterbox rect recomputing from the CURRENT physical resolution rather than a cached one. **Two real timing issues found and fixed, neither reachable from any test that only used the DEFAULT 800x480 size**: (1) `SDL_SetWindowSize()` (reached from both the very first `CreateDevice()` and every later `ApplyChanges()`) is not guaranteed synchronous under X11 -- even under a bare Xvfb with no window manager, the X server applies it asynchronously, so `LlglSdlSurface::GetContentSize()` (and, worse, the Vulkan driver's own surface-capabilities query) can still observe the OLD size for a moment after the call returns; calling `UpdateSwapChainResolution()`/`Present()` immediately afterward with no settling step produced a `VK_ERROR_OUT_OF_DATE_KHR` crash on `vkQueuePresentKHR`. Fixed in the TEST (not the backend -- a real game's own resize handling has the exact same obligation) by calling `SDL_SyncWindow()` + `SDL_PumpEvents()` before every post-resize `Present()`. (2) `ReadBackbuffer()` addresses LOGICAL (virtual-resolution) coordinates, not window pixels -- a letterbox bar cannot be named through it at all, since the bars sit outside the logical canvas by definition; fixed by following `llgl_presentation_test.cpp`'s own established pattern of switching to a 1:1 `NativeBackBuffer` presentation purely for reading back, after the frame's geometry has already been baked into window pixels by the draw call. |
| LLGL-23 | MSAA back buffer. | ✅ | Done 2026-07-31 — `examples/llgl_msaa_test.cpp`, `Llgl_Msaa` (+ `_OpenGL`), 7/7 on Vulkan (5/7 + 2 documented `[SKIP]`s on OpenGL — see below). A right triangle's diagonal hypotenuse is scanned transversally at the one pixel whose centre sits exactly on the geometric edge (perpendicular distance zero), so any real multisample pattern must split that pixel's samples across it, while single-sample rendering is always a clean in-or-out decision — proving genuine antialiasing rather than merely "didn't crash". **Real, non-test finding along the way**: `MultiSampleCount` is honoured by this backend ONLY at swap-chain construction time (`requestedSampleCount_` read once in the constructor, forwarded straight into `LLGL::SwapChainDescriptor::samples`) — there is no `ApplyMultiSampleCount()` override, matching EasyGL's own documented precedent. Since a `Game`'s single, eagerly-constructed `GraphicsDevice` is always built with default `PresentationParameters` (`MultiSampleCount=0`) before any `GraphicsDeviceManager` preference can possibly reach it, **MSAA can never actually be enabled through the ordinary `Game` + `GraphicsDeviceManager.ApplyChanges()` flow on this backend** — confirmed with a throwaway probe (`GetMultiSampleCount()` stayed 0 with `PreferMultiSampling=true`) before the real test was written. The test therefore constructs two independent, raw `GraphicsDevice` objects directly (mirroring `examples/dx3_resize_transaction_test.cpp`'s own established pattern for exactly this reason), each with its own window and an explicit `PresentationParameters.MultiSampleCount`. **Module-dependent, like `WireFrame`/`AnisotropicFiltering` already are**: on this environment (Xvfb + Mesa), the Vulkan module (lavapipe) genuinely applies the requested sample count; the OpenGL module (llvmpipe via GLX) does not — `GetMultiSampleCount()` stays 0 at every requested count (1/2/4/8 all tried), with nothing reported even under `CNA_LLGL_DEBUG=1` — a real GLX/driver constraint, not a CNA bug. The test detects this at runtime and reports `[SKIP]` for the two sample-count-dependent checks on that module instead of failing, while still asserting `ReadBackbuffer` returns a sane result either way. |

> Supersession note: LLGL-21's historical final sentence says `MultiSampleMask` was still unapplied at
> that point. LLGL-33 later wired it into the descriptor; the remaining defect is the truncated
> pipeline-cache identity tracked by LLGL-48, plus LLGL's OpenGL-module limitation.

---

## Phase LLGL-5 — 3D pipeline (implemented; conformance gaps tracked in Phase LLGL-8)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-24 | Vertex/index buffer draw path: translate `VertexDeclaration` into LLGL vertex attributes, per-layout pipelines, `DrawColoredPrimitives`/`DrawIndexedColoredPrimitives`. | ✅ | Done 2026-07-31 — `examples/llgl_3d_test.cpp`, `Llgl_3D` (+ `_OpenGL`), 12/12 on both modules through the public `BasicEffect`/`VertexBuffer`/`DrawPrimitives` API. The declaration translation feeds both the OpenGL vertex array and the Vulkan pipeline's input layout from one source, so the two cannot drift; attribute locations are assigned by usage, not declaration order. `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` are overridden specifically to honour `vertexStart`/`startIndex`/`baseVertex`, which the shared interface's default silently drops (a defect filed by name against other backends here). An effect asking for anything past vertex colours fails by name. **Four real findings, each caught by pixels rather than review:** a 3D draw refers to the caller's GPU buffers, so a `VertexBuffer` destroyed in the same frame left the recorded frame pointing at freed memory — buffer release is now deferred until the frame is submitted; XNA's `CreateOrthographicOffCenter` is right-handed, so visible geometry has negative z and the first version of the test clipped everything away; the winding the rasterizer sees is screen winding, because LLGL's Y-up clip space and its negated Vulkan viewport height cancel out; and depth testing against a never-cleared depth buffer produced a triangle drawn everywhere except near its apex. |
| LLGL-25 | `BasicEffect` family (`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`), depth/stencil state, cull and fill modes. | ✅ | **Textures, `DiffuseColor`, `Alpha`, vertex-colour modulation, fog, `AlphaTestEffect` and lighting all done 2026-07-31** — `examples/llgl_basiceffect_test.cpp` + `examples/llgl_lighting_test.cpp`, `Llgl_BasicEffect`/`Llgl_Lighting` (+ `_OpenGL`), 15/15 and 10/10 on both modules. All 3D shaders share one 400-byte uniform block (documented in `shaders/effect3d_common.glsl.inc`) -- the unlit shaders declare only the first 144 bytes of it, the lit ones the full block, so growing it for lighting needed no change to any existing shader's own declaration. The vertex-shader variant is chosen from what the vertex LAYOUT carries, not from what the effect asked for -- a shader declaring an input the buffer does not supply reads undefined data on Vulkan -- and a textured/lit effect drawn from a layout missing the attribute it needs is refused by name. Lighting is per-pixel only, matching the documented, accepted deviation every established CNA backend except D3D9 already has (`GpuDrawParams::preferPerPixelLighting`'s own comment). **Lighting without a texture, provided the draw carries vertex colours, is also done** (`LLGL-31`) -- a lit, untextured, and colourless draw is the one combination still refused by name. Depth/stencil state, cull and fill modes were done in `LLGL-24`. **`DualTextureEffect` is done too** (2026-07-31) -- see its own paragraph below. **Real bug found and fixed along the way**: `VertexColorEnabled` was never actually read by ANY colour-carrying unlit/lit shader (`colored3d`, `colored_textured3d`, `lit_colored3d`, `lit_colored_textured3d`) -- the vertex colour attribute was multiplied into the tint UNCONDITIONALLY whenever the vertex buffer happened to carry one, regardless of the effect's own `VertexColorEnabled` setting. `llgl_basiceffect_test.cpp`'s own Check D was silently relying on this bug (it never set `VertexColorEnabled = true` and still expected the multiply to happen) -- fixed both the shaders (a new `vertexColorEnabledPad`/`ambientColorLighting.w`-backed uniform gate, `FillEffectUniforms` writing it) and the test (now sets the property explicitly, and gained two new checks proving `VertexColorEnabled=false` genuinely leaves a colour-carrying draw untinted). `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect` and `SkinnedPbrEffect` are all done too -- see their own paragraphs below. Every stock effect this row originally scoped is now implemented. |
| LLGL-25 (DualTexture) | `DualTextureEffect`. | ✅ | Done 2026-07-31 — `examples/llgl_dualtexture_test.cpp`, `Llgl_DualTexture` (+ `_OpenGL`), 3/3 on both modules. Reuses the plain textured/colored-textured vertex shader as-is (`dual_textured3d.frag.glsl`/`.gl.frag.glsl` are the only new shader files): DualTextureEffect samples its second texture through the SAME UV as the first, matching FNA's own `PSDualTexture` (`base = SAMPLE(Texture,uv); base.rgb *= 2; color = base * overlay * tint`, `overlay = SAMPLE(Texture2,uv)`, no alpha test). A new `primitiveDualTextureLayout_` pipeline layout adds a second, independently bound texture/sampler pair (bindings 4/5 alongside the existing 2/3) -- `FrameCommand` grew `texture2`/`sampler2` fields, and `ReplayFrameCommandsList` binds them at resource slots 3/4 (positional, matching the layout's own binding declaration order) only when set. `QueuePrimitives` now throws by name ("DualTextureEffect needs both Texture and Texture2 bound") if either texture is missing, rather than a null-dereference or a silently wrong draw. **Not reused from the pre-existing, cross-backend `examples/dualtextureeffect_vertexcolor_test.cpp`** (already registered on EasyGL/Vulkan/Bgfx) because that file drives `GraphicsDevice::DrawUserPrimitives()`, whose internal `backend_->CreateVertexBuffer(int)`-based buffer carries NO attribute layout at all on this backend -- a real, pre-existing gap affecting every `DrawUserPrimitives` typed overload here, not specific to `DualTextureEffect`, and left open rather than fixed as a side effect of this task. |
| LLGL-25 (EnvironmentMapEffect) | `EnvironmentMapEffect` -- actually SAMPLING a cube map for reflections (creation/upload, LLGL-26's own scope, was already done). | ✅ | Done 2026-07-31 — reuses the shared, cross-backend `examples/environmentmapeffect_alphascaledlerp_test.cpp` verbatim (already registered on EasyGL/Vulkan/Bgfx, Task 891's alpha-scaled base-lerp coverage), `Llgl_EnvironmentMapEffect_AlphaScaledLerp`, 2/2. Unlike every other effect here, EnvironmentMapEffect gets its OWN dedicated vertex shader (`env_map3d.vert.glsl`/`.gl.vert.glsl`, world-space normal/eye-vector), fragment shader (`env_map3d.frag.glsl`/`.gl.frag.glsl`) and pipeline layout (`primitiveEnvMapLayout_`: its own `EnvMapParams` uniform block at binding 1, `colorMap`/`samplerState` at 2/3, `envMap`/`envMapSampler` at 4/5) -- it does not reuse `AcquirePrimitiveVertexShader()`/the shared 100-float `Transform` block at all, since its field set (Fresnel factor, environment map amount/specular, no per-light specular or alpha test, no per-light BasicEffect-style ambient) does not fit that layout and its own vertex/fragment pair is never linked with any other shader in this backend. A new 84-float (336-byte) `EnvMapParams` per-draw uniform buffer pool (`envMapUniformBuffers_`/`envMapUniformData_`) mirrors `transformBuffers_`/`customEffectUniformBuffers_`'s own growth/reuse discipline. The formula (`FillEnvMapUniforms`, `env_map3d.frag.glsl`) is transliterated directly from the Vulkan backend's own already-correct `env_map3d.frag.glsl` (itself the product of 3 previously-found-and-fixed formula bugs -- lerp not additive base blend, alpha-scaled specular, alpha-scaled base-lerp target -- documented in `docs/environmentmapeffect-support.md`) rather than re-derived, per this project's Behavior Fidelity convention. **One real bug found while adding it, NOT present in the Vulkan source it was transliterated from**: the fog blend's `mix()` argument order was accidentally copied from the Vulkan-standalone backend's own OPPOSITE `vFogFactor` convention (there, 0=fully fogged; in this backend's own established convention, matching `lit_textured3d.frag.glsl`, 0=no fog) while the VERTEX shader's `vFogFactor` computation was correctly copied from this backend's own convention -- the mismatch made every env-map draw with fog disabled (`vFogFactor=0`, the default) render fully in `fogColor` (black, since fog is unconfigured) instead of the real lit/reflected colour, discovered via `mix(baseColor,...)` bisection against a `CNA_LLGL_DEBUG=1` run showing no validation errors and a series of isolating debug-shader edits. Fixed by swapping to `mix(rgb, fogColor.rgb, vFogFactor)` (both `.frag.glsl` flavours) to match this backend's own already-established fog convention instead of the source it was copied from. No fabricated white-texture/cube fallback for a null `Texture`/`EnvironmentMap` (mirrors `DualTextureEffect`'s own established convention) -- `QueuePrimitives` throws by name instead. **`EnvironmentMapEffect` is NOT tested on the OpenGL module in this project's own environment**: attempting `CreateTextureCube` there aborts with `"ValidateGLTextureType: ... LLGL::RenderingFeatures::hasCubeTextures not supported"` -- a genuine, pre-existing GLX/llvmpipe software-rasterizer limitation discovered while adding this test (cube textures were previously only ever exercised through CnaTests' default Vulkan-preferred `TextureCubeTest`, never through a `CNA_LLGL_RENDERER=opengl`-pinned CTest), not a regression in this task's own code; no `_OpenGL` CTest variant is registered for this reason (see the comment in `cmake/Tests/LlglTests.cmake`). |
| LLGL-25 (SkinnedEffect) | `SkinnedEffect` -- GPU vertex skinning (bone weight/index blend). | ✅ | Done 2026-07-31 — two tests ported (not verbatim-shared like the two above, but adapted with only the class name/comment changed, since the Vulkan backend's own `examples/vulkan_skinnedeffect_*_test.cpp` are already fully backend-agnostic real-XNA-API code) as `examples/llgl_skinnedeffect_identity_bones_test.cpp`/`llgl_skinnedeffect_twobone_blend_test.cpp`, `Llgl_SkinnedEffect_IdentityBones`/`Llgl_SkinnedEffect_TwoBoneBlend` (+ `_OpenGL`), 1/1 each on both modules -- unlike `EnvironmentMapEffect`, `SkinnedEffect` needs no cube texture and works cleanly on the OpenGL module too. Like `EnvironmentMapEffect`, gets its own dedicated vertex shader (`skinned3d.vert.glsl`/`.gl.vert.glsl`), fragment shader (`skinned3d.frag.glsl`/`.gl.frag.glsl`) and pipeline layout (`primitiveSkinnedLayout_`: its own `SkinnedParams` uniform block at binding 1, a SEPARATE `BoneBlock` (72 `mat4`s, 4608 bytes) at binding 2, `colorMap`/`samplerState` at 3/4) -- the bone array is kept in its own buffer/pool (`skinnedBoneBuffers_`/`skinnedBoneData_`, distinct from the small per-draw `skinnedUniformBuffers_`/`skinnedUniformData_` parameter pool) because it is far larger than every other per-draw uniform block in this backend, mirroring the Vulkan backend's own `BoneBlock`/`FogParams` UBO split. The skinning formula (up to 4 bone weight/index pairs blended, gated at runtime by `WeightsPerVertex >= 2.0`/`>= 4.0` rather than a compile-time-unrolled per-bone-count shader variant, matching FNA's real `Skin(vin, boneCount)` and Task 895's own established simplification) is transliterated directly from the Vulkan backend's own `skinned3d.vert.glsl`, including composing the bone-skin 3x3 with the outer world inverse-transpose normal matrix (REMED-GFX-006) and dotting the fog vector against the POST-skin position (the Vulkan source's own comment there claims "pre-skin" but its actual code uses the skinned position -- the stale comment was not carried over). The lighting formula (per-light Lambertian diffuse + Blinn-Phong specular, `EmissiveColor` pre-folding `AmbientLightColor*DiffuseColor` exactly like `EnvironmentMapEffect`'s own convention, not `BasicEffect`'s separate-ambient one -- confirmed by reading `SkinnedEffect::FillGpuDrawParams` directly) applies the fog-mix convention fix learned from `EnvironmentMapEffect`'s own bug (`mix(color.rgb, fogColor.rgb, vFogFactor)`, not the reversed form) from the start, so it did not need to be rediscovered. Two real, independent gaps closed to make this possible, both found by reading the code rather than by a failing test: (1) `MapVertexUsage()` (the real-`VertexDeclaration` vertex-attribute-mapping path) had no cases for `VertexElementUsage::BlendWeight`/`BlendIndices` at all -- a `VertexPositionNormalTextureSkinned`-declared buffer would have silently produced a vertex layout with no bone data (Gap A, not exercised by any existing test in this repo, since none use the typed-declaration skinned path, but real and independently discoverable); (2) `LlglVertexBufferBackend::ResolveVertexAttributes()`'s declaration-less stride-inference `switch` (`LLGL-32`) had no case for stride 52 (`VertexPositionNormalTextureSkinned`'s own GPU-packed size) -- every reused/ported test drives `SkinnedEffect` through exactly this path (`VertexBuffer::SetDataRaw`), so this gap (Gap B) WAS exercised, and would have thrown "this vertex layout is not supported" without the fix. `BlendIndices` is bound as a genuine INTEGER vertex attribute (`LLGL::Format::RGBA8UInt`, read in GLSL as `uvec4`), not a normalized byte4, matching every other backend's own established convention (confirmed against EasyGL's `ApplyLayout` case 52). Both gaps fixed at locations 4 (`aBoneWeights`)/5 (`aBoneIndices`), avoiding collision with the existing 0-3 reservations (position/color/texCoord/normal). Worked cleanly on the first build and test run on both modules -- no bugs found, unlike `EnvironmentMapEffect`. **Still out of scope**: real XNA compiles 9 distinct vertex-shader permutations {vertex-lit, one-light, pixel-lit} x {1,2,4 bones}; this backend (like every established CNA backend except D3D9) is per-pixel-lit only regardless of `PreferPerPixelLighting`, matching `GpuDrawParams::preferPerPixelLighting`'s own documented deviation. The `VertexColorEnabled` CNA-only (`NOXNA`) extension property and its stride-56 vertex-colour variant are not implemented on this backend (no existing test exercises it here either). `SkinnedPbrEffect` (stride 68, `PbrEffect`+skinning combined) is done too -- see `LLGL-25 (SkinnedPbrEffect)`'s own row. |
| LLGL-25 (PbrEffect) | `PbrEffect` -- glTF metallic-roughness BRDF, 5 texture maps (base colour, normal, metallic-roughness, emissive, occlusion). | ✅ | Done 2026-07-31 — `examples/llgl_pbreffect_handderived_test.cpp`, `Llgl_PbrEffect_HandDerived` (+ `_OpenGL`), adapted from the Vulkan backend's own `examples/vulkan_pbreffect_handderived_test.cpp` (draws into an off-screen `RenderTarget2D` and reads with `GetData()` instead of the source's own hand-rolled `Game` subclass that resizes the whole window, matching this backend's own `PixelTestGame` convention -- see the test's own header comment for why; this is the ONE deliberate difference from the source, since its own `SkinnedPbrEffect` check now ported over unchanged once `LLGL-25 (SkinnedPbrEffect)` landed too -- see that row). Like `EnvironmentMapEffect`/`SkinnedEffect`, gets its own dedicated vertex shader (`pbr3d.vert.glsl`/`.gl.vert.glsl`), fragment shader (`pbr3d.frag.glsl`/`.gl.frag.glsl`) and pipeline layout (`primitivePbrLayout_`: its own 84-float `PbrParams` uniform block at binding 1, then 5 texture/sampler pairs at bindings 2-11 -- base colour, normal, metallic-roughness, emissive, occlusion). Needs a NEW vertex element this backend never had before: `VertexElementUsage::Tangent` (`MapVertexUsage`'s new case, location 6 -- the tangent-space TBN basis the fragment stage builds for normal mapping) and a new stride-48 (`VertexPositionNormalTangentTexture`) case in `ResolveVertexAttributes()`'s declaration-less fallback switch, mirroring `LLGL-32`'s own stride-inference precedent. The glTF 2.0 metallic-roughness BRDF (`PbrLight()`: GGX distribution, Smith-Schlick-GGX visibility, Schlick Fresnel) is transliterated directly from the Vulkan backend's own already-correct `pbr3d.frag.glsl`, applying the fog-mix convention fix learned from `EnvironmentMapEffect`'s own bug from the start (no rediscovery needed). Unlike `EnvironmentMapEffect`/`SkinnedEffect`'s "throw if the required texture is missing" convention, `PbrEffect`'s 4 optional maps (`NormalMap`/`MetallicRoughnessMap`/`EmissiveMap`/`OcclusionMap`) resolve to a new lazily-created 1x1 default texture (`EnsureDefaultPbrTexturesEXT()`: opaque white for MR/emissive/occlusion, RGBA(128,128,255,255) decoding to tangent-space (0,0,1) for the normal map) when the game left them null, mirroring the Vulkan backend's own `EnsureDefaultWhiteTexture`/`EnsureDefaultFlatNormalTexture` precedent -- real `PbrEffect::FillGpuDrawParams()` can legitimately leave all 4 null, so throwing would have wrongly rejected a valid, minimally-configured draw. Only `Texture` (base colour) is required and still throws by name if missing. All 5 texture units share this backend's one global sampler state (`ApplySamplerState` only ever tracks slot 0) -- the SAME `LLGL::Sampler` object is bound at all 5 sampler slots in `ReplayFrameCommandsList`, since each GLSL `sampler2D` declaration still needs its own binding even when the underlying resource is identical. **One test-authoring bug found and fixed while adding the test, not a backend defect**: the Vulkan source's own hand-rolled `Game` subclass shrinks the WHOLE WINDOW to a tiny back buffer via its own `GraphicsDeviceManager`; `PixelTestGame`'s `Game` construction has no equivalent hook, so reading a hard-coded small pixel address directly off the (much larger, ~800x480) default back buffer sampled a world position over a full unit away from the coordinate origin the analytic derivation assumes -- found via a debug shader pass outputting `vWorldPos` directly. Fixed by drawing into an explicit `RenderTarget2D` instead, matching every other `PixelTestGame`-based Llgl test's own established convention. |
| LLGL-25 (SkinnedPbrEffect) | `SkinnedPbrEffect` -- `PbrEffect` + `SkinnedEffect` combined (glTF BRDF over a GPU-skinned mesh). | ✅ | Done 2026-07-31, same day as `LLGL-25 (PbrEffect)` -- `examples/llgl_pbreffect_handderived_test.cpp`'s own Check (d) (ported back from `vulkan_pbreffect_handderived_test.cpp` verbatim, no new test file), 5/5 on both modules: a single identity bone (weight 1.0, default `Matrix.Identity` -- a mathematical no-op skin transform) must reproduce `PbrEffect`'s own Check (a) value exactly. Adds `pbr3d_skinned.vert.glsl`/`.gl.vert.glsl` (a NEW vertex shader only) and `primitivePbrSkinnedLayout_`, but reuses `primitivePbrFragmentShader_` **verbatim, unchanged** -- skinning is a vertex-stage-only concern, so the fragment stage byte-for-byte matches plain `PbrEffect`'s. This only works because `BoneBlock` is placed at binding 12, deliberately AFTER every PBR texture/sampler pair (bindings 1-11, identical to `primitivePbrLayout_`) rather than shifting them to make room -- if the shared bindings had moved, the already-compiled `primitivePbrFragmentShader_` binary would no longer match the new layout's binding numbers. `PbrParams`' own `roughnessWeightsPad.y` field (documented as "WeightsPerVertex, unused by this shader" in the unskinned `pbr3d.vert.glsl`'s own comment) was reserved for exactly this from the start, so `FillPbrUniforms()` needed zero changes -- it already wrote `params.weightsPerVertex` unconditionally. The bone transform buffer pool (`skinnedBoneBuffers_`/`skinnedBoneData_`, `FillSkinnedBoneData()`) is reused verbatim from plain `SkinnedEffect` too, since bone data is entirely effect-agnostic. The skinning formula itself (weightsPerVertex-gated bone blend; bone-skin 3x3 composed with the outer world inverse-transpose normal matrix; tangent skinned-then-raw-world, matching `pbr3d.vert.glsl`'s own tangent-is-a-direction simplification) is transliterated from `skinned3d.vert.glsl`/the Vulkan backend's own `pbr3d_skinned.vert.glsl`. A new stride-68 (`VertexPositionNormalTangentTextureSkinned`) case was added to `ResolveVertexAttributes()`'s declaration-less fallback switch, the stride-48 `PbrGpuVertex` layout with the stride-52 `BlendWeight`/`BlendIndices` suffix appended -- no new vertex-attribute locations needed, since `Tangent`(6)/`BlendWeight`(4)/`BlendIndices`(5) were all already reserved by `LLGL-25 (PbrEffect)`/`LLGL-25 (SkinnedEffect)`. `RejectUnsupportedDrawParams`'s own `pbr && skinned` refusal was removed; `QueuePrimitives`'/`AcquirePrimitivePipeline`'s dispatch chains gained a `pbrSkinned = pbr && skinned` branch, checked with higher priority than either flag alone. Worked cleanly on the first build and test run on both modules -- no bugs found. |
| LLGL-26 | Render targets (`RenderTarget2D`, `RenderTargetCube`, MRT), cube and volume textures. | ✅ | **Every item in this row's own original scope is done** — `RenderTarget2D`/`RenderTargetCube`, cube/volume textures, and MRT (see the row's own paragraphs below for each; MSAA/mip-mapped render targets remain a separate, not-yet-scoped follow-up, not part of this row's original list). **`RenderTarget2D` done 2026-07-31** — `examples/llgl_rendertarget_test.cpp`, `Llgl_RenderTarget` (+ `_OpenGL`), 8/8 on both modules: construction, drawing into the target, unbinding back to the swap chain (its own independent clear), sampling the target back onto the screen with `SpriteBatch` (the `RenderTarget2D`-vs-plain-`Texture2D` cross-cast `ITextureBackend` accepts), and `RenderTarget2D::GetData()` reading the colour attachment directly. **Architecture**: LLGL's public Vulkan render-pass API has no way to re-enter a render pass with `Load` semantics (`BeginRenderPass()` always opens the "primary", `Undefined`/`DONT_CARE`-load pass — confirmed by reading `VKSwapChain.cpp`/`VKCommandBuffer.cpp`/`VKRenderTarget.cpp`), so a frame's queued commands are grouped by target IDENTITY into one contiguous render pass per distinct target (`GroupFrameCommandsByTargetEXT`/`FrameCommandBucket`), in first-appearance order — not replayed in original interleaved order. `RenderTargetUsage.PreserveContents` is not honoured across separate binds in this cut. The colour attachment always takes the swap chain's own colour format and a depth/stencil attachment matching the swap chain's own format is always allocated (regardless of the requested `DepthFormat`, which only changes what `HasRealDepthBuffer()` reports) — confirmed via a standalone read of LLGL's `VKPipelineState`/`VKGraphicsPSO` that a `LLGL::PipelineState` bakes in no `VkRenderPass` handle at bind time, only the attachment formats/sample count checked at creation, so every cached sprite/primitive pipeline built against the swap chain's render pass is reusable as-is against a render target's own render pass as long as the attachment signature matches — avoiding a second, render-target-keyed pipeline cache entirely. **One real bug found before it could reach a test**: `RenderTarget2D::GetBackend()` returns an `LlglRenderTargetBackend*`, not `LlglTextureBackend*` — `SpriteBatch::Draw`/`QueuePrimitives`' effect texture now resolve either concrete backend through a shared `ResolveSampledTexture()` helper instead of a hard `dynamic_cast<LlglTextureBackend*>` that would have silently failed to sample a render target. **Three more real bugs found by running the tests**: (1) sprites queued while a render target was bound read the SWAP CHAIN's shared pixel-to-clip-space projection (sized for the window, e.g. 800x480) instead of the target's own (e.g. 64x64) — the whole draw collapsed into a sliver of the target's clip space instead of filling it. Fixed by giving each `LlglRenderTargetBackend` its own fixed projection buffer, built once at construction (a target's resolution never changes, unlike the swap chain's), referenced per `FrameCommand` instead of the frame-global buffer. (2) destroying a `RenderTarget2D` before `Present()` — a perfectly ordinary pattern XNA allows — segfaulted `RecordAndSubmitFrame` on a freed `LLGL::RenderTarget*` still referenced by `frameCommands_`; fixed the same way `VertexBuffer`/`IndexBuffer` destruction mid-frame already was, by deferring the actual release (`ScheduleRenderTargetReleaseEXT`) until the frame that may reference it is submitted. (3) `RenderTarget2D::GetData()` called with no intervening back-buffer read returned stale/undefined pixels, because unlike back-buffer reads (which flush through `CaptureBackbuffer()`) nothing forced the target's queued draws to actually reach the GPU first; fixed with a new `FlushPendingFrameEXT()` that `GetData()` calls before reading. **3D draws into a render target are pixel-verified too** (Check H: `BasicEffect` + `VertexBuffer` + depth test, a nearer quad drawn first survives a farther one drawn second, read back with `GetData()`) — confirming the render target's own depth/stencil attachment and the reused 3D pipeline cache both genuinely work, not just `SpriteBatch`. **Cube textures (`CreateTextureCube`) are done too** (2026-07-31): a new `LlglTextureCubeBackend`, one `LLGL::TextureType::TextureCube` resource with 6 array layers (face index maps directly to `baseArrayLayer` -- the project's own face order, 0=+X…5=-Z, already matches the Vulkan/GL cube-image convention LLGL itself uses, so no remapping is needed), mip count computed the same way as `VulkanTextureCubeBackend`'s own `CalculateVulkanTextureCubeMipLevels`. `SetData`/`GetData` mirror `LlglTextureBackend`'s existing per-mip-level `WriteTexture`/`ReadTexture` pattern with an added `baseArrayLayer`. Worked cleanly on the first build and test run — closed all 49 `TextureCubeTest` cases plus `Texture3DTextureCubeContentTypeReaderTest`/`XnbBuiltInReaderRegistrationTest`'s `TextureCube` fixture cases (see `LLGL-22`'s own updated entry for the full `CnaTests` before/after). No dedicated `Llgl_*` pixel test was added for this alone: `TextureCubeTest` already exercises real `SetData`/`GetData` round-trips (including DDS loading) against the real LLGL backend and swap chain this CnaTests build uses, which is the same level of GPU-real coverage a hand-written pixel test would add. **Volume (`Texture3D`) textures are done too** (2026-07-31): a new `LlglTexture3DBackend`, one `LLGL::TextureType::Texture3D` resource, box-region `SetData`/`GetData` via `LLGL::TextureRegion`'s real 3D `offset`/`extent` (no array-layer indexing needed, unlike cube textures, since this uses genuine depth). Mip level count is computed from `(width, height)` only -- depth does not participate, matching FNA's own `Texture3D` constructor and mirroring the Vulkan backend's own `CalculateVulkanTexture3DMipLevels` precedent exactly (confirmed by reading that code first rather than assuming the cube-texture formula would generalize, which it would not have). `SupportsCapability(CNA::GraphicsCapability::Texture3D)` now returns `true`. Worked cleanly on the first build and test run -- closed all 39 `Texture3DTest` cases (see `LLGL-22`'s own updated entry for the full `CnaTests` before/after). No dedicated `Llgl_*` pixel test was added for this alone, for the same reasoning as cube textures: `Texture3DTest` already exercises real `SetData`/`GetData` round-trips against the real LLGL backend and swap chain this `CnaTests` build uses. **`RenderTargetCube` is done too** (2026-07-31): a new `LlglRenderTargetCubeBackend`, ONE shared `LLGL::TextureType::TextureCube` colour texture (6 array layers) plus ONE shared depth/stencil texture (matching FNA's own `RenderTargetCube`, which allocates exactly one depth/stencil buffer for the whole cube, not one per face -- mirroring the Vulkan backend's own precedent), and 6 `LLGL::RenderTarget`s built once at construction, each attaching the shared colour texture at a different `arrayLayer` (`LLGL::AttachmentDescriptor`'s own documented cube-face convention) alongside the shared depth texture. A new `LlglBoundRenderTarget` common interface (`GetLlglRenderTarget()`/`GetWidth()`/`GetHeight()`/`GetSpriteProjectionBuffer()`) lets `currentRenderTargetBackend_` point at either a plain `LlglRenderTargetBackend` or one face of a cube (`LlglRenderTargetCubeFaceBinding`, a thin non-owning view) without any of `QueueClear`/`QueueSpriteEXT`/`QueuePrimitives`/`GetActiveDrawRect` needing to know which -- `GroupFrameCommandsByTargetEXT`/`RecordAndSubmitFrame`/`ReplayFrameCommandsList` needed ZERO changes, since they already group purely by `LLGL::RenderTarget*` pointer identity, and 6 distinct per-face pointers produce 6 distinct buckets automatically. `SetRenderTargets` now dispatches on `IsRenderTargetCubeFace()` (previously an unconditional `NotYetImplemented`). **One real bug, found by reasoning before it could reach a test** (the same class as `RenderTarget2D`'s own `ResolveSampledTexture` fix): `EnvironmentMapEffect`'s `params->envMap` resolution was a hard `dynamic_cast<const LlglTextureCubeBackend*>` that would have silently failed -- thrown "belongs to another backend" -- the moment a game sampled a `RenderTargetCube` through `EnvironmentMapEffect`, the entire real-time-reflection use case this feature exists for. Fixed with a new `ResolveSampledTextureCube()` helper mirroring the existing `ResolveSampledTexture()`, resolving either concrete cube backend. Worked cleanly on the first build and test run otherwise (all 9/9 checks in a new dedicated `examples/llgl_rendertargetcube_test.cpp`, `Llgl_RenderTargetCube`, passed first try): construction, 6 independent per-face `Clear`+`GetData()` round trips (the core face-isolation property), back-buffer independence, and an `EnvironmentMapEffect`-samples-a-`RenderTargetCube` check (uniform-filled, to isolate "does the sample path work at all" from exact reflection-vector-to-face selection, which `LLGL-25 (EnvironmentMapEffect)`'s own test already covers). Like `CreateRenderTarget2D`, `preserveContents`/`mipMap`/`multiSampleCount` are ignored in this first cut (always discard, always 1 level, always single-sample) -- the same, already-accepted scope boundary. No `_OpenGL` CTest variant, same reason as `EnvironmentMapEffect`: this project's own OpenGL module has no cube-texture support at all. **Not yet wired into this project's elaborate shared cross-backend `RenderTargetCube` oracles** (`examples/rendertargetcube_usage_test.cpp`'s `PreserveContents`/MSAA/mip battery, `rendertargetcube_getdata_contract_test.cpp`'s row-order/orientation/mirroring `Contract` table, `rendertargetcube_msaa_face_test.cpp`) -- each requires a carefully-reasoned per-backend `Contract` entry (exact row-order/mirroring claims this task did not independently verify) that a rushed entry could get wrong more cheaply than skipping it; left as a documented follow-up rather than guessed at. **MRT is done too** (2026-07-31) -- a deliberately narrower first cut than this project's other MRT-capable backends: 2-4 `RenderTarget2D` slots only (no `RenderTargetCube` faces), written only by a custom multi-output `ShaderEffect` drawn via `SpriteBatch` (a 3D colour-only draw while an MRT set is bound throws by name, since no stock effect family here declares more than one fragment output). See this row's own dedicated MRT status paragraph above (right after the `RenderTargetCube` paragraph) for the full architecture (`LlglMRTBinding`, `GetPrimaryRenderPassEXT()`'s new per-target render-pass selection, the `colorAttachmentCount`-aware pipeline cache key) and `examples/llgl_mrt_test.cpp`'s `Llgl_MRT`/`_OpenGL`, 9/9 on both modules. `GraphicsCapability::MultipleRenderTargets` now reports `true`. **MSAA render targets are done too** (2026-08-01) — see this row's own dedicated MSAA-render-target status paragraph above (right after the back-buffer MSAA paragraph) for the full architecture (LLGL's anonymous multisampled colour attachment plus an explicit resolve attachment, `LlglBoundRenderTarget::GetSampleCount()`/`GetPrimarySampleCountEXT()`, and the sample-count-aware pipeline cache key) and `examples/llgl_msaa_rendertarget_test.cpp`'s `Llgl_Msaa_RenderTarget`/`_OpenGL`, 7/7 on both modules. **Mip-mapped render targets are done too** (2026-08-01) — see this row's own dedicated mip-mapped-render-target status paragraph above (right after the MSAA render target paragraph) for the full architecture (a real mip chain on the colour texture, `LLGL::CommandBuffer::GenerateMips()` called after every render pass via `LlglBoundRenderTarget::GetMipRegenColorTextureEXT()`/`FindMipRegenColorTextureEXT()`, and `GetData(level)` now real) and `examples/llgl_rendertarget2d_mip_test.cpp`'s `Llgl_RenderTarget2D_Mip`/`_OpenGL`, 8/8 on both modules. **Every item this row ever scoped, including both follow-ups, is now done.** |
| LLGL-27 | Custom `ShaderEffect` via `IEffectBackend`. | ✅ | Done 2026-07-31 — `examples/llgl_shadereffect_test.cpp`, `Llgl_ShaderEffect` (+ `_OpenGL`), 6/6 on both modules: a hand-authored GLSL tint shader compiles, binds, and genuinely tints a drawn sprite by its own uniform (verified against a stock-shader control case that must NOT show the tint). **Scoped to `SpriteBatch` draws only** — the vertex layout is the fixed sprite `position/texCoord/color` stream, not an arbitrary `VertexDeclaration` — mirroring the native Vulkan backend's own `VulkanEffectBackend` precedent exactly, not a new limitation invented here. `vertSrc`/`fragSrc` are always real GLSL text (unlike the Vulkan backend's own convention of expecting pre-compiled SPIR-V bytes, documented in `docs/shader-effect-vs-fx-bytecode.md`): compiled directly when the loaded module accepts GLSL (OpenGL), or through a genuine runtime GLSL→SPIR-V compile via `libshaderc` when it does not (Vulkan) — the same problem `SDL_GPU`'s own effect backend already solved, ported over almost verbatim (`CompileGlslToSpirv`, the same hand-declared `extern "C"` shaderc ABI subset, the same `find_library`-then-glob CMake fallback for this environment's `libshaderc1`-only, no-`-dev`-package install). Named-uniform setters (`SetUniformMat4`/`Vec4`/`Vec3`/`Vec2`/`Float`/`Int`) do not do real name-based reflection -- LLGL exposes none for a raw GLSL/SPIR-V module -- they map onto the exact same fixed 32-float (128-byte) staging block `VulkanEffectBackend::pushConst_` already documents (`[0..1]=vpSize`, `[4..19]=uMatrix`, `[20..23]=uColor`, `[24]=uFloat0`), uploaded to a real constant buffer instead of a Vulkan push constant; `name` is accepted but not consulted, matching that same precedent rather than inventing new semantics. Every custom effect shares one `LLGL::PipelineLayout` (`customEffectLayout_`, built lazily on the first `ShaderEffect`); only the shader modules and each effect's own per-blend-state `LLGL::PipelineState` cache differ. A custom-effect sprite draw gets its own per-draw uniform buffer snapshot (`customEffectUniformBuffers_`/`customEffectUniformData_`, pooled exactly like the 3D path's `transformBuffers_`/`transformData_`) rather than one shared buffer overwritten in place, since `SetUniformX()` can legitimately change between two `Draw()` calls inside one `Begin()`/`End()` block. `LlglSpriteBatchBackend::SetCustomEffect()` (previously the shared interface's silent no-op default -- a real, if inert, gap this closes) is the actual wiring point `SpriteBatch::Begin()`/`End()` call, calling `effect->Apply()` to trigger `IEffectBackend::Bind()` exactly like the native Vulkan backend's own `VulkanSpriteBatchBackend::End()` does. Same deferred-release treatment as render targets/query heaps applies to a destroyed `ShaderEffect`'s shader modules and cached pipelines (`ScheduleEffectResourceReleaseEXT`) -- and this pass also fixed a real, separate gap found while adding it: `pendingRenderTargetReleases_`/`pendingTextureReleases_`/`pendingQueryHeapReleases_` (LLGL-26/28) were drained only by `ReleasePendingBuffers()` (called after a frame submit), never by `~LlglGraphicsBackend()` itself, so a render target/query destroyed mid-frame with the backend torn down before the next submit would leak; the destructor now calls `ReleasePendingBuffers()` up front. **One correctness point caught by reasoning, not yet covered by a dedicated test**: a custom effect's `vpSize` uniform must be the PHYSICAL swap-chain/render-target extent (matching what the stock shader's own per-frame projection divides by), not `GetActiveDrawRect()`'s letterboxed destination rect -- the two only coincide for an unscaled presentation or a render target (which has no letterboxing at all); `QueueSpriteEXT` already branches correctly, but no test combines a scaled `CnaPresentationMode` with a custom effect the way `Llgl_Presentation` alone or `Llgl_ShaderEffect` alone does. `CnjEffectTest.LoadsRealCnjFixture`/`CnjStockEffectTest.CustomGlslEffectStillWorks` remain in `CnaTests`' known-failure list under `-DCNA_GRAPHICS_BACKEND=LLGL` -- not a regression, and not this task's scope: those fixtures' GLSL was authored for EasyGL's GLES dialect and shaderc rejects it outright for SPIR-V (`ES shaders for SPIR-V require version 310 or higher`), a fixture-content mismatch, not a `LlglEffectBackend` defect (confirmed by running them directly: they now reach real compilation and fail with a specific shaderc diagnostic, instead of failing earlier for an unrelated reason). `GraphicsDeviceCapabilityTest.SupportsCustomEffects` now passes (18 failures at LLGL-26 -> 17 after LLGL-28 -> 16 after this task, in the full `CnaTests` sweep). |
| LLGL-28 | Occlusion queries (`LLGL::QueryHeap`). | ✅ | Done 2026-07-31 — `examples/llgl_occlusionquery_test.cpp`, `Llgl_OcclusionQuery` (+ `_OpenGL`), 6/6 on both modules (adapted from `examples/vulkan_occlusionquery_pixelcount_test.cpp`'s three scenarios: a fully visible quad reports a positive `PixelCount()`; a nearer opaque occluder reduces it to exactly 0 via the real depth test; two non-overlapping half-quads inside one `Begin()`/`End()` sum their contributions rather than only the last draw counting). `Begin()`/`End()` are queued into the deferred frame exactly like `Clear`/`Sprite`/`Primitives` (`FrameCommand::Kind::QueryBegin`/`QueryEnd`, replayed as `LLGL::CommandBuffer::BeginQuery`/`EndQuery` -- LLGL requires both inside an open render pass, which this backend only opens at submit time). **A fresh `LLGL::QueryHeap` is created for every `Begin()`, never reused**: reading LLGL 0.04b's own vendored Vulkan source found `VKCommandBuffer::ResetQueryPoolsInFlight` -- the call that would reset a query pool for reuse -- `#if 0`'d out, so a second `vkCmdBeginQuery` on the same query index without an external reset (which LLGL exposes no public API for) is undefined behaviour by the Vulkan spec's own query-reset rule; a fresh pool sidesteps the gap entirely rather than working around a reset LLGL does not offer. `IsComplete()`/`PixelCount()` answer synchronously -- the first call forces a full submit-and-wait (`FlushPendingFrameEXT()`, new, also used by `RenderTarget2D::GetData()`) rather than genuinely polling across frames like real hardware queries are meant to be used; a documented, deliberate trade of async performance for an answer that is always immediately correct. Query heap lifetime follows the same deferred-release pattern as buffers/render targets (`ScheduleQueryHeapReleaseEXT`). `GraphicsDeviceCapabilityTest.SupportsOcclusionQuery` now passes under `-DCNA_GRAPHICS_BACKEND=LLGL` (18 failures -> 17 in the full `CnaTests` sweep). |

---

## Gaps vs EasyGL

`EasyGL` is this project's mature, established OpenGL backend. As of 2026-08-01, `LLGL` covers the
same broad functional surface — the full 2D+3D pipeline, every stock effect (`BasicEffect`,
`AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `PbrEffect`,
`SkinnedPbrEffect`), `RenderTarget2D`/`RenderTargetCube`/MRT, MSAA and mip-mapped render targets,
custom `ShaderEffect`s, occlusion queries, cube/volume textures — but it is **not** at unqualified
parity with `EasyGL`. The real, currently-known gaps:

* **Never verified on real GPU hardware.** Every check in this plan runs against a software
  rasterizer (Xvfb + Mesa lavapipe for Vulkan, llvmpipe for OpenGL). `EasyGL` has real-hardware
  mileage this backend does not yet have — see "Closing notes" below.
* **This backend picks Vulkan or OpenGL at runtime, and several features only work on ONE of the
  two modules on this project's own test environment** (a single fixed OpenGL implementation like
  `EasyGL` has no equivalent split):
  - `FillMode::WireFrame` does not work on the Vulkan module (`LLGL-30`) — LLGL's own vendored
    Vulkan module never requests the `fillModeNonSolid` device feature at all, confirmed by reading
    its source; not fixable without patching vendored LLGL.
  - Back-buffer `MultiSampleCount` (MSAA) does not work on the OpenGL module (`LLGL-23`).
  - Cube textures — and therefore `EnvironmentMapEffect` and `RenderTargetCube` entirely — do not
    work on the OpenGL module at all (`hasCubeTextures` unsupported on this environment's GLX
    driver).
  - Per-slot `BlendState.ColorWriteChannels1..3` under MRT does not work on the OpenGL module
    (`LLGL-21`) — `glColorMaski` is not honoured by this environment's GL driver once the real
    `independentBlendEnabled` bug was fixed.
* **Target-identity replay can violate public command order.** `GroupFrameCommandsByTargetEXT()`
  merges every command for one target into one bucket and always appends the swap-chain bucket last.
  Producer/consumer chains that revisit a target, cube faces sharing one depth resource, or a
  back-buffer consumer between two binds of the same target can therefore observe the wrong cycle.
* **Deferred buffer mutation is not versioned.** `VertexBuffer::SetData()` and index-buffer upload
  mutate or replace the live LLGL resource immediately, while an earlier queued draw stores only the
  resource pointer and replays at frame end. Two draws reusing one buffer in a frame can both render
  the second upload.
* **Pipeline cache identity is incomplete.** The 3D path keeps only the low 16 bits of
  `MakeBlendPipelineKey()`, dropping blend factors/functions and `ColorWriteChannels0`; the shared
  blend key also keeps only the low nibble of the 32-bit `MultiSampleMask`. Distinct legal states can
  reuse the first pipeline.
* **Custom-effect descriptor layout is unsafe on Vulkan.** The backend creates one fixed descriptor
  set layout, but accepts SPIR-V that refers to additional sets. Pipeline creation can then crash in
  the native driver instead of rejecting an unsupported shader by name.
* **Sampler state is global, not per texture slot.** `ApplySamplerState()` ignores every slot except
  0, so `DualTextureEffect` slot 1 and the extra `PbrEffect` maps silently use slot 0's sampler.
* **Back-buffer and module-specific state contracts remain incomplete.**
  `FixedHeightDynamicWidth` can replace an explicitly requested logical width with one derived from
  the physical aspect ratio, and OpenGL back-buffer draws with a non-zero Y viewport/scissor can
  render nothing. `minDepth`/`maxDepth`, depth bias, slope-scale depth bias, and draw-time stencil
  state are accepted but not applied.
* **MRT combined-feature coverage is incomplete.** `LlglMRTBinding` inherits sample count 1 and its
  render target is built from the resolved single-sample colour textures, so binding multisampled
  slots as MRT silently loses MSAA semantics; mip regeneration of all MRT slots is also not yet a
  reliable contract.
* **Some legal BasicEffect/layout/camera combinations are missing or unexplained.** In particular,
  untextured+unlit vertices without vertex colour and lit+textured vertices without a normal are
  rejected, and one orthographic `CreateLookAt` conformance scenario remains unresolved.
* **Only per-pixel lighting.** Real XNA compiles 9 distinct vertex-shader permutations
  ({vertex-lit, one-light, pixel-lit} × {1,2,4 bones}); this backend is per-pixel-lit only
  regardless of `PreferPerPixelLighting` — the same documented deviation every established CNA
  backend except `D3D9` already has, not unique to `LLGL`.
* **`RenderTargetUsage.PreserveContents` is not honoured across separate binds** — every render
  pass this backend opens uses `Undefined`/`DONT_CARE` load semantics (LLGL's own public Vulkan
  API has no way to re-enter a render pass with real `Load` semantics).
* **`ColorWriteChannels1..3` outside MRT was never a gap** (a single-attachment bind only ever
  needed slot 0), but the underlying `independentBlendEnabled` bug (`LLGL-21`) means any FUTURE
  per-slot blend-state feature added to this backend needs the same care.

Several of these **are currently silent** (pipeline-cache collisions, later buffer uploads changing
earlier draws, per-slot samplers, MRT+MSAA degradation, ignored depth range/bias/stencil), while the
multi-set custom-effect case can crash. Until Phase LLGL-8 is complete, neither
`SupportsCapability()` nor the currently registered CTests are a complete statement of backend
correctness. `docs/llgl-backend.md` must be updated together with each remediation task.

Of the module-dependent gaps listed above, several are genuinely NOT actionable from this backend's own code: the
module-dependent driver/library limitations (`LLGL-30` WireFrame-on-Vulkan, back-buffer MSAA and
cube textures on OpenGL, `ColorWriteChannels1..3`-under-MRT on OpenGL) all require either patching
vendored LLGL or a different driver, neither of which this project controls; `PreserveContents`
across separate binds needs a public Vulkan render-pass API LLGL does not expose; and per-pixel-only
lighting matches every other established CNA backend except `D3D9`, so "fixing" it here alone would
create a new inconsistency rather than close one. The remaining items ARE real, scoped, implementable
work:

## Phase LLGL-6 — Closing the originally identified implementable EasyGL gaps (implemented; hardware verification open)

| # | Task | Status | Notes |
| --- | --- | --- | --- |
| LLGL-33 | `BlendState.MultiSampleMask`. | ✅ | `LLGL::BlendDescriptor`'s own `sampleMask` field is now wired end-to-end: a new `multiSampleMask_` member (default `0xFFFFFFFF`, matching `BlendWriteState`'s own default), set from `writeState.multiSampleMask` in `ApplyBlendState`, applied via `pipelineDesc.blend.sampleMask` in both `FillCurrentBlendAndRasterStateEXT` (sprite/custom-effect path) and `AcquirePrimitivePipeline`'s own inline 3D descriptor, and folded into `MakeBlendPipelineKey`'s cache key (as its own low nibble, folded in LAST so it survives `AcquirePrimitivePipeline`'s pre-existing low-16-bit truncation of that key -- see the open item below). Confirmed by reading LLGL's own vendored source: the Vulkan module applies `VkPipelineMultisampleStateCreateInfo::pSampleMask` unconditionally; the OpenGL module's own `SetSampleMask` call is permanently `#if 0`'d out, a genuine, unfixable-from-CNA's-side limitation on that module. New dedicated test `examples/llgl_multisamplemask_test.cpp` (`Llgl_MultiSampleMask`/`_OpenGL`, module-gated per the established `Llgl_Msaa`/`Llgl_MRT` `[SKIP]` precedent): 4x MSAA `RenderTarget2D`, differential dst/src baselines, `MultiSampleMask=0` resolving to the clear colour on a module that honours it. Result: **4/4 PASS on the Vulkan module, 3 PASS + 1 correctly-detected `[SKIP]` on the OpenGL module.** Along the way, registering the shared, cross-backend `examples/gfx077_colorwritechannels_3d_test.cpp` against this backend surfaced a real, PRE-EXISTING, separate bug in `AcquirePrimitivePipeline`'s own cache key (unrelated to `MultiSampleMask` itself, not fixed here) -- see `known_bugs.md`'s own open entry ("3D pipeline cache ignores `ColorWriteChannels`/blend factors for `DrawPrimitives`") for the full writeup; that shared test is deliberately NOT registered as an LLGL CTest until it is. |
| LLGL-34 | `RenderTargetCube` MSAA (`MultiSampleCount`). | ✅ | Mirrors `CreateRenderTarget2D`'s own MSAA follow-up (`LLGL-26`) architecture for colour: an anonymous (textureless) multisampled colour attachment, one per face (never shared -- each face is a SEPARATE `LLGL::RenderTarget`), plus an explicit `resolveAttachments[]` entry naming the cube's own single-sample colour texture at the relevant `arrayLayer`. The shared depth/stencil texture (one for the whole cube, matching FNA's own convention, unchanged from the non-MSAA path) needed a different treatment: LLGL requires a real, explicitly-referenced attachment texture to carry the SAME sample count as the `RenderTargetDescriptor` it's used in (`RenderTargetFlags.h`'s own documented constraint), and `TextureDescriptor::samples` "is only used for multi-sampled textures" (`LLGL::TextureType::Texture2DMS`/`Texture2DMSArray`, confirmed by reading `TextureFlags.h`) -- so the shared depth texture's own `type` now switches to `Texture2DMS` (`samples = requestedSamples`) whenever MSAA is requested, mirroring the Vulkan backend's own `VulkanRenderTargetCubeBackend::depthImage_`, "promoted to MSAA samples when this cube engages MSAA". `LlglRenderTargetCubeFaceBinding` (already an `LlglBoundRenderTarget`) got its own `sampleCount_`/`GetSampleCount()` override, set from `faceTargets[0]->GetSamples()` (device-clamped, shared by all 6 faces since they're all built from one request) via a new `LlglRenderTargetCubeBackend` constructor parameter; `LlglRenderTargetCubeBackend` itself also grew a real `GetMultiSampleCount()` override (`IRenderTargetCubeBackend`'s own interface member, previously defaulted to always-0), matching `LlglRenderTargetBackend`'s identical convention. New dedicated test `examples/llgl_msaa_rendertargetcube_test.cpp` (`Llgl_Msaa_RenderTargetCube`), reusing `examples/llgl_msaa_rendertarget_test.cpp`'s diagonal-edge technique verbatim against one cube face (`CubeMapFace::PositiveX`) instead of a plain `RenderTarget2D`. Result: **7/7 PASS on the Vulkan module** (including both module-dependent checks -- this environment's Vulkan module genuinely resolves MSAA into a cube face, not merely avoiding a crash). No `_OpenGL` CTest variant, same reason as every other `RenderTargetCube` test here: this project's own OpenGL module has no cube-texture support at all. |
| LLGL-35 | `RenderTargetCube` mip-mapping (`mipMap`). | ✅ | Mirrors `CreateRenderTarget2D`'s own mip-mapped-render-target follow-up (`LLGL-26`) architecture -- a real mip chain on the shared cube colour texture (`colorDesc.mipLevels` set from the same `CalculateMipLevels` formula `RenderTargetCube.cpp` already computes client-side), each per-face render-target attachment still binding only level 0. One real LLGL API constraint changed the original assumption in this row: `LLGL::CommandBuffer::GenerateMips(Texture&, const TextureSubresource&)`'s own `TextureSubresource::baseArrayLayer` is documented as ignored for a plain, non-array `LLGL::TextureType::TextureCube` (only `TextureCubeArray` honours it, confirmed by reading LLGL's own `TextureFlags.h`) -- so there is NO cheaper, single-face-only regeneration call available; the whole-texture `GenerateMips(Texture&)` overload (equivalent in effect to the subresource one here, since the array-layer restriction is ignored either way) regenerates every face's own mip chain at once regardless of which face's render pass just ended. `LlglRenderTargetCubeFaceBinding::GetMipRegenColorTextureEXT()` therefore returns the SAME shared cube texture pointer for every face (not a unique one per face the way `LlglRenderTargetBackend`'s own override is), so the existing per-render-pass-bucket regeneration mechanism (`FindMipRegenColorTextureEXT`, unchanged) calls the whole-cube regeneration once per face bound in a frame -- redundant when multiple faces are drawn in one frame, but not incorrect, since each call is a faithful, idempotent regeneration of every face's own current level-0 content. `RenderTargetCube::GetData(face, level, ...)` is real for any level in `[0, LevelCount)` now (previously a hard `level != 0` refusal). New dedicated test `examples/llgl_mip_rendertargetcube_test.cpp` (`Llgl_Mip_RenderTargetCube`), reusing `examples/llgl_rendertarget2d_mip_test.cpp`'s asymmetric-split technique verbatim against one cube face (`CubeMapFace::PositiveX`). Result: **8/8 PASS on the Vulkan module** (crisp level 0, an exact 1:1 downsample at an intermediate level, the coarsest level's true weighted average, both out-of-range-level and non-mipmapped rejection). No `_OpenGL` CTest variant, same `hasCubeTextures` reason as every other `RenderTargetCube` test here. |
| LLGL-36 | Wire `RenderTargetCube` into this project's shared cross-backend `RenderTargetCube` oracles. | ✅ | Registered all three previously-missing shared, cross-backend files with a genuine, independently-verified `CNA_BACKEND_LLGL` `Contract` branch each -- `examples/rendertargetcube_getdata_contract_test.cpp` (`Llgl_RenderTargetCube_GetDataContract`, 56/56 PASS: byte-exact face readback, correct row order/orientation against the `RenderTarget2D` reference, `SetData` correctly refused), `examples/rendertargetcube_usage_test.cpp` (`Llgl_RenderTargetCube_Usage`, 30/30 PASS), and `examples/rendertargetcube_msaa_face_test.cpp` (`Llgl_RenderTargetCube_MsaaFace`, 32/32 PASS -- this backend has NONE of the shared-MSAA-attachment aliasing defect REMED-GFX-141 fixed on EasyGL/Vulkan/SdlGpu, since every cube face is already its own distinct `LLGL::RenderTarget`). The most notable finding, discovered empirically (a wrong assumption was tried and disproven first -- see each file's own `CNA_BACKEND_LLGL` comment): this backend's `RenderTargetUsage.PreserveContents` genuinely works, but ONLY as an incidental consequence of its "one render pass per distinct target, not per bind" replay architecture, not because `preserveContents` is read at all (`CreateRenderTargetCube`'s own parameter is still unused). Every command naming the SAME target within one UNFLUSHED frame is grouped into ONE render pass, so a "second bind" of the same target never becomes a literal, freshly-cleared pass unless an explicit `Clear()` was queued (which only a `DiscardContents` bind does, at the shared XNA layer) -- content genuinely accumulates across binds as long as nothing (`GetData()`, `Present()`) flushes the queue in between. `rendertargetcube_usage_test.cpp`/`rendertargetcube_msaa_face_test.cpp`'s own producer/marker draws are never separated by a flush, so their own `preserves`/MSAA-preservation checks measure and confirm this (`Contract.preserves=true`); `rendertargetcube_getdata_contract_test.cpp`'s own U1/U2 checks DO call `GetData()` between the two draws, which flushes the queue and starts a genuinely new, unpreserved pass for the second bind (`Contract.preservesOnRebind=false`) -- the two claims are not a contradiction, they measure the identical architecture two different, both faithfully reproduced, ways; see each file's own comment for the full cross-reference. |
| LLGL-37 | `SkinnedEffect.VertexColorEnabled` (CNA-only `NOXNA` extension property) and its stride-56 vertex-colour vertex variant. | ✅ | Research before implementing corrected this row's own original premise: EasyGL, Vulkan, D3D9/D3D11/D3D12, WebGPU, SdlGpu and Bgfx already implement this (`CNB-67`) -- only LLGL was missing it, so this was a straightforward port, not new ground. A genuinely new stride-56 case was added to `ResolveVertexAttributes()`'s declaration-less fallback switch (colour APPENDED at offset 52, location 1 per this backend's own `MapVertexUsage()` mapping -- NOT location 5 like Vulkan/EasyGL use, their own convention doesn't apply here). Rather than EasyGL's single always-declared-attribute shader, this backend follows its own established per-layout-shape shader-file-selection convention (`AcquirePrimitiveVertexShader()`'s own colored/textured split is the precedent): a new `shaders/skinned3d_color.vert/frag.glsl` + `.gl.` pair, selected instead of `skinned3d.vert/frag.glsl` only when the bound layout carries a colour attribute, added to `compile_shaders.py`'s `SHADERS` list and regenerated into `llgl_shaders.hpp`. The gate reuses `emissiveColorPad.w` (otherwise free, since `SkinnedEffect` has no separate ambient term of its own to occupy it) -- the same free-slot-reuse trick `BasicEffect`'s own `ambientColorLighting.w` uses -- written unconditionally by `FillSkinnedUniforms` (harmless for the plain shader, which never reads it). Modulation order matches every other backend's own implementation exactly (EasyGL's `EnsureSkinnedProgram()` fragment shader is the reference): vertex-colour alpha multiplies in BEFORE the specular add, vertex-colour RGB multiplies the WHOLE combined diffuse+specular output AFTER it (so a pure black vertex colour genuinely zeroes the pixel). New dedicated test `examples/llgl_skinnedeffect_vertexcolor_test.cpp` (`Llgl_SkinnedEffect_VertexColor`/`_OpenGL`), adapted from `examples/vulkan_skinnedeffect_vertexcolor_test.cpp`'s own analytically-derived technique (a straight-on camera/light making `N=L=V`, `NdotL0=1`, isolating the vertex-colour multiply from the rest of the lighting math with exact expected pixel values, not a golden image). Result: **4/4 PASS on both renderer modules.** |
| LLGL-38 | Real GPU hardware verification (this backend has only ever run against Xvfb + Mesa lavapipe/llvmpipe). | ⬜ | Not code work in the usual sense -- an infrastructure/verification task, tracked here because it gates whether this backend can be described as a general alternative to `VULKAN`/`EASYGL` at all (see "Closing notes" below, unchanged since this backend's 2D baseline first landed). Would need to re-run this project's own `Llgl_*` CTest suite (and ideally the module-dependent checks currently `[SKIP]`ped -- `WireFrame`/back-buffer-MSAA/cube-textures/`ColorWriteChannels1..3`-under-MRT -- since a REAL Vulkan/OpenGL driver may behave differently than lavapipe/llvmpipe on every one of them) against a real Vulkan-capable and OpenGL-capable GPU, on whatever platform is available, and update every module-dependent finding in this plan with what a real driver actually does instead of only ever reporting the software-rasterizer answer. |

---

## Phase LLGL-7 — Auditing the remaining shared cross-backend test suite (audit complete; remediation is Phase LLGL-8)

The status in this historical table means that a batch was inspected and its LLGL contract branch
was decided; it does **not** mean that every test in the row passes or is registered. In particular,
LLGL-41, LLGL-43 and LLGL-44 retain open correctness findings and must not be treated as green
backend-conformance milestones. Their fixes and mandatory registrations are tracked below.

Found by diffing `cmake/Tests/EasyGLTests.cmake` against `cmake/Tests/LlglTests.cmake`
(`comm -23` on the sorted `examples/*.cpp` basenames each references), then filtering to files
registered on **3 or more** backends total (EasyGL plus at least 2 others) to isolate genuinely
shared, cross-backend oracles from EasyGL's own large body of `easygl_*`-prefixed, EasyGL-only
tests (which are not gaps -- they were never meant to run anywhere else). ~36 files qualify.

Two files were already closing this same gap for the `RenderTargetCube` family specifically before
this phase started (`LLGL-36`, done): `rendertargetcube_getdata_contract_test.cpp`,
`rendertargetcube_usage_test.cpp`, `rendertargetcube_msaa_face_test.cpp` -- not repeated here.

**Explicitly out of scope for this phase**: `avatar_attach_part_integration_test.cpp`,
`avatar_real_render_integration_test.cpp`, `avatar_tint_routing_integration_test.cpp` (registered
on EasyGL + Vulkan only). These need real `AvatarRenderer` rendering support
(`EnableRealRenderingEXT`/`AttachPartEXT`/`PartTintEXT`), which this backend has ZERO of --
`grep -rln "AvatarRenderer" src/CNA/Internal/Backends/Llgl/` returns nothing. That is a whole new
subsystem, not a test-wiring task, and belongs in its own future phase if ever pursued.

Each file falls into one of two shapes, found by grepping each file's own `CNA_BACKEND_` reference
count (`grep -c "CNA_BACKEND_" examples/<file>.cpp`):

* **Plain registration (0 references)** -- an ordinary `Game`-subclass test with no per-backend
  `Contract` struct at all; wiring it up is just `cna_llgl_test()` + `cna_register_backend_test()`
  plus a build+run to confirm it passes as-is, no capability claims to write.
* **Contract-branch (1+ references)** -- the same `Contract`-struct-per-`CNA_BACKEND_XXX` pattern
  `LLGL-36`'s three `RenderTargetCube` oracles already used: a NEW, independently-verified
  `CNA_BACKEND_LLGL` branch must be written and empirically checked field-by-field (build, run,
  read the actual behaviour) before registering -- copying another backend's branch, or guessing,
  is exactly the "a wrong `Contract` claim is worse than no entry" mistake `LLGL-36`'s own
  investigation avoided. These files are large (600-2300 lines each) and often encode multiple
  independent findings from OTHER backends' own past investigations (REMED-GFX-*/CNB-*/Task-*
  numbers) -- read each one's own header comment before writing the branch, the same way `LLGL-36`
  read `rendertargetcube_msaa_face_test.cpp`'s own header before concluding it could not honestly
  be registered without first re-checking whether `LLGL-36`'s own `preserves` finding still applied
  (it did, and the file WAS registerable in the end -- the lesson is "read first," not "expect no").

| # | Task | Files | Status | Notes |
| --- | --- | --- | --- | --- |
| LLGL-39 | Plain-registration batch (no `CNA_BACKEND_` branches needed). | 🟡 | 10 files, 8 registered and PASSING, 2 deliberately NOT registered -- each genuinely fails on this backend for a real, identified, unrelated reason, not silently swept aside. **Registered (8/8 PASS each):** `rendertarget2d_depth_test.cpp` (`Llgl_RenderTarget2D_DepthBuffer`), `rendertarget_viewport_scissor_reset_test.cpp` (`Llgl_RenderTarget_ViewportScissorReset`, 6/6), `skinnedeffect_lighting_conformance_test.cpp` (`Llgl_SkinnedEffect_LightingConformance`, 9/9), `viewport_reset_after_resize_test.cpp` (`Llgl_ViewportResetAfterResize`, 8/8), `graphicsdevice_clear_depth_test.cpp` (`Llgl_GraphicsDevice_ClearDepth`, 2/2), `rendertargetcube_plural_binding_test.cpp` (`Llgl_RenderTargetCube_PluralBinding`, 14/14), `spritebatch_custom_viewport_test.cpp` (`Llgl_SpriteBatch_CustomViewport`, 13/13), `spritebatch_viewport_switch_test.cpp` (`Llgl_SpriteBatch_ViewportSwitch`, 6/6). The last three previously failed here (2/6, 7/13, 9/14) tracing to the per-draw-`Viewport`-per-bucket architecture gap plus a sprite-geometry offset gap plus an independent `SpriteBatch.Begin()` transform-reset bug -- all three now FIXED, see `known_bugs.md`'s updated writeup. No `_OpenGL` CTest variant for these three: a newly-discovered, separate OpenGL-module-only limitation (Y-offset scissor/viewport against the backbuffer renders nothing under `CNA_LLGL_RENDERER=opengl` specifically) blocks verifying them there -- see `known_bugs.md`'s new open entry. **Deferred (real, unrelated findings, see `known_bugs.md`):** `rasterizerstate_cullmode_indexed_basiceffect_test.cpp` crashes on an untextured+unlit+no-vertex-colour `BasicEffect` draw -- `AcquirePrimitiveVertexShader()` has no shader variant for that combination at all. `rasterizerstate_cullmode_camera_test.cpp` hits an unexplained `Orthographic`+`CreateLookAt`-specific scenario-setup failure (every other scenario in the same file, including `Perspective`+`CreateLookAt` with the identical camera, passes) -- root cause not yet identified, and confirmed NOT the (now-fixed) viewport bug above (this file never changes `Viewport` mid-frame). |
| LLGL-40 | Back buffer contract-branch batch. | `backbuffer_pass_order_test.cpp`, `backbuffer_readback_dimension_test.cpp`, `backbuffer_headless_reject_test.cpp`, `backbuffer_first_read_test.cpp` | 🟡 | 3/4 files registered and PASSING; along the way found and fixed two genuine, previously-undiscovered bugs (see `known_bugs.md`): (1) the swap chain's own render-pass bucket replayed wherever it FIRST appeared in the frame instead of always LAST, so an ordinary render-target-then-composite-onto-backbuffer pattern could sample uninitialized texture content when any earlier command touched the backbuffer first (`GroupFrameCommandsByTargetEXT()` now always trails the swap chain's bucket) -- fixes `backbuffer_pass_order_test.cpp` (`Llgl_BackBuffer_PassOrder`, 30/30 PASS); (2) `LlglTextureBackend`/`LlglTextureCubeBackend`/`LlglTexture3DBackend` released their underlying `LLGL::Texture` immediately on destruction instead of deferring like `VertexBuffer`/`RenderTarget2D` already did, segfaulting whenever a `Texture2D` went out of scope before the frame that drew it was flushed (new `ScheduleTextureReleaseEXT()`, mirroring `ScheduleBufferReleaseEXT`) -- fixes `backbuffer_readback_dimension_test.cpp` (`Llgl_BackBuffer_ReadbackDimension`, 8/8 PASS) and most of `backbuffer_first_read_test.cpp`. `backbuffer_headless_reject_test.cpp` (`Llgl_BackBuffer_HeadlessReject`, 12/12 PASS) needed only its own `Contract` branch, no fix. `backbuffer_first_read_test.cpp` stays unregistered: 9/13 legs pass, but D63/D64/D65/E1 hit a genuine, separate, OPEN finding -- this backend's default `FixedHeightDynamicWidth` presentation mode derives its own internal logical width from the physical window's aspect ratio rather than the game's requested backbuffer width, truncating columns past the derived boundary whenever the requested aspect is wider than the (in this environment, consistently ~800x480) physical window's. See `known_bugs.md`'s new open entry. |
| LLGL-41 | `RenderTarget`/`RenderTargetCube` contract-branch batch. | `rendertarget_pass_boundary_test.cpp`, `rendertarget_depthstencil_usage_test.cpp`, `rendertarget_effect_source_test.cpp`, `rendertarget_sampling_orientation_test.cpp`, `rendertarget_producer_consumer_test.cpp`, `rendertarget_first_use_test.cpp`, `rendertarget_backbuffer_consumer_test.cpp`, `bound_target_lifetime_test.cpp` | 🟢 | 7/8 done. `rendertarget_first_use_test.cpp` (`Llgl_RenderTarget_FirstUse`, 26/26 PASS): a brand-new `RenderTarget2D`/`RenderTargetCube` constructed, bound, drawn into and read back all within one public frame works with no warm-up frame or manual flush needed -- no fix required, this backend already honoured the contract. `rendertarget_backbuffer_consumer_test.cpp` (88/90, NOT registered): G1 (a BACKBUFFER consumer issued between two bind cycles of the same target, expecting the FIRST cycle) is a fifth reproduction of the bucket-ordering finding below, sharing its exact shape with `rendertarget_producer_consumer_test.cpp`'s own I2. Everything else in the file -- 15 other producer/consumer legs including MSAA, mip-mapped, `RenderTargetCube`, multi-family (`SpriteBatch`+3D) ordering, and 8 same-frame bind cycles across 8 consecutive frames -- passes. `rendertarget_sampling_orientation_test.cpp` (10/10 checks up to CD3, NOT registered): CD4 ("BasicEffect lit + textured" on a normal-less `VertexPositionTexture`) crashes the process -- a new, distinct capability gap (same class as the untextured+unlit `flat3d` finding, see `known_bugs.md`). Everything else in the file (orientation via `SpriteBatch`, `BasicEffect`/`AlphaTestEffect` mesh-UV sampling) is correct. `rendertarget_pass_boundary_test.cpp` (`Llgl_RenderTarget_PassBoundary`, 43/43 PASS): its own `segmentsBindCycles` field reads `true` even though `GroupFrameCommandsByTargetEXT()` buckets by target identity, not by public bind cycle -- empirically, two bind cycles of the SAME target still resolve correctly because every command in that one bucket (including each bind's own explicit `DiscardContents` `Clear()`) still replays in original public order, which turns out to be observationally identical to true per-cycle native passes for every shape this file probes. `rendertarget_depthstencil_usage_test.cpp` (28/29 checks pass, NOT registered), `rendertarget_effect_source_test.cpp` (18/20 legs registered individually as `Llgl_RenderTarget_EffectSource_<leg>`, C1/F1 excluded), and `rendertarget_producer_consumer_test.cpp` (39/41 checks pass, NOT registered) all found the SAME general, DISTINCT bug from the above: bucket-level replay does NOT interleave two DIFFERENT buckets whose commands were queued out of first-appearance order relative to each other (a target revisited after depending on another target, or two targets aliasing one physical resource) -- see `known_bugs.md`'s open entry, generalized to cover `rendertarget_depthstencil_usage_test.cpp`'s U2, `rendertarget_effect_source_test.cpp`'s F1, and `rendertarget_producer_consumer_test.cpp`'s D5/I2 (the latter a new nuance: the swap-chain-always-trails-last rule from the `LLGL-40` fix is exactly wrong when an earlier bucket is revisited after the swap chain's own read was supposed to happen). `rendertarget_effect_source_test.cpp`'s C1 additionally found a second, unrelated, real bug: a custom `ShaderEffect` using multiple Vulkan descriptor sets (this shared fixture's own shader, not this backend's `llgl_shadereffect_test.cpp` one) crashes the Vulkan driver during pipeline creation -- also `known_bugs.md`, separately documented. `bound_target_lifetime_test.cpp` (3/18 legs fully pass, NOT registered): critically, 0/18 legs CRASHED -- the REMED-GFX-168 defect this fixture exists to catch (a SIGSEGV when a bound render target is destroyed mid-cycle) does not reproduce on LLGL at all, and every leg's own destroy-while-bound-specific assertions pass wherever not entangled with an unrelated finding. 15/18 legs fail their own unconditional `RequireBackbufferExact` check -- this file's 72x36 backbuffer request is a third reproduction of the `FixedHeightDynamicWidth` logical-width finding, empirically confirmed via a temporary debug print showing `physical=800x480 virtual=72x36` (derived `logicalWidth=60`, short of the requested 72). Leg L1's own MRT-slot-mip-regeneration gap is a separate, already cross-backend-documented (Vulkan, BGFX) capability boundary, not new -- the file's own `kMrtSlotMipReadable` now declares LLGL alongside them. LLGL-41 is now 8/8 done. |
| LLGL-42 | Texture/`TextureCube`/`Texture3D` contract-branch batch. | `texturecube_texture3d_setdata_contract_test.cpp`, `texturecube_texture3d_getdata_contract_test.cpp`, `texture2d_getdata_transfer_range_test.cpp`, `texture2d_getdata_contract_test.cpp`, `texture_filter_ordinal_contract_test.cpp`, `point_sampling_contract_test.cpp`, `colorspace_midtone_contract_test.cpp` | 🟢 | 7/7 done, all fully passing, no new bugs found: `Llgl_CubeVolume_SetDataContract` (56/56), `Llgl_CubeVolume_GetDataContract` (56/56), `Llgl_Texture2D_GetDataTransferRange` (74/74), `Llgl_Texture2D_GetDataContract` (40/40), `Llgl_TextureFilterOrdinalContract` (70/70), `Llgl_PointSamplingContract` (146/146), `Llgl_ColorSpace_MidTone` (17/17). Each file needed only its own `CNA_BACKEND_LLGL` Contract branch -- `TextureCube`/`Texture3D` SetData/GetData are `Support::Exact` at every mip level (matching Vulkan/BGFX/WebGPU/SDL_GPU), `RenderTargetCube::SetData` is `Support::Unsupported` (this backend's `LlglRenderTargetCubeBackend` only overrides `GetData`, inheriting the interface's default refusal), `RenderTarget2D`'s `GetData` render-target contract is `Exact`, all nine `TextureFilter` ordinals resolve to the correct min/mag/mip native sampler, and colour transfer through a render target round-trip is byte-identical with no sRGB conversion. |
| LLGL-43 | Deferred-capture / `SpriteBatch` viewport contract-branch batch. | `deferred_viewport_capture_test.cpp`, `deferred_scissor_capture_test.cpp`, `deferred_source_lifetime_test.cpp`, `spritebatch_3d_order_test.cpp` | 🟢 | 4/4 done. `Llgl_Deferred_Viewport` (39/39 PASS): every deferred draw executes under the `GraphicsDevice.Viewport` active at its own public call; `depthRangeApplies` declared `false` (this backend's `SetViewport` never forwards `minDepth`/`maxDepth` to LLGL, the same boundary already declared on bgfx). `Llgl_Deferred_Scissor` (47/47 PASS): the same contract for `GraphicsDevice.ScissorRectangle`/`RasterizerState.ScissorTestEnable`, including a degenerate zero-width/height rectangle rasterizing nothing (`emptyScissorDrawsNothing=true`, unlike Vulkan/EasyGL/bgfx's own declared `false`). `Llgl_SpriteBatch3DOrder` (83/83 PASS, 3 declared skips): a stock 3D draw issued after a `SpriteBatch` inside one bind cycle executes in public order, not grouped by family. `deferred_source_lifetime_test.cpp` (8/17 legs pass in full, NOT registered): critically, **0/17 legs crashed** -- the REMED-GFX-167 defect this fixture exists to catch (a heap-use-after-free when a deferred draw's source dies before replay) does not reproduce on LLGL. The other 9 legs fail their own unconditional backbuffer check -- a fourth reproduction of the `FixedHeightDynamicWidth` finding (this file's 72x36 backbuffer request), broadened into `known_bugs.md`'s existing entry rather than a new one. |
| LLGL-44 | Misc state contract-branch batch. | `graphicsdevice_ordered_clear_test.cpp`, `frontface_winding_test.cpp`, `stock_effect_sampler_contract_test.cpp` | 🟢 | 3/3 done. `Llgl_GraphicsDevice_OrderedClear` (46/46 PASS, no fix needed): every declared value (`orderedClear`/`clearOnPreserveTarget`/`clearIgnoresViewport`/`clearIgnoresScissor` all true, `stencilBuffer3D`/`preferMultiSampling` both false) was correct on the first try. `frontface_winding_test.cpp` (115/127, NOT registered): a genuine, NEW, OPEN bug found -- W3's 4 buffer-reuse entry points (`SetVertexBuffer`+`DrawPrimitives`/`DrawIndexedPrimitives` variants) each reuse the SAME persistent `VertexBuffer`/`IndexBuffer` object for two draws in one frame, and `SetData()` overwrites the live GPU buffer immediately while the earlier draw's queued command only replays at frame end -- so the second `SetData()` silently erases the first draw's content. A candidate fix (defer the SECOND `SetData()`'s write via the same `ScheduleBufferReleaseEXT` deferred-release mechanism already used for buffer destruction) was implemented, found to introduce a NEW crash on an unrelated, previously-passing entry point, and fully reverted -- see `known_bugs.md`'s new open entry for the complete analysis and why a second blind attempt was not made. `stock_effect_sampler_contract_test.cpp` (64/65, NOT registered): a second, INDEPENDENT, already-self-documented-in-code OPEN finding -- `ApplySamplerState()` only ever tracks slot 0's own sampler state in a single set of member variables, so `DualTextureEffect`'s slot 1 (and `PbrEffect`'s other 4 texture units) always samples with slot 0's current filter/address settings rather than its own. Not attempted, given the adjacent VertexBuffer fix's own crash earlier in this same batch. This completes LLGL-44 (3/3) and Phase LLGL-7 in full. |

### Effective LLGL-7 status after the 2026-08-03 audit

This table supersedes the historical green icons and "complete" wording embedded in the long batch
notes above; those icons recorded investigation coverage, not a passing conformance result.

| Batch | Audit coverage | Current conformance status |
| --- | --- | --- |
| LLGL-39 | Complete | 🟡 Two shared tests remain unregistered; OpenGL Y-offset coverage is missing. |
| LLGL-40 | Complete | 🟡 `backbuffer_first_read_test.cpp` remains blocked by logical-width handling. |
| LLGL-41 | Complete | 🟡 Ordering, multi-set shader, layout-variant, logical-width and MRT-mip findings remain open. |
| LLGL-42 | Complete | 🟢 All seven texture contract batches are registered and passing within declared module capabilities. |
| LLGL-43 | Complete | 🟡 Source-lifetime exactness and OpenGL deferred viewport/scissor cases remain open. |
| LLGL-44 | Complete | 🟡 Persistent buffer reuse and per-slot sampler state remain open. |

---

## Phase LLGL-8 — Correctness and production-readiness remediation (open)

Priority is part of the acceptance gate: P0 defects can reorder or corrupt draws or crash a native
driver; P1 defects silently apply the wrong public graphics contract; P2 items are incomplete
capabilities or infrastructure weaknesses. A task is complete only when its named regression tests
are registered for every renderer module that advertises the relevant capability. Excluding a
failing leg, changing an expected pixel to `[SKIP]`, or narrowing a capability solely to make CI
green does not close the task unless the API truly cannot support the contract and the limitation is
reported before submission.

| # | Priority | Task | Acceptance gate | Status |
| --- | --- | --- | --- | --- |
| LLGL-45 | P0 | **Replace target-identity bucket replay with ordered render-pass segments.** Preserve the public order of target binds, clears, producer/consumer sampling, back-buffer reads and cube-face operations. Implement real `PreserveContents` across flush/rebind either with load-capable passes, an explicit copy/restore path, or another design proven equivalent; do not rely on incidental one-bucket behaviour or the unconditional swap-chain-last rule. | Register and pass all legs of `rendertarget_depthstencil_usage_test.cpp`, `rendertarget_effect_source_test.cpp`, `rendertarget_producer_consumer_test.cpp`, `rendertarget_backbuffer_consumer_test.cpp` and `rendertarget_pass_boundary_test.cpp`, including A→B→A, target→backbuffer→target, shared cube depth and mid-frame readback cases. No ordering-specific exclusion remains. | 🟡 |
| LLGL-46 | P0 | **Version deferred vertex/index data.** A queued draw must retain the exact buffer contents and native resource lifetime visible at its public draw call even if `SetData()` later overwrites or enlarges the same object. Prefer immutable per-frame slices/ring allocation or explicit versioned upload commands over in-place writes; releasing a replaced buffer must remain deferred until its last queued consumer is submitted. | Register `frontface_winding_test.cpp`; all W3 persistent-buffer reuse entry points pass for indexed/non-indexed and dynamic/static buffers on Vulkan and OpenGL. Add a focused grow-capacity regression proving the old resource is neither freed early nor reused with new contents. | 🟡 |
| LLGL-47 | P0 | **Make custom-effect resource layouts safe.** Reflect or validate compiled GLSL/SPIR-V before LLGL pipeline creation. Either build every referenced descriptor set/binding or reject unsupported set numbers, resource types and stage visibility with a deterministic CNA exception; malformed/unsupported shader input must never reach a driver-crash path. | Enable `rendertarget_effect_source_test.cpp` C1 and add shaders using sets 0+1, missing bindings and conflicting declarations. Each supported shader renders correctly; each unsupported shader throws before `CreatePipelineState`. Run with Vulkan validation enabled where available. | 🟡 |
| LLGL-48 | P1 | **Use collision-free typed pipeline-cache keys.** Replace ad-hoc multiply/truncate packing with key structs whose equality and hash include every field consumed by the relevant LLGL pipeline descriptor: vertex layout, topology, render-pass signature, depth/raster state, all blend factors/functions/write masks, full 32-bit `MultiSampleMask`, scissor enable and effective sample count. | Register `gfx077_colorwritechannels_3d_test.cpp`; add pairwise tests for blend factors/functions and masks that differ only above bit 3, including an 8x-MSAA mask when supported. Instrumented test builds assert that descriptor equality and key equality cannot disagree. `Llgl_BasicEffect` alpha blending remains green. | ⬜ |
| LLGL-49 | P1 | **Track and capture sampler state per texture slot.** Store at least every slot consumed by stock effects, acquire the correct sampler for each slot, and capture those sampler objects in each deferred command so later state changes cannot leak backward. | Register `stock_effect_sampler_contract_test.cpp` at 65/65 or better. Add independent filter/address tests for `DualTextureEffect` slot 1 and all five `PbrEffect` maps; slot 0 changes must not alter another slot and vice versa. | 🟡 |
| LLGL-50 | P1 | **Separate logical back-buffer dimensions from presentation scaling.** `FixedHeightDynamicWidth` may choose a presentation rectangle, but it must not silently shrink an explicitly requested readable back buffer or make valid columns unreachable. Define the mode contract once and make draw, readback, viewport and resize code use the same dimensions. | Register and fully pass `backbuffer_first_read_test.cpp`, `bound_target_lifetime_test.cpp` and `deferred_source_lifetime_test.cpp` with their 72x36 cases. Add wider-than-window and narrower-than-window aspect tests plus resize round-trips. | 🟡 |
| LLGL-51 | P1 | **Fix OpenGL back-buffer viewport/scissor Y conversion and test both modules explicitly.** Normalize LLGL's screen-origin rules at the command boundary without changing render-target orientation or the already-passing zero-Y cases. | Add `_OpenGL` registrations for `deferred_viewport_capture_test.cpp`, `deferred_scissor_capture_test.cpp`, `spritebatch_custom_viewport_test.cpp`, `spritebatch_viewport_switch_test.cpp` and the applicable cube/plural tests. The current 37/39 and 43/47 OpenGL results become full passes, including non-zero-Y target→backbuffer→target sequences. | 🟡 |
| LLGL-52 | P1 | **Complete legal stock-effect vertex-layout permutations and resolve the orthographic camera case.** Add a defined untextured+unlit colourless BasicEffect path and a defined policy/shader for lit+textured input without normals; diagnose the `Orthographic`+`CreateLookAt` mismatch rather than masking it with a contract branch. | Register and pass `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`, `rendertarget_sampling_orientation_test.cpp` CD4 and `rasterizerstate_cullmode_camera_test.cpp` on each capable module. Unsupported declarations, if any remain, are rejected before queuing with a precise message. | 🟡 |
| LLGL-53 | P2 | **Finish or explicitly narrow raster/depth/stencil state support.** Wire viewport `minDepth`/`maxDepth`, depth bias, slope-scale depth bias and the full front/back stencil state into descriptors and cache keys. If LLGL 0.04b cannot express one field on a module, expose a precise module capability instead of accepting it as a silent no-op. | Add differential pixel tests for each state and for two draws differing only in that state. Enable the stencil branch of `graphicsdevice_ordered_clear_test.cpp`; update `SupportsCapability()` and `docs/llgl-backend.md` from measured module results. | 🟡 |
| LLGL-54 | P2 | **Define and implement combined MRT contracts.** Preserve the effective sample count when multiple multisampled targets are bound, create matching multisample/resolve attachments, regenerate mip chains for every written MRT slot, and either support cube-face slots or reject them before allocation without advertising broader support. | Add MRT+MSAA edge-resolution tests, per-slot mip readback after MRT writes, mixed requested/effective sample-count validation, and lifetime tests for every slot. `LlglMRTBinding::GetSampleCount()` must report the actual native target sample count. | 🟡 |
| LLGL-55 | P2 | **Harden build and virtual-display CI.** Do not compile ENet tests when `CNA_ENABLE_NET=OFF`; make shaderc discovery conditional on the renderer modules that need runtime SPIR-V compilation; preflight Xvfb for GLX/DRI3 and report Vulkan WSI unavailability as an infrastructure skip rather than twelve renderer crashes. Keep explicit Vulkan and `_OpenGL` lanes so auto-selection cannot hide one module. | A clean LLGL build with `CNA_ENABLE_NET=OFF` produces `CnaTests`; OpenGL-only, Vulkan-only and dual-module configurations build. CI records whether Xvfb supports DRI3, runs the matching module suite, and never labels `VK_ERROR_SURFACE_LOST_KHR` from missing DRI3 as a backend pixel failure. | ⬜ |
| LLGL-56 | P2 | **Clean up native-surface ownership and platform scope.** Stop leaking `XVisualInfo` from repeated `LlglSdlSurface::GetNativeHandle()` calls, document ownership in the adapter, and investigate a Wayland/native-handle path or make the X11-only restriction a first-class build/runtime capability. | ASan/LSan surface-create/destroy and resize loops show no X11 allocation leak. X11 rejection/selection is covered by tests; Wayland is either supported by an integration test or rejected once with an actionable capability message. | 🟡 |

Recommended execution order: LLGL-45 → LLGL-46 → LLGL-47 → LLGL-48, then LLGL-49 through
LLGL-54 in parallel-safe, independently testable changes, followed by LLGL-55/56 and the existing
LLGL-38 real-hardware matrix. The command-replay redesign in LLGL-45 should land first because
several later tests otherwise conflate their own state contract with known cross-target reordering.

**`LLGL-45` progress (2026-08-03): the ordering half is done and verified; the one remaining gap is
verification-blocked, not architectural.** `GroupFrameCommandsByTargetEXT()` now builds one segment
per contiguous same-target run in TRUE public order (a target revisited after another target's or
the swap chain's own commands appeared in between gets its own new segment in its own original
position, instead of being merged into whichever segment first used that target) -- the former
"swap chain always trails every other bucket" special case is gone entirely. Every segment's own
`BeginRenderPass()` uses a new `AcquireLoadRenderPassEXT()` helper: a small, backend-lifetime cache
of `AttachmentLoadOp::Load` render passes keyed only by (colour-attachment count, depth/stencil
presence, sample count) -- every render target and the swap chain in this backend always share the
same colour/depth-stencil FORMAT (confirmed by reading `CreateRenderTarget2D`/`CreateRenderTargetCube`/
`SetMultipleRenderTargetsEXT`, all of which derive both from the swap chain), so this one small cache
is compatible with all of them, generically, via the base `LLGL::RenderTarget` interface -- no
per-target-kind plumbing needed. Applying `Load` unconditionally, even to a target's very first-ever
segment, is safe: a `DiscardContents` bind already queues its own explicit `Clear()` as that segment's
first command, at the shared cross-backend layer above this one. **Confirmed fixed** (fresh runs,
`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03): `rendertarget_producer_consumer_test.cpp` D5/I2 (41/41,
now fully registered as `Llgl_RenderTarget_ProducerConsumer`), `rendertarget_effect_source_test.cpp`
F1 (32/32, now registered alongside its other passing legs), `rendertarget_backbuffer_consumer_test.cpp`
G1 (86/86, now fully registered as `Llgl_RenderTarget_BackbufferConsumer`). **Not yet verified:**
`rendertarget_depthstencil_usage_test.cpp`'s U2 (two `RenderTargetCube` faces sharing one physical
depth buffer) -- expected fixed by the same generic mechanism (it does not special-case cube faces
at all), but this sandbox's OpenGL module has no cube-texture support
(`LLGL::RenderingFeatures::hasCubeTextures == false`, confirmed via `rendertarget_pass_boundary_test.cpp`
crashing identically on the pre-fix binary too -- a pre-existing, unrelated limitation, not a
regression) and its Vulkan module cannot present under this sandbox's Xvfb (no DRI3,
`VK_ERROR_SURFACE_LOST_KHR`, the same infrastructure gap this file's own audit paragraph at the top of
this document already describes). `rendertarget_depthstencil_usage_test.cpp` is not yet even wired up
as a `cna_llgl_test()` build target for this reason. Closing `LLGL-45` fully needs either `LLGL-38`'s
real-hardware pass or a DRI3-capable Xvfb (`LLGL-55`) to actually run and register U2 -- see
`known_bugs.md`'s updated entry for the complete writeup.

**`LLGL-46` progress (2026-08-03): fixed and verified on OpenGL; Vulkan verification blocked by the
same infrastructure gap as `LLGL-45`'s own U2.** `LlglVertexBufferBackend::SetData()`/
`LlglIndexBufferBackend::Upload()` now call `LlglGraphicsBackend::FlushPendingFrameEXT()` (a no-op
when nothing is queued) before writing in place or reallocating, whenever this is not the buffer's
very first upload -- flushing submits and waits on every currently-queued draw, including any draw
that still holds this buffer's CURRENT content by raw `LLGL::Buffer*`, so it is always safe to write
in place or `Release()` the old buffer immediately afterward (no deferred-release bookkeeping or new
per-buffer tracking state needed at all). A buffer whose `buffer_` is still null (every
`GraphicsDevice::DrawUserPrimitives()`/`DrawUserIndexedPrimitives()` overload's own per-draw temp
buffer) never reaches this branch, so the fix cannot interact with that unrelated internal mechanism
-- which is exactly the interaction that broke an earlier, reverted candidate fix (see
`known_bugs.md`'s updated entry for that history). **Confirmed fixed** (`CNA_LLGL_RENDERER=opengl`,
Xvfb, 2026-08-03): `frontface_winding_test.cpp` W3's 12 former failures are gone (127/127, up from
115/127, now registered as `Llgl_FrontFaceWinding`). A new dedicated grow-capacity regression,
`examples/llgl_vertexindexbuffer_grow_test.cpp` (`Llgl_VertexIndexBuffer_Grow`, 6/6), specifically
proves the GROW case (a second upload exceeding the first one's byte capacity) does not corrupt or
lose an earlier still-queued draw's own content -- confirmed to genuinely catch the pre-fix defect by
running it against a stashed pre-fix binary, where it fails exactly as expected. A broad regression
sweep (smoke/2D/texture-readback/presentation/3D/BasicEffect/RenderTarget/MRT/MultiSampleMask/MSAA
render target/mip render target/lighting/ShaderEffect/resize/occlusion query/the `LLGL-45` target
files) shows no regressions. **Not yet verified on the Vulkan module**, same infrastructure gap as
`LLGL-45`'s own U2 (this sandbox's Xvfb has no DRI3) -- needs `LLGL-38`'s real-hardware pass or a
DRI3-capable Xvfb (`LLGL-55`) to confirm there too before this row can close fully.

**`LLGL-47` progress (2026-08-03): fixed via option (b) (reject, don't extend); verified in
isolation, end-to-end Vulkan run still needed.** `LlglEffectBackend::CompileProgram()` now scans the
SPIR-V `shaderc` just compiled for any `OpDecorate .../DescriptorSet` value other than 0
(`SpirvUsesOnlyDescriptorSetZero()`, a minimal targeted binary scan reading only the SPIR-V header
and `OpDecorate` shape -- not a full reflection library, matching this class's own doc comment that
already declined SPIRV-Cross as an added dependency) and fails compilation before any
`LLGL::Shader`/pipeline object is created, instead of letting a shader whose resources spread across
multiple Vulkan descriptor sets reach `LLGL::VKGraphicsPSO::CreateVkPipeline` (previously undefined
per the Vulkan spec and a real driver crash, see `known_bugs.md`).
`rendertarget_effect_source_test.cpp`'s own C1 leg already has a graceful escape hatch for exactly
this outcome (`if (!custom.IsEffectValid()) { boundary(...); return; }`), so the fix only needs to
make an unsupported shader fail safely, not actually work. **Verified in isolation**: a standalone
scratch program (not part of the project) compiled the EXACT shader source text from both
`examples/llgl_shadereffect_test.cpp` (the currently-passing custom-effect test, set 0 implicit) and
`rendertarget_effect_source_test.cpp`'s own C1 shaders (`set = 1`/`set = 2`/`set = 3`) through the
real `shaderc_compile_into_spv`, then ran the exact scan logic now in the backend against the real
compiled bytes: the working shader is correctly `allowed`, C1's own vertex and fragment SPIR-V are
both correctly `rejected`. A broad OpenGL-module regression sweep (including `Llgl_ShaderEffect`,
unaffected since GLSL never reaches `shaderc` at all on that module) shows no regressions. **Not yet
verified end-to-end**: this sandbox's Vulkan module cannot present under its own Xvfb (no DRI3), so
C1 itself cannot actually be run through the real backend here -- same infrastructure gap as
`LLGL-45`'s U2 and `LLGL-46`'s Vulkan-module verification. `Llgl_RenderTarget_EffectSource_C1` is not
registered until `LLGL-38`'s real-hardware pass or a DRI3-capable Xvfb (`LLGL-55`) confirms it.

**`LLGL-51` progress (2026-08-03): root cause re-investigated via live instrumentation; NOT fixed,
original hypothesis mostly disproven.** Temporarily instrumented the vendored, pinned LLGL
dependency itself (`~/deps/LLGL`'s own `GLStateManager.cpp`, `fprintf` added and later fully
reverted via `git checkout --`, confirmed clean) to observe `flipViewportYPos_`/`framebufferHeight_`
and every `SetViewport()` call live against this backend's real Xvfb/llvmpipe environment, instead of
continuing to reason from source alone. Two findings: (1) `deferred_viewport_capture_test.cpp`'s own
failing F2/F3 legs never actually use a non-zero-Y viewport/scissor rectangle at all (only X/width
vary) -- the "Y-conversion" framing this task inherited from `spritebatch_custom_viewport_test.cpp`'s
own separate finding does not describe F2/F3's own failure. (2) Live tracing shows
`flipViewportYPos_`/`framebufferHeight_` ARE correctly resynced on every swap-chain rebind, and the
actual `glViewport` inputs/outputs for F2's own draws are correct -- directly contradicting the
original (2026-08-02) hypothesis that LLGL's own screen-origin/clip-control state goes stale. The
real defect is therefore NOT in viewport/scissor application at all; F2's backbuffer portion reads
back as fully ABSENT (clear colour) rather than merely mispositioned, and F3 (a backbuffer-only leg
with no render target involved) only fails because it runs immediately after F2 in the same process
-- consistent with some state leaking forward from F2 rather than an independent bug of its own. See
`known_bugs.md`'s rewritten entry for the complete instrumentation trace and the next concrete
leads (`CommandBuffer::CopyTextureFromFramebuffer`'s own path, or leftover scissor/draw state from
the off-screen bind) for whoever continues this. `Llgl_Deferred_Viewport`/`Llgl_Deferred_Scissor`
remain unchanged at 37/39 and 43/47 -- this pass corrected the understanding of the defect but did
not close it, and no code in this repository was changed by this investigation.

**`LLGL-51` progress (2026-08-04): root cause FOUND and FIXED, following this task's own recommended
next leads exactly.** Continued live-instrumenting the vendored, pinned LLGL dependency (temporary
probes in `GLCommandExecutor.cpp` and `GLFramebufferCapture.cpp`, all fully reverted via `git
checkout --`, confirmed clean before implementing the real fix) -- this time tracing
`CommandBuffer::CopyTextureFromFramebuffer()`'s own internal steps, exactly the lead the prior pass
identified. Found: `CopyTextureFromFramebuffer` does `glCopyTexSubImage2D` (framebuffer -> an
intermediate texture) followed by `glBlitFramebuffer` (intermediate texture -> the destination
staging texture). The first is NOT scissor-tested (confirmed correct at every probed pixel); the
second IS scissor-tested per the GL spec, and nothing in `CaptureBackbuffer()`'s own sequence ever
resets `GL_SCISSOR_TEST`/`GL_SCISSOR_BOX` before it runs. This project's own `ComputeEffectiveScissor()`
deliberately computes an "effective scissor" for every `Primitives` draw with a smaller-than-target
viewport (intentional XNA-style sub-viewport clipping) -- so the LAST draw replayed before the
capture leaves ITS OWN small scissor rectangle active, and the blit silently confines the ENTIRE
backbuffer readback to that one rectangle. Confirmed via live `glGetIntegerv(GL_SCISSOR_BOX)`
probes: `(64,0,32,72)` and `(0,0,32,72)` for the two previously-failing legs, each matching that
leg's own last draw's sub-viewport exactly. **Fix** (`LlglGraphicsBackend.cpp`,
`CaptureBackbuffer()`): `commands_->SetScissor()` to the full bucket resolution immediately before
`CopyTextureFromFramebuffer()` -- verified two ways before implementing for real (temporary
`glDisable(GL_SCISSOR_TEST)`, then the actual fix's own "widen the box" approach, both worked) --
using only the public `CommandBuffer::SetScissor()` API, no vendored-source change needed.
**Verified** (`git stash` pre/post, `CNA_LLGL_RENDERER=opengl`, Xvfb): `Llgl_Deferred_Scissor`
43/47 -> **47/47 full pass**; `Llgl_Deferred_Viewport` 35/39 -> 37/39 (F2/F3, the only checks this
mechanism explains, now pass; E1/E2 are a separate, already-declared depth-remap limitation,
unaffected). This same mechanism turns out to be the ORIGINAL 2026-08-02 "Y-offset" symptom too --
`spritebatch_custom_viewport_test.cpp`/`spritebatch_viewport_switch_test.cpp` (this ticket's own
starting point) now pass **13/13 and 6/6, full passes**, up from reading back zero matching pixels
for any non-zero-Y rectangle. A full 69-binary regression sweep (Vulkan-default and OpenGL-forced)
shows zero new regressions. Added `_OpenGL` ctest registrations for all four files named in this
ticket's own acceptance gate, matching its own explicit ask; `rendertargetcube_plural_binding`
still has no `_OpenGL` lane -- blocked by the separate, pre-existing `hasCubeTextures` gap, not by
anything this fix touches, so that part of the acceptance gate ("and the applicable cube/plural
tests") is not yet satisfied. See `known_bugs.md`'s rewritten entry for the full instrumentation
trace.

**`LLGL-49` progress (2026-08-03): fixed and verified; PbrEffect's own dedicated multi-slot test not
added.** `ApplySamplerState(int slot, ...)` now tracks 5 slots independently
(`samplerFilter_[5]`/`samplerAddressU_[5]`/`samplerAddressV_[5]`/`samplerMaxAnisotropy_[5]`) instead
of a single set of scalars slot 0 alone ever wrote -- confirmed against the Vulkan backend's own
reference convention (`slotSamplers_[]`/`PbrSlotSamplersRawEXT()`): slot 0 is every family's own
base/primary texture, slot 1 is `DualTextureEffect`'s second texture OR `EnvironmentMapEffect`'s
cube map (mutually exclusive per draw), and slots 2/3/4 are `PbrEffect`'s metallic-roughness/
emissive/occlusion maps. `FrameCommand` grew 4 new `pbr*Sampler` fields (previously absent --
`ReplayFrameCommandsList` bound slot 0's own sampler at all 5 PBR texture units, a limitation the old
code already self-documented in a comment) so each PBR map now binds its own captured sampler.
**Verified fixed** (`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03):
`stock_effect_sampler_contract_test.cpp`'s M3 ("slot 1's Linear filter alone breaks block
uniformity") now passes, confirmed by a fresh run with zero failures and by reproducing the exact
pre-fix M3-only failure against a stashed pre-fix binary. Now registered as
`Llgl_StockEffectSampler`. A regression sweep (`Llgl_DualTexture`, `Llgl_DualTextureEffect_VertexColor`,
`Llgl_PbrEffect_HandDerived`, `Llgl_BasicEffect`, `Llgl_Lighting`, `Llgl_2D`, `Llgl_Smoke`) shows no
regressions. **Left at 🟡:** the acceptance gate's own "add independent filter/address tests for
... all five `PbrEffect` maps" was not done this session -- `stock_effect_sampler_contract_test.cpp`
only exercises the DualTextureEffect slot-0/1 case (its own `M` leg); a dedicated PbrEffect
multi-slot test proving each of the 5 maps independently (not just that the mechanism exists) is
still open for whoever continues this.

**`LLGL-50` progress (2026-08-03): fixed and verified; new tests for wider/narrower/resize cases not
added.** `ComputePresentationRect()`'s `FixedHeightDynamicWidth` branch now treats the aspect-derived
logical width as a FLOOR, not a hard override:
`logicalWidth = virtualWidth_ > 0 ? std::max(derivedWidth, virtualWidth_) : derivedWidth`. A window
wider (relative to its own height) than the requested aspect is unaffected (`derivedWidth` already
exceeds `virtualWidth_` there, matching this mode's own already-tested "a wider window shows more
content" contract, `llgl_presentation_test.cpp` Check E, still 6/6 PASS unchanged); only a window
narrower than the requested aspect (this project's own fixed ~800x480 headless test window combined
with a short/tall requested backbuffer) now keeps the full requested width addressable instead of
silently shrinking it. **Verified fixed** (`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03):
`backbuffer_first_read_test.cpp` 13/13 legs (up from 9/13, now registered as
`Llgl_BackBuffer_FirstRead`), `bound_target_lifetime_test.cpp` 17/18 (up from 3/18, registered
per-leg as `Llgl_BoundTargetLifetime_<leg>`), `deferred_source_lifetime_test.cpp` 15/17 (up from
8/17, registered per-leg as `Llgl_DeferredSourceLifetime_<leg>`) -- the remaining failures in each
(`bound_target_lifetime_test.cpp` F1, `deferred_source_lifetime_test.cpp` E1/E2) are the SAME
pre-existing, unrelated OpenGL-module `hasCubeTextures` gap confirmed elsewhere this session, not
this defect. **Left open:** the acceptance gate's own "add wider-than-window and narrower-than-window
aspect tests plus resize round-trips" was not done -- the fix is verified only via the three
already-existing files' own 72x36 requests against this sandbox's fixed ~800x480 window, not via new,
purpose-built tests spanning the full matrix of aspect combinations and live resizes.

While investigating this ticket's own regression sweep, discovered and fixed a SEPARATE, real issue:
`backbuffer_pass_order_test.cpp`'s own `CNA_BACKEND_LLGL` `Contract.orderedBackbufferSegments` was
still `false` (the pre-`LLGL-45` behavior) even though `LLGL-45` had already fixed the underlying
replay engine -- a stale test assumption. Correcting it to `true` made dozens of previously-silently-
skipped checks genuinely evaluate and PASS, strongly confirming `LLGL-45`'s own fix, but also exposed
a new, real, narrower, still-open gap (V1/V2: per-cycle viewport/scissor on a revisited backbuffer) --
see `known_bugs.md`'s new entry for the full writeup; not fixed this session.

**`LLGL-52` progress (2026-08-03): 3 of 4 vertex-layout/lighting gaps fixed and verified; the
orthographic camera case investigated but NOT fixed.** `AcquirePrimitiveVertexShader()`
(`LlglGraphicsBackend.cpp`) previously threw for three distinct `BasicEffect` vertex-layout/lighting
combinations that are all ordinary, real-XNA-legal draws; each now has its own dedicated shader
variant instead of refusing the draw:
1. Untextured + unlit + no vertex-colour attribute (`flat3d.vert.glsl`, pairs with the existing
   `untextured3d.frag.glsl` unchanged).
2. Lit + textured + no normal attribute (`lit_textured3d_flatnormal.vert.glsl`, a fixed `(0,0,1)`
   object-space normal, pairs with the existing `lit_textured3d.frag.glsl` unchanged).
3. Lit + untextured + no vertex-colour attribute (`lit_flat3d.vert.glsl`, pairs with the existing
   `lit_untextured3d.frag.glsl` unchanged) -- discovered via live `fprintf` instrumentation in
   `AcquirePrimitiveVertexShader()`, NOT the combination this ticket's own acceptance gate assumed:
   `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`'s `BasicEffect` actually calls
   `EnableDefaultLighting()`, so its crash was really this lit case falling through to the UNLIT
   `flat3d` branch and then failing to link against the LIT fragment shader the pipeline actually
   paired it with (`"definitions of uniform block 'Transform' do not match"`), not the unlit case an
   earlier `known_bugs.md` summary described.

New shaders were compiled to SPIR-V via a scratch `libshaderc`/`ctypes` script rather than
`compile_shaders.py`'s own `glslangValidator`, which is not installed in this sandbox and has no
passwordless `sudo` path to install (`compile_shaders.py`'s `SHADERS` list was still updated with
each new entry for whenever a proper regeneration is run). **Verified fixed**
(`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-03): `rasterizerstate_cullmode_indexed_basiceffect_test.cpp`
6/6 PASS (up from an uncaught crash, now registered as
`Llgl_RasterizerState_CullMode_IndexedBasicEffect`); `rendertarget_sampling_orientation_test.cpp`
61/61 PASS (up from crashing at CD4, now registered as `Llgl_RenderTarget_SamplingOrientation`);
`llgl_lighting_test.cpp`'s own Check H rewritten from "asserts a throw" to "asserts real lighting"
and now 10/10 PASS. A full 65-binary sweep of every LLGL test executable under
`CNA_LLGL_RENDERER=opengl` found no regressions; every failure present both before and after this
change traces to the same three already-documented, pre-existing environment limitations
(`hasCubeTextures` unsupported on this sandbox's OpenGL module, `LLGL-51`'s own OpenGL Y/viewport
gap on `Llgl_Deferred_Viewport`/`Llgl_Deferred_Scissor`/`spritebatch_custom_viewport`/
`spritebatch_viewport_switch`, and `REMED-GFX-155`'s pre-existing rasterizer-state-leak finding on
`spritebatch_3d_order_test.cpp`'s M2/M3) -- confirmed identical via `git stash` against the pre-fix
binary, not merely assumed.

**Left OPEN: the `Orthographic`+`CreateLookAt` camera case (`rasterizerstate_cullmode_camera_test.cpp`
scenario (b)).** Investigated live per this ticket's own acceptance gate ("diagnose ... rather than
masking"), narrowed considerably, but root cause NOT identified -- see `known_bugs.md`'s rewritten
entry for the complete trace. Confirmed via direct `Vector4::Transform(vertex, wvp)` instrumentation
that both test triangles have entirely valid, symmetric clip-space coordinates (`W=1.0` exactly,
`X`/`Y` well inside `[-1,1]`, identical `Z=0.1051` inside `[0,1]`) -- ruling out a matrix/math bug.
Also ruled out: `CullMode::None` silently not applying (the analogous CCW triangle in the Perspective
scenario with the identical camera DOES render under `CullMode::None`), draw order/leftover state
(swapping which triangle draws first reproduces identically), and the one LLGL-specific depth-range
remap that does exist in this backend (`QueuePrimitives`' `clippingRange == MinusOneToOne`
correction -- a real mechanism this entry's own earlier revision incorrectly claimed did not exist,
found this time by grepping for `ClippingRange` rather than `DepthRange`/`ClipControl`; it applies
identically to both triangles' shared WVP matrix and cannot explain the asymmetry). The test binary
is built (`cna_test_llgl_rasterizerstate_cullmode_camera`) but deliberately not CTest-registered
until this is resolved; a Vulkan-capable display (unavailable in this sandbox's Xvfb, no DRI3) is
the most promising next step, to determine whether this is OpenGL-module-specific or architectural.

**`LLGL-52` follow-up (2026-08-04): the promised next step landed -- this machine's own real
desktop (`DISPLAY=:0`, a physical AMD Radeon 780M with a working RADV driver) can present Vulkan,
unlike this sandbox's Xvfb instances. Two decisive new facts, both confirmed with live
instrumentation on real hardware, neither the root cause yet: (1) the bug is ARCHITECTURAL, not
OpenGL-module-specific -- `cna_test_llgl_rasterizerstate_cullmode_camera` run under
`CNA_LLGL_RENDERER=vulkan` on `DISPLAY=:0` reproduces scenario (b)'s failure identically, which
also independently rules out the GL-only `clippingRange` Z-remap as a cause (Vulkan never runs
that code path at all). (2) The failure tracks vertex POSITION, not winding -- swapping which
triangle is built at `centerA` vs `centerB` while keeping each one's own winding function
(`MakeCwBasis`/`MakeCcwBasis`) attached to its own variable name moved the failure WITH the
position (whichever triangle sits at `target + camRight*80`, this camera's screen-right side),
not with the label or the winding. See `known_bugs.md`'s rewritten entry for the full trace and
next lead (the shared `QueuePrimitives`/`AcquirePrimitivePipeline` deferred-command path, since
both independently-implemented renderer modules fail identically and the CPU-side matrix/vertex
math feeding them is provably correct for both triangles).

**`LLGL-53` progress (2026-08-04): all four requested mechanisms now wired end-to-end
(`SetViewport`'s `minDepth`/`maxDepth`, `RasterizerState.DepthBias`/`SlopeScaleDepthBias`, and
`DepthStencilState`'s full front/back stencil test); the core constant-DepthBias path is real,
verified, and committed, but each of the other three mechanisms hits its own genuine, unexplained
defect that is NOT this ticket's own new code (confirmed by isolation) -- left OPEN rather than
forced.** `AcquirePrimitivePipeline()` now sets `pipelineDesc.rasterizer.depthBias.constantFactor/
slopeFactor/clamp` (raw XNA units, matching the Vulkan backend's own unconverted
`vkCmdSetDepthBias` convention) and the full `pipelineDesc.stencil` (front/back
op/compare/mask, `TwoSidedStencilMode=false` falls back to front's own state for the back face,
matching the Vulkan/EasyGL backends' own established convention) instead of leaving both entirely
unapplied. `GraphicsDevice.ReferenceStencil` is wired as a genuinely dynamic per-draw state
(`stencil.referenceDynamic = true` + `CommandBuffer::SetStencilReference()` at replay, captured
per-`FrameCommand` the same way `BlendState.BlendFactor` already is) rather than baked statically
into the pipeline, so a standalone `ReferenceStencil` change (without a full `DepthStencilState`
re-apply) still takes effect. `SetViewport()`'s previously-ignored `minDepth`/`maxDepth` parameters
are now captured into `FrameCommand` (`CaptureFrameCommandViewportEXT()`, same "only `Primitives`
narrows away from the default" restriction the rect itself already had) and passed through to
`LLGL::Viewport`'s 6-argument constructor at replay.

**Process finding along the way, independently valuable:** folding these ~10 new fields into
`AcquirePrimitivePipeline()`'s existing single-`uint64_t` pipeline-cache key (using the SAME
small-multiplier style every other field there already uses) silently overflowed and discarded
`depthBias_`'s own contribution once the cumulative multiplier of everything folded in AFTER it
exceeded 2^64 -- `rasterizerstate_depthbias_test.cpp`'s A1 check computed the IDENTICAL key for
`DepthBias=0` and `DepthBias=3000000`, so the biased draw silently reused the unbiased cached
pipeline. Fixed by widening `primitivePipelineCache_`'s key to a 4-element
`std::tuple<uint64_t,uint64_t,uint64_t,uint64_t>` (free lexicographic ordering from `std::map`,
each new field group gets its own lossless 64-bit budget instead of competing for bits) -- see
`known_bugs.md`'s own dedicated entry for the two rejected techniques and the general lesson for
whoever eventually tackles `LLGL-48`.

**Verified** (`CNA_LLGL_RENDERER=opengl`, Xvfb, 2026-08-04):
`rasterizerstate_depthbias_test.cpp` (reused from Software, no `CNA_BACKEND_` conditionals) 12/17
PASS, now registered as `Llgl_RasterizerState_DepthBias` -- every constant-`DepthBias` flip check
(A1/B1/C1/E1/G0) and every zero-bias baseline that does NOT involve a custom depth range or a
render target (A0/B0/C0/D0/D2/E0/F0) passes. A new, minimal, RenderTargetCube-free
`llgl_stencil_test.cpp` (registered as `Llgl_Stencil`) confirms stencil WRITE + a MATCHING
`CompareFunction::Equal` test both work (2/4), but also newly found that a MISMATCHED reference is
NOT rejected -- the stencil test does not actually gate. `graphicsdevice_ordered_clear_test.cpp`'s
own `stencilBuffer3D` Contract flag is now `true` for LLGL (matching this ticket's own acceptance
gate, "enable the stencil branch"), though this sandbox's pre-existing `hasCubeTextures` crash
still blocks that file's own stencil checks from ever being reached here. A full 65-binary
regression sweep plus a focused core-test rerun (`Llgl_Smoke`, `Llgl_3D`, `Llgl_BasicEffect`,
`Llgl_Lighting`, `Llgl_FrontFaceWinding`, `Llgl_Msaa_RenderTarget`,
`Llgl_RenderTarget2D_Depth`, `Llgl_GraphicsDevice_ClearDepth`,
`Llgl_SkinnedEffect_LightingConformance`) shows zero regressions from this ticket's changes.

**Left OPEN, three/four separate genuine defects, none root-caused (see `known_bugs.md`'s own
dedicated entry for the full investigation trace):**
- `SlopeScaleDepthBias` alone has no measurable effect (D1 fails; the constant `DepthBias` term
  works perfectly in the SAME file).
- ANY draw under a non-default `Viewport.MinDepth`/`MaxDepth` renders nothing at all (H0/H1) --
  confirmed via `git stash` to be a genuine NEW regression from this ticket's own viewport-depth-
  range plumbing, most likely somewhere between the (confirmed-correct, via debug instrumentation)
  captured `LLGL::Viewport` call and LLGL's own OpenGL module, not yet traced further.
- A bound `RenderTarget2D` with depth testing renders nothing even at the completely default depth
  range and zero bias (I0/I1) -- also a confirmed new regression, via a different, unrelated
  trigger path from H0's; may share H0's root cause once found.
- The stencil COMPARE does not actually gate (writes correctly, but a mismatched reference is not
  rejected) -- found via the new `llgl_stencil_test.cpp`, not yet root-caused.

**`LLGL-53` follow-up (2026-08-04, docs only, commits `490ec9ed`/`8949e2c5`): cross-checked the
open stencil and depth-bias defects on real Vulkan** (this machine's own physical desktop,
`DISPLAY=:0`, AMD Radeon 780M/RADV -- see `LLGL-52`'s own follow-up above for how). The
stencil-doesn't-gate defect is now confirmed ARCHITECTURAL: `llgl_stencil_test.cpp` fails
identically under `CNA_LLGL_RENDERER=vulkan`, ruling out anything GL-module-specific. A NEW, fifth
defect was found this way, plausible but not fully confirmed: constant `DepthBias` -- the one
mechanism already verified working on OpenGL (Xvfb) -- appears to NOT take effect on the Vulkan
module at all (every bias-effect check failed while every baseline passed, a clean and
internally-consistent split). Confirmed NOT a repeat of the pipeline-cache-key bug (distinct
`LLGL::PipelineState*` objects ARE created for different bias values); LLGL's own
`VKGraphicsPSO.cpp` construction code was read and looks correct. **However, a real confound was
found in the same investigation:** the SAME suite run against the OpenGL module on this same real,
composited (`GNOME`/`Mutter`) display produced scattered, internally-inconsistent results (Xvfb-
reliable baselines failing here, an unrelated check unexpectedly passing) -- almost certainly
window/compositor interference with this test's pixel-exact small-window readback, not a real
defect. Since the Vulkan run was ALSO taken on this same composited display, the DepthBias-on-
Vulkan finding needs independent re-verification (ideally via a DRI3-capable Xvfb, `LLGL-55`'s own
scope) before being trusted fully, despite its own cleaner result pattern. See `known_bugs.md`'s
own entry for the complete trace.

The acceptance gate's own "update `SupportsCapability()` and `docs/llgl-backend.md` from measured
module results" was not done this session -- these five open defects should be reflected there
once (or instead of) being fixed, so the documented capability boundary matches what was actually
measured rather than what was intended.

**`LLGL-53` follow-up (2026-08-04, docs only): H0/H1's mechanism CONFIRMED, not fixed -- I0/I1
ruled to be a genuinely separate defect.** Live-instrumented the vendored LLGL OpenGL module again
(temporary, fully reverted). Traced `SetViewport()`'s captured values all the way to raw GL state
(`glGetIntegerv`/`glIsEnabled` queried directly): viewport, depth range, depth-test enable/func/
write-mask are ALL exactly as requested for H0's own draws -- ruling out the entire capture/replay
path. The scissor rect is identical between H0 (fails) and G0 (passes, same file, right before it),
ruling out `viewportSet_`'s own LLGL-53-widened condition (this investigation's first hypothesis)
too -- it was already true before this ticket touched anything, since this file's own viewport
width (96) already differs from the logical width (120) regardless of depth range. **Root
mechanism found:** `GraphicsDevice::Clear(Color)` clears the depth buffer to `Viewport.MaxDepth`
(not a hardcoded `1.0`) -- correct, FNA-faithful behavior (Task 928, predates this ticket). For H0
that's `0.8`; confirmed via direct `glGetFloatv(GL_DEPTH_CLEAR_VALUE, ...)` that this raw value IS
what reaches the GPU. Clearing to a non-`1.0` raw depth value, combined with a narrowed
`glDepthRangef(0.2, 0.8)` for the subsequent draws, causes the LEQUAL depth test to reject every
fragment -- proven by temporarily forcing the clear to always use `1.0` (a deliberately
XNA-incorrect experiment, reverted immediately), which makes H0 pass. **Not fixed**: the CORRECT
behavior is what triggers this, so reverting it would violate XNA fidelity for any other legitimate
non-default-`MaxDepth` clear, not just this case; the actual "why" (spec says `glClear` should
be depth-range-independent, but this is a software rasterizer) needs a native GPU debugger
(RenderDoc/apitrace, unavailable here) or real GPU hardware to distinguish an `llvmpipe`/Mesa
quirk from a genuine LLGL-side bug. Also established: I0/I1 do NOT share this mechanism (H1's own
block resets the viewport, and by extension `MaxDepth`, back to default before I's block runs) --
despite the superficially similar "renders nothing" symptom, I0/I1 remain a fully separate,
still-unexplained defect. See `known_bugs.md`'s entry for the complete trace.

**`LLGL-53` stencil-gate check (2026-08-04, no code change, nothing new found):** re-read the
stencil-doesn't-gate defect's own suspects with fresh eyes after cracking H0/H1 -- confirmed
`AcquirePrimitivePipeline()`'s pipeline-cache key already folds in `stencilFunction_`/`stencilPass_`
(so `WriteState`/`TestState` genuinely get distinct, non-colliding cached pipelines, not a repeat of
the depth-bias key-overflow class of bug); confirmed `ReferenceStencil` is captured correctly at
queue time and reaches `CommandBuffer::SetStencilReference()` at replay; confirmed LLGL's own
`GLDepthStencilState::BindStencilRefOnly()` reissues `glStencilFuncSeparate` with the CORRECT stored
func/mask alongside the new reference. No live instrumentation was run this pass (unlike H0/H1,
nothing in this reading turned up anything worth measuring) -- this is a "ruled out via code
reading" pass, not a "confirmed via live GL state" one; still open, root cause still unknown.

**`LLGL-54` progress (2026-08-04, commit `07a56e8d`): MSAA sample-count preservation across an MRT
bind fixed and verified; mip regeneration for MRT slots NOT addressed.**
`SetMultipleRenderTargetsEXT()` previously omitted `RenderTargetDescriptor::samples` entirely,
always creating a single-sample MRT render target regardless of what the individual slots were
created with -- fixed to read slot 0's own already-applied `MultiSampleCount` (the shared
`GraphicsDevice::SetRenderTargets()` layer already guarantees every slot's applied count matches
before this backend ever runs) and request it, with an anonymous multisampled colour attachment +
each slot's own texture as its resolve target, matching `CreateRenderTarget2D()`'s own established
single-target pattern. `LlglMRTBinding::GetSampleCount()` (previously hardcoded to the base
class's `1`, so `AcquirePrimitivePipeline()` kept building single-sample pipelines against a
multisampled MRT bind regardless) now has a real override. **Verified**
(`CNA_LLGL_RENDERER=opengl`, Xvfb): a new `llgl_mrt_msaa_test.cpp` (registered as `Llgl_MRT_MSAA`)
confirms BOTH bound slots independently resolve a genuinely antialiased edge, not just slot 0 --
confirmed via `git stash` to fail identically (a hard, unblended edge in both slots) on the
pre-fix binary. Also confirms the ticket's own "mixed requested/effective sample-count validation"
item is already enforced by the shared `SetRenderTargets()` layer upstream, not silently accepted.
A full 65+2-binary regression sweep plus the existing `llgl_mrt_test.cpp` show zero regressions.
Cube-face MRT slots were ALREADY rejected by name (not silently degraded) before this change,
satisfying that part of the acceptance gate without further work. **Left undone:** per-slot mip
regeneration after MRT writes -- `GetActiveMipRegenColorTextureEXT()`'s architecture returns a
SINGLE `LLGL::Texture*`, designed for one target; supporting "every written MRT slot" needs it (or
a sibling mechanism) restructured to return a list, a genuinely separate follow-up from the
MSAA fix. Per-slot lifetime tests (the acceptance gate's own explicit ask) were also not added.

**`LLGL-55` progress (2026-08-04): "no DRI3 crashes as an infrastructure skip" now covers every
LLGL test binary, and the `CNA_ENABLE_NET=OFF` build/link gap is closed. Two sub-items still open.**
Scope turned out much larger than the ticket's own "twelve renderer crashes" estimate: a full sweep
of all 69 registered LLGL test binaries with `CNA_LLGL_RENDERER` unset (this sandbox's Xvfb has no
DRI3, so the default Vulkan-preferring selection always hits `VK_ERROR_SURFACE_LOST_KHR`) found 43
hard crashes and 6 more that "only" mislabeled the crash as a false FAIL (see below) -- only 16 of
69 binaries route through `common/PixelTestGame.hpp`'s `RunPixelTest<TGame>()`, which already got a
narrow try/catch for this earlier in the session; every other binary hand-rolls its own
`Game`-derived class and `main()`, none of which caught anything.
- Tried `SKIP_REGULAR_EXPRESSION` on the CTest registration first, since it needs zero source
  changes -- **does not work**: CTest classifies a signal-terminated process as "Subprocess
  aborted" before it ever consults that property, regardless of what the process printed. Confirmed
  empirically, not assumed, before moving on (see [[feedback_llgl_investigate_dont_force_fix]]).
- Fix: a new shared translation unit, `examples/common/LlglVulkanWsiSkipGuard.cpp`, installs one
  process-wide `std::set_terminate()` handler at static-init time. It inspects the in-flight
  exception; if its message contains `VK_ERROR_SURFACE_LOST_KHR` it prints `[SKIP] ...` and
  `std::_Exit(77)` (`CNA::Examples::kSkipExitCode`, already covered project-wide by the root
  `CMakeLists.txt`'s bulk `SKIP_RETURN_CODE 77`); any other uncaught exception falls through to the
  previous terminate handler unchanged, so a genuine bug still aborts loudly. `cna_llgl_test()`
  (`cmake/Tests/LlglTests.cmake`) now compiles this file into every LLGL test executable, so the fix
  applies uniformly with no per-test-file `main()` retrofit. Deliberately test-only: linked via the
  test macro, never into the LLGL backend library itself, so a real game seeing a genuinely lost
  Vulkan surface is not silently swallowed.
- A second, distinct defect surfaced once the crashes stopped: 6 of the 69 binaries
  (`backbuffer_first_read`, `backbuffer_headless_reject`, `backbuffer_readback_dimension`,
  `bound_target_lifetime`, `deferred_source_lifetime`, `rendertarget_effect_source`) use a
  "supervisor" pattern (`RunLegIsolated()`) that forks+execs each leg as its own child process and
  classifies the child's exit code. None of the six recognized exit code 77 as anything but a
  generic non-zero failure, so once the guard converted the per-leg crash into a clean skip, the
  supervisor still reported `[FAIL] leg X: exited 77` and the whole binary still exited 1 -- the
  crash was gone but the false-FAIL mislabeling (the ticket's own acceptance criterion) was not.
  Fixed in all six: `RunLegIsolated()` gained a `skipped` out-parameter recognizing
  `kLegSkipExitCode == 77` distinctly from a real failure, and each `main()`'s aggregation now
  reports `%d passed, %d crashed, %d skipped` and returns 1 only if any leg genuinely failed,
  else 77 (skip) if any leg was skipped, else 0.
- **Verified**, `CNA_LLGL_RENDERER` unset, Xvfb `:99` (no DRI3): all 69/69 binaries now cleanly
  exit 77 with `[SKIP] Vulkan WSI unavailable ...`, zero crashes, zero false FAILs. Full `ctest -L
  Llgl` run: every test reports `Skipped` except one pre-existing, unrelated failure,
  `Llgl_Msaa_OpenGL` (a genuine swap-chain-MSAA-blending check failure under forced OpenGL,
  confirmed via `git log` to predate this entire session -- `llgl_msaa_test.cpp` untouched today --
  not investigated further here, out of LLGL-55's scope). Re-ran the existing
  `CNA_LLGL_RENDERER=opengl`-forced regression sweep across all 69 binaries: still exactly the
  established pre-existing crash/softfail set (`hasCubeTextures`, the LLGL-51 gap, the
  REMED-GFX-155 stencil leak, LLGL-52's camera bug, LLGL-53's open DepthBias/stencil items, plus
  the newly-confirmed `Llgl_Msaa_OpenGL`), zero new regressions.
- `CNA_ENABLE_NET=OFF` sub-item: also DONE, verified separately via a throwaway `build-probe/`
  configure (deleted once its result was recorded, per this project's own build-hygiene rule) --
  `cmake/UnitTests.cmake` now excludes the CNA_Net/GamerServices test directories by regex when
  `CNA_ENABLE_NET=OFF`, the same pattern already used there for FFmpeg/platform exclusions; `CnaTests`
  built and linked cleanly.
- **Left OPEN**: (1) "make shaderc discovery conditional on the renderer modules that need runtime
  SPIR-V compilation" -- investigated, no actionable fix exists yet: this project has no CMake
  option to select a subset of LLGL renderer modules (`CNA_GRAPHICS_BACKEND=LLGL` always links
  Null+OpenGL+Vulkan together), so shaderc is never actually unnecessary for an LLGL build today;
  doing this for real is module-selection infrastructure work, out of scope here. (2) "Keep explicit
  Vulkan and `_OpenGL` lanes so auto-selection cannot hide one module" -- only 23 of 69 registered
  LLGL tests currently have an explicit `_OpenGL`-suffixed lane, and there is no `_Vulkan`-suffixed
  lane anywhere, so on a DRI3-capable machine there is still no way to force+verify the Vulkan
  module through ctest specifically (only the auto-selecting default, which now merely skips
  cleanly here rather than proving Vulkan actually works elsewhere). Adding both lanes to all 69
  tests is a large, mechanical, separate follow-up.

**`LLGL-56` progress (2026-08-04): the named `XVisualInfo` leak is fixed and verified under
ASan/LSan; X11 rejection now has test coverage. Wayland/capability-API redesign not attempted.**
`LlglSdlSurface::GetNativeHandle()` called `XGetVisualInfo()` fresh on every invocation (LLGL calls
it once per swap-chain creation and again on every resize) and never freed the previous result --
its own comment claimed "at most one allocation per query," which was true per call but not
across calls, so each resize leaked another block. Fixed: the window's visual and colormap are
immutable for the window's lifetime (SDL commits to one at creation), so both are now resolved
once, cached in two new private members (`cachedVisualInfo_`, `cachedColorMap_`), and released by
a new `~LlglSdlSurface()` destructor via `XFree()`.
- **Verified via ASan/LSan**, not assumed: configured a new `build-asan/` (per this project's own
  shared-build-dir convention; `-fsanitize=address`, ccache, `CNA_ENABLE_NET=OFF` to keep the build
  small), built only `cna_test_llgl_resize` (the acceptance gate's own named scenario), and
  compared `git stash`'d pre-fix vs. post-fix leak totals under `LeakSanitizer`. Result: the fix
  removes exactly 3 objects / 1920 bytes (3 x 640-byte `XGetVisualInfo` allocations, matching this
  test's 3 total `GetNativeHandle()` calls: initial swap-chain creation + grow resize + shrink
  resize) -- confirmed via a temporary debug print that `~LlglSdlSurface()` does run and does call
  `XFree()` on a non-null cached pointer. The remaining ~899 leaked allocations in both runs are
  identical and live entirely inside vendored LLGL/Mesa GL-context code
  (`LinuxGLContextX11`/`GLContextManager`/`LinuxGLSwapChainContextX11`, per the full ASan stack
  traces) -- LLGL's own internal `XGetVisualInfo` calls, not `LlglSdlSurface`'s, and out of this
  ticket's named scope (`LlglSdlSurface::GetNativeHandle()` specifically).
- Added `tests/CNA/Internal/Backends/Llgl/LlglSdlSurfaceTests.cpp` (new directory; follows the
  existing `tests/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackendTests.cpp` precedent for
  gtest-covering a backend-internal adapter class): constructs a real `SDL_Window` under SDL's
  "dummy" video driver (via `SDL_SetHintWithPriority(..., SDL_HINT_OVERRIDE)` -- a plain
  `SDL_SetHint` does NOT override the `SDL_VIDEODRIVER=x11` this project's own ctest registrations
  already set, confirmed empirically after the first attempt silently ran under `x11` instead) and
  confirms `LlglSdlSurface`'s constructor throws with a message naming the actual fix
  (`SDL_VIDEODRIVER=x11`). This closes the acceptance gate's "X11 rejection/selection is covered by
  tests" half; the existing constructor error message already satisfied "Wayland is ... rejected
  once with an actionable capability message" before this session.
- **Left undone**: a real Wayland integration test (no Wayland session exists in this sandbox) and
  any redesign of the X11-only restriction into "a first-class build/runtime capability" API --
  the acceptance gate's own wording treats the existing throw-with-actionable-message as already
  satisfying that half, so no redesign was attempted.
- **New, separate finding surfaced while verifying this ticket** (not fixed here): `CnaTests`
  gtest fixtures that construct a real `GraphicsDevice` (unlike the `examples/` LLGL binaries) have
  no Vulkan-WSI-unavailable guard at all, and one such failure can break the X11 connection for
  every later test in the same process. See `known_bugs.md`'s new entry.

---

## Closing notes

The 2D baseline and a broad 3D/effect/render-target surface are real and pixel-verified, but only on
one platform and primarily against software rasterizers. Before this backend is offered as a general
alternative to `VULKAN` or `EASYGL`, Phase LLGL-8's P0/P1 tasks and LLGL-38's real-hardware matrix
must be complete; P2 limitations must either be implemented or accurately capability-gated.

`LLGL-17` is worth remembering for more than its fix: every symptom pointed at resource binding,
and every experiment aimed there was wasted. What actually settled it was a shader that could not
possibly produce black, and then instrumenting the dependency to see whether it compiled the source
at all. When a component's own behaviour contradicts what it was configured with, check that it
received what you think you gave it before theorising about what it does with it.
