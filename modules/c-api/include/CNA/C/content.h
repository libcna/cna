// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_CONTENT_H
#define CNA_C_CONTENT_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configures creation of an owned content manager.
 */
typedef struct CNA_ContentManagerCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief UTF-8 root directory prepended to asset names; an empty root is valid. */
    CNA_StringView root_directory;

    /** @brief Reserved for future use; callers must initialize this to zero. */
    uint64_t reserved;
} CNA_ContentManagerCreateInfo;

/**
 * @brief Creates an owned content manager associated with the active game and graphics device.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param create_info Versioned creation configuration.
 * @param out_content_manager Receives an owned content-manager handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The resulting content manager survives the callback, must be destroyed on the game creation
 * thread and must be destroyed before its parent game. The root directory is copied during this
 * call. No native service-provider, path or stream object crosses the C boundary.
 */
CNA_C_API CNA_Result cna_content_manager_create(
    CNA_Handle graphics_device,
    const CNA_ContentManagerCreateInfo* create_info,
    CNA_Handle* out_content_manager);

/**
 * @brief Gets the UTF-8 byte count of a content manager's root directory.
 *
 * @param content_manager Owned content-manager handle.
 * @param out_bytes Receives the exact byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/argument failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_root_directory_size(
    CNA_Handle content_manager,
    uint64_t* out_bytes);

/**
 * @brief Copies a content manager's complete UTF-8 root directory.
 *
 * @param content_manager Owned content-manager handle.
 * @param destination Caller-owned bytes, or null only when @p capacity is zero.
 * @param capacity Capacity of @p destination in bytes.
 * @param out_bytes Receives the exact byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * handle/thread/argument failure. No partial string is written.
 */
