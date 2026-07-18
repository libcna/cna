# Audit: examples/easygl_environmentmapeffect_amount_zero_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_amount_zero_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 393), `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style,
  single assertion
- Related production code: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureEnvMapped3DProgram`), `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx` (`PSEnvMap`)
- Main related tests: `easygl_environmentmapeffect_amount_one_test.cpp` (Task 394, explicitly
  handed off to by this file's own header comment), `easygl_emissive_ambient_composition_test.cpp`
  (references this file by name as a precedent for the DiffuseColor=(1,1,1) confound-avoidance
  technique).

## Purpose

Verifies that `EnvironmentMapEffect` ignores the cube map entirely when
`EnvironmentMapAmount=0` — a saturated pure-green cube map is deliberately used precisely because
any nonzero leak of its color would be immediately, unmistakably visible against the expected
texture-only result. `DiffuseColor` is deliberately left at its default `(1,1,1)` specifically to
avoid a second, unrelated confound (whether `DiffuseColor` double-multiplies into the
ambient-folded `EmissiveColor` term) — the file's own header comment names this exact concern and
explicitly hands off the resolution of the additive-vs-lerp env-map-blend question to "Task 394."

## Executive Verdict

**Healthy** — a single, well-isolated, correctly-derived assertion whose header comment is a model
of honest scope-limiting: it states plainly what this test does *not* prove and where that
question is picked up next, and both the produced numeric expectation and the deferred question
were independently confirmed accurate against the current shader source.

## Checklist Results

### API / XNA / FNA parity
Properties used match FNA's `EnvironmentMapEffect` public surface (`setTextureProperty`,
`setEnvironmentMapProperty`, `setEmissiveColorProperty`, `setEnvironmentMapAmountProperty`,
`setEnvironmentMapSpecularProperty`, matrix setters).

### Behavioral correctness
Re-derived against the current `EnsureEnvMapped3DProgram` fragment shader
(`EasyGLGraphicsBackend.cpp` lines 3219-3240): with `tex=(200,100,50)`, `greenCube=(0,255,0)`,
`EmissiveColor=(0.5,0.5,0.5)`, `DiffuseColor` default `(1,1,1,1)`, no lights configured (so
`lightSum=0`): `litRGB = lightSum*DiffuseColor.rgb + EmissiveColor = (0.5,0.5,0.5)`,
`baseColor = litRGB*texColor.rgb = (100,50,25)`. `EnvironmentMapAmount=0` (test line 138) means
`blendFactor = vFresnel = (fresnelEnabled ? pow(...)*0 : 0) = 0` regardless of the
`FresnelFactor` value (never explicitly set by this test, so left at the constructor default of
`1.0f`/enabled — irrelevant here since it's multiplied by `envMapAmount=0`). `mix(baseColor, greenCube,
0) = baseColor = (100,50,25)`, plus `uEnvMapSpecular*envSample.a*combinedAlpha = (0,0,0)*…=0`
(specular explicitly zeroed, line 139). Final `rgb=(100,50,25)` — exactly matches the test's
expected `Color(100, 50, 25, 255)` (line 149) and the inline derivation comment (lines 146-147).
**Independently confirmed correct**, both the formula and the arithmetic.

Note: the test's own inline comment (line 146: `litRGB = EmissiveColor(0.5,0.5,0.5) *
DiffuseColor(1,1,1) = (0.5,0.5,0.5)`) describes this as a **multiplicative** composition, which is
the *pre-fix* formula — but since `DiffuseColor=(1,1,1)` is the multiplicative identity, the
inline comment's arithmetic result coincidentally equals what the actual, current **additive**
formula (`lightSum*DiffuseColor+EmissiveColor = 0*1+0.5 = 0.5`) also produces. Both formulas agree
here for the same structural reason the file's own header (lines 18-20) already flags for the
*cube-map* blend question — but the header does not extend this same caveat to the inline
comment's *emissive* composition line. See F1.

### Logic
`colourMatch()` (tol=20) is appropriate for the deterministic, saturated setup.

### Memory/resource lifetime
`greenCube` (`std::unique_ptr<TextureCube>`) correctly scoped to `Draw()`, no issues.

### C++ correctness
No issues found.

### Performance
N/A — single `Draw()` call, correctly terminated (`Exit()` called at the end, no re-entrancy risk —
same `Game::Exit()`/`RunApplication` mechanism verified for the sibling amount_one test applies
here too).

### Thread safety
N/A.

### Architecture
Correctly limited to public XNA-facing API.

### Maintainability
The header comment is exemplary in explicitly scoping what this test does and does not prove,
including naming the specific follow-up task (394) that resolves the deferred question — a
practice this audit would like to see applied to the inline comment's emissive-formula description
too (F1).

### Portability
N/A.

### Robustness
N/A.

### Testing
This file is itself a test.

### Cross-file consistency
Consistent with the current shader source and with the fix narrative told across
`easygl_environmentmapeffect_amount_one_test.cpp` and `easygl_emissive_ambient_composition_test.cpp`
in this same batch — this file's header is referenced BY `easygl_emissive_ambient_composition_test.cpp`'s
own header comment as a precedent, and that cross-reference was independently checked (both files agree
on the same DiffuseColor=(1,1,1)-to-avoid-confound reasoning).

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Inline comment describes the pre-fix (multiplicative) emissive-composition formula; only correct here because DiffuseColor=(1,1,1) is a coincidental identity case

- Severity: LOW
- Confidence: HIGH
- Category: documentation accuracy
- Location/symbol: inline comment at lines 146-147:
  ```
  // litRGB = EmissiveColor(0.5,0.5,0.5) * DiffuseColor(1,1,1) = (0.5,0.5,0.5)
  // rgb = litRGB * texColor(200,100,50)/255 + greenCube*0 + 0 = (100,50,25)
  ```
  versus the actual current shader (`EasyGLGraphicsBackend.cpp` line 3229):
  `vec3 litRGB=lightSum*uDiffuseColor.rgb+uEmissiveColor;` (additive, not `EmissiveColor *
  DiffuseColor`).
- Evidence: with `DiffuseColor=(1,1,1)` (the multiplicative identity) and `lightSum=0`, both the
  additive formula (`0*1+0.5=0.5`) and the comment's multiplicative one (`0.5*1=0.5`) happen to
  agree numerically — the comment is arithmetically self-consistent and produces the right final
  number, but describes the wrong operation. The file's own header (lines 18-20) already
  acknowledges the analogous risk for the *env-map blend* formula and deliberately pins
  `DiffuseColor=(1,1,1)` to sidestep it — but does not extend the same acknowledgment to this
  inline comment's *emissive* line, which independently has the identical coincidental-agreement
  property.
- Why it matters: low impact (the test's numeric expectation is correct either way), but a future
  maintainer reading only this inline comment (not the more careful header) could come away with
  an incorrect mental model of the emissive-composition formula, same risk class as
  `easygl_env_map_test.cpp`'s F1 in this same batch.
- FNA/XNA comparison: FNA's real formula is additive (`Lighting.fxh` `ComputeLights()`), matching
  the current shader, not this inline comment.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`.
