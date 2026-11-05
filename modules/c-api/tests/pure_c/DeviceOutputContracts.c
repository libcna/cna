// SPDX-License-Identifier: MS-PL
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include "CnaRuntimeSmoke.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, __func__, #x); cna_smoke_error(); return CNA_TEST_FAIL(1); } } while (0)

int cna_test_texture_format_capability_exception(void);

static int filled(const void* pointer, size_t size)
{
    const uint8_t* bytes = pointer;
    for (size_t i = 0; i < size; ++i) if (bytes[i] != 0xa5) return 0;
    return 1;
}

static int error_is(CNA_Result result)
{
    CNA_ErrorInfo info = {sizeof(info), 1, 0, 0, 0};
    char message[2048]; uint64_t size = 0;
    memset(message, 0xa5, sizeof(message));
    CHECK(cna_error_get_last_info(&info) == CNA_RESULT_SUCCESS && info.result == result);
    if (result == CNA_RESULT_NOT_SUPPORTED) CHECK(info.category == CNA_ERROR_CATEGORY_NOT_SUPPORTED);
    if (result == CNA_RESULT_INVALID_STATE) CHECK(info.category == CNA_ERROR_CATEGORY_STATE);
    if (result == CNA_RESULT_INVALID_HANDLE) CHECK(info.category == CNA_ERROR_CATEGORY_HANDLE);
    if (result == CNA_RESULT_BUFFER_TOO_SMALL || result == CNA_RESULT_OVERFLOW) CHECK(info.category == CNA_ERROR_CATEGORY_RANGE);
    if (result == CNA_RESULT_INVALID_ARGUMENT) CHECK(info.category == CNA_ERROR_CATEGORY_ARGUMENT || info.category == CNA_ERROR_CATEGORY_RANGE);
    CHECK(cna_error_copy_last_message(message, sizeof(message), &size) == CNA_RESULT_SUCCESS);
    CHECK(size > 0 && size < sizeof(message) && size == info.message_byte_length);
    CHECK(memchr(message, 0, (size_t)size) == NULL && filled(message + size, sizeof(message) - (size_t)size));
    return 0;
}

static CNA_Result open_device(CNA_Handle* device)
{
    CNA_PresentationParameters pp;
    CNA_Result result = cna_presentation_parameters_init(&pp);
    if (result != CNA_RESULT_SUCCESS) return result;
    pp.back_buffer_width = 32; pp.back_buffer_height = 32; pp.is_full_screen = CNA_FALSE;
    return cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_HI_DEF, &pp, device);
}

static int creation_result(CNA_Result result, CNA_Handle handle)
{
    if (result == CNA_RESULT_SUCCESS) CHECK(handle != CNA_INVALID_HANDLE);
    else {
        CHECK(result == CNA_RESULT_NOT_SUPPORTED && handle == CNA_INVALID_HANDLE);
        CHECK(error_is(result) == 0);
    }
    return 0;
}

