# CNA C API content contract

## Scope

The C content surface exposes the native `ContentManager` without trying to spell its C++
`Load<T>` template. Each supported native result type has a dedicated load route, while content
produced by a caller-registered reader uses one opaque foreign-object route. No C++ type identifier,
Sharp Runtime service provider or filesystem object crosses the ABI.

The public declarations are divided by the type they return:

| Asset/result | C route | Declared in | Lifetime after manager unload |
|---|---|---|---|
| `Texture2D` | `cna_content_manager_load_texture2d` | `CNA/C/content.h` | usable; destroy explicitly |
| `TextureCube` | `cna_content_manager_load_texture_cube` | `CNA/C/content.h` | usable; destroy explicitly |
| `SoundEffect` | `cna_content_manager_load_sound_effect` | `CNA/C/content.h` | usable; destroy explicitly |
| `SpriteFont` and atlas | `cna_content_manager_load_sprite_font` | `CNA/C/content.h` | usable; destroy font before atlas |
| `Effect` | `cna_content_manager_load_effect` | `CNA/C/effects.h` | disposed; handle remains for cleanup |
| `Model` | `cna_content_manager_load_model` | `CNA/C/models.h` | native resources disposed; handle remains for inspection and cleanup |
| `Video` | `cna_content_manager_load_video` | `CNA/C/video.h` | usable; destroy explicitly |
| `Dictionary<string, object>` | `cna_content_manager_load_object_dictionary_ext` | `CNA/C/content_readers.h` | owned dictionary handle remains valid |
| caller-defined object | `cna_content_manager_load_foreign_ext` | `CNA/C/content.h` | caller-owned pointer; must outlive the manager cache |

There is no dedicated C content-manager load route for `Song`, `Texture3D`, `Curve` or a standalone
`AnimationClip`. Adding one of those is an additive typed binding, not a redesign of `Load<T>`.
The generic CNB container and codec boundary is documented separately in [`CNB.md`](CNB.md).

## Manager creation and ownership

`cna_content_manager_create` accepts a valid game-device loan (including a valid pre-run loan) or
an owned standalone graphics-device handle. The manager is owned and owner-thread-affine, and its
root string is copied. With a game device it is a game child that survives the creating callback
and must be destroyed before the game. With a standalone device it records that device's owner
token; it does not become a child of an unrelated game. The manager does not retain the standalone
device object: keep that device alive until the manager and its loaded GPU resources are finished,
and destroy them before the device. Device destruction does not enforce this manager lifetime.

CNA constructs both C-created and game-owned managers without an `IServiceProvider`;
`cna_content_manager_get_has_service_provider` therefore reports `CNA_FALSE`, while GPU content
uses the explicitly associated graphics device. The device setter requires the same creating
owner token: the same game, or the same standalone device.

Only managers created for a game count toward its shutdown guard. Standalone ordinary and
resource-backed managers do not block destruction of an unrelated game, and destroying one does
not decrement the count of a live game-owned manager. CNA-FIX-D02's strict-C regression verifies
both standalone constructor routes and this mixed-owner case on OPENGLES3 and SDLGPU. The device
lifetime requirement above still applies.

`cna_game_get_content_manager_ext` instead returns the stable borrowed handle for the game's value
member. It accepts all ordinary content-manager operations, cannot be destroyed and becomes invalid
with the game. `cna_game_set_content_manager_ext` copies the source manager into that value member:
the source remains independently owned, later source changes do not reach the game, and an existing
borrowed handle continues to address the game's manager.

### ResourceContentManager boundary

`cna_content_manager_create_resource` creates the canonical `ResourceContentManager` shape and the
same owned C handle, but the C constructor cannot receive a Sharp Runtime
`System::Resources::ResourceManager`. It therefore uses CNA's no-resource constructor. Shared
manager operations work, but every asset load through this C-created resource manager currently
returns `CNA_RESULT_IO` explaining that no resource family was supplied.

The native two-argument `ResourceContentManager` is implemented and can load binary resources;
the missing part is specifically a C representation or producer for its `ResourceManager`. A C
consumer needing embedded data must currently use an ordinary content manager or provide its host
resource store above the ABI.

