# Skia backend

## Current status

`CNA_GRAPHICS_BACKEND=SKIA` is an experimental CPU-raster 2D backend. Its first vertical slice owns a raster `SkSurface`, clears it through `SkCanvas`, reads RGBA8 pixels back, and presents them through an SDL streaming texture. It deliberately does not create an OpenGL context and does not call the EasyGL backend.

The implemented surface is intentionally bounded: `Clear`, `Present`, backbuffer readback, logical-size handling, window-coordinate transforms, level-0 `Texture2D` upload/readback, basic CPU-raster `RenderTarget2D`, and immediate `SpriteBatch` drawing work. The SpriteBatch slice covers destination/source rectangles, XNA-convention tint, rotation, flips, `Begin`'s 2D affine transform, viewport and scissor clipping, point/linear sampling, and independent Clamp/Wrap/Mirror U/V addressing, as well as `BlendState::{Opaque, AlphaBlend, NonPremultiplied, Additive}`. A newly created or resized backbuffer starts transparent black, so a zero-draw `Present` never exposes allocator data. A `RenderTarget2D` can be bound as the active canvas, read back, and sampled as a sprite once unbound; no depth, mipmap, or MSAA attachment is claimed. A requested `DepthFormat` remains public construction metadata for API compatibility, but `HasRealDepthBuffer` stays false and depth/stencil-only clears have no colour effect. Plain `TextureCube` and `Texture3D` have bounded CPU-only face/voxel storage for exact transfer, readback, mip, and DDS-loading contracts; they cannot be sampled. `RenderTargetCube` additionally has a bounded six-surface 2D emulation with exact face rendering, uploads/readback, Preserve/Discard, and generated mip levels. It has no cube sampler, real depth, or real MSAA. The complete storage/sampling boundary and 256 MiB policy are in [`skia-texture-storage.md`](skia-texture-storage.md). Effects, MRT, cube/volume sampling, and all 3D draw APIs currently report a deterministic exception rather than being silently ignored. `SetRenderTargets(nullptr, 0)` restores the default raster backbuffer.

The complete API-level comparison with EasyGL is maintained in
[`skia-easygl-parity-ledger.md`](skia-easygl-parity-ledger.md). Its 247 rows cover every current
backend/resource method, capability value, and public `GraphicsDevice` declaration; the registered
`Skia_ParityLedger_Audit` test prevents those headers and their classifications from drifting.
The companion [`skia-easygl-test-matrix.md`](skia-easygl-test-matrix.md) classifies all 347 current
EasyGL test registrations, manual tools, golden images, and XNA-oracle scenes by their most
demanding Skia route; `Skia_TestMatrix_Audit` keeps CMake and both asset directories synchronized.

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

## Startup capability diagnostic

After successful construction, the backend emits one stable capability line. It contains the
pinned Skia revision, `surface=raster`, `colour=RGBA_8888/premultiplied`, `samples=0`, and
`anisotropic filtering=unsupported`. The line has no private pointer/device values and is not
repeated per frame. It describes only the selected raster mode; a future accelerated mode must
replace these fields with its probed device results rather than inheriting them.

## Initialization and fallback policy

The current `SKIA` selection is unconditionally CPU raster; it does not probe an accelerated Skia
surface and therefore cannot silently fall back from one. Missing source headers or any of the six
required archives stop CMake configuration. Failure to create the SDL renderer or streaming
presentation texture aborts backend construction, releases every partially acquired object, and
preserves the caller's window for a retry. A successful construction emits the immutable raster
capability line above. There is no capability change or implementation swap during a frame.

A future accelerated mode must expose its selection at construction, report its own capabilities,
and define a tested reset/fallback policy before it can be enabled. It must not inherit the current
raster diagnostic or turn a runtime device loss into an unannounced CPU-mode switch.

## Diagnostic state trace

Set `CNA_SKIA_STATE_TRACE=1` when launching a Skia executable to emit backend-only state lines
to standard error. The trace reports backbuffer/render-target selection, stable surface identity,
and size, blend preset and
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

