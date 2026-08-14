// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <string.h>
#include <threads.h>

typedef struct InputState {
    int capture_validated;
} InputState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result mouse_result;
    CNA_Result gamepad_result;
    CNA_Result touch_result;
} WrongThreadState;

static int nearly_equal(const float left, const float right)
{
    return fabsf(left - right) < 0.00001F;
}

static int analog_is_normalized(const CNA_GamePadAnalogState* const analog)
{
    return analog->left_thumb_stick.x >= -1.0F && analog->left_thumb_stick.x <= 1.0F &&
        analog->left_thumb_stick.y >= -1.0F && analog->left_thumb_stick.y <= 1.0F &&
        analog->right_thumb_stick.x >= -1.0F && analog->right_thumb_stick.x <= 1.0F &&
        analog->right_thumb_stick.y >= -1.0F && analog->right_thumb_stick.y <= 1.0F &&
        analog->left_trigger >= 0.0F && analog->left_trigger <= 1.0F &&
        analog->right_trigger >= 0.0F && analog->right_trigger <= 1.0F;
}

static int validate_pure_gamepad_helpers(void)
{
    const CNA_GamePadAnalogState raw_none = {
        {2.0F, -2.0F}, {-0.5F, 0.25F}, -1.0F, 2.0F
    };
    CNA_GamePadAnalogState processed = {{9.0F, 9.0F}, {9.0F, 9.0F}, 9.0F, 9.0F};
    if (cna_gamepad_apply_dead_zone(
            CNA_GAMEPAD_DEAD_ZONE_NONE,
            &raw_none,
            &processed) != CNA_RESULT_SUCCESS ||
        processed.left_thumb_stick.x != 1.0F || processed.left_thumb_stick.y != -1.0F ||
        processed.right_thumb_stick.x != -0.5F || processed.right_thumb_stick.y != 0.25F ||
        processed.left_trigger != 0.0F || processed.right_trigger != 1.0F) {
        return 0;
    }

    const float left_midpoint = CNA_GAMEPAD_LEFT_DEAD_ZONE +
        0.5F * (1.0F - CNA_GAMEPAD_LEFT_DEAD_ZONE);
    const float right_midpoint = CNA_GAMEPAD_RIGHT_DEAD_ZONE +
        0.5F * (1.0F - CNA_GAMEPAD_RIGHT_DEAD_ZONE);
    const float trigger_midpoint = CNA_GAMEPAD_TRIGGER_THRESHOLD +
        0.5F * (1.0F - CNA_GAMEPAD_TRIGGER_THRESHOLD);
    const CNA_GamePadAnalogState raw_independent = {
        {CNA_GAMEPAD_LEFT_DEAD_ZONE, left_midpoint},
        {-right_midpoint, CNA_GAMEPAD_RIGHT_DEAD_ZONE},
        CNA_GAMEPAD_TRIGGER_THRESHOLD,
        trigger_midpoint
    };
    if (cna_gamepad_apply_dead_zone(
            CNA_GAMEPAD_DEAD_ZONE_INDEPENDENT_AXES,
            &raw_independent,
            &processed) != CNA_RESULT_SUCCESS ||
        processed.left_thumb_stick.x != 0.0F ||
        !nearly_equal(processed.left_thumb_stick.y, 0.5F) ||
        !nearly_equal(processed.right_thumb_stick.x, -0.5F) ||
        processed.right_thumb_stick.y != 0.0F || processed.left_trigger != 0.0F ||
        !nearly_equal(processed.right_trigger, 0.5F)) {
        return 0;
    }

    const CNA_GamePadAnalogState raw_circular = {
        {left_midpoint, 0.0F}, {2.0F, 0.0F}, trigger_midpoint, 0.0F
    };
    if (cna_gamepad_apply_dead_zone(
            CNA_GAMEPAD_DEAD_ZONE_CIRCULAR,
            &raw_circular,
            &processed) != CNA_RESULT_SUCCESS ||
        !nearly_equal(processed.left_thumb_stick.x, 0.5F) ||
        processed.left_thumb_stick.y != 0.0F ||
        !nearly_equal(processed.right_thumb_stick.x, 1.0F) ||
        processed.right_thumb_stick.y != 0.0F ||
        !nearly_equal(processed.left_trigger, 0.5F) || processed.right_trigger != 0.0F) {
        return 0;
    }

    const CNA_GamePadAnalogState unchanged = {
        {7.0F, 7.0F}, {7.0F, 7.0F}, 7.0F, 7.0F
    };
    const CNA_GamePadAnalogState invalid_raw = {
        {NAN, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F
    };
    processed = unchanged;
    if (cna_gamepad_apply_dead_zone(
            UINT32_MAX,
            &raw_none,
            &processed) != CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&processed, &unchanged, sizeof(processed)) != 0 ||
        cna_gamepad_apply_dead_zone(
            CNA_GAMEPAD_DEAD_ZONE_NONE,
            &invalid_raw,
            &processed) != CNA_RESULT_INVALID_ARGUMENT ||
        memcmp(&processed, &unchanged, sizeof(processed)) != 0) {
        return 0;
    }

    CNA_GamePadState buttons = {
        sizeof(CNA_GamePadState), UINT32_C(1), CNA_TRUE, {0U, 0U, 0U}, 4,
        CNA_GAMEPAD_BUTTON_A, 0U, {{0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F}
    };
    CNA_Bool result = UINT8_C(9);
    if (cna_gamepad_state_is_button_down(
            &buttons,
            CNA_GAMEPAD_BUTTON_A,
            &result) != CNA_RESULT_SUCCESS || result != CNA_TRUE ||
        cna_gamepad_state_is_button_down(
            &buttons,
            CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_B,
            &result) != CNA_RESULT_SUCCESS || result != CNA_FALSE ||
        cna_gamepad_state_is_button_up(
            &buttons,
            CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_B,
            &result) != CNA_RESULT_SUCCESS || result != CNA_TRUE ||
        cna_gamepad_state_is_button_down(
            &buttons,
            CNA_GAMEPAD_BUTTON_NONE,
            &result) != CNA_RESULT_SUCCESS || result != CNA_TRUE ||
        cna_gamepad_state_is_button_up(
            &buttons,
            CNA_GAMEPAD_BUTTON_NONE,
            &result) != CNA_RESULT_SUCCESS || result != CNA_FALSE) {
        return 0;
    }
    result = UINT8_C(9);
    if (cna_gamepad_state_is_button_down(
            &buttons,
            UINT32_C(0x80000000),
            &result) != CNA_RESULT_INVALID_ARGUMENT || result != UINT8_C(9)) {
        return 0;
    }
    return 1;
}

static int validate_pure_touch_helpers(void)
{
    CNA_TouchState state = {0};
    state.struct_size = sizeof(CNA_TouchState);
    state.struct_version = UINT32_C(1);
    state.is_connected = CNA_TRUE;
    state.touch_count = 2U;
    state.touches[0] = (CNA_TouchLocation){
        7, CNA_TOUCH_LOCATION_MOVED, {3.0F, 4.0F},
        CNA_TOUCH_LOCATION_PRESSED, {1.0F, 2.0F}, 0.75F
    };
    state.touches[1] = (CNA_TouchLocation){
        9, CNA_TOUCH_LOCATION_PRESSED, {8.0F, 6.0F},
        CNA_TOUCH_LOCATION_INVALID, {0.0F, 0.0F}, 0.25F
    };

    CNA_TouchLocation location = {0};
    CNA_Bool found = CNA_FALSE;
    if (cna_touch_state_find_by_id(&state, 7, &location, &found) != CNA_RESULT_SUCCESS ||
        found != CNA_TRUE || memcmp(&location, &state.touches[0], sizeof(location)) != 0) {
        return 0;
    }
    if (cna_touch_state_find_by_id(&state, 123, &location, &found) != CNA_RESULT_SUCCESS ||
        found != CNA_FALSE || location.id != -1 ||
        location.state != CNA_TOUCH_LOCATION_INVALID || location.position.x != 0.0F ||
        location.position.y != 0.0F ||
        location.previous_state != CNA_TOUCH_LOCATION_INVALID || location.pressure != 0.0F) {
        return 0;
    }

    CNA_TouchLocation previous = {0};
    if (cna_touch_location_try_get_previous(
            &state.touches[0],
            &previous,
            &found) != CNA_RESULT_SUCCESS || found != CNA_TRUE || previous.id != 7 ||
        previous.state != CNA_TOUCH_LOCATION_PRESSED || previous.position.x != 1.0F ||
        previous.position.y != 2.0F ||
        previous.previous_state != CNA_TOUCH_LOCATION_INVALID || previous.pressure != 0.0F ||
        cna_touch_location_try_get_previous(
            &state.touches[1],
            &previous,
            &found) != CNA_RESULT_SUCCESS || found != CNA_FALSE || previous.id != 9 ||
        previous.state != CNA_TOUCH_LOCATION_INVALID) {
        return 0;
    }

    state.touch_count = CNA_TOUCH_MAX_TOUCHES + 1U;
    found = UINT8_C(9);
    if (cna_touch_state_find_by_id(&state, 7, &location, &found) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        found != UINT8_C(9)) {
        return 0;
    }
    return 1;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    InputState* const state = (InputState*)context;
    if (game_time == 0) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_MouseState mouse = {
        sizeof(CNA_MouseState), UINT32_C(1), 0, 0, 0, 0, 0U, 0U
    };
    if (cna_mouse_get_state(game, &mouse) != CNA_RESULT_SUCCESS ||
        (mouse.pressed_buttons & ~(CNA_MOUSE_BUTTON_LEFT | CNA_MOUSE_BUTTON_MIDDLE |
            CNA_MOUSE_BUTTON_RIGHT | CNA_MOUSE_BUTTON_X1 | CNA_MOUSE_BUTTON_X2)) != 0U ||
        mouse.reserved != 0U) {
        return CNA_RESULT_INVALID_STATE;
    }

    for (CNA_PlayerIndex player = CNA_PLAYER_INDEX_ONE;
         player <= CNA_PLAYER_INDEX_FOUR;
         ++player) {
        CNA_GamePadState default_state = {
            sizeof(CNA_GamePadState), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U}, 0, 0U, 0U,
            {{0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F}
        };
        CNA_GamePadState independent_state = default_state;
        if (cna_gamepad_get_state(game, player, &default_state) != CNA_RESULT_SUCCESS ||
            cna_gamepad_get_state_with_dead_zone(
                game,
                player,
                CNA_GAMEPAD_DEAD_ZONE_INDEPENDENT_AXES,
                &independent_state) != CNA_RESULT_SUCCESS ||
            memcmp(&default_state, &independent_state, sizeof(default_state)) != 0 ||
            (default_state.pressed_buttons & ~CNA_GAMEPAD_BUTTON_ALL) != 0U ||
            !analog_is_normalized(&default_state.analog)) {
            return CNA_RESULT_INVALID_STATE;
        }
        for (CNA_GamePadDeadZone mode = CNA_GAMEPAD_DEAD_ZONE_NONE;
             mode <= CNA_GAMEPAD_DEAD_ZONE_CIRCULAR;
             ++mode) {
            CNA_GamePadState explicit_state = independent_state;
            if (cna_gamepad_get_state_with_dead_zone(
                    game,
                    player,
                    mode,
                    &explicit_state) != CNA_RESULT_SUCCESS ||
                (explicit_state.pressed_buttons & ~CNA_GAMEPAD_BUTTON_ALL) != 0U ||
                !analog_is_normalized(&explicit_state.analog)) {
                return CNA_RESULT_INVALID_STATE;
            }
        }
    }

    CNA_TouchCapabilities capabilities = {
        sizeof(CNA_TouchCapabilities), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U}, 0U
    };
    CNA_TouchState touches = {0};
    touches.struct_size = sizeof(CNA_TouchState);
    touches.struct_version = UINT32_C(1);
    if (cna_touch_get_capabilities(game, &capabilities) != CNA_RESULT_SUCCESS ||
        capabilities.maximum_touch_count > CNA_TOUCH_MAX_TOUCHES ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.touch_count > CNA_TOUCH_MAX_TOUCHES) {
        return CNA_RESULT_INVALID_STATE;
    }
    for (uint32_t index = 0U; index < touches.touch_count; ++index) {
        if (touches.touches[index].state > CNA_TOUCH_LOCATION_MOVED ||
            touches.touches[index].previous_state > CNA_TOUCH_LOCATION_MOVED ||
            touches.touches[index].pressure < 0.0F || touches.touches[index].pressure > 1.0F) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    CNA_GamePadState invalid_gamepad = {
        sizeof(CNA_GamePadState), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U}, 0, 0U, 0U,
        {{0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F}
    };
    CNA_MouseState invalid_mouse = mouse;
    invalid_mouse.struct_version = UINT32_C(2);
    if (cna_gamepad_get_state(game, UINT32_C(4), &invalid_gamepad) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_get_state_with_dead_zone(
            game,
            CNA_PLAYER_INDEX_ONE,
            UINT32_C(3),
            &invalid_gamepad) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_get_state(game, &invalid_mouse) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }

    state->capture_validated = 1;
    return CNA_RESULT_SUCCESS;
}

static int capture_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_MouseState mouse = {
        sizeof(CNA_MouseState), UINT32_C(1), 0, 0, 0, 0, 0U, 0U
    };
    CNA_GamePadState gamepad = {
        sizeof(CNA_GamePadState), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U}, 0, 0U, 0U,
        {{0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F}
    };
    CNA_TouchState touch = {0};
    touch.struct_size = sizeof(CNA_TouchState);
    touch.struct_version = UINT32_C(1);
    state->mouse_result = cna_mouse_get_state(state->game, &mouse);
    state->gamepad_result = cna_gamepad_get_state(
        state->game,
        CNA_PLAYER_INDEX_ONE,
        &gamepad);
    state->touch_result = cna_touch_get_state(state->game, &touch);
    return 0;
}

int main(void)
{
    if (!validate_pure_gamepad_helpers() || !validate_pure_touch_helpers()) {
        return 1;
    }

    InputState input_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &input_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API input snapshot smoke", UINT64_C(26)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        input_state.capture_validated != 1) {
        return 2;
    }

    WrongThreadState wrong_thread = {
        game, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS
    };
    thrd_t thread;
    if (thrd_create(&thread, capture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.mouse_result != CNA_RESULT_THREAD ||
        wrong_thread.gamepad_result != CNA_RESULT_THREAD ||
        wrong_thread.touch_result != CNA_RESULT_THREAD) {
        return 3;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    return 0;
}
