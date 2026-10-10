// SPDX-License-Identifier: MS-PL

/*
 * A C API game owns no GraphicsDeviceManager unless it creates one, and CNA still gives it a
 * device. That device used to come from PresentationParameters' own default, DepthFormat::None,
 * so the game's Clear(color, depth) was refused once SOFTWARE-333 stopped masking a clear of an
 * attachment that does not exist. An XNA game's device comes from a GraphicsDeviceManager, whose
 * PreferredDepthStencilFormat defaults to Depth24; the implicit device now carries the same, and
 * this pins it: the active parameters report Depth24, and on a renderer with a 3D pipeline the
 * depth clear succeeds. `new PresentationParameters()` itself keeps XNA's None.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

typedef struct {
    int frames;
    int saw_depth24;
    int three_d;
    CNA_Result clear_result;
} DepthState;

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    DepthState* const state = (DepthState*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_PresentationParameters active;
    CNA_Bool three_d = CNA_FALSE;
    const CNA_Color color = {100U, 149U, 237U, 255U};
    (void)game_time;
    (void)out_error;
    if (state->frames++ != 0) {
        return CNA_RESULT_SUCCESS;
    }
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_presentation_parameters_init(&active) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_presentation_parameters(device, &active) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_supports_capability(
            device, CNA_GRAPHICS_CAPABILITY_THREE_D, &three_d) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->saw_depth24 = active.depth_stencil_format == CNA_DEPTH_FORMAT_DEPTH24;
    state->three_d = three_d != CNA_FALSE;
    /* What an XNA game does in Draw: Clear(Color.CornflowerBlue, 1.0f). */
    state->clear_result = cna_graphics_device_clear_color_depth(device, color, 1.0F);
    return cna_game_request_exit(game);
}

int main(void)
{
    DepthState state;
    CNA_GameCallbacks callbacks;
    CNA_GameCreateInfo create_info;
    static const char Title[] = "C API game without a GraphicsDeviceManager";
    CNA_Handle game = CNA_INVALID_HANDLE;
    memset(&state, 0, sizeof(state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.update = on_update;
    callbacks.context = &state;
    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title.data = Title;
    create_info.window_title.byte_length = sizeof(Title) - 1U;
    create_info.callbacks = &callbacks;
    if (!CNA_TEST_STAGE(cna_game_create(&create_info, &game) == CNA_RESULT_SUCCESS) ||
        !CNA_TEST_STAGE(cna_game_run(game) == CNA_RESULT_SUCCESS) ||
        !CNA_TEST_STAGE(state.frames >= 1) ||
        /* the manager's XNA default, not PresentationParameters' None */
        !CNA_TEST_STAGE(state.saw_depth24) ||
        /* a 2D-only renderer has no real depth plane and may refuse; a 3D one must not */
        !CNA_TEST_STAGE(!state.three_d || state.clear_result == CNA_RESULT_SUCCESS) ||
        !CNA_TEST_STAGE(cna_game_destroy(game) == CNA_RESULT_SUCCESS)) {
        return CNA_TEST_FAIL(1);
    }
    return 0;
}
