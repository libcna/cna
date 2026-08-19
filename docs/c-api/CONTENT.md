# CNA C API Content Contract

## Scope

The content surface owns a native `ContentManager` and exposes three approved typed load routes:
Color `Texture2D`, `TextureCube` and `SoundEffect`. The C API does not attempt to express the C++
`Load<T>` template and never accepts a runtime type name, C++ type identifier, service-provider
pointer, path object or stream object.

Alongside loading, the manager exposes its resolved asset path and normalized cache key, built-in
loader registration, service-provider presence, graphics-device get/set and the content manifest
and `.xnb` reader-usage snapshots. `cna_content_manager_create_resource` produces the same owned
handle backed by the canonical `ResourceContentManager`.

The compiled-asset reader pipeline is covered too, in `CNA/C/content_readers.h`: an owned
`CNA_ContentReaderHandle` over a storage stream, an owned `CNA_ContentTypeReaderHandle`, and the
process-wide type-reader registry. See *The compiled-asset reader pipeline* below for what that
surface can and cannot express.

`cna_content_manager_create` accepts the callback-scoped graphics-device handle so the resulting
manager is tied to the same active game and device. Creation therefore occurs during a lifecycle
callback, but the owned manager survives that callback. It must be destroyed on the game creation
thread before the parent game.

## Roots and asset names

The root directory and logical asset name are UTF-8 string views copied during each call. Embedded
NUL scalar values and invalid UTF-8 are rejected. The root may be empty; an asset name may not.
Root-directory output uses the normal byte count/copy protocol and never appends a terminator or
writes a partial string.

The strict-C regression creates and loads an actual fixture whose filename contains valid UTF-8,
in addition to rejecting malformed and embedded-NUL names. Missing assets deterministically map to
`CNA_RESULT_IO` without returning a resource handle.

Changing the root directory matches the native property behavior: existing cache entries are not
automatically removed. Call `cna_content_manager_unload` when a root change must also invalidate
the native asset cache.

## Texture ownership and cache behavior

`cna_content_manager_load_texture2d` delegates resolution, decoding and caching to the canonical
native `ContentManager::Load<Texture2D>`. The initial route accepts only a loaded
`SurfaceFormat::Color` result. Each successful call returns a new owned C Texture2D handle, even
when the native manager satisfies the load from its weak texture cache.

The returned C texture is an independently owned game child. It remains valid after
`cna_content_manager_unload` or `cna_content_manager_destroy` and uses the same query, RGBA8
readback, SpriteBatch and destroy functions as a directly created C texture. It must be destroyed
before the parent game. Destroying or unloading the content manager does not implicitly destroy
issued C resource handles.

`cna_content_manager_unload` clears the manager's native asset and texture caches. Destroy performs
the same native disposal and invalidates only the manager handle.

## Failures and unavailable types

Missing, unreadable or undecodable assets return `CNA_RESULT_IO` with a per-thread diagnostic.
Renderer refusal and a successfully decoded non-Color Texture2D return
`CNA_RESULT_NOT_SUPPORTED`. Argument, UTF-8, handle and thread failures retain their standard C API
results. No native exception crosses the ABI.

Songs, video, models and 3D textures still have no typed C load route. Each needs its own
load function and ownership contract, because C cannot name the C++ type the generic `Load<T>` is
instantiated with. Fonts are no longer on that list -- see `cna_content_manager_load_sprite_font`
below -- and an asset of a type CNA does not know at all is reachable through
`cna_content_manager_load_foreign_ext`, described under *Readers a caller supplies*.

## Typed loads beyond Texture2D

`cna_content_manager_load_texture_cube` and `cna_content_manager_load_sound_effect` map the
canonical `Load<TextureCube>` and `Load<SoundEffect>` specializations. Neither is cached in the
canonical implementation, so every successful call returns an independently owned handle; both
resolve an extension-less asset name through their reader's own extension list.

Their failure results differ, and the difference is canonical rather than a C-layer choice: a cube
asset that cannot be decoded or does not exist returns `CNA_RESULT_IO`, and a backend without cube
storage returns `CNA_RESULT_NOT_SUPPORTED`, while the canonical sound loader reports a missing or
unreadable audio file — and an absent audio device — as `CNA_RESULT_NOT_SUPPORTED`. Branch on the
result, not on the renderer or on an assumed audio device.

