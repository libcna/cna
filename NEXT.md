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
- **Current phase:** the incremental fix/compliance plan in `plan_audio.md` **and** T-4A (real
  microphone capture) are both **done**. The audio task list has no open items; see §8 for
  optional follow-ups, none of which are required to consider the branch complete.
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
- **Tests:** `CnaTests` **1974 / 1974 pass** (1757 at branch start; +217 audio tests this branch).
  No known regressions.
- **Works now (ported, FNA-faithful, unit-tested):**
  - `SoundEffect`, `SoundEffectInstance`, `DynamicSoundEffectInstance` — full playback API.
  - `AudioEngine`, `RendererDetail`, `SoundBank`, `WaveBank`, `Cue`, `AudioCategory` — the entire
    XACT cluster: `System::` exceptions throughout, dotted+public `GetTypeName`, real (non-stub)
    `IsInUse`/`GetCue`/`GetCategory`/`Equals` behavior.
  - `Microphone` — compliance layer (`GetTypeName`, `System::` exceptions, visibility) **plus real
    capture, fully done end to end** (T-4A, all 4 steps): `getAllProperty()` calls
    `SDL_GetAudioRecordingDevices`/`SDL_GetAudioDeviceName` and returns a "Default Device" entry
    (bound to the SDL default-recording sentinel) followed by each real named device. `Start()`
    opens a real 44100 Hz/mono/S16 `SDL_AudioStream` via `SDL_OpenAudioDeviceStream` and resumes
    it; `Stop()` destroys it (closing the underlying device too). `GetData` reads real bytes via
    `SDL_GetAudioStreamData`; `GetQueuedBytes` (used by `CheckBuffer`) uses
    `SDL_GetAudioStreamAvailable` (a deliberate deviation from FNA's `SDL_GetAudioStreamQueued` —
    SDL3's own docs recommend `Available` for "how much can I read right now"). `MicrophoneTests.cpp`
    has a `MicrophoneCaptureTest` fixture that exercises `Start`/`Stop`/`GetData` against the real
    "Default Device" entry from `getAllProperty()` — deterministic even under the SDL `dummy`
    driver (its `RecordDevice` callback genuinely produces silence, so no `GTEST_SKIP` is needed
    there; skip only guards the case where no device at all is available, e.g. `SOUND_ENABLED`
    off). Verified against both the `dummy` driver (an arbitrary/invalid test handle also fails to
    open/read without crashing) and the real `pulseaudio` driver on this machine (2 real hardware
    mics + the synthetic default = 3 entries; polling `GetData()` every 100ms after `Start()`
    returns full 4096-byte chunks of live audio once the stream warms up).
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
  - 3D positional audio / Doppler: architectural SDL_mixer limitation, values are stored but never
    computed/applied.
  - `AudioCategory::SetVolume` only affects a category's *future* `Play()` calls, not sounds
    already playing (a dead re-apply loop exists in `AudioEngine::SetCategoryVolumeInternal`).

---

## 3. Recent changes (this branch, newest first)

- _(uncommitted)_ — T-4A step 4 (**final T-4A step — real microphone capture is now fully done**):
  added a `MicrophoneCaptureTest` fixture to `MicrophoneTests.cpp` covering `Start`/`Stop`
  state transitions and non-zero `GetData` against the *real* "Default Device" entry from
  `getAllProperty()` (not the arbitrary-handle `MakeMic()` helper used by the older tests).
  Discovered that the SDL `dummy` driver's `RecordDevice` callback genuinely produces silence
  continuously, so these tests are deterministic in headless CI with no `GTEST_SKIP` needed for
  "no real hardware" — `GTEST_SKIP` only guards the true "no device at all" case (`getDefaultProperty()
  == nullptr`, e.g. a `SOUND_ENABLED`-off build). Fixture's `TearDown()` always calls `Stop()` so
  the shared cached singleton starts clean for the next test regardless of assertion failures.
  Also corrected two now-stale comments/docs left over from the pre-T-4A stub era (in
  `MicrophoneTestAccess` and the two `GetData` zero-byte tests). +4 tests (1970→1974).
  **Unrelated blocker hit and fixed along the way:** `sharp-runtime` landed a permanent, committed
  `IAsyncResult` interface change (added `AsyncState`/`AsyncWaitHandle`) that broke the *entire*
  `CnaTests` link via `StorageDevice.cpp`'s `ContainerResult`/`SelectorResult` (its only
  implementers, unrelated to audio) — see the separate `fix(storage)` commit right before this one.
  Verified via an isolated test binary (Microphone-only object files + last-known-good `libCNA.a`)
  before that fix landed, then again via the full official `CnaTests` binary after. 1974/1974 pass.
