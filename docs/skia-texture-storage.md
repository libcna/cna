# Skia cube and volume CPU-storage contract

This is the SKIA-80 through SKIA-84 storage-versus-sampling decision. The selected Skia backend
is a 2D CPU rasterizer: it has no native cube-map or volume-image primitive and no supported CNA
shader path that could sample one. It can nevertheless implement the independently observable
`TextureCube` and `Texture3D` transfer APIs exactly with bounded RGBA8 CPU storage. The backend
does so without advertising a 3D renderer or silently pretending that the storage is sampleable.

## Public behavior matrix

| Resource/operation | Skia raster result | Evidence or boundary |
|---|---|---|
| `TextureCube(..., Color)` | Six zero-initialized CPU faces, with the declared mip chain | `Skia_TextureStorage_Policy` and `Skia_TextureCube_*` |
| Cube `SetData`/`GetData` | Exact face, mip, rectangle, source/destination range, top-row-first RGBA8 | Shared SetData/GetData contract tests plus face/partial/mip tests |
| `Content.Load<TextureCube>` from DDS | Decodes and stores all six faces exactly as the shared loader supplies them | `Skia_TextureCube_ContentLoad` |
| Cube shader/effect sampling | Unsupported | No `BindGL`/native cube handle and no supported Skia custom/3D effect consumer |
| `Texture3D(..., Color)` | Zero-initialized CPU voxels, with width/height-driven public mip count and halved depth per level | `Skia_TextureStorage_Policy` and `Skia_Texture3D_*` |
| Volume `SetData`/`GetData` | Exact mip/sub-volume, top-row-first rows and front-to-back slices | Shared SetData/GetData contract tests plus slice/box/mip tests |
| Volume shader/effect sampling | Unsupported | No `BindGL`/native volume handle and no supported Skia custom/3D effect consumer |
| `GraphicsCapability::Texture3D` | `true` | This capability's public contract is real persistent transfer/readback storage, which is implemented |
| `GraphicsCapability::ThreeD` / `CustomEffects` | `false` | Storage does not imply vertex processing, depth, stock effects, or cube/volume sampling |
| `RenderTargetCube` draw/transfer | Six independent raster faces at every declared mip; exact draw-to-face, `SetData`, `GetData`, Preserve/Discard, and generated mips | `Skia_RenderTargetCube_Policy` plus four shared public cube-target contracts |
| `RenderTargetCube` sampling | Unsupported | The raster faces expose no native cube handle and CNA has no supported Skia cube-sampling effect |
| `RenderTargetCube` depth/MSAA | No real depth attachment; requested MSAA is truthfully clamped to zero | Public depth metadata remains intact; `HasRealDepthBuffer` is false and policy/contracts verify single-sample output |

Assigning either resource to a public texture collection is harmless state storage, not a sampling
claim. Every currently supported Skia draw path consumes `Texture2D`/`SkiaImageSource`; every
custom-effect and 3D entry remains rejected before it could interpret a cube or volume binding.
The CPU storage backends intentionally leave `BindGL()` at its no-op interface default and expose
no alternative native sampling handle.

## Allocation and transfer limits

- Only the existing project-wide `SurfaceFormat::Color` path is accepted: tightly packed RGBA8,
  four bytes per texel/voxel.
- Every axis must be positive and at most 16384, matching the public texture-axis ceiling.
- One cube or volume resource may own at most 256 MiB across all faces and mip levels. Size
  multiplication and level summation use checked `size_t` arithmetic before each allocation.
- Cube accounting is `sum(levelWidth * levelHeight * 4 * 6)`. Volume accounting is
  `sum(levelWidth * levelHeight * levelDepth * 4)`. `Texture3D` level count follows CNA/FNA's
  width/height rule; allocated depth still halves toward one at each existing level.
- Allocated content is zero initialized. A rejected face, level, region, null pointer, short byte
  range, overflow, or budget violation reports failure before copying and cannot partially alter
  the resource or destination.
- Backend transfers copy directly row by row (and slice by slice for volumes), with no second
  full-resource staging allocation. The shared `Color` conversion layer owns one tightly packed
  scratch buffer exactly the requested region's RGBA8 size, so its additional transfer storage is
  also bounded by 256 MiB and is released when the call returns.
- `SkiaResourceStats` separately counts live cube/volume backends and their combined exact CPU
  bytes. Destruction, including constructor unwinding after a rejected later mip, releases all
  vectors; the direct policy test verifies counters return to zero.

## RenderTargetCube 2D emulation boundary

SKIA-85/86 adds a separate target implementation rather than pretending the plain byte-vector
cube can be drawn to. Each face and declared mip owns a stable CPU `SkSurface`; selecting a face
routes the existing `Clear` and SpriteBatch canvas path to its level-zero surface. Leaving a dirty
face regenerates only that face's remaining mip levels with a deterministic 2x2 RGBA8 box average.
Every surface has one equally sized canonical straight-RGBA shadow because premultiplied SkSurface
storage cannot byte-round-trip all translucent 8-bit uploads. `SetData` updates both stores and
`GetData` returns the canonical bytes; a Clear/SpriteBatch write synchronizes the shadow from the
rendered surface before mip generation. Both copies are included in the same checked 16384-axis
and 256 MiB per-resource limit, and synchronization reuses them without a retained or transient
full-face allocation. Live target count and exact combined storage bytes are tracked separately.

The public singular and normalized plural binding paths are pixel-equivalent. An empty binding set
restores the backbuffer, one cube-face binding works, and two or more attachments still throw
before drawing because SkCanvas has no multi-output draw. PreserveContents retains the selected
face; the common GraphicsDevice DiscardContents path clears only that face to its documented black
discard colour. Uploads/readback cover every face, mip, and rectangle without a row transform.

This is not a cube sampler or a depth/MSAA implementation. `GetGLHandle()` remains zero, no Skia
effect consumes the six surfaces as a cube, `HasRealDepthBuffer()` is false for every requested
depth format, and every nonzero multisample request applies and reports zero samples. Those limits
are deliberate observable results rather than unimplemented calls silently succeeding.

Plain cube/volume textures remain storage-only, while the cube target is draw/transfer-only.
Enabling a future effect or 3D sampler requires its own pixel evidence and capability decision; it
must not infer support merely because CPU readback or draw-to-face works.
