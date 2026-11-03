// SPDX-License-Identifier: MS-PL
// MTH-PLAT-004 D2: a pure-C program built only from the installed Emscripten package
// (CNA::CApiStatic and CNA::CApiStaticAsyncify) that creates a game, runs it in a browser through
// blocking cna_game_run, exits it and destroys it. scripts/run_browser_tests.mjs accepts it only
// on its fixed-denominator summary with no [FAIL] line, page error or uncaptured WebGPU error.
#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>

#define FRAME_COUNT 3
// Renderer identity, texture round trip, one check per frame, run returning, teardown.
#define EXPECTED (2 + FRAME_COUNT + 2)

typedef struct ProbeState {
    int frames;
    CNA_Handle texture;
} ProbeState;

static int passed;
static int total;

static void check(const int ok, const char* const label)
{
    ++total;
    passed += ok ? 1 : 0;
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
    fflush(stdout);
}

static void report_error(const char* const what, const CNA_Result result)
{
    char message[256];
    uint64_t bytes = 0U;
    if (cna_error_copy_last_message(message, sizeof(message) - 1U, &bytes) != CNA_RESULT_SUCCESS) {
        bytes = 0U;
    }
    message[bytes] = '\0';
    printf("[FAIL] %s returned %u: %s\n", what, (unsigned)result, message);
    fflush(stdout);
}

static CNA_Result load_content(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    static const CNA_Color texels[4] = {
        {UINT8_C(255), UINT8_C(0), UINT8_C(0), UINT8_C(255)},
        {UINT8_C(0), UINT8_C(255), UINT8_C(0), UINT8_C(255)},
        {UINT8_C(0), UINT8_C(0), UINT8_C(255), UINT8_C(255)},
        {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)}
    };
    ProbeState* const state = (ProbeState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Texture2DCreateInfo info;
    CNA_Color read_back[4];
    char name[64];
    uint64_t bytes = 0U;
    uint64_t pixels = 0U;
    CNA_Result result;
    (void)game_time;
    (void)out_error;

    result = cna_game_get_graphics_device(game, &device);
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_graphics_device_copy_renderer_name(device, name, sizeof(name) - 1U, &bytes);
    }
    if (result != CNA_RESULT_SUCCESS) {
        report_error("reading the renderer name", result);
        return result;
    }
    name[bytes] = '\0';
    printf("[RENDERER] %s expected=%s\n", name, PROBE_EXPECTED_RENDERER);
    check(strcmp(name, PROBE_EXPECTED_RENDERER) == 0, "installed C API runs the producer's renderer");

    // Upload and read back through the GPU; the readback waits on the device inside a callback.
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    info.width = UINT32_C(2);
    info.height = UINT32_C(2);
    info.mip_map = CNA_FALSE;
    info.format = CNA_SURFACE_FORMAT_COLOR;
    memset(read_back, 0, sizeof(read_back));
    result = cna_texture2d_create(device, &info, &state->texture);
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_texture2d_set_data_rgba8(state->texture, texels, UINT64_C(4));
    }
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_texture2d_get_data_rgba8(state->texture, read_back, UINT64_C(4), &pixels);
    }
    if (result != CNA_RESULT_SUCCESS) {
        report_error("the texture round trip", result);
    }
    check(result == CNA_RESULT_SUCCESS && pixels == UINT64_C(4) &&
              memcmp(read_back, texels, sizeof(texels)) == 0,
          "a texture uploaded through the C API reads back unchanged");
    return CNA_RESULT_SUCCESS;
}

static CNA_Result draw(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    ProbeState* const state = (ProbeState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    // Alternate the clear colour, as the C++ installed frame does.
    const float red = state->frames % 2 == 0 ? 1.0f : 0.0f;
    CNA_Result result;
    (void)game_time;
    (void)out_error;

    result = cna_game_get_graphics_device(game, &device);
    if (result == CNA_RESULT_SUCCESS) {
        result = cna_graphics_device_clear_rgba(device, red, 0.0f, 1.0f - red, 1.0f);
    }
    if (result != CNA_RESULT_SUCCESS) {
        report_error("clearing the backbuffer", result);
    }
    ++state->frames;
    printf("[FRAME] %d\n", state->frames);
    check(result == CNA_RESULT_SUCCESS, "frame cleared its backbuffer through the C API");
    if (state->frames == FRAME_COUNT) {
        return cna_game_request_exit(game);
    }
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    static const char title[] = "InstalledCApiBrowser";
    ProbeState state;
    CNA_GameCallbacks callbacks;
    CNA_GameCreateInfo create_info;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Result result;

    memset(&state, 0, sizeof(state));
    state.texture = CNA_INVALID_HANDLE;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.load_content = load_content;
    callbacks.draw = draw;
    callbacks.context = &state;
    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title.data = title;
    create_info.window_title.byte_length = sizeof(title) - 1U;
    create_info.callbacks = &callbacks;

    result = cna_game_create(&create_info, &game);
    if (result != CNA_RESULT_SUCCESS) {
        report_error("cna_game_create", result);
    } else {
        result = cna_game_run(game);
        if (result != CNA_RESULT_SUCCESS) {
            report_error("cna_game_run", result);
        }
        check(result == CNA_RESULT_SUCCESS && state.frames == FRAME_COUNT,
              "blocking cna_game_run returned after cna_game_request_exit on the last frame");
        result = state.texture == CNA_INVALID_HANDLE ? CNA_RESULT_SUCCESS
                                                     : cna_texture2d_destroy(state.texture);
        if (result == CNA_RESULT_SUCCESS) {
            result = cna_game_destroy(game);
        }
        if (result != CNA_RESULT_SUCCESS) {
            report_error("teardown", result);
        }
        check(result == CNA_RESULT_SUCCESS, "the texture and then the game were destroyed");
    }
    // Fixed denominator: an early exit cannot qualify a partial run.
    printf("=== %d/%d PASS ===\n", passed, EXPECTED);
    fflush(stdout);
    return passed == EXPECTED && total == EXPECTED ? 0 : 1;
}
