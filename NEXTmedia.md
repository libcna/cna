# NEXTmedia.md — CNA Media Namespace Handoff (`feature/media` branch)

> Scoped to `Microsoft::Xna::Framework::Media` + `CNA::Internal::Media::*` only, per the same
> per-domain convention as `NEXTaudio.md`/`NEXTdevices.md`/`NEXTinput.md`/`NEXTnet.md`. The repo-root
> `NEXT.md` is explicitly reserved for the `feature/dx9` branch (its own banner note, 2026-07-14) —
> **do not edit it from this branch.** Full task-by-task detail lives in `plans/plan_media.md`
> (`MEDIA-1`–`MEDIA-232`, Phases 0-16); this file is a short current-state index.

## 1. Status (2026-07-18) — Phases 0-16; **merged into `develop`**

**Where the work lives:** `feature/media` @ `22df8325`, merged into `develop` as `cb053b71`
(two merges: `a3f88c94`/`36ac9656` for Phases 8-16 Groups A-I, `cb053b71` for `MEDIA-228`..`232`).
Full task detail: `plans/plan_media.md` (`MEDIA-1`–`MEDIA-232`; **231 real tasks — `MEDIA-157`
was never assigned**, so the highest ID is not the count).

**Current baseline, measured on the merged tree — quote this number, not the historical ones:**
**5471 tests, 5467 passed, 0 failed, 4 pre-existing hardware skips** (two Accelerometer, two
Gyroscope; both need real hardware). Per-phase figures recorded earlier in this file are
*branch-only and pre-merge* — see `MEDIA-231`.

### Open work (7 tasks, all Group D)

`MEDIA-192`..`198` — **FFmpeg on Windows, Android and Emscripten.** `Video`, `VideoPlayer` and
`VideoDecoder` are excluded from the build there (`cmake/CnaLibrary.cmake`'s `CNA_FFMPEG_AVAILABLE`
gate) while their public headers remain, so using them is a **link error**, not a clean runtime
failure; `AudioDurationProbe` also returns 0 unconditionally on those platforms. **Deferred by the
project owner.** The owner explicitly rejected `NotSupportedException` stubs — the target is real
FFmpeg. `MEDIA-198` defines the honest platform-support matrix any attempt must fill in: what was
actually built and run, versus merely written. **This cannot be closed from a Linux-only sandbox.**

### Known gaps that are NOT tracked as open tasks

These are deliberate, documented limitations rather than unfinished work — but do not let a future
summary quietly upgrade them to "done":

- **Visualization has no TSAN, threaded, or end-to-end audio test.** The data race (`MEDIA-216`) is
  fixed *by construction* — `std::atomic<float>` samples plus install/reset ordering — **not by a
  test that would catch a regression of it.** The most valuable next piece of work in this
  namespace is a threaded `Push()`/`Read()` test under ThreadSanitizer.
- **Two error branches are unreachable from the test suite:** `GetMixer()` failing (the suite runs
  a dummy SDL audio driver that always succeeds, and the mixer is cached) and
  `MIX_SetPostMixCallback` failing to uninstall. Both are correct by construction (`MEDIA-222`,
  `MEDIA-226`), neither is covered.
- **`.m4a`/`.aac`/WMA are deliberately not indexed.** SDL3_mixer ships no AAC decoder at all, so
  indexing them would advertise songs `MediaPlayer::Play()` could never play (`MEDIA-199`/`201`).

### The one lesson to carry into any future namespace port

**FNA is authoritative for BEHAVIOR; the XNA 4.0 reference assemblies are authoritative for API
SURFACE.** FNA omits real XNA members — its own `Song.cs` has no `Album`/`Artist`/`Genre`/
`ToString()` — so **eight consecutive adversarial reviews auditing against FNA structurally could
not find that gap.** It took a ninth review diffing against
`/rv/data/library/github.com/borgesdan/xn65/references/Windows/Microsoft.Xna.Framework.xml` to
surface it. The rule and the mechanical `grep` recipe now live in `CHECKLIST.md`'s "API surface"
section; `MEDIA-213` applied it to all 24 Media types and found `Song` was the *only* type with
missing members.

### How to read the rest of this file

**Do not treat any "N/N done" line below as settled.** Fifteen external review rounds landed on
this plan, and *every one* found real, file-and-line-cited defects — repeatedly inside the fix
commits written in response to the previous round, and in several cases inside our own claims of
evidence (a test whose fixture had no audio; a test that passed against deliberately broken code; a
count whose method was never verified; an explanation that was arithmetically impossible). Sections
§1a-§1i below are the chronological record of those rounds, kept as written at the time rather than
retro-edited.

**Phase 9 correction (2026-07-18, same day as Phase 8):** a *second* external adversarial review —
this time of Phase 8's own fix commit `52eec0a5` — found the fixes were real but incomplete on
several points, **plus one genuine new regression Phase 8 itself introduced** while fixing the
track-switching bug (`MEDIA-131`): a fresh `Play()` call left the SDL audio stream paused forever,
so every video with audio played completely silently. Root cause went deeper than the review
itself suspected — `VideoPlayer` never initialized SDL's audio subsystem at all, a separate,
pre-existing gap the investigation surfaced. All Phase 9 findings fixed for real (`plans/plan_media.md`
`MEDIA-139`..`MEDIA-145`); see §1a below. **Two adversarial review passes in one day, two real
rounds of findings — this is exactly the "even a careful, well-documented fix needs its own
verification" lesson, twice in a row.**

**Phase 8 correction (2026-07-18):** an external adversarial code review of the Phase 7 "126/126
complete" claim found it was **not fully accurate** — 11 real, confirmed defects, several from
`MEDIA-N` tasks that had been checked off despite not genuinely meeting their own Accept criteria.
All 11 are now fixed or honestly documented (`plans/plan_media.md` Phase 8, `MEDIA-127`..`MEDIA-138`); see
§1a below for the full list. This is exactly the kind of "complete" claim needing its own
adversarial pass — treat any future "this plan is done" statement the same way, verify against the
actual code, not the checkbox.

**Phase 0 complete** (`MEDIA-1`..`MEDIA-8`, commits `eb3c48a3`, `3d6a7508` on `feature/media`). Test
infra (`tests/Microsoft/Xna/Framework/Media/` + `Video/`) and a full, verified fixture corpus
(`tests/assets/media/{music,pictures,video}/`, each with a `manifest.json`) are in place. See
`plans/plan_media.md` §5 Phase 0 for exact task detail.

**Phase 1 complete** (`MEDIA-9`..`MEDIA-31`, the compliance sweep), under an autonomous work session
(project owner unavailable for an extended period, explicit authorization given after a consolidated
question batch; periodic pushes to origin authorized). Real finding beyond the plan's own written
scope: `Video::FromUriEXT`/`SetAudioTrackEXT`/`SetVideoTrackEXT` and
`VideoPlayer::SetAudioTrackEXT`/`SetVideoTrackEXT` were missing `NOXNA` markers despite being real FNA
extensions beyond the XNA 4.0 API surface — fixed. Also found and fixed a real regression MEDIA-10
caused: `SongContentTypeReaderTests.cpp`'s `ReferenceToNonexistentFileFallsBackTo...` test asserted the
old `std::runtime_error` type; updated to assert `System::IO::FileNotFoundException` (this doubles as
Phase 5's `MEDIA-75`, closed early). Full-suite regression after all Phase 1 changes: **4666 tests,
4664 passed, 0 failed, 2 pre-existing hardware skips** (Accelerometer/Gyroscope, need real hardware) —
run against the real display per §2's corrected methodology, not the flawed all-`dummy` Phase 0 run.

**Phase 2 complete** (`MEDIA-32`..`MEDIA-45`, real bug fixes in the playback cluster). Real bugs
found and fixed beyond the plan's own written description:
- `swr_convert`'s return value (actual samples produced, can be less than requested or negative on
  error) was discarded entirely in `ProcessAudioPacket` -- a partial/failed conversion still
  appended the full, partly-stale buffer (including uninitialized trailing samples) to the pending
  audio queue. Fixed to trim to what was actually produced and skip entirely on error.
- `Video.hpp`'s two constructors had their `(XNB constructor)` / `(raw-file constructor)` doc
  labels literally swapped (the 2-arg probing constructor was labeled XNB; the 7-arg
  metadata-trusting constructor was labeled raw-file) -- fixed, doc-only.
