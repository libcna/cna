# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_colored_textured3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_colored_textured3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: BasicEffect fragment stage, VertexPositionColorTexture
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Samples the texture, multiplies by v_color0, fog mix.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Correct, identical structure to `fs_textured3d.sc`.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Byte-for-byte identical logic to `fs_textured3d.sc` (a deliberate, correct duplication for the different vertex-input variant).

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Correct, minimal implementation.

## Final Assessment

No defects found; inherits the vertex-shader fog-formula bug as a pass-through consumer only.
