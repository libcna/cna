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
- `RenderTarget2D` rendering, sampling after unbind, CPU readback on native Direct2D, and GPU-only
  tint/flip/Wrap/Mirror decoration when a render target is a SpriteBatch source.
- The standard `Opaque`, `AlphaBlend`, `NonPremultiplied`, and `Additive` SpriteBatch blend modes.
  Image sprites use Direct2D's explicit `SOURCE_OVER`, `SOURCE_COPY`, or `PLUS` composition,
  rather than treating the presentation D3D11 device as an application compositing pass.
- Scissor enable/rectangle and a 2D viewport transform+clip. SpriteBatch coordinates are local to
  the viewport; presentation is applied after that transform.
- Device recovery for registered 2D resources: ordinary textures are rebuilt from their RGBA CPU
  shadow; render targets are reallocated as transparent, so their former contents are invalid.

The routine test pair is `Direct2D_Smoke` and `Direct2D_2DParity`. The latter has narrowly scoped
Wine skips for `ID2D1Bitmap::CopyFromRenderTarget` and the built-in `ColorMatrix` effect because
WineD3D does not implement/register them. It also ignores the `PLUS` and `SOURCE_COPY` image
composite modes, so the native Windows run additionally retains the Additive/Opaque pixel probes.

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

The remaining 2D compatibility work is tracked in [`plan_direct2d.md`](../plan_direct2d.md).
