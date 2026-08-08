# Direct2D mip storage outcome

This document records the final outcome of the earlier storage spike. Direct2D remains the only
application drawing API; no D3D11 texture, shader-resource view, sampler, or hidden 3D pass was
introduced.

## Ordinary Texture2D: accepted

`Direct2DTextureBackend` stores level zero plus independently authored lower mip levels. Every
initialized level has an `ID2D1Bitmap1` and an RGBA shadow. `Texture2D::SetData(level, ...)` creates
or replaces exactly that level after validating its expected dimensions. Recovery reconstructs
all initialized levels from the matching shadows.

SpriteBatch computes a continuous LOD from source size, destination size, rotation, batch
transform, and presentation scale. Point-mip filters choose an initialized authored level;
mip-linear families blend the two bracketing initialized levels. An incomplete chain falls back
toward level zero. Unit tests cover NPOT coordinate mapping and transform mathematics, while the
Wine pixel gate covers authored levels 0/1/2, tint, non-premultiplied sampling, recovery, and an
exact fractional mip-linear blend.

## RenderTarget2D generated mips: rejected

The spike originally proposed one target-capable `ID2D1Bitmap1` per level and a Direct2D linear
downsample on unbind. The D2D-78 7x5 NPOT oracle disproved that proposal: successive bilinear draws
aliased spatially and the final 1x1 level omitted entire source quadrants. That is not an acceptable
generated-mip contract.

The production backend therefore supports only RenderTarget2D level zero. Creation with
`mipMap=true` fails before native allocation, level values other than zero fail deterministically,
and all generated-mip storage/dirty-state code has been removed. Applications that require
mipmapped render targets must use a 3D backend such as D3D11. This deliberate rejection closes the
supported-path defect without pretending nearest-level sampling is mip-linear or retaining an
unreachable flawed algorithm.

Level-zero rendering, sampling after unbind, `SetData`, `GetData`, recovery-to-transparent, and
`CopyFromRenderTarget`/Wine `CopyFromBitmap` readback remain fully exercised by the Direct2D parity
and lifetime gates.
