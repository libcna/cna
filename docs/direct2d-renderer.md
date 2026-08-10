# Direct2D 1.1 renderer

`CNA_GRAPHICS_RENDERER=DIRECT2D` is CNA's Windows-only, SpriteBatch-oriented 2D renderer. It is
graphics-renderer identity 39 and never falls back to another renderer. CMake rejects the selection
deterministically on non-Windows targets.

Application drawing is issued through `ID2D1DeviceContext`. A BGRA-capable D3D11 device and a DXGI
1.2 flip-sequential swap chain exist only to create the Direct2D device/surfaces and present an
HWND. They are not an application-visible D3D11 renderer. The renderer does not use DirectWrite,
WIC, SDL_Renderer, or a hidden 3D/compositing pass. Use `CNA_GRAPHICS_RENDERER=D3D11` for 3D.

## Supported 2D contract

- The only texture, render-target, and backbuffer format is XNA `SurfaceFormat::Color`. Public RGBA
  bytes are converted byte-exactly to Direct2D's native `DXGI_FORMAT_B8G8R8A8_UNORM` storage and
  back on readback. Odd widths, padded row pitches, asymmetric RGB channels, nontrivial alpha,
  repeated updates, and short-pitch rejection have independent tests.
- `Texture2D` supports level zero and explicitly authored mip levels. `SetData`, `GetData`, device
  recovery, and SpriteBatch sampling use the selected authored level. Mip-linear filter families
  interpolate between two initialized authored levels; an incomplete chain falls back toward the
  nearest initialized level instead of sampling undefined data.
- `RenderTarget2D` supports one color target at level zero, rendering, full/partial readback,
  level-zero upload, sampling after unbind, all three `RenderTargetUsage` values, and transparent
  reallocation after recovery. Mipmapped render targets are deliberately unsupported and fail at
  construction. The former generated-mip path was removed because its NPOT downsample omitted
  source quadrants (D2D-78).
- SpriteBatch supports crop, origin, positive or negative source rectangles, horizontal/vertical
  flips, rotation, nonuniform scale, batch transforms, viewport transforms, scissor clipping,
  `Clamp`/`Wrap`/`Mirror`, and all nine `TextureFilter` values within the authored-Texture2D
  contract. Unknown filter, address, presentation, or `SpriteEffects` values and non-finite
  transforms fail before native state changes.
- `Opaque`, `AlphaBlend`, and `NonPremultiplied` are supported. Exact symmetric Porter-Duff tuples
  map to Direct2D image-composite modes. Blend tuples without an exact Direct2D representation,
  channel masks, coverage masks, and non-white blend factors are rejected transactionally, with
  the previously accepted blend remaining active. In particular,
  `BlendState::Additive` is XNA `SourceAlpha/One`; it is not Direct2D `One/One`. Because the renderer
  cannot implement that contract for every source type, `AdditiveBlending` is reported false and
  Additive is rejected consistently rather than approximated.
- A render target used as a SpriteBatch source stays GPU-resident. Native Direct2D built-in
  `ColorMatrix`/`Premultiply` effects provide supported tint/straight-alpha decoration when the
  runtime exposes them. A runtime that reports the effects unregistered receives a named
  `NotSupportedException` for that exact decorated path; unexpected or device-loss HRESULTs are
  never converted into a compatibility skip.
- Sampling the currently bound render target is rejected as a read/write alias. `Present` while a
  render target remains bound is also rejected; callers must unbind explicitly. `SetData` during
  active drawing commits outstanding commands first, so an Immediate-mode draw keeps the old
  bitmap snapshot and the next draw observes the successful update.
- The logical framebuffer implements Letterbox, Overscan, Stretch, NativeBackBuffer, and
  FixedHeightDynamicWidth. Backbuffer readback remains in logical coordinates. Empty-frame
  `Present` still observes an SDL client resize. Final presentation uses linear Direct2D
  interpolation.
- CNA sprite, viewport, and scissor coordinates are logical-framebuffer pixel units, not Windows
  DIPs. The Direct2D context and every bitmap are forced to 96 DPI so one Direct2D unit equals one
  logical target pixel. `NativeBackBuffer` is 1:1 with client pixels; the other presentation modes
  map the logical framebuffer into the client-pixel swap chain. SDL3's Win32 HWND property supplies
  the native target. Deterministic conversion/resize tests run under Wine; physical multi-monitor
  DPI and desktop-capture validation remain an external Windows gate.
