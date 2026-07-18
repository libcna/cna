# Audit: examples/easygl_environmentmapeffect_specular_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect`'s `EnvironmentMapSpecular`
  (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()` fragment shader
  (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3230-3237`)
- FNA reference: `Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`'s `PSEnvMapSpecular`:
  `envmap = SAMPLE_CUBEMAP(...) * color.a; color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb);
  color.rgb += EnvironmentMapSpecular * envmap.a;` — i.e. the specular term is scaled by the cube map's own
  alpha channel (itself pre-multiplied by `color.a`), not added as a flat unconditional constant.
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_Specular` (`cmake/Tests/EasyGLTests.cmake:571-573`).

## Purpose

Task 395 test. Documents (lines 2-16) a specific, previously-confirmed CNA bug: the shader added
`EnvironmentMapSpecular` as a flat additive constant, never reading the cube map's alpha channel at all,
whereas FNA's real formula scales it by that alpha (further scaled by the combined texture/diffuse alpha).
Uses an opaque cube (non-discriminating sanity check) and a translucent cube (`alpha=128`, discriminating)
to prove the alpha-scaling is genuinely applied, not just present-but-inert.

## Executive Verdict

**Healthy** (post-fix). The current EasyGL fragment shader
(`rgb=mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)+uEnvMapSpecular*envSample.a*combinedAlpha`,
line 3236) matches FNA's formula exactly (`envSample.a` is the raw cube alpha, `combinedAlpha` corresponds
to FNA's `color.a` factor folded in twice — once into `envmap.rgb`/`envmap.a` via `* color.a` in FNA's own
code, and CNA applies the equivalent `combinedAlpha` multiply directly to both `envSample.rgb` and the
specular term, which is algebraically identical to FNA's `envmap.a = cubeAlpha * color.a` then
`+= EnvironmentMapSpecular * envmap.a`).

## Checklist Results

### API / XNA / FNA parity
`PASS`. `EnvironmentMapSpecular`'s XML-doc-documented behavior in FNA (`EnvironmentMapEffect.cs:287-293`:
"amount of the environment map alpha channel that will be added to the base texture... can be used to
implement cheap specular lighting") is exactly what this test targets and confirms.

### Behavioral correctness
`PASS`, verified by direct re-derivation of both cases:
  - Case (a), opaque cube (`alpha=255`, i.e. `1.0`): `combinedAlpha = diffuseColor.a*texColor.a = 1*1 = 1`
    (default `alpha_=1`, `Texture2D` alpha set to `255`). `baseColor = litRGB*texColor.rgb = 0.5*(200,100,50)/255*255
    = (100,50,25)` (identical emissive-only derivation verified in the `_golden_test.cpp`/`_fresnel_test.cpp`
    reports). Specular add `= EnvironmentMapSpecular(0.4,0.4,0.4) * envSample.a(1.0) * combinedAlpha(1.0)
    = (0.4,0.4,0.4) → (102,102,102)`, giving total `≈(100+102,50+102,25+102)=(202,152,127)`, matching the
    test's own expected `Color(202,152,127,255)` (line 159-161) — correctly flagged in the file's own
    comment as non-discriminating, since `envSample.a=1.0` here makes the alpha-scaled and flat-additive
    formulas coincide.
  - Case (b), translucent cube (`alpha=128→0.502`): specular add `= 0.4*0.502*1 ≈ 0.2 → 51/255` per channel,
    giving `≈(100+51,50+51,25+51)=(151,101,76)`, matching the test's expected `Color(151,101,76,255)`
    (line 167-169) exactly — and correctly distinguishing this from what the old buggy flat-additive formula
    would have produced (repeating case (a)'s `(202,152,127)`, per the header comment's own explicit
    contrast at lines 23-24). This is a genuinely discriminating check: the two candidate formulas produce
    visibly different results (`151` vs. `202` in the red channel, a ~50-unit gap comfortably outside the
    `tol=20` tolerance), so a regression back to flat-additive behavior would be caught.

### Logic
`PASS`. `EnvironmentMapAmount=0` (line 117) correctly removes the cube map's *RGB* contribution (the
`mix(baseColor, envSample.rgb*combinedAlpha, blendFactor)` term reduces to `baseColor` when `blendFactor=0`,
which it is since `uEnvMapAmount=0` forces `vFresnel=0` regardless of the Fresnel-enabled branch — verified
against the vertex shader's `vFresnel=(uFresnelEnabled>0.5) ? pow(...)*uEnvMapAmount : uEnvMapAmount`, both
branches multiply/equal `uEnvMapAmount=0`), so only `EnvironmentMapSpecular`'s own alpha-scaling is
exercised, as the file's header comment (lines 14-16) explicitly states.

### Memory/resource lifetime
`PASS`. Same ownership pattern as every sibling file in this batch.

### C++ correctness
`PASS`. No issues found.

### Architecture
`PASS`. XNA-facing API only.

### Testing
This is itself a test file. See "Missing or Weak Tests."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — this file's math was the most directly re-derivable of the batch (a
single linear additive term, unlike the Fresnel/gradient files' more involved trigonometric derivations).

### F1 — Relies on `DirectionalLight0`'s implicit default zero diffuse, same pattern as several siblings

- Severity: LOW
- Confidence: HIGH
- Category: test-robustness (identical pattern already detailed in the `_eyeposition_test.cpp` report's F1)
- Location/symbol: `Draw()` (no explicit light disable/zero), `renderWith()` (lines 109-125).
- Evidence/why it matters: identical mechanism — `EmissiveColor=(0.5,0.5,0.5)` alone does not zero the lit
  term; it is `DirectionalLight0`'s default diffuse color of `(0,0,0)` that does, silently.
- Suggested future action: same as noted in the sibling report — make explicit in a future consistency pass
  across this batch.

## Cross-File Observations

- Structurally near-identical to `_fresnel_test.cpp` (same batch): both use an "opaque/case-(a)
  non-discriminating sanity check, translucent/case-(b) discriminating check" narrative for a different
  effect parameter (`EnvironmentMapSpecular` here vs. the Fresnel blend factor there) — a deliberate,
  recognizable, and sound test-design pattern repeated across this task-series, not coincidental duplication.
- The `(151,101,76)` expected value this file derives for its case (b) is the *identical* number
  `_golden_test.cpp` independently re-derives (via a different camera/`World` setup) and asserts via its own
  `ExpectPixel` call — cross-file numeric consistency that increases confidence both derivations are correct
  (an error in one would very likely have produced a different number, not an accidentally-matching one,
  given the two files' setups differ in `World`/`View`/`Projection`).

## Missing or Weak Tests

- Does not test `EnvironmentMapSpecular` with different per-channel values (it's always `(0.4,0.4,0.4)`,
  uniform across R/G/B) — a per-channel-distinct specular tint (e.g. `(0.4,0.1,0.0)`) would additionally
  confirm the specular term is applied per-channel rather than, say, averaged or applied only to one
  channel by an implementation error. Given the formula is a simple vector multiply, this is a low-risk gap,
  but it is a real, nameable one.
- Does not test `alpha=0` (fully transparent cube), the other extreme from the opaque case — would confirm
  the specular term correctly vanishes entirely rather than assuming linear scaling holds at the boundary.

## Positive Findings

- The chosen translucent-alpha value (`128`, i.e. `≈0.502`, deliberately "non-saturated" per the header
  comment) is a good test-design choice — it avoids the trivial edge values (`0` or `255`) that could
  coincidentally make buggy and correct formulas agree, while still being cleanly resolvable to whole
  numbers when converted to 8-bit color for the tolerance check.
- Cross-file numeric consistency with `_golden_test.cpp`'s independently-derived same value strengthens
  confidence in both files' correctness.

## Final Assessment

A correct, cleanly-isolated regression test for a real, previously-confirmed alpha-scaling bug in
`EnvironmentMapEffect`'s specular term. Both cases' expected values were independently re-derived and match
the current EasyGL shader and FNA reference formula exactly. Only the shared implicit-lighting-default
fragility (F1, LOW) was found.
