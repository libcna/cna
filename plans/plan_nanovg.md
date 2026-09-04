# CNA NANOVG Renderer Plan

## Scope

`NANOVG` is CNA's second 2D-vector-graphics-on-real-GL renderer, implemented on
[memononen/nanovg](https://github.com/memononen/nanovg) (pinned commit
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353`, zlib license) — a small, widely used antialiased
vector-graphics library driven through its own compiled GLSL shader pipeline (the **GL2** backend:
`NANOVG_GL2_IMPLEMENTATION`, GLSL 1.10, no VAOs, no uniform-buffer path) on top of a real desktop
OpenGL 2.1 compatibility context CNA creates and owns itself — the same "own GL context, no
EasyGL" shape `OPENGL1`/`OPENGL2`/`OPENVG` already use.

Its place among the existing 2D-oriented identities (`docs/renderer-expansion-candidates.md` §A6),
and what justifies it as a separate public identity rather than an alias
(`docs/renderer-registry.md`'s no-alias rule):

| | Pipeline model | On |
|---|---|---|
| `SKIA` | CPU raster | no GPU |
| `BLEND2D` | CPU raster | no GPU |
| `OPENVG` | fixed-function OpenVG 1.1 (ShivaVG) | real desktop GL, immediate-mode client state |
| **`NANOVG`** | **shader-driven vector rasterization (own GLSL pipeline)** | **real desktop GL, compiled shaders + VBOs** |

`OPENVG` talks to the GPU exclusively through immediate-mode fixed-function GL (`glBegin`-era
state, no shader compilation at all). `NANOVG` is the first 2D-only CNA renderer that compiles and
links its own real GLSL program and drives it through vertex buffer objects — proving CNA's
renderer contract against a genuinely different, shader-based 2D vector pipeline that neither
`OPENVG` nor `SKIA`/`BLEND2D` (CPU-only) exercise.

Target platforms: desktop Linux, Windows, macOS only (same GLX/WGL/CGL-needs-a-real-context gate as
`OPENVG`/`OPENGL1`; NanoVG's GL2 backend needs a real, non-ES desktop GL context — GLES/WebGL are a
different, unbuilt NanoVG backend, so this identity does not claim them).

## Design decisions

1. **GL2 backend, not GL3.** NanoVG's GL3 backend (`NANOVG_GL3_IMPLEMENTATION`) additionally needs
   a core-profile context, VAOs, and (by default) a uniform-buffer-object path. The GL2 backend
   needs only a GL 2.0+ compatibility context (GLSL 1.10 shaders, loose uniforms, no VAOs) —
   exactly the same context shape `OPENVG`/`OPENGL2` already request
   (`majorVersion=2, minorVersion=1, profile=Compatibility`), so this renderer reuses that request
   verbatim rather than inventing a third context shape.
2. **NanoVG is not a loader.** `nanovg_gl.h` calls `gl*` entry points unqualified, assuming the
   including translation unit already has them resolvable — unlike GLAD/GLEW it does no loading of
   its own. On desktop GLX/WGL only GL 1.1 is guaranteed statically linkable; every later entry
   point NanoVG's GL2 backend calls (`glActiveTexture`, `glCreateShader`, `glBindBuffer`,
   `glBlendFuncSeparate`, `glStencilOpSeparate`, ~28 in total) is resolved through
   `CNA::Internal::Renderers::LoadPlatformGlProcAddress` (the same platform-provided loader
   `OpenGL2Renderer.cpp` already uses for its own shader functions) and `#define`d over the plain
   `gl*` name in exactly one translation unit (`NanoVgGl.cpp`) before `#include "nanovg_gl.h"` —
   see `nanovg-spike/README.md` for the proof this mechanism actually works, and
   `NanoVgGlLoader.hpp` for the CNA-integrated version.
3. **Presentation model reused verbatim from `OpenVgRenderer`.** `ComputeLogicalViewportEXT()`,
   `EnsureSurfaceSizeEXT()`, the `GL_PROJECTION`-adjacent ortho (expressed as the extent handed to
   `nvgBeginFrame`, since NanoVG has no separate projection matrix concept of its own) and the
   window<->logical transforms are the same algorithm, ported to `NanoVgRenderer`. No new
   presentation math is invented, and — unlike `OpenVgRenderer` — **no device-flip transform is
   involved anywhere**: NanoVG already renders in a Y-down, top-left-origin space, matching XNA,
   while OpenVG's image space is Y-up. (NVG-20 later replaced the logical extent with the active
   `GraphicsDevice.Viewport`'s own — see that task's row.)
