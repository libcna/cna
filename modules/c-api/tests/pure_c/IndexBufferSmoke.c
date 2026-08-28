// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_IndexBufferHandle) == 8U,
               "CNA_IndexBufferHandle size changed");
_Static_assert(sizeof(CNA_IndexBufferEventRegistrationHandle) == 8U,
               "CNA_IndexBufferEventRegistrationHandle size changed");
_Static_assert(sizeof(CNA_IndexBufferCreateInfo) == 24U &&
                   _Alignof(CNA_IndexBufferCreateInfo) == 4U,
               "CNA_IndexBufferCreateInfo layout changed");
_Static_assert(offsetof(CNA_IndexBufferCreateInfo, index_count) == 8U &&
                   offsetof(CNA_IndexBufferCreateInfo, index_element_size) == 12U &&
                   offsetof(CNA_IndexBufferCreateInfo, dynamic) == 20U,
               "CNA_IndexBufferCreateInfo field offsets changed");
_Static_assert(sizeof(CNA_IndexBufferInfo) == 24U &&
                   _Alignof(CNA_IndexBufferInfo) == 4U,
               "CNA_IndexBufferInfo layout changed");
_Static_assert(offsetof(CNA_IndexBufferInfo, index_count) == 8U &&
                   offsetof(CNA_IndexBufferInfo, dynamic) == 20U,
               "CNA_IndexBufferInfo field offsets changed");
_Static_assert(sizeof(CNA_IndexBufferTransfer) == 32U &&
                   _Alignof(CNA_IndexBufferTransfer) == 8U,
               "CNA_IndexBufferTransfer layout changed");
_Static_assert(offsetof(CNA_IndexBufferTransfer, start_index) == 16U &&
                   offsetof(CNA_IndexBufferTransfer, element_count) == 24U,
               "CNA_IndexBufferTransfer field offsets changed");

typedef struct LifecycleState {
    CNA_Handle borrowed_device;
    int content_lost_count;
    int disposing_count;
    int validated;
} LifecycleState;

typedef struct WrongThreadState {
    CNA_IndexBufferHandle buffer;
    CNA_IndexBufferEventRegistrationHandle registration;
    CNA_Result info_result;
    CNA_Result unsubscribe_result;
} WrongThreadState;

static CNA_IndexBufferTransfer make_transfer(
    const CNA_IndexElementSize element_size,
    const CNA_SetDataOptions options,
    const uint64_t start,
    const uint64_t count)
{
    const CNA_IndexBufferTransfer transfer = {
        sizeof(CNA_IndexBufferTransfer), UINT32_C(1),
        element_size, options, start, count};
    return transfer;
}

static CNA_Result create_buffer(
    const CNA_Handle device,
    const CNA_IndexElementSize element_size,
    const CNA_Bool dynamic,
    const CNA_BufferUsage usage,
    const int32_t count,
    CNA_IndexBufferHandle* const out_buffer)
{
    const CNA_IndexBufferCreateInfo info = {
        sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), count,
        element_size, usage, dynamic, {0U, 0U, 0U}};
    return cna_index_buffer_create(device, &info, out_buffer);
}