CNA_C_API CNA_Result cna_content_manager_copy_root_directory(
    CNA_Handle content_manager,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Sets the UTF-8 root directory used for subsequent uncached asset loads.
 *
 * @param content_manager Owned content-manager handle.
 * @param root_directory UTF-8 root directory copied during this call; an empty root is valid.
 * @return `CNA_RESULT_SUCCESS` or a documented encoding/handle/thread/native failure.
 *
 * Existing cache entries are not unloaded. Call @ref cna_content_manager_unload explicitly when
 * changing roots must also invalidate the existing native asset cache.
 */
CNA_C_API CNA_Result cna_content_manager_set_root_directory(
    CNA_Handle content_manager,
    CNA_StringView root_directory);

/**
 * @brief Unloads all assets cached by a content manager.
 *
 * @param content_manager Owned content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Independently owned C resource handles returned by earlier load calls remain valid and must
 * still be destroyed explicitly.
 */
CNA_C_API CNA_Result cna_content_manager_unload(CNA_Handle content_manager);

/**
 * @brief Loads a Color Texture2D asset through the native content pipeline.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name Non-empty UTF-8 logical asset name copied during this call.
 * @param out_texture Receives a new independently owned Texture2D handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a content-load failure,
 * `CNA_RESULT_NOT_SUPPORTED` for a loaded non-Color texture, or another documented
 * argument/encoding/handle/thread/native failure.
 *
 * The returned texture uses the normal `cna_texture2d_*` operations, remains valid across
 * content-manager unload or destruction and must be destroyed before the parent game.
 */
CNA_C_API CNA_Result cna_content_manager_load_texture2d(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_Handle* out_texture);

/**
 * @brief Creates an owned resource-backed content manager.
 *
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @param create_info Versioned creation configuration.
 * @param out_content_manager Receives an owned content-manager handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical `ResourceContentManager`, which reads assets embedded in the
 * application rather than from loose files. It is created with a null service provider for the
 * same reason as `cna_content_manager_create`, and the resulting handle uses every other
 * `cna_content_manager_*` operation unchanged.
 *
 * **Every load through it fails today, and that is inherited rather than introduced here.** The
 * canonical embedded-resource stream is a declared placeholder in CNA: there is no .NET assembly
 * for it to read resources out of, and nothing has yet decided what a native application's
 * embedded-resource store should be. A load therefore returns `CNA_RESULT_IO` naming the
 * placeholder, rather than returning empty data that a caller would mistake for an empty asset.
 *
 * The route is published anyway so the canonical type has a name in C and so this paragraph has
 * somewhere to live -- a consumer reaching for it deserves to learn that here rather than from a
 * failing load. A consumer that needs embedded assets today should implement that store itself,
 * above this ABI, where its host platform already has one.
 */
CNA_C_API CNA_Result cna_content_manager_create_resource(
    CNA_Handle graphics_device,
    const CNA_ContentManagerCreateInfo* create_info,
    CNA_Handle* out_content_manager);

/**
 * @brief Loads a WAV asset as an owned sound effect.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name, with or without its extension.
 * @param out_sound_effect Receives an owned sound-effect handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or undecodable asset,
 * `CNA_RESULT_NOT_SUPPORTED` when no audio device is available, or a documented
 * argument/handle/thread failure.
 *
 * This maps the canonical `Load<SoundEffect>` specialization, which deliberately does not cache:
 * every successful call returns an independently owned sound effect. The returned handle uses the
 * normal `cna_sound_effect_*` operations and must be destroyed before the parent game.
 */
CNA_C_API CNA_Result cna_content_manager_load_sound_effect(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_Handle* out_sound_effect);

/**
 * @brief Loads a DDS asset as an owned cube texture.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name, with or without its extension.
 * @param out_texture Receives an owned cube-texture handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or undecodable asset,
 * `CNA_RESULT_NOT_SUPPORTED` on a backend without cube storage, or a documented
 * argument/handle/thread failure.
 *
 * This maps canonical `Load<TextureCube>`, whose native resource is strongly cached until
 * `ContentManager::Unload()`. Every call still returns an independently owned C handle; handles
 * for the same cached asset share the underlying texture resource. The returned handle uses the
 * normal `cna_texture_cube_*` operations and must be destroyed before the parent game.
 */
CNA_C_API CNA_Result cna_content_manager_load_texture_cube(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_Handle* out_texture);

/**
 * @brief Loads a SpriteFont asset, reporting both the font and its glyph atlas.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name, with or without its extension.
 * @param out_sprite_font Receives an **owned** SpriteFont handle on success, destroyed with
 *        `cna_sprite_font_destroy`.
 * @param out_texture Receives an **owned** Texture2D handle for the glyph atlas, destroyed with
 *        `cna_texture2d_destroy`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_IO` for a missing or undecodable asset, or a
 *         documented argument/handle/thread failure. On any failure both outputs are left
 *         `CNA_INVALID_HANDLE` and nothing is created.
 *
 * This maps the canonical `Load<SpriteFont>` specialization, which reads both the `.xnb` font
 * container and CNA's own `.cnj` font descriptor. It removes the need for a consumer to parse
 * either format itself in order to obtain a font this ABI will accept.
 *
 * **Two owned handles, one asset**, which is why this loader has a fourth parameter the other
 * three do not. A SpriteFont is a font *and* the texture it draws from, and both have to be
 * reachable: the font for measuring, layout and `cna_sprite_batch_draw_string`, the atlas for a
 * consumer that places glyphs itself from `cna_sprite_font_copy_glyphs`. Handing back only the
 * font would leave the atlas alive but unnameable.
 *
 * **Destroy the font first.** The atlas is retained for as long as a SpriteFont uses it, exactly
 * as it is for a caller-built font, so `cna_texture2d_destroy` refuses with
 * `CNA_RESULT_INVALID_STATE` while the font exists. That ordering rule is the same one
 * `cna_sprite_font_create` already imposes, so a consumer needs no special case for a loaded font.
 */
CNA_C_API CNA_Result cna_content_manager_load_sprite_font(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    CNA_Handle* out_sprite_font,
    CNA_Handle* out_texture);

/**
 * @brief Loads a compiled asset whose root reader was registered by the caller.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name, with or without its extension.
 * @param out_object Receives the opaque object the caller's reader produced. This ABI does not
 *        own it: it is neither dereferenced nor freed here, and its lifetime is whatever the
 *        reader that made it says it is.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_IO` for a missing or malformed asset, for a file
 *         naming a reader nothing is registered under, and for a reader that refused -- the
 *         message carries the `CNA_Result` the callback returned; or a documented
 *         argument/handle/thread failure.
 *
 * The counterpart of `cna_content_type_reader_manager_register`, and the only route that reaches
 * a caller-supplied reader from outside: the typed loaders next to this one each name a C++ type
 * this ABI knows, and a custom content type is by definition not one of them. The asset's own
 * type-reader table decides which reader runs, so this route needs no type argument -- it needs
 * only that the reader the file names has been registered.
 *
 * **Only compiled `.xnb` assets reach a registered reader.** A loose file or a `.cnj` descriptor
 * is dispatched by the requested C++ type instead of by a reader name, and there is no C++ type
 * here to dispatch on. Such an asset fails with `CNA_RESULT_IO` rather than being read by the
 * wrong reader.
 *
 * The result is cached by asset name exactly as every other load is, so a second call for the
 * same name returns the same pointer without re-reading the file -- which is the caching XNA
 * guarantees for a reference type, and means the caller must not free the object while the
 * content manager that produced it can still hand it out. `cna_content_manager_unload` drops the
 * cache.
 */
CNA_C_API CNA_Result cna_content_manager_load_foreign_ext(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    void** out_object);

/**
 * @brief Builds one caller-owned object from a `.cnj` descriptor's raw JSON.
 *
 * @param context The context supplied at registration.
 * @param cnj_json The descriptor's whole text, borrowed for the duration of the call and **not**
 *        NUL-terminated -- read exactly `byte_length` bytes.
 * @param out_object Receives the caller's opaque object. This ABI never dereferences, copies or
 *        frees it.
 * @return `CNA_RESULT_SUCCESS`, or any documented result code to fail the load that asked for it.
 */
typedef CNA_Result (*CNA_CnjLoaderCallback)(
    void* context,
    CNA_StringView cnj_json,
    void** out_object);

/**
 * @brief Registers a loader for one `"type"` value in a `.cnj` descriptor.
 *
 * @param content_manager Owned content-manager handle.
 * @param type_name The descriptor's `"type"` string this loader handles, copied during the call.
 *        Must not be empty.
 * @param callback Non-null loader.
 * @param context Caller-owned context passed back to @p callback; it must outlive the content
 *        manager, which is what owns this registration.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null callback or an empty or
 *         non-UTF-8 type name; `CNA_RESULT_INVALID_STATE` when that type name is already
 *         registered on this manager; or a documented handle/thread failure.
 *
 * The `.cnj` counterpart of `cna_content_type_reader_manager_register`, and the piece that made
 * `cna_content_manager_load_foreign_ext` reach more than compiled assets: a registered reader
 * answers for an `.xnb`, and this answers for a `.cnj`. Load the result through that same route --
 * it needs no type argument and does not care which of the two produced the object.
 *
 * **Registration is per manager and lives as long as it.** There is no unregister, because the
 * canonical surface has none: the table belongs to the content manager and goes with it. That is
 * the one way this differs from the reader registry, which is process-wide and hands back a
 * handle to release.
 *
 * A descriptor whose `"type"` names nothing registered fails the load rather than falling back,
 * for the same reason a compiled asset naming an unregistered reader does.
 */
CNA_C_API CNA_Result cna_content_manager_register_cnj_loader_ext(
    CNA_Handle content_manager,
    CNA_StringView type_name,
    CNA_CnjLoaderCallback callback,
    void* context);

/*
 * The Effect loader, `cna_content_manager_load_effect`, is declared in `CNA/C/effects.h` beside
 * the rest of the effect surface, because that is where its `CNA_EffectHandle` return type lives.
 * It maps the canonical `Load<Effect>` specialization and belongs to this same family of loaders.
 */

/**
 * @brief Gets the UTF-8 byte count of the resolved filesystem path for an asset name.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_asset_path_size(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    uint64_t* out_bytes);

/**
 * @brief Copies the resolved filesystem path for an asset name without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial path is written.
 *
 * The path is the root directory joined with the asset name; it is reported whether or not a file
 * exists there.
 */
CNA_C_API CNA_Result cna_content_manager_copy_asset_path(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the UTF-8 byte count of the normalized cache key for an asset name.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_normalized_key_size(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    uint64_t* out_bytes);

/**
 * @brief Copies the normalized cache key for an asset name without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param asset_name UTF-8 logical asset name.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial key is written.
 *
 * Two asset names that normalize to the same key address the same cache entry.
 */
CNA_C_API CNA_Result cna_content_manager_copy_normalized_key(
    CNA_Handle content_manager,
    CNA_StringView asset_name,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Re-registers the built-in asset type readers on a content manager.
 *
 * @param content_manager Owned content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * Creation already performs this registration; the call is idempotent and exists so a C consumer
 * can restore the built-in readers after the native manager's reader table has been altered.
 */
CNA_C_API CNA_Result cna_content_manager_register_builtin_loaders(CNA_Handle content_manager);

/**
 * @brief Gets whether a content manager was constructed with a service provider.
 *
 * @param content_manager Owned content-manager handle.
 * @param out_has_service_provider Receives `CNA_TRUE` when a service provider is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A service provider is a Sharp Runtime object and never crosses the C boundary, so a manager
 * created through this API always reports `CNA_FALSE`; only the presence is observable.
 *
 * `CNA_FALSE` here is not evidence that a C-created manager is weaker than a native one. A game's
 * own content manager reports `CNA_FALSE` too: CNA constructs it without a provider, and no CNA
 * content path resolves a service through one, so the field is inert on both sides of the boundary.
 */
CNA_C_API CNA_Result cna_content_manager_get_has_service_provider(
    CNA_Handle content_manager,
    CNA_Bool* out_has_service_provider);

/**
 * @brief Gets the graphics device a content manager loads GPU resources with.
 *
 * @param content_manager Owned content-manager handle.
 * @param out_graphics_device Receives the borrowed graphics-device handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the borrowed device handle is no
 * longer in scope, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_graphics_device(
    CNA_Handle content_manager,
    CNA_Handle* out_graphics_device);

/**
 * @brief Sets the graphics device a content manager loads GPU resources with.
 *
 * @param content_manager Owned content-manager handle.
 * @param graphics_device Callback-scoped borrowed graphics-device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure. The device
 * must belong to the same game as the content manager.
 */
CNA_C_API CNA_Result cna_content_manager_set_graphics_device(
    CNA_Handle content_manager,
    CNA_Handle graphics_device);

/**
 * @brief Describes one logical asset found under the content root.
 */
typedef struct CNA_ContentManifestEntryInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when a compiled `.xnb` file exists for this entry. */
    CNA_Bool has_xnb;

    /** @brief `CNA_TRUE` when a `.cnj` document exists for this entry. */
    CNA_Bool has_cnj;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[6];

    /** @brief Number of native loose-file extensions recorded for this entry. */
    uint64_t native_extension_count;

    /** @brief Number of `.xnb` reader names recorded for this entry. */
    uint64_t xnb_reader_name_count;
} CNA_ContentManifestEntryInfo;

/**
 * @brief Describes one distinct `.xnb` reader name found while scanning the content root.
 */
typedef struct CNA_ContentReaderUsageInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief `CNA_TRUE` when a reader is currently registered for this name. */
    CNA_Bool is_registered;

    /** @brief Reserved bytes; always zero. */
    uint8_t reserved[7];

    /** @brief Number of scanned files that reference this reader name. */
    uint64_t file_count;
} CNA_ContentReaderUsageInfo;

/**
 * @brief Rescans the content root and rebuilds the manifest snapshot.
 *
 * @param content_manager Owned content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The manifest is a point-in-time snapshot; a file added afterwards is not visible until this is
 * called again. Querying the manifest builds it lazily when it has never been built.
 */
CNA_C_API CNA_Result cna_content_manager_refresh_content_manifest(CNA_Handle content_manager);

/**
 * @brief Gets the number of manifest entries.
 *
 * @param content_manager Owned content-manager handle.
 * @param out_count Receives the entry count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_manifest_entry_count(
    CNA_Handle content_manager,
    uint64_t* out_count);

/**
 * @brief Gets one manifest entry's fixed description.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based manifest entry index.
 * @param out_entry Caller-provided versioned structure to receive the description.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread/native failure.
 *
 * The canonical manifest is built from an unordered map, so entry order is unspecified; it is
 * stable until the manifest is rebuilt, which is what makes an index addressable.
 */
CNA_C_API CNA_Result cna_content_manager_get_manifest_entry(
    CNA_Handle content_manager,
    uint64_t index,
    CNA_ContentManifestEntryInfo* out_entry);

/**
 * @brief Copies one manifest entry's root-relative logical path without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based manifest entry index.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_copy_manifest_relative_path(
    CNA_Handle content_manager,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Copies one native loose-file extension of a manifest entry without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based manifest entry index.
 * @param extension_index Zero-based index into that entry's native extensions.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_copy_manifest_native_extension(
    CNA_Handle content_manager,
    uint64_t index,
    uint64_t extension_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Copies one `.xnb` reader name of a manifest entry without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based manifest entry index.
 * @param name_index Zero-based index into that entry's reader names.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_copy_manifest_xnb_reader_name(
    CNA_Handle content_manager,
    uint64_t index,
    uint64_t name_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the number of distinct `.xnb` reader names in the usage summary.
 *
 * @param content_manager Owned content-manager handle.
 * @param out_count Receives the reader-name count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical summary is unordered and recomputed per call, so the C routes sort it by reader
 * name; an index therefore addresses the same row for as long as the manifest is unchanged.
 */
CNA_C_API CNA_Result cna_content_manager_get_xnb_reader_usage_count(
    CNA_Handle content_manager,
    uint64_t* out_count);

/**
 * @brief Gets one reader-usage row's fixed description.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based index into the name-sorted usage summary.
 * @param out_usage Caller-provided versioned structure to receive the description.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_get_xnb_reader_usage(
    CNA_Handle content_manager,
    uint64_t index,
    CNA_ContentReaderUsageInfo* out_usage);

/**
 * @brief Copies one reader-usage row's reader name without a terminator.
 *
 * @param content_manager Owned content-manager handle.
 * @param index Zero-based index into the name-sorted usage summary.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_content_manager_copy_xnb_reader_usage_name(
    CNA_Handle content_manager,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Unloads cached assets and releases an owned content manager.
 *
 * @param content_manager Owned content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second destroy
 * returns `CNA_RESULT_INVALID_HANDLE`.
 *
 * Independently owned resource handles returned by the manager are not destroyed by this call.
 * A **borrowed** manager — the one a game owns, reached with `cna_game_get_content_manager_ext` —
 * is refused with `CNA_RESULT_INVALID_STATE`: it is released with its game.
 */
CNA_C_API CNA_Result cna_content_manager_destroy(CNA_Handle content_manager);

/**
 * @brief Borrows the content manager a game owns.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_content_manager Receives a borrowed content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A game owns exactly one content manager as a **value member**, so this handle borrows rather than
 * owns: it answers the same handle every time it is asked, it cannot be destroyed, and it is
 * released when the game is. Every other content-manager route accepts it, so a caller can set the
 * root directory the game loads from and load assets through the game's own cache. Destroying the
 * game while holding it is allowed — the handle simply becomes invalid, unlike an owned manager,
 * which must be destroyed first.
 */
CNA_C_API CNA_Result cna_game_get_content_manager_ext(
    CNA_Handle game,
    CNA_Handle* out_content_manager);

/**
 * @brief Replaces the content manager a game owns with a copy of another.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param content_manager Owned or borrowed content-manager handle to copy from.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical setter takes a reference and **copies**, so this does too: the caller keeps its own
 * manager, later changes to it do not reach the game, and the game's borrowed handle keeps
 * addressing the game's own manager rather than the source. What is copied includes the root
 * directory, the device association and whatever the source has already cached.
 */
CNA_C_API CNA_Result cna_game_set_content_manager_ext(
    CNA_Handle game,
    CNA_Handle content_manager);

#ifdef __cplusplus
}
#endif

#endif
