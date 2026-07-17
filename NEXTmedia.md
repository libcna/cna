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

**Now starting Phase 4** (`MEDIA-61`..`MEDIA-69`, wiring the real backend onto the public XNA API)
-- `MediaSource`/`MediaLibrary` first (they gate everything else), then the 6 item+collection
pairs (`Genre`, `Artist`, `Album`, `Picture`, `PictureAlbum`, `Playlist`), then `MEDIA-69`'s
cross-class object-graph integration audit as the capstone.

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

Work through `plan_media.md` Phase 2 (`MEDIA-32`..`MEDIA-45`): real bug fixes in the already-working
playback cluster — the `MediaPlayer` no-`SOUND_ENABLED` gap, shuffle/duplicate-on-play regressions,
`VideoDecoder` pixel-format/bit-depth gaps, FFmpeg error-handling hardening, the `VideoPlayer`
disposed-guard gap, and `Video`'s missing `FileNotFoundException`. Then Phase 3 (`MEDIA-46`..`MEDIA-60`,
the real from-scratch library backend) — the largest, most novel remaining chunk of work.

## 5. Open items / blocked

None yet.
