# Audit: examples/easygl_environmentmapeffect_worldtransform_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_worldtransform_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect`'s normal transform under
  non-uniform-scale `World` (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EnvironmentMapEffect::OnApply()`'s `WorldInverseTranspose` computation
  (`src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp:330-344`, though this value is stored in an
  `EffectParameter` for content-pipeline fidelity and is **not** what the GPU dispatch path actually uses —
  see Detailed Findings), and the actual GPU-facing normal-matrix computation in
  `EasyGLGraphicsBackend::BindDrawParams` (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3993-4011`).
- FNA reference: `Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`'s `ComputeEnvMapVSOutput`:
  `worldNormal = normalize(mul(vin.Normal, WorldInverseTranspose))` — i.e. FNA transforms normals by the
  inverse-transpose of the 3x3 `World` submatrix, not `World` directly, precisely to remain correct under
  non-uniform scale.
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_WorldTransform`
  (`cmake/Tests/EasyGLTests.cmake:604-606`).

## Purpose

Task 398 test. Documents (lines 2-45) a real, previously-confirmed bug found by auditing CNA's actual GPU
dispatch: `EnvironmentMapEffect::OnApply()` *does* correctly compute `WorldInverseTranspose` on the CPU side
(matching FNA and stored for API/content-pipeline fidelity), but this value was never forwarded into the
actual EasyGL/Bgfx GPU draw path — both backends instead independently (and, for non-uniform scale,
incorrectly) derived their own "normal matrix" from the raw `World` 3x3 submatrix at the shader-binding
layer, bypassing the effect's own correct computation entirely. Vulkan was already correct
(`transpose(inverse(mat3(world)))` computed directly in its vertex shader). Uses a deliberately
non-axis-aligned synthetic normal `(0,1,1)/√2` and an aggressive `20x` non-uniform Z-scale to make the two
candidate formulas (raw-multiply vs. inverse-transpose) produce reflection vectors landing in *opposite*
cube faces, avoiding the seam-blending artifact the file's own comment (lines 27-34) says an earlier, milder
`4x`-scale attempt actually hit in practice.

## Executive Verdict

**Healthy** (post-fix). Independently re-derived the cofactor/adjugate-based normal-matrix computation in
`EasyGLGraphicsBackend.cpp:3997-4011` by hand and confirmed it algebraically equals `transpose(inverse(A))`
for the 3x3 `World` submatrix `A` (worked through the full 3x3 cofactor expansion; see Behavioral
correctness below) — this is the correct formula, matching both FNA and the test's own stated expectation,
and the production code's own comment (lines 3993-3996) matches this audit's independent derivation.

## Checklist Results

### API / XNA / FNA parity
`PASS`. `EnvironmentMapEffect::OnApply()`'s `WorldInverseTranspose` computation
(`EnvironmentMapEffect.cpp:335-341`: `Matrix::Invert(world_, worldTranspose); Matrix::Transpose(worldTranspose,
worldInverseTranspose);` — note the local variable names are swapped from what they compute, i.e.
`worldTranspose` actually holds `Invert(world_)` and `worldInverseTranspose` holds `Transpose(Invert(world_))`;
functionally correct, just a locally confusing naming choice, not a defect) matches FNA's
`EffectHelpers.SetLightingMatrices` computing `Matrix.Invert(Matrix.Transpose(world))`-equivalent semantics
(inverse and transpose commute for `Invert(Transpose(M)) == Transpose(Invert(M))`, so both produce the same
result via different but equivalent operation order).

### Behavioral correctness
`PASS`, verified two ways:
  1. **Formula derivation** (`EasyGLGraphicsBackend.cpp:3997-4011`): re-derived the standard 3x3 cofactor/adjugate
     inverse formula by hand for `A=[[a,b,c],[d,e,f],[g,h,i]]` (columns `(a,d,g)`,`(b,e,h)`,`(c,f,i)` matching
     the column-major `worldColMajor` input) and confirmed the code's `nm[0..8]` array, laid out
     column-major, equals `transpose(inverse(A))` exactly — i.e. `nm`'s column 0 (`nm[0..2]`) is `inverse(A)`'s
     **row** 0, `nm`'s column 1 is `inverse(A)`'s row 1, etc., which is precisely what "transpose" means when
     going from row-major inverse to column-major storage. This is the correct normal-matrix formula.
  2. **Test-scenario re-derivation**: with `World=CreateScale(1,1,20)` (a pure diagonal matrix, so its inverse
     is trivially `diag(1,1,1/20)`, and diagonal matrices are their own transpose), `transpose(inverse(World3x3))
     = diag(1,1,0.05)`. Applying this to the test's synthetic normal `(0, 0.7071, 0.7071)` gives
     `(0, 0.7071, 0.03536)`, which normalizes to `(0, 0.9988, 0.04994)` — matching the file's own stated
     "Correct" result `(0, 0.9989, 0.0499)` (line 37) to within rounding. The buggy raw-`World`-multiply
     alternative instead scales the Z component *up* by `20` (not down by `1/20`), giving `(0,0.7071,14.14)`
     normalizing to `(0,0.0499,0.9988)` — the opposite dominant axis, matching the file's stated "Buggy"
     result (line 39). With the camera looking straight on from `(0,0,3)` (`eyeVector=(0,0,1)`), these two
     transformed normals produce reflection vectors with opposite-signed dominant Z (re-derivable via
     `reflect(-eyeVector,N)` the same way as the `_eyeposition_test.cpp` report), landing in `NegativeZ`
     (yellow) for the correct formula vs. `PositiveZ` (blue) for the buggy one — matching the test's single
     assertion (`Color(255,255,0,255)`, "NegativeZ, yellow", line 186-188) exactly.

### Logic
`PASS`. `EnvironmentMapAmount=1`/`FresnelFactor=0`/`EmissiveColor=Zero` with no lights explicitly disabled
(same implicit-default pattern as several siblings — see Cross-File Observations) correctly isolates the
output to exactly the sampled cube face color, as the header comment (lines 47-50) states.

### Memory/resource lifetime
`PASS`. Standard ownership pattern, consistent with every sibling file.

### C++ correctness
`PASS`. No issues found in the test file itself. (The production-code cofactor-formula implementation this
test validates was reviewed for correctness above, not for its own C++ style, which is out of this file's
scope — that belongs to the `backend-easygl` shard's own audit of `EasyGLGraphicsBackend.cpp`.)

### Maintainability
`PASS`. The header comment's explanation (lines 27-34) of *why* a milder `4x` scale was rejected during
development (it produced a blended yellow/green artifact from the 1x1-texel cube map's seamless bilinear
filtering near a face edge) is a genuinely valuable piece of institutional knowledge that would otherwise be
lost — this is exactly the kind of "why," not "what," comment this project's own `CLAUDE.md` documentation
philosophy asks for in production code, done well here in test code.

### Testing
This is itself a test file. See "Missing or Weak Tests."

### Cross-file consistency
`PASS`. Directly cross-references (by name) the sibling Bgfx backend's equivalent fix ("mirrors the Bgfx
sibling's Task 364/884 fix", line 141-142) and explicitly states the fix's status differs across all 3
backends at the time this bug was found (EasyGL/Bgfx buggy, Vulkan already correct) — a useful cross-backend
consistency note that the `backend-vulkan`/`backend-bgfx` shard audits should independently confirm remain
true today.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Local variable names in `EnvironmentMapEffect::OnApply()`'s `WorldInverseTranspose` block are swapped from what they actually hold

- Severity: LOW
- Confidence: HIGH
- Category: maintainability (flagged here because this file is what led this audit to read that code path
  closely; not a defect in the test file itself)
- Location/symbol: `EnvironmentMapEffect.cpp:337-340`:
  ```cpp
  Matrix worldTranspose;
  Matrix worldInverseTranspose;
  Matrix::Invert(world_, worldTranspose);
  Matrix::Transpose(worldTranspose, worldInverseTranspose);
  ```
- Evidence: the variable named `worldTranspose` is assigned the *inverse* of `world_` (not its transpose),
  and only becomes a transpose after the second call reassigns `worldInverseTranspose` from it. The final
  result (`worldInverseTranspose`) is correctly named and correctly computed (`Transpose(Invert(world_))`,
  matching FNA's own equivalent `SetLightingMatrices` semantics) — this is purely a misleading intermediate
  variable name, not a logic error.
- Why it matters: low severity/purely cosmetic — a future maintainer skimming this block could momentarily
  misread `worldTranspose` as literally `Transpose(world_)`, when it is actually `Invert(world_)`.
- FNA/XNA comparison: N/A (CNA-internal naming choice; FNA's C# equivalent uses no intermediate named
  variables at this granularity).
- Suggested future action (not implemented by this audit): rename `worldTranspose` to something like
  `worldInverse` to match what it actually holds, if this method is touched again for other reasons.

## Cross-File Observations

- Same implicit-default `DirectionalLight0` dependency as several siblings in this batch (see
  `_eyeposition_test.cpp` report's F1) — `EmissiveColor=Zero` alone would not zero the output if
  `DirectionalLight0`'s default diffuse were ever non-black.
- This file and `_eyeposition_test.cpp` are the two files in this batch that most directly stress the
  reflection-vector math (both read back a face color from a 6-distinct-color cube and reason about
  `reflect(-eyeVector,N)`'s dominant axis) — together they form a reasonably complete pair covering "does the
  eye position feed the reflection correctly" and "does the world-transformed normal feed the reflection
  correctly," the two independent inputs to `reflect()`.
- Explicitly documents a real prior test-design failure (the milder `4x`-scale attempt hitting a cube-map
  seam-blending artifact) — a rare and valuable kind of transparency that most sibling files in this batch
  do not include, since most of them got their test geometry right on the first attempt per their own
  comments.

## Missing or Weak Tests

- Only tests one axis of non-uniform scale (`Scale(1,1,20)`, Z-only). Does not test a *combined* non-uniform
  scale across two axes simultaneously (e.g. `Scale(1,4,20)`), which would additionally confirm the
  inverse-transpose formula's off-diagonal cross-terms are handled correctly, not just its diagonal-only
  special case (this test's `World` is itself a pure diagonal matrix, for which the cofactor formula's
  off-diagonal terms are all algebraically zero — a real, if narrow, gap: the general 3x3 cofactor formula
  this test validates is not fully exercised by a diagonal-only input).
- Does not test a *rotated* (non-diagonal) `World` combined with non-uniform scale, which is the scenario
  where a naive raw-multiply and the correct inverse-transpose most visibly diverge in real content (a
  scaled-then-rotated object) — this test's diagonal-only `World` is the simplest non-uniform-scale case,
  sufficient to catch the specific historical bug described, but not a stress test of the general formula.

## Positive Findings

- This audit's independent hand-derivation of the cofactor/adjugate normal-matrix formula in
  `EasyGLGraphicsBackend.cpp` confirmed it is mathematically exactly `transpose(inverse(World3x3))` — the
  correct, FNA-matching formula — giving high confidence this specific historical bug is genuinely fixed,
  not just superficially altered to pass this one test's diagonal-only scenario.
- The test's own documented rejection of an earlier, weaker test design (the `4x`-scale attempt) is a model
  example of iterating a test until it actually discriminates the intended bug, rather than settling for a
  test that merely "looks like" it should catch something.

## Final Assessment

A correct, carefully-engineered regression test for a real, previously-confirmed normal-transform bug. This
audit independently re-derived both the general cofactor-based normal-matrix formula in the EasyGL backend
and the specific test-scenario numbers, and both match. Only a cosmetic variable-naming nit in the
production code (F1, LOW) and two narrow coverage gaps (diagonal-only, no combined rotation+scale case) were
found; no correctness defects.
