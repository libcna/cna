# Skia backend

## Current status

`CNA_GRAPHICS_BACKEND=SKIA` is an experimental CPU-raster 2D backend. Its first vertical slice owns a raster `SkSurface`, clears it through `SkCanvas`, reads RGBA8 pixels back, and presents them through an SDL streaming texture. It deliberately does not create an OpenGL context and does not call the EasyGL backend.

The implemented surface is intentionally small: `Clear`, `Present`, backbuffer readback, logical-size handling, window-coordinate transforms, level-0 `Texture2D` upload/readback, basic CPU-raster `RenderTarget2D`, and immediate `SpriteBatch` drawing work. The SpriteBatch slice covers destination/source rectangles, XNA-convention tint, rotation, flips, `Begin`'s 2D affine transform, viewport and scissor clipping, point/linear sampling, and independent Clamp/Wrap/Mirror U/V addressing, as well as `BlendState::{Opaque, AlphaBlend, NonPremultiplied, Additive}`. A newly created or resized backbuffer starts transparent black, so a zero-draw `Present` never exposes allocator data. A `RenderTarget2D` can be bound as the active canvas, read back, and sampled as a sprite once unbound; no depth, mipmap, or MSAA attachment is claimed. A requested `DepthFormat` remains public construction metadata for API compatibility, but `HasRealDepthBuffer` stays false and depth/stencil-only clears have no colour effect. Mipmaps, effects, MRT, and all 3D APIs currently report a deterministic exception rather than being silently ignored. `SetRenderTargets(nullptr, 0)` restores the default raster backbuffer.

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

## Diagnostic state trace

Set `CNA_SKIA_STATE_TRACE=1` when launching a Skia executable to emit backend-only state lines
to standard error. The trace reports backbuffer/render-target selection and size, blend preset and
source-alpha convention, sampler filter/address modes, and scissor rectangle updates. It is off by
default (and also off when set to `0`), does not change raster state, and is intended for diagnosing
state leakage rather than application logging.

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
table maps only the four public tuples whose source convention is already defined: `Opaque` to
`kSrc` with premultiplied bytes, `AlphaBlend` to `kSrcOver` with premultiplied bytes,
`NonPremultiplied` to `kSrcOver` with straight bytes, and `Additive` to `kPlus` with straight
bytes. A custom `BlendState` carries factors and equations but no source-byte alpha label, so the
raster backend must not silently pick a superficially similar `SkBlendMode`. SKIA-54 additionally
accepts one independently proven runtime-blender tuple: colour
`DestinationColor`/`Zero`, alpha `One`/`Zero`, both `Add`; it uses premultiplied source bytes and
is proven only with opaque source/destination input. Every other tuple outside the four presets
still fails before drawing and names the requested factors/functions. SKIA-55 records this as the
current bounded full-matrix policy: no unproven tuple is silently treated as SourceOver.
direct `SkiaSurface::WritePixels`/`ReadPixels` round trip is
different by design: converting through Skia's 8-bit premultiplied storage has deterministic
integer unpremultiplication rounding for semi-transparent texels. The raster test records this
boundary explicitly; future code must not describe it as a byte-exact straight-alpha surface
round trip.

### Custom blend-state investigation

The pinned Skia checkout offers three relevant mechanisms. Fixed `SkBlendMode` values and
`SkBlenders::Arithmetic` apply one predetermined operation to all RGBA components, so neither can
represent an XNA state with independent colour and alpha factors/functions. In contrast,
`SkRuntimeEffect::MakeForBlender` accepts a `main(half4 src, half4 dst)` entry point and installs
the resulting `SkBlender` through `SkPaint::setBlender`. It can write separate RGB and alpha
expressions, use an RGBA uniform for `BlendFactor`, calculate `SourceAlphaSaturation` as
`min(src.a, 1-dst.a)` for RGB (and `1` for alpha), and use `+`, `-`, reversed `-`, `min`, or `max`
for the five XNA equations. Thus every current `Blend` and `BlendFunction` ordinal has an
algebraic direct-runtime-blender expression. This is not a claim that the public backend has
implemented the full matrix yet.

`Skia_RuntimeBlender_Raster` proves this interface on the selected CPU raster path: it composites
different RGB and alpha equations from the real source/destination canvas values. It also proves
that the raster pipeline retains a deliberately non-premultiplied output. That is required before
supporting an XNA state that leaves RGB while setting output alpha to zero, but it means later
public work must preserve that data through `RenderTarget2D`, readback, sampling, and SDL
presentation. `Skia_RuntimeBlender_Policy` now proves that a public SpriteBatch state with colour
`DestinationColor`/`Zero` and independent alpha `One`/`Zero` is exact for opaque source and
destination on the backbuffer, a `RenderTarget2D` readback, and a subsequent target sample.
Runtime Effects are documented by Skia itself as experimental, so their use stays behind the
explicit `SKIA` backend. SKIA-55's exhaustive selector maps only tuples with an established
source convention and rejects the rest. The source image's
straight/premultiplied label remains CNA-owned; the blender alone cannot infer it from arbitrary
RGBA bytes.

### Colour-write and sample-mask investigation

