# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d_vertexlit.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_lit_textured3d_vertexlit.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect fragment stage, per-vertex-lit path
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Consumes the already-computed v_litRGB/v_specularRGB varyings instead of recomputing lighting per pixel.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Correctly forwards vertex-computed lighting; emissive-unscaled convention correctly preserved (`v_litRGB` already includes it from the vertex stage).

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Correctly mirrors `fs_lit_textured3d.sc`'s own formula, per its own comment.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Clean division of labor between vertex and fragment stage.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
