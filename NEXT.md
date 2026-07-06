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
  gated on a new design decision -- see §8 for what's running/next; a genuinely new "Phase 11"
  scope is intentionally NOT started, since defining new task scope is exactly the kind of
  decision this session is deferring to the user's return (§9).
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is parsed
  by a hand-written `CNA::Internal::Audio::XactParser` and mixed through SDL_mixer. This backend
  choice is the root cause of every documented deviation from FNA (see `CHECKLIST.md` and
  `docs/xna-4-api-coverage.md`'s Audio section) — no per-source 3D audio graph, no aux-send/reverb
  bus, only a 2-value stereo gain pair instead of a 4-coefficient crossfeed matrix, a single
  per-track "cooked callback" slot, etc.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases used on the XNA API surface. It is under **separate, active, concurrent development** by
  another session — if a build ever fails inside `SHARP_RUNTIME/CMakeFiles/...`, check
  `git status`/`git log -1` there before assuming the audio code broke something.

---

## 2. Current status

- **Build:** clean, rebuilt and reverified this pass (`P10-AUDIT-002/003`, on top of
  `P10-LOOP-003/004`, `P10-FILTER-002/003/004/006`, and `P10-RPC-004`/`P10-RPC-007`). EasyGL
  backend (Linux default), `SOUND_ENABLED` on, SDL3_mixer linked. `cna_demo_sound`/`cna_demo_2d`
  example targets not rebuilt this pass (no Audio public API surface touched beyond internals/
  comments/one Doxygen wording fix, not signatures).
- **Tests:** `CnaTests` whole-suite count is **3340 / 3342 pass** (2 skipped:
  `AccelerometerTests`/`GyroscopeTests`' `GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  hardware-dependent, expected — not Audio; the 1-test increase since the last sync is
  `P10-AUDIT-002/003`'s new `AudioEngineTest.RendererDetailsReportsExactlyOneSdlMixerEntry`, plus
  one renamed-in-place `CueTest` and one tightened `SoundEffectTest` assertion, both net-zero on
  count). The audio-scoped subset (§7's `--gtest_filter` list, now 466 tests) was reverified this
  pass under a **fresh dedicated ASan+UBSan build**: 466/466 pass, zero leaks/errors -- the first
  ASan/UBSan run since `P10-SAN-002`, covering everything landed since (`P10-RPC-004/007`'s
  release-phase timers, `P10-FILTER-002/003/004/006`'s live filter-coefficient writes,
  `P10-LOOP-003/004`'s new raw-callback regression test). Also reverified under a fresh dedicated
  **ThreadSanitizer** build: the existing `ConcurrentFilterUpdatesDoNotRaceWithRealMixingThread`
  precedent test (10 repeats) and the new `BoundedLoopRegionPlaysIntroOnceThenRepeatsOnlyTheLoopRegion`
  raw-callback test (5 repeats, its own atomics-across-threads code is new this pass) both clean,
  no data races. Both sanitizer build dirs deleted after use, per this project's own convention.
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
  `CHECKLIST.md` accepted deviation (no reverb, no true 3D HRTF/elevation, stereo hard-pan instead
  of crossfeed, DSP-preset RPC targeting unsupported [no DSP preset system exists at all], etc.),
  not a bug — see §5 for the full table. `CHECKLIST.md` itself has not been re-synced against the
  most recent Phase 10 landings (P10-RPC-002/003/004, P10-FILTER-002/003/004/006) -- its
  `AttackTime`/`ReleaseTime`/filter-frequency/Q rows are now stale; deferred to P10-AUDIT-002/003
  rather than piecemeal-edited per task, per established practice this pass.

---

## 3. Recent changes

Newest first. Full rationale, FNA/FAudio line citations, and `git stash` verification notes for
every item are in `plan_audio.md`'s "Phase 9"/"Phase 10" sections.

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

**No build- or test-breaking blocker exists.** Build is clean, full suite passes (§2). Phase 10 is
a large, intentionally open task list, not a single confirmed next task — see §8.

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
| **Accepted deviation** | Stereo hard-pan eliminates the opposite channel instead of crossfeed-blending it | `CHECKLIST.md`, `CP-19` |
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

**Phase 10 is fully closed (89/89 task IDs), and the follow-up ASan+UBSan+ThreadSanitizer sweep
came back clean (§2/§3).** There is no further self-selectable work left on this branch that
doesn't require a new scope/design decision. Per this autonomous pass's own standing instruction
not to invent new scope (§9), **the session is stopping here** rather than starting a new "Phase
11" or otherwise picking new work. Genuinely open items that need the user's input before anything
further can be self-directed:

- **Deciding whether to open a "Phase 11"** (or otherwise define new Audio work) at all -- Phase 10
  was explicitly scoped as "hardening and XNA/XACT parity" and has run its course; what comes next
  (if anything) is a product decision, not a mechanical one.
- The two explicit design-only RFCs recorded in `plan_audio.md` (`P10-PAN-002`'s RFC-1: internal
  post-SDL3_mixer crossfeed mixing layer; `P10-HRTF-002`'s RFC-2: optional FAudio/FACT backend) --
  proposals only, never approved as work, per §9.
- Whether to push this branch's commits (`git log origin/feature/audio..HEAD`) -- not done this
  pass, per this project's standing "never push without being explicitly asked" rule.

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
Read NEXT.md first. Do not assume anything is complete beyond what NEXT.md §2/§4 state. Phase 9 is
fully closed. Phase 10 (plan_audio.md) is a large, intentionally open task list; 18 items are
closed so far -- read its Phase 10 section before picking anything, every task there already has a
concrete, cited status. See NEXT.md §8 for the remaining candidates: every one of them is real
feature/behavior-decision work or a large mechanical audit, and needs the user's confirmed scope
first -- there is no self-selectable pure-test-addition work left on the list (§9).

1. If the user names a specific task, inspect only the files needed for it -- do not refactor
   unrelated code. Confirm scope/approach with the user first if it's real feature work rather
   than a one-line fix.
2. Make one small, verified improvement at a time: add/extend a test, verify with the git-stash
   pattern (§7) for any behavioral fix, run the relevant build/test command, and run ASan+UBSan if
   it touches memory lifetime or ownership.
3. Update plan_audio.md's checkbox + note for whatever sub-item was completed, then update this
   NEXT.md (status, recent changes, next task) to reflect what changed, and commit.
```
