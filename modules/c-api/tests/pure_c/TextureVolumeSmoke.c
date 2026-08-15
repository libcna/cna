// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>
#include <threads.h>

typedef struct CallbackState {
    CNA_Handle borrowed_device;
    int stage;
    int validated;
} CallbackState;

typedef struct WrongThreadState {
    CNA_Handle texture;
    CNA_Result result;
} WrongThreadState;

static CNA_TextureCubeTransfer make_cube_transfer(
    const CNA_CubeMapFace face,
    const int32_t level,
    const uint64_t start_index,
    const uint64_t element_count)
{
    const CNA_TextureCubeTransfer transfer = {
        sizeof(CNA_TextureCubeTransfer), UINT32_C(1), face, level,
        CNA_FALSE, {0U, 0U, 0U}, {0, 0, 0, 0}, 0U,
        start_index, element_count
    };
    return transfer;
}

static void push_u32(uint8_t* const data, size_t* const offset, const uint32_t value)
{
    data[(*offset)++] = (uint8_t)(value & UINT32_C(0xff));
    data[(*offset)++] = (uint8_t)((value >> 8U) & UINT32_C(0xff));
    data[(*offset)++] = (uint8_t)((value >> 16U) & UINT32_C(0xff));
    data[(*offset)++] = (uint8_t)((value >> 24U) & UINT32_C(0xff));
}

static void push_tag(uint8_t* const data, size_t* const offset, const char tag[4])
{
    for (size_t index = 0U; index < 4U; ++index) {
        data[(*offset)++] = (uint8_t)tag[index];
    }
}

static size_t build_minimal_cube_dds(uint8_t data[176])
{
    size_t offset = 0U;
    push_tag(data, &offset, "DDS ");
    push_u32(data, &offset, 124U);
    push_u32(data, &offset, UINT32_C(0x6));
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 4U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 1U);
    for (int index = 0; index < 11; ++index) {
        push_u32(data, &offset, 0U);
    }
    push_u32(data, &offset, 32U);
    push_u32(data, &offset, UINT32_C(0x4));
    push_tag(data, &offset, "DXT1");
    for (int index = 0; index < 5; ++index) {
        push_u32(data, &offset, 0U);
    }
    push_u32(data, &offset, UINT32_C(0x1000));
    push_u32(data, &offset, UINT32_C(0xfe00));
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    push_u32(data, &offset, 0U);
    memset(data + offset, 0, 48U);
    return offset + 48U;
}

