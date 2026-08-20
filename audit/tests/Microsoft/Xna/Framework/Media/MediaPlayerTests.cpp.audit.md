# Audit: tests/Microsoft/Xna/Framework/Media/MediaPlayerTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/MediaPlayerTests.cpp`
- Audit status: AUDITED (full read, 318 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `MediaPlayer` (confirmed genuine FNA implementation, not a stub — real behavior to diff against)
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `MediaPlayer`'s static API: shuffle/repeat semantics, play/pause/resume/stop state transitions, queue navigation (`MoveNext`/`MovePrevious`), volume/mute, visualization data, and the `ActiveSongChanged`/`MediaStateChanged` events as driven through `FrameworkDispatcher::Update()`.

## Executive Verdict
**PASS — notably strong.** This file demonstrates the project's healthiest self-correction pattern in the entire media test suite: multiple tests carry comments explicitly documenting that an EARLIER version of the same test asserted stub/placeholder behavior as if it were the specification, and were later corrected once the underlying feature became real (MEDIA-129, MEDIA-188/189/190, MEDIA-220, MEDIA-222, MEDIA-225). No MEDIUM-or-higher findings.

## Checklist Results
- `ShuffleCanRepeatTheSameSongIndex` (line 72): correctly proves FNA's actual (non-exclusion) shuffle semantics via a probabilistic argument with an explicitly computed, astronomically-small false-negative probability (0.5^200) — not a flaky test, and the comment shows the author verified this against FNA's `NextSong` source directly rather than assuming.
- `PlayEnqueuesADuplicateNotTheOriginalInstance` (line 99): correctly tests FNA's documented copy-not-reference semantics for `Play(Song*)`, verifying both value-equality (`Equals`) and reference-inequality (`queued != &original`), then proves the duplicate is independent by mutating the original's `PlayCount` and confirming the queued copy is unaffected.
- `ActiveSongChangedAndMediaStateChangedFireThroughFrameworkDispatcherUpdate` (line 210): uses `Add()`/`Remove()` tokens (not `operator+=`) specifically to avoid leaking a lambda-capturing subscription into `MediaPlayer`'s static/process-global event fields — correct awareness of a real cross-test-contamination risk for static event handlers.
- Visualization tests (lines 240-317) correctly distinguish what IS proven (enable/disable round-trips safely, disabled state always zeroes buffers) from what is NOT provable in this environment (the no-mixer/failed-tap-install branch, since the dummy SDL audio driver always succeeds) — the comment at line 294 explicitly retracts an earlier, overclaiming test name (`...NeverClaimsEnabledWithoutAWorkingTap`) that asserted more than the test environment could actually prove.

## Detailed Findings
None at MEDIUM or higher.

- **LOW** — `VisualizationEnabledStateStaysConsistentWithGetVisualizationData` (line 300) cannot exercise the no-mixer/failed-install branch in this CI environment by the test's own admission (dummy audio driver + device caching). This is a genuine, acknowledged, environment-imposed coverage gap rather than an oversight — flagged here only for completeness, since the source comment already documents it honestly.

## Cross-File Observations
- `MediaState` values (`Playing`/`Paused`/`Stopped`) are referenced via the fully-qualified enum here; see `MediaStateTests.cpp` (pending in this batch) for the enum's own dedicated test coverage.
- Static-state reset in `SetUp`/`TearDown` (`Stop()`, `Clear()`, resetting `IsRepeating`/`IsShuffled`) is necessary given `MediaPlayer`'s static/global design — this is the correct pattern for testing a XNA-mandated static class, not a design smell in the test.

## Missing or Weak Tests
- None identified beyond the already-acknowledged, environment-imposed gap above.

## Positive Findings
- The comment history embedded in this file (MEDIA-129, MEDIA-188/189/190, MEDIA-220, MEDIA-222, MEDIA-225) is an unusually transparent record of a test suite iteratively catching and fixing its own vacuous/overclaiming assertions — exactly the discipline the project's CLAUDE.md and `plans/plan_media.md` documentation aim for.
- `GetVisualizationDataZeroesTheBuffersWhileDisabled` pre-fills the buffers with `1.0f` sentinel values before calling `GetVisualizationData`, so a no-op implementation that merely fails to touch the buffer could not pass — good defense against a "looks tested but isn't" false pass.

## Final Assessment
No changes needed. Reference-quality test file.
