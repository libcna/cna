# Audit: examples/bgfx_basiceffect_specular_test.cpp

## Metadata

- Source file: `examples/bgfx_basiceffect_specular_test.cpp`
- Audit status: AUDITED (static; Bgfx is not in the D-P4 opportunistic-build feasibility list for this
  sandbox — no `cmake-build*` directory exists here)
- Subsystem: `examples-tests-bgfx` shard — `BasicEffect` real specular-highlight pixel test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_basiceffect_specular …)` / `cna_register_backend_test(NAME Bgfx_BasicEffect_Specular …)`, `cmake/Tests/BgfxTests.cmake:317-320`)
- XNA/FNA relevance: direct — `BasicEffect.SpecularColor`/`SpecularPower`/`DirectionalLight0.SpecularColor`,
  `EyePosition` derivation (`IEffectLights`)
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`'s half-vector Blinn-Phong specular term),
  `HLSL/Common.fxh` (`AddSpecular`: `color.rgb += specular*color.a`, applied after the texture×diffuse
  multiply), `DirectionalLight.cs` (`Enabled` zeroes both `DiffuseColor` and `SpecularColor`)
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp` (`FillGpuDrawParams()`
  lines 85-131), `src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d_vertexlit.sc`,
  `vs_lit_textured3d_vertexlit.sc` (half-vector math lines 55-59)

## Purpose

4-check pixel test proving `BasicEffect`'s real half-vector Blinn-Phong specular model (not a reflect-vector
approximation): (a) baseline eye position, (b) a different `EyePosition` producing a different specular value
(proves `EyePosition`-dependence), (c) `SpecularColor=(0,0,0)` producing pure diffuse+ambient (proves the
material gate), (d) `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (proves the light's
own `Enabled` gate covers specular too). Per the file's own header, this is the Bgfx counterpart of
`easygl_basiceffect_specular_test.cpp`; per its inline comment (lines 53-58), `kExpectedStraightOn` was
explicitly updated from 155 to 127 for Task 1104's per-vertex-lit dispatch change.

## Executive Verdict

**Needs attention** — checks (a), (c), (d) are solid and their expected constants were independently
re-derived by this audit to match the current per-vertex-lit formula almost exactly. Check (b), however, has
the **same underlying staleness problem this audit's own example report already identified in the EasyGL
sibling file** — but this Bgfx version is a materially weaker report of it: unlike the EasyGL file, this file
carries **no comment at all** disclosing that `kExpectedOffAxisEye=68` is a stale, pre-Task-1104 value (see F1).

## Checklist Results

### API / XNA / FNA parity
`setSpecularColorProperty`/`setSpecularPowerProperty`/`DirectionalLight0.setSpecularColorProperty` (lines
116-122) map correctly to FNA's `IEffectLights` surface. `EyePosition` is not a settable `BasicEffect`
property in FNA (derived internally from `View`), and this test correctly does not attempt to set it directly
— it varies `View`'s eye position instead (lines 124-125), matching `BasicEffect::FillGpuDrawParams()`'s own
derivation (`Matrix::Invert(View).getTranslationProperty()`, `BasicEffect.cpp` lines 126-127).

### Behavioral correctness
Re-derived the half-vector formula by hand for check (a) (`kEyeStraightOn=(0,0,3)`,
`kLightDirRaw=(0.5,0,-1)` normalized, `kNormal=(0,0,1)`, `SpecularPower=32`) — matches this audit's own
independent computation in `bgfx_basiceffect_preferperpixellighting_test.cpp.audit.md` (identical scene):
diffuse `≈0.18688`, Gouraud-averaged specular `≈0.3164`, total `≈0.50328×255≈128.3`, rounding to 128 vs. the
asserted **127** (1-unit gap, ordinary float/interpolation rounding, not a bug).

For check (b) (`kEyeOffAxis=(3,0,1)`), independently re-derived the per-vertex Gouraud average from scratch:
- `eyeVector_TL = normalize((3,0,1)-(-1,1,0)) = normalize(4,-1,1) ≈ (0.9428,-0.2357,0.2357)`.
- `eyeVector_BR = normalize((3,0,1)-(1,-1,0)) = normalize(2,1,1) ≈ (0.8165,0.4082,0.4082)`.
- `nL = (0.4472,0,-0.8944)` (same as every other check in this file).
- `h_TL = normalize(eyeVector_TL - nL) ≈ (0.3945,-0.1876,0.8994)`; `dot(h,N)≈0.8994`;
  `spec_TL=pow(0.8994,32)≈0.0336`.
- `h_BR = normalize(eyeVector_BR - nL) ≈ (0.2612,0.2887,0.9214)`; `dot(h,N)≈0.9214`;
  `spec_BR=pow(0.9214,32)≈0.0728`.
