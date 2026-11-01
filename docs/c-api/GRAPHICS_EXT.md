# C graphics extensions

`CNA/C/graphics_ext.h` exposes standalone retro effects, DebugDraw and the core image-based-light
descriptor. These families have different build and lifetime rules:

| Family | Build requirement | C representation and release |
|---|---|---|
| CRT and color-depth reduction | `CNA_CNAEXT=ON` | Owned `CNA_EffectHandle`; release with `cna_effect_destroy`. |
| ASCII post-process | `CNA_CNAEXT=ON` | Owned `CNA_AsciiPostProcessEffectHandle`; release with `cna_ascii_post_process_effect_destroy`. |
| DebugDraw | `CNA_CNAEXT=ON` | Owned `CNA_DebugDrawHandle`; release with `cna_debug_draw_destroy`. |
| `ImageBasedLightEXT` | Always built | Caller-owned versioned `CNA_ImageBasedLightEXT` value containing borrowed texture handles. |

## Availability and ownership

`cna_graphics_ext_is_available` reports whether this binary contains the standalone extensions.
It is a build query, not a renderer capability query. All declarations remain exported when
`CNA_CNAEXT` is off; the CRT, depth, ASCII and DebugDraw routes then return
`CNA_RESULT_NOT_SUPPORTED`. Disabled creation routes clear a supplied handle output even when
ordinary argument checks would have failed. Core IBL value routes remain available.