- Every other MEDIA-35..45 item landed as planned: 4:2:2/4:4:4 chroma + 10-/12-bit HDR decode
  (`yuv_planar8_to_rgba`/`yuv_planar16_to_rgba`, generalized from the old 4:2:0-only function),
  `AllocAndConfigureCodecContext`/`SetupResampler` alloc-failure hardening (4 call sites),
  I/O-error-vs-EOF distinction in `NextFrame` (throws on a genuine demux error instead of treating
  it as clean EOF), the no-`SOUND_ENABLED` duration-based fallback (`DetectSongEndedByElapsedTime`,
  a pure always-compiled function so it's testable regardless of audio backend), confirmed
  shuffle-can-repeat and duplicate-on-play via FNA fidelity, `VideoPlayer`'s disposed-guard (7
  methods; `Dispose()` itself deliberately NOT guarded -- see the code comment on why replicating
  FNA's double-Dispose-throws would be UB in C++), `Video`'s missing-file `FileNotFoundException`,
  and the width/height/fps XNB-metadata-vs-decoded-file `InvalidOperationException` check.

Full-suite regression after Phase 2: **4699 tests, 4697 passed, 0 failed, 2 pre-existing hardware
skips.** New Media-scoped tests: 38 (`VideoDecoderTests.cpp` new under
`tests/CNA/Internal/Media/`, `MediaPlayerTests.cpp` new, `VideoTests.cpp` new, `VideoPlayerTests.cpp`
extended) -- includes two real-wall-clock-timing tests (~2-2.5s each) that actually play a fixture
video past its duration to prove the EOS/looping state machine, not just unit-test the pieces.

**Phase 3 complete** (`MEDIA-46`..`MEDIA-60`, the real from-scratch local media-library backend).
7 new internal classes under `CNA::Internal::Media::*`, all `NOXNA`: `MediaLibraryPaths` (real
`SDL_GetUserFolder` resolution + test override hooks), `AudioTagParser` (real from-scratch Ogg
page framing + Vorbis-comment extraction, real ID3v2.3/2.4 synchsafe frame parsing with full
text-encoding handling -- Latin-1/UTF-16+BOM/UTF-16BE/UTF-8 -- plus filename/folder fallback),
`MediaLibraryIndex` (recursive scan, symlink-cycle + permission-denied hardening, case-insensitive
Artist/Genre normalization), `MediaCollectionBase<T>` (shared collection template),
`PictureLibraryIndex` (real Picture/PictureAlbum tree, reusing the existing
`CNA::Internal::Graphics::ImageLoader` rather than reimplementing image decoding),
`PlaylistParser` (M3U/M3U8), `SavedPictureStore`.

**Real bug found via testing, not just planned work**: `std::filesystem::directory_iterator`'s
enumeration order is filesystem-dependent, not alphabetical -- the first version of
`MediaLibraryIndex`'s case-insensitive Artist/Genre normalization (D10, "first-seen casing wins")
was therefore non-deterministic: which of "Artist One"/"ARTIST ONE" won depended on arbitrary
filesystem entry order, not scan-source order as intended. Caught by
`MediaLibraryIndexTest.NormalizesCaseVariantArtistNamesToOneCanonicalValue` actually failing (not
by inspection). Fixed by sorting directory entries before processing in all three scanners
(`MediaLibraryIndex`, `PictureLibraryIndex`, `PlaylistParser::ScanDirectory`) -- makes library-scan
output reproducible across platforms/filesystems, not just "first-seen" in an unpredictable sense.

Also found and fixed: `PictureLibraryIndex` called `ImageLoader::Load()` unguarded, which throws
`std::runtime_error` on a corrupted/unsupported image -- would have aborted an entire library scan
over one bad file. Now caught and skipped per-file.

40 new tests across `AudioTagParserTests.cpp`, `MediaLibraryIndexTests.cpp` (incl. a real
self-referential-symlink fixture proving the cycle guard actually terminates, not just reasoning
about it), `PictureLibraryIndexTests.cpp`, `PlaylistParserTests.cpp`, `SavedPictureStoreTests.cpp`,
`MediaLibraryPathsTests.cpp`, `MediaCollectionBaseTests.cpp`. Full-suite regression: **4733 tests,
4731 passed, 0 failed, 2 pre-existing hardware skips.**

**Phase 4 complete** (`MEDIA-61`..`MEDIA-69`, wiring the real backend onto the public XNA API).
`MediaSource`, `MediaLibrary` (the ~230-line orchestrator building the entire real object graph
from the Phase 3 indexes in its constructor), and all 6 item+collection pairs (`Genre`, `Artist`,
`Album`, `Picture`, `PictureAlbum`, `Playlist`) are now genuinely real -- every one of the 14
previously-`NotImplementedException` classes plus `MediaSource`/`MediaLibrary` now has working
constructors, real equality, real relationships. `MEDIA-69`'s object-graph audit
(`ObjectGraphIsInternallyConsistent`) passed on the first real run, confirming the whole graph
(Genre↔Album↔Artist↔Song↔Picture↔PictureAlbum, all cross-references) is internally consistent, not
just individually populated.

Real bugs found via actually building and testing this (not caught by inspection):
- `std::make_unique<T>()` cannot construct a friend-gated private constructor -- the `new` happens
  inside `std::`'s own implementation, which isn't a friend of `T`, only `MediaLibrary`'s own code
  is. Every friend-constructed type (`Genre`/`Artist`/`Album`/`Picture`/`PictureAlbum`/`Playlist`/
  their Collections) had to use `std::unique_ptr<T>(new T(...))` instead -- a real, easy-to-hit C++
  gotcha, not specific to this codebase.
- A real memory leak: every per-genre/per-artist/per-album/per-playlist `SongCollection` was
  `new`'d inline with no owning storage at all. Fixed with a new `ownedGroupSongCollections_`
  vector in `MediaLibrary`.
- A real design flaw caught before it shipped: the original `SavePicture`/`SavedPictures` design
  called `SavedPictureStore::GetSavedPicturesDirectory()` (which *creates* the directory)
  unconditionally at `MediaLibrary` construction time -- meaning just *opening* a library (even
  read-only browsing) would silently create a new "Saved Pictures" folder on disk, including
  inside the checked-in test fixture tree during every test run. Redesigned to be lazy: the
  directory (and its PictureAlbum tree node) is only created the first time `SavePicture()` is
  actually called.
- A test-authoring bug (not a production bug) in the first version of the `GetImage()` byte-round-
  trip test: comparing a `std::vector<char>` (signed) against a `std::vector<uint8_t>` via
  `std::equal` silently fails for any byte >= 0x80 (both operands promote to `int`, and e.g.
  `char(-1)` promotes to `int(-1)` while `uint8_t(255)` promotes to `int(255)`) -- common
  throughout real binary JPEG data. Fixed by making both vectors `uint8_t`.

Design choices made without FNA precedent (§0 -- FNA never built this half), recorded here since
they're not otherwise obvious from reading the code: Album's Genre is derived from its first-seen
member song's genre (not a full per-song-genre consensus); `Song.Duration` (and therefore
Album/Playlist `Duration`) stays at TimeSpan.Zero for library-scanned songs until actually played
via MediaPlayer -- deliberately NOT eagerly probed via FFmpeg, both to avoid a real
platform-conditional CMake complication (`CNA_FFMPEG_AVAILABLE` isn't currently exposed as a C++
preprocessor define) and because it's consistent with Song's already-established lazy-duration
pattern elsewhere in this codebase; `Picture::GetPictureFromToken`'s token is the picture's own
resolved file path (exposed via a new NOXNA `Picture::getTokenEXT()`), since FNA's real "opaque
library token" has no real desktop equivalent to source from.

Full-suite regression: **4785 tests, 4783 passed, 0 failed, 2 pre-existing hardware skips.** 90 new
Media-scoped tests across 9 files (`MediaSourceTests.cpp`, `GenreTests.cpp`, `ArtistTests.cpp`,
`AlbumTests.cpp`, `PictureTests.cpp`, `PictureAlbumTests.cpp`, `PlaylistTests.cpp`,
`MediaLibraryTests.cpp` incl. the object-graph audit, `MediaLibraryTestFixture.hpp` shared fixture).

**Phase 5 complete** (`MEDIA-70`..`MEDIA-75`, content-pipeline/XNB integration). New
`CNA::Internal::Xnb::VideoContentTypeReader` (`.hpp`/`.cpp`), registered under FNA's real canonical
name `Microsoft.Xna.Framework.Content.VideoReader` from `XnbBuiltInReaders.cpp` — `ContentManager::
Load<Video>()` is possible for the first time (confirmed: no such reader existed anywhere in the
repo before this task, and `plans/plan_xnb.md` never planned one). Field layout matches FNA's real binary
format exactly (reference string with the fake `.wmv` suffix stripped and re-resolved against
`.ogv`/`.oga`, mirroring `SongReader`'s `.wma`-strip/`Normalize` pattern, then `durationMS`/`width`/
`height`(int32) + `framesPerSecond`(float32) + `soundTrackType`(int32)) but uses direct typed reads
(`ReadInt32`/`ReadSingle`), matching `SongContentTypeReader`'s established CNA code style rather than
FNA's own internal inconsistency (`SongReader` reads directly; `VideoReader` reads via the more
generic `ReadObject<T>()`) — a pure C# implementation-detail difference with no binary-format effect
(`MEDIA-71`).

