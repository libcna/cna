# Audit: examples/easygl_environmentmapeffect_combined_test.cpp

## Metadata

- Source file: `examples/easygl_environmentmapeffect_combined_test.cpp`
- Audit status: AUDITED
- Subsystem: EasyGL backend integration test (Task 399, cross-backend capstone),
  `examples-tests-easygl` shard
- File type: C++ integration-test executable (`Game` subclass, `main()`), pixel-readback style,
  single composite scene, single assertion
- Related production code: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`EnsureEnvMapped3DProgram`), `src/Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.cpp`
  (`FillGpuDrawParams`, `OnApply` — `EyePosition` derivation from `Matrix::Invert(view_)`)
- FNA reference: `src/Graphics/Effect/StockEffects/HLSL/EnvironmentMapEffect.fx`
- Main related tests: explicitly built on top of Tasks 393-398 (`easygl_environmentmapeffect_amount_zero_test.cpp`,
  `easygl_environmentmapeffect_amount_one_test.cpp` in this same batch, plus specular/Fresnel/eye-position/
  normal-matrix tasks not in this shard) — this file's stated purpose is to prove those fixes compose
  correctly together in one non-trivial scene, mirroring "Task 370/389's precedent for
  BasicEffect/DualTextureEffect."

## Purpose

A capstone scene combining: a real (non-0/1) Fresnel-suppressed lerp blend factor, a translucent
env-map specular contribution (`EnvironmentMapSpecular` with an alpha=128 cube), `FresnelFactor`
left at its true default (1, "enabled"), a real non-identity camera
(`CreateLookAt`/`CreatePerspectiveFieldOfView`) exercising `EyePosition` derived from the view
matrix's inverse, and a non-uniform-scale `World` (`CreateScale(2,1,1)`) exercising the
world-inverse-transpose normal transform — all in one draw, with a single expected pixel value
derived and explained in detail in the header comment.

## Executive Verdict

**Healthy** — the single composite assertion's expected value, `(151, 101, 76)`, was independently
re-derived from first principles against the actual current shader and `EnvironmentMapEffect`
production code and matches exactly, including the claimed near-zero Fresnel-suppressed blend
factor at this specific camera/geometry configuration.

## Checklist Results

### API / XNA / FNA parity
`Matrix::CreateScale`, `Matrix::CreateLookAt`, `Matrix::CreatePerspectiveFieldOfView`,
`MathHelper::PiOver4` all match FNA's `Matrix`/`MathHelper` static API; `EnvironmentMapEffect`
property usage matches FNA's public surface.

### Behavioral correctness
Independently re-derived the full pipeline:
- **World/normal**: `World = CreateScale(2,1,1)` is diagonal, so `Invert(World)` is
  `diag(0.5,1,1)` and its transpose is itself: `WorldInverseTranspose = diag(0.5,1,1)`. The quad's
  normal `(0,0,1)` transforms to `(0,0,1)*1 = (0,0,1)` (Z untouched by this scale) — confirms the
  header comment's claim (lines 20-24) that the X/Y-only scale leaves the Z-aligned normal
  unaffected, so this scene doesn't (and per its own stated intent, shouldn't) exercise the
  normal-matrix correctness question Task 398 owns separately. **Correct.**
- **Eye position / Fresnel blend factor**: `View = CreateLookAt((0,0,3), Vector3::Zero, (0,1,0))`;
  `EnvironmentMapEffect::OnApply()`/`FillGpuDrawParams()` both derive `EyePosition` from
  `Matrix::Invert(view_).getTranslationProperty()`, which for a standard look-at matrix recovers
  the camera's world position `(0,0,3)`. At the center pixel, `worldPos ≈ (0,0,0)` (quad center,
  after the X/Y-only scale still centered at the origin), so `eyeVector = normalize((0,0,3)-(0,0,0))
  = (0,0,1)`, and `worldNormal=(0,0,1)` — `viewAngle = dot(eyeVector,worldNormal) ≈ 1` (not exactly
  1 only due to the sub-pixel offset of the actual sampled pixel center, exactly as the header
  comment notes, lines 30-31). With `FresnelFactor=1.0` (explicitly set to the real FNA default,
  line 154): `blendFactor = pow(max(1-|viewAngle|,0),1)*EnvironmentMapAmount(1) ≈ (1-0.99996)*1 ≈
  0.00004` — matches the header's own derivation almost exactly.
