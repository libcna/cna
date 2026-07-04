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
  `CP`/`XA`/`IN`/`MC`) — is **fully complete: 30 of 30 fixed/closed**. Of the handful of
  pre-existing older items from Fáze 3/4/6 that were never in scope for that audit (`T-3F`,
  `T-3G`, `T-4B`, `T-4C`, `T-4D`, `T-6C`), **`T-4D`, `T-3F`, `T-3G`, `T-4B`, and `T-6C` were closed
  this session** (2026-07-04); see §3. Only `T-4C` is still open — see §5.
- **Key architectural decision:** the audio backend is **SDL3_mixer 3.x**
  (`MIX_Mixer`/`MIX_Track`/`MIX_Audio`), **not** FAudio/FACT. XACT (`.xgs`/`.xsb`/`.xwb`) is
  parsed by a hand-written `XactParser` and mixed through SDL_mixer. Consequences: 3D HRTF and
  Doppler are stored but never applied — pan + distance-attenuation is now applied at **every**
  level of the API (`SoundEffectInstance::Apply3D` since `CP-3`, and now `Cue::Apply3D`/3D
  `SoundBank::PlayCue` too, `T-4B`). `WaveBank`'s **streaming ctor now does real lazy per-entry
  disk reads** (`T-3F`) — only the non-streaming ctor loads the whole `.xwb` eagerly, matching
  FNA's own eager/lazy split. `SoundEffect` is now **move-only, with real instance-tracking +
  Dispose cascade** (`T-3G`) — `SoundEffect::Dispose()` stops/disposes every live
  `SoundEffectInstance` created via `CreateInstance()`, matching FNA's `SoundEffect.Instances`;
  `CreateInstance()`/`FromStream()` are still value-returning (no heap-reference API shape
  change), but `SoundEffect` can no longer be copied, only moved.
- `sharp-runtime` (sibling repo `../sharp-runtime`) supplies all `System.*` types and primitive
  aliases (`bytecs`, `Single`, `String`, …) used on the XNA API surface. It is under **separate,
  active, concurrent development** by another session — see §4.

---

## 2. Current status

- **Build:** clean at `1244430` (`HEAD`). EasyGL backend, `SOUND_ENABLED` on, SDL3_mixer linked.
  Verified immediately before writing this update; also rebuilt `cna_demo_sound`/`cna_demo_2d`
  (the example targets, `CNA_BUILD_EXAMPLES=ON` by default) since `T-3G` changed a public API
  shape (`SoundEffect` copyability) those depend on through `ContentManager::Load<SoundEffect>()`.
- **Tests:** `CnaTests` **2031 / 2031 pass** (2020 at the last handoff snapshot; +1 for `T-4D`,
  +3 for `T-3F`, +5 for `T-3G`, +2 for `T-4B`, +0 for `T-6C` — docs/checkpoint only, no code).
  No known regressions. Re-run to check for drift: `SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests`.
- **CLI/tools/apps:** none in the framework itself, but this session's `T-3G` work touched the
  two example demos (`cna_demo_sound`, `cna_demo_2d`) as collateral — see §7 for how to rebuild
  them; they're not part of `CnaTests` and easy to forget when just running `--target CnaTests`.
- **This session's work: closed `T-4D`, `T-3F`, `T-3G`, `T-4B`, and `T-6C`** — see §3 for detail.
  - `T-4D`: `AudioCategory::SetVolume` now re-applies to already-playing cues. This was the one
    task in the old §8 backlog that was a mechanical fix rather than an open design decision.
  - `T-3F`: asked the user how to close it (implement real streaming vs. document the deviation);
    the user chose **implement real streaming**, so `WaveBank`'s streaming ctor now does lazy
    per-entry disk reads instead of eagerly loading the whole file like the non-streaming ctor did.
  - `T-3G`: asked the user the same way; the user again chose **implement** (instance-tracking +
    Dispose cascade) over documenting the value-semantics deviation. This one had real teeth:
    making it correct required also making `SoundEffect` move-only and fixing a collateral
    `ContentManager::Load<T>()` compile break — see §3.
  - `T-4B`: unlike the other three, `plan_audio.md` framed this as concrete accept criteria, not
    an "implement vs. document" decision, so it was picked up directly without asking first —
    `Cue::Apply3D`/3D `PlayCue` now forward to `SoundEffectInstance::Apply3D` (already working
    since `CP-3`), instead of being no-ops.
  - `T-6C`: the formal build & report checkpoint itself — both targets were already green with
    nothing to rebuild; recorded the short report in `plan_audio.md`.
  - `T-4C` is the only item left open, and still needs a decision first (see §5/§8).