The raster present-interval policy delegates to SDL: Immediate requests 0, One/Default request 1,
and Two requests 2 with a deterministic fallback to 1 only if the current renderer rejects 2.
The backend records the actual applied value and reapplies it when the SDL presenter is rebuilt.
This is not evidence for a future accelerated Skia surface, which must probe its own native swap
or submit policy.

Presentation keeps the preferred virtual width and height separate from the current raster size.
Letterbox, Overscan, and Stretch retain the requested raster dimensions and delegate their scale
and centred offset to SDL. NativeBackBuffer copies the raster at 1:1 output-pixel size into the
top-left after clearing unused output black. FixedHeightDynamicWidth retains the preferred height
and computes `round(outputWidth * preferredHeight / outputHeight)` for both the CPU surface and
SDL logical presentation. A physical resize is detected by Clear, viewport query, or Present; the
just-completed frame is presented first, then the raster is replaced for the next frame. Leaving
the dynamic mode restores the preferred width. Window/logical transforms always use SDL's
DPI-aware renderer-coordinate API, so they share the presentation scale and offsets exactly.

The raster images and `SkSurface` objects are CPU-owned, so they have no GPU context handle to
recreate. The test-only recovery seam therefore rebuilds the SDL renderer and its streaming
presentation texture while retaining live raster textures, render targets, and their snapshots.
It synchronously reports `DeviceResetting` followed by `DeviceReset`; it deliberately does not
claim a `DeviceLost` event for resources that did not become unavailable. A future accelerated
Skia mode needs its own genuine device-loss contract and must not inherit this raster claim.

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
6. `cmake/Tests/SkiaTests.cmake` registers 96 SKIA-only CTests: nine window-independent raster
   tests, 85 display-required public tests, and two display-free source audits. The raster tests
   pass without a display. The capability test verifies only storage-only `Texture3D` is true and
   every GPU/3D capability remains false; 3D calls still throw. The public `Texture2D::GetData` and transfer-range contract tests pass 40/40 and
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
25. `Skia_Texture2D_MipmapPolicy` pins the raster decision after the SKIA-70 API audit:
    `SkImage::withDefaultMipmaps()` is public but immutable and does not provide the per-level
    readback or mutable-target invalidation/resolve contract CNA exposes. Mipmapped texture and
    render-target construction therefore both raise `System::NotSupportedException`; a following
    level-0 texture upload/draw and a fresh target bind/Clear/readback/unbind/sample cycle remain
    correct.
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
44. `Skia_Texture2D_MipmapPolicy` also closes the target-mipmap investigation: the pinned public
    Skia API can attach generated mips only to an immutable `SkImage` snapshot, while CNA requires
    deterministic public per-level target readback and invalidation after bind/upload. The raster
    backend consequently refuses a mipmapped target before construction, then proves a new
    level-0 target can render, read back, unbind, and sample normally.
45. `Skia_Resize_Presentation` keeps a `RenderTarget2D` and `SpriteBatch` alive across an active-
    target resize and two fullscreen/windowed presentation resets. It checks all three ordered
    device-reset pairs, old/new presentation dimensions, retained target readback, new-backbuffer
    sampling, and continued SpriteBatch output. Fullscreen asserts the stored public parameter;
    Xvfb is not required to support a physical fullscreen mode switch.
46. `Skia_DisplayScale` separates the 40×30 logical Skia raster from a 120×60 letterboxed SDL
    window, records the actual renderer-output/window ratio, and verifies the 20-pixel letterbox
    offset in both coordinate directions. `GetBackBufferData` still reads logical-raster pixels;
    coordinate conversion delegates to SDL's DPI- and offset-aware renderer APIs rather than
    manually mixing output pixels with window points. The Xvfb validation ratio is 1×; a real
    high-DPI display follows the same SDL conversion path.
