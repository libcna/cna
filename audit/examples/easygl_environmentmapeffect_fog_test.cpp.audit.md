# Audit: examples/easygl_environmentmapeffect_fog_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_fog_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect` fog (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EnvironmentMapEffect::OnApply()`/`FillGpuDrawParams()`
  (`src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp:286-467`),
  `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()` (`src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp:3145-3270`)
- FNA reference: `Graphics/Effect/EffectHelpers.cs` (`SetFogVector`), `Graphics/Effect/StockEffects/HLSL/Common.fxh`
  (`ComputeFogFactor`/`ApplyFog`), `EnvironmentMapEffect.fx` (`vout.Specular.a = ComputeFogFactor(vin.Position)`).
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_Fog` (`cmake/Tests/EasyGLTests.cmake:559-561`).

## Purpose

Task 900 test, with a documented real-bug provenance: the file's own header (lines 4-21) states that
writing this test *found* `EnvironmentMapEffect::FillGpuDrawParams()` never forwarding
`FogEnabled`/`FogColor`/`FogStart`/`FogEnd` to the GPU at all, and that a Task 1111 follow-up corrected the
fog *formula* itself (the naive `(FogEnd-Z)/(FogEnd-FogStart)` falloff was only coincidentally right at
`Z=0`). This audit independently verified both claims against the current source rather than taking the
comment at face value.

## Executive Verdict

**Healthy** (post-fix). `FillGpuDrawParams()` at `EnvironmentMapEffect.cpp:460-466` does forward all 4 fog
fields today, and the EasyGL vertex shader's `vFogFactor` formula (line 3195 of
`EasyGLGraphicsBackend.cpp`) matches the file's own hand-derivation exactly for all three test cases,
verified below by direct substitution.

## Checklist Results

### API / XNA / FNA parity
`PASS`. `setFogEnabledProperty`/`setFogColorProperty`/`setFogStartProperty`/`setFogEndProperty` match
FNA's `IEffectFog.FogEnabled`/`FogColor`/`FogStart`/`FogEnd`. FNA's own `FogStart`/`FogEnd` defaults are `0`/`1`
(`EnvironmentMapEffect.cs:63-64`), matching CNA's `fogStart_ = 0.0f; fogEnd_ = 1.0f;`
(`EnvironmentMapEffect.hpp:392-393`) — not directly exercised here since the test always sets both
explicitly, but confirmed consistent.

### Behavioral correctness
`PASS`, verified by direct formula substitution against `EnsureEnvMapped3DProgram`'s vertex shader (line 3195):
```
vFogFactor = fogEnabled ? (|FogEnd-FogStart|<1e-6 ? 0 : clamp((z+FogEnd)/(FogEnd-FogStart),0,1)) : 1
```
and fragment shader's `FragColor.rgb=mix(uFogColor,FragColor.rgb,vFogFactor)` (line 3240).
  - Case (a), fog OFF (line 170-171): `vFogFactor=1` unconditionally → `mix(fogColor, blue, 1) = blue`.
    Matches `kBlue` expectation.
  - Case (b), `Z=0, FogStart=-0.9, FogEnd=0.9` (line 174-176): `vFogFactor = clamp((0+0.9)/(0.9-(-0.9)),0,1)
    = clamp(0.9/1.8,0,1) = 0.5` → `mix(red, blue, 0.5) = (128,0,128)`. Matches the test's own expected
    `Color(128,0,128,255)` exactly.
  - Case (c), `Z=FogStart=-0.9` (line 179-180): `vFogFactor = clamp((-0.9+0.9)/1.8,0,1) = 0` →
    `mix(red, blue, 0) = red`. Matches `kRed`.
  All three derivations check out precisely against both the file's own comments and the actual shader code
  — this is not a coincidental pass; the formula is genuinely exercised at 3 meaningfully distinct points
  (off / 50% / 100%).

### Logic
`PASS`. `EnvironmentMapAmount=0`/`EnvironmentMapSpecular=0` (lines 123-124) correctly zero out the cube-map
contribution so `baseColor = EmissiveColor` exactly (verified: with `envMapAmount=0`, `blendFactor` from the
Fresnel branch still computes `pow(...)*uEnvMapAmount` which is `0` regardless of Fresnel state since
`uEnvMapAmount=0`, and specular term `uEnvMapSpecular*envSample.a*combinedAlpha` is also `0` since
`uEnvMapSpecular=(0,0,0)`) — isolating fog cleanly from the reflection math as claimed. `EmissiveColor=(0,0,1)`
(blue) with default `DiffuseColor=(1,1,1)` and no lights disabled relies on `DirectionalLight0`'s default
zero diffuse (same latent assumption noted in the eyeposition-test report — see Cross-File Observations).

### Memory/resource lifetime
`PASS`. `makeSolidCube()`/`renderQuad()` return/take ownership correctly; `cube`, `tex` outlive the
per-call `EnvironmentMapEffect fx` local.

### C++ correctness
`PASS`. No UB found. `renderQuad()`'s retry loop (lines 143-151) reads `got` by value each iteration and
returns the last value regardless of whether a non-black frame was ever found — see F1 below for a
robustness nuance in this pattern.

### Performance
`N/A` for a one-shot test.

### Architecture
`PASS`. XNA-facing API only.

### Maintainability
`PASS`. The header comment doubles as a changelog explaining *why* the fog formula looks the way it does
(the `FogStart==0` degenerate case note, lines 19-21, is a genuinely useful warning against reusing this
file's *old* magic numbers without re-deriving them — a real trap the comment explicitly protects future
maintainers from).

### Robustness
See F1.

### Testing
This is itself a test file. See "Missing or Weak Tests."

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — `renderQuad()`'s up-to-20-frame retry loop returns the last (possibly still-black) frame silently on exhaustion

- Severity: LOW
- Confidence: HIGH
- Category: test-robustness
- Location/symbol: `EnvironmentMapEffectFogTest::renderQuad()`, lines 141-153
- Evidence:
  ```cpp
  for (int i = 0; i < 20; ++i)
  {
      dev.Clear(kBlack);
      dev.setBlendStateProperty(BlendState::Opaque);
      dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
      got = readCenter(dev);
      if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
          break; // skip blank/black frames
  }
  return got;
  ```
  If all 20 iterations legitimately produce a black readback (e.g. a genuine rendering regression that
  makes the quad invisible, as opposed to a benign warm-up frame), the function silently returns black
  after exhausting the loop, and the caller's `check()` then correctly reports `FAIL` — so this is not a
  false-pass risk. The risk is purely diagnostic: a real "nothing rendered" regression and a benign
  "backbuffer wasn't ready yet" transient produce an identical code path (loop runs to completion, returns
  whatever the last frame was), so a failure here gives no signal about which of the two happened.
- Why it matters: low-severity because it doesn't cause a false pass, but it means a future regression that
  makes case (a)'s "fog OFF" check fail would print an unhelpfully generic `[FAIL] (a) fog OFF: ... got=(0,0,0)`
  regardless of whether the actual cause is "quad culled/not drawn at all" vs. "backbuffer readback timing" —
  wasting debugging time distinguishing a real defect from test infrastructure noise.
- FNA/XNA comparison: N/A (CNA/EasyGL test-infrastructure concern, not an XNA API behavior).
- Related files: this retry-loop idiom (with the identical "skip blank/black frames" comment) is used in 84
  files across `examples/` (grep-confirmed), so this is a project-wide convention, not unique to this file —
  flagged here as a per-file observation, not a novel defect.
- Suggested future action (not implemented by this audit): consider having the loop print a distinct
  `[WARN] exhausted N retries, still black` line on exhaustion so a genuine "never rendered" regression is
  distinguishable at a glance from "took 3 retries to warm up."

## Cross-File Observations

- Same `DirectionalLight0`-default-zero-diffuse dependency as noted in the `_eyeposition_test.cpp` report
  (F1 there) — this file also never explicitly disables/zeroes `DirectionalLight0`, relying on its default
  diffuse color being black.
- The retry-loop pattern (F1 above) is **not** used by `_eyeposition_test.cpp`, `_fresnel_test.cpp`,
  `_fresnel_gradient_test.cpp`, `_specular_test.cpp`, or `_worldtransform_test.cpp` in this same batch — only
  this file and `_multilight_test.cpp` use it. Both are the two newest tests in this batch (Task 900 and
  Task 890 respectively per their own header comments), suggesting the retry idiom was adopted after those
  two tests hit a real backbuffer-readback timing issue during development, while the older sibling tests in
  this batch never needed it. Worth checking during the `backend-easygl` shard audit whether this points to
  a genuine (if rare) readback-timing quirk specific to newly-added shader variants' first-use compile/link
  path, vs. pure historical inconsistency.

## Missing or Weak Tests

- Does not test `FogStart == FogEnd` (the degenerate case the shader's own comment calls out — "guarded, not
  sign-clamped" — `vFogFactor=(uFogEnabled>0.5)?((abs(uFogEnd-uFogStart)<1e-6)?0.0:...)`). This exact
  degenerate branch is untested by any of the three cases here (case (a) never reaches the branch since fog
  is disabled; cases (b)/(c) both use `FogStart=-0.9, FogEnd=0.9`, well clear of equality). Given the file's
  own comment about `FogStart==0` foot-guns, testing the `FogStart==FogEnd` branch explicitly would close a
  real, self-identified gap.
- Does not test `FogEnd < FogStart` (an inverted range), which the file's own header comment (lines 13-19)
  explicitly says the *old* formula silently mishandled — the fix's correctness for this specific case is
  asserted in the comment but not verified by any of the 3 in-file checks.

## Positive Findings

- Rare and valuable: the header comment documents an actual bug this test *found* (missing fog forwarding)
  and a second, subtler bug found *after* the first fix (wrong falloff formula, Task 1111) — this audit
  independently confirmed both are now fixed in `EnvironmentMapEffect.cpp`/`EasyGLGraphicsBackend.cpp`,
  giving high confidence this is a real regression test, not a test written to match whatever the code
  happened to do.
- The 3 test cases (off / 50% / full) meaningfully exercise 3 different points on the fog curve rather than
  just "on vs. off," which is exactly the kind of test that would have caught the Task 1111 formula bug the
  header describes.

## Final Assessment

A genuine, well-evidenced regression test with an unusually transparent bug-fix history in its own header
comment, independently verified against the current `EnvironmentMapEffect.cpp` and EasyGL shader source. No
correctness defects found in the file itself; the one LOW finding (F1) is a diagnostic-quality nit in the
retry-loop idiom shared project-wide, and two real coverage gaps (`FogStart==FogEnd`, inverted range) are
noted for future work.
