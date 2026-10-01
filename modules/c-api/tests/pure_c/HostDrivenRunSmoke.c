// SPDX-License-Identifier: MS-PL
// CBIND-134: cna_game_run_frame_ext, the run a host drives one frame at a time.

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdio.h>
#include <string.h>

typedef struct RunState {
    /* One letter per delivered event, in delivery order. */
    char order[64];
    int order_length;
    int updates;
    int exit_after_updates;
    int fail_update;
} RunState;

static void record(RunState* const state, const char event)
{
    if (state->order_length + 1 < (int)sizeof(state->order)) {
        state->order[state->order_length++] = event;
        state->order[state->order_length] = '\0';
    }
}

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

#define RECORDING_CALLBACK(name, letter) \
    static CNA_Result name(const CNA_Handle game, const CNA_GameTime* const game_time, \
                           void* const context, CNA_CallbackError* const out_error) \
    { \
        (void)game; \
        (void)game_time; \
        (void)out_error; \
        record((RunState*)context, letter); \
        return CNA_RESULT_SUCCESS; \
    }

RECORDING_CALLBACK(on_initialize, 'i')
RECORDING_CALLBACK(on_load_content, 'l')
RECORDING_CALLBACK(on_begin_run, 'b')
RECORDING_CALLBACK(on_draw, 'd')
RECORDING_CALLBACK(on_exiting, 'x')
RECORDING_CALLBACK(on_end_run, 'e')

static CNA_Result on_update(const CNA_Handle game, const CNA_GameTime* const game_time,
                            void* const context, CNA_CallbackError* const out_error)
{
    RunState* const state = (RunState*)context;
    (void)game_time;
    record(state, 'u');
    ++state->updates;
    if (state->fail_update) {
        out_error->message = view("host-driven run smoke: update refused");
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->updates == state->exit_after_updates &&
        cna_game_request_exit(game) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Handle create_game(RunState* const state, CNA_GameCallbacks* const callbacks,
                              CNA_GameFrameHooks* const hooks)
{
    CNA_GameCreateInfo create_info;
    CNA_Handle game = CNA_INVALID_HANDLE;

    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->struct_size = (uint32_t)sizeof(*callbacks);
    callbacks->struct_version = UINT32_C(1);
    callbacks->load_content = on_load_content;
    callbacks->update = on_update;
    callbacks->draw = on_draw;
    callbacks->exiting = on_exiting;
    callbacks->context = state;

    memset(hooks, 0, sizeof(*hooks));
    hooks->struct_size = (uint32_t)sizeof(*hooks);
    hooks->struct_version = UINT32_C(1);
    hooks->initialize = on_initialize;
    hooks->begin_run = on_begin_run;
    hooks->end_run = on_end_run;
    hooks->context = state;

    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_FALSE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title = view("C API host-driven run smoke");
    create_info.callbacks = callbacks;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_set_frame_hooks_ext(game, hooks) != CNA_RESULT_SUCCESS) {
        return CNA_INVALID_HANDLE;
    }
    return game;
}

int main(void)
{
    RunState state;
    CNA_GameCallbacks callbacks;
    CNA_GameFrameHooks hooks;
    CNA_Bool running = CNA_FALSE;
    int frames = 0;

    memset(&state, 0, sizeof(state));
    state.exit_after_updates = 3;
    CNA_Handle game = create_game(&state, &callbacks, &hooks);
    if (game == CNA_INVALID_HANDLE) {
        return CNA_TEST_FAIL(1);
    }
    if (cna_game_run_frame_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_TEST_FAIL(2);
    }

    /* The run begins on the first call, a frame per call, and ends on the call that finds the game
       exited -- delivering what cna_game_run delivers, in its order. */
    do {
        if (cna_game_run_frame_ext(game, &running) != CNA_RESULT_SUCCESS || ++frames > 100) {
            return CNA_TEST_FAIL(3);
        }
    } while (running == CNA_TRUE);
    if (strncmp(state.order, "ilbud", 5U) != 0 || state.updates != 3 ||
        state.order_length < 2 || strcmp(state.order + state.order_length - 2, "xe") != 0) {
        fprintf(stderr, "order: %s\n", state.order);
        return CNA_TEST_FAIL(4);
    }

    /* An ended run stays ended: later calls report it and deliver nothing. */
    {
        const int delivered = state.order_length;
        if (cna_game_run_frame_ext(game, &running) != CNA_RESULT_SUCCESS || running != CNA_FALSE ||
            state.order_length != delivered) {
            return CNA_TEST_FAIL(5);
        }
    }
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(6);
    }

    /* A failed callback ends the run in the same call and reports the failure, as cna_game_run
       returns it. */
    memset(&state, 0, sizeof(state));
    state.fail_update = 1;
    game = create_game(&state, &callbacks, &hooks);
    if (game == CNA_INVALID_HANDLE ||
        cna_game_run_frame_ext(game, &running) != CNA_RESULT_CALLBACK || running != CNA_FALSE ||
        cna_game_run_frame_ext(game, &running) != CNA_RESULT_CALLBACK || running != CNA_FALSE) {
        return CNA_TEST_FAIL(7);
    }
    /* The failure stays the game's: destroying it releases the handle and reports it again, as
       cna_game_destroy documents. */
    return cna_game_destroy(game) == CNA_RESULT_CALLBACK ? 0 : CNA_TEST_FAIL(8);
}
