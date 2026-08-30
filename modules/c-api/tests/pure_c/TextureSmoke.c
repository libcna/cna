// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static const char PngPath[] = "cna_c_api_texture_smoke.png";
static const char JpegPath[] = "cna_c_api_texture_smoke.jpg";

typedef struct WrongThreadState {
    CNA_Handle texture;
    CNA_Result result;
} WrongThreadState;

typedef struct CallbackState {
    const uint8_t* png;
    uint64_t png_size;
    CNA_Handle borrowed_device;
    int validated;
    int stage;
} CallbackState;

static int colors_equal(const CNA_Color* const left, const CNA_Color* const right)
{
    return memcmp(left, right, sizeof(CNA_Color)) == 0;
}

static CNA_Texture2DTransfer make_transfer(
    const int32_t level,
    const uint64_t start_index,
    const uint64_t element_count)
{
    const CNA_Texture2DTransfer transfer = {
        sizeof(CNA_Texture2DTransfer),
        UINT32_C(1),
        level,
        CNA_FALSE,
        {0U, 0U, 0U},
        {0, 0, 0, 0},
        start_index,
        element_count
    };
    return transfer;
}

static int validate_texture_helpers(void)
{
    static const int32_t BlockSizes[27] = {
        1, 1, 1, 1, 16, 16, 16, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 16, 16, 16, 1, 1
    };
    static const int32_t FormatSizes[27] = {
        4, 2, 2, 2, 8, 16, 16, 2, 4, 4, 4, 8, 1, 4,
        8, 16, 2, 4, 8, 8, 4, 4, 16, 16, 16, 1, 2
    };
    for (uint32_t format = CNA_SURFACE_FORMAT_COLOR;
         format <= CNA_SURFACE_FORMAT_USHORT_EXT;
         ++format) {
        int32_t block_size = -1;
        int32_t format_size = -1;
        int32_t alignment = -1;
        const int32_t expected_alignment =
            FormatSizes[format] < 8 ? FormatSizes[format] : 8;
        if (cna_texture_get_block_size_squared(format, &block_size) != CNA_RESULT_SUCCESS ||
            block_size != BlockSizes[format] ||
            cna_texture_get_format_size(format, &format_size) != CNA_RESULT_SUCCESS ||
            format_size != FormatSizes[format] ||
            cna_texture_get_pixel_store_alignment(format, &alignment) != CNA_RESULT_SUCCESS ||
            alignment != expected_alignment ||
            cna_texture_validate_get_data_format(format, 1) != CNA_RESULT_SUCCESS ||
            cna_texture_validate_get_data_format(format, FormatSizes[format] + 1) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        const CNA_Result validate_result = cna_texture_validate_format(format);
        if ((format == CNA_SURFACE_FORMAT_COLOR && validate_result != CNA_RESULT_SUCCESS) ||
            (format != CNA_SURFACE_FORMAT_COLOR &&
             validate_result != CNA_RESULT_NOT_SUPPORTED)) {
            return 0;
        }
    }

    int32_t untouched = 77;
    if (cna_texture_get_block_size_squared(UINT32_MAX, &untouched) !=
            CNA_RESULT_INVALID_ARGUMENT || untouched != 77 ||
        cna_texture_get_format_size(CNA_SURFACE_FORMAT_COLOR, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture_get_pixel_store_alignment(UINT32_MAX, &untouched) !=
            CNA_RESULT_INVALID_ARGUMENT || untouched != 77 ||
        cna_texture_validate_get_data_format(CNA_SURFACE_FORMAT_COLOR, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture_validate_format(UINT32_MAX) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int use_texture_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_TextureInfo info = {sizeof(CNA_TextureInfo), UINT32_C(1), 0U, 0U};
    state->result = cna_texture_get_info(state->texture, &info);
    return 0;
}

static int validate_default_texture(void)
{
    static const char TypeName[] = "Texture2D";
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_TextureInfo info = {sizeof(CNA_TextureInfo), UINT32_C(1), UINT32_MAX, UINT32_MAX};
    CNA_Texture2DInfo info_2d = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), UINT32_MAX, UINT32_MAX,
        UINT32_MAX, UINT32_MAX
    };
    CNA_Texture2DStorageInfo storage = {
        sizeof(CNA_Texture2DStorageInfo), UINT32_C(1), CNA_TRUE, CNA_TRUE,
        {1U, 1U, 1U, 1U, 1U, 1U}
    };
    uint64_t count = UINT64_MAX;
    char name[sizeof(TypeName) - 1U];
    char too_small = 'x';
    CNA_Bool disposed = CNA_TRUE;

    if (cna_texture2d_create_standalone(&texture) != CNA_RESULT_SUCCESS ||
        texture == CNA_INVALID_HANDLE ||
        cna_texture_get_info(texture, &info) != CNA_RESULT_SUCCESS ||
        info.level_count != 1U || info.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_texture2d_get_info(texture, &info_2d) != CNA_RESULT_SUCCESS ||
        info_2d.width != 0U || info_2d.height != 0U || info_2d.level_count != 1U ||
        info_2d.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_texture2d_get_storage_info(texture, &storage) != CNA_RESULT_SUCCESS ||
        storage.has_renderer != CNA_FALSE || storage.has_cpu_shadow != CNA_FALSE ||
        memcmp(storage.reserved, (uint8_t[6]){0U}, sizeof(storage.reserved)) != 0 ||
        cna_texture2d_get_type_name_byte_count(texture, &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(TypeName) - 1U ||
        cna_texture2d_copy_type_name(texture, &too_small, 1U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'x' ||
        count != sizeof(TypeName) - 1U ||
        cna_texture2d_copy_type_name(texture, name, sizeof(name), &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(name) ||
        memcmp(name, TypeName, sizeof(name)) != 0 ||
        cna_graphics_resource_get_is_disposed(texture, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE ||
        cna_graphics_resource_dispose(texture) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_is_disposed(texture, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_TRUE ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS ||
        cna_texture_get_info(texture, &info) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

static int validate_transfer_failures(const CNA_Handle texture)
{
    CNA_Texture2DTransfer transfer = make_transfer(0, 1U, 16U);
    uint8_t raw[17U * 16U];
    uint8_t destination[17U * 16U];
    uint64_t required = UINT64_C(999);
    memset(raw, 0, sizeof(raw));
    memset(destination, 0x5a, sizeof(destination));

    for (CNA_TextureDataType type = CNA_TEXTURE_DATA_BGR565;
         type <= CNA_TEXTURE_DATA_USHORT;
         ++type) {
        uint8_t before[sizeof(destination)];
        memcpy(before, destination, sizeof(before));
        required = UINT64_C(999);
        if (cna_texture2d_set_data(texture, type, &transfer, raw, 17U) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_texture2d_get_data(
                texture, type, &transfer, destination, 17U, &required) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            required != 16U || memcmp(destination, before, sizeof(before)) != 0) {
            return 0;
        }
    }

    required = UINT64_C(999);
    if (cna_texture2d_set_data(texture, UINT32_MAX, &transfer, raw, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_get_data(
            texture, UINT32_MAX, &transfer, destination, 17U, &required) !=
            CNA_RESULT_INVALID_ARGUMENT || required != UINT64_C(999) ||
        cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer, destination, 17U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer, 0, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Texture2DTransfer invalid = transfer;
    invalid.struct_size = sizeof(CNA_Texture2DTransfer) - 1U;
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = transfer;
    invalid.struct_version = 2U;
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = transfer;
    invalid.reserved[1] = 1U;
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = transfer;
    invalid.level = 1;
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = transfer;
    invalid.has_rectangle = CNA_TRUE;
    invalid.rectangle = (CNA_Rectangle){3, 3, 2, 2};
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = transfer;
    invalid.element_count = 15U;
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &invalid, raw, 17U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_Color color_destination[17];
    memset(color_destination, 0x33, sizeof(color_destination));
    CNA_Color before[17];
    memcpy(before, color_destination, sizeof(before));
    required = 0U;
    if (cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer,
            color_destination, 16U, &required) != CNA_RESULT_BUFFER_TOO_SMALL ||
        required != 16U || memcmp(color_destination, before, sizeof(before)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_cpu_texture_and_encoding(uint8_t** const out_png, uint64_t* const out_size)
{
    CNA_Color pixels[16];
    CNA_Color replacement[17];
    CNA_Color readback[17];
    for (uint32_t index = 0U; index < 16U; ++index) {
        pixels[index] = (CNA_Color){
            (uint8_t)(index * 11U),
            (uint8_t)(255U - index * 7U),
            (uint8_t)(index * 3U),
            UINT8_C(255)
        };
        replacement[index + 1U] = (CNA_Color){
            (uint8_t)(200U - index),
            (uint8_t)(20U + index),
            (uint8_t)(80U + index),
            UINT8_C(255)
        };
    }
    replacement[0] = (CNA_Color){1U, 2U, 3U, 4U};
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Texture2DTransfer full = make_transfer(0, 1U, 16U);
    uint64_t required = 0U;
    memset(readback, 0x6b, sizeof(readback));
    const CNA_Color untouched = readback[0];

    if (cna_texture2d_create_cpu_only_rgba8(
            4U, 4U, CNA_SURFACE_FORMAT_COLOR, pixels, 16U, &texture) !=
            CNA_RESULT_SUCCESS || texture == CNA_INVALID_HANDLE ||
        cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &full, readback, 17U, &required) !=
            CNA_RESULT_SUCCESS || required != 16U ||
        !colors_equal(&readback[0], &untouched) ||
        memcmp(&readback[1], pixels, sizeof(pixels)) != 0 ||
        cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &full, replacement, 17U) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    CNA_Texture2DTransfer rectangle = make_transfer(0, 1U, 6U);
    rectangle.has_rectangle = CNA_TRUE;
    rectangle.rectangle = (CNA_Rectangle){1, 1, 2, 2};
    CNA_Color patch[7] = {
        {9U, 9U, 9U, 9U},
        {255U, 0U, 0U, 255U},
        {0U, 255U, 0U, 255U},
        {0U, 0U, 255U, 255U},
        {255U, 255U, 255U, 255U},
        {8U, 8U, 8U, 8U},
        {7U, 7U, 7U, 7U}
    };
    CNA_Color patch_readback[7];
    memset(patch_readback, 0x44, sizeof(patch_readback));
    const CNA_Color patch_untouched = patch_readback[0];
    const CNA_Color tail_untouched = patch_readback[5];
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &rectangle, patch, 7U) !=
            CNA_RESULT_SUCCESS ||
        cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &rectangle,
            patch_readback, 7U, &required) != CNA_RESULT_SUCCESS ||
        required != 4U || !colors_equal(&patch_readback[0], &patch_untouched) ||
        memcmp(&patch_readback[1], &patch[1], 4U * sizeof(CNA_Color)) != 0 ||
        !colors_equal(&patch_readback[5], &tail_untouched) ||
        !colors_equal(&patch_readback[6], &tail_untouched) ||
        !validate_transfer_failures(texture)) {
        return 0;
    }

    CNA_Texture2DStorageInfo storage = {
        sizeof(CNA_Texture2DStorageInfo), UINT32_C(1), CNA_TRUE, CNA_FALSE,
        {0U, 0U, 0U, 0U, 0U, 0U}
    };
    WrongThreadState wrong_thread = {texture, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (cna_texture2d_get_storage_info(texture, &storage) != CNA_RESULT_SUCCESS ||
        storage.has_renderer != CNA_FALSE || storage.has_cpu_shadow != CNA_TRUE ||
        thrd_create(&thread, use_texture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.result != CNA_RESULT_THREAD) {
        return 0;
    }

    uint64_t png_size = 0U;
    uint64_t jpeg_size = 0U;
    if (cna_texture2d_get_encoded_byte_count(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG, 4U, 4U, &png_size) !=
            CNA_RESULT_SUCCESS || png_size < 8U ||
        cna_texture2d_get_encoded_byte_count(
            texture, CNA_TEXTURE_IMAGE_FORMAT_JPEG, 4U, 4U, &jpeg_size) !=
            CNA_RESULT_SUCCESS || jpeg_size < 2U) {
        return 0;
    }
    uint8_t* const png = (uint8_t*)malloc((size_t)png_size);
    uint8_t* const jpeg = (uint8_t*)malloc((size_t)jpeg_size);
    if (png == 0 || jpeg == 0) {
        free(png);
        free(jpeg);
        return 0;
    }
    memset(png, 0x35, (size_t)png_size);
    uint64_t copied_size = 0U;
    if (cna_texture2d_copy_encoded(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG, 4U, 4U,
            png, png_size - 1U, &copied_size) != CNA_RESULT_BUFFER_TOO_SMALL ||
        copied_size != png_size || png[0] != UINT8_C(0x35) ||
        cna_texture2d_copy_encoded(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG, 4U, 4U,
            png, png_size, &copied_size) != CNA_RESULT_SUCCESS ||
        copied_size != png_size ||
        memcmp(png, (uint8_t[8]){0x89U, 'P', 'N', 'G', 0x0dU, 0x0aU, 0x1aU, 0x0aU}, 8U) != 0 ||
        cna_texture2d_copy_encoded(
            texture, CNA_TEXTURE_IMAGE_FORMAT_JPEG, 4U, 4U,
            jpeg, jpeg_size, &copied_size) != CNA_RESULT_SUCCESS ||
        copied_size != jpeg_size || jpeg[0] != UINT8_C(0xff) || jpeg[1] != UINT8_C(0xd8) ||
        cna_texture2d_get_encoded_byte_count(
            texture, UINT32_MAX, 4U, 4U, &copied_size) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_get_encoded_byte_count(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG, 0U, 4U, &copied_size) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_copy_encoded(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG, 4U, 4U, 0, 1U, &copied_size) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        free(png);
        free(jpeg);
        return 0;
    }

    (void)remove(PngPath);
    (void)remove(JpegPath);
    if (cna_texture2d_save_file(
            texture, CNA_TEXTURE_IMAGE_FORMAT_PNG,
            (CNA_StringView){PngPath, sizeof(PngPath) - 1U}) != CNA_RESULT_SUCCESS ||
        cna_texture2d_save_file(
            texture, CNA_TEXTURE_IMAGE_FORMAT_JPEG,
            (CNA_StringView){JpegPath, sizeof(JpegPath) - 1U}) != CNA_RESULT_SUCCESS ||
        cna_texture2d_save_file(
            texture, UINT32_MAX,
            (CNA_StringView){JpegPath, sizeof(JpegPath) - 1U}) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        free(png);
        free(jpeg);
        return 0;
    }

    CNA_Handle file_texture = CNA_INVALID_HANDLE;
    CNA_Texture2DInfo file_info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    if (cna_texture2d_create_from_file(
            (CNA_StringView){PngPath, sizeof(PngPath) - 1U}, &file_texture) !=
            CNA_RESULT_SUCCESS || file_texture == CNA_INVALID_HANDLE ||
        cna_texture2d_get_info(file_texture, &file_info) != CNA_RESULT_SUCCESS ||
        file_info.width != 4U || file_info.height != 4U ||
        cna_texture2d_destroy(file_texture) != CNA_RESULT_SUCCESS ||
        cna_texture2d_create_from_file((CNA_StringView){0, 1U}, &file_texture) !=
            CNA_RESULT_INVALID_ARGUMENT || file_texture != CNA_INVALID_HANDLE ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS) {
        free(png);
        free(jpeg);
        return 0;
    }

    free(jpeg);
    *out_png = png;
    *out_size = png_size;
    return 1;
}

/*
 * Probes whether this backend accepts a mip-level upload above level zero. The probe writes the
 * same window the transfer test writes, so a supporting backend loses nothing by being asked.
 */
static int supports_mip_upload(const CNA_Handle texture)
{
    CNA_Texture2DTransfer transfer = make_transfer(1, 0U, 4U);
    const CNA_Color pixels[4] = {
        {1U, 2U, 3U, 255U}, {4U, 5U, 6U, 255U},
        {7U, 8U, 9U, 255U}, {10U, 11U, 12U, 255U}
    };
    return cna_texture2d_set_data(
               texture, CNA_TEXTURE_DATA_COLOR, &transfer, pixels, 4U) == CNA_RESULT_SUCCESS;
}

static int validate_mip_transfer(const CNA_Handle texture)
{
    CNA_TextureInfo info = {sizeof(CNA_TextureInfo), UINT32_C(1), 0U, 0U};
    CNA_Texture2DTransfer transfer = make_transfer(1, 0U, 4U);
    const CNA_Color pixels[4] = {
        {1U, 2U, 3U, 255U}, {4U, 5U, 6U, 255U},
        {7U, 8U, 9U, 255U}, {10U, 11U, 12U, 255U}
    };
    CNA_Color readback[4] = {{0U, 0U, 0U, 0U}};
    uint64_t required = 0U;
    const CNA_Result info_result = cna_texture_get_info(texture, &info);
    const CNA_Result set_result = cna_texture2d_set_data(
        texture, CNA_TEXTURE_DATA_COLOR, &transfer, pixels, 4U);
    const CNA_Result get_result = set_result == CNA_RESULT_SUCCESS
        ? cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer, readback, 4U, &required)
        : CNA_RESULT_INVALID_STATE;
    if (info_result != CNA_RESULT_SUCCESS ||
        info.level_count != 3U || info.format != CNA_SURFACE_FORMAT_COLOR ||
        set_result != CNA_RESULT_SUCCESS || get_result != CNA_RESULT_SUCCESS ||
        required != 4U || memcmp(readback, pixels, sizeof(pixels)) != 0) {
        (void)fprintf(
            stderr,
            "Mip transfer failed: info=%u levels=%u set=%u get=%u required=%llu\n",
            info_result,
            info.level_count,
            set_result,
            get_result,
            (unsigned long long)required);
        return 0;
    }
    return 1;
}

static int validate_level_zero_transfer(const CNA_Handle texture)
{
    CNA_TextureInfo info = {sizeof(CNA_TextureInfo), UINT32_C(1), 0U, 0U};
    CNA_Texture2DTransfer transfer = make_transfer(0, 0U, 16U);
    CNA_Color pixels[16];
    CNA_Color readback[16];
    for (uint32_t index = 0U; index < 16U; ++index) {
        pixels[index] = (CNA_Color){
            (uint8_t)(index + 1U),
            (uint8_t)(index + 2U),
            (uint8_t)(index + 3U),
            UINT8_C(255)
        };
    }
    memset(readback, 0, sizeof(readback));
    uint64_t required = 0U;
    if (cna_texture_get_info(texture, &info) != CNA_RESULT_SUCCESS ||
        info.level_count != 3U || info.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer, pixels, 16U) !=
            CNA_RESULT_SUCCESS ||
        cna_texture2d_get_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer,
            readback, 16U, &required) != CNA_RESULT_SUCCESS ||
        required != 16U || memcmp(readback, pixels, sizeof(pixels)) != 0) {
        return 0;
    }
    return 1;
}

static int validate_unsupported_mip_transfer(const CNA_Handle texture)
{
    CNA_Texture2DTransfer transfer = make_transfer(1, 0U, 4U);
    const CNA_Color pixels[4] = {
        {1U, 2U, 3U, 255U}, {4U, 5U, 6U, 255U},
        {7U, 8U, 9U, 255U}, {10U, 11U, 12U, 255U}
    };
    if (cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_COLOR, &transfer, pixels, 4U) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_texture2d_set_data(
            texture, CNA_TEXTURE_DATA_BGR565, &transfer, pixels, 4U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        !validate_level_zero_transfer(texture)) {
        return 0;
    }
    return 1;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CallbackState* const state = (CallbackState*)context;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 1;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_RendererInfo renderer = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    /*
     * An enumerated renderer identity is not a support claim, so this suite never allowlists one:
     * it probes each backend's actual behavior and asserts the matching contract instead.
     */
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_renderer_info(device, &renderer) != CNA_RESULT_SUCCESS ||
        renderer.renderer_type == CNA_GRAPHICS_RENDERER_UNKNOWN) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 4U, 4U,
        CNA_TRUE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR
    };
    CNA_Handle mip_texture = CNA_INVALID_HANDLE;
    state->stage = 2;
    const CNA_Result mip_create_result =
        cna_texture2d_create(device, &create_info, &mip_texture);
    /* Mip-level upload above level zero is a backend limitation, not a renderer identity. */
    const int transfer_valid = supports_mip_upload(mip_texture)
        ? validate_mip_transfer(mip_texture)
        : validate_unsupported_mip_transfer(mip_texture);
    if (mip_create_result != CNA_RESULT_SUCCESS || !transfer_valid) {
        (void)fprintf(stderr, "Mip texture create result: %u\n", mip_create_result);
        return CNA_RESULT_INVALID_STATE;
    }

    state->stage = 3;
    for (CNA_SurfaceFormat format = CNA_SURFACE_FORMAT_BGR565;
         format <= CNA_SURFACE_FORMAT_USHORT_EXT;
         ++format) {
        CNA_Texture2DCreateInfo candidate = create_info;
        CNA_RendererFormatUsageFlags known = 0U;
        CNA_RendererFormatUsageFlags supported = 0U;
        CNA_Result result = CNA_RESULT_INTERNAL;
        candidate.format = format;
        candidate.mip_map = CNA_FALSE;
        CNA_Handle output = UINT64_MAX;
        if (cna_graphics_device_get_surface_format_support_ext(
                device, format, &known, &supported) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        result = cna_texture2d_create(device, &candidate, &output);
        if (result == CNA_RESULT_SUCCESS) {
            if (output == CNA_INVALID_HANDLE ||
                ((known & CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE) != 0U &&
                 (supported & CNA_RENDERER_FORMAT_USAGE_TEXTURE_STORAGE) == 0U) ||
                cna_texture2d_destroy(output) != CNA_RESULT_SUCCESS) {
                return CNA_RESULT_INVALID_STATE;
            }
        } else if (result != CNA_RESULT_NOT_SUPPORTED || output != CNA_INVALID_HANDLE) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    const CNA_Color rgba[16] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U},
        {0U, 0U, 255U, 255U}, {255U, 255U, 255U, 255U}
    };
    CNA_Handle rgba_texture = CNA_INVALID_HANDLE;
    state->stage = 4;
    if (cna_texture2d_create_from_rgba8(device, 4U, 4U, rgba, 15U, &rgba_texture) !=
            CNA_RESULT_INVALID_ARGUMENT || rgba_texture != CNA_INVALID_HANDLE ||
        cna_texture2d_create_from_rgba8(device, 4U, 4U, rgba, 16U, &rgba_texture) !=
            CNA_RESULT_SUCCESS || rgba_texture == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    uint8_t raw_rgba[64];
    for (uint32_t index = 0U; index < 16U; ++index) {
        raw_rgba[index * 4U] = (uint8_t)(30U + index);
        raw_rgba[index * 4U + 1U] = (uint8_t)(60U + index);
        raw_rgba[index * 4U + 2U] = (uint8_t)(90U + index);
        raw_rgba[index * 4U + 3U] = UINT8_C(255);
    }
    if (cna_texture2d_set_data_rgba8_bytes(rgba_texture, raw_rgba, 15U) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_set_data_rgba8_bytes(rgba_texture, raw_rgba, 16U) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Color raw_readback[16];
    CNA_Texture2DTransfer raw_transfer = make_transfer(0, 0U, 16U);
    uint64_t raw_required = 0U;
    if (cna_texture2d_get_data(
            rgba_texture, CNA_TEXTURE_DATA_COLOR, &raw_transfer,
            raw_readback, 16U, &raw_required) != CNA_RESULT_SUCCESS ||
        raw_required != 16U || memcmp(raw_readback, raw_rgba, sizeof(raw_rgba)) != 0) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Texture2DStorageInfo storage = {
        sizeof(CNA_Texture2DStorageInfo), UINT32_C(1), CNA_FALSE, CNA_FALSE,
        {0U, 0U, 0U, 0U, 0U, 0U}
    };
    state->stage = 5;
    if (cna_texture2d_get_storage_info(rgba_texture, &storage) != CNA_RESULT_SUCCESS ||
        storage.has_renderer != CNA_TRUE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle file_texture = CNA_INVALID_HANDLE;
    state->stage = 6;
    if (cna_texture2d_create_from_file_with_device(
            device, (CNA_StringView){PngPath, sizeof(PngPath) - 1U}, &file_texture) !=
            CNA_RESULT_SUCCESS || file_texture == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle decoded = CNA_INVALID_HANDLE;
    CNA_Handle fitted = CNA_INVALID_HANDLE;
    CNA_Handle zoomed = CNA_INVALID_HANDLE;
    state->stage = 7;
    if (cna_texture2d_create_from_encoded_memory(
            device, state->png, state->png_size, 0, &decoded) != CNA_RESULT_SUCCESS ||
        decoded == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Texture2DDecodeInfo decode_info = {
        sizeof(CNA_Texture2DDecodeInfo), UINT32_C(1), 2U, 3U, CNA_FALSE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}
    };
    state->stage = 8;
    if (cna_texture2d_create_from_encoded_memory(
            device, state->png, state->png_size, &decode_info, &fitted) !=
            CNA_RESULT_SUCCESS || fitted == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    decode_info.zoom = CNA_TRUE;
    state->stage = 9;
    if (cna_texture2d_create_from_encoded_memory(
            device, state->png, state->png_size, &decode_info, &zoomed) !=
            CNA_RESULT_SUCCESS || zoomed == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Texture2DInfo decoded_info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    CNA_Texture2DInfo fitted_info = decoded_info;
    CNA_Texture2DInfo zoomed_info = decoded_info;
    state->stage = 10;
    if (cna_texture2d_get_info(decoded, &decoded_info) != CNA_RESULT_SUCCESS ||
        decoded_info.width != 4U || decoded_info.height != 4U ||
        cna_texture2d_get_info(fitted, &fitted_info) != CNA_RESULT_SUCCESS ||
        fitted_info.width != 3U || fitted_info.height != 3U ||
        cna_texture2d_get_info(zoomed, &zoomed_info) != CNA_RESULT_SUCCESS ||
        zoomed_info.width != 2U || zoomed_info.height != 3U) {
        return CNA_RESULT_INVALID_STATE;
    }

    decode_info.reserved[3] = 1U;
    CNA_Handle invalid_decode = UINT64_MAX;
    state->stage = 11;
    if (cna_texture2d_create_from_encoded_memory(
            device, state->png, state->png_size, &decode_info, &invalid_decode) !=
            CNA_RESULT_INVALID_ARGUMENT || invalid_decode != CNA_INVALID_HANDLE ||
        cna_texture2d_create_from_encoded_memory(
            device, 0, state->png_size, 0, &invalid_decode) !=
            CNA_RESULT_INVALID_ARGUMENT || invalid_decode != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    state->stage = 12;
    if (cna_texture2d_destroy(zoomed) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(fitted) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(decoded) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(file_texture) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(rgba_texture) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(mip_texture) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->borrowed_device = device;
    state->validated = 1;
    state->stage = 13;
    return CNA_RESULT_SUCCESS;
}

static int validate_device_texture(const uint8_t* const png, const uint64_t png_size)
{
    CallbackState state = {png, png_size, CNA_INVALID_HANDLE, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char Title[] = "C API texture";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_RendererInfo stale = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.validated != 1 ||
        state.borrowed_device == CNA_INVALID_HANDLE ||
        cna_graphics_device_get_renderer_info(state.borrowed_device, &stale) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        (void)fprintf(stderr, "Texture callback failed at stage %d\n", state.stage);
        return 0;
    }
    return 1;
}

static int validate_wrong_kind_and_arguments(void)
{
    CNA_CurveHandle curve = CNA_INVALID_HANDLE;
    CNA_TextureInfo info = {
        sizeof(CNA_TextureInfo), UINT32_C(1), UINT32_C(77), UINT32_C(77)
    };
    CNA_Texture2DStorageInfo storage = {
        sizeof(CNA_Texture2DStorageInfo), UINT32_C(1), CNA_TRUE, CNA_TRUE,
        {1U, 1U, 1U, 1U, 1U, 1U}
    };
    uint64_t count = UINT64_C(77);
    CNA_Handle output = UINT64_MAX;
    char byte = 'x';
    if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS ||
        cna_texture_get_info(curve, &info) != CNA_RESULT_INVALID_HANDLE ||
        info.level_count != 77U ||
        cna_texture2d_get_storage_info(curve, &storage) != CNA_RESULT_INVALID_HANDLE ||
        storage.has_renderer != CNA_TRUE ||
        cna_texture2d_get_type_name_byte_count(curve, &count) !=
            CNA_RESULT_INVALID_HANDLE || count != 77U ||
        cna_texture2d_copy_type_name(curve, &byte, 1U, &count) !=
            CNA_RESULT_INVALID_HANDLE || byte != 'x' || count != 77U ||
        cna_texture2d_create_standalone(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_create_cpu_only_rgba8(
            0U, 1U, CNA_SURFACE_FORMAT_COLOR, 0, 0U, &output) !=
            CNA_RESULT_INVALID_ARGUMENT || output != CNA_INVALID_HANDLE ||
        cna_texture2d_create_from_file(
            (CNA_StringView){PngPath, sizeof(PngPath) - 1U}, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_get_type_name_byte_count(curve, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_curve_destroy(curve) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

int main(void)
{
    uint8_t* png = 0;
    uint64_t png_size = 0U;
    if (!validate_texture_helpers()) {
        return 1;
    }
    if (!validate_default_texture()) {
        return 2;
    }
    if (!validate_cpu_texture_and_encoding(&png, &png_size)) {
        return 3;
    }
    if (!validate_device_texture(png, png_size)) {
        free(png);
        return 4;
    }
    free(png);
    if (!validate_wrong_kind_and_arguments()) {
        return 5;
    }
    (void)remove(PngPath);
    (void)remove(JpegPath);
    return 0;
}
