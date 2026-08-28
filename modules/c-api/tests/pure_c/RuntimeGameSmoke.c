// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdio.h>
#include <string.h>

typedef struct GameSmokeState {
    int validated;
    /* CBIND-063: the delivered lifecycle order, one letter per event. Counting calls is what every
       assertion here used to do, and counting cannot see an ordering defect at all -- which is how
       initialize and load_content ran in the wrong order for as long as they did. */
    char order[32];
    int order_length;
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

static void record(GameSmokeState* const state, const char event)
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
    record((GameSmokeState*)context, 'i');
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_load_content(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)game;
    (void)game_time;
    (void)out_error;
    record((GameSmokeState*)context, 'l');
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
    /* CBIND-054: the keys are enumerable, which is what turns three keyed accessors into a map a
       caller can actually materialize. The order is by name ascending and deliberately not the
       canonical hash map's own, so "depth" comes first here however the map happens to be laid
       out; a single insertion may rehash and reorder that container, and an index into it would
       mean nothing between calls. */
    /* The copy routes write no terminator, so each name is read into a freshly cleared buffer --
       otherwise a shorter name would inherit the tail of the longer one before it. */
    memset(text, 0, sizeof(text));
    if (cna_game_launch_parameters_get_key_size(game, UINT64_C(0), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen("depth") ||
        cna_game_launch_parameters_copy_key(
            game, UINT64_C(0), text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "depth") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_launch_parameters_copy_key(
            game, UINT64_C(1), text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "height") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_launch_parameters_copy_key(
            game, UINT64_C(2), text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "width") != 0) {
        return 0;
    }
    /* Every name the enumeration answers resolves through the keyed accessor, which is the only
       property that makes the pair usable together. */
    if (cna_game_launch_parameters_contains_key(game, view(text), &present) !=
            CNA_RESULT_SUCCESS ||
        present != CNA_TRUE) {
        return 0;
    }
    /* An index at or above the count is refused, the two-call size/copy contract holds, and a
       null output is refused before anything is read. */
    if (cna_game_launch_parameters_get_key_size(game, UINT64_C(3), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_copy_key(
            game, UINT64_C(3), text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_get_key_size(game, UINT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_launch_parameters_copy_key(game, UINT64_C(0), text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != (uint64_t)strlen("depth")) {
        return 0;
    }

    /* An empty argument list leaves the game with no parameters rather than re-reading the command
       line, which is what makes this route usable at all in a test. */
    return cna_game_launch_parameters_parse_ext(game, 0, UINT64_C(0)) == CNA_RESULT_SUCCESS &&
        cna_game_launch_parameters_get_count(game, &count) == CNA_RESULT_SUCCESS &&
        count == UINT64_C(0) &&
        /* With no parameters at all, index zero is out of range rather than empty. */
        cna_game_launch_parameters_get_key_size(game, UINT64_C(0), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
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

/* A window state change is a request to the platform, and a platform that cannot honour it says so:
   a headless video driver refuses to minimize a window it never really showed, which is a platform
   failure rather than a fault in the call. Both answers are correct, so both are accepted here. */
static int accepted_or_refused_by_platform(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_PLATFORM;
}

/* A game owns exactly one window, so every window route addresses the game handle -- the fourth time
   this ABI answers the one-per-game question the same way. */
static int validate_window(const CNA_Handle game)
{
    CNA_GameEventRegistrationHandle client_size = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle orientation = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle screen_name = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
    CNA_DisplayOrientation display_orientation = UINT32_C(99);
    CNA_Rectangle bounds;
    EventState events = {0};
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    uint64_t native = UINT64_C(1);
    CNA_NativeWindowHandle native_window;
    CNA_NativeWindowHandle uninitialized;
    char text[256];
    char device_name[256];

    if (cna_game_window_get_allow_user_resizing(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        !accepted_or_refused_by_platform(
            cna_game_window_set_allow_user_resizing(game, CNA_TRUE)) ||
        !accepted_or_refused_by_platform(
            cna_game_window_set_allow_user_resizing(game, CNA_FALSE)) ||
        cna_game_window_get_allow_user_resizing(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    memset(&bounds, 9, sizeof(bounds));
    if (cna_game_window_get_client_bounds(game, &bounds) != CNA_RESULT_SUCCESS ||
        bounds.width < 0 || bounds.height < 0 ||
        cna_game_window_get_current_orientation(game, &display_orientation) != CNA_RESULT_SUCCESS ||
        cna_game_window_get_native_handle_ext(game, &native) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The structure must be initialized for this ABI version; an uninitialized one is refused
       rather than filled in, so a consumer built against a later header cannot be handed fields it
       does not know how to read. */
    memset(&uninitialized, 0, sizeof(uninitialized));
    if (cna_game_window_get_native_window_ext(game, &uninitialized) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_window_get_native_window_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_native_window_handle_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_native_window_handle_init(&native_window) != CNA_RESULT_SUCCESS ||
        native_window.system != CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN ||
        native_window.display != 0 || native_window.window != 0 ||
        native_window.surface != 0 || native_window.window_id != UINT64_C(0)) {
        return 0;
    }
    /* No native window is an answer, not a failure, so every platform this runs on succeeds here
       and the system identity is what separates the cases. */
    if (cna_game_window_get_native_window_ext(game, &native_window) != CNA_RESULT_SUCCESS ||
        native_window.system > CNA_NATIVE_WINDOW_SYSTEM_MAXIMUM) {
        return 0;
    }
    /* CBIND-072. The two routes answer different things -- the header claimed they answered the
       same pointer, and they do not -- but they are not unrelated: both come from the platform
       window, so a reported native windowing system implies the round-trip token exists. The
       converse does not hold, and deliberately is not asserted: a driver with no native window
       system still creates a platform window, which is exactly the case a dummy video driver
       produces. */
    if (native_window.system != CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN &&
        native_window.system != CNA_NATIVE_WINDOW_SYSTEM_HEADLESS &&
        native_window.system != CNA_NATIVE_WINDOW_SYSTEM_TERMINAL &&
        native == UINT64_C(0)) {
        return 0;
    }

    /* Which fields carry anything is decided by the system identity, and a caller that reads one
       without checking gets a null it cannot distinguish from a real value. These are the same
       per-system invariants the canonical accessors enforce. */
    switch (native_window.system) {
        case CNA_NATIVE_WINDOW_SYSTEM_X11:
            /* An XID is an integer resource id, so it lives in its own field and `window` stays
               null even though the window is perfectly real. */
            if (native_window.display == 0 || native_window.window != 0 ||
                native_window.surface != 0 || native_window.window_id == UINT64_C(0)) {
                return 0;
            }
            break;
        case CNA_NATIVE_WINDOW_SYSTEM_WAYLAND:
            if (native_window.display == 0 || native_window.surface == 0 ||
                native_window.window_id != UINT64_C(0)) {
                return 0;
            }
            break;
        case CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN:
        case CNA_NATIVE_WINDOW_SYSTEM_HEADLESS:
        case CNA_NATIVE_WINDOW_SYSTEM_TERMINAL:
            if (native_window.display != 0 || native_window.window != 0 ||
                native_window.surface != 0 || native_window.window_id != UINT64_C(0)) {
                return 0;
            }
            break;
        default:
            /* Win32, Cocoa and Android all answer through `window`; Web answers through none of
               them, because its target is a canvas the host page selects. */
            if (native_window.window_id != UINT64_C(0)) {
                return 0;
            }
            break;
    }
    /* The title round-trips through the route this ABI has had since its first release. */
    memset(text, 0, sizeof(text));
    if (cna_game_set_window_title(game, view("C API window smoke")) != CNA_RESULT_SUCCESS ||
        cna_game_window_get_title_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(18) ||
        cna_game_window_copy_title(game, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "C API window smoke") != 0 ||
        cna_game_window_copy_title(game, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    memset(device_name, 0, sizeof(device_name));
    if (cna_game_window_get_screen_device_name_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(device_name) ||
        cna_game_window_copy_screen_device_name(
            game, device_name, (uint64_t)sizeof(device_name), &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_game_window_get_type_name_size(game, &bytes) != CNA_RESULT_SUCCESS ||
        cna_game_window_copy_type_name(game, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.GameWindow") != 0) {
        return 0;
    }
    /* A session with no native window accepts every state request and does nothing, which is the
       canonical behavior rather than a failure invented here. */
    if (cna_game_window_get_is_borderless_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        !accepted_or_refused_by_platform(cna_game_window_set_is_borderless_ext(game, CNA_TRUE)) ||
        !accepted_or_refused_by_platform(cna_game_window_set_is_borderless_ext(game, CNA_FALSE)) ||
        !accepted_or_refused_by_platform(cna_game_window_minimize_ext(game)) ||
        !accepted_or_refused_by_platform(cna_game_window_restore_ext(game))) {
        return 0;
    }
    /* The canonical name-only overload is the sized one with the current client size, so a
       non-positive size means "keep it" rather than being refused. */
    if (cna_game_window_begin_screen_device_change(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        !accepted_or_refused_by_platform(
            cna_game_window_end_screen_device_change(game, view(device_name), 0, 0)) ||
        cna_game_window_begin_screen_device_change(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        !accepted_or_refused_by_platform(
            cna_game_window_end_screen_device_change(game, view(device_name), 640, 480)) ||
        !accepted_or_refused_by_platform(
            cna_game_window_end_screen_device_change(game, view(device_name), 0, 0))) {
        return 0;
    }
    if (cna_game_window_subscribe(
            game,
            CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED,
            on_game_event,
            &events,
            &client_size) != CNA_RESULT_SUCCESS ||
        cna_game_window_subscribe(
            game,
            CNA_GAME_WINDOW_EVENT_ORIENTATION_CHANGED,
            on_game_event,
            &events,
            &orientation) != CNA_RESULT_SUCCESS ||
        cna_game_window_subscribe(
            game,
            CNA_GAME_WINDOW_EVENT_SCREEN_DEVICE_NAME_CHANGED,
            on_game_event,
            &events,
            &screen_name) != CNA_RESULT_SUCCESS ||
        cna_game_window_subscribe(
            game,
            CNA_GAME_WINDOW_EVENT_MAXIMUM + UINT32_C(1),
            on_game_event,
            &events,
            &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_game_window_subscribe(
            game, CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED, 0, &events, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A window registration is released by the game's own unsubscribe route: they are the same kind
       of thing and one route releases both. */
    return cna_game_unsubscribe(client_size) == CNA_RESULT_SUCCESS &&
        cna_game_unsubscribe(orientation) == CNA_RESULT_SUCCESS &&
        cna_game_unsubscribe(screen_name) == CNA_RESULT_SUCCESS &&
        cna_game_unsubscribe(screen_name) == CNA_RESULT_INVALID_HANDLE;
}

/* A game owns its content manager as a value member, so C borrows it: one handle, never destroyed,
   released with the game. */
static int validate_content_manager(const CNA_Handle game)
{
    CNA_Handle borrowed = CNA_INVALID_HANDLE;
    CNA_Handle again = CNA_INVALID_HANDLE;
    uint64_t bytes = UINT64_C(9);
    char text[256];

    if (cna_game_get_content_manager_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_get_content_manager_ext(game, &borrowed) != CNA_RESULT_SUCCESS ||
        borrowed == CNA_INVALID_HANDLE ||
        cna_game_get_content_manager_ext(game, &again) != CNA_RESULT_SUCCESS ||
        again != borrowed) {
        return 0;
    }
    /* Every other content-manager route accepts the borrowed handle, so the game's own root
       directory is readable and settable through it. */
    memset(text, 0, sizeof(text));
    if (cna_content_manager_get_root_directory_size(borrowed, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_content_manager_copy_root_directory(borrowed, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        cna_content_manager_set_root_directory(borrowed, view("cna-c-api-content")) !=
            CNA_RESULT_SUCCESS ||
        cna_content_manager_copy_root_directory(borrowed, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "cna-c-api-content") != 0) {
        return 0;
    }
    /* The borrowed manager cannot be destroyed: it belongs to the game. */
    if (cna_content_manager_destroy(borrowed) != CNA_RESULT_INVALID_STATE ||
        cna_content_manager_get_root_directory_size(borrowed, &bytes) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Replacing the game's manager copies, so the borrowed handle still addresses the game's own
       object and the source is untouched by later changes to it. */
    if (cna_game_set_content_manager_ext(game, borrowed) != CNA_RESULT_SUCCESS ||
        cna_game_get_content_manager_ext(game, &again) != CNA_RESULT_SUCCESS ||
        again == CNA_INVALID_HANDLE ||
        cna_content_manager_copy_root_directory(again, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "cna-c-api-content") != 0 ||
        cna_game_set_content_manager_ext(game, CNA_INVALID_HANDLE) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return cna_content_manager_set_root_directory(again, view("Content")) == CNA_RESULT_SUCCESS;
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
    record(state, 'u');
    if (game_time == 0 || !validate_properties(game) || !validate_launch_parameters(game) ||
        !validate_title(game) || !validate_events(game, &exiting) || !validate_window(game) ||
        !validate_content_manager(game)) {
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
    callbacks.load_content = on_load_content;
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
        return CNA_TEST_FAIL(1);
    }
    /* The frame hooks are a second table installed after creation, so the published callback table
       every existing consumer already writes stays exactly as it was. */
    {
        CNA_GameFrameHooks broken = hooks;
        broken.struct_version = UINT32_C(0);
        if (cna_game_set_frame_hooks_ext(game, &broken) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_game_set_frame_hooks_ext(game, &hooks) != CNA_RESULT_SUCCESS) {
            return CNA_TEST_FAIL(1);
        }
    }
    if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || smoke_state.validated != 1) {
        return CNA_TEST_FAIL(1);
    }
    /* The frame hooks the grown callback table adds all ran, and drawing happened between them. */
    if (smoke_state.initialize_calls != 1 || smoke_state.begin_draw_calls < 1 ||
        smoke_state.end_draw_calls < 1 || smoke_state.draw_calls < 1) {
        return CNA_TEST_FAIL(2);
    }
    /* CBIND-063: and they ran in the documented ORDER, which counting them cannot see.
       `initialize` is documented as running "while the game initializes, before content loads";
       the canonical Game::Initialize() ends by calling LoadContent(), so a hook invoked after the
       base delivered the two backwards. Most ported games touch fields in LoadContent that
       Initialize set, so the reversal breaks them at the first frame. */
    if (strncmp(smoke_state.order, "ilu", 3U) != 0) {
        return CNA_TEST_FAIL(2);
    }
    /* Suppressing the draw skips exactly one frame's drawing. */
    {
        const int draws_before = smoke_state.draw_calls;
        if (cna_game_suppress_draw(game) != CNA_RESULT_SUCCESS ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before + 1) {
            return CNA_TEST_FAIL(3);
        }
    }
    /* Refusing to draw from the pre-draw hook skips the draw callback and its end hook. */
    {
        const int draws_before = smoke_state.draw_calls;
        smoke_state.suppress_next_draw = CNA_TRUE;
        if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.draw_calls != draws_before) {
            return CNA_TEST_FAIL(7);
        }
        smoke_state.suppress_next_draw = CNA_FALSE;
    }
    /* A frame step outside any callback is an ordinary request. */
    if (cna_game_tick(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(4);
    }
    /* Removing the hooks stops every one of them. */
    {
        const int draws_before = smoke_state.begin_draw_calls;
        if (cna_game_set_frame_hooks_ext(game, 0) != CNA_RESULT_SUCCESS ||
            cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
            smoke_state.begin_draw_calls != draws_before) {
            return CNA_TEST_FAIL(5);
        }
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 6;
}
