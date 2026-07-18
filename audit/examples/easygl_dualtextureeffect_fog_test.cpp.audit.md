# Audit: examples/easygl_dualtextureeffect_fog_test.cpp

## Metadata

- Source file: `examples/easygl_dualtextureeffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (`examples-tests-easygl` shard)
- File type: C++ example/integration test, registered as CTest `EasyGL_DualTextureEffect_Fog`
  (`cmake/Tests/EasyGLTests.cmake:1168-1170`, `cna_test_easygl_dualtextureeffect_fog`)
- Related production code: `DualTextureEffect::FillGpuDrawParams()`
  (`DualTextureEffect.cpp:248-275`, forwards `fogEnabled_`/`fogColor`/`fogStart_`/`fogEnd_` to
  `GpuDrawParams`), `EasyGLGraphicsBackend::EnsureDualTextured3DProgram()`
  (`EasyGLGraphicsBackend.cpp:3009-3070`, vertex-shader `vFogFactor` computation and fragment-shader
  `mix(uFogColor, FragColor.rgb, vFogFactor)`).
- XNA/FNA relevance: `IEffectFog` (`FogEnabled`/`FogColor`/`FogStart`/`FogEnd`), FNA's
  `EffectHelpers.SetFogVector` fog-vector computation (`worldView` dot product), and
  `Common.fxh`'s `ApplyFog`/`ComputeFogFactor`.
- Main related tests: none other in this batch cover fog; `_combined_test.cpp` explicitly excludes
  fog from its capstone scene (documented reasoning cross-referenced there).

## Purpose

`DualTextureFogTest` verifies `DualTextureEffect`'s distance-fog blending via a 3-point Z-sweep
(`z=FogStart` full fog, `z=FogEnd` no fog, `z=0` halfway) to prove the blend is a genuine
interpolation rather than an on/off switch. Its header comment documents a real bug this test found
and fixed: `FillGpuDrawParams()` previously never forwarded fog parameters at all for
`DualTextureEffect` (a total no-op), and `EnsureDualTextured3DProgram()`'s shader had no fog uniforms
whatsoever (unlike the shared per-stride BasicEffect-family shaders, which already had fog
infrastructure `DualTextureEffect` doesn't reuse since it has its own dedicated program). Correct
placement/registration.

## Executive Verdict

**Mostly healthy for what it tests, but the test can only ever exercise the identity-transform special
case of fog, and the production fog formula it validates is provably not equivalent to FNA's real
per-vertex fog-vector computation for any non-identity `World`/`View`** (see F1) — a real, currently
undetectable gap given this file (and every other 3D-fog test in the project, per the project's own
prior audit memory) always leaves `World`/`View`/`Projection` at their default identity values.

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` — correct
XNA-property-style names matching `IEffectFog`.

### Behavioral correctness
Verified the full expected-value table by hand against the shader formula
(`EasyGLGraphicsBackend.cpp:3036`,
`vFogFactor=(uFogEnabled>0.5)?(abs(uFogEnd-uFogStart)<1e-6?0:clamp((aPos.z+uFogEnd)/(uFogEnd-uFogStart),0,1)):1.0`,
then `mix(uFogColor, FragColor.rgb, vFogFactor)`), with `kFogStart=-0.9`, `kFogEnd=0.9`,
`kDiffuse=(0.8,0.2,0.4)`, `kFogColor=(0.1,0.6,0.9)`, pre-fog material color ≈
`white×2×gray(0.502)×diffuse ≈ 1.00392×diffuse`:
- `z=kFogEnd=0.9`: `vFogFactor=(0.9+0.9)/(1.8)=1.0` → unblended material color
  `(0.8,0.2,0.4)×1.00392×255 ≈ (205,51,102)` — matches `kExpectedNoFog(205,51,102)` exactly (line 68).
- `z=kFogStart=-0.9`: `vFogFactor=(-0.9+0.9)/1.8=0.0` → pure fog color
  `(0.1,0.6,0.9)×255=(25.5,153,229.5)→(26,153,230)` — matches `kExpectedFullFog(26,153,230)` (line 69).
- `z=0`: `vFogFactor=(0+0.9)/1.8=0.5` → average of the two:
  `R=(25.5+204.8)/2≈115`, `G=(153+51.2)/2≈102`, `B=(229.5+102.4)/2≈166` — matches
  `kExpectedHalfFog(115,102,166)` (line 70).
All three derivations check out exactly against both the shader formula and the file's own expected
constants — the test's assertions are internally correct and the shader currently matches them.

### Logic
Same "retry up to 20 times until non-black" pattern as `_combined_test.cpp` (`renderAtZ()`, lines
132-144) — same LOW-severity masking risk noted there, not re-elaborated here.

### Memory/resource lifetime
`texWhite`/`texGray` declared once, reused across all 3 `renderAtZ()` calls with a fresh
`DualTextureEffect` per call — correct, no lifetime issue.

### C++ correctness
Correctly includes `<cstdlib>`.

### Performance / Thread safety / Portability
N/A for a single-frame CI test / no platform-specific code.

### Architecture
Correct XNA-only public API usage.

### Robustness
N/A (test file).

### Testing / Cross-file consistency
See F1 below — this is the substantive finding for this file.

## Detailed Findings

### F1 — Fog is computed from raw local-space vertex Z, not a proper view-space depth; this test can only validate the identity-transform special case

