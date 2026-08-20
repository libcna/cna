# Audit: tests/Microsoft/Xna/Framework/Media/Video/VideoPlayerTestAccess.hpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/Video/VideoPlayerTestAccess.hpp`
- Audit status: AUDITED (full read, 85 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test-only header (test access shim, not a Google Test file itself)
- XNA/FNA relevance: Test-only accessor exposing `VideoPlayer`'s private `decoder_`/`audioStream_`/`audioBuffer_` members
- Main related tests: `tests/Microsoft/Xna/Framework/Media/Video/VideoPlayerTests.cpp`

## Purpose
A `friend`-style test-access shim exposing `VideoPlayer`'s private internal state (decoder sample rate/channels/width, audio-stream presence/pause-state/pointer-identity, decoded-audio scratch-buffer size) to `VideoPlayerTests.cpp`, plus a helper (`SimulateAudioDeviceBecomingUnavailable`) that deterministically tears down a real, already-open SDL audio stream to exercise the no-audio-device code path without depending on the sandbox's real audio driver failing on its own.

## Executive Verdict
**PASS with one MEDIUM finding.** The accessors themselves are precise, well-scoped, and clearly justified (each is tied to a specific numbered regression). The one finding concerns a real cross-test global-state side effect in `SimulateAudioDeviceBecomingUnavailable`.

## Checklist Results
- Every accessor is minimal (single-purpose, returns exactly the value the corresponding test needs) and carries an inline comment naming the specific `plans/plan_media.md` task and defect it was added to prove.
- `GetAudioStreamPtr` (line 55) correctly returns RAW POINTER IDENTITY rather than just a boolean presence/pause-state check — the comment explains this is necessary to prove a track switch that shouldn't touch the audio stream genuinely left the SAME object alone, rather than a tear-down/reopen cycle that happens to end up in the same state.

## Detailed Findings
- **MEDIUM** — `SimulateAudioDeviceBecomingUnavailable` (line 74-82) calls `SDL_QuitSubSystem(SDL_INIT_AUDIO)` unconditionally whenever `player.audioStream_` is non-null. SDL subsystems are internally reference-counted, so this decrements (and potentially zeroes out) the PROCESS-WIDE audio subsystem refcount, not just this one player's state. If this test (`VideoPlayerTests.cpp`'s `AudioBufferDoesNotAccumulateWithoutAnAudioDevice`, the only caller) runs in the same test binary/process as other `VideoPlayer`/`MediaPlayer` tests that expect a working audio device stream, and gtest's execution order places this test BEFORE those others, the shared process' audio subsystem could be left in a torn-down state depending on how many times `SDL_InitSubSystem(SDL_INIT_AUDIO)` was called elsewhere. SDL3's `SDL_OpenAudioDeviceStream()`-family functions do generally auto-initialize the audio subsystem on demand, which likely self-heals this in practice (consistent with the suite reportedly passing), but the helper's own comment does not acknowledge or justify this global-state risk, and no compensating re-init call is made afterward to restore the subsystem to the state it found it in.

## Cross-File Observations
- The one caller of `SimulateAudioDeviceBecomingUnavailable`, `VideoPlayerTests.cpp`'s `AudioBufferDoesNotAccumulateWithoutAnAudioDevice`, does not call any subsystem-restoring cleanup in its own body either — see `VideoPlayerTests.cpp.audit.md` for the corresponding note from the test-file side.
- This is the only file in the entire `tests-xna-media` shard that manipulates an SDL subsystem's global init/quit state directly from test code (every other file in this shard operates purely through the public `Microsoft::Xna::Framework::Media` API surface) — worth a targeted, low-cost follow-up check (run this test file with `--gtest_shuffle` or in isolation vs. combined with the rest of the video/media test suite) to confirm test-order independence empirically, since the risk is real but plausibly already benign.

## Missing or Weak Tests
- N/A — this is a test-access shim, not itself a test file.

## Positive Findings
- Every accessor's comment cites the specific defect (MEDIA-90/131/148/153) it exists to prove, giving a future reader an immediate, precise reason for each otherwise-unusual private-member exposure.
- `GetAudioStreamPtr`'s pointer-identity design (rather than a simpler boolean) is a genuinely more rigorous test-design choice, catching a class of "looks fixed but actually tore down and rebuilt an equivalent object" false negative that a boolean check would miss.

## Final Assessment
Recommend either (a) documenting explicitly why the process-wide `SDL_QuitSubSystem(SDL_INIT_AUDIO)` side effect in `SimulateAudioDeviceBecomingUnavailable` is safe for cross-test ordering, or (b) restoring the subsystem (re-`SDL_InitSubSystem`) at the end of the one test that uses it, for defense-in-depth against future test-suite reordering or parallelization changes. MEDIUM priority — not blocking, since the current suite reportedly passes, but a real latent flakiness risk.
