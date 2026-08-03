# Direct2D 1.1 backend

`CNA_GRAPHICS_BACKEND=DIRECT2D` selects CNA's Windows-only, hardware-accelerated **2D** backend.
It uses an `ID2D1DeviceContext` for all application drawing. A BGRA-capable D3D11 device and a
DXGI flip-model swap chain exist only to host that Direct2D context and present the window; they
do not expose a D3D11 draw pipeline to the game.

Use `CNA_GRAPHICS_BACKEND=D3D11` when an application needs 3D rendering. Direct2D intentionally
does not grow a parallel 3D implementation.

## Current 2D surface

- `Texture2D`, including explicitly uploaded mip levels, SpriteBatch drawing, point/linear/native
  Direct2D anisotropic filtering and normal 2D source rectangles. Minification selects the nearest
  initialized mip from the complete sprite/batch/presentation transform; an uninitialized lower
  level safely falls back to level zero. Mixed min/mag `TextureFilter` values select the requested
  spatial filter for the actual direction. Values requesting mip-linear interpolation retain a
  documented nearest-level policy because the backend stores independent Direct2D bitmaps rather
  than a native mip-chain resource.
- `RenderTarget2D` rendering, sampling after unbind, and CPU readback. Native Direct2D uses
  `CopyFromRenderTarget`; if a runtime exposes the target bitmap but returns `E_NOTIMPL` there,
  the backend uses `CopyFromBitmap` into the same CPU-readable Direct2D bitmap. GPU-only
  tint/flip/Wrap/Mirror decoration applies when a render target is a SpriteBatch source. A target
  created with `mipMap=true` owns target-capable Direct2D bitmaps down to 1x1; lower levels are
  generated GPU-only on unbind and can be sampled or read through `GetData(level, ...)`.
  `SetData(level, ...)` updates the named GPU bitmap as well; partial writes preserve neighboring
  GPU pixels by readback and never leave a stale RenderTarget2D CPU shadow.
- Source rectangles may extend beyond a 2D image. `SamplerState` controls those coordinates just
  as in EasyGL: `Clamp` is clamp-to-edge, while `Wrap` and `Mirror` repeat or reflect. The
  shared image-brush path compensates for Direct2D's clipped negative source origin, so it has the
  same Point result in both axes (including FlipH/FlipV) and the same tested linear
  Clamp/Wrap/Mirror result in both axes for ordinary textures and render targets.
- The standard `Opaque`, `AlphaBlend`, `NonPremultiplied`, and `Additive` SpriteBatch blend modes,
  plus symmetric Add factor tuples that exactly match Direct2D's DestinationOver, Source/Destination
  In/Out/Atop and Xor Porter-Duff modes. Image sprites use Direct2D's explicit composite modes,
  rather than treating the presentation D3D11 device as an application compositing pass. Sprite
  `Color` modulates RGBA, including `Color.A`; render targets remain GPU image sources rather
  than being copied through CPU memory for blending. A decorated Porter-Duff source is materialized
  in a Direct2D command list before the same image composite mode is applied. A transformed
  rectangle geometry layer bounds every non-source-over image composite to the rasterized sprite
  quad, preventing Direct2D's transparent extension to the current clip from changing unrelated
  destination pixels.
- Scissor enable/rectangle and a 2D viewport transform+clip. SpriteBatch coordinates are local to
  the viewport. The scene is rendered into a logical Direct2D target, then `Present` composites it
  into the physical swap-chain bitmap using the selected presentation transform. Consequently
  `GetBackBufferData` remains exact in Letterbox, Overscan, Stretch, NativeBackBuffer and
  FixedHeightDynamicWidth instead of sampling presentation-scaled physical pixels.
- Device recovery for registered 2D resources: ordinary textures are rebuilt from their RGBA CPU
  shadow; render targets are reallocated as transparent, so their former contents are invalid.
