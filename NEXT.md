# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> Covers the **audio** subsystem work on `feature/audio` only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> Full file-by-file history, every fix's exact rationale, and FNA/FAudio line citations:
> **`plan_audio.md`** (repo root). This file is deliberately a *short* current-state summary, not
> a duplicate of that log.
> **Media namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend. It is a
framework/runtime, not a game.

- **This branch's goal:** port and verify `Microsoft::Xna::Framework::Audio` file-by-file against
  the authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`), matching XNA
  behavior exactly, with full test coverage.
- **Current phase:** the original compliance/bugfix plan (Fáze 0–6), Fáze 7 (30 findings) and
  Fáze 8 (25 findings) — two prior line-by-line audits against FNA — are fully closed. **Fáze 9**
  is a separate, user-directed "audio correctness hardening" pass against a fixed, user-specified
  11-group task list (`plan_audio.md`'s "Phase 9" section). 8 of those 11 groups are fully closed
  (`P9-LIFECYCLE`, `P9-CATEGORY`, `P9-VALIDATION`, `P9-DOCS`, `P9-BUILD`, `P9-STOP`, `P9-XACT`,
  `P9-3D` — `P9-XACT`'s own 15-item and `P9-3D`'s own 9-item lists are both now fully done, the
  latter finding and fixing a real distance-attenuation formula bug, implementing real Doppler
  pitch shift, and documenting one newly-found gap: pan ignores listener/emitter `Forward`/`Up`
  orientation entirely); `P9-HARDWARE` is 2/6 done (the `NoAudioHardwareException` audit + its
  raw-`std::runtime_error`-vs-XNA-exception-type fix); `P9-DYNAMIC` is **fully closed (9/9)** (the
  `PendingBufferCount` audit found and fixed two real bugs, one of which uncovered and fixed a
  serious cross-cutting `System::EventHandler<T>` bug in the sibling `sharp-runtime` repo, plus
  format-conversion and buffer-alignment audits confirming no further bugs); 1 group remains fully
  open (`P9-AUDIT`) — see §4/§8.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is parsed
  by a hand-written `CNA::Internal::Audio::XactParser` and mixed through SDL_mixer. This backend
  choice is the root cause of every documented deviation from FNA (see `CHECKLIST.md` and
  `docs/xna-4-api-coverage.md`'s Audio section for the full compatibility table and the
  SDL3_mixer-vs-FAudio/FACT limitations behind it) — no per-source 3D audio graph, no aux-send/
  reverb bus, only a 2-value stereo gain pair instead of a 4-coefficient crossfeed matrix, a single
  per-track "cooked callback" slot, etc.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases used on the XNA API surface. It is under **separate, active, concurrent development** by
  another session — if a build ever fails inside `SHARP_RUNTIME/CMakeFiles/...`, check
  `git status`/`git log -1` there before assuming the audio code broke something.

---

## 2. Current status

- **Build:** clean. EasyGL backend (Linux default), `SOUND_ENABLED` on, SDL3_mixer linked.
  Verified via both the manual `cmake-build-debug/` directory and the `tests` CMake preset
  (freshly reconfigured from a deleted build directory). `cna_demo_sound`/`cna_demo_2d` example
  targets also rebuild clean.
- **Tests:** `CnaTests` whole-suite count is **3256 / 3258 pass** (2 skipped:
  `AccelerometerTests`/`GyroscopeTests`' `GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  hardware-dependent, expected) — up from 3250/3252 (`P9-3D-007`'s 6 new tests, pan formula
  coverage; before that, 3246/3248 was `P9-3D-004/005`'s 4 new tests, real Doppler pitch shift,
  3243/3245 was `P9-DYNAMIC-009`'s 3 new tests, closing `P9-DYNAMIC`'s full task list, 3242/3244
  was `P9-DYNAMIC-008`'s 1 new test, 3241/3243 was `P9-DYNAMIC-007`'s 1 new test, 3238/3240 was
  `P9-3D-003`'s 3 new tests, 3230/3232 was `P9-DYNAMIC-001..006`'s 8 new tests, 3226/3228 was
  `P9-XACT-014/015`'s 5 new tests, 3212/3214 was `P9-XACT-011`'s 14 new tests, and the earlier
  jump from 2102 was `develop`'s `feature/net` merge, not this branch's work). One unrelated
  statistical test (`CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically`) failed
  once in a full run but passed consistently over 5 isolated repeats -- pre-existing randomness
  in an un-seeded RNG-based test, not a regression. The audio-scoped subset (§7's `--gtest_filter`
  audio suite list) is **350 / 350 pass** under ASan+UBSan, up from 306.
  Also verified clean under a full ASan+UBSan build of the audio suite (`P9-XACT-011` touches the
  shared `FilterState` mixing-thread interaction flagged risky by `P9-BUILD-001..007`;
  `P9-XACT-014`/`P9-DYNAMIC-001`/`P9-DYNAMIC-007`/`P9-DYNAMIC-009` re-verified after their
  respective changes).
  `P9-DYNAMIC-007`'s fix lives in the sibling `../sharp-runtime` repo (`System::EventHandler<T>`,
  commit `8342a2c` there, not pushed) -- see §3.
  Re-run to check for drift: `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests`.
- **CLI/tools/apps:** none in the framework itself — this is a library/framework, not an
  application. `cna_demo_sound`/`cna_demo_2d` are example programs exercising the Audio API; they
  aren't part of `--target CnaTests` and are easy to forget to rebuild.
- **What works (Audio namespace):** `SoundEffect`/`SoundEffectInstance` (real SDL3_mixer playback,
  move-only instance-tracking Dispose cascade, real low/high/band-pass filters);
  `DynamicSoundEffectInstance` (real buffer queue via `SDL_AudioStream`); `AudioEngine`/
  `SoundBank`/`WaveBank`/`Cue` (real hand-written XACT parser, real category volume/pause/resume/
  stop, real natural-completion and authored-stop-tail state reconciliation, real 3D pan/
  attenuation); `Microphone` (real SDL3 capture). See `docs/xna-4-api-coverage.md`'s Audio
  compatibility table for the full implemented/approximate/unsupported breakdown.