- `SpriteSortMode::Immediate` issues each sprite during `Draw`. Present interval zero calls the
  flip-model swap chain with `Present(0, 0)`; CNA does not advertise a tearing capability or add
  `DXGI_PRESENT_ALLOW_TEARING`. Intervals one and two use the corresponding synchronized interval.
- Registered ordinary textures recover from their RGBA shadows. Render targets are recreated
  transparent. Device loss is classified consistently across Direct2D, D3D11, and DXGI HRESULTs;
  public lost/resetting/reset events fire once in order. Unregistered stale resources fail instead
  of being used on a new device generation.

## Capability boundary

The capability query is an exhaustive switch over all 13 current `GraphicsCapability` values:

| Capability | Direct2D |
|---|---:|
| `ThreeD` | false |
| `DepthStencilBuffer` | false |
| `MultiSampleAntiAliasing` | false |
| `MultipleRenderTargets` | false |
| `AnisotropicFiltering` | true |
| `WireFrame` | false |
| `OcclusionQuery` | false |
| `CustomEffects` | false |
| `Texture3D` | false |
| `MultiStreamVertexInput` | false |
| `Instancing` | false |
| `StencilBuffer` | false |
| `AdditiveBlending` | false |

The fixed depth format is `DepthFormat::None`; no real depth or stencil buffer exists. A
multisample request other than the non-multisampled `0`/`1` convention is rejected. Multiple render
targets, vertex/index buffers, ordinary/indexed/instanced 3D draws, cube/volume textures and
targets, queries, wireframe, depth bias, custom effects, and 3D clear/state operations are outside
this renderer. The policy-aware 3D entry points throw by default and return only inert safe handles
when `Unsupported3DGraphicsCallBehavior::WarnAndStub` was explicitly selected.

## Validation gates

The Direct2D CTest label contains four sequential tests:

- `Direct2D_Smoke`: HWND, D3D11 staging readback, and point SpriteBatch draw.
- `Direct2D_2DParity`: public pixel, transform, update, render-target, presentation, recovery,
  capability, and deterministic-rejection oracles.
- `Direct2D_Lifetime`: repeated target switching, readback, recovery, resize, and resource churn.
- `Direct2D_Unit`: the `Direct2D*` GoogleTest subset from `CnaTests`, run through the dedicated
  Direct2D Wine/Proton runner and prefix.

A bounded cross-build and run uses:

```bash
cmake --build cmake-build-direct2d-integration --parallel 2 \
  --target CnaTests cna_test_direct2d_smoke cna_test_direct2d_2d_parity \
  cna_test_direct2d_lifetime
xvfb-run -a ctest --test-dir cmake-build-direct2d-integration -L Direct2D -V
```

`scripts/verify-direct2d-parallel-jobs.sh` rejects missing, nonnumeric, or greater-than-two build
parallelism in the Direct2D workflow/helpers. `CNA_ENABLE_NET=OFF` excludes only tests belonging to
the omitted Net and GamerServices modules; it retains the Direct2D unit subset.

Wine 10.0 is useful evidence for the supported portable/native-API surface but is not physical
Windows. WineD3D does not register Direct2D's built-in ColorMatrix effect and does not implement
the bounded-copy image composite used by the Opaque pixel oracle. The compatibility run therefore
sets only the narrow `CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION` and
`CNA_DIRECT2D_SKIP_ADVANCED_BLEND` branches. Those skips do not claim native coverage.

The manual Windows graphics workflow is prepared to run MSVC with the Direct2D/D3D11 debug layers,
hardware and WARP lifetime passes, diagnostics, and live-object output. Native built-in-effect and
composite pixels, physical display/DPI/presentation capture, adapter-specific behavior, and
debug-layer/live-object acceptance remain external evidence limits until a real x64 Windows run is
recorded. See [`plan_direct2d.md`](../plan_direct2d.md) for that evidence backlog and nonblocking
performance/process work.
