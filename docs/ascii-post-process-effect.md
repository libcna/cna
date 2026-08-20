# AsciiPostProcessEffect — renderer-neutral ASCII post-process

`CNA::Graphics::AsciiPostProcessEffect` (`modules/graphics-ext/`) is CNA's renderer-neutral
ASCII/glyph-grid image post-process effect. It replaces the former public `ASCII` graphics-renderer
identity (`CNA_GRAPHICS_RENDERER=ASCII`, removed 2026-08 — see `docs/ascii-renderer.md` for the
historical completeness record of that renderer).

## Why ASCII became an effect, not a renderer

The former `ASCII` renderer was never a genuine terminal/TTY renderer. It was architecturally a
thin decorator around `SDL_RENDERER`'s own `SdlRenderer`: the game drew normally into a private
offscreen target, and `Present()` read that frame back, quantized it into a glyph/color grid, and
drew the grid onto the real window instead of the game's actual pixels.

That is a post-processing operation on an already-rendered image, not a distinct graphics API
implementation — `ASCII` never had its own texture/vertex/present pipeline; every actual draw
call was forwarded to the wrapped `SdlRenderer`. Keeping it as a public renderer identity meant:

- an application had to opt into `CNA_GRAPHICS_RENDERER=ASCII` at compile time to get the glyph-grid
  look, which silently also opted it into that renderer's 2D-only limitation (every 3D draw call
  threw `ThrowNo3D`, inherited unmodified from `SDL_RENDERER`'s own 2D-only reality);
- the glyph-grid look could never be applied to a 3D scene, or composed with any other renderer, or
  applied to only part of a frame.

As a renderer-neutral post-process effect instead, the identical quantization/glyph-atlas logic
applies to the completed output of **any** renderer and **any** scene — 2D or 3D — because the
effect only ever reads finished pixels back through the public `Texture2D::GetData()`/`SpriteBatch`
APIs, never a renderer-internal type.

## Usage

```cpp
using CNA::Graphics::AsciiPostProcessEffect;
using CNA::Graphics::AsciiQuantizeMode;

AsciiPostProcessEffect asciiEffect(device);
asciiEffect.setQuantizeMode(AsciiQuantizeMode::Color); // or BlackWhite
asciiEffect.setCellSize(8, 8);                          // source pixels averaged per glyph cell

// ... draw a normal 2D or 3D scene into sceneTarget (a RenderTarget2D) ...

device.SetRenderTarget(nullptr); // back to the real backbuffer
asciiEffect.Draw(sceneTarget);   // stretched to fill the current viewport
// or: asciiEffect.Draw(sceneTarget, Rectangle(x, y, w, h)); // an explicit destination rectangle
```

No special `CNA_GRAPHICS_RENDERER` selection is needed — this is ordinary CNAEXT application code
(`#ifdef CNA_CNAEXT` gated, same as `CRTEffect`/`DepthEffect`), usable with whichever renderer the
application already selected.

### Public controls

Preserves the former renderer's own controls, now as effect properties instead of an
environment-variable-only renderer setting:

- `setCellSize(int width, int height)` / `getCellSize(int&, int&)` — source pixels averaged per
  glyph cell (default 8×8, throws `std::invalid_argument` for a non-positive size).
- `setQuantizeMode(AsciiQuantizeMode)` / `getQuantizeMode()` — `BlackWhite` (fixed white
  foreground, no background fill) or `Color` (default; foreground = the cell's averaged color,
  background = that color at quarter brightness). Defaults from the `CNA_ASCII_MODE` environment
  variable at construction time, same parsing (`BLACKWHITE`/`COLOR`, case-insensitive, unset/
  unrecognized → `Color`) the former renderer used.
- `Draw(Texture2D& source)` / `Draw(Texture2D& source, const Rectangle& destinationRectangle)`.
- `GetLastGridDimensions(int& columns, int& rows)` — the glyph grid's column/row count as of the
  most recent `Draw()` call (testing/diagnostics).

Not carried forward: the former renderer's own `gameTarget_`/`SetRenderTarget(nullptr)` redirect
and 3D-call `ThrowNo3D` policy. Those were properties of being a whole renderer identity, not of
the glyph-grid quantization itself — an application using the effect now manages its own
`RenderTarget2D`s directly, with full XNA `SetRenderTarget` semantics, and 3D rendering is fully
available (see below).

