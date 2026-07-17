# plan_media.md — Completing and implementing the FNA Media → CNA port (C++ / XNA 4.0)

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
> explicit request for maximum thoroughness — **126 tasks**, not padded for a round number: every task
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
  `#ifdef SOUND_ENABLED`. `VideoPlayer`/`CNA::Internal::Media::VideoDecoder` run on **FFmpeg**
  (`libavcodec`/`libavformat`/`libavutil`/`libswresample`), gated behind `CNA_FFMPEG_AVAILABLE`, with a
  **hand-written YUV→RGBA conversion** (no `libswscale`, per `CLAUDE.md`). Both are a backend swap vs.
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
  immediately-following `swr_init()` call. Add explicit null/error checks that throw a clear, documented
  C++ exception instead of risking a null dereference.
  *Accept:* a fault-injection test throws cleanly instead of crashing; ASan-clean.

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

- [ ] **MEDIA-46 — `MediaLibraryPaths`: real per-OS root discovery (D1).** New
  `CNA::Internal::Media::MediaLibraryPaths` resolving the Music/Pictures roots via
  `SDL_GetUserFolder(SDL_FOLDER_MUSIC)`/`SDL_GetUserFolder(SDL_FOLDER_PICTURES)`; plus NOXNA static
  override hooks (`MediaLibrary::SetMusicRootEXT(path)` / `SetPictureRootEXT(path)`) so tests don't
  depend on the real user's actual folders having content.
  *Accept:* on a machine with no override set, resolves to a real existing directory (or a documented
  graceful fallback if the OS folder doesn't exist); override hook redirects scanning to MEDIA-3/4's
  fixture trees.

- [ ] **MEDIA-47 — `AudioTagParser`: Ogg page-framing primitive (D2).** New
  `CNA::Internal::Media::AudioTagParser` internals — minimal from-scratch Ogg page/segment parsing
  sufficient to locate the Vorbis comment header packet within an `.ogg` file, independent of the
  comment-field extraction itself.
  *Accept:* correctly locates the comment-header packet boundary against MEDIA-3's `.ogg` fixture(s).

- [ ] **MEDIA-48 — `AudioTagParser`: Vorbis comment-field extraction (D2).** On top of MEDIA-47, extract
  `TITLE`/`ARTIST`/`ALBUM`/`GENRE`/`TRACKNUMBER` fields from the located comment header.
  *Accept:* MEDIA-3's tagged `.ogg` fixture parses to the exact documented tag values; malformed input
  fails gracefully (no crash), falling through to MEDIA-51's heuristics.

- [ ] **MEDIA-49 — `AudioTagParser`: ID3v2 synchsafe frame-size decoding primitive (D2).** Minimal header
  parsing for ID3v2.3/2.4: the 10-byte tag header, synchsafe-integer size decoding, and per-frame header
  (`id[4]`, `size`, `flags`) iteration, independent of interpreting any specific frame's payload.
  *Accept:* correctly enumerates every frame (id + byte range) in MEDIA-3's ID3v2.3 and ID3v2.4
  fixtures.

- [ ] **MEDIA-50 — `AudioTagParser`: ID3v2 text-frame extraction with encoding handling (D2/D11).** On
  top of MEDIA-49, extract the 5 relevant text frames (`TIT2`/`TPE1`/`TALB`/`TCON`/`TRCK`), decoding the
  text-encoding byte (`0x00` Latin-1, `0x01` UTF-16+BOM, `0x02` UTF-16BE, `0x03` UTF-8) rather than
  assuming ASCII.
  *Accept:* MEDIA-3's ID3v2.3/2.4 fixtures parse to the exact documented tag values; a fixture with a
  non-Latin-1 encoded frame (if authored in MEDIA-3) decodes correctly; a file with no ID3v2 tag (or
  ID3v1-only) falls through cleanly to MEDIA-51.

- [ ] **MEDIA-51 — `AudioTagParser`: filename/folder fallback heuristics (D2).** When a file has no
  usable tags (or is `.wav`/an otherwise-unsupported-for-tagging format), derive Title from the
  filename, Album from the parent directory name, Artist from the grandparent directory name.
  *Accept:* MEDIA-3's untagged `.wav` fixture (in a `Music/SomeArtist/SomeAlbum/Track.wav`-shaped tree)
  produces the expected inferred Artist/Album/Title.

- [ ] **MEDIA-52 — `MediaLibraryIndex`: recursive song scan + grouping (D3).** New
  `CNA::Internal::Media::MediaLibraryIndex` — one-shot recursive scan of the Music root, building an
  in-memory index of every audio file found (`.ogg`/`.mp3`/`.wav`, tagged via MEDIA-48/50/51) as a real
  `Song`, grouped Artist→Album→Song and flat Genre→Song.
  *Accept:* against MEDIA-3's fixture tree, the resulting groupings are exactly correct; a file with an
  unsupported extension is skipped, not errored.

- [ ] **MEDIA-53 — `MediaLibraryIndex`: harden the recursive scan against symlink loops and
  permission errors.** A real filesystem scan of a real user directory can encounter a symlink cycle
  (infinite recursion risk) or a subdirectory the process can't read (permission-denied). Add loop
  detection (e.g. track visited canonical/real paths) and treat unreadable entries as skip-with-warning,
  not a crash or hang.
  *Accept:* a synthetic fixture with a self-referential symlink terminates instead of hanging; a
  permission-denied subdirectory (where the test environment allows creating one) is skipped, not fatal.

- [ ] **MEDIA-54 — Case-insensitive Artist/Genre name normalization (D10).** Grouping key for
  Artist/Genre is case-folded + trimmed; displayed `Name` keeps first-seen original casing.
  *Accept:* a fixture with the same artist tagged as `"Artist A"` in one file and `"artist a"` in another
  (add to MEDIA-3 if not already present) produces exactly one `Artist` grouping both songs.

- [ ] **MEDIA-55 — `MediaCollectionBase<T>`: shared collection backend (D9).** New NOXNA internal
  template backing storage/indexer/enumerator/`Dispose` logic reused by all 6 public collection types
  (`AlbumCollection`/`ArtistCollection`/`GenreCollection`/`PictureCollection`/
  `PictureAlbumCollection`/`PlaylistCollection`). Public class names and exception contracts stay fully
  distinct.
  *Accept:* instantiates cleanly for at least 2 different `T`s in a throwaway test; no XNA-facing class
  is renamed or merged.

- [ ] **MEDIA-56 — `PictureLibraryIndex`: real Picture/PictureAlbum tree scan, reusing `ImageLoader`
  (D4).** New `CNA::Internal::Media::PictureLibraryIndex` — recursive scan of the Pictures root; one
  `PictureAlbum` node per real subdirectory (with real `Parent` links forming an actual tree), one
  `Picture` leaf per image file; dimensions read via the **existing**
  `CNA::Internal::Graphics::ImageLoader::Load(path).width/.height` (do not reimplement image decoding),
  `Date` via filesystem last-write-time.
  *Accept:* against MEDIA-4's fixture Pictures tree, the resulting tree's parent/child links and
  per-picture metadata are exactly correct.

- [ ] **MEDIA-57 — `PlaylistParser`: M3U reader (D5).** New `CNA::Internal::Media::PlaylistParser` —
  reads path-per-line `.m3u` files (with optional `#EXTINF` comments tolerated/ignored or used for a
  title hint), resolving each entry against the already-built `MediaLibraryIndex`.
  *Accept:* MEDIA-5's `.m3u` fixture resolves to the correct `Song` sequence; an entry pointing at a
  nonexistent file is skipped, not fatal.

- [ ] **MEDIA-58 — `PlaylistParser`: M3U8 (UTF-8) support (D5).** Extend MEDIA-57 to treat `.m3u8` as
  UTF-8-encoded (vs. `.m3u`'s local/legacy encoding), otherwise identical parsing logic.
  *Accept:* MEDIA-5's `.m3u8` fixture (with a non-ASCII filename/title) resolves correctly.

- [ ] **MEDIA-59 — `SavedPictureStore`: real `SavePicture` backing (D7).** New
  `CNA::Internal::Media::SavedPictureStore` — writes `SavePicture(name, buffer)`/`SavePicture(name,
  Stream)` output to a real `Saved Pictures` subfolder of the Pictures root; the result backs
  `MediaLibrary::SavedPictures`/`RootPictureAlbum` for real.
  *Accept:* calling `SavePicture` creates a real file on disk, readable back as a `Picture` with correct
  `GetImage()` content.

- [ ] **MEDIA-60 — Internal-backend compliance pass.** SPDX + Doxygen (brief, internal-header style —
  matching `XactParser`/`AudioMixer`'s existing precedent, not the full public-API Doxygen bar) across
  every new file from MEDIA-46-59; confirm the new `.cpp` files are picked up by the existing
  `GLOB_RECURSE` with zero `CMakeLists.txt` changes needed.
  *Accept:* `cmake --build cmake-build-debug --target CNA` compiles the new files with no manual CMake
  edits.

### Phase 4 — Wire the real backend onto the public XNA API (one class at a time, "make and forget")

- [ ] **MEDIA-61 — `MediaSource`: real implementation.** `GetAvailableMediaSources()` returns a real
  single-element list (`{ MediaSource(LocalDevice, "<platform-appropriate name>") }`) backed by a now
  genuinely reachable internal constructor; `Name`/`MediaSourceType`/`ToString()` all real.
  *FNA:* MediaSource.cs (API shape only — no FNA logic to port, see §0).
  *Accept:* `GetAvailableMediaSources().Count == 1`; round-trips through `MediaLibrary(MediaSource)`
  (MEDIA-62).

- [ ] **MEDIA-62 — `MediaLibrary`: real implementation.** Both constructors real: no-arg triggers
  `MediaLibraryIndex`+`PictureLibraryIndex` scans (D6, synchronous); `MediaLibrary(MediaSource)` accepts
  the one real `LocalDevice` source instead of throwing. All 10 properties
  (`Albums`/`Artists`/`Genres`/`Pictures`/`Playlists`/`RootPictureAlbum`/`SavedPictures`/`Songs`/
  `MediaSource`/`IsDisposed`) return real, populated collections. `GetPictureFromToken`, both
  `SavePicture` overloads, `Dispose` all real.
  *FNA:* MediaLibrary.cs (API shape only).
  *Accept:* constructing against MEDIA-3/4's fixture Music+Pictures tree produces a `MediaLibrary` whose
  every property is populated and internally consistent (feeds MEDIA-69's full graph audit).

- [ ] **MEDIA-63 — `Genre` + `GenreCollection`: real implementation.** Backed by `MediaLibraryIndex`'s
  genre grouping (incl. MEDIA-54's normalization); `Albums`/`Songs`/`Name`/`IsDisposed` real;
  `Equals`/`GetHashCode`/`operator==`/`operator!=`/`ToString` real, by `Name`.
  *Accept:* per-genre album/song membership matches the fixture tree exactly; equality tests for
  equal/unequal genres.

- [ ] **MEDIA-64 — `Artist` + `ArtistCollection`: real implementation.** Backed by the index's artist
  grouping (incl. MEDIA-54's normalization); `Albums`/`Songs`/`Name`/`IsDisposed` real; equality set
  real, by `Name`.
  *Accept:* per-artist album/song membership matches the fixture tree exactly; equality tests.

- [ ] **MEDIA-65 — `Album` + `AlbumCollection`: real implementation.** Backed by the index's
  artist→album grouping; `Artist`/`Genre`/`Duration` (sum of member `Song.Duration`)/`HasArt`/`Songs`/
  `Name`/`IsDisposed` real. `GetAlbumArt`/`GetThumbnail`: real `Stream` if a same-directory cover image
  (`cover.jpg`/`folder.jpg`) is found (MEDIA-4's fixture), else a documented, XNA-plausible exception
  (not a crash) when `HasArt==false`. Equality set real, by `(Name, Artist)` (album names can collide
  across artists).
  *Accept:* `HasArt` correctly reflects fixture presence/absence of a cover file; `Duration` sums
  correctly; equality tests including the same-name-different-artist case.

- [ ] **MEDIA-66 — `Picture` + `PictureCollection`: real implementation.** Backed by
  `PictureLibraryIndex`; `Album`/`Date`/`Height`/`Width`/`Name`/`IsDisposed` real. `GetImage`/
  `GetThumbnail` return real `System::IO::Stream`s (via `System::IO::FileStream`/`MemoryStream`,
  already in sharp-runtime). Equality set real, by resolved file path.
  *Accept:* dimensions/date match the fixture files exactly; `GetImage()` content round-trips
  byte-for-byte against the source file.

- [ ] **MEDIA-67 — `PictureAlbum` + `PictureAlbumCollection`: real implementation.** Real tree node
  backed by `PictureLibraryIndex`; `Albums`/`Pictures`/`Parent`/`Name`/`IsDisposed` real, including the
  self-referencing root case (`MediaLibrary.RootPictureAlbum.Parent == nullptr`).
  *Accept:* walking `Parent` from any leaf reaches the root in exactly as many steps as the fixture's
  real directory depth.

- [ ] **MEDIA-68 — `Playlist` + `PlaylistCollection`: real implementation.** Backed by `PlaylistParser`
  results scanned from the Music root's `.m3u`/`.m3u8` files. `Duration` (sum of member
  `Song.Duration`)/`Songs`/`Name`/`IsDisposed` real. `Equals`/`GetHashCode` real, by `Name`.
  `operator==`/`operator!=` **keep** FNA's exact null-check shape
  (`ReferenceEquals(first,null) ? ReferenceEquals(first,second) : first.Equals(second)`), now backed by
  a real, non-throwing `Equals` instead of a stub.
  *Accept:* MEDIA-5's fixture `.m3u`/`.m3u8` resolve to the correct `Song` sequence and `Duration`;
  equality tests including both-null/one-null/both-real cases.

- [ ] **MEDIA-69 — Full cross-class object-graph integration audit.** Construct a real `MediaLibrary`
  against MEDIA-3/4's fixture tree and walk every relationship round-trip (e.g. `Genres[i].Albums[j]`'s
  `Songs[k]`'s containing `Album` equals `Albums[j]`; every `Picture.Album.Pictures` contains that
  `Picture`) to confirm the object graph is internally consistent, not just individually populated.
  *Accept:* all round-trip assertions pass against the fixture; this test doubles as the canonical
  "Media library is real" regression guard.

### Phase 5 — Content-pipeline (XNB) integration

- [ ] **MEDIA-70 — `VideoContentTypeReader`.** New `CNA::Internal::Xnb::VideoContentTypeReader`
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

- [ ] **MEDIA-71 — Field-read style: match `SongContentTypeReader`'s existing code style, not FNA's own
  internal inconsistency.** FNA's `SongReader` reads its one int field directly (`ReadInt32()`) while
  `VideoReader` reads its 5 fields via the more generic `input.ReadObject<T>()` — an FNA-internal C#
  implementation-detail inconsistency with no observable binary-format effect either way. Use direct,
  typed reads (matching whatever style `SongContentTypeReader` already established in CNA) for
  `VideoContentTypeReader` too, for internal consistency, rather than replicating FNA's own stylistic
  unevenness.
  *Accept:* code review confirms consistent read-call style between the two readers; binary layout
  compatibility is unaffected (verified by MEDIA-70's round-trip test).

- [ ] **MEDIA-72 — Register the new reader.** Add `RegisterVideoXnbReader()`, called from
  `XnbBuiltInReaders.cpp`, matching `RegisterSongXnbReader()`'s existing call-site pattern exactly.
  *Accept:* `ContentManager::Load<Video>("fixture")` works end-to-end.

- [ ] **MEDIA-73 — Content-pipeline fixture + round-trip test.** Source or construct a real
  `.xnb`-wrapped `Video` fixture (matching the `Song` fixture's own sourcing approach — a real
  MonoGame-produced `.xnb` plus its companion video file, vendored alongside it) and verify the full
  `ContentManager::Load<Video>()` path.
  *Accept:* test passes; companion video file resolves and is playable via `VideoPlayer`.

- [ ] **MEDIA-74 — Cross-reference `plan_xnb.md`.** Add a pointer row there documenting this reader's
  completion, matching how `plan_xnb.md` documents the `SongReader` task, keeping the two plans
  consistent for future readers.
  *Accept:* `plan_xnb.md` updated.

- [ ] **MEDIA-75 — Re-verify `SongContentTypeReader` after MEDIA-10.** Confirm the existing Song
  reader's own error path (an unresolvable `.ogg`/`.oga`/`.qoa` after extension-reprobing) now surfaces
  `System::IO::FileNotFoundException` end-to-end through `ContentManager::Load<Song>()`, not just at the
  raw `Song` constructor level.
  *Accept:* a negative-path content-load test confirms the corrected exception type propagates.

### Phase 6 — Complete test suite (Google Test)

> Rules from `CLAUDE.md`/`CHECKLIST.md`: every public method, **every overload**, operator and constant
> has ≥1 test; `==`/`!=`/`Equals` both equal and unequal cases; `ToString` format; `GetHashCode`
> consistency; `GetTypeName` exact value. Tasks below are split by behavior/class rather than bundled,
> matching that per-overload mandate.

- [ ] **MEDIA-76 — SongTests.** Both ctors + file-existence validation (now `FileNotFoundException`,
  MEDIA-10); `Name`/`Duration`/`PlayCount`; the 4 FNA-faithful constant getters (MEDIA-13), asserted
  explicitly as constants; `Equals`/`GetHashCode` (incl. MEDIA-14's content-based-hash regression)/
  `operator==`/`operator!=`; `Dispose`/`IsDisposed`; `FromUri`.

- [ ] **MEDIA-77 — MediaQueueTests.** `ActiveSong`/`ActiveSongIndex`/`Count`/indexer (incl. MEDIA-11's
  corrected exception type)/`Add`/`Clear`.

- [ ] **MEDIA-78 — SongCollectionTests.** Indexer (incl. MEDIA-12's corrected exception type)/`Count`/
  `IsDisposed`/`Dispose`/iteration (`begin`/`end`).

- [ ] **MEDIA-79 — VisualizationDataTests.** `Frequencies`/`Samples` sizing (`Size==256`) and zero-init.

- [ ] **MEDIA-80 — MediaPlayer-PlaybackTests.** `Play(Song)`; `Play(SongCollection)`;
  `Play(SongCollection, index)`; `Pause`/`Resume`/`Stop`; `State` transitions across all of the above.

- [ ] **MEDIA-81 — MediaPlayer-QueueNavigationTests.** `MoveNext`/`MovePrevious`;
  `IsRepeating`/`IsShuffled` (incl. MEDIA-33's can-repeat regression and MEDIA-34's
  duplicate-on-play regression); `Queue` state after navigation.

- [ ] **MEDIA-82 — MediaPlayer-VolumeMuteTests.** `Volume` (incl. clamping), `IsMuted`,
  `GameHasControl` (hardcoded-`true` regression, matching FNA).

- [ ] **MEDIA-83 — MediaPlayer-EventsTests.** `ActiveSongChanged`+`MediaStateChanged`, including a test
  that actually drives them through `FrameworkDispatcher::Update()` (confirming the real, already-wired
  call chain, not just a direct internal `OnActiveSongChanged()`/`OnMediaStateChanged()` invocation).

- [ ] **MEDIA-84 — MediaPlayer-VisualizationTests.** `IsVisualizationEnabled`+`GetVisualizationData`,
  including the current SDL3_mixer limitation's documented behavior (MEDIA-32-adjacent finding: no real
  visualization data available — confirm this is asserted, not silently ignored by the test).

- [ ] **MEDIA-85 — Enum tests.** `MediaSourceType`/`MediaState`/`VideoSoundtrackType` value/order tests
  (closes MEDIA-29/30/31).

- [ ] **MEDIA-86 — VideoTests.** All 3 constructors (incl. MEDIA-44's `FileNotFoundException`
  regression); `FromUriEXT`; `SetAudioTrackEXT`/`SetVideoTrackEXT`; codec-guess-equivalent behavior;
  Duration-hack behavior differs correctly between raw-file and XNB-sourced construction.

- [ ] **MEDIA-87 — VideoPlayer-PlaybackStateTests.** `Play`/`Stop`/`Pause`/`Resume`/`State`/
  `PlayPosition`, against a real fixture video.

- [ ] **MEDIA-88 — VideoPlayer-DisposalGuardTests.** MEDIA-43's regression: every public method throws
  the documented exception after `Dispose()`.

- [ ] **MEDIA-89 — VideoPlayer-LoopMuteVolumeTests.** `IsLooped`/`IsMuted`/`Volume` (incl. clamping),
  and MEDIA-37's AV1-keeps-audio regression.

- [ ] **MEDIA-90 — VideoPlayer-TrackSelectionTests.** `SetAudioTrackEXT`/`SetVideoTrackEXT` round-trip
  against a multi-track fixture (if available; otherwise document as not-yet-coverable and note the gap
  rather than skip silently).

- [ ] **MEDIA-91 — VideoDecoder-PixelFormatMatrixTests.** MEDIA-35's 4:2:0/4:2:2/4:4:4 regression using
  MEDIA-6's fixtures.

- [ ] **MEDIA-92 — VideoDecoder-BitDepthTests.** MEDIA-36's 8/10/12-bit regression using MEDIA-7's
  fixtures.

- [ ] **MEDIA-93 — VideoDecoder-AudioExtractionTests.** Audio-stream extraction/resample correctness,
  independent of video-frame decoding.

- [ ] **MEDIA-94 — VideoDecoder-ErrorHandlingTests.** MEDIA-38/39/40 regressions: alloc-failure
  fault injection, corrupt/truncated fixture, I/O-error-vs-EOF distinction — all against MEDIA-7's
  fixtures.

- [ ] **MEDIA-95 — VideoDecoder-TrackSwitchingTests.** `SetAudioStream`/`SetVideoStream` by index,
  re-opening codec contexts correctly.

- [ ] **MEDIA-96 — MediaSourceTests.** Real `GetAvailableMediaSources()`/`Name`/`MediaSourceType`/
  `ToString` (MEDIA-61).

- [ ] **MEDIA-97 — MediaLibraryTests.** Both constructors; all 10 properties; `GetPictureFromToken`;
  both `SavePicture` overloads; `Dispose`; against the Phase 4 fixture tree (MEDIA-62/69).

- [ ] **MEDIA-98 — GenreTests.** Equality set, `Name`, `IsDisposed`, `Albums`, `Songs` (MEDIA-63).

- [ ] **MEDIA-99 — GenreCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration against real
  fixture-derived genres (MEDIA-63).

- [ ] **MEDIA-100 — ArtistTests.** Equality set, `Name`, `IsDisposed`, `Albums`, `Songs` (MEDIA-64).

- [ ] **MEDIA-101 — ArtistCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-64).

- [ ] **MEDIA-102 — AlbumTests.** Equality set (incl. same-name-different-artist), `Artist`/`Genre`/
  `Duration`/`HasArt`/`Songs`/`Name`/`IsDisposed`, `GetAlbumArt`/`GetThumbnail` found-vs-not-found
  branches (MEDIA-65).

- [ ] **MEDIA-103 — AlbumCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration (MEDIA-65).

- [ ] **MEDIA-104 — PictureTests.** Equality set, `Album`/`Date`/`Height`/`Width`/`Name`/`IsDisposed`,
  `GetImage`/`GetThumbnail` real `Stream` content round-trip (MEDIA-66).

- [ ] **MEDIA-105 — PictureCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-66).

- [ ] **MEDIA-106 — PictureAlbumTests.** Equality set, `Albums`/`Pictures`/`Parent`/`Name`/`IsDisposed`,
  including the root-node `Parent==nullptr` case and a multi-level `Parent` walk (MEDIA-67).

- [ ] **MEDIA-107 — PictureAlbumCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-67).

- [ ] **MEDIA-108 — PlaylistTests.** `Equals`/`GetHashCode`, `operator==`/`operator!=` (both-null/
  one-null/both-real), `Duration`/`Songs`/`Name`/`IsDisposed` (MEDIA-68).

- [ ] **MEDIA-109 — PlaylistCollectionTests.** Indexer/`Count`/`IsDisposed`/`Dispose`/iteration
  (MEDIA-68).

- [ ] **MEDIA-110 — AudioTagParser-VorbisCommentTests.** MEDIA-47/48 regression against MEDIA-3's `.ogg`
  fixture(s), incl. malformed-input graceful fallback.

- [ ] **MEDIA-111 — AudioTagParser-ID3v2Tests.** MEDIA-49/50 regression against MEDIA-3's ID3v2.3/2.4
  fixtures, incl. the text-encoding-byte matrix (Latin-1/UTF-16/UTF-16BE/UTF-8).

- [ ] **MEDIA-112 — AudioTagParser-FilenameFallbackTests.** MEDIA-51 regression against MEDIA-3's
  untagged `.wav` fixture.

- [ ] **MEDIA-113 — MediaLibraryIndexTests.** MEDIA-52 scan-correctness regression, plus MEDIA-53's
  symlink-loop/permission-hardening regression.

- [ ] **MEDIA-114 — PictureLibraryIndexTests.** MEDIA-56 scan-correctness regression, confirming
  `ImageLoader` reuse produces correct dimensions.

- [ ] **MEDIA-115 — PlaylistParserTests.** MEDIA-57/58 regression, incl. the M3U8 UTF-8 fixture.

- [ ] **MEDIA-116 — MediaCollectionBase\<T\> template tests.** MEDIA-55's generic
  storage/indexer/enumerator/dispose correctness for at least 2 distinct `T`s, independent of any one
  concrete public collection type.

- [ ] **MEDIA-117 — MediaLibraryPathsTests.** MEDIA-46's real-OS-folder resolution (or documented
  graceful fallback) and the `SetMusicRootEXT`/`SetPictureRootEXT` override hooks.

- [ ] **MEDIA-118 — SavedPictureStoreTests.** MEDIA-59's real file write + read-back content round-trip.

- [ ] **MEDIA-119 — Artist/Genre normalization regression test.** MEDIA-54's case-insensitive-dedup
  fixture case, isolated from the broader MEDIA-100/98 tests for a focused regression guard.

- [ ] **MEDIA-120 — Full Media-namespace headless-safety pass.** Confirm every new test added in this
  phase runs correctly under `SDL_VIDEODRIVER=dummy` where applicable (matching `plan_cnb.md`'s
  headless-CI precedent), and grep the **full** `CnaTests` log for `FAILED` (not a truncated tail) to
  confirm zero regressions introduced by any Phase 6 addition before moving to Phase 7.

### Phase 7 — Documentation and closure

- [ ] **MEDIA-121 — Add every new deviation to `CHECKLIST.md`.** New Media-specific rows: (a) real
  local media library vs. FNA's permanent stub — the single biggest one, explain the "no FNA logic to
  port, built from scratch per §4" framing; (b) SDL-user-folder root discovery (D1); (c) internal
  from-scratch Vorbis/ID3v2 tag parser instead of a third-party tag library (D2); (d) M3U/M3U8 as the
  chosen Playlist on-disk format (D5); (e) point-in-time library snapshot, no live filesystem watching
  (D6); (f) FFmpeg-unified video decode replacing FNA's separate `dav1dfile`/`Theorafile` backends
  (§2.2); (g) `VideoSoundtrackType` remaining metadata-only, matching FNA exactly (§2.5); (h) AV1
  content keeping its audio track, beyond FNA's own limitation (§2.3); (i) `Song::GetHashCode()`
  content-based vs. FNA's identity-based (§2.4); (j) the project-wide out-of-range-indexer exception-type
  inconsistency and Media's chosen resolution (§2.7).

- [ ] **MEDIA-122 — Update `AUDIT.md`'s `Microsoft::Xna::Framework::Media` table.** Replace every
  "(stub behavior)" row with the real post-Phase-4 status. Do **not** add the new
  `CNA::Internal::Media::*` internal types to this table — matching the existing convention that Audio's
  internal `XactParser`/`AudioMixer` aren't listed in the public `Microsoft::Xna::Framework::Audio`
  table either.

- [ ] **MEDIA-123 — Create `NEXTmedia.md`.** Matching the existing `NEXTaudio.md`/`NEXTdevices.md`/
  `NEXTinput.md`/`NEXTnet.md` per-domain convention — summarize this plan's outcome, open follow-ups
  (e.g. NOXNA `Refresh()`, a playlist-writer, a taglib upgrade path if the internal parser ever proves
  insufficient, embedded-`APIC`-frame album art as a `GetAlbumArt` enhancement beyond MEDIA-65's
  filename-only lookup, the `TouchCollection` exception-type inconsistency flagged in §2.7 as an
  Input-namespace follow-up out of this plan's scope), for a future session's quick pickup.

- [ ] **MEDIA-124 — Build & report.** `cmake --build cmake-build-debug --target CNA` and
  `--target CnaTests` green. Report per `CLAUDE.md`'s "Build and Report" section: changed files, added
  stubs (should be none left unexplained), missing dependencies (should be none — §0 confirmed no new
  third-party libraries), intentional deviations (point at MEDIA-121), build result.

- [ ] **MEDIA-125 — Full-suite regression run.** Confirm zero regressions in the rest of `CnaTests`
  (grep the **full** log for `FAILED` — do not trust a truncated tail) beyond MEDIA-120's Media-scoped
  pass, and confirm headless-safety project-wide is unaffected.

- [ ] **MEDIA-126 — Establish this plan's own future-addendum convention.** Matching `plan_audio.md`'s
  real evolution (an initial plan, then supplementary `P9-*`/`Phase 10` sections appended as later
  re-audits found more), record explicitly at the top of this file (already done in the provenance note)
  that any future Media re-audit should append a new numbered phase here rather than silently rewriting
  earlier phases' history — so this plan's own completion record stays trustworthy over time.
  *Accept:* convention documented; no further code change.

---

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
perform. Nothing in this plan has been implemented — it defines the work only.*
