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
  microphone capture) are both done. The branch is now working through **Fáze 7** — a fresh
  line-by-line re-audit against FNA run 2026-07-02 that found **30 new concrete bugs/gaps**
  (prefixes `CP`/`XA`/`IN`/`MC`). **5 of 30 are fixed** (`IN-1`, `CP-1`, `CP-3`, `XA-2`, `XA-1`);
  **25 remain**, plus a handful of pre-existing older items (`T-3F`, `T-3G`, `T-4B`, `T-4C`,
  `T-4D`, `T-6C`) that were never in scope for this audit and are still open too.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is
  parsed by a hand-written `XactParser` and mixed through SDL_mixer. Consequences: 3D HRTF and
  Doppler are stored but never applied; wavebanks are always loaded fully into memory (no real
  streaming); `SoundEffect::CreateInstance()`/`FromStream()` use C++ value semantics, not FNA's
  reference-counted instance tracking.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface. It is under **separate,
  active, concurrent development** by another session — see §4.

---

## 2. Current status

- **Build:** clean as of the last commit (`0c2fa58`). EasyGL backend, `SOUND_ENABLED` on,
  SDL3_mixer linked. Not re-verified in this update (per instruction not to build).
- **Tests:** `CnaTests` **1981 / 1981 pass**, last run immediately before committing `0c2fa58`
  (1757 at branch start; +224 audio tests this branch). No known regressions.
- **CLI/tools/apps:** none. This repo is a library/framework, not an application — there is no
  standalone audio demo or CLI in this branch. Verification is via `CnaTests` (Google Test) plus
  ad-hoc manual scratch probes against real SDL/SDL_mixer (documented in commit messages, not
  checked into the repo).
- **Recently implemented (this branch):**
  - Full XACT cluster port (`AudioEngine`, `SoundBank`, `WaveBank`, `Cue`, `AudioCategory`,
    `RendererDetail`) with `System::` exceptions, dotted+public `GetTypeName`, real (non-stub)
    `IsInUse`/`GetCue`/`GetCategory`/`Equals`.
  - `SoundEffect`, `SoundEffectInstance`, `DynamicSoundEffectInstance` full playback API.
  - **T-4A: real SDL3 microphone capture, end to end** — `Microphone::getAllProperty()` enumerates
    real recording devices (`SDL_GetAudioRecordingDevices`), `Start()`/`Stop()` open/close a real
    `SDL_AudioStream` (`SDL_OpenAudioDeviceStream`), `GetData()`/`GetQueuedBytes()` read real bytes
    (`SDL_GetAudioStreamData`/`SDL_GetAudioStreamAvailable`). Verified against both the SDL `dummy`
    driver and this machine's real `pulseaudio` driver (2 real hardware mics).
  - **Fáze 7 fixes so far:** a silent XACT `.xsb` parse-corruption bug (`IN-1`), a
    non-idempotent `SoundEffectInstance::Play()` (`CP-1`), `Apply3D` clobbering the public
    `Volume`/`Pan` properties (`CP-3`), a `SoundEffect` memory leak in `WaveBank::GetSoundEffect`
    (`XA-2`), and `SoundBank::PlayCue` force-stopping long-playing fire-and-forget cues (`XA-1`).
- **Does NOT work yet / known incomplete:**
  - 25 Fáze 7 findings are still open — see `plan_audio.md` §4 Fáze 7 for the full list with FNA
    references and accept criteria. Highlights: `SoundEffectInstance` holds a raw dangling-prone
    `const SoundEffect*` (`CP-7`); `Cue::Play` ignores authored variation weights (`XA-3`); a
    non-compact `.xwb` over-read into adjacent memory (`IN-2`) and a related integer-underflow
    bounds-check bypass (`IN-3`); `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` never got
    wired to delegate to `SoundEffect` as T-4A originally required (`MC-1`).
  - 3D positional audio is pan + distance-attenuation only (no elevation, no Doppler) — accepted
    SDL_mixer limitation, documented in `CHECKLIST.md`.
  - Streaming `WaveBank` constructor still loads the whole file into memory — accepted deviation
    (`T-3F`, undecided whether to ever implement real streaming).
  - `AudioCategory::SetVolume` does not retroactively affect already-playing cues, only future
    `Play()` calls (`T-4D`).

---

## 3. Recent changes (this branch, newest first)