- **What does not work / remains open:** the 5 open Fáze 9 groups (§4/§8) cover XACT RPC/DSP
  wiring, 3D audio fidelity beyond pan+attenuation, hardware/exception-path edge cases, and
  `DynamicSoundEffectInstance` buffer-lifecycle test coverage — none of these are known *bugs*,
  they're unaudited/unfinished hardening work per the user's own Fáze 9 scope.

---

## 3. Recent changes (most recent Fáze 9 groups, newest first)

- **`P9-3D-009`** (not yet committed) — `P9-3D`'s last remaining item, now closed (9/9).
  Consolidated summary write-up (new "`Apply3D` / 3D audio fidelity" subsection,
  `docs/xna-4-api-coverage.md`) covering all three of `Apply3D`'s positional effects: distance
  attenuation and Doppler are both **exact** closed-form matches for FAudio's `F3DAudio.c`
  formulas; pan is the one remaining **approximate** piece. While writing this up, found one
  genuinely new gap not previously documented: `Apply3D`'s pan is computed purely from
  world-space X displacement, **ignoring the listener's/emitter's `Forward`/`Up` orientation
  entirely** -- real X3DAudio computes azimuth relative to the listener's actual facing
  direction, so turning the listener around changes which side an emitter pans to in real
  XNA/FNA; CNA always pans as if the listener faces a fixed world axis. `Forward`/`Up` are
  stored (API-complete) but never read for panning (distinct from `Velocity`, now read for
  Doppler since `P9-3D-005`). Added a new `CHECKLIST.md` row for this finding. Read-only audit +
  documentation only -- no source/test changes, no build/test re-verification needed. Full
  detail: `plan_audio.md`'s `P9-3D-009` note.
- **`P9-3D-007`** (`567266a4`) — added panning test coverage. `P9-3D-001`'s audit already
  established SDL3_mixer has no `MIX_GetTrackStereo` getter, so `Apply3D`'s pan *result* can't be
  read back off the track (unlike gain/frequency-ratio, used for `P9-3D-003`/`P9-3D-005`).
  Extracted the pan formula (`dx/distance`, clamped) out of `Apply3D` into a new
  `SoundEffectInstance::INTERNAL_calculatePan` private static method (matching the
  `INTERNAL_calculateFilterCutoff` precedent from `P9-XACT-011`), exposed via
  `SoundEffectInstanceTestAccess::CalculatePan` for direct unit testing -- a pure refactor, no
  behavior change (all pre-existing `Apply3D*` tests pass unchanged). 6 new tests: emitter
  directly right/left/ahead-or-behind (this linear approximation only accounts for the X axis --
  documented, not a bug), a 45-degree diagonal, same position (avoids divide-by-zero), and
  out-of-geometric-range clamping. `git stash` confirms the test file fails to *compile*
  pre-refactor (no `CalculatePan` exists yet). Full suite 3256/3258 (2 expected hardware skips,
  plus one unrelated un-seeded-RNG statistical test flake, confirmed non-reproducing over 5
  isolated repeats), audio subset 350/350 under ASan+UBSan. Full detail: `plan_audio.md`'s
  `P9-3D-007` note.
- **`P9-3D-004/005`** (`ac9e92a9`) — audited Doppler against FNA's `UpdatePitch()`
  (`SoundEffectInstance.cs`) and FAudio's `F3DAudio.c` `CalculateDoppler`. Confirmed real Doppler
  is a **closed-form formula over `Position`/`Velocity`** (both already stored on
  `AudioListener`/`AudioEmitter`, just never read for this purpose -- the previous accepted
  deviation) needing **no native 3D audio API** -- unlike stereo crossfeed (`CP-19`) or true
  elevation/HRTF, `MIX_SetTrackFrequencyRatio` (already used for `Pitch`) is sufficient. Confirmed
  feasible and implemented: added `ComputeDopplerFactor` (matches `F3DAudio.c`'s formula exactly --
  project listener/emitter velocity onto the emitter-to-listener direction, clamp to
  `SpeedOfSound/DopplerScaler`, `DopplerFactor = (SpeedOfSound - DopplerScaler*listenerComp) /
  (SpeedOfSound - DopplerScaler*emitterComp)`, NaN-guarded to `1.0`, clamped to `[0.5, 4.0]`);
  `ApplyTrackProperties` gained a `doppler` multiplier parameter (default `1.0f`, every other
  caller unaffected); `Apply3D` computes it and multiplies it into the pitch-derived frequency
  ratio, gated by the global `SoundEffect.DopplerScale` (`0` disables it entirely, matching FNA).
  One-shot at `Apply3D()` call time, matching the same narrowing already accepted for
  distance-attenuation/pan (`P9-3D-003`) -- a real game calls `Apply3D()` every frame. Verified
  with 4 new tests via real `MIX_GetTrackFrequencyRatio` readback (SDL3_mixer has a getter, unlike
  stereo pan): receding emitter (dopplerFactor = 2/3, hand-derived and independently confirmed),
  approaching emitter (2.0), approaching listener (1.5), and `DopplerScale=0` disabling it
  entirely. `git stash` confirms 3 of the 4 fail pre-fix with the exact "no Doppler" values the
  old code produces. Also synced `CHECKLIST.md`/`docs/coverage.md`/`docs/xna-4-api-coverage.md`
  (previously said "Doppler stored but never applied," now real). Full suite 3250/3252 (2 expected
  skips), audio subset 344/344 under ASan+UBSan. Full detail: `plan_audio.md`'s `P9-3D-004/005`
  notes.
