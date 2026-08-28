// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdint.h>
#include <string.h>

static const char ContainerName[] = "SmokeSaves";
static const char TypeName[] = "Microsoft.Xna.Framework.Storage.StorageContainer";

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static CNA_StringView all_names(void)
{
    CNA_StringView result;
    result.data = 0;
    result.byte_length = UINT64_C(0);
    return result;
}

static void on_completed(void* const context)
{
    int* const counter = (int*)context;
    *counter += 1;
}

static int validate_identities(void)
{
    return CNA_FILE_MODE_CREATE_NEW == UINT32_C(1) && CNA_FILE_MODE_CREATE == UINT32_C(2) &&
        CNA_FILE_MODE_OPEN == UINT32_C(3) && CNA_FILE_MODE_OPEN_OR_CREATE == UINT32_C(4) &&
        CNA_FILE_MODE_TRUNCATE == UINT32_C(5) && CNA_FILE_MODE_APPEND == UINT32_C(6) &&
        CNA_FILE_ACCESS_READ == UINT32_C(1) && CNA_FILE_ACCESS_WRITE == UINT32_C(2) &&
        CNA_FILE_ACCESS_READ_WRITE == UINT32_C(3) && CNA_FILE_SHARE_NONE == UINT32_C(0) &&
        CNA_FILE_SHARE_READ == UINT32_C(1) && CNA_FILE_SHARE_WRITE == UINT32_C(2) &&
        CNA_FILE_SHARE_READ_WRITE == UINT32_C(3) && CNA_FILE_SHARE_DELETE == UINT32_C(4) &&
        CNA_FILE_SHARE_INHERITABLE == UINT32_C(16) && CNA_SEEK_ORIGIN_BEGIN == UINT32_C(0) &&
        CNA_SEEK_ORIGIN_CURRENT == UINT32_C(1) && CNA_SEEK_ORIGIN_END == UINT32_C(2);
}

