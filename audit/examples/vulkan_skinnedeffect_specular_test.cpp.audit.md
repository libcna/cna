# Audit: examples/vulkan_skinnedeffect_specular_test.cpp

## Metadata

- Source file: `examples/vulkan_skinnedeffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Task 894, `SkinnedEffect` real specular highlights
  test (Vulkan port; header cites "examples/easygl_basiceffect_specular_test.cpp for the full
  FNA-reference half-vector Blinn-Phong derivation").
- File type: hand-rolled `Game`-derived executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_skinnedeffect_specular …)` /
  `cna_register_backend_test(NAME Vulkan_SkinnedEffect_Specular …)`,
  `cmake/Tests/VulkanTests.cmake:651-654`).
- XNA/FNA relevance: direct — `SkinnedEffect.SpecularColor`/`SpecularPower`/per-light
  `SpecularColor`, `EyePosition` derivation via `IEffectLights`/`IEffectMatrices`. FNA source:
  `HLSL/Lighting.fxh`'s half-vector Blinn-Phong term, `HLSL/SkinnedEffect.fx`.
- Related production code: `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines
  362-382, specular/eye-position plumbing), `shaders/skinned3d_vertexlit.vert.glsl` (real dispatch
  target here, since this file never sets `PreferPerPixelLighting=true` and XNA's own default is
  `false`), `shaders/skinned3d.frag.glsl` (the per-pixel sibling, not exercised by this file).

## Purpose

Four-check pixel test proving `SkinnedEffect`'s real half-vector Blinn-Phong specular model: (a)
baseline eye position, (b) a different `EyePosition` producing a different specular value (proves
`EyePosition`-dependence), (c) `SpecularColor=(0,0,0)` producing pure diffuse+ambient (proves the
material gate), (d) `DirectionalLight0.Enabled=false` zeroing both diffuse and specular. A single
Identity bone at 100% weight keeps skinning a mathematical no-op, isolating the specular formula.

## Executive Verdict

**Needs attention** — cases (a), (c), (d) are internally consistent and this audit independently
re-derived case (a)'s value to within ordinary floating-point noise of the asserted `125`. Case (b),
however, has the **exact same defect shape** already found in this effect's EasyGL sibling test
(`easygl_skinnedeffect_specular_test.cpp`'s own F1): its expected constant is the stale, pre-vertex-
lit (per-pixel) value, and this audit's independent re-derivation shows the true current vertex-lit
value is `~61`, not `68` — the check only passes because `|61-68|=7 ≤ 10` (the check's own
tolerance).

## Checklist Results

### API / XNA / FNA parity
`setSpecularColorProperty`/`setSpecularPowerProperty`/`DirectionalLight0.setSpecularColorProperty`
(lines 129-138) map correctly to FNA's `IEffectLights` surface. `EyePosition` is correctly not set
directly (not a real, independently-settable XNA property) — derived internally via
`Matrix::Invert(view_).getTranslationProperty()` (`SkinnedEffect.cpp` lines 378-379), matching FNA.

### Behavioral correctness
Re-derived the half-vector formula by hand for case (a) (`kEyeStraightOn=(0,0,3)`,
`kLightDirRaw=(0.5,0,-1)` normalized to `(0.447214,0,-0.894427)`, `kNormal=(0,0,1)` constant across
the quad, `SpecularPower=32`, `kAmbient=0.02`, `kMaterialDiffuse=0.4`):
- Diffuse+ambient (constant, not eye-dependent): `NdotL0=dot(N,-L)=0.894427`; `litRGB=(0.02+
  0.5*0.894427)*0.4=0.186885`.
- Per-vertex specular at `TL=(-1,1,0)`: `E=normalize((0,0,3)-(-1,1,0))=(0.301511,-0.301511,0.904534)`;
  `H=normalize(E-L)=(-0.079625,-0.164778,0.982906)`; `dot(H,N)=0.982906`;
  `spec=pow(0.982906,32)=0.575836`.
- Per-vertex specular at `BR=(1,-1,0)`: `E=normalize((0,0,3)-(1,-1,0))=(-0.301511,0.301511,0.904534)`;
  `H=(-0.379754,0.152915,0.912371)`; `dot(H,N)=0.912371`; `spec=pow(0.912371,32)=0.053182`.
- Centre-pixel sample sits on the `TL`-`BR` diagonal's midpoint; by the scene's eye/quad symmetry
  (both vertices at equal view-space depth), Gouraud interpolation there reduces to a 0.5/0.5
  average: `spec_avg=0.5*(0.575836+0.053182)=0.314509`.
- Total: `litRGB + spec_avg = 0.186885+0.314509=0.501394` → `×255≈128`. This is within 3 units of the
  asserted `kExpectedStraightOn(125,125,125)`, plausibly the compounded effect of the ambient-drop
  defect documented as F1 in `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md` (which
  would land the "true, bug-included" value nearer `126`) plus ordinary GPU/perspective-interpolation
  rounding — consistent with, not contradicting, the file's own claim that `125` is a genuine live
  measurement (line 74-76: "125 is the real, measured value on this backend (confirmed against the
  actual render...)").
- Case (b) (`kEyeOffAxis=(3,0,1)`): re-derived independently. `TL` → `E=(0.942809,-0.235702,0.235702)`,
  `H=(0.394496,-0.187585,0.899485)`, `dot(H,N)=0.899485`, `spec=pow(0.899485,32)=0.033687`. `BR` →
  `E=(0.816497,0.408248,0.408248)`, `H=(0.261139,0.288675,0.921182)`, `dot(H,N)=0.921182`,
  `spec=pow(0.921182,32)=0.072318`. `spec_avg=0.5*(0.033687+0.072318)=0.053003`. Total:
  `0.186885+0.053003=0.239888` → `×255≈61.2`. **This audit's independently re-derived vertex-lit
  value for case (b) is `61`, not the asserted `kExpectedOffAxisEye=68`.** See F1.
- Cases (c)/(d): `SpecularColor=(0,0,0)` zeroes `specularRGB` in
  `skinned3d_vertexlit.vert.glsl` (`vSpecularRGB=(...)*fog.specularColor_power.xyz`, line 82),
  leaving pure diffuse+ambient — matches `kExpectedNoSpecular(48,48,48)` (`0.186885×255≈47.66→48`,
  modulo the same small ambient-drop noted above). `DirectionalLight0.Enabled=false` is gated by
  `FillGpuDrawParams()` lines 344-348/366-371 (`ld`/`ls` both forced to `Vector3::Zero`), zeroing
  both diffuse and specular contributions — matching `kExpectedLightDisabled(2,2,2)` (ambient-only,
  `0.02×0.4×255≈2.04→2`).

### Logic
Case (b)'s own numeric derivation was **not** independently re-verified against the current
vertex-lit formula by the file's own author — its comment (line 78, `// dotH=0.9239, spec=0.0794`)
gives the same numbers a pre-vertex-lit (per-pixel, single-point) derivation would produce, with no
equivalent "confirmed against the actual render" language that case (a)'s comment (lines 73-76)
carries. This audit's re-derivation confirms the real current value is materially different (`~61`,
not `~68`).