static int format_contracts(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0}; int status = 1;
    const CNA_SurfaceFormat formats[] = {CNA_SURFACE_FORMAT_COLOR, CNA_SURFACE_FORMAT_BGR565,
        CNA_SURFACE_FORMAT_BGRA5551, CNA_SURFACE_FORMAT_BGRA4444, CNA_SURFACE_FORMAT_RGBA1010102,
        CNA_SURFACE_FORMAT_RG32, CNA_SURFACE_FORMAT_RGBA64, CNA_SURFACE_FORMAT_ALPHA8,
        CNA_SURFACE_FORMAT_SINGLE, CNA_SURFACE_FORMAT_VECTOR2, CNA_SURFACE_FORMAT_VECTOR4,
        CNA_SURFACE_FORMAT_HALF_SINGLE, CNA_SURFACE_FORMAT_HALF_VECTOR2, CNA_SURFACE_FORMAT_HALF_VECTOR4};
    unsigned accepted = 0, refused = 0;
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        CNA_Handle texture = UINT64_MAX;
        CNA_Texture2DCreateInfo t = {sizeof(t), 1, 4, 4, CNA_FALSE, {0}, formats[i]};
        CNA_TextureCubeCreateInfo c = {sizeof(c), 1, 4, CNA_FALSE, {0}, formats[i], 0};
        CNA_Texture3DCreateInfo v = {sizeof(v), 1, 4, 4, 4, CNA_FALSE, {0}, formats[i], 0};
        CNA_RenderTarget2DCreateInfo r = {sizeof(r), 1, 4, 4, CNA_FALSE, {0}, formats[i], CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0};
        CNA_RenderTargetCubeCreateInfo rc = {sizeof(rc), 1, 4, CNA_FALSE, {0}, formats[i], CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS};
        CNA_Result results[5];
        results[0] = SMOKE_OWN(cna_texture2d_create(device, &t, &texture), &texture, cna_texture2d_destroy);
        RUNTIME_CHECK(creation_result(results[0], texture) == 0);
        if (texture) RUNTIME_CHECK(SMOKE_DROP(cna_texture2d_destroy(texture), texture) == CNA_RESULT_SUCCESS);
        texture = UINT64_MAX;
        results[1] = SMOKE_OWN(cna_texturecube_create_format_ext(device, &c, &texture), &texture, cna_texturecube_destroy);
        RUNTIME_CHECK(creation_result(results[1], texture) == 0);
        if (texture) RUNTIME_CHECK(SMOKE_DROP(cna_texturecube_destroy(texture), texture) == CNA_RESULT_SUCCESS);
        texture = UINT64_MAX;
        results[2] = SMOKE_OWN(cna_texture3d_create_format_ext(device, &v, &texture), &texture, cna_texture3d_destroy);
        RUNTIME_CHECK(creation_result(results[2], texture) == 0);
        if (texture) RUNTIME_CHECK(SMOKE_DROP(cna_texture3d_destroy(texture), texture) == CNA_RESULT_SUCCESS);
        texture = UINT64_MAX;
        results[3] = SMOKE_OWN(cna_render_target2d_create(device, &r, &texture), &texture, cna_render_target_destroy);
        RUNTIME_CHECK(creation_result(results[3], texture) == 0);
        if (texture) RUNTIME_CHECK(SMOKE_DROP(cna_render_target_destroy(texture), texture) == CNA_RESULT_SUCCESS);
        texture = UINT64_MAX;
        results[4] = SMOKE_OWN(cna_render_target_cube_create(device, &rc, &texture), &texture, cna_render_target_destroy);
        RUNTIME_CHECK(creation_result(results[4], texture) == 0);
        if (texture) RUNTIME_CHECK(SMOKE_DROP(cna_render_target_destroy(texture), texture) == CNA_RESULT_SUCCESS);
        printf("format=%u: Texture2D=%u Cube=%u Volume=%u Target2D=%u TargetCube=%u\n",
               formats[i], results[0], results[1], results[2], results[3], results[4]);
        for (size_t j = 0; j < 5; ++j) {
            if (results[j] == CNA_RESULT_SUCCESS) ++accepted; else ++refused;
            if (i == 0) RUNTIME_CHECK(results[j] == CNA_RESULT_SUCCESS);
        }
    }
    printf("creation: %u successes, %u checked capability refusals\n", accepted, refused);
    status = 0;
cleanup:
    return cna_smoke_cleanup(&smoke) ? 1 : status;
}