## Roots, names, paths and cache keys

Roots and logical asset names are copied UTF-8 string views. Invalid UTF-8 and embedded NUL values
are rejected. A root may be empty; a load's asset name may not. String outputs use the standard
size/copy protocol, omit the terminator and never perform a partial write.

Changing the root matches the native XNA-compatible property: any assignment is refused with
`CNA_RESULT_INVALID_STATE` while the cache is nonempty, including assigning the existing value.
The refusal preserves both root and cache. Call `cna_content_manager_unload` first; independently
transferred C handles survive that unload according to the table above.

`cna_content_manager_copy_asset_path` reports the root joined with the logical name whether or not
the file exists. `cna_content_manager_copy_normalized_key` reports the cache identity, which folds
backslashes to forward slashes and case; names with the same normalized key address the same cache
entry.

## Typed loads and disposal

The load routes delegate selection, decoding and caching to the native content pipeline. Missing,
malformed or wrongly typed content normally returns `CNA_RESULT_IO`; renderer or platform feature
refusals return `CNA_RESULT_NOT_SUPPORTED` where the route documents one. Invalid arguments,
encoding, handles and threads retain their ordinary results. Native exceptions never cross the ABI.

Texture2D and TextureCube handles preserve the native resource format and mip data. A cache hit
still produces a new C handle over the cached resource. SoundEffect is deliberately uncached, so
every successful call owns a distinct sound. SpriteFont returns two owned handles because both the
font and glyph atlas must be reachable; the font retains the atlas and must be destroyed first.
Video loading reads compiled metadata without requiring the optional decoder until playback; see
[`MEDIA.md`](MEDIA.md).

Effect supports compiled `.xnb`, stock-effect `.cnj` and custom-source `.cnj` assets. Model resolves
`.xnb`, `.cnb` and direct `.gltf`/`.glb` sources. Both follow native cache disposal: unloading or
destroying the manager disposes a loaded Effect and a Model's effects, buffers and content-owned
textures. Retained handles remain allocated and must be cleaned up, but applying the Effect or
drawing the Model returns `CNA_RESULT_INVALID_STATE`. A retained Model remains inspectable; child
handles it publishes are model-owned and must not be destroyed separately.

One Model limitation is format-specific: a direct glTF load publishes skins and skeletons, while a
CNB model currently returns an empty skin collection even when its `MSKL` chunk carries a skeleton.
That defect is `BINDFIX-035` in [`../xna-fna-behavior-decisions.md`](../xna-fna-behavior-decisions.md).

## Manifest and XNB reader usage

`cna_content_manager_refresh_content_manifest` rescans the content root. The first query builds the
snapshot lazily; later filesystem changes are invisible until another refresh. Each logical entry
reports its relative path, whether `.xnb` and `.cnj` forms exist, its loose native extensions and
the reader names found in its XNB type table. Malformed XNB reader inventories are skipped rather
than aborting the whole scan, and compressed XNB containers use the bounded native decoder.

The native manifest comes from an unordered map, so entry order is unspecified but stable until a
rebuild. The reader-usage summary aggregates file counts and current registry presence. Because the
native summary is unordered and recomputed, the C API sorts it by reader name before indexed access.

## ContentReader and ContentTypeReader

`CNA/C/content_readers.h` exposes the lower compiled-XNB reader layer. An owned
`CNA_ContentReaderHandle` wraps a readable storage stream positioned after the container header and
optionally borrows a content manager for external references. It exposes the asset name, version,
platform, fixed XNA value reads, type-reader initialization, shared-resource completion, collection
and decoded-byte bounds checks, and an exact-byte read whose capacity refusal consumes no input.

The reader borrows the stream, so closing the stream while the reader lives returns
`CNA_RESULT_INVALID_STATE`. Destroying the reader closes the native stream; the storage handle still
has to be released separately, and its close is idempotent. A new reader over that file needs a
fresh stream.

