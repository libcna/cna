// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>
#include <threads.h>

static const char FixturePath[] = "cna_c_api_content_\xC5\xBE_fixture.bmp";

typedef struct ContentState {
    CNA_Handle content_manager;
    CNA_Handle texture;
    int load_validated;
} ContentState;

typedef struct WrongThreadState {
    CNA_Handle content_manager;
    CNA_Result result;
} WrongThreadState;

static int write_fixture(void)
{
    static const unsigned char bmp[] = {
        0x42U, 0x4DU, 0x3AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x36U, 0x00U, 0x00U, 0x00U, 0x28U, 0x00U,
        0x00U, 0x00U, 0x01U, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U,
        0x00U, 0x00U, 0x01U, 0x00U, 0x18U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x13U, 0x0BU,
        0x00U, 0x00U, 0x13U, 0x0BU, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1EU, 0x14U,
        0x0AU, 0x00U
    };
    FILE* const file = fopen(FixturePath, "wb");
    if (file == 0) {
        return 0;
    }
    const int wrote = fwrite(bmp, sizeof(bmp), 1U, file) == 1U;
    const int closed = fclose(file) == 0;
    const int ok = wrote && closed;
    if (!ok) {
        (void)remove(FixturePath);
    }
    return ok;
}

static int pixel_is_fixture_color(const CNA_Handle texture)
{
    CNA_Texture2DInfo info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    CNA_Color pixel = {UINT8_C(0), UINT8_C(0), UINT8_C(0), UINT8_C(0)};
    uint64_t pixels = 0U;
    return cna_texture2d_get_info(texture, &info) == CNA_RESULT_SUCCESS &&
        info.width == 1U && info.height == 1U && info.level_count == 1U &&
        info.format == CNA_SURFACE_FORMAT_COLOR &&
        cna_texture2d_get_data_rgba8(texture, &pixel, 1U, &pixels) == CNA_RESULT_SUCCESS &&
        pixels == 1U && pixel.r == UINT8_C(10) && pixel.g == UINT8_C(20) &&
        pixel.b == UINT8_C(30) && pixel.a == UINT8_C(255);
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    ContentState* const state = (ContentState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time != 0 ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_StringView root = {".", UINT64_C(1)};
    CNA_ContentManagerCreateInfo create_info = {
        sizeof(CNA_ContentManagerCreateInfo), UINT32_C(1), root, UINT64_C(1)
    };
    CNA_Handle invalid_manager = UINT64_C(77);
    if (cna_content_manager_create(graphics_device, &create_info, &invalid_manager) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        invalid_manager != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    create_info.reserved = 0U;
    if (cna_content_manager_create(graphics_device, &create_info, &state->content_manager) !=
            CNA_RESULT_SUCCESS ||
        state->content_manager == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    uint64_t root_bytes = 0U;
    char root_copy[2] = {'x', 'x'};
    if (cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 1U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            0U,
            &root_bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        root_copy[0] != 'x' || root_bytes != 1U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            1U,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_copy[0] != '.' || root_copy[1] != 'x') {
        return CNA_RESULT_INVALID_STATE;
    }

    const char invalid_utf8[] = {(char)0xC3, '('};
    if (cna_content_manager_set_root_directory(
            state->content_manager,
            (CNA_StringView){invalid_utf8, UINT64_C(2)}) != CNA_RESULT_ENCODING ||
        cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 1U ||
        cna_content_manager_set_root_directory(
            state->content_manager,
            (CNA_StringView){"missing", UINT64_C(7)}) != CNA_RESULT_SUCCESS ||
        cna_content_manager_get_root_directory_size(
            state->content_manager,
            &root_bytes) != CNA_RESULT_SUCCESS ||
        root_bytes != 7U ||
        cna_content_manager_copy_root_directory(
            state->content_manager,
            root_copy,
            sizeof(root_copy),
            &root_bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        root_copy[0] != '.' || root_copy[1] != 'x' ||
        cna_content_manager_set_root_directory(state->content_manager, root) !=
            CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle invalid_texture = UINT64_C(88);
    const char embedded_nul[] = {'x', '\0', 'y'};
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            (CNA_StringView){0, 0U},
            &invalid_texture) != CNA_RESULT_INVALID_ARGUMENT ||
        invalid_texture != CNA_INVALID_HANDLE ||
        cna_content_manager_load_texture2d(
            state->content_manager,
            (CNA_StringView){embedded_nul, UINT64_C(3)},
            &invalid_texture) != CNA_RESULT_ENCODING ||
        invalid_texture != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }

    const CNA_StringView asset = {FixturePath, sizeof(FixturePath) - 1U};
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &state->texture) != CNA_RESULT_SUCCESS ||
        state->texture == CNA_INVALID_HANDLE || !pixel_is_fixture_color(state->texture)) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Handle cached_texture = CNA_INVALID_HANDLE;
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &cached_texture) != CNA_RESULT_SUCCESS ||
        cached_texture == CNA_INVALID_HANDLE || cached_texture == state->texture ||
        !pixel_is_fixture_color(cached_texture) ||
        cna_texture2d_destroy(cached_texture) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    if (cna_content_manager_unload(state->content_manager) != CNA_RESULT_SUCCESS ||
        !pixel_is_fixture_color(state->texture) || remove(FixturePath) != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    invalid_texture = UINT64_C(99);
    if (cna_content_manager_load_texture2d(
            state->content_manager,
            asset,
            &invalid_texture) != CNA_RESULT_IO ||
        invalid_texture != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_ErrorInfo error = {
        sizeof(CNA_ErrorInfo), UINT32_C(1), CNA_RESULT_SUCCESS, CNA_ERROR_CATEGORY_NONE, 0U
    };
    if (cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS ||
        error.result != CNA_RESULT_IO || error.category != CNA_ERROR_CATEGORY_IO ||
        error.message_byte_length == 0U) {
        return CNA_RESULT_INVALID_STATE;
    }

    state->load_validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int call_unload_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    state->result = cna_content_manager_unload(state->content_manager);
    return 0;
}

int main(void)
{
    if (!write_fixture()) {
        return 1;
    }

    ContentState state = {CNA_INVALID_HANDLE, CNA_INVALID_HANDLE, 0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API content smoke", UINT64_C(19)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || state.load_validated != 1 ||
        state.content_manager == CNA_INVALID_HANDLE || state.texture == CNA_INVALID_HANDLE) {
        (void)remove(FixturePath);
        return 2;
    }
    if (cna_game_destroy(game) != CNA_RESULT_INVALID_STATE) {
        return 3;
    }

    WrongThreadState wrong_thread = {state.content_manager, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, call_unload_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success || wrong_thread.result != CNA_RESULT_THREAD) {
        return 4;
    }

    if (cna_content_manager_destroy(state.content_manager) != CNA_RESULT_SUCCESS ||
        cna_content_manager_unload(state.content_manager) != CNA_RESULT_INVALID_HANDLE ||
        !pixel_is_fixture_color(state.texture) ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 5;
    }
    return 0;
}
