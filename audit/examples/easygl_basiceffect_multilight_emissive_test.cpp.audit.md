# Audit: examples/easygl_basiceffect_multilight_emissive_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_multilight_emissive_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — EasyGL backend `BasicEffect` multi-light + emissive pixel test
- File type: C++ example/integration-test executable (`BasicEffectMultiLightEmissiveTest : Game`, `main()`)
- Related production code: `Microsoft::Xna::Framework::Graphics::BasicEffect::FillGpuDrawParams()`
  (`BasicEffect.cpp:93-131`, all-three-lights + lit-path emissive forwarding),
  `CNA::Internal::Backends::EasyGL::EasyGLGraphicsBackend::BindDrawParams`
  (`EasyGLGraphicsBackend.cpp:4023-4067`, ambient/light0/light1/light2 uniform binding) and its lit GLSL
  (`lightSum=uAmbientColor+uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;
  litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;`)
- XNA/FNA relevance: `BasicEffect.LightingEnabled=true` with `DirectionalLight0/1/2` + `EmissiveColor`, judged
  against `FNA/src/Graphics/Effect/StockEffects/EffectHelpers.cs::SetMaterialColor()`'s lighting-enabled branch and
  `HLSL/Lighting.fxh::ComputeLights()`.
- Main related tests: this file (Task 885) is itself the origin of a real, fixed bug (light1/light2 + lit-path
  emissive previously silently dropped) — its own header documents this, analogous to
  `easygl_basiceffect_emissive_test.cpp` (Task 369) for the no-lighting path.

## Purpose

Verifies that with `LightingEnabled=true`, all three `DirectionalLight`s are summed (not just `DirectionalLight0`),
each respects its own `Enabled` flag and its own `Direction`/`DiffuseColor` fields independently (not aliased to
`DirectionalLight0`'s), and `EmissiveColor` is added on the lit path too (previously only forwarded on the disabled-
lighting path per Task 369). Three checks: (1) all 3 lights + emissive combined, (2) `DirectionalLight2.Enabled=false`
zeroes its contribution, (3) `DirectionalLight1` rotated off-axis to prove it reads its own direction field, not
`DirectionalLight0`'s. Placement matches `examples-tests-easygl`.

## Executive Verdict

**Healthy** — independently re-derived the full lit-path formula from FNA's `EffectHelpers.SetMaterialColor()` +
`Lighting.fxh::ComputeLights()`, confirmed CNA's `BasicEffect.cpp`/EasyGL shader chain implements the mathematically
equivalent result, and independently recomputed all three expected colors from scratch — all three matched exactly.
This is a rigorous, well-targeted regression test for a real, confirmed-fixed bug.

## Checklist Results

### API / XNA / FNA parity
`setLightingEnabledProperty(true)`, `setAmbientLightColorProperty`, `setDiffuseColorProperty`,
`setEmissiveColorProperty`, and each of `DirectionalLight0/1/2`'s `setEnabledProperty`/`setDirectionProperty`/
`setDiffuseColorProperty` are all real XNA members, used with correct signatures.

### Behavioral correctness — full FNA-formula re-derivation
Traced FNA's `EffectHelpers.SetMaterialColor()` (lines 211-227, lighting-enabled branch):
`emissive.X = (EmissiveColor.X + AmbientLightColor.X * DiffuseColor.X) * Alpha`, and the shader
(`Lighting.fxh::ComputeLights`, per this file's own header lines 5-13, corroborated by the file's own accurate
FNA-citation) computes `result.Diffuse = sum_i(max(dot(-Direction_i,N),0)*LightDiffuse_i) * DiffuseColor.rgb +
EmissiveColor(the pre-combined uniform above)`. Algebraically: `sum_i(NdotL_i*LightDiffuse_i)*DiffuseColor +
EmissiveColor + AmbientLightColor*DiffuseColor = (AmbientLightColor + sum_i NdotL_i*LightDiffuse_i)*DiffuseColor +
EmissiveColor` — exactly the formula this file's own header states (lines 12-13) and exactly what CNA implements,
just split differently across CPU/GPU: `BasicEffect::FillGpuDrawParams()` forwards `p.ambientColor` as a *plain*
`ambientLightColor_` (not pre-multiplied by diffuse, `BasicEffect.cpp:77-79`) and `p.emissiveColor` as plain
`emissiveColor_*alpha_` only when `lightingEnabled_` (`BasicEffect.cpp:115-119`) — then EasyGL's shader itself does
`lightSum=uAmbientColor+Σ(light_i*NdotL_i); litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;`
(`EasyGLGraphicsBackend.cpp:2836-2837`), which is the same net result as FNA's CPU-pre-combined-ambient approach,
confirmed algebraically equivalent (`(Ambient+Σ)*Diffuse+Emissive` either way the ambient*diffuse multiply is
grouped) — a correctly-noted, deliberate CNA implementation-detail deviation from FNA's exact CPU/GPU split, not a
behavioral difference, and the file's own header (lines 8-11) explicitly documents this as an intentional
"mathematically equivalent net result" choice.

