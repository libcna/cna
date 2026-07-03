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
  microphone capture) were already done before this update. **Fáze 7** — a fresh line-by-line
  re-audit against FNA (run 2026-07-02) that found 30 concrete bugs/gaps (prefixes
  `CP`/`XA`/`IN`/`MC`) — is now **fully complete: 30 of 30 fixed/closed**. A handful of
  pre-existing older items from Fáze 3/4/6 (`T-3F`, `T-3G`, `T-4B`, `T-4C`, `T-4D`, `T-6C`) were
  never in scope for that audit and are still open — see §5.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is
  parsed by a hand-written `XactParser` and mixed through SDL_mixer. Consequences: 3D HRTF and
  Doppler are stored but never applied; wavebanks are always loaded fully into memory (no real
  streaming); `SoundEffect::CreateInstance()`/`FromStream()` use C++ value semantics, not FNA's
  reference-counted instance tracking (though `SoundEffectInstance` now keeps the underlying audio
  resource alive via shared ownership — see `CP-7` in §3).
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface. It is under **separate,
  active, concurrent development** by another session — see §4.

---

## 2. Current status

- **Build:** clean at `678258e` (`HEAD`). EasyGL backend, `SOUND_ENABLED` on, SDL3_mixer linked.
  Verified immediately before writing this update.
- **Tests:** `CnaTests` **2020 / 2020 pass** (1981 at the last handoff snapshot; +39 this session).
  No known regressions. Re-run to check for drift: `./cmake-build-debug/CnaTests`.
- **CLI/tools/apps:** none. This repo is a library/framework, not an application — there is no
  standalone audio demo or CLI in this branch. Verification is via `CnaTests` (Google Test) plus
  one-off scratch probes (ASan/LeakSanitizer builds, deleted after use — see §7).
- **This session's work (26 commits, all 30 Fáze 7 findings closed):**
  - **Bug fixes (13):** `IN-2`/`IN-3` (XWB parser over-read + integer-underflow bounds-check
    bypass), `MC-1` (Microphone sample-duration math), `CP-8`/`CP-9` (`SoundEffect` API
    compliance: `System::Object` inheritance, private internal ctor), `XA-3` (weighted variation
    selection), `IN-4` (reject unsupported XACT variation-table types instead of misparsing),
    `CP-2` (`Play()` pan/pitch validation), `CP-5` (`DynamicSoundEffectInstance::Stop(false)` now
    throws), `CP-6` (sample-size math ignoring float submission mode), `CP-4`
    (`PendingBufferCount` now tracks real stream consumption, not submission), `CP-7` (dangling
    `SoundEffect*` in `SoundEffectInstance` — **confirmed via a real ASan build**, see §7), `MC-3`
    (`Microphone::GetData` no longer zero-fills on a no-op read).
  - **Test-coverage additions (7):** `CP-10`, `CP-11`, `CP-12`, `CP-13`, `XA-5`, `IN-6`
    (`XactParserTests.cpp` 8→22 tests), `MC-4`, `MC-5`.
  - **Docs/cleanup (3):** `XA-4`, `IN-5`, `MC-2`; `CP-14` closed as already-satisfied by `CP-1`'s
    own regression test.
  - Every behavioral fix was verified with the established `git stash` methodology: stash the
    fix, rebuild, confirm the new regression test actually fails against the pre-fix code (several
    surfaced as genuine compile failures or ASan-detected memory errors, not just assertion
    failures), then restore and confirm green.
  - `plan_audio.md` §4 Fáze 7, `AUDIT.md`, and `CHECKLIST.md` are all updated; open decisions
    `D5`/`D7`/`D8` are resolved and documented in `plan_audio.md` §6.
