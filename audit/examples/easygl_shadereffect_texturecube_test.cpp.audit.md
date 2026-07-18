# Audit: examples/easygl_shadereffect_texturecube_test.cpp

## Metadata

- Source file: `examples/easygl_shadereffect_texturecube_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 1081: `ShaderEffect::SetTexture(int, TextureCube&)` binding a
  cube texture to a custom shader's `samplerCube` uniform (for `NormalMapping.fx`'s reflection map)
- File type: raw `Game`-derived executable, two checks (A/B), with a decoy-texture false-positive guard whose
  *discovery* is documented in-file as a real bug the test's own first draft hit
- XNA/FNA relevance: `TextureCube`/`CubeMapFace` are genuine XNA types; `ShaderEffect::SetTexture(int,
  TextureCube&)` and `IEffectBackend::BindTextureCube()` are `NOXNA` plumbing.
- Related production files: `TextureCube.hpp`/`.cpp`, `ShaderEffect.cpp` (`SetTexture(int, TextureCube&)`),
  `EasyGLGraphicsBackend.cpp` (`EasyGLTextureCubeBackend::BindGL()`, `EasyGLEffectBackend::BindTextureCube()`).

## Purpose

Proves `IEffectBackend::BindTextureCube()` (Task 1081) genuinely binds a `TextureCube` to a custom shader's own
`samplerCube` uniform — a capability that had no code path at all before this task, since `ITextureCubeBackend`
is a wholly separate interface from `ITextureBackend` (confirmed, see below), so `IEffectBackend::BindTexture()`
(the `Texture2D` path) could never have accidentally serviced a cube texture.

## Executive Verdict

**Healthy**, and notable for its own honestly-documented test-authoring history: the file's header comment
describes a real false-positive its first draft fell into (a single `TextureCube` whose residual upload-time GL
binding made the test pass even with `BindTextureCube()` disabled) and the fix (a second, all-black decoy texture
uploaded last). This audit independently confirmed both the bug's plausibility and the fix's soundness against the
real GL binding model.

## Checklist Results

### API / XNA / FNA parity
`TextureCube(device, 1, false, SurfaceFormat::Color)` and `CubeMapFace::PositiveX/NegativeX/PositiveY/NegativeY/
PositiveZ/NegativeZ` are genuine XNA API surfaces, used with all 6 faces populated (only `PositiveX`/`PositiveY`
are actually exercised by the two checks; the other 4 are set for realism/non-triviality but not independently
verified by an assertion — see Missing or Weak Tests).

### Behavioral correctness
Confirmed via direct source read that `class ITextureCubeBackend` does **not** derive from `ITextureBackend` in
`IGraphicsBackend.hpp` — corroborating this file's own background comment verbatim (it explicitly states this was
"confirmed via direct source read, not assumption"). Traced `ShaderEffect::SetTexture(int, TextureCube&)`
(`ShaderEffect.cpp` lines 78-81) → `effectBackend_->BindTextureCube(unit, &texture.GetBackend())` →
`EasyGLEffectBackend::BindTextureCube()` (`EasyGLGraphicsBackend.cpp` lines 358-366): activates the unit, calls
`texture->BindGL()` (`EasyGLTextureCubeBackend::BindGL()`, line 205, binds `GL_TEXTURE_CUBE_MAP`), restores unit 0
— structurally identical to the sibling `BindTexture()`/`BindTexture3D()` implementations.

### Logic
Check A (`direction=(1,0,0)`) samples the `PositiveX` face (red); Check B (`direction=(0,1,0)`) samples
`PositiveY` (blue) — both are the GL cube-map convention's actual major-axis face selection for those exact
direction vectors (no off-axis blending risk since these are pure axis-aligned unit vectors, landing dead-centre
on each face). Distinct RGB between the two checks is genuine evidence the binding — not a stale/default value —
drives the sampled face.

### Memory/resource lifetime
`cubeTex_`/`decoyTex_` are `std::unique_ptr<TextureCube>`, both alive across both `DrawOnce()` calls. Temp
`.cnj`/`.vert.glsl`/`.frag.glsl` files written and never cleaned up — see F1.

### C++ correctness
No issues found; straightforward value/pointer usage throughout.

### Performance
N/A — single-shot test.

### Robustness
The vertex shader declares but never reads `aTexCoord` (location 1) — benign, same pattern as the sibling
`texture3d` test.

### Testing
**The header comment documents a real, previously-caught false positive, and this audit re-derived why it's a
genuine risk, not a hypothetical one**: with only one `TextureCube` created, `SetData()`'s own upload calls
(`CubeMapFace::PositiveX` through `NegativeZ`, `Initialize()` lines 159-164) necessarily leave `GL_TEXTURE_CUBE_MAP`
bound to `cubeTex_`'s own GL handle on texture unit 0 at the end of `Initialize()` — since nothing else touches
that unit/target before `Draw()` runs, a `BindTextureCube()` call that was silently disabled (a no-op regression)
would still read `cubeTex_`'s residual binding and pass both checks for the wrong reason. The fix — creating
`decoyTex_` (all 6 faces solid black) *after* `cubeTex_` and uploading it last — genuinely closes this gap: the
residual binding at the start of `Draw()` is now the decoy's, so an unmutated `SetTexture()`/`BindTextureCube()`
call is required each `DrawOnce()` to restore the correct texture. The header comment states this was
re-confirmed by disabling `BindTextureCube()` and observing both checks correctly fail to `(0,0,0,255)` (the
decoy's own colour) — a genuine mutation-based verification, not merely asserted.

### Cross-file consistency
This file's header comment is the origin point the sibling `texture3d` test explicitly credits for the same
technique — confirmed both files apply it identically and correctly.

## Detailed Findings

No CRITICAL/HIGH/MEDIUM findings.

### F1 — Temp directory (3 files) written per test run, never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: resource-hygiene
- Location/symbol: `Initialize()`, lines 123-135
- Evidence: no cleanup call anywhere in the file.
- Why it matters: same shared low-priority gap noted throughout this batch.
- Suggested future action (not implemented by this audit): clean up on success or failure.

## Cross-File Observations

- This file is the documented *origin* of the "decoy resource uploaded last" false-positive-avoidance pattern that
  the sibling `easygl_shadereffect_texture3d_test.cpp` explicitly credits and reuses — a good example of a lesson
  learned in one test propagating correctly to a structurally similar one instead of being independently
  re-discovered (or missed).
- Confirms `ITextureCubeBackend`'s independence from `ITextureBackend` at the interface level, a fact several
  other files/reports in this batch (and the `backend-common`/`IGraphicsBackend.hpp` audit) also rely on.

## Missing or Weak Tests

- Only 2 of the 6 uploaded cube faces (`PositiveX`, `PositiveY`) are exercised by an assertion; `NegativeX`
  (green), `NegativeY` (yellow), `PositiveZ` (magenta), `NegativeZ` (cyan) are populated but never read back —
  reasonable given the test's narrow scope (proving the binding mechanism works at all, not exhaustively testing
  every cube face's addressing), but a gap if a future regression corrupted face-selection logic in a way that
  only affects the 4 untested faces.

## Positive Findings

- Genuinely exemplary test-authoring transparency: the header comment documents a real defect the test's own first
  draft had (a false-positive-prone single-texture setup) and the concrete fix, rather than silently rewriting
  history — this audit was able to independently confirm both the bug's mechanism and the fix's soundness because
  the reasoning was laid out explicitly.
- The decoy-texture technique is a genuinely effective, minimal-overhead way to defeat "residual GL state" false
  positives without needing a full state-reset/`glBindTexture(0)` between draws.

## Final Assessment

A rigorous, self-aware test whose documented false-positive history was independently verified by this audit as
both real and correctly fixed; the only finding is the shared low-priority temp-file cleanup gap (F1).
