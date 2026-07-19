# Audit: cmake/Tests/SoftwareTests.cmake

## Metadata
- Source file: `cmake/Tests/SoftwareTests.cmake` (47 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake-tests` shard
- File type: CMake module (per-backend CTest registration)
- XNA/FNA relevance: N/A (build infrastructure — registers the Software (CPU rasterizer) backend's CTest suite)
- Main related tests: all 6 `examples/software_*_test.cpp` files (the `examples-tests-software` shard) plus `cross_backend_diagnostic_scene.cpp`

## Purpose
Registers the Software backend's 6 CTest executables (smoke, rasterizer, effects, culling,
clipping, dual-envmap-skinned) plus the non-CTest `cna_diag_software` cross-backend diagnostic dump
tool.

## Executive Verdict
Correct and minimal, consistent with `HeadlessTests.cmake`'s equally clean structure. The
diagnostic-dump tool (`cna_diag_software`) is correctly NOT registered via
`cna_register_backend_test` — consistent with `cmake/Harnesses.cmake`'s own `cna_diag_compare`
precedent of "standalone tool, not wired into ctest" for tools whose output is meant to be manually
compared, not pass/failed on exit code.

## Checklist Results
No issues — all 6 CTest registrations map 1:1 to the 6 files in the `examples-tests-software` shard.

## Detailed Findings
None.

## Cross-File Observations
`cna_diag_software`'s cross-backend-diagnostic role is directly paired with EasyGL's own
registration of the same `cross_backend_diagnostic_scene.cpp` source (see `docs/software-backend.md`'s
"Cross-backend diagnostic" section for the manual invocation, per this file's own comment).

## Missing or Weak Tests
N/A (build configuration).

## Positive Findings
Consistent, minimal, well-scoped registration file with a clear non-ctest tool correctly excluded
from `add_test()`.

## Final Assessment
No findings.
