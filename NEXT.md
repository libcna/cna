# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> This handoff covers the **audio** work on the `feature/audio` branch only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> The detailed, file-by-file task list lives in **`plan_audio.md`** (repo root).
> The graphics/main-line handoff lives on the `develop` branch's NEXT.md and `GRAPHICS_TASKS.md`.
> **Media namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`)
built on SDL3. It is a framework/runtime, not a game.

- **This branch's goal:** review, complete, and test the FNA → C++ port of the **audio** subsystem,
  file by file, against the authoritative FNA source at
  `/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`. No stubs without a documented reason.
- **Current phase:** working through `plan_audio.md`. Core playback is done and tested; the XACT
  cluster and Microphone capture are the main remaining work.
- **Key architectural decision (audio):** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer` / `MIX_Track` / `MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is parsed
  by a custom `XactParser` and mixed through SDL_mixer. Consequences: full 3D HRTF and Doppler are
  stored-but-not-applied; streaming wavebanks are loaded fully into memory.
- `sharp-runtime` (sibling repo `../sharp-runtime`) provides all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.

---

## 2. Current status

- **Build:** EasyGL `cmake-build-debug` builds clean, including all audio. (`SOUND_ENABLED` is on;
  SDL3_mixer is linked.)
- **Unit tests:** `CnaTests` **1839 / 1839 pass** (up from 1757 at the start of this branch; **+82
  audio tests added**). No regressions.
- **Build-dir note (important):** `cna_audio/cmake-build-debug` was previously configured against the
  sibling checkout `../cna` (wrong source tree). It has been wiped and reconfigured to build
  `cna_audio` itself. The sibling `../cna` has its own separate build dir and was not touched.

### Audio that works now (ported, FNA-faithful, unit-tested)
- Exceptions: `InstancePlayLimitException`, `NoAudioHardwareException` (→ `System::…::ExternalException`),
  `NoMicrophoneConnectedException` (→ `System::Exception`).
- Data classes: `AudioEmitter`, `AudioListener`.
- Enums: `AudioChannels`, `AudioStopOptions`, `MicrophoneState`, `SoundState`.
- `SoundEffect` — static sample math, volumes, ctors, `CreateInstance`, `Play`, `Dispose`, `FromStream`.
- `SoundEffectInstance` — Play/Pause/Resume/Stop, Volume/Pan/Pitch, IsLooped, Apply3D, Dispose.
- `DynamicSoundEffectInstance` — buffer submission, float buffers, Dispose, BufferNeeded.
- Device-dependent tests run under the **SDL "dummy" audio driver** (`SDL_AUDIODRIVER=dummy`); on a
  host with no audio device they `GTEST_SKIP` instead of failing.

### Audio that does NOT work / is not done yet
- **XACT cluster** (`AudioEngine`, `SoundBank`, `WaveBank`, `Cue`, `AudioCategory`): compiles and runs
  but has "stub behavior", wrong exception types (`std::*` not `System::*`), `GetTypeName` using `::`
  instead of `.`, `IsInUse` hard-coded `false`, and lookups that return stubs instead of throwing.
  **Zero tests.**
- **`XactParser`** has a confirmed data bug (compact `.xwb` length), dead code, and a fragile track-event
  walker (see §5). **Zero tests.**
- **`Microphone`** is a stub — no SDL audio capture; missing `GetTypeName`.
- **`RendererDetail`** missing `Equals`; no tests (only constructible via `AudioEngine`).
- 3D positional audio / Doppler: accepted SDL_mixer limitation (stored, not applied).

---

## 3. Recent changes (this branch, newest first)

| Commit | Area | Change |
|--------|------|--------|
| _(pending)_ | Internal audio | T-1A: added `// SPDX-License-Identifier: MS-PL` to `XactTypes.hpp`, `AudioMixer.hpp`, `XactParser.cpp`, `AudioMixer.cpp`. No behavior change; 1839 tests still pass. |
| `aca2712` | `SoundEffect`, `SoundEffectInstance` | Pan disposed/range throws; Volume + MasterVolume pass-through (FNA); IsLooped `hasStarted_` gate → `InvalidOperationException`; Apply3D null/`>1` throws; all exceptions → `System::` types; `GetSampleDuration` truncates to whole ms (FNA); removed non-XNA `SoundEffectI` interface; removed dead `LoadAudioFromMemory`. +29 tests. |
| `7b3d8fa` | `DynamicSoundEffectInstance` | Fixed `setIsLoopedProperty` to override **both** base virtuals (was hiding); added `Dispose()` override + made base `getIsDisposedProperty()` virtual (fixes stream/track leak, single disposed flag); `SubmitFloatBufferEXT` guard + stream-format rebuild; exceptions → `System::`; moved `GetTypeName` to public. +15 tests. |
| `443b501` | `AudioEmitter` + enums/data | `AudioEmitter.DopplerScale` → `ArgumentOutOfRangeException`; tests for AudioEmitter/AudioListener + 4 enums. +26 tests. |
| `4106674` | Audio exceptions | Rebased 3 audio exceptions onto sharp-runtime hierarchy; dropped hand-rolled inner-exception. +12 tests. |
| `0c3a76c` | docs | Added `plan_audio.md` (detailed per-file audio plan, 6 phases, ~40 tasks). |

