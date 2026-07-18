# Audit: src/CNA/Internal/Backends/D3D9/D3D9SkinnedVertexColorDraw.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/D3D9/D3D9SkinnedVertexColorDraw.cpp`
- Audit status: AUDITED
- Subsystem: `backend-d3d9` shard
- File type: C++ implementation, 192 lines (fully read)
- XNA/FNA relevance: N/A (CNA-original NOXNA SkinnedEffect+vertex-color draw dispatch, no Microsoft Stock
  Effect equivalent — real SkinnedEffect.fx bytecode is never modified to add this)
- Graphics backend relevance: real `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch for
  `SkinnedVertexColor3D.hlsl`, selected ahead of the real `SkinnedEffect` dispatch when the bound vertex
  buffer's stride is 56
- Main related tests: `examples-tests-d3d9` (already audited via mechanical batch earlier this session)

## Purpose

Bone-palette upload (`UploadBonesVS()`), per-draw constant upload, and the real draw-call sequence for CNA's
own `SkinnedVertexColor3D.hlsl` shader.

## Executive Verdict

**Needs attention — provides the direct C++-level confirmation of a significant cross-cutting refinement:
this backend's `EmissiveColor` register genuinely receives the real, pre-folded Ambient+Emissive value.**

## Checklist Results

### Confirmed: `EmissiveColor` upload matches the real `SkinnedEffect::FillGpuDrawParams()` convention
Line 154: `TryUploadPixelShaderConstantEXT(..., "EmissiveColor", Pad3(params.emissiveColor).v)`, with an
explicit comment: "`SkinnedEffect::FillGpuDrawParams()` already pre-folds ambient into emissiveColor (matches
`DrawSkinnedEffectEXT`'s own identical upload of `params.emissiveColor` as `EmissiveColor`)". This is the 4th
independent backend (after EasyGL, Bgfx, SdlGpu) confirming the same "ambient pre-folded into `emissiveColor`
for skinned draws" convention — a significant piece of evidence refining the Vulkan/D3D11/D3D12 bug's likely
true root cause (see `AUDIT_CROSS_CUTTING_FINDINGS.md`'s updated writeup): those 2 backend-groups most likely
misconsume an already-correct upstream value (reading a separate, always-zero-for-skinned-draws
`ambientColor` instead) rather than the upstream computation itself being wrong.

### `UploadBonesVS()`
Correctly transposes each bone matrix and packs only the first 12 of 16 column-major floats per bone (a
`float4x3`, matching the shader's own declared `Bones[72]` type and the real, vendored `SkinnedEffect.fx`'s
own identical packing convention) — `std::copy(full16, full16+12, ...)`, not a naive full-16-float copy.

### Fog upload
Uploads raw `fogStart`/`fogEnd`/`fogEnabled` plus `weightsPerVertex` into `FogParams`, consistent with — and
the C++-side half of — the already-recorded object-space-only fog defect (the C++ side correctly forwards
what it's given; the shader's own choice of raw local Z is the actual defect, already recorded).

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
No issues found; requires non-null `texture0` matching `DrawSkinnedEffectEXT`'s own identical requirement
(explicit, documented parity between the stock and custom skinned draw paths).

## Detailed Findings

None new — this file is the direct source of a significant cross-cutting refinement (see above), not a
defect of its own.

## Cross-File Observations

This file's own explicit comment is one of 4 independent pieces of evidence (alongside EasyGL, Bgfx's Task-899
fix comment, and SdlGpu's reused-fragment-shader mechanism) that `SkinnedEffect::FillGpuDrawParams()` correctly
computes a pre-folded Ambient+Emissive value — strengthening the case that Vulkan's and D3D11/D3D12's own
skinned-effect ambient/emissive gaps are backend-side misconsumption bugs, not an upstream defect.

## Missing or Weak Tests

None beyond what's already recorded cross-cuttingly for the object-space-only fog defect.

## Positive Findings

Direct, explicit confirmation of the correct Ambient+Emissive pre-folding convention; correct bone-matrix
transpose-and-pack matching the real vendored `SkinnedEffect.fx`'s own convention.

## Final Assessment

No new defects; this file provides significant, direct evidence refining an already-recorded cross-cutting
finding's root-cause hypothesis.
