# Audit: tools/devices/shutdown_ordering_harness.cpp

## Metadata
- Source file: `tools/devices/shutdown_ordering_harness.cpp` (53 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-devices` shard
- File type: C++ standalone tool (ASan-verification regression harness)
- XNA/FNA relevance: exercises `Microsoft::Devices::VibrateController::getDefaultProperty()` and
  `Detail::DevicesShutdownCoordinator::Shutdown()`
- Main related tests: intended to be run under ASan (`cmake-build-devices-asan`) with and without
  `--skip-shutdown-call`, per its own comment

## Purpose
Reproduces a real static-destruction-order hazard: `VibrateController::getDefaultProperty()`'s
function-local static singleton's destructor makes real `SDL_CloseHaptic()`/`SDL_QuitSubSystem()`
calls, but function-local statics are destroyed at process-exit teardown — *after* `main()`
returns, which can be after the application's own explicit `SDL_Quit()` call, a real use-after-free
risk this harness deliberately triggers (and, with `--skip-shutdown-call`, reproduces unfixed).

## Executive Verdict
Correct, small, and precisely single-purpose — exactly the kind of hazard that genuinely cannot be
exercised inside the shared `CnaTests` binary (calling real `SDL_Quit()` there would tear down SDL
process-wide for every other test sharing the binary), matching the isolation rationale already
established by other standalone harnesses in this shard/`tools/net`.

## Checklist Results
- The comment's exit-code semantics (lines 23-26) are unusually and correctly honest: "the actual
  signal this harness exists to produce is an ASan report... not the exit code itself" — this
  harness always returns 0 if it reaches the end of `main()` without crashing, deliberately not
  conflating "didn't crash" with "the fix works," since a real ASan heap-use-after-free report is
  the actual pass/fail signal an external caller (a CI script checking ASan output) must check.
- `getIsSupportedProperty()`'s comment (lines 11-16) correctly explains why this specific call
  reliably exercises the `SDL_QuitSubSystem(SDL_INIT_HAPTIC)` teardown branch regardless of what
  physical hardware the running container has: `SDL_InitSubSystem(SDL_INIT_HAPTIC)` succeeds even
  with no physical haptic device attached.
- `--skip-shutdown-call` (lines 18-21) is a deliberate, documented way to reproduce the *original*
  (unfixed) bug for verification purposes — explicitly stated as having been used to confirm under
  ASan that the harness "actually detects a real heap-use-after-free... not just that the fixed path
  happens not to crash," a genuinely rigorous verification-of-the-verifier step.

## Detailed Findings
None.

## Cross-File Observations
Shares the same standalone-process isolation rationale already established by
`tools/net/gamerservices_dispatcher_harness.cpp`/`net_two_process_harness.cpp` and
`tools/audio/mixer_destroy_active_*_voice_harness.cpp` (all audited this session) — process-global
singleton state with no reset hook, requiring a fresh process per test scenario.

## Missing or Weak Tests
N/A in the usual sense — this file IS the test, designed to be invoked externally under ASan with
its own output checked by the invoking script/CI step (not located/verified in this pass whether
that external invocation actually exists and runs in CI).

## Positive Findings
The "confirm the harness itself actually detects the bug, via `--skip-shutdown-call` under ASan,
not just that the fixed path is silent" verification step is a genuinely rigorous piece of
test-of-the-test discipline, directly analogous to `net_two_process_harness.cpp`'s own
`RunStartHostingPartialFailure()` verification rigor (audited this session).

## Final Assessment
No findings.
