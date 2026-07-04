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
  11-group task list (`plan_audio.md`'s "Phase 9" section). 6 of those 11 groups are fully closed
  (`P9-LIFECYCLE`, `P9-CATEGORY`, `P9-VALIDATION`, `P9-DOCS`, `P9-BUILD`, `P9-STOP`); `P9-XACT` is
  9/15 done (variation-table variable-driven selection and one-shot RPC volume/pitch wiring are
  in; DSP/filter wiring and missing-wave/cue fidelity remain); 4 groups remain fully open
  (`P9-3D`, `P9-HARDWARE`, `P9-DYNAMIC`, `P9-AUDIT`) — see §4/§8.
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
- **Tests:** `CnaTests` **2102 / 2102 pass**, reliably (stress-tested 5+ consecutive full-suite
  runs with zero failures since the latest change; the pre-`P9-XACT` baseline of 2093 was verified
  clean under a full ASan+UBSan build). Re-run to check for drift:
  `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests`.
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

- **`P9-XACT-001..009`** (uncommitted at time of writing) — two independent fixes to data that was
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

**No build- or test-breaking blocker exists.** The build is clean and all 2102 tests pass
reliably. This is **not** a "nothing left to do" branch state, though — Fáze 9 (a user-directed,
already-scoped task list) still has open work:

- `P9-XACT` (9/15 done) — RPC volume/pitch and interactive-variation selection are wired; still
  open: DSP/filter parsing+wiring audit (`P9-XACT-010..013`) and missing-wave/cue error-path
  fidelity (`P9-XACT-014/015`).
- `P9-3D` — 3D audio fidelity beyond the existing pan+distance-attenuation approximation (Doppler
  pitch adjustment feasibility, stereo panning model, more test coverage).
- `P9-HARDWARE` — `NoAudioHardwareException` usage audit, `std::runtime_error` vs XNA-compatible
  exception behavior, no-audio-device test coverage.
- `P9-DYNAMIC` — `DynamicSoundEffectInstance` `PendingBufferCount`/`BufferNeeded` test coverage
  audit (buffer completion while playing/paused, multiple subscribers, subscriber removal
  mid-callback).
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

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, under separate, active, concurrent development by another session. A build
failure inside `SHARP_RUNTIME/CMakeFiles/...` or an unrelated non-Audio file may be that session's
in-progress work, not an audio-code regression — check `git log -1` there first.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Confirmed, fixed** | `Cue::IsPlaying`/`IsPaused`/`IsStopped` never reconciled after natural completion | `P9-LIFECYCLE-001..009` |
| **Confirmed, fixed** | `offset+count` int32-overflow → real segfault in `SoundEffect` ctor / `DynamicSoundEffectInstance::SubmitBuffer`/`SubmitFloatBufferEXT` | `P9-VALIDATION-003/010/011` |
| **Confirmed, fixed** | `AudioEngine::StopCategoryInternal` mutate-during-iteration bug, silently skipped stopping cues | `P9-CATEGORY-001/002` |
| **Confirmed, fixed** | `Cue::Stop(AsAuthored)` marked a cue fully stopped/unregistered while its tail was still playing | `P9-STOP-002..005` |
| **Confirmed, fixed** | `SoundEffectInstance`/`DynamicSoundEffectInstance::Resume()` didn't call `Play()` when never-started/disposed (FNA does) | `P9-VALIDATION-010` |
| **Accepted deviation** | `IsPlaying`/`IsPaused` mutually exclusive, unlike real FACT — decision pending | `CHECKLIST.md`, `P9-LIFECYCLE-013` |
| **Accepted deviation** | Authored-stop tail duration ≠ real `fadeOutMS` curve (not parsed/retained at all) | `CHECKLIST.md`, `P9-STOP-010` |
| **Accepted deviation** | RPC volume/pitch curves evaluated once at `Play()` time, not continuously re-evaluated while playing (no per-frame `Cue` update tick exists) | `CHECKLIST.md`, `P9-XACT-005/006/007` |
| **Accepted deviation** | Stereo hard-pan eliminates the opposite channel instead of crossfeed-blending it | `CHECKLIST.md`, `CP-19` |
| **Accepted deviation** | No Doppler, no 3D HRTF/elevation — pan + linear distance-attenuation only | `CHECKLIST.md` |
| **Accepted deviation** | Reverb is a documented no-op (`INTERNAL_applyReverb`) — SDL3_mixer has no aux-send/return bus | `CHECKLIST.md`, `T-4C` |
| **Accepted deviation** | XACT category `instanceLimit`/`fadeInMS`/`fadeOutMS` parsed but never enforced | `CHECKLIST.md`, `XA-11` |
| **Accepted deviation** | `AudioEngine`/`SoundBank`/`WaveBank` silently stub instead of throwing on a missing/corrupt file; `NoAudioHardwareException` never thrown | `CHECKLIST.md`, `CP-18`/`XA-9` |
| **Needs verification** | `SoundEffectInstance` filter coefficient locking follows SDL3_mixer's documented practice but was never stress-tested under real concurrency (no ThreadSanitizer run) | `T-4C` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver here; real-hardware runs are manual/ad-hoc | — |
| **Incomplete** | `P9-XACT`/`P9-3D`/`P9-HARDWARE`/`P9-DYNAMIC` — see §4 | `plan_audio.md` |

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
order is exhausted through `P9-STOP`, and `P9-XACT` is partway done (9/15). The remaining groups
have no user-specified priority among them — suggested order below is by "smallest
independently-verifiable slice first":

1. **Audit DSP/filter parsing and runtime application** (`P9-XACT-010`). Goal: determine what a
   sound's parsed DSP preset codes (`SOUND_FLAG_HAS_DSP`, currently read-and-discarded in
   `XactParser.cpp`, same shape as the RPC codes `P9-XACT-005/006` just fixed) would need to wire
   low/high/band-pass filters into `SoundEffectInstance`'s existing real filter implementation
   (`INTERNAL_apply{Low,High,Band}PassFilter`, already real via a per-track SDL3_mixer callback).
   Files: `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp`. Verification: read-only audit, no
   command — produces a `plan_audio.md` note, same pattern as `P9-XACT-005`.
