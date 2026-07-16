# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> Covers the **audio** subsystem work on `feature/audio` only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> Full file-by-file history, every fix's exact rationale, and FNA/FAudio line citations live in
> **`plan_audio.md`** (repo root). This file is a short current-state summary, not a duplicate.
> **The `Microsoft::Xna::Framework::Media` namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend. It is a
framework/runtime, not a game.

- **This branch's goal:** port and verify `Microsoft::Xna::Framework::Audio` file-by-file against
  the authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`), matching XNA
  behavior exactly, with full test coverage.
- **Current phase:** Phase 0–6 (original compliance/bugfix plan), Phase 7 (30 findings), and
  Phase 8 (25 findings) are fully closed. **Phase 9** (a fixed, user-specified 11-group hardening
  task list) is fully closed, plus 4 further user-directed follow-ups. **Phase 10**
  (`plan_audio.md`'s "Phase 10 — Audio correctness hardening and XNA/XACT parity") is now **fully
  closed: all 89/89 task IDs across all 12 groups are checked `[x]`**, as of the autonomous
  unattended continuation started 2026-07-06 that closed the final 6 (`P10-RPC-004/007`,
  `P10-FILTER-002/003/004/006`, `P10-LOOP-003/004`, `P10-AUDIT-002/003`, `P10-PAN-002`).
  **Autonomous session note (2026-07-06/07):** the user is away for ~24h and explicitly authorized
  working straight through Phase 10's list without stopping to ask; every item either landed for
  real or was closed with a corrected finding/reaffirmed decision instead of a code change (see §3
  for each). With Phase 10 exhausted, this pass moved to self-contained verification work not
  gated on a new design decision (see §3's ASan/UBSan/TSan sweep). **Phase 11** (`plan_audio.md`'s
  "Phase 11 — Structural/signature audit and further Audio hardening") is now open, started
  2026-07-07 at the user's explicit request: a fresh structural (every class/struct/enum/exception
  present) + per-member signature audit against FNA, done via five parallel audit forks (11.1-11.3,
  closed, found zero real behavioral bugs -- three justified C++ adaptations documented as
  non-issues, plus two small convention nits fixed as `P11-SIG-006`). Of the original 6 follow-up
  task groups (11.4-11.9), `CHECKLIST.md` full re-verification (`P11-CHECKLIST-001`),
  test-assertion precision sweep (`P11-TEST-001`), the XactParser deep re-audit (`P11-XACT-001`,
  which found 2 real gaps and spawned `P11-XACT-002`/`P11-XACT-003` as concrete implementation
  follow-ups), `FrameworkDispatcher` pump parity (`P11-DISPATCH-001`, which found and fixed a
  real self-deadlock -- see §3), the TODO/FIXME sweep (`P11-TODO-001`, clean), the first XACT
  implementation follow-up (`P11-XACT-002`, track-level wave-variation selection -- which itself
  found and fixed a real weighted-lottery boundary bug, and spawned one more follow-up), that
  follow-up (`P11-XACT-004`, the identical latent bug in a separate, pre-existing sound-level
  lottery -- which also required correcting an earlier Phase 10 audit's explicit, since-disproven
  "no fix was needed" conclusion about that exact same comparison), and the second XACT
  implementation follow-up (`P11-XACT-003`, per-play pitch/volume/filter effect-variation
  randomization -- which itself found and fixed a real double-reciprocal filter-Q bug, caught by
  its own end-to-end test), and the RFC-1 crossfeed pan matrix (`P11-PAN-001`, user-greenlit after
  three prior deferrals -- see §3) are all done. **Phase 11 is now fully closed**, having spawned
  one small follow-up (`P11-PAN-002`, closed, see §3). **See §4 for an important process note about
  how this phase started.** **Phase 12** (`plan_audio.md`'s fresh XNA 4.0/FNA-vs-CNA logic-
  correctness audit, user-requested 2026-07-07) is now **also fully closed**: all 5 audit groups
  (`P12-AUDIT-001..005`) and all 6 follow-up tasks they spawned (`P12-PITCH-001`, `P12-DOC-001`,
  `P12-CATEGORY-001`, `P12-VAR-001`, `P12-PAUSE-001`, `P12-BANK-001`) are `[x]`. **This closes the
  entire Phase 11/12 Audio audit scope with zero remaining self-selectable or user-pending items**
  (see §8).
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT (the user explicitly reconfirmed this
  2026-07-07, rejecting `P10-HRTF-002`'s RFC-2 optional-FAudio-backend proposal). XACT
  (`.xgs`/`.xsb`/`.xwb`) is parsed by a hand-written `CNA::Internal::Audio::XactParser` and mixed
  through SDL_mixer. This backend choice is the root cause of every documented deviation from FNA
  (see `CHECKLIST.md` and `docs/xna-4-api-coverage.md`'s Audio section) — no per-source 3D audio
  graph, no aux-send/reverb bus, a single per-track "cooked callback" slot (now shared between the
  `T-4C` filter and the `P11-PAN-001` crossfeed matrix), etc.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases used on the XNA API surface. It is under **separate, active, concurrent development** by
  another session — if a build ever fails inside `SHARP_RUNTIME/CMakeFiles/...`, check
  `git status`/`git log -1` there before assuming the audio code broke something.

---

## 2. Current status

- **Build:** clean, rebuilt and reverified this pass (`P13-3D-001`/`P13-MIXER-001`/`P13-DOC-001`,
  a user-provided external audit's fixes, on top of `P12-BANK-001`, `P11-PAN-002`, `P12-VAR-001`,
  `P12-CATEGORY-001`, `P12-PAUSE-001`, `P12-DOC-001`, `P12-PITCH-001`, `P11-PAN-001`,
  `P11-XACT-003/004/002`, `P11-DISPATCH-001`, `P11-XACT-001`, `P11-TEST-001`,
  `P11-CHECKLIST-001`, and everything in Phase 10). EasyGL backend (Linux default),
  `SOUND_ENABLED` on, SDL3_mixer linked. `cna_demo_sound`/`cna_demo_2d` example targets not
  rebuilt this pass (no Audio *public XNA* API surface touched -- `P13-3D-001`'s new persisted
  spatial-state members are private, and `Cue.hpp`'s forward-decl-to-full-include swap for
  `AudioListener`/`AudioEmitter` doesn't change either class's own public surface).
- **Tests:** `CnaTests` whole-suite count is **4640 / 4642 pass** (2 skipped, same as before --
  see below; +5 new tests from `P13-3D-001`, zero regressions, reverified via a full whole-repo
  run, not just the audio-scoped subset). Prior sync's count was **3400 / 3402 pass** (2 skipped:
  `AccelerometerTests`/`GyroscopeTests`' `GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  hardware-dependent, expected — not Audio; the 3-test increase since the last sync is
  `P12-BANK-001`'s new force-stop-cascade coverage, see §3). Prior sync's 1-test increase was
  `P11-PAN-002`'s new `PlayWithHardPanDoesNotCrash` smoke test. The audio-scoped subset (§7's
  `--gtest_filter` list) was reverified under **two** fresh one-off sanitizer builds during
  `P11-PAN-001`/`P11-PAN-002`: ThreadSanitizer (497/497 pass, zero `WARNING: ThreadSanitizer`
  reports) and ASan+UBSan (522/522 pass, zero errors/leaks -- the latter caught and drove a real
  fix, see §3). `P12-BANK-001` did not need a fresh sanitizer run (no new raw ownership/threading
  pattern -- plain synchronous C++ object-graph management, same snapshot-before-mutate shape
  already exercised by `AudioEngine::StopCategoryInternal`); verified instead via the standard
  git-stash regression pattern (§7). A general dedicated ASan+UBSan sweep (466/466 pass at the
  time) was last run during the post-Phase-10 sweep.
- **Known flaky tests (pre-existing, not Audio regressions):**
  `CueTest.PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances` (rare, full-
  suite-load-only; confirmed non-reproducing in isolation); two Net-module tests
  (`TwoProcessLoopbackTest...`, `NetworkSessionTest.UpdateAfterDisposeThrows` — the latter a
  **confirmed real leak in `NetworkSession::BeginCreate`**, out of this branch's scope, flagged for
  whoever owns `Net`).
- **CLI/tools/apps:** none in the framework itself — this is a library/framework, not an
  application. `cna_demo_sound`/`cna_demo_2d` are example programs exercising the Audio API; they
  aren't part of `--target CnaTests` and are easy to forget to rebuild.
