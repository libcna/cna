# Vertex declarations, formats and usages — CNA

> Reconciled 2026-10-31 against the shared `VertexDeclaration` validation and the EasyGL,
> SDLGPU, WebGPU, Software, DirectX9 and FNA3D renderer implementations. This replaces the former
> EasyGL-only stride table and its obsolete legacy-semantic backlog.

This page covers three related but distinct contracts:

1. `VertexDeclaration` decides where values are stored: format, byte offset, usage and usage index.
2. A renderer maps each declared format to its native input representation.
3. The active vertex shader decides which `(usage, usageIndex)` pairs it consumes.

Support for a format or usage therefore does not mean that every stock effect reads it. An unused
declaration element is legal; a shader input with no matching declaration element is rejected.

## Declaration and layout rules

A declared layout is semantic-driven, not selected merely by its stride. Bound vertex buffers
retain their `VertexDeclaration`; multiple streams keep their own strides, offsets and instance
frequencies. If two streams claim the same `(usage, usageIndex)`, CNA follows FNA3D and moves the
later element to the first free index for that usage.

The typed `DrawUserPrimitives` and `DrawUserIndexedPrimitives` overloads infer declarations for the
four built-in vertex structures. An arbitrary plain C++ vertex record must use an overload that
takes its `VertexDeclaration`; CNA cannot reflect a structure from its C++ type. The default empty
declaration is a `CNAEXT` compatibility route for legacy buffers whose packed layout is known by
the renderer. It is not a general public XNA layout-inference mechanism, and unknown packed layouts
are refused.

XNA declarations are validated as follows (the empty `CNAEXT` route above is the sole exception):

- the declaration is non-empty and its stride is positive and divisible by four;
- element offsets are non-negative, four-byte aligned, non-overlapping and wholly inside the
  stride;
- each `(usage, usageIndex)` pair is unique within one declaration;
- device binding limits the stride to 255 bytes, the declaration to 16 elements and each usage
  index to `0..15`;
- `GraphicsProfile::Reach` accepts formats `Single` through `NormalizedShort4`; `HalfVector2` and
  `HalfVector4` require `GraphicsProfile::HiDef`.

The constructors validate layout-neutral rules. The profile and device limits are checked when a
vertex buffer is created or a user draw binds the declaration, matching XNA's device-dependent
validation point.

## `VertexElementFormat`

All six rasterizing renderer families map all twelve XNA formats. The table describes the values a
floating-point shader input observes; omitted components use the graphics API's normal vertex-input
defaults.

| XNA format | Bytes | Input representation |
|------------|------:|----------------------|
| `Single` | 4 | one 32-bit float |
| `Vector2` | 8 | two 32-bit floats |
| `Vector3` | 12 | three 32-bit floats |
| `Vector4` | 16 | four 32-bit floats |
| `Color` | 4 | four unsigned bytes normalized to `[0, 1]` |
| `Byte4` | 4 | four unsigned-byte values, not normalized |
| `Short2` | 4 | two signed 16-bit values, not normalized |
| `Short4` | 8 | four signed 16-bit values, not normalized |
| `NormalizedShort2` | 4 | two signed 16-bit values normalized to `[-1, 1]` |
| `NormalizedShort4` | 8 | four signed 16-bit values normalized to `[-1, 1]` |
| `HalfVector2` | 4 | two IEEE 754 binary16 values; HiDef only |
| `HalfVector4` | 8 | four IEEE 754 binary16 values; HiDef only |

`Color` includes a route-specific channel-order detail. CNA-owned packed stock layouts already
store the byte order their renderer expects. Compiled effects preserve XNA's `Color` semantic:
WebGPU uses `Unorm8x4` because it has no BGRA vertex format, then applies the required channel
swizzle in the linked shader input. DirectX9 uses `D3DDECLTYPE_UBYTE4N` for CNA-native RGBA records
instead of `D3DDECLTYPE_D3DCOLOR`, which would swap red and blue for those records.

### Renderer mapping status

