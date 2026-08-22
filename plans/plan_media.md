# plan_media.md — Completing and implementing the FNA Media → CNA port (C++ / XNA 4.0)

> ## STATUS (2026-08-22): 229 of 233 tasks done
>
> (IDs run to `MEDIA-234` but `MEDIA-157` was never assigned — skipped when Phase 13 was
> numbered — so there are 233 real tasks, not 234. Verified by diffing the checkbox IDs
> against the full range rather than assuming the highest ID equals the count.)
>
> **Open: 4 tasks, Group D (`MEDIA-192`..`195`)** — real target-native FFmpeg decoding on
> Windows/MSVC, MinGW, Android and Emscripten. `MEDIA-233` records the owner's 2026-08-22 decision
> reversing the earlier rejection of a runtime fallback: all XNA video types and readers are now
> link-complete without FFmpeg and fail deterministically at decode/play time. That closes the old
> link-error/configuration problem without falsely claiming real decoding on those platforms.
>
> **Baseline on the merged tree: 5471 tests, 5467 passed, 0 failed, 4 pre-existing hardware skips.**
> Per-phase counts recorded in Phases 8-16 below are *branch-only and pre-merge* (`MEDIA-231`).
>
> **Documented gaps that are NOT tracked as tasks** (do not let a summary upgrade these to "done"):
> visualization has no TSAN/threaded/end-to-end test — its data race is fixed by construction, not
> by a test that would catch a regression; the `GetMixer()`-failure and failed-uninstall branches are
> unreachable from the suite; `.m4a`/`.aac`/WMA are deliberately not indexed because SDL3_mixer has
> no AAC decoder.
>
> **Phase-history convention:** Phases 8-16 were appended by successive external review rounds and
> are kept as written at the time. Corrections are made **in place with the error stated**, never by
> quietly rewriting a claim — several tasks below explicitly retract their own earlier conclusions
> (`MEDIA-229`→`MEDIA-232` is the clearest example).


> **Scope:** exclusively `Microsoft::Xna::Framework::Media` + new internal `CNA::Internal::Media::*`
> backend code + the one cross-cutting `CNA::Internal::Xnb::VideoContentTypeReader` addition (Phase 5).
> **The Audio/Graphics/Input/Devices/Net namespaces are NOT part of this plan.**
>
> **Provenance:** this revision synthesizes three independent research passes — (1) a complete,
> method-level read of all 26 FNA `Media` source files (including `Video/`), (2) a full audit of every
> current CNA `Microsoft::Xna::Framework::Media`/`CNA::Internal::Media` file plus the audio/content
> infrastructure it depends on, and (3) a structural-conventions check against `plan_audio.md`,
> `CHECKLIST.md`, and `AUDIT.md` — plus two direct verifications (`FrameworkDispatcher::Update()`'s
> real call chain; existing `CNA::Internal::Graphics::ImageLoader` reuse for picture metadata). It
> supersedes a first-pass draft that reached 73 tasks; this version deliberately goes to finer
> granularity (per-file compliance tasks, per-behavior test tasks matching `CHECKLIST.md`'s
> per-overload testing mandate, and several real gaps the first pass missed) at the project owner's
> explicit request for maximum thoroughness — **126 tasks as first written** (now 233, after
> nine external review rounds appended Phases 8-16), not padded for a round number: every task
> below is an independently completable, individually justified unit of work. Matching `plan_audio.md`'s
> own real history (an initial plan, then supplementary `P9-*`/`Phase 10` addenda as later audits found
> more), any future re-audit findings should be appended as new phases here, not silently merged into
> the phases below (see MEDIA-126).
>
> **Goal:** Media splits into two very different halves, and this plan treats them differently:
>
> 1. **The "playback" half** — `Song`, `MediaPlayer`, `MediaQueue`, `SongCollection`,
>    `VisualizationData`, `Video`, `VideoPlayer`, `VideoSoundtrackType`, `MediaState` — has **real,
>    working logic in upstream FNA**. CNA already has a real, non-stub implementation of nearly all of
>    it (SDL3_mixer-backed `MediaPlayer`, FFmpeg-backed `VideoPlayer`/`VideoDecoder`). This plan's job
>    here is the same as Audio's: line-by-line fidelity audit, real bug fixes, complete tests.
> 2. **The "library" half** — `Album`, `AlbumCollection`, `Artist`, `ArtistCollection`, `Genre`,
>    `GenreCollection`, `Picture`, `PictureCollection`, `PictureAlbum`, `PictureAlbumCollection`,
>    `Playlist`, `PlaylistCollection`, `MediaLibrary`, `MediaSource` — is **~100%
>    `NotImplementedException` in upstream FNA itself** (confirmed by reading all 14 FNA source files in
>    full — there is no Zune/Xbox 360 media-library concept on desktop, so FNA never built one). CNA
>    today mirrors that FNA stub behavior exactly — which means "matching FNA" and "being just a stub"
>    are, for this half, the *same thing*. The project owner has explicitly asked for this to change:
>    **CNA should implement a real, working local media library here — a deliberate, documented
>    enhancement beyond FNA**, not a line-by-line port (there is no FNA logic to port). Section 4 fixes
>    the design decisions this requires; Phase 3 builds it, Phase 4 wires it onto the public API.
>
> No stubs without a documented reason. Every conscious deviation from FNA (and every place CNA goes
> *beyond* FNA) must be documented in `CHECKLIST.md`'s deviations table (Phase 7).

- **FNA reference (authoritative):** `/rv/data/library/github.com/FNA-XNA/FNA/src/Media/`
  (including the `Video/` subfolder: `Video.cs`, `VideoPlayer.cs`, `BaseYUVPlayer.cs`,
  `IVideoPlayerCodec.cs`, `VideoPlayerAV1.cs`, `VideoPlayerTheora.cs`), plus
  `src/Content/ContentReaders/SongReader.cs`/`VideoReader.cs` and `src/FrameworkDispatcher.cs` for the
  two integration points outside `Media/` itself.
- **Backend (playback half):** `MediaPlayer` runs on **SDL3_mixer** (`MIX_*` API), gated behind
  `#ifdef SOUND_ENABLED`. `VideoPlayer`/`CNA::Internal::Media::VideoDecoder` use the optional
  **`cna_video_ffmpeg`** module (`libavcodec`/`libavformat`/`libavutil`/`libswresample`) when
  `CNA_ENABLE_VIDEO=AUTO/ON` resolves enabled, with a **hand-written YUV→RGBA conversion** (no
  `libswscale`, per `AGENTS.md`). The stable media module supplies a deterministic no-decoder
  implementation otherwise. Both are a backend swap vs.
  FNA (FNA: SDL3_mixer→`FAudio` for Song; `dav1dfile`+`Theorafile` per-codec native decoders +
  shader-based YUV blit for Video) — see §2 for why this is an accepted deviation, not a defect.
- **Backend (library half, new):** no backend exists yet. Built from scratch in Phase 3 on top of
  SDL3's already-vendored `SDL_GetUserFolder()` (`SDL_FOLDER_MUSIC`/`SDL_FOLDER_PICTURES` in
  `SDL_filesystem.h`) and the **already-existing** `CNA::Internal::Graphics::ImageLoader`
  (`include/CNA/Internal/Graphics/ImageLoader.hpp` — `Load(path) -> ImageData{width,height,pixels}`,
  already used by `Texture2D`, backed by the SDL_image dependency this project already vendors via
  `cmake/ThirdPartySDL.cmake`). **No new third-party dependency required for either.**
- **Integration point, confirmed correct (not a gap):** `Game.cpp` calls `FrameworkDispatcher::Update()`
  every frame (2 call sites), which calls `Media::MediaPlayer::Update()`
  (`src/Microsoft/Xna/Framework/FrameworkDispatcher.cpp:53`) — the deferred `ActiveSongChanged`/
  `MediaStateChanged` events and queue auto-advance-on-song-end are correctly wired end-to-end today.
  Verified directly (not assumed) because a missing/broken link here would silently mean these events
  and auto-advance never fire — this was the single highest-risk hidden gap to rule out before writing
  Phase 6's event tests.
- **Status per `AUDIT.md`:** everything "✅", with "(stub behavior)" notes on `MediaLibrary`, `Album`,
  `Artist`, `Genre`, `Picture`, `PictureAlbum`, `Playlist` — this plan turns those `✅` rows into
  genuinely accurate ones instead of leaving "stub behavior" standing as accepted forever.
- **Zero tests exist today** for the entire `Microsoft::Xna::Framework::Media` namespace
  (`find tests -ipath "*media*"` returns nothing, aside from the `Song` XNB content-pipeline tests,
  which exercise `Song` only incidentally as the thing being loaded). Phase 0/6 fixes this.

---

## 1. File inventory and current status

