# CNA C API Media Contract

## Scope and ownership

The media boundary comprises [`media.h`](../../modules/c-api/include/CNA/C/media.h),
[`media_library.h`](../../modules/c-api/include/CNA/C/media_library.h),
[`media_player.h`](../../modules/c-api/include/CNA/C/media_player.h) and
[`video.h`](../../modules/c-api/include/CNA/C/video.h). Current member mapping and limitations
are tracked in [`COVERAGE.md`](COVERAGE.md) and [`LIMITATIONS.md`](LIMITATIONS.md).

Routes taking `game` require a live owned or callback-borrowed game handle on its creation
thread; a running loop or active callback is not required. Object handles retain registry thread
affinity. General contracts are in [`HANDLES.md`](HANDLES.md),
[`ERRORS.md`](ERRORS.md), [`STRINGS_AND_BUFFERS.md`](STRINGS_AND_BUFFERS.md) and
[`CALLBACKS_AND_THREADING.md`](CALLBACKS_AND_THREADING.md).

| Result | Native ownership and C release obligation |
|---|---|
| Song, song collection, library, Video, VideoPlayer | Owned C handle; release with its family destroy route |
| Library entity/collection | Owned C view handle retaining the native library; release each view separately |
| Media queue | Owned C view handle naming the process-wide queue; destroy the view |
| Media-player event registration | Owned handle; unsubscribe explicitly |
| Video frame texture | Player-scoped borrowed handle; invalidated by a later player call |

Destroying a view releases its C reference, while explicit Dispose changes the shared native
object. Aliases can therefore observe each other's disposal. Disposing a library leaves previously
obtained entity/collection handles working: they retain its native storage. Releasing the library
handle first is also safe. All owned C views still require their own release.

## Identities, visualization and media sources

MediaState, MediaSourceType and VideoSoundtrackType use fixed-width canonical identities.
MediaSourceType is sparse: LocalDevice is 0 and WindowsMediaConnect is 4. Source-kind validation
must check membership rather than a dense upper bound. VideoSoundtrackType is descriptive metadata;
it does not select a ducking or mixing policy.

`CNA_VisualizationData` is a versioned 2,056-byte POD with two fixed arrays of 256 floats.
Initialization zeroes both. MediaPlayer's visualization query fills samples/frequency bins;
disabled visualization or no captured data produces zeros. Type-name routes take no instance/game.

Media-source count, kind, name and type-name routes enumerate independently. Temporary native
source objects are destroyed before return; consumers receive indices, not source handles.
Index/name calls can observe a changed enumeration. Source string conversion is its display name,
so the name routes serve that result too. The English Refresh display name is
`Local Windows Media Player library` (MediaSource constructor, IL_000e–0013,
`WmpMediaSource` resource); the single returned source has kind LocalDevice (0).

## Songs and collections

Direct song construction checks file existence without decoding. Missing files map to
`CNA_RESULT_IO`. An omitted display name stays empty. The duration overload stores the supplied
duration. Library scanning separately populates tags and probes duration where available;
direct construction does not perform that catalog scan.

`cna_song_create_from_uri` accepts local file URIs or native paths. Relative paths resolve
against TitleLocation; absolute non-file schemes return `CNA_RESULT_INVALID_STATE`. File-URI
query/fragment removal precedes percent-decoding, preserving encoded question/hash characters as
filename characters. Native path spellings keep their own resolver behavior.

Song equality/hash use the file path rather than C handle identity. Display-name routes serve
ToString; handle-text extension routes expose the stored path. Catalog rating zero is unrated.

A C-built song collection retains its song resources. Releasing original handles does not destroy
those songs, and `get_at` publishes another owned handle sharing the same song. A library collection's
song result retains the library instead. Count/index operations on a disposed collection return
`CNA_RESULT_INVALID_STATE`; collection disposal does not dispose the songs.

Song disposal guards ordinary metadata, comparison/hash and string operations, but the C surface
has exceptions: IsDisposed, type-name and stored-path queries remain available, and duration/
play-count extension setters do not check native disposal. Repeated Dispose is a no-op; every
handle still needs destruction. Type-name queries on disposed catalog entities, collections and
libraries likewise remain available; “every member except IsDisposed fails” is too broad.

## The library catalog and pictures

Opening a library scans the platform's music/picture locations. An empty catalog is an ordinary
result. Source-based creation accepts an enumeration index; the native library keeps that very
enumerated source (shared ownership) and the rest of the list is released. Only LocalDevice is
supported.

Albums, artists, genres, playlists, songs, pictures, picture albums and collections are native
library-owned objects reached through separately owned C view handles. Album equality includes
both name and artist; artist/genre/playlist comparisons use names. Optional relations include
album artist/genre, song album/artist/genre, picture album, picture-album parent, root picture
album and picture token lookup.

For an absent optional relation the route succeeds with `out_available = CNA_FALSE` and an
invalid output handle. Generated output validation clears that handle before the helper runs;
it is not preserved as a caller sentinel. A root album has no parent, and no readable picture
location can mean no root tree. An unknown picture token is a successful absence.

Album-art, image and thumbnail routes read an internally owned stream into bytes and close it
inside each call. Size and copy perform separate reads; files can change between them.
Insufficient capacity reports the required size without a partial byte-array write. Albums with
no art return `CNA_RESULT_INVALID_STATE`; HasArt is a useful precheck but cannot guarantee later
file readability.

Thumbnails are no longer universally identical to full images. Large decodable file images are
downscaled and encoded as PNG. Small images and unsupported/unreadable thumbnail sources fall
back to original bytes; embedded album art also uses original bytes because it has no thumbnail
file path. No stream object crosses the ABI.