static int texture_windows(CNA_Handle texture, unsigned levels)
{
    uint8_t source[80], output[96]; uint64_t required = UINT64_MAX;
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)(i * 3 + 7);
    CNA_Texture2DTransfer full = {sizeof(full), 1, 0, CNA_FALSE, {0}, {0, 0, 0, 0}, 3, 64};
    CHECK(cna_texture2d_set_data(texture, CNA_TEXTURE_DATA_BYTE, &full, source, sizeof(source)) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, sizeof(output), &required) == CNA_RESULT_SUCCESS);
    CHECK(required == 64 && filled(output, 3) && memcmp(output + 3, source + 3, 64) == 0 && filled(output + 67, sizeof(output) - 67));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, NULL, 0, &required) == CNA_RESULT_BUFFER_TOO_SMALL && required == 64);
    CHECK(error_is(CNA_RESULT_BUFFER_TOO_SMALL) == 0);
    for (unsigned capacity = 65; capacity < 67; ++capacity) {
        memset(output, 0xa5, sizeof(output)); required = UINT64_MAX;
        CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, capacity, &required) == CNA_RESULT_BUFFER_TOO_SMALL);
        CHECK(required == 64 && filled(output, sizeof(output)));
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, 67, &required) == CNA_RESULT_SUCCESS);
    CHECK(required == 64 && memcmp(output + 3, source + 3, 64) == 0 && filled(output, 3) && filled(output + 67, sizeof(output) - 67));
    CNA_Texture2DTransfer rect = {sizeof(rect), 1, 0, CNA_TRUE, {0}, {1, 2, 2, 1}, 1, 8};
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &rect, output, sizeof(output), &required) == CNA_RESULT_SUCCESS);
    CHECK(required == 8 && memcmp(output + 1, source + 3 + (2 * 4 + 1) * 4, 8) == 0 && filled(output, 1) && filled(output + 9, sizeof(output) - 9));
    CHECK(cna_texture2d_set_data(texture, CNA_TEXTURE_DATA_BYTE, &rect, source, sizeof(source)) == CNA_RESULT_SUCCESS);
    uint8_t expected[64]; memcpy(expected, source + 3, sizeof(expected)); memcpy(expected + (2 * 4 + 1) * 4, source + 1, 8);
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, sizeof(output), &required) == CNA_RESULT_SUCCESS);
    CHECK(memcmp(output + 3, expected, sizeof(expected)) == 0);
    for (unsigned mode = 0; mode < 7; ++mode) {
        CNA_Texture2DTransfer bad = full;
        CNA_TextureDataType type = CNA_TEXTURE_DATA_BYTE;
        if (mode == 0) bad.level = (int32_t)levels;
        if (mode == 1) { bad.has_rectangle = CNA_TRUE; bad.rectangle = (CNA_Rectangle){3, 0, 2, 1}; }
        if (mode == 2) { bad.has_rectangle = CNA_TRUE; bad.rectangle = (CNA_Rectangle){-1, 0, 1, 1}; }
        if (mode == 3) bad.element_count = 63;
        if (mode == 4) type = UINT32_MAX;
        if (mode == 5) bad.struct_version = 2;
        if (mode == 6) bad.has_rectangle = 2;
        memset(output, 0xa5, sizeof(output)); required = 77;
        CHECK(cna_texture2d_get_data(texture, type, &bad, output, sizeof(output), &required) == CNA_RESULT_INVALID_ARGUMENT);
        CHECK(filled(output, sizeof(output)) && required == (mode == 3 ? 64U : 77U));
        CHECK(error_is(CNA_RESULT_INVALID_ARGUMENT) == 0);
        CHECK(cna_texture2d_set_data(texture, type, &bad, source, sizeof(source)) == CNA_RESULT_INVALID_ARGUMENT);
    }
    CHECK(cna_texture2d_set_data(texture, CNA_TEXTURE_DATA_BYTE, &full, source, 66) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(cna_texture2d_set_data(texture, CNA_TEXTURE_DATA_BYTE, &full, NULL, sizeof(source)) == CNA_RESULT_INVALID_ARGUMENT);
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, sizeof(output), NULL) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output)));
    CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &full, output, sizeof(output), &required) == CNA_RESULT_SUCCESS);
    CHECK(memcmp(output + 3, expected, sizeof(expected)) == 0);
    if (levels > 1) {
        CNA_Texture2DTransfer mip = {sizeof(mip), 1, 1, CNA_FALSE, {0}, {0, 0, 0, 0}, 2, 16};
        CHECK(cna_texture2d_set_data(texture, CNA_TEXTURE_DATA_BYTE, &mip, source, sizeof(source)) == CNA_RESULT_SUCCESS);
        memset(output, 0xa5, sizeof(output));
        CHECK(cna_texture2d_get_data(texture, CNA_TEXTURE_DATA_BYTE, &mip, output, sizeof(output), &required) == CNA_RESULT_SUCCESS);
        CHECK(required == 16 && memcmp(output + 2, source + 2, 16) == 0 && filled(output, 2) && filled(output + 18, sizeof(output) - 18));
    }
    return 0;
}

