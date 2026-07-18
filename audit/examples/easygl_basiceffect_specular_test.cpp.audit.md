# Audit: examples/easygl_basiceffect_specular_test.cpp

## Metadata

- Source file: `examples/easygl_basiceffect_specular_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — `BasicEffect` specular-highlight pixel test
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_easygl_test(cna_test_easygl_basiceffect_specular …)` /
  `cna_register_backend_test(NAME EasyGL_BasicEffect_Specular …)`, `cmake/Tests/EasyGLTests.cmake:809-811`).
- XNA/FNA relevance: direct — `BasicEffect.SpecularColor`/`SpecularPower`/`DirectionalLight0.SpecularColor`,
  `EyePosition` derivation (`IEffectLights`).
- FNA reference: `HLSL/Lighting.fxh` (`ComputeLights`'s half-vector Blinn-Phong specular term),
  `HLSL/Common.fxh` (`AddSpecular`: `color.rgb += specular*color.a`, applied after the texture×diffuse
  multiply, not multiplied into it), `DirectionalLight.cs` (`Enabled` zeroes both Diffuse and Specular).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/BasicEffect.cpp`
  (`FillGpuDrawParams()` lines 85-131), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureLit3DVertexLitProgram()` lines 2893-3010, half-vector math lines 2955-2958).

## Purpose

