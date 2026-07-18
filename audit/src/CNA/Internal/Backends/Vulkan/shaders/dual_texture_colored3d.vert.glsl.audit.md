# Audit: src/CNA/Internal/Backends/Vulkan/shaders/dual_texture_colored3d.vert.glsl

## Metadata

- Source file: `src/CNA/Internal/Backends/Vulkan/shaders/dual_texture_colored3d.vert.glsl`
- Audit status: AUDITED
- Subsystem: `backend-vulkan` shard
- File type: GLSL shader source (compiled to SPIR-V via `compile_shaders.py`/`shaderc`)
- XNA/FNA relevance: DualTextureEffect stride-24 (VertexPositionColorTexture) vertex shader variant with VertexColorEnabled support.
- Graphics backend relevance: Vulkan (SPIR-V) shader source
- Main related tests: `examples-tests-vulkan` (already audited via mechanical batch this session)

## Purpose

DualTextureEffect stride-24 (VertexPositionColorTexture) vertex shader variant with VertexColorEnabled support.

## Executive Verdict

Healthy relative to this file's own scope; instantiates 1 already-recorded cross-cutting defect (fog formula), no new file-local defects.

## Checklist Results

### Behavioral correctness / FNA parity
Applies the correct Vulkan-specific `gl_Position.y = -gl_Position.y` NDC Y-flip (Vulkan's clip space is Y-inverted relative to OpenGL/D3D), consistent with the majority of this backend's 3D vertex shaders. **Shares the cross-cutting mirrored fog-formula bug** (`(FogEnd - z)/(FogEnd - FogStart)` instead of FNA's correct `(z + FogEnd)/(FogEnd - FogStart)`), already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md` — Vulkan is the historically-original source this bug propagated from into Bgfx/D3D11/D3D12.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (shader source, not C++) or no issues found.

## Detailed Findings

Applies the correct Vulkan-specific `gl_Position.y = -gl_Position.y` NDC Y-flip (Vulkan's clip space is Y-inverted relative to OpenGL/D3D), consistent with the majority of this backend's 3D vertex shaders. **Shares the cross-cutting mirrored fog-formula bug** (`(FogEnd - z)/(FogEnd - FogStart)` instead of FNA's correct `(z + FogEnd)/(FogEnd - FogStart)`), already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md` — Vulkan is the historically-original source this bug propagated from into Bgfx/D3D11/D3D12.

## Cross-File Observations

None beyond what is already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`.

## Missing or Weak Tests

No dedicated test found in this audit exercising a non-center/asymmetric pixel that would reveal the Y-flip or
fog-formula defect classes already recorded in `AUDIT_CROSS_CUTTING_FINDINGS.md`, where applicable.

## Positive Findings

Correct, consistent with the shared shader family.

## Final Assessment

See `AUDIT_CROSS_CUTTING_FINDINGS.md` for any cross-cutting defects this file instantiates; no NEW file-local
defects beyond what is already recorded there.
