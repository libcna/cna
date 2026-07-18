# Audit: examples/easygl_environmentmapeffect_multilight_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_multilight_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test for `EnvironmentMapEffect` multi-light forwarding
  (`examples-tests-easygl` shard)
- File type: C++ example/integration test (`Game`-subclass, hand-rolled `main()`)
- Related production code: `EnvironmentMapEffect::FillGpuDrawParams()`
  (`src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp:428-449`),
  `EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()` fragment shader (lines 3222-3229)
- FNA reference: `Graphics/Effect/StockEffects/HLSL/Lighting.fxh`'s `ComputeLights()` (sums up to 3
  directional lights' `max(dot(-Direction,N),0)*DiffuseColor` before multiplying by material `DiffuseColor`
  and adding `EmissiveColor`), `EnvironmentMapEffect.fx`'s `#define DirLightNSpecularColor float3(0,0,0)`
  (hardcoding no per-light specular, since this effect's own "specular" concept is the env-map alpha term).
- Registered as CTest target: `EasyGL_EnvironmentMapEffect_MultiLight` (`cmake/Tests/EasyGLTests.cmake:616-618`).

## Purpose

Task 890 test. Documents (lines 13-15) that prior to this task, `EnvironmentMapEffect::FillGpuDrawParams()`
forwarded only `DirectionalLight0` to the GPU, silently dropping `DirectionalLight1`/`DirectionalLight2`
regardless of their `Enabled`/`DiffuseColor` settings. Uses a per-channel-color discrimination trick
(each light's diffuse color touches exactly one RGB channel) so a dropped light's contribution is
unambiguous — mirrors the equivalent `BasicEffect` test from Task 885, per the file's own comment.

## Executive Verdict

**Healthy** (post-fix). `FillGpuDrawParams()` (`EnvironmentMapEffect.cpp:439-449`) now forwards
`DirectionalLight1`/`DirectionalLight2`'s direction and (enabled-gated) diffuse color independently, matching
this test's 3 checks exactly by direct arithmetic re-derivation.

## Checklist Results

### API / XNA / FNA parity
`PASS`. `DirectionalLight0/1/2.setEnabledProperty()`/`setDirectionProperty()`/`setDiffuseColorProperty()`
match FNA's `DirectionalLight0`/`1`/`2` public getters returning a `DirectionalLight` with the same 3
settable fields (`Direction`, `DiffuseColor`, plus `SpecularColor`/`Enabled` not exercised by this effect's
lighting model here).

### Behavioral correctness
`PASS`, verified by direct re-derivation. The shared normal `kNormal=(0.866,0,-0.5)` and shared light
direction `kLightDir=(0,0,1)` give `dot(-kLightDir,N) = dot((0,0,-1),(0.866,0,-0.5)) = 0.5` for every light
that uses `kLightDir` (matches the file's own comment at line 65). Each light's diffuse color
(`kLight0Diffuse=(0.6,0,0)`, `kLight1Diffuse=(0,0.6,0)`, `kLight2Diffuse=(0,0,0.6)`) touches exactly one
channel:
  - Check 1 (all 3 lights on, shared direction): `lightSum = 0.5*(0.6,0,0)+0.5*(0,0.6,0)+0.5*(0,0,0.6) =
    (0.3,0.3,0.3)` → `litRGB = lightSum*diffuseColor(default 1,1,1) + emissive(default 0) = (0.3,0.3,0.3)`
    → `baseColor = litRGB*texColor(white,1,1,1) = (0.3,0.3,0.3)*255 ≈ (77,77,77)`, matching
    `kExpectedAllLights(77,77,77,255)` exactly (line 69).
  - Check 2 (`DirectionalLight2.Enabled=false`): `FillGpuDrawParams()`'s gating
    (`light2On ? DirectionalLight2.getDiffuseColorProperty() : Vector3::Zero`, line 445-446) correctly zeroes
    the forwarded diffuse when disabled, dropping the blue-channel term entirely: `lightSum=(0.3,0.3,0)` →
    `(77,77,0)`, matching `kExpectedLight2Disabled` (line 71).
  - Check 3 (`DirectionalLight1` rotated to `kLight1DirOffAxis=(1,0,0)`): `dot(-((1,0,0)), N) =
    dot((-1,0,0),(0.866,0,-0.5)) = -0.866`, clamped by `max(dotL1,0.0)` in the shader (line 3223) to `0` →
    `NdotL1=0` → green channel's `0.5*L1` term drops → `(77,0,77)`, matching `kExpectedLight1OffAxis`
    (line 73), and — critically — this correctly proves `DirectionalLight1` reads its *own* `Direction`
    field (not aliasing `DirectionalLight0`'s), since only rotating light1's direction (not light0's or
    light2's, both still at `kLightDir`) selectively zeroed only the green channel.

### Logic
`PASS`. `EnvironmentMapAmount=0`/`EnvironmentMapSpecular=0` (lines 135-136) correctly isolate the lit-diffuse
sum from the cube-map blend entirely, matching the file's own stated isolation rationale (line 17-19) and
independently confirmed via the same shader-structure argument used in the `_fog_test.cpp` report (the
Fresnel/lerp/specular terms all multiply by `uEnvMapAmount`/`uEnvMapSpecular`, both zero here).

### Memory/resource lifetime
`PASS`. `makeCube()` (lines 116-127) is documented (lines 113-115) as present purely so this file matches
every sibling `EnvironmentMapEffect` test's convention of always setting a real cube even when irrelevant to
the isolated variable under test — a deliberate defensive-consistency choice (the comment specifically notes
Bgfx's env-map path has no null-`EnvironmentMap` fallback, unlike EasyGL/Vulkan), not dead code.

### C++ correctness
`PASS`. No issues.

### Performance
`N/A` for a one-shot test, though see F1 below re: the retry loop's placement of `fx.Apply()`.

### Testing
This is itself a test file. See "Missing or Weak Tests."

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — `fx.Apply()` called inside the up-to-20-iteration retry loop, unlike sibling `_fog_test.cpp`'s single call outside its equivalent loop

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / minor-inefficiency / cross-file inconsistency
- Location/symbol: `renderWith()`, lines 161-174 — `fx.Apply()` is called at line 166, *inside* the
  `for (int i = 0; i < 20; ++i)` loop, before every `DrawUserPrimitives` call.
- Evidence: compare with `_fog_test.cpp`'s `renderQuad()` (same batch), which calls `fx.Apply()` **once**,
  before its own equivalent retry loop. Since `EnvironmentMapEffect::OnApply()` is a dirty-flag-gated
  incremental update (`EnvironmentMapEffect.cpp:286-396` — every block is guarded by `if ((dirtyFlags_ &
  DirtyX) != 0)`), calling it repeatedly with no state change in between is not incorrect (subsequent calls
  are cheap no-ops since the flags were already cleared by the first call), but it is inconsistent with the
  sibling file's convention and does perform up to 20 redundant dirty-flag checks + backend-uniform-rebind
  calls per `renderWith()` invocation instead of 1.
- Why it matters: purely a style/efficiency nit in test code, not a correctness issue — flagged because it
  is exactly the kind of small inconsistency across near-identical sibling files that's easy to miss without
  a side-by-side diff, and because a future reader might reasonably (but incorrectly) infer from this file
  that `Apply()` *needs* to be called per-frame/per-retry for this effect, when the dirty-flag design means
  it does not.
- FNA/XNA comparison: N/A (FNA's `Effect.Apply()` is also dirty-flag-gated identically; calling it
  redundantly is harmless there too).
- Suggested future action (not implemented by this audit): hoist `fx.Apply()` out of the retry loop here to
  match `_fog_test.cpp`'s convention, for consistency (no functional change expected).

## Cross-File Observations

- Directly complements this batch's `BasicEffect`-equivalent Task 885 test (referenced but out-of-batch,
  named in the file's own header comment) — confirms `EnvironmentMapEffect` was independently audited
  and fixed for the identical multi-light-forwarding bug class, rather than the fix being copy-pasted
  without verification (the file's own derivation is effect-specific: no per-light specular term, matching
  `EnvironmentMapEffect.fx`'s `DirLightNSpecularColor` hardcoded to zero, which `BasicEffect`'s equivalent
  test would not need to account for).
- Shares the "skip blank/black frames" retry-loop idiom with `_fog_test.cpp` (see that report's F1/Cross-File
  notes) — both are the two newest tests in this batch by task number (890, 900).

## Missing or Weak Tests

- Does not test all 3 lights disabled simultaneously (`litRGB` should reduce to exactly `EmissiveColor`,
  here `0`) — a trivial but currently-unverified degenerate case for this specific effect's multi-light path.
- Does not test a light with a non-trivial (non-axis-aligned, non-90°) off-axis rotation to confirm the
  `NdotL` falloff is smoothly proportional rather than just correctly gated to `0`/non-zero — check 3 only
  proves the `max(dot,0)` clamp fires correctly at exactly 90°, not that intermediate angles scale the
  diffuse contribution correctly (though this is more centrally `BasicEffect`'s/`Lighting.fxh`'s shared
  concern than something unique to `EnvironmentMapEffect`, and is presumably covered by a more fundamental
  lighting test elsewhere in the graphics test suite, outside this batch).

## Positive Findings

- The per-channel-color discrimination technique (each light claims one RGB channel) is an elegant,
  unambiguous way to prove 3 independent forwarding paths without needing 3 separate render passes — this
  audit's re-derivation confirms it genuinely works as designed, not just "looks clever."
- Check 3 specifically proves `DirectionalLight1` is not aliased to `DirectionalLight0`'s direction field —
  a real, non-trivial thing to verify given the bug class this task fixed involved exactly this kind of
  per-light-field forwarding.

## Final Assessment

A correct, well-designed regression test for a real, previously-confirmed multi-light-forwarding bug in
`EnvironmentMapEffect::FillGpuDrawParams()`. All 3 checks were independently re-derived and match the current
source exactly. Only a minor, non-functional style inconsistency (F1, LOW) versus a sibling file was found.
