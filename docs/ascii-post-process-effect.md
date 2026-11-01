# AsciiPostProcessEffect

`CNA::Graphics::AsciiPostProcessEffect` transforms a completed texture into a graphical glyph
grid. Enable `CNA_CNAEXT=ON` and include `CNA/Graphics/AsciiPostProcessEffect.hpp` (or
`CNA/Graphics/CNAEXT.hpp`). Its device must outlive the effect and its internal atlas/SpriteBatch.

The effect uses public graphics abstractions rather than a concrete renderer. It still requires
working source readback, atlas textures and SpriteBatch rendering on the selected device. Renderer
neutrality does not guarantee that every declared renderer can execute or read back the pass.

## Usage

```cpp
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"

// device and sceneTarget belong to the application and outlive this effect.
CNA::Graphics::AsciiPostProcessEffect asciiEffect(device);
asciiEffect.setCellSize(8, 8);
asciiEffect.setQuantizeMode(CNA::Graphics::AsciiQuantizeMode::Color);

// Render the source image into sceneTarget first, then select a different destination.
device.SetRenderTarget(nullptr);
asciiEffect.Draw(sceneTarget);
// An explicit destination is also available:
// asciiEffect.Draw(sceneTarget, Microsoft::Xna::Framework::Rectangle(x, y, width, height));
```

`Draw(source)` uses the current viewport's width/height as the destination size; the rectangle
overload selects an explicit destination. It draws into the currently bound output target.
The application owns target selection and normal graphics-state setup around the pass.

The source may have been produced by 2D drawing or a full 3D scene. A readable completed image is
the boundary: the effect does not render the source geometry or supply a 3D pipeline itself.

## Controls and grid

| API | Behavior |
|---|---|
| `setCellSize(width, height)` / `getCellSize(width, height)` | Source pixels per cell; defaults to 8×8. Nonpositive dimensions throw `std::invalid_argument` without changing the previous size. |
| `setQuantizeMode(mode)` / `getQuantizeMode()` | Color or BlackWhite quantization. |
| `Draw(source)` / `Draw(source, rectangle)` | Read, quantize and draw one image. |
| `GetLastGridDimensions(columns, rows)` | Initially zero; updated when Draw finishes quantization, before SpriteBatch submission, so a later draw failure need not preserve the old values. |

At construction, `CNA_ASCII_MODE` selects `COLOR` or `BLACKWHITE`, case-insensitively. Unset or
unrecognized text defaults to Color. Later environment changes do not mutate an existing effect.

The grid rounds up to `ceil(source_width / cell_width)` columns and
`ceil(source_height / cell_height)` rows. Partial edge cells average only their actual source
pixels. RGB averages use integer division; luminance selects the nearest density-ramp glyph.
Destination cell edges are computed from their grid indices, avoiding accumulated rounding gaps.

## Color and alpha

Source alpha is ignored. Color mode uses each cell's averaged RGB foreground and a quarter-bright
background. BlackWhite uses a white foreground; although the quantizer marks that cell as having no
colored background, the public effect explicitly paints black behind it.

Both modes draw opaque cell backgrounds and opaque glyph pixels. With an ordinary positive
destination rectangle, the rendered area is opaque subject to normal viewport/clipping behavior;
source transparency is not preserved. PointClamp atlas sampling prevents glyph-edge bleed.

## Cost and renderer boundary

Each Draw reads the complete source via `Texture2D::GetData`, converts native Color values to
packed RGBA bytes, builds the grid and submits background/glyph quads through SpriteBatch.
There are per-draw native arrays/grid allocations and up to two sprite commands per cell.
C++ Color objects are converted channel by channel; their memory is not assumed to be four packed
bytes.

A GPU-backed source can require a GPU-to-CPU readback on every call. A CPU-backed renderer can
serve its own data instead, so neither the transfer cost nor observable renderer behavior is
identical everywhere. Unsupported readback/draw behavior follows the underlying graphics contract.
No shader-only ASCII path or dirty-cell incremental update is implemented.

The C API's owned handle, device lifetime, inputs and refusal behavior are in
[c-api/GRAPHICS_EXT.md](c-api/GRAPHICS_EXT.md). General texture readback is described in
[c-api/TEXTURES.md](c-api/TEXTURES.md).

## Verification

The current regression sources are:

| Test/target | Evidence |
|---|---|
| `AsciiPostProcessEffectTest.*` | Constructor/defaults, controls, validation and initial grid state. |
| `Ascii_Quantizer` | CPU glyph/color selection and edge-cell behavior. |
| `Ascii_FontAtlas` | Atlas content/layout. |
| `AsciiPostProcessEffect_Pixel` | End-to-end rendered pixels and alpha/background behavior. |
| `AsciiPostProcessEffect_3DSource` | A depth-tested BasicEffect/VertexBuffer scene survives the readback/quantize/draw path. |

The example/oracle registration is in `modules/graphics-ext/examples/CMakeLists.txt`.
Build the relevant targets and run them through `tools/platform/run_gpu_tests_private.sh`.
The 3D source oracle skips without a real 3D pipeline; raster/readback refusals can skip the pixel
path too. Those skips do not qualify a renderer.

## Historical boundary

The retired `ASCII` renderer wrapped the former `SDL_RENDERER` implementation and applied the
look to a whole presented frame. Its reusable quantizer/atlas became this explicit texture effect;
the old renderer selector is refused. See [removed-renderers.md](removed-renderers.md).
Migration commits and the completed ASCII-1..ASCII-82 campaign remain in Git history.

Any future acceleration is a separate measured implementation change behind this effect API.
The effect produces textured framebuffer pixels, not ANSI/TTY character output.