- **Does NOT work yet / known incomplete (all pre-existing, outside Fáze 7's scope):**
  - `T-3F` — streaming `WaveBank` ctor still loads the whole file into memory (accepted
    deviation, documented in `CHECKLIST.md`; never decided whether to implement real streaming).
  - `T-3G` — `SoundEffect::CreateInstance()`/`FromStream()` still use C++ value semantics, not
    FNA's reference-counted instance tracking/Dispose-cascade (accepted deviation; `CP-7` this
    session fixed the specific dangling-pointer *safety* issue this caused, but did not change the
    underlying value-semantics decision).
  - `T-4B` — `Cue::Apply3D`/3D `PlayCue` are still no-ops; no pan/attenuation derived from
    listener/emitter geometry at the `Cue`/`SoundBank` level (note: `SoundEffectInstance::Apply3D`
    itself *does* work — that was `CP-3`, fixed earlier — this item is about the `Cue`-level API).
  - `T-4C` — no DSP filter/reverb routing (`applyReverb`/`applyLowPassFilter`/etc.) on
    `SoundEffectInstance`.
  - `T-4D` — `AudioEngine::SetCategoryVolumeInternal` is a documented no-op ("skipped for
    simplicity") — `AudioCategory::SetVolume()` never actually re-applies volume to already-
    playing cues, only affects future `Play()` calls. **Freshly re-confirmed this session** while
    writing `XA-5`'s test (noted there, not fixed — out of that task's scope).
  - `T-6C` — the formal "build & report" checklist step was never explicitly checked off, though
    its criteria (clean build, passing tests, changed-files report) were satisfied continuously
    throughout this session.

---

## 3. Recent changes (this branch, newest first)

All 26 commits below are this session's Fáze 7 closure work, `fed07f9`..`678258e`:

- `678258e` — **MC-4**: added `BufferReadyFiresWhenQueuedDataExceedsBufferDuration` (real capture,
  polls `CheckBuffer()` until `BufferReady` fires); fixed a latent test-infra issue
  (`MicrophoneCaptureTest::TearDown()` now clears `BufferReady` so a test-local lambda can't
  dangle into a later test sharing the same singleton mic).
- `1d98fd4` — **MC-3**: `Microphone::GetData` no longer zero-fills the buffer on a no-op read;
  returns 0 and leaves the caller's data untouched, matching FNA (resolves `D8`).
- `1d70f44` — **IN-6**: expanded `XactParserTests.cpp` from 8 to 22 tests (truncated/bad-magic for
  all 3 formats, non-compact `.xwb` standard 24-byte layout, ADPCM entry, `SOUND_FLAG_HAS_RPC`,
  all 4 variation-table types, RAMP-form PITCH event).
- `1fb919c` — **XA-5**: added a real `SoundBank`+`Cue` fixture so `AudioCategory`'s
  `Pause`/`Resume`/`Stop` are verified against an actual playing cue's state, not just
  `EXPECT_NO_THROW`. Discovered (but did not fix, out of scope) that `SetVolume` never actually
  re-applies to active cues — see `T-4D` in §5.
- `40b800a` — **CP-7**: `SoundEffectInstance` no longer holds a raw `const SoundEffect*`; replaced
  with a type-erased `shared_ptr<void>` keep-alive (capturing `SoundEffect::impl_`) plus a cached
  native handle. Confirmed the original bug with a real ASan build (`stack-use-after-scope` in
  `Play()`), confirmed the fix clean under ASan+LeakSanitizer.
- `49b5304` — **CP-4**: `PendingBufferCount` now tracks bytes actually consumed by the stream
  (`SDL_GetAudioStreamQueued`), not bytes merely submitted — `BufferNeeded` no longer over-fires.
- `9a44828` — **CP-6**: `DynamicSoundEffectInstance`'s sample-duration/size math now matches FNA's
  hardcoded 16-bit assumption instead of using the real (possibly 32-bit float) sample size.
- `b563e37` — **CP-2**: `SoundEffect::Play(volume, pitch, pan)` now validates/clamps pan/pitch
  exactly like `SoundEffectInstance`'s property setters.
- `f6f6c9e` — **CP-13**: added the missing `Stop(false)` test for the static `SoundEffectInstance`.
- `e18401a` — **CP-5**: `DynamicSoundEffectInstance::Stop(false)` now throws
  `InvalidOperationException` once playback has started (matches FNA); safe no-op before any
  `Play()`.
- `d5f0b14` — **CP-14**: closed as already-satisfied by `CP-1`'s own regression test.
- `101b2f8` — **MC-5**: added a genuinely negative-`count` test for `Microphone::GetData`.
- `31816bf` — **MC-2**: removed dead `friend class MicrophoneFactory` + rewrote a stale comment.
- `6086900` — **IN-5**: converted all of `XactTypes.hpp` to `/** @brief */` Doxygen blocks.
- `88c9f94` — **XA-4**: documented `AudioEngine`'s unused `lookAheadTime`/`rendererId` as an
  intentional deviation (single-backend project); strengthened the existing test.
