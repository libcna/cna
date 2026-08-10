# Skia generated raster blender

Status: promoted arbitrary raster blend surface complete for SKIA-120–124

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
  ignored. The generated route matches that actual renderer behavior rather than applying the stale
  weighted wording formerly present in the planning matrix.
- The destination supplied by Skia is premultiplied surface storage. The generator recovers logical
  RGB, evaluates the EasyGL formula, then re-encodes the result for SkSurface. Generated routes use
  SKIA-119's `Premultiplied` label so source/shader components are preserved and every factor is
  applied explicitly.

Invalid factor/function ordinals and non-finite or out-of-range constant channels return null with
a deterministic field-specific error before blender construction. A pinned-Skia compilation
failure is cached with its diagnostic, so repeated calls cannot trigger a compile storm.

## Public state integration

`SkiaRenderer::ApplyBlendState` keeps the five established preset/runtime mappings on their
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
target-0 masks, disabled replacement and re-enable restoration have exact opaque pixels.

`Skia_GeneratedBlend_BatchModes` closes SKIA-122. The common paint path produces one equivalent
translucent pixel from Texture2D components, an equivalent premultiplied RenderTarget2D snapshot,
and an identity explicit SkSL effect in Deferred, Immediate, Texture, BackToFront, and FrontToBack.
Texture mode sorts two distinct sources, and stock Opaque remains exact after successful effect use
or malformed-effect rejection.

`ClassifySkiaBlendSelectors` is used by both `ApplyBlendState` and the exhaustive display-free
audit. It assigns all 714,025 valid tuples to five established routes or 714,020 generic routes;
out-of-range raw values are Invalid. `Skia_GeneratedBlend_PublicCorpus` then renders the minimized
62-scene public set against an independent EasyGL/OpenGL scalar oracle, covering every factor in
all four positions and every function in both equations.

## Promoted boundary

SKIA-124 promotes exactly that selector surface for 2D raster draws: every tuple formed from the
13 public `Blend` values and five public `BlendFunction` values is accepted, including independent
RGB/alpha equations and live blend constants. All 16 target-0 colour-write masks compose with
those tuples. The five established mappings retain their specialized routes; the remaining
714,020 tuples use the generated blender.

This is not a claim for invalid raw enum values, target-1/2/3 masks, a non-default multisample
mask, MRT, MSAA, arbitrary EasyGL GLSL, or 3D/cube/volume sampling. Those inputs still refuse
before drawing. The release audit pins this distinction to the startup diagnostic, parity ledger,
and feature matrix so a future edit cannot silently widen or narrow the advertised surface.