`CNA_ContentTypeReaderHandle` exposes a reader's target type name, version, supported-version test,
initialization, in-place capability and untyped dispatch. The process-wide type-reader registry can
be queried, cleared, asked to create a reader, or extended with a caller callback table. A caller
registration owns an explicit handle and unregistering it frees the name; duplicate registration is
refused so a live handle can never silently refer to somebody else's factory. Callback reader
instances and their borrowed `CNA_ContentReaderHandle` live only for the load in progress.

The known-unsupported built-in set is currently empty. The hook remains idempotent for future
recognized formats, and `cna_known_unsupported_content_type_reader_create` can still construct an
explicit reader that always refuses compiled platform shader bytecode.

### Where native type erasure stops

`cna_content_reader_read_object_tag` and `cna_content_type_reader_read_untyped` execute native
type-erased dispatch but can return only whether a value was produced. They cannot return an
arbitrary `std::any` to C. Typed `ReadObject<T>`, `ReadRawObject<T>`, `ReadSharedResource<T>`,
`ReadAsset<T>` and `ReadExternalReference<T>` therefore have no generic C form.

Caller-defined objects are different because their representation is already an opaque `void*`.
That pointer is never dereferenced, copied or freed by CNA, and is returned through the foreign or
reflective-object routes described below.

## Caller-defined readers

There are three registration scopes:

- `cna_content_type_reader_manager_register` installs an XNB reader process-wide and returns an
  owned registration handle that can unregister it.
- `cna_cnb_loader_registry_register` installs a custom CNB loader process-wide; its full contract is
  in [`CNB.md`](CNB.md).
- `cna_content_manager_register_cnj_loader_ext` installs a named CNJ loader on one manager. The
  manager owns the registration and the native surface has no unregister operation.

`cna_content_manager_load_foreign_ext` is the common cached producer for all three. The file carries
the dispatch identity, so the call needs no runtime type argument. A second load of the same key
returns the same opaque pointer without rerunning the loader. The caller owns that pointer and must
keep it alive while the manager may still return it; unload drops the cache but does not free the
object. An unregistered or refusing loader fails the load instead of guessing a type.

### Reflective XNB readers

The reflective builder maps XNA's template reader into C callbacks and offsets. The caller supplies
an object factory, then declares members in wire order as fixed fields, named enums or custom field
callbacks. Fixed kinds cover Boolean, Single, Double, Int32, UInt32, Int64, Byte, Vector2/3/4,
Matrix, Quaternion, Color and TimeSpan. Canonical-name helpers publish the exact registry keys for
the reflected type and its enums.

Registration has no undo because the native reflective registration has none, and the first reader
registered under a canonical name wins. Use value-shaped registration for a root object and
reference-shaped registration for nested collections or `Model.Tag`; choosing the value shape for
a reference-shaped Model tag makes the Model load fail. Objects returned by the factory remain
caller-owned.

## Object dictionaries and Model tags

An XNB `Dictionary<string, object>` is reachable either as a root asset through
`cna_content_manager_load_object_dictionary_ext` or from a loaded Model's content tag. The owned
dictionary handle preserves its managed runtime identity and stable key order. Each entry can be
described before reading it, including scalar versus array and element count.

The C layer names Boolean, Int32, Single, Double, String, Vector2/3/4, Matrix, Quaternion, Color,
BoundingSphere, BoundingBox and caller-reflective foreign objects. Fixed-layout scalars and arrays
copy into caller buffers; strings and foreign pointers use dedicated routes. An unknown entry stays
inspectable through a diagnostic C++ type name, but that toolchain spelling is not an ABI identity.
An absent key or wrong requested kind returns `CNA_RESULT_INVALID_ARGUMENT` with the error message
distinguishing the native key-not-found and invalid-cast cases.

## Verification

The principal strict-C regressions are `CApi_ContentSmoke`, `CApi_ContentReaderSmoke`,
`CApi_ContentModelEffectSmoke`, `CApi_ContentExceptionSmoke`, `CApi_VideoSmoke` and the game-content
coverage in `CApi_RuntimeGameSmoke`. They cover typed and foreign loads, UTF-8 names, caching,
root changes, unload/disposal, manifests, custom and reflective readers, object dictionaries,
Model tags, stream lifetime and game manager borrowing/copying.
