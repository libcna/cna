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
