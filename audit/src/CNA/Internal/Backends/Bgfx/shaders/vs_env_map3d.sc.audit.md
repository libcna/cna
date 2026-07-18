# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_env_map3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: EnvironmentMapEffect vertex stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Transforms position, computes world-space normal via a CPU-precomputed inverse-transpose normal matrix, world-space eye direction.

## Executive Verdict

**Needs attention — correct normal transform (control-group member); shares the cross-cutting fog-formula bug.**

## Checklist Results

### API / XNA parity
**Correct** normal transform via `u_normalMatrix` (CPU-precomputed inverse-transpose of World's upper-left 3x3, explicitly cross-referenced against `EnvironmentMapEffect`'s own Task 398 fix) — part of this project's control group (with `vs_lit_textured3d.sc`/`vs_pbr3d.sc`) proving the skinned-shader normal-transform bug is a skinning-specific regression on this backend too, not general unfamiliarity with the correct math.

**Positive finding**: unlike Vulkan's own already-confirmed missing Y-flip bug for `EnvironmentMapEffect` (rendering scenes vertically mirrored), this file has no Y-flip issue of its own — Bgfx does not appear to share the Vulkan-specific Y-flip defect (not exhaustively re-verified against every camera/projection configuration in this pass, but no equivalent missing-flip pattern found).

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

Correct normal transform, consistent with `vs_lit_textured3d.sc`'s own Task 892 fix.

## Missing or Weak Tests

No dedicated fog test found (though `examples-tests-bgfx`'s own EnvironmentMapEffect tests, already audited, confirmed the emissive-remultiply bug in this shader's fragment sibling).

## Positive Findings

Correct, CPU-precomputed inverse-transpose normal-matrix approach (bgfx shading language has no built-in `inverse()`, unlike GLSL/HLSL — this project correctly works around that limitation on the CPU side rather than approximating on the GPU).

## Final Assessment

One MEDIUM finding (shared fog-formula bug); the normal transform this file owns is correct.
