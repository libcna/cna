# CNA C API Content Contract

## Initial scope

The experimental 0.1 content surface owns a native `ContentManager` and exposes one approved
typed load route: Color `Texture2D`. The C API does not attempt to express the C++ `Load<T>`
template and never accepts a runtime type name, C++ type identifier, service-provider pointer,
path object or stream object.

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

Sound effects, songs, video, models, effects, fonts, texture cubes/3D textures, custom readers,
manifests and other native content types are not part of CBIND-028. They require explicit typed C
load functions and resource ownership contracts in later coverage tasks.
