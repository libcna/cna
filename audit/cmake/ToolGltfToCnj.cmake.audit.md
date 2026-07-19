# Audit: cmake/ToolGltfToCnj.cmake

## Metadata
- Source file: `cmake/ToolGltfToCnj.cmake` (16 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — registers the glTF-to-.cnj offline converter tool)
- Main related tests: `GltfToCnjToolTests.cpp` (spawns this tool as a subprocess)

## Purpose
Registers the standalone `cna_tool_gltf_to_cnj` developer content-conversion executable, built
unconditionally (not gated behind `CNA_BUILD_TESTS`) since it's a real developer tool, not a test.

## Executive Verdict
Correct and minimal — 16 lines with a clear, accurate comment explaining why this is
unconditionally built (a developer content tool, matching `cna_diag_compare`'s own
"standalone tool, not wired into ctest" precedent) and why it needs no graphics backend/window
(it only constructs plain math value types: `Matrix`/`Vector3`/`Quaternion`/`TimeSpan`).

## Checklist Results
No issues — the comment's claim ("no graphics backend or window/device initialization is ever
triggered by this tool") is consistent with linking only `CNA`/`SHARP_RUNTIME`, no backend target.

## Detailed Findings
None.

## Cross-File Observations
`cmake/UnitTests.cmake` depends on this target (`add_dependencies(CnaTests
cna_tool_gltf_to_cnj)`) for `GltfToCnjToolTests.cpp`'s real-subprocess spawn.

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
Clear, accurate justification comment for an unconditional (not test-gated) build.

## Final Assessment
No findings.
