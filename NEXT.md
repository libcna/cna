# NEXT.md — CNA Audio Port Handoff (branch `feature/audio`)

> Covers the **audio** subsystem work on `feature/audio` only
> (`Microsoft::Xna::Framework::Audio` + `CNA::Internal::Audio`).
> Detailed file-by-file task list: **`plan_audio.md`** (repo root).
> Graphics/main-line handoff lives on `develop`'s own `NEXT.md`/`GRAPHICS_TASKS.md`.
> **Media namespace is explicitly out of scope for this branch.**

---

## 1. Project summary

**CNA** is a C++23 reimplementation of the XNA 4.0 programming model
(`Microsoft::Xna::Framework`), built on SDL3 with a pluggable graphics backend. It is a
framework/runtime, not a game.

- **This branch's goal:** port and verify `Microsoft::Xna::Framework::Audio` file-by-file
  against the authoritative FNA source (`/rv/data/library/github.com/FNA-XNA/FNA/src/Audio`),
  matching XNA behavior exactly, with full test coverage. No stubs without a documented reason.
- **Current phase:** the incremental fix/compliance plan in `plan_audio.md` is **done**. The one
  remaining item is a distinct feature (real microphone capture), not a small fix.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is
  parsed by a hand-written `XactParser` and mixed through SDL_mixer. Consequences: 3D HRTF and
  Doppler are stored but never applied; wavebanks are always loaded fully into memory (no real
  streaming).
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface.

---

## 2. Current status

- **Build:** EasyGL `cmake-build-debug` builds clean (`SOUND_ENABLED` on, SDL3_mixer linked).
- **Tests:** `CnaTests` **1970 / 1970 pass** (1757 at branch start; +213 audio tests this branch).
  No known regressions.
- **Works now (ported, FNA-faithful, unit-tested):**
  - `SoundEffect`, `SoundEffectInstance`, `DynamicSoundEffectInstance` — full playback API.
  - `AudioEngine`, `RendererDetail`, `SoundBank`, `WaveBank`, `Cue`, `AudioCategory` — the entire
    XACT cluster: `System::` exceptions throughout, dotted+public `GetTypeName`, real (non-stub)
    `IsInUse`/`GetCue`/`GetCategory`/`Equals` behavior.
  - `Microphone` — compliance layer (`GetTypeName`, `System::` exceptions, visibility) **plus real
    device enumeration** (T-4A step 1, this session): `getAllProperty()` now calls
    `SDL_GetAudioRecordingDevices`/`SDL_GetAudioDeviceName` and returns a "Default Device" entry
    (bound to the SDL default-recording sentinel) followed by each real named device. Verified
    against both the SDL `dummy` driver (always reports exactly one synthetic device) and the real
    `pulseaudio` driver on this machine (2 real hardware mics + the synthetic default = 3 entries).
    `Start`/`Stop`/`GetData`/`GetQueuedBytes` are still stubs — no device is opened yet (T-4A
    steps 2–4, see §8).
  - `XactParser` — all three data bugs found in the original audit are fixed: compact-`.xwb`
    length, track-event-walker `break`-on-unknown-event, dead XGS first-pass code.
  - Data classes/enums: `AudioEmitter`, `AudioListener`, `AudioChannels`, `AudioStopOptions`,
    `MicrophoneState`, `SoundState`. Audio exceptions: `InstancePlayLimitException`,
    `NoAudioHardwareException`, `NoMicrophoneConnectedException`.
  - Device-dependent tests run under the SDL `dummy` audio driver; they `GTEST_SKIP` on hosts with
    no audio device instead of failing. `MicrophoneTests.cpp` instead pins `SDL_AUDIODRIVER=dummy`
    via a file-scope static initializer (not a fixture `SetUp()`) because `getAllProperty()` caches
    its result for the process lifetime, so the driver must be fixed before the *first* call
    regardless of gtest run order.