- Severity: MEDIUM
- Confidence: HIGH (traced directly in the vertex shader source and corroborated against FNA's real
  fog-vector formula)
- Category: behavioral correctness / architecture / test-coverage
- Location/symbol: `EnsureDualTextured3DProgram()` vertex shader, `EasyGLGraphicsBackend.cpp:3036`:
  `vFogFactor=...clamp((aPos.z+uFogEnd)/(uFogEnd-uFogStart),0.0,1.0)...` — uses `aPos.z`, the raw
  incoming local-space vertex position's Z component, not any `World`/`View`-transformed depth.
  Compare with `DualTextureEffect::OnApply()` (`DualTextureEffect.cpp:192-214`), which *does*
  correctly compute a proper world-view-space fog vector
  (`worldView_.M13/M23/M33/M43`-based dot product, matching FNA's
  `EffectHelpers.SetWorldViewProjAndFog` exactly) into `fogVectorParam_` — but
  `DualTextureEffect::FillGpuDrawParams()` (`DualTextureEffect.cpp:248-275`) never forwards that
  computed fog vector to the GPU at all; it only forwards the raw scalar `fogStart_`/`fogEnd_`
  fields, which the shader then re-derives fog purely from object-space `aPos.z`, silently assuming
  `World=View=Identity`.
- Evidence this test cannot detect the gap: `DualTextureFogTest` never sets `World`/`View`/
  `Projection` away from their default identity values (`DualTextureEffect`'s own field defaults,
  `DualTextureEffect.hpp:264-266`), so `aPos.z` and the "correct" view-space Z are numerically
  identical in every case this file exercises — the shader's simplification is invisible here by
  construction, not because it's actually correct in general.
- Why it matters: for any real scene where the camera moves or an object is transformed (the normal
  case for a game, as opposed to this fixed-camera unit test), fog computed from local-space `aPos.z`
  would use each vertex's un-transformed coordinate rather than its true camera-relative depth —
  producing visibly wrong fog falloff (e.g. objects at different world positions but the same local
  mesh Z would fog identically; a translated/rotated object's fog would not track its actual depth
  from the camera at all).
- FNA/XNA comparison: FNA's real `DualTextureEffect` fog pipeline computes the fog vector from
  `world*view` (`EffectHelpers.SetWorldViewProjAndFog`), then the vertex shader dots the object-space
  position against that vector (`Common.fxh`'s `ComputeFogFactor`) — i.e., FNA's approach *does*
  correctly account for `World`/`View`, unlike this shader's `aPos.z` shortcut.
- Related files: this exact "object-space-only fog" pattern was already independently identified as a
  cross-cutting EasyGL limitation in prior project audit history (affecting other effects' fog paths
  too, not unique to `DualTextureEffect`) — this finding corroborates that it also applies to
  `DualTextureEffect`'s dedicated shader specifically, via direct source inspection of
  `EnsureDualTextured3DProgram()`.
- Suggested action (not implemented by this audit): either forward the already-correctly-computed
  `fogVectorParam_`/`worldView_`-based fog vector through `FillGpuDrawParams()` and consume it in the
  shader (matching FNA's real per-vertex dot-product), or add a second fog test in this project that
  sets a non-identity `World`/`View` and asserts on the *now-known-wrong* result, so the gap is
  tracked by a (currently failing, clearly labeled) test rather than remaining invisible.

## Cross-File Observations

- This same "identity-transform-only, `aPos.z`-based fog" limitation likely also affects
  `EnsureDualTexturedColored3DProgram()` (the stride-24 sibling program, `EasyGLGraphicsBackend.cpp:
  3072-3143`), which uses the identical `vFogFactor` formula — worth a targeted check in that
  program's own audit pass (not part of this batch) or a `VertexColorEnabled`+fog combined test.

## Missing or Weak Tests

- No test in this project (as far as this batch shows) exercises `DualTextureEffect` fog with a
  non-identity `World`/`View` matrix — see F1. This is the single most consequential testing gap
  found in this batch, since it hides a real, traceable behavioral divergence from FNA.

## Positive Findings

- The 3-point Z-sweep design (full/none/half fog) is a genuinely stronger test than a single
  on/off assertion would be — it directly proves interpolation, not just a threshold switch, which is
  exactly the kind of test the file's own comment says it's modeled after (Task 378's AlphaTestEffect
  precedent).
- The header comment is unusually honest about a subtlety a reader would likely get wrong: "with
  `View=Identity`, `FogStart` is the FULLY FOGGED boundary here, not the unfogged one the property
  name might suggest" — correctly flags a counter-intuitive but verified-correct aspect of the
  formula rather than glossing over it.
- Explicitly and correctly scopes out Vulkan/Bgfx (documented as having zero fog GPU implementation)
  rather than adding a test that would encode a known no-op as a false positive.

## Final Assessment

The test itself is internally correct and does genuinely validate interpolation-style fog blending
for the one scenario it exercises, but that scenario (identity `World`/`View`) is exactly the case
that hides a real, source-verified divergence from FNA's fog-vector formula — the production shader
computes fog from raw local-space Z rather than a proper camera-relative depth, and no test in this
batch (or, per the file's own honest framing, likely elsewhere in the project) would catch a
regression or a real-scene failure of this simplification.
