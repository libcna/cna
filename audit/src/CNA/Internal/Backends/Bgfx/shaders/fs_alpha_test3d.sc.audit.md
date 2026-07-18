# Audit: src/CNA/Internal/Backends/Bgfx/shaders/fs_alpha_test3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/fs_alpha_test3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: AlphaTestEffect fragment stage
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Samples the texture, multiplies by tint, applies the AlphaFunction/ReferenceAlpha test, discards on failure, fog mix.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
Alpha-test encoding (`u_alphaTest.y > 0` -> equality-with-tolerance; else -> less-than, weight-based discard) independently verified identical to the same convention already confirmed correct across every other backend reviewed in this audit — consistent cross-backend implementation.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Consistent alpha-test encoding shared across the whole project.

## Missing or Weak Tests

No dedicated test found for the discard branch specifically.

## Positive Findings

Consistent, correct alpha-test convention.

## Final Assessment

No defects found.