- **Prior session's work (26 commits, all 30 Fáze 7 findings closed):**
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
  - `T-4C` — no DSP filter/reverb routing (`applyReverb`/`applyLowPassFilter`/etc.) on
    `SoundEffectInstance`.

---

## 3. Recent changes (this branch, newest first)

- `1244430` — **T-6C**: formal build & report checkpoint. `cmake --build --target CNA` and
  `--target CnaTests` both green with nothing to rebuild (already current from the prior four
  commits); `CnaTests` 2031/2031 under `SDL_AUDIODRIVER=dummy`. Recorded the short report
  `CLAUDE.md` §Build and Report asks for directly in `plan_audio.md`'s T-6C entry: changed files
  this session, no new stubs/missing dependencies, the accepted-deviations list (already in
  `CHECKLIST.md`), and `T-4C` as the one remaining real gap. Pure docs/checkpoint — no code change.
- `feb6eda` — **T-4B**: `Cue::Apply3D` was a pure no-op (only checked `isDisposed_`); it now
  iterates `Cue::active_` and calls `SoundEffectInstance::Apply3D` (already working since `CP-3`)
  on each live instance — no new pan/attenuation math, just wiring to the mechanism `CP-3` already
  built. `SoundBank::PlayCue(name, listener, emitter)` discarded both parameters and just called
  the 2-arg overload; both overloads are refactored onto a shared private `PlayCueInternal(name,
  listener*, emitter*)`, and the 3D one now calls `cue->Apply3D(...)` right after `cue->Play()`,
  before storing the cue in `fireAndForget_` (matches FNA, where `FACT3DCalculate` runs before
  `FACTSoundBank_Play3D` — a synchronous same-thread call here, so no observable ordering
  difference). Doppler stays unapplied (existing accepted deviation); the `ObjectDisposedException`
  half of the original task description was already done, nothing left to change there. Needed a
  real WaveBank-backed fixture to test for real: none of the existing Cue/SoundBank fixtures
  (`MakeCue()`, `SharedWeightedVariationBank()`, `SoundBankTests.cpp`'s "Explosion") have a
  wavebank, so `Cue::active_` stays empty and there's nothing for `Apply3D` to reach. Added such a
  fixture to both `CueTests.cpp` and `SoundBankTests.cpp`, extracted `SoundEffectInstanceTestAccess`
  into a shared header (previously private to `SoundEffectInstanceTests.cpp`, same move
  `CueTestAccess` got for `T-4D`), and added `CueTestAccess::ActiveInstance()`/
  `SoundBankTestAccess::LastFireAndForgetCue()` so both new tests can read back the real
  `MIX_GetTrackGain()` and confirm it changes with distance (SDL3_mixer has no stereo-pan getter,
  so only attenuation is directly verifiable — the same limitation `CP-3`'s own `Apply3D` coverage
  already has). Verified via `git stash`: both new tests fail with `farGain == nearGain` against
  the pre-fix no-op code (not a compile break this time, since the test scaffolding itself doesn't
  depend on the fix). Also verified clean under ASan+LeakSanitizer.
- `71b7a45` — **T-3G**: `SoundEffect::Impl` gained a non-owning `std::vector<SoundEffectInstance*>
  instances`; `SoundEffectInstance` registers itself in its ctor (`SoundEffect::RegisterInstance`)
  and unregisters (plus drops its own `soundEffectKeepAlive_`) in `Dispose()`
  (`UnregisterInstance`), so the underlying `MIX_Audio` can free as soon as the `SoundEffect` and
  every instance are disposed — closer to FNA's eager release than before. `SoundEffect::Dispose()`
  now iterates a snapshot of tracked instances and disposes each one before `impl_.reset()`,
  matching FNA's `Instances.ToArray()` + foreach cascade. Two changes were required to make this
  correct, not just correct-looking:
  - **`SoundEffect` is now move-only** (copy ctor/assignment `= delete`) — a single owner per
    resource is what makes "whose `Dispose()` is authoritative" unambiguous. An Explore agent
    confirmed no call site in `src/`/`tests/`/`examples/` relies on `SoundEffect` copy semantics
    *except* `ContentManager::Load<T>()`'s generic `std::any` cache (requires `CopyConstructible`)
    — fixed with an explicit `Load<Audio::SoundEffect>` specialization that skips caching
    entirely (`ContentManager.hpp`/`.cpp`): sharing one cached instance across unrelated
    `Load<SoundEffect>()` call sites would let disposing one caller's copy silently cascade-stop
    another caller's still-playing instances, so removing the cache for this type isn't just a
    workaround for move-only-ness, it's the semantically correct outcome. Rebuilt
    `cna_demo_sound`/`cna_demo_2d` (the only call sites) to confirm the fix compiles.
  - **`SoundEffectInstance`'s move ctor/assignment now re-point cascade tracking** (unregister the
    old address, register the new one) — without this, moving an instance to a new address (e.g.
    `dst = std::move(src)` with `src` going out of scope) leaves `SoundEffect::Dispose()`'s cascade
    targeting a stale address. Confirmed this is a real, serious bug, not just theoretical:
    temporarily disabling just the repoint logic (keeping the rest of the feature intact) made the
    new regression test **segfault**, not merely fail an assertion — stronger evidence than the
    usual `git stash` check.
  Added 5 tests to `SoundEffectTests.cpp` (single/multiple-instance cascade, already-disposed
  instance skipped safely, moved-to instance via both move ctor and move-assignment) plus 4
  `static_assert`s locking in move-only-ness. Verified via `git stash` (the whole feature reverted
  makes the new tests fail to *compile*, since `RegisterInstance` etc. don't exist) and a full
  ASan+LeakSanitizer run (not just the new tests).
- `eefda45` — **T-3F**: implemented real streaming for `WaveBank`'s streaming ctor instead of the
  old "delegates to the same eager Init() as non-streaming" behavior. New
  `ParseXwbStreamingHeader(path)` (`XactParser.cpp`) reads only segments 0-3 (bank data, entry
  metadata, seek tables, entry names) from disk; segment 4 (wave data, by far the largest part of
  a real `.xwb`) is never loaded upfront. `XwbData` gained `streaming`/`sourcePath` fields.
  `WaveBank::GetSoundEffect()` now does a lazy per-entry disk read (seek to `entry.dataOffset`,
  read `entry.dataLength`) when the bank is in streaming mode, instead of slicing a fully-resident
  buffer. The non-streaming ctor is unchanged (still eager, matching FNA). `offset`/`packetSize`
  stay unused: confirmed by reading FNA's own `WaveBank.cs` that it never forwards them to
  `FACTStreamingParameters` either (only `.file` is set) — so leaving them dead matches the
  reference exactly rather than inventing semantics FNA itself doesn't have. Added
  `WaveBankTestAccess` (`StreamingInternal`/`ResidentFileBytesInternal` plus friend access to the
  private `GetSoundEffect`) and three tests: the non-streaming ctor still loads the whole file;
  the streaming ctor's resident memory excludes the wave-data segment; and a new two-entry fixture
  (`BuildMultiEntryXwbFixtureBytes`, different offset/length per entry) proves the lazy read lands
  on the correct byte range, not just "a" range of the right length — a single-entry fixture can't
  catch an offset bug since entry 0 always starts at offset 0 either way. Verified clean under
  ASan+LeakSanitizer, and via `git stash` (the test scaffolding doesn't even compile against the
  old code, since `StreamingInternal` etc. don't exist there — a genuine compile failure, not just
  a failing assert).
- `d468dc4` — **T-4D**: `AudioEngine::SetCategoryVolumeInternal`'s no-op loop body now calls a new
  `Cue::ApplyCategoryVolume(catVol)` for every active cue in the category. `Cue::PlaybackInstance`
  gained a `baseVolume` field (the wave's own volume, already combined with sound/track volume at
  parse time in `XactParser`); `ApplyCategoryVolume` recombines it with the new category volume
  using the same `clamp(base*cat, 0, 1)` formula `Play()` already used, so re-applying volume and
  the original apply-at-Play()-time path can never drift apart. Added
  `AudioCategoryTest.SetVolumeReappliesToAlreadyPlayingCueInstance` — this needed a *new* fixture
  with a real `WaveBank` (unlike the existing `PauseResumeStopRouteToRealActiveCueInCategory`
  fixture, whose cue has no wavebank, so `Cue::active_` stays empty and there's no instance to
  observe a volume change on). Extracted `CueTestAccess` (previously private to `CueTests.cpp`)
  into a shared `tests/.../Audio/CueTestAccess.hpp` so `AudioCategoryTests.cpp` could reuse it,
  and added `ActiveInstanceVolumes()` to it. Verified with the branch's `git stash` methodology:
  stashing `Cue.hpp`/`Cue.cpp`/`AudioEngine.cpp` made the new test fail (`1 == 1`, i.e. genuinely
  no change), confirming the test isn't a tautology.

All 26 commits below are the *prior* session's Fáze 7 closure work, `fed07f9`..`678258e`:

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

**No build- or test-breaking blocker.** No failing command, no failing test. The build is clean
and all 2031 tests pass as of `feb6eda` (last commit with an actual code/test change; `1244430`
and the NEXT.md rewrite itself are docs-only and don't touch build state). `cna_demo_sound`/
`cna_demo_2d` also rebuilt clean.

**Fáze 7 is now fully closed (30/30), and `T-4D`/`T-3F`/`T-3G`/`T-4B`/`T-6C` from the older
backlog are also closed.** There is no discrete backlog left from Fáze 7. `T-4C` is the only item
still open (see §5) — not a bug or regression, a genuinely unimplemented (but documented-as-such)
feature.

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
| **Confirmed, open** | No DSP filter/reverb routing on `SoundEffectInstance` | `T-4C` |
| **Fixed 2026-07-04** | `AudioCategory::SetVolume` now retroactively re-applies to already-playing cues via `Cue::ApplyCategoryVolume` | `T-4D` |
| **Fixed 2026-07-04** | `WaveBank`'s streaming ctor now does real lazy per-entry disk reads instead of eagerly loading the whole file like the non-streaming ctor | `T-3F` |
| **Fixed 2026-07-04** | `SoundEffect` now has real instance-tracking + Dispose cascade (matches FNA's `SoundEffect.Instances`); `SoundEffect` is move-only as a result | `T-3G` |
| **Fixed 2026-07-04** | `Cue::Apply3D`/3D `PlayCue` now forward to `SoundEffectInstance::Apply3D` instead of being no-ops | `T-4B` |
| **Fixed 2026-07-04** | Formal "build & report" checkpoint recorded in `plan_audio.md` | `T-6C` |
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
SoundEffect (loads MIX_Audio via GetMixer; move-only -- T-3G)
  → CreateInstance() returns SoundEffectInstance BY VALUE, registers it in
    SoundEffect::Impl::instances for the Dispose cascade (T-3G)
    (instance keeps SoundEffect::impl_ alive via a type-erased shared_ptr<void> -- CP-7)
  → SoundEffectInstance::Play() creates a MIX_Track, binds the MIX_Audio, plays
  → SoundEffect::Dispose() disposes every tracked instance before releasing impl_ -- T-3G
DynamicSoundEffectInstance
  → user submits buffers → SDL_AudioStream → MIX_Track
  → PendingBufferCount tracks bytes NOT yet consumed by the stream (SDL_GetAudioStreamQueued),
    not merely submitted -- CP-4
  → FrameworkDispatcher::Update() pumps registered instances (Streams list) and raises BufferNeeded
AudioEngine/SoundBank/WaveBank/Cue
  → XactParser reads .xgs/.xsb/.xwb → cues map to SoundEffect/SoundEffectInstance played via SDL_mixer
  → WaveBank's non-streaming ctor loads the whole .xwb eagerly; its streaming ctor reads only
    header/metadata upfront and does a lazy per-entry disk read in GetSoundEffect() -- T-3F
  → Cue::Play() selects a variation entry via a weighted lottery over weightMin/weightMax -- XA-3
  → Cue::Apply3D()/3D SoundBank::PlayCue() forward to each active SoundEffectInstance::Apply3D()
    (already working since CP-3) -- T-4B
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
- **`SoundEffect` is move-only; `SoundEffect::Dispose()` cascades to every live
  `SoundEffectInstance`** created via `CreateInstance()` (`T-3G`) — matches FNA's
  `SoundEffect.Instances`. Don't reintroduce the copy ctor/assignment: a single owner per resource
  is what makes the cascade unambiguous. `SoundEffectInstance`'s move ctor/assignment re-point the
  tracking registration (unregister old address, register new) — don't drop that when touching
  those; without it, moving an instance leaves the cascade targeting a stale (possibly
  out-of-scope) address, which is a real segfault, not just a logic bug (verified by disabling
  just the repoint step and watching the regression test crash).
  `SoundEffectInstance` also keeps the underlying `MIX_Audio` resource alive independent of the
  `SoundEffect` wrapper (`CP-7`) — that's a narrower memory-safety mechanism the cascade builds on
  top of, not a substitute for it.
- **`ContentManager::Load<Audio::SoundEffect>()` never caches** (always a fresh load/decode) — a
  deliberate consequence of `T-3G`'s move-only + cascade-dispose semantics, not an oversight.
  Don't "fix" this by caching a `shared_ptr<SoundEffect>` or similar; sharing one instance across
  unrelated `Load()` call sites would let disposing one caller's copy silently cascade-stop
  another caller's still-playing instances.
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
- **`Cue::Apply3D`/3D `SoundBank::PlayCue` forward to `SoundEffectInstance::Apply3D`** on every
  active instance (`T-4B`) — don't revert to the old no-op. No new pan/attenuation math lives at
  the `Cue`/`SoundBank` level; it's all in `SoundEffectInstance::Apply3D` (`CP-3`). Testing this
  needs a WaveBank-backed fixture (`Cue::active_` stays empty without one) — see `SharedApply3DBank`/
  `BuildApply3DXwbFixtureBytes` in `CueTests.cpp`/`SoundBankTests.cpp` for the pattern.
- **`DynamicSoundEffectInstance::PendingBufferCount`** reflects real stream consumption
  (`SDL_GetAudioStreamQueued`), not submission (`CP-4`) — don't revert to counting
  `queuedBuffers_.size()` alone; that was the exact bug.
- **`Microphone::GetData` never zero-fills** on a no-op/error read — it returns 0 and leaves the
  buffer untouched, matching FNA (`MC-3`). Don't reintroduce the old zero-fill "safety" behavior.
- **`WaveBank`'s streaming ctor must not eagerly load the wave-data segment** (`T-3F`) — only
  segments 0-3 (bank data, entry metadata, seek tables, entry names) are read upfront via
  `ParseXwbStreamingHeader`; entry audio is read lazily per-entry in `GetSoundEffect()` via
  `XwbData::sourcePath`. Don't revert to calling the same eager `Init()` the non-streaming ctor
  uses — that was the exact bug. `offset`/`packetSize` stay intentionally unused, matching FNA.
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

# Rebuild the example demos too if you touch anything on the Audio public API surface (e.g.
# SoundEffect's copyability, T-3G) -- CNA_BUILD_EXAMPLES defaults ON but these aren't part of
# --target CnaTests, so a green test run alone won't catch a break here.
cmake --build cmake-build-debug --target cna_demo_sound -j"$(nproc)"
cmake --build cmake-build-debug --target cna_demo_2d -j"$(nproc)"

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
`T-4D`, `T-3F`, `T-3G`, `T-4B`, and `T-6C` were closed 2026-07-04 — see §3. Exactly **one** item
remains:

1. **`T-4C` — DSP filter/reverb routing on `SoundEffectInstance`.** Add
   `INTERNAL_applyReverb`/`applyLowPassFilter`/`applyHighPassFilter`/`applyBandPassFilter`
   (private/detail) so callers from `Cue`/`AudioEngine` have something to call; implement via
   SDL_mixer where possible, documented no-op otherwise. It's the least-scoped item left in
   `plan_audio.md` — read `SoundEffectInstance.cs:488,518,536,554` in the FNA source first to
   size it before starting; ask/flag before picking the more invasive option if SDL_mixer turns
   out to support real filtering (unclear without checking).
   *FNA:* `SoundEffectInstance.cs:488,518,536,554`.
   *Accept:* callers compile; behavior (real/no-op) recorded in `CHECKLIST.md`'s deviation table.

**If a future fixture needs a real playing `SoundEffectInstance`** (not just Cue state), the
wavebank-less `SharedBank()`/`BuildXsbFixtureBytes` fixtures in `CueTests.cpp`/
`AudioCategoryTests.cpp`/`SoundBankTests.cpp` won't do (`Cue::active_` stays empty). See
`SharedVolBank()`/`BuildVolXwbFixtureBytes()` in `AudioCategoryTests.cpp` (`T-4D`),
`BuildMultiEntryXwbFixtureBytes()` in `WaveBankTests.cpp` (`T-3F`, two entries at different
offsets/lengths), or `SharedApply3DBank()`/`BuildApply3DXwbFixtureBytes()` in `CueTests.cpp`/
`SoundBankTests.cpp` (`T-4B`) for patterns to copy.

**On "implement vs. document" decision tasks generally:** `T-3F` and `T-3G` were both framed as
"either implement the real thing or document the deviation," and in both cases the user chose
implement, and both turned out to have real, bounded implementations once actually scoped out —
don't assume a task like this is bigger or smaller than it looks without first reading the FNA
source it cites. (`T-4B`, by contrast, had concrete accept criteria in `plan_audio.md` rather than
an open decision, so it was picked up directly without asking — check which shape a task actually
has before assuming it needs a decision.)

---

## 9. Do not do yet

- **No re-running a fresh full audit.** Fáze 7 is closed; the remaining work is the smaller,
  already-identified list in §5/§8. Don't go looking for a "Fáze 8" without being asked.
- **No Media namespace work** — explicitly out of scope for this branch.
- **No FAudio/FACT migration** — the backend is SDL3_mixer by design.
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
Read NEXT.md first, then plan_audio.md for full task detail (Fáze 7, T-4D, T-3F, T-3G, T-4B, and
T-6C are all done -- see §8 here for what's actually left: just T-4C).

1. Confirm the current build/test state matches NEXT.md §2 (build clean, 2031/2031 tests pass) --
   rebuild and rerun SDL_AUDIODRIVER=dummy ./cmake-build-debug/CnaTests to check for drift since
   this was last updated. Also rebuild cna_demo_sound/cna_demo_2d if you touch anything on the
   Audio public API surface -- they're not part of CnaTests and easy to forget.
2. Do NEXT.md §8's one remaining task, T-4C (DSP filter/reverb routing). It has real accept
   criteria in plan_audio.md but some ambiguity about SDL_mixer's real filtering capability --
   ask/flag before picking the more invasive option if that turns out to matter.
3. Inspect only the files that task names. Do not refactor unrelated code.
4. Make ONE small, verified improvement. Follow the established git-stash regression-verification
   pattern (see NEXT.md §7) for any behavioral fix: stash it, confirm the new test fails against
   the pre-fix code, restore, confirm green.
5. Run the task's verification command.

After finishing, check the task's checkbox in plan_audio.md, update NEXT.md (status, recent
changes, next task), and commit.
```
