# Audit: cmake/Harnesses.cmake

## Metadata
- Source file: `cmake/Harnesses.cmake` (208 lines)
- Audit status: AUDITED (full read)
- Subsystem: `build-cmake` shard
- File type: CMake module
- XNA/FNA relevance: N/A (build infrastructure — registers standalone (non-GTest) diagnostic/regression executables)
- Main related tests: `TwoProcessLoopbackTest.cpp`, `GamerServicesDispatcherHangRegressionTest.cpp`, `AudioMixerTests.cpp`, `GltfToCnjToolTests.cpp`, `DevicesShutdownOrderingTests.cpp` (each spawns one of these harnesses as a subprocess)

## Purpose
Registers 9 standalone (non-GTest, separate-`main()`) executables used as subprocess harnesses by
specific GTest regression tests that need real process-isolation (real two-process ENet, real
`SDL_Quit()`-ordering, a real no-audio-hardware condition, mixer-destroy-with-active-voice
use-after-free reproduction, etc.), plus the strict-XNA-API-surface compile-time gate (including
its `EXCLUDE_FROM_ALL` negative counterpart that must fail to compile) and a devices microbenchmark
tool.

## Executive Verdict
Excellent — each harness's comment explains precisely why it needs to be a separate OS process
(a process-wide singleton/cache that a shared `CnaTests` binary cannot safely reset or that would
tear down SDL for every other test sharing that binary), consistently citing the specific task ID
that introduced it. The negative-compile-check design (`cna_strict_xna_api_leak_check` +
`WILL_FAIL TRUE`) is a genuinely clever verification technique: it proves the enforcement mechanism
itself works by requiring a deliberately-broken build to fail, not just asserting the mechanism
exists.

## Checklist Results
- `cna_strict_xna_api_leak_check` is correctly `EXCLUDE_FROM_ALL` so it's never built by a normal
  `cmake --build .`, only by the one `ctest` invocation that expects its build to fail — a correct,
  deliberate isolation of a target whose entire purpose is to fail.
- Each harness correctly links only the minimal libraries it needs (e.g.
  `cna_audio_no_hardware_harness` links plain `CNA`, not `CNA_GamerServices`/`CNA_Net`).

## Detailed Findings
None.

## Cross-File Observations
`cmake/UnitTests.cmake` depends on several of these harness targets existing
(`add_dependencies(CnaTests cna_net_two_process_harness)` etc.) and bakes their built paths into
`CnaTests` via `target_compile_definitions` — this file and `UnitTests.cmake` are tightly coupled
by design, consistently so.

## Missing or Weak Tests
N/A (this file registers test-support harnesses, not tests directly) — see the consuming test
files (audited separately under `tests-cna-internal`) for whether each harness is actually
exercised meaningfully.

## Positive Findings
The `WILL_FAIL TRUE` negative-compile-check pattern for `cna_strict_xna_api_leak_check` is a
notably strong, self-verifying design that most projects would skip (asserting the positive check
works without ever confirming the negative case is actually enforceable).

## Final Assessment
No findings.