## Renderer neutrality

The effect does not depend on `SDL_RENDERER`, or any other specific renderer, as its
architectural owner. It operates entirely through ordinary CNA graphics abstractions:
`Texture2D`/`RenderTarget2D` (source), `GraphicsDevice` (owns the font-atlas texture and the
currently-bound render target the effect draws into), and `SpriteBatch` (draws the glyph quads).
It never downcasts to a concrete renderer type and never touches a renderer-internal API.

## 2D and 3D source-image support

Because the effect's input is a completed `Texture2D`/`RenderTarget2D`, it does not matter whether
that image came from 2D `SpriteBatch` drawing or a full 3D scene (`BasicEffect`/`SkinnedEffect`/
custom shaders, depth-tested geometry, etc.) — the effect never sees how the source was produced.
This makes the following a valid, supported pipeline, which the former `ASCII` renderer identity
could never express (it was itself 2D-only and could not run any 3D-capable renderer's draw calls
at all):

```text
VULKAN (or any 3D-capable renderer) 3D scene
    -> RenderTarget2D
    -> AsciiPostProcessEffect
    -> backbuffer
```

This is verified by a permanent regression, not just architectural inference:
`AsciiPostProcessEffect_3DSource` (`modules/graphics-ext/examples/ascii_posteffect_3d_source_test.cpp`)
renders two depth-tested, overlapping triangles into a `RenderTarget2D` via `BasicEffect` + a real
`VertexBuffer` + `GraphicsDevice::DrawPrimitives`, then applies the effect and confirms the
near/far occlusion relationship (proven by real depth-buffer testing, not draw-submission order)
survives the `GetData()` → `AsciiQuantizer` → `SpriteBatch` round-trip. It self-skips (exit 77) via
`GraphicsDevice::SupportsCapability(GraphicsCapability::ThreeD)` on renderers with no real 3D
pipeline.

## Portable CPU implementation (v1)

`Draw()` reads the source texture back to the CPU (`Texture2D::GetData()`), quantizes it there
(the migrated `AsciiQuantizer`/`AsciiFontAtlas` logic, byte-for-byte the former renderer's own
algorithm), then re-uploads the result as textured quads via `SpriteBatch`. This is a real
GPU-to-CPU readback on every `Draw()` call — the same cost class the former `ASCII` renderer's own
`Present()` already paid every frame (it read `gameTarget_` back on every present too). This
implementation intentionally does not depend on `plans/plan_moderngraphics.md`, compute shaders, or any
Vulkan-specific functionality — it works identically on every CNA renderer, including 2D-only,
non-shader ones (`SDL_RENDERER`, `HEADLESS`, `SOFTWARE`, ...).

## FUTURE ASCII GPU acceleration

A shader-only GPU path (source texture → fragment shader → cell luminance/color selection → glyph
atlas sampling, with no per-frame CPU readback) is architecturally possible on shader-capable
renderers via `Microsoft::Xna::Framework::Graphics::ShaderEffect` (the same GLSL-effect mechanism
`CRTEffect`/`DepthEffect` already use). It is deliberately deferred, not implemented in this
migration:

- it would not run on 2D-only/non-shader renderers (`SDL_RENDERER`, `HEADLESS`, `SOFTWARE`, ...),
  which this effect must keep supporting uniformly, the same way `CRTEffect`/`DepthEffect` fall
  back to a no-op on those renderers today;
- it is a genuinely new rendering-architecture investment (a GLSL luminance/glyph-selection shader,
  glyph-atlas texture sampling logic in GLSL), out of scope for a correctness-preserving migration.

If ever built, it stays a second implementation strategy behind the same public
`AsciiPostProcessEffect` API — not a prerequisite for it, and not a new public renderer identity.

## Not a terminal renderer

`AsciiPostProcessEffect` produces an ASCII-*looking* graphical framebuffer — real textured quads on
a real render target — not actual character-cell terminal/TTY output. A possible future genuine
terminal renderer (real ANSI/TTY output) would be an entirely separate, unrelated concept; none is
implemented or planned as part of this effect.