- Render targets and sampled textures are device-owned. Public bind calls, the concrete Direct2D
  entry points, and SpriteBatch reject resources from a different `GraphicsDevice` before changing
  native state; disposed targets are likewise rejected. Source/readback rectangle endpoint checks
  use widened arithmetic, including deterministic extreme-coordinate regression coverage.

The routine test trio is `Direct2D_Smoke`, `Direct2D_2DParity`, and `Direct2D_Lifetime`. The
parity test validates partial and full RT readback plus mip readback under Wine through the
`CopyFromBitmap` fallback; the lifetime smoke repeats target switches, readback, recovery and
resize across multiple frames. A Linux cross-build can use the default
`-DCNA_DIRECT2D_TEST_RUNTIME=WINE`, which calls `scripts/run-wine-direct2d.sh` and defaults to
Wine's normal prefix (or `CNA_DIRECT2D_WINEPREFIX`), rather than borrowing D3D11's potentially
Direct2D-incomplete prefix. It can alternatively use the opt-in
`-DCNA_DIRECT2D_TEST_RUNTIME=PROTON`; the latter calls `scripts/run-proton-direct2d.sh`, detects
Steam's Proton Experimental installation (including Debian's `~/.steam/debian-installation`), and
uses a dedicated compat-data directory. WineD3D and Proton's Wine Direct2D do not register the
built-in `ColorMatrix`/`Premultiply` effects and ignore `PLUS`/`BOUNDED_SOURCE_COPY` image composite modes,
so only those decorated and Additive/Opaque pixel probes remain native-Windows branches.

The manual `Windows graphics CI` workflow runs the native MSVC Direct2D suite with no compatibility
skips. It enables both D3D11 and Direct2D debug layers, records Windows/runtime DLL and adapter-driver
versions, captures `ReportLiveDeviceObjects`, transient-resource high-water and lifetime timing, and
runs a second lifetime pass with WARP forced. The resulting `direct2d-native-debug-*` artifact is the
audit record. The same modes are available locally through `CNA_DIRECT2D_DEBUG_LAYER=1`,
`CNA_DIRECT2D_DIAGNOSTICS=1`, and `CNA_DIRECT2D_FORCE_WARP=1`.

## Explicit limits

`GraphicsDevice::SupportsCapability()` returns `true` for `AnisotropicFiltering` and `false` for
`ThreeD`, `DepthStencilBuffer`, `MultiSampleAntiAliasing`, `MultipleRenderTargets`, `WireFrame`,
`OcclusionQuery`, `CustomEffects`, and `Texture3D`. Color-write masks, coverage masks, blend
factor/equation tuples without an exact Direct2D Porter-Duff equivalent,
`GraphicsDevice.BlendFactor`, and `DepthStencilState.StencilEnable` are rejected with named
exceptions rather than silently ignored. `DepthStencilState.DepthBufferEnable`/`WriteEnable`/
`Function` are accepted but always inert: Direct2D never allocates a depth buffer, so unlike
stencil there is no observable behavior to silently get wrong, and `GraphicsDevice`'s constructor
applies `DepthStencilState.Default` (`DepthBufferEnable=true`) unconditionally before any game
code runs.

Accordingly, Direct2D rejects 3D vertex/index-buffer creation and draw calls, stencil testing,
multiple render targets, 3D/cube textures, occlusion queries, custom `Effect` rendering,
wireframe, general blend equations/factors, write masks, and 3D MSAA.
Those are named errors rather than approximate D3D11 fallback passes. This keeps the backend's
behavior honest and prevents a D3D11 presentation detail from being mistaken for 3D support.

`MultiSampleCount` is consequently always `0` for both the swap chain and `RenderTarget2D`.
Direct2D's per-primitive antialiasing is not a sample-count surface, and DXGI multisampling would
need a D3D multisampled backing surface plus a resolve path. The Direct2D backend does not create
that pipeline; applications that require MSAA use `CNA_GRAPHICS_BACKEND=D3D11`.

The remaining 2D compatibility work is tracked in [`plan_direct2d.md`](../plan_direct2d.md).