`MEDIA-75` re-verification found the existing coverage was real but incomplete: Phase 1's fix (the
`SongContentTypeReaderTests.cpp` assertion now expecting `System::IO::FileNotFoundException`) only
proved the exception type at the reader-`Read()`-called-directly level, not through the actual, real
`.xnb` container-parsing path (header + type-reader table + object dispatch) that
`ContentManager::Load<Song>()` actually uses. Added `ContentManagerSongXnbTest.
UnresolvableReferenceThrowsFileNotFoundExceptionThroughLoad` to the pre-existing
`ContentManagerSongXnbTests.cpp` (hand-built full container byte stream, matching
`ContentManagerXnbTests.cpp`'s own `BuildTestXnbFile()` technique) — confirms the corrected exception
type genuinely propagates end-to-end, not just out of the reader in isolation. (While building this
test, briefly hit — and then understood, not "fixed" — `ContentReader`'s real, correct FNA-matching
behavior of eagerly resolving *every* entry in a `.xnb`'s type-reader table up front, even unused
ones: the real `one_two_three.xnb` fixture's table lists both `SongReader` and `Int32Reader`, and a
test fixture that only registers `SongReader` fails to open it at all, independent of the actual
duration-field encoding.)

**Honest gap** (`MEDIA-73`): unlike `Song`, no real MonoGame-produced `Video` `.xnb` fixture was
locatable, and this environment has no `dotnet`/`mgcb` tooling to produce one. `VideoContentTypeReaderTests.cpp`
exercises `VideoReader::Read()`'s real logic via a hand-constructed in-memory buffer (matching
`SongContentTypeReaderTests.cpp`'s own established technique for non-container tests) rather than a
true `ContentManager::Load<Video>()` round-trip against an externally-produced binary. The
container-parsing path itself is separately proven correct via `Song`'s real end-to-end tests, so
this gap is narrowly about lacking a genuine Video binary sample, not doubt about the reader logic.
Flagged as a candidate follow-up for a future session with `mgcb` access.

Full-suite regression after Phase 5: **4791 tests, 4789 passed, 0 failed, 2 pre-existing hardware
skips.** New/changed Media-scoped tests: `VideoContentTypeReaderTests.cpp` (5 new), 1 new test added
to the pre-existing `ContentManagerSongXnbTests.cpp`.

**Phase 6 complete** (`MEDIA-76`..`MEDIA-120`, the consolidated test-completeness audit). Ran a
dedicated audit pass (a background research agent, cross-checked before acting on it) against every
one of the 44 individual test-coverage tasks (`MEDIA-76`..`MEDIA-119`) comparing the plan's own
stated Accept criteria against the actual test files. Result: 15 tasks were already genuinely
covered by tests written incrementally in Phases 1-5 (confirming the plan's own "make and forget"
premise); **29 were real, concrete gaps** — this phase closed all but one of them:

- **Library item/collection classes** (`Genre`/`Artist`/`Album`/`Picture`/`PictureAlbum`/`Playlist`
  and their 6 collections, `MEDIA-98`..`MEDIA-109`): every collection's own `Dispose()`/`IsDisposed`
  was untested (only the contained item's), several in-bounds indexer paths were untested (only
  out-of-range), and several item-level properties had zero coverage at all (`Genre.Albums`,
  `Artist.IsDisposed`, `Album.Duration`/`GetThumbnail()`/`IsDisposed`, `Picture.Date`/
  `GetThumbnail()`/`IsDisposed`, `PictureAlbum.IsDisposed`, `Playlist.Duration`/`IsDisposed`/
  `Equals(nullptr)`). Fixed by adding the missing assertions to each class's existing test file.
  `Album`'s required-but-previously-only-indirect "same-name-different-artist" equality case
  (`MEDIA-102`) got a real, dedicated scratch music tree (two artists both named "Collision",
  matching `MediaLibrarySavePictureTest`'s own established scratch-dir pattern) rather than
  continuing to reason about it indirectly.
- **`MediaPlayer`** (`MEDIA-76`, `MEDIA-79`..`MEDIA-84`): `Song`'s unequal-handle equality case was
  missing; `VisualizationDataTests.cpp` **did not exist at all** before this phase (created); the
  plain `Play(SongCollection)` overload, `MovePrevious()`, `IsRepeating`/`IsShuffled` getters,
  `IsMuted`, and real `MediaState` transitions across Play/Pause/Resume/Stop had zero coverage.
  `ActiveSongChanged`/`MediaStateChanged` had never been driven through the real
  `FrameworkDispatcher::Update()` call chain — added, using `EventHandler<T>::Add()`/`Remove()`
  (not `operator+=`) since these events are process-global statics and a leaked `+=` subscription
  capturing local test variables by reference would dangle for the rest of the test binary's run.
- **`Video`/`VideoPlayer`/`VideoDecoder`** (`MEDIA-86`, `MEDIA-87`, `MEDIA-89`, `MEDIA-90`,
  `MEDIA-93`, `MEDIA-95`): `Video`'s own `SetAudioTrackEXT`/`SetVideoTrackEXT` were untested;
  `VideoPlayer`'s `Stop`/`Pause`/`Resume` state transitions and `PlayPosition` were only ever
  exercised as post-`Dispose()` throw-guards, never for their actual live behavior; `IsMuted`/
  `Volume` clamping were untested on `VideoPlayer`. Three **new real fixtures** were authored
  (`tests/assets/media/video/`, manifest updated) to close the rest: `av1_with_audio.mkv` (real
  `libaom-av1`-encoded video + a real audio track, decoded via libavcodec's native `av1` decoder —
  proves `MEDIA-37`'s "AV1 keeps its audio track" claim against genuine AV1 content, not just the
  Phase 0 fixture set's `ffv1` files, which can't exercise this codec-specific claim) and
  `multi_track_audio.mkv` (one video stream + two audio streams at deliberately different sample
  rates — 48000 Hz vs 44100 Hz — so `VideoDecoder::SetAudioStream()`'s track switch can be proven
  by an actual `GetSampleRate()` change, not just "didn't crash"). `DrainAudio()` itself had never
  been called directly by any test (only `HasAudio()`/`GetSampleRate()`/`GetChannels()` presence) —
  added a real decode-then-drain round-trip. **`MEDIA-86`'s "codec-guess-equivalent behavior"**
  wording was found to be stale/non-existent (confirmed by grep: no such logic exists anywhere in
  `Video.cpp`/`Video.hpp`) — dropped from the task rather than invented a test for it.
- **`AudioTagParser`** (`MEDIA-111`): the full ID3v2 text-encoding-byte matrix (Latin-1/
  UTF-16+BOM-little-endian/UTF-16+BOM-big-endian/UTF-16BE-no-BOM/UTF-8) was untested — the two real
  MP3 fixtures only exercise whichever single encoding their own real tagger happened to write.
  Closed with hand-built minimal ID3v2.4 byte buffers (matching this session's established
  hand-constructed-binary-buffer technique from the XNB reader tests), one per encoding, calling
  `AudioTagParser::TryReadId3v2()` directly.
- **`MediaLibraryIndex`** (`MEDIA-113`, `MEDIA-119`): the permission-denied half of `MEDIA-53`'s
  hardening (only the symlink-cycle half had a test) was untested — closed with a real `chmod`'d
  unreadable subdirectory (skips itself if run as root, since permission bits don't restrict root).
  `MEDIA-119`'s case-insensitive-Artist-normalization regression previously only lived inside
  `ArtistTests.cpp` (the file it was supposed to be isolated from) — moved to its own dedicated
  `ArtistGenreNormalizationRegressionTests.cpp`.

**One gap remains open, documented rather than closed with a workaround** (`MEDIA-94`): true
allocation-failure fault injection for `AllocAndConfigureCodecContext`'s two guarded branches.
`avcodec_alloc_context3` returning null is real-OOM-only and not reachable without process-level
fault injection (e.g. an `LD_PRELOAD` malloc interceptor) — infrastructure disproportionate to this
one test's value. `avcodec_parameters_to_context` failing on malformed codec parameters is
theoretically reachable via a hand-crafted file, but no reliable, non-flaky way to construct one was
found within this phase's scope. The corrupt/truncated-fixture and I/O-error-vs-EOF halves of
`MEDIA-94` (`MEDIA-38`/`MEDIA-40`) were already real and covered before this phase.

Full-suite regression after Phase 6: **4846 tests, 4844 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full for `FAILED`, not a truncated tail, per
`MEDIA-120`'s own requirement.

**Phase 7 complete** (`MEDIA-121`..`MEDIA-126`, documentation and closure — **`plans/plan_media.md` is now
fully complete, 126/126 tasks across all 7 phases**). Added 10 new Media-specific rows to
`CHECKLIST.md`'s deviations table (the real from-scratch library implementation itself, D1/D2/D5/D6
from §4, the FFmpeg-unified video decode backend, `VideoSoundtrackType` metadata-only fidelity, AV1
keeping its audio track, `Song::GetHashCode()`'s content-based semantics, and the project-wide
out-of-range-indexer exception-type inconsistency). Updated `AUDIT.md`'s
`Microsoft::Xna::Framework::Media` table: every `Album`/`Artist`/`Genre`/`MediaLibrary`/`Picture`/
`PictureAlbum`/`Playlist`/`MediaSource` row's stale "(stub behavior)" note replaced with a real
status description (none of the new internal `CNA::Internal::Media::*` types were added as their own
rows, matching Audio's existing `XactParser`/`AudioMixer` convention of keeping internal backend
types out of the public API table). Final build (`CNA` + `CnaTests`, both targets, `tests` preset)
and one more full-suite regression run: **4846 tests, 4844 passed, 0 failed, 2 pre-existing hardware
skips** — identical count to Phase 6's own pass, as expected since Phase 7 touched no code, only
documentation. The future-addendum convention (MEDIA-126) was already recorded in this plan's own
provenance note at the time it was first drafted — confirmed, no further edit needed.

This file's own remaining open items (NOXNA `Refresh()`, a Playlist writer, a taglib upgrade path,
embedded-`APIC`-frame album art, the `TouchCollection` exception-type inconsistency, and
`MEDIA-94`'s allocation-fault-injection gap) are recorded in §5 below for a future session to pick up
— none of them block calling this plan complete.

## 1a. Phase 8 — external review remediation (2026-07-18)

An external adversarial review of the Phase 7 "126/126, consistently done" claim checked the actual
code against every task's own literal Accept criterion (not the checkbox) and found 11 real,
confirmed defects. All are now fixed or honestly documented (`plans/plan_media.md` `MEDIA-127`..`MEDIA-138`):

1. **`Album`/`Playlist.Duration` were permanently zero** (`MEDIA-127`) — library-scanned `Song`s
   never had a real duration set, `Album.Duration` was hardcoded to `TimeSpan.Zero`, and even
   playing a whole Album/Playlist via `MediaPlayer` operated on a `MediaQueue` duplicate, never the
   library's own `Song` object. Fixed with a new, decode-free `AudioDurationProbe`
   (`avformat_find_stream_info` container-metadata parsing, gated behind a new
   `CNA_FFMPEG_AVAILABLE` compile definition) that populates real durations at library-scan time.
2. **`VideoDecoder`'s FFmpeg return-code hardening was incomplete** (`MEDIA-128`) —
   `avcodec_receive_frame()`'s genuine decode errors were still merged with clean EOF,
   `avcodec_send_packet()`'s return was discarded, `av_frame_alloc()`/`av_packet_alloc()` were
   never null-checked. All fixed.
3. **The truncated-file test was tautological** (`MEDIA-129`) — asserted success on *either* a
   throw *or* a clean EOF, so it could never fail. Replaced with two real, deterministic tests;
   closing this properly required discovering (via extensive direct `ffmpeg` CLI experimentation)
   that Matroska's demuxer always treats truncation as clean EOF, and that this build's `ffv1`
   decoder never hard-fails on corrupted data even under every `AV_EF_*` strictness flag — a new
   `corrupt_test_h264.mp4` fixture (H264 is far less lenient) was needed to genuinely exercise the
   error path, plus `AV_EF_CRCCHECK` added to `AllocAndConfigureCodecContext` (the specific bit
   that makes a decoder's own CRC check anything more than informational).
4. **A real `MEDIA-41` audio-tail-drain bug** (`MEDIA-130`) — `VideoPlayer::GetTexture()` only
   drained decoded audio in the successful-video-frame branch, stranding audio decoded during the
   final (EOF-returning) `NextFrame()` call. Fixed by draining unconditionally after every call.
5. **A real track-switching ordering bug** (`MEDIA-131`) — `OpenDecoder()` built the SDL audio
   stream/texture from the decoder's *default* track, only applying a caller's track preference
   afterward; a mid-playback switch had the identical problem. Fixed with a shared
   `ReconfigureAudioAndVideoOutputForCurrentTracks()` helper, applied at the right time in both
   cases. Found and fixed a related, separate pre-existing bug in the same pass:
   `getVideoProperty()` always returned `nullptr` after any successful `Play()` call (nothing
   restored `video_` after `CloseDecoder()`'s unconditional reset).
6. **`SavePicture()` never created a real `PictureAlbum` node for "Saved Pictures"** (`MEDIA-132`)
   — silently fell back to `RootPictureAlbum` forever. Fixed with a new, idempotent
   `EnsureSavedPicturesAlbum()`.
7. **SECURITY: `SavedPictureStore` had a path-traversal vulnerability** (`MEDIA-133`) — the
   caller-supplied `name` was concatenated into a filesystem path with zero sanitization; an
   absolute path or `../` sequence could write outside the real Saved Pictures directory entirely.
   Fixed with a new `SanitizePictureName()`.
8. **`MEDIA-73`'s checkbox self-contradicted its own written gap note** (`MEDIA-134`) — checked
   `[x]` while explicitly admitting its own Accept criterion wasn't met. Now genuinely closed: a
   hand-constructed (not mgcb-produced — no such tooling is available in this environment) but
   binary-format-accurate `.xnb` container proves the real, full `ContentManager::Load<Video>()`
   round-trip end-to-end, including that the result is playable via `VideoPlayer`.
9. **`MEDIA-32`'s test coverage was overclaimed** (`MEDIA-135`) — its own Accept criterion (a test
   exercising the no-`SOUND_ENABLED` fallback's real wiring into `MediaPlayer::Update()`) was never
   actually met, because `SOUND_ENABLED` is unconditionally defined for every build configuration
   this whole project produces — a genuine, pre-existing, project-wide condition affecting every
   `#ifdef SOUND_ENABLED` pair across the Audio/Media stack, not just Media. Documented honestly;
   closing it for real needs a new CMake build variant with CI coverage, out of this task's scope.
10. **`Video`/`VideoPlayer` are unavailable via a link error, not a clean `NotSupportedException`,
    on Windows/Android/Emscripten** (`MEDIA-136`) — a genuine, pre-existing architectural condition
    (`cmake/CnaLibrary.cmake`'s `CNA_FFMPEG_AVAILABLE` gate excludes the `.cpp` files but not the
    headers on those platforms). Documented in `AUDIT.md` (⚠️, not a silent ✅); a real fix (runtime
    stub implementations) is a larger, separate undertaking this sandbox can't build or verify
    anyway.
11. **Missing `GetTypeName()` tests + undocumented `CHECKLIST.md` deviations** (`MEDIA-137`) — all
    6 collection types and `VideoPlayer` had no `GetTypeName()` test; `VideoPlayer::Dispose()`'s
    idempotency and `GetTexture()`-before-`Play()`-returning-`nullptr` had inline comments but no
    `CHECKLIST.md` row. Both closed.

Full-suite regression after Phase 8: **4863 tests, 4861 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail.

**Lesson for future sessions:** the Phase 6 test-completeness audit (also done via a dedicated
research pass) caught 29 gaps and was itself a good process — but it audited *test coverage*
against the plan's task list, not *code correctness* against each task's actual runtime behavior.
Both kinds of adversarial pass are needed before trusting a "complete" claim; this phase is the
second kind.

## 1b. Phase 9 — second external review pass (2026-07-18, same day as Phase 8)

A second external review, this time of Phase 8's own fix commit (`52eec0a5`), found the Phase 8
fixes were real but incomplete on several points, plus one genuine new regression:

1. **New regression: `Play()` left the SDL audio stream paused forever** (`MEDIA-139`) — every
   video with an audio track played completely silently after Phase 8's own track-switching fix.
   `ReconfigureAudioAndVideoOutputForCurrentTracks()` only calls `SDL_ResumeAudioStreamDevice()`
   when `state_ == Playing`, but during a fresh `Play()`'s own `OpenDecoder()` call, `state_` was
   still `Stopped` (only set to `Playing` *after* `OpenDecoder()` returned) — and
   `SDL_OpenAudioDeviceStream()` opens every stream paused by default. Fixed by moving the
   `state_ = Playing` assignment to before the reconfigure call (with exception-safety: reverted to
   `Stopped` if the first-frame decode then throws). **A deeper root cause found in the same
   investigation:** `VideoPlayer.cpp` never initializes SDL's audio subsystem at all —
   `GraphicsDevice` only calls `SDL_InitSubSystem(SDL_INIT_VIDEO)`. Fixed with a properly paired
   `SDL_InitSubSystem(SDL_INIT_AUDIO)`/`SDL_QuitSubSystem(SDL_INIT_AUDIO)` around every stream
   open/destroy.
2. **`VideoDecoder`'s FFmpeg error handling was still incomplete** (`MEDIA-140`) — the EOF flush's
   return was unchecked, `ProcessAudioPacket`'s own `avcodec_send_packet` failure was silently
   swallowed, `av_frame_alloc()` for the audio frame was never null-checked, `avcodec_receive_frame`/
   `swr_convert` errors inside it weren't propagated, and a video packet that got `EAGAIN` from
   `avcodec_send_packet` was discarded instead of retried (silently dropping that frame). All fixed
   — the `EAGAIN` case now genuinely retains and retries the packet via a new
   `havePendingVideoPacket` flag.
3. **The audio-tail gap wasn't fully closed** (`MEDIA-141`) — the audio *codec* itself was never
   flushed at EOF (only video was), and the `VideoPlayer`-level `audio_tail.mkv` test stayed
   explicitly deferred while the task stayed checked off. Both fixed for real: `ProcessAudioPacket(nullptr)`
   now flushes audio at EOF too, and a genuine `NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration`
   test proves the real 2.0s-video/3.0s-audio behavior.
4. **"Duration probe always returns zero"** (`MEDIA-142`) — verified, not fixed: the cited line is
   inside the deliberate `#else` (no-FFmpeg) fallback branch, not the branch this sandbox actually
   compiles (`ninja -t commands` confirms `CNA_FFMPEG_AVAILABLE` is genuinely defined here), and
   the already-passing real-duration tests independently confirm the real branch works.
5. **`SavePicture()`'s edge case wasn't fully closed** (`MEDIA-143`) — if the Pictures root
   directory didn't exist at all when `MediaLibrary` was constructed, `EnsureSavedPicturesAlbum()`
   still bailed out with `nullptr` (no root to attach to), leaving the saved picture permanently
   unparented even though the file itself was written to disk. Fixed: the function now also lazily
   bootstraps a real root `PictureAlbum` node first when needed.
6. **A real bug found while verifying #1** (`MEDIA-144`, not part of either review's own list —
   found during this session's own verification work): `SetAudioStream()`/`SetVideoStream()`
   unconditionally discarded and recreated the codec context even when re-selecting the
   *already-active* track. Harmless for audio, but a real, user-visible bug for video: Phase 8's own
   error-propagation fix newly surfaced it as a thrown `"Cannot decode non-keyframe without valid
   keyframe"` exception (previously silently tolerated). Fixed with a same-index no-op check in
   both setters.

Full-suite regression after Phase 9: **4867 tests, 4865 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail. Three of the new/fixed
tests (`PlayGenuinelyResumesTheAudioStreamNotJustOpensIt`, `PauseStillActuallyPausesTheAudioStream`,
`SetAudioTrackEXTAndSetVideoTrackEXTDoNotBreakPlaybackAfterPlay`) were also verified passing in
isolation (`--gtest_filter` on just that one test), not only as part of the full suite, since one of
them turned out to be timing-sensitive enough that cross-test ordering could otherwise mask it.

**Lesson reinforced:** this is the *second* real round of findings from an adversarial review in
the same day, on the same plan. The Phase 8 fix commit itself introduced a brand-new severe
regression (silent audio) while fixing something else, and that regression's own root cause
(missing `SDL_INIT_AUDIO`) was *deeper* than what the review report itself identified — a symptom
one review round correctly spotted led to a cause a second, deeper investigation found. Do not
assume a fix commit is regression-free just because it was itself written carefully and its own
targeted tests passed; a fix for one bug can introduce another, especially in shared helper
functions (`ReconfigureAudioAndVideoOutputForCurrentTracks()` here) called from multiple call sites
with different preconditions.

## 1c. Phase 10 — third external review pass (2026-07-18, same day as Phase 8/9)

A *third* external review, this time of Phase 9's own fix commit (`9b80500d`), confirmed the
audio-silence regression and the track-switching bug were genuinely fixed — no new regression this
round — but found the Phase 9 fix itself was still incomplete on five specific points:

1. **The EAGAIN retry itself didn't survive across `NextFrame()` calls** (`MEDIA-146`) —
   `havePendingVideoPacket` was a function-local flag. The very next `avcodec_receive_frame()` call
   after setting it almost always immediately returns a buffered frame (that's what unblocked the
   EAGAIN), and the function returns to the caller *before* the retained packet is ever resent — so
   the *next* call to `NextFrame()` started with the flag reset to `false`, and the retained packet
   was silently overwritten by the next `av_read_frame()` call. The exact bug `MEDIA-128`/`MEDIA-140`
   set out to fix, recurring across call boundaries instead of within one call. Fixed by making it a
   class member (`havePendingVideoPacket_`), reset in `Close()`.
2. **The resampler's own internal buffer was never flushed at EOF** (`MEDIA-147`) — only the audio
   *codec* was flushed (Phase 9's own `MEDIA-141` fix), never `SwrContext` itself. Practical impact is
   small today (the resampler only does format conversion, not a real rate change, in every fixture
   this repo has), but it's a real completeness gap for any future path with a genuine rate change.
   Fixed with `swr_get_delay()` + a null-source `swr_convert()` call at EOF.
3. **A shared reconfiguration helper still destroyed the unrelated side on a track switch**
   (`MEDIA-148`) — `ReconfigureAudioAndVideoOutputForCurrentTracks()` always tore down and recreated
   *both* the SDL audio stream and the video texture, so `SetVideoTrackEXT()` discarded queued audio
   and `SetAudioTrackEXT()` needlessly reallocated the texture. Split into independent
   `ReconfigureVideoOutputForCurrentTrack()`/`ReconfigureAudioOutputForCurrentTrack()`, each called
   only by the setter that actually needs it (`OpenDecoder()` still calls both, for the initial open).
4. **`Play()`'s exception path left the player half-open** (`MEDIA-149`) — Phase 9's own
   `try`/`catch` (added to fix `MEDIA-139`) only reset `state_` to `Stopped` on a first-frame-decode
   failure, but left `decoder_`/`audioStream_`/`frameTexture_`/`video_->parent_` all still
   allocated/set — `state_` claimed nothing was open while every real resource said otherwise. Fixed
   by calling the same `CloseDecoder()` every other exit path already uses.
5. **A stale open-item note in this file** (`MEDIA-150`) — §5 still listed a dedicated
   `VideoPlayer`-level `audio_tail.mkv` test as future work, even though Phase 9 already added and
   closed exactly that (`NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration`). Corrected
   in place rather than deleted, so the closure is visible in the historical record.

Full-suite regression after Phase 10: **4871 tests, 4869 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full (`grep -c FAILED` on the complete log), not a
truncated tail. 4 new tests added by this phase, 0 pre-existing tests broken.

**Lesson reinforced a third time:** every one of the last three review rounds found real, specific,
file-and-line-cited defects that "the build is clean and the targeted tests pass" did not surface on
its own — a self-verification blind spot, not a one-off. Treat "N/N complete" as a claim needing its
own independent adversarial pass, every time, not a status to state and move on from.

## 1d. Phase 11 — fourth external review pass (2026-07-18, same day as Phase 8/9/10)

A *fourth* external review, this time of Phase 10's own fix commit (`8f23f747`), confirmed three of
Phase 10's five fixes fully sound (EAGAIN retention, resampler flush, the audio/video reconfigure
split) but found the `Play()`-exception-safety fix incomplete, plus two further real defects:

1. **`OpenDecoder()`'s exception safety still had a gap** (`MEDIA-152`) — `MEDIA-149`'s `try` block
   only wrapped the first-frame decode, not the two reconfigure calls immediately before it. An
   exception from `Texture2D` construction inside the reconfigure step would still bypass
   `CloseDecoder()`. Fixed by widening the `try` block to start right after
   `state_ = MediaState::Playing`. No dedicated fault-injection test — neither reconfigure function
   has a reachable, deterministic throw path with this repo's real fixtures today, matching the
   `MEDIA-94` precedent for not over-building test infrastructure for a defensive-only path.
2. **Unbounded audio-buffer accumulation with no audio device** (`MEDIA-153`) —
   `decoder_->DrainAudio(audioBuffer_)` ran every iteration, but `audioBuffer_.clear()` only ran
   inside `if (audioStream_)`. A video with real audio but no audio device accumulated its entire
   decoded audio track in memory for the rest of playback (potentially hundreds of MB to GB), and
   `CloseDecoder()` never cleared it either. Fixed by factoring the drain-and-feed logic into a new
   `DrainAndFlushAudioBuffer()` that always clears the buffer, and clearing it in `CloseDecoder()`
   too.
3. **Track setters still did needless/destructive work on a same-track reselect** (`MEDIA-154`) —
   `VideoDecoder::SetAudioStream()`/`SetVideoStream()` correctly no-op at the decoder level for an
   already-active or out-of-range track, but `VideoPlayer::SetAudioTrackEXT()`/`SetVideoTrackEXT()`
   called their reconfigure helper unconditionally anyway — the `MEDIA-148` problem on a different
   axis (nothing changed, not the wrong side reconfigured). Fixed: both `VideoDecoder` setters now
   return `bool` (true only on a genuine switch), and `VideoPlayer` only reconfigures when true.
4. **Two `VideoDecoder` internal-state-reset gaps** (`MEDIA-155`) — `Close()` never cleared
   `pendingAudio_` (stale samples could leak into a reused instance's next file), and
   `SeekToStart()` never reset the `MEDIA-146` pending-video-packet flag/reference (a packet
   retained from before a seek could be resent to the just-flushed codec, referring to data at the
   old read position). Both fixed. The `pendingAudio_` half has a direct deterministic test; the
   `SeekToStart()` half is verified by code review only — the reviewer's own report reached the same
   conclusion that reliable fault injection here is impractical.

Full-suite regression after Phase 11: **4876 tests, 4874 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail. 5 new tests added, 0
pre-existing tests broken.

**Lesson reinforced a fourth time:** this review, like the second and third, found gaps specifically
in a *fix commit* written in response to the prior review — not new symptoms of the original bug,
but a fix that was directionally correct yet didn't fully close its own stated problem (exception
safety wrapped one throw site but not an adjacent one) or introduced a new, narrower variant of a
problem it had just fixed on a different axis (`MEDIA-148`'s cross-track coupling fix left the
same-track-reselect case unfixed). Four rounds in, the pattern holds: verify every fix commit as
skeptically as the code it was responding to, and check whether a fix's own stated scope ("switching
tracks") was quietly narrower than the bug's real scope ("any call to this setter, including a
no-op one").

## 1e. Phase 12 — fifth external review pass (2026-07-18, same day as Phase 8/9/10/11)

A *fifth* external review, this time of Phase 11's own fix commit (`ec863ae9`), confirmed all four
of Phase 11's fixes real and effective, then found three more real defects — two in Phase 11's own
work, one a genuinely older gap (`MEDIA-38`) surfaced by looking one call deeper:

1. **`MEDIA-154`'s own bool-return contract was implemented wrong on the failure path**
   (`MEDIA-158`) — `SetAudioStream()`/`SetVideoStream()` called `OpenAudioStreamByIndex(i)`/
   `OpenVideoStreamByIndex(i)`, discarded the result, and unconditionally `return true`. Since both
   open functions build the new codec context before touching the "active" state, a genuine open
   failure left the *old* stream active while telling `VideoPlayer` a switch happened anyway —
   triggering a needless (and, for audio, destructive) reconfigure for a track that never actually
   changed. Fixed: both setters now `return OpenAudioStreamByIndex(i)`/`return
   OpenVideoStreamByIndex(i)` directly.
2. **`SeekToStart()` had three more gaps beyond the one `MEDIA-155` already fixed** (`MEDIA-159`) —
   `av_seek_frame()`'s own return was never checked (a failed seek still triggered a full flush,
   building an inconsistent "we're at the start" assumption on a position that never moved);
   `pendingAudio_` was never cleared by `SeekToStart()` itself (only by `Close()`); and the
   resampler's own internal delay buffer was never discarded on seek (the same class of gap
   `MEDIA-147` fixed for EOF, but for a seek discontinuity). Fixed: `SeekToStart()` now returns
   early on a failed seek, and on success also clears `pendingAudio_` and discards the resampler's
   buffered delay.
3. **The original `MEDIA-38` task's own Accept criterion was never actually met** (`MEDIA-160`) —
   it called for `swr_alloc_set_opts2()`/`swr_init()` failure to throw a clear exception; the real
   code has always just silently set `swrCtx_ = nullptr`. Worse, `HasAudio()` checked only
   `audioCtx_`, so a video whose resampler failed to set up would report `HasAudio() == true` while
   producing zero audio samples forever. Fixed the *observable* half: `HasAudio()` now also requires
   `swrCtx_ != nullptr`. Deliberately did **not** add a literal `throw` inside `SetupResampler()` —
   doing so would require either breaking `Open()`'s established non-throwing `bool` contract (relied
   on, unwrapped, by dozens of existing call sites) or throwing and immediately catching it right
   back into the same graceful-degradation behavior that already existed, a no-op complication this
   project's own conventions discourage. `MEDIA-38`'s task text is corrected in `plans/plan_media.md` to
   describe what's actually the right fix for this failure mode, rather than forced to match wording
   written before the failure mode's real behavior was understood.

Full-suite regression after Phase 12: **4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail, confirmed on two
independent full runs. 1 new test added. A `double free or corruption` crash in an unrelated ENet
test (`ENetBackendTest.HostFreesOwnedRemoteGamerOnDispose`) appeared on the very first full run of
this phase; before assuming it was a regression, it was isolated via `git stash`/`git stash pop`: the
same crash was absent both on a full run of the pre-Phase-12 baseline (`ec863ae9`) and on a
subsequent full run of Phase 12's own code — confirming it as pre-existing test-run-to-test-run
flakiness in `Net`/ENet, unrelated to anything this plan's scope touches, not something to chase down
here.

**Lesson reinforced a fifth time:** two of these three findings were, again, gaps in the *previous
fix commit itself* — `MEDIA-154` added a bool contract but got the failure-path return value wrong,
and `MEDIA-155` fixed one `SeekToStart()` gap while three siblings sat right next to it unaddressed.
The third finding (`MEDIA-38`) shows a different, complementary failure mode: a task marked `[x]`
years (well, phases) earlier whose Accept criterion was never actually verified against the shipped
code, only assumed satisfied because the surrounding hardening work *felt* thorough. Five rounds in,
both lessons hold simultaneously: verify every fix commit as skeptically as the code it responded to,
*and* periodically re-read old `[x]` tasks' own Accept criteria against current code rather than
trusting a checkbox that was never independently re-verified.

## 1f. Phase 13 — sixth external review pass (2026-07-18, same day as Phase 8/9/10/11/12)

A *sixth* external review, this time of the Phase 12 fix commit (`0df369bc`), confirmed everything
that phase actually landed, then found four more real defects — three of them gaps *within* Phase
12's own fixes, one an older asymmetry surfaced by looking at the audio path's EAGAIN handling for
the first time (the video side got this fix back in `MEDIA-146`; audio never did):

1. **`MEDIA-158`'s bool-contract fix was itself incomplete for the resampler-failure case**
   (`MEDIA-162`, most severe finding this round) — `OpenAudioStreamByIndex()` committed the new
   codec context and destroyed the OLD one *before* calling the resampler setup, then returned
   `true` unconditionally regardless of whether the resampler actually succeeded. A resampler
   failure at that point meant: the old, working audio track is gone, the new one can never produce
   audio, and the caller is told the switch succeeded anyway. Fixed by making the whole operation
   transactional: `SetupResampler()` (renamed `CreateResampler()`) now returns the new `SwrContext*`
   instead of assigning it directly, so both the codec AND the resampler are built and verified
   *before* anything old is touched. `Open()`'s own initial audio setup got the same treatment for
   consistency.
2. **`SeekToStart()`'s resampler-delay discard wasn't robust** (`MEDIA-163`) — a single
   `swr_convert()` call, return value unchecked, no confirmation the delay actually reached zero.
   Fixed: now a bounded loop (max 8 iterations) that checks the return each pass and stops on either
   a fully-drained delay or a non-positive result.
3. **`MEDIA-38`'s own original task text was never actually edited** (`MEDIA-165`) — Phase 12's
   commit message claimed the text was corrected, but the diff only appended a new note in the Phase
   12 section; `MEDIA-38`'s own bullet, in its own original location, still literally demanded a
   thrown exception and a fault-injection/ASan test, directly contradicting the new note next to it.
   Fixed: `MEDIA-38`'s own bullet now carries the correction in place (original text preserved and
   labeled as superseded, followed by the actual decision and why), matching this plan's own
   established precedent for in-place task-note correction.
