/* SPDX-License-Identifier: MS-PL */

/*
 * Process teardown, as the two distinct contracts it actually is.
 *
 * docs/c-api/OWNERSHIP.md requires a C caller to release every owned handle "exactly once before
 * runtime shutdown", and docs/c-api/HANDLES.md adds that "no garbage collector, finalizer or C++
 * destructor is assumed at the ABI boundary". A caller that honours that is the *explicit* path.
 * A caller that does not still returns from main, and the library must not die on the way out --
 * the handle leaks, which is the documented consequence, and nothing else happens. That is the
 * *fallback* path, and until plans/plan_capi_smoke_stability.md CSS-3 it crashed: the handle
 * registry is a static constructed before the statics a Game's own construction creates, so
 * reverse-order destruction always left ~Game reading a freed vector (ASan: heap-use-after-free
 * at UninstallPlatform).
 *
 * Nothing covered that. Every other smoke test destroys what it creates, and the eight that
 * crashed only reached the fallback by bailing out early on an unrelated stale expectation -- so
 * the suite could go green again with the defect still in place. Each mode below is a whole
 * process: the interesting part happens after main returns, so the exit status is the assertion.
 * A mode that survives its own checks and then dies in teardown is a failure, which is why every
 * mode ends in a plain `return 0` rather than a report of its own.
 *
 * Run with no argument to list the modes.
 */

#include "CNA/C/abi.h"
#include "CNA/C/display.h"
#include "CNA/C/graphics.h"
#include "CNA/C/graphics_device.h"
#include "CNA/C/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct TeardownState {
    CNA_Handle textures[2];
    int texture_count;
    int load_failed;
} TeardownState;

static const char TitleText[] = "C API teardown lifetime";

static CNA_Result make_textures(const CNA_Handle game, void* const context)
{
    TeardownState* const state = (TeardownState*)context;
    const CNA_Texture2DCreateInfo create_info = {
        sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 4U, 4U,
        CNA_FALSE, {0U, 0U, 0U}, CNA_SURFACE_FORMAT_COLOR
    };
    CNA_Handle device = CNA_INVALID_HANDLE;
    int index = 0;

    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE) {
        state->load_failed = 1;
        return CNA_RESULT_INVALID_STATE;
    }
    for (index = 0; index < 2; ++index) {
        CNA_Handle texture = CNA_INVALID_HANDLE;
        /* A renderer without 2D texture creation is not a failure of this test: teardown of a
           game that owns nothing is already covered by the game-only modes. */
        if (cna_texture2d_create(device, &create_info, &texture) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_SUCCESS;
        }
        state->textures[state->texture_count++] = texture;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_load(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const callback_error)
{
    (void)game_time;
    (void)callback_error;
    return make_textures(game, context);
}

/* Creates a game, optionally runs one frame, and hands the handle back. A frame is what brings the
   GraphicsDevice and the renderer up, so running none is a genuinely different teardown shape. */
static int start_game(
    TeardownState* const state,
    const CNA_GameCallbacks* const callbacks,
    const int run_frame,
    CNA_Handle* const out_game)
{
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {TitleText, sizeof(TitleText) - 1U}, callbacks
    };

    *out_game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, out_game) != CNA_RESULT_SUCCESS ||
        *out_game == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (run_frame && cna_game_run_one_frame(*out_game) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return !state->load_failed;
}

static int run_explicit(void)
{
    TeardownState state = {{CNA_INVALID_HANDLE, CNA_INVALID_HANDLE}, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    CNA_Handle game = CNA_INVALID_HANDLE;
    int index = 0;

    if (!start_game(&state, &callbacks, 1, &game)) {
        return 1;
    }
    for (index = 0; index < state.texture_count; ++index) {
        if (cna_texture2d_destroy(state.textures[index]) != CNA_RESULT_SUCCESS) {
            return 1;
        }
    }
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    /* Releasing twice must stay a detected error rather than a second teardown. */
    if (cna_game_destroy(game) != CNA_RESULT_INVALID_HANDLE) {
        return 1;
    }
    return 0;
}

static int run_game_alive(const int run_frame)
{
    TeardownState state = {{CNA_INVALID_HANDLE, CNA_INVALID_HANDLE}, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, 0, 0, 0, 0, &state};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!start_game(&state, &callbacks, run_frame, &game)) {
        return 1;
    }
    return 0; /* deliberately undestroyed */
}

static int run_children_alive(void)
{
    TeardownState state = {{CNA_INVALID_HANDLE, CNA_INVALID_HANDLE}, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!start_game(&state, &callbacks, 1, &game)) {
        return 1;
    }
    return 0; /* game and every child deliberately undestroyed */
}

static int run_partial(void)
{
    TeardownState state = {{CNA_INVALID_HANDLE, CNA_INVALID_HANDLE}, 0, 0};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), on_load, 0, 0, 0, 0, &state};
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!start_game(&state, &callbacks, 1, &game)) {
        return 1;
    }
    if (state.texture_count > 0 &&
        cna_texture2d_destroy(state.textures[0]) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    /* A game with a live child refuses destruction, so this is also the shape a caller reaches by
       trying to clean up and being told it cannot yet. */
    if (state.texture_count > 1 && cna_game_destroy(game) != CNA_RESULT_INVALID_STATE) {
        return 1;
    }
    return 0;
}

static int run_cycles(void)
{
    int cycle = 0;

    for (cycle = 0; cycle < 3; ++cycle) {
        if (run_explicit() != 0) {
            return 1;
        }
    }
    /* A fourth game, left alive, so the fallback runs against a registry whose slots have already
       been recycled through several generations. */
    return run_children_alive();
}

static int run_device_alive(void)
{
    CNA_PresentationParameters parameters;
    CNA_Handle device = CNA_INVALID_HANDLE;

    if (cna_presentation_parameters_init(&parameters) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    parameters.back_buffer_width = 64;
    parameters.back_buffer_height = 64;
    if (cna_graphics_device_create(
            0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &device) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    return 0; /* a standalone device, owning no game, deliberately undestroyed */
}

int main(const int argc, char** const argv)
{
    if (argc != 2) {
        (void)fprintf(
            stderr,
            "usage: %s <explicit|game-alive|game-alive-no-frame|children-alive|partial|cycles"
            "|device-alive>\n",
            argc > 0 ? argv[0] : "cna_c_api_teardown_lifetime_smoke");
        return 2;
    }
    if (strcmp(argv[1], "explicit") == 0) {
        return run_explicit();
    }
    if (strcmp(argv[1], "game-alive") == 0) {
        return run_game_alive(1);
    }
    if (strcmp(argv[1], "game-alive-no-frame") == 0) {
        return run_game_alive(0);
    }
    if (strcmp(argv[1], "children-alive") == 0) {
        return run_children_alive();
    }
    if (strcmp(argv[1], "partial") == 0) {
        return run_partial();
    }
    if (strcmp(argv[1], "cycles") == 0) {
        return run_cycles();
    }
    if (strcmp(argv[1], "device-alive") == 0) {
        return run_device_alive();
    }
    (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
