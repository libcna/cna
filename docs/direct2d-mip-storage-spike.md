# Direct2D mip storage spike

This spike defines the 2D-only storage model for the follow-up mip work. It intentionally does
not introduce a D3D11 texture, shader resource view, or D3D11 sampler: Direct2D remains the only
application drawing API.

## Proven level-0 path

`Direct2D_2DParity` already renders into a level-0 `RenderTarget2D`, unbinds it, samples it through
`SpriteBatch`, and reads it through the normal public CPU-readback route. Native Direct2D uses
`CopyFromRenderTarget`; WineD3D returns `E_NOTIMPL` there but implements `CopyFromBitmap`, so the
backend falls back to that current-target bitmap route before mapping the CPU-readable bitmap. That
makes the existing bitmap/target/readback lifetime a suitable level-0 foundation; mips must extend
it rather than replace it.

## Chosen storage model

`Direct2DTextureBackend` owns a vector indexed by mip level. Each entry contains the level's
dimensions, `ID2D1Bitmap1`, and the RGBA shadow used to restore an ordinary `Texture2D`. Level zero
continues to be the primary bitmap. `Texture2D::SetData(level, ...)` already passes an exact level
dimension to `ITextureBackend::UpdatePixelsLevel`, so the Direct2D implementation can allocate or
replace precisely that one bitmap without changing the public API.

For a mipmapped `RenderTarget2D`, the backend owns a separate target-capable
`ID2D1Bitmap1` for every level. Drawing binds only level zero. On unbind, Direct2D will draw each
level into the next smaller target bitmap with its linear interpolation mode; the previous level is
only an image source while the next level is the active target. `GetData(level, ...)` selects the
requested level, temporarily binds it, then uses the same CPU-readable-bitmap copy and BGRA-to-RGBA
conversion as level-zero readback.

## Sampling and lifecycle decisions

- Direct2D does not select a mip level implicitly for `DrawBitmap`. SpriteBatch therefore selects
  the nearest initialized Texture2D level, or the generated RenderTarget2D level, from the
  source-to-destination minification ratio; magnification, and Texture2D minification with no
  initialized lower level, select level zero. Point/linear filtering and Clamp/Wrap/Mirror then
  apply to that selected bitmap.
- A recovered ordinary `Texture2D` rebuilds every allocated level from its matching CPU shadow.
  A recovered `RenderTarget2D` recreates every target bitmap as transparent, exactly as its current
  level-zero recovery contract does; pre-loss rendered content and generated mips are invalid.
- Non-mip textures and render targets retain exactly one bitmap, so D2D-11/D2D-12 cannot regress
  current level-zero sampling, readback, clipping, or recovery behavior.
- The same image-brush/effect path used for decorated render-target sprites will consume the
  selected per-level `ID2D1Bitmap1`; it remains a Direct2D effect graph, never a D3D11 compositor.

`Direct2D_2DParity` covers level 0/1/2 RenderTarget2D sampling after unbind and exact `GetData` of
generated lower levels. The `CopyFromBitmap` fallback keeps those readback checks active under
WineD3D too.
