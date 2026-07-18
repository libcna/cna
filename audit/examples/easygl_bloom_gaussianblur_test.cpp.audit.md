# Audit: examples/easygl_bloom_gaussianblur_test.cpp

## Metadata

- Source file: `examples/easygl_bloom_gaussianblur_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend integration test
- File type: C++ example/integration-test executable (`EasyGLBloomGaussianBlurTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::ShaderEffect::SetUniformVec2Array`/
  `SetUniformFloatArray` (`ShaderEffect.cpp`), `CNA::Internal::Backends::EasyGLEffectBackend::
  SetUniformFloatArray`/`SetUniformVec2Array` (`EasyGLGraphicsBackend.cpp` lines 333-343),
  `Microsoft::Xna::Framework::Graphics::RenderTarget2D`.
- XNA/FNA relevance: exercises array-typed shader uniforms (`float[]`/`Vector2[]` equivalents) driving a real
  `RenderTarget2D`→`RenderTarget2D` pass — real XNA 4.0-shaped capability. The specific shader (`GaussianBlur.fx`)
  and its parameter-computation code (`BloomComponent.cs`'s `ComputeGaussian`/`SetBlurEffectParameters`) are from the
  Microsoft XNA Game Studio **BloomPostprocess** sample, not FNA.
- FNA reference: N/A for the shader/parameter-computation body — `BloomSample_4_0` is not present in the local FNA
  reference tree (confirmed by search). This audit checked internal consistency (the ported `ComputeGaussian`/
  `ComputeBlurParameters` C++ against the file's own transcribed C# comment, and the resulting math against the
  test's own pass/fail thresholds) rather than a byte-for-byte diff against Microsoft's original sample.
- Main related tests: sibling of `easygl_bloom_combine_test.cpp`, `easygl_bloom_extract_test.cpp`,
  `easygl_bloom_pipeline_test.cpp` (which reuses this exact `ComputeGaussian`/`ComputeBlurParameters` pair, see
  Cross-File Observations).

## Purpose

Task 946 shader-conversion proof for `GaussianBlur.fx`'s 15-tap separable-blur pixel shader, specifically targeting
the array-uniform capability (`SetUniformVec2Array`/`SetUniformFloatArray`) this task added to `ShaderEffect`. Also
ports the C#-side parameter computation (`ComputeGaussian`/`ComputeBlurParameters`, lines 98-132) that the real
`BloomComponent.cs` performs on the CPU before uploading to the shader — correctly treated as part of "porting the
shader" per the file's own header (lines 19-22), since the GLSL alone cannot be tested meaningfully without correct
weights/offsets. Correctly placed as an `easygl_`-prefixed integration test.

## Executive Verdict

**Healthy** — a rare case in this shard where the "far pixel must be *exactly* zero" assertion is provably correct
by construction (the shader's own 15 discrete sample taps have a hard maximum reach of 13.5 texels; the far probe
sits 24 texels away), giving this test's negative check unusually strong confidence rather than being an
approximation. `ComputeGaussian`/`ComputeBlurParameters` were independently re-derived and match the file's stated
intent. One real, disclosed test-scope note: the pipeline sibling reuses the same two helper functions almost
verbatim (Cross-File Observations) — a duplication worth consolidating but not a correctness risk.

## Checklist Results

### API / XNA / FNA parity
N/A directly (`ShaderEffect` is `NOXNA`), but `SetUniformVec2Array`/`SetUniformFloatArray` model a real XNA
capability gap FNA/XNA itself doesn't need (XNA's `EffectParameter.SetValue(Vector2[])` does this natively; CNA's
custom-GLSL path needs its own explicit array-uniform API since it isn't going through the `Effect`/`EffectParameter`
XNA-shaped object model this shader bypasses).

### Behavioral correctness
Traced the array-uniform path: `ShaderEffect::SetUniformVec2Array` (`ShaderEffect.cpp:68-71`) forwards to
`EasyGLEffectBackend::SetUniformVec2Array` (`EasyGLGraphicsBackend.cpp:339-343`), which calls
`program_.set_uniform_fv(loc, std::span<const float>(values, count*2), 2)` — the `2` component-count parameter
correctly matches a `vec2[]` upload (as opposed to `SetUniformFloatArray`'s component count of `1`, line 336) —
confirms the flattened `float offsets[15][2]` → `flatOffsets[30]` packing done in `Draw()` (lines 184-189) is the
layout the backend actually expects.

Re-derived `ComputeGaussian`/`ComputeBlurParameters` (lines 98-132) independently against the file's own
transcribed intent (BloomComponent.cs's `ComputeGaussian`/`SetBlurEffectParameters`): the loop correctly places
`weights[0]` at offset `(0,0)`, then for `i` in `[0, SAMPLE_COUNT/2)` computes one symmetric weight pair at
`±sampleOffset = ±(i*2+1.5)` texels, accumulating `totalWeights` and normalizing every weight by it at the end (line
131) — a standard, correctly-implemented separable-Gaussian-tap generator; no off-by-one in the `i*2+1`/`i*2+2`
indexing (verified for `i=0..6`, filling indices `1..14` exactly, matching `kSampleCount=15`).

Verified the "far pixel must be exactly black" claim is a hard guarantee, not an approximation: maximum sample
offset is `i=6 → (6*2+1.5)=13.5` texels. The probe at `x=40` is `40-16=24` texels from the only non-black source
column (`x=16`), which exceeds `13.5` by a comfortable margin (even accounting for bilinear-filter blending across
adjacent texel boundaries, since this test — unlike `easygl_blur_shader_test.cpp` — does not override sampler
state to `PointClamp`, so default linear filtering applies) — so `farOk` (line 216: exact `==0` on R/G/B) is a
sound, not fragile, assertion.

### Logic
Single linear `Draw()` guarded by `done_`: compile shader → render source (line into black `RenderTarget2D`) → blur
horizontally into a second `RenderTarget2D` → read back 3 probe pixels → restore `SetRenderTarget(nullptr)` →
compare → `Exit()`. No branches beyond the `IsEffectValid()` guard.

### Memory/resource lifetime
`sourceRt`/`destRt` are stack-local `RenderTarget2D` values in `Draw()`, both destructed automatically at scope
exit — no explicit `Dispose()` call, relying on the destructor path (acceptable given `GraphicsResource`'s
established RAII pattern elsewhere in the codebase). `device.SetRenderTarget(nullptr)` (line 212) is called before
these destructors run, correctly avoiding the "dispose while bound" `InvalidOperationException` path this same
shard's `easygl_bound_resource_dispose_test.cpp` explicitly verifies exists (`RenderTarget2D::Dispose`,
`RenderTarget2D.cpp:84-94`).

### C++ correctness
`float weights[kSampleCount]`/`float offsets[kSampleCount][2]` are plain stack arrays sized by a `constexpr`
(`kSampleCount=15`) — no dynamic allocation, no bounds-check risk given the fixed, matching size used throughout.
`ComputeBlurParameters` takes its output arrays by reference-to-array (`float (&weights)[kSampleCount]`) — a
correctly-sized, compile-time-checked parameter, not a raw pointer+length pair that could desync.

### Performance
N/A — single-frame test.

### Architecture
Correctly separates "port the shader" (GLSL) from "port the CPU-side parameter math" (`ComputeGaussian`/
`ComputeBlurParameters`), matching the real XNA sample's own separation of `BloomComponent.cs` (CPU) and
`GaussianBlur.fx` (GPU) — an accurate architectural mirroring, not just a shader-only stub.

### Robustness
`IsEffectValid()` guard (lines 160-165) fails loud with a printed message and non-zero exit rather than crashing or
silently mis-rendering.

### Testing
This file is itself a test.

## Detailed Findings

No MEDIUM-or-higher findings for this file in isolation.

### F1 — `ComputeGaussian`/`ComputeBlurParameters` duplicated verbatim in `easygl_bloom_pipeline_test.cpp`

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / duplication
- Location/symbol: `ComputeGaussian` (lines 98-102), `ComputeBlurParameters` (lines 105-132)
- Evidence: `easygl_bloom_pipeline_test.cpp` defines its own `ComputeGaussian` (identical body) and
  `ComputeBlurParameters` (identical math, only the output-array signature differs — this file emits a
  `float[15][2]` that the caller flattens separately, lines 184-189, whereas the pipeline test's version emits an
  already-flattened `float[30]` directly, lines 139-166 of that file).
- Why it matters: purely a maintenance/consistency risk — if the Gaussian-weight formula ever needs a correction
  (e.g. a normalization bug), it would need to be fixed in two places, with no shared header/helper enforcing they
  stay in sync. Not a live defect (both copies are currently correct and consistent).
- FNA/XNA comparison: N/A.
- Related files: `easygl_bloom_pipeline_test.cpp`.
- Suggested future action (not implemented by this audit): hoist both functions into a small shared test-helper
  header if any more bloom-pipeline test files are added.

## Cross-File Observations

- See F1 — `easygl_bloom_pipeline_test.cpp` independently re-implements (not `#include`s) the same blur-parameter
  math this file defines, with a differently-shaped output array; the two were checked against each other during
  this audit and found numerically consistent for the overlapping `dy=0` (horizontal) case.