4. **The audio side never got the EAGAIN-retry fix the video side got in `MEDIA-146`**
   (`MEDIA-164`) — `ProcessAudioPacket()` tolerated `AVERROR(EAGAIN)` from `avcodec_send_packet`
   without throwing, but never resent the packet; the caller unconditionally unrefs it regardless,
   silently discarding real audio data on the rare occasions this triggers. Fixed with a retry loop
   mirroring the video-side pattern, contained within the one function's own call (unlike the video
   side, an audio packet doesn't need to survive across separate `NextFrame()` calls).

Full-suite regression after Phase 13: **4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail. No new tests added this
phase; all four fixes verified by direct code review plus the unchanged full-suite pass (each
involves an FFmpeg failure mode this plan has repeatedly documented as impractical to fault-inject
with real fixtures — see `MEDIA-94`/`MEDIA-147`/`MEDIA-152`/`MEDIA-155` for the established
precedent this follows).

**Lesson reinforced a sixth time, with a new wrinkle:** three of four findings were gaps *inside*
fixes from the immediately preceding round — reinforcing the now-familiar pattern that a fix commit
needs its own skeptical read, not just "did the specific line I changed do what I meant." The new
wrinkle is finding #3: a commit message asserted a documentation correction that the actual diff
never made. **A commit message's own claims about what changed are not proof — verify the diff
itself, especially for claims like "corrected the text" or "fixed the docs," which are easy to
believe without opening the file.**

