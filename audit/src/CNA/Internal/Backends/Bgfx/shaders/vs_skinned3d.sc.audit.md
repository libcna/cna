# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: SkinnedEffect vertex stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal (matching FNA's real weight-count gating), transforms by WVP.

## Executive Verdict

**Needs attention — shares the confirmed skinned-normal-transform bug (complete omission), resolving this audit's last open question on this defect family.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed, RESOLVES THE LAST OPEN QUESTION IN THIS AUDIT'S BIGGEST CROSS-CUTTING FINDING (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** lines 27-29, `v_normal = normalize(skinMat[0].xyz*a_normal.x + skinMat[1].xyz*a_normal.y + skinMat[2].xyz*a_normal.z)` — the BGFX-shading-language component-wise spelling of `mat3(skinMat)*a_normal` (bgfx's shading language has no direct `mat3`-times-`vec3` shorthand for a `mat4`-derived 3x3, hence the manual expansion) — the normal is transformed by the bone-skin matrix alone; `u_world` never enters the calculation. **This makes Bgfx the 6th and FINAL backend confirmed at the direct shader-source level — every one of the 14 backends in this audit with a `SkinnedEffect` implementation now shares this exact defect, a complete, no-exceptions sweep.**

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, lines 27-29 — the final confirming instance of this audit's most exhaustively-verified defect.
**F2 (MEDIUM):** mirrored fog formula.

## Cross-File Observations

Completes the 6-of-6-backend confirmation of the skinned-normal-transform bug at the shader-source level — see `AUDIT_CROSS_CUTTING_FINDINGS.md` and `AUDIT_FINDINGS_INDEX.md` for the full cross-backend picture.

## Missing or Weak Tests

No dedicated rotated-World `SkinnedEffect` lighting test found (masked by `World=Identity` in every current test, per the established pattern for this whole bug family).

## Positive Findings

Correctly ports FNA's bone-weight-count gating exactly.

## Final Assessment

Two confirmed findings: HIGH (missing world-space normal transform — the final confirming instance across all 14 backends) and MEDIUM (shared fog-formula bug).
