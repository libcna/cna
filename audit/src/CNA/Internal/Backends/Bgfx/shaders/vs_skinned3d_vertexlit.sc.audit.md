# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d_vertexlit.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_skinned3d_vertexlit.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: SkinnedEffect vertex stage, per-vertex-lit path (XNA's real default)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal, computes full Blinn-Phong lighting in the vertex stage (Gouraud-interpolated).

## Executive Verdict

**Needs attention — shares the confirmed skinned-normal-transform bug (complete omission); confirms correct Ambient/Emissive forwarding for this variant too.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed:** lines 46-48, identical complete-omission normal transform to `vs_skinned3d.sc` (`mat3(skinMat)*a_normal`, no `u_world` contribution), explicitly self-documented as "unchanged from `vs_skinned3d.sc`."

### Positive: Ambient/Emissive forwarding
Line 66, `vec3 lit = u_emissiveColor.xyz + u_ambientColor.xyz + lightSum;` with an explicit comment confirming this "matches `fs_skinned3d.sc`'s own formula exactly" — independently confirms the Task 899 fix (Ambient pre-combined into the emissive uniform at the C++ level) applies to this per-vertex-lit variant too.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, lines 46-48.
**F2 (MEDIUM):** mirrored fog formula.

## Cross-File Observations

2nd of 2 non-PBR Bgfx skinned vertex shaders confirmed to share the exact complete-omission pattern; both correctly forward the pre-combined Ambient+Emissive term.

## Missing or Weak Tests

No dedicated rotated-World per-vertex-lit `SkinnedEffect` test found.

## Positive Findings

Correctly implements XNA's real default lighting mode (per-vertex) as its own dedicated variant; independently confirms the positive Ambient/Emissive-forwarding finding for a second shader.

## Final Assessment

Two confirmed findings: HIGH (missing world-space normal transform) and MEDIUM (shared fog-formula bug).
