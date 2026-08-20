# Audit: tests/Microsoft/Xna/Framework/Media/Video/VideoPlayerTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/Video/VideoPlayerTests.cpp`
- Audit status: AUDITED (full read, 558 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `VideoPlayer` (confirmed genuine FNA implementation — FNA's `VideoPlayerTheora`)
- Main related tests: N/A (this IS a test file); uses `VideoPlayerTestAccess.hpp` for private-state assertions

## Purpose
The most extensive single test file in this shard: covers `VideoPlayer` construction defaults, disposal (including idempotency and post-dispose throw guards), `Play()` metadata validation, real decode-to-texture playback, audio-stream resume/pause fidelity, EOF/looping/audio-tail timing behavior, multi-track audio/video switching (including no-op reselection and out-of-range indices), and a first-frame-decode-failure teardown regression.

## Executive Verdict
**PASS with two findings (one MEDIUM, one LOW-MEDIUM).** This file is exceptionally well-documented — nearly every test cites the exact `plans/plan_media.md` defect it guards against, several explicitly describing bugs found only via external code review AFTER an initial "complete" pass. The findings below concern one exception-type inconsistency and one inherited cross-file global-state risk.

## Checklist Results
- `MethodsThrowObjectDisposedExceptionAfterDispose` exhaustively lists every public method that must throw post-dispose (`Play`, `Stop`, `Pause`, `Resume`, `GetTexture`, `SetAudioTrackEXT`, `SetVideoTrackEXT`) in one place — good single-location coverage of the disposal contract across the whole public surface.
- `DisposeIsIdempotent`'s comment correctly documents an intentional deviation from FNA's literal double-`Dispose()`-throws behavior, explaining WHY replicating it verbatim would be dangerous in C++ (since `~VideoPlayer()` unconditionally calls `Dispose()`).
- `PlayGenuinelyResumesTheAudioStreamNotJustOpensIt` / `PauseStillActuallyPausesTheAudioStream` (MEDIA-131): together prove both halves of a resume-vs-pause regression using the real SDL device-pause state via `VideoPlayerTestAccess`, not just "doesn't throw."
- `NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration` (MEDIA-130/41): uses a dedicated fixture (`audio_tail.mkv`) whose audio deliberately outlasts its video track, proving the player waits on drained audio rather than video EOF alone — a genuine behavioral proof, not a timing coincidence.
- The track-switching regression suite (`SetVideoTrackEXTDoesNotTearDownTheUnrelatedAudioStream`, `SetAudioTrackEXTDoesNotRecreateTheUnrelatedVideoTexture`, `ReselectingTheSameAudioTrackDoesNotTearDownTheStream`, `ReselectingTheSameVideoTrackDoesNotRecreateTheTexture`, `SelectingAnOutOfRangeAudioTrackDoesNotTearDownTheStream`) systematically covers every combination of (video-only switch / audio-only switch) × (real switch / no-op reselect / out-of-range) against pointer-identity — a genuinely comprehensive regression matrix for MEDIA-148/154.

## Detailed Findings
- **LOW-MEDIUM** — `PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen` (line 432) asserts `EXPECT_THROW(player.Play(&video), std::runtime_error)` — a raw `std::runtime_error` rather than a `System::`-namespaced exception type. This is inconsistent with the SAME class's own other exception-throwing tests in this file (`MethodsThrowObjectDisposedExceptionAfterDispose` uses `System::ObjectDisposedException`; `PlayThrowsInvalidOperationExceptionOnDimensionMismatch` uses `System::InvalidOperationException`) — the same public `Play()` method throws a `System::` type for one failure mode (metadata mismatch) and a bare `std::runtime_error` for another (first-frame decode failure), with no comment explaining why this particular path was left as the C++-native type. CLAUDE.md's own acceptable-deviations table does list `std::runtime_error` as a generally-acceptable C++ exception mapping, so this is not a violation of project convention in isolation — but it IS an internal inconsistency within one class's own public-facing exception contract that a caller cannot predict without reading the source.
- **MEDIUM** (inherited, cross-file) — `AudioBufferDoesNotAccumulateWithoutAnAudioDevice` (line 480) calls `VideoPlayerTestAccess::SimulateAudioDeviceBecomingUnavailable`, which performs a process-wide `SDL_QuitSubSystem(SDL_INIT_AUDIO)` side effect with no compensating re-init. See `VideoPlayerTestAccess.hpp.audit.md` for the full analysis — flagged here too since this is the one test in this file responsible for triggering it.

## Cross-File Observations
- Several tests (`NonLoopedVideoEventuallyStopsAfterItsDuration`, `NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration`, `LoopedVideoKeepsPlayingPastItsDuration`, `PlayPositionIsZeroWhenStoppedAndAdvancesWhilePlaying`) rely on real wall-clock `sleep_for` calls with generous margins (up to ~6s polling windows for ~3s clips) — a standard and reasonably-defended timing-based testing pattern, but one that always carries some inherent CI-environment flakiness risk (e.g. an exceptionally slow or throttled CI runner). The margins chosen (2x the clip duration or more) are generous enough that this is a LOW rather than MEDIUM risk.
- `PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen` (line 432) writes a shared, fixed-path scratch fixture (`tests/assets/media/video/.player_first_frame_corrupted_fixture.mp4`) into the shared fixture directory with no per-run uniqueness (no PID/timestamp in the name); it does clean up via `std::remove` at the end, but a crash or early test-abort before that line would leave a stale corrupted file behind for the next run. LOW severity — self-healing on next successful run (the file is overwritten), and unlikely to cause a false result even if stale.

## Missing or Weak Tests
- No test verifies `VideoPlayer::Play()`'s exception TYPE (only that first-frame-decode-failure throws `std::runtime_error`) is stable/intentional versus incidental — see the exception-type inconsistency noted above.

## Positive Findings
- This file's regression-test density and documentation quality (nearly every test traces to a specific numbered defect with an explanation of both the bug and why the fix's correctness required a NEW test design, not just a assertion tweak) is the strongest in the entire `tests-xna-media` shard alongside `SongTests.cpp`.
- `PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen`'s corrupted-fixture technique (flipping bits inside the exact byte offset of the first keyframe packet, verified via `ffprobe pkt_pos`) is a genuinely rigorous way to force a real decoder failure deterministically rather than mocking the decoder.

## Final Assessment
Recommend (LOW-MEDIUM) clarifying or normalizing the exception type thrown by the first-frame-decode-failure path to match the class's own `System::`-exception convention used elsewhere in the same file, OR adding a comment explaining why `std::runtime_error` specifically was chosen for this one path. Recommend (MEDIUM, shared with `VideoPlayerTestAccess.hpp`) addressing the process-wide SDL audio subsystem teardown side effect in `AudioBufferDoesNotAccumulateWithoutAnAudioDevice`.