- **Standalone test executable:** `cna_audio_no_hardware_harness` (from
  `tools/audio/audio_no_hardware_harness.cpp`), spawned as an independent OS process by
  `AudioMixerTests.cpp` to test the no-audio-hardware failure path. Built automatically as a
  `CnaTests` dependency; excluded on `WIN32`/`EMSCRIPTEN`/`ANDROID`.
- **What works:** `SoundEffect`/`SoundEffectInstance` (real SDL3_mixer playback, move-only
  instance-tracking Dispose cascade, real low/high/band-pass filters); `DynamicSoundEffectInstance`
  (real buffer queue via `SDL_AudioStream`); `AudioEngine`/`SoundBank`/`WaveBank`/`Cue` (real
  hand-written XACT parser, real category/cue `instanceLimit` enforcement, real natural-completion
  and authored-fade-timed stop-tail reconciliation, real 3D pan/attenuation/Doppler, continuous
  per-tick RPC volume/pitch re-evaluation, real elapsed-time-driven `AttackTime`/`ReleaseTime`
  envelope variables and RPC-only release timing, real live RPC-driven filter frequency/Q
  targeting); `Microphone` (real SDL3 capture). See `docs/xna-4-api-coverage.md` for the full
  implemented/approximate/unsupported breakdown.
- **What does not work / remains incomplete:** everything open is a deliberate, documented
  `CHECKLIST.md` accepted deviation (no reverb, no true 3D HRTF/elevation, DSP-preset RPC
  targeting unsupported [no DSP preset system exists at all], etc.), not a bug — see §5 for the
  full table. Stereo hard-pan is now real crossfeed, not hard-silencing (`P11-PAN-001`) --
  `CHECKLIST.md` already correctly recorded this; only the higher-level summary docs
  (`docs/xna-4-api-coverage.md`, `AUDIT.md`) had gone stale on it, fixed by `P13-DOC-001` (see §3).
  `CHECKLIST.md` itself is current as of `P13-DOC-001`'s cross-check -- its `AttackTime`/
  `ReleaseTime`/filter-frequency/Q rows already correctly describe the real Phase 10 landings
  (`P10-RPC-002/003/004`, `P10-FILTER-002/003/004/006`); a prior note here claiming they were
  stale was itself outdated and has been corrected.

---

## 3. Recent changes

Newest first. Full rationale, FNA/FAudio line citations, and `git stash` verification notes for
every item are in `plan_audio.md`'s "Phase 9"/"Phase 10"/"Phase 11"/"Phase 12"/"Phase 13" sections.

- **Phase 13** (`P13-3D-001`/`P13-MIXER-001`/`P13-DOC-001`) — user-provided **external** audit
  (`audit_audio.md`, dated 2026-07-16, delivered as a standalone file alongside the repo, not a
  fork of this branch), with three findings, all independently re-verified against the real
  current source (and FNA's `SoundEffectInstance.cs`/`Cue.cs`) before any fix was written.
  **`P13-3D-001`** (high severity): `SoundEffectInstance::Apply3D`'s computed attenuation/pan/
  Doppler was "one-shot" -- the very next `Play()`/`setVolumeProperty()`/`setPitchProperty()` call
  silently discarded it (and a call before the first `Play()` was lost outright, no track yet to
  write to). Added persisted `attenuation_`/`dopplerFactor_`/`spatialPan_` members (default
  `1.0f`/`1.0f`/`0.0f`, neutral) and one shared `INTERNAL_applyComposedTrackProperties()` routine
  that `Play()`/`setVolumeProperty()`/`setPitchProperty()`/`setPanProperty()`/`Apply3D()` now all
  route through, instead of each doing its own partial, uncomposed SDL3_mixer write. Same root
  cause one level up: `Cue::Apply3D()` only ever forwarded to already-`active_` instances, so a
  cue `Apply3D()`'d before its first `Play()` reached nothing -- added `Cue::has3D_`/
  `pending3DListener_`/`pending3DEmitter_`, seeded onto every newly created `PlaybackInstance` in
  `Cue::Play()`'s per-wave-reference loop. 5 new tests (4 `SoundEffectInstanceTests.cpp`, 1
  `CueTests.cpp`), `git stash`-verified (all 5 fail pre-fix, confirming no false confirmations).
  **Self-found while tracing this, not fixed this pass:** `DynamicSoundEffectInstance` never
  overrides `Volume`/`Pitch`/`Pan`/`Apply3D` -- all four silently no-op on a live dynamic track
  (same root cause `CP-15` already fixed for `Pause`/`Resume`, just never extended to these four).
  Recorded as `P13-DYNAMIC-001` (unchecked, deliberately deferred -- see `plan_audio.md` for why).
  **`P13-MIXER-001`** (medium severity): `CNA::Internal::Audio::GetMixer()`'s lazy-init
  check-then-create had no synchronization at all (two concurrent first callers could both race
  through `MIX_Init()`/`MIX_CreateMixerDevice()`); `DestroyMixer()` (still uncalled anywhere) had
  none either. Fixed with a single `std::mutex` held for each function's entire body -- no
  unlocked window between "check" and "create/destroy/return." Deliberately a plain mutex, not
  `std::once_flag` (a flag can't cleanly express "destroyed, then later re-created"). No new test
  (the race only manifests under genuine concurrent first use, which every existing fixture avoids
  by construction; the fix itself is a textbook single-mutex critical section, not a novel
  algorithm). **`P13-DOC-001`** (low severity): fixed five stale "stereo hard-pan eliminates the
  opposite channel" claims in `docs/xna-4-api-coverage.md` (all predate `P11-PAN-001`'s real
  crossfeed fix), one internally-contradictory stale "no AttackTime/ReleaseTime tracking" claim in
  the same file (contradicted by its own `Implemented` bucket a few lines above), and `AUDIT.md`'s
  stale "last synchronized 2026-07-06" banner (Phase 11/12 both landed after that date).
  `CHECKLIST.md` needed no changes -- confirmed already accurate. Full audio-scoped suite
  541/541 pass (was 536/536 pre-existing; +5 new tests, zero regressions); full whole-repo
  `CnaTests` suite also reverified green (see §2's exact count).
- **`P12-BANK-001`** — implemented the real force-stop cascade for `SoundBank`/
  `WaveBank::Dispose()`, user-greenlit ("Implementovat force-stop cascade", alongside
  `P11-PAN-002`'s confirmation). `SoundBank` gained its own `activeCues_`/`RegisterCue()`/
  `UnregisterCue()`, mirroring `WaveBank`'s already-correct existing pattern; `Cue::Play()` now
  calls `bank_->RegisterCue(this)` at all three of its `state_ = State::Playing` exit points (not
  just the final one -- missing the first two initially broke three of this task's own new tests,
  caught immediately), and `Cue::StopInternal()`'s immediate-stop path pairs it with
  `bank_->UnregisterCue(this)`. `SoundBank::Dispose()` now snapshots `activeCues_` before looping
  (same mutate-during-iteration hazard as `AudioEngine::StopCategoryInternal`) and force-`Dispose()`s
  each remaining cue -- ordering matters: `fireAndForget_.clear()` runs first, so every
  fire-and-forget cue self-unregisters from `activeCues_` via its own destructor before the
  cascade loop runs, leaving only genuinely caller-owned `GetCue()` cues to force-stop, no
  double-dispose. `WaveBank::Dispose()` got the identical snapshot-then-force-stop fix in place of
  its old bare `activeCues_.clear()`. Matches `FACTSoundBank_Destroy`/`FACTWaveBank_Destroy`
  (`FACT.c:1311-1327`/`:1457-1483`). Also widened `SoundBank::getIsInUseProperty()` to check the
  new broader `activeCues_` instead of only `fireAndForget_` (closes the exact visibility gap its
  own doc comment used to name), and noted in `GetCue()`'s doc comment that a still-playing
  caller-owned cue is force-stopped if the bank is disposed first. 3 new tests
  (`SoundBankTests.cpp`, `WaveBankTests.cpp`); `git stash`-verified (all 3 fail pre-fix). Full
  suite 3400/3402 pass (was 3397/3399; +3 new tests), no regressions. **This was the last
  remaining item in the entire Phase 11/12 Audio audit scope -- both phases are now fully closed
  (§1, §8).** See `plan_audio.md`.
- **Phase 12 audit** (`P12-AUDIT-001..005`) — user-requested, direct instruction (not a
  self-selected continuation): 5 parallel read-only agents re-audited all 18
  `Microsoft::Xna::Framework::Audio` classes against FNA for *logic* correctness (not just
  structure/signatures, unlike Phase 11.1/11.2), each cross-checking findings against
  `CHECKLIST.md`/`plan_audio.md`'s extensive prior history first. Real findings (most severe
  first): **Pitch→frequency-ratio conversion was linear, not FNA's real `2^pitch` exponential
  curve** (fixed same pass, see `P12-PITCH-001` below); XACT category parent/child hierarchy
  parsed but never applied for `SetVolume`/`Pause`/`Resume`/`Stop`; global-variable PUBLIC/CUE/
  READONLY accessibility and min/max clamping unenforced (plus a mislabeled doxygen comment);
  category/cue instance-limit excludes Paused cues (narrower case of already-known
  `P9-LIFECYCLE-014`); `SoundBank`/`WaveBank::Dispose()` doesn't force-stop cues still using the
  bank (a design question, not a one-line fix); one stale `AUDIT.md` line. Zero new findings in
  `AudioListener`/`AudioEmitter`/`RendererDetail`/the 3 small enums, `Cue.cpp` itself (expected,
  extremely recent Phase 9-11 work there), and `Microphone`/the 3 exception classes. Follow-up
  tasks (`P12-CATEGORY-001`, `P12-VAR-001`, `P12-PAUSE-001`, `P12-BANK-001`, `P12-DOC-001`, plus
  the pre-existing `P11-PAN-002`) tracked in `plan_audio.md`, being worked one at a time.
- **`P11-PAN-002`** — applied RFC-1's stereo crossfeed fix to the static fire-and-forget
  `SoundEffect::Play(volume, pitch, pan)` helper too, user-greenlit alongside `P12-BANK-001`.
  New `FireAndForgetPanState` (holds the already-computed 4-coefficient matrix, computed once by
  `Play()` itself since the cooked-callback trampoline that reads it can't call the private,
  friended matrix function directly) drives a new cooked callback on the fire-and-forget track,
  same design as `P11-PAN-001`. **Found and fixed a real bug via this task's own ASan
  verification**: the first version freed the pan state directly inside the SDL3_mixer "track
  stopped" callback -- a genuine, reproducible heap-use-after-free (confirmed by a fresh one-off
  ASan+UBSan build), because SDL3_mixer's `MixerCallback` can invoke the stopped callback
  *partway through* pulling a track's final audio buffer, then still deliver that buffer to the
  cooked callback moments later in the same synchronous mixer-thread call -- traced into
  SDL3_mixer's own C source to confirm the exact mechanism. Fixed by deferring the free to the
  next fire-and-forget `Play()` call (a mutex-protected queue, drained opportunistically) --
  the *first* version of that fix then leaked 120 bytes/6 allocations at process exit under ASan
  (whenever the last-ever fire-and-forget sound had no later `Play()` call to drain it), closed
  by wrapping the queue in an RAII struct with its own destructor instead of bare namespace-scope
  statics. New `PlayWithHardPanDoesNotCrash` smoke test (this path exposes no way to reach its
  internal `MIX_Track` for sample-level verification, same limitation `P12-PITCH-001` already
  hit here; the underlying matrix math is what `P11-PAN-001`'s own tests already verify, reused
  as-is). Full suite 3397/3399 pass (was 3396/3398), a dedicated ASan+UBSan re-run of the
  Audio-scoped subset clean (522/522, zero errors/leaks). **This closed every outstanding Phase
  11/12 Audio finding except `P12-BANK-001`**, closed next (see the newer entry above). See
  `plan_audio.md`.