static int texture_contracts(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0}; int status = 1;
    CNA_Handle texture = 0, target = 0;
    CNA_Texture2DCreateInfo ci = {sizeof(ci), 1, 4, 4, CNA_TRUE, {0}, CNA_SURFACE_FORMAT_COLOR};
    CNA_RenderTarget2DCreateInfo ri = {sizeof(ri), 1, 4, 4, CNA_FALSE, {0}, CNA_SURFACE_FORMAT_COLOR, CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0};
    RUNTIME_CHECK(SMOKE_OWN(cna_texture2d_create(device, &ci, &texture), &texture, cna_texture2d_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(texture_windows(texture, 3) == 0);
    RUNTIME_CHECK(SMOKE_OWN(cna_render_target2d_create(device, &ri, &target), &target, cna_render_target_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(texture_windows(target, 1) == 0);
    status = 0;
cleanup:
    return cna_smoke_cleanup(&smoke) ? 1 : status;
}

static int backbuffer_contracts(CNA_Handle device)
{
    uint8_t output[64];
    CNA_BackBufferReadback rb = {sizeof(rb), 1, CNA_TRUE, {0}, {2, 3, 2, 1}, 3, 8};
    CHECK(cna_graphics_device_clear_rgba(device, 17.0f / 255, 51.0f / 255, 119.0f / 255, 1) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output));
    CNA_Result support = cna_graphics_device_get_backbuffer_data_raw_ext(device, &rb, 1, output, sizeof(output));
    CHECK(support == CNA_RESULT_SUCCESS || support == CNA_RESULT_NOT_SUPPORTED);
    if (support == CNA_RESULT_SUCCESS) {
        const uint8_t expected[] = {17, 51, 119, 255, 17, 51, 119, 255};
        CHECK(filled(output, 3) && memcmp(output + 3, expected, 8) == 0 && filled(output + 11, sizeof(output) - 11));
    } else {
        CHECK(filled(output, sizeof(output)) && error_is(support) == 0);
        puts("backbuffer pixels: NOT_SUPPORTED (capability refusal checked; pixels not qualified)");
    }
    for (unsigned mode = 0; mode < 10; ++mode) {
        CNA_BackBufferReadback bad = rb; uint64_t width = 1, capacity = sizeof(output);
        CNA_Result expected = CNA_RESULT_INVALID_ARGUMENT;
        if (mode == 0) width = 0;
        if (mode == 1) width = 3;
        if (mode == 2) { bad.source_rectangle.x = 32; }
        if (mode == 3) bad.struct_version = 2;
        if (mode == 4) { capacity = 10; expected = CNA_RESULT_BUFFER_TOO_SMALL; }
        if (mode == 5) { bad.element_count = 7; expected = CNA_RESULT_BUFFER_TOO_SMALL; }
        if (mode == 6) { bad.start_index = UINT64_MAX; expected = CNA_RESULT_OVERFLOW; }
        if (mode == 7) bad.has_source_rectangle = 2;
        if (mode == 8) bad.reserved[0] = 1;
        if (mode == 9) bad.element_count = 10;
        memset(output, 0xa5, sizeof(output));
        CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(device, &bad, width, output, capacity) == expected);
        CHECK(filled(output, sizeof(output)) && error_is(expected) == 0);
    }
    memset(output, 0xa5, sizeof(output));
    CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(device, &rb, 1, NULL, 0) == CNA_RESULT_BUFFER_TOO_SMALL);
    CHECK(cna_graphics_device_get_backbuffer_data_raw_ext(device, &rb, 1, NULL, sizeof(output)) == CNA_RESULT_INVALID_ARGUMENT);
    CHECK(filled(output, sizeof(output)));
    return 0;
}