4-check pixel test proving BasicEffect's real half-vector Blinn-Phong specular model (not a
reflect-vector approximation): (a) baseline eye position, (b) a different `EyePosition` producing a
different specular value (proves `EyePosition`-dependence, not a hardcoded bump), (c)
`SpecularColor=(0,0,0)` producing pure diffuse+ambient (proves the material gate), (d)
`DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (proves the light's own
`Enabled` gate covers specular too, not just diffuse). The file's own header comment (lines 29-53)
documents a **Task 1102 correction**: this test's expected constants were originally derived for a
per-pixel-lit evaluation, but since `BasicEffect`'s real default `PreferPerPixelLighting=false` now
routes through the per-vertex-lit shader (`EnsureLit3DVertexLitProgram()`), the sampled centre pixel
(sitting exactly on the two triangles' shared diagonal) instead reads a Gouraud-interpolated average
of the two vertices' own specular terms — a materially different quantity from the original per-pixel
value the constants were written against.

## Executive Verdict

**Needs attention** — checks (a), (c), (d) are solid and their expected constants were independently
re-derived by this audit to match the current per-vertex-lit formula almost exactly. Check (b),
however, is a **weak test by the file's own admission**: its expected constant
(`kExpectedOffAxisEye=68`) is the *stale, pre-Task-1102 per-pixel value*, and the comment explicitly
states the real current (correct) per-vertex-lit rendered value is `61`, which only passes because it
falls within the check's own `±10` tolerance of the wrong constant (see F1).

## Checklist Results

### API / XNA / FNA parity
`setSpecularColorProperty`/`setSpecularPowerProperty`/`DirectionalLight0.setSpecularColorProperty`
(lines 162-168) map correctly to FNA's `IEffectLights` surface. `EyePosition` is not a settable
BasicEffect property in FNA (it's derived internally from `View`), and this test correctly does not
attempt to set it directly — matching `BasicEffect::FillGpuDrawParams()`'s own derivation
(`Matrix::Invert(View).getTranslationProperty()`, `BasicEffect.cpp` lines 126-127) via `View` alone.

### Behavioral correctness
Re-derived the half-vector formula by hand for check (a) (`kEyeStraightOn=(0,0,3)`,
`kLightDirRaw=(0.5,0,-1)` normalized, `kNormal=(0,0,1)`, `SpecularPower=32`):
- Diffuse: `dot(-lightDir,N)=0.894427`; `litRGB=(ambient+lightDiffuse*0.894427)*materialDiffuse
  =(0.02+0.4472)*0.4=0.18685` per channel — matches the comment's own "diffuse(0.1869)" almost
  exactly.
- Per-vertex specular at `TL=(-1,1,0)`: eyeVector≈(0.3015,-0.3015,0.9045); halfVector≈
  normalize(eyeVector−lightDir)≈(-0.0796,-0.1648,0.9830); `dot(h,N)=0.983`; `spec=pow(0.983,32)
  ≈0.579` — matches the comment's "specular(TL)=0.5798."
- Per-vertex specular at `BR=(1,-1,0)`: eyeVector≈(-0.3015,0.3015,0.9045); halfVector≈
  (-0.7487,0.3015,1.7990) normalized→`dot(h,N)≈0.912`; `spec=pow(0.912,32)≈0.053` — matches
  "specular(BR)=0.0531."
- Gouraud average ≈0.316; total ≈0.1869+0.316=0.503→≈128×255... i.e. ≈128, rendered `127`
  (`kExpectedStraightOn`) — the 1-unit gap is ordinary GPU floating-point/interpolation precision,
  exactly as the comment states. **This audit independently confirms check (a)'s expected constant
  is correct for the current per-vertex-lit code path**, not merely internally self-consistent.
- Checks (c)/(d): `SpecularColor=(0,0,0)` zeroes `specularRGB` in the shader
  (`EnsureLit3DVertexLitProgram()` line 2958: `vSpecularRGB=(...)*uSpecularColor`) regardless of the
  per-light specular sum, leaving pure `diffuse+ambient` — matches `kExpectedNoSpecular(48,48,48)`.
  `DirectionalLight0.Enabled=false` is gated by `BasicEffect::FillGpuDrawParams()` lines 85-87
  (`ld`/`ls` both forced to `Vector3::Zero` when `!light0On`), zeroing *both* the diffuse and specular
  contributions of that light — matching FNA's `DirectionalLight.Enabled` setter semantics exactly,
  and giving the ambient-only `kExpectedLightDisabled(2,2,2)`.

### Logic
Check (b)'s own numeric derivation is **not** re-verified against the current formula the way check
(a) was — see F1.

### C++ correctness
`matches()`'s `closeTo(...,10)` tolerance (lines 132-139) is the mechanism that (accidentally) makes
check (b) pass despite its stale constant — see F1.

### Robustness
Checks (c)/(d) are the correct technique to isolate "does the material's own `SpecularColor` gate the
term" from "does a disabled light's `SpecularColor` get zeroed too" — these are two independently
failing hypotheses a naive implementation could get wrong in different ways, and both are covered.

### Testing
Three of four checks are strong, evidence-backed pixel assertions. The fourth (check b) is present
mainly to prove `EyePosition`-dependence via `!matches(b,a)` (line 215), which *does* still hold
(68 clearly differs from 127) — so the file's overall discriminating goal for check (b) (not a
hardcoded constant) is not defeated, but the specific `matches(b, kExpectedOffAxisEye)` assertion
(line 213-214) is verifying the wrong number and passing for the wrong reason.

## Detailed Findings

### F1 — Check (b)'s expected constant is a stale pre-Task-1102 value; the check passes only by tolerance overlap, not because the asserted value is correct

- Severity: MEDIUM
- Confidence: HIGH (self-documented in the file's own header comment, and independently confirmed by
  this audit's own re-derivation matching the comment's stated "real" value)
- Category: test-coverage / correctness-of-test
- Location/symbol: `kExpectedOffAxisEye(68, 68, 68, 255)` (line 106); check `(b)` (lines 212-213);
  header comment lines 47-53
- Evidence: the file's own comment states verbatim: *"Eye moved off-axis to (3,0,1) ... per-vertex
  specular at TL/BR Gouraud-averages to ~61 total (rendered: 61, exact) -- close enough to the OLD
  per-pixel value at this point (~68) to still pass this test's own ±10 tolerance, so its own
  expected constant is left as the historical per-pixel value below, not changed."* I.e. the actual,
  current, correct rendered value is `61`; the asserted expected constant is `68`; the check only
  passes because `|61-68|=7 ≤ 10`.
- Why it matters: this check does not actually verify the current per-vertex-lit formula's output at
  the off-axis eye position — it verifies that the output is *within 10* of a different, outdated
  formula's output. A regression that shifted the real per-vertex value from 61 to, say, 65 (still
  wrong relative to a hypothetical re-derivation, or right for a different reason) would pass
  unnoticed; conversely, a regression that moved it to 79 (just outside tolerance of the *stale*
  constant, but potentially still a legitimate per-vertex-lit value under different rounding) would
  fail for the wrong reason. The test's own self-disclosure that this is happening is a good sign
  of engineering honesty, but the numeric assertion itself should have been updated to `61` (or the
  nearest exact value) once Task 1102 changed what this code path actually computes.
- FNA/XNA comparison: N/A (test-authoring issue, not an XNA/FNA behavior question — the underlying
  BasicEffect behavior itself was independently confirmed correct for this scene in check (a)'s
  re-derivation).
- Related files: none outside this test file — the fix is local (update the constant to the
  currently-correct value, ideally re-derived to more decimal precision than the "~61" the comment
  gives).
- Suggested future action (not implemented by this audit): change `kExpectedOffAxisEye` to the
  current correct per-vertex-lit value (~61, to be re-derived precisely) so the check verifies actual
  current behavior rather than passing by accidental tolerance overlap with a superseded formula.

## Cross-File Observations

- This file, `easygl_basiceffect_preferperpixellighting_test.cpp`, and
  `easygl_basiceffect_one_light_test.cpp` all exercise `EnsureLit3DVertexLitProgram()`'s Blinn-Phong
  implementation; F1 only affects this file's check (b), not the shared production code (which this
  audit independently confirmed correct via check (a)'s exact re-derivation).
- The header comment's Task-1102 correction narrative is corroborated by `git log`
  (`cf3066a5 feat(Task 1102): EasyGL real per-vertex-lit shader + PreferPerPixelLighting dispatch`
  postdates this file's original authoring per its own Task-886 title) — the stale-constant situation
  in F1 is a natural, if avoidable, consequence of that later dispatch change, not a fabricated
  concern.

## Missing or Weak Tests

See F1 — check (b)'s own expected value should be refreshed to match the currently-computed
per-vertex-lit result rather than the historical per-pixel one.

## Positive Findings

- Checks (a)/(c)/(d) are precise, correctly-scoped, and their numeric derivations were independently
  confirmed by this audit against both FNA's `Lighting.fxh` formula and the live shader source.
- The file's own comment is unusually transparent about the check (b) discrepancy — most test files
  in this shard simply assert a number; this one explains exactly why its own assertion is weaker
  than it looks, which made this finding easy to verify rather than requiring independent discovery
  from scratch.
- `DirectionalLight0.Enabled=false` zeroing *both* diffuse and specular (check d) is a genuinely
  easy-to-miss XNA behavior (`DirectionalLight.Enabled`'s C# setter zeroes two separate GPU
  parameters) and this test explicitly isolates it.

## Final Assessment

A strong specular test overall, let down by one stale numeric constant that the file's own comments
already flag as passing "by luck" rather than by correctness — worth a follow-up to tighten check (b)
to the currently-correct value so a future regression in that specific code path would actually be
caught.
