# Direct2D 1.1 backend

`CNA_GRAPHICS_BACKEND=DIRECT2D` selects CNA's Windows-only, hardware-accelerated **2D** backend.
It uses an `ID2D1DeviceContext` for all application drawing. A BGRA-capable D3D11 device and a
DXGI flip-model swap chain exist only to host that Direct2D context and present the window; they
do not expose a D3D11 draw pipeline to the game.

Use `CNA_GRAPHICS_BACKEND=D3D11` when an application needs 3D rendering. Direct2D intentionally
does not grow a parallel 3D implementation.

## Current 2D surface

- `Texture2D`, including explicitly uploaded mip levels, SpriteBatch drawing, point/linear
  filtering and normal 2D source rectangles. Minification selects the nearest initialized mip;
  an uninitialized lower level safely falls back to level zero.
- `RenderTarget2D` rendering, sampling after unbind, and CPU readback. Native Direct2D uses
  `CopyFromRenderTarget`; if a runtime exposes the target bitmap but returns `E_NOTIMPL` there,
  the backend uses `CopyFromBitmap` into the same CPU-readable Direct2D bitmap. GPU-only
  tint/flip/Wrap/Mirror decoration applies when a render target is a SpriteBatch source. A target
  created with `mipMap=true` owns target-capable Direct2D bitmaps down to 1x1; lower levels are
  generated GPU-only on unbind and can be sampled or read through `GetData(level, ...)`.
- Source rectangles may extend beyond a 2D image. `SamplerState` controls those coordinates just
  as in EasyGL: `Clamp` is clamp-to-edge, while `Wrap` and `Mirror` repeat or reflect. The
  shared image-brush path compensates for Direct2D's clipped negative source origin, so it has the
  same Point result in both axes (including FlipH/FlipV) and the same tested linear
  Clamp/Wrap/Mirror result in both axes for ordinary textures and render targets.
- The standard `Opaque`, `AlphaBlend`, `NonPremultiplied`, and `Additive` SpriteBatch blend modes.
  Image sprites use Direct2D's explicit `SOURCE_OVER`, `SOURCE_COPY`, or `PLUS` composition,
  rather than treating the presentation D3D11 device as an application compositing pass. Sprite
  `Color` modulates RGBA, including `Color.A`; render targets remain GPU image sources rather
  than being copied through CPU memory for blending.
- Scissor enable/rectangle and a 2D viewport transform+clip. SpriteBatch coordinates are local to
  the viewport; presentation is applied after that transform.
- Device recovery for registered 2D resources: ordinary textures are rebuilt from their RGBA CPU
  shadow; render targets are reallocated as transparent, so their former contents are invalid.

The routine test pair is `Direct2D_Smoke` and `Direct2D_2DParity`. The latter validates partial
and full RT readback plus mip readback under Wine through the `CopyFromBitmap` fallback. A Linux
cross-build can use the default `-DCNA_DIRECT2D_TEST_RUNTIME=WINE`, or the opt-in
`-DCNA_DIRECT2D_TEST_RUNTIME=PROTON`; the latter calls `scripts/run-proton-direct2d.sh`, detects
Steam's Proton Experimental installation (including Debian's `~/.steam/debian-installation`), and
uses a dedicated compat-data directory. WineD3D and Proton's Wine Direct2D do not register the
built-in `ColorMatrix`/`Premultiply` effects and ignore `PLUS`/`SOURCE_COPY` image composite modes,
so only those decorated and Additive/Opaque pixel probes remain native-Windows branches.

## Explicit limits

`GraphicsDevice::SupportsCapability()` returns `false` for every currently defined optional
capability on this backend: `ThreeD`, `DepthStencilBuffer`, `MultiSampleAntiAliasing`,
`MultipleRenderTargets`, `AnisotropicFiltering`, `WireFrame`, `OcclusionQuery`, `CustomEffects`,
and `Texture3D`. Color-write masks, coverage masks, arbitrary blend factors/equations, and
`GraphicsDevice.BlendFactor` are rejected with named exceptions rather than silently ignored.

Accordingly, Direct2D rejects 3D vertex/index-buffer creation and draw calls, depth/stencil work,
multiple render targets, 3D/cube textures, occlusion queries, custom `Effect` rendering,
anisotropic filtering, wireframe, general blend equations/factors, write masks, and 3D MSAA.
Those are named errors rather than approximate D3D11 fallback passes. This keeps the backend's
behavior honest and prevents a D3D11 presentation detail from being mistaken for 3D support.

`MultiSampleCount` is consequently always `0` for both the swap chain and `RenderTarget2D`.
Direct2D's per-primitive antialiasing is not a sample-count surface, and DXGI multisampling would
need a D3D multisampled backing surface plus a resolve path. The Direct2D backend does not create
that pipeline; applications that require MSAA use `CNA_GRAPHICS_BACKEND=D3D11`.

The remaining 2D compatibility work is tracked in [`plan_direct2d.md`](../plan_direct2d.md).
