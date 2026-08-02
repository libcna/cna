# Skia backend

## Current status

`CNA_GRAPHICS_BACKEND=SKIA` is a release-gated experimental CPU-raster 2D backend. It owns a
raster `SkSurface`, clears it through `SkCanvas`, reads RGBA8 pixels back, and presents them through
an SDL streaming texture. It deliberately does not create or use a Skia OpenGL/GPU context and
does not call the EasyGL backend. SDL may internally choose an accelerated renderer solely to
present the completed CPU image; that platform-dependent presenter is not a Skia GPU execution
mode.

SKIA-1–114 remain the signed raster baseline while the active SKIA-115–170 successor expansion is
unadvertised work in progress. Its checked routing inventory is
[`skia-successor-contract-matrix.md`](skia-successor-contract-matrix.md), and every new storage or
oracle route is governed by
[`skia-successor-resource-oracles.md`](skia-successor-resource-oracles.md). Baseline capability
claims below change only after the successor release gate passes.

The implemented surface is intentionally bounded: `Clear`, `Present`, backbuffer readback,
logical-size handling, window-coordinate transforms, level-0 `Texture2D` upload/readback, CPU-raster
`RenderTarget2D`, and `SpriteBatch` drawing work. A newly created or resized backbuffer starts
transparent black, so a zero-draw `Present` never exposes allocator data. Plain `TextureCube` and
`Texture3D` provide bounded CPU transfer storage but cannot be sampled. `RenderTargetCube` is a
six-surface 2D emulation. Unsupported stock 3D effects, cube/volume sampling, and all 3D draw APIs
report one deterministic exception rather than being silently ignored. The table below is the
authoritative compact capability boundary; later sections explain the individual decisions.

## Verified capability boundary

