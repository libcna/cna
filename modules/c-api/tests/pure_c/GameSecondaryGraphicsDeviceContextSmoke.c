/* SPDX-License-Identifier: MS-PL */

/*
 * Regression for a caller-created GraphicsDevice changing the GL context underneath a running
 * Game. This deliberately uses only the public C ABI: repairing the context from the test with
 * SDL_GL_MakeCurrent would hide the product defect all bindings need CNA itself to solve.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <stdio.h>

typedef struct ContextSmokeState {
    int draws;
    int create_secondary;
    int failure_stage;
    CNA_Result failure_result;
    CNA_Handle secondary;
} ContextSmokeState;

static CNA_Result on_draw(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game_time;
    (void)out_error;
    ContextSmokeState* const state = (ContextSmokeState*)context;
    ++state->draws;
    CNA_Result result = cna_game_clear(game, (CNA_Color){24U, 48U, 96U, 255U});
    if (result != CNA_RESULT_SUCCESS || state->create_secondary == 0) {
        return result;
    }
    state->create_secondary = 0;

    /*
     * BeginDraw has made A current before this callback. Creating and using B here therefore
     * exercises the exact nested sequence that used to replace an actively-running Game context.
     */
    CNA_PresentationParameters parameters;
    result = cna_presentation_parameters_init(&parameters);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 4;
        state->failure_result = result;
        return result;
    }
    parameters.back_buffer_width = 64;
    parameters.back_buffer_height = 64;
    result = cna_graphics_device_create(
        0U, CNA_GRAPHICS_PROFILE_REACH, &parameters, &state->secondary);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 5;
        state->failure_result = result;
        return result;
    }

    result = cna_graphics_device_clear_options(
        state->secondary,
        CNA_CLEAR_OPTION_TARGET | CNA_CLEAR_OPTION_DEPTH_BUFFER | CNA_CLEAR_OPTION_STENCIL,
        (CNA_Color){96U, 48U, 24U, 255U},
        1.0F,
        0);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 6;
        state->failure_result = result;
        return result;
    }
    result = cna_graphics_device_present(state->secondary);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 7;
        state->failure_result = result;
        return result;
    }

    CNA_Handle game_device = CNA_INVALID_HANDLE;
    result = cna_game_get_graphics_device(game, &game_device);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 8;
        state->failure_result = result;
        return result;
    }
    result = cna_graphics_device_present(game_device);
    if (result != CNA_RESULT_SUCCESS) {
        state->failure_stage = 9;
        state->failure_result = result;
    }
    return result;
}

static int fail(const int code, const char* const operation, const CNA_Result result)
{
    (void)fprintf(stderr, "%s failed with CNA_Result %u\n", operation, (unsigned)result);
    return code;
}

int main(void)
{
    ContextSmokeState state = {
        0, 0, 0, CNA_RESULT_SUCCESS, CNA_INVALID_HANDLE};
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, 0, on_draw, 0, 0, &state};
    static const char title[] = "C API secondary graphics context";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, sizeof(title) - 1U},
        &callbacks};

    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Result result = cna_game_create(&create_info, &game);
    if (result != CNA_RESULT_SUCCESS) {
        return fail(1, "game creation", result);
    }
    result = cna_game_run_one_frame(game);
    if (result != CNA_RESULT_SUCCESS || state.draws != 1) {
        return fail(2, "first game frame", result);
    }

    state.create_secondary = 1;
    result = cna_game_run_one_frame(game);
    if (result != CNA_RESULT_SUCCESS || state.draws != 2) {
        (void)fprintf(
            stderr,
            "secondary-device callback stage %d returned CNA_Result %u\n",
            state.failure_stage,
            (unsigned)state.failure_result);
        return fail(10, "game frame after secondary creation", result);
    }

    result = cna_graphics_device_destroy(state.secondary);
    if (result != CNA_RESULT_SUCCESS) {
        return fail(11, "secondary graphics-device destruction", result);
    }

    result = cna_game_run_one_frame(game);
    if (result != CNA_RESULT_SUCCESS || state.draws != 3) {
        return fail(12, "game frame after secondary destruction", result);
    }
    result = cna_game_destroy(game);
    if (result != CNA_RESULT_SUCCESS) {
        return fail(13, "game destruction", result);
    }
    return 0;
}
