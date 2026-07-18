# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_skinned3d_vertexlit.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_skinned3d_vertexlit.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: SkinnedEffect fragment stage, per-vertex-lit path
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Consumes the already-computed v_litRGB/v_specularRGB varyings, applies vertex-color modulation after specular, fog mix.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Correctly forwards vertex-computed lighting (already including the correctly-forwarded Ambient+Emissive term from the vertex stage); vertex-color-modulation-after-specular ordering correctly matches `fs_skinned3d.sc`'s own discipline, per its own comment.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Correctly mirrors `fs_skinned3d.sc`'s own structure for the per-vertex-lit variant.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Clean division of labor between vertex and fragment stage.

## Final Assessment

No defects found; inherits the vertex-shader's normal-transform and fog-formula issues as a pass-through consumer, but correctly forwards the already-fixed Ambient/EmissiveColor terms.
