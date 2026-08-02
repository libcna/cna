# Skia generated raster blender

Status: internal generator complete for SKIA-120; public promotion remains SKIA-121–124

`SkiaGeneratedBlender` compiles one generic `SkRuntimeEffect::MakeForBlender` program per process.
Six integer uniforms select the independent RGB/alpha source factor, destination factor and
function, while one float4 uniform carries the blend constant. A state change instantiates only a
small uniform block and `SkBlender`; it does not generate source text or grow an unbounded cache.

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

`Skia_GeneratedBlender_Raster` is display-free. Its independent C++ scalar oracle exercises all 13
factor branches in RGB and alpha positions, all five functions independently in RGB and alpha,
constant/inverse constant, SourceAlphaSaturation, reverse subtract and a mixed separate-alpha
tuple. It also proves one compilation attempt and deterministic validation failures. This task
does not yet route arbitrary public `BlendState` values through the generator: live BlendFactor,
BlendEnabled/write-mask composition, batch modes, target/readback limitations and exhaustive
public/EasyGL pixels are the explicit SKIA-121–124 gates.