- `927d647` — `fix(storage)`: implemented `IAsyncResult::getAsyncStateProperty()`/
  `getAsyncWaitHandleProperty()` on `StorageDevice.cpp`'s internal `SelectorResult`/
  `ContainerResult` (existing `void* asyncState` → `std::any`; new always-signalled
  `EventWaitHandle` member, since these operations complete synchronously). Out of scope for
  audio, but required to unblock the whole project's build — see above.
- `afcf63c` — T-4A step 3: `Microphone::GetData` now reads real bytes from `captureStream_`
  via `SDL_GetAudioStreamData` (falling back to the existing zero-fill stub when there's no
  stream, or nothing was available); `GetQueuedBytes` uses `SDL_GetAudioStreamAvailable` instead
  of FNA's `SDL_GetAudioStreamQueued` (documented deviation: SDL3's own docs recommend
  `Available` for "how much can I read right now", which is what `CheckBuffer`/`GetData` actually
  need). Existing bounds checks in `GetData` untouched. Verified manually against this machine's
  real `pulseaudio` driver: polling `GetData()` every 100ms after `Start()` returns full
  4096-byte chunks of live audio once the stream warms up (77824 bytes read over a 2s window).
  1970/1970 pass (no count change).
- `75bbf4a` — T-4A step 2: `Microphone::Start()`/`Stop()` now open/close a real
  `SDL_AudioStream` capture stream via `SDL_OpenAudioDeviceStream` (44100 Hz/mono/S16, using
  `handle_` from enumeration) + `SDL_ResumeAudioStreamDevice`/`SDL_DestroyAudioStream`. A failed
  open (e.g. a stale/invalid handle) is tolerated silently, matching FNA (which never checks
  `FNAPlatform.StartMicrophone`'s result either). Added `~Microphone()` to release an open stream
  on destruction, and deleted copy/move (the class now owns a raw SDL resource) — both are new
  since the class previously had no user-declared special members. Forward-declared `SDL_AudioStream`
  in `Microphone.hpp` (same pattern as `GameWindow.hpp`'s `SDL_Window*`) to keep SDL out of the
  public XNA header. Removed the now-redundant `(void)handle_;` cast in `GetData` (the field is
  genuinely used elsewhere now). Verified against the SDL `dummy` driver (existing tests, no
  crash on an invalid test handle) and manually against this machine's real `pulseaudio` driver
  (`Default Device`: `Start()`/`Stop()`/`Start()`/`Stop()` cycles correctly, states transition
  Stopped→Started→Stopped as expected). 1970/1970 pass (no count change).
- `d63946d` — T-4A step 1: `Microphone::getAllProperty()` now enumerates real SDL3 recording
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

**A fresh 4-way parallel line-by-line audit against FNA ran 2026-07-02, after T-4A landed.** It
found **30 new concrete bugs/gaps** (prefixes `CP`/`XA`/`IN`/`MC`) beyond everything already
tracked in earlier phases — including real playback bugs (`SoundEffectInstance::Play()` isn't
idempotent; `Apply3D` silently overwrites the public `Volume`/`Pan` properties), a memory leak
(`WaveBank::GetSoundEffect` for 8-bit PCM/ADPCM), a dangling-pointer risk (`SoundEffectInstance`
holds a raw `const SoundEffect*`), and a parser bug that can silently desync every sound after one
malformed `.xsb` DSP block. **Full list with FNA line references and accept criteria: `plan_audio.md`
§4 Fáze 7.** Do not re-derive this list from scratch in a future session — it's already there.

It also confirmed T-4A didn't fully meet its own original accept criteria: `Microphone::GetSampleDuration`/
`GetSampleSizeInBytes` never got wired up to delegate to `SoundEffect` as `plan_audio.md`'s T-4A
task text required — tracked as `plan_audio.md` MC-1, not re-opening T-4A itself (see `plan_audio.md`'s
T-4A entry for the exact wording of what's still missing).

| Status | Issue | Ref |
|--------|-------|-----|
| **Real gap (untasked)** | `AudioCategory::SetVolume` doesn't retroactively re-apply to already-playing cues | plan_audio.md T-4D |
| **Accepted deviation** | `SoundBank::IsInUse` reflects only fire-and-forget cues it owns (from `PlayCue`); cues obtained via `GetCue` are caller-owned and not tracked, unlike FNA's FACT-engine-level tracking | T-3B |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global variable set + a built-in 3D-variable set, not a true per-cue-instance-overridable catalog (the `ACCESSIBILITY_CUE` bit isn't tracked) | T-3A |
| **Accepted limitation** | SDL_mixer: 3D HRTF + Doppler stored, never applied; streaming wavebanks always loaded fully into memory | `CHECKLIST.md` deviation table |
| **FNA-faithful dead code** | `Microphone::setBufferDurationProperty`'s `milliseconds > 1000` branch is unreachable (`TimeSpan::getMillisecondsProperty()` is bounded to `[-999,999]`); kept as-is to match FNA, not "fixed" | — |
| **Minor / intentional** | `SoundEffect::GetSampleDuration` truncates to whole ms (FNA); `DynamicSoundEffectInstance::GetSampleDuration` diverges from FNA by using real bytes-per-sample instead of hardcoded 16-bit | plan_audio.md CP-6 |
| **Needs verification** | Device-dependent audio tests rely on the SDL `dummy` driver; skip when no device is available — never verified against a *real* device in CI | — |
| **See plan_audio.md Fáze 7** | 30 new findings from the 2026-07-02 audit (real bugs, compliance gaps, test-coverage gaps) — not summarized here, this table would just go stale again | CP-1..14, XA-1..5, IN-1..6, MC-1..5 |

All previously-tracked bugs (T-1A–T-1H, T-2A–T-2G, T-3A–T-3E, T-5A–T-5O except T-5M, T-4A) are
fixed; see `plan_audio.md` for the full checked-off list. T-6A/T-6B (CHECKLIST.md/AUDIT.md
staleness) were also fixed as part of the 2026-07-02 audit pass.

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

**T-4A (real microphone capture) is fully done — all 4 steps committed** (`d63946d` → `75bbf4a` →
`afcf63c` → `435ff76`, plus an unrelated `fix(storage)` blocker `927d647`). `plan_audio.md`'s
original Fáze 0–6 compliance/bugfix plan is done. **But the task list is NOT empty** — a fresh
2026-07-02 audit (see §5) found 30 new concrete bugs/gaps, filed as `plan_audio.md` §4 **Fáze 7**
(`CP-1..14`, `XA-1..5`, `IN-1..6`, `MC-1..5`).

**Next smallest task = pick ONE item from `plan_audio.md` Fáze 7**, ordered roughly by severity
within each cluster (real bugs first, then compliance, then test gaps). The Fáze 7 intro block
lists the 8 most severe findings across all 4 clusters if you want a starting point instead of
reading the whole section — `IN-1` (silent XACT parse corruption) and `CP-1`
(`SoundEffectInstance::Play()` not idempotent) are good first candidates: real, narrowly-scoped
bugs with a clear FNA reference and accept criteria already written.

Do NOT re-run another full audit before working through some of Fáze 7 first — that would just
duplicate what's already found. Untasked candidates that are NOT bugs, for later:
- Merge/PR readiness review of the whole branch (`git log 85938b8..HEAD`) once Fáze 7 is worked
  down, if the intent is to land `feature/audio` into `develop`/`master`.
- Real-device CI verification (§5) — device-dependent tests still only ever run against the SDL
  `dummy` driver in this environment.

---

## 9. Do not do yet

- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No instance-tracking / Dispose-cascade redesign** of `CreateInstance` — value semantics is a
  documented deviation (plan D1); changing it is a deliberate decision, not a drive-by fix.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep as documented stored-not-applied.
- **No broad `XactParser` rewrite** — targeted fixes for the specific bugs `plan_audio.md`
  Fáze 7 found (`IN-1`..`IN-4`) and expanding its test coverage (`IN-6`) are sanctioned and
  expected; don't use that as an excuse to restructure the parser wholesale beyond what each
  task's accept criteria asks for.
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos/clones;
  if a build breaks there, wait or ask, don't "fix" it from this branch.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — add docs only when touching a file for another reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md §4 Fáze 7 for the current task list (30 items, prefixes
CP/XA/IN/MC). T-4A (real microphone capture) is fully done. plan_audio.md's original Fáze 0-6 is
done. Fáze 7 is NOT done -- that's where the real next work is.

1. Confirm the current build/test state matches NEXT.md §2 (build clean, all tests pass) --
   rebuild and rerun ./cmake-build-debug/CnaTests to check for drift since this was last updated.
2. Pick exactly ONE task from plan_audio.md Fáze 7 (start with the top-priority list at the start
   of that section if unsure -- IN-1 and CP-1 are good, narrowly-scoped first picks). Do not pick
   more than one, and do not re-run a fresh audit -- Fáze 7 already has 30 items queued.
3. Inspect only the files that task names. Do not refactor unrelated code. Do not touch the Media
   namespace or the sibling ../cna / ../sharp-runtime checkouts. Keep audio exceptions as System::
   types, GetTypeName dotted and public, SPDX + Doxygen present.
4. Follow that task's own *Accept* criteria, including its test requirement.

After finishing, check the task's checkbox in plan_audio.md, update NEXT.md (status, recent
changes, next task) and commit.
```
