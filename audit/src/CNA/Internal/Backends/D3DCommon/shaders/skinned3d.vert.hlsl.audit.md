# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.vert.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/skinned3d.vert.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: SkinnedEffect vertex stage, per-pixel-lit path
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: SkinnedEffect.fx / Lighting.fxh (fog + normal transform + skinning)
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Bone-skins Position/Normal (up to 72 bones, 1/2/4 weights, matching FNA's real weight-count gating), transforms by MVP, computes fog factor. Explicitly "ported line-by-line from Vulkan".

## Executive Verdict

**Needs attention — shares the systemic fog bug AND the skinned-normal-transform bug.**

## Checklist Results

### Systematic FNA parity gaps
**HIGH — complete omission of the world-space normal-matrix contribution.** Line 74: `output.Normal = normalize(mul(input.Normal, (float3x3)skinMat));` — the normal is transformed by the bone-skin matrix alone; `World` never enters the calculation at all (compare `lit_textured3d.vert.hlsl`'s correct `InverseTranspose3x3((float3x3)World)`, in the very same directory). Under a rotated/scaled `World` transform, a skinned model's lighting normals are wrong. This shader's own header comment states it was **"Ported line-by-line from `src/CNA/Internal/Backends/Vulkan/shaders/skinned3d.vert.glsl`"** — the clearest, most explicit first-hand evidence in this whole audit of the Vulkan→D3DCommon porting chain that propagated this defect (see `AUDIT_CROSS_CUTTING_FINDINGS.md` for the parallel EasyGL→WebGPU chain). 1 of 5 D3DCommon skinned vertex shaders confirmed to share this exact complete-omission pattern.

### Systematic FNA parity gaps
**MEDIUM — shares the cross-cutting D3DCommon fog-formula bug.** Line 80: `output.FogFactor = (FogColorEnabled.w > 0.5) ? saturate((FogStartEnd.y - input.Position.z) / max(FogStartEnd.y - FogStartEnd.x, 1e-6)) : 1.0;` — algebraically
`(FogEnd - z) / (FogEnd - FogStart)`, the mirror-image of FNA's real `SetFogVector`/`ComputeFogFactor` formula
(`(z + FogEnd) / (FogEnd - FogStart)`), already proven wrong by this project's own XNA-oracle diff (commit
`74ad3bae`) and fixed in EasyGL but never ported here. Confirmed identical in all 15 fog-capable D3DCommon vertex
shaders (see `AUDIT_CROSS_CUTTING_FINDINGS.md`, "Systematic FNA parity gaps" — D3D11+D3D12 is the widest single
instance of this bug in the audit so far, affecting literally every fog-capable shader in the shared source). This file's own comment (line 77-78) additionally claims the formula "matches EasyGL/Bgfx's established SkinnedEffect fog formula exactly" — **false**: EasyGL's real formula is the corrected, post-Task-1111 one; only Bgfx's (also wrong) formula matches. Likely propagation mechanism: whoever wrote this shader copied Bgfx's formula while incorrectly believing it agreed with EasyGL's since-fixed one.

## Detailed Findings

**F1 (HIGH):** complete omission of world-space normal-matrix contribution, line 74.
**F2 (MEDIUM):** mirrored fog formula, line 80, with an inaccurate justifying comment.

## Cross-File Observations

Its own fragment-shader sibling `skinned3d.frag.hlsl` additionally lacks any `EmissiveColor` cbuffer field at all (unlike `lit_textured3d.frag.hlsl`'s `EmissiveColorPad`) — see that file's own report and `AUDIT_CROSS_CUTTING_FINDINGS.md` for the 3rd finding in this shader's overall defect profile.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `SkinnedEffect` non-uniform-scale/rotated-World lighting test found — masked by every current test's use of `World=Identity`.

## Positive Findings

Correctly ports FNA's bone-weight-count gating exactly; the explicit "ported from Vulkan" comment, while revealing a defect's origin, is itself a positive example of honest provenance documentation.

## Final Assessment

Two confirmed findings: HIGH (missing world-space normal transform, explicitly self-documented as a cross-backend port) and MEDIUM (shared fog-formula bug with an inaccurate justifying comment).
