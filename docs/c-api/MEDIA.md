# CNA C API — media

`media.h` maps the `Microsoft::Xna::Framework::Media` module: playback of songs and video, the
device's media library, and the values those surfaces exchange. This page records only what a
consumer cannot read off the header. The general contracts live in [`HANDLES.md`](HANDLES.md),
[`OWNERSHIP.md`](OWNERSHIP.md), [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md),
[`ERRORS.md`](ERRORS.md) and [`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

The family is being mapped in slices; [`COVERAGE.md`](COVERAGE.md) is authoritative about which
rows exist today.

## Identities

`CNA_MediaState`, `CNA_MediaSourceType` and `CNA_VideoSoundtrackType` are fixed-width identities at
their canonical ordinals.

**`CNA_MediaSourceType` has a gap, and it is deliberate.** Its two values are **0 and 4**, not 0 and
1, because that is what the original API defines. The identity is not renumbered into a dense range,
so it has no `MAXIMUM` to compare against: a route that validates a source kind checks membership of
the two defined values instead of an upper bound. Every other identity in this header does have a
`MAXIMUM` and is validated against it.

`CNA_VideoSoundtrackType` is metadata only, in CNA as in the original — no playback path branches on
it to change volume or ducking. The identity exists so a consumer can read what a video declares,
not so it can expect mixing behavior to follow.

## Visualization data

`CNA_VisualizationData` is a plain 2,056-byte value rather than a handle with count/copy arrays,
because nothing about it is variable: both canonical buffers are fixed at
`CNA_VISUALIZATION_DATA_SIZE` (256) floats. The canonical type exposes the same two arrays both as
public fields and through getters; the one C value is both, since a second spelling would only let
the two disagree. `cna_visualization_data_init` reproduces the canonical constructor, which zeroes
both buffers.

Its type-name routes take no value and no game handle — the fully-qualified .NET name belongs to the
type, not to an instance.

## Media sources

The canonical enumeration allocates a fresh source list on every call with `new` and returns raw
pointers its caller would have to free. **None of that ownership crosses this ABI.** Each route —
`cna_media_source_get_available_count`, `cna_media_source_get_type_at`, the name count/copy pair and
the type-name count/copy pair — enumerates, reads the one source it was asked about, and destroys
the whole list before returning. An index is therefore a point-in-time value rather than a handle,
valid only until the device's source set changes, and there is nothing for a consumer to release.
The strict-C suite runs under AddressSanitizer with leak detection enabled, which is what actually
proves this rather than a comment claiming it.

The canonical string conversion returns the display name unchanged, so it needs no route of its own:
`cna_media_source_get_name_size_at`/`_copy_name_at` give both the name and the text. The type name,
unlike the visualization one, is addressed by index — the canonical member is an instance method and
the source object cannot be constructed from outside the library.

## Songs

A `CNA_SongHandle` is owned by whoever created it, and **several handles may share one song**:
releasing one handle never destroys a song that another handle — or a collection — still holds.
`cna_song_dispose` and `cna_song_destroy` are the canonical disposal-versus-release split the mouse
cursor and haptic device already use; disposal only marks the song, and every other member keeps
answering afterwards.

Three canonical behaviors are preserved rather than tidied up, and two of them will surprise a
reader of the C++ header:

- **An omitted name stays empty.** The canonical constructor's own documentation says the display
  name defaults to the file name; its implementation stores the empty string it was given. This ABI
  follows the behavior and says so, exactly as the touch-panel reset contract does.
- **Equality and the hash come from the file path, not from handle identity.** Two independently
  created songs over the same file compare equal and hash equal. That is a deliberate CNA
  improvement over FNA, whose identity-based hash lets two equal songs hash differently.
- **`getIsRated` is not "rating is nonzero".** Both tag formats reserve zero for unrated, so a file
  with an explicit zero rating still reports not-rated.

A song's file is only checked for existence, never opened or decoded, so a missing file surfaces as
`CNA_RESULT_IO`. `cna_song_create_from_uri` accepts a `file:` URI or a plain path and refuses any
other scheme with `CNA_RESULT_INVALID_STATE`; the canonical resolver strips a query or fragment
before percent-decoding, so a percent-encoded `?` or `#` stays part of the file name.

`cna_song_get_handle_text_size_ext`/`_copy_handle_text_ext` read the file path the song plays from —
the string equality and hashing are computed from — while the name routes read the display name. The
canonical string conversion returns the display name unchanged, so it needs no route of its own.

## Song collections

`cna_song_collection_create` takes an array of song handles and **retains every one of them**. The
canonical collection stores non-owning pointers, so a C caller that released its own handles right
after building a collection would otherwise be left with dangling elements; here the collection
keeps the songs alive, and `cna_song_collection_get_at` hands back a new handle sharing the same
song rather than a copy.

Canonical disposal **empties** the collection: afterwards its count is zero and every index is out
of range. The songs themselves are untouched, because the collection never owned them. Disposing
twice is a successful no-op; releasing the handle twice is not.

The canonical iterator pair and its two type aliases have no C counterpart and are recorded as
not-applicable — C reads the collection through the count and the indexer, the same decision the
touch collection already records.

## The media library and its catalog

`media_library.h` maps the catalog: the `CNA_MediaLibraryHandle` and the albums, artists, genres,
playlists and their collections reached through it.

**Everything except the library itself is a borrowed view, and every view keeps the library alive.**
None of the canonical entity types can be constructed from outside the library — they exist only as
the product of a scan — so C never creates one. A handle to an album, artist, genre, playlist, song
or collection holds a reference to the owning library, which means releasing the library handle
first is safe: the library object survives until the last handle into it is released. There is no
parent-before-child ordering rule to remember here.

`cna_media_library_create_from_source` takes the *index* of an enumerated media source rather than a
source handle, because no source handle exists in this ABI. The canonical constructor **borrows**
the source it is given — it copies that source's kind and name into an object of its own — so C
destroys every enumerated source before returning. That is not an assumption about the
implementation: leaving the selected source to the library leaked it under AddressSanitizer, which
is how the borrowing was established. A source whose kind is not the local device is refused with
`CNA_RESULT_NOT_SUPPORTED`, exactly as the canonical constructor refuses it.

Album equality deserves its own note: it is **not** the name alone. Album names collide across
artists, so the canonical comparison pairs the name with the artist, and two albums with no artist
match on the name. Artists, genres and playlists compare by name.

An album's artist and genre are optional, and a song's album, artist and genre are optional in the
same way — a song a caller built from a file path has no library context at all. All of these
follow the availability-separate-from-the-answer rule: the route succeeds, reports `CNA_FALSE`
through its availability flag, and leaves the output handle untouched.

**No stream crosses this ABI for album art.** The canonical `GetAlbumArt` and `GetThumbnail` return
a stream whose caller owns it; `cna_album_get_art_size`/`_copy_art` and the thumbnail pair read that
stream to its end and destroy it inside the call, so the image crosses as plain bytes. CNA generates
no separate thumbnail, so the thumbnail is the same image as the cover — canonical behavior, not a C
limitation. Both refuse an album without art with `CNA_RESULT_INVALID_STATE`; check
`cna_album_get_has_art` first.

The four entity collections share one C shape because the four canonical types are structurally
identical. Canonical disposal empties a collection — count zero, every index refused — while its
elements keep answering, since a collection never owned them. Their iterators and type aliases are
recorded as not-applicable, like every other collection in this ABI.

## Pictures

Pictures and picture albums are borrowed views like the music entities, with two shapes of their
own.

**The picture-album tree is the only tree in this family.** `cna_picture_album_get_parent` reports
availability rather than failing, and the root album is the one whose flag comes back `CNA_FALSE` —
that is how a caller walks upwards and knows when to stop. `cna_media_library_get_root_picture_album`
likewise reports availability, because a device with no readable picture location has no tree at
all.

**A picture's date is the ABI's first point in time.** Durations elsewhere are 100-nanosecond ticks
counted from zero; `cna_picture_get_date_unix_ticks` uses the same tick counted from the Unix epoch,
which is the canonical clock's own epoch. A picture whose file carries no timestamp reports whatever
the scan recorded.

Image and thumbnail bytes follow the album-art contract exactly: the canonical members hand back a
caller-owned stream, C reads it to its end and destroys it inside the call, and the image crosses as
bytes. The thumbnail is the same image as the full-size one — canonical, not a C limitation.

`cna_media_library_save_picture` takes bytes; `cna_media_library_save_picture_from_stream` takes a
**storage stream handle**, because a storage stream is the only byte source this ABI owns. The
stream is borrowed for the call and stays the caller's to close. An image the loader cannot measure
is still saved, with zero width and height, exactly as the canonical operation records it. A saved
picture joins the library's saved-picture collection and can be found again by its token —
`cna_media_library_get_picture_from_token`, where an unknown token is a successful "not found"
rather than a failure.

## Playback: the media player and its queue

`media_player.h` maps the static `MediaPlayer` as free `cna_media_player_*` routes, each taking an
active game handle — the player is process-wide state a running game owns, the same reason the input
captures take one.

Two canonical behaviors are preserved rather than tightened: the volume setter **clamps** to 0
through 1 instead of refusing an out-of-range value, and the indexed `play` overload does **not**
range-check its index — an out-of-range one simply leaves no active song. The two static events
become owned registrations with one shared `cna_media_player_unsubscribe_ext`, and the canonical
deferred raises are exposed so a C application can observe its own wiring without waiting for a song
to change.

**The queue is one process-lifetime object**, so `cna_media_player_get_queue` hands back a *view* of
it — the same shape a stock mouse cursor uses — and there is no route that constructs, moves or
destroys a queue, because C never can.

Two deviations inside the queue are forced by ownership and are worth knowing:

- **A queue entry crosses as an independently owned copy, not a view.** The canonical queue destroys
  its entries whenever it is cleared, which every `play` route does, so a borrowed handle would
  dangle. The copy carries the same file and name and therefore compares equal to the entry — song
  equality is the file path.
- **`cna_media_queue_add` appends a copy** for the mirror-image reason: the canonical `Add` adopts
  the pointer it is given, and C cannot hand a handle's object away without leaving the caller
  holding a stale handle. Appending a copy is exactly what the canonical player itself does when it
  enqueues a song.

Whether playback actually starts depends on the platform's ability to decode the file, not on this
ABI. `cna_media_player_get_state` reports what really happened, so a C application should read the
state back rather than assume `play` began playing.

## Video

`video.h` maps `Video` and `VideoPlayer`, the one media family that touches the graphics device.
Each video-creating route takes a callback-scoped borrowed device handle, which is where the
canonical device argument comes from — and the device is reported back only as **presence**
(`cna_video_get_has_graphics_device`), because a borrowed device handle is valid solely inside the
callback that produced it and handing one out later would be a promise this ABI cannot keep.

The ABI is identical with or without the optional FFmpeg backend. Configure
`CNA_ENABLE_VIDEO=OFF` to omit every FFmpeg dependency: metadata-only video construction and all
player state/configuration routes remain available, while file-backed construction and playback
return `CNA_RESULT_NOT_SUPPORTED`. `AUTO` (the default) uses FFmpeg when all required modules are
present and otherwise exposes that same deterministic fallback; `ON` requires the backend.

**The frame texture is solved by lifetime, not by copying.** The player owns and replaces its frame
texture, so `cna_video_player_get_texture` hands back a borrowed `CNA_Texture2DHandle` that the C
layer itself invalidates on the **next call to that player** — any later route, including another
`get_texture`, releases it. A stale frame handle therefore fails with `CNA_RESULT_INVALID_HANDLE`
instead of touching freed memory. Draw with it or copy its pixels before calling anything else on
that player.

Three canonical behaviors are reported rather than corrected:

- A file that exists but cannot be decoded leaves the video's width, height, frame rate and
  duration at **zero**. It is not an error; playing it is what surfaces the problem.
- Playing an undecodable file leaves the player **stopped**, and the canonical player then clears
  its video — so `cna_video_player_get_video` answers `CNA_FALSE`. Read the state back rather than
  assuming a play call started playback.
- The URI factory **does not parse URIs**. Unlike the song factory, it forwards its string straight
  to the file constructor, so a `file:` URI is not resolved and an `http:` one is simply a path that
  does not exist.

Asking for a frame before playback is an ordinary `CNA_FALSE`: the canonical implementation
deliberately answers null there, where the original API faults. Disposal makes every playback route
report the canonical disposed-object failure as `CNA_RESULT_INVALID_STATE`, while the disposal query
keeps answering.
