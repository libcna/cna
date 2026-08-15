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
