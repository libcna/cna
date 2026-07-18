# Audit: src/CNA/Internal/Backends/D3DCommon/shaders/compile_shaders_hlsl.py

## Metadata

- Source file: `src/CNA/Internal/Backends/D3DCommon/shaders/compile_shaders_hlsl.py`
- Audit status: AUDITED
- Subsystem: `backend-d3dcommon` shard
- File type: Python build/tooling script (149 lines), not part of any CMake build target (run by hand)
- XNA/FNA relevance: N/A (build tooling)
- Graphics backend relevance: regenerates `hlsl_shaders.hpp` from the 17 `.hlsl` source pairs
- FNA reference: N/A
- Main related tests: N/A (developer tooling, not test-covered)

## Purpose

Cross-builds `hlsl_compiler_tool.cpp` with `x86_64-w64-mingw32-g++`, runs the resulting `.exe` once per shader
file through `scripts/run-wine-dxvk.sh` (this project's established Wine+DXVK harness) to produce real DXBC
bytecode, then emits `hlsl_shaders.hpp` with one `static constexpr uint8_t` array per shader.

## Executive Verdict

**Healthy.** The `SHADERS` list was independently cross-checked line-for-line against the actual 17 `.hlsl` file
pairs on disk and the 34 array names in `hlsl_shaders.hpp` — exact 1:1 correspondence in all three places, with
correct `entry_point`/`target_profile` (`main`/`vs_5_0` or `ps_5_0`) for every shader.

## Checklist Results

### Behavioral correctness / Logic
`main()` correctly errors out (`sys.exit(1)`) if any listed `.hlsl` file is missing before attempting to compile
anything, rather than partially regenerating the output header. `compile_one()` correctly surfaces both stdout and
stderr from a failed compile and raises, rather than silently producing a truncated/empty array.

### Architecture
The documented rationale for why this can't be a native-Linux tool (`D3DCompile()` only exists inside
`d3dcompiler.dll`, no native Linux D3D shader compiler exists to call in-process the way `libshaderc` is used for
SPIR-V) is accurate and consistent with `hlsl_compiler_tool.cpp`'s own design.

### C++ correctness / Performance / Thread safety / Portability / Maintainability / Robustness / Testing
N/A (Python tooling) / No issues found.

## Detailed Findings

None.

## Cross-File Observations

The `SHADERS` list's 17 entries were verified to exactly match: (a) the 17 `.vert.hlsl`/`.frag.hlsl` file pairs
present on disk, (b) the 17 `D3DShaderVariant` enumerators in `D3DShaderCache.hpp`, and (c) the 34
`static constexpr uint8_t k*Dxbc[]` array names actually present in `hlsl_shaders.hpp` — full triangulated
consistency across the whole shader-embedding pipeline.

## Missing or Weak Tests

N/A — this is a manually-invoked regeneration script, not part of any automated build/test target, so there is no
CI verification that `hlsl_shaders.hpp` is actually up to date with the current `.hlsl` sources at any given
commit (a `.hlsl` source could be edited without anyone re-running this script, silently leaving stale bytecode
checked in). Not a defect in this file itself, but worth flagging as a project-wide process gap, consistent with
`compile_shaders.py`'s equivalent SPIR-V process elsewhere in this project having the same characteristic.

## Positive Findings

Correctly fails fast and loudly (missing-file check before compiling anything; full stderr/stdout surfaced on a
compile failure) rather than silently producing a partial or corrupted header.

## Final Assessment

No issues found in the script itself; the broader "nothing enforces hlsl_shaders.hpp staying in sync with its
.hlsl sources" process gap is a project-wide pattern (shared with the SPIR-V equivalent), not unique to this file.