`cna_content_manager_load_sprite_font` maps `Load<SpriteFont>`, which reads both the `.xnb` font
container and CNA's own `.cnj` font descriptor, so a consumer never has to parse either to obtain a
font this ABI accepts. It is the one loader with **four** parameters: a SpriteFont is a font *and*
the texture it draws from, and both have to be nameable — the font for measuring, layout and
`cna_sprite_batch_draw_string`, the atlas for a consumer that places glyphs itself from
`cna_sprite_font_copy_glyphs`. Both handles are owned, and the atlas refuses to be destroyed while
the font lives, which is the same ordering rule `cna_sprite_font_create` already imposes.

`cna_content_manager_load_effect` maps `Load<Effect>` -- the route an XNA game's
`ContentManager.Load<Effect>` takes -- and reads all three shapes CNA supports: a compiled `.xnb`
Effect asset, a `.cnj` descriptor naming one of the stock effects, and a `.cnj` descriptor carrying
custom shader source. It is declared in `CNA/C/effects.h` rather than here, beside the rest of the
effect surface, because that is where its `CNA_EffectHandle` return type lives. Which of the three
shapes an asset is decides which failures are possible, so branch on the result rather than on the
file name: only the compiled shape depends on
`CNA_GRAPHICS_CAPABILITY_COMPILED_EFFECTS`.

One failure is worth naming because it used to be reported wrongly: an `.xnb` whose root reader
produces a **different type** than the loader wants answers `CNA_RESULT_IO` naming the mismatch.
The canonical read reports that by failing an unbox, which is not a `runtime_error`, so it
previously reached the exception barrier's catch-all and surfaced as `CNA_RESULT_INTERNAL` — an
honest "this asset is not that type" reported as a fault inside CNA.

## Manifest and reader-usage snapshots

`cna_content_manager_refresh_content_manifest` rescans the content root; the query routes build the
manifest lazily when it has never been built. Both the manifest and the `.xnb` reader-usage summary
are exposed as a count plus indexed accessors, with the fixed `CNA_ContentManifestEntryInfo` and
`CNA_ContentReaderUsageInfo` structures carrying the scalar fields and separate UTF-8 count/copy
routes carrying every string.

The canonical manifest is built from an unordered map, so entry order is unspecified but stable
until the manifest is rebuilt. The canonical reader-usage summary is both unordered *and*
recomputed on every call, so the C routes sort it by reader name; without that an index could name
a different row between the count call and the copy call that follows it.

## Paths, keys and the service provider

`cna_content_manager_copy_asset_path` reports where an asset would be read from — the root joined
with the asset name — whether or not a file exists there. `cna_content_manager_copy_normalized_key`
reports the cache key the manager would use, which folds backslashes to forward slashes and lowers
the case; two names with the same key address the same cache entry.

`System::IServiceProvider` is a Sharp Runtime object and is a hard ABI boundary, so the two
service-provider constructors are reachable from C only with a null provider.
`cna_content_manager_get_has_service_provider` therefore reports presence only, and always
`CNA_FALSE` for a manager created through this API; a C-created manager resolves GPU work through
the graphics device set on it instead.

## The compiled-asset reader pipeline

`cna_content_reader_create` builds an owned reader over an owned, readable `CNA_StorageStreamHandle`
and an optional content-manager handle — the canonical reader accepts a null manager, and a C
consumer that only reads fixed values never needs one. The reader then offers the canonical fixed
reads (matrix, quaternion, the three vector widths, color, bounding sphere), the type-reader-table
and shared-resource passes, both bounds checks and a capacity-checked exact-byte read.

Two lifetime rules follow from the canonical types rather than from the binding:

- the reader borrows the stream, so `cna_storage_stream_close` is refused with
  `CNA_RESULT_INVALID_STATE` while a reader is alive; and
- destroying the reader **closes** that stream, matching the binary-reader contract the canonical
  reader derives from. The stream handle stays valid and is still released with
  `cna_storage_stream_close`, which is idempotent; a second reader over the same file needs a
  freshly opened stream.

`cna_content_reader_read_bytes_exact` checks the destination capacity before it touches the stream,
so a `CNA_RESULT_BUFFER_TOO_SMALL` refusal consumes nothing and the caller does not have to seek
back.

