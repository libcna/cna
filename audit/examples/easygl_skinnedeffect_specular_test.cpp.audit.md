# Audit: examples/easygl_skinnedeffect_specular_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `SkinnedEffect` specular-highlight test
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_skinnedeffect_specular …)` /
  `cna_register_backend_test(NAME EasyGL_SkinnedEffect_Specular …)`,
  `cmake/Tests/EasyGLTests.cmake:634-637`).
- XNA/FNA relevance: direct — `SkinnedEffect.SpecularColor`/`SpecularPower`/per-light `SpecularColor`,
  real XNA 4.0 API. FNA source: `Graphics/Effect/StockEffects/SkinnedEffect.cs`,
  `HLSL/SkinnedEffect.fx`, shared `Lighting.fxh` `ComputeLights()` Blinn-Phong half-vector term.
- Production code exercised: `SkinnedEffect::FillGpuDrawParams` (specular/eye-position plumbing,
  `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp` lines 362-382),
  `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s fragment stage (specular sum,
  `EasyGLGraphicsBackend.cpp` lines 3381-3392, dispatched here since this test never sets
  `PreferPerPixelLighting`, and per Task 1102b's fix the *default*, `false`, now selects
  `EnsureSkinnedVertexLitProgram()` instead — see Behavioral correctness below).

## Purpose