- **`P9-DYNAMIC-009`** (`308e542b`) — `P9-DYNAMIC`'s last remaining group, now closed
  (9/9). Read FAudio's buffer submission path via FNA's `SubmitBuffer`: FNA has **no
  block-alignment validation at all** -- `FAudioBuffer.PlayLength = AudioBytes / channels /
  bytesPerSample` truncates via plain integer division for a non-frame-aligned byte count, never
  throws. CNA's `SubmitBuffer`/`SubmitFloatBufferEXT` already match this exactly, and CNA's
  byte-oriented bookkeeping (`queuedBuffers_`/`submittedChunkSizes_`, matching
  `SDL_PutAudioStreamData`/`SDL_GetAudioStreamQueued`'s own byte-oriented API) means alignment
  doesn't enter into CNA's tracking at all -- no bug to find, only coverage to add. Added 3 tests:
  a non-frame-aligned byte count (63 bytes, not a multiple of the 4-byte stereo frame) via
  `SubmitBuffer` while stopped and while actually playing (device-dependent, confirmed clean
  under ASan+UBSan), and a sample count not divisible by channel count via
  `SubmitFloatBufferEXT`. All three confirm the same no-op-validation behavior FNA has: no throw,
  `PendingBufferCount` increments by exactly 1 regardless of alignment. Full suite 3246/3248 (2
  expected skips), audio subset 339/340 under ASan (1 pre-existing, unrelated self-skip). Full
  detail: `plan_audio.md`'s `P9-DYNAMIC-009` note.
- **`P9-DYNAMIC-008`** (`5e6818d1`) — audited dynamic stream format conversion
  (`EnsureStream()`/`SubmitBuffer`/`SubmitFloatBufferEXT`) against FNA's `FAudioWaveFormatEx`
  derivation. **No new bug found** -- confirmed correct on every point checked: `AudioChannels`
  enum values, format-tag switching guards, byte-vs-sample-count units in both submit overloads,
  and the channel-count divide/multiply in `GetSampleDuration`/`GetSampleSizeInBytes` (already
  bit-for-bit identical to FNA's formula). One confirmed **shared quirk, not a CNA-specific bug**:
  neither FNA's nor CNA's `SubmitBuffer` (byte/int) guards against being called while the instance
  is already in float mode and playing -- matching FNA's equal permissiveness here is correct per
  `CLAUDE.md` ("match XNA/FNA behavior"), not something to newly guard against. Found one minor
  test-coverage gap (not a bug): the existing `SampleDurationRoundTrip` only exercised Stereo;
  added `SampleDurationRoundTripMono` to independently exercise the channel-count divisor. Full
  suite 3243/3245 (2 expected skips), audio subset 336/336. Full detail: `plan_audio.md`'s
  `P9-DYNAMIC-008` note.
- **`P9-DYNAMIC-007`** (`87675e75`; sharp-runtime fix committed there as `8342a2c`,
  not pushed) — writing a test for "subscriber removes itself during `BufferNeeded`'s callback"
  (a common "handle once" event pattern) uncovered a **real, serious, cross-cutting bug** well
  beyond Audio: `System::EventHandler<T>::Raise()` (`../sharp-runtime`, shared by every event in
  the whole framework) iterated its live handler vector directly, so a handler removing itself or
  another handler mid-callback mutated that same vector mid-iteration -- confirmed via an isolated
  standalone repro (kept outside the shared `CnaTests` binary to avoid risking a process-wide
  crash) that this escaped as a real, uncaught `std::bad_function_call`, not a theoretical
  concern. Per `CLAUDE.md`'s "don't touch the sibling repo without asking" rule (`sharp-runtime`
  was also under concurrent development by another session at the time -- confirmed via its git
  log/status before and after), surfaced the finding and asked the user how to proceed; the user
  chose to fix it now. Fixed in `sharp-runtime` by taking a snapshot copy of the handler list
  before iterating in `Raise()`, matching real C# multicast delegate semantics (a handler that
  `Add()`s/`Remove()`s/`Clear()`s during `Raise()` only affects the *next* `Raise()`, not the one
  in progress). Verified sharp-runtime's own full suite (9075 tests) stayed green including the
  other session's concurrent, unrelated, uncommitted changes; committed only the 2 files this fix
  touched there, left the other session's in-progress file alone, did not push. Back in CNA: added
  `BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing`
  (`DynamicSoundEffectInstanceTests.cpp`). Full suite 3242/3244 (2 expected skips), audio subset
  335/335, clean under ASan+UBSan. Full detail: `plan_audio.md`'s `P9-DYNAMIC-007` note.
- **`P9-3D-003`** (`a86f4d6e`) — audited distance attenuation against FAudio's `F3DAudio.c`
  `ComputeDistanceAttenuation` (the function real `F3DAudioCalculate` uses for every XNA/FNA
  `AudioEmitter`, none of which set a custom volume curve). Found a **real, confirmed bug**: FAudio's
  no-custom-curve formula is full volume (zero attenuation) for any distance *within*
  `DistanceScale`, with inverse-distance falloff (`gain = DistanceScale/distance`) only strictly
  beyond it. CNA's `Apply3D` instead used a continuously-falling-off `1/(1+distance/scale)`
  formula with no "safe radius" at all -- already at half volume exactly at `distance ==
  DistanceScale`, where real XNA/FNA is still at full volume. This made every 3D-positioned sound
  play measurably quieter than real XNA at every distance, not a cosmetic approximation gap.
  Fixed with FAudio's exact formula. Verified with 3 new tests via `MIX_GetTrackGain` (SDL3_mixer
  *does* have a gain getter, unlike the stereo-pan case `P9-3D-001` found had none) at
  0.5x/1.0x/2.0x `DistanceScale`; `git stash` confirms all 3 fail against the pre-fix formula with
  the exact wrong values it predicts. Full suite 3241/3243 (2 expected skips), audio subset
  335/335, clean under ASan+UBSan. Full detail: `plan_audio.md`'s `P9-3D-003` note.
