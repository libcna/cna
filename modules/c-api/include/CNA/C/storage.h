// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_STORAGE_H
#define CNA_C_STORAGE_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle for a storage device. */
typedef CNA_Handle CNA_StorageDeviceHandle;

/** @brief Owned handle for a storage container. */
typedef CNA_Handle CNA_StorageContainerHandle;

/** @brief Owned handle for a byte stream over a file inside a storage container. */
typedef CNA_Handle CNA_StorageStreamHandle;

/** @brief Fixed-width identity selecting how a file is opened. */
typedef uint32_t CNA_FileMode;

/** @brief Creates a new file and fails when it already exists. */
#define CNA_FILE_MODE_CREATE_NEW UINT32_C(1)
/** @brief Creates a new file, overwriting an existing one. */
#define CNA_FILE_MODE_CREATE UINT32_C(2)
/** @brief Opens an existing file and fails when it does not exist. */
#define CNA_FILE_MODE_OPEN UINT32_C(3)
/** @brief Opens the file when it exists and creates it otherwise. */
#define CNA_FILE_MODE_OPEN_OR_CREATE UINT32_C(4)
/** @brief Opens an existing file and truncates it to zero bytes. */
#define CNA_FILE_MODE_TRUNCATE UINT32_C(5)
/** @brief Opens or creates a file and seeks to its end before each write. */
#define CNA_FILE_MODE_APPEND UINT32_C(6)

/** @brief Fixed-width identity selecting the access requested for a file. */
typedef uint32_t CNA_FileAccess;

/** @brief Requests read access. */
#define CNA_FILE_ACCESS_READ UINT32_C(1)
/** @brief Requests write access. */
#define CNA_FILE_ACCESS_WRITE UINT32_C(2)
/** @brief Requests read and write access. */
#define CNA_FILE_ACCESS_READ_WRITE UINT32_C(3)

/** @brief Fixed-width identity selecting how other openers may share a file. */
typedef uint32_t CNA_FileShare;

/** @brief Declines sharing. */
#define CNA_FILE_SHARE_NONE UINT32_C(0)
/** @brief Allows a subsequent open for reading. */
#define CNA_FILE_SHARE_READ UINT32_C(1)
/** @brief Allows a subsequent open for writing. */
#define CNA_FILE_SHARE_WRITE UINT32_C(2)
/** @brief Allows a subsequent open for reading or writing. */
#define CNA_FILE_SHARE_READ_WRITE UINT32_C(3)
/** @brief Allows the file to be deleted while open. */
#define CNA_FILE_SHARE_DELETE UINT32_C(4)
/** @brief Makes the handle inheritable by child processes. */
#define CNA_FILE_SHARE_INHERITABLE UINT32_C(16)

/** @brief Fixed-width identity selecting the origin a seek offset is measured from. */
typedef uint32_t CNA_SeekOrigin;

/** @brief Measures from the beginning of the stream. */
#define CNA_SEEK_ORIGIN_BEGIN UINT32_C(0)
/** @brief Measures from the current position. */
#define CNA_SEEK_ORIGIN_CURRENT UINT32_C(1)
/** @brief Measures from the end of the stream. */
#define CNA_SEEK_ORIGIN_END UINT32_C(2)

/**
 * @brief Receives the completion of a storage operation.
 *
 * The canonical API uses XNA's fake-async `BeginXxx`/`EndXxx` pair, which CNA completes
 * synchronously. The C route is therefore one synchronous call, and this callback is invoked
 * before it returns so the canonical completion contract is preserved.
 *
 * @param context Caller-owned context supplied at the call site.
 */
typedef void (*CNA_StorageCompletionCallback)(void* context);

/**
 * @brief Sets the application name used to build the storage root directory.
 *
 * @param app_name UTF-8 application name copied during this call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/native failure.
 *
 * The canonical contract expects this once at startup, before any storage access.
 */
CNA_C_API CNA_Result cna_storage_set_app_name_ext(CNA_StringView app_name);

/**
 * @brief Gets the UTF-8 byte count of the current storage root directory.
 *
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_storage_get_root_size_ext(uint64_t* out_bytes);

/**
 * @brief Copies the current storage root directory as UTF-8 bytes without a terminator.
 *
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or an argument failure. No partial
 * path is written.
 */