Creation accepts an owned device or a borrowed device within its documented validity window.
An extension created on a game's device is a graphics child: it survives its creation callback and
must be released before `cna_game_destroy`. Standalone-device extensions belong to that device
instead; release them before destroying it. A second release of an already-released handle returns
`CNA_RESULT_INVALID_HANDLE` in an enabled build. Shared ownership and thread rules are in
[`OWNERSHIP.md`](OWNERSHIP.md) and [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

## CRT and color-depth effects

`cna_crt_effect_create` and `cna_depth_effect_create` construct ordinary `ShaderEffect` descendants.
The common effect routes provide clone, apply, disposal, reflection and type-name operations;
see [`EFFECTS.md`](EFFECTS.md). Family-specific routes reject an effect of the wrong runtime type
with `CNA_RESULT_INVALID_HANDLE`.

Use `cna_sprite_batch_begin_with_effect` to draw a source texture through either effect. A CRT
pass should use one composited full-screen source: curvature and vignette use that quad's texture
coordinates, so applying the effect separately to individual sprites distorts each sprite
separately. Depth reduction can also be used per sprite. Chaining passes uses ordinary render
targets and SpriteBatch draws.

The built-in shader packages carry desktop GLSL, GLSL ES, SPIR-V and WGSL variants. Successful
construction only proves that the object exists. Use `cna_shader_effect_is_valid`, compile-error
inspection and the device's source-execution capability as described in `EFFECTS.md`; even a valid
program answer on a renderer that ignores source is not pixel evidence.

CRT's scanline intensity, curvature, vignette intensity and mask intensity default to 0.3, 0.08,
0.25 and 0.35. Their C setters reject non-finite values with `CNA_RESULT_INVALID_ARGUMENT` and
clamp finite values to [0, 1]. The mask defaults to aperture grille; none and shadow mask are the
other defined choices. Undefined mask identities are argument errors.

Depth defaults to RGB565 (`COLOR_16_BIT`) without dithering. It also exposes RGB332, 4/2/1-bit
grayscale, `PALETTE_256` and `PALETTE_16`. Despite its retained name, `PALETTE_256` uses the fixed
216-color web-safe palette; `PALETTE_16` uses the classic 16-color EGA/CGA palette. Dithering is
none, Bayer 4×4 or Bayer 8×8. Undefined mode or dither identities are argument errors.

## ASCII post-process

ASCII has a dedicated handle and direct draw route. It reads the source back to the CPU,
quantizes RGB into a glyph grid and draws the grid through SpriteBatch into the current render
target. It is not accepted by the generic `cna_effect_*` routes. Creation does not prove that the
renderer can read and draw the eventual source; unsupported draw/readback paths may return
`CNA_RESULT_NOT_SUPPORTED`.

The source must resolve to a Texture2D, including RenderTarget2D; another texture kind returns
`CNA_RESULT_INVALID_HANDLE`. A null destination selects the native viewport-size overload, while
a rectangle selects explicit destination bounds. The default cell size is 8×8 source pixels;
non-positive dimensions are refused. Quantization mode is `BLACK_WHITE` or `COLOR`, and an
undefined identity returns `CNA_RESULT_INVALID_ARGUMENT`. Construction reads `CNA_ASCII_MODE`
(`BLACKWHITE` or `COLOR`, case-insensitive; unset or unrecognized selects Color), so callers that
need deterministic mode selection should set it explicitly.

Source alpha is ignored. RGB selects each glyph and its colors, and every destination cell gets
an opaque background and glyph: black with white glyphs in BlackWhite mode, quarter-brightness
background with the averaged foreground color in Color mode. The destination is painted opaque.
The last-grid-dimensions route reports zero columns and rows before the first draw, then the most
recently quantized grid. Full native usage and pixel contracts are in
[`ascii-post-process-effect.md`](../ascii-post-process-effect.md).

## DebugDraw

DebugDraw records colored line vertices in a depth-tested list and an overlay list. Begin copies
view/projection matrices, clears both lists and resets the destination list to depth-tested. Set
`cna_debug_draw_set_depth_tested` after begin to direct subsequent shapes to the overlay; existing
vertices stay in their original list. Clear discards both lists without changing that selection
or the open-frame state.

The C routes add lines, twelve-edge boxes/frusta, three-ring spheres and three-axis crosses.
Sphere segment counts are clamped to `CNA_DEBUG_DRAW_MIN_SEGMENTS` (4) through
`CNA_DEBUG_DRAW_MAX_SEGMENTS` (128). Pointer-taking routes refuse null inputs and Boolean inputs
must be canonical. Invalid handles and native rendering/allocation failures remain normal C API
errors; DebugDraw is not a no-failure boundary.

End without an open frame succeeds without drawing. An open end draws depth-tested lines first
and overlay lines second, then clears both lists. On success it restores the prior depth/stencil
state; it does not save every device binding or effect state. Applications must establish the
state needed by subsequent rendering.

`cna_debug_draw_get_line_count` counts lines across both lists. The copy route selects one list
and counts **vertices**, two per line. Pass a null destination with zero capacity to query size;
a nonempty list returns `CNA_RESULT_BUFFER_TOO_SMALL` with the required count. A short array is
not partially written. The output contains C POD vertices converted from native values.

## Core image-based-light descriptor

`cna_image_based_light_ext_init` initializes a version-1 descriptor with three invalid texture
handles, one prefiltered mip and intensity 1. No device or optional extension module is needed.
The descriptor records handles without retaining or releasing textures.

`cna_image_based_light_ext_is_valid` requires a non-null descriptor, at least its current
structure size, a nonzero version and a non-null Boolean output. Completeness means:

- irradiance and prefiltered-specular fields name registered TextureCube or RenderTargetCube
  handles;
- the BRDF lookup names a registered Texture2D or RenderTarget2D handle;
- the prefiltered mip count is at least one and `intensity >= 0`.

Missing, stale and wrong-kind handles make the result false with `CNA_RESULT_SUCCESS`. A malformed
descriptor is `CNA_RESULT_INVALID_ARGUMENT`. This predicate checks handle kinds and those scalar
conditions; it does not validate texture contents, actual mip-chain length, common device ownership
or intensity finiteness. It neither generates environment maps nor binds the light to an effect.
Renderer IBL support is queried separately with
`cna_graphics_device_supports_image_based_lighting_ext` in
[`GRAPHICS_DEVICE.md`](GRAPHICS_DEVICE.md).

## Verification and scope

`CApi_GraphicsExtSmoke` exercises availability, fixed identities, CRT defaults/clamping, depth
modes, ASCII controls/draw acceptance, DebugDraw geometry and the IBL texture-kind predicate.
Its unavailable branch checks the feature-disabled refusals and cleared creation handles. The
test accepts documented renderer refusals where appropriate, so a passing C test does not by
itself prove the retro shader or ASCII pixels. Native `CRTEffectTests.cpp`, `DepthEffectTests.cpp`,
`DebugDrawTests.cpp` and the ASCII pixel/3D-source probes provide that evidence under their own
renderer and readback requirements. Generated route/Boolean tests and ABI checks supplement these
family tests.

The current standalone module boundary and removal of the former engine layer are documented in
[`cnaext-engine-layer.md`](../cnaext-engine-layer.md). Core PBR and renderer facilities do not
depend on that retired layer.
