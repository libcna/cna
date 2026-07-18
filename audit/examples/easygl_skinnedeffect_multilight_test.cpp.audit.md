# Audit: examples/easygl_skinnedeffect_multilight_test.cpp

## Metadata

- Source file: `examples/easygl_skinnedeffect_multilight_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 893, `SkinnedEffect`
  `DirectionalLight1`/`DirectionalLight2` forwarding pixel test
- File type: C++ example/integration test
- Related production code: `SkinnedEffect::FillGpuDrawParams()` (`SkinnedEffect.cpp` lines
  340-371, the 3-light diffuse+specular forwarding fix), `EasyGLGraphicsBackend.cpp::
  EnsureSkinnedProgram()`'s fragment shader (`lightSum` computation, lines 3364-3367)
- XNA/FNA relevance: real XNA `SkinnedEffect` implements `IEffectLights` with 3 independent
  `DirectionalLight` slots, matching FNA's `Lighting.fxh` `ComputeLights()` which sums all 3
  lights' diffuse contributions, not just light 0 — this is a real, previously-missing behavior
  this test's own header says it fixed.
- Main related tests: cross-referenced against `easygl_environmentmapeffect_multilight_test.cpp`
  and `easygl_basiceffect_multilight_emissive_test.cpp` (outside this batch) for "the full FNA-
  reference derivation and discrimination-trick rationale," per this file's own header — the
  underlying `Lighting.fxh` formula is shared across all 3 stock-effect ports.

## Purpose

Verifies that `SkinnedEffect::FillGpuDrawParams()` forwards **all 3** directional lights
(previously only light 0 was forwarded, per the header's own account of the bug this test
exposed and fixed) by assigning each light a distinct, single-channel diffuse color
(red/green/blue) and checking that disabling or rotating any one light drops exactly its own
channel from the summed result, while leaving the other two channels unaffected.

## Executive Verdict

**Healthy.** All 3 checks' expected values were independently re-derived from the real geometry
(`kNx=0.8660254, kNz=-0.5`, chosen so `dot(N,-L)=0.5` when a light shares `kLightDir`) and the
actual `EnsureSkinnedProgram()` fragment shader formula, and every one matches exactly — this is a
carefully constructed, channel-isolated discrimination test, not a coincidental pass.

## Checklist Results

### API / XNA / FNA parity
`fx.DirectionalLight0/1/2.setEnabledProperty/setDirectionProperty/setDiffuseColorProperty` are
genuine XNA `IEffectLights`/`DirectionalLight` API members. Real XNA `SkinnedEffect` does expose
independent `DirectionalLight0`/`DirectionalLight1`/`DirectionalLight2` fields — correct parity.

### Behavioral correctness
Independently re-derived the geometry: `kNx=0.8660254 (~sqrt(3)/2), kNy=0, kNz=-0.5`,
`kLightDir=(0,0,1)`. `dot(N, -kLightDir) = dot((0.866,0,-0.5),(0,0,-1)) = 0.5` — confirmed matches
the comment's own claim ("Normal chosen so dot(-kLightDir, N) = 0.5 for all 3 lights when they
share kLightDir", line 57).

Traced against `EnsureSkinnedProgram()`'s fragment shader:
```
float dotL0=dot(N,-uLight0Dir); NdotL0=max(dotL0,0.0);
... (same for L1, L2)
vec3 lightSum=uLight0Diffuse*NdotL0+uLight1Diffuse*NdotL1+uLight2Diffuse*NdotL2;
vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;
```
With `uDiffuseColor` default `(1,1,1)`, `uEmissiveColor` default `(0,0,0)`:
- **Check 1** (all 3 lights sharing `kLightDir`, all enabled): `NdotL0=NdotL1=NdotL2=0.5`.
  `lightSum = 0.6*0.5*(1,0,0) + 0.6*0.5*(0,1,0) + 0.6*0.5*(0,0,1) = (0.3,0.3,0.3)`. `litRGB =
  (0.3,0.3,0.3)*1 = (0.3,0.3,0.3)` -> byte `(76.5,76.5,76.5) ≈ (77,77,77)` — matches expected
  `kExpectedAllLights(77,77,77,255)` (line 61) exactly.
- **Check 2** (light2 disabled): `FillGpuDrawParams()`'s `light2On ? ... : Vector3::Zero`
  gating (`SkinnedEffect.cpp` line 356-357) forces `light2Diffuse=(0,0,0)` regardless of
  `NdotL2`'s value — blue channel's `0.5*0.6=0.3` term drops entirely, R/G unaffected. Matches
  expected `(77,77,0,255)` (line 63) exactly.
- **Check 3** (light1 rotated to `kLight1DirOffAxis=(1,0,0)`): `dot(N,-dir1) = dot((0.866,0,-0.5),
  (-1,0,0)) = -0.866`, negative -> `NdotL1=max(-0.866,0)=0` — green channel's term drops entirely
  (not because the light is disabled, but because the surface faces away from it), R/B unaffected.
  Matches expected `(77,0,77,255)` (line 65) exactly.
All 3 derivations land on the *exact* expected byte values, confirming this is a rigorously
constructed, channel-isolated test — not a loose predicate like several of its `SkinnedEffect`
siblings in this batch (`combined`/`golden`/`identity_bones`), because here the lighting
contribution per channel is fully analytically tractable (single-light-per-channel design), unlike
`EnableDefaultLighting()`'s 3-point white-light scenario used elsewhere.

### Logic
`renderWith()`'s retry loop (lines 138-150, matching the `fog` sibling's identical pattern) guards
against reading a stale/blank frame. Consistent, reused pattern across both files (same author
convention, not independently reinvented differently in each file).

### Memory/resource lifetime
Fresh `SkinnedEffect fx(dev)` per `renderWith()` call — same correct no-cross-check-state-leak
pattern as the `fog` sibling.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52)` — consistent with the shard.