static int validate_transfer(
    const CNA_Handle device,
    const CNA_IndexElementSize element_size,
    const CNA_Bool dynamic,
    const CNA_SetDataOptions options)
{
    CNA_IndexBufferHandle buffer = CNA_INVALID_HANDLE;
    if (create_buffer(
            device, element_size, dynamic, CNA_BUFFER_USAGE_NONE, 2, &buffer) !=
            CNA_RESULT_SUCCESS || buffer == CNA_INVALID_HANDLE) {
        return 0;
    }

    CNA_IndexBufferInfo info = {
        sizeof(CNA_IndexBufferInfo), UINT32_C(1), 0, UINT32_MAX,
        UINT32_MAX, CNA_FALSE, CNA_TRUE, CNA_FALSE, 1U};
    uint64_t type_name_count = UINT64_MAX;
    char type_name[sizeof(CNA_INDEX_BUFFER_TYPE_NAME) - 1U];
    CNA_Handle owner_device = CNA_INVALID_HANDLE;
    if (cna_index_buffer_get_info(buffer, &info) != CNA_RESULT_SUCCESS ||
        info.index_count != 2 || info.index_element_size != element_size ||
        info.buffer_usage != CNA_BUFFER_USAGE_NONE || info.dynamic != dynamic ||
        info.is_content_lost != CNA_FALSE || info.has_renderer != CNA_TRUE ||
        info.reserved != 0U ||
        cna_index_buffer_get_type_name_byte_count(buffer, &type_name_count) !=
            CNA_RESULT_SUCCESS || type_name_count != sizeof(type_name) ||
        cna_index_buffer_copy_type_name(
            buffer, type_name, sizeof(type_name), &type_name_count) != CNA_RESULT_SUCCESS ||
        memcmp(type_name, CNA_INDEX_BUFFER_TYPE_NAME, sizeof(type_name)) != 0 ||
        cna_graphics_resource_get_graphics_device(buffer, &owner_device) !=
            CNA_RESULT_SUCCESS || owner_device != device) {
        return 0;
    }

    CNA_IndexBufferTransfer transfer = make_transfer(element_size, options, 1U, 2U);
    uint64_t required = UINT64_MAX;
    if (element_size == CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS) {
        const uint16_t source[3] = {UINT16_C(11), UINT16_C(22), UINT16_C(33)};
        uint16_t destination[3] = {UINT16_C(101), UINT16_C(102), UINT16_C(103)};
        const uint16_t sentinel[3] = {UINT16_C(101), UINT16_C(102), UINT16_C(103)};
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.options = CNA_SET_DATA_NONE;
        if (cna_index_buffer_get_data(buffer, &transfer, destination, 2U, &required) !=
                CNA_RESULT_BUFFER_TOO_SMALL || required != 2U ||
            memcmp(destination, sentinel, sizeof(destination)) != 0 ||
            cna_index_buffer_get_data(buffer, &transfer, destination, 3U, &required) !=
                CNA_RESULT_SUCCESS || destination[0] != sentinel[0] ||
            destination[1] != source[1] || destination[2] != source[2]) {
            return 0;
        }
        transfer.start_index = 0U;
        transfer.element_count = 2U;
        transfer.options = options;
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        destination[0] = destination[1] = destination[2] = 0U;
        transfer.options = CNA_SET_DATA_NONE;
        if (cna_index_buffer_get_data(buffer, &transfer, destination, 2U, &required) !=
                CNA_RESULT_SUCCESS || destination[0] != source[0] ||
            destination[1] != source[1]) {
            return 0;
        }
        if (dynamic == CNA_TRUE &&
            cna_index_buffer_set_data(buffer, &transfer, source, 2U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.element_count = 0U;
        transfer.start_index = 3U;
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.start_index = 0U;
        if (cna_index_buffer_set_data(buffer, &transfer, 0, 0U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        /* CBIND-059: a windowed upload indexes the BUFFER, not the caller's array, so the index
           it does not name keeps whatever it held -- which is exactly what every other transfer
           route here destroys. */
        {
            const uint16_t window[1] = {UINT16_C(77)};
            uint16_t after[2] = {0U, 0U};
            transfer.element_count = 1U;
            transfer.start_index = 0U;
            transfer.options = CNA_SET_DATA_NONE;
            if (cna_index_buffer_set_data_at(buffer, 2U, &transfer, window, 1U) !=
                    CNA_RESULT_SUCCESS) {
                return 0;
            }
            transfer.element_count = 2U;
            if (cna_index_buffer_get_data(buffer, &transfer, after, 2U, &required) !=
                    CNA_RESULT_SUCCESS ||
                after[0] != source[0] || after[1] != window[0]) {
                return 0;
            }
            /* A window past the end, an unaligned offset, and a streaming hint that contradicts
               "keep the rest" are each refused. */
            transfer.element_count = 1U;
            if (cna_index_buffer_set_data_at(buffer, 4U, &transfer, window, 1U) ==
                    CNA_RESULT_SUCCESS ||
                cna_index_buffer_set_data_at(buffer, 1U, &transfer, window, 1U) ==
                    CNA_RESULT_SUCCESS) {
                return 0;
            }
            transfer.options = CNA_SET_DATA_DISCARD;
            if (cna_index_buffer_set_data_at(buffer, 0U, &transfer, window, 1U) !=
                    CNA_RESULT_NOT_SUPPORTED) {
                return 0;
            }
            transfer.options = CNA_SET_DATA_NONE;
            /* And the buffer is left as the window made it, for whatever runs after this. */
            transfer.element_count = 2U;
            if (cna_index_buffer_get_data(buffer, &transfer, after, 2U, &required) !=
                    CNA_RESULT_SUCCESS ||
                after[0] != source[0] || after[1] != window[0]) {
                return 0;
            }
            transfer.element_count = 2U;
            transfer.start_index = 0U;
            transfer.options = options;
            if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                    CNA_RESULT_SUCCESS) {
                return 0;
            }
            transfer.options = CNA_SET_DATA_NONE;
        }
    } else {
        const uint32_t source[3] = {UINT32_C(111), UINT32_C(222), UINT32_C(333)};
        uint32_t destination[3] = {UINT32_C(1001), UINT32_C(1002), UINT32_C(1003)};
        const uint32_t sentinel[3] = {UINT32_C(1001), UINT32_C(1002), UINT32_C(1003)};
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.options = CNA_SET_DATA_NONE;
        if (cna_index_buffer_get_data(buffer, &transfer, destination, 2U, &required) !=
                CNA_RESULT_BUFFER_TOO_SMALL || required != 2U ||
            memcmp(destination, sentinel, sizeof(destination)) != 0 ||
            cna_index_buffer_get_data(buffer, &transfer, destination, 3U, &required) !=
                CNA_RESULT_SUCCESS || destination[0] != sentinel[0] ||
            destination[1] != source[1] || destination[2] != source[2]) {
            return 0;
        }
        transfer.start_index = 0U;
        transfer.element_count = 2U;
        transfer.options = options;
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        destination[0] = destination[1] = destination[2] = 0U;
        transfer.options = CNA_SET_DATA_NONE;
        if (cna_index_buffer_get_data(buffer, &transfer, destination, 2U, &required) !=
                CNA_RESULT_SUCCESS || destination[0] != source[0] ||
            destination[1] != source[1]) {
            return 0;
        }
        if (dynamic == CNA_TRUE &&
            cna_index_buffer_set_data(buffer, &transfer, source, 2U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.element_count = 0U;
        transfer.start_index = 3U;
        if (cna_index_buffer_set_data(buffer, &transfer, source, 3U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
        transfer.start_index = 0U;
        if (cna_index_buffer_set_data(buffer, &transfer, 0, 0U) !=
                CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    return cna_index_buffer_destroy(buffer) == CNA_RESULT_SUCCESS;
}

static void on_content_lost(
    const CNA_IndexBufferHandle buffer,
    void* const context)
{
    LifecycleState* const state = (LifecycleState*)context;
    if (buffer != CNA_INVALID_HANDLE) {
        ++state->content_lost_count;
    }
}

static void on_disposing(const CNA_Handle resource, void* const context)
{
    LifecycleState* const state = (LifecycleState*)context;
    CNA_Bool disposed = CNA_TRUE;
    if (resource != CNA_INVALID_HANDLE &&
        cna_graphics_resource_get_is_disposed(resource, &disposed) == CNA_RESULT_SUCCESS &&
        disposed == CNA_FALSE) {
        ++state->disposing_count;
    }
}

static int use_buffer_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_IndexBufferInfo info = {
        sizeof(CNA_IndexBufferInfo), UINT32_C(1), 0, 0U,
        0U, CNA_FALSE, CNA_FALSE, CNA_FALSE, 0U};
    state->info_result = cna_index_buffer_get_info(state->buffer, &info);
    state->unsubscribe_result =
        cna_index_buffer_unsubscribe_content_lost(state->registration);
    return 0;
}

static int validate_lifecycle_and_failures(
    const CNA_Handle device,
    LifecycleState* const lifecycle)
{
    CNA_IndexBufferHandle static_buffer = CNA_INVALID_HANDLE;
    CNA_IndexBufferHandle dynamic_buffer = CNA_INVALID_HANDLE;
    if (create_buffer(
            device, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_FALSE,
            CNA_BUFFER_USAGE_NONE, 2, &static_buffer) != CNA_RESULT_SUCCESS ||
        create_buffer(
            device, CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS, CNA_TRUE,
            CNA_BUFFER_USAGE_NONE, 2, &dynamic_buffer) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_IndexBufferEventRegistrationHandle content_registration = CNA_INVALID_HANDLE;
    CNA_GraphicsResourceEventRegistrationHandle disposing_registration = CNA_INVALID_HANDLE;
    if (cna_index_buffer_subscribe_content_lost(
            static_buffer, on_content_lost, lifecycle, &content_registration) !=
            CNA_RESULT_NOT_SUPPORTED || content_registration != CNA_INVALID_HANDLE ||
        cna_index_buffer_subscribe_content_lost(
            dynamic_buffer, on_content_lost, lifecycle, &content_registration) !=
            CNA_RESULT_SUCCESS || content_registration == CNA_INVALID_HANDLE ||
        cna_graphics_resource_subscribe_disposing(
            static_buffer, on_disposing, lifecycle, &disposing_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_index_buffer_unsubscribe_content_lost(disposing_registration) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_graphics_resource_unsubscribe_disposing(content_registration) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    WrongThreadState wrong_thread = {
        dynamic_buffer, content_registration, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, use_buffer_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.info_result != CNA_RESULT_THREAD ||
        wrong_thread.unsubscribe_result != CNA_RESULT_THREAD) {
        return 0;
    }

    const uint16_t source16[2] = {UINT16_C(4), UINT16_C(7)};
    CNA_IndexBufferTransfer transfer = make_transfer(
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_SET_DATA_DISCARD, 0U, 2U);
    if (cna_index_buffer_set_data(static_buffer, &transfer, source16, 2U) !=
            CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    uint16_t destination16[2] = {UINT16_C(91), UINT16_C(92)};
    const uint16_t sentinel16[2] = {UINT16_C(91), UINT16_C(92)};
    uint64_t required = 0U;
    transfer.options = CNA_SET_DATA_NONE;
    transfer.index_element_size = CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS;
    if (cna_index_buffer_get_data(
            static_buffer, &transfer, destination16, 2U, &required) !=
            CNA_RESULT_INVALID_ARGUMENT || required != 2U ||
        memcmp(destination16, sentinel16, sizeof(destination16)) != 0) {
        return 0;
    }
    transfer.index_element_size = UINT32_MAX;
    if (cna_index_buffer_set_data(static_buffer, &transfer, source16, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_IndexBufferHandle write_only = CNA_INVALID_HANDLE;
    if (create_buffer(
            device, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_FALSE,
            CNA_BUFFER_USAGE_WRITE_ONLY, 2, &write_only) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    transfer = make_transfer(
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_SET_DATA_NONE, 0U, 2U);
    if (cna_index_buffer_set_data(write_only, &transfer, source16, 2U) !=
            CNA_RESULT_SUCCESS ||
        cna_index_buffer_get_data(
            write_only, &transfer, destination16, 2U, &required) !=
            CNA_RESULT_NOT_SUPPORTED || required != 2U ||
        memcmp(destination16, sentinel16, sizeof(destination16)) != 0 ||
        cna_index_buffer_destroy(write_only) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_graphics_resource_dispose(static_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->disposing_count != 1 ||
        cna_graphics_resource_dispose(static_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->disposing_count != 1) {
        return 0;
    }
    CNA_IndexBufferInfo info = {
        sizeof(CNA_IndexBufferInfo), UINT32_C(1), 0, 0U,
        0U, CNA_FALSE, CNA_FALSE, CNA_TRUE, 0U};
    if (cna_index_buffer_get_info(static_buffer, &info) != CNA_RESULT_SUCCESS ||
        info.has_renderer != CNA_FALSE ||
        cna_index_buffer_set_data(static_buffer, &transfer, source16, 2U) !=
            CNA_RESULT_INVALID_STATE ||
        cna_graphics_resource_unsubscribe_disposing(disposing_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_index_buffer_destroy(static_buffer) != CNA_RESULT_SUCCESS ||
        cna_index_buffer_get_info(static_buffer, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_index_buffer_destroy(dynamic_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->content_lost_count != 0 ||
        cna_index_buffer_unsubscribe_content_lost(content_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_index_buffer_unsubscribe_content_lost(content_registration) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_IndexBufferCreateInfo invalid = {
        sizeof(CNA_IndexBufferCreateInfo), UINT32_C(1), 1,
        CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE,
        CNA_FALSE, {1U, 0U, 0U}};
    CNA_IndexBufferHandle output = UINT64_MAX;
    if (cna_curve_create(&wrong_kind) != CNA_RESULT_SUCCESS ||
        cna_index_buffer_get_info(wrong_kind, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_index_buffer_create(device, &invalid, &output) != CNA_RESULT_INVALID_ARGUMENT ||
        output != CNA_INVALID_HANDLE ||
        cna_index_buffer_create(device, &invalid, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_destroy(wrong_kind) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_unsupported_creation(const CNA_Handle device)
{
    CNA_IndexBufferHandle buffer = UINT64_MAX;
    const CNA_Result result = create_buffer(
        device, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_FALSE,
        CNA_BUFFER_USAGE_NONE, 1, &buffer);
    return result == CNA_RESULT_NOT_SUPPORTED && buffer == CNA_INVALID_HANDLE;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const callback_error)
{
    (void)game_time;
    (void)callback_error;
    LifecycleState* const state = (LifecycleState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Bool supports_three_d = CNA_FALSE;
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE ||
        cna_graphics_device_supports_capability(
            device, CNA_GRAPHICS_CAPABILITY_THREE_D, &supports_three_d) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (supports_three_d == CNA_FALSE) {
        if (!validate_unsupported_creation(device)) {
            return CNA_RESULT_INVALID_STATE;
        }
    } else if (
        !validate_transfer(
            device, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS,
            CNA_FALSE, CNA_SET_DATA_NONE) ||
        !validate_transfer(
            device, CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS,
            CNA_FALSE, CNA_SET_DATA_NONE) ||
        !validate_transfer(
            device, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS,
            CNA_TRUE, CNA_SET_DATA_DISCARD) ||
        !validate_transfer(
            device, CNA_INDEX_ELEMENT_SIZE_THIRTY_TWO_BITS,
            CNA_TRUE, CNA_SET_DATA_NO_OVERWRITE) ||
        !validate_lifecycle_and_failures(device, state)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->borrowed_device = device;
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    LifecycleState state = {CNA_INVALID_HANDLE, 0, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    static const char Title[] = "C API index buffers";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks};
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_RendererInfo stale = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U};
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        state.validated != 1 || state.content_lost_count != 0 ||
        (state.disposing_count != 0 && state.disposing_count != 1) ||
        state.borrowed_device == CNA_INVALID_HANDLE ||
        cna_graphics_device_get_renderer_info(state.borrowed_device, &stale) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(1);
    }
    return 0;
}