- `615b74d` — **CP-12**: added move-ctor/move-assignment tests for `SoundEffectInstance`.
- `ef021d2` — **CP-11**: added a real successful-decode test for `SoundEffect::FromStream`.
- `e8702dd` — **CP-10**: added tests for `SoundEffect`'s file-path-loading constructor.
- `53c5dc0` — **IN-4**: variation-table type 3 (INTERACTIVE) gets its own explicit branch; any
  other unknown type now throws instead of being silently misparsed with the wrong byte layout.
- `8c7c467` — **CP-9**: `SoundEffectInstance(const SoundEffect&)` is now `private` (matches FNA's
  `internal`).
- `19c326f` — **XA-3**: `Cue::Play` now selects variation entries via a weighted lottery over
  `weightMin`/`weightMax`, matching FAudio's own algorithm.
- `84d9463` — **CP-8**: `SoundEffect` now inherits `System::Object` and has `GetTypeName()`.
- `4e7b32e` — **MC-1**: `Microphone::GetSampleDuration`/`GetSampleSizeInBytes` now delegate to
  `SoundEffect`'s versions (matches FNA's rounding).
- `1037305` — **IN-3**: compact-XWB `dataLength` underflow now throws instead of wrapping to a
  huge `uint32_t` value.
- `fed07f9` — **IN-2**: non-compact XWB entry parsing no longer reads foreign-entry bytes when
  `entryMetaDataSize < 24`.

Full detail for any of the above: `git show <hash>`, or the matching task ID in `plan_audio.md`
§4 Fáze 7 (each entry has a `*Pozn.:*` note with the exact verification method used).

Prior to this session (see `git log` for full history): `b9aabdd` (previous `NEXT.md` snapshot),
`0c2fa58`/`b8f1a1f`/`6280c51`/`800d1e9` (`XA-1`/`XA-2`/`CP-3`/`CP-1`, the first 4 of the 30 Fáze 7
findings, fixed in an earlier session).

---

## 4. Current blocker / main problem

**No build- or test-breaking blocker.** The build is clean and all 2020 tests pass as of `678258e`.

**Fáze 7 is now fully closed (30/30).** There is no discrete backlog left from that audit. The
remaining open work is the older, smaller set of pre-Fáze-7 items in §5 (`T-3F`, `T-3G`, `T-4B`,
`T-4C`, `T-4D`, `T-6C`) — none of these are bugs or regressions; they are either accepted
deviations awaiting a product decision, or genuinely unimplemented (but documented-as-such)
features.

**Known recurring hazard (not currently active):** this branch's build depends on
`../sharp-runtime`, which is under separate, active, concurrent development by another session.
Earlier this session (see prior `NEXT.md` history), a fresh rebuild briefly failed because of an
in-progress interface change there (`IAsyncResult` gaining new pure-virtual methods). If a future
build fails inside `SHARP_RUNTIME/CMakeFiles/...` or in an unrelated non-Audio file, check
`git status`/`git log -1` in `../sharp-runtime` before assuming the audio code broke something —
it may just need a retry once that unrelated work lands, or a small compliance patch.

---

## 5. Known bugs and limitations