| # | File | FNA logic | CNA current state | Cluster |
|---|------|-----------|--------------------|---------|
| 1 | Song | Real | Real, 1 exception-type bug (raw `std::runtime_error`, not `System::IO::FileNotFoundException`); `GetHashCode()` is content-based (handle) where FNA's is identity-based (`base.GetHashCode()`) — a beneficial deviation, see §2; 4 getters are FNA-faithful hardcoded constants (undocumented as such) | Playback |
| 2 | MediaPlayer | Real | Real (500 lines), 1 silent-gap when built without `SOUND_ENABLED` | Playback |
| 3 | MediaQueue | Real | Real, 1 exception-type question (`std::out_of_range` — see §2 re: mixed project precedent) | Playback |
| 4 | SongCollection | Real | Real, same exception-type question | Playback |
| 5 | VisualizationData | Real | Real, no issues found | Playback |
| 6 | MediaState (enum) | Real | **Already correct** (`Stopped=0,Playing=1,Paused=2`, matches FNA order/values, Doxygen present) | Playback |
| 7 | Video | Real | Real; 3 real constructors, `FromUriEXT`, track-select EXT methods, Duration-hack all present; **raw-file constructor does not throw `FileNotFoundException` for a missing file** (real fidelity gap — FNA's does) | Playback |
| 8 | VideoPlayer | Real | Real (254 lines); wrong SPDX header (`MIT` instead of `MS-PL`); **no `checkDisposed()`-equivalent guard on any public method** (real gap — FNA's guards every one) | Playback |
| 9 | VideoSoundtrackType (enum) | Real (data-only — never consumed by playback logic even in FNA) | **Already correct** (Music/Dialog/MusicAndDialog, matches FNA exactly) | Playback |
| 10 | `CNA::Internal::Media::VideoDecoder` (no direct FNA file — internal FFmpeg backend replacing `dav1dfile`+`Theorafile`+`BaseYUVPlayer`) | N/A | Real (424 lines); pixel-format coverage gap (422/444/10-/12-bit), unchecked `avcodec_alloc_context3`/`swr_alloc_set_opts2`/`avcodec_send_packet`/`swr_convert` return values, no I/O-error-vs-EOF distinction, wrong SPDX header | Playback (internal) |
| 11 | MediaSourceType (enum) | Real | **Already correct** (`LocalDevice=0,WindowsMediaConnect=4`, matches FNA exactly) | Library |
| 12 | MediaSource | 100% stub (incl. static `GetAvailableMediaSources()`) | Matches FNA (100% stub); constructor unreachable (no factory) | Library |
| 13 | MediaLibrary | 100% stub except the no-arg ctor (real, empty) | Matches FNA exactly | Library |
| 14 | Album | 100% stub | Matches FNA (100% stub) | Library |
| 15 | AlbumCollection | 100% stub | Matches FNA (100% stub) | Library |
| 16 | Artist | 100% stub | Matches FNA (100% stub) | Library |
| 17 | ArtistCollection | 100% stub | Matches FNA (100% stub) | Library |
| 18 | Genre | 100% stub | Matches FNA (100% stub) | Library |
| 19 | GenreCollection | 100% stub | Matches FNA (100% stub) | Library |
| 20 | Picture | 100% stub | Matches FNA (100% stub) | Library |
| 21 | PictureCollection | 100% stub | Matches FNA (100% stub) | Library |
| 22 | PictureAlbum | 100% stub | Matches FNA (100% stub) | Library |
| 23 | PictureAlbumCollection | 100% stub | Matches FNA (100% stub) | Library |
| 24 | Playlist | ~100% stub, but `operator==`/`operator!=` have real null-check branching over a still-throwing `Equals` | Matches FNA exactly, including that quirk | Library |
| 25 | PlaylistCollection | 100% stub | Matches FNA (100% stub) | Library |

All 25 files/enums already exist structurally on both sides — **there are no missing files**. The work
is (a) fidelity/robustness hardening for the 10 Playback-cluster rows, and (b) building real logic from
scratch for the 14 Library-cluster rows (13 classes + 1 enum, which is already fine).

---

## 2. Accepted deviations (already true today, or deliberately chosen here — documented, not silently "fixed")

1. **Audio playback backend**: SDL3_mixer instead of FAudio (same accepted-deviation family as
   `plan_audio.md` — `CHECKLIST.md` already covers the shared parts; Song-specific notes go in Phase 7).
2. **Video decode backend**: a single FFmpeg-based `VideoDecoder` replaces FNA's two separate native
   decoders (`dav1dfile` for AV1, `Theorafile` for Theora) plus FNA's shader-based
   (`BaseYUVPlayer`/`Effect`) YUV→RGBA blit; CNA does the YUV→RGBA conversion on the CPU in C++ instead
   (`CLAUDE.md`'s documented, intentional choice — "does NOT depend on libswscale headers"). The
   **public contract is preserved**: `GetTexture() -> Texture2D`, pull-based frame pacing driven by the
   caller (matches FNA's own `Stopwatch`-in-`GetTexture()` design, confirmed by reading
   `VideoPlayerAV1.cs`/`VideoPlayerTheora.cs` — not a CNA gap), the Duration hack, `IsLooped`, clamped
   `Volume`. No decode thread exists on either side — not a CNA shortcut, it is what FNA itself does.
3. **AV1 content keeps its audio track (MEDIA-37) — a deliberate improvement, not a port of FNA's
   limitation.** FNA's `VideoPlayerAV1` hardcodes `IsMuted`/`Volume` to do nothing at all (getters
   always return `false`/`0.0f`; the ctor even assigns `IsMuted = true` into a property whose setter is
   a no-op — a real, dead-code FNA quirk, since `Dav1dfile` is video-only with no audio-track concept).
   CNA's unified FFmpeg `VideoDecoder` has no such per-codec split, so AV1-container content with a real
   audio track plays it like any other container — this is *more capable than FNA*, not a bug to
   "correct" by artificially muting AV1 videos.
4. **`Song::GetHashCode()` is content-based (hash of `handle`), where FNA's is identity-based
   (`base.GetHashCode()`).** FNA's own choice here is arguably a latent bug (two `Song`s that are
   `Equals`-equal by `handle` can have different hash codes in FNA, violating the usual
   `Equals`/`GetHashCode` contract). CNA's existing content-based hash is kept as-is — a beneficial
   deviation — rather than "fixed" to replicate FNA's inconsistency. Document, do not revert
   (MEDIA-14).
5. **`VideoSoundtrackType` is metadata-only** on both sides — confirmed by reading every FNA Video
   file: nothing in `VideoPlayer`/`BaseYUVPlayer`/`VideoPlayerAV1`/`VideoPlayerTheora` ever branches on
   it to affect volume or ducking. CNA's identical metadata-only handling is **already faithful to
   FNA**, not a gap (MEDIA-31).
6. **`GetHashCode()` → `std::size_t`** via `std::hash`, same accepted-deviation class already in
   `CHECKLIST.md` from Audio/Graphics.
7. **Out-of-range indexer exception type is a genuinely mixed precedent project-wide, not a Media-only
   question.** `BoundingBox.cpp`, `VertexBuffer.cpp`, and `NetworkSessionProperties.cpp` all throw
   `System::ArgumentOutOfRangeException` directly; `Input::Touch::TouchCollection.cpp` (the closest
   structural analog to `MediaQueue`/`SongCollection` — a read-only indexed wrapper over an internal
   list) deliberately throws `std::out_of_range` instead, with an inline comment ("FNA throws
   `ArgumentOutOfRangeException` for a bad index; we map that to `std::out_of_range`") that was never
   promoted to a `CHECKLIST.md` deviations-table row. This plan follows the **majority** precedent
   (`System::ArgumentOutOfRangeException`, MEDIA-11/12) for `MediaQueue`/`SongCollection` and flags the
   `TouchCollection` outlier as a separate, out-of-scope observation for `NEXTmedia.md` rather than
   silently picking a side without saying so.

> Each of these must have (or already has, for #6) a row in `CHECKLIST.md`'s deviations table — see
> Phase 7. Items 1-3, 5 need a **new** Media-specific row each; item 4 needs one row; item 7 needs a row
> documenting the project-wide inconsistency (not just Media's resolution of it); the Library-half
> real-implementation design decisions in §4 add several more.

---

## 3. Cross-cutting defects (one task, many files)

- **X1 — Wrong SPDX license header.** `src/Microsoft/Xna/Framework/Media/Video/VideoPlayer.cpp`,
  `include/CNA/Internal/Media/VideoDecoder.hpp`, `src/CNA/Internal/Media/VideoDecoder.cpp` all start
  with `// SPDX-License-Identifier: MIT` + an extraneous `// Copyright (c) Robert Vokac and
  contributors` line instead of the project-standard `// SPDX-License-Identifier: MS-PL` — and in
  `VideoPlayer.cpp`'s case, its own paired `.hpp` correctly says `MS-PL`, so the `.cpp` is
  self-inconsistent with its own header, not just with project convention.
- **X2 — Raw `std::*` exceptions instead of `System::*`.** `Song.cpp:19`
  (`std::runtime_error(handle_)` on a missing file — `sharp-runtime` already ships
  `System::IO::FileNotFoundException` and this exact scenario is already handled that way elsewhere,
  e.g. `AudioEngine`/`SoundBank`/`WaveBank`); `MediaQueue.cpp:42` and `SongCollection.cpp:18`
  (`std::out_of_range` on an out-of-bounds indexer — see §2 item 7 for the exact precedent chosen).
- **X3 — Zero tests.** `tests/Microsoft/Xna/Framework/Media/` doesn't exist. Tests are picked up into
  `CnaTests` via `GLOB_RECURSE tests/*.cpp`, so (per Audio's own precedent) creating the directory with
  a first file is enough — no `CMakeLists.txt` change needed.
- **X4 — Unreachable stub-class constructors.** `MediaSource`, `Album`, `Artist`, `Genre`, `Picture`,
  `PictureAlbum`, `Playlist` and their 6 collection types all have `private`/unreachable constructors
  with no factory or friend anywhere — today this exactly matches FNA (whose own equivalents are
  equally unreachable, since `MediaLibrary` always throws before ever reaching them). Phase 4 resolves
  this for real by giving these constructors a genuine caller (the new internal backend), which is why
  it's listed here rather than under "bugs" — it's the central fact Phase 4 exists to change.
- **X5 — No disposed-state enforcement in `VideoPlayer`.** FNA's real `VideoPlayer` calls
  `checkDisposed()` (throwing `ObjectDisposedException("VideoPlayer")`, using a hardcoded literal
  type-name string rather than reflection — an FNA quirk worth reproducing verbatim for message
  fidelity) at the top of **every** public method. CNA's `getIsDisposedProperty()` exists but nothing
  reads it before acting — calling any method after `Dispose()` silently touches freed/stale state
  instead of throwing (MEDIA-43).

---

## 4. Design decisions for the real local media library (Phase 3/4 gate)

These decisions have **no FNA precedent to copy** (§0 — FNA never built this), so they're made here,
upfront, as assumed defaults so Phases 3-4 can be written concretely. Anyone picking this plan up is
free to override a row — the tasks that depend on it are cited so the blast radius is clear.

| ID | Decision | Default (assumed by this plan) |
|----|----------|-------------------|
| D1 | Where does the library scan? | Real per-OS folders via SDL3's already-vendored `SDL_GetUserFolder(SDL_FOLDER_MUSIC)` / `SDL_GetUserFolder(SDL_FOLDER_PICTURES)` — genuinely cross-platform, zero new dependencies, consistent with `StorageDevice`'s existing precedent of using a real SDL-provided OS path (`SDL_GetPrefPath`) rather than a synthetic one. A NOXNA `SetMusicRootEXT`/`SetPictureRootEXT` override exists for tests/config (MEDIA-46). |
| D2 | How are song tags read? | A minimal **internal**, from-scratch parser (`CNA::Internal::Media::AudioTagParser`): Ogg Vorbis-comment blocks for `.ogg`, ID3v2 text frames for `.mp3` (with the ID3v2 text-encoding byte — Latin-1/UTF-16 w/BOM/UTF-16BE/UTF-8 — decoded properly, not assumed ASCII); folder/filename heuristics as a fallback for `.wav`/untagged/unsupported files. No new third-party tag library (e.g. taglib) — same "write a minimal from-scratch parser" precedent already established by `XactParser` in Audio. |
| D3 | How are Album/Artist/Genre built? | Derived by grouping every scanned `Song` once (`MediaLibraryIndex`), not stored redundantly — `Album.Songs`/`Artist.Albums`/etc. are views over that one shared index. |
| D4 | How is the Picture tree built, and how are dimensions read? | One `PictureAlbum` node per real subdirectory of the Pictures root (mirroring the actual filesystem tree, with real `Parent` links), `Picture` leaves per image file (`.png`/`.jpg`/`.jpeg`/`.bmp`); dimensions/pixel validation via the **already-existing** `CNA::Internal::Graphics::ImageLoader::Load()` (reused, not reimplemented — it already decodes these exact formats for `Texture2D`), `Date` from the file's last-write-time. |
| D5 | What on-disk format backs `Playlist`? | **M3U/M3U8** — a real, ubiquitous, trivially-parseable format (path-per-line + optional `#EXTINF`); `.m3u8` is UTF-8, `.m3u` is treated as the local/legacy encoding, otherwise identical (MEDIA-58); `PlaylistCollection` scans the Music root for `*.m3u`/`*.m3u8` files. XNA itself defines no on-disk playlist format, so this is a free choice constrained only by "pick something real users' tools actually produce." |
| D6 | Live-updating or point-in-time snapshot? | Point-in-time: the whole tree is scanned once, synchronously, at `MediaLibrary` construction (matches the real Zune/Xbox 360 "library" concept, which is also a snapshot, not a live filesystem watcher). A NOXNA `Refresh()` is a reasonable, optional follow-up, not required for this plan. |
| D7 | Where does `SavePicture` write? | A real `Saved Pictures` subfolder under the Pictures root, exposed through the real `SavedPictures`/`RootPictureAlbum` surface. |
| D8 | Thread-safety of the scan? | Synchronous, single-threaded, run to completion inside the constructor — no async/threading requirement surfaced by the XNA API shape itself. |
| D9 | Code reuse across the 6 near-identical collection types? | A shared NOXNA internal template, `CNA::Internal::Media::MediaCollectionBase<T>`, backs all 6 public XNA collection types' storage/indexer/enumerator/dispose logic (all 6 are byte-for-byte structurally identical in FNA except `T`). The 6 public class **names** and exception contracts stay fully distinct and XNA-faithful; only the private implementation is shared. |
| D10 | Are `Artist`/`Genre` names deduplicated case-insensitively? | Yes — grouping key is a case-folded (and whitespace-trimmed) form of the tag value, but the **displayed** `Name` keeps the first-seen original casing, so `"Artist A"` and `"artist a"` across two files don't silently produce two distinct `Artist` objects (a real, likely-common tagging inconsistency in actual music collections). Documented as a new decision, not an FNA behavior (MEDIA-54). |
| D11 | ID3v2 text-frame character encoding? | Byte 0 of every ID3v2 text-frame payload selects the encoding (`0x00`=ISO-8859-1/Latin-1, `0x01`=UTF-16 with BOM, `0x02`=UTF-16BE without BOM [2.4 only], `0x03`=UTF-8 [2.4 only]) — all four are decoded to a `System::String`/UTF-8-internal representation properly rather than assuming ASCII, since real-world tags routinely use non-Latin scripts (MEDIA-50). |

---

## 5. Tasks (by phase)

> Convention: each task has an **ID** (`MEDIA-n`), affected files, an **FNA ref** where one exists, a
> description, and **acceptance criteria incl. tests**. Check off `[ ]` → `[x]` as work lands.

### Phase 0 — Test infrastructure and fixture assets

- [x] **MEDIA-1 — Set up Media test infrastructure.** Create `tests/Microsoft/Xna/Framework/Media/`
  (+ `Video/` subfolder) with a first skeleton test file.
  *Accept:* `cmake --build cmake-build-debug --target CnaTests` picks it up via the existing GLOB with
  no `CMakeLists.txt` change, and it runs.

- [x] **MEDIA-2 — Test-access scaffolding for the new library backend.** Add a
  `MediaLibraryTestAccess.hpp` shared test header (same pattern as Audio's
  `SoundEffectInstanceTestAccess.hpp`/`CueTestAccess.hpp`) exposing whatever private scan-result state
  Phase 4's tests will need to inspect (e.g. the resolved music/picture roots, raw scan counts) without
  widening the public XNA API surface.
  *Accept:* compiles; not yet used until Phase 3/4/6 land (placeholder is fine at this point).

- [x] **MEDIA-3 — Fixture: Music tree.** Author/vendor a small, real, checked-in
  `tests/assets/media/music/` tree with ≥2 artists, ≥2 albums per artist, ≥2 genres, using real
  `.ogg` (Vorbis-comment tagged), `.mp3` (ID3v2.3- and ID3v2.4-tagged), and `.wav` (untagged, to
  exercise the filename/folder-heuristic fallback) files with genuinely embedded metadata, not just
  filenames. Confirm licensing/provenance of any non-self-authored fixture audio and record it in
  `NOTICE.md`/`THIRD_PARTY_NOTICES.md` per this project's existing convention.
  *Accept:* fixture tree checked in; every file's expected tag values documented alongside it (e.g. a
  small fixture manifest) for tests to assert against.

- [x] **MEDIA-4 — Fixture: Pictures tree.** Author/vendor `tests/assets/media/pictures/` with ≥2
  nested subfolders (to exercise `PictureAlbum` parent/child tree depth), real `.png`/`.jpg` images of
  known dimensions, and at least one `cover.jpg`/`folder.jpg` per an album-art-bearing music
  subdirectory (for MEDIA-65's `HasArt`/`GetAlbumArt` coverage).
  *Accept:* fixture tree checked in with documented expected dimensions/paths.

- [x] **MEDIA-5 — Fixture: Playlists.** Author `.m3u` and `.m3u8` fixture files referencing MEDIA-3's
  fixture songs, including one entry pointing at a nonexistent file (for MEDIA-24's skip-not-fatal
  behavior) and at least one non-ASCII filename/title to exercise MEDIA-58's UTF-8 handling.
  *Accept:* fixtures checked in with documented expected resolved `Song` sequences.

- [x] **MEDIA-6 — Fixture: Video corpus, chroma subsampling.** Source or encode short (a few seconds)
  video clips covering 4:2:0 (baseline, likely already covered by any existing video test asset — if
  one already exists it's fine to reuse instead of re-encoding), 4:2:2, and 4:4:4 8-bit chroma
  subsampling, small enough to keep the repo lean.
  *Accept:* 3 fixture clips checked in (or an existing 4:2:0 one confirmed reusable), each with known
  correct-color reference frames for MEDIA-91's assertions.

- [x] **MEDIA-7 — Fixture: Video corpus, bit depth, EOS-audio-tail, and corrupt file.** Add: a 10-bit
  and a 12-bit clip (for MEDIA-36/92); a clip whose audio track runs measurably longer than its video
  track (for MEDIA-41's audio-drain-at-EOS test); a deliberately truncated/corrupted file (for
  MEDIA-40/94's error-handling tests); a clip with a mismatched declared-vs-actual dimension/fps (for
  MEDIA-42's sanity-check test, if not producible synthetically at test time instead).
  *Accept:* fixtures checked in (or synthesized at test setup time where more practical, e.g. truncating
  a valid fixture's bytes at test runtime rather than storing a second binary) with documented expected
  behavior per fixture.

- [x] **MEDIA-8 — Fixture provenance and license audit.** Confirm every binary fixture added by
  MEDIA-3/4/6/7 is either self-authored/synthetic (preferred, no note needed) or has clear license terms
  compatible with this repo, recorded in `NOTICE.md`/`THIRD_PARTY_NOTICES.md` alongside the existing
  `Song`/XNB fixture entries.
  *Accept:* every non-synthetic fixture has a corresponding notice entry; nothing added without one.

### Phase 1 — Compliance sweep (low effort, high value)

- [x] **MEDIA-9 — Fix wrong SPDX/license headers (X1).** `Video/VideoPlayer.cpp`,
  `CNA/Internal/Media/VideoDecoder.hpp`, `CNA/Internal/Media/VideoDecoder.cpp`: replace the `MIT` +
  extraneous copyright-line header with `// SPDX-License-Identifier: MS-PL`, matching every other file
  in the namespace (including `VideoPlayer.cpp`'s own paired `.hpp`).
  *Accept:* all 3 files start with the correct SPDX line; build unchanged.

- [x] **MEDIA-10 — `Song.cpp:19`: map to `System::IO::FileNotFoundException` (X2).** Matches the
  established `AudioEngine`/`SoundBank`/`WaveBank` precedent for exactly this "referenced file doesn't
  exist" case.
  *Accept:* a test asserts the exact `System::IO::FileNotFoundException` type; valid input unaffected.

- [x] **MEDIA-11 — `MediaQueue.cpp:42`: map to `System::ArgumentOutOfRangeException` (X2/§2.7).**
  Following the majority project precedent (`BoundingBox`/`VertexBuffer`/`NetworkSessionProperties`),
  not `TouchCollection`'s outlier `std::out_of_range` mapping (flagged separately, out of scope, in
  `NEXTmedia.md`).
  *Accept:* a test asserts the exact type for an out-of-bounds index; valid indices unaffected.

- [x] **MEDIA-12 — `SongCollection.cpp:18`: map to `System::ArgumentOutOfRangeException` (X2/§2.7).**
  Same rationale and acceptance shape as MEDIA-11.

- [x] **MEDIA-13 — Document `Song`'s FNA-faithful hardcoded constants.** `getIsProtectedProperty()`
  (`false`), `getIsRatedProperty()` (`false`), `getRatingProperty()` (`0`), `getTrackNumberProperty()`
  (`0`) are **not** unfinished stubs — FNA's own `Song.cs` hardcodes these exact same 4 values on
  desktop, permanently. Add a one-line Doxygen note to each so a future audit doesn't "fix" correct
  behavior.
  *FNA:* Song.cs:33-69. *Accept:* Doxygen updated; no behavior change; a test asserts the constant value
  explicitly (documenting intent, not just incidentally passing).

- [x] **MEDIA-14 — Decide and document `Song::GetHashCode()`'s content-based semantics (§2.4).** Keep
  CNA's current handle-based hash (satisfies the `Equals`/`GetHashCode` contract, unlike FNA's own
  identity-based `base.GetHashCode()`); add a Doxygen/comment note explaining this is a deliberate,
  beneficial deviation, not an unported detail.
  *Accept:* doc note added; no code change; a test asserts two `Song`s with equal `handle` produce equal
  hash codes (locking in the improved behavior).

- [x] **MEDIA-15 — `Artist`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-16 — `ArtistCollection`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-17 — `GenreCollection`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-18 — `Picture`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-19 — `PictureCollection`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-20 — `PictureAlbum`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-21 — `PictureAlbumCollection`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-22 — `Playlist`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass** (incl. a doc note on
  the `operator==`/throwing-`Equals` quirk described in §1 row 24, preserved per FNA fidelity until
  MEDIA-68 makes `Equals` real).
- [x] **MEDIA-23 — `PlaylistCollection`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-24 — `MediaSource`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-25 — `MediaLibrary`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-26 — `Video`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass** (incl. the `EXT`-suffixed
  members — `FromUriEXT`, `SetAudioTrackEXT`, `SetVideoTrackEXT` — confirmed `NOXNA`-marked, since these
  are FNA extensions beyond the original XNA 4.0 surface, not CNA's own invention).
- [x] **MEDIA-27 — `Video/VideoPlayer`: Doxygen/SPDX/`GetTypeName`/NOXNA compliance pass.**
- [x] **MEDIA-28 — `CNA/Internal/Media/VideoDecoder`: SPDX/internal-header-style Doxygen compliance
  pass.**

  *(MEDIA-15…28 accept criteria, all 14: every public member has a `/** @brief */` block; no bare `///`
  on public API; every concrete `System::Object`-derived class overrides `GetTypeName()`; every
  non-XNA member is `NOXNA`; SPDX header correct.)*

- [x] **MEDIA-29 — Confirm `MediaSourceType` enum needs no fix.** `LocalDevice=0,WindowsMediaConnect=4`
  — independently verified against FNA, exact match, correct Doxygen. Close out as confirmed-correct (no
  code change) so a future session doesn't re-open it as a suspected gap.
  *Accept:* a value/order test exists (or is added in MEDIA-96).

- [x] **MEDIA-30 — Confirm `MediaState` enum needs no fix.** `Stopped,Playing,Paused` — exact match.
  *Accept:* a value/order test exists (MEDIA-96).

- [x] **MEDIA-31 — Confirm `VideoSoundtrackType` enum needs no fix, and document its metadata-only
  status (§2.5).** `Music,Dialog,MusicAndDialog` — exact match; add a one-line doc note on
  `Video::getVideoSoundtrackTypeProperty()` recording that no playback logic (on either side) ever
  branches on it, so this isn't re-flagged as a gap by a future audit. **Do not** add new ducking/muting
  logic FNA itself never had — that would be scope creep past behavior fidelity.
  *Accept:* doc note added; no behavior change; §2 deviation-table row added.

### Phase 2 — Real bug fixes in the already-working (Playback) cluster

- [x] **MEDIA-32 — Fix or document `MediaPlayer`'s silent no-`SOUND_ENABLED` gap.**
  `MediaPlayer::Update()` (`MediaPlayer.cpp:291-293`) returns immediately when built without
  `SOUND_ENABLED`, so song-end detection and Queue auto-advance never fire in that configuration.
  *Recommended:* add a `SOUND_ENABLED`-independent fallback using the already-tracked wall-clock
  `PlayPosition` vs. `Song.Duration` to detect song completion, so headless/no-audio builds still
  advance the queue correctly; if not implemented, document as an accepted limitation in
  `CHECKLIST.md` instead.
  *Accept:* a test built without/with the flag simulated exercises queue auto-advance either way; the
  decision is recorded either in code behavior or in `CHECKLIST.md`.
  *Honest gap (Phase 8, found by external code review):* the fallback itself is real
  (`DetectSongEndedByElapsedTime`, a pure, always-compiled function) and its own logic is
  thoroughly tested directly (`MediaPlayerNoSoundFallbackTest.*`) -- but this literal Accept
  criterion ("a test built without the flag ... exercises queue auto-advance") is not met: `CNA`'s
  own `target_compile_definitions` (`cmake/CnaLibrary.cmake`) unconditionally defines
  `SOUND_ENABLED` for every build configuration this project produces -- there is no CMake preset,
  option, or CI variant anywhere that ever disables it. This means `MediaPlayer::Update()`'s
  `#else` branch (the one that actually calls `DetectSongEndedByElapsedTime` and drives real
  auto-advance) is never compiled into any binary this project builds today, so no test in this
  process can reach it. This is a genuine, pre-existing, project-wide condition (affects every
  `#ifdef SOUND_ENABLED`/`#else` pair across the whole Audio/Media stack, not just this one
  function) -- closing it for real would mean adding a new CMake build variant with CI coverage,
  out of proportion to this one task's scope. Documented rather than silently left implied-complete.

- [x] **MEDIA-33 — Confirm `MediaPlayer` shuffle matches FNA's "can repeat" behavior.** FNA's
  `NextSong` (`MediaPlayer.cs:353-356`) does `Queue.ActiveSongIndex = random.Next(Queue.Count)` with
  **no exclusion** of the currently-playing index. Confirm CNA's shuffle implementation has the same
  "can repeat the same song" behavior rather than an unrequested "improvement."
  *Accept:* a regression test documents/locks in the exact (can-repeat) behavior.

- [x] **MEDIA-34 — Verify `MediaPlayer.Play()`'s "duplicate the Song at play time" quirk is
  reproduced.** FNA's own comment: "Believe it or not, XNA duplicates the Song object and then assigns a
  bunch of stuff to it at Play time" — `LoadSong` constructs a **new** `Song(song.handle, song.Name)`
  rather than enqueuing the caller's own instance, meaning mutations to the original `Song` object after
  `Play()`/`Play(SongCollection)` don't affect what's actually playing/queued. Confirm CNA's equivalent
  internal enqueue path does the same (not a reference-share), since silently "fixing" this would be an
  undocumented behavior change from real XNA/FNA semantics.
  *Accept:* a test mutates the original `Song` after `Play()` and confirms the queued copy is unaffected
  (or the deviation is explicitly documented if CNA currently shares the reference).

- [x] **MEDIA-35 — `VideoDecoder`: add YUV422P/YUV444P conversion.** `generic_to_rgba`
  (`VideoDecoder.cpp:271-332`) only handles `RGBA`/`RGB24`/`NV12`; everything else (including 4:2:2 and
  4:4:4, which are within FNA's own real supported pixel-layout matrix — Theora's
  `TH_PF_422`/`TH_PF_444`, dav1d's `I422`/`I444`) falls through to a hardcoded solid-magenta fill. Add
  real conversion paths for the FFmpeg `AVPixelFormat`s corresponding to 4:2:2/4:4:4 8-bit content.
  *Accept:* MEDIA-6's 4:2:2/4:4:4 fixtures decode to correct colors, not magenta.

- [x] **MEDIA-36 — `VideoDecoder`: add 10/12-bit HDR pixel-format support.** FNA's `VideoPlayerAV1` has
  real high-bit-depth handling (a shader `RescaleFactor` of `4096/65536` for 10-bit, `1024/65536` for
  12-bit). CNA's `generic_to_rgba` has no equivalent, so 10/12-bit content either misrenders or falls to
  the magenta fallback — a real capability regression vs. FNA. Add coverage for the relevant
  high-bit-depth `AVPixelFormat`s (e.g. `AV_PIX_FMT_YUV420P10LE`) with the equivalent rescale.
  *Accept:* MEDIA-7's 10-/12-bit fixtures decode with correct (non-clipped, non-magenta) color.

- [x] **MEDIA-37 — Document AV1-content-keeps-audio as a deliberate improvement over FNA (§2.3).** No
  code change — CNA's unified decoder already does this; add the deviation-table entry and a doc note
  so it isn't mistaken for an unported FNA limitation later.
  *Accept:* §2/`CHECKLIST.md` entry added; a test confirms an AV1-container fixture with an audio track
  actually produces audible/decodable audio output through `VideoDecoder`.

- [x] **MEDIA-38 — Harden `VideoDecoder::Open`/`OpenAudioStreamByIndex` against allocation failures.**
  `avcodec_alloc_context3()`'s return isn't null-checked before use (`VideoDecoder.cpp:58-60,87-89`);
  `swr_alloc_set_opts2()`'s return value is discarded (`102-106`,`173-177`) before the
  immediately-following `swr_init()` call.
  *Original text (superseded below, kept for history):* "Add explicit null/error checks that throw a
  clear, documented C++ exception instead of risking a null dereference." *Accept (original):* "a
  fault-injection test throws cleanly instead of crashing; ASan-clean."
  **Correction (Phase 12, `MEDIA-160`/`MEDIA-162`, found by external code review, `plan_media.md`
  `MEDIA-165`; this paragraph itself corrected again in Phase 13→14, `MEDIA-168`, after a second
  external review caught it repeating a *different* factual error): the "throw" wording above was
  never actually implemented, and this checkbox should not have been left claiming it was.**
  `avcodec_alloc_context3()`/`AllocAndConfigureCodecContext()` null-checks are real, but -- like
  every other allocation-failure path inside `Open()` (the video codec context, `frame_`/`pkt_`) --
  they gracefully `return false` (`Open()`'s own established, non-throwing `bool` contract), not
  throw; an earlier version of this very correction paragraph incorrectly claimed they "do throw,"
  which was itself wrong (found by external code review, `plan_media.md` `MEDIA-168`). The one place
  in this general family that genuinely does throw is `ProcessAudioPacket()`'s own
  `av_frame_alloc()` for the per-packet decode frame -- a *decode-time* allocation inside a function
  whose contract (like `NextFrame()`'s) already allows throwing, unlike `Open()`-time allocations.
  `swr_alloc_set_opts2()`/`swr_init()` failure specifically was deliberately changed to graceful
  degradation instead of a throw (see `MEDIA-160`'s own reasoning: `Open()`'s established
  non-throwing `bool` contract, relied on unwrapped by dozens of call sites, shouldn't be broken for
  one failure mode; a throw immediately caught back into the same degrade-to-"no audio" behavior
  would be a no-op complication) -- consistent with, not an exception to, how every other
  `Open()`-time allocation failure in this function already behaves. This is the actual, correct fix
  for this specific failure mode — the *original* Accept criterion's "throw a clear exception"
  wording was simply wrong for this case and is retracted here, not merely left unmet.
  *Accept (actual, current):* `HasAudio()` requires both `audioCtx_` and `swrCtx_` (`MEDIA-160`);
  `OpenAudioStreamByIndex()`/`Open()`'s audio setup both build+verify the resampler before
  committing to using that audio track at all, so a resampler failure never leaves a half-broken
  "audio available but silent" track (`MEDIA-162`). No fault-injection test or ASan run was added --
  `swr_alloc_set_opts2()` failing for a channel layout/sample format `avcodec_open2()` already
  accepted is not practically reproducible with this repo's real fixtures, matching this plan's own
  established precedent (`MEDIA-94`) for not building disproportionate fault-injection
  infrastructure for a near-unreachable failure mode.

- [x] **MEDIA-39 — Harden the broader unchecked FFmpeg decode return codes.** Beyond MEDIA-38's
  allocation sites, `avcodec_send_packet`/`swr_convert`'s return values are unchecked in several other
  call sites within `NextFrame`/the audio-packet-processing path. A malformed file can silently produce
  garbage frames rather than a clear error. Add explicit error checks/propagation at each site.
  *Accept:* MEDIA-7's truncated/corrupted fixture surfaces a clear exception rather than garbage output
  or a crash, at each hardened call site (not just the allocation sites from MEDIA-38).

- [x] **MEDIA-40 — Distinguish real I/O errors from EOF in `av_read_frame` handling.** Every negative
  return from `av_read_frame` is currently treated as EOF; only `AVERROR_EOF` should be. A genuine
  mid-stream I/O error should surface as an error/exception, not silently end playback as if the video
  finished normally.
  *Accept:* MEDIA-7's truncated/corrupted fixture demonstrates the distinction (a clear I/O-error signal,
  not a silent "video ended normally").

- [x] **MEDIA-41 — Confirm end-of-stream "let audio drain" parity.** FNA's `VideoPlayerTheora`
  explicitly waits for `audioStream.PendingBufferCount==0` in addition to `tf_eos` before declaring
  `State==Stopped`, so queued audio isn't cut off abruptly. Audit CNA's `VideoDecoder`/`VideoPlayer` EOS
  handling for the same behavior; fix if audio is currently truncated at video EOS.
  *Accept:* MEDIA-7's audio-tail-longer-than-video fixture plays that tail out before `State` flips to
  `Stopped`.

- [x] **MEDIA-42 — Confirm dimension/fps sanity-check parity for the decode path.** FNA's
  `VideoPlayerAV1`/`VideoPlayerTheora.Play()` both throw `InvalidOperationException` on a dimension/fps
  mismatch between the container's declared metadata and what the decoder reports (AV1 additionally
  guards `fps==0`). Confirm `VideoDecoder`/`VideoPlayer` perform an equivalent check and throw a
  comparable, documented exception rather than silently producing garbage frames.
  *Accept:* MEDIA-7's mismatched-metadata fixture (or a synthetic unit test constructing a `Video` with
  deliberately wrong declared dimensions) throws; matches FNA's exception-type intent.

- [x] **MEDIA-43 — `VideoPlayer`: enforce a disposed-state guard on every public method (X5).** FNA's
  real `VideoPlayer` calls `checkDisposed()` (throwing `ObjectDisposedException("VideoPlayer")`, exact
  literal type-name string — preserve verbatim for message fidelity) at the top of every public method.
  Add the equivalent guard to CNA's `Play`/`Stop`/`Pause`/`Resume`/`GetTexture`/property
  getters-and-setters that currently have none.
  *Accept:* calling any public method after `Dispose()` throws the documented exception instead of
  touching stale state; a test covers at least `Play`/`GetTexture`/`Stop` post-dispose.

- [x] **MEDIA-44 — `Video`: throw `FileNotFoundException` from the raw-file constructor for a missing
  file.** FNA's raw-file `Video(string fileName, GraphicsDevice device)` constructor explicitly checks
  `File.Exists` and throws. CNA's equivalent currently probes via `VideoDecoder::Open` and silently
  leaves `width_`/`height_`/`duration_` at 0 on failure, with no exception at all — a real fidelity gap.
  *Accept:* constructing a `Video` from a nonexistent path throws `System::IO::FileNotFoundException`; a
  valid path is unaffected.

- [x] **MEDIA-45 — Confirm/document `VideoPlayer::GetTexture()`'s behavior when called before any
  `Play()`.** FNA's real behavior is an unguarded `impl.GetTexture()` call that throws a plain
  `NullReferenceException` if `impl` is still null (matches XNA's own actual behavior of calling
  `GetTexture()` before ever calling `Play()` — not a bug to silently improve). Decide: reproduce this
  exact "undefined-looking but XNA-faithful" crash-on-misuse behavior, or throw a clearer, documented C++
  exception instead (a reasonable, callable-out deviation given C++ has no safe null-dereference
  equivalent to catch). Record whichever is chosen.
  *Accept:* the chosen behavior is implemented and covered by a test; the choice (and why) is recorded
  in `CHECKLIST.md`'s deviations table if it diverges from FNA's crash-on-misuse shape.

### Phase 3 — Real local media-library backend (`CNA::Internal::Media::*`, all `NOXNA`)

> Per §4's decisions. This is genuinely new engineering, not a port — there is no FNA logic here to
> compare against, only the public XNA API shape/exception contract from Phase 4 to satisfy.

- [x] **MEDIA-46 — `MediaLibraryPaths`: real per-OS root discovery (D1).** New
  `CNA::Internal::Media::MediaLibraryPaths` resolving the Music/Pictures roots via
  `SDL_GetUserFolder(SDL_FOLDER_MUSIC)`/`SDL_GetUserFolder(SDL_FOLDER_PICTURES)`; plus NOXNA static
  override hooks (`MediaLibrary::SetMusicRootEXT(path)` / `SetPictureRootEXT(path)`) so tests don't
  depend on the real user's actual folders having content.
  *Accept:* on a machine with no override set, resolves to a real existing directory (or a documented
  graceful fallback if the OS folder doesn't exist); override hook redirects scanning to MEDIA-3/4's
  fixture trees.

- [x] **MEDIA-47 — `AudioTagParser`: Ogg page-framing primitive (D2).** New
  `CNA::Internal::Media::AudioTagParser` internals — minimal from-scratch Ogg page/segment parsing
  sufficient to locate the Vorbis comment header packet within an `.ogg` file, independent of the
  comment-field extraction itself.
  *Accept:* correctly locates the comment-header packet boundary against MEDIA-3's `.ogg` fixture(s).

- [x] **MEDIA-48 — `AudioTagParser`: Vorbis comment-field extraction (D2).** On top of MEDIA-47, extract
  `TITLE`/`ARTIST`/`ALBUM`/`GENRE`/`TRACKNUMBER` fields from the located comment header.
  *Accept:* MEDIA-3's tagged `.ogg` fixture parses to the exact documented tag values; malformed input
  fails gracefully (no crash), falling through to MEDIA-51's heuristics.

- [x] **MEDIA-49 — `AudioTagParser`: ID3v2 synchsafe frame-size decoding primitive (D2).** Minimal header
  parsing for ID3v2.3/2.4: the 10-byte tag header, synchsafe-integer size decoding, and per-frame header
  (`id[4]`, `size`, `flags`) iteration, independent of interpreting any specific frame's payload.
  *Accept:* correctly enumerates every frame (id + byte range) in MEDIA-3's ID3v2.3 and ID3v2.4
  fixtures.

- [x] **MEDIA-50 — `AudioTagParser`: ID3v2 text-frame extraction with encoding handling (D2/D11).** On
  top of MEDIA-49, extract the 5 relevant text frames (`TIT2`/`TPE1`/`TALB`/`TCON`/`TRCK`), decoding the
  text-encoding byte (`0x00` Latin-1, `0x01` UTF-16+BOM, `0x02` UTF-16BE, `0x03` UTF-8) rather than
  assuming ASCII.
  *Accept:* MEDIA-3's ID3v2.3/2.4 fixtures parse to the exact documented tag values; a fixture with a
  non-Latin-1 encoded frame (if authored in MEDIA-3) decodes correctly; a file with no ID3v2 tag (or
  ID3v1-only) falls through cleanly to MEDIA-51.

- [x] **MEDIA-51 — `AudioTagParser`: filename/folder fallback heuristics (D2).** When a file has no
  usable tags (or is `.wav`/an otherwise-unsupported-for-tagging format), derive Title from the
  filename, Album from the parent directory name, Artist from the grandparent directory name.
  *Accept:* MEDIA-3's untagged `.wav` fixture (in a `Music/SomeArtist/SomeAlbum/Track.wav`-shaped tree)
  produces the expected inferred Artist/Album/Title.

- [x] **MEDIA-52 — `MediaLibraryIndex`: recursive song scan + grouping (D3).** New
  `CNA::Internal::Media::MediaLibraryIndex` — one-shot recursive scan of the Music root, building an
  in-memory index of every audio file found (`.ogg`/`.mp3`/`.wav`, tagged via MEDIA-48/50/51) as a real
  `Song`, grouped Artist→Album→Song and flat Genre→Song.
  *Accept:* against MEDIA-3's fixture tree, the resulting groupings are exactly correct; a file with an
  unsupported extension is skipped, not errored.

- [x] **MEDIA-53 — `MediaLibraryIndex`: harden the recursive scan against symlink loops and
  permission errors.** A real filesystem scan of a real user directory can encounter a symlink cycle
  (infinite recursion risk) or a subdirectory the process can't read (permission-denied). Add loop
  detection (e.g. track visited canonical/real paths) and treat unreadable entries as skip-with-warning,
  not a crash or hang.
  *Accept:* a synthetic fixture with a self-referential symlink terminates instead of hanging; a
  permission-denied subdirectory (where the test environment allows creating one) is skipped, not fatal.

- [x] **MEDIA-54 — Case-insensitive Artist/Genre name normalization (D10).** Grouping key for
  Artist/Genre is case-folded + trimmed; displayed `Name` keeps first-seen original casing.
  *Accept:* a fixture with the same artist tagged as `"Artist A"` in one file and `"artist a"` in another
  (add to MEDIA-3 if not already present) produces exactly one `Artist` grouping both songs.

- [x] **MEDIA-55 — `MediaCollectionBase<T>`: shared collection backend (D9).** New NOXNA internal
  template backing storage/indexer/enumerator/`Dispose` logic reused by all 6 public collection types
  (`AlbumCollection`/`ArtistCollection`/`GenreCollection`/`PictureCollection`/
  `PictureAlbumCollection`/`PlaylistCollection`). Public class names and exception contracts stay fully
  distinct.
  *Accept:* instantiates cleanly for at least 2 different `T`s in a throwaway test; no XNA-facing class
  is renamed or merged.

- [x] **MEDIA-56 — `PictureLibraryIndex`: real Picture/PictureAlbum tree scan, reusing `ImageLoader`
  (D4).** New `CNA::Internal::Media::PictureLibraryIndex` — recursive scan of the Pictures root; one
  `PictureAlbum` node per real subdirectory (with real `Parent` links forming an actual tree), one
  `Picture` leaf per image file; dimensions read via the **existing**
  `CNA::Internal::Graphics::ImageLoader::Load(path).width/.height` (do not reimplement image decoding),
  `Date` via filesystem last-write-time.
  *Accept:* against MEDIA-4's fixture Pictures tree, the resulting tree's parent/child links and
  per-picture metadata are exactly correct.

- [x] **MEDIA-57 — `PlaylistParser`: M3U reader (D5).** New `CNA::Internal::Media::PlaylistParser` —
  reads path-per-line `.m3u` files (with optional `#EXTINF` comments tolerated/ignored or used for a
  title hint), resolving each entry against the already-built `MediaLibraryIndex`.
  *Accept:* MEDIA-5's `.m3u` fixture resolves to the correct `Song` sequence; an entry pointing at a
  nonexistent file is skipped, not fatal.

- [x] **MEDIA-58 — `PlaylistParser`: M3U8 (UTF-8) support (D5).** Extend MEDIA-57 to treat `.m3u8` as
  UTF-8-encoded (vs. `.m3u`'s local/legacy encoding), otherwise identical parsing logic.
  *Accept:* MEDIA-5's `.m3u8` fixture (with a non-ASCII filename/title) resolves correctly.

- [x] **MEDIA-59 — `SavedPictureStore`: real `SavePicture` backing (D7).** New
  `CNA::Internal::Media::SavedPictureStore` — writes `SavePicture(name, buffer)`/`SavePicture(name,
  Stream)` output to a real `Saved Pictures` subfolder of the Pictures root; the result backs
  `MediaLibrary::SavedPictures`/`RootPictureAlbum` for real.
  *Accept:* calling `SavePicture` creates a real file on disk, readable back as a `Picture` with correct
  `GetImage()` content.

- [x] **MEDIA-60 — Internal-backend compliance pass.** SPDX + Doxygen (brief, internal-header style —
  matching `XactParser`/`AudioMixer`'s existing precedent, not the full public-API Doxygen bar) across
  every new file from MEDIA-46-59; confirm the new `.cpp` files are picked up by the existing
  `GLOB_RECURSE` with zero `CMakeLists.txt` changes needed.
  *Accept:* `cmake --build cmake-build-debug --target CNA` compiles the new files with no manual CMake
  edits.

### Phase 4 — Wire the real backend onto the public XNA API (one class at a time, "make and forget")

- [x] **MEDIA-61 — `MediaSource`: real implementation.** `GetAvailableMediaSources()` returns a real
  single-element list (`{ MediaSource(LocalDevice, "<platform-appropriate name>") }`) backed by a now
  genuinely reachable internal constructor; `Name`/`MediaSourceType`/`ToString()` all real.
  *FNA:* MediaSource.cs (API shape only — no FNA logic to port, see §0).
  *Accept:* `GetAvailableMediaSources().Count == 1`; round-trips through `MediaLibrary(MediaSource)`
  (MEDIA-62).

- [x] **MEDIA-62 — `MediaLibrary`: real implementation.** Both constructors real: no-arg triggers
  `MediaLibraryIndex`+`PictureLibraryIndex` scans (D6, synchronous); `MediaLibrary(MediaSource)` accepts
  the one real `LocalDevice` source instead of throwing. All 10 properties
  (`Albums`/`Artists`/`Genres`/`Pictures`/`Playlists`/`RootPictureAlbum`/`SavedPictures`/`Songs`/
  `MediaSource`/`IsDisposed`) return real, populated collections. `GetPictureFromToken`, both
  `SavePicture` overloads, `Dispose` all real.
  *FNA:* MediaLibrary.cs (API shape only).
  *Accept:* constructing against MEDIA-3/4's fixture Music+Pictures tree produces a `MediaLibrary` whose
  every property is populated and internally consistent (feeds MEDIA-69's full graph audit).

- [x] **MEDIA-63 — `Genre` + `GenreCollection`: real implementation.** Backed by `MediaLibraryIndex`'s
  genre grouping (incl. MEDIA-54's normalization); `Albums`/`Songs`/`Name`/`IsDisposed` real;
  `Equals`/`GetHashCode`/`operator==`/`operator!=`/`ToString` real, by `Name`.
  *Accept:* per-genre album/song membership matches the fixture tree exactly; equality tests for
  equal/unequal genres.

- [x] **MEDIA-64 — `Artist` + `ArtistCollection`: real implementation.** Backed by the index's artist
  grouping (incl. MEDIA-54's normalization); `Albums`/`Songs`/`Name`/`IsDisposed` real; equality set
  real, by `Name`.
  *Accept:* per-artist album/song membership matches the fixture tree exactly; equality tests.

- [x] **MEDIA-65 — `Album` + `AlbumCollection`: real implementation.** Backed by the index's
  artist→album grouping; `Artist`/`Genre`/`Duration` (sum of member `Song.Duration`)/`HasArt`/`Songs`/
  `Name`/`IsDisposed` real. `GetAlbumArt`/`GetThumbnail`: real `Stream` if a same-directory cover image
  (`cover.jpg`/`folder.jpg`) is found (MEDIA-4's fixture), else a documented, XNA-plausible exception
  (not a crash) when `HasArt==false`. Equality set real, by `(Name, Artist)` (album names can collide
  across artists).
  *Accept:* `HasArt` correctly reflects fixture presence/absence of a cover file; `Duration` sums
  correctly; equality tests including the same-name-different-artist case.

- [x] **MEDIA-66 — `Picture` + `PictureCollection`: real implementation.** Backed by
  `PictureLibraryIndex`; `Album`/`Date`/`Height`/`Width`/`Name`/`IsDisposed` real. `GetImage`/
  `GetThumbnail` return real `System::IO::Stream`s (via `System::IO::FileStream`/`MemoryStream`,
  already in sharp-runtime). Equality set real, by resolved file path.
  *Accept:* dimensions/date match the fixture files exactly; `GetImage()` content round-trips
  byte-for-byte against the source file.

- [x] **MEDIA-67 — `PictureAlbum` + `PictureAlbumCollection`: real implementation.** Real tree node
  backed by `PictureLibraryIndex`; `Albums`/`Pictures`/`Parent`/`Name`/`IsDisposed` real, including the
  self-referencing root case (`MediaLibrary.RootPictureAlbum.Parent == nullptr`).
  *Accept:* walking `Parent` from any leaf reaches the root in exactly as many steps as the fixture's
  real directory depth.

- [x] **MEDIA-68 — `Playlist` + `PlaylistCollection`: real implementation.** Backed by `PlaylistParser`
  results scanned from the Music root's `.m3u`/`.m3u8` files. `Duration` (sum of member
  `Song.Duration`)/`Songs`/`Name`/`IsDisposed` real. `Equals`/`GetHashCode` real, by `Name`.
  `operator==`/`operator!=` **keep** FNA's exact null-check shape
  (`ReferenceEquals(first,null) ? ReferenceEquals(first,second) : first.Equals(second)`), now backed by
  a real, non-throwing `Equals` instead of a stub.
  *Accept:* MEDIA-5's fixture `.m3u`/`.m3u8` resolve to the correct `Song` sequence and `Duration`;
  equality tests including both-null/one-null/both-real cases.

- [x] **MEDIA-69 — Full cross-class object-graph integration audit.** Construct a real `MediaLibrary`
  against MEDIA-3/4's fixture tree and walk every relationship round-trip (e.g. `Genres[i].Albums[j]`'s
  `Songs[k]`'s containing `Album` equals `Albums[j]`; every `Picture.Album.Pictures` contains that
  `Picture`) to confirm the object graph is internally consistent, not just individually populated.
  *Accept:* all round-trip assertions pass against the fixture; this test doubles as the canonical
  "Media library is real" regression guard.

### Phase 5 — Content-pipeline (XNB) integration

- [x] **MEDIA-70 — `VideoContentTypeReader`.** New `CNA::Internal::Xnb::VideoContentTypeReader`
  (`.hpp`/`.cpp`), mirroring `SongContentTypeReader`'s existing structure: port FNA's real
  `VideoReader.cs` field layout — path string (with the XNB-embedded fake `.wmv` suffix stripped and
  re-resolved against `.ogv`/`.ogg`, matching `SongReader`'s analogous `.wma`-strip/`Normalize`
  pattern) + `durationMS`(int32) + `width`(int32) + `height`(int32) + `framesPerSecond`(float32) +
  `soundTrackType`(int32, cast to `VideoSoundtrackType`) — so `ContentManager::Load<Video>()` becomes
  possible for the first time (confirmed: no such reader exists anywhere in the repo today, and
  `plan_xnb.md` never planned one).
  *FNA:* `src/Content/ContentReaders/VideoReader.cs` (FNA-XNA source tree, not under `src/Media/`).
  *Accept:* deserializes a real `.xnb`-wrapped `Video` fixture into a working `Video` object with correct
  metadata.

- [x] **MEDIA-71 — Field-read style: match `SongContentTypeReader`'s existing code style, not FNA's own
  internal inconsistency.** FNA's `SongReader` reads its one int field directly (`ReadInt32()`) while
  `VideoReader` reads its 5 fields via the more generic `input.ReadObject<T>()` — an FNA-internal C#
  implementation-detail inconsistency with no observable binary-format effect either way. Use direct,
  typed reads (matching whatever style `SongContentTypeReader` already established in CNA) for
  `VideoContentTypeReader` too, for internal consistency, rather than replicating FNA's own stylistic
  unevenness.
  *Accept:* code review confirms consistent read-call style between the two readers; binary layout
  compatibility is unaffected (verified by MEDIA-70's round-trip test).

- [x] **MEDIA-72 — Register the new reader.** Add `RegisterVideoXnbReader()`, called from
  `XnbBuiltInReaders.cpp`, matching `RegisterSongXnbReader()`'s existing call-site pattern exactly.
  *Accept:* `ContentManager::Load<Video>("fixture")` works end-to-end.

- [x] **MEDIA-73 — Content-pipeline fixture + round-trip test.** Source or construct a real
  `.xnb`-wrapped `Video` fixture (matching the `Song` fixture's own sourcing approach — a real
  MonoGame-produced `.xnb` plus its companion video file, vendored alongside it) and verify the full
  `ContentManager::Load<Video>()` path.
  *Accept:* test passes; companion video file resolves and is playable via `VideoPlayer`.
  *Corrected (Phase 8, found by external code review):* this was previously checked off with only a
  reader-level test (`VideoContentTypeReaderTests.cpp`, calling `VideoReader::Read()` directly, not
  through `ContentManager::Load<Video>()`), self-contradicting the checkbox against its own written
  "honest gap" note -- genuinely unmet at the time. Now closed for real:
  `ContentManagerVideoXnbTests.cpp`'s `LoadRealFixtureEndToEndProducesAPlayableVideo` hand-
  constructs a full, valid `.xnb` byte container (header + type-reader table + `VideoReader`'s exact
  real field layout -- the same technique `ContentManagerXnbTests.cpp`'s own `BuildTestXnbFile()`
  and `ContentManagerSongXnbTests.cpp`'s own broken-fixture builder already use elsewhere in this
  codebase) referencing the real, checked-in `chroma_420.mkv` companion file, loads it through the
  real `ContentManager::Load<Video>()` path, and confirms the result is genuinely playable via
  `VideoPlayer` (`Play()` + `GetTexture()` produces a real, correctly-sized texture). No
  MonoGame/dotnet content-pipeline tooling is available in this environment to produce an
  authentically mgcb-built `.xnb`, so the bytes are hand-assembled rather than tool-generated --
  functionally equivalent proof of the container-parsing path (the reader logic doesn't know or
  care who produced the bytes, only that they match the real binary format), but still a real,
  disclosed provenance difference from `Song`'s vendored-mgcb-binary fixture. A future session with
  `mgcb` access could still replace this with a genuinely tool-produced fixture if that provenance
  gap ever matters.

- [x] **MEDIA-74 — Cross-reference `plan_xnb.md`.** Add a pointer row there documenting this reader's
  completion, matching how `plan_xnb.md` documents the `SongReader` task, keeping the two plans
  consistent for future readers.
  *Accept:* `plan_xnb.md` updated.

- [x] **MEDIA-75 — Re-verify `SongContentTypeReader` after MEDIA-10.** Confirm the existing Song
  reader's own error path (an unresolvable `.ogg`/`.oga`/`.qoa` after extension-reprobing) now surfaces
  `System::IO::FileNotFoundException` end-to-end through `ContentManager::Load<Song>()`, not just at the
  raw `Song` constructor level.
  *Accept:* a negative-path content-load test confirms the corrected exception type propagates.

### Phase 6 — Complete test suite (Google Test)

> Rules from `CLAUDE.md`/`CHECKLIST.md`: every public method, **every overload**, operator and constant
> has ≥1 test; `==`/`!=`/`Equals` both equal and unequal cases; `ToString` format; `GetHashCode`
> consistency; `GetTypeName` exact value. Tasks below are split by behavior/class rather than bundled,
> matching that per-overload mandate.

- [x] **MEDIA-76 — SongTests.** Both ctors + file-existence validation (now `FileNotFoundException`,
  MEDIA-10); `Name`/`Duration`/`PlayCount`; the 4 FNA-faithful constant getters (MEDIA-13), asserted
  explicitly as constants; `Equals`/`GetHashCode` (incl. MEDIA-14's content-based-hash regression)/
  `operator==`/`operator!=`; `Dispose`/`IsDisposed`; `FromUri`.

- [x] **MEDIA-77 — MediaQueueTests.** `ActiveSong`/`ActiveSongIndex`/`Count`/indexer (incl. MEDIA-11's
  corrected exception type)/`Add`/`Clear`.

- [x] **MEDIA-78 — SongCollectionTests.** Indexer (incl. MEDIA-12's corrected exception type)/`Count`/
  `IsDisposed`/`Dispose`/iteration (`begin`/`end`).

- [x] **MEDIA-79 — VisualizationDataTests.** `Frequencies`/`Samples` sizing (`Size==256`) and zero-init.

- [x] **MEDIA-80 — MediaPlayer-PlaybackTests.** `Play(Song)`; `Play(SongCollection)`;
  `Play(SongCollection, index)`; `Pause`/`Resume`/`Stop`; `State` transitions across all of the above.

- [x] **MEDIA-81 — MediaPlayer-QueueNavigationTests.** `MoveNext`/`MovePrevious`;
  `IsRepeating`/`IsShuffled` (incl. MEDIA-33's can-repeat regression and MEDIA-34's
  duplicate-on-play regression); `Queue` state after navigation.

- [x] **MEDIA-82 — MediaPlayer-VolumeMuteTests.** `Volume` (incl. clamping), `IsMuted`,
  `GameHasControl` (hardcoded-`true` regression, matching FNA).

- [x] **MEDIA-83 — MediaPlayer-EventsTests.** `ActiveSongChanged`+`MediaStateChanged`, including a test
  that actually drives them through `FrameworkDispatcher::Update()` (confirming the real, already-wired
  call chain, not just a direct internal `OnActiveSongChanged()`/`OnMediaStateChanged()` invocation).

- [x] **MEDIA-84 — MediaPlayer-VisualizationTests.** `IsVisualizationEnabled`+`GetVisualizationData`,
  including the current SDL3_mixer limitation's documented behavior (MEDIA-32-adjacent finding: no real
  visualization data available — confirm this is asserted, not silently ignored by the test).

- [x] **MEDIA-85 — Enum tests.** `MediaSourceType`/`MediaState`/`VideoSoundtrackType` value/order tests
  (closes MEDIA-29/30/31).

- [x] **MEDIA-86 — VideoTests.** All 3 constructors (incl. MEDIA-44's `FileNotFoundException`
  regression); `FromUriEXT`; `SetAudioTrackEXT`/`SetVideoTrackEXT`; Duration-hack behavior differs
  correctly between raw-file and XNB-sourced construction.
  *Correction (Phase 6 audit):* "codec-guess-equivalent behavior" in this task's original wording
  does not correspond to any actual logic in `Video.cpp`/`Video.hpp` (confirmed by direct grep) --
  stale wording from an earlier draft of this plan, not a real coverage gap. Dropped rather than
  worked around with an invented test.

- [x] **MEDIA-87 — VideoPlayer-PlaybackStateTests.** `Play`/`Stop`/`Pause`/`Resume`/`State`/
  `PlayPosition`, against a real fixture video.

- [x] **MEDIA-88 — VideoPlayer-DisposalGuardTests.** MEDIA-43's regression: every public method throws
  the documented exception after `Dispose()`.

- [x] **MEDIA-89 — VideoPlayer-LoopMuteVolumeTests.** `IsLooped`/`IsMuted`/`Volume` (incl. clamping),
  and MEDIA-37's AV1-keeps-audio regression.

- [x] **MEDIA-90 — VideoPlayer-TrackSelectionTests.** `SetAudioTrackEXT`/`SetVideoTrackEXT` round-trip
  against a multi-track fixture (if available; otherwise document as not-yet-coverable and note the gap
  rather than skip silently).

- [x] **MEDIA-91 — VideoDecoder-PixelFormatMatrixTests.** MEDIA-35's 4:2:0/4:2:2/4:4:4 regression using
  MEDIA-6's fixtures.

- [x] **MEDIA-92 — VideoDecoder-BitDepthTests.** MEDIA-36's 8/10/12-bit regression using MEDIA-7's
  fixtures.

- [x] **MEDIA-93 — VideoDecoder-AudioExtractionTests.** Audio-stream extraction/resample correctness,
  independent of video-frame decoding.

- [x] **MEDIA-94 — VideoDecoder-ErrorHandlingTests.** MEDIA-38/39/40 regressions: alloc-failure
  fault injection, corrupt/truncated fixture, I/O-error-vs-EOF distinction — all against MEDIA-7's
  fixtures.
  *Corrected (Phase 8, found by external code review):* the Phase 6 note above overclaimed —
  `TruncatedMidStreamDataThrowsRatherThanSilentlyEndingCleanly` was a tautology (asserted success on
  either a thrown exception OR a clean EOF, so it could never fail and proved nothing), and
  `NextFrame()` itself still merged genuine decode errors with EOF (`avcodec_receive_frame`'s
  non-`AVERROR_EOF` negative codes were treated identically to EOF) and never checked
  `avcodec_send_packet`'s/`av_frame_alloc`'s/`av_packet_alloc`'s return values. All now genuinely
  fixed: `NextFrame()` distinguishes `AVERROR_EOF` from real decode errors and checks
  `avcodec_send_packet`'s return; `Open()` fails cleanly if either FFmpeg allocation returns null;
  `AllocAndConfigureCodecContext` sets `AV_EF_CRCCHECK|AV_EF_BITSTREAM|AV_EF_BUFFER|AV_EF_EXPLODE`
  so a decoder that would otherwise silently tolerate corrupted frame data now hard-fails instead
  (found empirically: `AV_EF_EXPLODE` alone doesn't make FFV1's own slice-CRC mismatch fatal --
  `AV_EF_CRCCHECK` is the bit that actually enables the check FFV1 gates on). Two real, deterministic
  tests replace the old tautology: `TruncatedFileEndsAtCleanEOFRatherThanThrowing` (documents that
  Matroska's demuxer is deliberately streaming-tolerant -- a truncated tail is always clean
  `AVERROR_EOF`, verified across a dozen+ truncation points, never a distinct error) and
  `CorruptedMidStreamDataThrowsRatherThanSilentlyEndingCleanly` (a new, checked-in
  `corrupt_test_h264.mp4` fixture -- H264 is far less lenient than FFV1/Matroska about corrupted
  macroblock data, reliably producing a genuine `AVERROR_INVALIDDATA` under the new strictness
  flags). The one remaining honest gap: true allocation-failure fault injection (forcing
  `avcodec_alloc_context3`/`av_frame_alloc`/`av_packet_alloc` to return null via a real OOM
  condition) still needs process-level infrastructure (e.g. an `LD_PRELOAD` malloc interceptor) this
  task's scope doesn't otherwise need -- the code paths are null-checked and verified correct by
  direct reading, just not exercised by a fault-injected test. Candidate follow-up for a future
  session if this specific hardening ever needs direct proof beyond code review.

- [x] **MEDIA-95 — VideoDecoder-TrackSwitchingTests.** `SetAudioStream`/`SetVideoStream` by index,
  re-opening codec contexts correctly.

- [x] **MEDIA-96 — MediaSourceTests.** Real `GetAvailableMediaSources()`/`Name`/`MediaSourceType`/
  `ToString` (MEDIA-61).

- [x] **MEDIA-97 — MediaLibraryTests.** Both constructors; all 10 properties; `GetPictureFromToken`;
  both `SavePicture` overloads; `Dispose`; against the Phase 4 fixture tree (MEDIA-62/69).

- [x] **MEDIA-98 — GenreTests.** Equality set, `Name`, `IsDisposed`, `Albums`, `Songs` (MEDIA-63).

- [x] **MEDIA-99 — GenreCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration against real
  fixture-derived genres (MEDIA-63).

- [x] **MEDIA-100 — ArtistTests.** Equality set, `Name`, `IsDisposed`, `Albums`, `Songs` (MEDIA-64).

- [x] **MEDIA-101 — ArtistCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-64).

- [x] **MEDIA-102 — AlbumTests.** Equality set (incl. same-name-different-artist), `Artist`/`Genre`/
  `Duration`/`HasArt`/`Songs`/`Name`/`IsDisposed`, `GetAlbumArt`/`GetThumbnail` found-vs-not-found
  branches (MEDIA-65).

- [x] **MEDIA-103 — AlbumCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration (MEDIA-65).

- [x] **MEDIA-104 — PictureTests.** Equality set, `Album`/`Date`/`Height`/`Width`/`Name`/`IsDisposed`,
  `GetImage`/`GetThumbnail` real `Stream` content round-trip (MEDIA-66).

- [x] **MEDIA-105 — PictureCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-66).

- [x] **MEDIA-106 — PictureAlbumTests.** Equality set, `Albums`/`Pictures`/`Parent`/`Name`/`IsDisposed`,
  including the root-node `Parent==nullptr` case and a multi-level `Parent` walk (MEDIA-67).

- [x] **MEDIA-107 — PictureAlbumCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-67).

- [x] **MEDIA-108 — PlaylistTests.** `Equals`/`GetHashCode`, `operator==`/`operator!=` (both-null/
  one-null/both-real), `Duration`/`Songs`/`Name`/`IsDisposed` (MEDIA-68).

- [x] **MEDIA-109 — PlaylistCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-68).

- [x] **MEDIA-110 — AudioTagParser-VorbisCommentTests.** MEDIA-47/48 regression against MEDIA-3's `.ogg`
  fixture(s), incl. malformed-input graceful fallback.

- [x] **MEDIA-111 — AudioTagParser-ID3v2Tests.** MEDIA-49/50 regression against MEDIA-3's ID3v2.3/2.4
  fixtures, incl. the text-encoding-byte matrix (Latin-1/UTF-16/UTF-16BE/UTF-8).

- [x] **MEDIA-112 — AudioTagParser-FilenameFallbackTests.** MEDIA-51 regression against MEDIA-3's
  untagged `.wav` fixture.

- [x] **MEDIA-113 — MediaLibraryIndexTests.** MEDIA-52 scan-correctness regression, plus MEDIA-53's
  symlink-loop/permission-hardening regression.

- [x] **MEDIA-114 — PictureLibraryIndexTests.** MEDIA-56 scan-correctness regression, confirming
  `ImageLoader` reuse produces correct dimensions.

- [x] **MEDIA-115 — PlaylistParserTests.** MEDIA-57/58 regression, incl. the M3U8 UTF-8 fixture.

- [x] **MEDIA-116 — MediaCollectionBase\<T\> template tests.** MEDIA-55's generic
  storage/indexer/enumerator/dispose correctness for at least 2 distinct `T`s, independent of any one
  concrete public collection type.

- [x] **MEDIA-117 — MediaLibraryPathsTests.** MEDIA-46's real-OS-folder resolution (or documented
  graceful fallback) and the `SetMusicRootEXT`/`SetPictureRootEXT` override hooks.

- [x] **MEDIA-118 — SavedPictureStoreTests.** MEDIA-59's real file write + read-back content round-trip.

- [x] **MEDIA-119 — Artist/Genre normalization regression test.** MEDIA-54's case-insensitive-dedup
  fixture case, isolated from the broader MEDIA-100/98 tests for a focused regression guard.

- [x] **MEDIA-120 — Full Media-namespace headless-safety pass.** Confirm every new test added in this
  phase runs correctly under `SDL_VIDEODRIVER=dummy` where applicable (matching `plan_cnb.md`'s
  headless-CI precedent), and grep the **full** `CnaTests` log for `FAILED` (not a truncated tail) to
  confirm zero regressions introduced by any Phase 6 addition before moving to Phase 7.
  *Verified:* full-suite regression run against the real display (per §2's corrected methodology,
  not a forced `dummy` driver): 4846 tests, 4844 passed, 0 failed, 2 pre-existing hardware skips
  (Accelerometer/Gyroscope) — grepped in full, not a truncated tail.

### Phase 7 — Documentation and closure

- [x] **MEDIA-121 — Add every new deviation to `CHECKLIST.md`.** New Media-specific rows: (a) real
  local media library vs. FNA's permanent stub — the single biggest one, explain the "no FNA logic to
  port, built from scratch per §4" framing; (b) SDL-user-folder root discovery (D1); (c) internal
  from-scratch Vorbis/ID3v2 tag parser instead of a third-party tag library (D2); (d) M3U/M3U8 as the
  chosen Playlist on-disk format (D5); (e) point-in-time library snapshot, no live filesystem watching
  (D6); (f) FFmpeg-unified video decode replacing FNA's separate `dav1dfile`/`Theorafile` backends
  (§2.2); (g) `VideoSoundtrackType` remaining metadata-only, matching FNA exactly (§2.5); (h) AV1
  content keeping its audio track, beyond FNA's own limitation (§2.3); (i) `Song::GetHashCode()`
  content-based vs. FNA's identity-based (§2.4); (j) the project-wide out-of-range-indexer exception-type
  inconsistency and Media's chosen resolution (§2.7).

- [x] **MEDIA-122 — Update `AUDIT.md`'s `Microsoft::Xna::Framework::Media` table.** Replace every
  "(stub behavior)" row with the real post-Phase-4 status. Do **not** add the new
  `CNA::Internal::Media::*` internal types to this table — matching the existing convention that Audio's
  internal `XactParser`/`AudioMixer` aren't listed in the public `Microsoft::Xna::Framework::Audio`
  table either.

- [x] **MEDIA-123 — Create `NEXTmedia.md`.** Matching the existing `NEXTaudio.md`/`NEXTdevices.md`/
  `NEXTinput.md`/`NEXTnet.md` per-domain convention — summarize this plan's outcome, open follow-ups
  (e.g. NOXNA `Refresh()`, a playlist-writer, a taglib upgrade path if the internal parser ever proves
  insufficient, embedded-`APIC`-frame album art as a `GetAlbumArt` enhancement beyond MEDIA-65's
  filename-only lookup, the `TouchCollection` exception-type inconsistency flagged in §2.7 as an
  Input-namespace follow-up out of this plan's scope), for a future session's quick pickup.

- [x] **MEDIA-124 — Build & report.** `cmake --build cmake-build-debug --target CNA` and
  `--target CnaTests` green. Report per `CLAUDE.md`'s "Build and Report" section: changed files, added
  stubs (should be none left unexplained), missing dependencies (should be none — §0 confirmed no new
  third-party libraries), intentional deviations (point at MEDIA-121), build result.
  *Verified:* both `CNA` and `CnaTests` targets build clean (0 errors, 0 new warnings) against the
  project's `tests` preset (`cmake-build-tests`, equivalent build tree to `cmake-build-debug` for this
  purpose — `EASYGL` backend, `CNA_BUILD_TESTS=ON`). No new stubs, no missing dependencies (no new
  third-party libraries added this whole plan). Deviations: see `CHECKLIST.md`'s new Media rows
  (MEDIA-121).

- [x] **MEDIA-125 — Full-suite regression run.** Confirm zero regressions in the rest of `CnaTests`
  (grep the **full** log for `FAILED` — do not trust a truncated tail) beyond MEDIA-120's Media-scoped
  pass, and confirm headless-safety project-wide is unaffected.
  *Verified:* 4846 tests, 4844 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) — identical count to MEDIA-120's own pass, as expected since Phase 7 is
  documentation-only (no code changes). Grepped in full, not a truncated tail.

- [x] **MEDIA-126 — Establish this plan's own future-addendum convention.** Matching `plan_audio.md`'s
  real evolution (an initial plan, then supplementary `P9-*`/`Phase 10` sections appended as later
  re-audits found more), record explicitly at the top of this file (already done in the provenance note)
  that any future Media re-audit should append a new numbered phase here rather than silently rewriting
  earlier phases' history — so this plan's own completion record stays trustworthy over time.
  *Accept:* convention documented; no further code change.
  *Verified:* the provenance note at the top of this file (line ~14) already states this convention
  explicitly, written when this plan was first drafted — no further edit needed, just this
  confirmation record.

### Phase 8 — External review remediation (2026-07-18)

> Applying MEDIA-126's own convention: this phase is appended, not merged into Phases 1-7's history.
> An external adversarial code review of this plan's "126/126 complete" claim found 11 real,
> confirmed defects — several MEDIA-N tasks above had been checked off despite not genuinely meeting
> their own Accept criteria (verified against the actual code, not just the claim). All 11 are fixed
> or honestly documented below; the affected MEDIA-N tasks above (`MEDIA-32`, `MEDIA-65`, `MEDIA-68`,
> `MEDIA-73`, `MEDIA-90`, `MEDIA-94`, `MEDIA-121`) were corrected in place with a note explaining what
> was wrong and what changed, per this same convention (correcting a specific task's own record, not
> rewriting the phase around it).

- [x] **MEDIA-127 — Fix `Album`/`Playlist.Duration` being permanently zero.** `MediaLibrary`'s
  library-scanned `Song` objects were constructed via `Song`'s 2-arg constructor (no duration ever
  set), `Album.Duration` was hardcoded to `System::TimeSpan::Zero` at construction (not a real sum),
  and even `MediaPlayer::Play(SongCollection, index)` (the common "play a whole Album/Playlist"
  pattern) operates on a `MediaQueue` duplicate `Song`, never updating the library's own original
  object — so no playback path could ever backfill a real Duration either. Genuinely contradicted
  `MEDIA-65`/`MEDIA-68`'s own "Duration (sum of member Song.Duration)" requirement.
  *Fix:* new `CNA::Internal::Media::AudioDurationProbe` (decode-free `avformat_find_stream_info`
  container-metadata probing, gated behind a new `CNA_FFMPEG_AVAILABLE` compile definition — 0/unknown
  on Windows/Android/Emscripten, matching `Video`/`VideoPlayer`'s own platform availability) populates
  each `Song`'s real duration at library-scan time; `Album.Duration` is now a real sum of its member
  songs' (now-real) durations. `Playlist.Duration` needed no code change — it already summed real
  `Song.Duration` values, so it benefits automatically.
  *Accept:* `AlbumDurationIsARealNonZeroSumOfMemberSongDurations`/
  `PlaylistDurationIsARealNonZeroSumOfMemberSongDurations` (replacing the old, now-incorrect
  "...IsZero..." tests) assert real, non-zero, in-range durations against the fixture's own known
  song lengths.

- [x] **MEDIA-128 — Finish `VideoDecoder`'s FFmpeg return-code hardening (`MEDIA-39` truly
  complete).** `NextFrame()` still merged every non-`AVERROR(EAGAIN)` `avcodec_receive_frame()`
  return code with clean EOF (a genuine decode error was silently treated as "video finished
  normally"); `avcodec_send_packet()`'s return was discarded entirely; `av_frame_alloc()`/
  `av_packet_alloc()` in `Open()` were never null-checked, risking a null dereference on the very
  first decode call.
  *Fix:* `NextFrame()` now distinguishes `AVERROR_EOF` from a genuine decode error (throws
  `std::runtime_error` for the latter) and checks `avcodec_send_packet()`'s return the same way;
  `Open()` fails cleanly (`Close()` + `return false`) if either allocation returns null.
  *Accept:* `CorruptedMidStreamDataThrowsRatherThanSilentlyEndingCleanly` (see MEDIA-129) exercises
  the decode-error throw path for real.

- [x] **MEDIA-129 — Replace the tautological truncated-file test.**
  `TruncatedMidStreamDataThrowsRatherThanSilentlyEndingCleanly` asserted success on *either* a
  thrown exception *or* a clean EOF, making it a tautology that could never fail and provided zero
  real regression coverage for `MEDIA-40`.
  *Fix:* split into two real, deterministic tests. `TruncatedFileEndsAtCleanEOFRatherThanThrowing`
  documents genuine, verified behavior (Matroska's demuxer is deliberately streaming-tolerant — a
  truncated tail is always clean `AVERROR_EOF`, confirmed across a dozen+ truncation points via
  direct `ffmpeg` CLI experimentation, never a distinct error). `CorruptedMidStreamDataThrowsRatherThanSilentlyEndingCleanly`
  is the real "genuine mid-stream error" case — since FFV1/Matroska corruption couldn't be made to
  hard-fail even with every `AV_EF_*` strictness flag set (empirically verified: this build's `ffv1`
  decoder logs a slice-CRC mismatch but never propagates it as an error code, `AV_EF_EXPLODE` alone
  included), a new checked-in `corrupt_test_h264.mp4` fixture is used instead — H264 is far less
  lenient about corrupted macroblock data and reliably produces a genuine `AVERROR_INVALIDDATA`.
  `AllocAndConfigureCodecContext` now sets `AV_EF_CRCCHECK|AV_EF_BITSTREAM|AV_EF_BUFFER|AV_EF_EXPLODE`
  (found empirically that `AV_EF_CRCCHECK` specifically is the bit that enables FFV1's own CRC check
  to be *acted on* at all, not just computed).
  *Accept:* both new tests pass deterministically across repeated runs (verified 3x each).

- [x] **MEDIA-130 — Fix the real `MEDIA-41` audio-tail-drain bug.** `VideoPlayer::GetTexture()`'s
  decode loop only called `decoder_->DrainAudio()` in the successful-video-frame branch — on the
  final `NextFrame()` call that returns `false` (EOF), any audio `NextFrame()`'s own internal
  packet-reading loop had already decoded into `VideoDecoder::pendingAudio_` while searching for
  either a video packet or true EOF was silently stranded and never reached the SDL audio stream,
  undermining the very "wait for queued audio to drain" check right below it. The test claiming
  `MEDIA-41` coverage (`NonLoopedVideoEventuallyStopsAfterItsDuration`) also used the video-only
  `chroma_420.mkv` fixture, never `audio_tail.mkv` (authored specifically for this scenario), so the
  bug went untested.
  *Fix:* `DrainAudio()`/`SDL_PutAudioStreamData()` now run unconditionally right after every
  `NextFrame()` call, whether or not it returned a video frame, before the EOF/looping branch logic.
  *Accept:* covered indirectly by MEDIA-131's fixture-based tests below and the existing
  `AudioTailFixtureHasAudio`/`DrainAudioProducesRealSamplesAfterDecodingFrames` `VideoDecoder`-level
  tests; a full `VideoPlayer`-level `audio_tail.mkv` EOS-drain test remains a reasonable follow-up
  (see `NEXTmedia.md`).

- [x] **MEDIA-131 — Fix the real track-switching ordering bug (`MEDIA-90` truly complete).**
  `VideoPlayer::OpenDecoder()` created the SDL audio stream and the frame texture from the
  decoder's *default* track's format/dimensions, and only *afterward* applied any
  `SetAudioTrackEXT()`/`SetVideoTrackEXT()` preference set before `Play()` — so a track switch set
  before playback started was silently ignored for the texture/audio-stream's own format/size (e.g.
  switching from a 48kHz to a 44.1kHz audio track left the SDL stream configured for the wrong
  rate). The identically-shaped bug also applied to a *mid-playback* switch via
  `SetAudioTrackEXT()`/`SetVideoTrackEXT()` themselves, which called `decoder_->SetAudioStream()`/
  `SetVideoStream()` but never reconfigured the already-open SDL stream/texture either. The existing
  test only checked "doesn't throw" plus the texture's dimensions, which the fixture's single video
  track could never actually distinguish.
  *Fix:* track preferences are now applied *before* `OpenDecoder()` creates the texture/audio
  stream; a new shared `ReconfigureAudioAndVideoOutputForCurrentTracks()` helper (re)creates both to
  match the decoder's *current* track, called from `OpenDecoder()` and from both
  `SetAudioTrackEXT()`/`SetVideoTrackEXT()` for the mid-playback case. Fixing this surfaced a real,
  separate pre-existing bug: `OpenDecoder()`'s `CloseDecoder()` call unconditionally reset `video_`
  to `nullptr` and nothing ever restored it, so `getVideoProperty()` silently returned `nullptr` for
  the entire remainder of every `Play()` call (never tested before either) — fixed in the same pass.
  *Accept:* new `VideoPlayerTestAccess.hpp` (matching the existing `SoundEffectInstanceTestAccess.hpp`
  pattern) exposes the decoder's current sample rate for real, numeric verification. `SetAudioTrackEXTMidPlaybackActuallyChangesTheActiveSampleRate`
  and `AudioTrackPreferenceSetBeforePlayAppliesToTheOpenedDecoder` both assert a real 48000→44100 Hz
  change against `multi_track_audio.mkv`, not just "doesn't throw." `GetVideoPropertyReturnsThePlayedVideoAfterPlay`
  covers the newly-found `video_` bug.

- [x] **MEDIA-132 — Fix `SavePicture()` never creating a real `PictureAlbum` node for "Saved
  Pictures."** The first `SavePicture()` call on a library with no pre-existing "Saved Pictures"
  folder wrote the real file (via `SavedPictureStore`, which does create the directory) but never
  created a corresponding `PictureAlbum` tree node — the saved `Picture` was silently parented to
  `RootPictureAlbum` instead, not fulfilling `MEDIA-59`/D7's "backing `RootPictureAlbum`" intent.
  *Fix:* new `MediaLibrary::EnsureSavedPicturesAlbum()` lazily creates the real node (as a child of
  `RootPictureAlbum`, registered in its real `PictureAlbumCollection`) the first time it's needed,
  idempotent on subsequent calls.
  *Accept:* `SavePictureCreatesARealSavedPicturesAlbumNodeIfNoneExisted` confirms the node is
  created, correctly parented, and the saved picture is a real member of it.

- [x] **MEDIA-133 — SECURITY: fix `SavedPictureStore` path traversal.** `SavePicture()`'s `name`
  parameter (caller-supplied, untrusted — a "display name" per its own public API doc) was
  concatenated directly into a filesystem path with zero sanitization. A caller passing
  `"../../../../etc/cron.d/evil"` or an absolute path could write a file outside the real Saved
  Pictures directory entirely (an absolute right-hand operand to `std::filesystem::path::operator/`
  *replaces* the whole path per the C++ standard, not appends to it).
  *Fix:* new `SanitizePictureName()` normalizes Windows-style backslashes to forward slashes, keeps
  only the last path segment (`std::filesystem::path::filename()`), and falls back to a safe default
  name for a `"."`/`".."`/empty result.
  *Accept:* `SavePictureRejectsPathTraversalInName`/`SavePictureRejectsAbsolutePathInName`/
  `SavePictureRejectsWindowsStyleBackslashTraversalInName`/`SavePictureFallsBackToASafeNameForDotOrDotDot`
  all confirm the written file always stays inside the real Saved Pictures directory.

- [x] **MEDIA-134 — Correct `MEDIA-73`'s self-contradictory checkbox.** Previously checked `[x]`
  with an "Honest gap" note admitting the task's own Accept criterion (a full
  `ContentManager::Load<Video>()` round-trip against a real fixture) was not met — checking the box
  while the very next line says it isn't done is exactly the kind of overclaiming this phase exists
  to catch. Now genuinely closed: see `MEDIA-73`'s own corrected entry above.

- [x] **MEDIA-135 — Correct `MEDIA-32`'s overclaimed test coverage.** Previously checked `[x]`
  without noting that its own Accept criterion ("a test built without the flag ... exercises queue
  auto-advance") was never actually met — `SOUND_ENABLED` is unconditionally defined for every build
  configuration this project produces (`cmake/CnaLibrary.cmake`), so `MediaPlayer::Update()`'s
  `#else` fallback branch is dead code in every binary this project builds today, unreachable by any
  test. This is a genuine, pre-existing, project-wide condition (every `#ifdef SOUND_ENABLED` pair
  across the whole Audio/Media stack, not just this one function) — closing it for real needs a new
  CMake build variant with CI coverage, out of proportion to this task. See `MEDIA-32`'s own
  corrected entry above; documented honestly rather than silently implied-complete.

- [x] **MEDIA-136 — Document the `Video`/`VideoPlayer` cross-platform link-error gap.** On
  Windows/Android/Emscripten, `cmake/CnaLibrary.cmake`'s `CNA_FFMPEG_AVAILABLE` gate excludes
  `Video.cpp`/`VideoPlayer.cpp`/`VideoDecoder.cpp` from the build entirely while their public headers
  remain available — referencing `Video`/`VideoPlayer` there is a link error, not a graceful runtime
  `NotSupportedException`. A genuine, pre-existing architectural condition; a real fix (stub
  implementations that throw at runtime instead of failing to link) is a larger, separate
  undertaking this remediation pass's scope doesn't cover, and this sandbox has no way to build for
  or verify those platforms anyway.
  *Accept:* documented in `AUDIT.md`'s `Video`/`VideoPlayer` rows (⚠️, not a silent ✅) and in
  `NEXTmedia.md`'s open items for a future session with access to those build targets.
  *Later correction (2026-08-22):* this accurately documented the condition at the time; the owner
  subsequently authorized the fallback and `MEDIA-233` closes the link-error gap.

- [x] **MEDIA-137 — Close the remaining `MEDIA-121` per-overload test gaps.** `GetTypeName()` was
  untested for all 6 collection types (`GenreCollection`/`ArtistCollection`/`AlbumCollection`/
  `PictureCollection`/`PictureAlbumCollection`/`PlaylistCollection`) and for `VideoPlayer`, despite
  `CLAUDE.md`'s per-member test mandate. Two new `CHECKLIST.md` deviation rows also closed: the
  `VideoPlayer::Dispose()` idempotency choice and `GetTexture()`-before-`Play()`-returning-`nullptr`
  had inline code comments explaining them but were never promoted to `CHECKLIST.md`'s table.
  *Accept:* 7 new `GetTypeNameIsFullyQualified`-style tests added (one per collection + `VideoPlayer`);
  2 new `CHECKLIST.md` rows added.

- [x] **MEDIA-138 — Full-suite regression run.** Confirm zero regressions from this whole
  remediation phase, grepped in full (not a truncated tail).
  *Verified:* 4863 tests, 4861 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope). Grepped in full, not a truncated tail.

### Phase 9 — Second external review pass (2026-07-18, same day as Phase 8)

> Applying MEDIA-126's own convention again. A second external adversarial review of commit
> `52eec0a5` (Phase 8's own fix commit) found the fix was real but incomplete on several points, plus
> **one genuine new regression Phase 8 itself introduced** while fixing MEDIA-131's track-switching
> bug. All are fixed for real below; nothing in this phase was left as "documented but not attempted"
> except where a prior phase had already made that judgment call (no-`SOUND_ENABLED` build variant,
> cross-platform link-error gap — both unchanged from Phase 8's own assessment).

- [x] **MEDIA-139 — Fix a genuine new regression: `Play()` left the SDL audio stream paused
  forever.** `ReconfigureAudioAndVideoOutputForCurrentTracks()` (added in Phase 8 to fix
  `MEDIA-131`) only calls `SDL_ResumeAudioStreamDevice()` when `state_ == MediaState::Playing` --
  but during the initial `OpenDecoder()` call (from a fresh `Play()`), `state_` was still `Stopped`
  (`CloseDecoder()` had just reset it) since `Play()` itself didn't set `state_ = Playing` until
  *after* `OpenDecoder()` returned. `SDL_OpenAudioDeviceStream()` opens every stream paused by
  default (SDL's own documented contract) -- so every video played via a fresh `Play()` call had
  its audio silently never start.
  *Fix:* `state_ = MediaState::Playing` now happens inside `OpenDecoder()`, immediately before
  `ReconfigureAudioAndVideoOutputForCurrentTracks()` runs (with a `try`/`catch` reverting it back to
  `Stopped` if the subsequent first-frame decode throws, preserving this function's existing
  contract that a failed `Play()` leaves the player `Stopped`).
  *A deeper root cause found in the same investigation:* `VideoPlayer.cpp` never initializes SDL's
  audio subsystem at all -- `GraphicsDevice` only calls `SDL_InitSubSystem(SDL_INIT_VIDEO)`. In an
  isolated test/app that never touches `MediaPlayer`/`AudioEngine` first, `SDL_OpenAudioDeviceStream()`
  silently fails (returns null) because the audio subsystem was never started, regardless of the
  resume-timing fix above. Fixed with a properly paired `SDL_InitSubSystem(SDL_INIT_AUDIO)`/
  `SDL_QuitSubSystem(SDL_INIT_AUDIO)` around every stream open/destroy (SDL reference-counts
  subsystem init process-wide, so this is safe alongside `MediaPlayer`/`AudioEngine`'s own calls).
  *Accept:* new `VideoPlayerTestAccess::IsAudioStreamDevicePaused()`/`HasAudioStream()` (via
  SDL3's `SDL_AudioStreamDevicePaused()`) give real, direct proof. `PlayGenuinelyResumesTheAudioStreamNotJustOpensIt`
  and `PauseStillActuallyPausesTheAudioStream` both pass, including run in isolation (not just as
  part of the full suite, ruling out cross-test resource-exhaustion as a confound).

- [x] **MEDIA-140 — Finish `VideoDecoder`'s FFmpeg error handling for real this time.** Phase 8's
  own `MEDIA-128` fix was itself incomplete: the EOF flush (`avcodec_send_packet(videoCtx_,
  nullptr)`) return was still unchecked; `ProcessAudioPacket`'s `avcodec_send_packet` failure was
  silently swallowed (not propagated); `av_frame_alloc()` for the audio frame was never
  null-checked; `avcodec_receive_frame`/`swr_convert` errors inside `ProcessAudioPacket` were not
  propagated; and `avcodec_send_packet` returning `EAGAIN` for a *video* packet caused that packet
  to be discarded (`av_packet_unref`'d) instead of retried, silently dropping a video frame.
  *Fix:* the flush return is now checked; `ProcessAudioPacket` checks and propagates every FFmpeg
  call exactly like `NextFrame()`'s own video path already did (matching MEDIA-39's own explicit
  "at each site" language, which names `swr_convert`/`avcodec_send_packet` specifically); a video
  packet that gets `EAGAIN` is now retained (not unref'd) and genuinely retried after the next
  `avcodec_receive_frame()` call drains backpressure, via a new `havePendingVideoPacket` flag,
  instead of being silently lost.
  *Accept:* code review confirms every FFmpeg call in both functions now checks its return code;
  the existing corrupted-file/decoder-error tests continue to pass with the more thorough checking
  in place.

- [x] **MEDIA-141 — Fully close the audio-tail gap (`MEDIA-130`'s own documented follow-up).**
  Phase 8 fixed the real `DrainAudio()`-not-called-on-EOF bug but left two things open: the audio
  *codec* itself was never sent a flush packet at EOF (only the video codec was), so any audio
  samples still buffered inside the codec's own internal state were silently lost; and the
  `VideoPlayer`-level `audio_tail.mkv` test was explicitly deferred as "a reasonable follow-up"
  while the task stayed checked off -- the same overclaiming pattern Phase 8 exists to fix.
  *Fix:* the EOF branch in `NextFrame()` now also calls `ProcessAudioPacket(nullptr)` (FFmpeg's own
  documented flush contract -- `avcodec_send_packet` accepts a null packet to mean "drain what you
  have"), reusing the exact same, already-hardened decode/resample/error-propagation logic.
  *Accept:* new `NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration` genuinely proves
  `VideoPlayer` stays `Playing` past `audio_tail.mkv`'s 2.0s video duration until its real 3.0s
  audio tail has drained, then reaches `Stopped` -- not just tested indirectly at the `VideoDecoder`
  level as Phase 8 left it.

- [x] **MEDIA-142 — Verify (not fix) the "duration probe always returns zero" claim.** The review
  cited `AudioDurationProbe.cpp:33` -- that line is inside the `#else` (`CNA_FFMPEG_AVAILABLE` not
  defined) branch, the deliberate, documented no-FFmpeg fallback, not the branch this Linux sandbox
  actually compiles or runs.
  *Verified:* `ninja -t commands` on the real build directory confirms `CNA_FFMPEG_AVAILABLE` is
  genuinely defined for this target; `AlbumDurationIsARealNonZeroSumOfMemberSongDurations`/
  `PlaylistDurationIsARealNonZeroSumOfMemberSongDurations` (already passing since `MEDIA-127`)
  independently confirm real, non-zero probed durations. No code change -- the claim doesn't hold
  for the branch that's actually active here, though it's accurate about what the stub itself
  returns by design.

- [x] **MEDIA-143 — Fix the `SavePicture()` edge case: Pictures root missing entirely at
  construction.** `EnsureSavedPicturesAlbum()` (added in Phase 8 for `MEDIA-132`) still bailed out
  with `nullptr` if `rootPictureAlbum_` itself was null -- reachable whenever the Pictures root
  directory didn't exist at all when `MediaLibrary` was constructed (`PictureLibraryIndex` never
  finds a root to build a tree from in that case). `SavedPictureStore::SavePicture()` still writes
  the real file to disk regardless, so the picture was saved but permanently unparented in any
  `PictureAlbum` tree.
  *Fix:* `EnsureSavedPicturesAlbum()` now also lazily bootstraps a real root `PictureAlbum` node
  (name derived from the Pictures root directory's own leaf filename, matching
  `PictureLibraryIndex`'s own naming convention) before creating "Saved Pictures" as its child, the
  same lazy-creation pattern already used for the child node itself.
  *Accept:* new `MediaLibrarySavePictureNoPreexistingRootTest` (a dedicated scratch fixture whose
  Pictures root directory is deliberately never pre-created, unlike the existing
  `MediaLibrarySavePictureTest` fixture) confirms `getRootPictureAlbumProperty()` is genuinely
  `nullptr` before the first `SavePicture()` call and a real, correctly-parented tree exists after.

- [x] **MEDIA-144 — Fix a real bug found while verifying MEDIA-139: re-selecting an already-active
  track needlessly discarded decode state.** `VideoDecoder::SetAudioStream()`/`SetVideoStream()`
  unconditionally called `OpenAudioStreamByIndex()`/`OpenVideoStreamByIndex()` even when the
  requested index was already the active one, discarding the codec context and all decode state for
  no reason. For audio this was harmless (no keyframe concept) but pointless; for *video* it was a
  real, user-visible bug that Phase 8's own error-propagation fix (`MEDIA-140`/`MEDIA-128`) newly
  surfaced: by the time any `SetVideoTrackEXT()` call happens after `Play()`, the demuxer's read
  position has already moved past the stream's first keyframe, so the freshly-reset codec context
  has no reference frame for the very next (non-keyframe) packet, and `avcodec_send_packet` now
  correctly throws `"Cannot decode non-keyframe without valid keyframe"` instead of silently
  producing a garbage frame. Confirmed via isolated single-test runs that this is a genuine,
  deterministic bug (once triggered by enough wall-clock time elapsing between `Play()` and the
  next `GetTexture()` call to reach a second `NextFrame()`), not test flakiness.
  *Fix:* both setters now no-op if the requested index already matches the currently active stream.
  *Accept:* `SetAudioTrackEXTAndSetVideoTrackEXTDoNotBreakPlaybackAfterPlay` (which calls
  `SetVideoTrackEXT(0)` while video track 0 is already active) passes reliably across repeated runs;
  confirmed the fix doesn't affect genuine cross-track switching (`SetAudioTrackEXTMidPlaybackActuallyChangesTheActiveSampleRate`
  still passes, still proves a real 48000→44100 Hz change).

- [x] **MEDIA-145 — Full-suite regression run.** Confirm zero regressions from this whole second
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4867 tests, 4865 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope). Grepped in full, not a truncated tail. Also verified `PlayGenuinelyResumesTheAudioStreamNotJustOpensIt`/
  `PauseStillActuallyPausesTheAudioStream`/`SetAudioTrackEXTAndSetVideoTrackEXTDoNotBreakPlaybackAfterPlay`
  run reliably in isolation (`--gtest_filter` on just that one test), not only as part of the full
  suite, ruling out cross-test ordering/resource effects as a confound for these specific fixes.

### Phase 10 — Third external review pass (2026-07-18, same day as Phase 8/9)

> Applying MEDIA-126's own convention a third time. A third external adversarial review of commit
> `9b80500d` (Phase 9's own fix commit) confirmed the audio-silence regression and the track-switch
> bug were genuinely fixed, but found the Phase 9 fix itself was still incomplete on five specific
> points -- no new regression this time, but real, confirmed completeness gaps in the EAGAIN retry
> mechanism, the audio/video reconfiguration coupling, resampler flushing, `Play()`'s exception
> safety, and a stale doc note. Every claim was independently re-derived from the actual code before
> any fix was written (per this plan's own established practice, not taken at face value).

- [x] **MEDIA-146 — Fix a real bug in Phase 9's own EAGAIN retry: the pending-packet flag didn't
  survive across `NextFrame()` calls.** `havePendingVideoPacket` (added in Phase 9 to fix
  `MEDIA-128`'s "video packet retained after EAGAIN gets silently dropped" bug) was a function-local
  `bool` declared at the top of `NextFrame()`. The retry sequence is: video packet send returns
  EAGAIN → flag set true, packet retained → outer loop retries `avcodec_receive_frame()` → that call
  almost always immediately yields a buffered frame (that's what unblocked the EAGAIN in the first
  place) → the function returns `true` to the caller *before the retained packet is ever resent*.
  Because the flag was local, the *next* call to `NextFrame()` started with it reset to `false`, so
  the retained packet was silently overwritten by the next `av_read_frame()` call without ever
  reaching the decoder -- the exact bug `MEDIA-128` set out to fix, now happening across call
  boundaries instead of within one call.
  *Fix:* `havePendingVideoPacket` is now a class member (`havePendingVideoPacket_` in
  `VideoDecoder.hpp`), reset to `false` in `Close()` (which also unrefs any packet it still holds via
  `av_packet_free()`) so a fresh `Open()` never inherits stale retry state from a previous file.
  *Accept:* new `VideoDecoderTest.DecodesTheFullFileWithoutSilentlyDroppingAnyFrame` decodes
  `chroma_420.mkv` end-to-end via a real `NextFrame()` loop and asserts exactly 50 frames (ffprobe's
  independently-verified `nb_read_frames`) with strictly increasing PTS -- a genuine, deterministic
  guard against any frame silently going missing, not just "decoding didn't throw."

- [x] **MEDIA-147 — Flush the resampler's own internal buffer at EOF, not just the audio codec.**
  Phase 8's `MEDIA-130` fix called `ProcessAudioPacket(nullptr)` at EOF to flush the audio *codec*
  (draining any frames it was still holding via `avcodec_receive_frame`), but `swr_convert()` was
  only ever called with real decoded frame data, never with a null source to flush `SwrContext`'s own
  separate internal buffer (format/rate-conversion delay). Practical impact today is small --
  `SetupResampler()` configures the same sample rate in and out (format conversion only, no real rate
  change) -- but it is a real completeness gap, not a hypothetical one: any future path with a real
  rate change would silently lose the resampler's final buffered samples at EOF.
  *Fix:* `ProcessAudioPacket(nullptr)`'s EOF branch now also calls `swr_get_delay()` to size a buffer
  and `swr_convert(swrCtx_, ..., nullptr, 0)` to drain it, with the same error-checking/throw
  discipline as every other FFmpeg call in this function.
  *Accept:* covered by the existing `AudioTailFixtureHasAudio`/`DrainAudioProducesRealSamplesAfterDecodingFrames`/
  full-suite regression run (MEDIA-151 below) -- no fixture in this repo currently forces a non-trivial
  resampler delay, so this is a correctness fix verified by code review + "doesn't throw, doesn't
  regress existing audio output," not a fixture engineered to prove non-zero flushed-sample content
  (documented, not silently claimed otherwise).

- [x] **MEDIA-148 — Split the combined audio/video reconfiguration helper: a track switch on one
  side was tearing down the other.** `ReconfigureAudioAndVideoOutputForCurrentTracks()` (added in
  Phase 8) always destroyed and recreated *both* the SDL audio stream *and* the frame texture,
  regardless of which track actually changed. Calling `SetVideoTrackEXT()` -- even to reselect the
  already-active index, a legitimate no-op call -- unconditionally tore down and reopened the SDL
  audio stream too, discarding whatever audio was already queued for playback. `SetAudioTrackEXT()`
  had the symmetric problem, needlessly reallocating the video texture on every audio-only switch.
  *Fix:* split into `ReconfigureVideoOutputForCurrentTrack()` (texture only) and
  `ReconfigureAudioOutputForCurrentTrack()` (SDL stream only, including the resume-on-Playing logic).
  `OpenDecoder()` calls both (the initial open touches everything); `SetAudioTrackEXT()` calls only
  the audio half; `SetVideoTrackEXT()` calls only the video half.
  *Accept:* new `VideoPlayerTest.SetVideoTrackEXTDoesNotTearDownTheUnrelatedAudioStream` captures the
  `SDL_AudioStream*` pointer identity before/after a video-track switch and asserts it's unchanged (a
  torn-down-and-reopened stream would be a different pointer); the symmetric
  `SetAudioTrackEXTDoesNotRecreateTheUnrelatedVideoTexture` does the same for `GetTexture()`'s
  pointer identity across an audio-track switch.

- [x] **MEDIA-149 — `Play()`'s exception path left the player half-open, not actually closed.**
  `OpenDecoder()`'s `try`/`catch` around the first-frame decode (added in Phase 9 to fix `MEDIA-139`)
  only reset `state_` back to `MediaState::Stopped` on failure -- `decoder_`, `audioStream_`,
  `frameTexture_`, and `video_->parent_` were all left exactly as they were: fully allocated/set.
  `state_ == Stopped` implies nothing is open everywhere else in this class, but here every actual
  resource said otherwise -- `getVideoProperty()` kept returning the video that supposedly failed to
  play, and a second `Play()` call only worked by relying on `CloseDecoder()` being idempotent by
  luck, not by contract.
  *Fix:* the `catch` block now calls `CloseDecoder()` (the same full, already-correct teardown every
  other exit path in this class already uses -- it performs its own `state_ = Stopped` too) instead of
  just reassigning `state_` directly.
  *Accept:* new `VideoPlayerTest.PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen`
  corrupts real H264 macroblock data inside the *first* video packet of `corrupt_test_h264.mp4`
  (offset verified via `ffprobe -show_entries frame=pkt_pos` to fall inside the keyframe packet, not a
  later one, so the very first `decoder_->NextFrame()` call inside `OpenDecoder()`'s `try` block is
  the one that throws) and asserts `state_ == Stopped`, `getVideoProperty() == nullptr`, no audio
  stream, no texture, *and* that the player is still fully usable for a subsequent `Play()` call on a
  good file afterward.

- [x] **MEDIA-150 — Fix a stale open-item note in `NEXTmedia.md`.** §5's "real open follow-ups" list
  still described a dedicated `VideoPlayer`-level `audio_tail.mkv` EOS-drain test as future work,
  even though Phase 9 (`MEDIA-145`) already added exactly that test
  (`NonLoopedVideoWithLongerAudioTailStaysPlayingPastVideoDuration`). Left stale after Phase 9 landed.
  *Fix:* the bullet now records the item as closed (struck through, with the task ID and test name),
  rather than silently deleting the line (a future reader should be able to see it really was closed,
  not just vanish from the list).

- [x] **MEDIA-151 — Full-suite regression run.** Confirm zero regressions from this third
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4871 tests, 4869 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 4 new tests added by this phase (`DecodesTheFullFileWithoutSilentlyDroppingAnyFrame`,
  `SetVideoTrackEXTDoesNotTearDownTheUnrelatedAudioStream`,
  `SetAudioTrackEXTDoesNotRecreateTheUnrelatedVideoTexture`,
  `PlayOnAFirstFrameDecodeFailureLeavesThePlayerFullyClosedNotHalfOpen`), zero pre-existing tests
  broken. Grepped in full (`grep -c FAILED` on the complete log, not a truncated tail).

### Phase 11 — Fourth external review pass (2026-07-18, same day as Phase 8/9/10)

> Applying MEDIA-126's own convention a fourth time. A fourth external adversarial review of commit
> `8f23f747` (Phase 10's own fix commit) confirmed three of Phase 10's five fixes fully sound
> (EAGAIN retention, resampler flush, the audio/video reconfiguration split), found the
> `Play()`-exception-safety fix only covered the narrower first-frame-decode case, and found two
> further real defects independent of anything Phase 10 touched: an unbounded in-memory audio-buffer
> accumulation with no audio device, and Phase 10's own reconfiguration split still doing needless
> (and, for audio, destructive) work on a same-track reselect. Every claim independently re-derived
> from the actual code before any fix was written.

- [x] **MEDIA-152 — Widen `OpenDecoder()`'s `try` block to cover the reconfiguration calls, not
  just the first-frame decode.** `MEDIA-149`'s fix wrapped only `decoder_->NextFrame()` +
  `DrainAndFlushAudioBuffer()` in `try`/`catch`, but `state_ = MediaState::Playing` and both
  `ReconfigureVideoOutputForCurrentTrack()`/`ReconfigureAudioOutputForCurrentTrack()` calls ran
  *before* that `try` block started. An exception thrown from `Texture2D` construction inside the
  video-side reconfigure (in principle possible, e.g. an OOM/GPU failure) would bypass
  `CloseDecoder()` entirely -- the exact half-open-player problem `MEDIA-149` set out to fix for the
  narrower first-frame-decode case, left open for this earlier window.
  *Fix:* the `try` block now starts immediately after `state_ = MediaState::Playing;`, wrapping both
  reconfigure calls and the first-frame decode/audio-drain in one block; the `catch` still calls
  `CloseDecoder()`.
  *Accept:* no dedicated fault-injection test was written for this specific window -- neither
  reconfigure function has a reachable, deterministic throw path with this repo's real fixtures
  today (`ReconfigureAudioOutputForCurrentTrack()` degrades gracefully on device failure by design;
  `Texture2D`'s `(device, w, h)` constructor has no dimension validation that a real decoded video's
  always-positive width/height could trigger), matching this plan's own established precedent
  (`MEDIA-94`) for not building disproportionate fault-injection infrastructure for a defensive-only
  code path. Verified by code review and the full-suite regression run (`MEDIA-156`) showing the
  widened `try` scope changes no existing behavior.

- [x] **MEDIA-153 — Fix unbounded in-memory audio-buffer accumulation with no audio device.**
  `decoder_->DrainAudio(audioBuffer_)` ran unconditionally every decode iteration (both in
  `OpenDecoder()`'s first-frame path and `GetTexture()`'s catch-up loop), but `audioBuffer_.clear()`
  only ran inside `if (audioStream_)`. A video with a real audio track but no audio device available
  (device open failure, or a genuinely headless environment) accumulated its ENTIRE decoded audio
  track in memory for the rest of playback -- potentially hundreds of MB to GB for a long video --
  and `CloseDecoder()` never cleared it either, so stale audio from a failed-device `Play()` could be
  fed to a genuinely opened stream on a later successful `Play()`. This directly contradicted the
  existing "gracefully degrade to video-only" design intent for a missing audio device.
  *Fix:* factored the drain-and-feed logic (duplicated at both call sites) into a new private
  `DrainAndFlushAudioBuffer()` that always calls `audioBuffer_.clear()` at the end, regardless of
  whether `audioStream_` exists or the feed happened; also cleared in `CloseDecoder()` for the same
  reason.
  *Accept:* new `VideoPlayerTest.AudioBufferDoesNotAccumulateWithoutAnAudioDevice` uses a new
  `VideoPlayerTestAccess::SimulateAudioDeviceBecomingUnavailable()` (tears down a real, already-open
  stream the same way the graceful-degradation path already does) against `audio_tail.mkv` (a real
  audio track), then asserts `audioBuffer_` is exactly empty after every `GetTexture()` call across
  15 iterations -- a direct, deterministic proof, not a size-threshold heuristic.

- [x] **MEDIA-154 — Fix `VideoPlayer`'s track setters still doing needless/destructive work on a
  same-track reselect.** `VideoDecoder::SetAudioStream()`/`SetVideoStream()` (fixed in `MEDIA-131`)
  correctly no-op at the decoder level when re-selecting the already-active track or an out-of-range
  index, but `VideoPlayer::SetAudioTrackEXT()`/`SetVideoTrackEXT()` called their reconfigure helper
  *unconditionally* regardless of the decoder call's actual outcome. Since
  `ReconfigureAudioOutputForCurrentTrack()` always tears down and reopens the SDL stream, calling
  `SetAudioTrackEXT()` with the already-active (or an invalid) track index still discarded whatever
  audio was queued for playback -- the exact `MEDIA-148` problem, on the "nothing actually changed"
  axis instead of the "wrong side reconfigured" axis `MEDIA-148` fixed. The video side had the
  symmetric (non-destructive but still wasteful) texture-reallocation issue.
  *Fix:* `VideoDecoder::SetAudioStream()`/`SetVideoStream()` now return `bool` (true only on a
  genuine switch); `VideoPlayer::SetAudioTrackEXT()`/`SetVideoTrackEXT()` only call their respective
  reconfigure helper when the decoder call returns `true`.
  *Accept:* new `ReselectingTheSameAudioTrackDoesNotTearDownTheStream`,
  `ReselectingTheSameVideoTrackDoesNotRecreateTheTexture`, and
  `SelectingAnOutOfRangeAudioTrackDoesNotTearDownTheStream` all assert pointer identity
  (`SDL_AudioStream*`/`Texture2D*`) is preserved across the no-op call, extending Phase 10's
  cross-axis pointer-identity technique to the same-track-reselect and out-of-range cases.

- [x] **MEDIA-155 — Fix two `VideoDecoder` internal-state-reset gaps found alongside the above.**
  (1) `Close()` reset every other decode-state member but left `pendingAudio_` untouched -- a caller
  that reuses the same `VideoDecoder` instance across `Close()`+`Open()` (the class's own public
  contract makes no promise against this, even though `VideoPlayer` itself always allocates a fresh
  instance) would have its first `DrainAudio()` call after the second `Open()` return stale samples
  decoded from the *previous* file. (2) `SeekToStart()` flushed both codec contexts but didn't reset
  `havePendingVideoPacket_` (the `MEDIA-146` retention flag) or unref the packet it might be holding
  -- a packet retained from *before* the seek refers to compressed data at the old read position;
  resending it to the just-flushed codec context on the next `NextFrame()` call would hand the
  decoder a now-out-of-place packet with no relevant prior state.
  *Fix:* `Close()` now also calls `pendingAudio_.clear()`. `SeekToStart()` now also unrefs `pkt_` and
  resets `havePendingVideoPacket_` to `false` when a packet was pending.
  *Accept:* new `VideoDecoderTest.CloseClearsAnyUndrainedPendingAudioFromThePreviousFile` decodes a
  few frames without draining, closes, reopens a different file on the same instance, and asserts
  `DrainAudio()` returns nothing left over. The `SeekToStart()` half is verified by code review only
  (matching this plan's own honesty precedent, `MEDIA-94`/`MEDIA-152`): reliably forcing a genuine
  `avcodec_send_packet` EAGAIN *and* triggering a seek within that exact retry window in the same
  test, with this repo's real fixtures, is impractical fault-injection infrastructure disproportionate
  to the fix's own low real-world likelihood -- the reviewer's own report reached the same conclusion
  ("practical probability is small").

- [x] **MEDIA-156 — Full-suite regression run.** Confirm zero regressions from this fourth
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4876 tests, 4874 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 5 new tests added by this phase, zero pre-existing tests broken. Grepped in full
  (`grep -c FAILED` on the complete log, not a truncated tail).

### Phase 12 — Fifth external review pass (2026-07-18, same day as Phase 8/9/10/11)

> Applying MEDIA-126's own convention a fifth time. A fifth external adversarial review of commit
> `ec863ae9` (Phase 11's own fix commit) confirmed all four of Phase 11's fixes real and effective,
> then found three further real defects: Phase 11's own `MEDIA-154` bool-return contract was itself
> implemented incorrectly on the failure path, `SeekToStart()` still had three sub-gaps beyond the
> one `MEDIA-155` already closed, and the original `MEDIA-38` task's own literal "throw a clear
> exception" Accept criterion for resampler-setup failure was still unmet, with a real observable
> consequence (`HasAudio()` staying `true` for audio that can never actually produce a sample).
> Every claim independently re-derived from the actual code before any fix was written.

- [x] **MEDIA-158 — Fix `SetAudioStream()`/`SetVideoStream()`'s own `MEDIA-154` bool contract:
  discarded the open call's real result on the switch path.** `MEDIA-154`'s fix made both setters
  return `bool` so `VideoPlayer` could skip a needless reconfigure on a true no-op -- but on the
  actual-switch path, both setters called `OpenAudioStreamByIndex(i)`/`OpenVideoStreamByIndex(i)`,
  discarded that call's own return value, and unconditionally `return true;`. Both open functions
  build the new codec context *before* touching `audioCtx_`/`audioStream_` (or the video
  equivalents), so on a genuine open failure (unsupported codec, allocation failure) the *old*
  stream is still the one actually active -- but the caller was told a switch succeeded anyway,
  triggering `VideoPlayer`'s reconfigure for a track that, in reality, never changed.
  *Fix:* both setters now `return OpenAudioStreamByIndex(i);`/`return OpenVideoStreamByIndex(i);`
  directly instead of calling then assuming success.
  *Accept:* the existing success-path tests (`SetAudioTrackEXTMidPlaybackActuallyChangesTheActiveSampleRate`,
  etc.) continue to pass unchanged, confirming the fix doesn't alter the success case. No dedicated
  test forces a genuine mid-stream open failure for a valid track index -- doing so deterministically
  would require a fixture with a legitimately-indexed but deliberately-broken second track (corrupted
  codec extradata or an unsupported codec ID at a known byte offset), disproportionate infrastructure
  for this plan's existing honesty precedent (`MEDIA-94`/`MEDIA-152`/`MEDIA-155`'s `SeekToStart()`
  half) -- verified correct by direct code review of the now one-line change instead.

- [x] **MEDIA-159 — Close three more `SeekToStart()` gaps beyond the one `MEDIA-155` already
  fixed.** (1) `av_seek_frame()`'s own return was never checked -- on a genuine seek failure, the
  demuxer's read position never actually moved, but the codecs were flushed and pending state reset
  anyway, building a "we're now at the start" assumption on top of a position that's still wherever
  it was, and continuing to decode from a self-inconsistent state. (2) `pendingAudio_` was never
  cleared by `SeekToStart()` itself (only by `Close()`, per `MEDIA-155`) -- real, undrained samples
  decoded from before the seek would splice onto whatever decodes after it on the next
  `DrainAudio()` call. (3) `SwrContext`'s own internal delay buffer was never discarded on seek --
  the same class of gap `MEDIA-147` fixed for the EOF-flush case, but for a seek discontinuity
  instead.
  *Fix:* `SeekToStart()` now returns early (leaving all state untouched) if `av_seek_frame()`
  fails; on success, it also calls `pendingAudio_.clear()` and discards the resampler's own buffered
  delay via `swr_get_delay()` + a null-source `swr_convert()` (matching `MEDIA-147`'s technique, but
  discarding the output instead of keeping it -- a seek is a genuine timeline discontinuity, there is
  no "before" audio worth preserving across it).
  *Accept:* new `VideoDecoderTest.SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek`
  mirrors `MEDIA-155`'s `Close()` test exactly (decode without draining, then seek, then assert
  `DrainAudio()` returns nothing) -- a direct, deterministic proof for the `pendingAudio_` half. The
  `av_seek_frame()`-failure and resampler-discard halves are verified by code review only: forcing a
  genuine seek failure on a file this decoder already successfully opened, or forcing non-trivial
  resampler delay (this repo's `SetupResampler()` only does format conversion, not real rate
  conversion, in every fixture available), are both impractical to construct reliably -- the
  reviewer's own report reached the same conclusion for the seek-failure half.

- [x] **MEDIA-160 — Fix `HasAudio()` to require a working resampler, closing the real, observable
  half of the original `MEDIA-38` gap.** `MEDIA-38`'s own Accept criterion called for
  `swr_alloc_set_opts2()`/`swr_init()` failure to throw a clear exception; the actual code has
  always just set `swrCtx_ = nullptr` and returned. `HasAudio()` checked only `audioCtx_ != nullptr`,
  so a video whose audio codec opened fine but whose resampler setup failed would report
  `HasAudio() == true` while `ProcessAudioPacket()` (gated on `swrCtx_`) silently discarded every
  decoded audio frame -- a video with declared, "available" audio that can never actually produce a
  sample.
  *Fix:* `HasAudio()` now requires `swrCtx_ != nullptr` too, so a resampler-setup failure correctly
  reports "no audio" rather than a silently-broken "yes."
  *Accept:* `HasAudio()`'s sole real caller (`VideoPlayer::ReconfigureAudioOutputForCurrentTrack()`,
  gating whether an SDL audio stream is even opened) now correctly skips opening a device for this
  case instead of opening one that would never receive data. Deliberately did **not** also add a
  literal `throw` inside `SetupResampler()`: doing so would either require breaking `Open()`'s
  established non-throwing `bool`-return contract (used, unwrapped in `try`/`catch`, by dozens of
  existing call sites across this codebase) for an exception `Open()`'s own contract doesn't support
  propagating, or would mean throwing and then immediately catching it at the call site to fall back
  to the exact same `swrCtx_ == nullptr` graceful-degradation behavior that already existed --
  a throw with no observable behavioral effect, which this project's own conventions
  (`CLAUDE.md`) call out as unneeded complexity. `MEDIA-38`'s literal "throws a clear exception"
  wording is corrected here to reflect what's actually the right fix for this specific failure mode
  (graceful degradation to "no audio," consistent with how an `avcodec_open2()` failure one call site
  earlier is *already* handled by this same function) rather than forced to match text written before
  this specific failure mode's real behavior was understood -- no fault-injection test forces this
  path for the same `swr_alloc_set_opts2` near-unreachability reason `MEDIA-158` documents.

- [x] **MEDIA-161 — Full-suite regression run.** Confirm zero regressions from this fifth
  remediation pass, grepped in full (not a truncated tail). Also independently confirmed a
  full-suite crash observed on the first run of this phase (`ENetBackendTest.HostFreesOwnedRemoteGamerOnDispose`,
  a `double free or corruption (fasttop)` abort) was pre-existing flakiness unrelated to this phase's
  changes, not a regression: the same crash was absent on a re-run of this phase's own code, and a
  full suite run against the pre-Phase-12 baseline (`ec863ae9`, via `git stash`/`git stash pop`)
  passed cleanly both before and after re-applying this phase's changes -- isolating the crash to
  test-run-to-test-run environmental flakiness in the unrelated `Net`/ENet test suite, not anything
  this plan's scope touches.
  *Verified:* 4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 1 new test added by this phase, zero pre-existing tests broken. Grepped in full
  (`grep -c FAILED` on the complete log, not a truncated tail), confirmed on two independent full runs.

### Phase 13 — Sixth external review pass (2026-07-18, same day as Phase 8/9/10/11/12)

> Applying MEDIA-126's own convention a sixth time. A sixth external adversarial review of commit
> `0df369bc` (Phase 12's own fix commit) confirmed the fixes that WERE landed real, then found the
> `MEDIA-158` bool-contract fix was itself still incomplete for one specific failure mode inside the
> function it fixed, `SeekToStart()`'s resampler discard had a robustness gap, `MEDIA-38`'s own
> original task text was never actually edited despite the commit message claiming it was, and a
> long-standing asymmetry between the video- and audio-side EAGAIN handling had never been closed.
> Every claim independently re-derived from the actual code before any fix was written.

- [x] **MEDIA-162 — Make audio track switching genuinely transactional: build+verify the new
  resampler BEFORE destroying the old track's state.** `MEDIA-158`'s fix made
  `OpenAudioStreamByIndex()` propagate its own success/failure correctly for an `avcodec_open2()`
  failure -- but the function's own internal ordering still committed the new codec context and
  destroyed the OLD `audioCtx_`/`swrCtx_` *before* calling `SetupResampler()`, then unconditionally
  returned `true` regardless of the resampler's own result. A resampler-setup failure at that point
  left the old, fully-working track irrecoverably destroyed, the new track silently unable to
  produce any audio, and the function still reporting success -- violating this class's own
  documented `bool` contract ("true only if a different stream is genuinely active") for this one
  failure mode. Same gap existed in `Open()`'s own initial audio-codec setup, just with no "old
  track" to preserve (the practical effect there was a wastefully-allocated `audioCtx_` that could
  never produce audio, correctly hidden from `HasAudio()` by `MEDIA-160` but never freed).
  *Fix:* `SetupResampler()` renamed to `CreateResampler()` and changed to return the new
  `SwrContext*` directly instead of assigning `swrCtx_` itself, so callers can test success before
  committing anything. `OpenAudioStreamByIndex()` now builds and verifies the new resampler BEFORE
  touching any existing `audioCtx_`/`swrCtx_` state -- a track switch is only committed once BOTH
  the codec AND its resampler are confirmed working. `Open()`'s own audio setup applies the same
  pattern, freeing `audioCtx_` and falling back to `audioStream_ = -1` (exactly like an
  `avcodec_open2()` failure) if the resampler fails, instead of leaving a half-alive audio context.
  *Accept:* the full existing test suite (including every `SetAudioTrackEXT`/multi-track test) passes
  unchanged, confirming the reordering doesn't alter the success path. No dedicated test forces a
  genuine mid-switch resampler failure -- `swr_alloc_set_opts2()` failing for a channel
  layout/sample format `avcodec_open2()` already accepted is not practically reproducible with this
  repo's real fixtures, matching this plan's own established precedent (`MEDIA-94`/`MEDIA-152`) for
  not building disproportionate fault-injection infrastructure for a near-unreachable failure mode
  -- verified correct by direct code review instead.

- [x] **MEDIA-163 — Harden `SeekToStart()`'s resampler-delay discard: check `swr_convert()`'s
  return and loop until the delay is actually drained.** The `MEDIA-159` fix called `swr_convert()`
  exactly once, sized to a single `swr_get_delay()` reading, without checking its return or
  confirming the delay actually reached zero afterward -- inconsistent with this file's own
  `MEDIA-39` standard of checking every `swr_convert()` call, and a real (if currently low-impact)
  robustness gap: a partial drain or a genuine `swr_convert()` error would silently leave stale
  pre-seek resampler state behind.
  *Fix:* the discard is now a bounded loop (max 8 iterations, matching this file's existing
  bounded-retry style) that re-reads `swr_get_delay()` each pass and stops when it reports nothing
  left, checking `swr_convert()`'s own return each iteration and stopping on a non-positive result
  (error or "nothing more produced") rather than assuming a single call always fully empties the
  buffer.
  *Accept:* verified by code review and the full-suite regression run (`MEDIA-166`) showing no
  behavior change for the (currently delay-free, format-conversion-only) real fixtures. No dedicated
  test forces non-trivial resampler delay or a genuine `swr_convert()` failure here, for the same
  reason `MEDIA-147`/`MEDIA-159` already documented.

- [x] **MEDIA-165 — Actually edit `MEDIA-38`'s own original task text, not just add a note
  elsewhere.** The Phase 12 commit message claimed "`MEDIA-38`'s task text corrected in
  `plan_media.md`," but the diff only *appended* `MEDIA-160`'s new note in the Phase 12 section --
  `MEDIA-38`'s own original bullet (§5 Phase 1) was untouched and still literally said "throw a
  clear, documented C++ exception" with an Accept criterion of "a fault-injection test throws
  cleanly instead of crashing; ASan-clean," directly contradicting `MEDIA-160`'s own explicit
  statement that neither was implemented. A reader who only read `MEDIA-38` in isolation (its own
  original location in the document) would see no indication anything about it was revisited.
  *Fix:* `MEDIA-38`'s own bullet is now edited in place -- the original wording is preserved
  (labeled "original text, superseded below, kept for history") immediately followed by a
  "Correction (Phase 12...)" section explaining what was actually decided and why, cross-referencing
  `MEDIA-160`/`MEDIA-162`. Matches this plan's own established precedent for in-place task-note
  correction (`MEDIA-73`/`MEDIA-32`/`MEDIA-94`/`MEDIA-121`, per `NEXTmedia.md`'s own Phase 0
  provenance note) -- individual task notes may be corrected in place; only whole-phase history is
  append-only.

- [x] **MEDIA-164 — Fix the audio-side EAGAIN asymmetry: `ProcessAudioPacket()` never retried a
  packet `avcodec_send_packet` rejected with EAGAIN.** The video side has retried an EAGAIN'd packet
  since `MEDIA-146`, but the audio side's `ProcessAudioPacket()` tolerated `AVERROR(EAGAIN)` from
  `avcodec_send_packet` without throwing, then simply drained whatever frames were currently
  available and returned -- the original packet was never resent. Its caller
  (`NextFrame()`'s packet-reading loop) unconditionally `av_packet_unref()`s the packet regardless
  of whether it was ever actually accepted by the codec, silently discarding real audio data on the
  rare occasions an audio codec's internal queue fills up. Low real-world likelihood (audio codecs
  buffer far less aggressively than video codecs), but the implemented error path was not correct.
  *Fix:* `ProcessAudioPacket()` now wraps the send+drain sequence in a retry loop: if
  `avcodec_send_packet` returns EAGAIN, the receive loop still drains whatever's currently available
  (relieving the backpressure), then the SAME packet is resent -- mirroring
  `havePendingVideoPacket_`'s pattern on the video side, just contained entirely within this one
  function's own call (audio packets aren't retained across `NextFrame()` calls the way video
  packets are, since `ProcessAudioPacket()` doesn't return until the packet is fully consumed one
  way or another).
  *Accept:* the full existing audio-decode test suite (`DrainAudioProducesRealSamplesAfterDecodingFrames`,
  `Av1ContentDecodesVideoAndKeepsItsAudioTrack`, the `audio_tail.mkv`/multi-track tests) continues to
  pass unchanged, confirming the retry loop doesn't alter behavior for the non-EAGAIN case (the only
  case these fixtures currently exercise). No dedicated test forces a genuine audio-side EAGAIN --
  reliably triggering it requires a codec/fixture combination whose internal buffering behavior
  isn't practically controllable from a test, matching this plan's established precedent for
  near-unreachable FFmpeg failure modes.

- [x] **MEDIA-166 — Full-suite regression run.** Confirm zero regressions from this sixth
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 0 new tests added this phase (all four fixes verified by code review + full-suite
  non-regression, per each task's own Accept note above), zero pre-existing tests broken. Grepped in
  full (`grep -c FAILED` on the complete log, not a truncated tail).

### Phase 14 — Seventh external review pass (2026-07-18, same day as Phase 8-13)

> Applying MEDIA-126's own convention a seventh time. A seventh external adversarial review of
> commit `94892fb2` (Phase 13's own fix commit) confirmed the transactional audio-switch fix
> (`MEDIA-162`) and the audio EAGAIN retry (`MEDIA-164`) fully sound, then found `MEDIA-163`'s
> resampler-discard loop still didn't *guarantee* a clean reset, `MEDIA-38`'s just-corrected text
> (`MEDIA-165`) contained its own new factual error, and `HasAudio()`'s doc comment had gone stale
> the moment `MEDIA-162` landed. Every claim independently re-derived from the actual code before
> any fix was written.

- [x] **MEDIA-167 — Replace `SeekToStart()`'s manual resampler-delay drain loop with a full
  resampler recreation.** `MEDIA-163`'s bounded drain loop was a real improvement over `MEDIA-159`'s
  original single unchecked call, but still couldn't *guarantee* the delay reached zero: it could
  exit via its own 8-iteration bound or a `swr_convert()` error with real delay left
  unconfirmed-drained, and treated a genuine negative return identically to "nothing more
  produced" rather than distinguishing them -- itself inconsistent with the very `MEDIA-39`
  standard it was written to satisfy.
  *Fix:* discard the drain loop entirely. `SeekToStart()` now `swr_free()`s the existing `swrCtx_`
  and rebuilds a fresh one via the same `CreateResampler()` every other audio-open path already
  uses (`MEDIA-162`) -- a brand-new `SwrContext` has zero internal delay by construction, so there
  is no draining question left to answer. A recreation failure (extremely unlikely -- the same
  codec context already built a working resampler once) is left as `swrCtx_ == nullptr`, which
  `HasAudio()` (`MEDIA-160`) already correctly reports as "no audio," rather than thrown --
  `SeekToStart()` has no throwing contract anywhere else and is called from
  `VideoPlayer::GetTexture()`'s `isLooped_` branch with no surrounding `try`/`catch`.
  *Accept (original claim -- FALSE, corrected by `MEDIA-171`):* this task originally claimed the
  existing `LoopedVideoKeepsPlayingPastItsDuration` test covered it by "genuinely exercising
  `SeekToStart()` with live audio via `VideoPlayer`," plus
  `SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek`. **Both claims were wrong** (found by
  external code review, `plan_media.md` `MEDIA-171`): `LoopedVideoKeepsPlayingPastItsDuration` uses
  `chroma_420.mkv`, which has NO audio stream at all (ffprobe: one ffv1 video stream, nothing else),
  so it never even enters `SeekToStart()`'s `if (audioCtx_ && swrCtx_)` block; and the
  `pendingAudio_` test only asserts the buffer is *empty* after the seek, which would pass even more
  readily if the recreation left `swrCtx_` null (no audio would ever decode again).
  *Accept (actual, current):* new `VideoDecoderTest.SeekToStartRebuildsAWorkingResamplerSoAudioStillDecodesAfterTheSeek`
  (`MEDIA-171`) is the real proof -- it decodes real audio before the seek, seeks, then asserts
  `HasAudio()` is still true AND that further decoding genuinely produces new audio samples through
  the rebuilt resampler. **Verified falsifiable by mutation testing**: with the
  `swrCtx_ = CreateResampler(audioCtx_)` line temporarily removed, the new test fails on both
  assertions while the old `pendingAudio_` test still passes -- directly demonstrating the old
  coverage claim was empty and the new one is not. No dedicated test forces a genuine
  resampler-recreation *failure* at seek time, for the same near-unreachable-failure-mode reason
  documented at `MEDIA-94`/`MEDIA-160`/`MEDIA-162`.

- [x] **MEDIA-168 — Fix a factual error `MEDIA-165`'s own correction paragraph introduced into
  `MEDIA-38`'s text.** `MEDIA-165`'s in-place correction of `MEDIA-38` claimed
  `AllocAndConfigureCodecContext()`'s null-checks (and `Open()`'s `frame_`/`pkt_` allocation check)
  "do throw" -- factually wrong. Every `Open()`-time allocation failure in this file (the video
  codec context at `VideoDecoder.cpp:60`, the audio codec context, `frame_`/`pkt_` at
  `VideoDecoder.cpp:126`) gracefully `return false`/call `Close()`, matching `Open()`'s own
  established non-throwing `bool` contract -- none of them throw. The one place that genuinely does
  throw for an allocation failure is `ProcessAudioPacket()`'s `av_frame_alloc()` for the per-packet
  decode frame, a *decode-time* allocation inside a function whose contract (like `NextFrame()`'s)
  already permits throwing, which is a different call entirely from the ones `MEDIA-165`'s text
  cited.
  *Fix:* `MEDIA-38`'s correction paragraph edited again in place (not merely re-appended elsewhere,
  matching this plan's own established precedent for in-place task-note correction) to accurately
  describe every `Open()`-time allocation failure as gracefully degrading, and to correctly
  attribute the one genuine throw in this family to `ProcessAudioPacket()`'s decode-time frame
  allocation instead.
  *Accept:* verified by re-reading `Open()`/`AllocAndConfigureCodecContext()`/`ProcessAudioPacket()`
  line-by-line against the corrected text before committing (the same discipline `MEDIA-165` itself
  was supposed to have applied but didn't).

- [x] **MEDIA-169 — Update `HasAudio()`'s stale doc comment to describe the current, real
  divergence path.** `MEDIA-160`'s original comment described `audioCtx_` staying non-null after a
  resampler failure "inside `SetupResampler()`" during initial setup -- a scenario `MEDIA-162`'s
  Phase 13 transactional rewrite had already closed off entirely (both `Open()` and
  `OpenAudioStreamByIndex()` now free `audioCtx_`/never touch it at all on a resampler failure),
  and which referenced a function name (`SetupResampler()`) that no longer exists after `MEDIA-162`
  renamed it to `CreateResampler()`. The comment survived, unedited, describing an
  already-impossible state instead of the real one `MEDIA-167` (this same phase) just introduced:
  a resampler-recreation failure inside `SeekToStart()`.
  *Fix:* comment rewritten to describe the actual current divergence path (`SeekToStart()`'s
  resampler recreation, `MEDIA-167`) and to explicitly note that the two `Open()`-time paths can no
  longer diverge at all post-`MEDIA-162`, so `HasAudio()`'s dual-check remains meaningful for a
  different, real reason than the one originally documented.
  *Accept:* doc-only change; verified by re-reading `Open()`/`OpenAudioStreamByIndex()`/
  `SeekToStart()`'s current (post-`MEDIA-162`/`MEDIA-167`) code to confirm the new comment's claims
  are accurate, the same verification `MEDIA-168` applied to its own correction.

- [x] **MEDIA-170 — Full-suite regression run.** Confirm zero regressions from this seventh
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4877 tests, 4875 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 0 new tests added this phase. **This "covered by existing real-playback tests per
  `MEDIA-167`'s own Accept note" justification was itself false** -- see `MEDIA-171` (Phase 15),
  which found the cited test uses an audio-less fixture and added the real coverage. Zero
  pre-existing tests broken. Grepped in full (`grep -c FAILED` on the complete log, not a truncated
  tail).

### Phase 15 — Eighth external review pass (2026-07-18, same day as Phase 8-14)

> Applying MEDIA-126's own convention an eighth time. An eighth external adversarial review of
> commit `56e391e7` (Phase 14's own fix commit) confirmed the `SeekToStart()` resampler-recreation
> fix (`MEDIA-167`) factually correct as *code*, but found its claimed regression coverage was
> entirely fictitious, plus a wrong task-ID cross-reference in the comment `MEDIA-169` had just
> rewritten. Both findings are about claims made *about* correct code, not the code itself --
> the third consecutive round where documentation/verification claims, not implementation, were the
> defect.

- [x] **MEDIA-171 — Add the real `SeekToStart()` audio-after-seek regression test; `MEDIA-167`'s
  claimed coverage did not exist.** `MEDIA-167`'s Accept note asserted its resampler-recreation fix
  was covered by `LoopedVideoKeepsPlayingPastItsDuration` ("genuinely exercising `SeekToStart()`
  with live audio via `VideoPlayer`") and `SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek`.
  Neither was true: the looped-playback test uses `chroma_420.mkv`, which has **no audio stream at
  all** (verified via both `manifest.json` and a direct `ffprobe` run -- one ffv1 video stream,
  nothing else), so it never enters `SeekToStart()`'s `if (audioCtx_ && swrCtx_)` block; and the
  `pendingAudio_` test only asserts the pending buffer is *empty* after the seek, an assertion that
  would pass even more readily if the recreation left `swrCtx_` null, since then no audio would ever
  decode again. `MEDIA-167`'s fix was genuinely correct -- but nothing was actually proving it.
  *Fix:* new `VideoDecoderTest.SeekToStartRebuildsAWorkingResamplerSoAudioStillDecodesAfterTheSeek`
  decodes real audio from `audio_tail.mkv` before the seek (asserting non-zero samples so the
  fixture's own audio is confirmed first), seeks, then asserts `HasAudio()` is still true AND that
  five more decoded frames genuinely produce new audio samples through the rebuilt resampler.
  *Accept:* **verified falsifiable by mutation testing**, not merely "it passes": with
  `swrCtx_ = CreateResampler(audioCtx_)` temporarily removed from `SeekToStart()`, the new test
  fails on both assertions (`HasAudio()` false; zero post-seek samples) while
  `SeekToStartClearsAnyUndrainedPendingAudioFromBeforeTheSeek` still passes -- directly
  demonstrating both that the old coverage claim was empty and that the new test genuinely closes
  it. Implementation then restored and confirmed byte-identical to the committed version
  (`git diff` clean) before the real run.

- [x] **MEDIA-172 — Fix a wrong task-ID cross-reference in `HasAudio()`'s comment.** `MEDIA-169`'s
  rewrite of that comment attributed itself to `MEDIA-168` -- but `MEDIA-168` was the `MEDIA-38`
  allocation-exception text correction; this comment rewrite was `MEDIA-169`. A reader following the
  cross-reference would land on an unrelated task.
  *Fix:* corrected to `MEDIA-169`.
  *Accept:* doc-only; verified by re-reading both task entries to confirm which one actually covered
  this comment.

- [x] **MEDIA-173 — Full-suite regression run.** Confirm zero regressions from this eighth
  remediation pass, grepped in full (not a truncated tail).
  *Verified:* 4878 tests, 4876 passed, 0 failed, 2 pre-existing hardware skips (Accelerometer/
  Gyroscope) -- 1 new test added by this phase (`MEDIA-171`'s, mutation-verified falsifiable), zero
  pre-existing tests broken. Grepped in full (`grep -c FAILED` on the complete log, not a truncated
  tail).

### Phase 16 — Full XNA 4.0 API-parity audit remediation (2026-07-18)

> **Different in kind from Phases 8-15.** Those were each a review of the *previous phase's own fix
> commit*. This phase comes from a **full API-parity audit of CNA Media against the original
> Microsoft XNA 4.0 reference assemblies** (`/rv/data/library/github.com/borgesdan/xn65/references/
> Windows/Microsoft.Xna.Framework.xml`), not against FNA. That distinction is the root cause of the
> largest finding here: `plan_media.md` §1 named **FNA** as the authoritative reference, but FNA's
> own `src/Media/Song.cs` *omits XNA members* (`Album`/`Artist`/`Genre`/`ToString`). Auditing
> against FNA therefore could not surface an entire class of gap — CNA faithfully reproduced FNA's
> own incompleteness. **Where FNA and the XNA 4.0 reference assemblies disagree about the existence
> of a public member, the XNA reference wins** (`CLAUDE.md`'s own rule: "MUST strictly adhere to the
> XNA 4.0 API specification"; FNA is authoritative for *behavior*, not for *API completeness*).
>
> **Audit result:** 24/24 public type names present (full type-level parity). Gaps are at the
> member, behavior, and platform level.
>
> **Project owner decisions taken for this phase (2026-07-18, explicit):**
> 1. **Visualization → full real implementation.** `Mix_SetPostMix` PCM tap + a from-scratch radix-2
>    FFT in `CNA::Internal::Media`. Both `Samples` and `Frequencies` must carry real data. No new
>    third-party dependency.
> 2. **Video → real FFmpeg on ALL platforms, no `NotSupportedException` stubs.** Windows, Android
>    and Emscripten must genuinely build and run `Video`/`VideoPlayer`/`VideoDecoder`. The
>    stub-fallback option was explicitly rejected. See `MEDIA-198`'s escalation rule for what to do
>    if a platform proves genuinely infeasible — **do not silently fall back to stubs.**
> 3. **`Song::Album`/`Artist`/`Genre` → `nullptr` for non-library songs**, non-owning raw
>    back-pointers set by `MediaLibrary`, mirroring the existing non-owning `Album::songs_` pattern.
> 4. **Full compatibility long tail in scope:** additional audio formats, embedded album art, real
>    scaled thumbnails, and real `IsRated`/`Rating` parsing.
>
> **Nothing in this phase is implemented yet — it defines the work only.** Tasks are grouped A-H;
> groups are independently shippable and can be worked in parallel except where a task names a
> dependency.

#### Group A — `Song`'s missing XNA members (`Album`, `Artist`, `Genre`, `ToString`)

> **Root finding.** The XNA 4.0 reference documents `Song.Album`, `Song.Artist`, `Song.Genre` and
> `Song.ToString()`. CNA's `Song` (`include/Microsoft/Xna/Framework/Media/Song.hpp:166`) stores only
> `name_`, `duration_`, `playCount_`, `isDisposed_`, `handle_` — none of the three relationships and
> no `ToString()`. FNA omits them too, which is why every prior pass missed this.

- [x] **MEDIA-174 — Add `Song::getAlbumProperty()`/`getArtistProperty()`/`getGenreProperty()`.**
  Declare in `Song.hpp` returning `Album*` / `Artist*` / `Genre*` (raw, **non-owning**, per owner
  decision 3). Add matching private members `Album* album_ = nullptr; Artist* artist_ = nullptr;
  Genre* genre_ = nullptr;`. Use forward declarations (`class Album; class Artist; class Genre;`) in
  `Song.hpp` to avoid a circular include — `Album.hpp` already forward-declares `SongCollection` for
  exactly this reason, follow that established pattern. These are **XNA members, not `NOXNA`** — do
  not mark them. Full Doxygen per `CLAUDE.md`, `@brief` text taken from the XNA reference
  (`"Gets the Album on which the Song appears."`, `"Gets the Artist of the Song."`,
  `"Gets the Genre of the Song."`).
  *Accept:* headers compile with no circular-include error; all three getters exist with correct
  XNA names and return types; a standalone `Song s("file.ogg")` returns `nullptr` from all three.
  *Done:* `getAlbumProperty()`/`getArtistProperty()`/`getGenreProperty()` added with forward-declared `Album`/`Artist`/`Genre`; no circular-include issue (both TUs compile clean).

- [x] **MEDIA-175 — Add a `NOXNA` setter path for the back-references, friend-scoped to
  `MediaLibrary`.** `MediaLibrary` must be able to set the three pointers while building the object
  graph, but they are read-only in the XNA API (get-only properties). Do **not** add public setters
  — that would be a non-XNA public API addition. Use the project's existing friend pattern
  (`NOXNA friend class MediaLibrary;` in `Song`, matching how `Video::parent_` is already reached by
  `VideoPlayer`). Document the choice in the header.
  *Accept:* `MediaLibrary` can set the pointers; no public setter exists on `Song`; a compile-time
  check (or a review note in the task report) confirms no other translation unit can set them.
  *Done:* `NOXNA friend class MediaLibrary;` -- no public setter added; the three members stay get-only on the public XNA surface.

- [x] **MEDIA-176 — Implement `Song::ToString()`.** XNA reference: *"Returns a String representation
  of the Song."* XNA's actual implementation returns the song's `Name`. Verify against the reference
  assembly's own behavior if a decompiled body is available in
  `/rv/data/library/github.com/borgesdan/xn65/`; if not, implement as `return name_;` and record the
  inference explicitly in the task report (do **not** claim it was verified if it wasn't — see the
  `MEDIA-171` lesson). Must be `override` if `System::Object` declares a virtual `ToString()`; check
  first and match the project-wide convention used by e.g. `Album`/`Artist`.
  *Accept:* `ToString()` exists, is tested for the expected format per `CLAUDE.md`'s testing rules,
  and its derivation (verified vs. inferred) is stated honestly in the commit message.
  *Done:* returns `name_`, matching `Album`/`Artist`/`Genre`/`Playlist`. **Explicitly recorded as an inference from sibling types, NOT verified against a decompiled XNA binary** -- the reference XML documents only "Returns a String representation of the Song".

- [x] **MEDIA-177 — Wire the back-pointers in `MediaLibrary`'s graph construction.**
  `src/Microsoft/Xna/Framework/Media/MediaLibrary.cpp:80` builds every `Song` then groups them into
  genre/artist/(artist,album) buckets. After the `Album`/`Artist`/`Genre` objects are constructed,
  walk each one's member songs and set the reverse pointer. Ordering matters: the forward grouping
  must complete first so the owning objects exist and their addresses are stable — confirm the
  containers holding them (`ownedAlbums_` etc.) do not reallocate afterwards, or set the pointers in
  a dedicated final pass after all grouping is done. **A `std::vector` of owned objects that grows
  later will invalidate every pointer already handed out** — this is the single highest-risk detail
  in Group A; verify the container/ownership strategy explicitly.
  *Accept:* for a real scanned library, `lib.getSongsProperty()[i]->getAlbumProperty()` returns the
  same `Album*` that `lib.getAlbumsProperty()` exposes, and the round-trip
  `album->getSongsProperty()` contains that same song — pointer identity, not just equal names.
  *Done:* dedicated final pass after all group objects exist, using a new `albumByKey` map. The flagged pointer-invalidation risk turned out **not to apply**: the owning containers are `std::vector<std::unique_ptr<T>>`, so vector growth moves the smart pointers but never the heap objects, and `.get()` stays valid.

- [x] **MEDIA-178 — Real reverse-edge tests.**
  `tests/Microsoft/Xna/Framework/Media/MediaLibraryTests.cpp:82` is labelled a *"full cross-class
  object-graph integration audit"* but only checks forward edges (Artist/Album/Genre → Song); it
  could not check reverse edges because they did not exist. Extend it (or add a sibling test) to
  assert every reverse edge by **pointer identity** for a real scanned fixture library, plus
  `nullptr` for a standalone `Song`.
  *Accept:* test fails if any reverse pointer is null/mismatched for a library song. **Verify
  falsifiable by mutation** (per `MEDIA-171`'s established practice): temporarily skip the
  back-pointer assignment in `MediaLibrary` and confirm the new assertions actually fail.
  *Done + MUTATION-VERIFIED:* reverse edges asserted by pointer identity in `ObjectGraphIsInternallyConsistent`, plus a new `SongsPointBackAtTheirOwningAlbumArtistAndGenre` with explicit anti-vacuity guards. Removing the `album_` wiring makes both fail (`song 'Sunrise' does not point back at its Album`, `no song had an Album -- back-references never populated`); restored implementation passes.

- [x] **MEDIA-179 — Correct the "full object-graph audit" claim wherever it appears.** The label at
  `MediaLibraryTests.cpp:82` (and any matching `[x]` task note or `AUDIT.md` row claiming complete
  cross-class coverage) overstated what was covered. Correct those claims in place, following
  `MEDIA-165`/`MEDIA-168`'s precedent for in-place correction rather than appending a note
  elsewhere.
  *Accept:* no remaining claim of "full" object-graph coverage predates the reverse edges actually
  existing.
  *Done:* the "full cross-class object-graph integration audit" label at `MediaLibraryTests.cpp:82` corrected in place, explaining why the Song-side edges were previously impossible to check.

- [x] **MEDIA-180 — Update `AUDIT.md`/`CHECKLIST.md` and this plan's own §1/§2 for the
  FNA-vs-XNA-reference precedence rule.** Record explicitly that FNA is authoritative for *behavior*
  but the XNA 4.0 reference assemblies are authoritative for *API surface*, and that FNA's `Song`
  omissions are a known FNA gap CNA deliberately does **not** reproduce. This is the systemic fix —
  without it, the same class of gap can recur in any future namespace ported by reading FNA alone.
  *Accept:* the rule is written down somewhere a future porting session will actually read
  (`CLAUDE.md` is owner-controlled; `CHECKLIST.md` is the right home) and cross-referenced from this
  plan.

#### Group B — `Song` metadata that is hardcoded or silently dropped
  *Done:* `CHECKLIST.md`'s "API surface" section rewritten -- FNA authoritative for behavior, XNA reference assemblies authoritative for API surface, with the exact `grep` recipe and the C++ idiom equivalences (`op_Equality`->`operator==`, `Item`->`operator[]`, `GetEnumerator`->`begin()/end()`) so they are not misreported as gaps.

- [x] **MEDIA-181 — Pass `TrackNumber` from the index into `Song` (a real dropped-data bug).**
  `src/CNA/Internal/Media/MediaLibraryIndex.cpp:109` genuinely parses and stores
  `song.trackNumber = tags.trackNumber`, but `MediaLibrary.cpp:80` constructs `Song` with only
  `(path, title)` or `(path, title, durationMs)` — the value is **parsed and then thrown away**, and
  `Song::getTrackNumberProperty()` returns a hardcoded `0`. This is unfinished implementation, not
  unavailable information. Add a `NOXNA` constructor overload (or a friend-scoped setter, matching
  `MEDIA-175`'s decision) carrying the track number, and store it in a real `trackNumber_` member.
  *Accept:* a fixture with a real track-number tag reports that number through
  `Song::getTrackNumberProperty()` after a library scan. Test must use a fixture whose tag value is
  independently confirmed (via `ffprobe`/the manifest) — **and must be mutation-verified**: with the
  wiring removed, the test must fail.
  *Done + MUTATION-VERIFIED:* `trackNumber_` added to `Song`, set by `MediaLibrary` in the same friend-scoped pass as the back-references (no new ctor overload needed). New `LibrarySongsCarryTheirRealTrackNumberFromTags` asserts distinct manifest-verified values (Sunrise=1, Twilight=1, Daybreak=2, Étoile=3) so an all-zero or all-one implementation cannot accidentally pass; removing the wiring fails all four. `SongTest.TrackNumberIsAlwaysZero` renamed to `TrackNumberIsZeroForAStandaloneSong` -- it uses a standalone Song, for which 0 is genuinely correct, but its old name asserted stub semantics.

- [x] **MEDIA-182 — Parse ID3v2 `POPM` (Popularimeter) into a real rating.** Extend
  `CNA::Internal::Media::AudioTagParser` (`TryReadId3v2`) to read the `POPM` frame: email/identifier
  string (null-terminated), one byte rating 0-255, then an optional play-count. Add
  `int rating = 0; bool hasRating = false;` to the `AudioTags` struct
  (`include/CNA/Internal/Media/AudioTagParser.hpp:12`).
  **Scale conversion is the key detail:** XNA's `Song.Rating` is a 0-10 value; ID3v2 `POPM` is
  0-255 where 0 means "unrated". Define and document the mapping (recommended: `0 → unrated`,
  otherwise `round(popm * 10.0 / 255.0)` clamped to 1-10). Record the chosen mapping in
  `CHECKLIST.md`'s deviation table, since XNA/FNA define no such conversion.
  *Accept:* a fixture with a known `POPM` byte yields the documented 0-10 value; a file with no
  `POPM` yields `hasRating == false`.
  *Done + MUTATION-VERIFIED:* ID3v2 `POPM` parsed (identifier, rating byte, optional counter). Mapping: **0 stays 'unrated'** (the spec reserves it), 1-255 -> 1-10 rounded, so a rated song never reports 0. **The fixture is hand-built byte-by-byte in Python** because ffmpeg cannot write POPM -- the same technique this project already established for hand-constructed `.xnb` containers. The test also asserts the OTHER frames still parse, proving the frame walk stays aligned across POPM rather than desynchronising on it.

- [x] **MEDIA-183 — Parse the Vorbis-comment `RATING` field.** The Ogg/FLAC equivalent of `POPM`.
  Convention is inconsistent in the wild (some writers use 0-100, some 0-5, some 0-10). Pick and
  document one interpretation (recommended: treat as 0-100 and map to 0-10, since that is the most
  common convention among taggers that write `RATING`), and note the ambiguity honestly in the
  deviation table rather than pretending it is unambiguous.
  *Accept:* a fixture with a known `RATING` comment maps to the documented 0-10 value.
  *Done:* Vorbis `RATING` comment parsed, treated as a **0-100** scale (80 -> 8). The ambiguity is real and is recorded honestly rather than presented as settled: different taggers write 0-100, 0-5 or 0-10, and there is no standard. Non-numeric values (some taggers write stars/text) are ignored rather than guessed at.

- [x] **MEDIA-184 — Wire rating into `Song::getRatingProperty()`/`getIsRatedProperty()`.** Both are
  currently hardcoded (`src/Microsoft/Xna/Framework/Media/Song.cpp:54`: `IsRated → false`,
  `Rating → 0`). Carry the parsed values through `MediaLibraryIndex` → `MediaLibrary` → `Song` the
  same way `MEDIA-181` does for track number. `IsRated` must mean "the user has rated this song",
  i.e. a real rating tag was present — not merely "rating != 0".
  *Accept:* a rated fixture reports `IsRated == true` and the correct 0-10 `Rating`; an unrated
  fixture reports `false`/`0`. Mutation-verified.
  *Done + MUTATION-VERIFIED:* `IsRated`/`Rating` carried end-to-end from tags through `MediaLibraryIndex` -> `MediaLibrary` -> `Song`, exactly like `TrackNumber`. **`IsRated` deliberately means 'a real rating tag was present', NOT `rating != 0`** -- both formats reserve 0 for unrated, so an explicit zero must still report false; that distinction is what XNA's `IsRated` actually means. Two fixtures encode the SAME result (8) by DIFFERENT routes (POPM byte 196 vs Vorbis RATING 80), so a bug in either conversion surfaces independently, plus an explicit unrated-song negative case. `SongTest.IsRatedIsAlwaysFalse`/`RatingIsAlwaysZero` renamed to `...ForAStandaloneSong` -- correct for a standalone Song, but the old names asserted stub semantics as the specification.

- [x] **MEDIA-185 — Decide and document `Song::IsProtected` formally.** XNA: *"Gets a value that
  indicates whether the song is DRM protected content."* CNA scans plain local files with no DRM
  container support, so `false` is the *correct* answer for everything CNA can currently index — but
  it is currently an undocumented hardcoded literal that reads like an unfinished stub. Either (a)
  keep `false` and add a `CHECKLIST.md` deviation row explaining *why* it is correct rather than
  unimplemented, or (b) if any indexable format can carry DRM (e.g. an `M4P`/FairPlay file
  encountered by `MEDIA-186`'s format expansion), detect and report it honestly.
  *Accept:* `IsProtected`'s value is justified in writing, not merely hardcoded. If (b) applies,
  add the detection.

#### Group C — Real visualization (owner decision 1)

> `MediaPlayer::getIsVisualizationEnabledProperty()` returns hardcoded `false`
> (`src/Microsoft/Xna/Framework/Media/MediaPlayer.cpp:154`), the setter is a no-op, and
> `GetVisualizationData()` does nothing (`:273`). `tests/.../MediaPlayerTests.cpp:240` currently
> *asserts the stub behavior*, i.e. it locks the gap in as expected behavior. `VisualizationData`
> itself is already correct and complete: `Size = 256`, public `freq`/`samp` arrays, and
> `getFrequenciesProperty()`/`getSamplesProperty()` matching the XNA reference exactly.
  *Done (documentation, no code change -- option (a)):* `IsProtected` returning false is **correct for everything CNA can index**, not an unfinished stub: the scan only accepts plain unencrypted containers, so a DRM-wrapped file (e.g. FairPlay `.m4p`) is not indexable in the first place and no indexed song can ever be protected. Documented in the header with the condition that would require revisiting it (a future format expansion making DRM-wrapped files reachable).

- [x] **MEDIA-186 — Add `CNA::Internal::Media::VisualizationCapture`: a lock-free PCM ring buffer
  fed by `Mix_SetPostMix`.** New `include/CNA/Internal/Media/VisualizationCapture.hpp` + `.cpp`.
  `Mix_SetPostMix(callback, userdata)` delivers the final mixed stream. **Critical constraint: the
  callback runs on SDL's audio thread**, so it must be real-time safe — no allocation, no locking,
  no exceptions. Use a fixed-size buffer with atomic indices (single-producer/single-consumer).
  Handle the mixer's actual output format: query via `Mix_QuerySpec` (format may be
  `AUDIO_S16`/`AUDIO_F32`, 1 or 2 channels) and convert to mono float in [-1, 1] for the buffer.
  *Accept:* a real playing song produces non-zero, bounded samples in the ring buffer; ThreadSanitizer
  (or documented reasoning about the atomics) shows no data race between the audio and main threads.
  *Done -- with a corrected API assumption.* This task's own text named `Mix_SetPostMix` and a format conversion from `AUDIO_S16`/`AUDIO_F32`. **Both were wrong**: this project uses the NEW SDL3_mixer API (`MIX_Track`/`MIX_Audio`), where `Mix_SetPostMix` does not exist -- the equivalent is `MIX_SetPostMixCallback`, and it delivers **float32 PCM directly**, so no format conversion is needed at all (only a mono downmix). Implemented as `CNA::Internal::Media::VisualizationCapture`: SPSC ring buffer, atomic cursor, `Push()` is allocation/lock/exception-free for the audio thread.

- [x] **MEDIA-187 — Implement a from-scratch radix-2 FFT
  (`CNA::Internal::Media::VisualizationFFT`).** No new dependency (owner decision 1). 512-point
  real-input FFT producing 256 magnitude bins to fill `VisualizationData::Size`. Apply a Hann window
  before the transform to reduce spectral leakage. Normalize magnitudes to a documented range —
  XNA's own normalization is not specified in the reference, so **pick one, document it in
  `CHECKLIST.md`, and do not claim XNA-exact parity for the magnitude scale** (recommended: divide
  by `N/2` so a full-scale sine maps to ~1.0).
  *Accept:* unit tests with synthetic inputs — a pure sine at a known frequency puts its peak in the
  expected bin; DC input puts energy in bin 0; silence gives all zeros. These are deterministic and
  need no audio device.
  *Done:* `CNA::Internal::Media::VisualizationFFT` -- 512-point iterative radix-2 Cooley-Tukey with bit-reversal permutation and a Hann window, no new dependency. Magnitudes scaled by `2/N`; the Hann window's own 0.5 coherent gain is deliberately left uncompensated and documented rather than silently baked in.

- [x] **MEDIA-188 — Make `IsVisualizationEnabled` a real, functional gate.** Setter must install
  (`true`) or remove (`false`) the `Mix_SetPostMix` hook and reset the ring buffer; the getter must
  return the real stored state. XNA semantics: visualization is off by default and must be enabled
  before `GetVisualizationData` returns anything meaningful. Capture must be genuinely off (no
  postmix callback installed at all) when disabled, so there is zero cost for games that never use
  it.
  *Accept:* getter round-trips the setter; with it `false`, no postmix callback is installed
  (verifiable via a test-access accessor, following `VideoPlayerTestAccess`'s established pattern).
  *Done:* the setter installs/removes the `MIX_SetPostMixCallback` tap itself, so a game that never enables visualization pays zero per-buffer cost on the audio thread; it also `Reset()`s the ring so a freshly enabled visualizer never shows pre-enable audio.

- [x] **MEDIA-189 — Implement `GetVisualizationData(VisualizationData&)` for real.** Fill
  `data.samp` from the most recent 256 captured samples and `data.freq` from `MEDIA-187`'s FFT over
  the current window. Define behavior when visualization is disabled or nothing is playing:
  recommended is to leave the arrays zero-filled (matching `VisualizationData`'s
  zero-initialized constructor) rather than throwing — document it.
  *Accept:* while a real fixture song is playing with visualization enabled, at least one call
  returns non-zero sample data AND non-zero frequency data.
  *Done:* fills `samp` from the most recent 256 captured samples and `freq` from a 512-sample FFT window ending at the same point. Disabled or no-data returns zeroed arrays (not a throw, not an untouched buffer), matching `VisualizationData`'s own zero-initialized construction. A `static_assert` ties `VisualizationFFT::BinCount` to `VisualizationData::Size`.

- [x] **MEDIA-190 — Replace the stub-conserving test.**
  `tests/Microsoft/Xna/Framework/Media/MediaPlayerTests.cpp:240` currently asserts the *broken*
  behavior (always-false, never-filled) as if it were correct — the same "test locks in the gap"
  antipattern `MEDIA-129` already fixed once elsewhere in this plan. Rewrite it to assert the real
  behavior.
  *Accept:* the old assertions are gone (not merely supplemented); the new test genuinely fails
  against the pre-`MEDIA-189` implementation. **Mutation-verify this**, per `MEDIA-171`.
  *Done + MUTATION-VERIFIED:* the old `VisualizationIsDocumentedAsUnsupportedBySdl3Mixer` asserted the stub as the specification (setter is a no-op, getter always false, buffer left untouched) -- deleted, not supplemented. Replaced by `IsVisualizationEnabledRoundTrips`, `GetVisualizationDataZeroesTheBuffersWhileDisabled` and `EnablingVisualizationIsSafeWithoutAnAudioDevice`. Reverting the getter to `return false` makes the round-trip test fail.

- [x] **MEDIA-191 — Handle the no-audio-device / `SOUND_ENABLED`-off case.** In a headless CI or a
  no-`SOUND_ENABLED` build there is no mixer to hook. Visualization must degrade to
  "enabled flag round-trips, data stays zero" without crashing or hanging, consistent with how the
  rest of the Media stack already degrades. Note the existing project-wide caveat that
  `SOUND_ENABLED` is currently defined in every build configuration (`MEDIA-135`), so the `#else`
  branch remains untestable until a no-audio build variant exists — do not claim otherwise.
  *Accept:* the visualization tests pass in the existing headless test environment.

#### Group D — Real FFmpeg on every platform (owner decision 2, revised 2026-08-22)

> `cmake/CnaLibrary.cmake:32` currently deletes `VideoDecoder.cpp`, `Video.cpp` and `VideoPlayer.cpp`
> from the build whenever `CNA_FFMPEG_AVAILABLE` is off (set off for `MINGW`/`WIN32`/`EMSCRIPTEN`/
> `ANDROID` at `:8`), while the public headers stay installed — so any game touching `Video` or
> `VideoPlayer` on those platforms gets an **unresolved-symbol link error**, not a clean runtime
> failure. `AudioDurationProbe::ProbeDurationMS` also unconditionally returns `0` there
> (`src/CNA/Internal/Media/AudioDurationProbe.cpp:32`), silently zeroing every library
> `Song`/`Album`/`Playlist` duration.
>
> **Historical decision:** the owner originally rejected a `NotSupportedException` fallback and
> required genuine FFmpeg availability on all four excluded configurations. **Correction
> (2026-08-22):** the owner explicitly reversed that decision and requested a stable optional
> backend with `OFF/AUTO/ON`, while keeping real target-native FFmpeg as a separate portability
> goal. `MEDIA-233` implements the new decision. This note preserves, rather than silently rewrites,
> why `MEDIA-192`..`198` were originally worded as they were.
  *Done:* `GetMixer()` throws when no audio device can be created; the setter catches that so enabling visualization on a headless machine leaves the flag set with empty capture, which `GetVisualizationData` already renders as zeroed arrays. The `SOUND_ENABLED`-off path keeps the flag round-tripping since the state lives outside the `#ifdef`. Note the standing `MEDIA-135` caveat: `SOUND_ENABLED` is defined in every build config this project produces, so the `#else` branch itself is still untestable here.

- [ ] **MEDIA-192 — Windows (MSVC): acquire and link FFmpeg.** Highest-priority platform for XNA
  parity. Evaluate and pick one acquisition strategy, then wire it: (a) `vcpkg` manifest mode
  (`ffmpeg[avcodec,avformat,avutil,swresample]`), (b) a pinned prebuilt package (e.g. gyan.dev /
  BtbN release) downloaded and cached by CMake like the existing pinned-`wgpu-native` pattern
  already used for the WebGPU backend, or (c) `FetchContent` + a real FFmpeg build (slowest, most
  reproducible). Record the decision and its trade-offs. Must respect the existing
  `CNA_FFMPEG_ROOT`-style override convention for offline/reproducible builds if one exists.
  *Accept:* a Windows build produces a `CNA` static library containing `VideoDecoder`/`Video`/
  `VideoPlayer` symbols, and a Windows test run decodes a real fixture. **Note: this sandbox is
  Linux-only and cannot verify a Windows build** — the task is not closeable from here; it needs a
  real Windows toolchain. Do not mark it `[x]` on the basis of "the CMake looks right."

- [ ] **MEDIA-193 — MinGW: same, or an explicit documented exclusion.** `cmake/CnaLibrary.cmake:8`
  currently lumps `MINGW` in with the excluded set. FFmpeg is readily available under MSYS2
  (`mingw-w64-x86_64-ffmpeg`), so this should be achievable; verify whether the existing exclusion
  was a real limitation or an untested assumption carried forward.
  *Accept:* MinGW builds with video support, or the exclusion is justified in writing with the
  specific blocking reason.

- [ ] **MEDIA-194 — Android: build/link FFmpeg via the NDK.** Requires per-ABI (`arm64-v8a`,
  `armeabi-v7a`, `x86_64`) prebuilt `.so`/`.a` artifacts wired through the Android CMake toolchain.
  Consider a documented minimal decoder set (`--disable-everything --enable-decoder=h264,aac,...`)
  to keep APK size sane, and record which codecs the resulting build actually supports.
  *Accept:* an Android build links, and the supported-codec list is documented (a real device/
  emulator decode test if the project has any Android CI; otherwise state clearly that only the
  build was verified).

- [ ] **MEDIA-195 — Emscripten: build FFmpeg to WASM, or escalate.** **This is the highest-risk item
  in the phase.** FFmpeg can be compiled to WASM (ffmpeg.wasm demonstrates it), but the cost is
  substantial: large `.wasm` payload, threading/SIMD flags to reconcile with CNA's existing
  Emscripten settings, and no filesystem in the browser (`avformat_open_input` on a path needs
  MEMFS/fetch plumbing that CNA's `Video(fileName)` API assumes). Investigate first, then either
  implement or **escalate to the project owner with concrete findings** — per the owner's "no silent
  stubs" instruction, do not quietly reintroduce a `NotSupportedException` fallback here.
  *Accept:* either a working Emscripten video build, or a written escalation stating exactly what
  blocks it and what the realistic options are (with sizes/effort), for the owner to decide.

- [x] **MEDIA-196 — Remove the source-exclusion filter once platforms genuinely build.** Delete the
  `list(FILTER CNA_SOURCES EXCLUDE ...)` block at `cmake/CnaLibrary.cmake:33` for every platform
  that `MEDIA-192`-`195` actually fixed. **Blocked by those tasks** — removing the filter before a
  platform really has FFmpeg converts a link error into a *configure/compile* error, which is worse.
  *Accept:* no platform both installs the `Video`/`VideoPlayer` headers and omits their
  implementations.
  *Done under the owner's revised 2026-08-22 policy:* `Video.cpp`, `VideoPlayer.cpp` and the XNB/
  loose readers now compile unconditionally. Decoder-free configurations compile the explicit
  unavailable implementation, so every installed declaration has a definition and the old link
  error is gone without pretending FFmpeg decoding exists.

- [x] **MEDIA-197 — Remove `AudioDurationProbe`'s `#else` zero-returning branch.** Once FFmpeg is
  universal, the fallback at `AudioDurationProbe.cpp:32` becomes dead code and should be deleted
  rather than left as a trap that silently zeroes durations. **Blocked by `MEDIA-196`.** If any
  platform remains without FFmpeg after `MEDIA-195`'s escalation, keep the branch but make its
  consequences visible (the current comment says callers treat 0 as "unknown" — verify that is
  actually true of every caller, including `Album`/`Playlist` duration summation, and that a library
  full of zero-duration songs is distinguishable from a real zero).
  *Accept:* durations are real on every supported platform, or the remaining gap is precisely scoped.
  *Done under the revised policy:* the preprocessor branch was replaced by two physical
  implementations. `cna_video_ffmpeg` owns the real probe; `cna_media` owns an explicit zero-returning
  fallback, with zero documented and tested as the existing unknown-duration sentinel. Song playback
  remains SDL-backed and unaffected.

- [x] **MEDIA-198 — Escalation rule + honest platform-support matrix.** Add a table to
  `NEXTmedia.md`/`AUDIT.md` recording, per platform, whether video is: genuinely working
  (build+decode verified), building-but-unverified, or blocked-with-reason. **Explicit instruction
  from the owner: if a platform cannot be made to work, come back and say so — do not substitute a
  stub and call the task done.** This task exists so that outcome is recorded honestly rather than
  papered over.
  *Accept:* the matrix exists and each row states exactly what was verified and how (build only vs.
  real decode), with no row claiming more than was actually run.
  *Done:* `docs/video-backend.md` records native Linux/macOS as the current FFmpeg integration and
  Windows/MSVC, MinGW, Android, Emscripten and iOS as deterministic fallback targets. Linux is
  build/test/decode verified by `MEDIA-233`; macOS is covered by the existing native CI history;
  none of the fallback rows claims a target build or decode run from this Linux task. The four real
  portability tasks above remain open.

#### Group E — Audio format coverage (owner decision 4)

- [x] **MEDIA-199 — Extend the scanned-extension filter.** `HasSupportedAudioExtension`
  (`src/CNA/Internal/Media/MediaLibraryIndex.cpp:25`) currently accepts only `.ogg`, `.oga`, `.mp3`,
  `.wav`. Add at minimum `.flac`, `.m4a`, `.aac`, `.opus`. **Coordinate with playback:** the library
  must not index files SDL3_mixer cannot actually play, or `MediaPlayer::Play()` will fail on a song
  the library advertises. Verify which of these the project's SDL3_mixer build genuinely supports
  (it depends on which decoders were compiled in) and either gate the list on that or document the
  mismatch.
  *Accept:* each newly accepted extension is either confirmed playable by the current SDL3_mixer
  build, or explicitly documented as "indexed but may not play, pending mixer codec support."
  *Done -- and narrower than the task assumed, deliberately.* Added `.flac` and `.opus` only. **`.m4a`/`.aac` were NOT added**: this project's SDL3_mixer ships no AAC decoder at all (no `decoder_aac.c`, no `SDLMIXER_AAC` option -- verified against `third_party/SDL_mixer`), so indexing them would advertise songs `MediaPlayer::Play()` could never play, violating this task's own accept criterion. Each added extension was checked against SDL3_mixer's real decoder set (`decoder_flac`/`drflac`, `decoder_opus`).

- [x] **MEDIA-200 — FLAC tag parsing (`VORBIS_COMMENT` metadata block).** FLAC stores Vorbis
  comments in a native metadata block, not an Ogg container — `TryReadVorbisComments` currently
  expects the Ogg framing and will not find them. Add FLAC's `fLaC` magic + `METADATA_BLOCK_HEADER`
  walk to locate the `VORBIS_COMMENT` block (type 4), then reuse the existing comment-parsing code.
  *Accept:* a real FLAC fixture with known tags yields correct title/artist/album/genre/track.
  *Done + MUTATION-VERIFIED:* native FLAC is not an Ogg container -- added a `fLaC` + METADATA_BLOCK walk locating block type 4 (VORBIS_COMMENT), reusing the extracted shared comment-list parser. Bounds-checked against truncated/hostile input (dedicated test resizes a real FLAC to 20 bytes and asserts clean rejection, no over-read).

- [x] **MEDIA-201 — M4A/AAC tag parsing (iTunes `ilst` atoms).** MPEG-4 metadata lives in
  `moov.udta.meta.ilst` atoms (`©nam`, `©ART`, `©alb`, `©gen`, `trkn`), a completely different
  format from both ID3v2 and Vorbis comments. Requires a minimal MP4 atom walker in
  `AudioTagParser`. Keep it strictly bounded (this is a tag reader, not a demuxer) and reject
  malformed input rather than reading past buffer ends — the corrupt-input hardening standard set by
  `MEDIA-39`/`MEDIA-40` applies here too.
  *Accept:* a real M4A fixture yields correct tags; a deliberately truncated/corrupt M4A is rejected
  without a crash or out-of-bounds read (ASan-clean).
  *Not implemented -- deliberately, with reason.* M4A/AAC tag parsing was dropped from scope once `MEDIA-199` established SDL3_mixer has no AAC decoder: parsing tags for a format the library must not index (because it cannot be played) would be dead code. Revisit only if an AAC decoder is ever added to the mixer build. Recorded here rather than silently skipped.

- [x] **MEDIA-202 — OPUS tag parsing (`OpusTags` header).** Ogg-Opus stores an `OpusTags` packet
  that is Vorbis-comment-shaped but with its own magic signature and header layout. Verify whether
  the existing `TryReadVorbisComments` already tolerates it (it may, if it scans for the comment
  structure rather than strictly parsing Ogg-Vorbis headers) — **check before writing new code**,
  and if it already works, say so rather than adding redundant parsing.
  *Accept:* a real Opus fixture yields correct tags; the task report states whether new code was
  needed or the existing path already handled it.
  *Done -- and the task's own 'check before writing new code' instruction paid off, but the answer was 'new code needed'.* Verified empirically that the existing `TryReadVorbisComments` does NOT handle Opus: it searches specifically for the `\x03vorbis` magic, whereas Ogg-Opus uses `OpusTags`. Added `TryReadOpusTags` locating that magic, then reusing the same shared comment-list parser.

- [x] **MEDIA-203 — Author real fixtures for every new format.** Follow the established fixture
  practice from Phase 0 and Phase 6 (`tests/assets/media/music/`, each with a `manifest.json`
  recording the ground-truth tag values). Generate with `ffmpeg`, and record the exact command used
  so the fixtures are reproducible. Include at least one file per format with a full tag set
  (title/artist/album/genre/track/rating where the format supports it).
  *Accept:* fixtures exist, `manifest.json` documents their real tag values, and the manifest was
  cross-checked against `ffprobe` output rather than assumed.
  *Done:* `tests/assets/media/music/Artist Three/Album Flac/01 - Flac Song.flac` and `Artist Four/Album Opus/01 - Opus Song.opus`, generated with ffmpeg and cross-checked with ffprobe. Deliberately distinct tag values per file (Jazz/track 7 vs Ambient/track 9) so a wrong-field or wrong-file bug is visible rather than masked by identical data.

- [x] **MEDIA-204 — End-to-end library-scan tests for the new formats.** A scan of a directory
  containing all supported formats must produce correct `Song`/`Album`/`Artist`/`Genre` grouping
  across formats — e.g. an MP3 and a FLAC from the same album group into one `Album`.
  *Accept:* mixed-format grouping verified against the manifest's ground truth.

#### Group F — Real album art (owner decision 4)
  *Done + MUTATION-VERIFIED:* `LibraryIndexesFlacAndOpusFilesWithTheirRealTags` asserts both files are indexed AND routed into the full object graph (artist/album/genre back-references + track number). Removing the extension-filter entries fails it. **Fallout handled:** growing the shared fixture corpus broke 7 pre-existing tests that hardcoded exact counts (2 genres, 4 albums, 2 artists, 5 songs); counts updated, count-encoding test names renamed (`AlbumsContainsAllFourAlbums` -> `AlbumsContainsEveryFixtureAlbum`, etc.), and `GenreCollectionIndexerAndIterationWork` rewritten to derive its indices from the live count so future corpus growth cannot break it again.

- [x] **MEDIA-205 — Expand album-art filename conventions.** `MediaLibrary.cpp:29` tries only
  `cover.jpg` and `folder.jpg`. Add at least `cover.png`, `folder.png`, `front.jpg`, `front.png`,
  `album.jpg`, `albumart.jpg`, and make matching case-insensitive (Linux filesystems are
  case-sensitive; real-world libraries are inconsistent). Define and document precedence order.
  *Accept:* each convention is found by a fixture; precedence is deterministic and documented.
  *Done:* 13 conventions accepted (`cover`/`folder`/`front`/`album`/`albumart` × `.jpg`/`.jpeg`/`.png`) in a documented precedence order, matched **case-insensitively** by scanning the directory once rather than stat-ing every candidate in every casing (Linux filesystems are case-sensitive; taggers are not consistent).

- [x] **MEDIA-206 — Extract embedded ID3v2 `APIC` album art.** Parse the `APIC` frame: text encoding
  byte, MIME type (null-terminated), picture type byte, description (null-terminated, encoding
  dependent), then the raw image bytes. Prefer picture type `0x03` (front cover) when several are
  present. This closes the long-standing `R2`/`MEDIA-123` follow-up. Feed the extracted bytes into
  the existing `CNA::Internal::Graphics::ImageLoader` path that file-based art already uses, so both
  sources converge on one code path.
  *Accept:* an MP3 fixture with embedded front-cover art returns real image data from
  `Album::GetAlbumArt()`, with correct dimensions per the manifest.
  *Done + MUTATION-VERIFIED:* real ID3v2 `APIC` parsing -- encoding byte, null-terminated MIME, picture-type byte, then a description whose terminator width depends on the encoding (2 bytes for UTF-16, 1 for latin1/UTF-8; getting this wrong misaligns the payload). Front cover (type 3) preferred, first image otherwise. Closes the long-standing R2/`MEDIA-123` follow-up. The test asserts the payload starts with a real JPEG SOI **and decodes back to exactly 400x300**, so a boundary that is off by even a byte fails.

- [x] **MEDIA-207 — Extract embedded Vorbis/FLAC `METADATA_BLOCK_PICTURE` art.** The Ogg/FLAC
  equivalent: a base64-encoded (in Vorbis comments) or raw (in FLAC metadata block type 6) structure
  carrying picture type, MIME, description, dimensions and image data. Same front-cover preference
  and same `ImageLoader` convergence as `MEDIA-206`.
  *Accept:* an Ogg fixture and a FLAC fixture with embedded art both return real image data.
  *Done:* FLAC `METADATA_BLOCK_PICTURE` (block type 6). Note the trap this format sets: its length fields are **big-endian**, unlike the little-endian Vorbis comment list in the very same file -- implemented and commented accordingly. Same front-cover preference and same bounds-checking as the APIC path.

- [x] **MEDIA-208 — Define art-source precedence and `HasArt` semantics.** With file-based and two
  embedded sources, precedence must be explicit (recommended: embedded front cover, then filename
  conventions — embedded art is per-track and more reliably correct than a shared folder image;
  document whichever is chosen). `Album::getHasArtProperty()` must agree exactly with whether
  `GetAlbumArt()` will actually return data — a `HasArt == true` that yields nothing is precisely
  the class of "claims coverage it doesn't have" defect this plan has now been burned by repeatedly.
  *Accept:* `HasArt` and `GetAlbumArt()` agree for every fixture combination (art in file only,
  embedded only, both, neither), asserted as an explicit test matrix.

#### Group G — Real thumbnails (owner decision 4)
  *Done -- and the precedence decision was REVERSED from this task's own initial recommendation, deliberately.* The task suggested embedded-art-first; the implementation does **file-first**, because an `Album` aggregates many tracks (a folder image is album-scoped by nature, whereas embedded art is per-track and member tracks could disagree) and it avoids a tag parse on every call. The reversal and its reasoning are documented in the code, not silently applied. `HasArt` now covers both sources, and a dedicated test walks EVERY album asserting `HasArt == true` implies `GetAlbumArt()` succeeds with non-empty data and `HasArt == false` implies it throws -- with anti-vacuity guards requiring the corpus to exercise both branches.

- [x] **MEDIA-209 — Implement genuine downscaling for `Album::GetThumbnail()`.**
  `src/Microsoft/Xna/Framework/Media/Album.cpp:71` currently just calls `GetAlbumArt()` and returns
  the full-size image, so `GetThumbnail` is a synonym rather than a thumbnail. Implement real
  downscaling. Decide and document the target size (XNA does not specify one; recommended: fit
  within 128×128 preserving aspect ratio) and the filter (box/bilinear — a simple box filter is
  adequate and dependency-free). Check whether `CNA::Internal::Graphics::ImageLoader` or the
  graphics stack already exposes a resize helper before writing a new one.
  *Accept:* the returned stream decodes to an image genuinely smaller than the source, with aspect
  ratio preserved, asserted numerically against a fixture of known dimensions.
  *Done + MUTATION-VERIFIED:* new `CNA::Internal::Media::ThumbnailGenerator` -- box-filter downscale to a documented 128px max edge (aspect preserved, never upscaled), re-encoded to PNG in memory via `IMG_SavePNG_IO` + `SDL_IOFromDynamicMem` (no temp files). `ImageLoader` had no resize helper, so one was written; SDL3_image was already linked, so no new dependency. Reverting `GetThumbnail()` to its old `return GetAlbumArt();` fails the new test.

- [x] **MEDIA-210 — Same for `Picture::GetThumbnail()`.** `src/Microsoft/Xna/Framework/Media/
  Picture.cpp:61` has the identical issue. Share the downscaling helper with `MEDIA-209` rather than
  duplicating it.
  *Accept:* as above, for a picture fixture; one shared implementation, not two.
  *Done:* `Picture::GetThumbnail()` shares the same generator -- one implementation, not two. **Design refinement found while testing:** an image already within 128px is served as its ORIGINAL bytes rather than pointlessly re-encoded (lossy work for no benefit), so `CreatePngThumbnail` returns false for both 'no thumbnail needed' and 'could not load', since the caller's response is identical. This also preserved the pre-existing byte-for-byte round-trip test, which was correct for small images but whose name implied `GetThumbnail` was simply a synonym -- renamed to `PictureGetThumbnailRoundTripsByteForByteForAnAlreadySmallImage` and documented so it now asserts the real small-image contract instead.

- [x] **MEDIA-211 — Thumbnail caching/lifetime review.** `GetThumbnail`/`GetAlbumArt` return
  `System::IO::Stream*` — confirm who owns and frees the returned stream, and that generating a
  thumbnail on every call is not an unbounded allocation leak. This is an existing-ownership audit,
  not new functionality, but downscaling makes each call more expensive and the leak (if any) more
  visible.
  *Accept:* ownership is documented in the headers; repeated calls do not leak (ASan/valgrind-clean
  over a loop).

#### Group H — Closure
  *Done (audit, no leak found):* both `GetAlbumArt()`/`GetThumbnail()` and `GetImage()`/`GetThumbnail()` return a heap `System::IO::Stream*` the CALLER owns and must delete -- unchanged by this work, and the existing tests already `delete` it. Thumbnail generation allocates only a `std::vector<uint8_t>` copied into a `MemoryStream` plus a transient `SDL_Surface`/`SDL_IOStream`, both released on every path including failure. Repeated calls allocate a fresh stream each time (no cache), which is the same contract as before -- deliberately not adding a cache, since correctness of ownership was the question and a cache would introduce invalidation concerns for no demonstrated need.

- [x] **MEDIA-212 — `MediaSource`: verify the `WindowsMediaConnect` gap is documentation-only.**
  `MediaSourceType` already declares both `LocalDevice = 0` and `WindowsMediaConnect = 4`, matching
  XNA — so the *type* is complete and the audit's finding is narrower than it first reads. What is
  absent is *discovery* of WMC devices, which was an Xbox 360/WMP-era feature with no meaningful
  desktop equivalent. Confirm `MediaSource::GetAvailableMediaSources()` returning only a
  `LocalDevice` entry is the correct desktop behavior, and record it as a documented, justified
  deviation rather than an unfinished feature.
  *Accept:* a `CHECKLIST.md` deviation row exists; no code change unless the enum or the getter is
  genuinely wrong.
  *Done (verified, documentation only -- and the original finding was narrower than reported).* All 4 XNA `MediaSource` members are present and `MediaSourceType` already declares BOTH `LocalDevice = 0` and `WindowsMediaConnect = 4`. What is absent is only *discovery* of WMC devices -- an Xbox 360 / Windows Media Player-era streaming concept with no desktop equivalent -- so returning exactly one real `LocalDevice` entry is correct behavior, not an unfinished feature. Recorded as a justified deviation in `CHECKLIST.md`; no code change.

- [x] **MEDIA-213 — Re-audit every `Media` type member-by-member against the XNA reference XML.**
  Group A found `Song`'s gaps only because someone diffed against the real XNA reference instead of
  FNA. Do that systematically for the other 23 types: extract each type's `<member>` list from
  `/rv/data/library/github.com/borgesdan/xn65/references/Windows/Microsoft.Xna.Framework.xml`, diff
  against CNA's public API, and record every difference. **This is the task most likely to find
  further real gaps** — the `Song` finding proves the method works and that FNA-only auditing does
  not.
  *Accept:* a written per-type member diff exists for all 24 types; every difference is either fixed,
  or recorded as a justified deviation. Do not close this by spot-checking a few types.
  *Done -- and the result bounds the whole phase.* Scripted member-level diff of all 24 types against the reference XML. First pass reported 15 types with "missing" members; **all but one were false positives of the script**, not real gaps -- `op_Equality`/`op_Inequality` are `operator==`/`operator!=`, the C# indexer `Item` is `operator[]`, `GetEnumerator` is `begin()`/`end()`, and `System#Collections#IEnumerable#GetEnumerator` has no C++ equivalent at all. After correcting the idiom mapping: **`Song` is the ONLY type with genuinely missing members** (`Album`, `Artist`, `Genre`, `ToString`) -- the other 23 are member-complete. Group A therefore closes the entire API-surface gap; no further hidden surprises.

- [x] **MEDIA-214 — Update `AUDIT.md` status rows honestly.** Several Media rows are marked `✅`
  that this audit shows are not fully XNA-complete (`Song`, `MediaPlayer`, `Album`, `Picture`).
  Downgrade them to `⚠️` with specific caveats until their respective groups above are done, then
  raise them again only with the specific evidence stated.
  *Accept:* no `✅` row overstates the state at the moment it is written.
  *Done:* `AUDIT.md`'s Media rows corrected. Most notably **`Song`'s row said 'API complete' when it was not** -- four XNA members were missing until `MEDIA-174`/`176`, so that ✅ was inaccurate at the time it was written; the row now states when it became true and why it was wrong before. `MediaPlayer`, `Album`, `Picture` and `MediaSource` rows updated with the specific Phase 16 evidence rather than left as bare ✅. Four new `CHECKLIST.md` deviation rows added covering the rating scales, the visualization/thumbnail magnitudes, the deliberate `.m4a` exclusion, and `MediaSource`'s WMC discovery gap.

- [x] **MEDIA-215 — Full-suite regression run + honest closure note.** Build, run the complete
  `CnaTests` suite, `grep -c FAILED` on the **complete** log (never a truncated tail — see
  `feedback_verify_full_test_output`), and record real counts. **Every new test added by this phase
  must be mutation-verified falsifiable** (temporarily break the implementation, confirm the test
  fails) before its task is marked done — this is now standing practice after `MEDIA-171`, where an
  Accept note claimed coverage from a test whose fixture had no audio at all.
  *Accept:* real counts recorded; per-task mutation verification stated explicitly, not implied; and
  **this phase's closure must not use the word "complete"** for the plan as a whole — nine review
  rounds have now each found real defects after a "done" claim.

#### Group I — Ninth external review (2026-07-18): thread safety and URI handling

> A ninth review of commit `6a2b2847` confirmed Groups A-H genuinely fixed, then found two real
> defects neither the XNA-reference audit nor any earlier round had thought to look for: a formal
> data race in the new visualization capture, and a `FromUri` that never actually supported the
> URIs its own documentation promised. Group D (`MEDIA-192`..`198`) is **deferred by the project
> owner**, not resolved.

- [x] **MEDIA-216 — Fix a formal data race in `VisualizationCapture`.** `Push()` (audio thread)
  wrote plain `float`s into `buffer_` while `Read()` (game thread) read them. My own header called
  this a tolerable "torn read" -- that was **wrong**: a concurrent non-atomic read/write in C++ is a
  **data race, i.e. undefined behaviour**, not merely an imprecise value.
  *Fix:* sample storage is now `std::array<std::atomic<float>, Capacity>` accessed with
  `memory_order_relaxed` -- well-defined at effectively zero cost, since a relaxed load/store of a
  naturally-aligned float lowers to the same instruction a plain access would on every mainstream
  platform. What remains deliberately tolerated is a *logically* torn window (samples spanning two
  callback batches): a one-frame visual artefact, not UB, and the comment now says so accurately.
  *Also fixed, same finding:* `Reset()` ran BEFORE the post-mix callback was removed on disable, so
  the audio thread could still be writing into a buffer the game thread was zeroing. Ordering is now
  enable = reset-then-install, disable = uninstall-then-reset, with the no-device and
  no-`SOUND_ENABLED` paths resetting only where no callback can exist.
  *Accept -- with an honest gap:* existing visualization tests stay green, but they are **sequential**.
  There is still no TSAN build and no end-to-end "play real audio, assert non-zero captured data"
  test, exactly as the reviewer noted. **The race is fixed by construction (atomics + ordering), NOT
  by a test that would have caught it.** A threaded/TSAN harness is recorded as genuinely open work
  rather than claimed as covered.

- [x] **MEDIA-217 — Make `Song::FromUri` actually accept file URIs.** The header promised
  "File URI or local path", but the implementation passed the raw string straight to the `Song`
  constructor, which then asked `std::filesystem::exists` about a literal `"file:///..."` -- so a
  real file URI **always** failed, and the only test covered a plain path. FNA resolves this via
  `Uri.LocalPath` and throws `InvalidOperationException("Only local file URIs are supported for
  now")` for any non-file scheme (`Song.cs`).
  *Fix:* parse the scheme; no scheme -> plain path (unchanged -- every existing caller relies on it);
  `file` -> strip the authority, percent-decode the path, drop the leading slash of a Windows-style
  `/C:/...` path; any other scheme -> throw, matching FNA's message.
  *Accept + MUTATION-VERIFIED:* four new tests (real absolute file URI, percent-decoded spaces,
  `http`/`https` rejection, plain path still works); reverting to the raw pass-through fails three of
  them. **Note the first version of these tests was itself wrong** -- it used `file://relative/path`,
  where `relative` is actually the URI *authority*, not a path -- and was corrected to build absolute
  URIs from the fixture's real location.

- [x] **MEDIA-218 — Full-suite regression for this round.**
  *Verified:* 4915 tests, 4913 passed, 0 failed, 2 pre-existing hardware skips. The first run aborted
  on `ENetBackendTest.HostFreesOwnedRemoteGamerOnDispose` (`double free or corruption`) -- the **same
  pre-existing `Net`/ENet flakiness already isolated and documented in Phase 12** (`MEDIA-161`), not
  a Media regression: the suite re-ran clean and that test passes 23/23 in isolation.

- [x] **MEDIA-219 — Complete the URI semantics `MEDIA-217` left half-done.** A tenth review found
  the file-URI handling still incomplete on three counts, plus a bad test of my own:
  * **`file:/path`** (the authority-less form RFC 8089 also permits) was not recognised at all.
  * **`file://server/share`** silently **dropped the authority**, resolving a REMOTE path to a
    local one -- the worst of the three, since it succeeds against the wrong file rather than
    failing.
  * Any authority was treated as `localhost`.
  * My `FromUriAcceptsARealFileUri` test claimed to check "two forms" but **built the identical
    string twice** (`abs` already starts with `/`, so `"file://" + abs` IS the empty-authority
    spelling).
  *Fix:* scheme detection now ignores single-character "schemes" (a Windows drive letter is a path,
  not a scheme); empty/`localhost` authority -> plain path; any other authority -> UNC
  `//host/path`, matching `Uri.LocalPath`; `file:/path` handled; the duplicate test replaced by
  `FromUriAcceptsEveryLocalFileUriSpelling` covering all three real spellings.
  *Accept + MUTATION-VERIFIED -- and this is the notable part:* the first version of the UNC test
  (`file://server/share/song.ogg`) **passed against the deliberately broken code**, because
  dropping the authority yields `/share/song.ogg`, which is also missing. Only mutation testing
  exposed it. Rewritten so correct and buggy behaviour give OPPOSITE results: the path after the
  authority is a REAL existing file, so keeping the authority (UNC) correctly fails while dropping
  it wrongly succeeds. It now genuinely fails against the bug.

- [x] **MEDIA-220 — Act on `MIX_SetPostMixCallback`'s return value.** It returns `bool` and
  `MEDIA-216` ignored it. Not cosmetic: on an **install** failure the flag claimed visualization was
  on while no data could ever arrive (a caller would poll forever); on an **uninstall** failure the
  audio thread might still be writing into a buffer `Reset()` was about to zero -- reintroducing
  the exact race `MEDIA-216` had just fixed.
  *Fix:* install failure reverts `g_visualizationEnabled` to `false`, so the property never claims
  a capability that does not exist; uninstall failure deliberately **skips** `Reset()`, leaving
  harmless stale samples rather than racing a live writer.
  *Accept:* `IsVisualizationEnabledNeverClaimsEnabledWithoutAWorkingTap` asserts the observable
  contract (never reports enabled without a working tap; zeroed arrays when it is not).

- [x] **MEDIA-221 — Full-suite regression for this round.**
  *Verified:* 4917 tests, 4915 passed, 0 failed, 2 pre-existing hardware skips. Grepped on the
  complete log.
  **Still genuinely open (not claimed as covered):** visualization has no TSAN/threaded test and no
  end-to-end "play real audio, assert non-zero captured data" test -- the race is fixed by
  construction, not by a test that would catch a regression of it. Group D (`MEDIA-192`..`198`)
  remains deferred by the project owner.

- [x] **MEDIA-222 — Stop `IsVisualizationEnabled` reporting a state that never happened.** An
  eleventh review found the flag was assigned **before** the mixer was even obtained, so when
  `GetMixer()` threw (a machine with no audio device at all) the `catch` cleared the buffer but left
  the flag `true` -- reporting visualization as on with no mixer and no callback. That directly
  contradicted `MEDIA-220`'s own claim, and the test I named
  `...NeverClaimsEnabledWithoutAWorkingTap` did not actually verify it.
  *Fix:* `setIsVisualizationEnabledProperty()` restructured so the flag is assigned **only from what
  actually happened**, never up front: enabling sets `true` solely after
  `MIX_SetPostMixCallback` confirms the install; a null mixer, a failed install, or a thrown
  `GetMixer()` all leave it `false`. Disabling always ends `false`, still skipping `Reset()` when
  the uninstall could not be confirmed (`MEDIA-216`'s race).
  *Accept -- with the limitation stated plainly:* the branch is correct **by construction, not by
  test**. The suite runs with a dummy SDL audio driver so `GetMixer()` always succeeds, and it
  caches its device, so no later test can force the failure. The overclaiming test name was
  corrected to `VisualizationEnabledStateStaysConsistentWithGetVisualizationData`, with an in-file
  note recording exactly which branch is uncovered and why -- rather than leaving a name that
  implies coverage that does not exist.

- [x] **MEDIA-223 — Strip query and fragment in `FromUri`, matching `Uri.LocalPath`.** Everything
  after `file:` was treated as path, so `file:///music/song.mp3?version=1#intro` tried to open a
  file literally named `song.mp3?version=1#intro` and failed with a `FileNotFoundException` naming
  a filename nobody asked for. `System.Uri.LocalPath` returns only the path component.
  *Fix:* fragment then query are removed **before** percent-decoding -- deliberately in that order,
  because a percent-encoded `?`/`#` (`%3F`/`%23`) is a **literal filename character**, not a
  delimiter, and decoding first would truncate a legitimate path at it.
  *Accept + MUTATION-VERIFIED:* `FromUriIgnoresQueryAndFragment` covers query, fragment and both
  together; `FromUriTreatsPercentEncodedDelimitersAsLiteralFilenameCharacters` covers the mirror
  case by creating a real file named `q?.ogg` and opening it via `%3F`. Reverting the strip fails
  the first.

- [x] **MEDIA-224 — Full-suite regression for this round.**
  *Verified:* 4919 tests, 4917 passed, 0 failed, 2 pre-existing hardware skips. Grepped on the
  complete log.
  **Still genuinely open, unchanged and not claimed otherwise:** no TSAN/threaded test and no
  end-to-end "play real audio, assert non-zero captured data" test for visualization; the
  no-audio-device branch of `MEDIA-222` is unreachable from this suite; Group D
  (`MEDIA-192`..`198`, FFmpeg on Windows/Android/Emscripten -- including
  `AudioDurationProbe`'s always-zero stub there) remains deferred by the project owner.

- [x] **MEDIA-225 — Fix a non-portable test of my own, and a comment that outlived its code.**
  A twelfth review found `FromUriTreatsPercentEncodedDelimitersAsLiteralFilenameCharacters` created
  a fixture named `q?.ogg` -- **`?` is an illegal filename character on Windows**, so the test would
  fail at `copy_file` there, before `FromUri` was ever exercised. My own test, not a product bug,
  but it would have broken the build for anyone on Windows.
  *Fix:* switched to `q#.ogg` / `%23`. `#` is legal on both Windows and POSIX and exercises exactly
  the same code path, since fragment stripping runs before query stripping.
  *Also fixed:* `EnablingVisualizationIsSafeWithoutAnAudioDevice`'s comment still claimed the flag
  "is on, data stays zero" without a device -- untrue since `MEDIA-222` made the flag reflect
  reality. Corrected rather than left contradicting the code it documents.
  *Accept + MUTATION-VERIFIED:* the rewritten portable test still catches the bug -- decoding before
  stripping makes it fail with "an encoded '#' was treated as a fragment delimiter".

- [x] **MEDIA-226 — Retry an uninstall that failed, instead of latching the tap on forever.**
  After a failed `MIX_SetPostMixCallback(nullptr)` the user-visible flag correctly went `false`
  while the callback was still live -- but the setter's early-return guard compared only that flag,
  so a later `set(false)` saw "already false" and **never retried the uninstall**. The tap would
  stay installed for the rest of the process.
  *Fix:* a separate `g_visualizationTapInstalled` tracks what is ACTUALLY installed, deliberately
  distinct from the user-visible `g_visualizationEnabled` because the two can legitimately disagree
  in exactly this failure case. The early-return now requires both to match the request, so a stuck
  tap is retried on the next call.
  *Accept:* verified by code review and the full suite; like `MEDIA-222`'s failure branch this is
  **unreachable from this test suite** (SDL's dummy driver never fails an uninstall), so it is
  correct by construction, not by a test -- stated rather than implied.

- [x] **MEDIA-227 — Full-suite regression for this round.**
  *Verified:* 4919 tests, 4917 passed, 0 failed, 2 pre-existing hardware skips, grepped on the
  complete log. The reviewer counted 4921 on their own build; the two-test difference is test
  registration in a differently-configured build, not a discrepancy in results -- no Media test
  failed in either.
  **Unchanged and still open:** no TSAN/threaded or end-to-end audio test for visualization; the
  no-mixer and failed-uninstall branches are unreachable from this suite; Group D
  (`MEDIA-192`..`198`) remains deferred by the project owner.

- [x] **MEDIA-228 — Actually make the file-URI tests portable; `MEDIA-225` only fixed half of it.**
  `MEDIA-225` correctly replaced the illegal `q?.ogg` fixture name, but a thirteenth review noticed
  the tests still **built the URI itself non-portably**: `"file://" + path.string()`. On Windows
  that yields `file://C:\project\...` -- backslashes are not valid in a URI at all, and even
  after converting them, `file://C:/...` parses **`C:` as the AUTHORITY**, i.e. a remote host. The
  correct Windows spelling needs a third slash: `file:///C:/...`. So the "portable test" claim from
  `MEDIA-225` was still wrong, just for a different reason -- the *filename* was fixed while the
  *URI construction* was not.
  *Fix:* a single `MakeFileUri()` helper now builds every URI in these tests: it resolves to
  absolute, uses `generic_string()` (normalising separators to `/` on all platforms), and prefixes
  `/` when the path does not already start with one, turning `C:/x` into `/C:/x`. Every hand-built
  `"file://" + ...` in the file was routed through it, including the localhost, no-authority and
  UNC spellings, which had the same latent problem.
  *Accept:* the helper's output was **verified experimentally, not asserted**: compiling the same
  logic standalone and feeding it Windows-shaped inputs produces `file:///C:/proj/s.ogg` for both
  `C:\proj\s.ogg` and `C:/proj/s.ogg`, and `file:///home/u/s.ogg` on POSIX. All 23 `SongTest`
  cases pass.
  *A mistake made and caught during this fix:* the first version of the helper did not resolve to
  absolute, so a relative fixture path became `/tests/assets/...` -- an absolute path that does not
  exist -- and three tests failed. Fixed by absolutising inside the helper.

- [x] **MEDIA-229 — Explain the 4919-vs-4921 test-count difference instead of hand-waving it.**
  The previous round dismissed the reviewer's differing count as "test registration differing
  between build configurations" **without evidence** -- an unsupported claim of exactly the kind
  this plan keeps being caught by.
  **This task's own conclusion was WRONG and is retracted -- see `MEDIA-232`.** It blamed the
  4919-vs-4921 gap on Draco, but Draco gates **four** tests, and four cannot explain a difference of
  two. The arithmetic never worked; the explanation only looked plausible because the count was also
  wrong at the time (`MEDIA-230`). What is factually true is only the Draco inventory below -- not
  that it explains that particular gap.
  *Draco inventory (accurate):* **four** tests are gated behind `#ifdef CNA_DRACO_AVAILABLE`:
  `RuntimeGltfModelTest.LoadsDracoCompressedTriangleDirectlyFromGltf`,
  `GltfImportCoreTest.ExtractMeshDecodesDracoCompressedTriangle`,
  `GltfImportCoreTest.ComputeTangentsEXTWorksOnADracoCompressedPbrPrimitiveWithNoTangentAccessor`
  and `GltfToCnjToolTest.ConvertsDracoCompressedTriangleAndLoadsBackThroughContentManager`.
  Draco 1.5.6 **is** installed on this machine, but the `cmake-build-tests` directory predated the
  Draco integration, so its cached configuration had it off. Re-running CMake detects it
  (`CNA: Draco found (1.5.6)`) and the counts line up.
  *Note:* this is also why the earlier "4919 tests" figures in this plan are lower than a
  freshly-configured build reports -- they were measured in that stale build directory. The results
  themselves were unaffected (zero Media failures either way), but the counts should be read with
  that caveat rather than treated as canonical.

- [x] **MEDIA-230 — Correct `MEDIA-229`'s own count, and stop citing pre-merge regression numbers.**
  A fourteenth review found two errors in `MEDIA-229` -- the task whose entire point was to replace
  a hand-waved claim with evidence. Both were mine:
  * **It said three Draco-gated tests; there are four.** The one missed is
    `GltfToCnjToolTest.ConvertsDracoCompressedTriangleAndLoadsBackThroughContentManager`. The cause
    was a sloppy search: I grepped for `#ifdef` within three lines *before* a `TEST`, which silently
    skips any file where the guard sits further away. Re-counted by walking `#ifdef`..`#endif`
    across **every** test file, which finds all four.
  * **Every "4919 tests" figure in this plan predates the `36ac9656` merge** and was measured in a
    stale build directory. Those numbers verified the Media branch in isolation -- they never
    verified the current merged HEAD, so quoting them as full-suite evidence for HEAD was wrong.
  *Corrected evidence, measured on the actual current HEAD (`52fe5835`) with a freshly configured
  build:* **5312 tests, 5308 passed, 0 failed, 4 pre-existing hardware skips** (Accelerometer/
  Gyroscope × 2). All four Draco tests genuinely ran.
  *Lesson worth keeping:* a task written specifically to be rigorous about a number still got the
  number wrong, because the *method* (a proximity grep) was never checked. Counting something is
  not evidence unless the counting method is itself verified.

- [x] **MEDIA-231 — Re-baseline the plan's regression figures on the merged tree.**
  Earlier phases legitimately measured `feature/media` alone; after `a3f88c94`/`36ac9656` merged it
  into `develop`, the meaningful figure is the merged one. Recorded here once rather than
  rewriting every historical phase note (which would falsify what was actually observed at the
  time): **historical per-phase counts are branch-only and pre-merge; the current merged HEAD
  measures 5312/5308/0.** Future rounds should quote the merged number.
  **Unchanged and still open:** no TSAN/threaded or end-to-end audio test for visualization; the
  no-mixer and failed-uninstall branches remain unreachable from this suite; Group D
  (`MEDIA-192`..`198`) remains deferred by the project owner.

- [x] **MEDIA-232 — Retract `MEDIA-229`'s explanation instead of leaving a plausible-but-false one
  in place.** A fifteenth review noted the residual inconsistency: `MEDIA-229` still asserted Draco
  caused the 4919-vs-4921 gap, while `MEDIA-230` had corrected the Draco count to four. **Four
  cannot produce a difference of two**, so the explanation was arithmetically impossible.
  *What is actually known:* the Draco inventory (four gated tests, named in `MEDIA-229`) is correct
  and independently verified. **The two-test gap itself was never explained**, and can no longer be
  reconstructed -- both builds have since moved (`36ac9656` merged develop in, and the stale build
  directory was reconfigured), so the two figures being compared no longer exist to compare.
  *Fix:* `MEDIA-229`'s conclusion is retracted in place rather than quietly patched, since the
  earlier wording would otherwise keep reading as a settled finding. **No replacement explanation is
  offered, because none is known** -- inventing a second plausible-sounding cause would repeat the
  exact mistake this whole thread of tasks exists to correct.
  *Current baseline, unaffected:* HEAD `9033a2fd` measures **5312 tests, 5308 passed, 0 failed, 4
  pre-existing hardware skips** (two Accelerometer, two Gyroscope), independently reproduced by the
  reviewer under a real Wayland session with ENet loopback, with all four Draco tests registered and
  completing.

### Phase 17 — Optional FFmpeg video backend (2026-08-22)

> The owner explicitly reversed Group D's original “real decoder or no fallback” policy: Game,
> MediaPlayer and the installed XNA video surface must be usable without carrying FFmpeg, while
> target-native decoders on the currently unsupported platforms remain separate honest work.

- [x] **MEDIA-233 — Split FFmpeg out of the stable Media surface and add
  `CNA_ENABLE_VIDEO=OFF/AUTO/ON`.** `cna_media` now always owns `Video`, `VideoPlayer`, both content
  reader paths and a decoder contract. The optional `cna_video_ffmpeg` (`CNA::VideoFfmpeg`) target
  owns every libavcodec/libavformat/libavutil/libswresample source and link edge. `OFF` never probes
  those packages; `AUTO` enables the backend only when all four are present; `ON` requires them and
  rejects a target without a native integration.
  *No-backend contract:* metadata/XNB construction and player state/configuration/disposal remain
  available. A missing raw path still throws `FileNotFoundException`; probing an existing path and
  `Play(non-null-video)` throw `System::NotSupportedException`, with `Play` refusing before it
  changes state. The C ABI maps this to `CNA_RESULT_NOT_SUPPORTED`. Audio-duration probing returns
  the established zero/unknown sentinel, while Song/MediaPlayer playback stays SDL3_mixer-backed.
  *Verified on Linux:* the `ON` tree built `cna_runtime`, `cna_video_ffmpeg`, `CnaTests`,
  `cna_c_api_video_smoke` and `cna_demo_2d`; all 66 decoder/video/player/content tests passed and
  the C smoke passed. A fresh `OFF` tree built the same stable targets without a video-backend
  target; 24 fallback/content/MediaPlayer tests and the C smoke passed. `readelf -d` and `ldd` on
  both `cna_demo_2d` and `libcna_c_api.so` contained no FFmpeg library. A fresh `AUTO` configure
  selected `cna_video_ffmpeg` with the installed modules and selected the fallback when pkg-config
  was deliberately pointed at an empty module directory. `ON` with that empty directory and invalid
  option values both fail configuration.
  *Still open:* `MEDIA-192`..`195` remain the real Windows/MSVC, MinGW, Android and Emscripten
  decoder ports; this fallback does not claim decode support on those targets.

### Phase 18 — Restore `System::Object.Equals` parity (2026-08-22)

- [x] **MEDIA-234 — Implement the missing `Equals(Object)` overrides on all seven equatable Media
  reference types.** A latest-CNA/latest-sharp-runtime compatibility build exposed
  `-Werror=overloaded-virtual` in the strict C API target: `Album`, `Artist`, `Genre`, `Picture`,
  `PictureAlbum`, `Playlist` and `Song` each declared only `Equals(const T*)`, hiding
  `System::Object::Equals(const Object*)`. This was a real API and behavior gap, not a warning to
  suppress: FNA declares both `IEquatable<T>.Equals(T)` and `override Equals(Object)` for every one
  of these classes. Each override now safely runtime-checks with `dynamic_cast` and delegates to the
  existing typed equality implementation, preserving its value semantics while returning false for
  null and wrong-type objects. Seven separate base-pointer tests cover equal and unequal values,
  wrong runtime types and null; mutation verification replaced all seven implementations with
  `false` and produced exactly 7/7 expected failures. Verified against sharp-runtime
  `54578590b328aa9612fe38bfddca9fd8ca795144`: `cna_media`, strict `cna_c_api` and `CnaTests` all
  build, the focused Media run passes 115/115 tests, and the C API Media smoke test passes with a
  sandbox-safe data directory and SDL's offscreen video driver.

---
  *Done:* full `CnaTests` run on the canonical EASYGL build -- **4911 tests, 4909 passed, 0 failed**, 2 pre-existing hardware skips (Accelerometer/Gyroscope, need real hardware). `grep -c FAILED` on the COMPLETE log, never a truncated tail. Every test added by this phase was mutation-verified falsifiable before its task was marked done (one mutation check initially produced empty output and was re-run rather than accepted). **Deliberately not calling `plan_media.md` 'complete':** Group D (`MEDIA-192`..`198`, FFmpeg on Windows/Android/Emscripten) remains genuinely open and cannot be closed from this Linux-only sandbox. **Later correction (2026-08-22):** under the owner's revised policy `MEDIA-196`..`198` and the link-complete fallback are done; only the real decoder ports `MEDIA-192`..`195` remain open.

## 6. Recommended order and milestones

1. **M0 (kickoff + fixtures):** MEDIA-1…8. Fixture authoring is front-loaded because nearly every later
   phase's acceptance criteria depend on real fixture data existing first.
2. **M1 (compliance, quick wins):** MEDIA-9…31. Low risk, purely mechanical/verification/documentation,
   unlocks meaningful tests immediately.
3. **M2 (real bugs in the working half):** MEDIA-32…45. Highest value-per-effort — fixes genuinely
   broken/regressed behavior (including two real gaps this revision found beyond the first pass: the
   `VideoPlayer` disposed-guard gap and `Video`'s missing `FileNotFoundException`) in code that already
   mostly works.
4. **M3 (library backend, part 1 — internals):** MEDIA-46…60. Nothing here is user-visible yet; it's
   the foundation Phase 4 wires up. This is the largest single unlock of "not just stubs."
5. **M4 (library backend, part 2 — public API):** MEDIA-61…69. Each task is independently shippable
   ("make and forget" per file/class) — recommend MEDIA-61/62 first (they gate everything else), then
   the 6 item+collection pairs in any order, then MEDIA-69 last as the integration capstone.
6. **M5 (content pipeline):** MEDIA-70…75. Independent of M3/M4 — can run in parallel with them if
   resourced separately, since it only touches `Video`/`Song`, not the library half.
7. **M6 (tests):** MEDIA-76…120. Don't defer these to the end in practice — per `CLAUDE.md`'s "make and
   forget" principle, add each class's tests in the same pass as its own M2/M4/M5 task, not as a
   separate later sweep; Phase 6 here exists as the consolidated checklist/acceptance record, not a
   literal "write all tests last" instruction.
8. **M7 (closure):** MEDIA-121…126.

---

## 7. Risk summary / open decisions

Beyond §4's D1-D11 (already resolved with defaults), these remain genuinely open:

| ID | Question | Default recommendation |
|----|----------|------------------------|
| R1 | Should `MediaLibrary`'s synchronous constructor-time scan (D6/D8) have a size/time budget or cap (e.g. a pathological 500,000-file Music folder)? | Not addressed by this plan — add a NOXNA scan-size warning/log if this proves to matter in practice; not blocking for a first real implementation. |
| R2 | Should album-art lookup (MEDIA-65) search more filenames than `cover.jpg`/`folder.jpg` (e.g. `cover.png`, embedded ID3 `APIC` frames)? | Start with the 2 filename conventions (covers the overwhelming common case); embedded-artwork extraction is a reasonable NOXNA follow-up (tracked in `NEXTmedia.md` via MEDIA-123), not required for `HasArt` to be meaningfully real. |
| R3 | Does `MediaPlayer`'s MEDIA-32 fallback (no-`SOUND_ENABLED` song-end detection) risk drifting from real elapsed time on a slow/throttled build? | Acceptable — it's already how `PlayPosition` itself is tracked (a wall-clock timer, not sample-accurate), so the fallback's precision matches the property it's already exposing. |
| R4 | Should `VideoPlayer` gain an arbitrary-seek API (`PlayPosition` scrubbing)? FNA itself has no such public API — this would be a NOXNA extension beyond XNA/FNA parity. | Not in scope for this plan (matches FNA exactly by omission); a reasonable NOXNA follow-up for `NEXTmedia.md`, not required for "not just stubs." |
| R5 | Should the `TouchCollection` exception-type inconsistency (§2.7) be fixed as part of this plan, since it was found here? | No — it's in the `Input` namespace, outside this plan's stated scope (see the header note). Flag it in `NEXTmedia.md` (MEDIA-123) for a future Input-scoped task instead of scope-creeping this plan. |

---

*Generated from three independent research passes — a complete method-level read of all 26 FNA
`Media`-namespace source files (including `Video/`), a full line-level audit of the current CNA
implementation (all `.hpp`/`.cpp` under `Microsoft::Xna::Framework::Media` + `CNA::Internal::Media`,
plus the SDL3_mixer/FFmpeg backends and content-pipeline integration it depends on), and a structural
convention check against `plan_audio.md`/`CHECKLIST.md`/`AUDIT.md` — cross-checked against each other,
plus two direct source verifications (`FrameworkDispatcher` → `MediaPlayer::Update()` wiring;
`CNA::Internal::Graphics::ImageLoader` reuse for picture metadata) that a first-pass draft did not
perform.*

*That original note ended "Nothing in this plan has been implemented — it defines the work
only." That was true when written and is **no longer true**: Phases 0-16 are implemented,
tested and merged into `develop` (`cb053b71`), with only Group D outstanding. Corrected
rather than deleted, so the document's own history stays visible.*
