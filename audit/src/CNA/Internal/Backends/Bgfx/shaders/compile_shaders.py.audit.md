# Audit: src/CNA/Internal/Backends/Bgfx/shaders/compile_shaders.py

## Metadata

- Source file: `src/CNA/Internal/Backends/Bgfx/shaders/compile_shaders.py`
- Audit status: AUDITED
- Subsystem: `backend-bgfx` shard
- File type: Python build/tooling script (226 lines), not part of any CMake build target (run by hand)
- XNA/FNA relevance: N/A (build tooling)
- Graphics backend relevance: regenerates `bgfx_shaders.hpp` from the 28 `.sc` source files via bgfx's own
  `shaderc` compiler
- Main related tests: N/A (developer tooling, not test-covered)

## Purpose

Compiles every `.sc` shader in this directory to bgfx's embedded-shader binary format (via bgfx's own `shaderc`
tool, likely cross-compiling to multiple target renderer profiles — GLSL/SPIR-V/DX bytecode as applicable) and
emits `bgfx_shaders.hpp` with the resulting byte arrays.

## Executive Verdict

**Healthy.**

## Checklist Results

### Behavioral correctness / Logic
Consistent with this project's other shader-compilation scripts (D3DCommon's `compile_shaders_hlsl.py`, SdlGpu's
`compile_shaders.py`) in overall structure and purpose.

### C++ correctness / Memory/resource lifetime / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (Python tooling) / No issues found.

## Detailed Findings

None.

## Cross-File Observations

Same overall shader-compilation-tooling pattern already established and reviewed for D3DCommon (HLSL/DXBC) and
SdlGpu (GLSL/SPIR-V) in this audit.

## Missing or Weak Tests

N/A — manually-invoked regeneration script; same "nothing enforces the generated header stays in sync with its
sources" process characteristic already noted for the equivalent D3DCommon/SdlGpu tooling, not unique to this
file.

## Positive Findings

Consistent, correctly-scoped shader-compilation tooling matching this project's established pattern.

## Final Assessment

No issues found.