| Status | Issue | Ref |
|---|---|---|
| **Confirmed, open** | Streaming `WaveBank` ctor ignores `offset`/`packetSize`, always loads the whole file | `T-3F` |
| **Confirmed, open** | `SoundEffect::CreateInstance()`/`FromStream()` value semantics — no instance-tracking/Dispose-cascade (the specific dangling-pointer hazard this caused was fixed as `CP-7`; the broader decision is still open) | `T-3G` |
| **Confirmed, open** | `Cue::Apply3D`/3D `PlayCue` are no-ops — no pan/attenuation from listener/emitter geometry at the Cue/SoundBank level | `T-4B` |
| **Confirmed, open** | No DSP filter/reverb routing on `SoundEffectInstance` | `T-4C` |
| **Confirmed, open** | `AudioCategory::SetVolume` doesn't retroactively re-apply to already-playing cues (`AudioEngine::SetCategoryVolumeInternal` is a documented no-op) — re-confirmed while writing `XA-5`'s test this session | `T-4D` |
| **Housekeeping, open** | Formal "build & report" step never explicitly checked off, though satisfied continuously this session | `T-6C` |
| **Accepted deviation** | 3D positional audio is pan + distance-attenuation only, no elevation/Doppler | `CHECKLIST.md` |
| **Accepted deviation** | Interactive-type (`type==3`) XACT variation tables fall back to a uniform pick instead of a variable-driven one (parser doesn't retain `var_min`/`var_max` per entry) | `CHECKLIST.md`, `XA-3` |
| **Accepted deviation** | `SoundBank::IsInUse` only tracks fire-and-forget cues it owns; `GetCue`-obtained cues are caller-owned | `T-3B` |
| **Accepted deviation** | `Cue::GetVariable`/`SetVariable` validate against `AudioEngine`'s global set + built-in 3D variables, not a true per-cue-instance catalog | `T-3A` |
| **Needs verification** | Device-dependent tests only ever run against the SDL `dummy` driver in CI/this environment; real-hardware runs are manual and ad-hoc, not automated | — |

All Fáze 0–7 findings (`T-1A`–`T-1H`, `T-2A`–`T-2G`, `T-3A`–`T-3E`, `T-5A`–`T-5O`, `T-4A`, `T-6A`,
`T-6B`, and all 30 of `CP-1`..`CP-14`/`XA-1`..`XA-5`/`IN-1`..`IN-6`/`MC-1`..`MC-5`) are fixed; see
`plan_audio.md` for the full checked-off list with verification notes.

---

## 6. Architecture notes

### Main modules

| Component | Location | Notes |
|---|---|---|
| XNA audio API | `include/Microsoft/Xna/Framework/Audio/`, `src/.../Audio/` | Must match XNA 4.0 / FNA exactly |
| Internal mixer | `CNA/Internal/Audio/AudioMixer.{hpp,cpp}` | SDL3_mixer `MIX_Mixer` singleton via `GetMixer()`; single 44100/stereo/S16 device (per-audio sample rate is set separately when loading each `MIX_Audio`, so non-44100Hz content is not broken — verified, not a bug) |
| XACT parser | `CNA/Internal/Audio/XactParser.cpp`, `XactTypes.hpp` | Custom `.xgs`/`.xsb`/`.xwb` reader (FACT is **not** used); now has broad test coverage (`IN-6`, 22 tests) after this session |
| sharp-runtime | `../sharp-runtime/` | `System.*` types, primitive aliases, exception hierarchy |

### Data flow (playback)

```
SoundEffect (loads MIX_Audio via GetMixer)
  → CreateInstance() returns SoundEffectInstance BY VALUE
    (instance keeps SoundEffect::impl_ alive via a type-erased shared_ptr<void> -- CP-7)
  → SoundEffectInstance::Play() creates a MIX_Track, binds the MIX_Audio, plays
DynamicSoundEffectInstance
  → user submits buffers → SDL_AudioStream → MIX_Track
  → PendingBufferCount tracks bytes NOT yet consumed by the stream (SDL_GetAudioStreamQueued),
    not merely submitted -- CP-4
  → FrameworkDispatcher::Update() pumps registered instances (Streams list) and raises BufferNeeded
AudioEngine/SoundBank/WaveBank/Cue
  → XactParser reads .xgs/.xsb/.xwb → cues map to SoundEffect/SoundEffectInstance played via SDL_mixer
  → Cue::Play() selects a variation entry via a weighted lottery over weightMin/weightMax -- XA-3
Microphone (capture)
  → getAllProperty() enumerates via SDL_GetAudioRecordingDevices
  → Start() opens a real SDL_AudioStream (SDL_OpenAudioDeviceStream); Stop() destroys it
  → GetData()/GetQueuedBytes() read from that stream; GetData() leaves the buffer untouched on a
    no-op read (no zero-fill) -- MC-3
```

### Invariants / rules that must stay stable

- **Backend = SDL3_mixer only.** Do not reintroduce FAudio/FACT.
- **Device opened lazily.** `AudioMixer::GetMixer()` opens the shared playback device on first use;
  `Microphone::Start()` opens its capture stream on first use, not at enumeration time. Both are
  gated by `#ifdef SOUND_ENABLED`.
- **Exceptions on the XNA surface must be `System::` types**, never `std::runtime_error`/
  `std::out_of_range`/`std::invalid_argument`. (Internal `XactParser` corrupt-data throws are the
  one sanctioned exception to this — they use `std::runtime_error`, matching `D7`'s decision, and
  are caught/converted at the `SoundBank`/`WaveBank` constructor boundary before reaching the XNA
  surface.)
- **`GetTypeName()` returns the dotted .NET name** (`"Microsoft.Xna.Framework.Audio.X"`) and lives
  in the **public** section for every concrete `System::Object` subclass — including `SoundEffect`
  now (`CP-8`).
- **`SoundEffectInstance::hasStarted_`** is set on `Play()` and never reset; gates `IsLooped`
  (set-after-play → `InvalidOperationException`).
- **`DynamicSoundEffectInstance`** must override **both** `setIsLoopedProperty(const bool&)` and
  `setIsLoopedProperty(bool&&)` as no-ops (a single-signature override silently hides instead of
  overriding) — same C++ name-hiding trap that made `Stop(bool)` need an explicit override too
  (`CP-5`).
- **`CreateInstance` returns by value** (no FNA-style instance tracking/Dispose cascade) — a
  documented, deliberate value-semantics deviation (`T-3G`, still open). `SoundEffectInstance` does
  now keep the underlying `MIX_Audio` resource alive independent of the `SoundEffect` wrapper
  (`CP-7`), but this is a narrower memory-safety fix, not a resolution of `T-3G`'s broader
  tracking/Dispose-cascade question — do not conflate the two.
- **`Apply3D` must not mutate the public `Volume`/`Pan` properties** (fixed as `CP-3` — don't
  reintroduce the old bug).
- **`SoundBank`'s fire-and-forget sweep is event-based (`!IsPlaying`), not purely time-based**
  (fixed as `XA-1` — don't reintroduce the old "older than N seconds" bug).
- **`Cue` never self-transitions out of `Playing`** without an explicit `Stop()`/`Pause()` call —
  no real playback-finished detection at the `Cue` level (unlike `SoundEffectInstance`, which
  queries the real `MIX_Track` state). This is relied on by `SoundBankTestAccess`'s
  backdating-based tests; don't "fix" this as an unrelated drive-by without checking those tests.
- **`Cue::Play`'s variation selection is a weighted lottery** over `weightMin`/`weightMax` for
  wave/sound/compact_wave tables, matching FAudio's `get_active_variation_index` exactly (`XA-3`).
  Interactive-type (3) tables fall back to a uniform pick (documented deviation, `CHECKLIST.md`) —
  don't "fix" this without first deciding to parse `var_min`/`var_max` into `XsbVariEntry`.
- **`DynamicSoundEffectInstance::PendingBufferCount`** reflects real stream consumption
  (`SDL_GetAudioStreamQueued`), not submission (`CP-4`) — don't revert to counting
  `queuedBuffers_.size()` alone; that was the exact bug.
- **`Microphone::GetData` never zero-fills** on a no-op/error read — it returns 0 and leaves the
  buffer untouched, matching FNA (`MC-3`). Don't reintroduce the old zero-fill "safety" behavior.
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

# One-off ASan+LeakSanitizer verification (used for XA-2 and CP-7 this session; NOT part of the
# normal build, delete the build dir after use):
cmake -B cmake-build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_GRAPHICS_BACKEND=EASYGL -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fsanitize=leak -g" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address -fsanitize=leak"
cmake --build cmake-build-asan --target CnaTests -j"$(nproc)"
ASAN_OPTIONS=detect_leaks=1 ./cmake-build-asan/CnaTests --gtest_filter='<TestName>'
rm -rf cmake-build-asan   # clean up afterward

# git-stash regression-verification pattern used for every fix this session:
#   1. git stash push -- <changed source files>   (keep the new test, revert just the fix)
#   2. rebuild, run the new test, confirm it FAILS against the pre-fix code
#   3. git stash pop                                (restore the fix)
#   4. rebuild, run the full suite, confirm green

# FNA reference source for any audio file
ls /rv/data/library/github.com/FNA-XNA/FNA/src/Audio

# FAudio C reference for XACT byte-format details not documented elsewhere
grep -n "<symbol>" /rv/data/library/github.com/FNA-XNA/FAudio/src/FACT_internal.c
```

---

## 8. Next smallest tasks

Fáze 7 (`plan_audio.md` §4) is fully closed — there is no `CP-`/`XA-`/`IN-`/`MC-` backlog left.
Remaining work is the smaller, older set below. None of these have ready-made accept criteria as
granular as Fáze 7's did — each starts with a **decision**, not just a fix.

1. **`T-4D` — make `AudioCategory::SetVolume` actually affect already-playing cues.**
   - Goal: `AudioEngine::SetCategoryVolumeInternal` (`AudioEngine.cpp:224-234`) currently has a
     literal no-op comment ("Cue would need to re-apply volume — skipped for simplicity"). At
     minimum, iterate `activeCues` for the category and re-apply volume to each cue's active
     `SoundEffectInstance`(s).
   - Files: `src/Microsoft/Xna/Framework/Audio/AudioEngine.cpp:224-234`,
     `src/Microsoft/Xna/Framework/Audio/Cue.cpp` (may need a new internal method to re-apply
     category volume to `active_`).
   - Verify: extend `AudioCategoryTests.cpp`'s `PauseResumeStopRouteToRealActiveCueInCategory`
     fixture (already has a real playing cue) to check the resulting `SoundEffectInstance`'s
     volume changes after `SetVolume()`.

2. **`T-3G` — decide `SoundEffect` instance-tracking vs. documented value-semantics deviation.**
   - This is a **decision task**, not a mechanical fix: either implement FNA-style weak-ref
     instance tracking (`SoundEffect::Dispose()` stops/disposes all live instances), or formally
     write the value-semantics deviation into `CHECKLIST.md` as permanent (it's currently only
     implicitly accepted via `CP-7`'s narrower fix).
   - Do not start implementing tracking without discussing the tradeoff first — it's a real API
     behavior change, not a bug fix.

3. **`T-3F` — decide streaming `WaveBank` vs. documented in-memory-only deviation.**
   - Same shape as `T-3G`: either implement real offset/packetSize streaming reads, or formally
     close this as an accepted deviation in `CHECKLIST.md` (it's likely already the practical
     answer, given SDL3_mixer's own loading model, but has never been written down as final).

4. **`T-4B` — 3D pan/attenuation at the `Cue`/`SoundBank` level.**
   - Note this is different from `SoundEffectInstance::Apply3D`, which already works (`CP-3`).
     This is specifically about `Cue::Apply3D` and 3D `SoundBank::PlayCue` being no-ops.
   - Files: `src/Microsoft/Xna/Framework/Audio/Cue.cpp` (`Apply3D`), `SoundBank.cpp` (3D
     `PlayCue` overload).

5. **`T-6C` — formal build & report checkpoint.** Genuinely small: run
   `cmake --build cmake-build-debug --target CNA` and `--target CnaTests`, confirm both are
   clean, write the short report `CLAUDE.md` §Build and Report asks for, check the box.

`T-4C` (DSP filter/reverb routing) is the least-scoped of the remaining items — read
`SoundEffectInstance.cs:488,518,536,554` in the FNA source first to size it before starting.

---

## 9. Do not do yet

- **No re-running a fresh full audit.** Fáze 7 is closed; the remaining work is the smaller,
  already-identified list in §5/§8. Don't go looking for a "Fáze 8" without being asked.
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
- **No implementing `T-3G`/`T-3F`'s "real" option (instance tracking / real streaming) without
  discussing it first** — both are framed as decisions in §8, not mechanical fixes. Don't pick the
  more invasive option unilaterally.
- **No real 3D HRTF or Doppler** — SDL_mixer cannot do it; keep as documented stored-not-applied.
- **No parsing `var_min`/`var_max` into `XsbVariEntry` as a drive-by** — this would change
  interactive-type variation selection (currently a documented uniform-pick deviation from `XA-3`);
  it's a real feature addition, not a bug fix, if ever done.
- **No touching the sibling `../cna` or `../sharp-runtime` checkouts** — separate repos/clones. If
  a build breaks there, check whether it's the known concurrent-development hazard (§4) before
  "fixing" it from this branch; only patch CNA-side compliance code if truly needed to unblock the
  build.
- **No API renames / namespace moves** — XNA names are frozen.
- **No mass Doxygen/format passes** — `IN-5` already brought `XactTypes.hpp` up to standard this
  session; don't repeat that sweep elsewhere without a specific reason.

---

## 10. Resume prompt

```
Read NEXT.md first, then plan_audio.md for full task detail (Fáze 7 is done -- see §8 here for
what's actually left: T-3F, T-3G, T-4B, T-4C, T-4D, T-6C).

1. Confirm the current build/test state matches NEXT.md §2 (build clean, 2020/2020 tests pass) --
   rebuild and rerun ./cmake-build-debug/CnaTests to check for drift since this was last updated.
2. Pick exactly ONE task from NEXT.md §8. Note that unlike Fáze 7's items, most of these start
   with a decision (see each task's note) -- don't silently pick the more invasive option.
3. Inspect only the files that task names. Do not refactor unrelated code.
4. Make ONE small, verified improvement. Follow the established git-stash regression-verification
   pattern from this session (see NEXT.md §7) for any behavioral fix: stash it, confirm the new
   test fails against the pre-fix code, restore, confirm green.
5. Run the task's verification command.

After finishing, check the task's checkbox in plan_audio.md, update NEXT.md (status, recent
changes, next task), and commit.
```