| CNA feature | Current Skia route | Direct/emulation decision | Evidence |
|---|---|---|---|
| Clear, present, resize, five presentation modes, coordinate transforms and backbuffer readback | Verified direct CPU surface plus SDL presentation | Direct raster implementation; SDL uploads the completed image and is not a Skia GPU mode. | [SKIA-7, SKIA-13–17, SKIA-71–72](../plan_skia.md); `Skia_PresentationModes`, `Skia_Resize_Presentation`, `Skia_DisplayScale` |
| `SpriteBatch` draw overloads, sorting, transforms, source rectangles, tint, flips and `SpriteFont` | Verified direct 2D path | Direct `SkCanvas` image operations preserve the shared XNA-shaped batching and font-atlas layout. | [SKIA-31–40](../plan_skia.md); [2D EasyGL registration](skia-2d-easygl-registration.md); [XNA oracle](skia-xna-oracle.md) |
| Point/linear filtering, Anisotropic fallback, and independent Clamp/Wrap/Mirror U/V | Verified bounded 2D path | Point/Linear and tile modes map directly. Level-zero Anisotropic is byte-identical to the documented Linear fallback; the capability stays false and mip-dependent modes reject. | [SKIA-43–46, SKIA-70, SKIA-78–79](../plan_skia.md); `Skia_TextureAddressAxes`, `Skia_Sampler_MipmapFilterPolicy` |
| Blend presets and target-0 colour-write masks | Verified bounded direct/runtime-blender path | `Opaque` and `AlphaBlend` use direct modes; XNA's independent alpha, one custom tuple, and masks use pixel-proven SkSL blenders. Every other tuple rejects before drawing. | [SKIA-47–57, SKIA-108](../plan_skia.md); [XNA oracle](skia-xna-oracle.md); `Skia_BlendMapping_Raster`, `Skia_ColorWrite_Policy` |
| `Texture2D` level-0 upload, partial transfer, readback and NPOT sampling | Verified direct CPU image path | Direct CPU shadow plus Skia image; mip levels above zero reject after `withDefaultMipmaps()` was evaluated and found unable to provide CNA's mutable per-level contract. | [SKIA-22–30, SKIA-70, SKIA-106, SKIA-109](../plan_skia.md); [API contract comparison](skia-api-contract-comparison.md) |
| `RenderTarget2D` colour rendering, readback, sampling and Preserve/Discard | Verified direct level-0 raster target | Direct `SkSurface`; real depth, mip chains and MSAA are unavailable rather than fabricated. | [SKIA-61–75](../plan_skia.md); `Skia_RenderTarget2D_Golden`, `Skia_RenderTarget2D_DepthPolicy`, `Skia_RenderTarget2D_MsaaPolicy` |
| `TextureCube`/`Texture3D` transfers and mip storage | Verified bounded CPU storage | Emulated only as exact CPU face/voxel transfer storage. Sampling was evaluated and rejected because no compatible Skia cube/volume sampler or CNA 3D effect route exists. | [SKIA-80–84, SKIA-101–102](../plan_skia.md); [texture storage policy](skia-texture-storage.md) |
| `RenderTargetCube` face rendering, transfers, Preserve/Discard and generated mips | Verified bounded six-surface 2D emulation | Each face is an independent raster target. Cube sampling, real depth and real MSAA reject explicitly. | [SKIA-85–86](../plan_skia.md); `Skia_RenderTargetCube_Policy` and four shared cube contracts |
| Multiple render targets | Unsupported | Direct support is absent because `SkCanvas` has one colour result; replay emulation was evaluated and rejected because distinct shader outputs for slots 0–3 cannot be reproduced. Empty or one-target plural binding remains supported. | [SKIA-87–88](../plan_skia.md); `Skia_MRT_Rejection` |
| Stock `SpriteEffect` and explicit custom 2D fragment SkSL | Verified bounded path | Stock `SpriteEffect` aliases the default batch path. Only the `CNA_SKIA_SKSL_V1` fragment-only ABI is accepted; arbitrary EasyGL GLSL and vertex/3D effects reject after staged emulation investigation. | [SKIA-89–94](../plan_skia.md); [effects boundary](skia-effects.md) |
| Real depth/stencil, MSAA, wireframe, 3D draws, models and cube/volume sampling | Unsupported | Direct selected-raster support is absent. SkVertices and CPU depth/stencil/geometry/effect prototypes were evaluated; completing them would be a separate software renderer, so production emulation was rejected. | [SKIA-95–103](../plan_skia.md); [3D emulation ADR](skia-3d-emulation-adr.md); `Skia_3D_Refusal` |
| Occlusion queries | Unsupported | Skia raster exposes no samples-passed query. Framebuffer-diff, replay and hidden-GPU emulations were evaluated and rejected as observably incorrect; properties safely return false/zero and Begin/End reject. | [SKIA-104–105](../plan_skia.md); [occlusion-query feasibility](skia-occlusion-query-feasibility.md) |
| Ganesh/Graphite accelerated Skia mode | Not implemented or advertised | The accepted release mode is raster. Pinned-header comparison names Ganesh/OpenGL only as the first future candidate and requires a successor plan to reopen interop/parity/reset gates; no raster-to-GPU emulation is claimed. | [surface-mode ADR](skia-surface-mode-adr.md); [SKIA-5–6, SKIA-107, SKIA-110](../plan_skia.md) |

The complete API-level comparison with EasyGL is maintained in
[`skia-easygl-parity-ledger.md`](skia-easygl-parity-ledger.md). Its 248 rows cover every current
backend/resource method, capability value, and public `GraphicsDevice` declaration; the registered
`Skia_ParityLedger_Audit` test prevents those headers and their classifications from drifting.
The companion [`skia-easygl-test-matrix.md`](skia-easygl-test-matrix.md) classifies all 347 current
EasyGL test registrations, manual tools, golden images, and XNA-oracle scenes by their most
demanding Skia route; `Skia_TestMatrix_Audit` keeps CMake and both asset directories synchronized.
The effect-specific stage, source-language, uniform and texture-child boundary is documented in
[`skia-effects.md`](skia-effects.md); arbitrary `ShaderEffect` source remains unsupported.

## Dependency policy

CNA does not download Skia during CMake configuration. Build Skia outside the CNA source tree and pass the two resulting paths explicitly. The dependency is pinned to the official Skia commit `ebf50520d720a1ce9d842d942d04c6c39c3fbc7b`; it was the `main` revision used for the initial integration spike. Skia is distributed under the BSD-style license in its source checkout; a packaged CNA distribution must include its upstream license/notice before this experimental backend is shipped.

The current link adapter requires every archive emitted by the minimal raster build. A missing or incompatible build therefore fails at CMake configure time rather than silently linking a different Skia installation.

## Reproducible Linux raster build

The complete fresh-checkout developer procedure, prerequisite list, test commands, deliberate
fallback policy, and diagnostics are maintained in
[`skia-developer-build.md`](skia-developer-build.md). The exact artifact recipe is repeated here so
the dependency pin remains visible at the backend boundary.

