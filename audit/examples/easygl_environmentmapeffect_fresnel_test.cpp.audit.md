# Audit: examples/easygl_environmentmapeffect_fresnel_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_fresnel_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect` Fresnel edge-weighting
  (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EnvironmentMapEffect::setFresnelFactorProperty()`
  (`src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp:272-284`),
  `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()` (lines 3145-3270, specifically the vertex shader's
  `vFresnel` computation, line 3182-3185)
- FNA reference: `Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`'s `ComputeFresnelFactor()`:
  `pow(max(1 - abs(viewAngle), 0), FresnelFactor) * EnvironmentMapAmount`.
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_Fresnel` (`cmake/Tests/EasyGLTests.cmake:585-587`).

## Purpose

Task 396 test. Verifies that `EnvironmentMapEffect`'s env-map blend factor genuinely depends on view angle
when Fresnel is enabled (the default, `FresnelFactor=1`), rather than being a flat `EnvironmentMapAmount`
constant regardless of angle. Documents (lines 9-11) that Tasks 393/394 had already established CNA
implemented *no* Fresnel uniform at all prior to this task's fix, across all 3 backends.

## Executive Verdict

**Healthy.** Both test cases' expected values were independently re-derived and match the FNA formula, the
`EnvironmentMapEffect.fx` reference, and the actual EasyGL shader (`EnsureEnvMapped3DProgram`, lines
3178-3185).

## Checklist Results

### API / XNA / FNA parity
`PASS`. `FresnelFactor` left at its constructor default of `1.0` (line 125's comment: "FresnelFactor left at
its default (1.0, Fresnel enabled) -- the real FNA default") is independently confirmed correct against both
`EnvironmentMapEffect.cpp:43` (`setFresnelFactorProperty(1.0f)` in the CNA ctor) and FNA's own
`EnvironmentMapEffect.cs:370` (`FresnelFactor = 1;`).

### Behavioral correctness
`PASS`, verified by direct substitution into `EnsureEnvMapped3DProgram`'s vertex shader formula
(`vFresnel = pow(max(1-abs(viewAngle),0), FresnelFactor) * EnvironmentMapAmount`, matching FNA's
`ComputeFresnelFactor` exactly):
  - Case (a), grazing/coplanar camera (`View=Projection=Identity`, line 165): the quad's normal is pure `+Z`
    while the eye-to-surface vector under an identity view/projection has no meaningful Z separation at the
    screen center (`eyeVector.z≈0`), giving `viewAngle≈0` → `pow(max(1-0,0),1)=1` → the Fresnel-weighted
    formula reduces to exactly `EnvironmentMapAmount=1`. Correctly documented (lines 18-19) as
    non-discriminating (both Fresnel-enabled and disabled formulas agree here) and included only as a sanity
    check — a legitimate test-design choice, not padding, since it independently confirms the file's own
    camera setup produces the expected degenerate case before relying on it in case (b).
  - Case (b), head-on perspective (`eye=(0,0,3)`, looking at the origin, line 172-173): at the screen center,
    `eyeVector` is parallel to the surface normal (`(0,0,1)`), so `viewAngle≈1` → `pow(max(1-1,0),1) =
    pow(0,1) = 0` — the Fresnel-weighted term fully suppresses the cube-map contribution at normal
    incidence. `baseColor = litRGB*texColor = (100,50,25)` (emissive `0.5*(200,100,50)/1≈(100,50,25)` after
    the same emissive/texture math verified in the sibling specular/golden test reports), matching the
    test's expected `Color(100,50,25,255)` at line 175-177 — and correctly distinguishes from the pre-fix
    "flat Amount" behavior the header describes as `(128,128,128)` (the gray cube's raw color, which would
    leak through unsuppressed under the old bug).

### Logic
`PASS`. `EnvironmentMapSpecular=(0,0,0)` (line 124) correctly removes the specular/alpha-scaled additive
term from both cases, isolating the test to the Fresnel-weighted lerp blend factor alone, as claimed.

### Memory/resource lifetime
`PASS`. Same ownership pattern as sibling files in this batch — `grayCube`/`tex` outlive the per-call `fx`.

### C++ correctness
`PASS`. No issues found; `renderWith()` signature (`const VertexPositionNormalTexture (&quad)[6]`) is
consistent with sibling files.

### Architecture
`PASS`. XNA-facing API only.

### Maintainability
`PASS`. Reuses the exact same helper-function shape as every sibling file in this batch (see Cross-File
Observations in the `_eyeposition_test.cpp` report — not repeated here).

### Testing
This is itself a test file. See "Missing or Weak Tests."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `EmissiveColor=(0.5,0.5,0.5)` combined with default `DiffuseColor=(1,1,1)` and enabled-but-unset `DirectionalLight0` relies on the light's default zero diffuse, same as several siblings

- Severity: LOW
- Confidence: HIGH
- Category: test-robustness (identical pattern to the `_eyeposition_test.cpp` report's F1 — cross-referenced
  here rather than re-derived in full)
- Location/symbol: `Draw()`, `renderWith()` (lines 120-132) — no explicit `DirectionalLight0` disable or
  zero-diffuse call.
- Evidence/why it matters: identical mechanism to the `_eyeposition_test.cpp` F1 finding — the
  `(100,50,25)` "baseColor" the header derives for case (b) is only exactly `EmissiveColor*Texture` because
  `DirectionalLight0`'s default diffuse color is `(0,0,0)`, not because the test disabled lighting.
- Suggested future action: same as the sibling finding — make the isolation explicit rather than
  default-dependent, for this file and its siblings collectively, in one pass.

## Cross-File Observations

- Directly complementary to `_eyeposition_test.cpp` (same batch): that file isolates the *reflection
  vector* from Fresnel (`FresnelFactor=0`); this file isolates *Fresnel* from the reflection vector's
  specifics (using solid-color cube faces, so which face is hit doesn't matter, only the blend factor does).
  Together the two tests cover orthogonal axes of the same shader path — a sound test-design split.
- `_specular_test.cpp` (same batch) reuses this file's exact "opaque case is non-discriminating, translucent
  case is" narrative structure for a different term (`EnvironmentMapSpecular` vs. cube alpha) — a consistent,
  repeated test-design idiom across the batch worth recognizing as intentional rather than coincidental.

## Missing or Weak Tests

- Does not test an intermediate view angle (e.g. 45°) to confirm the `pow(...)` falloff shape is smooth
  and monotonic between the two tested extremes (`viewAngle≈0` and `viewAngle≈1`) — `_fresnel_gradient_test.cpp`
  in this same batch covers a *different* aspect of Fresnel (per-vertex vs. per-fragment interpolation) at
  intermediate points, but neither file directly confirms the `pow()` exponent behavior at, say,
  `FresnelFactor=2` or `4` (only `1.0`, the default, is exercised here).
- Does not test `FresnelFactor=0` (Fresnel explicitly disabled) as a third case in *this* file — that
  scenario is instead covered by the separate `_eyeposition_test.cpp`/`_worldtransform_test.cpp` (which set
  `FresnelFactor=0` for unrelated isolation purposes) rather than by a dedicated assertion in this file that
  specifically targets "Fresnel disabled reproduces the flat-Amount formula." A reasonable division of
  responsibility across the batch, but worth noting this file alone does not close that loop.

## Positive Findings

- Case (a)'s inclusion as an explicit non-discriminating sanity check (rather than omitting it and jumping
  straight to the discriminating case (b)) is good test hygiene — it independently confirms the test rig's
  camera/geometry setup behaves as expected before relying on that same setup's absence of confounds in the
  discriminating case.
- The expected values for both cases were independently re-derivable from the FNA formula without needing to
  trust the file's own comments, and matched exactly.

## Final Assessment

A correct, well-isolated regression test for `EnvironmentMapEffect`'s Fresnel-weighted blend factor, whose
two expected values both check out against the FNA reference formula and the actual EasyGL shader. The only
finding (F1, LOW) is the same implicit-default-lighting fragility noted across several sibling files in this
batch.
