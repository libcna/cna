# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> Covers the **audio** subsystem work on `feature/audio` only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> Full file-by-file history, every fix's exact rationale, and FNA/FAudio line citations live in
> **`plan_audio.md`** (repo root). This file is a *short* current-state summary, not a duplicate
> of that log — if a section here grows into a multi-paragraph history again, trim it back.
> **The `Microsoft::Xna::Framework::Media` namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend. It is a
framework/runtime, not a game.

- **This branch's goal:** port and verify `Microsoft::Xna::Framework::Audio` file-by-file against
  the authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`), matching XNA
  behavior exactly, with full test coverage.
- **Current phase:** the original compliance/bugfix plan (Phase 0–6), Phase 7 (30 findings) and
  Phase 8 (25 findings) — two prior line-by-line audits against FNA — are fully closed. **Phase 9**
  is a separate, user-directed "audio correctness hardening" pass against a fixed, user-specified
  task list (`plan_audio.md`'s "Phase 9" section), organized into 11 named groups
  (`P9-LIFECYCLE`, `P9-CATEGORY`, `P9-VALIDATION`, `P9-DOCS`, `P9-BUILD`, `P9-STOP`, `P9-XACT`,
  `P9-3D`, `P9-HARDWARE`, `P9-DYNAMIC`, `P9-AUDIT`). **All 11 groups are now fully closed** —
  `P9-CATEGORY`'s last 6 deferred sub-items (real XACT category `instanceLimit`/
  `maxInstanceBehavior`/fade in/out enforcement) closed in `d3b66dea`. Four further user-directed
  follow-ups beyond Phase 9's original fixed list were also completed, each after the user was
  asked which of several remaining candidate areas to continue in: **`P9-CATEGORY-011`** (real
  cue-level, as opposed to category-level, `instanceLimit`/`maxInstanceBehavior`/fade
  enforcement), `effc3626`; a **ThreadSanitizer stress test** for `SoundEffectInstance`'s
  filter-coefficient locking (`T-4C`'s previously-open "not empirically stress-tested" gap),
  `3ebec7db` — ran clean, zero races found; **`P9-3D-010`** (listener-orientation-aware
  `Apply3D` pan, the candidate explicitly parked at `P9-3D-009`), `8c622307`; and **`P9-XACT-016`**
  (continuous RPC volume/pitch re-evaluation, the candidate explicitly parked at `P9-XACT-005`),
  `76b71b07` — the last remaining candidate from that series. Eight genuine open design decisions
  came up on this branch and were all asked-and-resolved with the user's explicit input (XACT
  filter `OneOverQ` fidelity, `Cue::IsPlaying`/`IsPaused` coexistence, `Cue::Stop(AsAuthored)` fade
  timing, category `instanceLimit`/fade scope+approach, and four successive rounds of "which
  follow-up area next" — cue-level `instanceLimit`, the ThreadSanitizer stress test, `Apply3D` pan
  orientation, then continuous RPC re-evaluation) — none remain open.
- **Phase 10** (started 2026-07-06, `plan_audio.md`'s "Phase 10 — Audio correctness hardening and
  XNA/XACT parity" section) is a large, mostly-open forward-looking task list (12 groups,
  `P10-AUDIT`/`P10-VAR`/`P10-RPC`/`P10-FILTER`/`P10-DSP`/`P10-REVERB`/`P10-3D`/`P10-PAN`/
  `P10-HRTF`/`P10-LOOP`/`P10-DYN`/`P10-SE`/`P10-SEI`/`P10-XACT`/`P10-MIC`/`P10-HW`/`P10-CI`/
  `P10-SAN`/`P10-DOC`), NOT a fixed closed set like Phase 9 was. Its kickoff pass investigated the
  reported "weighted variation lottery double-subtraction bug" and found the algorithm itself
  correct (byte-for-byte FAudio port, `P10-VAR-002`) — the real, previously-undiagnosed bug was in
  the *pre-existing statistical test*, which reused one `Cue` across 200 loop iterations and (due
  to a Cue modeling exactly one playthrough) was silently only ever running 1 real trial, not 200
  — now fixed, plus new deterministic (seed-hook-based and construction-guaranteed) tests added
  (`P10-VAR-004/005`). Also fixed one stale doc claim (interactive variation selection was still
  described as "uniform pick" despite being variable-range-driven since `P9-XACT-002/003/004`,
  `P10-AUDIT-004`), and resolved+locked-down two open XNA-docs-vs-FNA-behavior decisions for
  `DynamicSoundEffectInstance`'s constructor and post-dispose accessors (match FNA's permissive/
  no-throw behavior, `P10-DYN-001/002/004/005`). See `plan_audio.md`'s Phase 10 section for the
  full per-task breakdown, the "Known accepted deviations" table, and two recorded (not started)
  design proposals (RFC-1: internal crossfeed-capable mixing layer; RFC-2: optional FAudio/FACT
  backend for exact XACT 3D/reverb parity).
  **A second, self-directed round (still 2026-07-06) closed all 8 remaining "verification sweep"/
  small-test candidates from the kickoff's own next-steps list**, each its own commit:
  `P10-3D-003` (emitter above/below/front/behind/left/right pan tests), `P10-LOOP-005` (remaining
  loop-region edge cases), `P10-XACT-004/005` (mid-record truncation tests; confirmed compact
  wave-bank coverage), `P10-XACT-007` (duplicate names, remaining disposed-bank per-method cases),
  `P10-XACT-010` (confirmed XACT event-type coverage is complete vs. FAudio's real enum),
  `P10-MIC-004` (`Microphone::GetData` empty-buffer and after-stop cases), `P10-HW-004`
  (audited hardware-dependent test skip discipline; found one pre-existing, unfixed, low-
  probability gap in `SoundEffectTests.cpp`'s buffer-constructor tests), `P10-SAN-002` (dedicated
  adversarial ASan/UBSan sweep, repeat-stressed 15-20x per risk area — clean). No design
  decisions were needed for any of these (pure test-addition or read-only verification), so none
  required a confirm-with-user round first.
  **A third, self-directed round (still 2026-07-06) closed the two remaining pure-test-addition
  candidates**: `P10-SE-002` (three new malformed-WAV `FromStream` fixtures — unsupported format
  tag, truncated `fmt` chunk, truncated `data` chunk — all empirically confirmed to throw
  `System::NotSupportedException` via SDL3_mixer's own decoder rejecting them) and `P10-SEI-002`
  (a full `Volume`/`Pitch`/`Pan`/`IsLooped` × during-play/after-pause/after-stop/after-dispose test
  matrix, 14 new tests, built from a line-by-line read of FNA's real setters rather than guessed —
  confirmed CNA already matches FNA's exact per-property gating, including the non-obvious
  `IsLooped`-vs-`Pan` disposal asymmetry). Every remaining Phase 10 candidate is now either real
  feature/behavior-decision work or large mechanical audit work — both need the user's confirmed
  scope first (see §8).
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is parsed
  by a hand-written `CNA::Internal::Audio::XactParser` and mixed through SDL_mixer. This backend
  choice is the root cause of every documented deviation from FNA (see `CHECKLIST.md` and
  `docs/xna-4-api-coverage.md`'s Audio section for the full compatibility table) — no per-source 3D
  audio graph, no aux-send/reverb bus, only a 2-value stereo gain pair instead of a 4-coefficient
  crossfeed matrix, a single per-track "cooked callback" slot, etc.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases used on the XNA API surface. It is under **separate, active, concurrent development** by
  another session — if a build ever fails inside `SHARP_RUNTIME/CMakeFiles/...`, check
  `git status`/`git log -1` there before assuming the audio code broke something.

---

## 2. Current status

- **Build:** clean as of commit `4c4dc272` (last verified). EasyGL backend (Linux default),
  `SOUND_ENABLED` on, SDL3_mixer linked. `cna_demo_sound`/`cna_demo_2d` example targets not
  rebuilt this pass (no Audio public API surface touched, only tests/docs).
- **Tests:** `CnaTests` whole-suite count is **3321 / 3323 pass** as of the last full run (2
  skipped: `AccelerometerTests`/`GyroscopeTests`' `GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  hardware-dependent, expected). The audio-scoped subset (§7's `--gtest_filter` audio suite list)
  is **430 / 430 pass** under a fresh one-off ASan+UBSan build, repeat-stressed 3x in full plus
  15-20x on the four `P10-SAN-002` risk areas individually (dispose cascades, fire-and-forget
  sweep, `DynamicSoundEffectInstance` callback lifetime, dynamic stream pause/resume) — zero
  leaks/errors throughout, exit 0. Also clean (zero races) under a dedicated one-off
  ThreadSanitizer build (see §5's `T-4C` row, unchanged this pass).
  `CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically`'s prior "un-seeded RNG" flake
  is now understood and fixed (Phase 10 kickoff, `P10-VAR-002`): it reused one `Cue` across all 200
  loop iterations, which (since a `Cue` models exactly one playthrough) meant only the first
  iteration ever actually ran -- fixed to create a fresh `Cue` per iteration, matching real usage.
  One remaining pre-existing, full-suite-load-only flake:
  `CueTest.PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances`
  (a short ~1.13ms fixture occasionally racing full-suite scheduling load), confirmed
  non-reproducing in isolation (not a regression). A third, also
  unrelated to Audio, was observed once under a full-suite ASan+UBSan run:
  `TwoProcessLoopbackTest.HostAndClientJoinAndExchangeAppDataAcrossRealProcesses` (Net module,
  two-process integration test, likely ASan-timing-sensitive) — not reproduced in the audio-scoped
  ASan subset, and outside this branch's `Microsoft::Xna::Framework::Audio`/
  `CNA::Internal::Audio` scope. A fourth, also outside Audio's scope, surfaced during
  `P10-SAN-002`'s ASan sweep while an overly-broad `--gtest_filter` briefly caught a `Net` test:
  `NetworkSessionTest.UpdateAfterDisposeThrows` genuinely leaks in `NetworkSession::BeginCreate`
  (confirmed reproducible). Flagged here for whoever owns `Net`, not fixed on this branch.
- **New standalone test executable:** `cna_audio_no_hardware_harness` (from
  `tools/audio/audio_no_hardware_harness.cpp`), spawned as a real independent OS process by
  `tests/CNA/Internal/Audio/AudioMixerTests.cpp` to test the no-audio-hardware failure path (a
  process-wide, once-ever-initialized mixer cache makes this untestable any other way). Not part
  of `--target CnaTests` itself but built automatically as one of its dependencies. Excluded on
  `WIN32`/`EMSCRIPTEN`/`ANDROID` (same reasons as the pre-existing `cna_net_two_process_harness`).
- **CLI/tools/apps:** none in the framework itself — this is a library/framework, not an
  application. `cna_demo_sound`/`cna_demo_2d` are example programs exercising the Audio API; they
  aren't part of `--target CnaTests` and are easy to forget to rebuild.
- **What works:** `SoundEffect`/`SoundEffectInstance` (real SDL3_mixer playback, move-only
  instance-tracking Dispose cascade, real low/high/band-pass filters); `DynamicSoundEffectInstance`
  (real buffer queue via `SDL_AudioStream`); `AudioEngine`/`SoundBank`/`WaveBank`/`Cue` (real
  hand-written XACT parser, real category volume/pause/resume/stop, real natural-completion and
  authored-fade-timed stop-tail state reconciliation, real 3D pan/attenuation/Doppler);
  `Microphone` (real SDL3 capture). See `docs/xna-4-api-coverage.md`'s Audio compatibility table
  for the full implemented/approximate/unsupported breakdown.
- **What does not work / remains incomplete:** everything open is a deliberate, documented
  `CHECKLIST.md` accepted deviation (no reverb, no true 3D HRTF/elevation, stereo hard-pan instead
  of crossfeed, `AttackTime`/`ReleaseTime` RPC envelope variables and filter-frequency/Q/DSP-preset
  RPC targeting unsupported, etc.), not a bug. See §5 for the full table. Both category-level *and*
  cue-level XACT `instanceLimit`/`maxInstanceBehavior`/fade are now real and enforced
  (`P9-CATEGORY-005..011`). `Apply3D`'s pan is now listener-orientation-aware too (`P9-3D-010`) —
  it's still a linear approximation (no multi-speaker azimuth diffusion), not "ignoring orientation
  entirely" anymore. XACT RPC volume/pitch curves are now continuously re-evaluated every engine
  tick (`P9-XACT-016`), not just once at `Play()` time.

---

## 3. Recent changes

Newest first. One line each; full rationale, FNA/FAudio citations, and `git stash` verification
notes for every item are in `plan_audio.md`'s "Phase 9"/"Phase 10" sections.

- `4c4dc272` — **`P10-SEI-002`**: a full `Volume`/`Pitch`/`Pan`/`IsLooped` × during-play/
  after-pause/after-stop/after-dispose test matrix (14 new tests, `SoundEffectInstanceTests.cpp`).
  Built from a line-by-line read of FNA's real setters, not a guess: `Volume`/`Pitch` have no
  `IsDisposed`/`hasStarted` guard at all; `Pan` is gated only by `IsDisposed`; `IsLooped` is gated
  only by a one-way `hasStarted` latch that `Pause()`/`Stop()`/`Dispose()` never reset. CNA's
  setters already matched exactly — no production code change. Locks down the non-obvious
  `IsLooped`-vs-`Pan` disposal asymmetry: disposing *before ever playing* does not throw for
  `IsLooped` (unlike `Pan`'s `ObjectDisposedException`), since `hasStarted` is still false.
- `2c4cbd82` — **`P10-SE-002`**: added the three remaining `FromStream` malformed-WAV fixtures
  (`SoundEffectTests.cpp`) — an unsupported `fmt` `audioFormat` tag (`0x2000`), a truncated `fmt`
  chunk, and a `data` chunk whose declared size wildly exceeds the actual sample bytes present.
  Empty-stream and non-WAV-garbage cases were already covered pre-existing (Fáze 9). All three new
  cases empirically confirmed (not assumed) to throw `System::NotSupportedException` — SDL3_mixer's
  own WAV decoder rejects each cleanly, converted by `FromStream`'s existing error path. No
  production code change needed.
- `88c6bddf` — **`P10-SAN-002`**: dedicated one-off ASan+UBSan sweep, repeat-stressed (15-20x)
  separately across dispose cascades, fire-and-forget sweep/timeout teardown, dynamic-instance
  callback lifetime, and stream pause/resume, plus the full audio suite 3x — all clean. Docs-only
  (no code change; found nothing to fix in Audio, see §2's flake note for the one unrelated `Net`
  leak surfaced while narrowing the filter).
- `3770fbf5` — **`P10-HW-004`**: audited every Audio test for hardware-guard discipline.
  `SoundEffectInstanceTest`'s 46 `TEST_F` bodies each call `REQUIRE_DEVICE()` as their literal
  first statement (verified, not assumed); `DynamicSoundEffectInstanceTests.cpp`'s inline
  `GTEST_SKIP()` guards are placed exactly where needed. One real, pre-existing, low-probability
  gap found and documented (not fixed, out of this pass's scope): `SoundEffectTests.cpp`'s
  buffer/range-constructor tests don't guard against `NoAudioHardwareException` the way this same
  file's `FromStream` tests do.
- `48955e52` — **`P10-MIC-004`**: added `GetDataSingleArgOverloadWithEmptyBufferThrows` and
  `GetDataAfterStopReturnsZeroAndLeavesBufferUntouched` (real-device fixture, runs for real even
  under the dummy driver). Confirmed "after dispose" doesn't apply — `Microphone` has no
  `Dispose()`, matching FNA.
- `0191c568` — **`P10-XACT-010`**: confirmed `XactParser.cpp`'s `FACTEVENT_*` constants match
  FAudio's real enum byte-for-byte (no XACT event type CNA fails to recognize); added
  `ComplexTrackStopsScanningAtUnrecognizedEventType` proving the one remaining malformed-input
  path stops scanning safely instead of misreading trailing bytes.
- `5cf885ad` — **`P10-XACT-007`**: added `UpdateAfterDisposeDoesNotThrow` (documents the one
  deliberate exception to `AudioEngine`'s throw-on-disposed pattern), `PlayCueThreeArgAfterDispose
  ThrowsObjectDisposed`, `IsInUseFalseAfterDisposeWhilePlaying`, and
  `DuplicateCategoryNamesResolveDeterministicallyToLastDeclaredIndex` (proves the plain
  `std::unordered_map`-overwrite semantics for duplicate XACT names are well-defined, not UB).
- `c142b40a` — **`P10-XACT-004/005`**: added one mid-record-truncation test per format (XGS/XSB/
  XWB), each truncating a real fixture to the format's exact minimum-size threshold. Confirmed
  compact wave-bank coverage is already thorough (6 dedicated tests plus every higher-level
  fixture builder defaulting to the compact `wbFlags` bit) — no new test needed.
- `62919a17` — **`P10-LOOP-005`**: added tests for a full-sound loop region, `start+length==full`
  with a nonzero start, an explicitly out-of-range region (confirms `P9-VALIDATION-002` holds
  here too), immediate `Stop()` while looping, and `Dispose()` while looping.
- `194cb512` — **`P10-3D-003`**: added a `ComposedPan()` helper reproducing `Apply3D`'s real
  listener-right-projection + pan-formula pipeline, then six tests for the canonical emitter
  directions — closes the previously-unasserted above/below (vertical) case.
- `b2f234fe` — **Phase 10 kickoff** (`plan_audio.md`): investigated the reported weighted-
  variation-lottery "double-subtraction" bug and found the algorithm correct (byte-for-byte FAudio
  port, `P10-VAR-002`) -- the real bug was in the *pre-existing statistical test*, which reused one
  `Cue` across 200 loop iterations and silently only ran 1 real trial (a `Cue` models exactly one
  playthrough); fixed to use a fresh `Cue` per iteration. Added a deterministic RNG seed hook
  (`Cue::INTERNAL_seedRngForTest`/`CueTestAccess::SeedRng`) plus new construction-guaranteed and
  seed-replica-verified weighted-variation tests (`P10-VAR-004/005`). Fixed one stale doc claim
  (interactive variation selection still described as "uniform pick" despite being variable-range-
  driven since `P9-XACT-002/003/004`, `P10-AUDIT-004`). Resolved and locked down (via new tests)
  two open XNA-docs-vs-FNA-behavior decisions for `DynamicSoundEffectInstance`: its constructor
  and `GetSampleDuration`/`GetSampleSizeInBytes` intentionally match FNA's permissive/no-throw
  behavior rather than MSDN's stricter documented contract (`P10-DYN-001/002/004/005`). Appended
  the full Phase 10 task list (~90 items across 12 groups, mostly still open) to `plan_audio.md`,
  which had been deleted from the working tree in a prior commit (`14f1f9cd`) and was restored
  from git history before appending (see Phase 10's own "Housekeeping note").
- `76b71b07` — **`P9-XACT-016`** (user-directed follow-up, the candidate parked at `P9-XACT-005`,
  and the last remaining item from this branch's "which follow-up area next" rounds): XACT RPC
  volume/pitch curves are now re-evaluated continuously, every `AudioEngine::Update()` tick (via
  `Cue::ReconcileState()`), instead of only once at `Cue::Play()` time — matching real FACT's
  `FACT_INTERNAL_UpdateRPCs`. The per-frame hook point already existed (`AudioEngine::Update()`
  already ticks `ReconcileState()` for `P9-STOP-010`'s fade timing), lowering the scope versus
  what `P9-XACT-005` originally anticipated. A cue with no RPC bindings keeps the exact same
  zero-per-tick-work path as before. Side-effect fix found while touching this code: the existing
  fade-out/fade-in wall-clock ramps now also fold in the RPC volume multiplier, which they
  previously silently dropped the moment a fade began (independent of one-shot-vs-continuous RPC).
  `AttackTime`/`ReleaseTime` envelope variables and filter-frequency/Q/DSP-preset RPC targeting
  remain unsupported, unchanged.
- `8c622307` — **`P9-3D-010`** (user-directed follow-up, the candidate parked at `P9-3D-009`):
  `Apply3D`'s pan now projects the emitter's relative position onto the listener's own right axis
  (`Cross(Forward, Up)`, normalized) instead of raw world-space X, via new
  `SoundEffectInstance::INTERNAL_calculateListenerRight()`. Verified the axis order against XNA's
  own `Vector3.Right` constant (default orientation reduces to exactly `(1,0,0)`), so an unrotated
  listener — the only case the old code ever handled — gets bit-identical pan to before. Only the
  listener's orientation is used; the emitter's own `Forward`/`Up` remain unread, matching real
  X3DAudio (emitter orientation there only affects multi-channel emitter configs).
- `3ebec7db` — **`T-4C` ThreadSanitizer stress test** (user-directed follow-up, no
  behavior change): new `SoundEffectInstanceTests.cpp::ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread`
  hammers the real production `INTERNAL_apply{Low,High,Band}PassFilter` setters from a second
  thread against a real background mixing thread (even the SDL `dummy` driver spins one up) for
  400ms straight. Run 10x plus once across the whole audio suite under a dedicated one-off
  ThreadSanitizer build — zero races reported, closing `T-4C`'s previously-open "not empirically
  stress-tested" gap (full detail, including the two-different-locks finding in
  `MIX_SetTrackCookedCallback` vs `MIX_LockMixer`, is in `plan_audio.md`'s `T-4C` entry, not the
  Phase 9 section).
- `effc3626` — **`P9-CATEGORY-011`** (user-directed follow-up beyond Phase 9's original
  list): real cue-level (XSB, per-cue) `instanceLimit`/`maxInstanceBehavior`/fade enforcement via
  new `AudioEngine::CheckCueInstanceLimit()`, called from `Cue::Play()` *before* the category-level
  check, matching `FACT_internal.c`'s `play_sound()` order exactly. Reuses the same FAIL/
  REPLACE_LOWEST_PRIORITY/collapsed-QUEUE-OLDEST-QUIETEST behavior as the category-level check,
  scoped to "same SoundBank + same cue index" for the *count*. The one new wrinkle: the *eviction
  victim search*, when the cue-level limit triggers, has **no category filter and no
  same-cue-definition filter at all** (matches `handle_instance_limit(cue, NULL)` exactly — a real
  FAudio quirk/oversight, replicated rather than "fixed"), so it can evict a completely unrelated
  cue instead of a sibling instance of the same named cue; proven with a dedicated test.
- `d3b66dea` — **`P9-CATEGORY-005..010`** (closes Phase 9): real XACT category `instanceLimit`/
  `maxInstanceBehavior` enforcement in `AudioEngine::CheckCategoryInstanceLimit()` (called from
  `Cue::Play()`) — `FAIL` rejects the new cue, `REPLACE_LOWEST_PRIORITY` evicts the lowest-priority
  active same-category cue, `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` all evict the oldest
  (matching FAudio's own acknowledged collapse of those three). Category `fadeInMS`/`fadeOutMS`
  now drive a real fade-out (victim, reusing `P9-STOP-010`'s ramp) and fade-in (new cue, new
  symmetric ramp) — exactly where real FACT applies them (instance-limit replacement only, never
  `AudioCategory::Pause/Resume/SetVolume/Stop`).
- `642b8432` — **`P9-STOP-010`** (resolved open decision): `Cue::Stop(AsAuthored)` now uses a real,
  parsed, authored `fadeOutMS` to drive an actual linear volume ramp over that exact duration
  (ticked lazily on every state getter and per-frame via `AudioEngine::Update()`); a cue with no
  authored fade (every "simple" XACT cue) now hard-stops immediately, matching FNA/FACT exactly.
- `3df604ba` — **`P9-LIFECYCLE-013`** (resolved open decision): `Cue::IsPlaying` and `IsPaused` can
  now both be `true` at once, matching FACT (`Pause()` no longer clears `IsPlaying`).
- `c5049c9c` — **`P9-AUDIT-001..005`**: forked re-read of the whole Audio namespace; found and
  fixed one real bug (`Microphone::GetData`'s int32 `offset+count` overflow, same class as
  `P9-VALIDATION-003`) and one stale doc-comment; confirmed `CHECKLIST.md` otherwise accurate.
- `5e9063a7` — **`P9-HARDWARE-005/006`**: added a real fresh-process regression test for
  `GetMixer()`'s no-audio-hardware failure path via a new standalone harness executable.
- `4fe631fa` — **`P9-HARDWARE-003/004`** (resolved open decision): `AudioEngine`/`SoundBank`/
  non-streaming `WaveBank` now throw `FileNotFoundException`/`InvalidOperationException` on
  missing/corrupt files, matching FNA's `TitleContainer.ReadToPointer` exactly.
- `a8b8ab76` … `88703d53` — **`P9-3D-001..009`** (all closed, 9/9): audited/fixed `Apply3D`'s
  distance attenuation (real bug found and fixed) and implemented real Doppler pitch shift; pan
  remains a documented world-space-X-only approximation (`Forward`/`Up` orientation ignored).
- `308e542b` … `4d78b139` — **`P9-DYNAMIC-001..009`** (all closed, 9/9): fixed two real
  `PendingBufferCount` bugs; uncovered and fixed a cross-cutting `System::EventHandler<T>` bug in
  the sibling `sharp-runtime` repo (commit `8342a2c` there, not yet pushed).
- `85b8ce86` / `b718a8d1` / `4d2246bd` — **`P9-HARDWARE-001/002`**, **`P9-XACT-001..015`** (all
  closed): raw `std::runtime_error` converted to `NoAudioHardwareException`; real per-track XACT
  filter (frequency+Q) wiring; fixed a real sound-code aliasing bug that could play the wrong
  sound on corrupt/malformed content.
- Earlier (`bd1c932`/`e4ffb1e`/`47a58f5`/`7bec360`/`8f439dd`/`9039ec6`/`5f3e5d0`..`c5f50c9`) —
  **`P9-STOP-001..009`**, **`P9-BUILD-001..007`**, **`P9-DOCS-001..007`**,
  **`P9-VALIDATION-001..015`**, **`P9-CATEGORY-001..004`**, **`P9-LIFECYCLE-001..015`**: original
  Phase 9 pass. Fixed the two most serious bugs on this branch: an `offset+count` int32-overflow
  causing a real segfault (`SoundEffect`/`DynamicSoundEffectInstance`), and `Cue::IsPlaying`/
  `IsPaused`/`IsStopped` never reconciling after natural playback completion.

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists, and Phase 9 is now fully closed** (all 11 groups,
including `P9-CATEGORY`'s last 6 sub-items, `d3b66dea`). As of the last full run, the build was
clean and all tests passed (§2). Phase 10 (`plan_audio.md`) is a large, intentionally-open task
list, not a single confirmed next task — see §8 for the smallest concrete candidates.

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, under separate, active, concurrent development by another session. A build
failure inside `SHARP_RUNTIME/CMakeFiles/...` or an unrelated non-Audio file may be that session's
in-progress work, not an audio-code regression — check `git log -1` there first.

**Dependency note:** `DynamicSoundEffectInstanceTests.cpp`'s
`BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing` test depends on
`sharp-runtime` commit `8342a2c` (`System::EventHandler<T>::Raise()`'s snapshot-before-iterating
fix). That commit exists in the local `../sharp-runtime` checkout but has **not been pushed**. If a
fresh clone/pull of `sharp-runtime` ever lacks this commit, that one CNA test will fail.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Accepted deviation** | XACT category/cue `maxInstanceBehavior`'s `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` values all collapse to "evict the oldest active cue" (matches FAudio's own acknowledged collapse of those three, not a CNA-only shortcut) | `CHECKLIST.md`, `P9-CATEGORY-005/010/011` |
| **Accepted deviation** | A cue-level `instanceLimit` eviction's victim search has no category filter and no same-cue-definition filter at all — it can evict a completely unrelated cue in the same SoundBank instead of a sibling of the same named cue | `CHECKLIST.md`, `P9-CATEGORY-011` |
| **Accepted deviation** | The built-in `"AttackTime"`/`"ReleaseTime"` RPC envelope variables, and RPCs targeting filter frequency/Q or a DSP preset, remain unevaluated (no elapsed-playback-time tracking, no DSP preset system) — RPC volume/pitch targeting is otherwise now continuously re-evaluated, `P9-XACT-016` | `CHECKLIST.md`, `P9-XACT-005/006/007/016` |
| **Accepted deviation** | RPC-only cue release timing (`maxRpcReleaseTime`, no authored `fadeOutMS`) unimplemented — needs the `"ReleaseTime"` built-in variable specifically, not blocked by RPC continuity in general anymore | `CHECKLIST.md`, `P9-STOP-010`, `P9-XACT-016` |
| **Accepted deviation** | `Apply3D`'s pan is a single-axis linear approximation (listener-orientation-aware since `P9-3D-010`, projected onto `Cross(Forward, Up)`), not real X3DAudio's full multi-speaker energy-diffusion azimuth pipeline; the emitter's own `Forward`/`Up` remain unread (matches real X3DAudio — emitter orientation only affects multi-channel emitter configs there) | `CHECKLIST.md`, `P9-3D-010` |
| **Accepted deviation** | Stereo hard-pan eliminates the opposite channel instead of crossfeed-blending it (`Pan` property and `Apply3D` both) | `CHECKLIST.md`, `CP-19`, `P9-3D-001` |
| **Accepted deviation** | A parsed per-track filter's type can only ever decode to low-pass or high-pass, never band-pass (replicates a real FAudio bit-decode quirk) | `CHECKLIST.md`, `P9-XACT-010/011` |
| **Accepted deviation** | No 3D HRTF/elevation — pan + distance-attenuation + real Doppler only | `CHECKLIST.md` |
| **Accepted deviation** | Reverb is a documented no-op (`INTERNAL_applyReverb`) — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md`, `T-4C` |
| **Accepted deviation** | `AudioEngine::Init()` never queries real hardware, so it can never throw `NoAudioHardwareException` from the constructor the way FNA's does (`SoundEffect`/`DynamicSoundEffectInstance` can) | `CHECKLIST.md` |
| **Internal-only, documented** | `ParseXgs`/`ParseXsb` accept a big-endian magic cosmetically only — no byte-swap logic exists, so a real BE file would silently misparse, not throw | `XactParser.cpp` comments, `P9-AUDIT-003` |
| **Internal-only, documented** | `AudioMixer::DestroyMixer()` is dead code — nothing calls it | `AudioMixer.hpp` comment, `P9-AUDIT-003` |
| **Internal-only, documented** | `g_mixer`'s lazy-init has no mutex — assumed (not enforced) main-thread-only contract | `AudioMixer.cpp` comment, `P9-AUDIT-003` |
| **Verified clean (2026-07-06)** | `SoundEffectInstance` filter coefficient locking stress-tested under real concurrency: a new test hammers the real production filter setters from a second thread against a real background mixing thread for 400ms, run 10x plus once across the whole audio suite under a dedicated ThreadSanitizer build — zero races reported | `T-4C`, `ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver (aside from the dedicated no-hardware harness); real-hardware runs are manual/ad-hoc | — |

Full list with FNA/FAudio line citations: `plan_audio.md`. `CHECKLIST.md` is the authoritative,
current list of accepted deviations from FNA/XNA behavior.

---

## 6. Architecture notes

### Main modules

| Component | Location | Notes |
|---|---|---|
| XNA audio API | `include/Microsoft/Xna/Framework/Audio/`, `src/.../Audio/` | Must match XNA 4.0 / FNA exactly |
| Internal mixer | `CNA/Internal/Audio/AudioMixer.{hpp,cpp}` | SDL3_mixer `MIX_Mixer` singleton, opened lazily on first use |
| XACT parser | `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp` | Hand-written `.xgs`/`.xsb`/`.xwb` reader — FACT is **not** used |
| sharp-runtime | `../sharp-runtime/` | `System.*` types, primitive aliases, exception hierarchy |

### Data flow (playback)

```
SoundEffect (loads MIX_Audio; move-only, instance-tracking Dispose cascade)
  → CreateInstance() returns SoundEffectInstance BY VALUE
  → Play() creates a MIX_Track, binds the MIX_Audio, plays
  → INTERNAL_apply{Low,High,Band}PassFilter register a real state-variable filter via a
    SDL3_mixer per-track callback; reverb is a documented no-op
DynamicSoundEffectInstance
  → user submits buffers → SDL_AudioStream → MIX_Track
  → FrameworkDispatcher::Update() pumps registered instances and raises BufferNeeded
AudioEngine/SoundBank/WaveBank/Cue
  → XactParser reads .xgs/.xsb/.xwb → cues map to SoundEffect/SoundEffectInstance via SDL_mixer
  → Cue::ReconcileState() lazily reconciles Playing→Stopped (natural completion) and
    Stopping→Stopped (authored fadeOutMS elapsing, or the tail naturally finishing if no fade was
    authored), queried live on every state getter and ticked once per frame by
    AudioEngine::Update()
  → AudioCategory operations snapshot activeCues before iterating (mutate-during-iteration-safe)
  → AudioEngine::Update() sweeps each registered SoundBank's finished fire-and-forget cues
Microphone (capture)
  → getAllProperty() enumerates via SDL_GetAudioRecordingDevices; Start()/Stop() manage a real
    SDL_AudioStream capture stream
```

### Invariants that must stay stable

- **Backend = SDL3_mixer only.** Do not reintroduce FAudio/FACT.
- **Exceptions on the XNA surface must be `System::` types**, never raw `std::` exceptions
  (internal `XactParser` corrupt-data throws are the one sanctioned exception, caught/converted at
  the `SoundBank`/`WaveBank` constructor boundary).
- **`Cue::ReconcileState()` only mutates `active_`/`state_`/`paused_`/`fadeOutMS_`, never
  `waveBanksUsed_`/`AudioEngine`'s registries** — it runs from `const` state getters that may be
  called mid-iteration over those other registries elsewhere (e.g.
  `WaveBank::getIsInUseProperty()`). Actual unregistration only happens from `StopInternal()`
  (explicit `Stop(Immediate)`/`Dispose()`) or `SoundBank::SweepFireAndForget()`.
- **All four `AudioEngine` category operations snapshot `activeCues` before iterating** — don't
  revert to live iteration; `Cue::Stop()` cascades into `UnregisterCue()`, which erases from that
  same vector.
- **Never validate a byte-range as `offset + count > buffer.size()` in `intcs` (int32)** — this
  caused a real segfault twice (`SoundEffect`/`DynamicSoundEffectInstance`, then `Microphone`). Use
  the unsigned-arithmetic pattern (`off > buf.size() || cnt > buf.size() - off`) everywhere.
- **`Cue::State::Stopping`** models an authored-stop cue whose real tail (or authored fade) is
  still in progress; don't collapse it back into `Stopped` synchronously.
- **`Cue::Pause()`/`IsPaused` never clear/depend on `IsPlaying`** — they're independent flags,
  matching real FACT's bitmask semantics (`P9-LIFECYCLE-013`).
- **`SoundEffect` is move-only** — a single owner per resource is what makes its instance-tracking
  Dispose cascade unambiguous. Don't reintroduce copy semantics.
- **SPDX + Doxygen + `NOXNA` + SharpRuntime aliases** required per `CLAUDE.md`/`CHECKLIST.md`.

---

## 7. Useful commands

```bash
# --- Native desktop test preset (recommended) ---
cmake --preset tests
cmake --build --preset tests --target CnaTests -j"$(nproc)"
SDL_AUDIODRIVER=dummy ./cmake-build-tests/CnaTests

# --- Manual equivalent ---
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests

# Run only the audio test suites (always include *Microphone* -- no other pattern here matches
# "Microphone" as a substring of MicrophoneTest.<TestName>, a gap found and fixed by P9-AUDIT-005)
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='*SoundEffect*:*Dynamic*:*AudioEmitter*:*AudioListener*:*SoundState*:*AudioChannels*:*AudioStopOptions*:*MicrophoneState*:*Microphone*:*PlayLimit*:*NoAudio*:*NoMicrophone*:*Audio*:*Cue*:*WaveBank*:*SoundBank*:*XactParser*'

# Rebuild the example demos too if touching anything on the Audio public API surface
cmake --build cmake-build-debug --target cna_demo_sound cna_demo_2d -j"$(nproc)"

# One-off ASan+UBSan verification (delete the build dir after use)
cmake -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build cmake-build-asan --target CnaTests -j"$(nproc)"
SDL_AUDIODRIVER=dummy ./cmake-build-asan/CnaTests
rm -rf cmake-build-asan

# One-off ThreadSanitizer verification (delete the build dir after use) -- used to confirm
# SoundEffectInstance's filter-coefficient locking under real concurrent mixing-thread load
# (T-4C's ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread); check for a genuinely-linked
# TSan runtime with `nm cmake-build-tsan/CnaTests | grep __tsan_init` if a run looks suspiciously
# clean.
cmake -B cmake-build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build cmake-build-tsan --target CnaTests -j"$(nproc)"
SDL_AUDIODRIVER=dummy ./cmake-build-tsan/CnaTests --gtest_filter='SoundEffectInstanceTest.ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread' --gtest_repeat=10
rm -rf cmake-build-tsan

# git-stash regression-verification pattern used for every fix on this branch:
#   1. git stash push -- <changed source files>   (keep the new test, revert just the fix)
#   2. rebuild, run the new test, confirm it FAILS against the pre-fix code
#   3. git stash pop                                (restore the fix)
#   4. rebuild, run the full suite, confirm green

# Dependencies: SDL3/SDL3_image/SDL3_mixer are git submodules under third_party/, built into the
# persistent .sdl-prebuilt/ cache on first configure (-DCNA_USE_SYSTEM_SDL=ON to use system
# packages instead). googletest is a git submodule under vendor/, always built from source.
git submodule update --init --recursive   # run once after cloning

# FNA reference source for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio
```

---

## 8. Next smallest tasks

Phase 9 (§1/§4) and its four user-directed follow-ups are all closed. **Phase 10**
(`plan_audio.md`'s "Phase 10 — Audio correctness hardening and XNA/XACT parity") is now the
active task list — unlike Phase 9, it is NOT a fixed closed set; it has ~90 individual task IDs
across 12 groups. Its kickoff pass plus two further self-directed rounds (§3, all 2026-07-06)
closed 16 items total, including **every** "verification sweep"/small-test candidate previously
listed here (`P10-3D-003`, `P10-LOOP-005`, `P10-XACT-004/005/007/010`, `P10-MIC-004`, `P10-HW-004`,
`P10-SAN-002`, `P10-SE-002`, `P10-SEI-002`). Read `plan_audio.md`'s Phase 10 section in full before
picking anything — every task there already has a concrete, cited current-status note (done /
partially done / open-with-a-reason), not a vague placeholder. **Every remaining candidate below
is either real feature/behavior-decision work or a large mechanical audit — both need the user's
confirmed scope first**, unlike the 16 already closed. Roughly in increasing order of scope:

1. **`P10-RPC-002`** (real feature work, confirm scope first) — wire `Cue::Apply3D()`'s
   already-computed distance/angle/Doppler values back into `variables_` so RPC curves bound to
   `"Distance"`/`"OrientationAngle"`/`"DopplerPitchScalar"` see live values instead of whatever was
   last manually `SetVariable()`-d.
2. **`P10-LOOP-003/004`** (real feature work, confirm scope first) — a custom playback-cursor or
   raw-callback approach so a bounded loop region no longer truncates the first (pre-loop)
   playthrough. Two concrete approaches already sketched in `plan_audio.md`, neither started.
3. **`P10-FILTER-002/003/004/006`** (real feature work, confirm scope first) — RPC-driven live
   filter frequency/Q targeting, extending the continuous-tick infra `P9-XACT-016` already built
   for volume/pitch into `SoundEffectInstance`'s filter setters.
4. **`P10-AUDIT-002/003`** (large, mechanical, no design decision, but sizable) — the full
   per-member XNA 4.0 Audio API cross-reference at full granularity (every property/method/
   constructor/enum-value/exception), bucketed into implemented/tested/approximate/unsupported.
   Only a class-level table exists today (`docs/xna-4-api-coverage.md`).
5. **`P10-PAN-002`/RFC-1** and **`P10-HRTF-002`/RFC-2** — the two largest, most invasive items
   (crossfeed stereo mixing layer; optional FAudio/FACT backend). Explicit design proposals only
   so far, not started; would need their own confirm-scope round the same way `P9-XACT-016` did.

As before on this branch: ask the user to confirm scope/approach before starting any item that's
real feature work (not a small test addition or read-only verification sweep), the same way every
prior real decision on this branch was confirmed first.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit.** Phase 7 and Phase 8 already did two
  rounds of that.
- **No silently picking a Phase 10 item that is real feature/behavior work and just starting it.**
  Confirm scope with the user first, same as every real decision on this branch so far, especially
  for the two explicit design-only RFCs (`P10-PAN-002`, `P10-HRTF-002`), which are proposals, not
  approved work. **Exception, already established practice on this branch:** a pure test-addition
  or read-only verification-sweep item (no behavior/design decision involved) can be self-selected
  without asking first -- this is exactly how `P10-3D-003`/`P10-LOOP-005`/`P10-XACT-004/005/007/010`/
  `P10-MIC-004`/`P10-HW-004`/`P10-SAN-002`/`P10-SE-002`/`P10-SEI-002` were all closed across two
  self-directed rounds (§3). Use judgment: if closing the item could ever require picking between
  two different real behaviors, it needs the user's input first; if it's "add a test proving X" or
  "audit and report," it doesn't. **As of this pass, every remaining Phase 10 candidate needs the
  user's confirmed scope first** (§8) -- there is no more self-selectable pure-test-addition work
  left on the list.
- **No re-litigating a resolved open decision without the user asking first.** All eight that ever
  came up on this branch are resolved: XACT filter `OneOverQ` fidelity vs. narrowing
  (`P9-XACT-011`); `Cue::IsPlaying`/`IsPaused` coexistence (`P9-LIFECYCLE-013`);
  `Cue::Stop(AsAuthored)`'s authored `fadeOutMS` timing (`P9-STOP-010`); category
  `instanceLimit`/`maxInstanceBehavior`/fade scope (`P9-CATEGORY-005..010`, `d3b66dea` — including
  the QUEUE/REPLACE_OLDEST/REPLACE_QUIETEST collapse, matching FAudio's own shipped behavior);
  cue-level `instanceLimit`/`maxInstanceBehavior`/fade scope (`P9-CATEGORY-011` — including the
  bank-wide-unfiltered victim search quirk, also matching FAudio's own shipped behavior);
  `Apply3D`'s pan-orientation fix scope (`P9-3D-010` — contained to the pan projection only, not
  full X3DAudio multi-speaker diffusion); RPC continuity scope (`P9-XACT-016` — volume/pitch only,
  explicitly not `AttackTime`/`ReleaseTime` or filter/DSP-preset RPC targeting); and which
  follow-up area to continue in next, four times in a row (cue-level `instanceLimit`, then the
  `T-4C` ThreadSanitizer stress test, then `Apply3D` pan orientation, then RPC continuity — the
  last of which exhausted every candidate raised so far, see §8).
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No real 3D HRTF, Doppler-beyond-what's-implemented, or reverb implementation** — SDL3_mixer
  cannot do full HRTF/reverb; keep as documented accepted deviations unless the user explicitly
  asks to revisit the backend choice.
- **No touching the sibling `../sharp-runtime` checkout** without asking — separate repo under
  concurrent development. If a build breaks there, check whether it's the known
  concurrent-development hazard (§4) first.
- **No API renames / namespace moves** — XNA names are frozen.
- **No broad refactors or unrelated cleanup** — every fix on this branch has been a small,
  targeted change plus its own regression test; keep it that way.

---

## 10. Resume prompt

```
Read NEXT.md first. Do not assume anything is complete beyond what NEXT.md §2/§4 state -- Phase 9
is fully closed (all 11 groups plus 4 user-directed follow-ups). Phase 10 (plan_audio.md) exists
and is a large, intentionally-open task list (~90 items, 12 groups); 16 items are closed so far
across three rounds (kickoff + two self-directed verification-sweep rounds, all 2026-07-06) -- read
its Phase 10 section before picking anything; every task there already has a concrete, cited
status, not a placeholder. See NEXT.md §8 for the remaining candidates -- as of this pass, every
one of them is real feature/behavior-decision work or a large mechanical audit, and needs the
user's confirmed scope first; there is no more self-selectable pure-test-addition work left on
the list (§9).

1. If the user names a specific task, inspect only the files needed for it -- do not refactor
   unrelated code. Confirm scope/approach with the user first if it's real feature work rather
   than a one-line fix (every design decision on this branch so far was confirmed before
   implementation, see §9).
2. Make one small, verified improvement at a time: add/extend a test, verify with the git-stash
   pattern (§7) for any behavioral fix, run the relevant build/test command, and run ASan+UBSan if
   it touches memory lifetime or ownership.
3. Update plan_audio.md's checkbox + note for whatever sub-item was completed, then update this
   NEXT.md (status, recent changes, next task) to reflect what changed, and commit.
```
