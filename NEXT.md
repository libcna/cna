# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> Covers the **audio** subsystem work on `feature/audio` only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> Detailed file-by-file task list: **`plan_audio.md`** (repo root).
> **Media namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend. It is a
framework/runtime, not a game.

- **This branch's goal:** port and verify `Microsoft::Xna::Framework::Audio` file-by-file against
  the authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`), matching XNA
  behavior exactly, with full test coverage. No stubs without a documented reason.
- **Current phase:** `plan_audio.md`'s original compliance/bugfix plan (Fáze 0–6) and T-4A (real
  microphone capture) were already done before this update. **Fáze 7** — a fresh line-by-line
  re-audit against FNA (run 2026-07-02) that found 30 concrete bugs/gaps (prefixes
  `CP`/`XA`/`IN`/`MC`) — is **fully complete: 30 of 30 fixed/closed**. The handful of pre-existing
  older items from Fáze 3/4/6 that were never in scope for that audit (`T-3F`, `T-3G`, `T-4B`,
  `T-4C`, `T-4D`, `T-6C`) were also all closed earlier the same day (2026-07-04). **Fáze 8** — a
  second fresh audit, explicitly requested by the user after Fáze 7/the older backlog hit zero,
  run the same day with the same 4-parallel-agent methodology — found **25 new findings**
  (`CP-15`..`CP-23`, `XA-6`..`XA-13`, `IN-7`..`IN-12`, `MC-6`..`MC-7`), continuing the Fáze 7 ID
  sequences. **Fáze 8 is now also fully closed: 25 of 25 resolved** (22 real fixes + 3 items
  explicitly consulted with the user and closed as documented accepted deviations: `CP-19`,
  `CP-18`/`XA-9`). **Fáze 9** — a user-directed "audio correctness hardening" pass, explicitly
  scoped by the user into 11 task groups (`P9-AUDIT`, `P9-LIFECYCLE`, `P9-STOP`, `P9-CATEGORY`,
  `P9-VALIDATION`, `P9-XACT`, `P9-3D`, `P9-HARDWARE`, `P9-DYNAMIC`, `P9-DOCS`, `P9-BUILD`) with an
  explicit implementation order — is **in progress**: all 15 of `P9-LIFECYCLE`'s items are now
  done as of 2026-07-04 (`001..012` closing a real bug — `Cue` state never naturally reconciled
  after playback finished; `013..015` an audit of disposed-state behavior that found and fixed a
  second real bug — `GetVariable()`/`SetVariable()` had no disposed guard at all — see §5/§6).
  Every other Fáze 9 group (`P9-CATEGORY`, `P9-VALIDATION`, `P9-XACT`, `P9-3D`, `P9-HARDWARE`,
  `P9-DYNAMIC`, `P9-DOCS`, `P9-BUILD`, plus `P9-AUDIT` itself) is **still open** — see §4/§5/§8,
  this is not a "no known open item" branch state right now.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is
  parsed by a hand-written `XactParser` and mixed through SDL_mixer. Consequences: 3D HRTF and
  Doppler are stored but never applied — pan + distance-attenuation is now applied at **every**
  level of the API (`SoundEffectInstance::Apply3D` since `CP-3`, and now `Cue::Apply3D`/3D
  `SoundBank::PlayCue` too, `T-4B`). `WaveBank`'s **streaming ctor now does real lazy per-entry
  disk reads** (`T-3F`) — only the non-streaming ctor loads the whole `.xwb` eagerly, matching
  FNA's own eager/lazy split. `SoundEffect` is now **move-only, with real instance-tracking +
  Dispose cascade** (`T-3G`) — `SoundEffect::Dispose()` stops/disposes every live
  `SoundEffectInstance` created via `CreateInstance()`, matching FNA's `SoundEffect.Instances`;
  `CreateInstance()`/`FromStream()` are still value-returning (no heap-reference API shape
  change), but `SoundEffect` can no longer be copied, only moved. `SoundEffectInstance` now has
  **real low/high/band-pass filters** (`T-4C`) via an SDL3_mixer per-track callback running
  FAudio's own state-variable filter algorithm; reverb stays a documented no-op (SDL3_mixer has
  no aux-send/return bus).
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface. It is under **separate,
  active, concurrent development** by another session — see §4.

---

## 2. Current status

- **Build:** clean (uncommitted changes on top of `9435da9`, `HEAD` at last commit). EasyGL
  backend, `SOUND_ENABLED` on, SDL3_mixer linked. Verified immediately before writing this update;
  also rebuilt `cna_demo_sound`/`cna_demo_2d` (the example targets, `CNA_BUILD_EXAMPLES=ON` by
  default) — no failures.
- **Tests:** `CnaTests` **2078 / 2078 pass** (2064 at the last handoff snapshot, +14 net new across
  Fáze 9's `P9-LIFECYCLE-001..015`: 13 new regression tests plus a moved/shared
  `SoundBankTestAccess.hpp`). No known regressions.
  Re-run to check for drift: `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests`.
- **CLI/tools/apps:** none in the framework itself. `cna_demo_sound`/`cna_demo_2d` aren't part of
  `CnaTests` and are easy to forget when just running `--target CnaTests` — see §7 for how to
  rebuild them; both still build clean as of this update.
- **Fáze 9 (started 2026-07-04, user-directed "audio correctness hardening" pass): `P9-LIFECYCLE`
  is now fully closed (15/15), the other 10 task groups untouched.** This is the user's own scoped
  plan (not a repeat of the Fáze 7/8 "fresh full audit against FNA" pattern) — see `plan_audio.md`'s
  new "Phase 9" section for the complete task list. Priority bug fixed first, as explicitly
  directed:
  - **The bug:** `Cue::IsPlaying`/`IsPaused`/`IsStopped` were a cached `state_` enum that only ever
    changed on an explicit `Play()`/`Pause()`/`Resume()`/`Stop()` call — a cue whose sound finished
    playing on its own (no loop, nobody called `Stop()`) stayed reported as `Playing` **forever**.
    This kept `SoundBank::IsInUse`/`WaveBank::IsInUse` stuck `true` too, and kept fire-and-forget
    cues alive in `SoundBank::fireAndForget_` for the full 5-minute safety-net timeout instead of
    being swept as soon as they actually finished.
  - **The fix:** new `Cue::ReconcileState() const` (queries each `active_` instance's live
    `SoundEffectInstance::getStateProperty()`, same as `SoundEffectInstance` already does against
    `MIX_TrackPlaying` — CNA's live-per-call-query pattern, no background thread needed) — called
    from every `Is*Property()` getter that can observe `Playing`, plus `Play()`/`Pause()`. Also
    found (same FACT audit) that `Cue::Play()` didn't reject being called twice on an
    already-playing cue — real FACT does (`FACTCue_Play` returns `FACTENGINE_E_INVALIDUSAGE` for
    `PLAYING|STOPPING|STOPPED`, silently discarded by FNA's `Cue.Play()`), so `Cue::Play()` now
    matches. `AudioEngine::Update()` now also sweeps every registered `SoundBank`'s
    fire-and-forget list (mirrors FNA's `Update()` → `FACTAudioEngine_DoWork`, which is what
    destroys a `managed` cue once `FACT_STATE_STOPPED` — see `FACT_internal.c` line ~1732), instead
    of only sweeping lazily on that bank's next `PlayCue()` call.
  - **`P9-LIFECYCLE-013..015` (disposed-state audit):** read `FACTCue_Pause`/`FACTCue_Stop`/
    `FACTCue_GetVariableIndex` (`FACT.c`) line-by-line against `Cue::Pause()`/`Resume()`/
    `StopInternal()`/`GetVariable()`/`SetVariable()`. `Pause()`/`Resume()`/`Stop()` already matched
    FNA exactly for Disposed/Stopped/Playing/Paused (silent no-op on a disposed cue, matching
    FACT's own `pCue == NULL` check) — no fix needed, just 3 new tests locking that in.
    `GetVariable()`/`SetVariable()` had a real bug: **no disposed guard at all**, unlike `Play()`/
    `Apply3D()` in the same class — working only because a disposed cue's `bank_`/`engine_` raw
    pointers happened to still be valid, not because of any real contract. (Checked what real FNA
    does: `FACTCue_GetVariableIndex` dereferences `pCue->parentBank` **before** its null check, so
    calling this on a disposed FNA `Cue` would crash natively — an unintentional FAudio bug, not
    something worth reproducing.) Fixed: both now throw `ObjectDisposedException` first, matching
    `Play()`/`Apply3D()`'s own precedent in this class.
  - **Found but deliberately not fixed (`P9-LIFECYCLE-013`, documented, not implemented):** real
    FACT lets `IsPlaying` and `IsPaused` be `true` simultaneously (pausing never clears the
    `PLAYING` bit) — CNA's `Cue::State` enum is mutually exclusive by construction. Fixing this
    would ripple into `AudioEngine::PauseCategoryInternal`/`ResumeCategoryInternal` and a number of
    already-passing tests that assume disjoint `IsPlaying`/`IsPaused` — flagged for its own future
    pass, not fixed as a drive-by here.
  - 13 new tests total (natural Playing→Stopped transition, `SoundBank`/`WaveBank` `IsInUse` after
    natural completion, `AudioEngine::Update()` sweep, `Play()`/`Pause()` no-op guards,
    duplicate-registry regression, disposed-state `GetVariable`/`SetVariable`/`Pause`/`Resume`/
    `Stop`) all verified via `git stash` to fail against the pre-fix code where a real bug existed
    (the duplicate-registry test and the 3 Pause/Resume/Stop-after-Dispose tests are regression
    guards locking in already-correct behavior, not bug reproductions — see `plan_audio.md`'s
    `P9-LIFECYCLE-012`/`014` notes for why). Full detail with FNA/FAudio line citations:
    `plan_audio.md`'s new "Phase 9" section.
- **Fáze 8 audit (found 2026-07-04, closed the same day): 25/25 resolved.** After the pre-existing
  backlog (below) hit zero, the user asked for a fresh second audit, run with the same
  4-parallel-agent methodology as Fáze 7. All 25 findings (`CP-15`..`CP-23`, `XA-6`..`XA-13`,
  `IN-7`..`IN-12`, `MC-6`..`MC-7`) are now closed — 22 as real fixes, 3 as explicitly
  user-consulted, documented accepted deviations (`CP-19` stereo-pan crossfeed; `CP-18`/`XA-9`
  silent-stub construction, closed together). Highlights, worst first:
  - **`IN-7`** — `.xwb` entry `nChannels` was read with a spurious `+1` on **every** entry (compact
    and non-compact) — mono played as stereo and vice versa for real content, not just corrupt
    data. Fixed: `+1` removed on both sites.
  - **`IN-8`** — COMPLEX sounds with an RPC or DSP flag read per-track metadata **before** the
    RPC/DSP block instead of after — corrupted the parse of the rest of the file. Fixed: reordered
    to trackCount → RPC skip → DSP skip → per-track metadata, matching FACT exactly.
  - **`XA-6`** — `Cue::Stop(AsAuthored)` behaved identically to `Stop(Immediate)` (`active_.clear()`
    always ran right after `Stop(false)`, hard-stopping regardless). Fixed: `active_.clear()` now
    only runs for an immediate stop; a non-immediate one leaves the still-releasing instance owned
    until the `Cue` is later disposed.
  - **`XA-7`** — the fire-and-forget sweep and both `IsInUse` properties only checked `IsPlaying`,
    so pausing a fire-and-forget cue then calling `PlayCue()` again on the same bank silently
    destroyed it. Fixed: all three now treat `IsPlaying || IsPaused` as "still alive".
  - **`CP-15`** — `DynamicSoundEffectInstance::Pause()`/`Resume()` were dead code (inherited base
    methods touched `track_`, but the dynamic subclass only ever uses `dynamicTrack_`). Fixed:
    `Pause()`/`Resume()` are now `virtual`, with a `DynamicSoundEffectInstance` override.
  - **`CP-16`** — `SoundEffect::MasterVolume` never affected already-playing sounds, only future
    `Play()` calls. Fixed: the getter/setter now read/write SDL3_mixer's real live master gain
    (`MIX_GetMixerGain`/`MIX_SetMixerGain`) instead of a static field baked into per-track gain.
  - Also real fixes: `CP-17`/`CP-23` (loop region now actually applied + `FromStream` `smpl`-chunk
    parsing), `CP-20` (`is3D` latch stops `setPanProperty` clobbering `Apply3D`), `CP-22`
    (`SoundEffect` move-ctor/assignment tests), `XA-8` (`AudioEngine::Dispose()` now cascades to
    every `WaveBank`/`SoundBank`/`Cue` it created), `XA-10`/`CP-21` (stale `AudioCategory` doc),
    `XA-11` (CHECKLIST.md gap), `XA-12` (`ContentVersion` type alias), `XA-13` (corrupt-file
    constructor tests), `IN-9` (streaming oversized-length guard), `IN-10` (compact ADPCM
    samplesPerBlock/blockAlign), `IN-11` (`MIX_Init` refcount leak), `IN-12` (test gaps), `MC-6`
    (`CheckBuffer()` made private), `MC-7` (deterministic negative-case test).
  - Documented, not implemented (both consulted with the user first): `CP-19` (stereo hard-pan
    eliminates a channel instead of crossfeed-blending — SDL3_mixer has no crossfeed API, and a
    real fix would collide with T-4C's already-shipped DSP filter callback); `CP-18`/`XA-9`
    (`AudioEngine`/`SoundBank`/`WaveBank` constructors stay silently in a stub state on a missing/
    corrupt file rather than throwing, and `AudioEngine` never throws `NoAudioHardwareException` —
    fixing this would require rewriting the `SharedEngine()` test helper in 4 files, touching the
    shared foundation ~80+ existing tests build on).
  - Full list with FNA/CNA/FAudio line citations, accept criteria, and a `*Pozn.:*` closure note
    on every item: `plan_audio.md` §4 "Fáze 8".
  - 13 commits, one (or a small tightly-related group) per finding; verified via the established
    `git stash` methodology (a few surfaced as genuine compile failures across shared test-access
    headers) plus targeted ASan+LeakSanitizer runs for anything touching object lifetime.
- **Prior work this branch: closed the entire remaining pre-Fáze-7 backlog** — `T-4D`, `T-3F`,
  `T-3G`, `T-4B`, `T-6C`, and `T-4C` — see §3 for detail on each.
  - `T-4D`: `AudioCategory::SetVolume` now re-applies to already-playing cues. This was the one
    task in the old §8 backlog that was a mechanical fix rather than an open design decision.
  - `T-3F`: asked the user how to close it (implement real streaming vs. document the deviation);
    the user chose **implement real streaming**, so `WaveBank`'s streaming ctor now does lazy
    per-entry disk reads instead of eagerly loading the whole file like the non-streaming ctor did.
  - `T-3G`: asked the user the same way; the user again chose **implement** (instance-tracking +
    Dispose cascade) over documenting the value-semantics deviation. This one had real teeth:
    making it correct required also making `SoundEffect` move-only and fixing a collateral
    `ContentManager::Load<T>()` compile break — see §3.
  - `T-4B`: unlike the "implement vs. document" tasks, `plan_audio.md` framed this as concrete
    accept criteria, so it was picked up directly without asking first — `Cue::Apply3D`/3D
    `PlayCue` now forward to `SoundEffectInstance::Apply3D` (already working since `CP-3`),
    instead of being no-ops.
  - `T-6C`: the formal build & report checkpoint itself — both targets were already green with
    nothing to rebuild; recorded the short report in `plan_audio.md`.
  - `T-4C`: asked the user how to close it, same as `T-3F`/`T-3G`; the user chose **implement
    real filters** (reverb stays a no-op, since SDL3_mixer genuinely has no aux-send/return bus
    equivalent). This one had the same "no caller exists, even in FNA" surprise as `T-4B`, plus a
    real cross-thread-safety design question (SDL3_mixer's per-track callback runs on the mixing
    thread) that none of the earlier tasks needed — see §3.
  - **No known open backlog item remains** for this branch (see §4/§5/§8).
- **Prior session's work (26 commits, all 30 Fáze 7 findings closed):**
  - **Bug fixes (13):** `IN-2`/`IN-3` (XWB parser over-read + integer-underflow bounds-check
    bypass), `MC-1` (Microphone sample-duration math), `CP-8`/`CP-9` (`SoundEffect` API
    compliance: `System::Object` inheritance, private internal ctor), `XA-3` (weighted variation
    selection), `IN-4` (reject unsupported XACT variation-table types instead of misparsing),
    `CP-2` (`Play()` pan/pitch validation), `CP-5` (`DynamicSoundEffectInstance::Stop(false)` now
    throws), `CP-6` (sample-size math ignoring float submission mode), `CP-4`
    (`PendingBufferCount` now tracks real stream consumption, not submission), `CP-7` (dangling
    `SoundEffect*` in `SoundEffectInstance` — **confirmed via a real ASan build**, see §7), `MC-3`
    (`Microphone::GetData` no longer zero-fills on a no-op read).
  - **Test-coverage additions (7):** `CP-10`, `CP-11`, `CP-12`, `CP-13`, `XA-5`, `IN-6`
    (`XactParserTests.cpp` 8→22 tests), `MC-4`, `MC-5`.
  - **Docs/cleanup (3):** `XA-4`, `IN-5`, `MC-2`; `CP-14` closed as already-satisfied by `CP-1`'s
    own regression test.
  - Every behavioral fix was verified with the established `git stash` methodology: stash the
    fix, rebuild, confirm the new regression test actually fails against the pre-fix code (several
    surfaced as genuine compile failures or ASan-detected memory errors, not just assertion
    failures), then restore and confirm green.
  - `plan_audio.md` §4 Fáze 7, `AUDIT.md`, and `CHECKLIST.md` are all updated; open decisions
    `D5`/`D7`/`D8` are resolved and documented in `plan_audio.md` §6.

---

## 3. Recent changes (this branch, newest first)

- *(uncommitted)* — **`P9-LIFECYCLE-013..015`**: disposed-state audit for `Cue::Pause()`/
  `Resume()`/`Stop()`/`GetVariable()`/`SetVariable()` against `FACT.c`. `Pause`/`Resume`/`Stop`
  already matched FNA (silent no-op when disposed) — locked in with 3 new tests. `GetVariable`/
  `SetVariable` had no disposed guard at all (unlike `Play()`/`Apply3D()` in the same class) — now
  throw `ObjectDisposedException` first, matching that same-class precedent instead of the native
  crash real FNA/FAudio would hit here (`FACTCue_GetVariableIndex` dereferences `pCue->parentBank`
  before its null check). 5 new tests total, `git stash`-verified (the 2 throw-tests fail against
  pre-fix code; the 3 no-op-lock tests pass either way, confirming they're not bug reproductions).
  `P9-LIFECYCLE` group is now fully closed (15/15). See §2/§5/`plan_audio.md`'s Phase 9 section.
- `9435da9`/`eb7149e`/`5f3e5d0` — **Fáze 9 kickoff + `P9-LIFECYCLE-001..012`**: added
  `plan_audio.md`'s new "Phase 9" section (11 task groups, user-specified verbatim) and closed 12
  of `P9-LIFECYCLE`'s 15 items — see §2 for the full summary. Files touched: `Cue.hpp`/`.cpp`
  (`ReconcileState()`, `Play()`/`Pause()` guards), `SoundBank.hpp`/`.cpp` (`SweepFireAndForget()`
  factored out of `PlayCueInternal`, new `AudioEngine` friend), `AudioEngine.hpp`/`.cpp`
  (`Update()` now sweeps every registered `SoundBank`, `RegisterCue()` dedup guard, new test-only
  `ActiveCueCountForTest()`), plus `CueTests.cpp`/`SoundBankTests.cpp`/`WaveBankTests.cpp`/
  `AudioEngineTests.cpp` (8 new tests) and two new shared test-access headers
  (`SoundBankTestAccess.hpp` — extracted from `SoundBankTests.cpp`'s inline struct so
  `AudioEngineTests.cpp` could reuse it; `AudioEngineTestAccess.hpp` — new). Verified via `git
  stash` (all 8 tests fail against pre-fix code, except the P9-LIFECYCLE-012 regression guard —
  see its `plan_audio.md` note) plus a full ASan+UBSan+LeakSanitizer run of the whole suite.
- `b6557a1`..`91cd93c` (26 commits) — **Fáze 8 audit + full closure**: `plan_audio.md` §4 gained a
  new "Fáze 8" section (25 items, `CP-15`..`CP-23`/`XA-6`..`XA-13`/`IN-7`..`IN-12`/`MC-6`..`MC-7`),
  all 25 then closed (22 real fixes, 3 documented deviations). See §2's prior-session summary
  (now superseded by the Fáze 9 summary above) and `plan_audio.md` §4 "Fáze 8" for full detail.
- `1b3b188` — **T-4C**: `SoundEffectInstance` had no `INTERNAL_applyReverb`/`applyLowPassFilter`/
  `applyHighPassFilter`/`applyBandPassFilter` at all. Checked FNA first, same surprise as `T-4B`:
  **none of these have any caller even in FNA's own source** -- FACT applies XACT RPC/filter
  routing natively, invisible to the C# layer -- so "callers from Cue/AudioEngine line up" from
  the task description doesn't correspond to a real caller in either codebase.
  Filters are implemented for real: SDL3_mixer's `MIX_SetTrackCookedCallback` gives per-track
  access to raw float PCM after gain/pan/3D, right before mixing -- close enough to FAudio's
  per-voice filter to host FAudio's own state-variable filter algorithm
  (`FAudio_internal.c`'s `FAudio_INTERNAL_FilterVoice`, the Chamberlin SVF), copied exactly
  rather than approximated. Reverb stays a documented no-op: SDL3_mixer has no aux-send/return
  bus, and building one would be disproportionate scope. Two design details made this correct,
  not just plausible:
  - Filter state (`FilterState`) is a heap-allocated, namespace-scope (not nested, to dodge a
    friend-access dead end) type owned via `unique_ptr`. Moving a `SoundEffectInstance` moves the
    `unique_ptr`, not the object it points to, so the SDL3_mixer callback's `userdata` pointer
    stays valid across a move with **no re-registration needed** -- the same class of bug as
    `T-3G`'s instance-tracking repoint, avoided here by construction instead of by explicit
    re-pointing.
  - Coefficients (`kind`/`frequency`/`oneOverQ`) are written under `MIX_LockMixer`/`UnlockMixer`
    in the setters; the callback itself does **not** lock, relying on SDL3_mixer's documented
    guarantee that its mixing thread already holds that same lock while invoking track callbacks.
    The recursive filter state (`yl`/`yb`) is touched only by the callback, so needs no
    synchronization at all.
  Added 9 tests: no-op-before-`Play()`, exact single-sample transient values (computable in
  closed form from a fresh zero state: `Yl(1)=0`, `Yh(1)=x`, `Yb(1)=F*x`), DC-signal convergence
  (unity gain for low-pass, zero for high-pass), and survival across a move. Tests drive the
  filter math directly and synchronously via a dedicated test-only entry point
  (`ProcessFilterSamplesForTest`) that exercises the identical production code path, bypassing
  SDL3_mixer's actual async callback dispatch (which would otherwise make them either slow or
  flaky). **Known test-coverage gap:** the concurrent (setter-thread vs. mixing-thread) path
  itself is not stress-tested -- no ThreadSanitizer run, no real non-dummy audio device in this
  environment. The locking follows SDL3_mixer's own documented practice but wasn't empirically
  verified under real concurrency. Verified via `git stash` (reverted feature fails to compile)
  and a full ASan+LeakSanitizer run.
- `1244430` — **T-6C**: formal build & report checkpoint. `cmake --build --target CNA` and
  `--target CnaTests` both green with nothing to rebuild (already current from the prior four
  commits); `CnaTests` 2031/2031 under `SDL_AUDIODRIVER=dummy`. Recorded the short report
  `CLAUDE.md` §Build and Report asks for directly in `plan_audio.md`'s T-6C entry: changed files
  this session, no new stubs/missing dependencies, the accepted-deviations list (already in
  `CHECKLIST.md`), and `T-4C` as the one remaining real gap. Pure docs/checkpoint — no code change.
- `feb6eda` — **T-4B**: `Cue::Apply3D` was a pure no-op (only checked `isDisposed_`); it now
  iterates `Cue::active_` and calls `SoundEffectInstance::Apply3D` (already working since `CP-3`)
  on each live instance — no new pan/attenuation math, just wiring to the mechanism `CP-3` already
  built. `SoundBank::PlayCue(name, listener, emitter)` discarded both parameters and just called
  the 2-arg overload; both overloads are refactored onto a shared private `PlayCueInternal(name,
  listener*, emitter*)`, and the 3D one now calls `cue->Apply3D(...)` right after `cue->Play()`,
  before storing the cue in `fireAndForget_` (matches FNA, where `FACT3DCalculate` runs before
  `FACTSoundBank_Play3D` — a synchronous same-thread call here, so no observable ordering
  difference). Doppler stays unapplied (existing accepted deviation); the `ObjectDisposedException`
  half of the original task description was already done, nothing left to change there. Needed a
  real WaveBank-backed fixture to test for real: none of the existing Cue/SoundBank fixtures
  (`MakeCue()`, `SharedWeightedVariationBank()`, `SoundBankTests.cpp`'s "Explosion") have a
  wavebank, so `Cue::active_` stays empty and there's nothing for `Apply3D` to reach. Added such a
  fixture to both `CueTests.cpp` and `SoundBankTests.cpp`, extracted `SoundEffectInstanceTestAccess`
  into a shared header (previously private to `SoundEffectInstanceTests.cpp`, same move
  `CueTestAccess` got for `T-4D`), and added `CueTestAccess::ActiveInstance()`/
  `SoundBankTestAccess::LastFireAndForgetCue()` so both new tests can read back the real
  `MIX_GetTrackGain()` and confirm it changes with distance (SDL3_mixer has no stereo-pan getter,
  so only attenuation is directly verifiable — the same limitation `CP-3`'s own `Apply3D` coverage
  already has). Verified via `git stash`: both new tests fail with `farGain == nearGain` against
  the pre-fix no-op code (not a compile break this time, since the test scaffolding itself doesn't
  depend on the fix). Also verified clean under ASan+LeakSanitizer.
- `71b7a45` — **T-3G**: `SoundEffect::Impl` gained a non-owning `std::vector<SoundEffectInstance*>
  instances`; `SoundEffectInstance` registers itself in its ctor (`SoundEffect::RegisterInstance`)
  and unregisters (plus drops its own `soundEffectKeepAlive_`) in `Dispose()`
  (`UnregisterInstance`), so the underlying `MIX_Audio` can free as soon as the `SoundEffect` and
  every instance are disposed — closer to FNA's eager release than before. `SoundEffect::Dispose()`
  now iterates a snapshot of tracked instances and disposes each one before `impl_.reset()`,
  matching FNA's `Instances.ToArray()` + foreach cascade. Two changes were required to make this
  correct, not just correct-looking:
  - **`SoundEffect` is now move-only** (copy ctor/assignment `= delete`) — a single owner per
    resource is what makes "whose `Dispose()` is authoritative" unambiguous. An Explore agent
    confirmed no call site in `src/`/`tests/`/`examples/` relies on `SoundEffect` copy semantics
    *except* `ContentManager::Load<T>()`'s generic `std::any` cache (requires `CopyConstructible`)
    — fixed with an explicit `Load<Audio::SoundEffect>` specialization that skips caching
    entirely (`ContentManager.hpp`/`.cpp`): sharing one cached instance across unrelated
    `Load<SoundEffect>()` call sites would let disposing one caller's copy silently cascade-stop
    another caller's still-playing instances, so removing the cache for this type isn't just a
    workaround for move-only-ness, it's the semantically correct outcome. Rebuilt
    `cna_demo_sound`/`cna_demo_2d` (the only call sites) to confirm the fix compiles.
  - **`SoundEffectInstance`'s move ctor/assignment now re-point cascade tracking** (unregister the
    old address, register the new one) — without this, moving an instance to a new address (e.g.
    `dst = std::move(src)` with `src` going out of scope) leaves `SoundEffect::Dispose()`'s cascade
    targeting a stale address. Confirmed this is a real, serious bug, not just theoretical:
    temporarily disabling just the repoint logic (keeping the rest of the feature intact) made the
    new regression test **segfault**, not merely fail an assertion — stronger evidence than the
    usual `git stash` check.
  Added 5 tests to `SoundEffectTests.cpp` (single/multiple-instance cascade, already-disposed
  instance skipped safely, moved-to instance via both move ctor and move-assignment) plus 4
  `static_assert`s locking in move-only-ness. Verified via `git stash` (the whole feature reverted
  makes the new tests fail to *compile*, since `RegisterInstance` etc. don't exist) and a full
  ASan+LeakSanitizer run (not just the new tests).
- `eefda45` — **T-3F**: implemented real streaming for `WaveBank`'s streaming ctor instead of the
  old "delegates to the same eager Init() as non-streaming" behavior. New
  `ParseXwbStreamingHeader(path)` (`XactParser.cpp`) reads only segments 0-3 (bank data, entry
  metadata, seek tables, entry names) from disk; segment 4 (wave data, by far the largest part of
  a real `.xwb`) is never loaded upfront. `XwbData` gained `streaming`/`sourcePath` fields.
  `WaveBank::GetSoundEffect()` now does a lazy per-entry disk read (seek to `entry.dataOffset`,
  read `entry.dataLength`) when the bank is in streaming mode, instead of slicing a fully-resident
  buffer. The non-streaming ctor is unchanged (still eager, matching FNA). `offset`/`packetSize`
  stay unused: confirmed by reading FNA's own `WaveBank.cs` that it never forwards them to
  `FACTStreamingParameters` either (only `.file` is set) — so leaving them dead matches the
  reference exactly rather than inventing semantics FNA itself doesn't have. Added
  `WaveBankTestAccess` (`StreamingInternal`/`ResidentFileBytesInternal` plus friend access to the
  private `GetSoundEffect`) and three tests: the non-streaming ctor still loads the whole file;
  the streaming ctor's resident memory excludes the wave-data segment; and a new two-entry fixture
  (`BuildMultiEntryXwbFixtureBytes`, different offset/length per entry) proves the lazy read lands
  on the correct byte range, not just "a" range of the right length — a single-entry fixture can't
  catch an offset bug since entry 0 always starts at offset 0 either way. Verified clean under
  ASan+LeakSanitizer, and via `git stash` (the test scaffolding doesn't even compile against the
  old code, since `StreamingInternal` etc. don't exist there — a genuine compile failure, not just
  a failing assert).
- `d468dc4` — **T-4D**: `AudioEngine::SetCategoryVolumeInternal`'s no-op loop body now calls a new
  `Cue::ApplyCategoryVolume(catVol)` for every active cue in the category. `Cue::PlaybackInstance`
  gained a `baseVolume` field (the wave's own volume, already combined with sound/track volume at
  parse time in `XactParser`); `ApplyCategoryVolume` recombines it with the new category volume
  using the same `clamp(base*cat, 0, 1)` formula `Play()` already used, so re-applying volume and
  the original apply-at-Play()-time path can never drift apart. Added
  `AudioCategoryTest.SetVolumeReappliesToAlreadyPlayingCueInstance` — this needed a *new* fixture
  with a real `WaveBank` (unlike the existing `PauseResumeStopRouteToRealActiveCueInCategory`
  fixture, whose cue has no wavebank, so `Cue::active_` stays empty and there's no instance to
  observe a volume change on). Extracted `CueTestAccess` (previously private to `CueTests.cpp`)
  into a shared `tests/.../Audio/CueTestAccess.hpp` so `AudioCategoryTests.cpp` could reuse it,
  and added `ActiveInstanceVolumes()` to it. Verified with the branch's `git stash` methodology:
  stashing `Cue.hpp`/`Cue.cpp`/`AudioEngine.cpp` made the new test fail (`1 == 1`, i.e. genuinely
  no change), confirming the test isn't a tautology.

All 26 commits below are the *prior* session's Fáze 7 closure work, `fed07f9`..`678258e`:

- `678258e` — **MC-4**: added `BufferReadyFiresWhenQueuedDataExceedsBufferDuration` (real capture,
  polls `CheckBuffer()` until `BufferReady` fires); fixed a latent test-infra issue
  (`MicrophoneCaptureTest::TearDown()` now clears `BufferReady` so a test-local lambda can't
  dangle into a later test sharing the same singleton mic).
- `1d98fd4` — **MC-3**: `Microphone::GetData` no longer zero-fills the buffer on a no-op read;
  returns 0 and leaves the caller's data untouched, matching FNA (resolves `D8`).
- `1d70f44` — **IN-6**: expanded `XactParserTests.cpp` from 8 to 22 tests (truncated/bad-magic for
  all 3 formats, non-compact `.xwb` standard 24-byte layout, ADPCM entry, `SOUND_FLAG_HAS_RPC`,
  all 4 variation-table types, RAMP-form PITCH event).
- `1fb919c` — **XA-5**: added a real `SoundBank`+`Cue` fixture so `AudioCategory`'s
  `Pause`/`Resume`/`Stop` are verified against an actual playing cue's state, not just
  `EXPECT_NO_THROW`. Discovered (but did not fix, out of scope) that `SetVolume` never actually
  re-applies to active cues — see `T-4D` in §5.
- `40b800a` — **CP-7**: `SoundEffectInstance` no longer holds a raw `const SoundEffect*`; replaced
  with a type-erased `shared_ptr<void>` keep-alive (capturing `SoundEffect::impl_`) plus a cached
  native handle. Confirmed the original bug with a real ASan build (`stack-use-after-scope` in
  `Play()`), confirmed the fix clean under ASan+LeakSanitizer.
- `49b5304` — **CP-4**: `PendingBufferCount` now tracks bytes actually consumed by the stream
  (`SDL_GetAudioStreamQueued`), not bytes merely submitted — `BufferNeeded` no longer over-fires.
- `9a44828` — **CP-6**: `DynamicSoundEffectInstance`'s sample-duration/size math now matches FNA's
  hardcoded 16-bit assumption instead of using the real (possibly 32-bit float) sample size.
- `b563e37` — **CP-2**: `SoundEffect::Play(volume, pitch, pan)` now validates/clamps pan/pitch
  exactly like `SoundEffectInstance`'s property setters.
- `f6f6c9e` — **CP-13**: added the missing `Stop(false)` test for the static `SoundEffectInstance`.
- `e18401a` — **CP-5**: `DynamicSoundEffectInstance::Stop(false)` now throws
  `InvalidOperationException` once playback has started (matches FNA); safe no-op before any
  `Play()`.
- `d5f0b14` — **CP-14**: closed as already-satisfied by `CP-1`'s own regression test.
- `101b2f8` — **MC-5**: added a genuinely negative-`count` test for `Microphone::GetData`.
- `31816bf` — **MC-2**: removed dead `friend class MicrophoneFactory` + rewrote a stale comment.
- `6086900` — **IN-5**: converted all of `XactTypes.hpp` to `/** @brief */` Doxygen blocks.
- `88c9f94` — **XA-4**: documented `AudioEngine`'s unused `lookAheadTime`/`rendererId` as an
  intentional deviation (single-backend project); strengthened the existing test.
- `615b74d` — **CP-12**: added move-ctor/move-assignment tests for `SoundEffectInstance`.
- `ef021d2` — **CP-11**: added a real successful-decode test for `SoundEffect::FromStream`.
- `e8702dd` — **CP-10**: added tests for `SoundEffect`'s file-path-loading constructor.
- `53c5dc0` — **IN-4**: variation-table type 3 (INTERACTIVE) gets its own explicit branch; any
  other unknown type now throws instead of being silently misparsed with the wrong byte layout.
- `8c7c467` — **CP-9**: `SoundEffectInstance(const SoundEffect&)` is now `private` (matches FNA's
  `internal`).
- `19c326f` — **XA-3**: `Cue::Play` now selects variation entries via a weighted lottery over
  `weightMin`/`weightMax`, matching FAudio's own algorithm.
- `84d9463` — **CP-8**: `SoundEffect` now inherits `System::Object` and has `GetTypeName()`.
- `4e7b32e` — **MC-1**: `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` now delegate to
  `SoundEffect`'s versions (matches FNA's rounding).
- `1037305` — **IN-3**: compact-XWB `dataLength` underflow now throws instead of wrapping to a
  huge `uint32_t` value.
- `fed07f9` — **IN-2**: non-compact XWB entry parsing no longer reads foreign-entry bytes when
  `entryMetaDataSize < 24`.

Full detail for any of the above: `git show <hash>`, or the matching task ID in `plan_audio.md`
§4 Fáze 7 (each entry has a `*Pozn.:*` note with the exact verification method used).

Prior to this session (see `git log` for full history): `b9aabdd` (previous `NEXT.md` snapshot),
`0c2fa58`/`b8f1a1f`/`6280c51`/`800d1e9` (`XA-1`/`XA-2`/`CP-3`/`CP-1`, the first 4 of the 30 Fáze 7
findings, fixed in an earlier session).

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker.** No failing command, no failing test. The build is clean
and all 2078 tests pass. `cna_demo_sound`/`cna_demo_2d` also rebuilt clean. Changes are
**uncommitted** on top of `9435da9` (`HEAD`) — see §8 for the suggested commit boundary.

**Fáze 7, the pre-existing older backlog, and Fáze 8 are fully closed. `P9-LIFECYCLE` (all 15
items) is now also fully closed. Fáze 9 as a whole (user-directed hardening pass, started
2026-07-04) is still NOT fully closed** — every other one of Fáze 9's 10 task groups
(`P9-STOP`/`P9-CATEGORY`/`P9-VALIDATION`/`P9-XACT`/`P9-3D`/`P9-HARDWARE`/`P9-DYNAMIC`/`P9-DOCS`/
`P9-BUILD`, plus `P9-AUDIT` itself) are still open. This is real, tracked open work — not a
"no known open item" state like the prior handoff. See `plan_audio.md`'s "Phase 9" section for
the full unchecked list, and §8 below for the user-specified implementation order to continue in.

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, which is under separate, active, concurrent development by another session.
Earlier this session (see prior `NEXT.md` history), a fresh rebuild briefly failed because of an
in-progress interface change there (`IAsyncResult` gaining new pure-virtual methods). If a future
build fails inside `SHARP_RUNTIME/CMakeFiles/...` or in an unrelated non-Audio file, check
`git status`/`git log -1` in `../sharp-runtime` before assuming the audio code broke something —
it may just need a retry once that unrelated work lands, or a small compliance patch.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Fixed 2026-07-04** | `Cue::IsPlaying`/`IsPaused`/`IsStopped` never reconciled after a sound finished playing naturally (no loop, no explicit `Stop()`) — stayed `Playing` forever, keeping `SoundBank`/`WaveBank` `IsInUse` stuck `true` and fire-and-forget cues alive for the full 5-minute safety net | `P9-LIFECYCLE-001..009` |
| **Fixed 2026-07-04** | `Cue::Play()` didn't reject being called again on an already Playing/Paused/Stopped cue — could spawn duplicate overlapping `SoundEffectInstance`s and duplicate `AudioEngine` registry entries | `P9-LIFECYCLE-010`, `011` |
| **Fixed 2026-07-04** | `Cue::GetVariable()`/`SetVariable()` had no disposed guard at all (unlike `Play()`/`Apply3D()` in the same class) — now throw `ObjectDisposedException` first | `P9-LIFECYCLE-015` |
| **Open (Fáze 9)** | Real FACT/FNA lets `IsPlaying` and `IsPaused` be `true` simultaneously (pausing never clears the `PLAYING` bit); CNA's `Cue::State` enum keeps them mutually exclusive — found during the audit above, not yet fixed (would ripple into `AudioEngine::PauseCategoryInternal`/`ResumeCategoryInternal` and existing tests) | `P9-LIFECYCLE-013` |
| **Fixed 2026-07-04** | `.xwb` entry `nChannels` no longer read with a spurious `+1` — matches raw on-disk value on every entry | `IN-7` |
| **Fixed 2026-07-04** | COMPLEX sound + RPC/DSP flag: per-track metadata now parsed after the RPC/DSP block, matching FACT's real order | `IN-8` |
| **Fixed 2026-07-04** | `Cue::Stop(AsAuthored)` now leaves the track playing its release/tail instead of hard-stopping like `Stop(Immediate)` | `XA-6` |
| **Fixed 2026-07-04** | Fire-and-forget sweep and both `IsInUse` properties now treat `IsPlaying \|\| IsPaused` as alive, not `IsPlaying` alone | `XA-7` |
| **Fixed 2026-07-04** | `DynamicSoundEffectInstance::Pause()`/`Resume()` are now `virtual` with a real override on `dynamicTrack_` | `CP-15` |
| **Fixed 2026-07-04** | `SoundEffect::MasterVolume` now reads/writes SDL3_mixer's live master gain, so it affects already-playing sounds too | `CP-16` |
| **Fixed 2026-07-04** | `SoundEffect`'s loop region (`loopStart`/`loopLength`) is now actually applied at `Play()`, and `FromStream` parses the WAV `smpl` chunk | `CP-17`, `CP-23` |
| **Fixed 2026-07-04** | `setPanProperty()` no longer clobbers `Apply3D`'s pan once `Apply3D` has run (new `is3D_` latch, matches FNA) | `CP-20` |
| **Fixed 2026-07-04** | `AudioEngine::Dispose()` now cascades `IsDisposed` to every `WaveBank`/`SoundBank`/`Cue` it created (new `SoundBank` registry) | `XA-8` |
| **Fixed 2026-07-04** | Stale `AudioCategory` Doxygen (claimed `SetVolume` doesn't affect playing cues) rewritten to match real, correct behavior | `XA-10`, `CP-21` |
| **Fixed 2026-07-04** | `AudioEngine::ContentVersion` now uses `SharpRuntime::intcs` instead of a raw `int` | `XA-12` |
| **Fixed 2026-07-04** | Streaming `WaveBank::GetSoundEffect` now bounds-checks a corrupt/oversized `dataLength` against the real file size before allocating | `IN-9` |
| **Fixed 2026-07-04** | Compact-format `.xwb` ADPCM entries now derive `samplesPerBlock`/`blockAlign` (previously only the non-compact path did) | `IN-10` |
| **Fixed 2026-07-04** | `AudioMixer::GetMixer()` no longer leaks a `MIX_Init()` refcount when `MIX_CreateMixerDevice` fails | `IN-11` |
| **Fixed 2026-07-04** | `Microphone::CheckBuffer()` is now `private` (was public `NOXNA`, against T-1H's own accept criterion) | `MC-6` |
| **Fixed 2026-07-04** | Test-coverage gaps closed: `CP-22` (`SoundEffect` move tests), `IN-12` (channel/RPC-DSP/ADPCM/streaming fixtures), `XA-11` (CHECKLIST.md fade/instanceLimit gap), `XA-13` (corrupt-but-present file tests), `MC-7` (deterministic `BufferReady` negative case) | `plan_audio.md` §4 "Fáze 8" |
| **Accepted deviation** | Hard-panning a **stereo** source eliminates the opposite channel instead of crossfeed-blending it (mono is bit-exact vs FNA) | `CHECKLIST.md`, `CP-19` |
| **Accepted deviation** | `AudioEngine`/`SoundBank`/`WaveBank` constructors stay in a silent "stub" state on a missing/corrupt file instead of throwing; `AudioEngine` never throws `NoAudioHardwareException` | `CHECKLIST.md`, `CP-18`, `XA-9` |
| **Accepted deviation** | A bounded loop region truncates the *entire* track (including the first, pre-loop playthrough), not just later iterations | `CHECKLIST.md`, `CP-17` |
| **Accepted deviation** | XACT category `instanceLimit`/`fadeInMS`/`fadeOutMS` are parsed but never enforced/applied | `CHECKLIST.md`, `XA-11` |
| **Fixed 2026-07-04** | `AudioCategory::SetVolume` now retroactively re-applies to already-playing cues via `Cue::ApplyCategoryVolume` | `T-4D` |
| **Fixed 2026-07-04** | `WaveBank`'s streaming ctor now does real lazy per-entry disk reads instead of eagerly loading the whole file like the non-streaming ctor | `T-3F` |
| **Fixed 2026-07-04** | `SoundEffect` now has real instance-tracking + Dispose cascade (matches FNA's `SoundEffect.Instances`); `SoundEffect` is move-only as a result | `T-3G` |
| **Fixed 2026-07-04** | `Cue::Apply3D`/3D `PlayCue` now forward to `SoundEffectInstance::Apply3D` instead of being no-ops | `T-4B` |
| **Fixed 2026-07-04** | Formal "build & report" checkpoint recorded in `plan_audio.md` | `T-6C` |
| **Fixed 2026-07-04** | `SoundEffectInstance` now has real low/high/band-pass filters via an SDL3_mixer per-track callback; reverb stays a documented no-op | `T-4C` |
| **Accepted deviation** | 3D positional audio is pan + distance-attenuation only, no elevation/Doppler | `CHECKLIST.md` |
| **Accepted deviation** | `SoundEffectInstance::INTERNAL_applyReverb` is a no-op — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md`, `T-4C` |
| **Accepted deviation** | Interactive-type (`type==3`) XACT variation tables fall back to a uniform pick instead of a variable-driven one (parser doesn't retain `var_min`/`var_max` per entry) | `CHECKLIST.md`, `XA-3` |
| **Accepted deviation** | `SoundBank::IsInUse` only tracks fire-and-forget cues it owns; `GetCue`-obtained cues are caller-owned | `T-3B` |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global set + built-in 3D variables, not a true per-cue-instance catalog | `T-3A` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver in CI/this environment; real-hardware runs are manual and ad-hoc, not automated | — |
| **Needs verification** | `SoundEffectInstance`'s filter coefficient locking (`MIX_LockMixer`/`UnlockMixer` in the setters, no lock in the SDL3_mixer callback) follows SDL3_mixer's documented practice but was never stress-tested under real concurrency (no ThreadSanitizer run, no real non-dummy audio device here) | `T-4C` |

All Fáze 0–7 findings (`T-1A`–`T-1H`, `T-2A`–`T-2G`, `T-3A`–`T-3E`, `T-5A`–`T-5O`, `T-4A`, `T-6A`,
`T-6B`, and all 30 of `CP-1`..`CP-14`/`XA-1`..`XA-5`/`IN-1`..`IN-6`/`MC-1`..`MC-5`) are fixed, and so
are all 25 of Fáze 8's findings (22 fixed, 3 closed as documented deviations above). **Fáze 9 is
partial**: all 15 of `P9-LIFECYCLE` fixed/tested as above; `P9-STOP`/`P9-CATEGORY`/`P9-VALIDATION`/
`P9-XACT`/`P9-3D`/`P9-HARDWARE`/`P9-DYNAMIC`/`P9-DOCS`/`P9-BUILD` (plus `P9-AUDIT` itself) remain
open — see `plan_audio.md`'s "Phase 9" section for the full checked/unchecked list with
verification notes.

---

## 6. Architecture notes

### Main modules

| Component | Location | Notes |
|---|---|---|
| XNA audio API | `include/Microsoft/Xna/Framework/Audio/`, `src/.../Audio/` | Must match XNA 4.0 / FNA exactly |
| Internal mixer | `CNA/Internal/Audio/AudioMixer.{hpp,cpp}` | SDL3_mixer `MIX_Mixer` singleton via `GetMixer()`; single 44100/stereo/S16 device (per-audio sample rate is set separately when loading each `MIX_Audio`, so non-44100Hz content is not broken — verified, not a bug) |
| XACT parser | `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp` | Custom `.xgs`/`.xsb`/`.xwb` reader (FACT is **not** used); `XactParserTests.cpp` has 27 tests (was 22 after Fáze 7's `IN-6`; +5 for Fáze 8's `IN-7`/`IN-8`/`IN-10`) |
| sharp-runtime | `../sharp-runtime/` | `System.*` types, primitive aliases, exception hierarchy |

### Data flow (playback)

```
SoundEffect (loads MIX_Audio via GetMixer; move-only -- T-3G)
  → CreateInstance() returns SoundEffectInstance BY VALUE, registers it in
    SoundEffect::Impl::instances for the Dispose cascade (T-3G)
    (instance keeps SoundEffect::impl_ alive via a type-erased shared_ptr<void> -- CP-7)
  → SoundEffectInstance::Play() creates a MIX_Track, binds the MIX_Audio, plays
  → INTERNAL_applyLowPassFilter/HighPassFilter/BandPassFilter register a real state-variable
    filter via MIX_SetTrackCookedCallback; INTERNAL_applyReverb is a documented no-op -- T-4C
  → SoundEffect::Dispose() disposes every tracked instance before releasing impl_ -- T-3G
DynamicSoundEffectInstance
  → user submits buffers → SDL_AudioStream → MIX_Track
  → PendingBufferCount tracks bytes NOT yet consumed by the stream (SDL_GetAudioStreamQueued),
    not merely submitted -- CP-4
  → FrameworkDispatcher::Update() pumps registered instances (Streams list) and raises BufferNeeded
AudioEngine/SoundBank/WaveBank/Cue
  → XactParser reads .xgs/.xsb/.xwb → cues map to SoundEffect/SoundEffectInstance played via SDL_mixer
  → WaveBank's non-streaming ctor loads the whole .xwb eagerly; its streaming ctor reads only
    header/metadata upfront and does a lazy per-entry disk read in GetSoundEffect() -- T-3F
  → Cue::Play() selects a variation entry via a weighted lottery over weightMin/weightMax -- XA-3
  → Cue::Apply3D()/3D SoundBank::PlayCue() forward to each active SoundEffectInstance::Apply3D()
    (already working since CP-3) -- T-4B
Microphone (capture)
  → getAllProperty() enumerates via SDL_GetAudioRecordingDevices
  → Start() opens a real SDL_AudioStream (SDL_OpenAudioDeviceStream); Stop() destroys it
  → GetData()/GetQueuedBytes() read from that stream; GetData() leaves the buffer untouched on a
    no-op read (no zero-fill) -- MC-3
```

### Invariants / rules that must stay stable

- **Backend = SDL3_mixer only.** Do not reintroduce FAudio/FACT.
- **Device opened lazily.** `AudioMixer::GetMixer()` opens the shared playback device on first use;
  `Microphone::Start()` opens its capture stream on first use, not at enumeration time. Both are
  gated by `#ifdef SOUND_ENABLED`.
- **Exceptions on the XNA surface must be `System::` types**, never `std::runtime_error`/
  `std::out_of_range`/`std::invalid_argument`. (Internal `XactParser` corrupt-data throws are the
  one sanctioned exception to this — they use `std::runtime_error`, matching `D7`'s decision, and
  are caught/converted at the `SoundBank`/`WaveBank` constructor boundary before reaching the XNA
  surface.)
- **`GetTypeName()` returns the dotted .NET name** (`"Microsoft.Xna.Framework.Audio.X"`) and lives
  in the **public** section for every concrete `System::Object` subclass — including `SoundEffect`
  now (`CP-8`).
- **`SoundEffectInstance::hasStarted_`** is set on `Play()` and never reset; gates `IsLooped`
  (set-after-play → `InvalidOperationException`).
- **`DynamicSoundEffectInstance`** must override **both** `setIsLoopedProperty(const bool&)` and
  `setIsLoopedProperty(bool&&)` as no-ops (a single-signature override silently hides instead of
  overriding) — same C++ name-hiding trap that made `Stop(bool)` need an explicit override too
  (`CP-5`).
- **`SoundEffect` is move-only; `SoundEffect::Dispose()` cascades to every live
  `SoundEffectInstance`** created via `CreateInstance()` (`T-3G`) — matches FNA's
  `SoundEffect.Instances`. Don't reintroduce the copy ctor/assignment: a single owner per resource
  is what makes the cascade unambiguous. `SoundEffectInstance`'s move ctor/assignment re-point the
  tracking registration (unregister old address, register new) — don't drop that when touching
  those; without it, moving an instance leaves the cascade targeting a stale (possibly
  out-of-scope) address, which is a real segfault, not just a logic bug (verified by disabling
  just the repoint step and watching the regression test crash).
  `SoundEffectInstance` also keeps the underlying `MIX_Audio` resource alive independent of the
  `SoundEffect` wrapper (`CP-7`) — that's a narrower memory-safety mechanism the cascade builds on
  top of, not a substitute for it.
- **`ContentManager::Load<Audio::SoundEffect>()` never caches** (always a fresh load/decode) — a
  deliberate consequence of `T-3G`'s move-only + cascade-dispose semantics, not an oversight.
  Don't "fix" this by caching a `shared_ptr<SoundEffect>` or similar; sharing one instance across
  unrelated `Load()` call sites would let disposing one caller's copy silently cascade-stop
  another caller's still-playing instances.
- **`Apply3D` must not mutate the public `Volume`/`Pan` properties** (fixed as `CP-3` — don't
  reintroduce the old bug).
- **`SoundBank`'s fire-and-forget sweep is event-based (`!IsPlaying`), not purely time-based**
  (fixed as `XA-1` — don't reintroduce the old "older than N seconds" bug).
- **`Cue` now DOES self-transition out of `Playing` once every wave it spawned finishes naturally**
  (`P9-LIFECYCLE-001`, fixed 2026-07-04 — this note used to say the opposite; that was the bug).
  `Cue::ReconcileState() const` runs at the top of `getIsPlayingProperty`/`getIsPausedProperty`/
  `getIsStoppedProperty`/`getIsStoppingProperty` and at the top of `Play()`/`Pause()`: if `state_`
  is `Playing` and every `active_` instance's live `getStateProperty()` is `Stopped`, it clears
  `active_` and flips `state_` to `Stopped`. **Exception, still intentional:** a cue whose
  `active_` is empty from the start (no wavebank reference resolved at `Play()` time — e.g.
  `SoundBankTests.cpp`'s wavebank-less "Explosion"/`CueTests.cpp`'s `MakeCue()` fixtures) has
  nothing to reconcile and stays `Playing` forever; this is what `SoundBankTestAccess`'s
  backdating-based sweep tests still rely on — don't "fix" that specific case as a drive-by
  without checking those tests first. `ReconcileState()` deliberately does **not** touch
  `waveBanksUsed_`/`AudioEngine`'s registries (only `active_`/`state_`) since it runs from `const`
  getters that may themselves be called mid-iteration over those registries (e.g.
  `WaveBank::getIsInUseProperty()`) — the actual unregistration still happens only from
  `StopInternal()` (explicit `Stop()`/`Dispose()`, or `SoundBank::SweepFireAndForget()`, now also
  called from `AudioEngine::Update()`).
- **`Cue::Play()` rejects being called again on an already Playing/Paused/Stopping/Stopped cue**
  (silent no-op, matches `FACTCue_Play`'s `FACT_STATE_PLAYING|STOPPING|STOPPED` reject in
  `FACT.c` — `P9-LIFECYCLE-010/011`, fixed 2026-07-04). Don't remove this guard to "allow restarting
  a cue" — a `Cue` models exactly one playthrough in both FNA and CNA now.
- **Known, not-yet-fixed deviation found alongside the above (`P9-LIFECYCLE-013`, un-implemented):**
  real FACT/FNA lets `IsPlaying` and `IsPaused` both be `true` at once (`FACTCue_Pause` never
  clears `FACT_STATE_PLAYING`) — CNA's `Cue::State` enum is mutually exclusive, so `IsPlaying`
  and `IsPaused` are always disjoint here. Fixing this would touch `AudioEngine::
  PauseCategoryInternal`/`ResumeCategoryInternal`'s `IsPlaying`/`IsPaused` checks and a number of
  existing tests — don't fix as a drive-by; it's queued as its own Phase 9 item.
- **`Cue::GetVariable()`/`SetVariable()` now throw `ObjectDisposedException` first**
  (`P9-LIFECYCLE-015`, fixed 2026-07-04), matching `Play()`/`Apply3D()`'s own precedent in this
  class. Don't remove this guard on the theory that `bank_`/`engine_` "still work" post-dispose —
  that was incidental, not a contract, and real FNA/FAudio would crash natively here anyway
  (`FACTCue_GetVariableIndex` dereferences `pCue->parentBank` before its null check). `Pause()`/
  `Resume()`/`Stop()` deliberately do NOT get the same guard — they already matched FNA's real
  silent-no-op-when-disposed behavior; don't add a throw there, that would be a regression away
  from FNA.
- **`Cue::Play`'s variation selection is a weighted lottery** over `weightMin`/`weightMax` for
  wave/sound/compact_wave tables, matching FAudio's `get_active_variation_index` exactly (`XA-3`).
  Interactive-type (3) tables fall back to a uniform pick (documented deviation, `CHECKLIST.md`) —
  don't "fix" this without first deciding to parse `var_min`/`var_max` into `XsbVariEntry`.
- **`Cue::Apply3D`/3D `SoundBank::PlayCue` forward to `SoundEffectInstance::Apply3D`** on every
  active instance (`T-4B`) — don't revert to the old no-op. No new pan/attenuation math lives at
  the `Cue`/`SoundBank` level; it's all in `SoundEffectInstance::Apply3D` (`CP-3`). Testing this
  needs a WaveBank-backed fixture (`Cue::active_` stays empty without one) — see `SharedApply3DBank`/
  `BuildApply3DXwbFixtureBytes` in `CueTests.cpp`/`SoundBankTests.cpp` for the pattern.
- **`SoundEffectInstance`'s DSP filters (`T-4C`) are real, reverb is a documented no-op.**
  `FilterState` is heap-owned via `unique_ptr` specifically so a move doesn't change its address —
  the SDL3_mixer callback's `userdata` stays valid across a move with no re-registration. Don't
  make `FilterState` a member stored by value, and don't skip re-deriving this if refactoring
  `SoundEffectInstance`'s move ctor/assignment. Coefficient writes (setters) must stay under
  `MIX_LockMixer`/`UnlockMixer`; the callback itself must NOT also lock (SDL3_mixer's mixing
  thread already holds that lock while invoking track callbacks — a second lock would be
  redundant at best). Don't add a real reverb implementation without discussing it first — it
  would need a whole aux-send/return bus SDL3_mixer doesn't have, a much bigger scope than the
  filters.
- **`DynamicSoundEffectInstance::PendingBufferCount`** reflects real stream consumption
  (`SDL_GetAudioStreamQueued`), not submission (`CP-4`) — don't revert to counting
  `queuedBuffers_.size()` alone; that was the exact bug.
- **`Microphone::GetData` never zero-fills** on a no-op/error read — it returns 0 and leaves the
  buffer untouched, matching FNA (`MC-3`). Don't reintroduce the old zero-fill "safety" behavior.
- **`WaveBank`'s streaming ctor must not eagerly load the wave-data segment** (`T-3F`) — only
  segments 0-3 (bank data, entry metadata, seek tables, entry names) are read upfront via
  `ParseXwbStreamingHeader`; entry audio is read lazily per-entry in `GetSoundEffect()` via
  `XwbData::sourcePath`. Don't revert to calling the same eager `Init()` the non-streaming ctor
  uses — that was the exact bug. `offset`/`packetSize` stay intentionally unused, matching FNA.
- **`.xwb` entry `nChannels` is the raw on-disk value, not "channels minus one"** (`IN-7`) — don't
  reintroduce a `+1` anywhere the compact/non-compact format bitfield is decoded; every existing
  test fixture that encodes a channel count uses the raw value directly.
- **COMPLEX-sound parsing order is trackCount → RPC skip → DSP skip → per-track metadata**
  (`IN-8`), matching FACT exactly — don't move per-track metadata parsing back before the RPC/DSP
  blocks; track event data lives *outside* the contiguous sound-header stream (referenced only by
  absolute offset), not immediately after a sound's own metadata.
- **`Cue::StopInternal` only destroys active instances (`active_.clear()`) for an immediate
  stop** (`XA-6`) — a non-immediate (`AsAuthored`) stop must leave them owned by `active_` so their
  already-triggered release/loop tail keeps playing, until this `Cue` is later disposed.
- **Fire-and-forget sweep and `IsInUse` (`SoundBank` and `WaveBank`) treat `IsPlaying || IsPaused`
  as alive** (`XA-7`) — checking `IsPlaying` alone silently destroys/misreports a paused cue.
- **`SoundEffectInstance::Pause()`/`Resume()` are `virtual`**, with a `DynamicSoundEffectInstance`
  override operating on `dynamicTrack_` (`CP-15`) — the base implementation only ever touches the
  protected `track_`, which a dynamic instance never populates.
- **`SoundEffect::MasterVolume` reads/writes SDL3_mixer's live master gain**
  (`MIX_GetMixerGain`/`MIX_SetMixerGain`), not a value baked into each track's own gain (`CP-16`)
  — don't multiply master volume into per-track gain anywhere again; that's what made it never
  affect already-playing sounds.
- **`SoundEffectInstance::is3D_`** latches once `Apply3D` has run; `setPanProperty()` still
  updates the `Pan` property but stops writing the real track output while it's set (`CP-20`),
  matching FNA's own `is3D` guard — never reset back to `false`.
- **`AudioEngine::Dispose()` cascades to every `WaveBank`/`SoundBank`/`Cue` it created** via a
  `SoundBank` registry symmetric to the existing `WaveBank` one (`XA-8`) — snapshots all three
  registries into local vectors and resets `xactImpl_` *before* calling `Dispose()` on each, so
  their own `Unregister*()` callbacks become safe no-ops instead of mutating a container mid-iteration.
- **SPDX + Doxygen + `NOXNA` + SharpRuntime aliases** required per `CLAUDE.md`/`CHECKLIST.md`.

---

## 7. Useful commands

```bash
# Configure (EasyGL, audio enabled) — run from the cna_audio repo root
cmake -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON

# Build the unit-test binary (also builds the CNA library)
cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"

# Run all unit tests
./cmake-build-debug/CnaTests

# Run only the audio test suites
./cmake-build-debug/CnaTests --gtest_filter='*SoundEffect*:*Dynamic*:*AudioEmitter*:*AudioListener*:*SoundState*:*AudioChannels*:*AudioStopOptions*:*MicrophoneState*:*PlayLimit*:*NoAudio*:*NoMicrophone*:*Audio*:*Cue*:*WaveBank*:*SoundBank*:*XactParser*'

# Rebuild the example demos too if you touch anything on the Audio public API surface (e.g.
# SoundEffect's copyability, T-3G) -- CNA_BUILD_EXAMPLES defaults ON but these aren't part of
# --target CnaTests, so a green test run alone won't catch a break here.
cmake --build cmake-build-debug --target cna_demo_sound -j"$(nproc)"
cmake --build cmake-build-debug --target cna_demo_2d -j"$(nproc)"

# Device-dependent tests use the SDL dummy driver automatically, but you can force it globally:
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='SoundEffectInstanceTest.*'

# One-off ASan+LeakSanitizer verification (used for XA-2 and CP-7 this session; NOT part of the
# normal build, delete the build dir after use):
cmake -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=leak -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -fsanitize=leak"
cmake --build cmake-build-asan --target CnaTests -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=1 ./cmake-build-asan/CnaTests --gtest_filter='<TestName>'
rm -rf cmake-build-asan   # clean up afterward

# git-stash regression-verification pattern used for every fix this session:
#   1. git stash push -- <changed source files>   (keep the new test, revert just the fix)
#   2. rebuild, run the new test, confirm it FAILS against the pre-fix code
#   3. git stash pop                                (restore the fix)
#   4. rebuild, run the full suite, confirm green

# FNA reference source for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio

# FAudio C reference for XACT byte-format details not documented elsewhere
grep -n "<symbol>" /rv/data/library/github.com/FNA-XNA/FAudio/src/FACT_internal.c
```

---

## 8. Next smallest tasks

**Fáze 9 is a real, user-specified, still-open task list — continue it, don't invent a new one.**
The user gave an explicit implementation order; pick up exactly where it left off:

**`P9-LIFECYCLE` (all 15 items) is now fully closed as of 2026-07-04.** The one thing it left
behind is a genuine open decision, not a task: the `IsPlaying`+`IsPaused` coexistence deviation
(§5/§6) — fixing it would touch `PauseCategoryInternal`/`ResumeCategoryInternal` and existing
tests, similar in shape to the Fáze 8 `CP-19`/`CP-18`/`XA-9` "touches shared infrastructure"
decisions, so ask the user before implementing rather than picking a side. It is NOT blocking
anything else in Fáze 9.

1. **`P9-CATEGORY-001..004`** next per the user's specified order: fix `AudioCategory` operations
   to snapshot `activeCues` before iterating (currently iterates `AudioEngine::activeCues` live in
   `PauseCategoryInternal`/`ResumeCategoryInternal`/`StopCategoryInternal`/
   `SetCategoryVolumeInternal` — check whether any of those four can mutate the vector mid-iteration
   before assuming this is purely hypothetical), then regression tests for multi-cue pause/resume/
   stop/volume.
2. **`P9-VALIDATION-001..013`** (`SoundEffect`/`DynamicSoundEffectInstance` constructor and
   post-Dispose argument validation) after that, then **`P9-DOCS-001..007`**, then
   **`P9-BUILD-001..007`** — this is the user's specified order, don't reshuffle it without asking.
3. **`P9-STOP`, `P9-XACT`, `P9-3D`, `P9-HARDWARE`, `P9-DYNAMIC`** come last per the user's own
   ordering — don't jump ahead to these before the groups above are done.
4. **`P9-AUDIT-001..005`** (the fresh-read audit tasks) were never in the user's explicit
   implementation order and remain unchecked; the per-file reading needed to fix `P9-LIFECYCLE`
   happened ad hoc but the formal audit deliverable (comparison write-up) was never produced —
   pick this up if asked, or fold it into whichever group is being worked when a stale-doc
   inconsistency is found.

**Commit boundary:** everything in this handoff (`P9-LIFECYCLE-013..015` fix + this `NEXT.md`
update) is currently uncommitted on top of `9435da9` — commit before starting `P9-CATEGORY` so
this fix isn't bundled with unrelated work.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit (a hypothetical "Fáze 10").** Fáze 7
  and Fáze 8 already did two rounds of that and are closed. Fáze 9 itself is a *different*,
  already-user-scoped kind of pass (hardening against a fixed task list, not "re-audit everything
  again") and is legitimately still open — don't confuse "don't re-audit" with "Fáze 9 is done,"
  it isn't (§4/§8).
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep as documented stored-not-applied.
- **No parsing `var_min`/`var_max` into `XsbVariEntry` as a drive-by** — this would change
  interactive-type variation selection (currently a documented uniform-pick deviation from `XA-3`);
  it's a real feature addition, not a bug fix, if ever done.
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos/clones. If
  a build breaks there, check whether it's the known concurrent-development hazard (§4) before
  "fixing" it from this branch; only patch CNA-side compliance code if truly needed to unblock the
  build.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — `IN-5` already brought `XactTypes.hpp` up to standard this
  session; don't repeat that sweep elsewhere without a specific reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md if you need file-by-file history. Fáze 7 (30 items),
Fáze 8 (25 items), and now all 15 of P9-LIFECYCLE are fully done (2026-07-04). Fáze 9 as a whole
is still IN PROGRESS: P9-CATEGORY/P9-VALIDATION/P9-XACT/P9-3D/P9-HARDWARE/P9-DYNAMIC/P9-DOCS/
P9-BUILD/P9-AUDIT are all still open -- see §4/§8. This is real open work, don't treat this
branch as "nothing left to do."

1. Confirm the current build/test state matches NEXT.md §2 (build clean, 2078/2078 tests pass) --
   rebuild and rerun SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests to check for drift since
   this was last updated. Also rebuild cna_demo_sound/cna_demo_2d if you touch anything on the
   Audio public API surface -- they're not part of CnaTests and easy to forget.
2. Commit the uncommitted P9-LIFECYCLE-013..015 work first (see §8's "Commit boundary") if it
   isn't already committed, so the next group starts from a clean base.
3. Continue Fáze 9 in the order specified in plan_audio.md's "Phase 9" section / this file's §8:
   P9-CATEGORY-001..004 next, then P9-VALIDATION, P9-DOCS, P9-BUILD, then
   P9-STOP/P9-XACT/P9-3D/P9-HARDWARE/P9-DYNAMIC last. Don't invent a "Fáze 10" full re-audit on
   your own initiative (see §9) -- Fáze 9's own task list is not exhausted yet. The one leftover
   decision from P9-LIFECYCLE (IsPlaying+IsPaused coexistence, §5/§6) is NOT blocking -- ask the
   user before implementing it, don't let it stall P9-CATEGORY.
4. Follow the established git-stash regression-verification pattern (see NEXT.md §7) for any
   behavioral fix -- stash it, confirm the new test fails against the pre-fix code, restore,
   confirm green. Run ASan+LeakSanitizer too if the change touches memory lifetime, ownership, or
   cross-thread state. For any "implement vs. document" framed decision, ask the user rather than
   assuming -- the established pattern is "implement unless it risks destabilizing already-shipped
   shared infrastructure."

After finishing a task, check its checkbox in plan_audio.md, update NEXT.md (status, recent
changes, next task), and commit.
```