- Gouraud average `≈0.0532`. Total `=0.18688+0.0532=0.24008×255≈61.2` → rounds to **61**.
This independently-derived value (61) is **not** the file's asserted `kExpectedOffAxisEye(68,68,68)` — it is,
instead, essentially identical to the value the sibling EasyGL test's own header comment explicitly discloses
as "the real current (correct) per-vertex-lit rendered value," which that file's comment states is 61 against
its own stale constant of 68. This Bgfx file asserts the **exact same stale constant, 68**, with **zero
comment anywhere in the file acknowledging the discrepancy** — see F1.

Checks (c)/(d) re-derived and confirmed correct: (c) `SpecularColor=(0,0,0)` zeroes the specular sum
regardless of the per-light specular contribution, leaving `diffuse=0.18688×255≈47.65→48`, matching
`kExpectedNoSpecular(48,48,48)`. (d) `DirectionalLight0.Enabled=false` zeroes both `ld`/`ls` in
`FillGpuDrawParams()` (lines 85-87), leaving ambient-only `0.02×0.4×255≈2.04→2`, matching
`kExpectedLightDisabled(2,2,2)`.

### Logic
Check (b)'s own numeric derivation is **not** re-verified against the current formula anywhere in this file —
see F1. The `±10` tolerance in `matches()` (line 90) is the only reason `|61-68|=7` still passes.

### C++ correctness
No issues found beyond the tolerance-masking behavior noted in F1.

### Robustness
Checks (c)/(d) are the correct technique to isolate "does the material's own `SpecularColor` gate the term"
from "does a disabled light's `SpecularColor` get zeroed too" — two independently-failing hypotheses a naive
implementation could get wrong in different ways, and both are covered.

### Testing
Three of four checks are strong, evidence-backed pixel assertions with values independently re-derived by
this audit to match the current shader exactly. The fourth (check b) still achieves its stated *structural*
goal — proving `EyePosition`-dependence via `!matches(b,a)` (line 168), which does hold (68/61 both clearly
differ from 127/128) — but the specific `matches(b, kExpectedOffAxisEye)` assertion (lines 166-167) verifies
the wrong number and passes only because of tolerance overlap with a stale constant.

## Detailed Findings

### F1 — Check (b)'s expected constant is a stale pre-Task-1104 value, passing only by tolerance overlap, and — unlike the EasyGL sibling file — this file discloses none of it

- Severity: MEDIUM
- Confidence: HIGH (independently re-derived from the current shader/CPU formula by this audit; corroborated
  by the sibling EasyGL file's own explicit self-disclosure of the same numeric situation for the same scene)
- Category: test-coverage / correctness-of-test
- Location/symbol: `kExpectedOffAxisEye(68, 68, 68, 255)` (line 60); check `(b)` (lines 165-168)
- Evidence: this audit's own hand re-derivation (see Behavioral correctness above) computes the current,
  correct per-vertex-lit rendered value for this exact scene as **≈61**, not 68. The file's own inline comment
  block (lines 53-58) explicitly discusses updating `kExpectedStraightOn` from 155 to 127 for Task 1104, and
  even cross-references `bgfx_basiceffect_preferperpixellighting_test.cpp` for the dedicated dispatch test —
  demonstrating the file's author was actively aware of, and updating for, the Task 1104 vertex-lit dispatch
  change at the time of that edit — yet `kExpectedOffAxisEye` was left unchanged at its old value with no
  parallel note. This is a materially weaker report of the same underlying issue than
  `easygl_basiceffect_specular_test.cpp`, whose own header comment (see this audit's separate example report,
  `audit/examples/easygl_basiceffect_specular_test.cpp.audit.md`) explicitly states: *"per-vertex specular at
  TL/BR Gouraud-averages to ~61 total (rendered: 61, exact) -- close enough to the OLD per-pixel value at this
  point (~68) to still pass this test's own ±10 tolerance, so its own expected constant is left as the
  historical per-pixel value below, not changed."* The Bgfx file asserts the identical numeric pair (61 real
  vs. 68 asserted) but contains no equivalent acknowledgment anywhere.
- Why it matters: this check does not actually verify the current per-vertex-lit formula's output at the
  off-axis eye position — it verifies the output is *within 10* of a different, outdated (pre-Task-1104,
  always-per-pixel) formula's output. A regression that shifted the real per-vertex value from 61 to,
  say, 65 (which could itself be wrong, or right for a different reason) would pass unnoticed; a regression
  that moved it to 79 (just outside tolerance of the *stale* 68, but potentially a legitimate per-vertex-lit
  value under different rounding) would fail for the wrong reason. Because this Bgfx copy carries no
  disclosure comment, a future reader has no signal that this specific assertion is weaker than it appears —
  worse than the EasyGL sibling, where the comment itself flags the issue for anyone who reads it.