- This file's `RenderTarget2D`-based blur pass predates (per its own header, "This is the shader that most exercises
  the array-uniform capability") the bug `easygl_bloom_pipeline_test.cpp`'s own header describes finding (Task 1078,
  viewport sizing to the wrong RT) — this file's own `RenderTarget2D`s (`sourceRt`/`destRt`, both `kSize×kSize=64×64`,
  matching the window's own preferred back-buffer size set in the constructor, lines 236-238) would not have
  surfaced that bug, since — per the pipeline test's own diagnosis — the bug only manifests when a bound RT's size
  differs from the window size. Confirms the pipeline test's claim that this file's own tests "happened to use a
  RenderTarget2D the same size as the window" and thus could not have caught that regression on their own.

## Missing or Weak Tests

- No test in this file exercises a *vertical*-only blur pass in isolation (`dy≠0, dx=0`) — only horizontal
  (`ComputeBlurParameters(1.0f/kSize, 0.0f, ...)`, line 183). The vertical direction is exercised by
  `easygl_bloom_pipeline_test.cpp`'s pass 3, so overall shard coverage exists, but not within this file alone.

## Positive Findings

- The "far probe must be exactly pure black" assertion is a genuinely strong, non-fuzzy test design — provably
  correct by the shader's own fixed 15-tap discrete kernel reach, not merely "probably far enough."
- Correctly separates and independently ports both halves (CPU parameter computation + GPU shader) of the real
  sample's own two-file design, rather than only testing the shader with made-up parameters.

## Final Assessment

An accurate, carefully-reasoned test whose one hard guarantee (far-pixel exact black) is genuinely provable from the
shader's own fixed kernel width, and whose CPU-side Gaussian-parameter port was independently re-derived and found
correct. The only note is a maintainability one (duplicated helper math with `easygl_bloom_pipeline_test.cpp`), not
a correctness concern.
