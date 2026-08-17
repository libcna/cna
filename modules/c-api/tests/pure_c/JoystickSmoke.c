// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>
#include <threads.h>

typedef struct JoystickSmokeState {
    int validated;
} JoystickSmokeState;

typedef struct HotplugState {
    uint32_t connected_id;
    uint32_t disconnected_id;
    int connected_calls;
    int disconnected_calls;
} HotplugState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_JoystickStateHandle state;
    CNA_Result count_result;
    CNA_Result state_result;
} WrongThreadState;

static const CNA_StringView empty_text = {0, UINT64_C(0)};

static CNA_StringView text_of(const char* const value)
{
    CNA_StringView view;
    view.data = value;
    view.byte_length = (uint64_t)strlen(value);
    return view;
}

/* Pure value operations need no runtime at all, so they run before a game exists. */
static int validate_pure_info(void)
{
    CNA_JoystickInfo info;
    CNA_JoystickInfo other;
    CNA_Bool equal = UINT8_C(9);
    CNA_JoystickType type = CNA_JOYSTICK_TYPE_UNKNOWN;

    memset(&info, 9, sizeof(info));
    if (cna_joystick_info_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joystick_info_init(&info) != CNA_RESULT_SUCCESS ||
        info.struct_size != sizeof(CNA_JoystickInfo) || info.struct_version != UINT32_C(1) ||
        info.id != UINT32_C(0) || info.type != CNA_JOYSTICK_TYPE_UNKNOWN) {
        return 0;
    }

    other = info;
    if (cna_joystick_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* All three canonical fields participate: identifier, name and type. */
    other.id = UINT32_C(7);
    if (cna_joystick_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = info;
    other.type = CNA_JOYSTICK_TYPE_WHEEL;
    if (cna_joystick_info_equals(&info, empty_text, &other, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = info;
    if (cna_joystick_info_equals(&info, empty_text, &other, text_of("wheel"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_joystick_info_equals(&info, text_of("wheel"), &other, text_of("wheel"), &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }

    /* Every defined identity round-trips; the first undefined one is refused. */
    for (type = CNA_JOYSTICK_TYPE_UNKNOWN; type <= CNA_JOYSTICK_TYPE_MAXIMUM; ++type) {
        other = info;
        other.type = type;
        if (cna_joystick_info_equals(&other, empty_text, &other, empty_text, &equal) !=
                CNA_RESULT_SUCCESS ||
            equal != CNA_TRUE) {
            return 0;
        }
    }
    other = info;
    other.type = CNA_JOYSTICK_TYPE_MAXIMUM + UINT32_C(1);
    if (cna_joystick_info_equals(&other, empty_text, &info, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = info;
    other.struct_version = UINT32_C(2);
    if (cna_joystick_info_equals(&other, empty_text, &info, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    {
        /* Malformed UTF-8 is refused rather than compared byte-wise. */
        static const char malformed[2] = {(char)0xC3, (char)0x28};
        CNA_StringView bad;
        bad.data = malformed;
        bad.byte_length = UINT64_C(2);
        if (cna_joystick_info_equals(&info, bad, &info, empty_text, &equal) !=
            CNA_RESULT_ENCODING) {
            return 0;
        }
    }
    return cna_joystick_info_equals(0, empty_text, &info, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_info_equals(&info, empty_text, 0, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_info_equals(&info, empty_text, &info, empty_text, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int capabilities_are_default(const CNA_JoystickCapabilities* const capabilities)
{
    return capabilities->struct_size == sizeof(CNA_JoystickCapabilities) &&
        capabilities->struct_version == UINT32_C(1) &&
        capabilities->is_connected == CNA_FALSE && capabilities->axis_count == 0 &&
        capabilities->button_count == 0 && capabilities->hat_count == 0 &&
        capabilities->ball_count == 0 && capabilities->type == CNA_JOYSTICK_TYPE_UNKNOWN &&
        capabilities->power_state == CNA_POWER_STATE_UNKNOWN &&
        capabilities->power_percent == -1 && capabilities->reserved[0] == UINT8_C(0) &&
        capabilities->reserved[1] == UINT8_C(0) && capabilities->reserved[2] == UINT8_C(0);
}

static int validate_pure_capabilities(void)
{
    CNA_JoystickCapabilities capabilities;
    CNA_JoystickCapabilities other;
    CNA_Bool equal = UINT8_C(9);

    memset(&capabilities, 9, sizeof(capabilities));
    if (cna_joystick_capabilities_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joystick_capabilities_init(&capabilities) != CNA_RESULT_SUCCESS ||
        !capabilities_are_default(&capabilities)) {
        return 0;
    }

    other = capabilities;
    if (cna_joystick_capabilities_equals(
            &capabilities, empty_text, empty_text, &other, empty_text, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }
    /* Every canonical field separates, including the two that live outside the value. */
    other.ball_count = 3;
    if (cna_joystick_capabilities_equals(
            &capabilities, empty_text, empty_text, &other, empty_text, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = capabilities;
    other.power_percent = 50;
    if (cna_joystick_capabilities_equals(
            &capabilities, empty_text, empty_text, &other, empty_text, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE) {
        return 0;
    }
    other = capabilities;
    if (cna_joystick_capabilities_equals(
            &capabilities, text_of("stick"), empty_text, &other, empty_text, empty_text, &equal) !=
            CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_joystick_capabilities_equals(
            &capabilities, empty_text, text_of("03000000"), &other, empty_text, empty_text,
            &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_FALSE ||
        cna_joystick_capabilities_equals(
            &capabilities, text_of("stick"), text_of("03000000"), &other, text_of("stick"),
            text_of("03000000"), &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE) {
        return 0;
    }

    other = capabilities;
    other.power_state = CNA_POWER_STATE_CHARGED + UINT32_C(1);
    if (cna_joystick_capabilities_equals(
            &other, empty_text, empty_text, &capabilities, empty_text, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = capabilities;
    other.type = CNA_JOYSTICK_TYPE_MAXIMUM + UINT32_C(1);
    if (cna_joystick_capabilities_equals(
            &other, empty_text, empty_text, &capabilities, empty_text, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other = capabilities;
    other.struct_size = UINT32_C(4);
    if (cna_joystick_capabilities_equals(
            &other, empty_text, empty_text, &capabilities, empty_text, empty_text, &equal) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_joystick_capabilities_equals(
               0, empty_text, empty_text, &capabilities, empty_text, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_capabilities_equals(
            &capabilities, empty_text, empty_text, 0, empty_text, empty_text, &equal) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_capabilities_equals(
            &capabilities, empty_text, empty_text, &capabilities, empty_text, empty_text, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_captured_state(const CNA_JoystickStateHandle state)
{
    uint32_t axes = UINT32_C(9);
    uint32_t buttons = UINT32_C(9);
    uint32_t hats = UINT32_C(9);
    uint32_t balls = UINT32_C(9);
    uint64_t written = UINT64_C(9);
    int16_t axis_values[64];
    CNA_Bool button_values[256];
    CNA_JoystickHatPosition hat_values[16];
    CNA_Point ball_values[16];

    if (cna_joystick_state_get_axis_count(state, &axes) != CNA_RESULT_SUCCESS ||
        cna_joystick_state_get_button_count(state, &buttons) != CNA_RESULT_SUCCESS ||
        cna_joystick_state_get_hat_count(state, &hats) != CNA_RESULT_SUCCESS ||
        cna_joystick_state_get_ball_count(state, &balls) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (axes > (uint32_t)(sizeof(axis_values) / sizeof(axis_values[0])) ||
        buttons > (uint32_t)(sizeof(button_values) / sizeof(button_values[0])) ||
        hats > (uint32_t)(sizeof(hat_values) / sizeof(hat_values[0])) ||
        balls > (uint32_t)(sizeof(ball_values) / sizeof(ball_values[0]))) {
        return 0;
    }

    if (cna_joystick_state_copy_axes(
            state, axis_values, (uint64_t)(sizeof(axis_values) / sizeof(axis_values[0])),
            &written) != CNA_RESULT_SUCCESS ||
        written != (uint64_t)axes ||
        cna_joystick_state_copy_buttons(
            state, button_values,
            (uint64_t)(sizeof(button_values) / sizeof(button_values[0])), &written) !=
            CNA_RESULT_SUCCESS ||
        written != (uint64_t)buttons ||
        cna_joystick_state_copy_hats(
            state, hat_values, (uint64_t)(sizeof(hat_values) / sizeof(hat_values[0])), &written) !=
            CNA_RESULT_SUCCESS ||
        written != (uint64_t)hats ||
        cna_joystick_state_copy_balls(
            state, ball_values, (uint64_t)(sizeof(ball_values) / sizeof(ball_values[0])),
            &written) != CNA_RESULT_SUCCESS ||
        written != (uint64_t)balls) {
        return 0;
    }

    /* Whatever the device reports must be a defined hat identity. */
    for (uint32_t index = UINT32_C(0); index < hats; ++index) {
        if (hat_values[index] > CNA_JOYSTICK_HAT_POSITION_MAXIMUM) {
            return 0;
        }
    }
    for (uint32_t index = UINT32_C(0); index < buttons; ++index) {
        if (button_values[index] != CNA_FALSE && button_values[index] != CNA_TRUE) {
            return 0;
        }
    }

    /* A zero capacity still reports the required count, and never writes. */
    written = UINT64_C(9);
    if (cna_joystick_state_copy_axes(state, 0, UINT64_C(0), &written) !=
            (axes == UINT32_C(0) ? CNA_RESULT_SUCCESS : CNA_RESULT_BUFFER_TOO_SMALL) ||
        written != (uint64_t)axes) {
        return 0;
    }
    return cna_joystick_state_copy_axes(state, axis_values, UINT64_C(1), 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_state_copy_buttons(state, 0, UINT64_C(1), &written) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_state_get_axis_count(state, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_state_get_button_count(state, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_state_get_hat_count(state, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_joystick_state_get_ball_count(state, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static void on_connected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->connected_id = id;
    ++state->connected_calls;
}

static void on_disconnected(const uint32_t id, void* const context)
{
    HotplugState* const state = (HotplugState*)context;
    state->disconnected_id = id;
    ++state->disconnected_calls;
}

static int validate_hotplug_events(const CNA_Handle game)
{
    HotplugState hotplug = {UINT32_C(0), UINT32_C(0), 0, 0};
    CNA_JoystickEventRegistrationHandle connected = CNA_INVALID_HANDLE;
    CNA_JoystickEventRegistrationHandle disconnected = CNA_INVALID_HANDLE;
    CNA_JoystickEventRegistrationHandle rejected = CNA_INVALID_HANDLE;

    if (cna_joysticks_subscribe_connected_ext(0, &hotplug, &connected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_subscribe_connected_ext(on_connected, &hotplug, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_subscribe_disconnected_ext(0, &hotplug, &disconnected) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_joysticks_subscribe_connected_ext(on_connected, &hotplug, &connected) !=
            CNA_RESULT_SUCCESS ||
        connected == CNA_INVALID_HANDLE ||
        cna_joysticks_subscribe_disconnected_ext(on_disconnected, &hotplug, &disconnected) !=
            CNA_RESULT_SUCCESS ||
        disconnected == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* Handlers run synchronously before the raise returns, and each event carries its own id. */
    if (cna_joysticks_raise_connected_ext(game, UINT32_C(11)) != CNA_RESULT_SUCCESS ||
        hotplug.connected_calls != 1 || hotplug.connected_id != UINT32_C(11) ||
        hotplug.disconnected_calls != 0 ||
        cna_joysticks_raise_disconnected_ext(game, UINT32_C(12)) != CNA_RESULT_SUCCESS ||
        hotplug.disconnected_calls != 1 || hotplug.disconnected_id != UINT32_C(12) ||
        hotplug.connected_calls != 1) {
        return 0;
    }

    /* Releasing one registration detaches exactly that subscription. */
    if (cna_joysticks_unsubscribe_ext(connected) != CNA_RESULT_SUCCESS ||
        cna_joysticks_unsubscribe_ext(connected) != CNA_RESULT_INVALID_HANDLE ||
        cna_joysticks_raise_connected_ext(game, UINT32_C(13)) != CNA_RESULT_SUCCESS ||
        hotplug.connected_calls != 1 ||
        cna_joysticks_raise_disconnected_ext(game, UINT32_C(14)) != CNA_RESULT_SUCCESS ||
        hotplug.disconnected_calls != 2 || hotplug.disconnected_id != UINT32_C(14)) {
        return 0;
    }

    /* Clearing the process-wide event leaves the surviving registration releasable: detaching a
       subscription that is already gone removes nothing rather than failing. */
    if (cna_joysticks_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_joysticks_raise_disconnected_ext(game, UINT32_C(15)) != CNA_RESULT_SUCCESS ||
        hotplug.disconnected_calls != 2 ||
        cna_joysticks_unsubscribe_ext(disconnected) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_joysticks_unsubscribe_ext(rejected) == CNA_RESULT_INVALID_HANDLE;
}

static int validate_joystick_family(const CNA_Handle game)
{
    CNA_JoystickInfo info;
    CNA_JoystickCapabilities capabilities;
    CNA_JoystickStateHandle state = CNA_INVALID_HANDLE;
    CNA_JoystickStateHandle other_state = CNA_INVALID_HANDLE;
    CNA_JoystickStateHandle rejected = CNA_INVALID_HANDLE;
    CNA_Bool equal = UINT8_C(9);
    uint32_t count = UINT32_C(9);
    uint64_t bytes = UINT64_C(9);
    char name[256];

    if (cna_joysticks_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_joysticks_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An index at or past the count is refused, which pins the empty case too. */
    if (cna_joysticks_get_info_at(game, count, &info) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_get_name_size_at(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_copy_name_at(game, count, name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_get_info_at(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Whatever this machine has is enumerated fully; zero devices is an ordinary answer. */
    for (uint32_t index = UINT32_C(0); index < count; ++index) {
        memset(name, 0, sizeof(name));
        if (cna_joysticks_get_info_at(game, index, &info) != CNA_RESULT_SUCCESS ||
            info.struct_size != sizeof(CNA_JoystickInfo) ||
            info.type > CNA_JOYSTICK_TYPE_MAXIMUM ||
            cna_joysticks_get_name_size_at(game, index, &bytes) != CNA_RESULT_SUCCESS ||
            bytes >= (uint64_t)sizeof(name) ||
            cna_joysticks_copy_name_at(game, index, name, (uint64_t)sizeof(name), &bytes) !=
                CNA_RESULT_SUCCESS ||
            (uint64_t)strlen(name) != bytes) {
            return 0;
        }
        if (cna_joysticks_get_capabilities(game, info.id, &capabilities) != CNA_RESULT_SUCCESS ||
            capabilities.is_connected != CNA_TRUE ||
            capabilities.type != info.type ||
            cna_joysticks_capture_state(game, info.id, &state) != CNA_RESULT_SUCCESS ||
            !validate_captured_state(state) ||
            cna_joystick_state_destroy(state) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* An identifier that is not connected is not an error: the capability value comes back with
       the canonical disconnected defaults and both strings empty. */
    if (cna_joysticks_get_capabilities(game, UINT32_C(0xFFFFFFFF), &capabilities) !=
            CNA_RESULT_SUCCESS ||
        !capabilities_are_default(&capabilities) ||
        cna_joysticks_get_capabilities_name_size(game, UINT32_C(0xFFFFFFFF), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(0) ||
        cna_joysticks_get_capabilities_guid_size(game, UINT32_C(0xFFFFFFFF), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(0) ||
        cna_joysticks_copy_capabilities_name(
            game, UINT32_C(0xFFFFFFFF), name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(0) ||
        cna_joysticks_copy_capabilities_guid(
            game, UINT32_C(0xFFFFFFFF), name, (uint64_t)sizeof(name), &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(0)) {
        return 0;
    }
    if (cna_joysticks_get_capabilities(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_get_capabilities_name_size(game, UINT32_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_get_capabilities_guid_size(game, UINT32_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_copy_capabilities_name(game, UINT32_C(0), 0, UINT64_C(4), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_joysticks_copy_capabilities_guid(game, UINT32_C(0), name, UINT64_C(4), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Capturing an absent device succeeds and yields an all-empty snapshot; two such snapshots
       compare equal because the canonical comparison compares the four arrays. */
    if (cna_joysticks_capture_state(game, UINT32_C(0xFFFFFFFF), &state) != CNA_RESULT_SUCCESS ||
        state == CNA_INVALID_HANDLE ||
        cna_joysticks_capture_state(game, UINT32_C(0xFFFFFFFE), &other_state) !=
            CNA_RESULT_SUCCESS ||
        !validate_captured_state(state) ||
        cna_joystick_state_equals(state, other_state, &equal) != CNA_RESULT_SUCCESS ||
        equal != CNA_TRUE ||
        cna_joystick_state_equals(state, other_state, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_joystick_state_equals(state, rejected, &equal) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    {
        uint32_t axes = UINT32_C(9);
        if (cna_joystick_state_get_axis_count(state, &axes) != CNA_RESULT_SUCCESS ||
            axes != UINT32_C(0)) {
            return 0;
        }
    }
    if (cna_joysticks_capture_state(game, UINT32_C(0), 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Releasing is not idempotent, and every snapshot route refuses a released handle. */
    if (cna_joystick_state_destroy(other_state) != CNA_RESULT_SUCCESS ||
        cna_joystick_state_destroy(other_state) != CNA_RESULT_INVALID_HANDLE ||
        cna_joystick_state_get_axis_count(other_state, &count) != CNA_RESULT_INVALID_HANDLE ||
        cna_joystick_state_destroy(state) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A handle that was never created is refused by every snapshot route. */
    if (cna_joystick_state_get_axis_count(rejected, &count) != CNA_RESULT_INVALID_HANDLE ||
        cna_joystick_state_copy_axes(rejected, 0, UINT64_C(0), &bytes) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_joystick_state_destroy(rejected) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    return validate_hotplug_events(game);
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    JoystickSmokeState* const state = (JoystickSmokeState*)context;
    if (game_time == 0 || !validate_joystick_family(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int capture_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    uint32_t count = UINT32_C(0);
    uint32_t axes = UINT32_C(0);
    state->count_result = cna_joysticks_get_count(state->game, &count);
    state->state_result = cna_joystick_state_get_axis_count(state->state, &axes);
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_info()) {
        return 1;
    }
    if (!validate_pure_capabilities()) {
        return 2;
    }

    JoystickSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API joystick smoke", UINT64_C(20)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return 3;
    }

    /* Both the facade and a captured snapshot are thread-affine. */
    CNA_JoystickStateHandle state = CNA_INVALID_HANDLE;
    if (cna_joysticks_capture_state(game, UINT32_C(0), &state) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    WrongThreadState wrong_thread = {game, state, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, capture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.count_result != CNA_RESULT_THREAD ||
        wrong_thread.state_result != CNA_RESULT_THREAD) {
        return 5;
    }
    if (cna_joystick_state_destroy(state) != CNA_RESULT_SUCCESS) {
        return 6;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 7;
    }
    return 0;
}
