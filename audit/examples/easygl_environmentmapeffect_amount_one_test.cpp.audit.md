# Audit: examples/easygl_environmentmapeffect_amount_one_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_amount_one_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 394), `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style,
  2 sub-tests (`pass_`/`fail_` counters)
- Related production code: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureEnvMapped3DProgram`)
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`:
  `color.rgb = lerp(color.rgb, envmap.rgb, pin.Specular.rgb);`)
- Main related tests: `easygl_environmentmapeffect_amount_zero_test.cpp` (Task 393, the sibling
  test whose own header comment identifies the additive-vs-lerp discrepancy this file was written
  to resolve), `easygl_environmentmapeffect_combined_test.cpp` (Task 399, capstone combining this
  fix with several others).

## Purpose

Verifies that at `EnvironmentMapAmount=1`, FNA's real per-pixel formula (`lerp(litColor, envColor,
Amount)`) **fully replaces** the lit/textured color with the cube-map color, rather than merely
adding the cube-map contribution on top of it (the bug this file's own header comment says Task
393 identified). Two sub-tests: (a) a fully-saturated white cube map (both the correct lerp and the
old buggy additive formula predict the same clamped-to-white result — included only as a sanity
check, explicitly *not* claimed to be discriminating); (b) a non-saturated gray (128,128,128) cube
map against a genuinely nonzero lit/textured base color — the actually discriminating case, since
lerp-at-Amount=1 predicts (128,128,128) while the old additive formula would predict
(100,50,25)+(128,128,128)=(228,178,153), unambiguously different.

## Executive Verdict

**Healthy** — a well-reasoned, correctly-derived discriminating test whose expected values were
independently re-verified against both FNA's actual `PSEnvMap` formula and CNA's current
`EnsureEnvMapped3DProgram` shader source, and both agree with what the test expects.

## Checklist Results

### API / XNA / FNA parity
Properties used (`setTextureProperty`, `setEnvironmentMapProperty`, `setEmissiveColorProperty`,
`setEnvironmentMapAmountProperty`, `setEnvironmentMapSpecularProperty`, matrix setters) all match
FNA's public `EnvironmentMapEffect` surface.

### Behavioral correctness
Independently re-derived the shader math against `EnsureEnvMapped3DProgram`'s current fragment
shader (`EasyGLGraphicsBackend.cpp` lines 3219-3240):
```cpp
vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;
...
vec3 baseColor=litRGB*texColor.rgb;
float combinedAlpha=uDiffuseColor.a*texColor.a;
float blendFactor=vFresnel;
vec3 rgb=mix(baseColor,envSample.rgb*combinedAlpha,blendFactor)+uEnvMapSpecular*envSample.a*combinedAlpha;
```
This is a genuine `mix()` (GLSL lerp) between `baseColor` and the cube-map sample, gated by
`blendFactor` — confirms the fix Task 394 was written to verify is actually present in the current
source, not merely claimed by the test's header comment.

- **Sub-test (a)**: `tex=(200,100,50)`, `whiteCube=(255,255,255)`, `EmissiveColor=(0.5,0.5,0.5)`,
  `DiffuseColor` default `(1,1,1,1)` (never set by this test, matching
  `EnvironmentMapEffect`'s own constructor default — no lights set either, so `lightSum=0`
  ⇒ `litRGB=EmissiveColor=(0.5,0.5,0.5)`), `baseColor=litRGB*tex=(100,50,25)`. `FresnelFactor` is
  never set, so it's left at its constructor default of `1.0f` (`fresnelEnabled_=true`, per
  `EnvironmentMapEffect.cpp` line 41). With `World=View=Projection=Identity`, the same
  always-perpendicular-eyeVector geometry analyzed in the sibling `easygl_env_map_test.cpp` audit
  applies: `viewAngle=0` at every fragment, so `blendFactor = pow(1,1)*envMapAmount(1) = 1`.
  `mix(baseColor, whiteCube*combinedAlpha(1), 1) = whiteCube = (255,255,255)` — matches the test's
  expected value and its own explicit caveat that this case can't discriminate the two formulas
  (both saturate to white regardless).
- **Sub-test (b)**: identical setup but `grayCube=(128,128,128)`. `mix(baseColor=(100,50,25),
  grayCube=(128,128,128), blendFactor=1) = (128,128,128)` exactly — matches the test's expected
  value and correctly discriminates from the old additive formula's
  `(100,50,25)+(128,128,128)=(228,178,153)` (clamped or not, clearly different in every channel).
  **Independently confirmed correct.**

### Logic
`colourMatch()` (tol=20, tighter than the sibling `easygl_env_map_test.cpp`'s tol=40) is
appropriate given the smaller 64×64 backbuffer and the fully-deterministic (no lighting variance)
setup.

### Memory/resource lifetime
`whiteCube`/`grayCube` (`std::unique_ptr<TextureCube>`, lines 142-143) correctly scoped to the
whole `Draw()` call; `renderWith()` (lines 107-123) takes them by reference, no ownership transfer
issues.

### C++ correctness
No issues found.

### Performance
N/A — single `Draw()` call invoked once (see F1 for a minor stylistic note on how that
single-invocation guarantee is established here versus sibling files).

### Thread safety
N/A.

### Architecture
Correctly limited to public XNA-facing API.

### Maintainability
Clear, well-reasoned header comment that explicitly explains why sub-test (a) is included despite
not being discriminating (a valuable practice — most files in this batch that include a
"sanity check" sub-test are similarly explicit about it, but it's easy to omit).

### Portability
N/A.

### Robustness
N/A.

### Testing
This file is itself a test. See F1 and Missing or Weak Tests.

### Cross-file consistency
Consistent with the actual current `EnsureEnvMapped3DProgram` shader and with the fix history
documented in the sibling `easygl_environmentmapeffect_amount_zero_test.cpp` (Task 393) file's own
header comment, which explicitly hands off this exact question to "Task 394."

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — `Draw()` omits the explicit `done_` re-entry guard used by sibling files (stylistic only, verified harmless)

- Severity: INFO
- Confidence: HIGH
- Category: maintainability
- Location/symbol: `Draw(const GameTime&)` override (lines 126-171) has no `done_`/`if (already ran)
  return;` guard, unlike `easygl_env_map_test.cpp`, `easygl_environmentmapeffect_amount_zero_test.cpp`,
  `easygl_environmentmapeffect_combined_test.cpp`, and `easygl_emissive_ambient_composition_test.cpp`
  in this same batch, all of which have one. This file does call `Exit()` at the end of `Draw()`
  (line 170), same as its siblings.
- Evidence: traced `Game::Exit()` (`Game.cpp` lines 306-310: `RunApplication = false; suppressDraw_
  = true;`) and the main loop (`Game.cpp` line 448: `Draw(gameTime_)` called at most once per
  `Tick()`; line 832: `while (RunApplication)`) — `Exit()`'s effect is checked at the *next* loop
  iteration, not re-entrantly mid-`Draw()`, so `Draw()` provably runs exactly once here: the same
  guarantee the sibling files' explicit `done_` flags provide, just established structurally
  through `Game`'s own loop/flag semantics rather than an extra field in this file.
- Why it matters: purely stylistic — this file is consistent with the *effect* of the sibling
  files' `done_` guard, just via a different (and, on inspection, equally correct) mechanism.
- FNA/XNA comparison: N/A.
- Suggested future action: none required; noted only for consistency with sibling-file style if
  this file is touched again for other reasons.

## Cross-File Observations

- This file, together with `easygl_environmentmapeffect_amount_zero_test.cpp` and
  `easygl_environmentmapeffect_combined_test.cpp`, forms a coherent Task 393→394→399 narrative that
  is fully corroborated by the actual current shader source — a rare case in this batch where the
  historical bug-fix story told across multiple files' comments was independently confirmed true at
  every step rather than merely assumed.
- Contrast with `easygl_env_map_test.cpp` (Task 134/192, earlier in the numbering), whose own
  header comment was found to be stale relative to the very fix this file (Task 394) documents —
  see that file's own audit report, F1.

## Missing or Weak Tests

- Only `Amount=1` is tested here with a non-saturated (gray) cube map; no test in this file (or,
  per the sibling files' own scope notes, elsewhere) exercises a genuinely intermediate
  `EnvironmentMapAmount` (e.g. 0.5) against a non-saturated cube and a non-saturated lit color to
  confirm the lerp interpolates correctly at fractional blend factors, rather than only checking
  the two endpoints (0 and 1) each in their own file.

## Positive Findings

- Explicitly acknowledges and explains a non-discriminating sub-test (a) rather than silently
  including it as if it were meaningful evidence — a good testing-hygiene practice.
- Correctly identifies and constructs the actually-discriminating case (b) with a
  non-saturated gray cube map and a nonzero lit/textured base color, and its expected values were
  independently re-derived and confirmed exactly correct against the real shader.

## Final Assessment

A correctly-reasoned and correctly-verified regression test for a real formula-level fix
(additive → lerp env-map blending at Amount=1). No functional defects found; F1 is a purely
stylistic observation confirmed harmless by tracing `Game`'s own loop/exit semantics.
