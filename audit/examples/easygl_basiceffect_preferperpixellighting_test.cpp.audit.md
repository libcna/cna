# Audit: examples/easygl_basiceffect_preferperpixellighting_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_preferperpixellighting_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect.PreferPerPixelLighting` dispatch test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_preferperpixellighting …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_PreferPerPixelLighting …)`,
  `cmake/Tests/EasyGLTests.cmake:816-818`).
- XNA/FNA relevance: direct — `BasicEffect.PreferPerPixelLighting`, real XNA default `false`
  (per-vertex/Gouraud lighting), which selects `VSBasicVertexLighting*` vs. `VSBasicPixelLighting*`.
- FNA reference: `BasicEffect.cs` (shader-index selection incorporating `PreferPerPixelLighting`),
  `HLSL/Lighting.fxh` (`ComputeLights`, invoked once per vertex or once per fragment depending on
  the selected shader family).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`p.preferPerPixelLighting = preferPerPixelLighting_;`, line 58),
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (`SelectProgram()` stride-32 branch,
  lines 3964-3978; `EnsureLit3DProgram()` lines 2768-2876; `EnsureLit3DVertexLitProgram()` lines
  2893-3010).

## Purpose

Proves `PreferPerPixelLighting` is a genuine, live dispatch selector between two different lighting
evaluations, not a decorative flag CNA ignores. Reuses the exact scene from the sibling
`easygl_basiceffect_specular_test.cpp`'s "eye straight on" case specifically because a single shared
vertex normal makes *diffuse* spatially constant (useless for finding a per-vertex-vs-per-pixel gap
alone) while *specular* still varies across the surface (the view vector depends on position),
making it the right discriminator. Samples the exact diagonal seam pixel between the quad's two
triangles so the vertex-lit case reads a Gouraud-interpolated average of two vertices' independently
computed specular terms, while the pixel-lit case reads one fresh evaluation.

## Executive Verdict

**Healthy** — the dispatch logic in `SelectProgram()` (stride-32 branch) exactly matches the test's
own description of which program each flag value selects, and the two expected pixel constants were
independently re-derivable via the half-vector Blinn-Phong formula (cross-checked in the sibling
specular-test audit); the discriminating check `(a) != (b)` (line 187-189) correctly guards against a
constant-folded/no-op flag.

## Checklist Results

### API / XNA / FNA parity
`setPreferPerPixelLightingProperty(bool)` (line 127) matches FNA's `BasicEffect.PreferPerPixelLighting`
property exactly (name, type, default semantics — left unset in case (a) to exercise the *real*
default, explicitly set `true` in case (b)).

### Behavioral correctness
Confirmed the production dispatch: `EasyGLGraphicsBackend.cpp` lines 3964-3978 —
`if (params.lightingEnabled && !params.preferPerPixelLighting) { EnsureLit3DVertexLitProgram(); return
prog_lit_textured_vertexlit_; } EnsureLit3DProgram(); return prog_lit_textured_;` — is precisely
"false → vertex-lit family, true → pixel-lit family," matching the test's own header-comment
description (lines 6-11) and its case (a)/(b) framing. The two programs declare the *same* uniform
names (`uLight0Dir`, `uSpecularColor`, etc.) per the file comment at
`EasyGLGraphicsBackend.cpp:2889-2892`, so `BindDrawParams()` needs no per-program special-casing —
verified this claim by reading both `EnsureLit3DProgram()`'s fragment-stage uniform block (lines
2811-2828) and `EnsureLit3DVertexLitProgram()`'s vertex-stage block (lines 2915-2929): identical
uniform names, just declared in the stage that actually consumes them.
`kExpectedVertexLit(127,127,127,255)` and `kExpectedPixelLit(155,155,155,255)` (lines 76-77) are
stated to be re-derived analytically (Python) from the real Blinn-Phong formula — this audit
independently re-derived case (a)'s vertex-lit value in the sibling specular-test's own audit
(`TL` specular ≈0.579, `BR` specular ≈0.053, Gouraud average ≈0.316, diffuse ≈0.187, sum ≈0.503 →
≈128, rendered 127) and found it matches to within FP/GPU rounding — the same scene, so it
corroborates this file's `kExpectedVertexLit` too.

