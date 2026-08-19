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
   `EnsureSurfaceSizeEXT()`, the `GL_PROJECTION`-adjacent ortho (expressed as NanoVG's own
   `nvgBeginFrame` device-pixel-ratio + a device-flip transform, since NanoVG has no separate
   projection matrix concept of its own — it always renders in a Y-down, top-left-origin logical
   space already, unlike OpenVG's Y-up image space) and the window<->logical transforms are the
   same algorithm, ported to `NanoVgRenderer`. No new presentation math is invented.
4. **`SpriteBatch` draws via `nvgImagePattern` + a filled rect path**, not a hypothetical
   "draw image" primitive (NanoVG has none) — `nvgBeginPath`/`nvgRect`/`nvgFillPaint`/`nvgFill`
   with a paint built from `nvgImagePattern(ox,oy,ex,ey,angle,image,alpha)`, matching the pattern
   every NanoVG-based sprite renderer in the wild uses. Rotation/origin/scale/flip are expressed as
   `nvgSave`/`nvgTranslate`/`nvgRotate`/`nvgScale`/`nvgRestore` around the pattern + fill, composed
   in the same translate→rotate→scale→flip order `OpenVgSpriteBatchRenderer`/`CanvasSpriteBatchRenderer`
   already establish.
5. **Real `BlendState.Additive` support — a genuine capability edge over `OPENVG`.** NanoVG's own
   fragment shader always premultiplies an RGBA image's sampled colour by its own alpha before any
   blend stage runs (`nanovg_gl.h`: `if (texType == 1) color = vec4(color.xyz*color.w, color.w)`,
   true for every non-`NVG_IMAGE_PREMULTIPLIED` image, which is every `NanoVgTextureRenderer`).
   That is *why* `NVG_LIGHTER` (`GL_ONE, GL_ONE`, `nanovg.c`'s own `nvg__compositeOperationState`)
   exactly reproduces real XNA `BlendState.Additive` (`SourceAlpha, One`,
   `modules/graphics/src/Xna/BlendState.cpp`) — the shader's own premultiply already applies the
   `SourceAlpha` factor — unlike ShivaVG, which declares `VG_BLEND_ADDITIVE` but never implements
   it. The same premultiply is why `NVG_SOURCE_OVER` (`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) exactly
   reproduces both `AlphaBlend` and `NonPremultiplied`. It is also why `NVG_COPY` (Opaque,
   `GL_ONE, GL_ZERO`) has one real, permanent, documented deviation: no compensating blend factor
   exists for "copy", so a translucent (not fully-opaque) source shows alpha-attenuated colour
   instead of the full un-multiplied source colour — see `docs/nanovg-renderer.md`'s own section on
   this. `GraphicsCapability::AdditiveBlending` reports **true**.
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
| NVG-10 | `NanoVgSpriteBatchRenderer`: `nvgImagePattern` + filled-rect-path draw, translate/rotate/scale/flip composition, tint via overwriting the pattern paint's `innerColor`/`outerColor`, sampler address mode (NanoVG images are always clamped — `Wrap`/`Mirror` rejected, matching `OPENVG`'s own out-of-bounds behavior; `SetSamplerFilter` is a documented no-op — see docs/nanovg-renderer.md). | DONE |
| NVG-11 | `NanoVgRendererDescriptor.cpp`: pre-construction contract (`RendererWindowKind::OpenGL`, `needsGlContext=true`, `AlwaysAvailable`). | DONE |
| NVG-12 | `scripts/check_renderer_identities.py`: add `("NANOVG", "NanoVg")` to the canonical `IDENTITIES` table (49 → 50). | DONE |
| NVG-13 | Update the whole-registry count in every `COUNTED_DOCUMENTS`-listed file whose count changes (`docs/runtime-renderer-selection.md`, `docs/renderer-expansion-candidates.md`, `docs/physical-modules.md`, `plan_platform.md`), and mark NANOVG delivered in `docs/renderer-expansion-candidates.md` §3 (Tier A6) and `FUTURE.md`. | DONE |
| NVG-14 | `docs/nanovg-renderer.md`: capability boundary, presentation model, blend-mapping table, dependency/build notes, test status — same shape as `docs/openvg-renderer.md`. | DONE |
| NVG-15 | Tests (`modules/renderers/nanovg/examples/`): smoke (Clear + SpriteBatch draw + readback), rotation/orientation pixel oracle, blend-mode pixel oracle (Opaque/AlphaBlend/NonPremultiplied/**Additive**), unsupported-3D-behavior guard. | DONE |
| NVG-16 | Configure + build `-DCNA_GRAPHICS_RENDERER=NANOVG` (`cmake-build-nanovg/`), run the NanoVg-labelled CTest suite under Xvfb, confirm `scripts/check_renderer_identities.py` passes with the new count. | DONE |
| NVG-17 | Audit pass: two adversarial test files (`nanovg_texture_orientation_test`, `nanovg_presentation_viewport_scissor_test`) closing the rigor gap against `OPENVG`'s own test precedent (partial-`sourceRectangle` crop math, `Clamp` pixel-exactness, `SpriteEffects` flips, `UpdatePixels`, every presentation mode, custom `Viewport`, resize-without-`Clear`, scissor, multi-instance coexistence). Found and genuinely fixed a real bug: `NanoVgRenderer` and its `SpriteBatch`/`Texture` helpers never called `MakeCurrent()`, so two live instances silently corrupted each other's GL state (added `MakeContextCurrentEXT()` to every GL-touching entry point). Also found and documented a real, permanent characteristic (not a bug): a partial-`sourceRectangle` crop's internal seam with its own neighboring texel bleeds under linear filtering with no flat safety margin, unlike the outer `GL_CLAMP_TO_EDGE` bound. | DONE |

## Status

**Complete**, including a second adversarial audit pass (NVG-17). See `docs/nanovg-renderer.md`
for the delivered capability boundary and test status, and `nanovg-spike/README.md` for the
existence-gate proof that predates the CNA integration.

No further work (a GL3/shader-effect path, render-target support via `NVGLUframebuffer`, or a
GLES/WebGL backend) is planned. Any of it needs its own explicit owner instruction, exactly like
every other renderer's plan.
