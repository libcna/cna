# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/pbr3d.frag.hlsl

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/pbr3d.frag.hlsl`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard (shared D3D11/D3D12 HLSL source)
- File type: HLSL shader source
- XNA/FNA relevance: NOXNA — PbrEffect (metallic-roughness PBR), a CNA extension
- Graphics backend relevance: compiled into both the D3D11 and D3D12 backends via this shared directory
- FNA reference: N/A — glTF 2.0-derived BRDF, no direct FNA/XNA equivalent
- Main related tests: covered indirectly by `examples-tests-generic`/backend-specific example shards where a
  cross-backend test happens to register on D3D11/D3D12; no D3D11/D3D12-specific shader unit test found

## Purpose

Full metallic-roughness PBR shading: GGX/Trowbridge-Reitz normal distribution, Smith-Schlick-GGX visibility, Schlick Fresnel (glTF 2.0's own reference BRDF), per-pixel normal mapping via a re-orthogonalized TBN basis, ambient/emissive/occlusion terms, then fog mix.

## Executive Verdict

**Healthy — correct, well-implemented PBR BRDF and correct emissive handling.**

## Checklist Results

### API / XNA parity
N/A (non-XNA extension) — internal correctness against the glTF 2.0 reference BRDF is what matters, and the implementation (D term, G term via Smith-Schlick-GGX with the standard `k=(roughness+1)^2/8` direct-lighting remapping, Schlick Fresnel) is a faithful, term-for-term match to the documented reference.

### Logic
`emissive` (line 113) is added **unscaled** to `ambient + Lo` (line 115) — correct, matching glTF/PBR convention (not multiplied by albedo or diffuse). Deliberately omits an `AlphaTest` discard branch present in the EasyGL reference purely for boilerplate parity, correctly documented as dead code for real `PbrEffect` usage (its own `FillGpuDrawParams()` never enables alpha testing, and this backend's draw dispatch routes real `AlphaTestEffect` draws to a separate shader variant) — a well-reasoned, explicitly-justified scope cut rather than an unexplained omission.

## Detailed Findings

None.

## Cross-File Observations

Shares its BRDF math and correct emissive-unscaled convention with `pbr_skinned3d.frag.hlsl` — verified consistent.

## Missing or Weak Tests

No dedicated D3D11/D3D12 `PbrEffect` BRDF-correctness test found (PBR shader math correctness is verified elsewhere in this audit, e.g. `sdlgpu_pbreffect_test.cpp`, but not for this specific backend).

## Positive Findings

Faithful, correctly-cited glTF 2.0 reference BRDF implementation; well-justified, explicitly-documented scope cut for the unreachable alpha-test branch.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