| Renderer identity/family | Format mapping | Usage mapping | Current evidence |
|--------------------------|----------------|---------------|------------------|
| `OPENGLES3`, `OPENGL33`, `WEBGL2` (EasyGL) | all 12 to GL scalar/count/normalization tuples | all 13 in the compiled-effect linker; stock inputs by usage/index | declaration-driven pixel fixture renders all 12 formats; invalid ordinals are refused |
| `SDLGPU` | all 12 to `SDL_GPUVertexElementFormat` | all 13 to MojoShader semantics | exhaustive mapping regression plus compiled-effect declaration matching |
| `WEBGPU` | all 12 to one shared `WGPUVertexFormat` map | all 13 to MojoShader semantics | all pipeline builders use the shared map; compiled-effect tests cover semantic translation and `Color` linking |
| `SOFTWARE` | all 12 decoded by the CPU vertex reader | all 13 translated from MojoShader semantics | the same declaration-driven pixel fixture as EasyGL renders all 12 formats |
| `DIRECTX9` | all 12 to `D3DDECLTYPE_*` | all 13 to `D3DDECLUSAGE_*` and MojoShader semantics | explicit declaration and compiled-effect maps; runtime availability remains Windows/device dependent |
| `FNA3D` | all 12 checked and passed to the matching FNA3D enum | all 13 checked and passed to the matching FNA3D enum | compile-time ordinal assertions, range guards and enum-mapping regression |

`HEADLESS` and `STUB` still apply the shared declaration/profile validation, but they do not
rasterize and therefore have no native vertex-fetch mapping to list here.

The table establishes enum and input-representation support. It does not promise that every native
device accepts every arbitrary combination, that every renderer can infer an undeclared packed
layout, or that a declaration satisfies the active shader. Those are separate layout, capability
and shader-input checks.

## `VertexElementUsage`

CNA accepts the complete XNA usage vocabulary:

| Ordinal | Usage | Typical shader semantic |
|--------:|-------|-------------------------|
| 0 | `Position` | `POSITION` |
| 1 | `Color` | `COLOR` |
| 2 | `TextureCoordinate` | `TEXCOORD` |
| 3 | `Normal` | `NORMAL` |
| 4 | `Binormal` | `BINORMAL` |
| 5 | `Tangent` | `TANGENT` |
| 6 | `BlendIndices` | `BLENDINDICES` |
| 7 | `BlendWeight` | `BLENDWEIGHT` |
| 8 | `Depth` | `DEPTH` |
| 9 | `Fog` | `FOG` |
| 10 | `PointSize` | `PSIZE` / `POINTSIZE` |
| 11 | `Sample` | `SAMPLE` |
| 12 | `TessellateFactor` | `TESSFACTOR` |

Classic stock-effect variants consume only the inputs required by their active shader:
`Position0`; optional `Color0`; `TextureCoordinate0` and, for `DualTextureEffect`,
`TextureCoordinate1`; `Normal0` for lighting/environment/skinning; and `BlendIndices0` plus
`BlendWeight0` for skinning. CNA's PBR extension also consumes `Tangent0` where its selected layout
requires it.

The remaining usages are nevertheless valid declaration semantics and are available to compiled
effects. In particular, `Depth`, `Fog`, `PointSize`, `Sample` and `TessellateFactor` were never a
missing-enum backlog: the compiled-effect paths map them on every renderer that owns such a linker,
and DirectX9/FNA3D expose the corresponding native semantics. Merely declaring one does not add
fixed-function fog, point sprites or tessellation; a vertex shader must request and use that input.

## Regression coverage

The shared unit corpus covers enum ordinals, value semantics, automatic stride calculation,
constructor layout validation, profile/device limits, stock-program input matching and declaration
fidelity. Renderer-specific coverage adds:

- EasyGL and Software pixel readback for every `VertexElementFormat`, reordered declarations,
  stream offsets and multiple streams;
- SDLGPU's exhaustive twelve-format/thirteen-usage native mapping test;
- WebGPU compiled-effect semantic and `Color`-swizzle tests;
- DirectX9 declaration translation fixtures; and
- FNA3D checked enum pass-through tests.

The authoritative implementation points are `VertexDeclaration.cpp`, each renderer's declaration
or compiled-effect mapper, and the tests beside those modules. This page is a status summary, not a
replacement for those executable contracts.
