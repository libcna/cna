# SKIA-97 CPU depth/raster handoff spike

SKIA-96 showed that post-projection SkVertices cannot carry reciprocal W, homogeneous clipping
or culling. This follow-up tests the only bounded alternative still compatible with a raster Skia
backend: own the 3D colour/depth result in CPU memory and give Skia a completed 2D image.

The implementation remains entirely inside `Skia_CpuDepthRaster_Spike`. It is not a public
graphics backend, does not change capability reporting and is not reusable production code.

## Prototype boundary

Each `CpuDepthTarget` owns:

- one tightly packed, top-row-first RGBA8 colour array;
- one `float` depth value per pixel;
- dimensions in `1..16384`; and
- at most 256 MiB for the two arrays combined, checked before allocation.

The exact persistent cost is eight bytes per pixel. The measured 640×360 target therefore owns
1,843,200 bytes; a 1920×1080 target would own 16,588,800 bytes before textures, stencil, temporary
clip polygons or Skia's own surface storage. A colour+depth target and its matching Skia surface
necessarily duplicate the colour plane during this handoff design.

The scalar triangle loop samples pixel centres, interpolates post-divide depth affinely, applies
LessEqual with depth writes, and retains reciprocal W for perspective-correct RGBA varyings. It
accepts only finite, positive reciprocal W. Input is already transformed, culled and clipped; the
prototype intentionally does not conceal the work still required by SKIA-98–100.

`HandoffTo` requires identical target dimensions and performs one whole-target
`SkiaSurface::WritePixels` copy. A mismatch returns before touching the destination. The spike uses
opaque pixels, so straight/premultiplied alpha conversion does not blur the depth/raster result.

## Semantic results

- Far-then-near and near-then-far draws both leave the near green triangle and depth 0.2 at the
  sample. The farther second draw reports that it wrote no pixels.
- Clearing colour blue with depth 0.1 blocks a depth-0.8 triangle. Clearing depth alone to 1.0
  permits that triangle without replacing the blue colour outside its coverage.
- Retaining reciprocal W `(1, 0.25, 1)` recovers byte 30 for SKIA-96's discriminating pixel,
  matching EasyGL's perspective result instead of SkVertices' affine byte 88.
- Binding A, then a differently sized B, then A preserves independent colour and depth. Exact
  opaque pixels survive two separate Skia handoffs.
- A 1×1 destination rejects a 32×32 handoff and keeps its magenta sentinel byte-exactly.

## Measurements

The workload clears 640×360, draws 128 overlapping 600×320 right triangles with progressively
nearer depth (12,288,000 covered fragment candidates), and copies the final colour plane to a
matching raster `SkiaSurface`. It is single-threaded; the eight-core limit applies only to builds
and concurrent CTests.

| Build | Clear | 128 triangles | RGBA8 handoff |
|---|---:|---:|---:|
| Debug | 6,452 µs | 1,244,666 µs | 537 µs |
| Release | 64 µs | 193,043 µs | 616 µs |
| ASan+LSan | 32,178 µs | 2,206,201 µs | 534 µs |

Release scalar throughput for the covered candidates is about 63.7 million fragments/second.
These are feasibility measurements from this machine, not performance promises or CI thresholds;
the test asserts only the exact storage size, successful work/handoff and a ten-second safety
ceiling.

## Decision for the next task

The depth/target/handoff prerequisite passes, so SKIA-98 may test stencil in the same isolated CPU
model. This does not yet show that a complete 3D emulator is maintainable:

- edge ownership is only an epsilon-inclusive single-triangle rule, not the complete EasyGL
  top-left/shared-edge contract;
- depth format quantization, depth bias, clipping, culling, scissor and viewport transitions are
  absent;
- texture sampling, alpha/blend/colour-write behavior, every stock effect and arbitrary vertex
  programs remain absent;
- mixed SpriteBatch/3D ordering would require a defined flush/handoff on every transition; and
- every target duplicates its colour bytes until a production ownership design exists.

SKIA-98 was therefore evidence gathering, not permission to expose depth or `ThreeD`. SKIA-101
subsequently rejected the full renderer after judging the complete SKIA-95 matrix; this buffer
stays test-only.