## 1g. Phase 14 — seventh external review pass (2026-07-18, same day as Phase 8-13)

A *seventh* external review, this time of the Phase 13 fix commit (`94892fb2`), confirmed the
transactional audio-switch fix (`MEDIA-162`) and the audio EAGAIN retry (`MEDIA-164`) fully sound,
then found three more real defects — one a genuine robustness gap in a just-landed fix, two pure
documentation errors introduced by the immediately preceding round's own correction work:

1. **`SeekToStart()`'s resampler-delay drain loop still couldn't guarantee a clean reset**
   (`MEDIA-167`) — `MEDIA-163`'s bounded loop was real progress but could still exit via its own
   8-iteration bound or a `swr_convert()` error with delay left unconfirmed-drained, and didn't
   distinguish a genuine negative error from "nothing more produced." Fixed by discarding the whole
   approach: `SeekToStart()` now `swr_free()`s the resampler and rebuilds a fresh one via
   `CreateResampler()` — a new `SwrContext` has zero delay by construction, sidestepping the
   draining question entirely.
2. **`MEDIA-165`'s own correction of `MEDIA-38`'s text introduced a new factual error**
   (`MEDIA-168`) — it claimed `Open()`-time allocation null-checks "do throw," but every one of them
   (video codec context, audio codec context, `frame_`/`pkt_`) actually gracefully `return false`,
   matching `Open()`'s own non-throwing contract. The one genuine throw in this family is
   `ProcessAudioPacket()`'s decode-time `av_frame_alloc()`, a different call entirely. Fixed by
   editing `MEDIA-38`'s correction paragraph again, in place, verified line-by-line against the
   actual code this time.