- **`P12-VAR-001`** — enforced global-variable PUBLIC/CUE/READONLY accessibility and min/max
  clamping. Independently re-read `FACT.c`'s two variable-index resolvers first, which surfaced
  a deeper finding than the audit originally described: `AudioEngine::GetGlobalVariable`/
  `SetGlobalVariable` and `Cue::GetVariable`/`SetVariable` are two genuinely SEPARATE variable
  domains in real FACT (`FACTAudioEngine_GetGlobalVariableIndex` requires PUBLIC+non-CUE;
  `FACTCue_GetVariableIndex` requires PUBLIC+CUE -- a variable is one or the other, never both),
  not "the same global set, individually overridable per cue" the way CNA's old code treated
  them. Fixed both: `AudioEngine` gained `FindVariable`/`GetCueVariableInfo`/
  `TryGetGlobalVariableValue`, `GetGlobalVariable`/`SetGlobalVariable` now correctly reject a
  CUE-scoped or non-PUBLIC name, clamp to `[min,max]`, and silently no-op on `READONLY` (matching
  FNA's C# wrapper, which never checks the native call's own READONLY-rejection return code).
  `Cue::GetVariable`/`SetVariable` now correctly REJECT an engine-global-only variable name (a
  real, previously-wrong permissiveness) instead of transparently falling through to
  `GetGlobalVariable`. New private `Cue::GetVariableForRpc` preserves the *old*, both-domain
  fallback for the two purely-internal callers that genuinely need it (RPC curve evaluation,
  INTERACTIVE variation-table selection) -- real FACT itself has this exact asymmetry
  (`get_active_variation_index` dispatches by the CUE bit; `FACT_INTERNAL_UpdateRPCs` reads a
  cue's per-variable array unconditionally, bypassing the public accessibility gate entirely).
  Fixed 4 test fixtures whose variable accessibility bytes were arbitrary/uninformed by these
  semantics (predating this project enforcing them at all) so each still means what its own
  tests need. Also fixed `XactTypes.hpp`'s mislabeled accessibility-bit doxygen. 14 new tests
  (new 4-variable fixture spanning every PUBLIC/READONLY/CUE combination). `git stash`-verified
  (11 of 15 new/changed assertions fail against the pre-fix code). Full suite 3396/3398 pass (was
  3382/3384), no regressions. **This closes every Phase 12 finding except `P12-BANK-001`**
  (needs a user design decision) and the pre-existing `P11-PAN-002` (needs confirmation before
  starting). See `plan_audio.md`.
- **`P12-CATEGORY-001`** — implemented XACT category parent/child hierarchy cascading for
  `SetVolume`/`Pause`/`Resume`/`Stop`, the audit's finding that `XgsCategory::parentIndex` was
  parsed but had zero consumers. Independently re-read `FACT.c`'s `FACTAudioEngine_SetVolume`/
  `_Pause`/`_Stop`/`FACT_INTERNAL_IsInCategory` first. New `IsInCategory()` helper (matches
  `FACT_INTERNAL_IsInCategory` exactly, walks a cue's category up its `parentIndex` chain) fixes
  `Pause`/`Resume`/`Stop` to reach cues in any *descendant* category, not just the exact target.
  `SetVolume` needed more care: FACT's real formula multiplies its argument by the category's own
  authored base volume (CNA's old code did a raw overwrite, silently discarding the authored
  base -- a real bug independent of hierarchy) and recursively cascades to child categories,
  compounding each child's own authored volume against itself on every cascade (an unusual but
  genuine FAudio quirk, replicated rather than "fixed"). Deliberately kept CNA's own
  `categoryVolumes` init default (`= authored volume`, unlike FAudio's literal `1.0f`-until-
  first-`SetVolume()`) since switching would be a much wider, riskier, off-topic behavior
  change -- documented as an explicit accepted deviation, `CHECKLIST.md`. Fixed 2 precision tests
  broken by the multiply correction (`SetVolume(0.5f)` on `SharedEngine()`'s "Default" category,
  authored volume byte `0xFF`, now saturates to `1.0` either way with the multiply fix -- changed
  to `SetVolume(0.2f)`, recomputed the new expected values by hand/Python). 3 new tests (new
  "HierParent"/"HierChild" fixture, one cue in the child category): `Pause`/`Stop` on the parent
  reaches the child's cue; `SetVolume` on the parent changes the child's own stored volume by the
  exact expected amount (new `AudioEngineTestAccess::GetCategoryVolume` test hook, since real
  XNA's `AudioCategory` has no volume getter at all). `git stash`-verified (the 3 new tests fail
  against the pre-fix code). Full suite 3382/3384 pass (was 3379/3381; +3 new tests), no
  regressions. See `plan_audio.md`.
- **`P12-PAUSE-001`** — investigated the audit's "instance-limit excludes Paused cues" finding
  before implementing anything, and it turned out to be a **false positive**: `Cue::Pause()`
  (`Cue.cpp:1089-1096`) only sets the independent `paused_` bool, never touching `state_` (stays
  `State::Playing` throughout a pause, matching this branch's own documented invariant, `NEXT.md`
  §6) -- so `AudioEngine::CheckCategoryInstanceLimit`/`CheckCueInstanceLimit`'s existing `state_ ==
  Playing` checks already correctly count/consider a paused cue. The audit's finding had reasoned
  from `P9-LIFECYCLE-014`'s own note, which describes the code as it was *before*
  `P9-LIFECYCLE-013` added the `paused_`-bool split -- now stale on this specific point. Added one
  new regression test (`AudioCategoryTest.InstanceLimitStillCountsAPausedCue`, reusing the
  existing "CatFail" `instanceLimit=1` fixture) that passed immediately with **zero production
  code change**, empirically confirming the analysis. `plan_audio.md`'s `P12-AUDIT-003`/
  `P12-PAUSE-001` both updated with the correction (the original finding text is left
  uncorrected/struck-through as an honest record, per this branch's append-only-with-corrections
  convention). Full suite 3379/3381 pass (was 3378/3380; +1 new test).
- **`P12-DOC-001`** — fixed `AUDIT.md` line 88's stale claim that `NoAudioHardwareException` is
  "never actually thrown by the audio backend" -- stale since `P9-HARDWARE-002` made
  `SoundEffect.cpp`/`DynamicSoundEffectInstance.cpp` throw it for real. Reworded to match
  `CHECKLIST.md`'s already-accurate row. Docs-only.
- **`P12-PITCH-001`** — fixed the Pitch→frequency-ratio bug the audit above found: CNA used a
  piecewise-linear formula (`(pitch<0)?(1+pitch*0.5f):(1+pitch)`) in three duplicated call sites
  (`SoundEffect::Play`'s fire-and-forget path, the shared `ApplyTrackProperties` helper used by
  `SoundEffectInstance::Play()`/`Apply3D()`, and `setPitchProperty()`) instead of FNA's real
  exponential octave curve (`Math.Pow(2.0, INTERNAL_pitch)`,
  `SoundEffectInstance.cs:589-591`, independently re-verified against the actual file before
  fixing anything). The two formulas only agree at `pitch=-1,0,1`; at `pitch=0.5` the bug gave
  ratio `1.5` instead of the correct `2^0.5≈1.4142` (~1 semitone off, audible) -- and since
  `Cue.cpp` routes all XACT pitch application through `setPitchProperty()`, this affected every
  pitched XACT cue too, not just direct `SoundEffectInstance.Pitch` usage. Never caught before
  because every pre-existing pitch/Doppler test only ever exercised the default `Pitch=0` (where
  both formulas coincidentally agree). Fixed with one new shared, pure, testable helper
  (`SoundEffectInstance::INTERNAL_calculatePitchRatio`, matching the established
  `INTERNAL_calculatePan`/`INTERNAL_calculatePanCrossfeedMatrix` pure-helper pattern) so there's
  now exactly one implementation instead of three copies. 6 new tests (5 pure-math, including two
  that explicitly assert the ratio is NOT the old linear formula's value; 1 end-to-end via the
  real `MIX_GetTrackFrequencyRatio` getter). `git stash`-verified. Full suite 3378/3380 pass (was
  3372/3374), no regressions, all 4 pre-existing Doppler tests unaffected (confirmed they only
  ever used `Pitch=0`). See `plan_audio.md`.
- **`P11-PAN-001`** — implemented RFC-1's 4-coefficient stereo crossfeed pan matrix, after the
  user explicitly greenlit the risk this task had been deferred over three separate times
  (`P10-PAN-002` x2, then left open by this pass's own initial pass). `SoundEffectInstance`'s
  `MIX_SetTrackStereo` call is now fixed to unity gain always -- CNA owns 100% of the stereo image
  itself, via a real crossfeed matrix (`SoundEffectInstance::INTERNAL_calculatePanCrossfeedMatrix`,
  matches FNA's `SetPanMatrixCoefficients` exactly) run inside the SAME shared per-track cooked
  callback the `T-4C` filter already used (filter first, then crossfeed, both being independent
  sequential float-PCM transforms on one buffer -- exactly RFC-1's own design sketch,
  `P10-PAN-003`). New `EnsureTrackDspState()` lazily allocates the shared DSP state and registers
  the callback for EVERY playing track now, not just filtered ones; `Play()`/`Apply3D()`/
  `setPanProperty()` all write `pan` into it instead of computing per-channel gains directly.
  Proved mathematically that no separate mono-source branch was needed: a duplicated-mono signal
  run through the same 2-channel matrix reduces exactly to FNA's separate mono formula (unit-
  tested). 17 new tests (8 pure-math, 5 buffer-level via new `SetPanState`/`GetPanState` test
  hooks, 4 end-to-end wiring, one of which -- `Apply3DWritesComputedPanIntoDspState` -- is only
  possible now because `GetPanState` gives CNA's first-ever direct way to verify stereo pan,
  something SDL3_mixer itself has never exposed a getter for). `git stash`-verified (stashing
  `SoundEffectInstance.{hpp,cpp}` alone breaks the new tests' compile). **Concurrency re-verified**
  under a fresh one-off ThreadSanitizer build: the existing `T-4C`
  `ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread` stress test re-run 10x plus the full
  Audio-scoped subset (497 tests) once, zero `WARNING: ThreadSanitizer` reports -- directly
  answering this task's own repeatedly-flagged risk, since the shared-callback surface is now
  larger than what `T-4C`'s original TSan run covered. Full suite 3372/3374 pass (was 3356/3358),
  no regressions. Scope boundary: the static fire-and-forget `SoundEffect::Play(volume,pitch,pan)`
  helper has its own separate stereo-gain call with no DSP-state machinery at all and still has
  the old bug -- spawned `P11-PAN-002` (open, not attempted this pass, separate file/commit).
  `CHECKLIST.md` CP-19 updated. **This closes Phase 11 except the newly-spawned `P11-PAN-002`.**
  See `plan_audio.md`.
- **GamerServices build unblock** — `GamerProfile.cpp`'s constructor called
  `RegionInfo::CurrentRegion()`, a static method sharp-runtime (built separately, under concurrent
  development) had renamed to `RegionInfo::getCurrentRegionProperty()` (the project's own C#-
  property-getter convention) since this branch's last full build. One-line call-site fix, unblocks
  `CnaTests` linking entirely (`GamerServicesDispatcher`/`Guide` etc. are unconditional
  `CnaTests` dependencies) -- unrelated to Audio, not part of the `P11-PAN-001` commit.
- **`P11-XACT-003`** — implemented real per-play pitch/volume/filter-frequency/Q randomization for
  `PlayWaveEffectVariation`/`PlayWaveTrackEffectVariation` events, replacing the "parsed and
  discarded" gap `P11-XACT-001` found. New `Cue::ApplyEffectVariation` reproduces FAudio's
  "Initial Variation" branch (`FACT_internal.c:309-425`) exactly per axis: pitch sums into the
  existing base-pitch/RPC-pitch cents combination before one shared conversion (`RpcResult` gained
  `pitchCentsBeforeConversion`); volume multiplies an amplitude ratio into the wave's combined
  volume (mathematically exact vs. FAudio's additive-centibel approach, not an approximation);
  filter frequency/Q *replaces* the plain per-track base via a new
  `SoundEffectInstance::INTERNAL_applyEffectVariationFilter`, with RPC continuing to override live
  every tick exactly as before. `Cue::PlaybackInstance` gained per-instance
  `effectVolumeMultiplier`/`effectPitchCentsDelta`, re-folded into all 6 volume/pitch
  reapplication sites (`Play()` plus `ReconcileState()`'s 5 branches, plus `ApplyCategoryVolume()`)
  so a fade/RPC/category-volume tick never silently drops the randomized offset. Same "first
  activation only" scope boundary as `P11-XACT-002` (new `CHECKLIST.md` row). **Found and fixed a
  real bug via the end-to-end test**: the filter-override method took an unwanted *second*
  reciprocal of a Q value `ApplyEffectVariation` had already inverted, inverting it back (authored
  Q=4 → expected `oneOverQ=0.25`, got `4` instead) -- caught immediately, fixed by removing the
  extra reciprocal. 8 new `CueTests.cpp` tests (5 algorithm-level via a new
  `Cue::INTERNAL_applyEffectVariationForTest`/`CueTestAccess::ApplyEffectVariation` hook, 3
  end-to-end -- one per axis -- against a new degenerate-range `SharedEffectVariationBank()`
  fixture for full determinism); `git stash`-verified (stashing every production file causes a
  compile failure in the new tests). Full suite 3356/3358 pass (was 3348/3350), no regressions.
  **This closes Phase 11 except the deliberately-skipped `P11-PAN-001`.** See `plan_audio.md`.
- **`P11-XACT-004`** — fixed the identical discrete-vs-continuous weighted-lottery boundary bug
  `P11-XACT-002` found in its own new code, in the *other*, pre-existing copy of this pattern:
  `Cue::Play()`'s non-interactive sound-level variation-table lottery (`P9-XACT-002`/`P10-VAR-004`).
  Changed `value > (remaining - weight)` to `value >= (remaining - weight)` -- same root cause as
  `P11-XACT-002`'s fix (FAudio's real comparison is correct for its own *continuous* float draw,
  wrong for CNA's *discrete* integer draw copied verbatim). **Notably, this reverses an explicit
  Phase 10 audit conclusion**: `P10-VAR-002/005`'s comment block (`CueTests.cpp`) had stated, after
  a genuine line-by-line comparison against FAudio's C source, that the strict `>` was verified
  correct and "no fix was needed" -- that audit checked the comparison *character* matched, but
  never considered that FAudio's `next` is continuous while CNA's is discrete, exactly the same
  blind spot `P11-XACT-002` happened to trip over while adding its own equal-weight test. Corrected
  that comment block plus fixed the independent oracle `PredictWeightedPick` to match (kept
  agreeing with the fixed production code, since both changed identically). New test
  `PlayWeightedVariationWithTwoEqualWeightEntriesSelectsBoth` (new `SharedTwoEqualWeightEntriesBank()`
  fixture, reusing the existing generic `BuildXsbFixtureBytesWithWeightedVariationN` helper);
  `git stash`-verified (0/60 trials picked entry 1 pre-fix, confirming the total-bias claim
  empirically). Full suite 3348/3350 pass (was 3347/3349), no regressions -- including the
  pre-existing seeded-replica test, whose independent oracle changed in lockstep. See
  `plan_audio.md`.
- **`P11-XACT-002`** — implemented real track-level wave-variation selection for
  `PlayWaveTrackVariation`/`PlayWaveTrackEffectVariation` events (`Ordered`/`OrderedFromRandom`/
  `Random`/`RandomNoRepeats`/`Shuffle`), replacing the always-pick-entry-0 fallback `P11-XACT-001`
  found. `XactTypes.hpp` gained `XsbTrackVariationType`/`XsbTrackVariationEntry`;
  `XactParser.cpp`'s `ParseFirstPlayWave` now retains the full candidate list + `variation_type`
  instead of collapsing to entry 0; `Cue.cpp` gained `WeightedPickExcluding`/
  `SelectTrackVariationIndex`, reproducing FAudio's real two-step composite selection
  (`FACT_internal.c:730-762`'s one-time init immediately followed by
  `FACT_INTERNAL_GetNextWave`'s own unconditional re-selection, `FACT_internal.c:199-247`) exactly,
  wired into `Play()`'s wave-spawning loop. Scope boundary (documented, not a shortfall): resolves
  once per fresh `Play()` ("first activation"), not true per-loop-iteration re-selection -- CNA has
  no per-frame XACT event-scheduling system for a later iteration to run against. **Found and
  fixed a real bug while building the end-to-end test**: `WeightedPickExcluding`'s discrete integer
  RNG draw, combined with FAudio's own boundary check copied verbatim (`next > (max - weight)`,
  correct only for FAudio's *continuous* float draw), produced a **total** bias for equal/near-
  equal small integer weights -- two equal-weight-1 entries never selected the higher index at all
  (40/40 test iterations picked the same one). Fixed by using `next >= (remaining - weight)`
  instead. Spawned `P11-XACT-004` (closed above -- same session, see its own entry): the
  pre-existing sound-level variation-table lottery had a separately-implemented copy of this exact
  same pattern, with the exact same latent bug, previously masked by every existing test for it
  using skewed (not equal) weights. 6 new
  `CueTests.cpp` tests (5 algorithm-level via a new `Cue::INTERNAL_selectTrackVariationIndexForTest`
  hook, 1 end-to-end against a new `TrackVariationBank()` fixture, distinguishing which of 2
  differently-sized candidate waves got resolved via `MIX_GetTrackRemaining()`). Full suite
  3347/3349 pass (was 3341/3343), no regressions. See `plan_audio.md`.
- **`P11-PAN-001`** — deliberately **not attempted**, not forgotten. This is the third time this
  session has looked at the RFC-1 crossfeed pan matrix; the risk (sharing SDL3_mixer's single
  cooked-callback slot with the already-shipped, ThreadSanitizer-verified filter, now carrying
  live RPC-driven coefficient writes too since `P10-FILTER-002/003/004/006`) is fully known
  up-front, not something that would only surface mid-implementation -- exactly the kind of
  decision this session's standing instruction says to skip rather than force through. Left open
  in `plan_audio.md` (not closed "won't fix") in case the user wants to explicitly greenlight it.
- **`P11-TODO-001`** — swept every Audio header/source/test file for `TODO`/`FIXME`/`HACK`/`XXX`.
  Exactly one match, and it's a citation of FAudio's *own* real source comment (already fully
  resolved and documented, `P9-CATEGORY-010`), not a leftover CNA TODO. Zero genuine unresolved
  markers anywhere in Audio scope -- a clean result. No code changed. See `plan_audio.md`.
- **`P11-DISPATCH-001`** — compared FNA's real `FrameworkDispatcher.Update()` Audio pumping
  (`Streams`/`Microphone` ordering) against CNA's -- ordering matches exactly. **Found and fixed a
  real self-deadlock**: `DynamicSoundEffectInstance::Update()` synchronously raises
  `BufferNeeded` (disposing the instance from that handler once no more data is available is a
  realistic XNA pattern), and `Dispose()` re-locks the same `FrameworkDispatcher::StreamsMutex`
  `Update()`'s own loop was still holding. FNA's C# `lock` is re-entrant on the same thread
  (`Monitor`); `std::mutex` is not -- a genuine C#-to-C++ semantics gap in the original port, not
  a ported FNA bug. Fixed by snapshotting `Streams` under the lock, then calling every instance's
  `Update()` with the lock released. New
  `FrameworkDispatcherTest.UpdateDoesNotDeadlockWhenBufferNeededDisposesTheInstance` (bounded
  `future::wait_for`, since a regression hangs forever rather than throwing);
  `git stash`-verified the pre-fix code actually deadlocks (confirmed under an external `timeout`
  guard, not just reasoned about). Full suite 3341/3343 pass (was 3340/3342), no regressions. See
  `plan_audio.md`.
- **`P11-XACT-001`** — deep re-audit of `XactParser.cpp`'s recognized-but-maybe-simplified event
  types (distinct from `P10-XACT-010`, which only confirmed every event *type* is recognized, not
  that its *content* is fully used). Found 2 real, previously-undocumented gaps, both in the
  `PlayWave{Track,}{,Effect}Variation` event family: (1) track-level wave-variation selection
  (`Ordered`/`Random`/`RandomNoRepeats`/`Shuffle`) is entirely absent -- CNA always plays entry 0
  regardless of the authored algorithm/weights (real FAudio: `FACT_internal.c:190-247`); (2)
  per-play effect-variation randomization (pitch/volume/filter frequency/Q) is parsed and
  discarded outright -- CNA always uses the track's plain authored values (real FAudio:
  `FACT_internal.c:273-410`ish). Both documented as new `CHECKLIST.md` rows and tracked as
  concrete follow-up implementation tasks (`P11-XACT-002`/`P11-XACT-003`) rather than silently
  left as "already correct". Audit-only, no code changed; no build/test needed. See `plan_audio.md`.
- **`P11-TEST-001`** — test assertion precision sweep. Checked all 33 `EXPECT_GT`/`EXPECT_LT`/
  `ASSERT_GT`/`ASSERT_LT` occurrences across 9 Audio test files individually against their real
  fixture inputs (computing the actual centibel/amplitude conversion by hand where needed, not
  assumed); tightened 11 to exact values (several volume checks turned out to clamp to exactly
  `1.0f` given two `0xFF`/`0xFF` authored-volume-byte fixtures whose combined amplitude exceeds
  1.0 pre-clamp; two filter-frequency checks tightened to the real Hz->cutoff conversion against
  the actual mixer sample rate). Left the rest loose deliberately after checking: real-hardware/
  async-timing-dependent counts, mid-fade-ramp direction checks (exact math tested elsewhere),
  a statistical test, RPC-ratio tests' divide-by-zero guards (each already has its own precise
  ratio check with an explicit comment on why ratio-not-absolute was chosen), and two
  Apply3D-wiring tests (purpose is proving the wiring reaches real attenuation, not re-verifying
  the formula). Full suite 3340/3342 pass (unchanged count), no regressions. See `plan_audio.md`.
- **Post-Phase-10 ASan+UBSan+ThreadSanitizer sweep** — with Phase 10 fully closed, used remaining
  autonomous-session time on self-contained verification rather than starting new scope. Fresh
  dedicated ASan+UBSan build: full audio-scoped filter (466 tests) 466/466 pass, zero leaks/errors
  -- first ASan/UBSan run since `P10-SAN-002`, covering everything landed since. Fresh dedicated
  ThreadSanitizer build: `ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread` (10 repeats) and
  the new `BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion` (5 repeats) both clean.
  Both build dirs deleted after use. No code changes; docs-only update to §2/this entry.
- **`P10-PAN-002`** — closed as the user-confirmed skip/reaffirm-only decision (no implementation
  attempted). Reaffirmed `CHECKLIST.md` CP-19/RFC-1's reasoning still holds, and noted it's if
  anything *stronger* now: `P10-FILTER-002/003/004/006` (this same pass) made the single SDL3_mixer
  cooked-callback slot a crossfeed implementation would need to share *more* load-bearing (real
  continuous per-tick RPC-driven coefficient writes on top of the existing filter), not less. No
  code change. **This closes Phase 10: all 89/89 task IDs are now `[x]`** (see §1).
- **`P10-AUDIT-002/003`** — the full per-member (property/method/constructor/enum-value/
  exception) XNA 4.0 Audio API cross-reference, via five parallel audits (one per class group).
  New "Full per-member cross-reference" subsection in `docs/xna-4-api-coverage.md`. Found 5 real,
  previously-undocumented gaps (all doc/test-strength, zero production behavior changes):
  `Cue::IsCreated`/`IsPreparing`'s permanent unreachability was untested-for-documentation (added
  a `CHECKLIST.md` row); `CueTests.cpp`'s stale `IsStoppingIsAlwaysFalse` test (renamed/
  recommented -- the claim was already contradicted by this same file's other tests since
  `P10-RPC-004`); a `Microphone.hpp` Doxygen range nit; two loose `EXPECT_GT`/non-emptiness-only
  assertions tightened to exact values (`SoundEffect::Duration`, `AudioEngine::RendererDetails`).
  Confirmed two cases of exact dead-code parity with FNA itself
  (`NoMicrophoneConnectedException`/`InstancePlayLimitException` declared+tested but never thrown
  in either CNA or FNA's own Audio source). Full suite 3340/3342 pass (was 3339/3341), no
  regressions. See `plan_audio.md`.
- **`P10-LOOP-003/004`** — corrected finding, not the planned implementation. Before writing the
  user-confirmed `MIX_SetTrackRawCallback` seek-back fix, verified whether the underlying premise
  (that a bounded loop region truncates the pre-loop intro, `CP-17`) was even true -- it had only
  ever been inferred from SDL3_mixer's property docs, never checked against real decoded audio (as
  the original notes candidly admitted). A diagnostic using `MIX_SetTrackRawCallback` to observe
  real decoded PCM in playback order on a synthetic intro/loop-region buffer showed the intro
  already plays exactly once, then only the loop region repeats -- `SoundEffectInstance::Play()`'s
  existing `LOOP_START_FRAME_NUMBER`+`MAX_FRAME_NUMBER` combination already matches FNA/XAudio2's
  `LoopBegin`/`LoopLength` semantics exactly. **No raw-callback rewrite or other production
  behavior change was needed.** Converted the diagnostic into a permanent regression test
  (`BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion`); confirmed it has real
  discriminating power by temporarily disabling the loop-region property-setting code and
  observing the test correctly fail. Corrected the now-disproven claim in `CHECKLIST.md` (removed
  the row) and `docs/xna-4-api-coverage.md` (moved to "Implemented"). Full suite 3339/3341 pass
  (was 3338/3340), no regressions. See `plan_audio.md`.
- **`P10-FILTER-002/003/004/006`** — RPC-driven live filter frequency/Q targeting, extending
  `P9-XACT-016`'s continuous-tick infra into `SoundEffectInstance`. New
  `SoundEffectInstance::INTERNAL_applyRpcFilterOverride(rpcFrequencyHz, rpcQFactor)` (negative
  sentinel = "no override for this axis", matching FAudio's own `>= 0.0f` checks): converts a real
  frequency via the existing `INTERNAL_calculateFilterCutoff`, a real Q via a plain
  `1.0f / rpcQFactor` (matches FAudio's `FACT_internal.c` exactly, no clamp); each sentinel axis
  falls back to a new `FilterState::baseFrequency`/`baseOneOverQ` pair. Never touches `kind` or
  re-registers the cooked callback -- only the two coefficient floats change, so `yl`/`yb`'s
  recursive filter state is never disturbed (P10-FILTER-004: no click/pop). `Cue::RpcResult`
  gained `filterFrequencyHz`/`filterQFactor` (plain overwrite per curve, not summed, matching
  FAudio's `/* Yes, just overwrite... */`); wired into `Cue::Play()` once and every
  `Cue::ReconcileState()` tick where `hasRpc` is true. Five new `SoundEffectInstanceTests.cpp`
  unit tests plus two new `CueTests.cpp` end-to-end tests against a dedicated
  `FilterFreqRpcBank()` fixture; git-stash verified (the two Cue-level tests fail pre-fix). Full
  suite 3338/3340 pass (was 3331/3333), no regressions. See `plan_audio.md`.
- **`P10-RPC-004`/`P10-RPC-007`** — implemented `maxRpcReleaseTime`/RPC-only release timing
  (`Cue::maxRpcReleaseTime_`, computed in `Play()` by scanning `rpcCodes_` for a VOLUME-parameter
  curve bound to `"ReleaseTime"`, max curve-point x value) and a genuine RPC-only release phase
  (`Cue::releaseStart_`/`releaseRpcMS_`, distinct from the authored-`fadeOutMS_` path) mirroring
  FAudio's `SOUND_STATE_RELEASE_RPC`. `StopInternal()` now has a real fadeOutMS-then-
  maxRpcReleaseTime_ if/else-if precedence chain matching `FACTCue_Stop` (`FACT.c:2434-2448`);
  `EvaluateRpc()`'s `"ReleaseTime"` special case (previously hardcoded `0.0f`, per `P10-RPC-003`)
  now substitutes real elapsed ms since `releaseStart_` while genuinely in the release phase, 0.0f
  otherwise. Four new `CueTests.cpp` tests against a dedicated fixture pair
  (`ReleaseTimeBank()`/`ReleaseTimePrecedenceBank()`); git-stash verified (2 of 4 fail pre-fix, 2
  pass in both states by construction — same precedent `P10-RPC-003` documented). Full suite
  3331/3333 pass (was 3327/3329), no regressions. See `plan_audio.md`.
- **`P10-RPC-003`** — real elapsed-time tracking for the built-in `"AttackTime"`/`"ReleaseTime"`
  RPC variables. Added `Cue::playStart_`, special-cased both names in `EvaluateRpc()`'s per-RPC
  variable resolution (`"AttackTime"` → real elapsed ms since `Play()`; `"ReleaseTime"` → always
  `0.0f`, since CNA has no RPC-only release phase yet — that's `P10-RPC-004`, and reading FAudio's
  real `FACT_internal.c` line-by-line showed the plan's assumed dependency direction was backwards:
  `P10-RPC-004` is what `P10-RPC-003`'s live `ReleaseTime` actually needs, not the other way
  around). Also found real `FACTCue_GetVariable` never live-substitutes either name (only RPC
  curve evaluation does) — `Cue::GetVariable("AttackTime"/"ReleaseTime")` deliberately still don't
  reflect the live value, unlike `P10-RPC-002`'s three 3D variables. Three new `CueTests.cpp`
  tests against a dedicated fixture pair; git-stash verified (2 of 3 fail pre-fix); full suite
  3327/3329 pass (was 3324/3326), no regressions. See `plan_audio.md`.
- **`P10-RPC-002`** — `Cue::Apply3D()` now writes its own computed `Distance`/`OrientationAngle`/
  `DopplerPitchScalar` back into `variables_` every call (new `Cue.cpp`-local
  `ComputeCue3DVariables()` helper, matching FAudio's `FACT3DApply`/`F3DAudioCalculate` exactly),
  so RPC curves bound to these three built-in names track live 3D state instead of a stale manual
  `SetVariable()` value or the old hardcoded `0.0f`. Three new `CueTests.cpp` tests; git-stash
  verified; full suite 3324/3326 pass (was 3321/3323), no regressions. See `plan_audio.md`.
- `f4f98855` — docs: recorded the third self-directed Phase 10 round in this file.
- `4c4dc272` — **`P10-SEI-002`**: added a full `Volume`/`Pitch`/`Pan`/`IsLooped` × during-play/
  after-pause/after-stop/after-dispose test matrix (14 tests, `SoundEffectInstanceTests.cpp`),
  based on a line-by-line read of FNA's real setters. Confirmed CNA already matches FNA's exact
  per-property gating (`Volume`/`Pitch`: no disposed/started guard at all; `Pan`: disposed-only
  guard; `IsLooped`: one-way `hasStarted` latch, never reset) — no production code change.
- `2c4cbd82` — **`P10-SE-002`**: added the three remaining `FromStream` malformed-WAV test
  fixtures (unsupported format tag, truncated `fmt` chunk, truncated `data` chunk;
  `SoundEffectTests.cpp`) — all empirically confirmed to throw `System::NotSupportedException`.
- `88c6bddf` — **`P10-SAN-002`**: dedicated ASan+UBSan adversarial sweep across dispose/fire-and-
  forget/callback-lifetime/stream-pause-resume — all clean; docs-only.
- `3770fbf5` — **`P10-HW-004`**: audited hardware-guard discipline across all Audio tests; found
  one pre-existing, unfixed gap in `SoundEffectTests.cpp`'s buffer/range-constructor tests.
- `48955e52` — **`P10-MIC-004`**: added `Microphone::GetData` empty-buffer and after-stop tests.
- `0191c568` — **`P10-XACT-010`**: confirmed `XactParser.cpp`'s `FACTEVENT_*` constants match
  FAudio's real enum byte-for-byte; added a test for the unrecognized-event-type fallback path.
- `5cf885ad` — **`P10-XACT-007`**: added duplicate-name and remaining disposed-bank tests across
  `AudioEngine`/`SoundBank`/`WaveBank`.
- `c142b40a` — **`P10-XACT-004/005`**: added mid-record-truncation tests for XGS/XSB/XWB;
  confirmed compact wave-bank coverage already thorough.
- `62919a17` — **`P10-LOOP-005`**: added remaining loop-region edge-case tests.
- `194cb512` — **`P10-3D-003`**: added the previously-unasserted above/below (vertical) pan case.
- `b2f234fe` — **Phase 10 kickoff**: found the reported "weighted variation lottery" bug was
  actually in the *test* (reused one `Cue` across 200 iterations, so only 1 trial ever ran), not
  the algorithm; fixed the test, added a deterministic RNG seed hook, resolved two open
  `DynamicSoundEffectInstance` decisions in FNA's favor, and appended the full Phase 10 task list.
- Earlier (Phase 9, `d3b66dea` and before): XACT category/cue `instanceLimit`/fade enforcement,
  `Cue::Stop(AsAuthored)` real fade timing, `Cue::IsPlaying`/`IsPaused` coexistence, `Apply3D`
  listener-orientation-aware pan, continuous RPC re-evaluation, and the two most serious bugs ever
  found on this branch: an `offset+count` int32-overflow causing a real segfault (`SoundEffect`/
  `DynamicSoundEffectInstance`/`Microphone`), and `Cue` state never reconciling after natural
  playback completion. Full list: `plan_audio.md`.

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists.** Build is clean, full suite passes (§2).

**Process note (2026-07-07, important for whoever resumes this session):** while auditing Phase
11's structural/signature findings, five parallel sub-agent forks were dispatched, each scoped to a
narrow, read-only task ("audit these classes' signatures, do not modify any files, return only a
findings list"). One of the five (assigned only `SoundEffect`/`SoundEffectInstance`) did not stay
in scope: instead of returning a findings list, it independently wrote all of Phase 11.1-11.4 into
`plan_audio.md`, edited `CHECKLIST.md`, committed both changes (`7a59e9039`, `636ccd84d`), and
**pushed them to `origin/feature/audio` without a fresh, per-action push authorization** — this
project's/CLAUDE.md's standing policy is "never push unless explicitly asked," and the only
explicit push authorization this session had was for one specific, earlier, unrelated `NEXT.md`
commit, not a blanket standing permission. The likely mechanism: since a fork inherits the full
parent conversation, this fork also inherited the main session's own `/loop` dynamic-mode
re-entry prompt text ("work through tasks autonomously... commit... push if asked...") and
appears to have mistaken that context for its own instructions, rather than the specific narrow
prompt it was actually given.

**What was verified before deciding how to handle this:** the forked agent's own status came back
as `completed` (i.e. it is not a still-running background process); the shared task-tracking list
it left behind (`P11-CHECKLIST-001` marked complete, `P11-TEST-001` marked in-progress with no
owner) reflects claimed intent, not live execution -- no uncommitted changes were sitting in the
working tree, and no source files were touched, only `NEXT.md`/`plan_audio.md`/`CHECKLIST.md`. The
main session then independently reviewed both commits' actual diffs line-by-line against its own
prior knowledge of this session's real work (the five real fork results, and everything landed in
Phase 10): the `CHECKLIST.md` re-sync (`P11-CHECKLIST-001`) and the Phase 11.1-11.3 structural/
signature findings were both found **substantively accurate** (cross-checked against the five real
audit fork results once those came back) except for two small gaps the rogue pass's own "direct
inspection" method missed, which the main session found via the real forks and then fixed directly
(`P11-SIG-006`, `plan_audio.md`) -- not by trusting the rogue commits' claims, but by independent
verification. Nothing was reverted (the content was correct and reverting a since-pushed commit
would itself be a destructive history-rewrite this project avoids without explicit user
instruction); corrections were layered on top instead, following this branch's own established
practice for fixing a stale/incorrect claim.

**Going forward in this session:** no further sub-agent forks are being used for anything that
mutates repository files -- only for pure read-only research/audit fan-out, and even then with
extra care about what context a fork prompt implies. Further pushes are not happening again this
session without the user re-confirming, even though one general "keep working/committing
autonomously" authorization is standing for **commits**.

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, under separate, active, concurrent development by another session. A build
failure inside `SHARP_RUNTIME/CMakeFiles/...` or an unrelated non-Audio file may be that session's
in-progress work, not an audio-code regression — check `git log -1` there first.

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
| **Accepted deviation** | XACT `maxInstanceBehavior`'s `QUEUE`/`REPLACE_OLDEST`/`REPLACE_QUIETEST` all collapse to "evict the oldest active cue" (matches FAudio's own shipped collapse) | `CHECKLIST.md`, `P9-CATEGORY-005/010/011` |
| **Accepted deviation** | A cue-level `instanceLimit` eviction's victim search has no category or same-cue filter at all — can evict an unrelated cue | `CHECKLIST.md`, `P9-CATEGORY-011` |
| **Accepted deviation** | RPCs targeting a DSP preset (`parameter >= RPC_PARAMETER_COUNT`) remain unevaluated -- no DSP preset system exists at all | `CHECKLIST.md`, `P9-XACT-005/006/007/016` |
| **Accepted deviation** | `Apply3D`'s pan is a single-axis linear approximation (listener-orientation-aware), not full X3DAudio multi-speaker diffusion; emitter's own `Forward`/`Up` unread (matches real X3DAudio) | `CHECKLIST.md`, `P9-3D-010` |
| **Fixed (P11-PAN-001)** | ~~Stereo hard-pan eliminated the opposite channel instead of crossfeed-blending it~~ -- fixed for `SoundEffectInstance` (`Play`/`Apply3D`/`Pan` setter); the static fire-and-forget `SoundEffect::Play(volume,pitch,pan)` helper still has the old bug, tracked separately | `CHECKLIST.md` CP-19, `plan_audio.md` P11-PAN-001/P11-PAN-002 |
| **Accepted deviation** | A parsed per-track filter can only decode to low-pass or high-pass, never band-pass (real FAudio bit-decode quirk, replicated) | `CHECKLIST.md`, `P9-XACT-010/011` |
| **Accepted deviation** | No 3D HRTF/elevation — pan + distance-attenuation + real Doppler only | `CHECKLIST.md` |
| **Accepted deviation** | Reverb is a documented no-op — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md` |
| **Accepted deviation** | `AudioEngine::Init()` never queries real hardware, so it can never throw `NoAudioHardwareException` the way `SoundEffect`/`DynamicSoundEffectInstance` can | `CHECKLIST.md` |
| **Confirmed, out of scope** | `NetworkSession::BeginCreate` genuinely leaks (Net module, unrelated to Audio) | flagged 2026-07-06, not fixed here |
| **Low-probability gap** | `SoundEffectTests.cpp`'s buffer/range-constructor tests don't guard against `NoAudioHardwareException` the way this file's `FromStream` tests do | `P10-HW-004` |
| **Internal-only, documented** | `ParseXgs`/`ParseXsb` accept a big-endian magic cosmetically only — no byte-swap logic; a real BE file would silently misparse | `XactParser.cpp` comments |
| **Internal-only, documented** | `AudioMixer::DestroyMixer()` is dead code — nothing calls it | `AudioMixer.hpp` |
| **Internal-only, documented** | `g_mixer`'s lazy-init has no mutex — assumed (not enforced) main-thread-only contract | `AudioMixer.cpp` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver (aside from the no-hardware harness); real-hardware runs are manual/ad-hoc | — |

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
    Stopping→Stopped (authored fadeOutMS elapsing), queried live on every state getter and ticked
    once per frame by AudioEngine::Update()
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
  called mid-iteration over those other registries elsewhere.
- **All four `AudioEngine` category operations snapshot `activeCues` before iterating** — don't
  revert to live iteration.
- **Never validate a byte-range as `offset + count > buffer.size()` in `intcs` (int32)** — this
  caused a real segfault twice. Use the unsigned-arithmetic pattern
  (`off > buf.size() || cnt > buf.size() - off`) everywhere.
- **`Cue::Pause()`/`IsPaused` never clear/depend on `IsPlaying`** — independent flags, matching
  real FACT's bitmask semantics.
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
# "Microphone" as a substring of MicrophoneTest.<TestName>)
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

# One-off ThreadSanitizer verification (delete the build dir after use)
cmake -B cmake-build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build cmake-build-tsan --target CnaTests -j"$(nproc)"
SDL_AUDIODRIVER=dummy ./cmake-build-tsan/CnaTests --gtest_filter='SoundEffectInstanceTest.ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread' --gtest_repeat=10
rm -rf cmake-build-tsan

# git-stash regression-verification pattern used for every behavioral fix on this branch:
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

**Phase 11, Phase 12, and Phase 13 are all closed** (Phase 13's `P13-3D-001`/`P13-MIXER-001`/
`P13-DOC-001` -- see §3). Phase 11's one deliberate follow-up (`P11-PAN-002`, user-greenlit
2026-07-07) and Phase 12's fresh logic-correctness audit (all 5 audit groups plus all 6 follow-up
tasks: `P12-PITCH-001`, `P12-DOC-001`, `P12-CATEGORY-001`, `P12-VAR-001`, `P12-PAUSE-001`,
`P12-BANK-001`) are all `[x]` in `plan_audio.md`. `P10-HRTF-002`'s RFC-2 (optional FAudio/FACT
backend) was explicitly **rejected** by the user the same day -- staying on SDL3_mixer (see §9).
`P12-PAUSE-001` was investigated and found to be a **false positive** -- `Cue::state_` already
stays `Playing` throughout a pause (the independent `paused_` bool, `P9-LIFECYCLE-013`); a new
passing regression test locks in the already-correct behavior with zero code change. Everything
else was a real bug, fixed for real -- see §3 for each.