Independently recomputed all three test's expected colors from the stated constants
(`kAmbient=(0.05,0.05,0.05)`, `kMaterialDiffuse=(1,1,1)`, `kEmissive=(0.10,0.05,0.02)`,
`kLight0Diffuse=(0.6,0,0)`, `kLight1Diffuse=(0,0.6,0)`, `kLight2Diffuse=(0,0,0.6)`, all lights sharing
`kLightDir=(0,0,1)` in check 1, `kNormal=(0.8660254,0,-0.5)` giving `NdotL=dot(N,-Dir)=dot((0.866,0,-0.5),(0,0,-1))
=0.5` for every light sharing that direction):
- **Check 1 (all 3 lights)**: `lightSum = (0.05,0.05,0.05) + 0.5*(0.6,0,0) + 0.5*(0,0.6,0) + 0.5*(0,0,0.6) =
  (0.35,0.35,0.35)`; `litRGB = (0.35,0.35,0.35)*(1,1,1) + (0.10,0.05,0.02) = (0.45,0.40,0.37)`; ×255 =
  `(114.75, 102, 94.35)` → rounds to `(115,102,94)` — matches `kExpectedAllLights` (line 73) exactly.
- **Check 2 (`DirectionalLight2.Enabled=false`)**: blue channel's `0.5*(0,0,0.6)` term drops (per
  `BasicEffect::FillGpuDrawParams()`'s `light2On`-gated zeroing, `BasicEffect.cpp:101-103`, confirmed this
  actually zeroes the diffuse/specular forwarded to the GPU regardless of the C++-level `DiffuseColor` field still
  being set): `lightSum = (0.35,0.35,0.05)`; `litRGB = (0.45,0.40,0.07)` → `(114.75,102,17.85)` → `(115,102,18)` —
  matches `kExpectedLight2Disabled` (line 75) exactly.
- **Check 3 (`DirectionalLight1` rotated to `(1,0,0)`)**: `NdotL1 = dot(N,-Dir1) = dot((0.866,0,-0.5),(-1,0,0)) =
  -0.866`, clamped to `max(.,0)=0` by the shader's `max(dot(N,-uLight1Dir),0.0)` (confirmed at
  `EasyGLGraphicsBackend.cpp:2834`/`2951`/etc.) — green channel's `0.5*(0,0.6,0)` term drops instead:
  `lightSum = (0.35,0.05,0.35)`; `litRGB = (0.45,0.10,0.37)` → `(114.75,25.5,94.35)` → `(115,26,94)` — matches
  `kExpectedLight1OffAxis` (line 78) exactly.

