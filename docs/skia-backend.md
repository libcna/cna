# Skia backend

## Current status

`CNA_GRAPHICS_BACKEND=SKIA` is an experimental CPU-raster 2D backend. Its first vertical slice owns a raster `SkSurface`, clears it through `SkCanvas`, reads RGBA8 pixels back, and presents them through an SDL streaming texture. It deliberately does not create an OpenGL context and does not call the EasyGL backend.

The implemented surface is intentionally small: `Clear`, `Present`, backbuffer readback, logical-size handling, window-coordinate transforms, level-0 `Texture2D` upload/readback, basic CPU-raster `RenderTarget2D`, and immediate `SpriteBatch` drawing work. The SpriteBatch slice covers destination/source rectangles, XNA-convention tint, rotation, flips, `Begin`'s 2D affine transform, viewport and scissor clipping, point/linear sampling with Clamp addressing, and `BlendState::{Opaque, AlphaBlend, NonPremultiplied, Additive}`. A `RenderTarget2D` can be bound as the active canvas, read back, and sampled as a sprite once unbound; no depth, mipmap, or MSAA attachment is claimed. Mipmaps, Wrap/Mirror addressing, effects, MRT, and all 3D APIs currently report a deterministic exception rather than being silently ignored. `SetRenderTargets(nullptr, 0)` restores the default raster backbuffer.

## Dependency policy

CNA does not download Skia during CMake configuration. Build Skia outside the CNA source tree and pass the two resulting paths explicitly. The dependency is pinned to the official Skia commit `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`; it was the `main` revision used for the initial integration spike. Skia is distributed under the BSD-style license in its source checkout; a packaged CNA distribution must include its upstream license/notice before this experimental backend is shipped.

The current link adapter requires every archive emitted by the minimal raster build. A missing or incompatible build therefore fails at CMake configure time rather than silently linking a different Skia installation.

## Reproducible Linux raster build

The following is the supported initial build input. It assumes a C++23-capable Clang, Ninja, Python 3, and Skia's `gn` tool. Use no more than two jobs where the host requires that limit.

```sh
git clone https://skia.googlesource.com/skia.git /path/to/skia
git -C /path/to/skia checkout ebf50520d720a1ce9d842d942d04c6c39c3fbc7b
cd /path/to/skia
bin/fetch-gn
bin/gn gen /path/to/skia-out/raster --args='is_official_build=true is_debug=false cc="clang" cxx="clang++" skia_use_gl=false skia_enable_ganesh=false skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false skia_enable_pdf=false skia_use_freetype=false skia_use_fontconfig=false skia_use_libpng_decode=false skia_use_libjpeg_turbo_decode=false skia_use_libwebp_decode=false skia_use_wuffs=false skia_use_icu=false skia_enable_tools=false'
ninja -C /path/to/skia-out/raster -j2 skia
```

The output directory must contain `libskia.a`, `libskcms.a`, `liballocator_base.a`, `liballocator_core.a`, `liballocator_shim.a`, and `libraw_ptr.a`.

## Configure CNA

```sh
cmake -S . -B build-skia -G Ninja \
  -DCNA_GRAPHICS_BACKEND=SKIA \
  -DCNA_SKIA_ROOT=/path/to/skia \
  -DCNA_SKIA_BUILD_DIR=/path/to/skia-out/raster
cmake --build build-skia --parallel 2
```

`cmake/ThirdPartySkia.cmake` exports `CNA::Skia`, including the header root, all six static archives in a linker group, threads, and `dl` where needed. It is intentionally limited to the tested GNU/Clang ELF raster configuration until platform-specific adapters are added.

## Execution modes and capability policy

| Mode | Status | Presentation | 3D/depth/stencil |
|---|---|---|---|
| Raster | Implemented first slice | `SkSurface` readback to SDL streaming texture | Unsupported |
| Ganesh/OpenGL | Not implemented | Requires a separately owned current GL context and framebuffer wrapper | Not claimed |
| Graphite/Vulkan/Metal/Dawn | Not investigated | No selected interop or reset contract | Not claimed |

Raster uses premultiplied RGBA8888 inside Skia and normalizes readback into top-row-first RGBA8 bytes for SDL. A future GPU path must preserve that contract and pass the same pixel tests; it may not silently change reported capabilities mid-frame.

`Texture2D` keeps a CPU shadow, so its successful public `GetData` calls return the exact bytes
accepted by `SetData`. At draw time the active blend preset selects an explicitly labelled
premultiplied (`AlphaBlend`) or straight-alpha (`NonPremultiplied`) Skia image made from those
same bytes. Tint uses a cached SkSL color filter so XNA's per-component colour and alpha
multiplication is preserved without applying tint alpha to premultiplied RGB a second time. A
direct `SkiaSurface::WritePixels`/`ReadPixels` round trip is
different by design: converting through Skia's 8-bit premultiplied storage has deterministic
integer unpremultiplication rounding for semi-transparent texels. The raster test records this
boundary explicitly; future code must not describe it as a byte-exact straight-alpha surface
round trip.

The current public texture-format policy is intentionally the existing CNA-wide policy:
`SurfaceFormat::Color` is the only accepted format. Every other `SurfaceFormat` value is rejected
by shared validation before a Skia allocation is attempted. Raster textures accept one-pixel and
NPOT dimensions, report the shared 16384 maximum single axis, and reject a dimension above that
limit before allocation. Mipmapped texture construction is also rejected before data is accepted.

## Verification recorded for the initial slice

