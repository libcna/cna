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
- **Unit tests:** `CnaTests` **1966 / 1966 pass** (up from 1757 at the start of this branch; **+209
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
- `AudioEngine` — both ctors, `IsDisposed`, `RendererDetails`, `GetCategory`/`GetGlobalVariable`/
  `SetGlobalVariable` (valid+invalid+disposed), `Update`, `Dispose`+`Disposing`, `GetTypeName`.
- `RendererDetail` — `ToString`, `GetHashCode`, `Equals`, `operator==`/`!=` (equal+unequal).
- `SoundBank` — ctor (null/empty/valid), `IsDisposed`, `IsInUse` (fire-and-forget cues only — see
  known deviation in §5), `GetCue`/`PlayCue` (2-arg and 3-arg, valid+invalid+disposed), `Dispose`+
  `Disposing`, `GetTypeName`.
- `WaveBank` — both ctors (null/empty/valid, each with its own FNA param name), `IsDisposed`,
  `IsPrepared`, `IsInUse` (real: tracks `Cue`s currently playing a `SoundEffectInstance` sourced from
  this bank via `RegisterCue`/`UnregisterCue`, wired from `Cue::Play`/`StopInternal`), `Dispose`+
  `Disposing`, `GetTypeName`. Verified end-to-end with a synthetic `.xwb`+`.xsb`+`Cue` playback test.
- `Cue` — all 9 state properties + `Name`, `Apply3D`, `GetVariable`/`SetVariable` (valid engine
  variable, built-in 3D variables, local override, empty name, name outside the known set),
  `Play`/`Pause`/`Resume`/`Stop` (`AsAuthored` and `Immediate` separately), `Dispose`+`Disposing`,
  `GetTypeName`.
- `AudioCategory` — `Name`, `Pause`/`Resume`/`SetVolume`/`Stop` (both stop options), `Equals`
  (equal+unequal by Name), `GetHashCode` consistency, `operator==`/`!=`. This was the last untested
  XACT class — the whole XACT cluster (`AudioEngine`, `RendererDetail`, `SoundBank`, `WaveBank`, `Cue`,
  `AudioCategory`) is now exception-fidelity-complete and unit-tested.
- `Microphone` (compliance layer only — real SDL capture is still T-4A, deferred): `GetTypeName`;
  exceptions → `System::` (`ArgumentOutOfRangeException` on `BufferDuration`, `ArgumentException` on
  `GetData` bounds); `micList`/`SAMPLERATE` are private FNA internals again (not `NOXNA`); a new
  `Microphone::CheckAllBuffers()` replaces `FrameworkDispatcher`'s direct `micList` iteration. Tested:
  static discovery (`All`/`Default` with no backend), Name, state machine (`Start`/`Stop`),
  `BufferDuration` valid+invalid, `GetSampleDuration`/`GetSampleSizeInBytes` round-trip, `GetData`
  bounds, `CheckBuffer`/`CheckAllBuffers`, `GetTypeName`.
- Device-dependent tests run under the **SDL "dummy" audio driver** (`SDL_AUDIODRIVER=dummy`); on a
  host with no audio device they `GTEST_SKIP` instead of failing.

### Audio that does NOT work / is not done yet
- **`XactParser`** compact `.xwb` length bug is fixed and covered by one round-trip test (T-2D/T-5O);
  the dead XGS first-pass code and the fragile track-event walker are still open (see §5). Coverage is
  otherwise minimal (1 test).
- **`Microphone`** capture itself is still a stub — no real SDL recording (`getAllProperty()` always
  empty, `GetData` always returns 0 bytes). Compliance (exceptions/GetTypeName/visibility/tests) is
  done; only the actual capture backend (T-4A) remains, a separate larger task.
- 3D positional audio / Doppler: accepted SDL_mixer limitation (stored, not applied). `Cue::Apply3D` is
  a no-op beyond the disposed check; the three standard 3D variables (`Distance`,
  `DopplerPitchScalar`, `OrientationAngle`) are readable/writable but never computed from a real
  listener/emitter.
- `AudioCategory::SetVolume` updates the category's baseline volume for future `Play()` calls only; it
  does not retroactively re-apply to sounds already playing (`AudioEngine::SetCategoryVolumeInternal`
  has a dead per-cue re-apply loop) — a real gap, not yet in scope of any numbered task.
- `Microphone::setBufferDurationProperty`'s upper-bound check (`milliseconds > 1000`) is dead code:
  `TimeSpan::getMillisecondsProperty()` returns the sub-second component, bounded to `[-999, 999]`, so
  that branch can never trigger. Faithfully ported from FNA (`Microphone.cs:60`); not "fixed" per
  `CLAUDE.md`'s behavior-fidelity rule.

---

## 3. Recent changes (this branch, newest first)

| Commit | Area | Change |
|--------|------|--------|
| _(pending)_ | `Microphone`, `FrameworkDispatcher` | T-1C/T-1D(remainder)/T-1H/T-5M: `GetTypeName` added (dotted, public — was entirely absent despite deriving from `System::Object`); `setBufferDurationProperty`'s `std::out_of_range` → `System::ArgumentOutOfRangeException`, both `GetData` bounds checks → `System::ArgumentException` (matching FNA's literal `ArgumentException("offset")`/`("count")`, an XNA quirk — bounds errors that read like `ArgumentOutOfRangeException` but aren't, kept as-is per behavior fidelity). `micList`/`SAMPLERATE` moved to `private` and un-`NOXNA`-tagged (they're FNA `internal`s, not CNA additions); `FrameworkDispatcher.cpp`'s direct `micList` null-check-and-iterate block replaced with a new `Microphone::CheckAllBuffers()` (avoids a cross-namespace `friend` declaration, verified behavior-preserving against `FrameworkDispatcherTests.cpp`). Found and documented a real dead-code quirk while porting: `setBufferDurationProperty`'s `milliseconds > 1000` branch is unreachable since `TimeSpan::getMillisecondsProperty()` is bounded to `[-999,999]` (sub-second component, not total) — faithfully kept, matching FNA (`Microphone.cs:60`), not "fixed". Added a `NOXNA`-tagged `MicrophoneTestAccess` friend (same pattern as `RendererDetail`/T-3D) since the constructor is private with no reachable factory. +24 tests (`MicrophoneTests.cpp`). Real SDL capture (T-4A) remains deferred. |
| `ec6a930` | `AudioCategory` | T-2G: `Equals` now compares by `name_` (FNA compares `Name`'s hash — CNA compares the string directly, which is behaviorally equivalent without FNA's theoretical hash-collision edge case) instead of `parent_+index_`, fixing an actual Equals/GetHashCode contract violation (`GetHashCode` was already name-based, so two categories with the same name but obtained as different `AudioCategory` instances had equal hashes but compared unequal). Rewrote the stale "no-op on SDL3_mixer backend" doxygen: `Pause`/`Resume`/`Stop` actually do route to every active `Cue` in the category with real, immediate effect (via `AudioEngine::{Pause,Resume,Stop}CategoryInternal`) — only `SetVolume` doesn't retroactively affect already-playing sounds, which is now documented as its own specific gap rather than lumped into a blanket "no-op" claim. +11 tests (`AudioCategoryTests.cpp`). This was the last untested XACT class. |
| `24af33c` | `Cue`, `AudioEngine` (helper) | T-1F/T-3A: exceptions → `System::`; `GetTypeName` dotted and moved from `private` to `public` (same recurring misplacement bug, now fixed in every XACT class); `GetVariable`/`SetVariable` validate the name against `AudioEngine`'s parsed global variable set (a per-cue XACT variable is the same named global variable, individually overridable per cue — CNA doesn't track the per-name cue-overridable bit, so any known global name is accepted) via a new private `AudioEngine::IsValidVariableName` helper, plus a small built-in set (`Distance`, `DopplerPitchScalar`, `OrientationAngle` — the three variables real XACT projects always define for 3D audio, which a hand-built test `.xgs` fixture wouldn't otherwise include) that never throws. A cue-local `SetVariable` call always shadows the engine's global default on subsequent `GetVariable` calls. +28 tests (`CueTests.cpp`, including a real `.xgs`+`.xsb` fixture pair so the "valid engine variable" path is exercised against actual parsed data, not just the built-in set). |
| `7be3513` | `SoundBank`, `WaveBank`, `Cue` (wiring only) | T-1F/T-3A/T-3B: exceptions → `System::`; `GetTypeName` dotted and moved from `private` to `public` (same misplacement bug as `AudioEngine`, found in both files); `GetCue` throws `InvalidOperationException` on unknown cue names instead of returning a `0xFFFF`-sentinel stub cue. Fixed a real fidelity bug: `WaveBank`'s streaming ctor delegated to the non-streaming ctor for validation, so an empty streaming filename raised `ArgumentNullException("nonStreamingWaveBankFilename")` instead of `"streamingWaveBankFilename"` — replaced delegation with a shared private `Init()` (mirrors `AudioEngine::Init`) so each ctor validates with its own FNA param name. Implemented real `IsInUse`: `SoundBank` checks its own fire-and-forget `Cue`s' playing state (cues obtained via `GetCue` are caller-owned and intentionally not tracked — documented deviation from FNA, which tracks all cues at the FACT-engine level); `WaveBank` gained `RegisterCue`/`UnregisterCue` (mirroring `AudioEngine`'s existing pattern) plus minimal wiring in `Cue::Play`/`StopInternal` (`Cue.hpp`/`.cpp`, registration only — Cue's own exception/GetTypeName work is still open, see below) since WaveBank has no other way to know which `SoundEffectInstance`s it produced are playing. +32 tests (`SoundBankTests.cpp` 19, `WaveBankTests.cpp` 13, including a synthetic `.xwb`+`.xsb` fixture that exercises real `AudioEngine`→`SoundBank`→`WaveBank`→`Cue` playback end-to-end to verify the new registration wiring actually works, not just compiles). |
| `0494439` | `AudioEngine`, `RendererDetail` | T-1F/T-1B/T-3A/T-3D: exceptions → `System::` (`ArgumentNullException`/`ObjectDisposedException`/`InvalidOperationException`); `GetCategory`/`GetGlobalVariable`/`SetGlobalVariable` now throw `InvalidOperationException` on unknown names instead of returning a stub or silently inserting (`SetGlobalVariable` previously never validated the name at all); `GetTypeName` dotted and moved from `private` to `public` (was misplaced, contradicting the class's own convention); added `RendererDetail::Equals` (`operator==` now delegates to it) with a `NOXNA`-tagged test-only friend accessor since the ctor is private to `AudioEngine`. +31 tests (`AudioEngineTests.cpp`, `RendererDetailTests.cpp`). |
| `2bafede` | `XactParser` | T-2D/T-5O: fixed compact `.xwb` per-entry `dataLength` — now derived from the gap to the next entry's offset minus the 11-bit deviation (last entry: remaining wave-data segment, unchanged), instead of using the raw deviation value as the length. Added `XactParserTests.cpp` round-trip test on a synthetic 3-entry compact fixture; confirmed it fails against the pre-fix code (verified via `git stash`). +1 test. |
| `704aae5` | Internal audio | T-1A: added `// SPDX-License-Identifier: MS-PL` to `XactTypes.hpp`, `AudioMixer.hpp`, `XactParser.cpp`, `AudioMixer.cpp`. No behavior change; 1839 tests still pass. |
| `aca2712` | `SoundEffect`, `SoundEffectInstance` | Pan disposed/range throws; Volume + MasterVolume pass-through (FNA); IsLooped `hasStarted_` gate → `InvalidOperationException`; Apply3D null/`>1` throws; all exceptions → `System::` types; `GetSampleDuration` truncates to whole ms (FNA); removed non-XNA `SoundEffectI` interface; removed dead `LoadAudioFromMemory`. +29 tests. |
| `7b3d8fa` | `DynamicSoundEffectInstance` | Fixed `setIsLoopedProperty` to override **both** base virtuals (was hiding); added `Dispose()` override + made base `getIsDisposedProperty()` virtual (fixes stream/track leak, single disposed flag); `SubmitFloatBufferEXT` guard + stream-format rebuild; exceptions → `System::`; moved `GetTypeName` to public. +15 tests. |
| `443b501` | `AudioEmitter` + enums/data | `AudioEmitter.DopplerScale` → `ArgumentOutOfRangeException`; tests for AudioEmitter/AudioListener + 4 enums. +26 tests. |
| `4106674` | Audio exceptions | Rebased 3 audio exceptions onto sharp-runtime hierarchy; dropped hand-rolled inner-exception. +12 tests. |
| `0c3a76c` | docs | Added `plan_audio.md` (detailed per-file audio plan, 6 phases, ~40 tasks). |

---

## 4. Current blocker / main problem

**No hard blocker** — the build is clean and all 1966 unit tests pass.

**Note for the next session:** this branch's build depends on the sibling `../sharp-runtime` repo,
which has (separately from this branch) been under active, uncommitted, incremental development during
recent sessions — twice now a fresh rebuild here briefly failed on `-Werror` errors from an in-progress
sharp-runtime class (`DateTime`, then `Decimal`), and both times it resolved itself moments later once
that unrelated work landed. If a fresh build ever fails inside `SHARP_RUNTIME/CMakeFiles/...` rather
than `CNA`/`CnaTests`, suspect the sibling repo's transient state first (check `git status`/`git log -1
--format=%cd` there), not the audio code — do not "fix" sharp-runtime files from this branch.

**The entire XACT cluster is exception-fidelity-complete and unit-tested, and `Microphone`'s compliance
layer is now done too** (`GetTypeName`, `System::` exceptions, visibility, tests — see §3). What's left
on the audio task list is exactly two `XactParser` hardening items, still open and untested beyond the
one T-5O fixture (dead XGS first-pass code T-2F, fragile track-event walker T-2E), plus real SDL
microphone capture (T-4A, a separate larger feature task, not a compliance fix).

No failing command today; the risk is silent incorrectness, not a crash. Suggested next probe: T-2E
(track-event walker) — build a minimal `.xsb` fixture with a non-PlayWave event before a PlayWave event
in the same track, and confirm today's `break`-on-unknown-event logic actually misses it (regression
test first, then fix), or T-2F (delete the dead XGS first-pass loop) as the smaller of the two.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|--------|-------|-----|
| **Done** | ~~`XactParser` compact `.xwb` `dataLength` wrong → garbage audio for compact wavebanks~~ | T-2D |
| **Confirmed (dead code)** | `XactParser` XGS first-pass category loop is dead/buggy; correct reparse follows it | T-2F |
| **Suspected bug** | `XactParser` track-event walker `break`s on unknown events (PITCH/VOLUME/MARKER) → first PlayWave missed in multi-event tracks | T-2E |
| **Incomplete** | `Microphone` capture itself is a stub — no real SDL recording, `All`/`GetData` never produce real devices/bytes | T-4A |
| **Done** | ~~SPDX header missing in `XactTypes.hpp`, `XactParser.cpp`, `AudioMixer.hpp/.cpp`~~ | T-1A |
| **Done** | ~~`AudioEngine` throws `std::*` not `System::*`; `GetTypeName` uses `::` not `.`; `GetCategory`/`SetGlobalVariable` stub instead of throwing~~ | T-1F, T-1B, T-3A |
| **Done** | ~~`RendererDetail` missing `Equals`; no tests~~ | T-3D, T-5L |
| **Done** | ~~`SoundBank`/`WaveBank` throw `std::*` not `System::*`; `GetTypeName` uses `::` not `.`; `GetCue` stub instead of throwing; `IsInUse` hard-coded `false`~~ | T-1F, T-1B, T-3A, T-3B |
| **Done** | ~~`Cue` throws `std::*` not `System::*`; `GetTypeName` uses `::` not `.` and was declared `private`; `GetVariable`/`SetVariable` accepted any name instead of validating~~ | T-1F, T-1B, T-3A |
| **Done** | ~~`AudioCategory::Equals` compares parent+index, not Name (FNA compares Name); stale "no-op" doxygen~~ | T-2G |
| **Done** | ~~`Microphone` missing `GetTypeName`; throws `std::out_of_range` not `System::*`; `micList`/`SAMPLERATE` public + wrongly `NOXNA`-tagged~~ | T-1C, T-1D, T-1H |
| **Accepted deviation** | `SoundBank::IsInUse` reflects only fire-and-forget cues it owns (created by `PlayCue`); cues obtained via `GetCue` are caller-owned per its own doc comment and are not tracked, unlike FNA's FACT-engine-level tracking of all cues from the bank | T-3B |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global variable set (plus a built-in 3D-variable set), not a separate per-cue-instance-overridable catalog — CNA's `XactParser` doesn't track the `ACCESSIBILITY_CUE` bit that would distinguish per-cue-overridable variables from category/global-only ones | T-3A |
| **Real gap (untasked)** | `AudioCategory::SetVolume` doesn't retroactively re-apply to already-playing cues (`AudioEngine::SetCategoryVolumeInternal` has a dead per-cue loop with an empty body) | — |
| **Accepted limitation** | SDL_mixer: 3D HRTF + Doppler stored, not applied; streaming wavebanks loaded fully into memory | plan §2 |
| **FNA-faithful dead code** | `Microphone::setBufferDurationProperty`'s `milliseconds > 1000` branch is unreachable (`TimeSpan::getMillisecondsProperty()` is bounded to `[-999,999]`, the sub-second component); ported as-is from FNA (`Microphone.cs:60`), not "fixed" | — |
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

_T-1A (SPDX headers), T-2D/T-5O (compact `.xwb` length fix + parser test), T-1F/T-1B/T-3A/T-3D
(AudioEngine + RendererDetail), T-1F/T-3A/T-3B/T-5F/T-5G (SoundBank + WaveBank), T-1F/T-3A/T-5H (Cue),
T-2G/T-5I (AudioCategory), and T-1C/T-1D/T-1H/T-5M (Microphone compliance) are done — see §3/§5.
Only `XactParser` hardening (T-2E/T-2F) and real SDL microphone capture (T-4A) remain._

1. **T-2E — Track-event walker regression.**
   - Goal: replace the `break` on unknown events (PITCH/VOLUME/MARKER) in `XactParser.cpp`'s track
     walker with a skip, so the first `PlayWave` is found even when preceded by other events in the
     same track. Write the regression test *first* against a minimal `.xsb` fixture (a track with a
     non-PlayWave event before its PlayWave) to confirm today's code actually misses it, then fix.
   - Files: `src/CNA/Internal/Audio/XactParser.cpp`; extend `tests/CNA/Internal/Audio/XactParserTests.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='XactParserTest.*'`.

2. **T-2F — Remove dead XGS first-pass + redundant re-seek.**
   - Goal: delete the dead/buggy first-pass category loop in `XactParser.cpp` (the correct reparse
     that follows it already does the real work); read `variationOffset` from the header once instead
     of re-seeking to a hardcoded `0x32`. Behavior-preserving cleanup — add a test first if none covers
     XGS category/variable parsing yet, to catch any accidental behavior change.
   - Files: `src/CNA/Internal/Audio/XactParser.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='XactParserTest.*:AudioEngineTest.*'`.

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

Make ONE small, verified improvement (start with task #1 in NEXT.md §8: T-2E, the XactParser
track-event walker — write a regression test first against a minimal .xsb fixture, confirm it fails
against today's break-on-unknown-event code, then fix). The entire XACT cluster and Microphone's
compliance layer are done; only XactParser hardening (T-2E/T-2F) and real SDL capture (T-4A, a
separate larger feature) remain on the audio task list.

Build and test:
  cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
  ./cmake-build-debug/CnaTests --gtest_filter='<relevant suite>'

Keep audio exceptions as System:: types, GetTypeName dotted, SPDX + Doxygen present.
After finishing, update NEXT.md (status, recent changes, next task) and commit.
```