3. **`HasAudio()`'s doc comment went stale the moment `MEDIA-162` landed** (`MEDIA-169`) — it
   described a resampler-failure scenario ("inside `SetupResampler()`" during initial setup) that
   Phase 13's transactional rewrite had already made impossible, referencing a function name that no
   longer existed. Fixed by rewriting the comment to describe the real, current divergence path
   (`SeekToStart()`'s resampler recreation, introduced in this very phase by `MEDIA-167`).

Full-suite regression after Phase 14: **4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail. No new tests added —
**and this phase's claim that `MEDIA-167` was "covered by the existing real-playback
`LoopedVideoKeepsPlayingPastItsDuration` test" was itself false**, corrected in Phase 15 below
(`MEDIA-171`): that fixture has no audio stream at all.

**Lesson reinforced a seventh time, with a new wrinkle:** two of three findings this round were
*documentation errors introduced by the previous round's own correction commit* — not gaps in the
code, but factual mistakes made while writing prose ABOUT the code, while trying to fix a different
documentation problem. **Writing a correction is not exempt from the same verify-against-the-actual-
code discipline as writing the original code — a paragraph explaining "here's what the code actually
does" needs the same line-by-line check as the code itself, especially right after being told the
previous version of that exact paragraph was wrong.**

## 1h. Phase 15 — eighth external review pass (2026-07-18, same day as Phase 8-14)

An *eighth* external review, this time of the Phase 14 fix commit (`56e391e7`), confirmed the
`SeekToStart()` resampler-recreation fix factually correct as code — but found its claimed
regression coverage was entirely fictitious:

1. **`MEDIA-167`'s claimed test coverage did not exist** (`MEDIA-171`) — its Accept note asserted
   coverage from `LoopedVideoKeepsPlayingPastItsDuration` ("genuinely exercising `SeekToStart()`
   with live audio"), but that test uses `chroma_420.mkv`, which has **no audio stream at all**
   (confirmed via `manifest.json` and a direct `ffprobe` run), so it never enters `SeekToStart()`'s
   audio block. The second cited test only asserts the pending buffer is *empty* post-seek — which
   would pass even more readily if the fix were broken and no audio decoded at all. Fixed by adding
   `SeekToStartRebuildsAWorkingResamplerSoAudioStillDecodesAfterTheSeek`, which asserts `HasAudio()`
   is still true after a seek AND that further decoding genuinely produces new audio samples.
   **Verified falsifiable by mutation testing**: with the `CreateResampler()` call temporarily
   removed, the new test fails both assertions while the old test still passes.
2. **Wrong task-ID cross-reference** (`MEDIA-172`) — `HasAudio()`'s comment, rewritten by
   `MEDIA-169`, attributed itself to `MEDIA-168` (which was the unrelated `MEDIA-38` text fix).
   Corrected.

Full-suite regression after Phase 15: **4878 tests, 4876 passed, 0 failed, 2 pre-existing hardware
skips** (Accelerometer/Gyroscope) — grepped in full, not a truncated tail. 1 new test added.

**Lesson reinforced an eighth time — the sharpest form yet:** for the third consecutive round, the
defect was not in the code but in a *claim about* the code. This round's was the most consequential
kind: **a false claim that a fix was tested.** The cited test passed, the fix was genuinely correct,
and the suite was green — so nothing in the normal workflow surfaced that the test never touched the
code path at all. **Before writing "covered by test X" in an Accept note, verify X actually
exercises the changed path — check the fixture's real contents (`ffprobe`, the manifest), not just
the test's name and general shape.** The strongest form of this check, now used for `MEDIA-171` and
worth repeating for any load-bearing coverage claim: **temporarily break the fix and confirm the
test actually fails** — a test that passes against deliberately-broken code proves it was never
covering that code.

## 2. Correction to Phase 0's own build-verification record

Phase 0's commit `eb3c48a3` states the full `CnaTests` run showed "506 pre-existing failures, all one
root cause (no real GL context)" — **this was a methodology mistake in that verification pass, not a
real environment limitation.** `SDL_VIDEODRIVER=dummy` was forced for the *entire* suite; `dummy`
cannot create a GL context at all. The project's own established precedent (`plans/plan_cnb.md` `CNB-38`)
only uses `SDL_VIDEODRIVER=dummy` for the specific graphics-free test subset and runs the rest against
a real display. This sandbox actually has a real X display (`DISPLAY=:0`, Mesa softwareish/llvmpipe-or-
real GL via `glxinfo`, confirmed "direct rendering: Yes") — re-running the two sampled "failed" tests
without forcing `dummy` passed both immediately (`EasyGLGraphicsBackend initialized with OpenGL OpenGL
ES 3.2 Mesa 25.0.7-2`). **The commit's code changes are unaffected and correct** (nothing in Phase 0
touches Graphics/window code) — only that one build-log narrative claim in the commit message is wrong.
Not amending the commit for it (low value, already-established "new commits, not amends" default); this
note is the correction of record. A proper full-suite re-verification (real display, no forced driver)
is queued as part of Phase 1 closure instead of redone in isolation.

`CNA_GRAPHICS_BACKEND=HEADLESS` (proven real per `plans/plan_headless.md`, `SDL_INIT_VIDEO` never called) and
`SOFTWARE` remain available as a stronger-guarantee fallback if a future session's sandbox has no real
display at all — not needed here since one exists.

## 3. Environment / build notes — read this before touching anything

**Build and test:**
- `cmake --preset tests` (binaryDir `cmake-build-tests`, `EASYGL` backend, `CNA_BUILD_TESTS=ON`).
  Run `./cmake-build-tests/CnaTests`.
- Submodules: `git submodule update --init` — **non-recursive on purpose.** SDL_image's and
  SDL_mixer's own nested codec submodules are disabled by this project's CMake args, so `--recursive`
  only adds a much slower, useless fetch.
- **Grep the FULL test log for `FAILED`** (`grep -c FAILED log`), never a truncated tail. A run can
  abort mid-suite and still look fine at the end.

**Traps that have actually cost time in this repo — all hit during Phase 16:**
- **`easy-gl` and `meta-gl` are SIBLING repositories**, not submodules (`../easy-gl`, `../meta-gl`).
  If someone is mid-edit in them, the `EASYGL` build fails with errors that look like they are yours
  but are not. Check `git status` in those directories before debugging. A scratch
  `-DCNA_GRAPHICS_BACKEND=SDL_RENDERER` build sidesteps them entirely — but note that backend has
  ~20 pre-existing Graphics/Content failures of its own, so verify any failure against a clean
  baseline before blaming your change.
- **A stale `cmake-build-tests` silently changes the test count.** Ours predated the Draco
  integration, so its cache had `CNA_DRACO_AVAILABLE` off and four Draco-gated tests never
  registered. Re-run `cmake` on the build dir if a count looks wrong (`MEDIA-229`..`232`).
- **`ENetBackendTest.HostFreesOwnedRemoteGamerOnDispose` intermittently aborts the whole suite** with
  `double free or corruption`. Pre-existing, unrelated to Media, verified by `git stash` — re-run
  before investigating.
- **The suite runs a dummy SDL audio driver.** `GetMixer()` therefore always succeeds and caches its
  device, which is why the audio-failure branches in `MediaPlayer` are untestable here.
- **`develop` is checked out in a different worktree** (`../cna`), so it cannot be checked out here.
  To merge: `git worktree add --detach <sibling-path> origin/develop`, merge, build, run the FULL
  suite, push, then `git worktree remove`. Place it **beside the other repos**, not in `/tmp`, or
  `../easy-gl` will not resolve. Re-verify if `origin/develop` moves during your build — it did, and
  a stale verification would have missed a real `Texture3D`/`TextureCube` API change.

**Tooling available:** `ffmpeg`/`ffprobe` 7.1.5 (full codec set), ImageMagick, Python 3 + PIL. No
`mutagen`/`id3v2`/`eyeD3` — fixture tags are verified with `ffprobe` or raw byte inspection. ID3v2
`POPM` cannot be written by ffmpeg at all; that fixture is hand-built byte-by-byte in Python
(`MEDIA-182`).

**CPU thermal pacing (project owner instruction, standing):** check `sensors` (`Tctl`) roughly every
10 minutes during heavy work. At **≥85°C**, finish the in-flight step but do not start the next
build/test run until a re-check reads **≤75°C**. Always finish work already started regardless of
temperature. The owner has granted one-off "start now" overrides before; treat those as unblocking
that moment only, not as repealing the rule.

**Project conventions confirmed during this work:** out-of-range indexers throw
`System::ArgumentOutOfRangeException` (majority precedent; `Input::Touch::TouchCollection` is a known
outlier using `std::out_of_range`, out of scope here). `EXPECT_THROW((void)expr, T)` is the idiom for
a `[[nodiscard]]` `operator[]`.

## 4. Where to pick up

**Do not read any "N/N complete" line in this repo as settled** — an earlier version of this very
section claimed "plans/plan_media.md is complete, all 126 tasks across all 7 phases" while nine further
review rounds and 100+ tasks were still ahead of it. That sentence is exactly the failure mode this
plan documents.

**Actual state:** `plans/plan_media.md` has 231 tasks (IDs run to `MEDIA-232`; `MEDIA-157` was never
assigned). **224 done, 7 open.** All Media work is merged into `develop` (`cb053b71`).

**If you are picking this up cold, in priority order:**

1. **Group D (`MEDIA-192`..`198`) — deferred by the owner, do not start without checking.** Real
   FFmpeg for `Video`/`VideoPlayer`/`VideoDecoder` on Windows, Android, Emscripten. The owner
   explicitly **rejected** `NotSupportedException` stubs. **Not closeable from a Linux-only
   sandbox** — and `MEDIA-198` requires a matrix recording what was actually built and run versus
   merely written. If a platform proves infeasible (Emscripten is the likeliest), the owner's
   instruction is to **escalate, not silently stub**.
2. **A threaded/TSAN visualization test.** The single most valuable *closeable* gap. The data race
   in `VisualizationCapture` is fixed by construction (`std::atomic<float>` + install/reset
   ordering, `MEDIA-216`/`226`) but **nothing would catch a regression of it**. A `Push()`/`Read()`
   test on two threads under ThreadSanitizer would.
3. **`§5` follow-ups** — genuinely optional enhancements, none blocking.

**Working conventions for this plan, learned the hard way:**
- A re-audit **appends a new phase**; it never rewrites earlier phases (`MEDIA-126`). Corrections go
  **in place with the error stated** — several tasks retract their own earlier conclusions
  (`MEDIA-229`→`MEDIA-232`).
- **Mutation-verify every new test**: break the implementation, confirm the test fails. This caught a
  test that passed against deliberately broken code (`MEDIA-219`) and an Accept note citing a test
  whose fixture had no audio at all (`MEDIA-171`).
- **Verify the method, not just the result.** A task written specifically to be rigorous about a
  count still got it wrong because the counting method (a proximity `grep`) was never checked
  (`MEDIA-230`).

## 5. Open items / blocked

Nothing is currently blocking. The following are real, deliberately-deferred follow-ups — not bugs,
not required for "not just stubs" — surfaced during this plan's own design/audit work:

- **NOXNA `MediaLibrary::Refresh()`.** The library scan is a synchronous, point-in-time snapshot
  taken once at construction (`plans/plan_media.md` D6) — there is no live filesystem-watching, and no way
  to re-scan an already-constructed `MediaLibrary` short of constructing a new one. A `Refresh()`
  method would be a reasonable NOXNA addition if a game needs to observe library changes without a
  full reconstruction.
- **A real Playlist writer.** `PlaylistParser`/`PlaylistCollection` are read-only (parses existing
  `.m3u`/`.m3u8` files found under the Music root) — there is no API to create or modify a playlist
  from within CNA. XNA's own `Playlist` API is also read-only, so this isn't a fidelity gap, just a
  capability a future NOXNA extension could add.
- **A taglib upgrade path**, if `AudioTagParser`'s internal from-scratch Vorbis/ID3v2 parser (D2)
  ever proves insufficient for some real-world tag variant it doesn't handle (e.g. ID3v1, APEv2,
  FLAC-native `VORBIS_COMMENT` outside an Ogg container, MP4/`M4A` `ilst` atoms). Not needed today —
  the fixture corpus's real-world tag variety (ID3v2.3, ID3v2.4, Vorbis comments, untagged
  fallback) is fully covered — but worth tracking if a future session's real-world test collection
  surfaces a format this parser can't read.
