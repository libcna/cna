# Audit: tests/CNA/Internal/Audio/AudioMixerTests.cpp

## Metadata
- Source file: `tests/CNA/Internal/Audio/AudioMixerTests.cpp` (378 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-cna-internal` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `CNA::Internal::Audio::AudioMixer` (CNA-internal, no direct FNA equivalent)
- Main related tests: N/A (this IS a test file)

## Purpose
Tests `AudioMixer::GetMixer()`/`DestroyMixer()`'s device-negotiation, invalid-spec-rejection,
lifecycle-leak-freedom, and (via two spawned-process harnesses) use-after-free-on-active-voice
behavior.

## Executive Verdict
An exceptionally rigorous test file. Every test cites a specific tracked task ID (AUD-04-001/004/
006/007/008/009, AUD-15-017) tied to a real, specific concern, and several explicitly explain *why*
a naive version of the same test would be insufficient (e.g. the entryCount/device-negotiation
tests explain why testing only "did SDL decode it" wouldn't catch a subtly-wrong byte layout).

## Checklist Results
- Correctly handles the shared-process-singleton problem (`g_mixer` is process-wide,
  once-initialized) by using `MixerSpecOverrideGuard` (RAII, restores default spec + destroys mixer
  on scope exit regardless of how the scope is left) rather than leaving cross-test contamination
  risk.
- The two hardware-dependent tests (`GetMixerThrowsNoAudioHardwareExceptionWhenSdlAudioDriverIsInvalid`,
  the two `MixerDestructionWithActive*VoiceDoesNotCrashOrUseAfterFree` tests) correctly spawn a
  separate process with a watchdog (`WaitWithWatchdog`, `SpawnHarness`) rather than testing directly
  in-process — a deliberate, well-justified choice given `g_mixer`'s singleton nature would
  otherwise make a real crash in one test corrupt every other test in the same binary. This mirrors
  the established precedent set by `TwoProcessLoopbackTest.cpp` for the identical "needs a fresh
  process" class of problem.
- `AudioMixerInvalidSpecThrowsTest`/`RepeatedDeviceOpenFailuresLeaveBalancedLifecycleAndSubsequentSuccessIntact`
  correctly use `EXPECT_THROW(..., std::runtime_error)` — consistent with `AudioMixer`'s own
  documented exception-type contract (not flagged as an exception-type-convention violation, since
  this internal subsystem's own design uses `std::runtime_error` deliberately, unlike the XNA-facing
  API surface where `System::*` exceptions are the established convention).
- `GTEST_SKIP()` on a caught exception (rather than a hard failure) for the environment-dependent
  tests is a reasonable choice given the sandbox may only have SDL's "dummy" audio driver.

## Detailed Findings
None.

## Cross-File Observations
`AUD-04-008`/`AUD-04-009`'s spawned-harness pattern (`SpawnHarness`/`WaitWithWatchdog`/
`DrainRemaining`) is shared boilerplate explicitly generalized "to take an explicit path so the same
spawn/watchdog/drain plumbing serves the no-hardware harness plus the two mixer-destroy harnesses" —
a good example of DRY test infrastructure rather than triplicated spawn logic.

## Missing or Weak Tests
None identified — this file appears to have deliberately thorough coverage of `AudioMixer`'s public
surface, including several edge cases (32-bit multiplication overflow risk is covered in a sibling
`XactParserTests.cpp` file, not here, but the lifecycle-leak and invalid-spec paths here are
thorough).

## Positive Findings
The `AUD-04-004` test's own comment correctly distinguishes and cites real, verified SDL3 source
behavior (`OpenPhysicalAudioDevice`'s documented minimum-format floor) from a hypothetical CNA bug —
a good example of confirming test expectations against the actual dependency's real behavior rather
than assuming.

## Final Assessment
No findings.