1. A standalone C++23 smoke target created a raster `SkSurface`, cleared it, read a pixel, and linked the six archives above.
2. CNA configured with `CNA_GRAPHICS_BACKEND=SKIA`, the two explicit Skia paths, and no EasyGL target.
3. The `CNA` static-library target compiled successfully with the SKIA backend selection using `cmake --build ... --parallel 2`.
4. A second C++23 smoke target uploaded and updated a two-pixel `SkiaTextureBackend`, drew it to a `SkiaSurface`, and compared the exact RGBA8 readback bytes after each draw.
5. The same smoke target uploaded a `SkiaRenderTargetBackend`, sampled its immutable `SkImage` snapshot, and checked exact target readback bytes.
6. `cmake/Tests/SkiaTests.cmake` registers forty-three SKIA-only CTests: two window-independent raster
   surface pixel tests and forty-one display-required public tests. The raster tests pass without a
   display. The capability test verifies every current `GraphicsCapability` is false and 3D calls
   still throw. The public `Texture2D::GetData` and transfer-range contract tests pass 40/40 and
   70/70 checks respectively against the raster backend; the demo smoke exits successfully after
   three frames in Xvfb.
7. `Skia_Texture2D_Constraints` verifies the successful `Color` path, all 26 unsupported
   `SurfaceFormat` values, 1×1 and 3×5 uploads, a large valid single axis, zero dimensions, and
   the precise above-limit rejection. `Skia_Texture2D_NpotSampling` then samples both 3×5 and
   7×11 textures and reads each distinct source row back from the rendered frame.
8. `Skia_SpriteBatch_BeginEnd`, `Skia_SpriteBatch_SourceRect`, and
   `Skia_SpriteBatch_Overloads` exercise real public SpriteBatch sessions. They verify invalid
   sequencing, native-size and destination/source-rectangle draws, all current overloads, tint,
   scaling, and a discriminating horizontal-flip pixel assertion.
9. `Skia_BlendState_Opaque`, `Skia_BlendState_AlphaBlend`,
   `Skia_BlendState_NonPremultiplied`, and `Skia_BlendState_Additive` verify the four currently
   supported SpriteBatch blend presets over a real background, including source-alpha scaling and
   saturation for additive composition. `Skia_SpriteBatch_TintAlpha` then verifies
   semi-transparent tint for both source-alpha conventions with distinct expected pixel values.
10. `Skia_SpriteBatch_Rotation`, `Skia_SpriteBatch_Scale`,
    `Skia_SpriteBatch_NegativeScale`, `Skia_SpriteBatch_Effects`, and
    `Skia_SpriteBatch_TransformMatrix` verify rotation around a caller-origin, positive/non-uniform
    and negative X/Y scale, both SpriteEffects flips, and an affine Begin transform in XNA order.
11. `Skia_SpriteBatch_DeferredOrder`, `Skia_SpriteBatch_ImmediateFlush`,
    `Skia_SpriteBatch_LayerDepth`, and `Skia_SpriteBatch_TextureSort` prove the common
    SpriteBatch queue preserves deferred/immediate, layer-depth, and texture sort order through
    real Skia canvas submission.
12. `Skia_TextureFilter_PointVsLinear` and `Skia_TextureFilter_Minification` prove the
    `SamplerState` mapping distinguishes Point from Linear in both magnification and minification
    rather than selecting a fixed sampling mode.
13. `Skia_SpriteBatch_SamplerTransition` proves that successive SpriteBatch Begin blocks replace
    their sampler state (Point → Linear → Point) without leaking the previous filter.
14. `Skia_SpriteFont_SingleGlyph`, `Skia_SpriteFont_MultiGlyphSpacing`,
    `Skia_SpriteFont_Newline`, `Skia_SpriteFont_DefaultChar`, and `Skia_SpriteFont_Effects`
    confirm that shared SpriteFont atlas layout—not Skia text APIs—renders glyphs, spacing,
    line advances, fallback glyphs, scale/origin, and flips correctly.
15. Headless `Skia_Texture_AlphaBoundary` proves the two RGBA source-alpha labels produce the
    exact distinct source-over result expected from premultiplied and straight source data, and
    that a strided CPU upload retains each row while excluding caller padding bytes.
16. `Skia_RenderTarget2D_SampleAfterUnbind`, `Skia_RenderTarget2D_Usage`,
    `Skia_GetBackBufferData_AfterRtUnbind`, and `Skia_RenderTarget2D_Readback` prove the
    CPU-raster target can render, survive or discard a rebind as requested, restore the
    backbuffer, be sampled afterward, and return full/partial top-row-first pixels.
17. `Skia_RenderTarget2D_Switch` proves an A → B → backbuffer target sequence preserves
    independent target content through the surface switch and subsequent sampling.
18. `Skia_SpriteBatch_SourceRectLinear` magnifies a one-texel source rectangle with `LinearClamp`
    and verifies every source edge and two corners remain isolated from distinct neighbouring texels.
19. `Skia_SpriteBatch_RasterizerState` verifies that a Begin-supplied `RasterizerState` enables and
    disables the stored `ScissorRectangle` for deferred, immediate, and front-to-back SpriteBatch
    submissions, without retaining a pointer to the caller's state object.
20. `Skia_RenderTarget2D_Scissor` verifies target-local scissor coordinates, all four clip
    boundaries, target unbinding, and the disabled-scissor control through `RenderTarget2D::GetData`.
21. `Skia_SpriteBatch_Viewport` verifies non-zero viewport placement and clipping on both surface
    types. `Skia_Viewport_ProjectUnproject` then verifies a 2D orthographic `Project`/`Unproject`
    round trip against the same live, offset `GraphicsDevice::Viewport` convention.

Automated Skia raster/display tests, SpriteBatch, textures, render targets, and the GPU strategy remain tracked in `plan_skia.md`.