- Suggested future action (not implemented by this audit): correct the inline comment to read
  `litRGB = lightSum(0)*DiffuseColor(1,1,1) + EmissiveColor(0.5,0.5,0.5) = (0.5,0.5,0.5)` for
  accuracy, matching the header's own careful treatment of the analogous env-map-blend caveat.

## Cross-File Observations

- This file's header comment (lines 9-16) is directly referenced and corroborated by
  `easygl_emissive_ambient_composition_test.cpp`'s own header in this same batch — a genuine,
  verified cross-file consistency (not just an assumed one).
- Shares the F1-class issue (a comment describing the superseded multiplicative emissive formula)
  with `easygl_env_map_test.cpp`'s header comment (that file's F1) — suggesting this was a
  widespread documentation lag after the "sixth round" emissive-composition fix landed, not
  isolated to one file.

## Missing or Weak Tests

- Only a single assertion/scene; reasonable given the narrow, explicitly-scoped purpose of this
  file (isolate `EnvironmentMapAmount=0` behavior only), with the harder question explicitly
  deferred to Task 394's own file.

## Positive Findings

- Best-in-batch example of explicit test-scope honesty: the header comment states outright what
  this test does *not* prove (the additive-vs-lerp question at nonzero Amount) and names exactly
  which follow-up test resolves it — verified this hand-off is accurate by reading both files.
- Deliberately saturated, maximally-distinctive cube-map color (`pure green`) chosen specifically
  to make any leak unmistakable — good test design reasoning, explained inline.

## Final Assessment

A small, correctly-derived, and unusually well-scoped test. Its only flaw is a stale inline
comment describing the historical (pre-fix) multiplicative emissive-composition formula rather
than the current additive one — numerically harmless here due to `DiffuseColor=(1,1,1)` being a
coincidental identity value, but worth correcting for documentation accuracy.