`Skia_ColorWriteMask_Raster` proves the direct raster building block for `ColorWriteChannels`: a
runtime blender computes the normal premultiplied source-over result first and then chooses each
of RGBA from that result or the original destination. All sixteen masks preserve their disabled
destination bytes, including `None`. SKIA-57 applies that post-blend selection to the four direct
routes and the bounded destination-reading runtime route. `Skia_ColorWrite_Policy` checks all
sixteen masks after every accepted route on the backbuffer, all sixteen after the destination-
reading route on RenderTarget2D readback, and alpha selection with distinct source/destination
alpha. Only target-0 `ColorWriteChannels` is meaningful for this one-target backend;
ColorWriteChannels1-3 still reject rather than pretending MRT support.

Raster `RenderTarget2D` has no physical samples: requests 0 and 1 apply a reported count of 0,
while real MSAA requests are rejected at construction. Therefore `MultiSampleMask` has no
per-sample object to select. The all-bits default remains accepted; every other public mask,
including zero, is rejected rather than being silently ignored or pretending to provide coverage
control.

The current public texture-format policy is intentionally the existing CNA-wide policy:
`SurfaceFormat::Color` is the only accepted format. Every other `SurfaceFormat` value is rejected
by shared validation before a Skia allocation is attempted. Raster textures accept one-pixel and
NPOT dimensions, report the shared 16384 maximum single axis, and reject a dimension above that
limit before allocation. Mipmapped `Texture2D` construction is also rejected with
`System::NotSupportedException` before data is accepted; `mipMap=true` `RenderTarget2D`
construction raises the same exception. This is a deliberate raster policy: the pinned Skia
checkout does contain a mip builder under private `src/core`, but the public raster `SkImage`
creation path used by CNA accepts a level-0 pixmap only and supplies no stable public contract for
CNA-owned level upload/readback. CNA will not bind Skia's internal cache implementation merely to
imply a mip-chain contract it cannot fully expose. The six `TextureFilter` values that name a mip
selection rule are likewise rejected with `System::NotSupportedException` during
`SpriteBatch::Begin`; they are never silently treated as a level-0 Point or Linear request.

## Verification recorded for the initial slice

1. A standalone C++23 smoke target created a raster `SkSurface`, cleared it, read a pixel, and linked the six archives above.
2. CNA configured with `CNA_GRAPHICS_BACKEND=SKIA`, the two explicit Skia paths, and no EasyGL target.
3. The `CNA` static-library target compiled successfully with the SKIA backend selection using `cmake --build ... --parallel 2`.
4. A second C++23 smoke target uploaded and updated a two-pixel `SkiaTextureBackend`, drew it to a `SkiaSurface`, and compared the exact RGBA8 readback bytes after each draw.
5. The same smoke target uploaded a `SkiaRenderTargetBackend`, sampled its immutable `SkImage` snapshot, and checked exact target readback bytes.
6. `cmake/Tests/SkiaTests.cmake` registers sixty-seven SKIA-only CTests: six window-independent raster
   pixel tests and sixty-one display-required public tests. The raster tests pass without a
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
    `Skia_GetBackBufferData_AfterRtUnbind`, `Skia_GetBackBufferData_ActiveTarget`, and
    `Skia_RenderTarget2D_Readback` prove the CPU-raster target can render, survive or discard a
    rebind as requested, expose active-target versus restored-backbuffer readback correctly, be
    sampled afterward, and return full/partial top-row-first pixels.
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
22. `Skia_TextureAddressAxes` verifies Point Clamp/Wrap/Mirror in both dimensions, including a
    negative source coordinate and mixed U/V modes. The 2D demo smoke also completes under the
    new tiled shader path.
23. `Skia_SpriteBatch_RemainingOverloads`, alongside `Skia_SpriteBatch_Overloads`, executes all ten
    public texture `Draw` variants and all six `DrawString` variants. It verifies the final optional
    source-rectangle/flip overload and the string and `StringBuilder` glyph routes at basic,
    scalar, and non-uniform scales.
24. `Skia_SpriteBatch_Stress` reuses twelve textures and two preserve-content targets for 64 actual
    frames. Each frame switches A → B → backbuffer and makes 26 independent SpriteBatch sessions;
    the two target anchor pixels and a complete-backbuffer FNV-1a hash must remain stable.
25. `Skia_Texture2D_MipmapPolicy` pins the raster decision after the Skia API audit: mipmapped
    texture and render-target construction both raise `System::NotSupportedException`, while a
    subsequent level-0 upload and Point sprite draw remain correct.
26. `Skia_Sampler_MipmapFilterPolicy` verifies every mip-dependent `TextureFilter` value rejects
    during `SpriteBatch::Begin`, before any draw; it then reuses the same batch successfully with
    `PointClamp`.
27. `Skia_BlendEnabled_State` verifies `SetBlendEnabled(false)` makes sprites replace their
    destination, `Clear` remains an unconditional surface clear, and re-enabling restores the
    previously configured `BlendState::AlphaBlend` composition.
