# Audit: tests/Microsoft/Xna/Framework/Audio/MicrophoneTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Audio/MicrophoneTests.cpp` (462 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-audio` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Audio::Microphone`
- Main related tests: N/A (this IS a test file)

## Purpose
Exercises static discovery (`getAllProperty`/`getDefaultProperty`), lifecycle (`Start`/`Stop`),
`BufferDuration` validation, `GetSampleDuration`/`GetSampleSizeInBytes` delegation to
`SoundEffect`, `GetData`'s extensive bounds/overflow validation, and `BufferReady` event firing —
both against an isolated test-constructed instance and, in a separate fixture class, a real SDL
dummy-driver capture device.

## Executive Verdict
Excellent test file. `MicrophoneCaptureTest`'s design (using SDL's "dummy" audio driver, forced
via a static initializer that runs before any `TEST` body regardless of gtest run order) makes
real-capture-device tests deterministic in headless CI without `GTEST_SKIP` for the common case —
only genuinely device-less builds skip. `GetDataRejectsOffsetCountIntegerOverflow` (P9-AUDIT-002)
is a real, well-targeted regression test for an int32-overflow class of bug already fixed
elsewhere in this codebase (`SoundEffect`'s buffer/range constructor,
`DynamicSoundEffectInstance::SubmitBuffer`/`SubmitFloatBufferEXT`) but missed in this file until
this task, closing a real out-of-bounds-write risk (`SDL_GetAudioStreamData` receiving an
overflow-wrapped huge count).

## Checklist Results
- `BufferDurationNotMultipleOfTenThrows`'s own comment correctly identifies and explains an
  intentionally-untested branch (the setter's ">1000" check is unreachable via
  `getMillisecondsProperty()`'s `[-999, 999]` sub-second range) rather than silently having a gap
  with no explanation.
- `GetDataZeroOrNegativeCountThrows` and `GetDataNegativeCountThrows` are correctly split into two
  distinct tests despite the first test's name suggesting both are covered — the file's own
  comment explicitly flags that the first test only exercises `count == 0`, not a genuinely
  negative count, and adds the second test to close that real gap (MC-5).
- `GetDataSingleArgOverloadWithEmptyBufferThrows` is correctly distinguished from the 3-arg
  overload's zero-count test as "a genuinely distinct call path," not a duplicate.
- `GetDataAfterStopReturnsZeroAndLeavesBufferUntouched`'s comment explicitly distinguishes this
  from the never-started case as "a genuinely different history" even though both currently reach
  the same code path — a forward-looking regression-test rationale.
- `MicrophoneCaptureTest::TearDown()` explicitly documents why it clears `BufferReady` (the
  cached singleton `Microphone` instance is shared across every test in the binary, and a
  test-local lambda capturing locals must never survive to be called by a later test).

## Detailed Findings
None. This file substantially exceeds this project's own test-coverage bar.

## Cross-File Observations
`GetDataRejectsOffsetCountIntegerOverflow`'s own comment cross-references the exact same
integer-overflow bug class already fixed in `SoundEffect`/`DynamicSoundEffectInstance` (P9-VALIDATION-003)
— a good example of a later audit pass systematically re-checking a known bug pattern across every
file that shares the same validation shape, rather than assuming one fix generalizes automatically.

## Missing or Weak Tests
None identified.

## Positive Findings
The dummy-audio-driver-forced-before-any-test-runs static-initializer pattern, and the careful,
explicit reasoning for why seemingly-duplicate test pairs are actually distinct regression
coverage, are both strong test-engineering practices.

## Final Assessment
No findings.
