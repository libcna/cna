// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>
#include <threads.h>

typedef struct LifecycleState {
    int load_count;
    int update_count;
    int draw_count;
    int unload_count;
    int exit_count;
    int saw_time;
    int lifecycle_stage;
} LifecycleState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result result;
} WrongThreadState;

static int set_title_on_wrong_thread(void* context)
{
    static const char title[] = "wrong thread";
    WrongThreadState* const state = (WrongThreadState*)context;
    state->result = cna_game_set_window_title(
        state->game,
        (CNA_StringView){title, sizeof(title) - 1U});
    return 0;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    if (game_time != 0 || out_error == 0 || out_error->struct_size != sizeof(CNA_CallbackError)) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 0 && state->lifecycle_stage != 5) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->load_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_update(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time == 0 || game_time->elapsed_game_time_ticks <= 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage == 1) {
        ++state->lifecycle_stage;
    } else if (state->lifecycle_stage != 2) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->update_count;
    state->saw_time = 1;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_draw(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time == 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 2) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (cna_game_clear(game, (CNA_Color){UINT8_C(10), UINT8_C(20), UINT8_C(30), UINT8_C(255)}) !=
        CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->draw_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_update_and_exit(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)out_error;
    if (game_time == 0 || state->lifecycle_stage != 6 ||
        cna_game_request_exit(game) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->update_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_unload(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 4 && state->lifecycle_stage != 8) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->unload_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_exit(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 3 && state->lifecycle_stage != 7) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->exit_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_failing_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    static const char message[] = "test callback failure";
    (void)game;
    (void)game_time;
    (void)context;
    out_error->message.data = message;
    out_error->message.byte_length = sizeof(message) - 1U;
    return CNA_RESULT_INVALID_STATE;
}

static CNA_GameCreateInfo make_create_info(
    const CNA_GameCallbacks* callbacks,
    const char* title,
    uint64_t title_bytes)
{
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, title_bytes},
        callbacks
    };
    return create_info;
}

int main(void)
{
    LifecycleState state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks),
        UINT32_C(1),
        on_load,
        on_update,
        on_draw,
        on_unload,
        on_exit,
        &state
    };
    static const char initial_title[] = "C API lifecycle";
    CNA_GameCreateInfo create_info = make_create_info(
        &callbacks,
        initial_title,
        sizeof(initial_title) - 1U);
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS || game == CNA_INVALID_HANDLE) {
        return 1;
    }
    WrongThreadState wrong_thread_state = {game, CNA_RESULT_SUCCESS};
    thrd_t wrong_thread;
    int wrong_thread_return = 0;
    if (thrd_create(&wrong_thread, set_title_on_wrong_thread, &wrong_thread_state) != thrd_success ||
        thrd_join(wrong_thread, &wrong_thread_return) != thrd_success ||
        wrong_thread_return != 0 || wrong_thread_state.result != CNA_RESULT_THREAD) {
        return 2;
    }
    if (cna_game_set_window_title(game, (CNA_StringView){"C API title", 11U}) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        state.load_count != 1 || state.update_count < 1 || state.draw_count != 1 ||
        state.saw_time != 1) {
        return 3;
    }
    if (cna_game_request_exit(game) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS ||
        state.unload_count != 1 || state.exit_count != 1 ||
        state.lifecycle_stage != 5 ||
        cna_game_run_one_frame(game) != CNA_RESULT_INVALID_HANDLE) {
        return 4;
    }

    callbacks.update = on_update_and_exit;
    callbacks.draw = 0;
    callbacks.unload_content = on_unload;
    callbacks.exiting = on_exit;
    create_info = make_create_info(&callbacks, "", 0U);
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run(game) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS ||
        state.unload_count != 2 || state.exit_count != 2 || state.lifecycle_stage != 9) {
        return 5;
    }

    callbacks.load_content = on_failing_load;
    callbacks.update = 0;
    callbacks.draw = 0;
    callbacks.unload_content = 0;
    callbacks.exiting = 0;
    callbacks.context = 0;
    create_info = make_create_info(&callbacks, "", 0U);
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_CALLBACK) {
        return 6;
    }

    CNA_ErrorInfo error_info = {sizeof(CNA_ErrorInfo), UINT32_C(1), 0U, 0U, 0U};
    uint64_t message_bytes = 0U;
    char message[22] = {0};
    if (cna_error_get_last_info(&error_info) != CNA_RESULT_SUCCESS ||
        error_info.result != CNA_RESULT_CALLBACK ||
        error_info.category != CNA_ERROR_CATEGORY_CALLBACK ||
        error_info.message_byte_length != 21U ||
        cna_error_copy_last_message(message, sizeof(message) - 1U, &message_bytes) !=
            CNA_RESULT_SUCCESS ||
        message_bytes != 21U || memcmp(message, "test callback failure", 21U) != 0 ||
        cna_game_destroy(game) != CNA_RESULT_CALLBACK) {
        return 7;
    }

    return 0;
}