28. `Skia_Texture2D_Dispose`, `Skia_DisposedGuards`, and `Skia_DoubleDispose` verify shared
    texture-copy ownership, disposed-resource consumption guards, and idempotent cleanup under
    both the normal raster build and the AddressSanitizer/LeakSanitizer raster build. The latter
    keeps leak detection enabled and suppresses only SDL loader and host-process font/runtime
    globals, so CNA and Skia allocations remain checked. In this GCC/Xvfb environment, a 2,864-byte
    unsymbolized process-exit residual remains in both the old presentation test and the SKIA-69
    window test after those suppressions; it is recorded rather than hidden by a broad suppression.
29. `Skia_RasterizerState_Policy` distinguishes the one 2D RasterizerState field from 3D-only
    fields: a solid SpriteBatch ignores cull/depth-bias/MSAA values, `WireFrame` remains
    unadvertised and fails with an actionable error, and the rejected `Begin` leaves the batch
    reusable for a subsequent solid draw.
30. `Skia_StateTransition` keeps scissor enabled across a RenderTarget2D bind/unbind, proves the
    public reset expands it to the full backbuffer, verifies that `Clear` is unconditional without
    discarding the raster state, and checks both rejected `Begin` recovery and custom-viewport
    preservation across a same-size `Present`.
31. Running `Skia_StateTransition` with `CNA_SKIA_STATE_TRACE=1` emits readable surface, blend,
    sampler, and scissor transitions while preserving all eleven pixel assertions; without the
    flag it emits none of those diagnostic lines.
32. `Skia_RenderTarget2D_DepthPolicy` constructs a target requesting `Depth24Stencil8`, verifies
    the request stays metadata while the backend reports no real attachment, and proves a
    depth/stencil-only clear leaves its red target pixel unchanged.
33. `Skia_GetBackBufferData_ActiveTarget` verifies that `GetBackBufferData` reads the currently
    bound target canvas, agrees with target readback, and restores the independent red default
    backbuffer after unbinding the blue target.
34. `Skia_Presentation_Edge` proves a zero-draw `Present` reads transparent black, a Clear-only
    frame presents its fresh colour, and a disposed-texture draw failure followed by `Present`
    retains the current clear rather than reviving an older frame.
35. `Skia_RenderTarget2D_SetData` proves full and partial level-0 uploads update the existing
    target surface without replacing its backend identity, preserves untouched pixels, round-trips
    through public readback, and rejects level 1 and mipmapped-target requests precisely.
36. `Skia_RenderTarget2D_MsaaPolicy` fixes the raster MSAA contract: requests 0 and 1 report 0,
    while normalized real MSAA requests (2, 3, and 4) fail before target creation and the
    corresponding capability remains false.
37. `Skia_RenderTargetBinding_Raster` and `Skia_RenderTarget2D_Lifetime` close the target lifetime
    hole: a target's destructor weakly detaches it from the backend-owned active-surface record
    before releasing its `SkSurface`. The raster test covers 128 snapshot lifetimes and the inverse
    backend-before-target order; the public test proves a subsequent Clear and SpriteBatch draw land
    on the backbuffer and that a fresh target cycle still works.
38. `Skia_BlendMapping_Raster` table-tests all thirteen public `Blend` ordinals in each factor
    position, all twenty-five colour/alpha `BlendFunction` pairs, the four direct preset mappings,
    and their diagnostic names. `Skia_BlendMapping_Policy` verifies public rejection of an
    unsupported factor/equation and that each failed `SpriteBatch::Begin` leaves the batch usable.
39. `Skia_RuntimeBlender_Raster` compiles a two-input `SkRuntimeEffect` blender, composites an
    independent RGB/alpha expression against an actual raster destination, and proves the selected
    Skia raster pipeline preserves an intentional non-premultiplied output rather than silently
    replacing it with SourceOver or clamping RGB to alpha.
40. `Skia_RuntimeBlender_Policy` moves the first non-preset runtime blender through the public
    SpriteBatch path. It proves a destination-reading, independent-alpha state on the backbuffer,
    then on RenderTarget2D readback and target sampling, without retaining the custom blender for a
    following Opaque sampling draw.
41. `Skia_BlendMapping_Raster` exhaustively checks all 714,025 combinations of the thirteen
    source/destination factors and five colour/alpha functions. It accepts only the four direct
    presets plus the SKIA-54 runtime tuple; every other combination has the deterministic error
    path rather than a silent `kSrcOver` fallback.
42. `Skia_ColorWriteMask_Raster` runs a real post-blend runtime blender for all sixteen RGBA write
    masks and checks raw premultiplied output bytes: each disabled component is retained from the
    destination, including the zero-mask no-write case. `Skia_BlendMapping_Policy` confirms the
    raster's non-default sample-mask rejection and safe batch recovery.
43. `Skia_ColorWrite_Policy` exercises the public post-blend mask implementation: all sixteen
    masks after every accepted blend route on the backbuffer, all sixteen on the destination-
    reading runtime route in RenderTarget2D, and alpha-bit selection with distinct 128/255 source
    and destination alpha values.

Automated Skia raster/display tests, SpriteBatch, textures, render targets, and the GPU strategy remain tracked in `plan_skia.md`.
