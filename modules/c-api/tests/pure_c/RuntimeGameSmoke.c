// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>

typedef struct GameSmokeState {
    int validated;
    int initialize_calls;
    int begin_run_calls;
    int end_run_calls;
    int begin_draw_calls;
    int end_draw_calls;
    int draw_calls;
    CNA_Bool suppress_next_draw;
} GameSmokeState;

typedef struct EventState {
    int calls;
} EventState;

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static void on_game_event(void* const context)
{
    ++((EventState*)context)->calls;
}

static CNA_Result on_initialize(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    ++((GameSmokeState*)context)->initialize_calls;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_begin_run(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    ++((GameSmokeState*)context)->begin_run_calls;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_end_run(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    ++((GameSmokeState*)context)->end_run_calls;
    return CNA_RESULT_SUCCESS;
}

/* The canonical hook answers whether the frame draws at all, so this one does too. */
static CNA_Result on_begin_draw(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_Bool* const out_should_draw,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)out_error;
    GameSmokeState* const state = (GameSmokeState*)context;
    ++state->begin_draw_calls;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->suppress_next_draw == CNA_TRUE) {
        *out_should_draw = CNA_FALSE;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_end_draw(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    ++((GameSmokeState*)context)->end_draw_calls;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_draw(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    ++((GameSmokeState*)context)->draw_calls;
    return CNA_RESULT_SUCCESS;
}

static int validate_properties(const CNA_Handle game)
{
    CNA_Bool flag = UINT8_C(9);
    int64_t ticks = INT64_C(-1);
    double value = -1.0;
    uint64_t bytes = UINT64_C(9);
    char text[128];

    if (cna_game_get_is_active(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_game_get_is_active(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_game_get_is_mouse_visible(game, &flag) != CNA_RESULT_SUCCESS ||
        cna_game_set_is_mouse_visible(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_game_get_is_mouse_visible(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_game_set_is_mouse_visible(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_game_get_is_mouse_visible(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE) {
        return 0;
    }
    if (cna_game_get_is_fixed_time_step(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_game_set_is_fixed_time_step(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_game_get_is_fixed_time_step(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_game_set_is_fixed_time_step(game, CNA_TRUE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The target step round-trips and a step that is not positive is refused. */
    if (cna_game_get_target_elapsed_time_ticks(game, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(166667) ||
        cna_game_set_target_elapsed_time_ticks(game, INT64_C(333334)) != CNA_RESULT_SUCCESS ||
        cna_game_get_target_elapsed_time_ticks(game, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(333334) ||
        cna_game_set_target_elapsed_time_ticks(game, INT64_C(0)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_set_target_elapsed_time_ticks(game, INT64_C(-1)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The frame rate and frame time are derived from that step, so they follow it. */
    if (cna_game_get_target_fps_ext(game, &value) != CNA_RESULT_SUCCESS ||
        value < 29.0 || value > 31.0 ||
        cna_game_get_target_ms_frame_time_ext(game, &value) != CNA_RESULT_SUCCESS ||
        value < 33.0 || value > 34.0 ||
        cna_game_fps_to_milliseconds_per_frame_ext(60, &value) != CNA_RESULT_SUCCESS ||
        value < 16.6 || value > 16.7 ||
        cna_game_fps_to_milliseconds_per_frame_ext(60, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_game_set_target_elapsed_time_ticks(game, INT64_C(166667)) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_game_get_inactive_sleep_time_ticks(game, &ticks) != CNA_RESULT_SUCCESS ||
        ticks < INT64_C(0) ||
        cna_game_set_inactive_sleep_time_ticks(game, INT64_C(100000)) != CNA_RESULT_SUCCESS ||
        cna_game_get_inactive_sleep_time_ticks(game, &ticks) != CNA_RESULT_SUCCESS ||
        ticks != INT64_C(100000) ||
        cna_game_set_inactive_sleep_time_ticks(game, INT64_C(-1)) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_game_get_run_application_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        cna_game_set_run_application_ext(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_game_get_run_application_ext(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_get_type_name_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_game_copy_type_name(game, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Game") != 0 ||
        cna_game_copy_type_name(game, text, UINT64_C(2), &bytes) != CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    /* Forgetting the accumulated time is an ordinary request from inside a frame; stepping a frame
       is not, and is refused for the same reason running the game from inside a callback is. */
    return cna_game_reset_elapsed_time(game) == CNA_RESULT_SUCCESS &&
        cna_game_tick(game) == CNA_RESULT_INVALID_STATE;
}

static int validate_launch_parameters(const CNA_Handle game)
{
    CNA_StringView arguments[3];
    CNA_Bool present = UINT8_C(9);
    uint64_t count = UINT64_C(99);
    uint64_t bytes = UINT64_C(9);
    char text[128];

    arguments[0] = view("--width:800");
    arguments[1] = view("/height:480");
    arguments[2] = view("nope");

    if (cna_game_launch_parameters_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_game_launch_parameters_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_parse_ext(game, arguments, UINT64_C(3)) != CNA_RESULT_SUCCESS ||
        cna_game_launch_parameters_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(2)) {
        return 0;
    }
    /* The canonical parser splits on a colon and trims leading flag markers, so both spellings
       above become names; the third argument has no colon and is skipped in silence. */
    memset(text, 0, sizeof(text));
    if (cna_game_launch_parameters_contains_key(game, view("width"), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_game_launch_parameters_contains_key(game, view("height"), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_game_launch_parameters_contains_key(game, view("nope"), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_FALSE ||
        cna_game_launch_parameters_get_value_size(game, view("width"), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(3) ||
        cna_game_launch_parameters_copy_value(
            game, view("width"), text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "800") != 0) {
        return 0;
    }
    /* An absent parameter is refused rather than answering an empty value. */
    if (cna_game_launch_parameters_get_value_size(game, view("depth"), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_copy_value(
            game, view("depth"), text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_add(game, view("depth"), view("24")) != CNA_RESULT_SUCCESS ||
        cna_game_launch_parameters_contains_key(game, view("depth"), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_TRUE ||
        cna_game_launch_parameters_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        count != UINT64_C(3)) {
        return 0;
    }
    /* An empty argument list leaves the game with no parameters rather than re-reading the command
       line, which is what makes this route usable at all in a test. */
    return cna_game_launch_parameters_parse_ext(game, 0, UINT64_C(0)) == CNA_RESULT_SUCCESS &&
        cna_game_launch_parameters_get_count(game, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(0) &&
        cna_game_launch_parameters_parse_ext(game, 0, UINT64_C(1)) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_title(const CNA_Handle game)
{
    uint64_t bytes = UINT64_C(9);
    uint8_t buffer[64];
    char path[512];
    FILE* file = 0;

    memset(path, 0, sizeof(path));
    if (cna_title_location_get_path_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(path) ||
        cna_title_location_copy_path(game, path, (uint64_t)sizeof(path), &bytes) !=
            CNA_RESULT_SUCCESS ||
        cna_title_location_get_path_size(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Pointing the title at this process's working directory is what lets the read below find a
       file the test itself wrote. The path is process-wide, exactly as canonically. */
    if (cna_title_location_set_path_ext(game, view(".")) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    file = fopen("cna-c-api-title.bin", "wb");
    if (file == 0) {
        return 0;
    }
    if (fwrite("CNA-TITLE", 1U, 9U, file) != 9U) {
        fclose(file);
        return 0;
    }
    fclose(file);

    memset(buffer, 0, sizeof(buffer));
    if (cna_title_container_read_ext(
            game, view("cna-c-api-title.bin"), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(9) || memcmp(buffer, "CNA-TITLE", 9U) != 0) {
        remove("cna-c-api-title.bin");
        return 0;
    }
    /* A buffer one byte short refuses without writing anything, and a missing file is an I/O
       failure rather than an empty read. */
    memset(buffer, 0, sizeof(buffer));
    if (cna_title_container_read_ext(
            game, view("cna-c-api-title.bin"), buffer, UINT64_C(8), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        buffer[0] != 0U || bytes != UINT64_C(9) ||
        cna_title_container_read_ext(
            game, view("cna-c-api-missing.bin"), buffer, (uint64_t)sizeof(buffer), &bytes) !=
            CNA_RESULT_IO) {
        remove("cna-c-api-title.bin");
        return 0;
    }
    remove("cna-c-api-title.bin");
    return cna_title_location_set_path_ext(game, view(path)) == CNA_RESULT_SUCCESS &&
        cna_framework_dispatcher_update(game) == CNA_RESULT_SUCCESS;
}

static int validate_events(const CNA_Handle game, EventState* const exiting)
{
    CNA_GameEventRegistrationHandle activated = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle deactivated = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle disposed = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle exiting_registration = CNA_INVALID_HANDLE;
    EventState unused = {0};

    if (cna_game_subscribe(game, CNA_GAME_EVENT_ACTIVATED, on_game_event, &unused, &activated) !=
            CNA_RESULT_SUCCESS ||
        cna_game_subscribe(
            game, CNA_GAME_EVENT_DEACTIVATED, on_game_event, &unused, &deactivated) !=
            CNA_RESULT_SUCCESS ||
        cna_game_subscribe(game, CNA_GAME_EVENT_DISPOSED, on_game_event, &unused, &disposed) !=
            CNA_RESULT_SUCCESS ||
        cna_game_subscribe(
            game, CNA_GAME_EVENT_EXITING, on_game_event, exiting, &exiting_registration) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A refused subscription clears its output first, so these take a handle of their own. */
    if (cna_game_subscribe(
            game, CNA_GAME_EVENT_MAXIMUM + UINT32_C(1), on_game_event, &unused, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_game_subscribe(game, CNA_GAME_EVENT_ACTIVATED, 0, &unused, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_subscribe(game, CNA_GAME_EVENT_ACTIVATED, on_game_event, &unused, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Focus events cannot be provoked in a headless session, so what is proved here is that the
       subscriptions attach and detach; the exiting one is fired for real below. */
    if (cna_game_unsubscribe(activated) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(activated) != CNA_RESULT_INVALID_HANDLE ||
        cna_game_unsubscribe(deactivated) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(disposed) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_game_unsubscribe(exiting_registration) == CNA_RESULT_SUCCESS ||
        exiting->calls >= 0;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    GameSmokeState* const state = (GameSmokeState*)context;
    EventState exiting = {0};
    if (game_time == 0 || !validate_properties(game) || !validate_launch_parameters(game) ||
        !validate_title(game) || !validate_events(game, &exiting)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    GameSmokeState smoke_state;
    CNA_GameCallbacks callbacks;
    CNA_GameFrameHooks hooks;
    CNA_GameCreateInfo create_info;
    CNA_Handle game = CNA_INVALID_HANDLE;

    memset(&smoke_state, 0, sizeof(smoke_state));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.update = on_update;
    callbacks.draw = on_draw;
    callbacks.context = &smoke_state;

    memset(&hooks, 0, sizeof(hooks));
    hooks.struct_size = (uint32_t)sizeof(hooks);
    hooks.struct_version = UINT32_C(1);
    hooks.initialize = on_initialize;
    hooks.begin_run = on_begin_run;
    hooks.end_run = on_end_run;
    hooks.begin_draw = on_begin_draw;
    hooks.end_draw = on_end_draw;
    hooks.context = &smoke_state;

    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title = view("C API game smoke");
    create_info.callbacks = &callbacks;

    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return 1;
    }
    /* The frame hooks are a second table installed after creation, so the published callback table
       every existing consumer already writes stays exactly as it was. */
    {
        CNA_GameFrameHooks broken = hooks;
        broken.struct_version = UINT32_C(0);
        if (cna_game_set_frame_hooks_ext(game, &broken) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_game_set_frame_hooks_ext(game, &hooks) != CNA_RESULT_SUCCESS) {
            return 1;
        }
    }
    if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || smoke_state.validated != 1) {
        return 1;
    }
    /* The frame hooks the grown callback table adds all ran, and drawing happened between them. */
    if (smoke_state.initialize_calls != 1 || smoke_state.begin_draw_calls < 1 ||
        smoke_state.end_draw_calls < 1 || smoke_state.draw_calls < 1) {
        return 2;
    }
    /* Suppressing the draw skips exactly one frame's drawing. */
    {
        const int draws_before = smoke_state.draw_calls;
        if (cna_game_suppress_draw(game) != CNA_RESULT_SUCCESS ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before + 1) {
            return 3;
        }
    }
    /* Refusing to draw from the pre-draw hook skips the draw callback and its end hook. */
    {
        const int draws_before = smoke_state.draw_calls;
        smoke_state.suppress_next_draw = CNA_TRUE;
        if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before) {
            return 7;
        }
        smoke_state.suppress_next_draw = CNA_FALSE;
    }
    /* A frame step outside any callback is an ordinary request. */
    if (cna_game_tick(game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    /* Removing the hooks stops every one of them. */
    {
        const int draws_before = smoke_state.begin_draw_calls;
        if (cna_game_set_frame_hooks_ext(game, 0) != CNA_RESULT_SUCCESS ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.begin_draw_calls != draws_before) {
            return 5;
        }
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 6;
}