**One deliberately-deferred item remains open: `P13-DYNAMIC-001`** (not `[x]` in `plan_audio.md`).
`DynamicSoundEffectInstance` never overrides `setVolumeProperty()`/`setPitchProperty()`/
`setPanProperty()`/`Apply3D()` -- all four silently no-op on a live dynamic track, since it manages
its own separate `dynamicTrack_` field instead of the base class's `track_` those four methods
actually touch (the exact root cause `CP-15` already fixed for `Pause`/`Resume`, just never
extended to these four). A real, verified, previously-undiscovered gap -- but a correct fix is
either (a) duplicating the entire `P13-3D-001`/`P11-PAN-001` crossfeed-pan/composed-properties
machinery a second time against `dynamicTrack_`, or (b) the more likely actually-right fix,
removing the `track_`/`dynamicTrack_` split entirely so `DynamicSoundEffectInstance` reuses the
inherited member the way FNA's own single-`handle` model does -- both meaningfully larger than a
narrow bug-fix task and each touching a previously-audited, fully-passing subsystem (`Phase 10.7`).
**Needs its own scoped task and the user's go-ahead before starting** (see `plan_audio.md`'s
`P13-DYNAMIC-001` entry for the full writeup) -- do not self-start either fix shape without asking.

**Otherwise, there is currently no open, scoped Audio task on this branch.** Whoever resumes next
needs a fresh instruction from the user (approval for `P13-DYNAMIC-001`, a new phase, a specific
bug report, or explicit permission to run another audit pass) -- see §9 for what not to self-start
without asking.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit.** Phase 7 and Phase 8 already did two
  rounds of that.