---

## 4. Current blocker / main problem

**No hard blocker** — the build is clean and all 1839 unit tests pass.

The most important *substantive* problem is that the **XACT cluster is the largest unverified area**:
- `src/CNA/Internal/Audio/XactParser.cpp` has a **confirmed data bug**: compact `.xwb` per-entry
  `dataLength` is computed from the 11-bit deviation alone instead of consecutive entry offsets,
  so **compact wavebanks decode to truncated/garbage audio** (`plan_audio.md` task **T-2D**).
- There are **no tests** for any XACT class or for the parser, so `AudioEngine`/`SoundBank`/`WaveBank`/
  `Cue` playback fidelity is currently unverified.

No failing command today; the risk is silent incorrectness, not a crash. Suggested first probe: build a
minimal `.xgs`/`.xsb`/`.xwb` fixture and write a parser round-trip test (T-2D / T-5O) to expose the bug.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|--------|-------|-----|
| **Confirmed bug** | `XactParser` compact `.xwb` `dataLength` wrong → garbage audio for compact wavebanks | T-2D |
| **Confirmed (dead code)** | `XactParser` XGS first-pass category loop is dead/buggy; correct reparse follows it | T-2F |
| **Suspected bug** | `XactParser` track-event walker `break`s on unknown events (PITCH/VOLUME/MARKER) → first PlayWave missed in multi-event tracks | T-2E |
| **Incomplete** | `AudioEngine`/`SoundBank`/`WaveBank`/`Cue` throw `std::*` not `System::*`; `GetTypeName` uses `::` not `.` | T-1F, T-1B |
| **Incomplete** | `AudioEngine::GetCategory`, `SoundBank::GetCue`, `SetGlobalVariable`, `Cue::*Variable` return/accept stubs instead of throwing `InvalidOperationException` on bad names | T-3A |
| **Incomplete** | `SoundBank::IsInUse` / `WaveBank::IsInUse` hard-coded `false` | T-3B |
| **Incomplete** | `Microphone` capture is a stub (no SDL recording); missing `GetTypeName` override | T-1C, T-4A |
| **Incomplete** | `AudioCategory::Equals` compares parent+index, not Name (FNA compares Name); stale "no-op" doxygen | T-2G |
| **Incomplete** | `RendererDetail` missing `Equals`; no tests (only constructible via `AudioEngine`) | T-3D, T-5L |
| **Done** | ~~SPDX header missing in `XactTypes.hpp`, `XactParser.cpp`, `AudioMixer.hpp/.cpp`~~ | T-1A |
| **Accepted limitation** | SDL_mixer: 3D HRTF + Doppler stored, not applied; streaming wavebanks loaded fully into memory | plan §2 |
| **Minor / intentional** | `SoundEffect::GetSampleDuration` truncates to whole ms (FNA); `DynamicSoundEffectInstance::GetSampleDuration` keeps full precision (float-aware EXT path) | — |
| **Needs verification** | Device-dependent audio tests rely on the SDL `dummy` driver; they skip when no device is available | — |

---

## 6. Architecture notes

### Main modules
| Component | Location | Notes |
|-----------|----------|-------|
| XNA audio API | `include/Microsoft/Xna/Framework/Audio/`, `src/.../Audio/` | Must match XNA 4.0 / FNA exactly |
| Internal mixer | `CNA/Internal/Audio/AudioMixer.{hpp,cpp}` | SDL3_mixer `MIX_Mixer` singleton via `GetMixer()`; single 44100/stereo/S16 device |
| XACT parser | `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp` | Custom `.xgs`/`.xsb`/`.xwb` reader (FACT is **not** used) |
| sharp-runtime | `../sharp-runtime/` | `System.*` types, primitive aliases, exception hierarchy |

### Data flow (playback)
```
SoundEffect (loads MIX_Audio via GetMixer)
  → CreateInstance() returns SoundEffectInstance BY VALUE
  → SoundEffectInstance::Play() creates a MIX_Track, binds the MIX_Audio, plays
DynamicSoundEffectInstance
  → user submits buffers → SDL_AudioStream → MIX_Track
  → FrameworkDispatcher::Update() pumps registered instances (Streams list) and raises BufferNeeded
AudioEngine/SoundBank/WaveBank/Cue
  → XactParser reads .xgs/.xsb/.xwb → cues map to SoundEffect/SoundEffectInstance played via SDL_mixer
```

### Invariants / rules that must stay stable
- **Backend = SDL3_mixer only.** Do not reintroduce FAudio/FACT.
- **Device is opened lazily** by `CNA::Internal::Audio::GetMixer()` on first playback; all SDL_mixer
  code is gated by `#ifdef SOUND_ENABLED`. Construction of `SoundEffect` from a PCM buffer triggers it.
