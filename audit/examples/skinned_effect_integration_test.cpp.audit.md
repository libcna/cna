# Audit: examples/skinned_effect_integration_test.cpp

## Metadata

- Source file: `examples/skinned_effect_integration_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard by manifest assignment, but registration is EasyGL-only
  in practice: `cmake/Tests/EasyGLTests.cmake:230-233` (`EasyGL_SkinnedEffect_BoneDeformation`). No
  Vulkan/Bgfx registration found anywhere in `cmake/Tests/*.cmake`.
- XNA/FNA relevance: direct — `SkinnedEffect.SetBoneTransforms`/`WeightsPerVertex`, GPU vertex
  skinning (bone-weighted position transform).
- FNA reference: `Graphics/Effect/StockEffects/SkinnedEffect.cs`,
  `HLSL/SkinnedEffect.fx` (bone-weighted `Skin1x`/`Skin2x`/`Skin4x` position transforms).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/SkinnedEffect.cpp`
  (`SetBoneTransforms`/`setWeightsPerVertexProperty`/`FillGpuDrawParams`), and EasyGL's
  `EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram` in
  `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`.

## Purpose

A GPU pixel-readback test proving that `SkinnedEffect`'s bone-weighted vertex transform is real:
a quad spanning NDC x∈[-1,0] has every vertex bound 100% to bone 0 (`w0=1`, `i0=0`), bone 0 is set
to `Matrix::CreateTranslation(0.5, 0, 0)`, and the test checks that the quad visibly moved to
NDC x∈[-0.5,0.5] via three sampled pixels (left-quarter, centre, right-quarter) discriminating red
(quad) from green (background).

## Executive Verdict

**Mostly healthy** — the core position-transform proof is sound and its three-pixel discrimination
correctly isolates "did the bone transform actually move the geometry." However, this file sets
`World=Identity` (line 83) and calls `fx.EnableDefaultLighting()` (line 95, "needed: without lights
the Phong shader outputs black"), which means it genuinely exercises EasyGL's lit skinned shader
path — yet it is structurally incapable of revealing the already-confirmed, project-wide
skinned-effect normal-transform defect referenced in `AUDIT_CROSS_CUTTING_FINDINGS.md`, for the
exact reason that document itself identifies: the bug is invisible whenever `World=Identity`.

## Checklist Results

### API / XNA / FNA parity
`fx.SetBoneTransforms(bones)` (line 93), `fx.setWeightsPerVertexProperty(1)` (line 94),
`fx.EnableDefaultLighting()` (line 95) all map directly to FNA's `SkinnedEffect` surface. The
52-byte `SkinnedGpuVertex` layout (lines 39-47, `pos(12)+normal(12)+uv(8)+weights(16)+indices(4)`)
matches the layout documented in this batch's cross-cutting context as EasyGL's real skinned
vertex stride.

### Behavioral correctness
Independently confirmed `SkinnedEffect::SetBoneTransforms` (`SkinnedEffect.cpp:294-302`) forwards
directly to `bonesParam_->SetValue(boneTransforms)` with no silent reordering/renormalization, and
`setWeightsPerVertexProperty(1)` (`SkinnedEffect.cpp:286-292`) is a valid value (1/2/4 only)
correctly accepted. `fx.EnableDefaultLighting()` is required precisely because, per
`SkinnedEffect::FillGpuDrawParams` (`SkinnedEffect.cpp:320-338`), `p.lightingEnabled = true`
unconditionally for `SkinnedEffect` (unlike `BasicEffect`, which has a separate toggle) — so a
scene with no lights enabled would indeed render fully black, matching the comment's own stated
rationale.

### Logic — normal-transform bug exposure analysis (primary finding of this report)
This audit traced whether this specific file could reveal the cross-cutting-confirmed EasyGL
skinned normal-transform defect (per `AUDIT_CROSS_CUTTING_FINDINGS.md`: "`EnsureSkinnedProgram`/
`EnsureSkinnedVertexLitProgram` never register or use a `uNormalMatrix` uniform at all — normal
transformed only by the bone-skin matrix"). With `World` set to `Matrix::getIdentityProperty()`
(line 84) here, `WorldInverseTranspose` (were it computed at all) would *also* be the identity
matrix — so a shader that skips the `World`-space normal composition and only applies the
per-vertex bone-skin matrix produces **exactly the same lit result** as a hypothetical
correctly-composing shader would, for this specific scene. This file is therefore a confirmed
instance of the exact masking pattern the cross-cutting document already generalizes from other
EasyGL skinned-effect tests ("same root cause, same 'invisible because every test uses
World=Identity' masking") — not a new occurrence needing separate root-causing, but a concrete,
independently-verified additional data point for that finding, since this file was not among the
three EasyGL skinned test files the cross-cutting document explicitly names.

### C++ correctness
`static_assert(sizeof(SkinnedGpuVertex) == 52, ...)` (line 47) is a good compile-time guard against
accidental struct-layout drift (padding, member reordering) silently breaking the raw vertex upload.

### Testing
The `leftOk`/`centOk`/`rightOk` discriminators (lines 131-133) use `R > G` (or `G > R`) rather than
exact color matching, and `centOk` additionally requires `R > 50` — reasonable given lighting
attenuates the red channel unpredictably depending on `EnableDefaultLighting()`'s exact light
setup, but this also means the test cannot distinguish "correct Blinn-Phong lit red" from "any
red-dominant value including a badly-lit or wrongly-normal-transformed one" — consistent with, and
another instance of, the same masking situation above: even if the normal-transform bug were fixed
tomorrow, this test's pass/fail would not change, because it was never sensitive to the exact lit
intensity in the first place (only to hue dominance).

## Detailed Findings

### F1 — Test exercises the EasyGL lit-skinned shader path but is structurally blind to the confirmed WorldInverseTranspose-skip defect
- Severity: MEDIUM
- Confidence: HIGH (independently traced: `World=Identity` at line 84 makes any correct vs.
  incorrect normal-composition shader produce identical output for this scene, and the pixel
  discriminator only checks hue dominance, not lit intensity)
- Category: test-coverage / masked-defect
- Location: lines 83-84 (`fx.setWorldProperty(Matrix::getIdentityProperty())`), line 95
  (`fx.EnableDefaultLighting()`), lines 131-133 (loose `R>G`/`R>50` discriminators)
- Evidence: cross-referenced against `AUDIT_CROSS_CUTTING_FINDINGS.md`'s "CONFIRMED SYSTEMIC,
  MULTI-BACKEND: skinned-effect shaders skip the WorldInverseTranspose normal transform" entry,
  which independently confirms (via direct `EasyGLGraphicsBackend.cpp` reading) that
  `EnsureSkinnedProgram`/`EnsureSkinnedVertexLitProgram` never register a `uNormalMatrix` uniform.
  This file's own scene setup (`World=Identity`) is exactly the condition under which that defect
  cannot manifest.
- Why it matters: this file is registered as an EasyGL integration test and does exercise the real
  lit-skinned code path (via `EnableDefaultLighting()`), making it a *plausible* candidate for
  catching a normal-transform regression — but it never will, regardless of whether the underlying
  bug is fixed or worsens, because the test's own scene construction (identity `World`) and its
  loose hue-only pixel check are both insensitive to the exact lighting math. A future reader
  might mistake "this integration test passes" as partial evidence the lighting math is fine; it
  is not evidence either way for this specific defect.
- FNA/XNA comparison: N/A here (this is a test-design gap, not a new production-behavior finding —
  the underlying shader defect itself is already tracked in `AUDIT_CROSS_CUTTING_FINDINGS.md` and
  in `EasyGLGraphicsBackend.cpp`'s own audit report, not newly discovered by this file).
- Suggested follow-up (not implemented by this audit): a companion test using a non-identity
  `World` (e.g. a 90° rotation) with a specifically-oriented normal and a strong single directional
  light would immediately discriminate correct vs. incorrect normal transform — this file's own
  scene cannot be trivially repurposed for that without changing its primary (position-transform)
  test intent.

## Cross-File Observations

- Shares the "Task 896: `RasterizerState::CullNone`" convention (line 117) with every other
  full-screen/user-primitive test in this batch — consistent, already-established codebase pattern.
- `device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2)` (line 118) correctly passes the
  **primitive** count (2 triangles), not vertex count (6) — consistent with the project's own
  documented `DrawPrimitives`-count convention (matches memory note on this exact class of bug).
- This file, together with `skinned_effect_test.cpp` (same batch, pure property test, no rendering)
  and the EasyGL-shard `easygl_skinnedeffect_*` files referenced by
  `AUDIT_CROSS_CUTTING_FINDINGS.md`, forms the full population of `SkinnedEffect` coverage
  encountered in this audit so far — none of them (per the cross-cutting document, and now
  confirmed for this file too) actually discriminates the normal-transform defect.

## Missing or Weak Tests

See F1. A non-identity-`World`, lighting-intensity-sensitive skinned test does not appear to exist
in this batch or in the cross-cutting document's already-examined EasyGL skinned test files.

## Positive Findings

- The core claim this file makes (bone-weighted position transform is real, not a no-op) is
  soundly proven: three independent sample points, correctly reasoned NDC-space geometry, and a
  `static_assert` guarding the raw vertex layout against silent drift.
- Correctly identifies and works around `SkinnedEffect`'s unconditional `lightingEnabled=true`
  requirement (verified against `FillGpuDrawParams`) rather than being surprised by an all-black
  render.

## Final Assessment

A sound, narrowly-scoped position-transform test that does its one job correctly. Its use of
`World=Identity` combined with a hue-only pixel discriminator means it cannot serve as
supplementary evidence for or against the separately-confirmed EasyGL skinned-effect
normal-transform defect, despite exercising the exact lit shader path where that defect lives —
worth noting precisely because this file looked, at first glance, like it might be "uniquely
positioned" to reveal cross-backend divergence on this defect, and on inspection is not.