2. **Wire parsed filters into `SoundEffectInstance` where feasible** (`P9-XACT-011`), plus keep
   reverb a documented no-op if infeasible (`P9-XACT-012`) and document exactly what's supported
   (`P9-XACT-013`). Files: `Cue.cpp`, `SoundEffectInstance.cpp`. Verification:
   `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='CueTest.*:SoundEffectInstanceTest.*'`.
3. **Ensure missing wave/sound/cue-index behavior matches FNA** (`P9-XACT-014/015`). Goal: audit
   what `Cue::Play()`/`SoundBank::GetCue()` do today for an out-of-range cue index or an
   unresolvable wave/sound reference against FNA's equivalent error handling; add tests. Files:
   `Cue.cpp`, `SoundBank.cpp`. Verification:
   `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='CueTest.*:SoundBankTest.*'`.
4. **Audit `NoAudioHardwareException` usage** (`P9-HARDWARE-001`). Goal: confirm it's a type-only
   stub never thrown (already suspected, see §5) and decide whether that's worth changing. Files:
   `Microsoft/Xna/Framework/Audio/NoAudioHardwareException.hpp`, `AudioEngine.cpp`. Verification:
   read-only audit.
5. **Audit `DynamicSoundEffectInstance::PendingBufferCount` transitions** (`P9-DYNAMIC-001`). Goal:
   confirm current behavior across Play/Pause/Resume/Stop/Dispose matches FNA; add any missing
   tests (`P9-DYNAMIC-002..007`). Files: `DynamicSoundEffectInstance.cpp`,
   `tests/.../DynamicSoundEffectInstanceTests.cpp`. Verification:
   `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='DynamicSoundEffectInstanceTest.*'`.
6. **Audit `Apply3D` for stereo sources** (`P9-3D-001`/`002`). Goal: confirm/document current
   stereo panning behavior under `Apply3D` (as distinct from the direct `Pan` property, already
   covered by `CP-19`). Files: `SoundEffectInstance.cpp`. Verification: read-only audit, or a new
   test under `SoundEffectInstanceTests.cpp` if a gap is found.

Each task, once implemented: add/extend tests, verify with the `git stash` pattern (§7) for any
behavioral fix, run ASan+UBSan if it touches memory lifetime or ownership, update `plan_audio.md`'s
checkbox + `*Note:*`, then update this file and commit.

---

## 9. Do not do yet

- **No re-running a fresh full "line-by-line vs FNA" audit.** Fáze 7 and Fáze 8 already did two
  rounds of that. Fáze 9 is a different, already-scoped hardening pass — don't invent a "Fáze 10".
- **No implementing either of the two open decisions in §4** (`IsPlaying`/`IsPaused` coexistence;
  authored-stop fade-curve timing) **without asking the user first** — both would need real
  feature work or touch already-shipped shared infrastructure.
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
Read NEXT.md first. Fáze 9 (a user-directed, already-scoped hardening pass) has 6 of 11 task
groups fully closed (P9-LIFECYCLE, P9-CATEGORY, P9-VALIDATION, P9-DOCS, P9-BUILD, P9-STOP),
P9-XACT is 9/15 done (variation selection + RPC volume/pitch wired; DSP/filter and missing-
wave/cue fidelity remain), and 4 groups are fully open (P9-3D, P9-HARDWARE, P9-DYNAMIC,
P9-AUDIT) -- see §4/§8. No known build/test blocker.

1. Confirm current state matches NEXT.md §2 (build clean, 2102/2102 tests pass) -- rebuild and
   rerun SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests (or the `tests` CMake preset, §7) to
   check for drift since this was last updated.
2. Inspect only the files needed for the first §8 task (start with the XACT DSP/filter audit
   unless the user names something else) -- don't refactor unrelated code.
3. Make one small, verified improvement: if it's an audit, write the finding into plan_audio.md;
   if it's a fix, add/extend a test, verify with the git-stash pattern (§7), run the relevant
   build/test command, and run ASan+UBSan if it touches memory lifetime or ownership.
4. Update plan_audio.md's checkbox + note, then update this NEXT.md (status, recent changes, next
   task) to reflect what changed, and commit.
```
