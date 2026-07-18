# Audit: examples/easygl_skinnedeffect_fog_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 900, `SkinnedEffect` linear-fog pixel test
- File type: C++ example/integration test
- Related production code: `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines
  397-403 for the fog fields), `EasyGLGraphicsBackend.cpp::EnsureSkinnedProgram()`'s
  `vFogFactor` computation (lines 3322-3331) and its use in the fragment shader
  (`FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor);`, line 3395)
- XNA/FNA relevance: `IEffectFog`/`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` are real XNA 4.0
  `SkinnedEffect` members (`FNA-XNA/FNA/.../SkinnedEffect.cs` implements `IEffectFog`).
- Main related tests: this file's header states it found and fixed a **real production bug**:
  `SkinnedEffect::FillGpuDrawParams()` never forwarded fog fields to the GPU at all prior to
  Task 900, silently making fog a total no-op for `SkinnedEffect` on every backend.

## Purpose

Verifies `SkinnedEffect`'s fog contract end-to-end: fog disabled (pass-through), fog at 50%
density (linear mix), and full fog (fully replaced by fog color) — using an emissive-only material
(no directional-light contribution) to isolate fog from lighting/skinning.

## Executive Verdict

**Healthy**, and notable for documenting a genuine prior defect it fixed (fog silently doing
nothing for `SkinnedEffect`) plus a second, subtler formula-correctness fix (Task 1111's real fog
dot-product vs. a coincidentally-right-at-one-point falloff approximation). Independently
re-derived all 3 expected pixel values against the current shader source and they match exactly;
found one small, non-functional inaccuracy in this file's own commentary (see Finding F1).

## Checklist Results

### API / XNA / FNA parity
`setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` are real
XNA `IEffectFog` members, correctly used. `EnableDefaultLighting()` is intentionally *not* called
here — the test instead sets `EmissiveColor` directly and leaves lighting off, a valid way to
isolate fog from the (already separately tested, per Cross-File Observations) lighting formula.

### Behavioral correctness
Independently re-derived all 3 checks against the real shader (`EasyGLGraphicsBackend.cpp` lines
3331, 3395):
```
vFogFactor = fogEnabled ? (|FogEnd-FogStart|<1e-6 ? 0.0 : clamp((z+FogEnd)/(FogEnd-FogStart),0,1)) : 1.0
FragColor.rgb = mix(FogColor, color, vFogFactor)
```
- **(a)** fog OFF, z=0: `vFogFactor=1.0` -> `mix(fogColor, color, 1.0) = color`. With
  `EmissiveColor=(0,0,1)` (blue), no lights enabled (see Finding F1), `diffuseColor` default
  `(1,1,1)`, texture white: `litRGB = 0 + emissive = (0,0,1)`, final `= (0,0,255,255)` — matches
  test's expected pure blue (line 163).
- **(b)** fog ON, z=0, `FogStart=-0.9`, `FogEnd=0.9`, `FogColor=(1,0,0)` red:
  `vFogFactor = clamp((0+0.9)/(0.9-(-0.9)),0,1) = clamp(0.5,0,1) = 0.5` ->
  `mix(red, blue, 0.5) = (0.5,0,0.5)` -> byte `(128,0,128)` — matches expected exactly (line 167).
- **(c)** fog ON, `z=-0.9=FogStart`: `vFogFactor = clamp((-0.9+0.9)/1.8,0,1) = 0` ->
  `mix(red, blue, 0) = red` — matches expected `(255,0,0)` exactly (line 171).
All 3 derivations land on the *exact* expected byte values (not just "within tolerance by luck"),
which is strong evidence this test genuinely encodes the real formula rather than a coincidentally
passing approximation — directly relevant given the file's own header explains a previous formula
*was* coincidentally right only at one z value and had to be corrected (Task 1111).

### Logic
`renderQuad()`'s retry loop (lines 135-146, up to 20 iterations, breaking once a non-black pixel is
read) guards against reading back a stale/blank frame before the GPU has actually presented — a
reasonable, if slightly unusual, defensive pattern; worth checking whether other sibling tests in
this shard need the same guard or whether this file is compensating for a timing quirk specific to
how `SkinnedEffect` + fog is drawn. Not flagged as a defect (no evidence it's masking anything;
each of the 3 draws clears to black first specifically so "still black" is a meaningful, detectable
failure-to-render signal, not just a fixed retry count hiding a real problem).

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` (line 62) — consistent with every sibling.

