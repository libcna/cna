# NEXTmedia.md — CNA Media Namespace Handoff (`feature/media` branch)

> Scoped to `Microsoft::Xna::Framework::Media` + `CNA::Internal::Media::*` only, per the same
> per-domain convention as `NEXTaudio.md`/`NEXTdevices.md`/`NEXTinput.md`/`NEXTnet.md`. The repo-root
> `NEXT.md` is explicitly reserved for the `feature/dx9` branch (its own banner note, 2026-07-14) —
> **do not edit it from this branch.** Full task-by-task detail lives in `plan_media.md`
> (`MEDIA-1`–`MEDIA-126`); this file is a short current-state index.

## 1. Status (2026-07-17)

**Phase 0 complete** (`MEDIA-1`..`MEDIA-8`, commits `eb3c48a3`, `3d6a7508` on `feature/media`). Test
infra (`tests/Microsoft/Xna/Framework/Media/` + `Video/`) and a full, verified fixture corpus
(`tests/assets/media/{music,pictures,video}/`, each with a `manifest.json`) are in place. See
`plan_media.md` §5 Phase 0 for exact task detail.

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
repo before this task, and `plan_xnb.md` never planned one). Field layout matches FNA's real binary
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

**Now starting Phase 7** (`MEDIA-121`..`MEDIA-126`, documentation and closure): add every new
deviation to `CHECKLIST.md`'s table, update `AUDIT.md`'s Media table, finalize this file, final
build & report, one more full-suite regression run, and the future-addendum convention note.

## 2. Correction to Phase 0's own build-verification record

Phase 0's commit `eb3c48a3` states the full `CnaTests` run showed "506 pre-existing failures, all one
root cause (no real GL context)" — **this was a methodology mistake in that verification pass, not a
real environment limitation.** `SDL_VIDEODRIVER=dummy` was forced for the *entire* suite; `dummy`
cannot create a GL context at all. The project's own established precedent (`plan_cnb.md` `CNB-38`)
only uses `SDL_VIDEODRIVER=dummy` for the specific graphics-free test subset and runs the rest against
a real display. This sandbox actually has a real X display (`DISPLAY=:0`, Mesa softwareish/llvmpipe-or-
real GL via `glxinfo`, confirmed "direct rendering: Yes") — re-running the two sampled "failed" tests
without forcing `dummy` passed both immediately (`EasyGLGraphicsBackend initialized with OpenGL OpenGL
ES 3.2 Mesa 25.0.7-2`). **The commit's code changes are unaffected and correct** (nothing in Phase 0
touches Graphics/window code) — only that one build-log narrative claim in the commit message is wrong.
Not amending the commit for it (low value, already-established "new commits, not amends" default); this
note is the correction of record. A proper full-suite re-verification (real display, no forced driver)
is queued as part of Phase 1 closure instead of redone in isolation.

`CNA_GRAPHICS_BACKEND=HEADLESS` (proven real per `plan_headless.md`, `SDL_INIT_VIDEO` never called) and
`SOFTWARE` remain available as a stronger-guarantee fallback if a future session's sandbox has no real
display at all — not needed here since one exists.

## 3. Environment/build notes for this session

- Submodules `third_party/SDL`, `third_party/SDL_image`, `third_party/SDL_mixer`, `vendor/googletest`
  needed a one-time non-recursive `git submodule update --init <paths>` (were uninitialized at session
  start) — done.
- Build/test preset: `cmake --preset tests` (binaryDir `cmake-build-tests`, `EASYGL` backend,
  `CNA_BUILD_TESTS=ON`) — this is the project's own designated tests preset, used instead of a
  hand-rolled `cmake-build-debug` invocation.
- `ffmpeg`/`ffprobe` (7.1.5, full codec set incl. `libvorbis`/`libmp3lame`/`ffv1`/`dav1d`),
  ImageMagick (`convert`/`magick`), and Python 3 + PIL are available in this environment and were used
  to author Phase 0's fixture corpus. No `mutagen`/`id3v2`/`eyeD3` installed — tag verification during
  fixture authoring used `ffprobe`/raw byte inspection instead; not needed for the actual C++
  `AudioTagParser` implementation work ahead.
- CPU thermal pacing policy in effect throughout this session (project owner instruction, 2026-07-16,
  threshold raised 2026-07-17): check `sensors` (`Tctl`/`CPU`) roughly every 10 min during heavy work
  (builds, big test runs); if a check reads above **85°C**, finish the in-flight step but hold the next
  build/heavy step until a re-check reads back at or below 70°C.
- Established project-wide precedent found during Phase 1 (§2.7 in `plan_media.md`): out-of-range
  indexer exceptions are a genuinely mixed precedent across the codebase (`BoundingBox`/`VertexBuffer`/
  `NetworkSessionProperties` throw `System::ArgumentOutOfRangeException`; `TouchCollection` deliberately
  throws `std::out_of_range` instead, undocumented in `CHECKLIST.md`). Media's `MediaQueue`/
  `SongCollection` now follow the majority (`ArgumentOutOfRangeException`). The `TouchCollection`
  outlier is Input-namespace, out of this plan's scope — flagged here as a candidate follow-up, not
  fixed.
- `EXPECT_THROW((void)expr, ExceptionType)` is the established idiom for a `[[nodiscard]]`-returning
  `operator[]` under `EXPECT_THROW` (see `GameComponentCollectionTests.cpp`,
  `SamplerStateCollectionTests.cpp`, `NetworkSessionPropertiesTests.cpp`) — used in the new
  `MediaQueueTests.cpp`/`SongCollectionTests.cpp`.

## 4. Immediate next steps

Phases 0-5 are complete (see §1). Work through `plan_media.md` Phase 6 (`MEDIA-76`..`MEDIA-120`,
consolidated test-completeness audit against `CLAUDE.md`'s per-overload mandate) next, then Phase 7
(`MEDIA-121`..`MEDIA-126`: `CHECKLIST.md` deviations table, `AUDIT.md`'s Media table, final
`NEXTmedia.md` update, final build + full-suite regression, future-addendum convention note).

## 5. Open items / blocked

None yet.