47. `Skia_ResourceBudget` validates the bounded raster resource policy through the internal
    debug-facing `SkiaGraphicsBackend::GetResourceStatsEXT()` counters: a texture retains exactly
    two alpha-labelled images; a target retains one surface and at most one immutable sampling
    snapshot. A target write drops the old snapshot, and 64 create/sample/release cycles return
    target, snapshot, and byte counters to zero. There is no unbounded hidden image/snapshot cache.
48. `Skia_RenderTarget2D_Golden` shares one checked-in, top-row-first 4×4 opaque RGBA quadrant
    oracle with the EasyGL and SDL_Renderer builds. Each backend matches all 16 target-readback
    pixels and all 64 Point-sampled 2× backbuffer pixels exactly, with zero tolerance; this covers
    common target orientation, unbind/restoration, snapshot freshness, and Point-sampling semantics
    without claiming GPU parity.
49. `Skia_ContextRecovery` holds a Texture2D, RenderTarget2D, and cached target snapshot across
    both debug presenter-recovery entries. Each rebuilds the real SDL renderer and streaming texture,
    reports exactly one `DeviceResetting`/`DeviceReset` pair without `DeviceLost`, preserves the
    bounded resource counters, and proves target readback, texture draw, target sampling, and
    presentation remain exact afterward.
50. `Skia_StartupDiagnostic_Raster` verifies the immutable startup report contains the pinned
    revision, raster surface mode, exact RGBA/premultiplied storage, zero samples, and unsupported
    anisotropy. It also enforces one-line static storage with no pointer-shaped value; a real Xvfb
    backend run emits that line once at construction.
51. `Skia_Lifecycle` injects constructor failures after SDL renderer creation, backbuffer/streaming-
    texture creation, and window registration. Every failure keeps its stage diagnostic, releases
    the renderer/texture/registry entry, preserves the caller window, and permits an immediately
    usable backend. Sixteen further create/present/destroy cycles leave no renderer or registry
    state; ASan is clean, while LSan sees only the known 2,864-byte external X11 exit residual.
52. The Skia selection audit proves configuration requires the resolved external dependency and
    never substitutes a fallback backend. The common compile-definition test now includes the
    previously omitted `CNA_BACKEND_SKIA` count and explicitly checks it maps to
    `GraphicsBackendType::Skia` and the exact public name `SKIA`; all eight focused identity tests
    pass in the Skia build.
53. `Skia_PresentInterval` cycles Immediate, One, Two, and Default through public device resets,
    checks each stored request and the actual SDL interval (Two may clamp from 2 to 1), rebuilds the
    presenter and verifies the selected value persists, then proves exact Clear/readback/Present.
    All fifteen checks pass in normal and AddressSanitizer builds.
54. `Skia_PresentationModes` checks all five presentation modes against measured window/output
    dimensions. Its 25 assertions cover requested and dynamically derived raster sizes, scale and
    centred offsets, bidirectional logical/window round trips, exact Clear/readback/Present, a real
    resize after mode switches, preferred-width restoration, and non-mutating invalid-mode
    rejection. It passes normally and under AddressSanitizer together with the existing display-
    scale, resize/reset, and presentation-edge regressions.
55. `Skia_Ownership` proves the raster/SDL backend is owner-thread bound, its window owns the
    expected presenter, rejected foreign-thread calls do not change backend or SpriteBatch state,
    and a SpriteBatch surviving graphics-backend destruction fails before dereferencing its raw
    drawing-state pointers. The expanded `Skia_RenderTargetBinding_Raster` rejects null/aliased
    active surfaces without mutation and proves both backend/target destruction orders. Both pass
    under AddressSanitizer; the display-free binding test also passes LeakSanitizer with leak
    detection enabled.
56. A persistent `cmake-build-skia-release` GNU 14/C++23 configuration builds the selected Skia
    backend and links the full CNA static library plus `Skia_Surface_Raster` against the six pinned
    archives using two jobs. The current Release executable passes all fourteen raster surface,
    identity, lifetime, orientation, stride, alpha-conversion, bounds, and resize checks; Debug
    and ASan/LSan variants pass the same boundary. Together with the missing-dependency and
    constructor-unwind probes, this closes the
    initial raster compile/presentation/fallback spikes without implying accelerated support.