Task 894's specular-infrastructure test for `SkinnedEffect`: before this task, per its own header
comment, the effect had "zero specular infrastructure" (no `SpecularColor`/`SpecularPower` forwarding
in either the C++ effect or any backend's skinned shader). Uses a single Identity-weighted bone to
keep skinning a no-op, isolating the specular Blinn-Phong computation as the thing under test. Four
checks: (a) strong specular at a straight-on eye position, (b) weaker specular at an off-axis eye
position (proves `EyePosition` genuinely feeds the half-vector, not a hardcoded constant), (c) zero
`SpecularColor` yields a pure diffuse+ambient baseline, (d) disabling the light zeroes both diffuse and
specular.

## Executive Verdict

**Mostly healthy** — the specular math and per-light gating are correctly exercised and match FNA's
`Lighting.fxh` formula, and the file is self-aware and transparent about a Task-1102b-induced value
change (see below), but it has one internal inconsistency in its own commentary/dead value (F1, LOW)
and inherits the same missing-world-space-normal production defect documented in this shard's sibling
reports (F2, HIGH, not detectable by this file's Identity-World scene).

## Checklist Results

### API / XNA / FNA parity
`fx.setSpecularColorProperty`/`setSpecularPowerProperty` and `DirectionalLight0.setSpecularColorProperty`
match FNA's `SkinnedEffect.SpecularColor`/`SpecularPower` and `DirectionalLight.SpecularColor` exactly.
`FillGpuDrawParams()` (lines 362-376) forwards all three lights' `SpecularColor` — verified this
matches FNA's `Lighting.fxh` `ComputeLights()`, which sums each enabled light's own specular
contribution before the material's `SpecularColor` multiplies the summed result once (comment at
SkinnedEffect.cpp lines 362-365 states this explicitly and correctly).

### Behavioral correctness
The file's own header comment (lines 14-27) transparently documents that Task 1102b changed case (a)'s
expected value from the old always-per-pixel behavior (~155) to the new default-per-vertex/Gouraud
behavior (~126, since this test never calls `setPreferPerPixelLightingProperty` and the real XNA
default is `false`) — this is exactly the kind of self-aware "why did this number change" commentary
the project's own audit process values; verified the claim against `SelectProgram()`
(`EasyGLGraphicsBackend.cpp` lines 3929-3941): with `lightingEnabled=true` and
`preferPerPixelLighting=false` (the C++ default per `SkinnedEffect.hpp` — confirmed
`preferPerPixelLighting_` is not set by this test, so it retains its default-constructed value), the
dispatch does indeed route to `EnsureSkinnedVertexLitProgram()`, not `EnsureSkinnedProgram()` — the
header comment's own claim is correct.

Case (b) (`kExpectedOffAxisEye`, line 86) has an interesting, honestly-documented wrinkle: its own
comment says the value is "the old per-pixel value; still within tolerance of the vertex-lit result,
left unchanged." This means case (b) is **not actually independently re-verified** against the new
vertex-lit code path's own real analytical value — it is asserted to merely fall within the existing
±10 tolerance of whatever the vertex-lit shader now produces, which was not independently confirmed by
a fresh derivation, only inherited from before Task 1102b. That is a materially weaker check than cases
(a)/(c)/(d), which do restate an intermediate derivation. See F1.

Case (d) (`kExpectedLightDisabled`, `(2,2,2,255)`) is ambient-only (`kAmbient=(0.02,0.02,0.02)` ×
`kMaterialDiffuse=(0.4,0.4,0.4)` × `alpha=1` × 255 ≈ 2.04 → 2) — correct arithmetic, and correctly
proves (per its own label) that disabling `DirectionalLight0` zeroes the specular term too, not just
diffuse (verified against `FillGpuDrawParams()`'s `light0On` gate at line 344/366, which zeroes both
`p.light0Diffuse` and `p.light0Specular` when disabled — a single shared gate, so this check, while
correct, cannot actually distinguish "diffuse gated correctly but specular gating is broken" from
"both gated correctly," since a bug in specular-only gating would still pass this particular assertion
as long as diffuse's own gating is correct and the specular leak is smaller than the ±10 tolerance).

### Logic
`renderWith()`'s up-to-20-frame retry-until-nonblack loop (lines 167-177) is the same pattern as this
shard's sibling files — reasonable first-frame-flash guard, not a masking of a real render bug (none of
the expected values here are black).

### Testing
Covers specular sign/magnitude, `EyePosition` dependence, zero-specular-color baseline, and
light-disabled zeroing — a solid four-case spread for a first specular test. Does not cover
`SpecularPower` variation (always `32.0f`), multiple simultaneous lights' specular sums, or specular
combined with `WeightsPerVertex`>1 — reasonably left to other files/future work, not a defect in this
file's own scope.

## Detailed Findings

### F1 — Case (b)'s expected value is a stale pre-Task-1102b constant, not independently re-derived for the new vertex-lit default

- Severity: LOW
- Confidence: HIGH
- Category: test-coverage / maintainability
- Location/symbol: `kExpectedOffAxisEye` (line 86), the file's own comment
  `// (old per-pixel value; still within tolerance of the vertex-lit result, left unchanged...)`
- Evidence: the comment itself concedes the value was never re-derived under the current (vertex-lit)
  default; it was only confirmed to still fall inside the existing `±10` tolerance window at line
  116-118 (`matches()`). Cases (a)/(c)/(d) all cite a genuine intermediate derivation or a directly
  computable arithmetic identity; case (b) does not.
- Why it matters: this narrows what a regression in the vertex-lit specular path at an off-axis eye
  position would need to do to slip past this check — as long as the (buggy) new value stays within
  ±10 of the old, no-longer-analytically-verified constant, the test still reports PASS. Low severity
  because the tolerance is not egregiously wide and the check still meaningfully constrains behavior
  (case (b) vs (a) comparison at line 196 still proves eye-position dependence), but it is a real gap
  relative to the file's own otherwise-strong standard of showing its arithmetic.
- FNA/XNA comparison: N/A (test-authoring gap, not an XNA parity issue).
- Suggested future action (not implemented by this audit): re-derive case (b)'s expected value
  analytically under the vertex-lit (Gouraud) formula the same way case (a) was, and tighten the
  tolerance back down if the re-derived value differs meaningfully from the current constant.

### F2 — Shared with sibling shard files: EasyGL's skinned shaders never transform the normal into world space; this test's Identity-World scene cannot detect it

- Severity: HIGH
- Confidence: HIGH
- Category: correctness (production shader), test-coverage gap
- Location/symbol: `EnsureSkinnedProgram()`/`EnsureSkinnedVertexLitProgram()` vertex stages
  (`EasyGLGraphicsBackend.cpp` lines 3300-3318, 3489-3512) — see the full evidence and FNA-parity
  analysis in `easygl_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1, which applies
  identically here since this file also uses `fx.setWorldProperty(Matrix::getIdentityProperty())`
  (line 150).
- Why it matters here specifically: this test's entire premise (`EyePosition`-dependent specular
  highlights) is precisely the kind of directional lighting math that a wrong-space normal would
  corrupt once `World` is not Identity — this file is a strong candidate for extension (rotate the
  quad via a non-Identity `World` and re-derive the expected specular highlight position/intensity)
  once F1 is fixed in production code, since it already has the specular-formula machinery in place.
- Suggested future action: see the referenced sibling report; not implemented by this audit.

## Cross-File Observations

- Forms the root of the "shared oracle" chain referenced by
  `easygl_skinnedeffect_preferperpixellighting_test.cpp`'s own header comment — this file's case (a)
  value is treated as ground truth by at least one sibling file. Confirmed self-consistent as read;
  no evidence found of it being wrong, but its correctness is now load-bearing for more than one test
  file.
- `SkinnedEffect::FillGpuDrawParams()`'s emissive-pre-folds-ambient convention
  (`emissiveColor_ + ambientLightColor_ * diffuseColor_`) is identical to `BasicEffect`'s own, and this
  test's case (d) arithmetic implicitly depends on that being correct — consistent with the fourth-round
  `audit_net.md` remediation comment already present in the shader source (lines 3368-3379), which this
  audit independently confirms is now applied correctly (`emissiveColor` added, not multiplied, after
  the diffuse product).

## Missing or Weak Tests

- See F1 (case (b) not independently re-derived).
- See F2 (no non-Identity-`World` specular test exists anywhere in this shard).

## Positive Findings

- Cases (a)-(d) collectively isolate four genuinely distinct aspects of the specular formula
  (magnitude, view-dependence, material-color gating, light-enable gating) rather than four variations
  on the same check.
- The header comment's transparency about exactly which expected value changed, why, and by how much
  across a related task (1102b) is a strong practice this audit would like to see repeated project-wide.

## Final Assessment

A solidly-designed specular test whose core assertions are correct and traceable to FNA's real
formula; its only genuine weaknesses are one under-verified constant (F1, LOW) and the test family's
shared blind spot for world-space lighting correctness (F2, HIGH, a production defect, not something
this file did wrong itself).
