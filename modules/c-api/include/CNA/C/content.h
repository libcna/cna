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
 * @brief Unloads cached assets and releases an owned content manager.
 *
 * @param content_manager Owned content-manager handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second destroy
 * returns `CNA_RESULT_INVALID_HANDLE`.
 *
 * Independently owned resource handles returned by the manager are not destroyed by this call.
 */
CNA_C_API CNA_Result cna_content_manager_destroy(CNA_Handle content_manager);

#ifdef __cplusplus
}
#endif

#endif