static int buffer_contracts(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0}; int status = 1;
    CNA_Handle declaration = 0, vertex = 0, index = 0;
    const CNA_VertexElement element = {0, CNA_VERTEX_ELEMENT_FORMAT_VECTOR3, CNA_VERTEX_ELEMENT_USAGE_POSITION, 0};
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_declaration_create_with_stride(12, &element, 1, &declaration), &declaration, cna_vertex_declaration_destroy) == CNA_RESULT_SUCCESS);
    CNA_VertexBufferCreateInfo ci = {sizeof(ci), 1, declaration, 4, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_buffer_create(device, &ci, &vertex), &vertex, cna_vertex_buffer_destroy) == CNA_RESULT_SUCCESS);
    uint8_t source[48], output[64], expected[48];
    for (size_t i = 0; i < sizeof(source); ++i) source[i] = (uint8_t)(i + 1);
    memcpy(expected, source, sizeof(expected)); memcpy(expected + 12, source + 24, 12);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw(vertex, source, sizeof(source), 4, 12)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw_at(vertex, 12, source + 24, 12, 1, 12)) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output));
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_get_data_raw(vertex, 0, output + 1, 48, 4, 12)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(memcmp(output + 1, expected, 48) == 0 && filled(output, 1) && filled(output + 49, sizeof(output) - 49));
    for (unsigned mode = 0; mode < 5; ++mode) {
        uint64_t offset = 0, count = 1, bytes = 12; uint32_t stride = 12;
        if (mode == 0) bytes = 11;
        if (mode == 1) stride = 0;
        if (mode == 2) { offset = 48; count = 1; }
        if (mode == 3) { count = 5; bytes = 60; }
        if (mode == 4) offset = UINT64_MAX;
        memset(output, 0xa5, sizeof(output));
        RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_get_data_raw(vertex, offset, output, bytes, count, stride)) == CNA_RESULT_INVALID_ARGUMENT);
        RUNTIME_CHECK(filled(output, sizeof(output)) && error_is(CNA_RESULT_INVALID_ARGUMENT) == 0);
    }
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw_at(vertex, 1, source, 12, 1, 12)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw_at(vertex, 12, source, 12, 1, 4)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw_at(vertex, 0, source, 11, 1, 12)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_set_data_raw_at(vertex, 0, NULL, 0, 0, 12)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_get_data_raw(vertex, 0, NULL, 0, 0, 12)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_get_data_raw(vertex, 0, output, sizeof(output), 4, 12)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(memcmp(output, expected, sizeof(expected)) == 0);
    CNA_Handle write_only = 0;
    ci.buffer_usage = CNA_BUFFER_USAGE_WRITE_ONLY;
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_buffer_create(device, &ci, &write_only), &write_only, cna_vertex_buffer_destroy) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output));
    RUNTIME_CHECK(SMOKE_API(cna_vertex_buffer_get_data_raw(write_only, 0, output, 12, 1, 12)) == CNA_RESULT_NOT_SUPPORTED);
    RUNTIME_CHECK(filled(output, sizeof(output)) && error_is(CNA_RESULT_NOT_SUPPORTED) == 0);
    CNA_IndexBufferCreateInfo ii = {sizeof(ii), 1, 8, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    RUNTIME_CHECK(SMOKE_OWN(cna_index_buffer_create(device, &ii, &index), &index, cna_index_buffer_destroy) == CNA_RESULT_SUCCESS);
    const uint16_t indices[8] = {0x0102, 0x0304, 0x0506, 0x0708, 0x090a, 0x0b0c, 0x0d0e, 0x0f10};
    CNA_IndexBufferTransfer it = {sizeof(it), 1, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_SET_DATA_NONE, 0, 8};
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data(index, &it, indices, 8)) == CNA_RESULT_SUCCESS);
    memset(output, 0xa5, sizeof(output));
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_get_data_raw_ext(index, 3, 3, 2, 2, output, 10)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(filled(output, 6) && memcmp(output + 6, (const uint8_t*)indices + 3, 6) == 0 && filled(output + 12, sizeof(output) - 12));
    for (unsigned mode = 0; mode < 6; ++mode) {
        uint64_t offset = 0, width = 1, capacity = sizeof(output); int64_t start = 0, count = 4;
        CNA_Result result = CNA_RESULT_INVALID_ARGUMENT;
        if (mode == 0) width = 0;
        if (mode == 1) start = -1;
        if (mode == 2) count = 0;
        if (mode == 3) { offset = 15; result = CNA_RESULT_INVALID_STATE; }
        if (mode == 4) { capacity = 3; result = CNA_RESULT_BUFFER_TOO_SMALL; }
        if (mode == 5) { offset = UINT64_MAX; result = CNA_RESULT_OVERFLOW; }
        memset(output, 0xa5, sizeof(output));
        RUNTIME_CHECK(SMOKE_API(cna_index_buffer_get_data_raw_ext(index, offset, width, start, count, output, capacity)) == result);
        RUNTIME_CHECK(filled(output, sizeof(output)) && error_is(result) == 0);
    }
    it.element_count = 9;
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data(index, &it, indices, 8)) == CNA_RESULT_BUFFER_TOO_SMALL);
    it.element_count = 8; it.options = UINT32_MAX;
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data(index, &it, indices, 8)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_get_data_raw_ext(index, 0, 1, 0, 16, output, sizeof(output))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(memcmp(output, indices, sizeof(indices)) == 0);
    /* The source window and destination buffer byte offset are independent. */
    it.options = CNA_SET_DATA_NONE; it.start_index = 3; it.element_count = 2;
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data_at(index, 4, &it, indices, 8)) == CNA_RESULT_SUCCESS);
    uint16_t expected_indices[8]; memcpy(expected_indices, indices, sizeof(indices));
    memcpy(expected_indices + 2, indices + 3, 2 * sizeof(uint16_t));
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data_at(index, 1, &it, indices, 8)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_set_data_at(index, 14, &it, indices, 8)) == CNA_RESULT_INVALID_STATE);
    /* Refresh IndexBuffer.CopyData IL_007c-0091, FrameworkResources.ResourceDataMustBeCorrectSize;
       the C exception barrier preserves the native message and maps InvalidOperation to STATE. */
    {
        static const char expected[] = "The array is not the correct size for the amount of data requested.";
        char message[sizeof(expected) + 8U];
        uint64_t length = 77;
        memset(message, 0xa5, sizeof(message));
        RUNTIME_CHECK(error_is(CNA_RESULT_INVALID_STATE) == 0);
        RUNTIME_CHECK(cna_error_copy_last_message(message, sizeof(message), &length) == CNA_RESULT_SUCCESS);
        RUNTIME_CHECK(length == sizeof(expected) - 1U && memcmp(message, expected, sizeof(expected) - 1U) == 0);
        RUNTIME_CHECK(filled(message + length, sizeof(message) - (size_t)length));
    }
    RUNTIME_CHECK(SMOKE_API(cna_index_buffer_get_data_raw_ext(index, 0, 1, 0, 16, output, sizeof(output))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(memcmp(output, expected_indices, sizeof(expected_indices)) == 0);
    status = 0;
cleanup:
    return cna_smoke_cleanup(&smoke) ? 1 : status;
}

static int affinity_contracts(CNA_Handle device)
{
    CnaRuntimeSmoke smoke = {0}; int status = 1;
    CNA_Handle other = 0, texture = 0, target = 0, vertex = 0, index = 0;
    RUNTIME_CHECK(SMOKE_OWN(open_device(&other), &other, cna_graphics_device_destroy) == CNA_RESULT_SUCCESS);
    CNA_Texture2DCreateInfo ti = {sizeof(ti), 1, 4, 4, CNA_FALSE, {0}, CNA_SURFACE_FORMAT_COLOR};
    RUNTIME_CHECK(SMOKE_OWN(cna_texture2d_create(device, &ti, &texture), &texture, cna_texture2d_destroy) == CNA_RESULT_SUCCESS);
    CNA_RenderTarget2DCreateInfo ri = {sizeof(ri), 1, 4, 4, CNA_FALSE, {0}, CNA_SURFACE_FORMAT_COLOR, CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_PRESERVE_CONTENTS, 0};
    RUNTIME_CHECK(SMOKE_OWN(cna_render_target2d_create(device, &ri, &target), &target, cna_render_target_destroy) == CNA_RESULT_SUCCESS);
    CNA_VertexBufferCreateInfo vi = {sizeof(vi), 1, 0, 4, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    CNA_IndexBufferCreateInfo ii = {sizeof(ii), 1, 4, CNA_INDEX_ELEMENT_SIZE_SIXTEEN_BITS, CNA_BUFFER_USAGE_NONE, CNA_FALSE, {0}};
    RUNTIME_CHECK(SMOKE_OWN(cna_vertex_buffer_create(device, &vi, &vertex), &vertex, cna_vertex_buffer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_index_buffer_create(device, &ii, &index), &index, cna_index_buffer_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_texture(other, CNA_SHADER_STAGE_PIXEL, 0, texture)) == CNA_RESULT_INVALID_STATE);
    RUNTIME_CHECK(error_is(CNA_RESULT_INVALID_STATE) == 0);
    CNA_TextureSlotInfo slot = {0}; slot.struct_size = sizeof(slot); slot.struct_version = 1;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_texture(other, CNA_SHADER_STAGE_PIXEL, 0, &slot)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(slot.bound == CNA_FALSE && slot.texture == CNA_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_vertex_buffer(other, vertex)) == CNA_RESULT_INVALID_STATE);
    RUNTIME_CHECK(error_is(CNA_RESULT_INVALID_STATE) == 0);
    CNA_Handle bound = UINT64_MAX;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_vertex_buffer(other, &bound)) == CNA_RESULT_SUCCESS && bound == CNA_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_index_buffer(other, index)) == CNA_RESULT_INVALID_STATE);
    RUNTIME_CHECK(error_is(CNA_RESULT_INVALID_STATE) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_index_buffer(other, &bound)) == CNA_RESULT_SUCCESS && bound == CNA_INVALID_HANDLE);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_render_target2d(other, target)) == CNA_RESULT_INVALID_HANDLE);
    RUNTIME_CHECK(error_is(CNA_RESULT_INVALID_HANDLE) == 0);
    uint64_t count = UINT64_MAX;
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_get_render_target_count(other, &count)) == CNA_RESULT_SUCCESS && count == 0);
    /* Each refused object remains usable through its creating device. */
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_texture(device, CNA_SHADER_STAGE_PIXEL, 0, texture)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_texture(device, CNA_SHADER_STAGE_PIXEL, 0, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_vertex_buffer(device, vertex)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_vertex_buffer(device, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_index_buffer(device, index)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_index_buffer(device, 0)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_render_target2d(device, target)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_graphics_device_set_render_target2d(device, 0)) == CNA_RESULT_SUCCESS);
    status = 0;
