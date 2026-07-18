# Audit: src/CNA/Internal/Backends/Bgfx/shaders/vs_instanced3d.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/vs_instanced3d.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: NOXNA — GPU instancing helper
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Builds a per-instance World matrix from 4 instanced input rows, transforms position by world then the shared view-projection.

## Executive Verdict

**Healthy.**

## Checklist Results

### API / XNA parity
N/A (non-XNA extension). Correct instance-matrix construction from the 4 `i_dataN` rows.

### Logic
No fog term at all — correct and consistent with FNA (no instancing API, no fog handling for instanced draws) and with every other backend's own instanced3d shader.

### C++ correctness
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Consistent minimal design with every other backend's own instanced3d shader.

## Missing or Weak Tests

No dedicated test found.

## Positive Findings

Correctly minimal, stride-agnostic (only Position/Color read from the per-vertex stream).

## Final Assessment

No defects found.