CNA_C_API CNA_Result cna_storage_copy_root_ext(
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Selects a storage device for all players.
 *
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_device Receives an owned storage-device handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/native failure.
 *
 * This collapses the canonical `BeginShowSelector`/`EndShowSelector` pair, which CNA completes
 * synchronously; no operation handle is invented for work that never pends.
 */
CNA_C_API CNA_Result cna_storage_device_show_selector(
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_StorageDeviceHandle* out_device);

/**
 * @brief Selects a storage device for one player.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_device Receives an owned storage-device handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown player, or a
 * documented thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_device_show_selector_for_player(
    CNA_PlayerIndex player,
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_StorageDeviceHandle* out_device);

/**
 * @brief Selects a storage device with space requirements.
 *
 * @param size_in_bytes Required free space in bytes; must not be negative.
 * @param directory_count Required number of directories; must not be negative.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_device Receives an owned storage-device handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative requirement, or a
 * documented thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_device_show_selector_with_space(
    int32_t size_in_bytes,
    int32_t directory_count,
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_StorageDeviceHandle* out_device);

/**
 * @brief Selects a storage device for one player with space requirements.
 *
 * @param player One of the `CNA_PLAYER_INDEX_*` identities.
 * @param size_in_bytes Required free space in bytes; must not be negative.
 * @param directory_count Required number of directories; must not be negative.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_device Receives an owned storage-device handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown player or a negative
 * requirement, or a documented thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_device_show_selector_for_player_with_space(
    CNA_PlayerIndex player,
    int32_t size_in_bytes,
    int32_t directory_count,
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_StorageDeviceHandle* out_device);

/**
 * @brief Gets the available free space on the underlying filesystem.
 *
 * @param device Owned storage-device handle.
 * @param out_free_space Receives the free space in bytes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the storage device is not
 * connected, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_device_get_free_space(
    CNA_StorageDeviceHandle device,
    int64_t* out_free_space);

/**
 * @brief Gets whether the storage location is currently accessible.
 *
 * @param device Owned storage-device handle.
 * @param out_is_connected Receives `CNA_TRUE` while the storage location is accessible.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_device_get_is_connected(
    CNA_StorageDeviceHandle device,
    CNA_Bool* out_is_connected);

/**
 * @brief Gets the total size of the underlying filesystem.
 *
 * @param device Owned storage-device handle.
 * @param out_total_space Receives the total space in bytes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the storage device is not
 * connected, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_device_get_total_space(
    CNA_StorageDeviceHandle device,
    int64_t* out_total_space);

/**
 * @brief Deletes an entire container directory tree.
 *
 * @param device Owned storage-device handle.
 * @param title_name UTF-8 container name copied during this call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_device_delete_container(
    CNA_StorageDeviceHandle device,
    CNA_StringView title_name);

/**
 * @brief Subscribes to the process-wide device-changed event.
 *
 * @param callback Non-null callback invoked synchronously when the device set changes.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/thread/native failure.
 *
 * The canonical event is a static member, so the subscription belongs to the process rather than
 * to any one device handle.
 */
CNA_C_API CNA_Result cna_storage_device_subscribe_device_changed(
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Unsubscribes and releases a device-changed registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. A second release returns
 * `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_storage_device_unsubscribe_device_changed(CNA_Handle registration);

/**
 * @brief Releases an owned storage-device handle.
 *
 * @param device Owned storage-device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure. Containers opened from the
 * device must be destroyed first.
 */
CNA_C_API CNA_Result cna_storage_device_destroy(CNA_StorageDeviceHandle device);

/**
 * @brief Opens a storage container on a device.
 *
 * @param device Owned storage-device handle.
 * @param display_name UTF-8 container name copied during this call.
 * @param callback Optional completion callback invoked before this call returns.
 * @param context Caller-owned callback context, which may be null.
 * @param out_container Receives an owned container handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for invalid UTF-8, or a documented
 * argument/handle/thread/native failure.
 *
 * This collapses the canonical `BeginOpenContainer`/`EndOpenContainer` pair.
 */
CNA_C_API CNA_Result cna_storage_container_open(
    CNA_StorageDeviceHandle device,
    CNA_StringView display_name,
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_StorageContainerHandle* out_container);

/**
 * @brief Gets the UTF-8 byte count of a container's display name.
 *
 * @param container Owned container handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_display_name_size(
    CNA_StorageContainerHandle container,
    uint64_t* out_bytes);

/**
 * @brief Copies a container's display name as UTF-8 bytes without a terminator.
 *
 * @param container Owned container handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_storage_container_copy_display_name(
    CNA_StorageContainerHandle container,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the UTF-8 byte count of a container's fully qualified .NET type name.
 *
 * @param container Owned container handle.
 * @param out_bytes Receives the byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_type_name_size(
    CNA_StorageContainerHandle container,
    uint64_t* out_bytes);

/**
 * @brief Copies a container's fully qualified .NET type name without a terminator.
 *
 * @param container Owned container handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented
 * argument/handle/thread failure. No partial name is written.
 */
CNA_C_API CNA_Result cna_storage_container_copy_type_name(
    CNA_StorageContainerHandle container,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets whether a container has been disposed.
 *
 * @param container Owned container handle.
 * @param out_is_disposed Receives `CNA_TRUE` when the container has been disposed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_is_disposed(
    CNA_StorageContainerHandle container,
    CNA_Bool* out_is_disposed);

/**
 * @brief Gets the storage device that owns a container.
 *
 * @param container Owned container handle.
 * @param out_device Receives the owning device handle supplied when the container was opened.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_storage_device(
    CNA_StorageContainerHandle container,
    CNA_StorageDeviceHandle* out_device);

/**
 * @brief Disposes a container without releasing its handle.
 *
 * @param container Owned container handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. Disposal is
 * idempotent and raises the canonical disposing event exactly once.
 */
CNA_C_API CNA_Result cna_storage_container_dispose(CNA_StorageContainerHandle container);

/**
 * @brief Subscribes to a container's disposing event.
 *
 * @param container Owned container handle.
 * @param callback Non-null callback invoked synchronously during disposal.
 * @param context Caller-owned callback context, which may be null.
 * @param out_registration Receives an owned registration handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_subscribe_disposing(
    CNA_StorageContainerHandle container,
    CNA_StorageCompletionCallback callback,
    void* context,
    CNA_Handle* out_registration);

/**
 * @brief Unsubscribes and releases a container disposing registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_storage_container_unsubscribe_disposing(CNA_Handle registration);

/**
 * @brief Creates a subdirectory relative to a container's root.
 *
 * @param container Owned container handle.
 * @param directory UTF-8 relative directory path copied during this call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_create_directory(
    CNA_StorageContainerHandle container,
    CNA_StringView directory);

/**
 * @brief Gets whether a relative directory exists in a container.
 *
 * @param container Owned container handle.
 * @param directory UTF-8 relative directory path.
 * @param out_exists Receives `CNA_TRUE` when the directory exists.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_directory_exists(
    CNA_StorageContainerHandle container,
    CNA_StringView directory,
    CNA_Bool* out_exists);

/**
 * @brief Deletes an empty relative directory from a container.
 *
 * @param container Owned container handle.
 * @param directory UTF-8 relative directory path.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_delete_directory(
    CNA_StorageContainerHandle container,
    CNA_StringView directory);

/**
 * @brief Gets whether a relative file exists in a container.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @param out_exists Receives `CNA_TRUE` when the file exists.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_file_exists(
    CNA_StorageContainerHandle container,
    CNA_StringView file,
    CNA_Bool* out_exists);

/**
 * @brief Deletes a relative file from a container.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_delete_file(
    CNA_StorageContainerHandle container,
    CNA_StringView file);

/**
 * @brief Gets the number of directory names in a container's root.
 *
 * @param container Owned container handle.
 * @param search_pattern UTF-8 glob pattern, or a zero-length view for every name.
 * @param out_count Receives the matching directory count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_directory_name_count(
    CNA_StorageContainerHandle container,
    CNA_StringView search_pattern,
    uint64_t* out_count);

/**
 * @brief Copies one matching directory name as UTF-8 bytes without a terminator.
 *
 * @param container Owned container handle.
 * @param search_pattern UTF-8 glob pattern, or a zero-length view for every name.
 * @param index Zero-based index into the matching names.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure. The native listing is not
 * ordered, so a name is addressed by the index of the same immediately preceding count call.
 */
CNA_C_API CNA_Result cna_storage_container_copy_directory_name(
    CNA_StorageContainerHandle container,
    CNA_StringView search_pattern,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Gets the number of file names in a container's root.
 *
 * @param container Owned container handle.
 * @param search_pattern UTF-8 glob pattern, or a zero-length view for every name.
 * @param out_count Receives the matching file count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_get_file_name_count(
    CNA_StorageContainerHandle container,
    CNA_StringView search_pattern,
    uint64_t* out_count);

/**
 * @brief Copies one matching file name as UTF-8 bytes without a terminator.
 *
 * @param container Owned container handle.
 * @param search_pattern UTF-8 glob pattern, or a zero-length view for every name.
 * @param index Zero-based index into the matching names.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, `CNA_RESULT_INVALID_ARGUMENT` for
 * an out-of-range index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_copy_file_name(
    CNA_StorageContainerHandle container,
    CNA_StringView search_pattern,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Creates or truncates a file and opens it for writing.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @param out_stream Receives an owned stream handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The stream is a child of its container and must be closed before the container is destroyed.
 */
CNA_C_API CNA_Result cna_storage_container_create_file(
    CNA_StorageContainerHandle container,
    CNA_StringView file,
    CNA_StorageStreamHandle* out_stream);

/**
 * @brief Opens a file with an explicit mode, defaulting access and sharing to read/write.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @param file_mode One of the `CNA_FILE_MODE_*` identities.
 * @param out_stream Receives an owned stream handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_open_file(
    CNA_StorageContainerHandle container,
    CNA_StringView file,
    CNA_FileMode file_mode,
    CNA_StorageStreamHandle* out_stream);

/**
 * @brief Opens a file with an explicit mode and access, defaulting sharing to read/write.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @param file_mode One of the `CNA_FILE_MODE_*` identities.
 * @param file_access One of the `CNA_FILE_ACCESS_*` identities.
 * @param out_stream Receives an owned stream handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_container_open_file_access(
    CNA_StorageContainerHandle container,
    CNA_StringView file,
    CNA_FileMode file_mode,
    CNA_FileAccess file_access,
    CNA_StorageStreamHandle* out_stream);

/**
 * @brief Opens a file with an explicit mode, access and sharing selection.
 *
 * @param container Owned container handle.
 * @param file UTF-8 relative file path.
 * @param file_mode One of the `CNA_FILE_MODE_*` identities.
 * @param file_access One of the `CNA_FILE_ACCESS_*` identities.
 * @param file_share Zero or more `CNA_FILE_SHARE_*` bits.
 * @param out_stream Receives an owned stream handle on success.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown identity, or a
 * documented handle/thread/native failure.
 *
 * The canonical implementation currently ignores @p file_share, so this route differs from
 * `cna_storage_container_open_file_access` only in which selection the caller states explicitly.
 */
CNA_C_API CNA_Result cna_storage_container_open_file_share(
    CNA_StorageContainerHandle container,
    CNA_StringView file,
    CNA_FileMode file_mode,
    CNA_FileAccess file_access,
    CNA_FileShare file_share,
    CNA_StorageStreamHandle* out_stream);

/**
 * @brief Releases an owned container handle, disposing it if necessary.
 *
 * @param container Owned container handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. Streams opened from
 * the container must be closed first.
 */
CNA_C_API CNA_Result cna_storage_container_destroy(CNA_StorageContainerHandle container);

/**
 * @brief Reads bytes from a storage stream.
 *
 * @param stream Owned stream handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_read Receives the number of bytes actually read, which may be fewer than requested.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_read(
    CNA_StorageStreamHandle stream,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_read);

/**
 * @brief Writes bytes to a storage stream.
 *
 * @param stream Owned stream handle.
 * @param data Caller-owned bytes copied during this call, or null only when @p count is zero.
 * @param count Number of bytes beginning at @p data.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_write(
    CNA_StorageStreamHandle stream,
    const uint8_t* data,
    uint64_t count);

/**
 * @brief Moves the stream position.
 *
 * @param stream Owned stream handle.
 * @param offset Signed offset measured from @p origin.
 * @param origin One of the `CNA_SEEK_ORIGIN_*` identities.
 * @param out_position Receives the resulting position.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an unknown origin or an offset
 * outside the native range, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_seek(
    CNA_StorageStreamHandle stream,
    int64_t offset,
    CNA_SeekOrigin origin,
    int64_t* out_position);

/**
 * @brief Gets the current stream position.
 *
 * @param stream Owned stream handle.
 * @param out_position Receives the position in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_get_position(
    CNA_StorageStreamHandle stream,
    int64_t* out_position);

/**
 * @brief Gets the stream length.
 *
 * @param stream Owned stream handle.
 * @param out_length Receives the length in bytes.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_get_length(
    CNA_StorageStreamHandle stream,
    int64_t* out_length);

/**
 * @brief Sets the stream length, truncating or extending the file.
 *
 * @param stream Owned stream handle.
 * @param length Desired length in bytes; must not be negative or exceed the native range.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range length, or a
 * documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_set_length(
    CNA_StorageStreamHandle stream,
    int64_t length);

/**
 * @brief Gets whether a stream currently supports reading.
 *
 * @param stream Owned stream handle.
 * @param out_can_read Receives `CNA_TRUE` when reading is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_get_can_read(
    CNA_StorageStreamHandle stream,
    CNA_Bool* out_can_read);

/**
 * @brief Gets whether a stream currently supports writing.
 *
 * @param stream Owned stream handle.
 * @param out_can_write Receives `CNA_TRUE` when writing is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_get_can_write(
    CNA_StorageStreamHandle stream,
    CNA_Bool* out_can_write);

/**
 * @brief Gets whether a stream currently supports seeking.
 *
 * @param stream Owned stream handle.
 * @param out_can_seek Receives `CNA_TRUE` when seeking is supported.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_get_can_seek(
    CNA_StorageStreamHandle stream,
    CNA_Bool* out_can_seek);

/**
 * @brief Flushes buffered stream writes.
 *
 * @param stream Owned stream handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_storage_stream_flush(CNA_StorageStreamHandle stream);

/**
 * @brief Closes a stream and releases its handle.
 *
 * @param stream Owned stream handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure. A second close
 * returns `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_storage_stream_close(CNA_StorageStreamHandle stream);

#ifdef __cplusplus
}
#endif

#endif