4. **`SpriteBatch` draws via `nvgImagePattern` + a filled rect path**, not a hypothetical
   "draw image" primitive (NanoVG has none) — `nvgBeginPath`/`nvgRect`/`nvgFillPaint`/`nvgFill`
   with a paint built from `nvgImagePattern(ox,oy,ex,ey,angle,image,alpha)`, matching the pattern
   every NanoVG-based sprite renderer in the wild uses. Rotation/origin/scale/flip are expressed as
   `nvgSave`/`nvgTranslate`/`nvgRotate`/`nvgScale`/`nvgRestore` around the pattern + fill, composed
   in the same translate→rotate→scale→flip order `OpenVgSpriteBatchRenderer`/`CanvasSpriteBatchRenderer`
   already establish.
5. **Blending is expressed as real blend factors, not as NanoVG composite presets.** *(Corrected
   by NVG-18; the original decision routed through `nvgGlobalCompositeOperation` and is described
   in that task's row below.)* `nvgGlobalCompositeBlendFuncSeparate(ctx, srcRGB, dstRGB, srcAlpha,
   dstAlpha)` reaches a genuine `glBlendFuncSeparate`, which is a 1:1 fit for `BlendState`'s own
   four factors — so `Opaque`/`AlphaBlend`/`NonPremultiplied`/`Additive` and every custom state
   built from representable factors are honoured exactly, with the colour and alpha channels
   independent. For that to be correct the fragment stage must emit XNA's own `texel * tint`, so
   every image is created with `NVG_IMAGE_PREMULTIPLIED` (which selects the shader branch that
   leaves the sampled texel alone — it is not a claim about the uploaded bytes, which stay straight
   RGBA8) and the tint is pre-divided by its own alpha to survive NanoVG's `glnvg__premulColor`.
   `GraphicsCapability::AdditiveBlending` reports **true**; `Blend.BlendFactor`/
   `InverseBlendFactor`, non-`Add` `BlendFunction`s and `SourceAlphaSaturation` as a destination
   factor are refused by name.
6. **No render targets, no 3D, no custom Effects** — NanoVG has no off-screen-image-as-draw-target
   concept usable as a `RenderTarget2D` (its `NVGLUframebuffer` helper in `nanovg_gl_utils.h` is
   explicitly excluded from this renderer's scope; `CreateRenderTarget2D` keeps the shared
   `nullptr` default) and no 3D pipeline at all — every inherently-3D `IGraphicsRenderer` pure
   virtual throws under the shared `Unsupported3DGraphicsCallBehavior::Throw` default, identical in
   shape to `OpenVgRenderer`/`CanvasRenderer`.

## Tasks

| ID | Task | Status |
|---|---|---|
| NVG-1 | Existence-gate spike (`nanovg-spike/`): real GLX context, custom GL2 function loader, `nvgCreateGL2`, a filled path rendered and read back via `glReadPixels` under Xvfb. | DONE |
| NVG-2 | `cmake/ThirdPartyNanoVG.cmake`: `FetchContent` NanoVG at the pinned commit, compile `nanovg.c` (GL2 implementation) into `cna_thirdparty_nanovg`, link real `OpenGL::GL`. | DONE |
| NVG-3 | `GraphicsRendererType.hpp`: add `NanoVg` to the enum (after `PixiJs`), the name-switch entry `"NANOVG"`, and move the `tryParseGraphicsRendererName` upper bound to the new last ordinal. | DONE |
| NVG-4 | `cmake/RendererSelection.cmake`: `CNA_GRAPHICS_RENDERER` STRINGS list, `option(CNA_RENDERER_NANOVG ...)`, the multi-option OR/enabled-list arms, the `elseif(... STREQUAL "NANOVG")` dispatch arm, and the desktop-only platform gate (same shape as `OPENVG`'s). | DONE |
| NVG-5 | `cmake/RendererRegistry.cmake`: `NANOVG NanoVg` map entry. `cmake/RendererCombinations.cmake`: add `NANOVG` to `CNA_RENDERER_REAL_GL_FAMILIES` (own real GL context, same reasoning as `OPENVG`). | DONE |
| NVG-6 | `modules/renderers/nanovg/` module skeleton (`CMakeLists.txt`, `include/CNA/Internal/Renderers/NanoVg/`, `src/`, `tests/`, `examples/`), registered by the physical source-partition validator automatically (directory-name-derived). | DONE |
| NVG-7 | `NanoVgGlLoader.hpp`/`NanoVgGl.cpp`: the ~28-entry GL function-pointer loader + `#define` shim, and the one translation unit that `#include`s `nanovg_gl.h` with `NANOVG_GL2_IMPLEMENTATION`. | DONE |
| NVG-8 | `NanoVgTextureRenderer`: `nvgCreateImageRGBA`/`nvgUpdateImage`-backed texture, straight (non-premultiplied) RGBA8, top-row-first (NanoVG images are already top-left-origin — no row flip needed, unlike OpenVG). | DONE |
| NVG-9 | `NanoVgRenderer`: construction/destruction (real GL context via `PlatformGlContextOwner`, `nvgCreateGL2`/`nvgDeleteGL2` reached through `NanoVgGl.cpp`'s own `CreateNanoVgGL2Context`/`DeleteNanoVgGL2Context` wrappers — see NVG-7's own note on why), `Clear`/`Present`, presentation-mode viewport math ported from `OpenVgRenderer`, `SetScissorRect`/`SetViewport`, `ApplyBlendState` (real `nvgGlobalCompositeOperation`), `ApplyRasterizerState`/`ApplyDepthStencilState` (2D-only rejection shape), `Ensure3DSupported` + every inherently-3D override, `ReadBackbuffer`. | DONE |
| NVG-10 | `NanoVgSpriteBatchRenderer`: `nvgImagePattern` + filled-rect-path draw, translate/rotate/scale/flip composition, tint via overwriting the pattern paint's `innerColor`/`outerColor`, sampler state (see NVG-18 for its corrected per-batch handling). | DONE |
| NVG-11 | `NanoVgRendererDescriptor.cpp`: pre-construction contract (`RendererWindowKind::OpenGL`, `needsGlContext=true`, `AlwaysAvailable`). | DONE |
| NVG-12 | `scripts/check_renderer_identities.py`: add `("NANOVG", "NanoVg")` to the canonical `IDENTITIES` table (49 → 50). | DONE |
| NVG-13 | Update the whole-registry count in every `COUNTED_DOCUMENTS`-listed file whose count changes (`docs/runtime-renderer-selection.md`, `docs/renderer-expansion-candidates.md`, `docs/physical-modules.md`, `plan_platform.md`), and mark NANOVG delivered in `docs/renderer-expansion-candidates.md` §3 (Tier A6) and `FUTURE.md`. | DONE |
| NVG-14 | `docs/nanovg-renderer.md`: capability boundary, presentation model, blend-mapping table, dependency/build notes, test status — same shape as `docs/openvg-renderer.md`. | DONE |
| NVG-15 | Tests (`modules/renderers/nanovg/examples/`): smoke (Clear + SpriteBatch draw + readback), rotation/orientation pixel oracle, blend-mode pixel oracle (Opaque/AlphaBlend/NonPremultiplied/**Additive**), unsupported-3D-behavior guard. | DONE |
| NVG-16 | Configure + build `-DCNA_GRAPHICS_RENDERER=NANOVG` (`cmake-build-nanovg/`), run the NanoVg-labelled CTest suite under Xvfb, confirm `scripts/check_renderer_identities.py` passes with the new count. | DONE |
| NVG-17 | Audit pass: two adversarial test files (`nanovg_texture_orientation_test`, `nanovg_presentation_viewport_scissor_test`) closing the rigor gap against `OPENVG`'s own test precedent (partial-`sourceRectangle` crop math, `Clamp` pixel-exactness, `SpriteEffects` flips, `UpdatePixels`, every presentation mode, custom `Viewport`, resize-without-`Clear`, scissor, multi-instance coexistence). Found and genuinely fixed a real bug: `NanoVgRenderer` and its `SpriteBatch`/`Texture` helpers never called `MakeCurrent()`, so two live instances silently corrupted each other's GL state (added `MakeContextCurrentEXT()` to every GL-touching entry point). Also found and documented a real, permanent characteristic (not a bug): a partial-`sourceRectangle` crop's internal seam with its own neighboring texel bleeds under linear filtering with no flat safety margin, unlike the outer `GL_CLAMP_TO_EDGE` bound. | DONE |
| NVG-18 | Deep correctness audit of `SpriteBatch` semantics, and the repair of what it found. Five defects, all reproduced against the shipped implementation before any fix: (1) `AlphaBlend` double-premultiplied its source RGB, because `nanovg_gl.h`'s fragment shader applied a `SourceAlpha` factor of its own on top of already-premultiplied data; (2) `AlphaBlend` and `NonPremultiplied` were both mapped to `NVG_SOURCE_OVER` and so produced identical pixels although their source factors differ; (3) destination alpha was wrong for every state whose alpha source factor is not `One`; (4) `Opaque` attenuated a translucent source by its own alpha — documented until then as an unavoidable NanoVG limitation, actually a consequence of (1); (5) every custom `BlendState` was rejected although `nvgGlobalCompositeBlendFuncSeparate` can express nearly all of them. Also: `SetSamplerFilter` was an empty no-op while `SamplerState.PointClamp` was accepted without complaint, `Wrap`/`Mirror` was rejected outright, and sprite quads received NanoVG's vector-path edge feathering. Fixed by emitting XNA's own `texel * tint` from the fragment stage (`NVG_IMAGE_PREMULTIPLIED` plus an alpha-pre-divided tint), mapping `BlendState` factor-by-factor onto `NVGblendFactor`, writing the batch's sampler onto each drawn image's GL texture object per draw, and scoping `nvgShapeAntiAlias(0)` to the sprite fill. Three test files rewritten or added (`nanovg_blend_test`, `nanovg_sampler_state_test`, `nanovg_sprite_rasterization_test`), each check confirmed to fail against the previous implementation. | DONE |

| NVG-19 | Follow-up review pass on NVG-18. Three further accepted-API divergences, each reproduced against the then-current code before being fixed: (1) `SetImmediateMode` was a documented no-op on a renderer that defers every `Draw()` to `nvgEndFrame`, so a `GraphicsDevice` operation issued between two Immediate draws landed before the whole batch instead of between them -- fixed with a real per-draw flush through `nvgInternalParams(ctx)->renderFlush`, which empties the recorded call list without ending the frame (so the batch's scissor/transform/blend state survives, unlike an `nvgEndFrame`/`nvgBeginFrame` pair); (2) a mip-mapped `Texture2D` was accepted although `nvgCreateImageRGBA` allocates one level, and `UpdatePixelsLevel` inherited `ITextureRenderer`'s empty default so `SetData(level>0, ...)` discarded the upload silently -- both now refused, at construction and at upload, matching TINYGL's own construction-time gate; (3) found by the new Immediate test rather than by review: `nvgScissor` is a fragment-shader MASK, not a rasterizer clip, so a clipped-away fragment still writes zero and blackens the region under any `BlendState` whose destination factor does not preserve the destination at zero source (`Opaque` most visibly) -- each sprite quad is now clipped geometrically against the scissor rectangle instead, which is exact for every `BlendState`, exact for a rotated quad, and hard-edged. New `nanovg_immediate_mode_test`; mip and Opaque-scissor cases added to the existing texture and presentation tests. The pre-existing scissor assertion was also strengthened: it had run against a `(3,3,3)` background, close enough to black that a blackened fragment passed inside the tolerance. | DONE |

| NVG-20 | Third review pass. Two further accepted-API divergences, both reproduced before being fixed. (1) A custom `GraphicsDevice.Viewport` was honoured as a rasterizer viewport but NOT as a sprite coordinate space: `Begin()` handed `nvgBeginFrame` the full drawable while `glViewport` already held the sub-region, so every sprite was squashed into it -- REMED-GFX-072's canonical "squish" (a 17px sprite in a 41-wide viewport on a 96-wide backbuffer came out 7px). Fixed by `NanoVgRenderer::GetSpriteProjectionEXT()`, which sizes the sprite space to the active viewport whenever it differs from `GetDefaultViewportRect()` -- compared against the DEFAULT rect rather than the whole drawable, because this renderer's own `Letterbox`/`Overscan` default viewport is already a physical sub-rectangle while sprites there stay in logical space. The scissor rectangle is carried into the new space with it. (2) Immediate mode gained ordering in NVG-19 but still not live device state: the blend factors and projection were captured once at `Begin()`, so a `BlendState` set between two Immediate `Draw()` calls did not apply to the second sprite. Each Immediate `Draw()` now re-reads both first. NANOVG is registered against the SHARED `spritebatch_custom_viewport_test.cpp` and `spritebatch_viewport_switch_test.cpp` instead of a renderer-local substitute -- the local test that existed drew a full-canvas sprite, which squashes into exactly the sub-viewport and therefore passed against the broken projection. | DONE |

| NVG-21 | Fourth review pass. One correctness bug plus the consistency cleanup it exposed. (1) NVG-20's per-draw Immediate re-read assigned its cached `SpriteProjection` only inside the re-open branch, but re-opening is required only when `nvgBeginFrame`'s own inputs (extent, device-pixel ratio) change -- so a viewport MOVED at constant size refreshed nothing, leaving the previous viewport's origin in `scissorOffsetX/Y` and clipping the next sprite against the old rectangle (viewport `(60,10,40,30)` -> `(80,10,40,30)` with scissor `(80,10,20,30)` put the second sprite at physical x 100..120 instead of 80..100). Split into a narrow `needsFrameReopen` test -- which now also covers `devicePixelRatio`, previously unchecked -- with the projection refreshed unconditionally. (2) `BlendState.MultiSampleMask` was the last state this renderer neither implemented nor refused; a coverage mask has no observable effect without a multisample framebuffer, but that argues for silence rather than acceptance, so it is now rejected. Also added the `Stretch` + custom `Viewport` + scissor scene -- the only configuration where the scissor's X and Y scale factors differ, so NVG-20's mapping is exercised rather than merely believed -- and corrected three stale comments that still described `nvgScissor` as the sprite clip and the frame as strictly one per `Begin()`/`End()`. A closing review found the last wording gap: only `colorWriteChannels[0]` was checked while the documentation claimed the whole property was refused, so all four per-render-target slots are now checked and each names its own slot. | DONE |

| NVG-22 | External review pass (Linux scope only; Windows/macOS validation explicitly deferred by the owner). Three defensive gaps, none of them a wrong-pixel bug in ordinary single-device drawing: (1) `Draw()` accepted a `Texture2D` created on a DIFFERENT `NanoVgRenderer` after only a `dynamic_cast` -- NanoVG image handles are per-`NVGcontext` integers from a counter that starts at the same value in every context, so a foreign texture names a valid but different image and would have been drawn silently as the wrong picture (proven with two live renderers whose first textures share a handle); now refused. (2) `LoadNanoVgGlFunctions()` stored loader results without checking them, so an unresolvable entry point became a null call inside `nvgCreateGL2()`; every required symbol is now verified and the first missing one named, with `glGenerateMipmap` exempt because its only call site is compiled out of the GL2 backend. (3) `nvgCreateImageRGBA` never asks GL what it can allocate and does not check `glGetError`, so a texture past `GL_MAX_TEXTURE_SIZE` became a storage-less texture object sampling as garbage; the limit is queried once at construction and enforced in `NanoVgTextureRenderer`. Deliberately NOT routed through `GetMaxTextureSizeForProfileEXT()`, whose own contract is a `GraphicsProfile` ceiling rather than a hardware query. A follow-up review corrected two things this task got wrong on its first pass: the granted GL context attributes ARE readable (`PlatformGlContextOwner::GetAttributes()`), so construction now refuses a sub-2.0 context and one with fewer than 8 stencil bits -- the latter matters because a missing stencil plane, unlike a missing shader stage, does not make `nvgCreateGL2()` fail at all; and the cross-instance texture refusal belongs in `SpriteBatch::pushSprite` (a `GraphicsDevice` mismatch, refused before the sprite is queued) rather than only at the renderer seam, because a refusal raised from `flushBatch()` throws out of `End()` and leaves the batch wedged with the offending sprite still in its queue. Also added `.github/workflows/nanovg-ci.yml` (Linux/Mesa/Xvfb, the one configuration this renderer is validated on), a `THIRD_PARTY_NOTICES.md` entry for NanoVG's zlib license, and removed design decision 3's stale "device-flip transform" wording -- this renderer has never used one. | DONE |

## Status

**Complete**, including six adversarial audit passes: NVG-17 (geometry, presentation, multi-instance
coexistence), NVG-18 (blend, sampler and rasterization semantics), NVG-19 (Immediate-mode
submission ordering, mip-level refusal, geometric scissor clipping) and NVG-20 (custom-viewport
sprite projection, Immediate-mode live device state) and NVG-21 (constant-size viewport moves,
`MultiSampleMask` refusal) and NVG-22 (cross-instance texture refusal, GL loader and texture-size
validation, Linux CI). See
`docs/nanovg-renderer.md` for the delivered capability boundary and test status, and
`nanovg-spike/README.md` for the existence-gate proof that predates the CNA integration.

No further work (a GL3/shader-effect path, render-target support via `NVGLUframebuffer`, or a
GLES/WebGL backend) is planned. Any of it needs its own explicit owner instruction, exactly like
every other renderer's plan.
