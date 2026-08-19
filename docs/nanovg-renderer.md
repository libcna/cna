# NanoVG renderer

## Current status

`CNA_GRAPHICS_RENDERER=NANOVG` is a 2D vector-graphics renderer implemented through
[NanoVG](https://github.com/memononen/nanovg) (memononen/nanovg, pinned commit
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353`, zlib license) — a small, widely used vector-graphics
library driven through its own compiled GLSL 1.10 shader pipeline (the **GL2 backend**,
`NANOVG_GL2_IMPLEMENTATION`) on top of a real desktop OpenGL 2.1 compatibility context CNA creates
and owns itself (via `PlatformGlContextOwner`, the same "own GL context, no EasyGL" shape
`OPENGL1`/`OPENGL2`/`OPENVG` already use).

    CNA -> NanoVgRenderer -> NanoVG (real nvgBeginPath/nvgFill/nvgImagePattern/... entry points,
           compiled GLSL shaders) -> OpenGL

See `nanovg-spike/README.md` for the standalone existence-gate proof (a real GLSL-shader-driven
path drawn and read back via `glReadPixels`, under Xvfb) that predates this renderer's CNA
integration, and `plan_nanovg.md` for the delivery task list and design decisions.

NanoVG is a 2D-only vector-graphics API: it has no 3D pipeline, no vertex/index buffer concept
reachable from CNA, and no caller-addressable depth/stencil surface (its own internal stencil
usage for anti-aliased stroke rendering is a private implementation detail). Every inherently-3D
`IGraphicsRenderer` pure virtual throws (`Unsupported3DGraphicsCallBehavior::Throw`, the shared
default) rather than being silently ignored or faked, and `Ensure3DSupported()` is overridden so
every modern `GraphicsDevice.Draw*`/`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry point is
rejected before any vertex/index-buffer validation or sampler-state application runs.

## How it differs from OPENVG

`OPENVG` (ShivaVG) and `NANOVG` are CNA's two 2D-vector-on-real-GL renderers, but they occupy
genuinely different cells (`docs/renderer-registry.md`'s no-alias rule):

| | Pipeline model | Coordinate system |
|---|---|---|
| `OPENVG` | Fixed-function/immediate-mode GL (no shader compilation at all) | Y-up image space; every draw needs a device-flip transform |
| `NANOVG` | Compiled GLSL shaders, real VBOs | Top-left-origin, Y-down (matches XNA/Canvas2D directly) |

Two concrete consequences, both verified by tests:

- **No device-flip transform anywhere in `NanoVgSpriteBatchRenderer`.** NanoVG's own coordinate
  system already matches XNA's, the same reason `CanvasSpriteBatchRenderer` needs none either.
- **Blending is expressed as real blend factors, not as preset modes.** NanoVG exposes
  `nvgGlobalCompositeBlendFuncSeparate(ctx, srcRGB, dstRGB, srcAlpha, dstAlpha)`, whose factors
  reach a genuine `glBlendFuncSeparate` (`nanovg_gl.h`'s own `glnvg__blendCompositeOperation`).
  That is a 1:1 fit for `BlendState`'s own four factors, so `NANOVG` honours `Additive` and every
  custom combination built from representable factors — while ShivaVG declares
  `VG_BLEND_ADDITIVE` but its `updateBlendingStateGL` has no case for it and silently falls back
  to ordinary alpha blending, so `OPENVG` must reject it. `NANOVG` reports
  `GraphicsCapability::AdditiveBlending` true — the most visible capability edge over `OPENVG`.

## Source colour, tint and the blend stage

XNA's `SpriteBatch` pixel shader emits `texel * tint`, component-wise, with **no** alpha
premultiplication of its own; `BlendState` alone decides what happens to that value afterwards.
Everything this renderer does with colour follows from reproducing exactly that, because it is the
only model in which `AlphaBlend` (whose source data is already premultiplied) and
`NonPremultiplied` (whose source data is straight) can stay distinct — `BlendState.cpp` gives them
the same destination factor and different source factors, so collapsing the two is a semantic
error, not a rounding one.

NanoVG's own image-paint fragment shader (`nanovg_gl.h`) is:

    if (texType == 1) color = vec4(color.xyz*color.w, color.w);   // premultiply by own alpha
    if (texType == 2) color = vec4(color.x);
    color *= innerCol;                                            // CPU-premultiplied paint colour
    color *= strokeAlpha * scissor;

`texType` is `1` for an RGBA image created **without** `NVG_IMAGE_PREMULTIPLIED` and `0` with it
(`glnvg__convertPaint`), and `innerCol` is `glnvg__premulColor(paint->innerColor)`, i.e.
`(r*a, g*a, b*a, a)`. Both are therefore CNA's to choose, and both are chosen so the shader's
output equals XNA's:

- **Every `NanoVgTextureRenderer` image is created WITH `NVG_IMAGE_PREMULTIPLIED`** — even though
  the bytes uploaded are always straight RGBA8, exactly like every other CNA renderer's. The flag
  is not a claim about the data; it selects the branch that leaves the sampled texel alone. Leaving
  it alone is what XNA's own shader does, and it is what lets the `SourceAlpha` factor that
  `NonPremultiplied` and `Additive` call for be applied once, by the real blend stage, where
  `BlendState` says it belongs.
- **The tint is pre-divided by its own alpha** before being written into the paint's
  `innerColor`/`outerColor`, so NanoVG's own `glnvg__premulColor` puts it back and the uniform the
  shader multiplies by is the tint itself. A tint alpha of exactly zero cannot be inverted, so the
  round-trip alpha is floored at `1/65536` — far below one 8-bit step, which preserves the tint's
  RGB exactly while contributing at most 0.004 of a step to any alpha derived from it. That case is
  not academic: a zero source alpha does not imply a zero source colour under `Opaque` (`One`,
  `Zero`) or `AlphaBlend` (`One`, `InverseSourceAlpha`).

With the shader emitting `texel * tint`, each built-in `BlendState` then maps straight onto its own
factors, with the colour and alpha channels independent all the way to `glBlendFuncSeparate`:

| `BlendState` | Colour src/dst | Alpha src/dst | Destination alpha |
|---|---|---|---|
| `Opaque` | `One` / `Zero` | `One` / `Zero` | `As` |
| `AlphaBlend` | `One` / `1-As` | `One` / `1-As` | `As + Ad(1-As)` |
| `NonPremultiplied` | `As` / `1-As` | `As` / `1-As` | `As² + Ad(1-As)` |
| `Additive` | `As` / `One` | `As` / `One` | `As² + Ad` |

`NonPremultiplied`'s destination alpha genuinely differs from `AlphaBlend`'s, because its
`AlphaSourceBlend` is `SourceAlpha` rather than `One`. `nanovg_blend_test` asserts all four
channels against a CPU reference computed from the same factor ordinals `ApplyBlendState` receives,
precisely so a renderer that gets RGB right and alpha wrong cannot pass.

**None of NanoVG's own `NVGcompositeOperation` presets are used.** They are a lossy vocabulary:
`NVG_SOURCE_OVER` means `(ONE, ONE_MINUS_SRC_ALPHA)` on both channels, which is `AlphaBlend` and
not `NonPremultiplied`, and `NVG_COPY`/`NVG_LIGHTER` are similarly single points in a space
`BlendState` addresses continuously.

## Sprite quads are rasterized, not vector-filled

The `NVGcontext` is created with `NVG_ANTIALIAS`, which is right for genuine vector work and wrong
for a sprite quad: `nvgFill` then insets the filled polygon by half a pixel and covers the missing
half with a fringe triangle strip whose alpha ramps across roughly one pixel
(`nanovg.c`'s own `nvg__expandFill`). XNA's `SpriteBatch` has no coverage antialiasing at all
unless the backbuffer is multisampled, and this renderer never creates a multisample-capable
context (`GraphicsCapability.MultiSampleAntiAliasing` is `false`).

`NanoVgSpriteBatchRenderer::Draw` therefore calls `nvgShapeAntiAlias(ctx, 0)` **inside** the
`nvgSave`/`nvgRestore` pair that already scopes each sprite, so `nvg__expandFill` emits the path's
own vertices verbatim and GL's ordinary "is the pixel centre inside" coverage rule decides each
pixel — the same rule XNA uses. Antialiasing is untouched for any other NanoVG drawing on the same
context. `nanovg_sprite_rasterization_test` censuses every pixel of the frame after drawing an
opaque sprite over a contrasting background and requires that none of them is a partially-covered
in-between value, for an axis-aligned quad and for ~17°/~23° rotations with fractional
origins, non-integer scale, a partial source rectangle, both `SpriteEffects` flips and a
`SetTransformMatrix`.

## SamplerState is honoured per batch, not per texture

NanoVG's sampler-related image flags (`NVG_IMAGE_NEAREST`, `NVG_IMAGE_REPEATX`/`Y`) are applied
once, inside `glnvg__renderCreateTexture`, and never re-applied per draw — `glnvg__setUniforms`
only binds the texture. XNA's `SamplerState` is the opposite: chosen per `SpriteBatch.Begin()`,
independent of which texture is drawn. The creation-time flags therefore cannot express it.

Each `Draw()` instead writes the batch's filter and address pair straight onto the drawn image's own
GL texture object (`ApplyNanoVgImageSamplerState`, in `NanoVgGl.cpp` — the one translation unit that
can reach `nvglImageHandleGL2`). This is exact (NanoVG's flags reduce to these same GL enums), costs
no duplicated pixel storage, and reaches `GL_MIRRORED_REPEAT`, which NanoVG's flag set has no name
for at all. It is safe because NanoVG only *records* draw calls until `nvgEndFrame` and binds
textures when that flush runs, and because the parameters are written back to a texture binding of
`0` — which is both what NanoVG's `NANOVG_GL_USE_STATE_FILTER` cache holds while a frame is being
recorded and what `glnvg__renderFlush` resets it to before its first draw.

`GL_TEXTURE_MIN_FILTER` and `GL_TEXTURE_MAG_FILTER` are independent, so every `TextureFilter` whose
minification and magnification components differ is representable exactly. The mip component of the
six mip-qualified ordinals selects nothing, because there is never a chain to select from: a
mip-mapped `Texture2D` is refused outright (see below), so every texture that exists here has
exactly one level and the mip component is vacuous rather than approximated.

## Mip levels are refused, not quietly dropped

`nvgCreateImageRGBA` allocates a single level and NanoVG exposes no per-level upload or LOD
sampling API at all, so a mip chain can be neither stored, written to, nor sampled from.

Two entry points reach it and both refuse:

- `NanoVgTextureRenderer`'s constructor rejects `ImageData::mipLevels != 1`, which is what
  `Texture2D(device, w, h, mipMap: true, format)` produces — the same construction-time gate
  `TINYGL` uses for its own single-level textures. Accepting it would leave `Texture2D` reporting
  `LevelCount > 1` for storage that does not exist.
- `UpdatePixelsLevel` rejects any level above zero. `ITextureRenderer`'s own default for that
  method is an **empty body**, so without the override `Texture2D::SetData(level, ...)` would
  succeed while the upload vanished — the exact silent approximation this renderer must not have.
  Level 0 through the same entry point is an ordinary full-surface update.

## SpriteSortMode::Immediate is a real ordering guarantee

NanoVG submits nothing until `nvgEndFrame()` calls the backend's `renderFlush()`, so this renderer
defers its GPU work across `Draw()` calls. That makes `ISpriteBatchRenderer::SetImmediateMode` — a
no-op by default, and correctly so for renderers that are already synchronous per draw —
load-bearing here rather than decorative.

Under `SpriteSortMode::Immediate` each `Draw()` submits its own work before returning, through
`nvgInternalParams(ctx)->renderFlush`. That flushes the recorded call list **without ending the
frame**, so the batch's scissor, transform and blend factors survive it; `nvgEndFrame()` /
`nvgBeginFrame()` would not, because that pair runs `nvgReset()`. Deferred batches are untouched
and still flush once, at `End()`.

The difference is directly observable and is what `nanovg_immediate_mode_test` pins down:

    Begin(mode); Draw(red); Clear(green); End();

is **green** under `Immediate` (the sprite was already on the surface, so the `Clear` wiped it) and
**red** under `Deferred` (the sprite was still queued, so it landed afterwards). Both are asserted,
so neither ignoring the flag nor flushing unconditionally passes.

Flushing is only half of it. The contract is explicitly about device state changed **between** two
`Draw()` calls, and a per-draw flush only submits work that was already recorded against the state
captured at `Begin()`. Each Immediate `Draw()` therefore re-reads the owner's current blend factors
and sprite projection first, re-issuing `nvgGlobalCompositeBlendFuncSeparate` when the factors moved
and re-opening the frame when the viewport extent did (safe at that point: the previous Immediate
draw already flushed, so no recorded call can be lost, and it happens before the `nvgSave` that
scopes the draw). The scissor rectangle needs no equivalent — `Draw()` reads it from the owner on
every call already. `Deferred` deliberately keeps the batch snapshot every other CNA renderer
establishes.

Re-opening the frame is the narrower of the two: only the quantities `nvgBeginFrame` itself
consumes (extent and device-pixel ratio) require it. The scissor mapping does **not** — it is
consumed by this renderer's own CPU-side clipper — but it still has to be refreshed every draw,
because a viewport that MOVES at constant size changes the mapping without changing anything
`nvgBeginFrame` cares about. Refreshing it only alongside a re-open would clip such a sprite
against the previous viewport's rectangle.

Calling `nvgBeginFrame` inside an open batch is safe only because an Immediate draw has already
flushed by then: `glnvg__renderFlush` zeroes the recorded vertex, path, call and uniform counts, so
the reset `nvgBeginFrame` performs discards nothing. That is a dependency on the pinned NanoVG
revision's behaviour rather than on anything its public API documents — upstream describes drawing
as taking place between one `nvgBeginFrame` and one `nvgEndFrame` — and must be re-verified if the
pin moves.

## The sprite coordinate space follows GraphicsDevice.Viewport

XNA/FNA build the `SpriteBatch` projection from the active viewport
(`CreateOrthographicOffCenter(0, Viewport.Width, Viewport.Height, 0, 0, 1)`), so a custom
`GraphicsDevice.Viewport` makes sprite destination rectangles **viewport-local**: sprite `(0,0)` is
the viewport's top-left corner and the rasterizer viewport alone positions the result.
`Viewport.X`/`Y` are never subtracted from sprite coordinates.

`nvgBeginFrame(ctx, w, h, ratio)` is what establishes that space here — NanoVG's vertex shader
normalises positions by exactly that extent — so it is handed the viewport's own size whenever a
custom viewport is active. Handing it the full drawable while `glViewport` already held a
sub-region is the canonical "squish" failure `modules/graphics/examples/spritebatch_custom_viewport_test.cpp`
exists to catch: a 17-pixel-wide sprite in a 41-wide viewport on a 96-wide backbuffer came out
`17 x 41 / 96 = 7` pixels wide.

"Custom" means *differing from `GetDefaultViewportRect()`*, not *differing from the whole drawable*.
That distinction is load-bearing for this renderer specifically: under `Letterbox`/`Overscan` the
DEFAULT viewport is already a physical sub-rectangle of the window, while sprites there are still
addressed in the logical (virtual-resolution) space. Comparing against the full drawable would
treat every letterboxed frame as a custom viewport and project sprites in physical pixels.

`GraphicsDevice.ScissorRectangle` stays in the render target's own logical space regardless, so it
is carried into the sprite space (logical → physical through the presentation mapping, then minus
the viewport origin) before the clipping below runs.

This renderer runs the two shared contract tests directly —
`spritebatch_custom_viewport_test.cpp` and `spritebatch_viewport_switch_test.cpp` — rather than a
renderer-local approximation of them. A local test that draws a full-canvas sprite into a
sub-viewport passes whether the projection is right or wrong, because a wrongly-projected
full-canvas sprite squashes into exactly the sub-viewport; the shared files use a small
viewport-local rectangle precisely so that cannot happen.

## The scissor rectangle is clipped geometrically, not masked

`nvgScissor` is **not** used for sprite draws. NanoVG's scissor is a shader mask — the fragment
shader ends with `color *= scissor` — not a rasterizer clip, so a masked-out fragment still writes;
it writes zero. Under any `BlendState` whose destination factor does not evaluate to one for a zero
source, that blackens the clipped region instead of leaving it alone. `BlendState.Opaque`
(destination factor `Zero`) is the obvious case, and every custom state with a `Zero`,
`SourceAlpha`, `SourceColor` or `Destination*` destination factor behaves the same way. Only
`One`, `InverseSourceAlpha` and `InverseSourceColor` happen to preserve the destination — which is
why the mask looked correct for as long as the tests around it all ran under `AlphaBlend`.

Each `Draw()` therefore clips its own quad instead: the four corners are transformed into logical
space, clipped against the scissor rectangle with Sutherland-Hodgman over four axis-aligned
half-planes (orientation-independent, so a `SpriteEffects` flip needs no special case), and the
surviving convex polygon is filled. `nvgFillPaint` has already multiplied the sprite's own
transform into the paint by that point, so the texture mapping is unaffected by emitting the
outline in logical space. A quad that survives with fewer than three corners is skipped entirely.

Two consequences beyond correctness under `Opaque`: the clip is exact for a rotated sprite as well
as an axis-aligned one, and its edge is hard rather than carrying the shader mask's own one-pixel
feather — matching XNA's rectangular scissor, which is a rasterizer decision and not a colour
operation.

## Presentation model

Ported directly from `OpenVgRenderer::ComputeLogicalViewportEXT()` (`NanoVgRenderer` has its own
copy, `NanoVgRenderer::ComputeLogicalViewportEXT()`), physical-pixel-based throughout, covering all
five `CnaPresentationMode` values (`Letterbox`, `Overscan`, `Stretch`, `NativeBackBuffer`,
`FixedHeightDynamicWidth`). `glViewport` places the current logical canvas onto the current
physical sub-rectangle; each `NanoVgSpriteBatchRenderer::Begin()` calls
`nvgBeginFrame(ctx, logicalWidth, logicalHeight, ratio)` scoped to the SAME logical size (NanoVG's
own `glnvg__renderViewport` builds an internal orthographic mapping from `[0,logicalWidth] x
[0,logicalHeight]` to NDC, independent of `devicePixelRatio` — that parameter only affects
anti-aliasing feather width, not the coordinate mapping, confirmed by reading `nanovg_gl.h`
directly), so the two combine to map sprites correctly under every presentation mode.

`NanoVgRenderer::SetScissorRect` stores its argument verbatim, in the render target's own logical
space — exactly what `GraphicsDevice.ScissorRectangle` means. The remapping into whichever space
sprites are currently addressed in happens per draw instead, because that space depends on the
active `Viewport` and can change between two draws of one Immediate batch.

## Verified capability boundary

| CNA feature | NanoVG route | Notes |
|---|---|---|
| `Clear`, `Present` | Real `glClearColor`/`glClear` + `SDL_GL_SwapWindow` | |
| Presentation modes (`Letterbox`/`Overscan`/`Stretch`/`NativeBackBuffer`/`FixedHeightDynamicWidth`) | Real `glViewport` for the physical sub-rectangle + NanoVG's own internal logical-space ortho | All five modes implemented; pixel-tested for `NativeBackBuffer` (`nanovg_spritebatch_rotation_test`). |
| Custom `Viewport` | Real `glViewport`, **plus a sprite projection sized to the active viewport** | XNA builds the `SpriteBatch` ortho from `Viewport.Width`/`Height`, so a custom viewport makes sprite coordinates viewport-local — see "The sprite coordinate space follows GraphicsDevice.Viewport" below. Verified by the shared `spritebatch_custom_viewport_test` / `spritebatch_viewport_switch_test`, not by a renderer-local substitute. |
| `ScissorRectangle` / `RasterizerState.ScissorTestEnable` | **Geometric clip of each sprite quad**, not `nvgScissor` | Exact for every `BlendState` (including `Opaque`) and for rotated quads, with a hard edge — see "The scissor rectangle is clipped geometrically" above. Enable and rectangle stay independent, matching `OPENVG`. Pixel-tested under both `AlphaBlend` and `Opaque`. |
| `SpriteSortMode.Immediate` | **Real per-draw flush** (`nvgInternalParams(ctx)->renderFlush`) **plus a per-draw device-state re-read** | Covers both halves of the contract: a `GraphicsDevice` operation issued between two `Draw()` calls is ordered between them, AND a `BlendState`/`Viewport` changed between them applies from that sprite onward. `Deferred` keeps the batch snapshot (all of its draws run from `End()` under one device state). Pixel-tested against each other (`nanovg_immediate_mode_test`). |
| Mip-mapped `Texture2D` (`mipMap: true`), and `Texture2D.SetData(level > 0, ...)` | **Rejected** (throws) | NanoVG images are single-level with no per-level upload or LOD-sampling API. Refused at construction and at `UpdatePixelsLevel`, rather than accepted with storage that does not exist. Tested. |
| `Texture2D` create/update | Real `nvgCreateImageRGBA`/`nvgUpdateImage` | Straight (non-premultiplied) RGBA8, top-row-first, tightly packed (NanoVG's own API has no stride parameter — `NanoVgTextureRenderer` repacks when the caller's stride differs). No row flip anywhere: NanoVG's Y-down image space already matches `ImageData`'s own convention. |
| `SpriteBatch` draw (all 3 overloads, rotation, origin, source rectangle, tint, `SpriteEffects` flip) | Real `nvgImagePattern` + a filled rectangle path (`nvgBeginPath`/`nvgRect`/`nvgFillPaint`/`nvgFill`) | NanoVG has no "draw image" primitive; a partial `sourceRectangle` needs no CPU-side sub-image copy (unlike `OPENVG`'s `vgCopyImage` workaround) — the pattern box is positioned purely algebraically, all inside the already-`nvgScale`d coordinate system. Verified by `nanovg_spritebatch_rotation_test`. |
| `SpriteBatch.SetTransformMatrix` | Real `nvgTransform` | Row-major XNA `Matrix` decomposed to a 2D affine, same `(a,b,c,d,e,f)` convention `OpenVgRenderer`/`CanvasRenderer` use. |
| `SamplerState.Filter` — `Linear`, `Point`, and the four `Min*Mag*` combinations | **Real per-batch `GL_TEXTURE_MIN_FILTER`/`GL_TEXTURE_MAG_FILTER`** | Written onto the drawn image's own GL texture object by each `Draw()` — see "SamplerState is honoured per batch" above. Minification and magnification are independent, so a filter whose two components differ is exact. The mip component of `LinearMipPoint`/`PointMipLinear`/`Min*Mip*` is inert (one mip level exists). Pixel-tested (`nanovg_sampler_state_test`). |
| `SamplerState.Filter` — `Anisotropic` | **Rejected** (throws) | No anisotropic sampler is configured; `SupportsCapability(AnisotropicFiltering)` reports `false`. Tested. |
| `SamplerState.AddressU`/`AddressV` — `Clamp`, `Wrap`, `Mirror` | **Real `GL_CLAMP_TO_EDGE`/`GL_REPEAT`/`GL_MIRRORED_REPEAT`**, per batch and per axis | Including on an out-of-bounds `sourceRectangle`, where the three modes genuinely differ. `Mirror` has no NanoVG image flag at all and is reachable only because the wrap mode is written to the GL texture directly. Pixel-tested (`nanovg_sampler_state_test`, `nanovg_texture_orientation_test`). |
| Backbuffer readback | Real `glReadPixels` against the same GL framebuffer NanoVG rendered into | Physical pixel coordinates. |
| `BlendState.Opaque` | Real `(GL_ONE, GL_ZERO)` on both channels | Exact for a translucent source too: the shader emits the un-attenuated `texel * tint`. Pixel-tested for alpha 255, 128 and 0 (`nanovg_blend_test`). |
| `BlendState.NonPremultiplied` | Real `(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` on both channels | Pixel-tested against a CPU reference on all four channels, at four source alphas. |
| `BlendState.AlphaBlend` | Real `(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` on both channels | Genuinely distinct from `NonPremultiplied`: it consumes already-premultiplied source RGB without multiplying it again, and writes a different destination alpha. Pixel-tested, including the "premultiplied texel and its straight twin composite to the same RGB" contract `cross_renderer_2d_corpus.cpp` states across renderers. |
| `BlendState.Additive` | Real `(GL_SRC_ALPHA, GL_ONE)` on both channels | CNA's `BlendState.Additive` is `SourceAlpha`/`One`, not `One`/`One`. Pixel-tested including saturation. |
| Custom `BlendState` built from representable factors | **Honoured exactly** | Every XNA `Blend` except `BlendFactor`/`InverseBlendFactor` maps onto an `NVGblendFactor`, and the colour and alpha factor pairs stay independent to `glBlendFuncSeparate`. Four custom states pixel-tested, including asymmetric colour/alpha pairs. |
| `BlendState` using `Blend.BlendFactor`/`InverseBlendFactor` | **Rejected** (throws) | `NVGblendFactor` has no constant-colour factor, so `GraphicsDevice.BlendFactor` can never reach the blend stage. Tested. |
| `BlendState` using `Blend.SourceAlphaSaturation` as a **destination** factor | **Rejected** (throws) | GL accepts `GL_SRC_ALPHA_SATURATE` as a destination factor only from OpenGL 4.4; this renderer requests 2.1. Accepted as a source factor. Tested. |
| `BlendState.ColorBlendFunction`/`AlphaBlendFunction` other than `Add` | **Rejected** (throws) | NanoVG's GL2 backend never calls `glBlendEquation`, so the equation is permanently `GL_FUNC_ADD`. Tested. |
| `BlendState.ColorWriteChannels` | **Rejected when non-default** (throws) | `nanovg_gl.h`'s own `glnvg__renderFlush` calls `glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE)` at the top of **every** flush, before the first draw call it submits, so an externally-set write mask cannot survive to the draw that would need it — verified empirically (a mask wrapped around a whole `SpriteBatch` batch was silently undone). Rejecting is the honest choice; silently ignoring it would be a capability lie. Tested. |
| `BlendState.MultiSampleMask` | **Rejected when non-default** (throws) | This renderer never creates a multisample-capable GL context (`GraphicsCapability.MultiSampleAntiAliasing` is `false`), so no sample-coverage mask can be applied. Having no observable effect is an argument for silence, not for acceptance — it is refused like every other state this renderer cannot honour. Tested. |
| `RasterizerState.CullMode` | Accepted for every value | 2D quads are never back-face culled by any CNA renderer regardless of value. |
| `RasterizerState.FillMode.WireFrame` | **Rejected** (throws) | No unfilled-polygon draw path exists. `SupportsCapability(WireFrame)` reports `false`. |
| `RasterizerState.DepthBias`/`SlopeScaleDepthBias` | **Rejected when non-zero** (throws) | No depth buffer exists for either to bias against. |
| `DepthStencilState` | **`DepthStencilState.None` is accepted; any meaningfully-enabled depth or stencil state, or a non-zero `ReferenceStencil`, is rejected** (throws) | Matches ordinary `SpriteBatch.Begin()` usage and normal `GraphicsDevice` construction. `SupportsDepthStencil()` reports `false`. |
| Modern 3D `Draw*`/`DrawIndexed*`/`DrawInstanced*`/`DrawUser*` entry points | **Rejected by `Ensure3DSupported()`** (Throw policy) or safely no-op'd (WarnAndStub policy) | Verified through the public `GraphicsDevice` API (`nanovg_unsupported_3d_behavior_test`). |
| Render targets (`RenderTarget2D`/`RenderTargetCube`) | **Unsupported** (`CreateRenderTarget2D` returns `nullptr`, the shared default) | NanoVG's own off-screen-framebuffer helper (`nanovg_gl_utils.h`'s `NVGLUframebuffer`) is deliberately out of this renderer's scope — see `plan_nanovg.md`'s "Known limitations". `GraphicsDevice::SetRenderTarget(RenderTarget2D*)` throws `System::NotSupportedException` transactionally, matching every renderer with no real render-target storage. |
| Custom `Effect` in `SpriteBatch` | **Rejected** (throws) | No caller-addressable programmable shader stage exists on this renderer (NanoVG's own GLSL pipeline is fixed, internal, and not exposed to CNA's `Effect` system). |
| `GraphicsCapability.ThreeD`, `DepthStencilBuffer`, `MultipleRenderTargets`, `OcclusionQuery`, `CustomEffects`, `CompiledEffects`, `Texture3D`, `MultiSampleAntiAliasing`, `AnisotropicFiltering`, `WireFrame`, `Instancing`, `MultiStreamVertexInput`, `StencilBuffer` | All report `false` | See `NanoVgRenderer::SupportsCapability`'s own comment. |
| `GraphicsCapability.AdditiveBlending` | Reports **`true`** | The one genuine capability edge over `OPENVG` — see above. |
| Swap interval | Real `SDL_GL_SetSwapInterval`, with a fallback chain (requested -> 1 -> 0) | |

## Dependency and build

`cmake/ThirdPartyNanoVG.cmake` fetches NanoVG's source at the pinned commit via `FetchContent` and
compiles its backend-agnostic core (`nanovg.c`, which itself pulls in the vendored `fontstash.h`/
`stb_image.h` with their own `..._IMPLEMENTATION` macros) into a small static library,
`cna_thirdparty_nanovg`. The GL2 render backend (`nanovg_gl.h`) is deliberately **not** compiled
there: it calls `gl*` entry points unqualified rather than loading them itself (unlike a real
loader such as GLAD), so `modules/renderers/nanovg/src/NanoVgGl.cpp` declares file-scope
function-pointer shims for the ~28 post-GL-1.1 entry points NanoVG's GL2 backend calls, resolves
them through the platform's GL loader (`LoadPlatformGlProcAddress`, the same one
`OpenGL2Renderer.cpp` uses for its own shader functions), and only then `#include`s `nanovg_gl.h`
with `NANOVG_GL2_IMPLEMENTATION` — see that file's own header comment for exactly why
`GL_GLEXT_PROTOTYPES` is never defined there. `nanovg-spike/README.md` documents the standalone
proof this mechanism works before it was integrated into CNA.

## Supported platforms

Desktop Linux, Windows, and macOS only (`cmake/RendererSelection.cmake`'s own platform gate) —
NanoVG's GL2 backend needs a real desktop OpenGL 2.x+ context (GLSL 1.10 shaders), which neither
WebGL (Emscripten) nor a GLES-only mobile target can create. Native validation actually performed
in this environment is **Linux + Xvfb (X11 GLX) only**; Windows (MSVC)/macOS were NOT compiled or
run here.

## Test status

Real windowed/pixel behavior is covered by `modules/renderers/nanovg/examples/` (this project's
established split for GPU/window-creating tests — pure-function pieces live in
`modules/renderers/nanovg/tests/` instead):

| Test | Coverage |
|---|---|
| `nanovg_smoke_test` | Vertical slice: Clear, SpriteBatch draw, readback. |
| `nanovg_spritebatch_rotation_test` | Decisive rotation/origin geometry oracle (`NativeBackBuffer`). |
| `nanovg_blend_test` | Every built-in `BlendState` and four custom ones, all four RGBA channels, against a CPU reference computed from the same factor ordinals `ApplyBlendState` receives; genuinely premultiplied source data for `AlphaBlend` and its straight twin for `NonPremultiplied`; translucent and alpha-zero sources under `Opaque`; six tint combinations; deterministic rejection of non-`Add` blend functions, constant-colour factors, `SourceAlphaSaturation` as a destination factor, a non-default `ColorWriteChannels` and a non-default `MultiSampleMask` (39 checks). |
| `nanovg_unsupported_3d_behavior_test` | Throw/WarnAndStub policy across every inherently-3D entry point, `AdditiveBlending`/`Texture3D` capability honesty. |
| `nanovg_texture_orientation_test` | Upload/`UpdatePixels` row orientation, partial-`sourceRectangle` `nvgImagePattern` box crop math (including a multi-texel span), tint, rotation, both `SpriteEffects` flips, out-of-bounds `Clamp` pixel-exactness (right edge and left/top simultaneously), real out-of-bounds `Wrap` tiling / `Mirror` reflection, and refusal of both a mip-mapped `Texture2D` and a level>0 upload (29 checks). |
| `nanovg_sampler_state_test` | `PointClamp` vs `LinearClamp` at sample points where the two genuinely disagree, the four `Min*Mag*` filters, the inert mip component, the same texture drawn Point → Linear → Point across consecutive batches, two textures in one batch, `Clamp`/`Wrap`/`Mirror` on an out-of-bounds source rectangle, independent U/V address modes, and rejection of `Anisotropic` and of out-of-range ordinals (22 checks). |
| `nanovg_immediate_mode_test` | `SpriteSortMode::Immediate` vs `Deferred` as an ordering guarantee: a `Clear()` between the `Draw()` and the `End()` must wipe the sprite under Immediate and be overdrawn by it under Deferred; ordering between two Immediate draws; a `BlendState` and a `Viewport` changed between two Immediate draws applying from that sprite onward; a viewport MOVED at constant size re-mapping the scissor (the case a "did the extent change" check misses); that the per-draw flush leaves the batch's own scissor/transform/blend state intact; and that the flag is per batch rather than sticky (17 checks). |
| `spritebatch_custom_viewport_test` (shared) | REMED-GFX-072's own contract: sprite clip space built from the active `GraphicsDevice.Viewport`, viewport-local placement, no squish, a transform composed in viewport-local space, and a full-target batch staying full-target afterwards (13 checks). |
| `spritebatch_viewport_switch_test` (shared) | Two `SpriteBatch` batches with different viewports in one frame, each projected and rasterized by its own (6 checks). |
| `nanovg_sprite_rasterization_test` | A whole-frame census requiring that no pixel is partially covered — for an axis-aligned integer quad (with its exact edge columns/rows and covered-pixel count), a ~23° rotation with a fractional origin, non-integer scale and a partial source rectangle, and two ~17° rotations with both `SpriteEffects` flips under a `SetTransformMatrix`; plus a translucent sprite whose edge column must composite exactly once (11 checks). |
| `nanovg_presentation_viewport_scissor_test` | Every `CnaPresentationMode` (`Letterbox`/`Overscan`/`Stretch`/`FixedHeightDynamicWidth`/`NativeBackBuffer`), `TransformWindowToLogical`/`TransformLogicalToWindow` round-trips, a custom `Viewport`, resize-without-`Clear`, `RasterizerState`-driven scissor pixel-clipping under both `AlphaBlend` and `Opaque` (the latter over a background far from black, so a masked-but-still-written fragment cannot hide in the tolerance), `Stretch` presentation combined with a custom `Viewport` AND a scissor in one scene (the only configuration where the scissor's X and Y scale factors differ, so applying one to both is visible), two simultaneous `NanoVgRenderer` instances (construction, interleaved `Clear`/readback, and destroying one while the other stays live), 25 repeated construct/destroy cycles, `SetSwapInterval` (49 checks). |

Plus `NanoVgBlendStateMapping.*` (`modules/renderers/nanovg/tests/`, the pure
`BlendStateToNvgBlendFunc` mapping function, no window/GL context needed) — including the case
that keeps `AlphaBlend` and `NonPremultiplied` from collapsing onto the same factors again.

**Cross-renderer comparison.** `modules/graphics/examples/cross_renderer_2d_corpus.cpp` is
deliberately NOT registered for `NANOVG`: its row 4 round-trips through a `RenderTarget2D`, which
this renderer has no storage for, so the file would abort rather than dump a comparable frame (it
is registered for `EASYGL` and `DIRECT2D`, both of which do). What the corpus exists to pin down —
that a premultiplied texel under `AlphaBlend` and its straight twin under `NonPremultiplied` must
composite to the same colour — is instead asserted against `NANOVG` with the corpus's own texels,
tint and background inside `nanovg_blend_test`. The rest of that test compares against a CPU
reference derived from `BlendState`'s own factor ordinals rather than against another renderer's
output, which is a stronger oracle than a differential: it cannot agree with a second
implementation that is wrong in the same way.

**Exact commands run** (this environment): `Xvfb :64 -screen 0 1280x1024x24 -nolisten tcp &`, then
`DISPLAY=:64 ctest --test-dir cmake-build-nanovg -R NanoVg --output-on-failure` (21/21 pass) and
`DISPLAY=:64 ctest --test-dir cmake-build-nanovg -j4` (the complete `CnaTests` corpus).

**Audit note.** The two multi-texel/presentation test files above were added in a second,
deliberately adversarial pass after the renderer's initial delivery, closing a real rigor gap
against `OPENVG`'s own established test precedent (partial-`sourceRectangle` crop math, `Clamp`
pixel-exactness, `SpriteEffects` flips, `UpdatePixels`, non-`NativeBackBuffer` presentation modes,
a custom `Viewport`, resize-without-`Clear`, scissor, and multi-instance coexistence were all
previously untested claims). That pass found and fixed one real renderer bug — see "Known
limitations" below for the internal-texel-seam-bleed finding, which is a documented characteristic
rather than a bug, and the "multi-instance coexistence was broken by a missing `MakeCurrent`"
entry, which was a genuine defect, now fixed.

## Known cross-renderer test gaps

Running the full `CnaTests` corpus against `-DCNA_GRAPHICS_RENDERER=NANOVG` surfaces the same two
categories of pre-existing shared-test gap `docs/openvg-renderer.md` already documents for
`OPENVG` — neither is a NANOVG-specific defect, both are generic gaps in shared test files that
assumed every renderer provides 3D/render-target/cube-texture storage:

* **`RenderTarget2D`/`TextureCube` genuinely unsupported.** Fixed the same way `OPENVG` was: added
  `NanoVg` to the small number of shared-test allowlists that already gate on a renderer's real
  "no storage" behavior (`GraphicsDeviceValidationTests.cpp`'s `SetRenderTargets_*`/
  `SetRenderTarget_SingleOverload_MatchesArrayOverloadRejection`, and every `CubeStorageSupported()`
  helper across `TextureCubeTests.cpp`, `Texture3DTextureCubeContentTypeReaderTests.cpp`,
  `XnbBuiltInReaderRegistrationTests.cpp` and `CnjCapabilityMatrixTests.cpp`). Also added explicit
  `NanoVg` arms to `GraphicsDeviceCapabilityTests.cpp` (`ExpectedCapabilities()`, `IsTwoDimensionalOnly()`,
  the `WireFrame` expectation chain), `GraphicsBackendCategoryTests.cpp`/`GraphicsBackendMaturityTests.cpp`
  (`TranslationLayer`/`Experimental`), `GraphicsRendererTypeTests.cpp` (`ExpectedNameFor()`) and
  `GraphicsRendererCompileDefinitionTests.cpp` — the same per-renderer-arm registration discipline
  every renderer addition in this repository follows.

* **Pre-existing "content pipeline assumes a 3D pipeline" gap, not introduced by NANOVG.** The same
  ~100 tests across `Model`/`Cnj`/`Gltf`/`SkinnedModel`-loading suites `docs/openvg-renderer.md`
  already names (`GltfCameras`, `GltfConformanceL6`, `GltfModelShape`, `GltfSceneGraphBones`,
  `GltfSkinSpaces`, `GltfRigidAnimation`, `GltfStrideAndBuffer`, `CnaGltfConformanceL*`, ...)
  construct a real `VertexBuffer` (directly, or transitively through `Model` loading) with no
  `GraphicsCapability::ThreeD` guard. `NanoVgRenderer::CreateVertexBuffer` honestly throws
  (`HandleUnsupported3DCall`) under the default `Throw` policy — the exact same call every other
  2D-only renderer already in this registry makes for the identical entry point. Left deliberately
  unmodified for the same reason `OPENVG`'s own audit left it unmodified: a genuine, useful
  follow-up, but a general content-pipeline/2D-renderer-family gap outside this renderer's own
  minimal-shared-change scope.

* **Unrelated, non-graphics flakiness/environment gaps observed in the same full-corpus run.**
  `ModuleLinkClosure_*` (expects a Makefiles-generator `link.txt`, fails under this environment's
  Ninja generator), `CApiCoverageMatrix`/`CApiHeaderCompatibility` (need Doxygen 1.9.8+ and a
  `-std=c23`-capable GCC, neither installed here), `VibrateControllerTests` (SEGFAULT, unrelated to
  graphics), and a couple of `ENet*` networking tests — all independent of which graphics renderer
  is selected, confirmed by the identical failure shape `docs/openvg-renderer.md` already records.

## Known limitations

- **No render targets.** NanoVG's own off-screen-framebuffer helper (`nanovg_gl_utils.h`'s
  `NVGLUframebuffer`) was deliberately left out of this renderer's initial scope (`plan_nanovg.md`)
  — a real follow-up, not attempted here.
- **No custom `Effect`/shader stage.** NanoVG's GLSL pipeline is fixed and internal; there is no
  mechanism to inject a caller-supplied shader into it.
- **`TextureFilter.Anisotropic` is rejected.** No anisotropic sampler is configured on this
  renderer, which is why `SupportsCapability(AnisotropicFiltering)` reports `false`. Every other
  `TextureFilter`, and all three `TextureAddressMode` values, are honoured exactly per batch —
  see "SamplerState is honoured per batch, not per texture" above.
- **Mip chains are unsupported and refused.** `nvgCreateImageRGBA` allocates exactly one level and
  NanoVG has no per-level upload or LOD-sampling API, so a mip-mapped `Texture2D` is rejected at
  construction and `SetData(level > 0, ...)` throws. The mip component of a mip-qualified
  `TextureFilter` is consequently vacuous; its minification and magnification components are still
  applied exactly.
- **`BlendState` factors that name a constant colour are rejected.** `NVGblendFactor` has no
  `GL_CONSTANT_COLOR` counterpart, so `Blend.BlendFactor`/`InverseBlendFactor` — and therefore
  `GraphicsDevice.BlendFactor` — can never reach the blend stage.
- **Only `BlendFunction.Add` is supported.** NanoVG's GL2 backend never calls
  `glBlendEquation`/`glBlendEquationSeparate`, so the equation is permanently `GL_FUNC_ADD` and
  `Subtract`/`ReverseSubtract`/`Min`/`Max` are rejected rather than approximated.
- **`Blend.SourceAlphaSaturation` is a source factor only.** GL accepts `GL_SRC_ALPHA_SATURATE` as
  a destination factor only from OpenGL 4.4 onwards, and this renderer requests a 2.1 context.
- **`BlendState.ColorWriteChannels` cannot be honored at all and is rejected when non-default.**
  `nanovg_gl.h`'s own `glnvg__renderFlush` unconditionally resets `glColorMask` to
  all-channels-enabled before the first draw of every flush — verified empirically, not a CNA
  design choice.
- **A partial `sourceRectangle`'s internal seam with its own neighboring texel bleeds under linear
  filtering, with no flat margin.** There is no CPU-side sub-image copy (see "How it differs from
  OPENVG" above) — cropping is purely a `nvgImagePattern` box-position trick over the SAME
  uncropped image, so a sample point near the edge of the crop is still, physically, right next to
  real neighboring texel data in the same texture. `GL_CLAMP_TO_EDGE` only produces a flat,
  blend-free margin at the texture's OUTER bound (any point past a texel's own center is clamped to
  a constant); an INTERNAL seam between two texels that are both inside the crop has no such
  margin — the sampled colour blends linearly with distance from each texel's own center, all the
  way up to that center. Only exactly at a texel's center is the read guaranteed pure. This is
  irrelevant at normal sprite-sheet scale (texels many pixels wide, so any reasonable interior
  sample point is negligibly close to its own center) and was found by
  `nanovg_texture_orientation_test`'s multi-texel-span case, which now samples texel centers
  exactly rather than "just inside" the texel, matching this renderer's real, no-flat-margin
  seam behavior. It applies only to a linear-filtered crop: `SamplerState.PointClamp` has no seam
  at all, and is now genuinely honoured.
- **Multiple simultaneous `NanoVgRenderer` instances required `MakeContextCurrentEXT()` at every
  GL-touching entry point — found and fixed by this audit.** OpenGL context state is current to
  the calling THREAD, not to the C++ renderer object: with two live instances, whichever one's
  context was made current most recently silently received every subsequent GL call from EITHER
  instance, including `Clear`/`ReadBackbuffer`/`ApplyBlendState`/`SpriteBatch` draws. This made the
  class doc's original "no single-live-context restriction" claim false in practice (proven by
  `nanovg_presentation_viewport_scissor_test`'s `TestMultiInstanceCoexistence`). Fixed by having
  every entry point that issues GL/NanoVG calls — on `NanoVgRenderer` itself, and transitively
  through `NanoVgSpriteBatchRenderer`/`NanoVgTextureRenderer`, both of which hold a reference back
  to their owning `NanoVgRenderer` — call `NanoVgRenderer::MakeContextCurrentEXT()` first. The
  claim is now genuinely true, not just documented.

### Corrected by the NVG-18 audit

The four entries below described real behaviour of an earlier implementation and are recorded here
only so a reader of an older revision is not misled. None of them is true any more; each is now a
pixel-tested guarantee instead.

- *"`BlendState.Opaque` on a translucent source shows alpha-attenuated colour, and there is no way
  to avoid this within NanoVG's public API."* There was: `NVG_IMAGE_PREMULTIPLIED` selects the
  fragment-shader branch that does not premultiply, and the claim that moving the multiply to the
  CPU "produces the identical final colour by associativity" was simply wrong for `Opaque`, whose
  `(One, Zero)` factors have no `1/a` anywhere to cancel it.
- *"`AlphaBlend` produces the same visible pixel as `NonPremultiplied`, and this is exact, not
  approximate."* It was neither: `AlphaBlend`'s source data is already premultiplied, so the shader
  applied its alpha a second time, and the two states' destination alpha differed from the contract
  in opposite directions.
- *"No generic blend-factor/equation model exists, so an arbitrary custom `BlendState` is
  rejected."* `nvgGlobalCompositeBlendFuncSeparate` is exactly that model.
- *"`SetSamplerFilter` is a documented no-op."* Documented, but not defensible while
  `SamplerState.PointClamp` was accepted without complaint.

A third pass (NVG-20) found two more:

- *"Custom `Viewport`: real `glViewport`, raw pass-through."* Pass-through of the rasterizer
  viewport was never the whole job: the sprite projection has to be sized to it as well, or every
  sprite is squashed into the sub-region. The renderer-local viewport test in place at the time
  drew a full-canvas sprite, which squashes into exactly the sub-viewport and so passed either way.
- *"A `GraphicsDevice` operation issued between two Immediate `Draw()` calls."* True for ordering
  after NVG-19, but not yet for the device STATE that operation changes — the blend factors and the
  sprite projection were still the ones captured at `Begin()`.

A fourth pass (NVG-21) closed the last one of that family, plus a consistency gap:

- The NVG-20 re-read refreshed its cached projection only when it re-opened the frame, so a
  viewport MOVED at constant size updated neither — leaving the previous viewport's origin in the
  scissor mapping and clipping the next sprite against the wrong rectangle.
- *"`BlendState.MultiSampleMask`: intentionally ignored (not honored, not rejected)."* The last
  state this renderer neither implemented nor refused. Now refused.

A follow-up review pass (NVG-19) found three more, all of the same shape — an accepted API whose
behaviour quietly differed from its contract:

- *"`SetImmediateMode` is still a no-op"* (the class's own header comment). It could not be: this
  renderer defers every `Draw()` to `nvgEndFrame`, which is exactly the case
  `ISpriteBatchRenderer::SetImmediateMode` exists for.
- Mip-mapped `Texture2D` was accepted with no chain behind it, and `UpdatePixelsLevel` inherited a
  base-class default with an empty body, so a level>0 upload was discarded without a word.
- `nvgScissor`'s shader mask was treated as a clip. It is not one, and the difference is visible
  under any `BlendState` whose destination factor does not preserve the destination at zero source.
