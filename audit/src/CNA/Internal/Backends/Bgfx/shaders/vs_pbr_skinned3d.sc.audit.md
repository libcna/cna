# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_pbr_skinned3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_pbr_skinned3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: NOXNA — SkinnedPbrEffect (metallic-roughness PBR + bone skinning)
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Bone-skins Position/Normal/Tangent (matching FNA's real weight-count gating), then applies an additional World-space transform to the skinned normal/tangent before feeding pbr3d's own BRDF fragment stage unchanged.

## Executive Verdict

**Needs attention — shares the skinned-normal-transform bug's narrower (raw-World) variant, with a uniquely candid self-documented explanation.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH, confirmed (see `AUDIT_CROSS_CUTTING_FINDINGS.md`):** line 39, `v_normal = normalize(mul(u_world, vec4(skinnedNormal, 0.0)).xyz);` — uses the raw `u_world` matrix, NOT the inverse-transpose `vs_pbr3d.sc`'s own unskinned sibling correctly uses (via a CPU-precomputed `u_normalMatrix`). **This file's own header comment is uniquely, explicitly candid about the divergence**: "this applies an EXTRA World-space normal/tangent transform after skinning (unlike `vs_skinned3d.sc`'s plain `mat3(skinMat)` multiply) — an intentional divergence from the regular SkinnedEffect shader, matching `EnsurePbrSkinnedProgram()`'s own documented behavior." This is the 6th confirmed instance of this narrower bug variant (after EasyGL, WebGPU, D3D9, D3D11/D3D12, SdlGpu) and provides a 3rd, independent piece of direct evidence for this bug family's cross-backend propagation mechanism (alongside the D3D11 "ported line-by-line from Vulkan" comment and SdlGpu's "mirrors VulkanGraphicsBackend... exactly" comment) — see `AUDIT_CROSS_CUTTING_FINDINGS.md`.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting Bgfx fog-formula bug (already confirmed via the `examples-tests-bgfx`
batch, now confirmed directly at the source level here too).** `v_fogFactor = (u_fogParams.z - a_position.z) /
max(u_fogParams.z - u_fogParams.y, 1e-6)` — algebraically `(FogEnd-z)/(FogEnd-FogStart)`, the mirror-image of
FNA's real formula, already proven wrong by this project's own XNA-oracle diff (commit `74ad3bae`) and fixed in
EasyGL but never ported here. The comment's claim that this "matches EasyGL's established formula exactly" is
false — it matches EasyGL's pre-fix (wrong) formula, not its current, corrected one.

## Detailed Findings

**F1 (HIGH):** raw-World-instead-of-inverse-transpose normal transform, line 39.
**F2 (MEDIUM):** mirrored fog formula.

## Cross-File Observations

Directly comparable to `vs_pbr3d.sc` (gets this right) and `vs_skinned3d.sc` (shares the complete-omission variant of the same mistake) — the same 3-way comparison already established for D3DCommon/SdlGpu.

## Missing or Weak Tests

No dedicated non-uniform-scale-World test found (masked by every current test's use of `World=Identity`).

## Positive Findings

Correctly ports FNA's bone-weight-count gating exactly; the uniquely candid self-documentation of the correct-vs-incorrect divergence is a genuinely valuable piece of engineering honesty, even though the underlying choice is still wrong.

## Final Assessment

Two confirmed findings: HIGH (self-documented raw-World normal transform, F1) and MEDIUM (shared fog-formula bug, F2).
