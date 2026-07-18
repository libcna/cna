# Audit: src/CNA/Internal/Backends/Bgfx/shaders/varying.def.sc

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/varying.def.sc`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: BGFX shading language (.sc) source
- XNA/FNA relevance: N/A — shared vertex-attribute/varying declaration file for the whole shader family
- Graphics backend relevance: compiled by `compile_shaders.py` into `bgfx_shaders.hpp`, consumed by
  `BgfxGraphicsBackend`
- Main related tests: `examples-tests-bgfx` (98 files, already audited via mechanical batch this session)

## Purpose

Declares every semantic-tagged varying (`v_color0`, `v_texcoord0`, `v_normal`, `v_fogFactor`, `v_worldPos`, `v_litRGB`, `v_specularRGB`, `v_tangent`, `v_vertexColor0`) and vertex attribute (`a_position` through `i_data3`) used across every `.sc` file in this shard.

## Executive Verdict

**Healthy.**

## Checklist Results

### Architecture
Correctly documents the rationale for `v_vertexColor0`'s separate `COLOR1` slot (distinct from `v_color0`, which already carries `u_diffuseColor` for the skinned-vertex-color variants) — matches the same design already independently verified in `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s own `vColor` handling.

### C++ correctness / Logic
No issues found.

## Detailed Findings

None.

## Cross-File Observations

Central shared declaration file every other `.sc` shader in this shard depends on.

## Missing or Weak Tests

No dedicated test found for this file specifically.

## Positive Findings

Clear rationale for a non-obvious design choice (separate vertex-color varying slot).

## Final Assessment

No defects found.