57. `Skia_WindowLifecycle` models a minimized presenter reporting 0×0 and proves the last valid
    raster dimensions and sentinel pixel survive a Present. It then performs real synchronized
    SDL hide/show, eight physical window resizes with exact fixed-height width recalculation and
    far-corner readback, plus four actual SDL renderer/streaming-texture reconstructions while the
    CPU raster remains live. All twelve checks pass normally and under AddressSanitizer; existing
    presentation-mode and resource-recovery tests pass alongside it.
58. `Skia_Surface_Raster` closes the internal surface boundary with fourteen checks. Every logical
    surface receives a distinct non-zero process-local identity; `Resize` replaces pixel storage
    without changing it, while copying and moving are compile-time forbidden because active target
    bindings retain stable addresses. An unallocated surface exposes neither canvas nor snapshot
    and rejects `Clear` safely. The existing top-left RGBA8, premultiplication, bounds, and resize
    checks remain exact. `SkiaRenderTargetBinding` records the identities selected alongside its
    pointers and validates both before active-surface use; state trace output makes backbuffer and
    target transitions observable. Surface and binding tests pass under Debug, Release, and
    ASan/LSan with leak detection enabled, and the windowed eleven-check transition suite passes
    normally and under ASan.
59. `Skia_ParityLedger_Audit` extracts eleven backend/resource interfaces, all nine capability
    values, and every public non-deleted `GraphicsDevice` method from the live headers. It matches
    all 247 entries against `docs/skia-easygl-parity-ledger.md`, whose rows record EasyGL behavior
    and tests, the bounded Skia result/plan, final status, and evidence. Missing, stale, duplicate,
    malformed, empty, or unknown-status rows fail the display-free audit instead of allowing the
    parity inventory to drift silently.
60. `Skia_TestMatrix_Audit` inventories all 289 live EasyGL CTest registrations, two manual
    comparison executables, 17 golden PNGs, and 39 XNA-oracle scenes. The 347-entry matrix assigns
    exactly one route to each item: 75 2D-direct, 31 2D-emulation, 216 3D, and 25 device-dependent.
    The validator discovers registrations and directory contents rather than trusting recorded
    totals, so a newly added or removed EasyGL test/asset requires an explicit Skia decision.
61. `Skia_TextureStorage_Policy` proves the 16384-axis and 256 MiB per-resource limits, checked
    cube/volume mip accounting, zero initialization, exact row/slice order, atomic short-transfer
    rejection, and counter release. Nine shared public transfer fixtures plus the DDS content-load
    fixture cover every cube face, mip, rectangle, volume slice/box, start index, disposal, and
    unchanged-destination contract. The exhaustive shared read and write audits pass 56/56 each.
62. `Skia_RenderTargetCube_Policy` proves exact six-face/two-level surface-plus-shadow accounting,
    stable and isolated face identities, dirty-face mip regeneration, safe binding/destruction order, and
    pre-allocation axis/256 MiB refusal. Four shared public fixtures then cover exact asymmetric
    rendered/uploaded readback, every face and mip, Preserve/Discard, requested depth interaction,
    truthful zero-sample MSAA clamping, public properties, singular/plural binding equivalence,
    viewport/scissor reset, multi-object switching, disposal, arbitrary translucent byte-exact
    upload, and explicit MRT rejection. Cube sampling and real depth/MSAA remain unsupported and
    are never inferred from this 2D emulation.
    The complete Debug Skia suite passes 96/96 in 12.55 seconds with eight-way test parallelism;
    the six focused target/SetData tests also pass in Release and under AddressSanitizer.

Automated Skia raster/display tests, SpriteBatch, textures, render targets, and the GPU strategy remain tracked in `plan_skia.md`.