- **Exceptions on the XNA surface must be `System::` types**, never `std::runtime_error`/`std::out_of_range`.
- **`GetTypeName()` returns the dotted .NET name** (`"Microsoft.Xna.Framework.Audio.X"`) and lives in
  the **public** section for concrete `System::Object` subclasses.
- **`SoundEffectInstance::hasStarted_`** is set on `Play()` and never reset; it gates `IsLooped`
  (set-after-play → `InvalidOperationException`).
- **`DynamicSoundEffectInstance`** must override **both** `setIsLoopedProperty(const bool&)` and
  `setIsLoopedProperty(bool&&)` as no-ops (single-signature overrides silently hide and break dispatch).
- **`CreateInstance` returns by value** (no FNA-style instance tracking / Dispose cascade). This is a
  documented value-semantics deviation; do not "fix" it without a deliberate decision (plan D1).
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

# Run only the audio tests
./cmake-build-debug/CnaTests --gtest_filter='*SoundEffect*:*Dynamic*:*AudioEmitter*:*AudioListener*:*SoundState*:*AudioChannels*:*AudioStopOptions*:*MicrophoneState*:*PlayLimit*:*NoAudio*:*NoMicrophone*'

# Device-dependent audio tests use the SDL dummy driver automatically (set in the tests),
# but you can force it for the whole binary:
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='SoundEffectInstanceTest.*'

# FNA reference for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio
```

---

## 8. Next smallest tasks (ordered; see `plan_audio.md` for full detail)

_T-1A (SPDX headers on internal audio files) is done — see §3/§5._

1. **T-2D + T-5O — Fix compact `.xwb` length and add the first parser test.**
   - Goal: compute per-entry length from consecutive offsets (last = segment − offset), not from the
     11-bit deviation; add a round-trip parser test on a minimal `.xwb` fixture asserting PCM lengths.
   - Files: `src/CNA/Internal/Audio/XactParser.cpp`; new `tests/.../Audio/XactParserTests.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='XactParser*'`.

2. **T-1F + T-1B + T-3A + T-5E — Complete `AudioEngine` (and `RendererDetail` T-3D/T-5L, coupled).**
   - Goal: map exceptions to `System::`; fix `GetTypeName` to dots; throw `InvalidOperationException`
     on unknown category/variable names; add `RendererDetail::Equals`; full `AudioEngineTests`/
     `RendererDetailTests` (RendererDetail instances obtained via `AudioEngine::RendererDetails`).
   - Files: `…/Audio/AudioEngine.{hpp,cpp}`, `…/Audio/RendererDetail.{hpp,cpp}`; new tests.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='AudioEngineTest.*:RendererDetailTest.*'`.

3. **T-1F + T-3A/B + T-5F/G — `SoundBank` and `WaveBank`.**
   - Goal: `System::` exceptions; `GetTypeName` dots; throw on bad cue name; real `IsInUse`; tests.
   - Files: `…/Audio/SoundBank.{hpp,cpp}`, `…/Audio/WaveBank.{hpp,cpp}`; new tests.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='SoundBankTest.*:WaveBankTest.*'`.

4. **T-1F + T-3A + T-5H — `Cue`.**
   - Goal: `System::` exceptions; `GetTypeName` dots; validate variable names; `Apply3D`
     `ObjectDisposedException`; tests.
   - Files: `…/Audio/Cue.{hpp,cpp}`; new tests.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='CueTest.*'`.

5. **T-2G + T-5I — `AudioCategory`.**
   - Goal: `Equals` by Name (FNA); fix stale "no-op" doxygen; tests.
   - Files: `…/Audio/AudioCategory.{hpp,cpp}`; new tests.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='AudioCategoryTest.*'`.

6. **T-1C + T-5M — `Microphone` compliance (capture T-4A deferred).**
   - Goal: add `GetTypeName`; map `Microphone` exceptions to `System::`; tests for the headless
     surface. (Real SDL capture, T-4A, is a separate larger task.)
   - Files: `…/Audio/Microphone.{hpp,cpp}`; new tests.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='MicrophoneTest.*'`.

---

## 9. Do not do yet

- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No instance-tracking / Dispose-cascade redesign** of `CreateInstance` — value semantics is a
  documented deviation (plan D1); changing it is a deliberate decision, not a drive-by fix.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep them as documented stored-not-applied.
- **No broad `XactParser` rewrite** — fix the targeted data bugs (T-2D/E/F) only; the parser is
  load-bearing and untested.
- **No touching the sibling `../cna` checkout or its build dir** — it is a separate clone.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — add docs only when touching a file for another reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md for the detailed audio task list.
Open only the files needed for the first task. Do not refactor unrelated code.
Do not touch the Media namespace or the sibling ../cna checkout.

Make ONE small, verified improvement (start with task #1 in NEXT.md §8: fix the compact
.xwb length bug in XactParser.cpp with a round-trip parser test, T-2D/T-5O).

Build and test:
  cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
  ./cmake-build-debug/CnaTests --gtest_filter='<relevant suite>'

Keep audio exceptions as System:: types, GetTypeName dotted, SPDX + Doxygen present.
After finishing, update NEXT.md (status, recent changes, next task) and commit.
```
