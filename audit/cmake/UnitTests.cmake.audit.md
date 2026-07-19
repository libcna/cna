# Audit: cmake/UnitTests.cmake

## Metadata
- Source file: `cmake/UnitTests.cmake` (271 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — defines the `CnaTests` executable and the global input-test filter)
- Main related tests: `CnaTests` (the entire gtest suite), `CnaInputTests` (registered here)

## Purpose
Defines the `CnaTests` executable: globs all `tests/*.cpp`, excludes POSIX-process-spawning test
files on WIN32/Emscripten/Android (`TwoProcessLoopbackTest.cpp`,
`GamerServicesDispatcherHangRegressionTest.cpp`, `AudioMixerTests.cpp`, `GltfToCnjToolTests.cpp`,
`DevicesShutdownOrderingTests.cpp`), wires up each harness dependency + baked-in path define, sets
up MinGW/Emscripten-specific link options, configures the Wine/DXVK/vkd3d cross-compiling-emulator
wrapper for D3D9/D3D11/D3D12, defines `CNA_INPUT_TEST_FILTER` (the canonical Input-track selector),
and applies a universal `SKIP_RETURN_CODE 77` convention to every registered test.

## Executive Verdict
Excellent — this is the file `input-ci.yml`'s audit praised for its single-source-of-truth
`CNA_INPUT_TEST_FILTER` design; reading it directly confirms the claim: the filter is defined here
exactly once (line 224), with an explicit comment warning that a new input suite whose name matches
none of the existing tokens must extend this one string (not a second copy elsewhere). This is the
stronger counterpart to `devices-tests.yml`'s hand-duplicated `DEVICES_GTEST_FILTER`.

## Checklist Results
- Each excluded POSIX-only test file has its own clear, consistent comment explaining exactly which
  process API it needs and why it can't work under MinGW/Emscripten/Android (no real multi-process
  spawning in Node/Wasm; Android's baked-in absolute harness path is meaningless on-device).
- The universal `SKIP_RETURN_CODE 77` retrofit (applied once via `get_property(...DIRECTORY
  PROPERTY TESTS)` rather than added individually to ~330 existing registrations) is a clean,
  purely-additive mechanism — the comment correctly notes this doesn't affect any test that never
  exits with code 77.
- `*_SKIP_*_GATE=1` environment variables passed to the Wine/DXVK wrappers for
  `gtest_discover_tests(DISCOVERY_MODE PRE_TEST)` are explained clearly: a bare
  `--gtest_list_tests` never creates a real graphics device, so the wrapper's normal DXVK/vkd3d-
  presence gate would otherwise misfire.
- mingw-w64's `std::type_info::operator==` COMDAT-folding linker limitation
  (`-Wl,--allow-multiple-definition`) is documented as a real, confirmed toolchain limitation with
  the specific reasoning why allowing the duplicate is safe (both copies are byte-identical).

## Detailed Findings
None.

## Cross-File Observations
Directly referenced/praised by `input-ci.yml`'s own audit as the single source of truth for the
Input-track test filter; contrasts favorably with `devices-tests.yml`'s separate, hand-duplicated
filter string.

## Missing or Weak Tests
N/A (build configuration, not a test file itself).

## Positive Findings
The `CNA_INPUT_TEST_FILTER` single-source-of-truth design and the purely-additive
`SKIP_RETURN_CODE 77` retrofit are both strong examples of avoiding maintenance-hazard duplication
across ~330+ test registrations.

## Final Assessment
No findings. Sets a design precedent (`ctest -L <label>` + a single canonical filter variable)
this project should apply consistently elsewhere — see `devices-tests.yml`'s finding for where it
hasn't been yet.
