# Skia occlusion-query feasibility

SKIA-104 concludes that the selected Skia raster renderer cannot implement XNA/FNA occlusion-query
semantics soundly. SKIA-105 therefore retains a deterministic unsupported path: construction is
safe, `IsComplete` is false, `PixelCount` is zero, `Begin`/`End` throw the stable Skia 3D error,
and `GraphicsCapability::OcclusionQuery` remains false.

## Required observation

`OcclusionQuery` surrounds an arbitrary span of draw calls with `Begin` and `End`. `IsComplete`
must poll availability without blocking and `PixelCount` must expose the result of samples that
passed rasterization and depth/stencil testing. EasyGL uses `GL_ANY_SAMPLES_PASSED`, so its
documented result is the weaker but still meaningful zero/one question: did any sample pass?

The current renderer instead owns an immediate CPU raster `SkSurface`/`SkCanvas`, has no depth or
stencil attachment, and presents its completed RGBA8 pixels through SDL. The pinned artifact is
built with Ganesh, Graphite, OpenGL, Vulkan and Dawn disabled. The pinned source does contain
submission-wide Graphite/Vulkan `GpuStats::numOcclusionPassSamples`, but that API is not linked into
this raster artifact and does not add a user Begin/End scope to `SkCanvas`.

## Rejected emulations

### Before/after framebuffer comparison

Counting changed final pixels is not a samples-passed query. `Skia_OcclusionQuery_Feasibility`
demonstrates the contradiction directly on the pinned raster surface:

- a full-target opaque draw of the colour already present covers samples but changes zero pixels;
- a wholly out-of-bounds draw covers no samples and also changes zero pixels;
- destination-preserving blending covers samples while retaining the same bytes;
- one and two full-target submissions produce the same final image and changed-pixel count.

The positive-coverage and zero-coverage snapshots are byte-identical. Framebuffer comparison
therefore cannot implement even EasyGL's boolean `ANY_SAMPLES_PASSED`, let alone a literal count.
Readback would also impose a synchronous full-surface cost at `End`, although immediate completion
alone would not repair the incorrect result.

### Auxiliary mask replay

An auxiliary mask would need every draw between `Begin` and `End` to be intercepted and replayed
with identical geometry coverage, transforms, clipping, antialiasing, alpha-test discard and
depth/stencil ordering, while deliberately ignoring colour/blend output. `SkCanvas` exposes no
post-draw coverage callback. Replaying paint into a second canvas is observably wrong for runtime
shaders, destination-reading blends, target switches and repeated draws, and there is no Skia
raster depth buffer against which an occluded fragment could be rejected.

Supplying the missing coverage and depth machinery is the separate software renderer rejected by
the accepted SKIA-101 ADR. The isolated SKIA-96--100 CPU prototypes remain evidence of that scope;
they are not a public submission bridge and cannot observe ordinary SpriteBatch/SkSL operations.

### Hidden GPU context

Creating Ganesh/Graphite or a raw GL/Vulkan query solely for this type would introduce a second
device/context and resource-ownership model into a deliberately CPU-raster renderer. It also would
not observe CPU `SkCanvas` work. A future accelerated Skia renderer may investigate its own native
submission statistics, but only under a replacement architecture decision and with dedicated
visible/occluded/availability tests; it cannot change this raster renderer's capability claim.

## Resulting contract

Returning a fake completed zero would conflate unsupported work with genuine complete occlusion.
Returning a fake positive value would be worse. The refusal object instead keeps property polling
safe for callers that inspect capability/state, while its mutating lifecycle entries fail
immediately and consistently. `Skia_3D_Refusal` verifies the exact false/zero/throw behavior and
proves subsequent 2D drawing remains usable.

This is an unsupported feature due to the selected Skia raster surface's capabilities and the
absence of an accepted 3D bridge, not an unfinished silent no-op.
