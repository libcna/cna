# Skia generated raster blender

Status: generator and live public state integration complete for SKIA-120/121; promotion remains
SKIA-122–124

`SkiaGeneratedBlender` compiles one generic `SkRuntimeEffect::MakeForBlender` program per process.
Six integer uniforms select the independent RGB/alpha source factor, destination factor and
function, while two float4 uniforms carry the blend constant and output write mask. A state change
instantiates only a small uniform block and `SkBlender`; it does not generate source text or grow
an unbounded cache.

## Selector semantics

- All 13 `Blend` ordinals have a closed branch: One, Zero, source/inverse source colour,
  source/inverse source alpha, destination/inverse destination colour, destination/inverse
  destination alpha, constant/inverse constant, and SourceAlphaSaturation.
- SourceAlphaSaturation is `min(sourceAlpha, 1-destinationAlpha)` for RGB and One for alpha, matching
  the EasyGL/OpenGL factor.
- Add, Subtract and ReverseSubtract operate on independently weighted source/destination terms and
  clamp to the normalized target range.
- EasyGL maps Max and Min to OpenGL `GL_MAX`/`GL_MIN`, for which source/destination factors are
  ignored. The generated route matches that actual backend behavior rather than applying the stale
  weighted wording formerly present in the planning matrix.
- The destination supplied by Skia is premultiplied surface storage. The generator recovers logical
  RGB, evaluates the EasyGL formula, then re-encodes the result for SkSurface. Generated routes use
  SKIA-119's `Premultiplied` label so source/shader components are preserved and every factor is
  applied explicitly.

Invalid factor/function ordinals and non-finite or out-of-range constant channels return null with
a deterministic field-specific error before blender construction. A pinned-Skia compilation
failure is cached with its diagnostic, so repeated calls cannot trigger a compile storm.

## Public state integration

`SkiaGraphicsBackend::ApplyBlendState` keeps the five established preset/runtime mappings on their
pixel-proven routes. Every other valid six-selector tuple constructs the generic blender with the
current `GraphicsDevice.BlendFactor`. `SetBlendFactor` rebuilds that fixed uniform block
transactionally, so a live A→B→A change cannot reuse stale constants or grow a selector cache.

The write-mask uniform is applied after the independent RGB/alpha equation and SkSurface encoding.
Each disabled bit selects the original premultiplied destination component, including the no-write
mask. Disabling blending selects source replacement; a partial mask uses the established Opaque
masked route, and applying another state while disabled refreshes that mask. Re-enabling restores
the configured generated blender. The one-target raster policy still rejects non-default masks for
targets 1–3 and every non-default `MultiSampleMask` before state mutation.

`Skia_GeneratedBlender_Raster` is display-free. Its independent C++ scalar oracle exercises all 13
factor branches in RGB and alpha positions, all five functions independently in RGB and alpha,
constant/inverse constant, SourceAlphaSaturation, reverse subtract and a mixed separate-alpha
tuple. It also proves one compilation attempt and deterministic validation failures.

`Skia_GeneratedBlendState_Policy` is the public SKIA-121 boundary: baked and live constants, all 16
target-0 masks, disabled replacement and re-enable restoration have exact opaque pixels. General
compatibility is not promoted yet. Translucent texture/target/effect equivalence across every batch
mode, the exhaustive selector/public EasyGL corpus, and final documentation remain SKIA-122–124.