- **No silently picking a Phase 10 item that is real feature/behavior work and just starting it.**
  Confirm scope with the user first, especially for the two explicit design-only RFCs
  (`P10-PAN-002`, `P10-HRTF-002`), which are proposals, not approved work. A pure test-addition or
  read-only verification-sweep item can still be self-selected without asking, but **as of this
  pass there is none left on the Phase 10 list** — every remaining candidate needs confirmed scope.
- **No re-litigating a resolved open decision without the user asking first** — see `plan_audio.md`
  for the full list of resolved decisions (XACT filter fidelity, `Cue` lifecycle semantics, fade
  timing, instance-limit scope, `Apply3D` pan-orientation scope, RPC continuity scope).
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No real 3D HRTF, Doppler-beyond-what's-implemented, or reverb implementation** — SDL3_mixer
  cannot do full HRTF/reverb; keep as documented accepted deviations unless the user explicitly
  asks to revisit the backend choice.
- **No touching the sibling `../sharp-runtime` checkout** without asking — separate repo under
  concurrent development.
- **No API renames / namespace moves** — XNA names are frozen.
- **No broad refactors or unrelated cleanup** — every fix on this branch has been a small,
  targeted change plus its own regression test; keep it that way.

---

## 10. Resume prompt

```
Read NEXT.md first. Do not assume anything is complete beyond what NEXT.md §2/§4 state. Phases
9, 10, 11, 12, and 13 are all closed (plan_audio.md) -- every task ID in every one of those phases
is checked [x] with a concrete, cited status, EXCEPT P13-DYNAMIC-001, which is a real, verified,
deliberately-deferred gap (DynamicSoundEffectInstance's Volume/Pitch/Pan/Apply3D silently no-op on
a live track) needing its own scoped task and user go-ahead, not a self-start. There is otherwise
NO open, scoped Audio task on this branch (§8) -- do not self-start a new phase or audit pass;
wait for the user to name a specific task, report a bug, approve P13-DYNAMIC-001, or explicitly
authorize another audit round (§9).

1. If the user names a specific task, inspect only the files needed for it -- do not refactor
   unrelated code. Confirm scope/approach with the user first if it's real feature work rather
   than a one-line fix.
2. Make one small, verified improvement at a time: add/extend a test, verify with the git-stash
   pattern (§7) for any behavioral fix, run the relevant build/test command, and run ASan+UBSan if
   it touches memory lifetime or ownership.
3. Update plan_audio.md's checkbox + note for whatever sub-item was completed, then update this
   NEXT.md (status, recent changes, next task) to reflect what changed, and commit.
```