static int use_cube_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_TextureCubeInfo info = {
        sizeof(CNA_TextureCubeInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    state->result = cna_texturecube_get_info(state->texture, &info);
    return 0;
}

static int validate_texture3d_rejection(const CNA_Handle device)
{
    CNA_Texture3DCreateInfo create_info = {
        sizeof(CNA_Texture3DCreateInfo), UINT32_C(1), 4U, 4U, 4U,
        CNA_TRUE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U
    };
    CNA_Handle texture = UINT64_MAX;
    if (cna_texture3d_create(device, &create_info, &texture) !=
            CNA_RESULT_NOT_SUPPORTED || texture != CNA_INVALID_HANDLE) {
        return 0;
    }
    create_info.format = CNA_SURFACE_FORMAT_BGR565;
    texture = UINT64_MAX;
    if (cna_texture3d_create(device, &create_info, &texture) !=
            CNA_RESULT_NOT_SUPPORTED || texture != CNA_INVALID_HANDLE) {
        return 0;
    }
    create_info.format = UINT32_MAX;
    texture = UINT64_MAX;
    if (cna_texture3d_create(device, &create_info, &texture) !=
            CNA_RESULT_INVALID_ARGUMENT || texture != CNA_INVALID_HANDLE) {
        return 0;
    }
    create_info.format = CNA_SURFACE_FORMAT_COLOR;
    create_info.width = 0U;
    if (cna_texture3d_create(device, &create_info, &texture) !=
            CNA_RESULT_INVALID_ARGUMENT || texture != CNA_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

/*
 * Cube storage is backend-dependent, so the transfer contract is probed once and then asserted in
 * whichever direction this backend actually implements. Both directions are real evidence: a
 * refusing backend must leave the destination untouched and still report the required count, and a
 * storing backend must return exactly what was uploaded.
 */
static int validate_cube_failures(
    const CNA_Handle cube,
    const CNA_TextureCubeTransfer* const full,
    const CNA_Color data[17])
{
    CNA_Color destination[17];
    CNA_Color before[17];
    uint64_t required = UINT64_C(999);
    memset(destination, 0x5a, sizeof(destination));
    memcpy(before, destination, sizeof(before));

    const CNA_Result stored = cna_texturecube_set_data(cube, full, data, 17U);
    if (stored != CNA_RESULT_SUCCESS && stored != CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    const int has_storage = stored == CNA_RESULT_SUCCESS;

    for (CNA_CubeMapFace face = CNA_CUBE_MAP_FACE_POSITIVE_X;
         face <= CNA_CUBE_MAP_FACE_NEGATIVE_Z;
         ++face) {
        CNA_TextureCubeTransfer transfer = *full;
        transfer.face = face;
        required = UINT64_C(999);
        if (has_storage) {
            memset(destination, 0x5a, sizeof(destination));
            /* The window starts at element one, so the round trip is compared from there. */
            if (cna_texturecube_set_data(cube, &transfer, data, 17U) !=
                    CNA_RESULT_SUCCESS ||
                cna_texturecube_get_data(
                    cube, &transfer, destination, 17U, &required) != CNA_RESULT_SUCCESS ||
                required != 16U ||
                memcmp(&destination[1], &data[1], sizeof(CNA_Color) * 16U) != 0) {
                return 0;
            }
            continue;
        }
        if (cna_texturecube_set_data(cube, &transfer, data, 17U) !=
                CNA_RESULT_NOT_SUPPORTED ||
            cna_texturecube_get_data(
                cube, &transfer, destination, 17U, &required) !=
                CNA_RESULT_NOT_SUPPORTED || required != 16U ||
            memcmp(destination, before, sizeof(destination)) != 0) {
            return 0;
        }
    }
    memset(destination, 0x5a, sizeof(destination));

    CNA_TextureCubeTransfer invalid = *full;
    invalid.face = UINT32_MAX;
    if (cna_texturecube_set_data(cube, &invalid, data, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = *full;
    invalid.struct_version = 2U;
    if (cna_texturecube_set_data(cube, &invalid, data, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = *full;
    invalid.has_rectangle = CNA_TRUE;
    invalid.rectangle = (CNA_Rectangle){3, 3, 2, 2};
    if (cna_texturecube_set_data(cube, &invalid, data, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    invalid = *full;
    invalid.element_count = 15U;
    if (cna_texturecube_set_data(cube, &invalid, data, 17U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    required = 0U;
    if (cna_texturecube_get_data(cube, full, destination, 16U, &required) !=
            CNA_RESULT_BUFFER_TOO_SMALL || required != 16U ||
        memcmp(destination, before, sizeof(destination)) != 0 ||
        cna_texturecube_get_data(cube, full, destination, 17U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    CNA_TextureCubeTransfer mip = make_cube_transfer(
        CNA_CUBE_MAP_FACE_POSITIVE_Y, 1, 1U, 4U);
    const CNA_Result expected_mip =
        has_storage ? CNA_RESULT_SUCCESS : CNA_RESULT_NOT_SUPPORTED;
    required = 0U;
    if (cna_texturecube_set_data(cube, &mip, data, 5U) != expected_mip ||
        cna_texturecube_get_data(cube, &mip, destination, 5U, &required) != expected_mip ||
        required != 4U) {
        return 0;
    }
    return 1;
}

static int validate_cube(const CNA_Handle device)
{
    static const char TypeName[] = CNA_TEXTURE_CUBE_TYPE_NAME;
    static const char ResourceName[] = "volume cube";
    CNA_TextureCubeCreateInfo create_info = {
        sizeof(CNA_TextureCubeCreateInfo), UINT32_C(1), 4U,
        CNA_TRUE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR, 0U
    };
    CNA_Handle cube = CNA_INVALID_HANDLE;
    if (cna_texturecube_create(device, &create_info, &cube) != CNA_RESULT_SUCCESS ||
        cube == CNA_INVALID_HANDLE) {
        return 0;
    }

    CNA_TextureInfo common = {
        sizeof(CNA_TextureInfo), UINT32_C(1), 0U, 0U
    };
    CNA_TextureCubeInfo info = {
        sizeof(CNA_TextureCubeInfo), UINT32_C(1), 0U, 0U, 0U, UINT32_MAX
    };
    uint64_t count = UINT64_MAX;
    char name[sizeof(TypeName) - 1U];
    char resource_name[sizeof(ResourceName) - 1U];
    char too_small = 'x';
    CNA_Bool disposed = CNA_TRUE;
    CNA_Handle owner = CNA_INVALID_HANDLE;
    CNA_GraphicsResourceTag tag = 0U;
    if (cna_texture_get_info(cube, &common) != CNA_RESULT_SUCCESS ||
        common.level_count != 3U || common.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_texturecube_get_info(cube, &info) != CNA_RESULT_SUCCESS ||
        info.size != 4U || info.level_count != 3U ||
        info.format != CNA_SURFACE_FORMAT_COLOR || info.reserved != 0U ||
        cna_texturecube_get_type_name_byte_count(cube, &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(TypeName) - 1U ||
        cna_texturecube_copy_type_name(cube, &too_small, 1U, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL || too_small != 'x' ||
        cna_texturecube_copy_type_name(cube, name, sizeof(name), &count) !=
            CNA_RESULT_SUCCESS || memcmp(name, TypeName, sizeof(name)) != 0 ||
        cna_graphics_resource_set_name(
            cube, (CNA_StringView){ResourceName, sizeof(ResourceName) - 1U}) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_resource_copy_name(
            cube, resource_name, sizeof(resource_name), &count) != CNA_RESULT_SUCCESS ||
        count != sizeof(resource_name) ||
        memcmp(resource_name, ResourceName, sizeof(resource_name)) != 0 ||
        cna_graphics_resource_get_graphics_device(cube, &owner) != CNA_RESULT_SUCCESS ||
        owner != device ||
        cna_graphics_resource_set_tag(cube, UINT64_C(0x123456789abcdef0)) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_tag(cube, &tag) != CNA_RESULT_SUCCESS ||
        tag != UINT64_C(0x123456789abcdef0) ||
        cna_graphics_resource_get_is_disposed(cube, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_FALSE) {
        return 0;
    }

    WrongThreadState wrong_thread = {cube, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, use_cube_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 0;
    }

    CNA_Color data[17];
    for (uint32_t index = 0U; index < 17U; ++index) {
        data[index] = (CNA_Color){
            (uint8_t)index, (uint8_t)(index + 1U),
            (uint8_t)(index + 2U), UINT8_C(255)};
    }
    CNA_TextureCubeTransfer full = make_cube_transfer(
        CNA_CUBE_MAP_FACE_POSITIVE_X, 0, 1U, 16U);
    if (!validate_cube_failures(cube, &full, data)) {
        return 0;
    }

    uint8_t dds[176];
    const size_t dds_size = build_minimal_cube_dds(dds);
    CNA_Handle decoded = UINT64_MAX;
    /* DDS decoding needs cube storage, so a backend without it refuses and one with it succeeds. */
    const CNA_Result dds_result = cna_texturecube_create_from_dds_memory(
        device, dds, (uint64_t)dds_size, &decoded);
    if (dds_size != sizeof(dds) ||
        (dds_result != CNA_RESULT_NOT_SUPPORTED && dds_result != CNA_RESULT_SUCCESS) ||
        (dds_result == CNA_RESULT_NOT_SUPPORTED && decoded != CNA_INVALID_HANDLE) ||
        (dds_result == CNA_RESULT_SUCCESS &&
         (decoded == CNA_INVALID_HANDLE ||
          cna_texturecube_destroy(decoded) != CNA_RESULT_SUCCESS))) {
        return 0;
    }
    decoded = UINT64_MAX;
    if (cna_texturecube_create_from_dds_memory(device, 0, 1U, &decoded) !=
            CNA_RESULT_INVALID_ARGUMENT || decoded != CNA_INVALID_HANDLE) {
        return 0;
    }

    if (cna_graphics_resource_dispose(cube) != CNA_RESULT_SUCCESS ||
        cna_graphics_resource_get_is_disposed(cube, &disposed) != CNA_RESULT_SUCCESS ||
        disposed != CNA_TRUE ||
        cna_texturecube_set_data(cube, &full, data, 17U) !=
            CNA_RESULT_INVALID_STATE ||
        cna_texturecube_destroy(cube) != CNA_RESULT_SUCCESS ||
        cna_texturecube_get_info(cube, &info) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return 1;
}

static int validate_render_target_cube(const CNA_Handle device)
{
    static const char TypeName[] = CNA_RENDER_TARGET_CUBE_TYPE_NAME;
    const CNA_RenderTargetCubeCreateInfo create_info = {
        sizeof(CNA_RenderTargetCubeCreateInfo), UINT32_C(1), 2U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR,
        CNA_DEPTH_FORMAT_NONE, 0, CNA_RENDER_TARGET_USAGE_DISCARD_CONTENTS
    };
    CNA_Handle target = CNA_INVALID_HANDLE;
    CNA_TextureCubeInfo info = {
        sizeof(CNA_TextureCubeInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    CNA_TextureInfo common = {
        sizeof(CNA_TextureInfo), UINT32_C(1), 0U, 0U
    };
    char name[sizeof(TypeName) - 1U];
    uint64_t count = 0U;
    CNA_Color colors[4] = {{1U, 2U, 3U, 4U}};
    CNA_TextureCubeTransfer transfer = make_cube_transfer(
        CNA_CUBE_MAP_FACE_NEGATIVE_Z, 0, 0U, 4U);
    if (cna_render_target_cube_create(device, &create_info, &target) !=
            CNA_RESULT_SUCCESS || target == CNA_INVALID_HANDLE ||
        cna_texturecube_get_info(target, &info) != CNA_RESULT_SUCCESS ||
        info.size != 2U || info.level_count != 1U ||
        cna_texture_get_info(target, &common) != CNA_RESULT_SUCCESS ||
        common.level_count != 1U ||
        cna_texturecube_copy_type_name(target, name, sizeof(name), &count) !=
            CNA_RESULT_SUCCESS || count != sizeof(name) ||
        memcmp(name, TypeName, sizeof(name)) != 0 ||
        cna_texturecube_set_data(target, &transfer, colors, 4U) !=
            CNA_RESULT_NOT_SUPPORTED ||
        cna_texturecube_destroy(target) != CNA_RESULT_INVALID_HANDLE ||
        cna_render_target_destroy(target) != CNA_RESULT_SUCCESS) {
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
     * The renderer identity is deliberately not an allowlist: an enumerated identity is not a
     * support claim, so this suite branches on the reported capabilities instead and runs
     * unchanged on any backend.
     */
    CNA_Bool supports_texture3d = CNA_TRUE;
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_renderer_info(device, &renderer) != CNA_RESULT_SUCCESS ||
        renderer.renderer_type == CNA_GRAPHICS_RENDERER_UNKNOWN ||
        cna_graphics_device_supports_capability(
            device, CNA_GRAPHICS_CAPABILITY_TEXTURE_3D, &supports_texture3d) !=
            CNA_RESULT_SUCCESS ||
        supports_texture3d != CNA_FALSE) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 2;
    if (!validate_texture3d_rejection(device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 3;
    if (!validate_cube(device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->stage = 4;
    if (!validate_render_target_cube(device)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->borrowed_device = device;
    state->validated = 1;
    state->stage = 5;
    return CNA_RESULT_SUCCESS;
}

static int validate_wrong_kind(void)
{
    CNA_CurveHandle curve = CNA_INVALID_HANDLE;
    CNA_Texture3DInfo info_3d = {
        sizeof(CNA_Texture3DInfo), UINT32_C(1), 1U, 1U, 1U, 1U, 1U, 1U
    };
    CNA_TextureCubeInfo info_cube = {
        sizeof(CNA_TextureCubeInfo), UINT32_C(1), 1U, 1U, 1U, 1U
    };
    CNA_Texture3DTransfer transfer_3d = {
        sizeof(CNA_Texture3DTransfer), UINT32_C(1), 0,
        0, 0, 1, 1, 0, 1, 0U, 0U, 1U
    };
    CNA_Color color = {1U, 2U, 3U, 4U};
    uint64_t count = 77U;
    uint8_t bytes[4] = {1U, 2U, 3U, 4U};
    if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS ||
        cna_texture3d_get_info(curve, &info_3d) != CNA_RESULT_INVALID_HANDLE ||
        cna_texture3d_get_type_name_byte_count(curve, &count) !=
            CNA_RESULT_INVALID_HANDLE || count != 77U ||
        cna_texture3d_copy_type_name(curve, 0, 0U, &count) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_texture3d_set_data(curve, &transfer_3d, &color, 1U) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_texture3d_get_data(curve, &transfer_3d, &color, 1U, &count) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_texture3d_set_data_bytes(curve, &transfer_3d, bytes, 4U) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_texture3d_destroy(curve) != CNA_RESULT_INVALID_HANDLE ||
        cna_texturecube_get_info(curve, &info_cube) != CNA_RESULT_INVALID_HANDLE ||
        cna_texturecube_destroy(curve) != CNA_RESULT_INVALID_HANDLE ||
        cna_curve_destroy(curve) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!validate_wrong_kind()) {
        return 1;
    }
    CallbackState state = {CNA_INVALID_HANDLE, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    static const char Title[] = "C API texture volume";
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
        cna_graphics_device_get_renderer_info(state.borrowed_device, &stale) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        (void)fprintf(stderr, "Texture volume callback failed at stage %d\n", state.stage);
        return 2;
    }
    return 0;
}