- **Embedded `APIC`-frame album art**, as a `GetAlbumArt`/`GetThumbnail` enhancement beyond
  `MEDIA-65`'s current filename-only lookup (`cover.jpg`/`folder.jpg` in the album's own folder).
  Real-world ID3v2 tags frequently embed cover art directly in the `APIC` frame instead of (or in
  addition to) a sibling image file — `AudioTagParser` does not currently extract it. Flagged as
  `plans/plan_media.md` R2, default recommendation "start with the 2 filename conventions," this is the
  natural next increment if that ever proves insufficient.
- **`Input::Touch::TouchCollection`'s exception-type inconsistency**, found during this plan's own
  audit (`plans/plan_media.md` §2 item 7 / `CHECKLIST.md`'s new project-wide deviation row) but explicitly
  out of scope here: `TouchCollection.cpp`'s indexer throws `std::out_of_range` instead of the
  project's majority `System::ArgumentOutOfRangeException` (used by `BoundingBox`/`VertexBuffer`/
  `NetworkSessionProperties`, and now `MediaQueue`/`SongCollection` too). A future `Input`-namespace
  task should decide whether to bring `TouchCollection` in line with the majority or formally adopt
  it as a second accepted pattern.
- **`MEDIA-94`'s allocation-failure fault injection** (see Phase 6's own note above): closing this
  for real would need process-level fault-injection infrastructure (e.g. an `LD_PRELOAD` malloc
  interceptor) that doesn't exist in this repo today. Only worth building if this specific hardening
  ever needs direct proof beyond code review — the corrupt/truncated-fixture and I/O-error-vs-EOF
  halves of the same task are already covered.