- **Base color**: `DiffuseColor` is never set by this test, so it stays at
  `EnvironmentMapEffect`'s member-initializer default `Vector3{1,1,1}`/`alpha_=1.0f`
  (confirmed at `EnvironmentMapEffect.hpp` lines 387/390) — no directional lights are
  enabled/configured either (constructor only turns on `DirectionalLight0.Enabled`, whose
  diffuse color itself defaults to black per `DirectionalLight`'s default constructor), so
  `lightSum=0` and `litRGB = 0*DiffuseColor(1,1,1) + EmissiveColor(0.5,0.5,0.5) = (0.5,0.5,0.5)`.
  `baseColor = litRGB*texColor(200,100,50) = (100,50,25)` — matches the header's derivation
  exactly.
- **Final composite**: `combinedAlpha = texColor.a(1)*DiffuseColor.a(1) = 1`.
  `mix(baseColor=(100,50,25), envColor*combinedAlpha, blendFactor≈0.00004) ≈ baseColor` (the
  ~0.00004 perturbation toward the translucent-black cube's `(0,0,0)*0.502` contribution is
  negligible, `<0.01` per channel as the header itself notes). Then
  `+= EnvironmentMapSpecular(0.4,0.4,0.4) * envSample.a(128/255≈0.502) * combinedAlpha(1) ≈
  (0.2008,0.2008,0.2008) → ~51.2` per channel. Total `≈ (151, 101, 76)` — **exactly matches** the
  test's expected `Color(151, 101, 76, 255)` (line 162), independently re-derived, not merely
  trusted.

### Logic
`colourMatch()` (tol=20) is reasonable given the derivation above nets a sub-1-level Fresnel
perturbation — plenty of margin against the ~0.00004 blend factor's negligible effect, and the
tolerance is consistent with the sibling amount_zero/amount_one files in this batch.

### Memory/resource lifetime
`cube` (`std::unique_ptr<TextureCube>`) correctly scoped to `Draw()`, no issues.

### C++ correctness
No issues found.

### Performance
N/A — single `Draw()` call; correctly terminates via `Exit()` (line 167), same verified-safe
`Game::Exit()`/`RunApplication` mechanism as the sibling amount_one/amount_zero files (no `done_`
guard present here either, same as the amount_one file — confirmed harmless by the same reasoning
recorded in that file's own audit report, not repeated as a separate finding here).

### Thread safety
N/A.

### Architecture
Correctly limited to public XNA-facing API; uses a real perspective camera rather than Identity,
appropriately for a capstone test meant to validate composition under realistic transform state.

### Maintainability
The header comment (lines 1-36) is an unusually thorough worked derivation, explicitly citing
which prior task (393-398) contributes which term to the final expected value — genuinely useful
for a future maintainer trying to understand why `(151,101,76)` is the "right" answer.

### Portability
N/A.

### Robustness
N/A.

### Testing
This file is itself a test.

### Cross-file consistency
Fully consistent with `EnvironmentMapEffect.cpp`'s `OnApply()`/`FillGpuDrawParams()` eye-position
derivation and the current `EnsureEnvMapped3DProgram` shader; consistent with the fix history
narrated across the other `easygl_environmentmapeffect_*` files in this same batch.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings — every numeric claim in this file's header comment was
independently re-derived and confirmed accurate against the actual current production code.

### F1 — Single composite assertion means a future regression in any one contributing term could be masked by cancellation, or could fail without immediately indicating which term is at fault

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage / diagnosability
- Location/symbol: whole file — only one `check()` call (line 162) covers the combination of five
  independently-varying terms (lerp blend, specular, Fresnel, eye position, non-identity world).
- Evidence: by design (per the header comment, this is deliberately a capstone/integration test
  layered on top of five prior per-term unit tests), a single aggregate pixel check cannot, by
  itself, indicate *which* of the five composed terms regressed if the assertion fails — a
  maintainer would need to re-run the individual Task 393-398 tests to localize a future failure.
- Why it matters: acceptable for a capstone test whose explicit purpose is integration, not
  isolation (the per-term isolation is intentionally left to the other files) — flagged as an
  observation, not a defect, since the design intent is documented and reasonable.
- FNA/XNA comparison: N/A.
- Suggested future action: none required given the explicit, documented intent; if this file is
  ever extended, consider printing intermediate term values (blend factor, base color, specular
  contribution) on failure to speed up localization, mirroring what
  `easygl_emissive_ambient_composition_test.cpp`'s `check()` already does (prints both the actual
  and the "old bug" hypothetical value).

## Cross-File Observations

- This file cleanly builds on and cross-references (by task number) the exact set of prior fixes
  verified individually in `easygl_environmentmapeffect_amount_zero_test.cpp` and
  `easygl_environmentmapeffect_amount_one_test.cpp` in this same batch — every cross-reference
  checked out against those files' own actual content.
- Like its `amount_one` sibling, this file omits the `done_` re-entry guard other files in the
  batch use; confirmed harmless via the same `Game::Exit()`/`RunApplication` trace recorded in
  that file's audit report (not re-litigated here as a separate finding).

## Missing or Weak Tests

- See F1 — no per-term diagnostic breakdown on failure; acceptable given the test's explicit,
  documented role as an integration capstone rather than a unit test.

## Positive Findings

- One of the most rigorously-derived expected values found in this entire batch — the header
  comment's math was independently re-derived from the view/projection/world matrices down to the
  final RGB triple and found to match exactly, including a subtle near-zero (not exactly zero)
  Fresnel blend factor whose magnitude the comment itself correctly bounds.
- Deliberately chooses a non-uniform-scale `World` and a real perspective camera specifically to
  validate the pipeline under realistic (non-identity) transform state, rather than the
  Identity-everywhere shortcut several sibling files in this batch use — genuinely exercises more
  of the real transform/lighting pipeline than most of its siblings.

## Final Assessment

A high-quality, well-reasoned capstone integration test whose single composite expected value was
independently verified correct against the full transform/lighting/env-map pipeline. Its only
limitation (a single aggregate assertion covering five composed terms) is a deliberate, documented
design choice appropriate to its stated role, not an oversight.
