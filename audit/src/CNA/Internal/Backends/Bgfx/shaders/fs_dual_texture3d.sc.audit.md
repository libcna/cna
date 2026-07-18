# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_dual_texture3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_dual_texture3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: DualTextureEffect fragment stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Samples both textures, doubles the first sample's RGB (the standard lightmap-multiply-by-2 convention), multiplies both with tint, fog mix.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
`base.rgb *= 2.0` correctly matches FNA's real `DualTextureEffect` convention, independently verified consistent with this exact formula already confirmed correct across every other backend in this audit.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Formula matches every other backend's own `DualTextureEffect` implementation exactly.

## Missing or Weak Tests

No dedicated test found for this specific file (the cross-backend `dualtextureeffect_vertexcolor_test.cpp` registers on EasyGL/Vulkan/Bgfx — already independently confirmed correct on this backend in that file's own audit).

## Positive Findings

Correct, FNA-accurate lightmap-multiply convention.

## Final Assessment

No defects found.