Picture dates use signed 100-nanosecond ticks since the Unix epoch, while media durations are
ticks measured from zero. SavePicture accepts bytes or an open storage stream handle. The stream
remains caller-owned and its read position advances. Saving writes to the configured picture
location and updates the saved/global/parent collections; an unmeasurable image can still be saved
with zero dimensions. Token lookup can retrieve it later. See native scan/storage behavior in
the media implementation rather than assuming a live rescan on every catalog getter.

## MediaPlayer and its queue

`cna_media_player_*` routes expose process-wide transport, volume/mute/repeat/shuffle,
position/state/control and visualization settings. Most take a game; static event subscriptions
do not. Volume clamps to 0 through 1. Indexed collection Play and queue active-index assignment
store an unchecked index, which can leave no active song. Read back state to determine whether
decoding/playback began.

ActiveSongChanged and MediaStateChanged use owned registrations and shared family unsubscription.
The explicit raise routes dispatch the events; native update/FrameworkDispatcher processing
delivers pending playback notifications. Application changes affect this process-wide player.

`cna_media_player_get_queue` creates a new owned handle naming the same native singleton each
time. Release each with `cna_media_queue_destroy`; this does not clear/destroy the singleton.
Queue count/index/control routes use that handle without another game parameter.

Queue song retrieval returns a new independent Song created from the entry's path and name,
so it survives queue clear or a later Play. Queue Add also constructs a path/name copy before
native adoption. These are not full metadata clones: duration/play count and library relations
are not copied, and construction rechecks that the file exists. Returned songs need destruction.

## Video construction and availability

Direct video factories accept independently owned devices or valid borrowed devices, including
pre-run loans. Keep the native creating device alive for video playback. Presence is exposed by
`cna_video_get_has_graphics_device`; that route does not return a device handle or prove its
continued liveness.

Compiled loading through `cna_content_manager_load_video` produces a distinct owned Video value
on each call, including cache hits. It survives manager unload/destruction; device lifetime remains
a separate requirement. Metadata-only construction stores supplied values without probing the
file. Probing construction opens the file; an existing undecodable file leaves dimensions/frame
rate/duration zero. Missing files yield IO at probing construction; a missing file deferred until
Play yields INVALID_ARGUMENT.

The ABI is available with or without optional FFmpeg support. `CNA_ENABLE_VIDEO=OFF` omits its
dependencies while preserving metadata construction, compiled Video loading and player
state/configuration. Operations requiring probing/decoding return `CNA_RESULT_NOT_SUPPORTED`.
AUTO uses FFmpeg when found; ON requires it. See [`video-backend.md`](../video-backend.md) for
the native boundary. The current audit's SDLGPU configuration has a working decoder.

The URI factory accepts native absolute paths, decodes absolute file URIs, resolves relative URIs
against TitleLocation and rejects absolute non-file URIs with INVALID_STATE. It does not fetch
network media.

## VideoPlayer, association and frames

Play retains the native Video resource in the C player, so releasing the original Video handle
does not interrupt playback. `cna_video_player_get_video` returns the original handle while it is
live, without transferring ownership. If it has been released, the getter publishes a valid
player-scoped borrowed handle to the retained video. That renewed handle expires at the next call
on the same player, including another association query, Stop, Dispose or destruction; no explicit
Video destruction is required for it. Ordinary Video queries leave the borrow valid. Play can
consume the renewed handle on the same player or retain its video independently in another player.
The native association remains queryable after Stop.

A decoder-open failure for an existing undecodable file leaves the player stopped and clears its
native Video association. Fresh/failed-play players reject frame queries with INVALID_STATE.
Stop retains the association and last frame, resets position and stops decoding advancement.
Pause/Resume retain normal state semantics.

`get_texture` returns a player-scoped Texture2D handle. Draw/read/copy its pixels while the borrow
is valid; release belongs to the player. A later player route invalidates the old frame once it
reaches player lookup, even if a subsequent native operation fails. A call rejected earlier for
an invalid output or Boolean can leave that borrow intact. Another texture/frame query issues a
new handle even when it reports the same decoded frame.

`cna_video_player_get_frame_ext` additionally reports generation, presentation time and
availability in a versioned descriptor. Generation increases on decoded-frame updates, remains
monotonic across Stop/Play and does not increase merely because a new C handle is issued.
Compare generation within one player's lifetime. The descriptor is cleared after version
validation, before player lookup, so a failure can modify it. Its borrowed texture follows the
same lifetime as `get_texture`.

Volume outside 0..1 is INVALID_ARGUMENT without mutation; NaN is accepted by native ordered
comparisons. Disposal refuses transport, State, PlayPosition, frame queries and configuration
setters with INVALID_STATE. IsDisposed, Video, configuration getters and type-name queries remain
available. Disposal and destruction are distinct; release the owned player handle after Dispose.

## Validation evidence

The strict-C suites are `CApi_MediaSmoke`, `CApi_MediaLibrarySmoke`,
`CApi_MediaPlayerSmoke` and `CApi_VideoSmoke`, plus content, lifetime and ABI checks as applicable.
Catalog tests use isolated music/picture fixtures; playback tests select dummy audio and run
through the private display runner. VideoSmoke exercises real frame/generation behavior when
a decoder is available and reports that branch explicitly.

CNA-FIX-D15's retained-video regression fails before correction with INVALID_HANDLE when
querying width through the association after original-handle destruction. It passes after
correction in the existing multi-renderer build on private OPENGLES3 and SDLGPU, with the real
decoder branch explicitly exercised. It covers same-player replay through the renewed borrow,
independent retention in a second player, original/renewed handle transitions, Stop, Dispose,
destruction and frame invalidation after rejected Play. The C ABI, C17/C++23 header, export-count
and route-generation checks pass. No fresh sanitizer/leak-check or decoder-disabled build is
claimed for this repair.