- **`P9-3D-001/002`** (`88703d53`) — audited `Apply3D` against FNA's `SoundEffectInstance.cs`
  for mono/stereo source behavior. Key finding: FNA's `Apply3D` does **not** use the same
  `SetPanMatrixCoefficients` formula the `Pan` property setter uses -- it's guarded by
  `if (is3D) return;` and skipped entirely once `Apply3D` has run. `Apply3D` instead computes its
  matrix via the native X3DAudio `F3DAudioCalculate` call, a completely different,
  channel-count-aware code path. CNA's `Apply3D`, by contrast, calls the exact same
  `ApplyTrackProperties` formula the `Pan` setter also uses, with zero source-channel-count
  awareness anywhere in the method -- so a stereo source gets identical treatment to a mono
  source under `Apply3D`, always. For mono sources this happens to coincide with FNA's answer
  (already established bit-exact by `CP-19`); for stereo sources, this is the *exact same* root
  limitation `CP-19` already found and the user already discussed for the `Pan` property (SDL3_mixer's
  `MIX_StereoGains` has no crossfeed API) -- `Apply3D` just reaches it via a different call site.
  No new fix (same accepted deviation, not re-litigated) and no new test (every existing `Apply3D`
  test already runs against the shared fixture's stereo `SoundEffect`; SDL3_mixer has no gain
  readback API to verify exact values, so a new test would only re-assert "does not throw," already
  covered). `CHECKLIST.md`'s `CP-19` row now explicitly names `Apply3D` alongside `Pan`. Read-only
  audit -- no source changed. Distance attenuation's formula shape vs FNA's X3DAudio default
  linear curve is a separate deviation, out of this task's scope (`P9-3D-003`). Full detail:
  `plan_audio.md`'s `P9-3D-001/002` notes.
- **`P9-DYNAMIC-001..006`** (`4d78b139`) — audited `DynamicSoundEffectInstance`'s
  `PendingBufferCount` transitions against FNA's `DynamicSoundEffectInstance.cs`/
  `SoundEffectInstance.cs`. Found and fixed **two real bugs**: (1) `Play()` only called `Update()`
  from its "Stopped, start fresh" branch, after already returning early for Paused/Playing --
  FNA's `Play()` calls `Update()` unconditionally before dispatching on state, so a redundant
  `Play()` call while *already Playing* should still pump `Update()` (submit freshly queued data,
  fire `BufferNeeded` if starved), which CNA silently skipped. Fixed by moving the `Update()` call
  to the top of `Play()`, matching FNA's structure exactly (no dedicated test -- proving it
  deterministically needs either invasive call-count instrumentation or a real elapsed-time
  buffer-consumption window, the same timing-flakiness risk already guarded against elsewhere on
  this branch). (2) A cleanly-testable bug: `Stop()` (no-arg) duplicated `Stop(bool)`'s clearing
  logic directly instead of delegating through it (`{ Stop(true); }`, matching the base class and
  FNA exactly) -- so it skipped `Stop(bool)`'s "no active track yet -> no-op" guard, meaning a
  direct `Stop()` call on a never-played instance with staged buffers cleared
  `PendingBufferCount` to 0 in CNA where FNA leaves it untouched. Fixed by extracting the
  unconditional-clearing logic into a private `StopInternal()` (called only from `Stop(bool)`'s
  already-guarded branch) and making `Stop()` itself just delegate. 8 new tests covering
  PendingBufferCount across Play/Pause/Resume/Stop/Dispose, multiple `BufferNeeded` subscribers,
  and exact starvation-count firing; verified via `git stash` that the `Stop()` fix's regression
  test genuinely fails pre-fix. Full suite 3238/3240 (2 expected skips), audio subset 331/331,
  clean under ASan+UBSan. `P9-DYNAMIC-007` (subscriber removal during callback), `-008` (stream
  format conversion audit), and `-009` (invalid buffer size/alignment, partially covered
  pre-existing) remain open. Full detail: `plan_audio.md`'s `P9-DYNAMIC-001..006` notes.