### Robustness
`renderQuad()` re-creates a fresh `SkinnedEffect fx(dev)` for every call (line 111) rather than
reusing one across the 3 checks — correct, avoids any possible cross-check dirty-flag/state leakage
between checks a shared-instance approach might risk.

### Testing
This file is itself the test.

### Cross-file consistency
The `vFogFactor` formula and its derivation comment (lines 3322-3331 of
`EasyGLGraphicsBackend.cpp`) explicitly cross-reference "every sibling `*_fog_test.cpp`" using the
same `FogStart=-0.9`/`FogEnd=0.9` convention — confirmed this file follows that exact convention
(line 20, 166, 171), not a one-off value choice.

## Detailed Findings

### F1 — Header/inline comment's "DirectionalLight0 left at its own default-disabled state" is inaccurate

- Severity: LOW
- Confidence: HIGH (read both `SkinnedEffect`'s constructor and `DirectionalLight`'s own default
  constructor directly)
- Category: documentation / test-comment accuracy (not a functional defect)
- Location/symbol: file header, lines 22-26 ("no directional light contribution, since
  `DirectionalLight0` is left at its own default-disabled state")
- Evidence: `SkinnedEffect`'s constructor (`SkinnedEffect.cpp` line 42) explicitly calls
  `DirectionalLight0.setEnabledProperty(true);` — so `DirectionalLight0.Enabled` is actually
  **true** by default, not "disabled" as the comment states. The reason this test still correctly
  sees zero light-0 contribution is that `DirectionalLight0`'s `DiffuseColor` (and `Direction`)
  remain at `DirectionalLight`'s own default-constructed `Vector3::Zero` (`DirectionalLight.cpp`
  lines 6-11) — a diffuse color of `(0,0,0)` contributes nothing to `lightSum` regardless of
  whether `NdotL` is 0 or 1, independent of the `Enabled` flag's actual value.
- Why it matters: purely a documentation-accuracy nit — the test's numeric result is unaffected
  either way (zero diffuse color zeroes the term regardless of the enabled flag), so there is no
  functional risk. Flagged because a future maintainer reading this file's comment could be misled
  into thinking `DirectionalLight0.Enabled` defaults to `false` (it does not — only
  `DirectionalLight1`/`DirectionalLight2` do, per `SkinnedEffect`'s constructor only explicitly
  enabling light 0).
- FNA/XNA comparison: real XNA's `SkinnedEffect` also enables only `DirectionalLight0` by default
  (matches `BasicEffect`'s own convention) — so the *enabled* behavior CNA implements is correct;
  only this file's own inline explanation of *why* the test result comes out zero is imprecise.
- Suggested future action (not implemented by this audit): reword the comment to say
  "DirectionalLight0's diffuse color defaults to zero" rather than "is left at its own
  default-disabled state," if this file is touched again for other reasons.

## Cross-File Observations

Complements `easygl_skinnedeffect_multilight_test.cpp` (same batch) — that file isolates and
verifies the 3-directional-light *summing* formula (also a real bug this project fixed, per its own
header: Task 893), while this file isolates fog. Together they cover the two `FillGpuDrawParams()`
fixes referenced by both files' headers (Task 893 lights, Task 900/1111 fog) with non-overlapping,
complementary pixel checks.

## Missing or Weak Tests

None beyond the documentation nit above — the 3 checks (off/50%/full) are the natural minimum set
for a linear fog falloff and are well-chosen (boundary at `FogStart`, midpoint, and the disabled
case).

## Positive Findings

- The header comment's own account of a real, previously-shipped bug (fog was a complete no-op
  for `SkinnedEffect`) plus a second, independently-tracked formula-correctness fix (Task 1111) is
  exactly the kind of concrete, falsifiable engineering history this audit values — and both fixes
  were independently confirmed correct by re-deriving the current shader's formula from scratch.
- All 3 expected pixel values land exactly (not just within tolerance) when independently
  re-derived, which is strong evidence of a genuinely correct, non-coincidental test.

## Final Assessment

A well-verified fog test with real, checkable engineering history behind it. Only issue found is a
cosmetic inaccuracy in the file's own explanatory comment (Finding F1, LOW severity, no functional
impact).