- `0c2fa58` — **XA-1**: `SoundBank::PlayCue`'s fire-and-forget sweep now removes cues by
  `!IsPlaying` instead of "older than 5 seconds", so long one-shot/music cues no longer get cut
  off; added a 5-minute safety-net timeout as a backstop. +2 tests.
- `b8f1a1f` — **XA-2**: fixed a `SoundEffect` memory leak in `WaveBank::GetSoundEffect`'s
  8-bit-PCM/ADPCM branches (`FromStream`'s heap pointer was never freed). Verified with a scratch
  ASan+LeakSanitizer build. +1 test.
- `6280c51` — **CP-3**: `Apply3D` no longer overwrites the public `Volume`/`Pan` properties;
  applies the computed 3D attenuation directly to the underlying `MIX_Track` instead. Also added a
  missing `ObjectDisposedException` check. +2 tests.
- `800d1e9` — **CP-1**: `SoundEffectInstance::Play()` is now idempotent while already `Playing`
  (previously restarted playback from frame 0 on every repeated call). +1 test.
- `8acd86e` — **IN-1**: fixed `ParseXsb`'s XACT `.xsb` DSP-block skip, which could silently desync
  parsing for every sound after a malformed block. +1 test.
- `4c905a5` — docs only: added `plan_audio.md` §4 Fáze 7 (the 30-item audit backlog); fixed stale
  "stub behavior" rows in `AUDIT.md`; added 5 audio-specific rows to `CHECKLIST.md`'s deviation
  table.
- `927d647` — unrelated `fix(storage)`: implemented `System::IAsyncResult`'s `AsyncState`/
  `AsyncWaitHandle` on `StorageDevice.cpp`'s internal result classes, needed to unblock the whole
  project's build after a `sharp-runtime` interface change (see §4).
- `435ff76`, `afcf63c`, `75bbf4a`, `d63946d` — the four T-4A steps (enumeration, Start/Stop stream
  lifecycle, GetData/GetQueuedBytes, capture-dependent tests).

Full detail for any of the above: `git show <hash>`, or the matching task ID in `plan_audio.md`.

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker right now.** The build was clean and all 1981 tests passed as
of the last commit.

The actual "main problem" is simply that **Fáze 7 is unfinished**: 25 of its 30 findings are still
open in `plan_audio.md` §4. There is no single failing command or failing test to chase — this is
a backlog of discrete, independently-scoped bugs/gaps found by a fresh audit against FNA, each
with its own accept criteria already written.

**Known recurring hazard (not currently active):** this branch's build depends on `../sharp-runtime`,
which is under separate, active, concurrent development by another session. Twice this session, a
fresh rebuild briefly failed because of an in-progress interface change there (`IAsyncResult`
gaining new pure-virtual methods broke `StorageDevice.cpp`, fixed in `927d647`). If a future build
fails inside `SHARP_RUNTIME/CMakeFiles/...` or in an unrelated non-Audio file, check `git status`/
`git log -1` in `../sharp-runtime` before assuming the audio code broke something — it may just
need a retry once that unrelated work lands, or a small compliance patch like `927d647`.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Confirmed, open** | 25 Fáze 7 findings — full list with FNA line references and accept criteria | `plan_audio.md` §4 Fáze 7 |
| **Confirmed, open** | `AudioCategory::SetVolume` doesn't retroactively re-apply to already-playing cues | `T-4D` |
| **Confirmed, open** | Streaming `WaveBank` ctor ignores `offset`/`packetSize`, always loads the whole file | `T-3F` |
| **Confirmed, open** | `SoundEffect::CreateInstance()`/`FromStream()` value semantics — no instance-tracking/Dispose-cascade | `T-3G`, entangled with open decision `D5` (`CP-7`) |
| **Accepted deviation** | 3D positional audio is pan + distance-attenuation only, no elevation/Doppler | `CHECKLIST.md` |
| **Accepted deviation** | `SoundBank::IsInUse` only tracks fire-and-forget cues it owns; `GetCue`-obtained cues are caller-owned | `T-3B` |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global set + built-in 3D variables, not a true per-cue-instance catalog | `T-3A` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver in CI/this environment; real-hardware runs this session were manual and ad-hoc, not automated | — |
| **Open design decision** | `D5`: `SoundEffect`/`SoundEffectInstance` ownership (dangling-safe contract vs. shared ownership) — blocks starting `CP-7` | `plan_audio.md` §6 |
| **Open design decision** | `D7`: throw vs. saturating clamp on corrupted XACT data — needed before `IN-2`/`IN-3` | `plan_audio.md` §6 |
| **Open design decision** | `D8`: `Microphone::GetData` buffer-zero-on-error semantics — needed before `MC-3` | `plan_audio.md` §6 |

All previously-tracked bugs from Fáze 0–6 (`T-1A`–`T-1H`, `T-2A`–`T-2G`, `T-3A`–`T-3E`,
`T-5A`–`T-5O`, `T-4A`, `T-6A`, `T-6B`) are fixed; see `plan_audio.md` for the full checked-off list.

---

## 6. Architecture notes

### Main modules

| Component | Location | Notes |
|---|---|---|
| XNA audio API | `include/Microsoft/Xna/Framework/Audio/`, `src/.../Audio/` | Must match XNA 4.0 / FNA exactly |
| Internal mixer | `CNA/Internal/Audio/AudioMixer.{hpp,cpp}` | SDL3_mixer `MIX_Mixer` singleton via `GetMixer()`; single 44100/stereo/S16 device (per-audio sample rate is set separately when loading each `MIX_Audio`, so non-44100Hz content is not broken — verified, not a bug) |
| XACT parser | `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp` | Custom `.xgs`/`.xsb`/`.xwb` reader (FACT is **not** used); load-bearing, coverage still thin (`IN-6`) |
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
Microphone (capture)
  → getAllProperty() enumerates via SDL_GetAudioRecordingDevices
  → Start() opens a real SDL_AudioStream (SDL_OpenAudioDeviceStream); Stop() destroys it
  → GetData()/GetQueuedBytes() read from that stream
```

### Invariants / rules that must stay stable

- **Backend = SDL3_mixer only.** Do not reintroduce FAudio/FACT.
- **Device opened lazily.** `AudioMixer::GetMixer()` opens the shared playback device on first use;
  `Microphone::Start()` opens its capture stream on first use, not at enumeration time. Both are
  gated by `#ifdef SOUND_ENABLED`.
- **Exceptions on the XNA surface must be `System::` types**, never `std::runtime_error`/
  `std::out_of_range`/`std::invalid_argument`.
- **`GetTypeName()` returns the dotted .NET name** (`"Microsoft.Xna.Framework.Audio.X"`) and lives
  in the **public** section for every concrete `System::Object` subclass.
- **`SoundEffectInstance::hasStarted_`** is set on `Play()` and never reset; gates `IsLooped`
  (set-after-play → `InvalidOperationException`).
- **`DynamicSoundEffectInstance`** must override **both** `setIsLoopedProperty(const bool&)` and
  `setIsLoopedProperty(bool&&)` as no-ops (a single-signature override silently hides instead of
  overriding).
- **`CreateInstance` returns by value** (no FNA-style instance tracking/Dispose cascade) — a
  documented, deliberate value-semantics deviation. Do not "fix" it as a drive-by change; it needs
  its own decision (`D5`/`T-3G`).
- **`Apply3D` must not mutate the public `Volume`/`Pan` properties** (fixed this session as `CP-3`
  — don't reintroduce the old bug).
- **`SoundBank`'s fire-and-forget sweep is event-based (`!IsPlaying`), not purely time-based**
  (fixed this session as `XA-1` — don't reintroduce the old "older than N seconds" bug).
- **`Cue` never self-transitions out of `Playing`** without an explicit `Stop()`/`Pause()` call —
  no real playback-finished detection at the `Cue` level (unlike `SoundEffectInstance`, which
  queries the real `MIX_Track` state). This is relied on by `SoundBankTestAccess`'s
  backdating-based tests; don't "fix" this as an unrelated drive-by without checking those tests.
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

# One-off leak verification (used for XA-2; NOT part of the normal build, delete after use):
cmake -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=leak -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -fsanitize=leak"
cmake --build cmake-build-asan --target CnaTests -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=1 ./cmake-build-asan/CnaTests --gtest_filter='<TestName>'
rm -rf cmake-build-asan   # clean up afterward

# FNA reference source for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio

# FAudio C reference for XACT byte-format details not documented elsewhere
grep -n "<symbol>" /rv/data/library/github.com/FNA-XNA/FAudio/src/FACT_internal.c
```

---

## 8. Next smallest tasks

Pick **exactly one** from `plan_audio.md` §4 Fáze 7 per session. Suggested order (see that file
for full FNA references and accept criteria — do not re-derive them from scratch):

1. **`IN-2` — non-compact `.xwb` entry `entryMetaDataSize < 24` over-reads into adjacent memory.**
   - Goal: read fields conditionally/bounded by `entryMetaDataSize` instead of always reading all
     24 bytes before checking the size.
   - Files: `src/CNA/Internal/Audio/XactParser.cpp:458-476`.
   - Verify: add a non-compact `.xwb` fixture with `entryMetaDataSize < 24` to
     `tests/CNA/Internal/Audio/XactParserTests.cpp`; `./cmake-build-debug/CnaTests --gtest_filter='XactParserTest.*'`.

2. **`IN-3` — integer underflow in compact-`.xwb` `dataLength` can bypass `WaveBank`'s bounds
   check.** Related to `IN-2` (same file, same `D7` decision on throw-vs-clamp) — consider doing
   both in one session.
   - Files: `src/CNA/Internal/Audio/XactParser.cpp:449-452`, `src/Microsoft/Xna/Framework/Audio/WaveBank.cpp:221-228`.
   - Verify: fixture with an artificially large compact-entry deviation; same test command as above.

3. **`MC-1` — `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` never delegate to
   `SoundEffect`** as T-4A's own accept criteria required; own formula differs from FNA on
   non-integer-ms boundaries.
   - Files: `src/Microsoft/Xna/Framework/Audio/Microphone.cpp:161-176`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='MicrophoneTest.*'`.

4. **`CP-8` — `SoundEffect` doesn't inherit `System::Object`, has no `GetTypeName()`**, unlike
   every sibling class in this cluster.
   - Files: `include/Microsoft/Xna/Framework/Audio/SoundEffect.hpp`,
     `src/Microsoft/Xna/Framework/Audio/SoundEffect.cpp`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='SoundEffectTest.*'`.

5. **`XA-3` — `Cue::Play` ignores authored `weightMin`/`weightMax`**, always picks a wave
   uniformly at random instead of weighted.
   - Files: `src/Microsoft/Xna/Framework/Audio/Cue.cpp:139-197`,
     `include/CNA/Internal/Audio/XactTypes.hpp:88-105`.
   - Verify: `./cmake-build-debug/CnaTests --gtest_filter='CueTest.*'`.

Do **not** start `CP-7` (needs `D5` decided first) or `MC-3` (needs `D8` decided first) — see §5.

---

## 9. Do not do yet

- **No re-running a fresh full audit.** Fáze 7 already has 25 open findings queued in
  `plan_audio.md` — work through those before looking for more.
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No instance-tracking / Dispose-cascade redesign** of `CreateInstance` without deciding `D5`
  first (see `plan_audio.md` §6) — it's a deliberate decision, not a drive-by fix.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep as documented stored-not-applied.
- **No broad `XactParser` rewrite** — targeted fixes for specific Fáze 7 findings (`IN-2`–`IN-4`)
  and expanding its test coverage (`IN-6`) are sanctioned; don't restructure it wholesale beyond
  what each task's accept criteria asks for.
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos/clones. If
  a build breaks there, check whether it's the known concurrent-development hazard (§4) before
  "fixing" it from this branch; only patch CNA-side compliance code (like `927d647`) if truly
  needed to unblock the build.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — add docs only when touching a file for another reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md §4 Fáze 7 for the current task list.

1. Confirm the current build/test state matches NEXT.md §2 (build clean, 1981/1981 tests pass) --
   rebuild and rerun ./cmake-build-debug/CnaTests to check for drift since this was last updated.
2. Pick exactly ONE task from NEXT.md §8 (or another Fáze 7 item if you have a good reason -- but
   don't pick CP-7 or MC-3 without first deciding D5/D8 in plan_audio.md §6).
3. Inspect only the files that task names. Do not refactor unrelated code.
4. Make ONE small, verified improvement following that task's own *Accept* criteria in
   plan_audio.md, including its test requirement.
5. Run the task's verification command.

After finishing, check the task's checkbox in plan_audio.md, update NEXT.md (status, recent
changes, next task), and commit.
```