### Type readers and the registry

The canonical `ContentTypeReaderManager` is entirely static, so its C routes are free functions and
no handle is invented for an object that holds no state. `cna_content_type_reader_manager_create_reader`
reports an unregistered canonical name as `CNA_RESULT_NOT_SUPPORTED` rather than returning a null
handle. `cna_content_type_reader_manager_clear_type_creators` empties the process-wide registry and
is genuinely destructive; `cna_content_register_known_unsupported_xnb_readers` restores the
placeholder registrations.

**The known-unsupported set is currently empty.** Its one entry was the general `EffectReader`,
which the compiled Effect Framework work replaced with a reader that really decodes compiled
effects, registered by an internal entry point with no C form. The published hook remains the
registry's extension point and stays idempotent; today it registers nothing. A caller that needs a
recognized-but-unsupported reader builds one directly with
`cna_known_unsupported_content_type_reader_create`, which is unaffected.

An owned `CNA_ContentTypeReaderHandle` exposes the target type name, the type version, both
version-support answers, the in-place-deserialization capability and the post-table initialization
step.

### Where type erasure stops

`cna_content_reader_read_object_tag` and `cna_content_type_reader_read_untyped` run the canonical
operations and report only whether an object was produced. The canonical return type is a
type-erased C++ object, and C has no representation for it, so the value cannot be handed back. A
placeholder reader for a recognized-but-unsupported asset type is the one case where nothing is
lost: it always refuses, and its canonical diagnostic reaches the caller as `CNA_RESULT_IO`.

Everything else in the pipeline is a C++-only extension point and has no C route at all: the typed
`ReadObject<T>` / `ReadRawObject<T>` / `ReadSharedResource<T>` / `ReadAsset<T>` /
`ReadExternalReference<T>` templates, `ContentTypeReader<T>` and `LooseFileContentTypeReader<T>`.
Each needs C to name an arbitrary C++ type, so the C API adds a typed load route per asset type
rather than an untyped one.

## Readers a caller supplies

Registration is **not** on that list, though it reads as if it should be. A C caller cannot write a
`ContentTypeReader<T>`, but it does not have to: `cna_content_type_reader_manager_register` takes a
versioned callback table — a per-file instance factory, a read callback, an optional per-instance
destructor — and an owned registration handle, and the adapter wraps that table in a canonical
reader. Downstream, nothing can tell the difference.

Three properties are worth knowing before using it.

**The read callback returns `CNA_Result`**, unlike `CNA_GameComponentCallbacks`, which returns
`void`. A component that fails has a next frame to recover in; a half-read asset does not. A
refusing read fails the whole load, and the message carries the code the callback returned.

**The `CNA_ContentReaderHandle` it receives is callback-scoped.** It answers every ordinary
`cna_content_reader_*` route while the callback runs, is invalidated before the callback returns,
and refuses `cna_content_reader_destroy` from inside — the reader belongs to the load in progress.

**Registration refuses a name that is already taken**, where the canonical `AddTypeCreator`
silently ignores a repeat. Silence is right for a built-in registering itself twice through two
static-initialisation paths and wrong here: the caller would hold a live handle whose factory is
never called, and would find out only from assets deserialising into the wrong type.
`cna_content_type_reader_manager_unregister` frees the name again.

What such a reader produces is an opaque `void*` — `CNA::Content::ForeignContentObjectEXT` on the
C++ side — that this ABI never dereferences and never frees; the reader that made it owns it.
`cna_content_manager_load_foreign_ext` is the route that reaches one, and the only one: the typed
loaders each name a C++ type this ABI knows, and a custom content type is by definition not one of
them. The asset's own type-reader table decides which reader runs, so the route needs no type
argument.

Two consequences of it being an ordinary load. It is **cached** by asset name like any other asset,
so a second call answers the same pointer without re-reading the file and the object must outlive
the manager's cache — `cna_content_manager_unload` drops it. And only **compiled `.xnb` assets**
reach a registered reader: a loose file or a `.cnj` descriptor is dispatched by requested C++ type
rather than by reader name, and there is no C++ type here to dispatch on, so such an asset fails
with `CNA_RESULT_IO` rather than being read by the wrong reader.