cleanup:
    (void)cna_graphics_device_set_texture(device, CNA_SHADER_STAGE_PIXEL, 0, 0);
    (void)cna_graphics_device_set_vertex_buffer(device, 0);
    (void)cna_graphics_device_set_index_buffer(device, 0);
    (void)cna_graphics_device_set_render_target2d(device, 0);
    if (other) {
        (void)cna_graphics_device_set_texture(other, CNA_SHADER_STAGE_PIXEL, 0, 0);
        (void)cna_graphics_device_set_vertex_buffer(other, 0);
        (void)cna_graphics_device_set_index_buffer(other, 0);
        (void)cna_graphics_device_set_render_target2d(other, 0);
    }
    return cna_smoke_cleanup(&smoke) ? 1 : status;
}

static int cpu_contracts(void)
{
    CHECK(cna_test_texture_format_capability_exception());
    CNA_Color pixels[16] = {{0}}; CNA_Handle texture = 0;
    CHECK(cna_texture2d_create_cpu_only_rgba8(4, 4, CNA_SURFACE_FORMAT_COLOR, pixels, 16, &texture) == CNA_RESULT_SUCCESS);
    int status = texture_windows(texture, 1);
    CHECK(cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS);
    return status;
}

int main(int argc, char** argv)
{
    if (argc != 2) return CNA_TEST_FAIL(1);
    if (strcmp(argv[1], "cpu") == 0) return cpu_contracts();
    CnaRuntimeSmoke smoke = {0}; int status = 1; CNA_Handle device = 0;
    RUNTIME_CHECK(SMOKE_OWN(open_device(&device), &device, cna_graphics_device_destroy) == CNA_RESULT_SUCCESS);
    if (strcmp(argv[1], "formats") == 0) status = format_contracts(device);
    if (strcmp(argv[1], "textures") == 0) status = texture_contracts(device);
    if (strcmp(argv[1], "backbuffer") == 0) status = backbuffer_contracts(device);
    if (strcmp(argv[1], "buffers") == 0) status = buffer_contracts(device);
    if (strcmp(argv[1], "affinity") == 0) status = affinity_contracts(device);
cleanup:
    if (cna_smoke_cleanup(&smoke)) status = 1;
    if (status == 0) printf("DeviceOutputContracts/%s: PASS\n", argv[1]);
    return status;
}