static int validate_root(void)
{
    char buffer[1024];
    uint64_t size = UINT64_C(0);
    uint64_t copied = UINT64_C(0);

    if (cna_storage_set_app_name_ext(view("cna-c-api-storage-smoke")) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_get_root_size_ext(&size) != CNA_RESULT_SUCCESS || size == UINT64_C(0)) {
        return 0;
    }
    if (size >= (uint64_t)sizeof(buffer)) {
        return 0;
    }
    if (cna_storage_copy_root_ext(buffer, UINT64_C(0), &copied) != CNA_RESULT_BUFFER_TOO_SMALL ||
        copied != size) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_copy_root_ext(buffer, (uint64_t)sizeof(buffer), &copied) !=
            CNA_RESULT_SUCCESS ||
        copied != size || buffer[0] == '\0') {
        return 0;
    }
    if (cna_storage_get_root_size_ext(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_storage_copy_root_ext(0, UINT64_C(4), &copied) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_selectors(void)
{
    CNA_StorageDeviceHandle device = CNA_INVALID_HANDLE;
    int completions = 0;

    if (cna_storage_device_show_selector(on_completed, &completions, &device) !=
            CNA_RESULT_SUCCESS ||
        completions != 1 || cna_storage_device_destroy(device) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_device_show_selector_for_player(
            CNA_PLAYER_INDEX_TWO,
            on_completed,
            &completions,
            &device) != CNA_RESULT_SUCCESS ||
        completions != 2 || cna_storage_device_destroy(device) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    // A null completion callback is the documented "no notification wanted" case, not an error.
    if (cna_storage_device_show_selector_with_space(1024, 2, 0, 0, &device) !=
            CNA_RESULT_SUCCESS ||
        completions != 2 || cna_storage_device_destroy(device) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_device_show_selector_for_player_with_space(
            CNA_PLAYER_INDEX_ONE,
            0,
            0,
            on_completed,
            &completions,
            &device) != CNA_RESULT_SUCCESS ||
        completions != 3 || cna_storage_device_destroy(device) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_storage_device_show_selector_with_space(-1, 0, 0, 0, &device) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        device != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_device_show_selector_with_space(0, -1, 0, 0, &device) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_device_show_selector_for_player(UINT32_C(9), 0, 0, &device) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_device_show_selector_for_player_with_space(
            UINT32_C(9),
            0,
            0,
            0,
            0,
            &device) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_storage_device_show_selector(0, 0, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_device_state(const CNA_StorageDeviceHandle device)
{
    CNA_Bool connected = CNA_FALSE;
    int64_t freeSpace = INT64_C(0);
    int64_t totalSpace = INT64_C(0);

    if (cna_storage_device_get_is_connected(device, &connected) != CNA_RESULT_SUCCESS ||
        connected != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_device_get_free_space(device, &freeSpace) != CNA_RESULT_SUCCESS ||
        freeSpace <= INT64_C(0)) {
        return 0;
    }
    if (cna_storage_device_get_total_space(device, &totalSpace) != CNA_RESULT_SUCCESS ||
        totalSpace < freeSpace) {
        return 0;
    }
    if (cna_storage_device_get_free_space(device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_device_get_total_space(device, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_device_get_is_connected(device, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_storage_device_get_is_connected(CNA_INVALID_HANDLE, &connected) ==
        CNA_RESULT_INVALID_HANDLE;
}

static int validate_device_changed(void)
{
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    int changes = 0;

    if (cna_storage_device_subscribe_device_changed(on_completed, &changes, &registration) !=
            CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_device_subscribe_device_changed(0, 0, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_device_subscribe_device_changed(on_completed, &changes, 0) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_device_unsubscribe_device_changed(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_storage_device_unsubscribe_device_changed(registration) ==
        CNA_RESULT_INVALID_HANDLE;
}

static int validate_container_identity(
    const CNA_StorageDeviceHandle device,
    const CNA_StorageContainerHandle container)
{
    char buffer[256];
    uint64_t size = UINT64_C(0);
    uint64_t copied = UINT64_C(0);
    CNA_Bool disposed = CNA_TRUE;
    CNA_StorageDeviceHandle owner = CNA_INVALID_HANDLE;

    if (cna_storage_container_get_display_name_size(container, &size) != CNA_RESULT_SUCCESS ||
        size != (uint64_t)strlen(ContainerName)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_container_copy_display_name(
            container,
            buffer,
            (uint64_t)sizeof(buffer),
            &copied) != CNA_RESULT_SUCCESS ||
        copied != size || strcmp(buffer, ContainerName) != 0) {
        return 0;
    }
    if (cna_storage_container_copy_display_name(container, buffer, UINT64_C(1), &copied) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        copied != size) {
        return 0;
    }

    if (cna_storage_container_get_type_name_size(container, &size) != CNA_RESULT_SUCCESS ||
        size != (uint64_t)strlen(TypeName)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_container_copy_type_name(
            container,
            buffer,
            (uint64_t)sizeof(buffer),
            &copied) != CNA_RESULT_SUCCESS ||
        copied != size || strcmp(buffer, TypeName) != 0) {
        return 0;
    }

    if (cna_storage_container_get_is_disposed(container, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE) {
        return 0;
    }
    if (cna_storage_container_get_storage_device(container, &owner) != CNA_RESULT_SUCCESS ||
        owner != device) {
        return 0;
    }
    return cna_storage_container_get_display_name_size(CNA_INVALID_HANDLE, &size) ==
        CNA_RESULT_INVALID_HANDLE;
}

static int validate_directories(const CNA_StorageContainerHandle container)
{
    char buffer[256];
    uint64_t count = UINT64_C(0);
    uint64_t copied = UINT64_C(0);
    CNA_Bool exists = CNA_TRUE;

    if (cna_storage_container_directory_exists(container, view("levels"), &exists) !=
            CNA_RESULT_SUCCESS ||
        exists != CNA_FALSE) {
        return 0;
    }
    if (cna_storage_container_create_directory(container, view("levels")) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_container_directory_exists(container, view("levels"), &exists) !=
            CNA_RESULT_SUCCESS ||
        exists != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_container_get_directory_name_count(container, all_names(), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(1)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_container_copy_directory_name(
            container,
            all_names(),
            UINT64_C(0),
            buffer,
            (uint64_t)sizeof(buffer),
            &copied) != CNA_RESULT_SUCCESS ||
        copied != UINT64_C(6) || strcmp(buffer, "levels") != 0) {
        return 0;
    }
    if (cna_storage_container_get_directory_name_count(container, view("lev*"), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(1)) {
        return 0;
    }
    if (cna_storage_container_get_directory_name_count(container, view("z*"), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(0)) {
        return 0;
    }
    if (cna_storage_container_copy_directory_name(
            container,
            view("z*"),
            UINT64_C(0),
            buffer,
            (uint64_t)sizeof(buffer),
            &copied) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_container_copy_directory_name(
            container,
            all_names(),
            UINT64_C(0),
            buffer,
            UINT64_C(2),
            &copied) != CNA_RESULT_BUFFER_TOO_SMALL ||
        copied != UINT64_C(6)) {
        return 0;
    }
    if (cna_storage_container_delete_directory(container, view("levels")) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_container_directory_exists(container, view("levels"), &exists) !=
            CNA_RESULT_SUCCESS ||
        exists != CNA_FALSE) {
        return 0;
    }
    // An empty relative path is rejected by the canonical container, not silently resolved to
    // the container root.
    return cna_storage_container_create_directory(container, all_names()) ==
        CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_write(const CNA_StorageContainerHandle container, const uint8_t* const payload)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    CNA_Bool capability = CNA_FALSE;
    int64_t length = INT64_C(0);

    if (cna_storage_container_create_file(container, view("save.dat"), &stream) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_stream_get_can_write(stream, &capability) != CNA_RESULT_SUCCESS ||
        capability != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_stream_get_can_read(stream, &capability) != CNA_RESULT_SUCCESS ||
        capability != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_stream_get_can_seek(stream, &capability) != CNA_RESULT_SUCCESS ||
        capability != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_stream_write(stream, payload, UINT64_C(8)) != CNA_RESULT_SUCCESS ||
        cna_storage_stream_flush(stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_stream_get_length(stream, &length) != CNA_RESULT_SUCCESS ||
        length != INT64_C(8)) {
        return 0;
    }
    // A stream is a child of its container: the container cannot be destroyed while one is open.
    if (cna_storage_container_destroy(container) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_storage_stream_write(stream, 0, UINT64_C(4)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_stream_write(stream, 0, UINT64_C(0)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_stream_close(stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_storage_stream_close(stream) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_read(const CNA_StorageContainerHandle container, const uint8_t* const payload)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    uint8_t buffer[16];
    uint64_t read = UINT64_C(0);
    int64_t position = INT64_C(0);

    if (cna_storage_container_open_file(
            container,
            view("save.dat"),
            CNA_FILE_MODE_OPEN,
            &stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_stream_read(stream, buffer, UINT64_C(8), &read) != CNA_RESULT_SUCCESS ||
        read != UINT64_C(8) || memcmp(buffer, payload, 8U) != 0) {
        return 0;
    }
    if (cna_storage_stream_get_position(stream, &position) != CNA_RESULT_SUCCESS ||
        position != INT64_C(8)) {
        return 0;
    }
    if (cna_storage_stream_seek(stream, INT64_C(4), CNA_SEEK_ORIGIN_BEGIN, &position) !=
            CNA_RESULT_SUCCESS ||
        position != INT64_C(4)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_stream_read(stream, buffer, UINT64_C(4), &read) != CNA_RESULT_SUCCESS ||
        read != UINT64_C(4) || memcmp(buffer, payload + 4, 4U) != 0) {
        return 0;
    }
    if (cna_storage_stream_seek(stream, INT64_C(-2), CNA_SEEK_ORIGIN_END, &position) !=
            CNA_RESULT_SUCCESS ||
        position != INT64_C(6)) {
        return 0;
    }
    if (cna_storage_stream_seek(stream, INT64_C(-1), CNA_SEEK_ORIGIN_BEGIN, &position) !=
        CNA_RESULT_IO) {
        return 0;
    }
    if (cna_storage_stream_seek(stream, INT64_C(0), UINT32_C(7), &position) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_stream_read(stream, buffer, UINT64_C(0), &read) != CNA_RESULT_SUCCESS ||
        read != UINT64_C(0)) {
        return 0;
    }
    return cna_storage_stream_close(stream) == CNA_RESULT_SUCCESS;
}

static int validate_access_and_share(const CNA_StorageContainerHandle container)
{
    CNA_StorageStreamHandle stream = CNA_INVALID_HANDLE;
    CNA_Bool capability = CNA_TRUE;
    int64_t length = INT64_C(0);
    const uint8_t byte = 0xEEU;

    if (cna_storage_container_open_file_access(
            container,
            view("save.dat"),
            CNA_FILE_MODE_OPEN,
            CNA_FILE_ACCESS_READ,
            &stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_stream_get_can_write(stream, &capability) != CNA_RESULT_SUCCESS ||
        capability != CNA_FALSE) {
        return 0;
    }
    // The canonical stream refuses the write instead of dropping the bytes, so the C route
    // reports it as unsupported rather than as success.
    if (cna_storage_stream_write(stream, &byte, UINT64_C(1)) != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    if (cna_storage_stream_close(stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_storage_container_open_file_share(
            container,
            view("save.dat"),
            CNA_FILE_MODE_OPEN,
            CNA_FILE_ACCESS_READ_WRITE,
            CNA_FILE_SHARE_READ | CNA_FILE_SHARE_DELETE,
            &stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_stream_set_length(stream, INT64_C(4)) != CNA_RESULT_SUCCESS ||
        cna_storage_stream_get_length(stream, &length) != CNA_RESULT_SUCCESS ||
        length != INT64_C(4)) {
        return 0;
    }
    if (cna_storage_stream_set_length(stream, INT64_C(-1)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_stream_close(stream) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_storage_container_open_file(container, view("save.dat"), UINT32_C(99), &stream) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_container_open_file_access(
            container,
            view("save.dat"),
            CNA_FILE_MODE_OPEN,
            UINT32_C(7),
            &stream) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_container_open_file_share(
            container,
            view("save.dat"),
            CNA_FILE_MODE_OPEN,
            CNA_FILE_ACCESS_READ_WRITE,
            UINT32_C(64),
            &stream) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    // Append with read access is rejected by the canonical file-mode validation.
    if (cna_storage_container_open_file_access(
            container,
            view("save.dat"),
            CNA_FILE_MODE_APPEND,
            CNA_FILE_ACCESS_READ,
            &stream) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    // A missing file is an I/O failure, not a generic internal one.
    return cna_storage_container_open_file(
        container,
        view("missing.dat"),
        CNA_FILE_MODE_OPEN,
        &stream) == CNA_RESULT_IO;
}

static int validate_files(const CNA_StorageContainerHandle container)
{
    static const uint8_t payload[8] = {0x10U, 0x20U, 0x30U, 0x40U, 0x50U, 0x60U, 0x70U, 0x80U};
    char buffer[256];
    uint64_t count = UINT64_C(0);
    uint64_t copied = UINT64_C(0);
    CNA_Bool exists = CNA_TRUE;

    if (!validate_write(container, payload)) {
        return 0;
    }
    if (cna_storage_container_file_exists(container, view("save.dat"), &exists) !=
            CNA_RESULT_SUCCESS ||
        exists != CNA_TRUE) {
        return 0;
    }
    if (cna_storage_container_get_file_name_count(container, all_names(), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(1)) {
        return 0;
    }
    memset(buffer, 0, sizeof(buffer));
    if (cna_storage_container_copy_file_name(
            container,
            view("*.dat"),
            UINT64_C(0),
            buffer,
            (uint64_t)sizeof(buffer),
            &copied) != CNA_RESULT_SUCCESS ||
        copied != UINT64_C(8) || strcmp(buffer, "save.dat") != 0) {
        return 0;
    }
    if (cna_storage_container_get_file_name_count(container, view("*.png"), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(0)) {
        return 0;
    }
    if (!validate_read(container, payload)) {
        return 0;
    }
    if (!validate_access_and_share(container)) {
        return 0;
    }
    if (cna_storage_container_delete_file(container, view("save.dat")) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_storage_container_file_exists(container, view("save.dat"), &exists) ==
            CNA_RESULT_SUCCESS &&
        exists == CNA_FALSE;
}

static int validate_disposal(const CNA_StorageContainerHandle container)
{
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    CNA_Bool disposed = CNA_TRUE;
    int disposals = 0;

    if (cna_storage_container_subscribe_disposing(
            container,
            on_completed,
            &disposals,
            &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_container_subscribe_disposing(container, 0, 0, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_container_subscribe_disposing(container, on_completed, &disposals, 0) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS || disposals != 1) {
        return 0;
    }
    if (cna_storage_container_get_is_disposed(container, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_TRUE) {
        return 0;
    }
    // Disposal is idempotent and raises the canonical event exactly once.
    if (cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS || disposals != 1) {
        return 0;
    }
    if (cna_storage_container_unsubscribe_disposing(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_storage_container_unsubscribe_disposing(registration) == CNA_RESULT_INVALID_HANDLE;
}

/* Records a disposal and asks the container about itself from inside the callback, which is how
   the ordering half of the contract is observable at all: the event has to arrive while the
   handle is still answerable, not after it has been invalidated. */
typedef struct DisposingObserver {
    CNA_StorageContainerHandle container;
    int calls;
    CNA_Result query_result;
    CNA_Bool seen_disposed;
} DisposingObserver;

static void on_disposing_observed(void* const context)
{
    DisposingObserver* const observer = (DisposingObserver*)context;
    observer->calls += 1;
    observer->query_result = cna_storage_container_get_is_disposed(
        observer->container, &observer->seen_disposed);
}

/* CABI-34: the other ordering half the work order asks for -- Disposing relative to the container's
   own file cleanup. Asked from inside the callback, because that is the only moment at which the
   answer distinguishes "the event arrives before the container tears its contents down" from
   "after". */
typedef struct FileVisibilityObserver {
    CNA_StorageContainerHandle container;
    int calls;
    CNA_Result query_result;
    CNA_Bool seen_file;
} FileVisibilityObserver;

static void on_disposing_checks_files(void* const context)
{
    FileVisibilityObserver* const observer = (FileVisibilityObserver*)context;
    observer->calls += 1;
    observer->query_result = cna_storage_container_file_exists(
        observer->container, view("cabi-ordering.bin"), &observer->seen_file);
}

static int validate_disposal_edges(const CNA_StorageDeviceHandle device)
{
    CNA_StorageContainerHandle container = CNA_INVALID_HANDLE;
    CNA_Handle first = CNA_INVALID_HANDLE;
    CNA_Handle second = CNA_INVALID_HANDLE;
    int a = 0;
    int b = 0;

    /* Two subscribers on one container: both run, each exactly once. */
    if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_container_subscribe_disposing(container, on_completed, &a, &first) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_subscribe_disposing(container, on_completed, &b, &second) !=
            CNA_RESULT_SUCCESS ||
        first == second) {
        return 0;
    }
    if (cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS || a != 1 || b != 1) {
        return 0;
    }
    if (cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_SUCCESS ||
        cna_storage_container_unsubscribe_disposing(second) != CNA_RESULT_SUCCESS ||
        cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A subscriber removed before disposal does not run. Without this, an implementation that
       ignored unsubscribe entirely would still pass every count-based assertion above. */
    a = 0;
    if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_subscribe_disposing(container, on_completed, &a, &first) !=
            CNA_RESULT_SUCCESS ||
        cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_SUCCESS ||
        cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS ||
        a != 0 ||
        cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Destroying a container that was never disposed still raises Disposing exactly once -- the
       destroy-if-needed path, which the explicit-dispose cases never reach. */
    {
        DisposingObserver observer = {CNA_INVALID_HANDLE, 0, CNA_RESULT_SUCCESS, CNA_FALSE};
        if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
        observer.container = container;
        if (cna_storage_container_subscribe_disposing(
                container, on_disposing_observed, &observer, &first) != CNA_RESULT_SUCCESS ||
            cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS ||
            observer.calls != 1) {
            return 0;
        }
        /* Ordering: the callback ran while the container was still addressable, and reported
           itself already disposed -- the event follows the state change, not the handle's
           release. */
        if (observer.query_result != CNA_RESULT_SUCCESS || observer.seen_disposed != CNA_TRUE) {
            return 0;
        }
        /* The registration is a separately owned handle, as its header says, so destroying the
           container does not release it -- the caller still has to, and only once. Pinning this
           keeps the ownership rule from drifting into an implicit free. */
        if (cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_SUCCESS ||
            cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_INVALID_HANDLE) {
            return 0;
        }
    }

    /* Repeated create/destroy leaves nothing behind that changes the next cycle's answer. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        int calls = 0;
        if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
                CNA_RESULT_SUCCESS ||
            cna_storage_container_subscribe_disposing(container, on_completed, &calls, &first) !=
                CNA_RESULT_SUCCESS ||
            cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS ||
            calls != 1) {
            return 0;
        }
    }

    /* CABI-34: double dispose. The second call must be accepted and must not raise again --
       "exactly once" has to survive a caller that disposes defensively. */
    {
        int calls = 0;
        if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
                CNA_RESULT_SUCCESS ||
            cna_storage_container_subscribe_disposing(container, on_completed, &calls, &first) !=
                CNA_RESULT_SUCCESS ||
            cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS ||
            calls != 1) {
            return 0;
        }
        if (cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS || calls != 1) {
            return 0;
        }
        /* And destroying an already-disposed container does not raise a third time either. */
        if (cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS || calls != 1) {
            return 0;
        }
        if (cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* CABI-34: ordering against the container's own file cleanup. The event must arrive while the
       container's contents are still there, for the same reason it must arrive while the handle is
       still answerable: a subscriber whose whole purpose is to flush or inspect state cannot do it
       after the state is gone. */
    {
        FileVisibilityObserver observer = {CNA_INVALID_HANDLE, 0, CNA_RESULT_SUCCESS, CNA_FALSE};
        CNA_StorageStreamHandle file = CNA_INVALID_HANDLE;
        if (cna_storage_container_open(device, view("cabi-edges"), 0, 0, &container) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
        observer.container = container;
        if (cna_storage_container_create_file(container, view("cabi-ordering.bin"), &file) !=
                CNA_RESULT_SUCCESS ||
            cna_storage_stream_close(file) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (cna_storage_container_subscribe_disposing(
                container, on_disposing_checks_files, &observer, &first) != CNA_RESULT_SUCCESS ||
            cna_storage_container_dispose(container) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        if (observer.calls != 1 || observer.query_result != CNA_RESULT_SUCCESS ||
            observer.seen_file != CNA_TRUE) {
            return 0;
        }
        if (cna_storage_container_unsubscribe_disposing(first) != CNA_RESULT_SUCCESS ||
            cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    return cna_storage_device_delete_container(device, view("cabi-edges")) == CNA_RESULT_SUCCESS;
}

static int validate_storage(void)
{
    CNA_StorageDeviceHandle device = CNA_INVALID_HANDLE;
    CNA_StorageContainerHandle container = CNA_INVALID_HANDLE;
    int completions = 0;

    if (cna_storage_device_show_selector(0, 0, &device) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (!validate_device_state(device) || !validate_device_changed()) {
        return 0;
    }
    // Start from a known-empty container even when an earlier run stopped part way through.
    if (cna_storage_device_delete_container(device, view(ContainerName)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_storage_container_open(
            device,
            view(ContainerName),
            on_completed,
            &completions,
            &container) != CNA_RESULT_SUCCESS ||
        completions != 1) {
        return 0;
    }
    if (!validate_container_identity(device, container) || !validate_directories(container) ||
        !validate_files(container) || !validate_disposal(container) ||
        !validate_disposal_edges(device)) {
        return 0;
    }
    // A device cannot be released while it still owns a live container.
    if (cna_storage_device_destroy(device) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_storage_container_destroy(container) != CNA_RESULT_SUCCESS ||
        cna_storage_container_destroy(container) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    if (cna_storage_device_delete_container(device, view(ContainerName)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    // A title name that escapes the storage root is refused rather than resolved.
    if (cna_storage_device_delete_container(device, view("../escape")) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_storage_device_delete_container(device, all_names()) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_storage_container_open(device, all_names(), 0, 0, &container) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_storage_device_destroy(device) == CNA_RESULT_SUCCESS &&
        cna_storage_device_destroy(device) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    if (!validate_identities()) {
        return CNA_TEST_FAIL(1);
    }
    if (!validate_root()) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_selectors()) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_storage()) {
        return CNA_TEST_FAIL(4);
    }
    return 0;
}