- FNA/XNA comparison: N/A (test-authoring issue; the underlying `BasicEffect` behavior itself was
  independently confirmed correct for this scene via check (a)'s exact re-derivation, matching this audit's
  separately-verified `bgfx_basiceffect_preferperpixellighting_test.cpp` numbers for the identical straight-on
  scene).
- Related files: `examples/easygl_basiceffect_specular_test.cpp` has the identical numeric situation, already
  documented in this audit's own prior example report (`easygl_basiceffect_specular_test.cpp.audit.md`, F1) —
  this is the same class of stale-constant issue recurring across two backends' sibling test files, but the
  EasyGL copy at least self-discloses it.
- Suggested future action (not implemented by this audit): update `kExpectedOffAxisEye` to the currently-
  correct per-vertex-lit value (~61, ideally re-derived to more decimal precision), or at minimum add a
  disclosure comment matching the EasyGL sibling's, so a future reader/maintainer is not misled about what
  this specific assertion actually verifies.

### F2 — Header comment's cull-state claim is stale (shared with 7 sibling files)

- Severity: MEDIUM
- Confidence: HIGH
- Category: documentation-accuracy / stale-comment
- Location/symbol: header comment lines 7-11 (`"tracked as Task 896, not fixed there or here"`)
- Evidence: same as recorded for all other files in this batch — `b6a00bc6 fix(Task 896): push
  GraphicsDevice's real default RasterizerState to all 3 backends` is confirmed (`git merge-base
  --is-ancestor`) as an ancestor of the current `HEAD`; `GraphicsDevice.cpp` line 207 confirms the fix is
  live. This file's last content change is commit `0cb4a591` (Jul 16), postdating `b6a00bc6` (Jul 7 19:39).
- Why it matters: same reasoning as recorded across the batch — the `RasterizerState::CullNone` workaround
  (line 143) remains correct and necessary, but the stated cause ("Bgfx-only, unaddressed") is now inaccurate.
- FNA/XNA comparison: N/A.
- Related files: shared with all 7 other files in this batch.
- Suggested future action (not implemented by this audit): refresh the comment.

## Cross-File Observations

- This file, `bgfx_basiceffect_preferperpixellighting_test.cpp`, and `bgfx_basiceffect_one_light_test.cpp` all
  exercise the same underlying Blinn-Phong implementation (`vs_lit_textured3d_vertexlit.sc` /
  `fs_lit_textured3d_vertexlit.sc` half-vector math); F1 only affects this file's check (b), not the shared
  production code, which this audit independently confirmed correct via check (a)'s exact re-derivation.
- The header comment's Task-1104 correction narrative for `kExpectedStraightOn` (lines 53-58) is corroborated
  by `git log` (`0cb4a591 feat(Task 1104): Bgfx real per-vertex-lit shader + PreferPerPixelLighting dispatch`)
  — but the same authoring pass that updated `kExpectedStraightOn` did not extend the same care to
  `kExpectedOffAxisEye`, an inconsistency within this file's own edit history, not merely an artifact of it
  predating some later change (unlike F2, which genuinely does predate the relevant fix for most sibling
  files).

## Missing or Weak Tests

See F1 — `kExpectedOffAxisEye`'s own value should be refreshed to match the currently-computed per-vertex-lit
result, and ideally accompanied by the same disclosure the EasyGL sibling already carries.

## Positive Findings

- Checks (a)/(c)/(d) are precise, correctly-scoped, and their numeric derivations were independently
  confirmed by this audit against both FNA's `Lighting.fxh` formula and the live shader source.
- `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (check d) is a genuinely easy-to-miss
  XNA behavior (`DirectionalLight.Enabled`'s C# setter zeroes two separate GPU parameters) and this test
  explicitly isolates it.
- The file's inline comment about updating `kExpectedStraightOn` from 155→127 (lines 53-58) shows real,
  demonstrated engineering care was applied to at least one of this file's constants during the Task 1104
  transition — making the complete silence around `kExpectedOffAxisEye`'s parallel staleness (F1) more
  notable by contrast, not less.

## Final Assessment

A mostly strong specular test, let down by one stale numeric constant (check b) that — unlike its EasyGL
sibling, which explicitly discloses the exact same situation — carries no acknowledgment at all here, despite
this file's own edit history showing its author was actively revisiting other constants for the same Task
1104 transition at the same time. Worth a follow-up to tighten check (b) to the currently-correct value (~61).