All three expected colors independently re-derived and verified correct during this audit, including the specific
per-channel isolation design (each of the three RGB channels is driven by a *different* light, making each check
individually diagnostic of which specific light's forwarding broke).

### Logic
`renderWith()` (lines 119-164) is a well-factored helper taking `light2Enabled`/`light1Dir` as parameters, reused
across all three checks — good avoidance of duplicated setup code across the three scenes, unlike some sibling
files that repeat the same block per case.

### Memory/resource lifetime
`Texture2D tex` constructed once in `Draw()`, passed by reference into `renderWith()` and referenced via
`fx.setTextureProperty(&tex)` for all three calls — outlives every use, no dangling risk.

### C++ correctness
Same `closeTo`/`matches` int-domain pattern as sibling files — safe.

### Performance / Robustness

### F1 — Same retry-until-non-black loop weakness as its `combined_test`/`emissive_test` siblings

- Severity: MEDIUM
- Confidence: MEDIUM
- Category: test-coverage / robustness
- Location/symbol: `renderWith()`, lines 150-163
- Evidence: identical shape and identical weakness to the pattern documented in full in the
  `easygl_basiceffect_combined_test.cpp` audit report's Finding F1 — accepts the first non-black frame
  unconditionally, three times (once per `renderWith()` call), without re-validating stability.
- Why it matters: same reasoning as the cross-referenced report — cross-referenced here rather than repeated in
  full to avoid redundant boilerplate across this batch's reports.
- Related files: `easygl_basiceffect_combined_test.cpp` (full finding), `easygl_basiceffect_emissive_test.cpp`
  (same finding).
- Suggested future action: same as the combined-test report.

### Testing
This file is itself a test. See Missing or Weak Tests.

## Detailed Findings

(F1 above is the only substantive finding; no HIGH/CRITICAL findings.)

## Cross-File Observations

- This file's own header (lines 15-19) explicitly states the *pre-fix* behavior: `FillGpuDrawParams()` "only ever
  forwarded `DirectionalLight0`'s direction/diffuse to the GPU, and never forwarded `EmissiveColor` at all on the
  lit path" — cross-checked against the *current* `BasicEffect.cpp` and confirmed both are now fixed (light1/light2
  forwarding at lines 93-107; lit-path emissive at lines 115-119) — this file is a genuine, currently-valid
  regression guard for a real historical gap, not describing a bug that's still present.
- Shares the `RasterizerState::CullNone`/"Task 896" pattern and the 20-iteration retry-loop pattern with
  `easygl_basiceffect_combined_test.cpp` and `easygl_basiceffect_emissive_test.cpp`.
- The per-channel-isolation test design (each RGB channel driven by a different light) is a stronger and more
  diagnostic pattern than a single scalar "did the color change" check — worth calling out as a reusable pattern
  for any future multi-input effect test.

## Missing or Weak Tests

- No case tests all three lights *and* a non-default `SpecularColor`/`SpecularPower` together — the file's own
  header (line 25's referenced `EasyGLGraphicsBackend.cpp` "Task 886"/"Task 894" comments, cross-checked during
  this audit) shows specular forwarding for all three lights *does* exist in production code
  (`BasicEffect.cpp:120-124`, `EasyGLGraphicsBackend.cpp:4053-4062`) but this test only exercises diffuse — no
  pixel test in this batch (or apparently elsewhere, based on the surrounding shader comments referencing later
  task numbers 886/890/894 as separate, presumably-tested changes not part of this file) verifies specular
  contribution numerically. Out of this file's stated scope (diffuse+emissive multi-light), but worth flagging as
  a coverage gap for whichever shard covers `BasicEffect` specular.
- See F1 for the retry-loop oracle-strength gap.

## Positive Findings

- All three expected colors independently re-derived from FNA reference formulas and CNA/EasyGL production code
  during this audit, and all three matched exactly — a rigorous, non-trivial correctness test.
- The per-channel-isolation design (each of R/G/B independently attributable to one specific light) is genuinely
  strong test-oracle engineering — a regression that broke, say, only `DirectionalLight1`'s forwarding would fail
  exactly check 3 (and, more subtly, check 1's green channel) while leaving checks unrelated to that light
  unaffected, making failures highly diagnostic.
- `renderWith()`'s parameterized-helper design avoids the code duplication present in some sibling files' repeated
  per-case blocks.
- This file's own claim of a real, confirmed historical bug (light1/light2 silently dropped, lit-path emissive
  silently dropped) was independently corroborated against the current production source and found to be
  genuinely fixed.

## Final Assessment

A rigorously derived, well-engineered regression test for a real, confirmed-fixed multi-light + lit-path-emissive
bug in `BasicEffect::FillGpuDrawParams()`, with the same shared retry-loop oracle-strength gap (F1) as its
`combined_test`/`emissive_test` siblings and no other defects found.
