# SKIA-99 CPU geometry/input-assembly spike

SKIA-96 rejected direct post-projection `SkVertices`; SKIA-97 and SKIA-98 then established
test-only CPU colour/depth/stencil building blocks. SKIA-99 asks whether CNA's vertex/index input
and raster-state surface can reach that bridge without silently discarding layouts or ranges.

The implementation remains entirely inside the headless `Skia_CpuGeometry_Spike`. It adds no
public buffer, `Draw*` route, effect, attachment, or capability.

## Bounded model

The prototype owns exact uploaded stream bytes and decoded 32-bit indices. Each buffer is limited
to 256 MiB before allocation. A vertex declaration is accepted only with:

- a stride in `1..4096`;
- `1..32` elements;
- one of all 12 current `VertexElementFormat` values;
- an element byte range wholly inside its stride;
- one of all 13 current `VertexElementUsage` values; and
- a usage index in `0..31`.

These are reversible feasibility bounds, not new public validation rules. An unknown format,
semantic, topology, cull/fill value, or a drawable stream without `POSITION0` rejects before output.
Unlike EasyGL's legacy unknown-stride fallback, no path guesses “first 12 bytes are position” and
ignores the rest.

The format decoder matches the current EasyGL attribute shapes:

- `Single`/`Vector2`/`Vector3`/`Vector4` remain 32-bit float;
- `Color` maps four bytes to `[0,1]`;
- `Byte4` remains four integer-valued bytes;
- `Short2`/`Short4` retain signed values;
- normalized shorts map `-32768` to `-1` and `32767` to `1`; and
- half vectors decode IEEE binary16, including the exponent/mantissa boundary.

Every declared element is materialized in declaration order with its semantic and usage index.
Only `POSITION0` is interpreted by this task; SKIA-100 owns the effect-specific meaning of colour,
normal, tangent, texture, blend-weight/index and other varyings.

## Upload and draw-input results

The test proves:

- all seven canonical stream declarations have exact strides and element counts:
  `16/20/24/32/48/52/68` for VPC, VPT, VPCT, VPNT, tangent, skinned and tangent+skinned;
- all 12 formats decode discriminating values, including `Byte4` integer behavior, short
  normalization and half values;
- all 13 semantics and nonzero usage indices survive decoding;
- vertex and index source offsets select the caller range while the CPU destination begins at
  zero, matching CNA's current overloads;
- `None`, `Discard` and `NoOverwrite` have identical logical byte results; they remain only
  synchronization hints;
- bad stride, width, source range or logical capacity refuses atomically;
- 16-bit indices remain 16-bit and a 32-bit draw fetches vertices `70000..70002` without
  truncation;
- non-indexed `vertexStart`, indexed `startIndex` and `baseVertex` are applied exactly once;
- the decoded index must fit both the caller-declared `minVertexIndex/numVertices` window and the
  uploaded stream, preventing a CPU out-of-bounds fetch; and
- raw custom DrawUser, all four current typed DrawUser stream packers, and raw 16/32-bit indexed
  DrawUser preserve independent vertex/index offsets. Typed objects are explicitly packed through
  `BuiltInVertexStreams`; their C++ vtables/padding never become vertex bytes.

## Primitive and raster-state results

The primitive assembler uses the same element-count formulas as `GraphicsDevice` and covers all
five current topologies:

| Topology | Input elements for `N` primitives | CPU output |
|---|---:|---|
| TriangleList | `3N` | `N` triangles |
| TriangleStrip | `N+2` | `N` triangles; first two indices swap on odd triangles |
| LineList | `2N` | `N` independent lines |
| LineStrip | `N+1` | `N` adjacent lines |
| PointListEXT | `N` | `N` points |

Culling is evaluated after projection in top-row-first screen coordinates, where positive signed
area is clockwise as displayed. This reproduces the project's authoritative XNA oracle:
`CullNone` keeps both orientations, `CullClockwiseFace` removes the clockwise triangle, and
`CullCounterClockwiseFace` removes the counter-clockwise triangle.

Wireframe runs after culling and converts each surviving triangle into its three explicit edge
lines. A two-triangle strip therefore emits six lines; shared edges are deliberately not deduplicated,
matching EasyGL's existing expansion. Line and point topologies pass through unchanged.

## Remaining boundary

This task proves bounded input assembly, not a production 3D renderer:

- the records are not wired to public `VertexBuffer`, `IndexBuffer` or `GraphicsDevice::Draw*`;
- SKIA-97's triangle coverage is still already-clipped, scalar and missing exact shared-edge,
  viewport/scissor and depth-bias rules;
- line endpoint/diamond-exit behavior, point coverage/size and their pixel-level EasyGL oracle are
  not implemented; SKIA-99 proves their assembly without claiming their rasterization;
- destination-offset buffer updates are absent from CNA's current public overloads, while dynamic
  hints have not been performance-characterized;
- multiple vertex streams, instance frequency and instanced draws were rejected by SKIA-101;
- no effect consumes the retained non-position attributes yet; textures, lighting, fog, skinning,
  custom vertex programs and stock-effect permutations belong to SKIA-100/101;
- mixed SpriteBatch/CPU-3D ordering and flush/handoff ownership remain unproven; and
- the prototype favors explicit validation and materialized decoded attributes, so production
  memory/performance would require a separate design under any future replacement ADR.

SKIA-99 therefore passes its layout/primitive/range feasibility gate, while public `ThreeD`,
depth/stencil, wireframe and buffer capabilities remain false. SKIA-100 may reuse the decoded
attribute records only to test complete effect-family requirements; one textured triangle must not
be mistaken for stock-effect support.
