# CNA C API Content Contract

## Scope

The content surface owns a native `ContentManager` and exposes three approved typed load routes:
Color `Texture2D`, `TextureCube` and `SoundEffect`. The C API does not attempt to express the C++
`Load<T>` template and never accepts a runtime type name, C++ type identifier, service-provider
pointer, path object or stream object.

Alongside loading, the manager exposes its resolved asset path and normalized cache key, built-in
loader registration, service-provider presence, graphics-device get/set and the content manifest
and `.xnb` reader-usage snapshots. The XNB reader pipeline itself — `ContentReader`,
`ContentTypeReader`, `ContentTypeReaderManager`, `LooseFileContentTypeReader`,
`KnownUnsupportedContentTypeReader` and `ResourceContentManager` — is not part of this contract
yet.

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

Songs, video, models, effects, fonts and 3D textures still have no typed C load route. They each
require an explicit typed C load function and resource ownership contract in later coverage tasks,
because C cannot name the C++ type the generic `Load<T>` is instantiated with.

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
