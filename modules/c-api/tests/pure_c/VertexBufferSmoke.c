// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <threads.h>

_Static_assert(sizeof(CNA_VertexBufferHandle) == 8U,
               "CNA_VertexBufferHandle size changed");
_Static_assert(sizeof(CNA_VertexBufferEventRegistrationHandle) == 8U,
               "CNA_VertexBufferEventRegistrationHandle size changed");
_Static_assert(sizeof(CNA_VertexBufferCreateInfo) == 32U &&
                   _Alignof(CNA_VertexBufferCreateInfo) == 8U,
               "CNA_VertexBufferCreateInfo layout changed");
_Static_assert(offsetof(CNA_VertexBufferCreateInfo, vertex_declaration) == 8U &&
                   offsetof(CNA_VertexBufferCreateInfo, vertex_count) == 16U &&
                   offsetof(CNA_VertexBufferCreateInfo, dynamic) == 24U,
               "CNA_VertexBufferCreateInfo field offsets changed");
_Static_assert(sizeof(CNA_VertexBufferInfo) == 32U &&
                   _Alignof(CNA_VertexBufferInfo) == 8U,
               "CNA_VertexBufferInfo layout changed");
_Static_assert(offsetof(CNA_VertexBufferInfo, vertex_stride) == 20U &&
                   offsetof(CNA_VertexBufferInfo, vertex_element_count) == 24U,
               "CNA_VertexBufferInfo field offsets changed");
_Static_assert(sizeof(CNA_VertexBufferTransfer) == 32U &&
                   _Alignof(CNA_VertexBufferTransfer) == 8U,
               "CNA_VertexBufferTransfer layout changed");
_Static_assert(offsetof(CNA_VertexBufferTransfer, start_index) == 16U &&
                   offsetof(CNA_VertexBufferTransfer, element_count) == 24U,
               "CNA_VertexBufferTransfer field offsets changed");

typedef struct LifecycleState {
    CNA_Handle borrowed_device;
    int content_lost_count;
    int disposing_count;
    int validated;
} LifecycleState;

typedef struct WrongThreadState {
    CNA_VertexBufferHandle buffer;
    CNA_VertexBufferEventRegistrationHandle registration;
    CNA_Result info_result;
    CNA_Result unsubscribe_result;
} WrongThreadState;

static CNA_VertexBufferTransfer make_transfer(
    const CNA_VertexType type,
    const CNA_SetDataOptions options,
    const uint64_t start,
    const uint64_t count)
{
    const CNA_VertexBufferTransfer transfer = {
        sizeof(CNA_VertexBufferTransfer), UINT32_C(1),
        type, options, start, count};
    return transfer;
}

