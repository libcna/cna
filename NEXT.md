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
  `P9-3D`, `P9-HARDWARE`, `P9-DYNAMIC`, `P9-AUDIT`). **10 of the 11 groups are fully closed; one
  group (`P9-CATEGORY`) has 6 deferred sub-items still open** — see §4 for exactly what's left.
  Three genuine open design decisions came up during Phase 9 and were all asked-and-resolved with
  the user's explicit input (XACT filter `OneOverQ` fidelity, `Cue::IsPlaying`/`IsPaused`
  coexistence, `Cue::Stop(AsAuthored)` fade timing) — none remain open.
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

- **Build:** clean as of commit `642b8432` (last verified). EasyGL backend (Linux default),
  `SOUND_ENABLED` on, SDL3_mixer linked. `cna_demo_sound`/`cna_demo_2d` example targets also
  rebuild clean as of their last touch.
- **Tests:** `CnaTests` whole-suite count was **3263 / 3265 pass** as of the last full run (2
  skipped: `AccelerometerTests`/`GyroscopeTests`' `GetCurrentValuePropertyDoesNotThrowWhenSupported`,
  hardware-dependent, expected). The audio-scoped subset (§7's `--gtest_filter` audio suite list)
  was **389 / 389 pass** under ASan+UBSan. Two unrelated, pre-existing, full-suite-load-only
  flakes have been observed and confirmed non-reproducing in isolation (not regressions):
  `CueTest.PlayWeightedVariationFavorsHigherWeightEntryStatistically` (un-seeded RNG) and
  `CueTest.PlayCalledTwiceWhileAlreadyPlayingIsANoOpAndDoesNotDuplicateInstances` (a short ~1.13ms
  fixture occasionally racing full-suite scheduling load).
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
- **What does not work / remains incomplete:** XACT category-level `instanceLimit` enforcement and
  category-level fade-in/fade-out are parsed but not enforced (`P9-CATEGORY-005..010`, still
  open — see §4/§8). Everything else open is a deliberate, documented `CHECKLIST.md` accepted
  deviation (no reverb, no true 3D HRTF/elevation, stereo hard-pan instead of crossfeed, RPC curves
  evaluated once not continuously, RPC-only cue release timing unimplemented, etc.), not a bug.

---

## 3. Recent changes

Newest first. One line each; full rationale, FNA/FAudio citations, and `git stash` verification
notes for every item are in `plan_audio.md`'s "Phase 9" section.

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

**No build- or test-breaking blocker exists.** As of the last full run, the build was clean and
all tests passed (§2).

**The actual open item is a documentation/completeness gap, not a bug:** `plan_audio.md`'s Phase 9
task list has 6 unchecked items — `P9-CATEGORY-005` through `P9-CATEGORY-010` — that were missed
when Phase 9 was earlier (incorrectly) reported as "fully closed." These are **deferred sub-items
of the already-closed `P9-CATEGORY` group**, explicitly named in the plan's own stated
implementation order (`plan_audio.md`, intro to the Phase 9 section: "...(6) ... and
`P9-LIFECYCLE-013..015` / `P9-CATEGORY-005..010` (deferred sub-items of already-started groups)").

- **Symptom:** `plan_audio.md` lines ~2070–2075 still show `* [ ]` (unchecked) for
  `P9-CATEGORY-005` through `P9-CATEGORY-010`.
- **No failing command or failing test** — this is unimplemented *feature* work, not a defect.
  `AudioCategory`/`AudioEngine`'s existing Pause/Resume/Stop/SetVolume behavior all pass their
  tests as-is; category `instanceLimit`/fade-in/fade-out are simply never enforced.
- **Affected files/modules:** `include/CNA/Internal/Audio/XactTypes.hpp` (`XgsCategory` already
  parses `instanceLimit`/`fadeInMS`/`fadeOutMS`, just never consumed),
  `src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp` (category state/registry),
  `src/Microsoft/Xna/Framework/Audio/Cue.cpp` (would reuse the fade-tick infrastructure just added
  for `P9-STOP-010`'s per-cue fade), `tests/Microsoft/Xna/Framework/Audio/AudioCategoryTests.cpp`.
- **Suspected cause of the tracking miss:** the parsed data (`instanceLimit`/`fadeInMS`/
  `fadeOutMS` on `XgsCategory`) was already documented as a *known, accepted* deviation in
  `CHECKLIST.md` (row citing `XA-11`) from an earlier phase, which made it easy to mistake "already
  documented as deferred" for "done" when tallying Phase 9's groups.
- **What's already been tried / decided:** the user was asked (2026-07-05) whether to implement
  this now or record it as a future task; **the user chose to record it as a future task** (§8)
  rather than implement it in this session. No code has been written for it.

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
| **Incomplete** | XACT category `instanceLimit` parsed but never enforced (no consumer) | `P9-CATEGORY-005/006` |
| **Incomplete** | XACT category-level `fadeInMS`/`fadeOutMS` parsed but never applied on category-wide operations | `P9-CATEGORY-007/008/009` |
| **Accepted deviation** | RPC volume/pitch curves evaluated once at `Cue::Play()` time, not continuously re-evaluated while playing | `CHECKLIST.md`, `P9-XACT-005/006/007` |
| **Accepted deviation** | RPC-only cue release timing (`maxRpcReleaseTime`, no authored `fadeOutMS`) unimplemented — tied to the RPC-evaluated-once deviation above | `CHECKLIST.md`, `P9-STOP-010` |
| **Accepted deviation** | `Apply3D`'s pan ignores listener/emitter `Forward`/`Up` orientation entirely (world-space X displacement only) | `CHECKLIST.md`, `P9-3D-009` |
| **Accepted deviation** | Stereo hard-pan eliminates the opposite channel instead of crossfeed-blending it (`Pan` property and `Apply3D` both) | `CHECKLIST.md`, `CP-19`, `P9-3D-001` |
| **Accepted deviation** | A parsed per-track filter's type can only ever decode to low-pass or high-pass, never band-pass (replicates a real FAudio bit-decode quirk) | `CHECKLIST.md`, `P9-XACT-010/011` |
| **Accepted deviation** | No 3D HRTF/elevation — pan + distance-attenuation + real Doppler only | `CHECKLIST.md` |
| **Accepted deviation** | Reverb is a documented no-op (`INTERNAL_applyReverb`) — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md`, `T-4C` |
| **Accepted deviation** | `AudioEngine::Init()` never queries real hardware, so it can never throw `NoAudioHardwareException` from the constructor the way FNA's does (`SoundEffect`/`DynamicSoundEffectInstance` can) | `CHECKLIST.md` |
| **Internal-only, documented** | `ParseXgs`/`ParseXsb` accept a big-endian magic cosmetically only — no byte-swap logic exists, so a real BE file would silently misparse, not throw | `XactParser.cpp` comments, `P9-AUDIT-003` |
| **Internal-only, documented** | `AudioMixer::DestroyMixer()` is dead code — nothing calls it | `AudioMixer.hpp` comment, `P9-AUDIT-003` |
| **Internal-only, documented** | `g_mixer`'s lazy-init has no mutex — assumed (not enforced) main-thread-only contract | `AudioMixer.cpp` comment, `P9-AUDIT-003` |
| **Needs verification** | `SoundEffectInstance` filter coefficient locking follows SDL3_mixer's documented practice but was never stress-tested under real concurrency (no ThreadSanitizer run) | `T-4C` |
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

The confirmed next task (per the user's explicit request to record it here rather than implement
it immediately): close out `P9-CATEGORY`'s 6 deferred sub-items (§4). This is real feature work —
ask the user to confirm scope/approach before implementing, the same way `P9-STOP-010` and
`P9-LIFECYCLE-013` were confirmed before implementation.

1. **Implement XACT category `instanceLimit` enforcement** (`P9-CATEGORY-005`).
   - Goal: cap concurrent playing cues per category at the parsed `instanceLimit`, per FACT's
     category-level instance limiting (evict-oldest or reject-new, matching the real
     `maxInstanceBehavior` semantics FACT uses — read `FACT_internal.c`'s category-instance-count
     handling before implementing).
   - Files: `include/CNA/Internal/Audio/XactTypes.hpp` (`XgsCategory::instanceLimit`, already
     parsed), `src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp` (category/cue registry),
     `src/Microsoft/Xna/Framework/Audio/Cue.cpp` (`Play()`).
   - Verification: new tests in `tests/Microsoft/Xna/Framework/Audio/AudioCategoryTests.cpp`;
     `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter="AudioCategoryTest.*"`.
2. **Add regression tests for the instance-limit behavior** (`P9-CATEGORY-006`) — can land in the
   same session/commit as `P9-CATEGORY-005`.
3. **Implement category-level fade-in/fade-out** (`P9-CATEGORY-007/008`).
   - Goal: apply the parsed `fadeInMS`/`fadeOutMS` on `XgsCategory` when a category-wide operation
     (`AudioCategory::Pause/Resume/Stop`/volume change) affects its active cues, reusing the
     wall-clock fade-tick pattern `P9-STOP-010` already added to `Cue::ReconcileState()` as a
     template (`fadeStart_`/`fadeOutMS_`-style fields, ticked by `AudioEngine::Update()`).
   - Files: same as above.
   - Verification: same `AudioCategoryTest.*` filter.
4. **Add regression tests for category fade-in/fade-out** (`P9-CATEGORY-009`).
5. **Document remaining category approximations** (`P9-CATEGORY-010`) — update `CHECKLIST.md`'s
   existing `XA-11` row (or replace it, if 005–009 are fully implemented) to reflect whatever
   actually landed; update `plan_audio.md`'s checkboxes with detailed notes, matching every other
   Phase 9 group's documentation style.
   - Verification: none (docs only) — but do this task last, after 005–009 are either implemented
     or the user decides to formally close them as an accepted deviation instead.

If the user prefers *not* to implement `P9-CATEGORY-005..010` and instead formally accept it as a
permanent deviation, task 5 above (documentation) is the only one needed — update `CHECKLIST.md`'s
`XA-11` row to say so explicitly and check off all 6 items in `plan_audio.md` with that rationale.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit.** Phase 7 and Phase 8 already did two
  rounds of that. Phase 9 is a different, already-scoped hardening pass — don't invent a "Phase
  10".
- **No re-litigating a resolved open decision without the user asking first.** All three that ever
  came up on this branch are resolved: XACT filter `OneOverQ` fidelity vs. narrowing
  (`P9-XACT-011`); `Cue::IsPlaying`/`IsPaused` coexistence (`P9-LIFECYCLE-013`); and
  `Cue::Stop(AsAuthored)`'s authored `fadeOutMS` timing (`P9-STOP-010`).
- **No implementing `P9-CATEGORY-005..010` without confirming scope with the user first** — it's
  real feature work, not a small fix; ask before starting, per the pattern the other two resolved
  decisions followed.
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
is 10 of 11 groups closed, with P9-CATEGORY-005..010 (XACT category instanceLimit + fade-in/
fade-out enforcement) still open and explicitly deferred (see §4 for why this was previously
mis-reported as done, and §8 for the concrete next steps).

1. Inspect only the files needed for the first §8 task (P9-CATEGORY-005: category instanceLimit
   enforcement) -- do not refactor unrelated code. Confirm scope with the user first if anything
   is ambiguous (this is real feature work, not a one-line fix).
2. Make one small, verified improvement at a time: add/extend a test, verify with the git-stash
   pattern (§7) for any behavioral fix, run the relevant build/test command, and run ASan+UBSan if
   it touches memory lifetime or ownership.
3. Update plan_audio.md's checkbox + note for whatever sub-item was completed, then update this
   NEXT.md (status, recent changes, next task) to reflect what changed, and commit.
```