- ~~A `VideoPlayer`-level `audio_tail.mkv` EOS-drain test~~ — **closed in Phase 9** (`MEDIA-145`):
  `VideoPlayerTests.cpp`'s `NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration` is
  exactly this dedicated `VideoPlayer::Play()`+loop-until-`Stopped()` test against
  `audio_tail.mkv`. This bullet was left stale after Phase 9 landed (flagged by a third external
  code review of Phase 9's own fix commit, `plans/plan_media.md` MEDIA-150) — corrected here rather than
  removed outright, so a future reader can see the open item really was closed, not silently
  dropped.
- **`MediaPlayer::Update()`'s no-`SOUND_ENABLED` fallback is untestable in this project's current
  build matrix** (Phase 8, `MEDIA-32`/`MEDIA-135`): `SOUND_ENABLED` is unconditionally defined for
  every build configuration `cmake/CnaLibrary.cmake` produces, so the `#else` branch that actually
  wires `DetectSongEndedByElapsedTime()` into real queue auto-advance is dead code today, in every
  binary this project builds, not specific to Media. A future session adding a genuine no-audio
  CMake build variant (with CI coverage) would let this — and the identically-shaped situation
  across the rest of the Audio/Media stack's own `#ifdef SOUND_ENABLED` pairs — finally be tested
  for real.
- **`Video`/`VideoPlayer` fail with a link error, not a runtime `NotSupportedException`, on
  Windows/Android/Emscripten** (Phase 8, `MEDIA-136`): `cmake/CnaLibrary.cmake`'s
  `CNA_FFMPEG_AVAILABLE` gate excludes `Video.cpp`/`VideoPlayer.cpp`/`VideoDecoder.cpp` from the
  build on those platforms while the public headers stay available. A real fix would need either
  runtime stub `.cpp` implementations that throw `NotSupportedException` for those platforms, or a
  compile-time guard surfaced earlier/more clearly than a linker error. This sandbox has no way to
  build for or verify any of those three platforms, so this is documented rather than attempted
  blind.
