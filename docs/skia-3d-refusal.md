# Skia 3D refusal contract

SKIA-102 implements the accepted decision in `skia-3d-emulation-adr.md`: the Skia backend is a
2D raster backend and does not contain a partial public 3D renderer.

## Stable boundary

Every rejected operation throws `std::runtime_error` with this prefix:

```text
Skia (raster 2D) does not support 3D: 
```

`SkiaUnsupported3D.hpp` owns that prefix and the throw helper. `IGraphicsBackend::Ensure3DSupported`
lets common `GraphicsDevice` and `ModelMesh` code ask the selected backend before inspecting bound
buffers, packing DrawUser input, allocating temporary buffers, or iterating model parts. Its default
is a no-op, so other backends retain their established validation and draw behavior. Skia overrides
it with the stable refusal.

Skia also overrides the backend methods that previously inherited a less-specific default:

- 32-bit index creation names `CreateIndexBuffer32`, independently from the 16-bit path;
- effect-aware, indexed and instanced draws each name their actual entry point;
- tagged SkSL cube and volume bindings use the same prefix;
- an unsupported occlusion-query object reports `IsComplete=false` and `PixelCount=0`, while
  `Begin` and `End` throw instead of silently doing nothing.

`DepthStencilState::None` and reference value zero remain accepted because SpriteBatch uses them to
describe the absence of depth/stencil work. Any enabled depth/write/stencil state, nonzero stencil
reference, wireframe state, direct depth toggle, or attachment-bearing backend clear rejects. The
common device constructor no longer applies a native depth state to a backend that reports no
depth/stencil support. Public `GraphicsDevice::Clear` retains the XNA/FNA rule: it masks absent
depth/stencil aspects and performs the requested color clear, if any.

## Atomicity and retained behavior

`Skia_3D_Refusal` binds a preserve-contents raster target containing a non-black sentinel, then
exercises:

- static/dynamic vertex buffers and both index widths;
- public buffered, indexed, instanced, raw and typed DrawUser families;
- all colored/effect-aware/instanced backend draw methods;
- active depth/stencil state, reference stencil, wireframe, and all six attachment clear methods;
- all seven stock 3D effect families and `ModelMesh::Draw`;
- tagged SkSL cube/volume binding and occlusion-query lifecycle.

The target stays byte-exact after every failure and a depth/stencil-only public clear. A combined
public clear changes only color. The same fixture then proves cube/volume CPU SetData/GetData and a
normal SpriteBatch draw still work. Failed reference-stencil application also leaves the common
public state cache unchanged.

This contract does not turn cube/volume transfer storage into shader sampling, does not claim a
depth attachment, and does not promote stock effects. The only true capability in the 3D-adjacent
set remains bounded CPU `Texture3D` storage.
