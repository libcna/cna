# SKIA-96 projected SkVertices spike

This experiment asks one narrow question: can CPU-projected
`VertexPositionColorTexture` TriangleList data use `SkVertices` as an exact foundation for the
EasyGL 3D contract? It is a headless internal test and is not connected to `GraphicsDevice`,
buffers, effects or capability reporting.

## Reference contract

The current EasyGL fixed PCT program in
`src/CNA/Internal/Renderers/EasyGL/EasyGLRenderer.cpp`:

- uploads CNA's row-vector `World * View * Projection` matrix in column-major form;
- computes `gl_Position = uWVP * vec4(aPos, 1.0)`;
- exports unqualified `vColor` and `vUV`, which GL interpolates perspective-correctly;
- relies on GL homogeneous clipping before perspective division; and
- applies the configured cull face to GL's counter-clockwise front-face convention.

`SkVertices`, by contrast, accepts `SkPoint` positions, 2D texture coordinates, colours and
optional 16-bit indices. There is no clip-space Z/W, custom varying, depth or cull field.

## Controlled result

`Skia_ProjectedVertices_Spike` uses a 64×64 raster surface and one PCT triangle-list input. The
same CPU function performs CNA's perspective divide and top-left viewport mapping in every case.

| Contract | Observation |
|---|---|
| WVP and viewport | A non-trivial CNA row-vector transform produces clip `(0, 0.25, 0.25, 1)` and raster `(32, 24)` exactly. CPU transformation is mandatory because SkVertices cannot consume the 3D input or clip coordinate. |
| Equal-W interpolation | At pixel centre `(24.5, 24.5)`, red/green/blue vertex interpolation produces Skia `(80, 88, 88)` against the barycentric `(80, 88, 88)` expectation. This bounded affine case is viable. |
| Unequal-W interpolation | Keeping the same raster triangle but using clip W `(1, 4, 1)` produces texture byte 88 in Skia. EasyGL's perspective equation produces 30. The 58-byte discrepancy is the first material mismatch. |
| Homogeneous clipping | An apex with `z=-2,w=1` is outside EasyGL/GL's `z >= -w` plane. Its two intersections lie at raster Y=32, yet un-clipped SkVertices paints white at `(32,16)`. CPU clip-space polygon clipping is mandatory. |
| Winding/culling | Reversing B/C negates signed area, but both SkVertices triangles paint the interior pixel. CPU culling is mandatory and must account for the top-left viewport Y flip. |

The result is deterministic on the pinned CPU-raster Skia and needs no antialiasing tolerance;
the image shader uses nearest Clamp sampling. The equal-W colour comparison allows three byte
units only to avoid treating Skia's documented colour interpolation rounding as a geometry
difference.

## Decision boundary

SkVertices is suitable for an explicitly affine, already-clipped 2D triangle mesh. It is not an
exact bridge for general EasyGL 3D: adding CPU WVP alone loses reciprocal W before rasterization,
and canvas clipping/culling cannot restore it.

SKIA-97 may therefore proceed only as a CPU rasterizer experiment that owns homogeneous clipping,
coverage, perspective varyings and depth, then hands a completed colour image to Skia. It must not
describe that design as "SkVertices 3D". If that independent rasterizer is not bounded and
maintainable. SKIA-101 subsequently rejected full 3D emulation; SKIA-102 retains uniform failures.
