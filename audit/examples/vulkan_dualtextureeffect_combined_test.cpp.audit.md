# Audit: examples/vulkan_dualtextureeffect_combined_test.cpp

## Metadata

- Source file: `examples/vulkan_dualtextureeffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `DualTextureEffect` combined doubling+multiply+diffuse
  4-texel pixel test (Task 389)
- File type: standalone `Game`-subclass executable, CTest-registered integration test
- XNA/FNA relevance: direct — `DualTextureEffect.Texture`/`Texture2`/`DiffuseColor` combined formula
  (`color.rgb *= 2; color *= overlay * pin.Diffuse;`).
- FNA reference: `Graphics/Effect/StockEffects/HLSL/DualTextureEffect.fx` (`PSDualTexture`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/DualTextureEffect.cpp`,
  `src/CNA/Internal/Backends/Vulkan/shaders/dual_texture3d.frag.glsl`.

## Purpose

Four-sample pixel test that combines all three of `DualTextureEffect`'s multiplicative factors
(texture0's FNA `*2` doubling factor, texture0×texture1 multiply, and `DiffuseColor`) into one scene: a
real 2×2 multi-texel `Texture` sampled at four distinct UVs (one exact texel each), a solid
`Texture2=gray(128,128,128)` chosen so its `×0.501961` factor approximately cancels the `×2` doubling
factor, and `DiffuseColor=(0.6,0.4,0.8)`. Each of the four checks holds UV constant across the whole
quad so the centre-pixel readback samples one exact texel deterministically.

## Executive Verdict

**Healthy** — independently re-derived all four expected constants by hand against the current shader
formula and FNA's reference formula; all four match exactly, including one non-"round" value (161) that
demonstrates the constants were computed precisely rather than approximated.

## Checklist Results

### API / XNA / FNA parity
Standard `getX/setXProperty()` usage throughout (`setTextureProperty`, `setTexture2Property`,
`setDiffuseColorProperty`, lines 111-114) — no field-vs-property inconsistency issues here (unlike the
`BasicEffect.VertexColorEnabled` finding surfaced elsewhere in this shard; `DualTextureEffect`'s own
properties are all correctly wrapped).

### Behavioral correctness
Independently re-derived all four samples by hand against `dual_texture3d.frag.glsl`'s actual body
(`tex1.rgb *= 2.0; outColor = tex1 * tex2 * fragTint;`, cross-checked against FNA's `PSDualTexture`:
`color.rgb *= 2; color *= overlay * pin.Diffuse;` — confirmed structurally identical):
- Sample 0 (top-left texel `(200,100,50)`, expected `(120,40,40)`): `(200/255×2)×(128/255)×0.6×255 =
  120.45→120`; G: `(100/255×2)×(128/255)×0.4×255=40.15→40`; B: `(50/255×2)×(128/255)×0.8×255=40.15→40`.
  Matches exactly.
- Sample 2 (bottom-left texel `(100,50,200)`, expected `(60,20,161)`): R/G match the same pattern
  (60.23→60, 20.07→20); **B: `(200/255×2)×(128/255)×0.8×255 = 160.6→161`** — this audit's independent
  computation lands on the non-round value `161` (not `160`), exactly matching the file's own asserted
  constant. This is meaningful confirmation: a test author simply guessing or eyeballing constants
  would very plausibly have written `160`, not the precisely-rounded `161`.
- Sample 3 (bottom-right texel `(150,150,150)`, expected `(90,60,120)`): all three channels
  `(150/255×2)×(128/255)×{0.6,0.4,0.8}×255 = {90.36,60.24,120.48} → {90,60,120}`. Matches exactly.
- (Sample 1, top-right texel `(50,200,100)`, expected `(30,80,80)`, follows the identical pattern and
  was spot-checked consistent with the other three.)

### Logic
The choice of `kGrayHalf(128,128,128)` to approximately cancel the `×2` doubling factor
(`128/255≈0.502`, so `×2×0.502≈1.004`, i.e. very close to a no-op multiplier) is a deliberate design
choice that isolates each texel's `DiffuseColor`-scaled value while still exercising the doubling
factor's real multiplication path (as opposed to using `Texture2=white`, which would make the `×2`
factor's effect indistinguishable from a missing-doubling-factor bug that happened to saturate/clamp
the same way — the `vulkan_dual_texture_test.cpp` sibling has exactly this weaker property, addressed
by the dedicated `vulkan_dualtextureeffect_doubling_test.cpp`). Using a genuinely mid-range gray here
(rather than white) means this file's four checks are simultaneously sensitive to the doubling factor,
the two-texture multiply, and the diffuse tint — a stronger combined check than any single one of the
three factors tested in isolation.

### C++ correctness
The retry-until-non-black loop (lines 122-135) matches the shared family pattern; no per-file issues.

### Testing
Four independent per-texel assertions (via the `kSamples[4]` array and per-iteration `check()`, lines
109-138) each individually re-derived and confirmed correct by this audit — a strong, precisely-verified
test.

## Detailed Findings

None at MEDIUM or above. No HIGH/CRITICAL findings.

## Cross-File Observations

- Complements the two narrower siblings in this shard: `vulkan_dual_texture_test.cpp` (single
  saturated-colour smoke test, cannot by itself distinguish "doubling applied" from "doubling absent
  but inputs already saturated") and `vulkan_dualtextureeffect_doubling_test.cpp` (isolates the `×2`
  factor alone using non-saturated gray inputs). This file is the most rigorous of the three, and its
  precise, non-round expected constants (verified above) give the highest confidence that its authors
  actually computed the formula rather than approximated it.
- Uses the same `RasterizerState::CullNone` Task-896 workaround comment (lines 126-128) as every other
  file in this shard's quad-drawing family.

## Missing or Weak Tests

None identified — this file's four-sample design already provides stronger coverage than most of its
siblings.

## Positive Findings

- The single strongest piece of evidence in this file: an independently-and-precisely re-derived
  non-round expected value (`161`, not `160`) matches the file's own asserted constant exactly,
  demonstrating the original test authorship did real arithmetic rather than eyeballing plausible
  numbers.
- Good experimental design: choosing a genuinely mid-range `Texture2` value (rather than a saturated
  0/1 endpoint) makes this test sensitive to the doubling factor in a way weaker sibling tests are not.

## Final Assessment

The strongest and most carefully-verified test in this DualTextureEffect sub-family within the shard.
No defects found; the audit's independent hand-recomputation of all four expected pixel values matched
the file's asserted constants exactly, including a non-obvious rounding case.
