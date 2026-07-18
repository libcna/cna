# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_lit_textured3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect vertex stage, per-pixel-lit path
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Transforms position, computes world-space normal via a CPU-precomputed inverse-transpose normal matrix.

## Executive Verdict

**Needs attention — correct normal transform (control-group member); shares the cross-cutting fog-formula bug.**

## Checklist Results

### API / XNA parity
**Correct** `u_normalMatrix`-based transform (Task 892 fix, explicitly and accurately documented as correcting a prior WVP-based approach that would bake View/Projection into the normal, wrong under any non-identity camera) — this file is part of the control group proving the skinned-shader normal-transform bug (in `vs_skinned3d.sc`/`vs_pbr_skinned3d.sc`) is skinning-specific.

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

Correct normal transform, consistent with `vs_env_map3d.sc`/`vs_pbr3d.sc`.

## Missing or Weak Tests

No dedicated fog test found.

## Positive Findings

Well-documented Task 892 fix rationale (WVP-based normal transform is wrong under any non-identity camera, not just non-uniform World scale — a subtlety worth calling out explicitly).

## Final Assessment

One MEDIUM finding (shared fog-formula bug); the normal transform this file owns is correct.