The following is the supported initial build input. It assumes a C++23-capable Clang, Ninja,
Python 3, and Skia's `gn` tool. Adjust the job count to the host's global limit; the validated
developer procedure uses at most eight workers.

```sh
git clone https://skia.googlesource.com/skia.git /path/to/skia
git -C /path/to/skia checkout ebf50520d720a1ce9d842d942d04c6c39c3fbc7b
cd /path/to/skia
bin/fetch-gn
bin/gn gen /path/to/skia-out/raster --args='is_official_build=true is_debug=false cc="clang" cxx="clang++" skia_use_gl=false skia_enable_ganesh=false skia_use_vulkan=false skia_use_dawn=false skia_enable_graphite=false skia_enable_pdf=false skia_use_freetype=false skia_use_fontconfig=false skia_use_libpng_decode=false skia_use_libjpeg_turbo_decode=false skia_use_libwebp_decode=false skia_use_wuffs=false skia_use_icu=false skia_enable_tools=false'
ninja -C /path/to/skia-out/raster -j8 skia
```

The output directory must contain `libskia.a`, `libskcms.a`, `liballocator_base.a`, `liballocator_core.a`, `liballocator_shim.a`, and `libraw_ptr.a`.

## Configure CNA

```sh
export CMAKE_BUILD_PARALLEL_LEVEL=8
cmake -S . -B build-skia -G Ninja \
  -DCNA_GRAPHICS_BACKEND=SKIA \
  -DCNA_SKIA_ROOT=/path/to/skia \
  -DCNA_SKIA_BUILD_DIR=/path/to/skia-out/raster
cmake --build build-skia --parallel 8
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
| Raster | Release-gated implementation | `SkSurface` readback to SDL streaming texture | Unsupported |
| Ganesh/OpenGL | First future candidate; not implemented | Pinned APIs were compared, but context/framebuffer ownership, wrapping and reset proof require a successor plan | Not claimed |
| Graphite/Vulkan/Metal/Dawn | Deferred; pinned artifact disables them | Device/queue/recorder/swapchain ownership is broader than the first GL candidate; no emulation claimed | Not claimed |

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
`kSrc` with premultiplied bytes and `AlphaBlend` to `kSrcOver` with premultiplied bytes.
`NonPremultiplied` and `Additive` use bounded runtime blenders with straight-labelled input so
their independent XNA alpha equations (`Sa*Sa + Da*(1-Sa)` and `Sa*Sa + Da`) are preserved as
well as their RGB equations. A custom `BlendState` carries factors and equations but no source-byte alpha label, so the
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

The complete storage-to-working-colour and tint/effect rules are fixed by
[`skia-source-alpha-contract.md`](skia-source-alpha-contract.md) and its raw-byte
`Skia_SourceAlpha_Policy` test. Generated blend routes must use that contract rather than inferring
a convention from source pixel values.

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
6. `cmake/Tests/SkiaTests.cmake` registers 99 SKIA-only CTests: nine window-independent raster
   tests, 88 display-required public tests, and two display-free source audits. The raster tests
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
    during `SpriteBatch::Begin`, before any draw. It then renders a non-uniform 2×2 source and
    proves Anisotropic with `MaxAnisotropy` 1, 4, and 9999 is byte-identical to the documented
    Linear fallback while the capability remains false, before reusing the batch with PointClamp.
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
36. `Skia_RenderTarget2D_MsaaPolicy` fixes the raster MSAA contract: backbuffer requests
    0/1/2/4/4096 all write back the actual zero count; target requests 0 and 1 report 0, while
    normalized real or oversized requests 2/3/4/4096 fail before allocation. The capability stays
    false and an exact post-probe backbuffer read proves the device remains usable.
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
    all 248 entries against `docs/skia-easygl-parity-ledger.md`, whose rows record EasyGL behavior
    and tests, the bounded Skia result/plan, final status, and evidence. Missing, stale, duplicate,
    malformed, empty, or unknown-status rows fail the display-free audit instead of allowing the
    parity inventory to drift silently.
60. `Skia_TestMatrix_Audit` inventories all 289 live EasyGL CTest registrations, two manual
    comparison executables, 17 golden PNGs, and 39 XNA-oracle scenes. The 347-entry matrix assigns
    exactly one route to each item: 76 2D-direct, 33 2D-emulation, 213 3D, and 25 device-dependent.
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
63. `Skia_MRT_Rejection` records the SKIA-87 prototype result: replay cannot synthesize distinct
    fragment outputs or per-slot write masks from SkCanvas's single result colour. Otherwise-valid
    two-, three-, and four-target 2D/cube sets therefore reject before changing the active binding,
    viewport, scissor, or any pixel. Shared dimension/duplicate/null/>4 validation keeps its
    precedence, a failed bind does not clear a `DiscardContents` candidate, and an AlphaBlend draw
    afterward reaches only the previously active target. `MultipleRenderTargets` remains false.
    The complete Debug suite passes 97/97 in 12.63 seconds with eight-way test parallelism; this
    focused contract also passes in Release and under AddressSanitizer (`detect_leaks=0`).
64. `docs/skia-effects.md` closes the SKIA-89 audit with a source-stage and parameter/texture
    compatibility table. Untagged backend-specific `ShaderEffect` strings remain invalid because
    runtime SkSL has no user vertex stage and uses named 2D child shaders rather than numeric GLSL
    samplers. `Skia_Effect_Boundary` proves source/clone retention, false capability reporting,
    non-drawing no-backend setters, deterministic custom-Begin refusal, and immediate reuse of the
    same SpriteBatch on its stock path. The focused test passes in Debug, Release, and ASan.
65. `Skia_SpriteEffect_Alias` proves the exact stock effect and its clone are full-target pixel-
    equivalent to a null-effect SpriteBatch across transform, rotation, both flips, tint and point
    sampling. Only exact runtime identity is accepted; a derived SpriteEffect rejects before Begin
    and the batch remains immediately reusable. This does not advertise `CustomEffects`. The
    complete Debug suite passes 99/99 in 12.84 seconds; both effect tests pass in Release and ASan,
    and the three existing SpriteEffect/ShaderEffect unit tests pass under Xvfb.
66. `Skia_SkSL_Effect_Prototype` proves the explicit `CNA_SKIA_SKSL_V1` route compiles and renders
    a real fragment-only runtime shader through reserved `cnaTexture0` and `cnaTint` inputs. The
    adapter retains Skia compiler text and deterministic ABI/size errors, limits source to 64 KiB
    and reflected uniforms to 16 KiB, and never interprets untagged GLSL as SkSL. A failed tagged
    Begin cannot retain the prior shader. General uniforms/additional children were deferred to
    SKIA-92, so this checkpoint did not advertise `CustomEffects`. The complete Debug suite passes 100/100 in 12.99
    seconds; the focused effect passes in Release and ASan (`detect_leaks=0`).
67. `Skia_SkSL_UniformTexture` covers the complete promoted v1 parameter subset: float, int,
    float2/3/4, a deliberately non-symmetric column-major float4x4 element, float arrays and
    float2 arrays all participate in one exact pixel result. Units 1–7 map to named 2D shader
    children through weak lifetime-tracked backends, so a post-bind texture update is visible and
    disposal rejects before Begin rather than retaining stale memory. The test also proves tint,
    source rectangle, transform, PointClamp, clone uniform/binding isolation, missing/type/count/
    null/unit diagnostics, and explicit cube/volume/unsupported-reflection refusal. The complete
    Debug suite passes 101/101 in 16.22 seconds with eight-way parallelism; both SkSL tests pass in
    Release and ASan (`detect_leaks=0`), and 37 related supported ShaderEffect/Texture2D unit tests
    pass under Xvfb.
68. `Skia_Effect_Emulation_Spike` separates three fragment-like operations from their stock 3D
    wrappers. A binary `clipShader` preserves failed alpha-test pixels for all eight compare modes
    at below/equal/above reference values; a transparent-source control proves a one-pass colour
    substitute is not discard under source replacement. A single runtime shader matches the
    dual-texture RGB-doubling formula while its two child images use independent Repeat/Mirror
    addressing, and a single-pass color filter matches two asymmetric swizzle/scale/bias pixels.
    Alpha coverage samples the source twice and pushes one clip, while the other candidates need
    no intermediate target; all final output is quantized once to premultiplied RGBA8. This proves
    reusable composition pieces only, not matrices, triangle coverage, fog, depth or the stock
    effect types. The focused headless raster test passes 8/8 in Debug, Release and under
    ASan/LSan with leak detection enabled. The complete Debug suite passes 102/102 in 16.32 seconds
    with eight-way parallelism (10 Raster, 90 Display and two Audit tests).
69. `Skia_StockEffect_Boundary` closes SKIA-94 without a false promotion. All eight
    AlphaTestEffect compare modes forward the expected 24 threshold decisions, and 16
    DualTextureEffect texture-availability/fog/vertex-colour combinations preserve their CPU
    parameters, yet public `DrawUserPrimitives` consistently rejects at the raster backend's
    public 3D guard before inspecting input or changing a sentinel pixel. SpriteBatch distinguishes
    the missing stock-3D primitive route from the explicit tagged-SkSL custom route, and remains
    reusable after both refusals. The focused test and the earlier generic effect diagnostic test
    pass together; 78 existing AlphaTest/DualTexture property/clone/forwarding tests also pass
    under Xvfb. No Skia golden is registered because no additional stock effect passed the public
    geometry/depth/fog/property gate; `ThreeD` and `CustomEffects` remain false. Both focused
    display tests pass in Debug, Release and under ASan (`detect_leaks=0`). The complete Debug suite
    passes 103/103 in 12.86 seconds with eight-way parallelism (10 Raster, 91 Display, two Audit).
70. `docs/skia-3d-call-effect-matrix.md` closes SKIA-95 with 37 stable renderer requirement IDs.
    The extended test-matrix audit expands all 213 current 3D entries plus 16 exact
    device-dependent depth/MSAA/anisotropy/query cross-cuts and fails unknown entries, undocumented
    features or features with no live evidence. Source verification corrected three stale SKIA-2
    classifications: the misleadingly named textured-quad fixture is SpriteBatch-only, and both
    cube/volume data-contract fixtures are transfer-only. `--dump-3d` emits all 229 mappings.
71. `Skia_ProjectedVertices_Spike` closes SKIA-96 below the public backend. CPU WVP/viewport and
    equal-W PCT interpolation are exact, including RGB `(80,88,88)`. With the same projected
    triangle and clip W `(1,4,1)`, SkVertices samples 88 while EasyGL's perspective-correct GLSL
    contract requires 30. SkVertices also paints an apex outside `z >= -w` and both windings.
    A direct SkVertices 3D bridge is therefore unsound; only a CPU rasterizer that owns clipping,
    perspective varyings, coverage and depth may proceed to SKIA-97. The focused Raster test passes
    in Debug, Release and ASan/LSan, and the complete Debug suite passes 104/104 in 17.07 seconds
    (11 Raster, 91 Display, two Audit). `ThreeD` remains false.
72. `Skia_CpuDepthRaster_Spike` closes SKIA-97 as an internal alternative to the failed direct
    SkVertices route. RGBA8+float depth costs exactly eight bytes/pixel and is capped at 256 MiB;
    LessEqual/write ordering, depth clear, reciprocal-W interpolation, two-target switching and
    an exact same-size whole-image Skia handoff pass. The 640×360 target owns 1,843,200 bytes;
    Release measured 64 µs clear, 193,043 µs for 128 overlapping triangles and 616 µs handoff.
    This already-clipped, opaque scalar spike is not production coverage/state/effect support;
    only the isolated stencil prerequisite advances to SKIA-98. The focused test passes in Debug,
    Release and ASan/LSan; the complete Debug suite passes 105/105 in 13.14 seconds (12 Raster,
    91 Display, two Audit).
73. `Skia_CpuStencil_Spike` closes the isolated SKIA-98 prerequisite without exposing an
    attachment or public draw path. Its exhaustive 8-bit value matrices cover all eight compare
    functions and all eight wrap/saturate/reference operations through discriminating read/write
    masks (4,194,304 cases each). Branch tests prove stencil-fail/depth-fail/pass ordering,
    disabled bypass, narrow masks, the EasyGL two-sided `0x06` versus one-sided `0x04` result, all
    16 `ColorWriteChannels` masks and clear independence. Adding stencil to the candidate
    RGBA8+float-depth layout would cost nine CPU bytes/pixel. The focused test passes in Debug,
    Release and leak-enabled ASan; the complete Debug suite passes 106/106 in 13.28 seconds
    (13 Raster, 91 Display, two Audit). Public depth/stencil and `ThreeD` capabilities remain false.
74. `Skia_CpuGeometry_Spike` closes SKIA-99 below every public buffer and Draw entry point. It
    retains all seven built-in vertex declarations, all 12 formats and 13 usages, bounded upload
    and source-offset behavior, exact 16/32-bit index fetch (including vertex 70000), all five
    primitive types, alternating strip winding, XNA clockwise-as-displayed culling, post-cull
    wire expansion, and raw/four typed/indexed DrawUser input. Invalid layouts, counts, ranges and
    states reject atomically instead of falling back to position-only bytes. Exact line/point pixel
    rules, instancing, clipping/coverage, effects, public resource ownership and mixed ordering are
    intentionally outside this input-assembly result. A test-only temporary-lifetime bug found by
    ASan was fixed; the final focused test passes in Debug, Release and leak-enabled ASan/LSan.
    The complete Debug suite passes 107/107 in 13.21 seconds (14 Raster, 91 Display, two Audit).
    `ThreeD`, depth/stencil and wireframe capability reporting remains false.
75. `Skia_CpuStockEffect_Spike` closes SKIA-100 with one deliberately narrow unlit/no-fog
    textured BasicEffect route. Its four PCT quadrants match every exact
    `EasyGL_BasicEffect_Combined` pixel, an unequal-W triangle proves perspective texture
    interpolation and depth retention, and the completed RGBA8 image reaches Skia without a
    second shading pass. Missing texture, lighting/fog and unclipped input reject atomically.
    `docs/skia-stock-effect-feasibility.md` separately classifies Basic, AlphaTest, DualTexture,
    EnvironmentMap, Skinned, PBR and SkinnedPBR requirements across 21 closed component groups;
    lighting/fog, cube sampling, skinning, PBR, sampler LOD, exact coverage, public ownership and
    mixed ordering remain gaps. The focused test passes in Debug, Release and leak-enabled
    ASan/LSan. The complete Debug suite passes 108/108 in 12.99 seconds (15 Raster, 91 Display,
    two Audit). No 3D-related capability changes.
76. The accepted SKIA-101 ADR keeps the backend 2D-only. It rejects a new embedded CPU renderer,
    hidden Software-backend delegation and a hybrid EasyGL context after deciding all 37 live
    renderer requirements: 16 prototype-only, seven 2D-only, two transfer-only and 12 rejected.
    The isolated SKIA-96--100 spikes remain evidence, not product code; tagged fragment SkSL,
    proven 2D behavior and cube/volume transfer storage retain their narrower contracts.
    `Skia_3DDecision_Audit` now fails any missing, duplicate, stale or unclassified ADR row and
    requires an explicit SKIA-102 consequence. All three audits pass, and the complete Debug suite
    passes 109/109 in 12.92 seconds (15 Raster, 91 Display, three Audit). SKIA-103 is obsolete;
    SKIA-102 subsequently makes public 3D refusal exhaustive and atomic. All
    3D/depth/stencil/wireframe capability values remain false.
77. `Skia_3D_Refusal` closes SKIA-102 with one stable diagnostic prefix and an early backend guard
    used by all public GraphicsDevice draws and ModelMesh. It covers static/dynamic buffers, both
    index widths, raw/typed/indexed/instanced draws, all backend draw and attachment-clear entries,
    active depth/stencil state, wireframe, seven stock effects, cube/volume SkSL binding and query
    Begin/End. A preserve-contents sentinel target remains byte-exact after every failure; disabled
    depth state, masked public clears, CPU cube/volume transfers and SpriteBatch remain functional.
    Occlusion properties safely report false/zero, 32-bit index refusal names its real route, and a
    rejected reference-stencil update no longer corrupts the public cache. The fixture passes
    25/25 in Debug, Release and ASan; the complete Debug Skia suite passes 110/110 in 13.47 seconds
    (15 Raster, 92 Display, three Audit). See `docs/skia-3d-refusal.md`.
78. `Skia_OcclusionQuery_Feasibility` closes SKIA-104 by proving final-pixel comparison cannot
    distinguish full positive coverage from zero coverage: same-colour and destination-preserving
    full-target draws are byte-identical to an out-of-bounds draw, and repeated submissions cannot
    be recovered from the final image. The raster canvas has no depth/samples-passed query; the
    pinned build excludes the Graphite/Vulkan submission statistic. SKIA-105 therefore retains the
    SKIA-102 false/zero property object with throwing Begin/End and false capability. See
    `docs/skia-occlusion-query-feasibility.md`. The seven-check spike passes in Debug, Release and
    leak-enabled ASan/LSan; the complete Debug suite passes 111/111 in 13.74 seconds (16 Raster,
    92 Display, three Audit).
79. SKIA-106 registers eleven exact EasyGL 2D sources under Skia: textured-quad readback; rotation,
    source-rectangle and layer-depth SpriteBatch draws; one SpriteFont glyph; Wrap/Clamp and Mirror
    addressing; Clear overloads; RenderTarget2D readback; disposal; and immediate grow/shrink
    backbuffer readback. Four otherwise-2D sources now express disabled depth through the public
    `DepthStencilState::None` object, retaining their EasyGL behavior without invoking a 3D-only
    backend entry point. The shrink fixture exposed SDL's asynchronous X11 resize: Skia now uses
    `SDL_SyncWindow` before deriving FixedHeightDynamicWidth and recreating the raster backbuffer.
    The full Debug suite passes 122/122 in 16.29 seconds (16 Raster, 103 Display, three Audit); all
    eleven focused tests pass in Release and ASan (`detect_leaks=0`), and all four modified shared
    sources pass in EasyGL. See `docs/skia-2d-easygl-registration.md`.
80. `docs/skia-verification-boundary.md` closes SKIA-107 by mapping ownership, presenter recovery,
    mode policy, alpha conversion, state leakage and capability diagnostics to direct observable
    assertions and the exact defect each catches. `Skia_RasterMode_Coherence` adds the missing
    cross-boundary check: runtime `surface=raster`, the closed capability set and exact translucent
    straight-RGBA8 bytes remain unchanged through an ordered SDL presenter reset. There is no Skia
    GPU mode to compare; adding one must reopen the parity gate. SDL may internally accelerate
    presentation (including with OpenGL), but it receives a completed CPU image and does not change
    the Skia execution-mode claim. The focused test passes in Debug, Release and ASan
    (`detect_leaks=0`); the complete Debug suite passes 123/123 in 15.88 seconds (16 Raster,
    104 Display, three Audit).
81. `Skia_XNA_2D_Oracle` closes SKIA-108 by rendering all nine SpriteBatch-only declarative scenes
    against checked-in PNGs produced by real XNA 4.0. Seven results are byte-exact. The flipped
    and rotated linear-filter scenes allow only RGB delta one, exact alpha, at most 1,591 raw
    differing pixels, and their measured transformed sprite footprints; there is no blanket
    antialias tolerance. The first comparison exposed exact RGB but incorrect opaque output alpha
    in the three sort scenes. `NonPremultiplied` and `Additive` now use bounded runtime blenders
    for XNA's independent alpha equations, with a separate public-API alpha regression in
    `Skia_ColorWrite_Policy`. The full Debug suite passes 124/124 in 17.51 seconds (16 Raster,
    105 Display, three Audit); focused Release and ASan checks pass. See
    `docs/skia-xna-oracle.md`.
82. SKIA-109 compares 13 backend-independent public API contracts by compiling the same fixture
    source for Skia and EasyGL. Eight new Skia registrations cover device validation, partial
    `Texture2D` transfers, surface formats, SpriteFont properties, viewport resize reset, first
    backbuffer read, raster backbuffer acceptance, and 2D/cube render-target pass boundaries; five
    existing same-source pairs cover disposal and exhaustive 2D/cube/volume transfer contracts.
    The inventory found and fixed a stale EasyGL validation assertion that incorrectly expected 16
    null vertex bindings to succeed. Six mixed lifecycle fixtures are now correctly classified 3D
    because their mandatory buffer construction reaches the accepted SKIA-102 refusal; no product
    API was widened or conditionally skipped. All 13 pairs pass in Debug on both backends, the eight
    new Skia registrations pass in Release and ASan (`detect_leaks=0`), and the full Debug Skia
    suite passes 132/132 in 21.66 seconds (16 Raster, 113 Display, three Audit). See
    `docs/skia-api-contract-comparison.md`.
83. SKIA-110 expands `Skia_ResourceBudget` into 64 paired target/snapshot and SDL presenter
    reconstruction cycles. Every recovery reuses the live snapshot, presents and reads an exact
    pixel, preserves all resource counters, emits one ordered reset pair without DeviceLost, and
    releases the target back to baseline. The complete 132-test suite passes under ASan+UBSan;
    all 16 display-free raster tests and the dummy/software-isolated recreation gate pass with
    LSan enabled. The pinned no-RTTI Skia archives require disabling only UBSan's RTTI-dependent
    `vptr` check at the adapter/fixture boundary. Default Xvfb runs report the same non-growing,
    fully `libGLX_mesa.so.0`-rooted 100,956-byte residual for the stress and one-presenter control.
    Ganesh, Graphite, GL, Vulkan and Dawn remain disabled, so no accelerated Skia suite exists to
    run or advertise. See `docs/skia-sanitizer-validation.md`.
84. SKIA-111 synchronizes the backend guide, feature matrix, selection documentation, capability
    comments, and the live 248-entry parity ledger. Only bounded CPU Texture3D transfer storage is
    true beyond the verified 2D path; every GPU/3D family names its refusal or emulation evidence.
85. SKIA-112 supplies the reproducible pinned-dependency build procedure, and SKIA-113 verifies
    fresh native Skia/EasyGL/SDL_Renderer/Software/Vulkan/BGFX builds plus available MinGW/Wine
    D3D11 and D3D12 evidence. See `docs/skia-developer-build.md` and
    `docs/skia-nonskia-build-matrix.md` for exact platform boundaries and commands.
86. The accepted SKIA-5/6 surface ADR selects CPU raster for this release. Ganesh/OpenGL is only
    the first future acceleration candidate and must prove ownership, flush/swap, readback,
    resize, context loss, and CPU/GPU parity through a successor plan. SKIA-76/77 prove the
    zero-sample MSAA clamp/refusal matrix; SKIA-78/79 prove the level-zero Anisotropic-to-Linear
    fallback at three requested qualities while both capabilities remain false.
87. SKIA-114 passes the final release gate: all 114 plan rows, all nine capability values, and all
    direct/bounded/refused feature families are audited. The complete Debug suite passes 133/133
    on real display `:0` in 61.14 seconds (16 Raster, 113 Display, four Audit); the final two policy
    tests also pass in Release and ASan+UBSan. See `docs/skia-release-gate.md`.
88. `Skia_GeneratedBlender_Raster` closes the internal SKIA-120 generator. One process-cached,
    fixed-size runtime program covers all 13 factors and five independent RGB/alpha functions;
    its 46-check independent scalar oracle includes constants, source-alpha saturation,
    factor-independent EasyGL/OpenGL Min/Max, separate alpha, deterministic validation failures,
    and exactly one compilation attempt. Debug, Release, and ASan+UBSan pass. At the SKIA-120
    checkpoint it was not yet a public compatibility claim; SKIA-121's live-state layer is the
    next entry below. See `docs/skia-generated-blender.md`.
89. `Skia_GeneratedBlendState_Policy` closes SKIA-121's live public state layer. Unlisted valid
    factor/function tuples use the fixed generator while the five established mappings retain
    their pixel-proven alpha routes. Baked constants, live red→green→red changes, all sixteen
    post-equation target-0 masks, masked source replacement while disabled, and configured-state
    restoration pass; invalid selectors and unsupported sample masks remain atomic refusals. The
    focused suite passes Debug, Release, and ASan+UBSan, and all 138 tests at that checkpoint pass
    in sequential Xvfb blocks. The next entry closes batch/effect equivalence; exhaustive EasyGL
    pixels and promotion remain SKIA-123/124.
90. `Skia_GeneratedBlend_BatchModes` closes SKIA-122 without duplicating production blend paths.
    Deferred, Immediate, Texture, BackToFront, and FrontToBack all reach the same generated
    `SkPaint` composition for translucent Texture2D, premultiplied RenderTarget2D, and explicit
    identity-SkSL inputs. Two distinct sources exercise real texture sorting. All 19 checks pass,
    stock Opaque remains exact after successful effect use and malformed-effect Begin rejection,
    and the policy is green in Debug, Release, and ASan+UBSan. Exhaustive classification and final
    promotion remain SKIA-123/124.
91. SKIA-123's production-used classifier assigns all 714,025 valid selector tuples to exactly five
    established mappings or 714,020 generated routes; explicit invalid ordinals form the refusal
    class. `Skia_GeneratedBlend_PublicCorpus` independently implements the EasyGL/OpenGL math and
    passes 62/62 real SpriteBatch scenes: every factor in all four positions and every function in
    both equations. Classifier and corpus pass Debug, Release, and ASan+UBSan; the expanded focused
    blend/effect suite passes 17/17 on Xvfb. Public promotion remains SKIA-124.

The original SKIA-1–114 CPU-raster plan is complete. The active successor plan keeps those claims
immutable while expanded features pass their own implementation and promotion gates.