- **`P9-HARDWARE-001/002`** (`85b8ce86`) — audited `NoAudioHardwareException` usage
  (confirmed the pre-existing suspicion: type-only stub, never thrown) and found a real bug:
  `AudioMixer.cpp`'s `GetMixer()` throws a raw `std::runtime_error` -- never caught anywhere --
  on the exact "no audio hardware" failure FNA's `SoundEffect.Device()` throws
  `NoAudioHardwareException` from, directly violating `CLAUDE.md`'s "no raw `std::` exceptions on
  the XNA surface" rule. Fixed with a small `GetMixerOrThrowXna()` conversion helper wired into
  every entry point that can be the *first* `GetMixer()` call for the whole process:
  `SoundEffect`'s two audio-loading constructors, `SoundEffect::FromStream()`,
  `SoundEffect::get/setMasterVolumeProperty()` (static properties FNA's own `MasterVolume`
  routes through the identical throw site), and `DynamicSoundEffectInstance::Play()` (its
  constructor, unlike FNA's, never touches the mixer at all). `SoundEffect::Play()`'s own
  `GetMixer()` call is deliberately left unwrapped -- provably unreachable as a first-failure
  site. No new automated test: the shared test binary's process-wide `GetMixer()` cache makes the
  actual failure path untestable without a fresh, isolated process (`SDL_AUDIODRIVER=dummy`
  always trivially succeeds) -- deferred to `P9-HARDWARE-005`, which already scopes this
  ("where feasible"). Manually verified: full suite 3230/3232 (unchanged, no regressions), audio
  subset 324/324, clean under ASan+UBSan. `AudioEngine::Init()` never querying real hardware at
  all (so it can never throw from the constructor the way FNA's does) is a separate, larger
  design question left to `P9-HARDWARE-003`. Full detail: `plan_audio.md`'s `P9-HARDWARE-001/002`
  notes.
- **`P9-XACT-014/015`** (`b718a8d1`) — `P9-XACT`'s last remaining group, now closed
  (15/15). Audited `SoundBank::GetCue`/`Cue::Play()` against FNA's `SoundBank.cs`/FAudio's
  `FACT_internal.c`. Invalid-cue-*name* handling already matched FNA exactly (pre-existing).
  Found and fixed a **real bug** in the different case the task targets: an internal
  sound-code-to-index resolution that only corrupt/malformed content can trigger (real
  XACT-tool-built content's codes always resolve). `XactParser.cpp` had 5 sites where an
  unresolvable sound code silently fell back to `soundIndex = 0` — **aliasing onto whichever
  sound happens to be first in the bank and playing it**, instead of resolving to "no sound
  found" (the correct behavior `Cue::Play()` already had for a genuinely out-of-range index).
  Fixed with a new `kInvalidSoundIndex = 0xFFFFFFFFu` sentinel at all 5 sites — relies entirely on
  `Cue::Play()`'s existing bounds checks, no consumer code needed changing. Confirmed via `git
  stash` that a real (non-null) `SoundEffectInstance` actually got spawned playing the wrong sound
  pre-fix — a genuine "wrong audio plays" defect, not theoretical. Missing/unregistered wave bank
  and out-of-range wave index within a real wave bank were both already correct pre-existing but
  untested — added 3 new end-to-end `CueTest`s covering "unresolvable sound code,"
  "unregistered wave bank name," and "out-of-range wave index," plus 1 parser-level test. Full
  suite 3230/3232 (2 expected skips), audio subset 324/324, clean under ASan+UBSan. Full detail:
  `plan_audio.md`'s `P9-XACT-014/015` notes.
- **`P9-XACT-011/012/013`** (`4d2246bd`) — real per-track XACT filter wiring, the wiring
  the `P9-XACT-010` audit scoped out. `XactTypes.hpp`'s `XsbWaveRef` gained
  `filterType`/`filterFrequencyHz`/`filterQFactorRaw` (default `filterType=0xFF` sentinel);
  `XactParser.cpp` now retains the per-track `filterData`/`frequency` bytes it used to discard,
  replicating FAudio's exact (band-pass-unreachable) bit-decode. Per the user's explicit choice
  (asked before implementing, since it touches a private-API signature): `SoundEffectInstance::
  INTERNAL_apply{Low,High,Band}PassFilter` gained a real `oneOverQ` parameter (default `1.0f`,
  every existing caller unaffected) instead of narrowing to a fixed Q — real Q fidelity for
  XACT-driven playback. Two pure static helpers (`INTERNAL_calculateFilterCutoff`/
  `INTERNAL_calculateFilterOneOverQ`) do FAudio's exact Hz->normalized-cutoff and
  qfactor->OneOverQ conversions, independently unit-tested; the new `INTERNAL_
  applyXactTrackFilter(filterType, frequencyHz, qfactorRaw)` (NOXNA, `friend class Cue`) queries
  the live SDL3_mixer sample rate and dispatches to the right filter method.
  `Cue::Play()` calls it once per spawned instance when a track has filter data — same
  one-shot-at-`Play()` narrowing already accepted for RPC volume/pitch. Sound-level
  `SOUND_FLAG_HAS_DSP`'s reverb-send no-op stays as-is (confirmed infeasible, `P9-XACT-012`);
  `CHECKLIST.md` documents the new accepted narrowings (`P9-XACT-013`). 14 new tests across
  `XactParserTests.cpp`/`SoundEffectInstanceTests.cpp`/`CueTests.cpp` (parser retention + bit-decode,
  pure conversion math, dispatch/guards, and a new real-WaveBank-backed end-to-end `CueTest`
  fixture); verified via `git stash` (all 5 production files) that every new test fails to
  *compile* pre-fix. Full suite 3226/3228 (2 expected skips), audio subset 320/320, also clean
  under ASan+UBSan. Full detail: `plan_audio.md`'s `P9-XACT-011/012/013` notes.
- **`P9-XACT-010`** (read-only audit) — audited XACT "DSP/filter" data against
  `FAudio`'s `FACT_internal.c` (the only available byte-level reference, since FNA's C# layer never
  parses XACT content itself). Two distinct concepts were conflated under the task name: (1)
  sound-level `SOUND_FLAG_HAS_DSP`/`dspCodes` is actually a **reverb-send enable flag** in real
  FACT — the DSP preset code *values* are parsed but never read back anywhere in FAudio either,
  only used as `dspCodeCount > 0` to route the wave to an aux reverb bus SDL3_mixer doesn't have —
  confirming the reverb no-op (`P9-XACT-012`) should stay as-is; (2) the real low/high/band-pass
  filter data is a **separate** per-track field (`filterData:u16` + `frequency:u16`) that CNA's
  parser already reads-and-discards correctly. Found a likely-genuine upstream FAudio bit-decode
  quirk (`(filterData>>1)&0x02` structurally can't select `BandPass`) to replicate as-is, not "fix,"
  if wired. Full citations and the `INTERNAL_apply*Filter` feasibility analysis (frequency-only
  signature matches FNA's own public API; the real per-track `qfactor`/`OneOverQ` has no home in it
  yet) are in `plan_audio.md`'s `P9-XACT-010` note. No source changed — audit only.
- **`P9-XACT-001..009`** (`8800254`) — two independent fixes to data that was
  parsed but never used. (1) Interactive (`type==3`) variation tables now select an entry by
  finding which one's `[varMin, varMax]` contains the bound variable's current value (matching
  FAudio's `get_active_variation_index`), instead of falling back to a uniform random pick —
  `XsbVariEntry` gained `varMin`/`varMax` fields the parser used to read and discard. (2) RPC
  (Runtime Parameter Control) volume/pitch curves are now parsed from the `.xgs` (previously not
  parsed at all — `rpcOffset`/`rpcCount` weren't even read from the header) and from each `.xsb`
  sound's RPC code list (previously read only far enough to skip past), and evaluated once at
  `Cue::Play()` time against the bound variable's current value (**not** continuously
  re-evaluated while playing, unlike real FACT's per-tick update — documented gap in
  `CHECKLIST.md`). DSP/filter wiring (`P9-XACT-010..013`) and missing-wave/cue fidelity
  (`P9-XACT-014/015`) remain open.
- **`P9-STOP-001..010`** (`bd1c932`/`e4ffb1e`) — fixed a real bug: `Cue::Stop(AsAuthored)` used to
  mark a cue fully `Stopped` and unregister it from `AudioEngine`/`WaveBank` immediately, even
  while its release tail was still audibly playing. Now uses a `State::Stopping` transitional
  state (the enum value already existed, unused) that `ReconcileState()` promotes to `Stopped`
  once the tail actually finishes — same mechanism already used for natural-completion detection.
- **`P9-BUILD-001..007`** (`47a58f5`/`1cb2b75`) — added `CMakePresets.json`'s first native desktop
  preset (`tests`). While verifying "all tests pass from a clean checkout," found and fixed a real
  data race in 3 DSP-filter tests (the real SDL3_mixer mixing thread racing a test's synchronous
  filter-processing calls on shared state) — confirmed by a ~15–25% failure rate over repeated
  runs, fixed by stopping the track before the manual test loop.
- **`P9-DOCS-001..007`** (`7bec360`/`cdec800`) — synchronized `AUDIT.md`, `docs/xna-4-api-
  coverage.md`, and `docs/coverage.md` against real current code (several claims predated this
  branch's work entirely, describing implemented XACT/Microphone/DynamicSoundEffectInstance
  functionality as unimplemented stubs); added a consolidated Audio compatibility table.
- **`P9-VALIDATION-001..015`** (`8f439dd`/`2463eed`) — found and fixed the most serious bug of
  Fáze 9: `offset+count` validated as a plain `int32` addition in `SoundEffect`'s buffer/range
  constructor and `DynamicSoundEffectInstance::SubmitBuffer`/`SubmitFloatBufferEXT` could overflow
  and wrap negative, bypassing the bounds check entirely — **confirmed by a real segfault**, not
  just a failing assertion. Fixed with overflow-safe unsigned arithmetic at all three sites.
- **`P9-CATEGORY-001..004`** (`9039ec6`/`1733e28`) — found and fixed a real bug: `AudioEngine::
  StopCategoryInternal` iterated `activeCues` directly while `Cue::Stop()` cascades into
  `UnregisterCue()`, erasing from that same vector mid-iteration — silently skipped stopping a cue
  whenever 3+ cues shared a category. Fixed by snapshotting before iterating.
- **`P9-LIFECYCLE-001..015`** (`5f3e5d0`..`c5f50c9`) — the priority bug for Fáze 9: `Cue::IsPlaying`/
  `IsPaused`/`IsStopped` never reconciled after natural playback completion, staying `Playing`
  forever. Fixed with `Cue::ReconcileState()`, a live per-call reconciliation against each active
  instance's real SDL3_mixer state.

Full commit-by-commit detail, FNA/FAudio source citations, and `git stash` verification notes for
every item above: `plan_audio.md`'s "Phase 9" section.

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists.** The build is clean and all tests pass reliably
(§2). This is **not** a "nothing left to do" branch state, though — Fáze 9 (a user-directed,
already-scoped task list) still has open work:

- `P9-XACT` — **fully closed (15/15)**, see §3.
- `P9-3D` — **fully closed (9/9)**, see §3.
- `P9-HARDWARE` (2/6 done, see §3) — `NoAudioHardwareException` audit and the
  `std::runtime_error`-vs-XNA-exception-type fix are done; still open: whether missing/corrupt
  XGS/XSB/XWB constructors should keep silently stubbing or throw (`P9-HARDWARE-003`, a separate,
  larger decision from the exception-*type* fix already made — see `CHECKLIST.md`'s existing
  `CP-18`/`XA-9` accepted-deviation entries), any consequent test updates (`-004`), no-audio-device
  test coverage (`-005`, needs a fresh isolated process — `SDL_AUDIODRIVER=dummy` always trivially
  succeeds in this repo's shared test binary), and documentation (`-006`).
- `P9-DYNAMIC` — **fully closed (9/9)**, see §3.
- `P9-AUDIT` — the original "fresh-read audit" deliverable (a formal per-file comparison write-up)
  was never produced as its own artifact; the reading needed to fix the other groups happened ad
  hoc. Optional/lowest priority; was never in the user's explicit implementation order.

Two genuine **open decisions** (not tasks) are recorded in `CHECKLIST.md` and require the user's
input before implementing either way:
1. `Cue::IsPlaying`/`IsPaused` are mutually exclusive in CNA; real FACT lets both be `true`
   simultaneously (pausing never clears the `PLAYING` bit). Fixing it would touch
   `AudioEngine::PauseCategoryInternal`/`ResumeCategoryInternal` and existing tests.
   (`P9-LIFECYCLE-013`)
2. `Cue::Stop(AsAuthored)`'s release-tail *duration* is however long the wave naturally takes, not
   an authored `fadeOutMS`/RPC-release curve — `XactParser` doesn't retain per-cue fade timing at
   all. Fixing it would need parser changes plus a new time-driven update mechanism. (`P9-STOP-010`)

(A third open decision — real per-track XACT filter `OneOverQ` fidelity vs. a fixed-Q narrowing —
was asked and resolved in favor of real fidelity; see `P9-XACT-011` in §3. `INTERNAL_apply{Low,
High,Band}PassFilter` now takes a real `oneOverQ` parameter.)

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, under separate, active, concurrent development by another session. A build
failure inside `SHARP_RUNTIME/CMakeFiles/...` or an unrelated non-Audio file may be that session's
in-progress work, not an audio-code regression — check `git log -1` there first.

**Dependency note (`P9-DYNAMIC-007`):** `DynamicSoundEffectInstanceTests.cpp`'s
`BufferNeededSubscriberCanRemoveItselfDuringCallbackWithoutCrashing` test depends on
`sharp-runtime` commit `8342a2c` (`System::EventHandler<T>::Raise()`'s snapshot-before-iterating
fix). That commit exists in the local `../sharp-runtime` checkout but has **not been pushed**
(a sibling shared branch under concurrent development). If a fresh clone/pull of `sharp-runtime`
ever lacks this commit, that one CNA test will fail (or, pre-fix, could throw
`std::bad_function_call`) — not an audio-code regression.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Confirmed, fixed** | `Cue::IsPlaying`/`IsPaused`/`IsStopped` never reconciled after natural completion | `P9-LIFECYCLE-001..009` |
| **Confirmed, fixed** | `offset+count` int32-overflow → real segfault in `SoundEffect` ctor / `DynamicSoundEffectInstance::SubmitBuffer`/`SubmitFloatBufferEXT` | `P9-VALIDATION-003/010/011` |
| **Confirmed, fixed** | `AudioEngine::StopCategoryInternal` mutate-during-iteration bug, silently skipped stopping cues | `P9-CATEGORY-001/002` |
| **Confirmed, fixed** | `Cue::Stop(AsAuthored)` marked a cue fully stopped/unregistered while its tail was still playing | `P9-STOP-002..005` |
| **Confirmed, fixed** | `SoundEffectInstance`/`DynamicSoundEffectInstance::Resume()` didn't call `Play()` when never-started/disposed (FNA does) | `P9-VALIDATION-010` |
| **Confirmed, fixed** | An unresolvable cue/variation-entry sound code (corrupt/malformed data) silently aliased onto sound index 0 and played the wrong sound, instead of resolving to "no sound found" | `P9-XACT-014` |
| **Confirmed, fixed** | `GetMixer()`'s no-audio-hardware failure threw a raw `std::runtime_error`, never `NoAudioHardwareException`, through public XNA entry points (`SoundEffect` ctors/`FromStream`/`MasterVolumeProperty`, `DynamicSoundEffectInstance::Play()`) | `P9-HARDWARE-002` |
| **Confirmed, fixed** | `DynamicSoundEffectInstance::Play()` skipped `Update()`'s buffer-refill pump when called redundantly while already `Playing` (FNA calls `Update()` unconditionally at the top of `Play()`) | `P9-DYNAMIC-001` |
| **Confirmed, fixed** | `DynamicSoundEffectInstance::Stop()` (no-arg) cleared `PendingBufferCount` even on a never-played instance, skipping `Stop(bool)`'s "no active track -> no-op" guard (FNA's `Stop()` is exactly `Stop(true)`, inheriting the guard) | `P9-DYNAMIC-001` |
| **Confirmed, fixed** | `Apply3D`'s distance attenuation fell off continuously from distance 0 (`1/(1+distance/scale)`), already at half volume exactly at `distance == DistanceScale`, instead of FAudio's real formula: full volume within `DistanceScale`, inverse-distance falloff only beyond it | `P9-3D-003` |
| **Confirmed, implemented** | Real Doppler pitch shift via `Apply3D` (closed-form formula matching FAudio's `CalculateDoppler` exactly) -- previously `DopplerScale`/`Velocity` were stored but never applied to pitch at all | `P9-3D-004/005` |
| **Accepted deviation** | `Apply3D`'s pan ignores listener/emitter `Forward`/`Up` orientation entirely (world-space X displacement only) -- real X3DAudio computes azimuth relative to the listener's actual facing direction; `Forward`/`Up` are stored but never read for panning. Newly found, not previously documented | `CHECKLIST.md`, `P9-3D-009` |
| **Confirmed, fixed (in `../sharp-runtime`)** | `System::EventHandler<T>::Raise()` iterated its live handler list directly -- a handler removing itself or another handler mid-callback (a common "handle once" pattern) dereferenced an already-destroyed `std::function`, observed as an escaping `std::bad_function_call`. Affects every event in the framework, not just Audio's `BufferNeeded` | `P9-DYNAMIC-007`, sharp-runtime commit `8342a2c` (not pushed) |
| **Accepted deviation** | `IsPlaying`/`IsPaused` mutually exclusive, unlike real FACT — decision pending | `CHECKLIST.md`, `P9-LIFECYCLE-013` |
| **Accepted deviation** | Authored-stop tail duration ≠ real `fadeOutMS` curve (not parsed/retained at all) | `CHECKLIST.md`, `P9-STOP-010` |
| **Accepted deviation** | RPC volume/pitch curves evaluated once at `Play()` time, not continuously re-evaluated while playing (no per-frame `Cue` update tick exists) | `CHECKLIST.md`, `P9-XACT-005/006/007` |
| **Confirmed, implemented** | Per-track XACT low/high/band-pass filter (frequency + Q, real) now wired at `Cue::Play()` time — see §3; still one-shot (no continuous per-tick re-eval) and not RPC-filter-frequency/Q overridable | `CHECKLIST.md`, `P9-XACT-011` |
| **Accepted deviation** | A parsed per-track filter's type can only ever decode to low-pass or high-pass, never band-pass — replicates a likely-genuine upstream FAudio bit-decode quirk as-is | `CHECKLIST.md`, `P9-XACT-010/011` |
| **Accepted deviation** | Stereo hard-pan eliminates the opposite channel instead of crossfeed-blending it — same limitation whether reached via the `Pan` property or `Apply3D` (confirmed identical formula, `P9-3D-001`); FNA's `Apply3D` uses a completely different, channel-count-aware X3DAudio computation instead | `CHECKLIST.md`, `CP-19`, `P9-3D-001` |
| **Accepted deviation** | No 3D HRTF/elevation — pan + distance-attenuation + real Doppler only | `CHECKLIST.md` |
| **Accepted deviation** | Reverb is a documented no-op (`INTERNAL_applyReverb`) — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md`, `T-4C` |
| **Accepted deviation** | XACT category `instanceLimit`/`fadeInMS`/`fadeOutMS` parsed but never enforced | `CHECKLIST.md`, `XA-11` |
| **Accepted deviation** | `AudioEngine`/`SoundBank`/`WaveBank` silently stub instead of throwing on a missing/corrupt file (decision pending, `P9-HARDWARE-003`); `AudioEngine::Init()` never queries real hardware, so it can never throw `NoAudioHardwareException` from the constructor the way FNA's does (`SoundEffect`/`DynamicSoundEffectInstance` *can* now, since `P9-HARDWARE-002`) | `CHECKLIST.md`, `CP-18`/`XA-9` |
| **Needs verification** | `SoundEffectInstance` filter coefficient locking follows SDL3_mixer's documented practice but was never stress-tested under real concurrency (no ThreadSanitizer run) | `T-4C` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver here; real-hardware runs are manual/ad-hoc | — |
| **Incomplete** | `P9-HARDWARE`/`P9-AUDIT` — see §4 (`P9-XACT`/`P9-DYNAMIC`/`P9-3D` are fully closed) | `plan_audio.md` |

Full list with FNA/FAudio line citations and verification notes: `plan_audio.md`.

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
    Stopping→Stopped (authored-stop tail finishing), queried live on every state getter
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
- **`Cue::ReconcileState()` only mutates `active_`/`state_`, never `waveBanksUsed_`/`AudioEngine`'s
  registries** — it runs from `const` state getters that may be called mid-iteration over those
  other registries elsewhere (e.g. `WaveBank::getIsInUseProperty()`). Actual unregistration only
  happens from `StopInternal()` (explicit `Stop(Immediate)`/`Dispose()`) or `SoundBank::
  SweepFireAndForget()`.
- **All four `AudioEngine` category operations snapshot `activeCues` before iterating** — don't
  revert to live iteration; `Cue::Stop()` cascades into `UnregisterCue()`, which erases from that
  same vector.
- **Never validate a byte-range as `offset + count > buffer.size()` in `intcs` (int32)** — this
  caused a real segfault. Use the unsigned-arithmetic pattern at all three existing call sites.
- **`Cue::State::Stopping`** models an authored-stop cue whose real tail is still playing; don't
  collapse it back into `Stopped` synchronously.
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

# Run only the audio test suites
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='*SoundEffect*:*Dynamic*:*AudioEmitter*:*AudioListener*:*SoundState*:*AudioChannels*:*AudioStopOptions*:*MicrophoneState*:*PlayLimit*:*NoAudio*:*NoMicrophone*:*Audio*:*Cue*:*WaveBank*:*SoundBank*:*XactParser*'

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

Fáze 9's own task list (`plan_audio.md`) is the source of truth; the user's explicit implementation
order is exhausted through `P9-STOP`, `P9-XACT` is **fully closed (15/15)**, `P9-DYNAMIC` is
**fully closed (9/9)**, `P9-3D` is **fully closed (9/9)**, and `P9-HARDWARE` is 2/6 done. The
remaining groups have no user-specified priority among them — suggested order below is by
"smallest independently-verifiable slice first":

1. **Decide missing/corrupt XGS/XSB/XWB constructor behavior** (`P9-HARDWARE-003`). Goal: decide
   whether `AudioEngine`/`SoundBank`/`WaveBank` should keep silently stubbing on a missing/corrupt
   file (current behavior, `CHECKLIST.md` `CP-18`/`XA-9`) or throw -- a genuine open decision, not
   a clear-cut fix like `P9-HARDWARE-002` was, since ~80+ existing tests build on the current
   `SharedEngine()`-style stub-on-missing-file fixtures (see `CHECKLIST.md`'s existing note on
   `CP-18`). If changed, `P9-HARDWARE-004` updates the tests that lock in today's stub behavior.
   Needs the user's input before implementing either way.
2. **Add no-audio-device test coverage** (`P9-HARDWARE-005`). Goal: a real regression test for
   `GetMixer()`'s failure path (`P9-HARDWARE-002`) needs a fresh, isolated process with an invalid
   `SDL_AUDIODRIVER` set before anything else calls `GetMixer()` -- this repo's shared `CnaTests`
   binary can't exercise it (the mixer's cache is process-wide and once-ever-initialized). May
   need a small standalone test executable (precedent: `cna_net_two_process_harness`). "Where
   feasible" per the task's own wording -- confirm feasibility first.

Each task, once implemented: add/extend tests, verify with the `git stash` pattern (§7) for any
behavioral fix, run ASan+UBSan if it touches memory lifetime or ownership, update `plan_audio.md`'s
checkbox + `*Note:*`, then update this file and commit.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit.** Fáze 7 and Fáze 8 already did two
  rounds of that. Fáze 9 is a different, already-scoped hardening pass — don't invent a "Fáze 10".
- **No implementing either of the two open decisions in §4** (`IsPlaying`/`IsPaused` coexistence;
  authored-stop fade-curve timing) **without asking the user first** — both would need real
  feature work or touch already-shipped shared infrastructure. (A third such decision, XACT filter
  `OneOverQ` fidelity vs. narrowing, was asked and resolved — see `P9-XACT-011`.)
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No real 3D HRTF, Doppler, or reverb implementation** — SDL3_mixer cannot do it; keep as
  documented accepted deviations unless the user explicitly asks to revisit the backend choice.
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos. If a build
  breaks there, check whether it's the known concurrent-development hazard (§4) first.
- **No API renames / namespace moves** — XNA names are frozen.
- **No broad refactors or unrelated cleanup** — every fix on this branch has been a small,
  targeted change plus its own regression test; keep it that way.

---

## 10. Resume prompt

```
Read NEXT.md first. Fáze 9 (a user-directed, already-scoped hardening pass) has 9 of 11 task
groups fully closed (P9-LIFECYCLE, P9-CATEGORY, P9-VALIDATION, P9-DOCS, P9-BUILD, P9-STOP,
P9-XACT, P9-DYNAMIC, P9-3D -- P9-XACT's own 15-item, P9-DYNAMIC's own 9-item, and P9-3D's own
9-item lists are now all fully done; P9-DYNAMIC uncovered/fixed a cross-cutting
System::EventHandler<T> bug in ../sharp-runtime -- see §3/§4's dependency note; P9-3D found/fixed
a real distance-attenuation bug, implemented real Doppler pitch shift, and documented a newly
found pan-orientation gap), P9-HARDWARE is 2/6 done (the NoAudioHardwareException audit + its
std::runtime_error-vs-XNA-exception-type fix), and 1 group is fully open (P9-AUDIT) -- see §4/§8.
No known build/test blocker.

1. Confirm current state matches NEXT.md §2 (build clean, whole-suite 3256/3258 pass, audio-scoped
   subset 350/350 under ASan+UBSan) -- rebuild and rerun SDL_AUDIODRIVER=dummy
   ./cmake-build-debug/CnaTests (or the `tests` CMake preset, §7) to check for drift since this
   was last updated. If a test involving BufferNeeded/EventHandler fails unexpectedly, check
   whether ../sharp-runtime still has commit 8342a2c (§4's dependency note) before assuming an
   audio regression.
2. Inspect only the files needed for the first §8 task (P9-HARDWARE-003: missing/corrupt file
   constructor behavior decision) unless the user names something else -- don't refactor
   unrelated code. This is a genuine open decision, not a clear-cut fix -- ask the user before
   implementing either way.
3. Make one small, verified improvement: if it's an audit, write the finding into plan_audio.md;
   if it's a fix, add/extend a test, verify with the git-stash pattern (§7), run the relevant
   build/test command, and run ASan+UBSan if it touches memory lifetime or ownership.
4. Update plan_audio.md's checkbox + note, then update this NEXT.md (status, recent changes, next
   task) to reflect what changed, and commit.
```
