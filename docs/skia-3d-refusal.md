# Skia 3D refusal contract

SKIA-102 implements the accepted decision in `skia-3d-emulation-adr.md`: the Skia renderer is a
2D raster renderer and does not contain a partial public 3D renderer.

## Stable boundary

Every rejected operation throws `std::runtime_error` with this prefix, followed by the name of
the entry point that refused:

```text
Skia (raster 2D) does not support 3D: <operation>
```

`SkiaUnsupported3D.hpp` owns that prefix and the throw helper. `IGraphicsRenderer::Ensure3DSupported`
lets common `GraphicsDevice` and `ModelMesh` code ask the selected renderer before inspecting bound
buffers, packing DrawUser input, allocating temporary buffers, or iterating model parts. Its default
is a no-op, so other renderers retain their established validation and draw behavior. Skia overrides
it with the stable refusal.

Skia also overrides the renderer methods that previously inherited a less-specific default:

- 32-bit index creation names `CreateIndexBuffer32`, independently from the 16-bit path;
- effect-aware, indexed and instanced draws each name their actual entry point;
- an unsupported occlusion-query object reports `IsComplete=false` and `PixelCount=0`, while
  `Begin` and `End` throw instead of silently doing nothing.

Tagged SkSL cube/volume bindings (`SetTexture(1, TextureCube&)` / `SetTexture(1, Texture3D&)`) are
**not** part of this blanket prefix since SKIA-149 (`docs/skia-cube-volume-sampling-contract.md`):
they are a separate, bounded, fragment-only sampling extension, not a rejected 3D route. For an
effect whose own SkSL never calls `cnaSampleCubeEXT`/`cnaSampleVolumeEXT`, binding still rejects,
but with an actionable `std::invalid_argument` naming exactly which call is missing -- not this
prefix. This contract's own fixture (`Skia_3D_Refusal`) proves that specific case still rejects
(see "Atomicity and retained behavior" below); `Skia_CubeVolume_Effect_Binding` and
`Skia_CubeVolume_Sampling_Oracle` prove the positive case, an effect that does declare and bind
them, succeeds and samples real pixels.

`DepthStencilState::None` and reference value zero remain accepted because SpriteBatch uses them to
describe the absence of depth/stencil work. Any enabled depth/write/stencil state, nonzero stencil
reference, wireframe state, direct depth toggle, or attachment-bearing renderer clear rejects. The
common device constructor no longer applies a native depth state to a renderer that reports no
depth/stencil support. Public `GraphicsDevice::Clear` retains the XNA/FNA rule: it masks absent
depth/stencil aspects and performs the requested color clear, if any.

## Atomicity and retained behavior

`Skia_3D_Refusal` binds a preserve-contents raster target containing a non-black sentinel, then
exercises:

- static/dynamic vertex buffers and both index widths;
- public buffered, indexed, instanced, raw and typed DrawUser families;
- all colored/effect-aware/instanced renderer draw methods;
- active depth/stencil state, reference stencil, wireframe, and all six attachment clear methods;
- all seven stock 3D effect families and `ModelMesh::Draw`;
- tagged SkSL cube/volume binding for an effect that never declares the matching children (the one
  case where cube/volume binding still rejects, per above) and occlusion-query lifecycle.

The target stays byte-exact after every failure and a depth/stencil-only public clear. A combined
public clear changes only color. The same fixture then proves cube/volume CPU SetData/GetData and a
normal SpriteBatch draw still work. Failed reference-stencil application also leaves the common
public state cache unchanged.

This contract governs the general/stock 3D draw and effect boundary only. It does not claim a depth
attachment or promote stock effects, and it does not change SKIA-144–151's separate, bounded
`cnaSampleCubeEXT`/`cnaSampleVolumeEXT` fragment-only sampling extension
(`docs/skia-cube-volume-sampling-contract.md`), which is real but narrower than this refusal
boundary -- see above. See `skia-occlusion-query-feasibility.md` for the SKIA-104 proof that final
raster pixels cannot supply a samples-passed query.