- **Does NOT work yet:**
  - `Microphone` real capture: `Start()`/`Stop()` don't open/close a real SDL capture stream yet,
    so `GetData` always returns 0 bytes and `GetQueuedBytes` always returns 0. Enumeration
    (T-4A step 1) is done; steps 2–4 remain (see §8).
  - 3D positional audio / Doppler: architectural SDL_mixer limitation, values are stored but never
    computed/applied.
  - `AudioCategory::SetVolume` only affects a category's *future* `Play()` calls, not sounds
    already playing (a dead re-apply loop exists in `AudioEngine::SetCategoryVolumeInternal`).

---

## 3. Recent changes (this branch, newest first)

- _(uncommitted)_ — T-4A step 1: `Microphone::getAllProperty()` now enumerates real SDL3 recording
  devices (`SDL_GetAudioRecordingDevices`/`SDL_GetAudioDeviceName`) instead of always being empty;
  leads with a synthetic "Default Device" entry per FNA's `SDL3_FNAPlatform.GetMicrophones`, but
  does **not** open any device at enumeration time (deviation: CNA opens capture devices lazily in
  `Start()`, matching `AudioMixer`'s lazy-open convention — see `Microphone.cpp` comment). Updated
  `MicrophoneTests.cpp`'s two static-discovery tests, which asserted an always-empty list; that
  assumption no longer holds since the SDL `dummy` driver itself always reports one recording
  device. Added a file-scope static initializer to pin `SDL_AUDIODRIVER=dummy` before any test
  runs (`getAllProperty()`'s result is cached for the process lifetime). Net +1 test (1969→1970).
- `b8e2eec` — T-2F: deleted dead/buggy XGS first-pass category loop in `XactParser.cpp`; removed
  a redundant `variationOffset` re-seek (now captured once at its sequential read site). Added
  `XactParserTest.XgsParsesCategoryAndVariable`, verified identical pass/fail via `git stash`
  against pre- and post-cleanup code. +1 test.
- `54778b1` — T-2E: track-event walker no longer `break`s on PITCH/VOLUME/MARKER(+repeating)
  events; skips them correctly (byte layouts verified against FAudio) so a later `PlayWave` in the
  same track is still found. +2 tests.
- `270bbb4` — T-1C/T-1D/T-1H/T-5M: `Microphone` compliance (`GetTypeName`, `System::` exceptions,
  `micList`/`SAMPLERATE` made private/un-`NOXNA`'d); added `Microphone::CheckAllBuffers()`,
  simplifying `FrameworkDispatcher.cpp`. +24 tests.
- `aa76549` / `ec6a930` — T-2G: `AudioCategory::Equals` fixed to compare by Name (was
  parent+index, violating the Equals/GetHashCode contract); doxygen corrected. +11 tests.
- `24af33c` — T-1F/T-3A: `Cue` exceptions → `System::`, dotted+public `GetTypeName`,
  `GetVariable`/`SetVariable` validated against `AudioEngine`'s variable set + built-in 3D
  variables. +28 tests.
- `7be3513` — T-1F/T-3A/T-3B: `SoundBank`/`WaveBank` exceptions, `GetTypeName`, real `IsInUse`
  (new `WaveBank::RegisterCue`/`UnregisterCue`); fixed a real bug (wrong param name in
  `WaveBank`'s streaming-ctor validation). +32 tests.
- `0494439` — T-1F/T-1B/T-3A/T-3D: `AudioEngine`/`RendererDetail` exceptions, `GetTypeName`,
  `RendererDetail::Equals`. +31 tests.
- `2bafede` — T-2D: fixed compact-`.xwb` `dataLength` (was reading the raw 11-bit deviation
  field as the length instead of deriving it from consecutive entry offsets). +1 test.
- `704aae5` — T-1A: SPDX headers added to the 4 internal audio files.
- Earlier (pre-dating this session, already on branch): `aca2712`/`7b3d8fa`/`443b501`/`4106674` —
  `SoundEffect`/`SoundEffectInstance`/`DynamicSoundEffectInstance` exception fixes and bug fixes
  (loop-property override hiding, `Dispose()` leak, float-buffer format mismatch), `AudioEmitter`
  exception fix, base-exception rebase onto `sharp-runtime`.

Full detail for any of the above: `git show <hash>` or the task IDs in `plan_audio.md`.

---

## 4. Current blocker / main problem

**No blocker.** Build is clean, all 1969 tests pass.

Every incremental item in `plan_audio.md`'s bug-fix/compliance plan is done. The **only** open
item on the audio task list is **T-4A (real SDL microphone capture)** — a genuinely different,
larger feature (device enumeration + streaming capture) rather than a same-shaped small fix.
Recommend scoping it with the user (see §8) before starting rather than treating it as "the next
small task."

**Sibling-repo note:** this branch's build depends on `../sharp-runtime`, which has (separately)
been under active, uncommitted development in recent sessions — twice a fresh rebuild here briefly
failed on `-Werror` in an in-progress sharp-runtime class (`DateTime`, then `Decimal`), and both
times it resolved itself minutes later once that unrelated work landed. If a build ever fails
inside `SHARP_RUNTIME/CMakeFiles/...` rather than `CNA`/`CnaTests`, check `git status`/`git log -1
--format=%cd` in `../sharp-runtime` before assuming the audio code broke something.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|--------|-------|-----|
| **Incomplete** | `Microphone` capture is a stub — no real SDL recording; `All`/`GetData` never produce real devices/bytes | T-4A |
| **Real gap (untasked)** | `AudioCategory::SetVolume` doesn't retroactively re-apply to already-playing cues | — |
| **Accepted deviation** | `SoundBank::IsInUse` reflects only fire-and-forget cues it owns (from `PlayCue`); cues obtained via `GetCue` are caller-owned and not tracked, unlike FNA's FACT-engine-level tracking | T-3B |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global variable set + a built-in 3D-variable set, not a true per-cue-instance-overridable catalog (the `ACCESSIBILITY_CUE` bit isn't tracked) | T-3A |
| **Accepted limitation** | SDL_mixer: 3D HRTF + Doppler stored, never applied; streaming wavebanks always loaded fully into memory | plan §2 |
| **FNA-faithful dead code** | `Microphone::setBufferDurationProperty`'s `milliseconds > 1000` branch is unreachable (`TimeSpan::getMillisecondsProperty()` is bounded to `[-999,999]`); kept as-is to match FNA, not "fixed" | — |
| **Minor / intentional** | `SoundEffect::GetSampleDuration` truncates to whole ms (FNA); `DynamicSoundEffectInstance::GetSampleDuration` keeps float precision | — |
| **Needs verification** | Device-dependent audio tests rely on the SDL `dummy` driver; skip when no device is available — never verified against a *real* device in CI | — |

All previously-tracked bugs (T-1A–T-1H, T-2A–T-2G, T-3A–T-3E, T-5A–T-5O except T-5M) are fixed;
see `plan_audio.md` for the full checked-off list.

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
- **Device opened lazily** by `CNA::Internal::Audio::GetMixer()` on first playback; all SDL_mixer
  code is gated by `#ifdef SOUND_ENABLED`.
- **Exceptions on the XNA surface must be `System::` types**, never `std::runtime_error`/`std::out_of_range`.
- **`GetTypeName()` returns the dotted .NET name** (`"Microsoft.Xna.Framework.Audio.X"`) and lives
  in the **public** section for every concrete `System::Object` subclass (this exact
  private-vs-public placement bug was found and fixed in every XACT class this branch touched —
  check it explicitly on any new class, don't assume the existing placement is correct).
- **`SoundEffectInstance::hasStarted_`** is set on `Play()` and never reset; gates `IsLooped`
  (set-after-play → `InvalidOperationException`).
- **`DynamicSoundEffectInstance`** must override **both** `setIsLoopedProperty(const bool&)` and
  `setIsLoopedProperty(bool&&)` as no-ops (a single-signature override silently hides instead of
  overriding).
- **`CreateInstance` returns by value** (no FNA-style instance tracking/Dispose cascade) — a
  documented, deliberate value-semantics deviation (plan D1). Do not "fix" it as a drive-by change.
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

# Device-dependent tests use the SDL dummy driver automatically, but you can force it globally:
SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests --gtest_filter='SoundEffectInstanceTest.*'

# FNA reference source for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio
```

---

## 8. Next smallest tasks

_All plan_audio.md compliance/bugfix tasks are done. What remains is T-4A (real microphone
capture), broken down below into session-sized steps rather than one big task._

1. ~~**Enumerate real capture devices.**~~ **Done this session** (uncommitted). Handles are raw
   `SDL_AudioDeviceID`s (or the default-recording sentinel for entry 0) — nothing is opened yet.

2. **Wire `Start()`/`Stop()` to a real `SDL_AudioStream` capture stream.**
   - Goal: `Start()` opens a 44100 Hz/mono/S16 SDL3 audio capture stream for this device; `Stop()`
     closes it. No byte reading yet — just correct stream lifecycle and `MicrophoneState`
     transitions.
   - Files: `Microphone.hpp` (new stream handle field), `Microphone.cpp`.
   - Verify: same test filter as above; must still pass with `SDL_AUDIODRIVER=dummy` (no real
     device) without crashing.

3. **Wire `GetData`/`GetQueuedBytes` to the real stream.**
   - Goal: `GetData` reads via `SDL_GetAudioStreamData`, `GetQueuedBytes` via
     `SDL_GetAudioStreamAvailable`, replacing today's always-0-bytes stub. Keep existing bounds
     checks unchanged.
   - Files: `Microphone.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='MicrophoneTest.*'`; manually confirm
     `Start()` → speak/make noise → `GetData()` returns >0 bytes on a machine with a microphone.

4. **Extend `MicrophoneTests.cpp` for real capture.**
   - Goal: add capture-dependent tests (state transitions through real `Start`/`Stop`, non-zero
     `GetData` after capture) that `GTEST_SKIP` when no real device is available, matching the
     dummy-driver skip pattern already used for playback tests in this branch.
   - Files: `tests/Microsoft/Xna/Framework/Audio/MicrophoneTests.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='MicrophoneTest.*'` (passes/skips cleanly
     both with and without a real device).

---

## 9. Do not do yet

- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No instance-tracking / Dispose-cascade redesign** of `CreateInstance` — value semantics is a
  documented deviation (plan D1); changing it is a deliberate decision, not a drive-by fix.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep as documented stored-not-applied.
- **No broad `XactParser` rewrite** — the three known data bugs are fixed; don't use T-4A or
  anything else as an excuse to restructure it further. It's load-bearing and coverage is thin
  (4 tests).
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos/clones;
  if a build breaks there, wait or ask, don't "fix" it from this branch.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — add docs only when touching a file for another reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md for background if needed.
Inspect only the files needed for the first task in NEXT.md §8. Do not refactor unrelated code.
Do not touch the Media namespace or the sibling ../cna / ../sharp-runtime checkouts.

Make ONE small, verified improvement: task #2 in NEXT.md §8 (wire Start()/Stop() to a real
SDL_AudioStream capture stream). Task #1 (real device enumeration) is done. This is step 2 of
T-4A (real microphone capture) — the only remaining item on the audio task list; everything else
in plan_audio.md is done.

Build and test:
  cmake --build cmake-build-debug --target CnaTests -j"$(nproc)"
  ./cmake-build-debug/CnaTests --gtest_filter='MicrophoneTest.*'

Keep audio exceptions as System:: types, GetTypeName dotted and public, SPDX + Doxygen present.
After finishing, update NEXT.md (status, recent changes, next task) and commit.
```
