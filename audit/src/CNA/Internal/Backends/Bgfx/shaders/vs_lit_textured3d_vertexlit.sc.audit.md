# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d_vertexlit.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d_vertexlit.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect vertex stage, per-vertex-lit path (XNA's real default)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Computes full Blinn-Phong lighting in the vertex stage (Gouraud-interpolated via v_litRGB/v_specularRGB) instead of the fragment stage.

## Executive Verdict

**Needs attention — correct normal transform and lighting formula; shares the cross-cutting fog-formula bug.**

## Checklist Results

### API / XNA parity
**Correct** inverse-transpose normal transform (unchanged from the per-pixel-lit sibling) and correct ambient+diffuse+specular formula, independently re-verified matching FNA's real per-vertex lighting path — this is XNA's actual default lighting mode (`PreferPerPixelLighting=false`).

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (MEDIUM):** mirrored fog formula.

## Cross-File Observations

Correctly implements XNA's real default lighting mode as its own dedicated shader variant (Task 1104), matching this project's stated XNA-parity goal.

## Missing or Weak Tests

No dedicated test found for this exact per-vertex-lit path on this backend specifically.

## Positive Findings

Correct, FNA-accurate per-vertex lighting implementation matching XNA's actual default.

## Final Assessment

One MEDIUM finding (shared fog-formula bug); the lighting formula itself is correct.