### Architecture
Explicit `GraphicsDeviceManager` construction with `kSize=64` (constructor lines 182-187) —
consistent with the `fog` sibling, unlike `combined`/`identity_bones`/`golden` which rely on the
`Game` fallback device. No functional difference, just a style variance across this batch (already
noted for the other files).

### Maintainability
Clean, well-commented derivation constants (`kNx`/`kNy`/`kNz` with an inline explanation of *why*
that specific normal was chosen) — good self-documentation, directly checkable (and checked above).

### Robustness
N/A — deterministic, no external input.

### Testing
This file is itself the test.

### Cross-file consistency
Same `RasterizerState::CullNone` Task-896 comment; same "Task 893" reference to the underlying
`FillGpuDrawParams()` fix as the file header's own framing. Confirmed `FillGpuDrawParams()`
(`SkinnedEffect.cpp` lines 350-360) genuinely forwards `DirectionalLight1`/`DirectionalLight2`
direction and diffuse with the same `lightXOn ? ... : Vector3::Zero` enabled-gating pattern as
light 0 — the fix this test's header claims is real and present in the current source, not just
asserted.

## Detailed Findings

No MEDIUM+ findings. This is one of the strongest-verified files in this batch — every check's
expected value was independently reconstructed from first principles (dot products, the real
shader formula) and matched exactly, with no reliance on tolerance-absorbed approximation or a
captured-once value (contrast with `easygl_skinnedeffect_golden_test.cpp`'s Finding F1 in this same
batch).

## Cross-File Observations

Companion to `easygl_skinnedeffect_fog_test.cpp` — both target distinct fields
`FillGpuDrawParams()` previously failed to forward (fog fields entirely; lights 1/2 entirely),
both share the retry-loop-on-blank-frame pattern, and both are held to the same tight (±8 for
this file, exact-value-friendly) tolerance rather than the wider ±30-40 tolerances used by the
`EnableDefaultLighting()`-based tests elsewhere in this batch — a sensible correlation between
"how analytically tractable is the expected value" and "how tight is the tolerance."

## Missing or Weak Tests

None found — the 3-check design (all-on baseline, disable one, rotate one) is a well-chosen
minimal set to prove per-light independence for both the enable-gate and the direction-dependent
`NdotL` term.

## Positive Findings

- Exceptionally well-designed channel-isolation technique: assigning each of the 3 lights its own
  RGB channel makes "did light N's own contribution genuinely drop" directly legible in the pixel
  read-back, rather than requiring a more complex combined-intensity inference.
- Every expected value was verified to match exactly (not just within a generous tolerance),
  the strongest form of evidence available short of instrumenting the shader directly.
- Correctly distinguishes disabling a light (Check 2) from rotating it off-axis (Check 3) — two
  different code paths in `FillGpuDrawParams()`/the shader (the `Enabled` gate vs. the `NdotL`
  clamp), both independently exercised.

## Final Assessment

One of the most rigorously self-verified files in this batch. No defects found; the test design
(single-channel-per-light) is a genuinely clever way to make a 3-light summation formula fully
analytically checkable at the pixel level.