### Testing
Three of four checks are strong, evidence-backed assertions; the fourth (case b) verifies the output
is *within 10* of a stale, superseded formula's output rather than the current vertex-lit formula's
actual value — see F1.

## Detailed Findings

### F1 — Case (b)'s expected constant is a stale pre-vertex-lit value; the check passes only by tolerance overlap, not because the asserted value is correct

- Severity: MEDIUM
- Confidence: HIGH (independently re-derived from the exact current shader formula and geometry;
  matches the identical defect already found and reported in the EasyGL sibling test's own audit)
- Category: test-coverage / correctness-of-test
- Location/symbol: `kExpectedOffAxisEye(68, 68, 68, 255)` (line 78, comment: `// dotH=0.9239,
  spec=0.0794`); check (b) (lines 185-188)
- Evidence: unlike `kExpectedStraightOn`'s comment (lines 73-77), which explicitly documents "125 is
  the real, measured value on this backend (confirmed against the actual render, not copied from
  EasyGL's own close-but-not-identical 126 for the same scene)", `kExpectedOffAxisEye`'s comment
  carries no equivalent re-verification language — it is the same shape of derivation (`dotH`,
  `spec=pow(dotH,32)`) that would be computed for a single analytically-tractable point, not a
  Gouraud-averaged two-vertex value. This audit's own from-scratch re-derivation of the current
  vertex-lit formula (see Behavioral correctness above) computes `61`, not `68` — an exact parallel
  to `easygl_skinnedeffect_specular_test.cpp.audit.md`'s F1, which found the identical stale-value
  pattern for the EasyGL backend's own version of this same case, also landing at the same true
  value of `~61` (since the underlying formula and scene are identical between the two backends).
- Why it matters: this check does not actually verify the current vertex-lit formula's output at the
  off-axis eye position — it verifies the output is *within 10* of a different, outdated formula's
  output. A regression that shifted the real per-vertex value from `61` to, say, `65` would still
  pass; a regression that moved it to `79` (just outside tolerance of the stale `68`, but potentially
  a legitimate value under different rounding) would fail for the wrong reason.
- FNA/XNA comparison: N/A (test-authoring issue, not an XNA/FNA behavior question — the underlying
  `SkinnedEffect` specular behavior itself was independently confirmed correct for this scene via
  case (a)'s re-derivation, modulo the separately-tracked ambient-forwarding defect in F1 of the
  `preferperpixellighting` report).
- Related files: `easygl_skinnedeffect_specular_test.cpp.audit.md`'s own F1 documents the identical
  gap for the EasyGL backend's version of this exact test.
- Suggested future action (not implemented by this audit): change `kExpectedOffAxisEye` to the
  currently-correct vertex-lit value (`~61`, re-derived precisely above) so the check verifies actual
  current behavior rather than passing by accidental tolerance overlap with a superseded formula.

### F2 — Shared production concerns (ambient/emissive forwarding, world-space normal transform) apply to this file too, but are undetectable by its own scene

- Severity: HIGH (see full analysis at the cross-referenced report; not re-derived in full here to
  avoid duplication)
- Confidence: HIGH
- Category: correctness (production code), cross-backend parity gap / test-coverage gap
- Location/symbol: see `vulkan_skinnedeffect_preferperpixellighting_test.cpp.audit.md`'s F1
  (`SkinnedEffect`'s `AmbientLightColor`/`EmissiveColor` never reach the rendered pixel on Vulkan)
  and F2 (missing world-space normal transform in `skinned3d.vert.glsl`/`skinned3d_vertexlit.vert.glsl`).
  This file's own `kAmbient=0.02` and Identity `World` mean both defects are present in this file's
  rendered output but too small/structurally invisible to flip any of its four checks.
- Why it matters here specifically: this file's entire premise (`EyePosition`-dependent specular
  highlights) is exactly the kind of directional-lighting math a wrong-space normal would corrupt
  once `World` is non-Identity — a strong candidate for extension once F2 is fixed in production
  code, since this file already has the specular-formula machinery in place.
- Suggested future action: see the referenced report; not implemented by this audit.

## Cross-File Observations

- This file, `vulkan_skinnedeffect_preferperpixellighting_test.cpp`, and (by citation)
  `vulkan_basiceffect_specular_test.cpp` (not in this batch) form the same cross-checking chain
  documented in the EasyGL shard — this file's case (a) value is treated as ground truth by the
  `preferperpixellighting` sibling. Confirmed self-consistent (both cite `125`).
- The specific stale-constant pattern in F1 mirrors not just the EasyGL `SkinnedEffect` specular
  test's own F1, but also the EasyGL `BasicEffect` specular test's identical, earlier-found F1
  (`easygl_basiceffect_specular_test.cpp.audit.md`) — the same "off-axis case inherits an unrevised
  pre-vertex-lit constant" mistake has now recurred at least three times across this project's
  specular test family (BasicEffect/EasyGL, SkinnedEffect/EasyGL, SkinnedEffect/Vulkan), suggesting
  the fix (re-deriving case (b) alongside case (a) whenever a shader-dispatch default changes) is
  worth applying as a blanket cleanup across the whole family rather than file-by-file.

## Missing or Weak Tests

- See F1 (stale case-(b) constant).
- See F2 (ambient/emissive-drop and normal-transform gaps, shared with the whole shard).
- No test in this file varies `SpecularPower`, combines specular with multiple simultaneous lights,
  or with `WeightsPerVertex`>1 — reasonably left to sibling files/future work.

## Positive Findings

- Cases (a)/(c)/(d) collectively isolate three genuinely distinct aspects of the specular formula
  (magnitude, material-color gating, light-enable gating), each independently re-derivable and
  matching to within ordinary GPU rounding.
- The file's own comment for case (a) is unusually transparent about being a genuine live
  measurement rather than a copied/assumed value, which is exactly the practice that made this
  audit's re-derivation straightforward to cross-check.
- `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (case d) is a genuinely
  easy-to-miss XNA behavior and this test explicitly isolates it.

## Final Assessment

A mostly strong specular test whose case (a)/(c)/(d) arithmetic is sound, let down by the same
stale-off-axis-constant pattern (F1, MEDIUM) already seen twice in this project's sibling specular
tests, plus two real, shared production defects (F2, HIGH) that this file's own design cannot detect
but that this audit traced concretely while verifying this file's math.