static int make_declaration(
    const CNA_VertexType type,
    CNA_VertexDeclarationHandle* const out_declaration)
{
    CNA_VertexElement elements[8];
    uint64_t count = 0U;
    uint32_t stride = 0U;
    if (cna_vertex_type_get_stride(type, &stride) != CNA_RESULT_SUCCESS ||
        cna_vertex_type_copy_elements(type, 0, 0U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || count == 0U || count > 8U ||
        cna_vertex_type_copy_elements(type, elements, 8U, &count) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_create_with_stride(
            (int32_t)stride, elements, count, out_declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int create_buffer(
    const CNA_Handle device,
    const CNA_VertexType type,
    const CNA_Bool dynamic,
    const CNA_BufferUsage usage,
    const int32_t count,
    CNA_VertexBufferHandle* const out_buffer)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    if (!make_declaration(type, &declaration)) {
        return 0;
    }
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), declaration,
        count, usage, dynamic, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    const CNA_Result result = cna_vertex_buffer_create(device, &info, out_buffer);
    if (cna_vertex_declaration_destroy(declaration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return result == CNA_RESULT_SUCCESS && *out_buffer != CNA_INVALID_HANDLE;
}

static int run_typed_transfer(
    const CNA_Handle device,
    const CNA_VertexType type,
    const CNA_Bool dynamic,
    const CNA_SetDataOptions options,
    const void* const source,
    const size_t element_size)
{
    CNA_VertexBufferHandle buffer = CNA_INVALID_HANDLE;
    if (!create_buffer(
            device, type, dynamic, CNA_BUFFER_USAGE_NONE, 2, &buffer)) {
        return 0;
    }

    CNA_VertexBufferInfo info = {
        sizeof(CNA_VertexBufferInfo), UINT32_C(1), 0, UINT32_MAX,
        CNA_FALSE, CNA_TRUE, CNA_FALSE, 0U, 0, 0U};
    CNA_VertexElement elements[8];
    uint64_t declaration_count = UINT64_MAX;
    uint64_t type_name_count = UINT64_MAX;
    char type_name[sizeof(CNA_VERTEX_BUFFER_TYPE_NAME) - 1U];
    CNA_Handle owner_device = CNA_INVALID_HANDLE;
    if (cna_vertex_buffer_get_info(buffer, &info) != CNA_RESULT_SUCCESS ||
        info.vertex_count != 2 || info.buffer_usage != CNA_BUFFER_USAGE_NONE ||
        info.dynamic != dynamic || info.is_content_lost != CNA_FALSE ||
        info.has_renderer != CNA_TRUE || info.vertex_stride <= 0 ||
        info.vertex_element_count == 0U ||
        cna_vertex_buffer_copy_declaration_elements(
            buffer, elements, 8U, &declaration_count) != CNA_RESULT_SUCCESS ||
        declaration_count != info.vertex_element_count ||
        cna_vertex_buffer_get_type_name_byte_count(buffer, &type_name_count) !=
            CNA_RESULT_SUCCESS ||
        type_name_count != sizeof(type_name) ||
        cna_vertex_buffer_copy_type_name(
            buffer, type_name, sizeof(type_name), &type_name_count) != CNA_RESULT_SUCCESS ||
        memcmp(type_name, CNA_VERTEX_BUFFER_TYPE_NAME, sizeof(type_name)) != 0 ||
        cna_graphics_resource_get_graphics_device(buffer, &owner_device) !=
            CNA_RESULT_SUCCESS || owner_device != device) {
        return 0;
    }

    CNA_VertexBufferTransfer transfer = make_transfer(type, options, 1U, 2U);
    if (cna_vertex_buffer_set_data(buffer, &transfer, source, 3U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    unsigned char destination[216];
    unsigned char sentinel[216];
    memset(destination, 0x5a, sizeof(destination));
    memcpy(sentinel, destination, sizeof(destination));
    transfer.options = CNA_SET_DATA_NONE;
    uint64_t required = UINT64_MAX;
    if (cna_vertex_buffer_get_data(
            buffer, &transfer, destination, 2U, &required) !=
            CNA_RESULT_BUFFER_TOO_SMALL || required != 2U ||
        memcmp(destination, sentinel, sizeof(destination)) != 0 ||
        cna_vertex_buffer_get_data(
            buffer, &transfer, destination, 3U, &required) != CNA_RESULT_SUCCESS ||
        required != 2U ||
        memcmp(destination, sentinel, element_size) != 0 ||
        memcmp(destination + element_size,
               (const unsigned char*)source + element_size,
               element_size * 2U) != 0) {
        return 0;
    }

    transfer.options = options;
    transfer.start_index = 0U;
    transfer.element_count = 2U;
    if (cna_vertex_buffer_set_data(buffer, &transfer, source, 3U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(destination, 0x5a, sizeof(destination));
    transfer.options = CNA_SET_DATA_NONE;
    if (cna_vertex_buffer_get_data(
            buffer, &transfer, destination, 2U, &required) != CNA_RESULT_SUCCESS ||
        required != 2U || memcmp(destination, source, element_size * 2U) != 0) {
        return 0;
    }

    transfer.start_index = 3U;
    transfer.element_count = 0U;
    if (cna_vertex_buffer_set_data(buffer, &transfer, source, 3U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    transfer.start_index = 0U;
    if (cna_vertex_buffer_set_data(buffer, &transfer, 0, 0U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    transfer.element_count = 2U;
    memset(destination, 0x5a, sizeof(destination));
    if (cna_vertex_buffer_get_data(
            buffer, &transfer, destination, 2U, &required) != CNA_RESULT_SUCCESS ||
        memcmp(destination, source, element_size * 2U) != 0 ||
        cna_vertex_buffer_destroy(buffer) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_all_typed(const CNA_Handle device)
{
    CNA_VertexPositionColor position_color[3] = {0};
    CNA_VertexPositionColorTexture color_texture[3] = {0};
    CNA_VertexPositionNormalTangentTexture tangent[3] = {0};
    CNA_VertexPositionNormalTangentTextureSkinned tangent_skinned[3] = {0};
    CNA_VertexPositionNormalTexture normal_texture[3] = {0};
    CNA_VertexPositionNormalTextureSkinned normal_skinned[3] = {0};
    CNA_VertexPositionTexture position_texture[3] = {0};
    for (size_t index = 0U; index < 3U; ++index) {
        const float value = (float)(index + 1U);
        position_color[index].position = (CNA_Vector3){value, value + 1.0f, value + 2.0f};
        position_color[index].color =
            (CNA_Color){(uint8_t)(10U + index), (uint8_t)(20U + index),
                        (uint8_t)(30U + index), (uint8_t)(40U + index)};
        color_texture[index].position = position_color[index].position;
        color_texture[index].color = position_color[index].color;
        color_texture[index].texture_coordinate = (CNA_Vector2){value, value + 0.5f};
        tangent[index].position = position_color[index].position;
        tangent[index].normal = (CNA_Vector3){0.0f, 0.0f, value};
        tangent[index].tangent = (CNA_Vector4){value, 0.0f, 0.0f, -1.0f};
        tangent[index].texture_coordinate = color_texture[index].texture_coordinate;
        tangent_skinned[index].position = tangent[index].position;
        tangent_skinned[index].normal = tangent[index].normal;
        tangent_skinned[index].tangent = tangent[index].tangent;
        tangent_skinned[index].texture_coordinate = tangent[index].texture_coordinate;
        tangent_skinned[index].blend_weight =
            (CNA_Vector4){value, value + 1.0f, value + 2.0f, value + 3.0f};
        tangent_skinned[index].blend_indices[0] = (uint8_t)(index + 1U);
        tangent_skinned[index].blend_indices[3] = (uint8_t)(index + 4U);
        normal_texture[index].position = position_color[index].position;
        normal_texture[index].normal = tangent[index].normal;
        normal_texture[index].texture_coordinate = tangent[index].texture_coordinate;
        normal_skinned[index].position = normal_texture[index].position;
        normal_skinned[index].normal = normal_texture[index].normal;
        normal_skinned[index].texture_coordinate = normal_texture[index].texture_coordinate;
        normal_skinned[index].blend_weight = tangent_skinned[index].blend_weight;
        normal_skinned[index].blend_indices[1] = (uint8_t)(index + 5U);
        normal_skinned[index].blend_indices[2] = (uint8_t)(index + 6U);
        position_texture[index].position = position_color[index].position;
        position_texture[index].texture_coordinate = color_texture[index].texture_coordinate;
    }

    return
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_FALSE,
            CNA_SET_DATA_NONE, position_color, sizeof(position_color[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE, CNA_FALSE,
            CNA_SET_DATA_NONE, color_texture, sizeof(color_texture[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE, CNA_FALSE,
            CNA_SET_DATA_NONE, tangent, sizeof(tangent[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_NORMAL_TANGENT_TEXTURE_SKINNED, CNA_FALSE,
            CNA_SET_DATA_NONE, tangent_skinned, sizeof(tangent_skinned[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE, CNA_FALSE,
            CNA_SET_DATA_NONE, normal_texture, sizeof(normal_texture[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED, CNA_FALSE,
            CNA_SET_DATA_NONE, normal_skinned, sizeof(normal_skinned[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_TEXTURE, CNA_FALSE,
            CNA_SET_DATA_NONE, position_texture, sizeof(position_texture[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_TRUE,
            CNA_SET_DATA_DISCARD, position_color, sizeof(position_color[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR_TEXTURE, CNA_TRUE,
            CNA_SET_DATA_NO_OVERWRITE, color_texture, sizeof(color_texture[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE, CNA_TRUE,
            CNA_SET_DATA_NONE, normal_texture, sizeof(normal_texture[0])) &&
        run_typed_transfer(
            device, CNA_VERTEX_TYPE_POSITION_TEXTURE, CNA_TRUE,
            CNA_SET_DATA_DISCARD, position_texture, sizeof(position_texture[0]));
}

static int validate_default_and_raw(const CNA_Handle device)
{
    CNA_VertexBufferCreateInfo empty_info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE,
        0, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    CNA_VertexBufferHandle empty = CNA_INVALID_HANDLE;
    CNA_VertexBufferInfo metadata = {
        sizeof(CNA_VertexBufferInfo), UINT32_C(1), 7, UINT32_MAX,
        CNA_TRUE, CNA_TRUE, CNA_FALSE, 1U, 7, 7U};
    if (cna_vertex_buffer_create(device, &empty_info, &empty) != CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_get_info(empty, &metadata) != CNA_RESULT_SUCCESS ||
        metadata.vertex_count != 0 || metadata.vertex_stride != 0 ||
        metadata.vertex_element_count != 0U ||
        cna_vertex_buffer_set_data_raw(empty, 0, 0U, 0U, 1U) != CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_destroy(empty) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_VertexBufferHandle raw = CNA_INVALID_HANDLE;
    if (!create_buffer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_FALSE,
            CNA_BUFFER_USAGE_NONE, 2, &raw)) {
        return 0;
    }
    const CNA_VertexPositionColor expected[2] = {
        {{1.0f, 2.0f, 3.0f}, {4U, 5U, 6U, 7U}},
        {{8.0f, 9.0f, 10.0f}, {11U, 12U, 13U, 14U}}};
    unsigned char bytes[32];
    memcpy(bytes, expected, sizeof(bytes));
    CNA_VertexPositionColor actual[2] = {0};
    CNA_VertexBufferTransfer transfer = make_transfer(
        CNA_VERTEX_TYPE_POSITION_COLOR, CNA_SET_DATA_NONE, 0U, 2U);
    uint64_t required = 0U;
    if (cna_vertex_buffer_set_data_raw(raw, bytes, sizeof(bytes), 2U, 16U) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_get_data(raw, &transfer, actual, 2U, &required) !=
            CNA_RESULT_SUCCESS || required != 2U ||
        memcmp(actual, expected, sizeof(actual)) != 0 ||
        cna_vertex_buffer_set_data_raw(raw, bytes, 31U, 2U, 16U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_buffer_set_data_raw(raw, bytes, sizeof(bytes), 2U, 12U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_vertex_buffer_destroy(raw) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static void on_content_lost(
    const CNA_VertexBufferHandle buffer,
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
    CNA_VertexBufferInfo info = {
        sizeof(CNA_VertexBufferInfo), UINT32_C(1), 0, 0U,
        CNA_FALSE, CNA_FALSE, CNA_FALSE, 0U, 0, 0U};
    state->info_result = cna_vertex_buffer_get_info(state->buffer, &info);
    state->unsubscribe_result =
        cna_vertex_buffer_unsubscribe_content_lost(state->registration);
    return 0;
}

static int validate_lifecycle_and_failures(
    const CNA_Handle device,
    LifecycleState* const lifecycle)
{
    CNA_VertexBufferHandle static_buffer = CNA_INVALID_HANDLE;
    CNA_VertexBufferHandle dynamic_buffer = CNA_INVALID_HANDLE;
    if (!create_buffer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_FALSE,
            CNA_BUFFER_USAGE_NONE, 2, &static_buffer) ||
        !create_buffer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_TRUE,
            CNA_BUFFER_USAGE_NONE, 2, &dynamic_buffer)) {
        return 0;
    }

    CNA_VertexBufferEventRegistrationHandle content_registration = CNA_INVALID_HANDLE;
    CNA_GraphicsResourceEventRegistrationHandle disposing_registration = CNA_INVALID_HANDLE;
    if (cna_vertex_buffer_subscribe_content_lost(
            static_buffer, on_content_lost, lifecycle, &content_registration) !=
            CNA_RESULT_NOT_SUPPORTED || content_registration != CNA_INVALID_HANDLE ||
        cna_vertex_buffer_subscribe_content_lost(
            dynamic_buffer, on_content_lost, lifecycle, &content_registration) !=
            CNA_RESULT_SUCCESS || content_registration == CNA_INVALID_HANDLE ||
        cna_graphics_resource_subscribe_disposing(
            static_buffer, on_disposing, lifecycle, &disposing_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_unsubscribe_content_lost(disposing_registration) !=
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

    CNA_VertexPositionColor value = {{1.0f, 2.0f, 3.0f}, {4U, 5U, 6U, 7U}};
    CNA_VertexBufferTransfer transfer = make_transfer(
        CNA_VERTEX_TYPE_POSITION_COLOR, CNA_SET_DATA_DISCARD, 0U, 1U);
    if (cna_vertex_buffer_set_data(static_buffer, &transfer, &value, 1U) !=
            CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    transfer.vertex_type = CNA_VERTEX_TYPE_POSITION_NORMAL_TEXTURE_SKINNED;
    if (cna_vertex_buffer_set_data(dynamic_buffer, &transfer, &value, 1U) !=
            CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    transfer.vertex_type = UINT32_MAX;
    transfer.options = CNA_SET_DATA_NONE;
    if (cna_vertex_buffer_set_data(static_buffer, &transfer, &value, 1U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_VertexBufferHandle write_only = CNA_INVALID_HANDLE;
    if (!create_buffer(
            device, CNA_VERTEX_TYPE_POSITION_COLOR, CNA_FALSE,
            CNA_BUFFER_USAGE_WRITE_ONLY, 1, &write_only)) {
        return 0;
    }
    transfer = make_transfer(
        CNA_VERTEX_TYPE_POSITION_COLOR, CNA_SET_DATA_NONE, 0U, 1U);
    CNA_VertexPositionColor write_destination = {
        {99.0f, 98.0f, 97.0f}, {96U, 95U, 94U, 93U}};
    const CNA_VertexPositionColor write_sentinel = write_destination;
    uint64_t required = 0U;
    if (cna_vertex_buffer_set_data(write_only, &transfer, &value, 1U) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_get_data(
            write_only, &transfer, &write_destination, 1U, &required) !=
            CNA_RESULT_NOT_SUPPORTED || required != 1U ||
        memcmp(&write_destination, &write_sentinel, sizeof(write_destination)) != 0 ||
        cna_vertex_buffer_destroy(write_only) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_graphics_resource_dispose(static_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->disposing_count != 1 ||
        cna_graphics_resource_dispose(static_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->disposing_count != 1) {
        return 0;
    }
    CNA_VertexBufferInfo info = {
        sizeof(CNA_VertexBufferInfo), UINT32_C(1), 0, 0U,
        CNA_FALSE, CNA_FALSE, CNA_TRUE, 0U, 0, 0U};
    transfer = make_transfer(
        CNA_VERTEX_TYPE_POSITION_COLOR, CNA_SET_DATA_NONE, 0U, 1U);
    if (cna_vertex_buffer_get_info(static_buffer, &info) != CNA_RESULT_SUCCESS ||
        info.has_renderer != CNA_FALSE ||
        cna_vertex_buffer_set_data(static_buffer, &transfer, &value, 1U) !=
            CNA_RESULT_INVALID_STATE ||
        cna_graphics_resource_unsubscribe_disposing(disposing_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_destroy(static_buffer) != CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_get_info(static_buffer, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_vertex_buffer_destroy(dynamic_buffer) != CNA_RESULT_SUCCESS ||
        lifecycle->content_lost_count != 0 ||
        cna_vertex_buffer_unsubscribe_content_lost(content_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_unsubscribe_content_lost(content_registration) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    CNA_CurveHandle wrong_kind = CNA_INVALID_HANDLE;
    CNA_VertexBufferCreateInfo invalid = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), CNA_INVALID_HANDLE,
        1, CNA_BUFFER_USAGE_NONE, CNA_TRUE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    CNA_VertexBufferHandle output = UINT64_MAX;
    if (cna_curve_create(&wrong_kind) != CNA_RESULT_SUCCESS ||
        cna_vertex_buffer_get_info(wrong_kind, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_vertex_buffer_create(device, &invalid, &output) != CNA_RESULT_INVALID_ARGUMENT ||
        output != CNA_INVALID_HANDLE ||
        cna_vertex_buffer_create(device, &invalid, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_destroy(wrong_kind) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

static int validate_unsupported_creation(const CNA_Handle device)
{
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    if (!make_declaration(CNA_VERTEX_TYPE_POSITION_COLOR, &declaration)) {
        return 0;
    }
    const CNA_VertexBufferCreateInfo info = {
        sizeof(CNA_VertexBufferCreateInfo), UINT32_C(1), declaration,
        1, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0U, 0U, 0U, 0U, 0U, 0U, 0U}};
    CNA_VertexBufferHandle buffer = UINT64_MAX;
    const CNA_Result result = cna_vertex_buffer_create(device, &info, &buffer);
    const CNA_Result destroy_result = cna_vertex_declaration_destroy(declaration);
    if (destroy_result != CNA_RESULT_SUCCESS || result != CNA_RESULT_NOT_SUPPORTED ||
        buffer != CNA_INVALID_HANDLE) {
        fprintf(
            stderr,
            "unsupported vertex-buffer creation: result=%u handle=%llu destroy=%u\n",
            (unsigned int)result,
            (unsigned long long)buffer,
            (unsigned int)destroy_result);
        return 0;
    }
    return 1;
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
        state->borrowed_device = device;
        state->validated = 1;
        return CNA_RESULT_SUCCESS;
    }
    if (
        !validate_all_typed(device) ||
        !validate_default_and_raw(device) ||
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
    static const char Title[] = "C API vertex buffers";
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
        return 1;
    }
    return 0;
}