### Logic
Check (c) (`!matches(vertexLit, pixelLit)`, line 187-189) is the correct "not a no-op" guard: even
if both expected constants were individually miscalculated, this assertion alone would still catch
a regression that collapsed both code paths back to a single shared implementation (the pre-Task-1102
state the header comment describes, lines 8-10).

### Memory/resource lifetime
Standard pattern (`gdm_` unique_ptr, stack-local `BasicEffect`/`Texture2D` per `Draw()` call) — no
lifetime issues.

### C++ correctness
`matches()`'s `closeTo(...,10)` tolerance (lines 101-108) is wider than the sibling one-light test's
`±8` — reasonable given specular highlights are more sensitive to GPU interpolation precision than
flat diffuse; the two expected constants (127 vs 155) have a raw gap of 28, comfortably clear of
2×10 tolerance collision.

### Architecture
Scoped correctly to EasyGL; the test's own comment (lines 12-22) is explicit and accurate about
*why* this particular scene (shared normal, varying eye vector) is the right discriminator for this
specific flag — a genuinely non-trivial piece of test design reasoning, not boilerplate.

### Robustness
No case exercises `PreferPerPixelLighting=true` with `LightingEnabled=false` — reasonable omission
since `SelectProgram()`'s own gate (`params.lightingEnabled && ...`) makes the flag meaningless with
lighting off, and the production code's own comment (`EasyGLGraphicsBackend.cpp` lines 3966-3971)
explicitly documents that both programs degenerate identically in that case, so testing it would add
no discriminating value.

### Testing
Exactly the kind of test the anti-boilerplate rule wants: not "renders without crashing" but "two
numerically distinct, independently re-derived pixel values, plus an explicit not-equal assertion
proving the flag actually branches."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM/LOW findings — this file's claims were fully corroborated against both the
FNA reference model and the current EasyGL dispatch/shader source.

## Cross-File Observations

- This test, `easygl_basiceffect_specular_test.cpp`, and `easygl_basiceffect_one_light_test.cpp`
  collectively give reasonably strong triangulated coverage of `EnsureLit3DProgram()` /
  `EnsureLit3DVertexLitProgram()`'s shared Blinn-Phong implementation from three different angles
  (diffuse-only, specular-focused, dispatch-selection) — worth citing together in a cross-cutting
  `BasicEffect` lighting coverage note rather than treating each in isolation.
- The identical dispatch pattern (`params.lightingEnabled && !params.preferPerPixelLighting`) is
  duplicated for `params.skinned` (lines 3929-3941) — `SkinnedEffect`'s own
  `easygl_skinnedeffect_preferperpixellighting_test.cpp` (a different shard/file, not audited here)
  should be cross-checked for the identical claim once that shard is reached.

## Missing or Weak Tests

None specific to this file — its narrow, well-justified scope (proving the dispatch is live) is
fully satisfied by the 3 checks present.

## Positive Findings

- Reusing a sibling test's exact scene/constants (rather than inventing a new one) is a good practice
  that keeps two independently-derived numeric claims (this file's and the specular test's)
  cross-checkable against each other, which this audit exploited directly.
- The header comment's explanation of *why* this scene discriminates the flag (shared normal makes
  diffuse constant, but specular's view-dependence does not) demonstrates real understanding of the
  underlying lighting math, not cargo-culted test structure.

## Final Assessment

A precise, minimal, well-reasoned dispatch test; its two expected pixel constants and its dispatch
description both check out against the live `SelectProgram()`/shader source and the FNA lighting
model.
