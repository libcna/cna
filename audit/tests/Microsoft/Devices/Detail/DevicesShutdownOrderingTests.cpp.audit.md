# Audit: tests/Microsoft/Devices/Detail/DevicesShutdownOrderingTests.cpp

## Metadata
- Source file: `tests/Microsoft/Devices/Detail/DevicesShutdownOrderingTests.cpp` (133 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-microsoft-devices` shard
- File type: C++ test file (Google Test), spawns a separate harness process
- XNA/FNA relevance: Tests the real static-teardown-after-`SDL_Quit()` ordering hazard for
  `VibrateController::getDefaultProperty()`'s process-lifetime singleton (NOXNA internal
  infrastructure, no FNA reference)
- Main related tests: N/A (this IS a test file)

## Purpose
Spawns `tools/devices/shutdown_ordering_harness.cpp` as a genuinely separate process (since this
exact hazard — a function-local static singleton destructing after the application's own
`SDL_Quit()` call already ran — cannot be safely reproduced inside the shared, multi-test `CnaTests`
binary without tearing down SDL for every other test sharing it), with a watchdog timeout and clean
stderr capture for diagnostics.

## Executive Verdict
Correct, and a well-engineered solution to a genuine "needs a fresh process" testing problem, with
an explicit precedent citation (`tests/CNA/Internal/Audio/AudioMixerTests.cpp`) for this exact
pattern already established elsewhere in the codebase. The watchdog (`SIGKILL` + reap on timeout)
correctly prevents a hung harness from blocking the test suite indefinitely.

## Checklist Results
- `SpawnHarness()` correctly uses `posix_spawn_file_actions_adddup2`/`addclose` to capture the
  child's stderr without leaking the write end of the pipe into the parent — proper file-descriptor
  hygiene around `posix_spawn`.
- `WaitWithWatchdog()` correctly uses non-blocking `waitpid(..., WNOHANG)` polling with a deadline,
  falling back to `SIGKILL` + blocking reap on timeout — no zombie-process risk.
- The test's own comment (lines 106-115) honestly discloses what this does NOT prove: a real,
  successfully-opened haptic device is never available in this container, so the
  `SDL_CloseHaptic()` guard specifically remains reasoned-from-source only, not reproduced under
  ASan here — an accurate, precise scope boundary.

## Detailed Findings
None.

## Cross-File Observations
Directly tests the real-world consequence of `DevicesShutdownCoordinatorTests.cpp`'s unit-level
coverage of the coordinator's flag semantics — this file proves the coordinator's actual purpose
(safe ordering relative to `SDL_Quit()`) holds in the one scenario that matters, via a real process
boundary rather than an in-process simulation.

## Missing or Weak Tests
The file's own comment already discloses the haptic-device-close-guard gap — an honest,
already-identified limitation, not a hidden one.

## Positive Findings
A genuinely well-engineered separate-process test harness with correct subprocess lifecycle
management (spawn, capture, watchdog, clean reap).

## Final Assessment
No findings.
