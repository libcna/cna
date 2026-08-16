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
    CNA_Result capabilities_result;
    CNA_Result vibration_result;
    CNA_Result touch_result;
    CNA_Result text_input_result;
    CNA_Result touch_panel_result;
} WrongThreadState;

/* The next representable float above a positive finite value, without pulling in libm: the C API's
   test programs link only the C API itself. */
static float next_float_above(const float value)
{
    uint32_t bits = UINT32_C(0);
    float result = 0.0F;
    memcpy(&bits, &value, sizeof(bits));
    bits += UINT32_C(1);
    memcpy(&result, &bits, sizeof(result));
    return result;
}

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

/* A disconnected controller is exactly what the canonical default constructor produces. */
static int capabilities_are_disconnected(const CNA_GamePadCapabilities* const value)
{
    return value->gamepad_type == CNA_GAMEPAD_TYPE_UNKNOWN &&
        value->is_connected == CNA_FALSE && value->has_a_button == CNA_FALSE &&
        value->has_b_button == CNA_FALSE && value->has_x_button == CNA_FALSE &&
        value->has_y_button == CNA_FALSE && value->has_back_button == CNA_FALSE &&
        value->has_start_button == CNA_FALSE && value->has_big_button == CNA_FALSE &&
        value->has_dpad_up_button == CNA_FALSE && value->has_dpad_down_button == CNA_FALSE &&
        value->has_dpad_left_button == CNA_FALSE && value->has_dpad_right_button == CNA_FALSE &&
        value->has_left_shoulder_button == CNA_FALSE &&
        value->has_right_shoulder_button == CNA_FALSE &&
        value->has_left_stick_button == CNA_FALSE &&
        value->has_right_stick_button == CNA_FALSE &&
        value->has_left_x_thumb_stick == CNA_FALSE &&
        value->has_left_y_thumb_stick == CNA_FALSE &&
        value->has_right_x_thumb_stick == CNA_FALSE &&
        value->has_right_y_thumb_stick == CNA_FALSE &&
        value->has_left_trigger == CNA_FALSE && value->has_right_trigger == CNA_FALSE &&
        value->has_left_vibration_motor == CNA_FALSE &&
        value->has_right_vibration_motor == CNA_FALSE &&
        value->has_voice_support == CNA_FALSE && value->has_light_bar_ext == CNA_FALSE &&
        value->has_trigger_vibration_motors_ext == CNA_FALSE &&
        value->has_misc1_ext == CNA_FALSE && value->has_paddle1_ext == CNA_FALSE &&
        value->has_paddle2_ext == CNA_FALSE && value->has_paddle3_ext == CNA_FALSE &&
        value->has_paddle4_ext == CNA_FALSE && value->has_touchpad_ext == CNA_FALSE &&
        value->has_gyro_ext == CNA_FALSE && value->has_accelerometer_ext == CNA_FALSE &&
        value->reserved[0] == UINT8_C(0);
}

/* Every field is writable, because each canonical property has a setter as well as a getter. */
static int capabilities_are_all_set(const CNA_GamePadCapabilities* const value)
{
    return value->is_connected != CNA_FALSE && value->has_a_button != CNA_FALSE &&
        value->has_accelerometer_ext != CNA_FALSE && value->has_touchpad_ext != CNA_FALSE &&
        value->gamepad_type == CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD;
}

static int validate_pure_capabilities_helpers(void)
{
    CNA_GamePadCapabilities capabilities;
    memset(&capabilities, 0xEE, sizeof(capabilities));
    if (cna_gamepad_capabilities_init(&capabilities) != CNA_RESULT_SUCCESS ||
        capabilities.struct_size != sizeof(CNA_GamePadCapabilities) ||
        capabilities.struct_version != UINT32_C(1) ||
        !capabilities_are_disconnected(&capabilities)) {
        return 0;
    }
    capabilities.is_connected = CNA_TRUE;
    capabilities.has_a_button = CNA_TRUE;
    capabilities.has_accelerometer_ext = CNA_TRUE;
    capabilities.has_touchpad_ext = CNA_TRUE;
    capabilities.gamepad_type = CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD;
    if (!capabilities_are_all_set(&capabilities)) {
        return 0;
    }
    return cna_gamepad_capabilities_init(0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_button_set_helpers(void)
{
    static const CNA_GamePadButtonFlags array[3] = {
        CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_START, CNA_GAMEPAD_BUTTON_A
    };
    CNA_GamePadButtonFlags buttons = UINT32_C(0xFFFFFFFF);
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;

    if (cna_gamepad_buttons_init(&buttons) != CNA_RESULT_SUCCESS ||
        buttons != CNA_GAMEPAD_BUTTON_NONE ||
        cna_gamepad_buttons_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_gamepad_buttons_init_from_mask(
            CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_Y, &buttons) != CNA_RESULT_SUCCESS ||
        buttons != (CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_Y) ||
        cna_gamepad_buttons_init_from_mask(UINT32_C(0x80000000), &buttons) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        buttons != (CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_Y)) {
        return 0;
    }
    /* A repeated identity contributes once, because the canonical factory ORs the array. */
    if (cna_gamepad_buttons_init_from_button_array(array, UINT64_C(3), &buttons) !=
            CNA_RESULT_SUCCESS ||
        buttons != (CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_START) ||
        cna_gamepad_buttons_init_from_button_array(0, UINT64_C(0), &buttons) !=
            CNA_RESULT_SUCCESS ||
        buttons != CNA_GAMEPAD_BUTTON_NONE ||
        cna_gamepad_buttons_init_from_button_array(0, UINT64_C(1), &buttons) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Every one of the eleven named canonical getters answers through the one route. */
    {
        static const CNA_GamePadButtonFlags named[11] = {
            CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_B, CNA_GAMEPAD_BUTTON_X,
            CNA_GAMEPAD_BUTTON_Y, CNA_GAMEPAD_BUTTON_BACK, CNA_GAMEPAD_BUTTON_START,
            CNA_GAMEPAD_BUTTON_BIG_BUTTON, CNA_GAMEPAD_BUTTON_LEFT_SHOULDER,
            CNA_GAMEPAD_BUTTON_RIGHT_SHOULDER, CNA_GAMEPAD_BUTTON_LEFT_STICK,
            CNA_GAMEPAD_BUTTON_RIGHT_STICK
        };
        size_t index = 0U;
        for (index = 0U; index < 11U; ++index) {
            if (cna_gamepad_buttons_is_pressed(named[index], named[index], &flag) !=
                    CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
                cna_gamepad_buttons_is_pressed(
                    CNA_GAMEPAD_BUTTON_NONE, named[index], &flag) != CNA_RESULT_SUCCESS ||
                flag != CNA_FALSE) {
                return 0;
            }
        }
    }
    /* An identity with no canonical getter on this type still answers, through the same test. */
    if (cna_gamepad_buttons_is_pressed(
            CNA_GAMEPAD_BUTTON_LEFT_TRIGGER, CNA_GAMEPAD_BUTTON_LEFT_TRIGGER, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_buttons_is_pressed(CNA_GAMEPAD_BUTTON_A, UINT32_C(0x80000000), &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_buttons_is_pressed(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_A, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_gamepad_buttons_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_A, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_buttons_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_B, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_buttons_not_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_B, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_buttons_not_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_A, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_buttons_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_A, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_buttons_not_equals(CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_A, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical hash is the mask itself, reinterpreted as a signed value. */
    if (cna_gamepad_buttons_get_hash_code(
            CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_B, &hash) != CNA_RESULT_SUCCESS ||
        hash != (int32_t)(CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_B) ||
        cna_gamepad_buttons_get_hash_code(CNA_GAMEPAD_BUTTON_A, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_pure_dpad_helpers(void)
{
    static const CNA_GamePadButtonFlags mixed[3] = {
        CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_DPAD_LEFT, CNA_GAMEPAD_BUTTON_START
    };
    CNA_GamePadButtonFlags dpad = UINT32_C(0xFFFFFFFF);
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;

    if (cna_gamepad_dpad_init(&dpad) != CNA_RESULT_SUCCESS ||
        dpad != CNA_GAMEPAD_BUTTON_NONE ||
        cna_gamepad_dpad_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_dpad_init_from_states(CNA_TRUE, CNA_FALSE, CNA_TRUE, CNA_FALSE, &dpad) !=
            CNA_RESULT_SUCCESS ||
        dpad != (CNA_GAMEPAD_BUTTON_DPAD_UP | CNA_GAMEPAD_BUTTON_DPAD_LEFT) ||
        cna_gamepad_dpad_init_from_states(CNA_TRUE, CNA_TRUE, CNA_TRUE, CNA_TRUE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Non-directional identities in the array are ignored, as the canonical factory ignores them. */
    if (cna_gamepad_dpad_init_from_button_array(mixed, UINT64_C(3), &dpad) !=
            CNA_RESULT_SUCCESS ||
        dpad != CNA_GAMEPAD_BUTTON_DPAD_LEFT ||
        cna_gamepad_dpad_init_from_button_array(0, UINT64_C(1), &dpad) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_gamepad_dpad_is_pressed(
            CNA_GAMEPAD_BUTTON_DPAD_LEFT, CNA_GAMEPAD_BUTTON_DPAD_LEFT, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_dpad_is_pressed(
            CNA_GAMEPAD_BUTTON_DPAD_LEFT, CNA_GAMEPAD_BUTTON_DPAD_UP, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_dpad_is_pressed(CNA_GAMEPAD_BUTTON_DPAD_UP, CNA_GAMEPAD_BUTTON_A, &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_dpad_is_pressed(
            CNA_GAMEPAD_BUTTON_DPAD_UP, CNA_GAMEPAD_BUTTON_DPAD_UP, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Only the four directional bits participate in equality. */
    if (cna_gamepad_dpad_equals(
            CNA_GAMEPAD_BUTTON_DPAD_UP,
            CNA_GAMEPAD_BUTTON_DPAD_UP | CNA_GAMEPAD_BUTTON_A,
            &flag) != CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_dpad_equals(
            CNA_GAMEPAD_BUTTON_DPAD_UP, CNA_GAMEPAD_BUTTON_DPAD_DOWN, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_dpad_not_equals(
            CNA_GAMEPAD_BUTTON_DPAD_UP, CNA_GAMEPAD_BUTTON_DPAD_DOWN, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_dpad_not_equals(
            CNA_GAMEPAD_BUTTON_DPAD_UP, CNA_GAMEPAD_BUTTON_DPAD_UP, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_dpad_equals(CNA_GAMEPAD_BUTTON_NONE, CNA_GAMEPAD_BUTTON_NONE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_dpad_not_equals(CNA_GAMEPAD_BUTTON_NONE, CNA_GAMEPAD_BUTTON_NONE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical hash has its own weighting -- Down 1, Left 2, Right 4, Up 8 -- which is not
       the button bits and is reproduced rather than replaced by the raw mask. */
    if (cna_gamepad_dpad_get_hash_code(CNA_GAMEPAD_BUTTON_DPAD_DOWN, &hash) !=
            CNA_RESULT_SUCCESS || hash != 1 ||
        cna_gamepad_dpad_get_hash_code(CNA_GAMEPAD_BUTTON_DPAD_LEFT, &hash) !=
            CNA_RESULT_SUCCESS || hash != 2 ||
        cna_gamepad_dpad_get_hash_code(CNA_GAMEPAD_BUTTON_DPAD_RIGHT, &hash) !=
            CNA_RESULT_SUCCESS || hash != 4 ||
        cna_gamepad_dpad_get_hash_code(CNA_GAMEPAD_BUTTON_DPAD_UP, &hash) !=
            CNA_RESULT_SUCCESS || hash != 8 ||
        cna_gamepad_dpad_get_hash_code(CNA_GAMEPAD_BUTTON_NONE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_pure_analog_value_helpers(void)
{
    const CNA_Vector2 left_position = {0.25F, -0.5F};
    const CNA_Vector2 right_position = {-0.75F, 0.125F};
    const CNA_Vector2 out_of_range = {2.0F, -2.0F};
    CNA_GamePadThumbSticks clamped = {{9.0F, 9.0F}, {9.0F, 9.0F}};
    CNA_GamePadThumbSticks sticks = {{9.0F, 9.0F}, {9.0F, 9.0F}};
    CNA_GamePadThumbSticks other_sticks = {{0.0F, 0.0F}, {0.0F, 0.0F}};
    CNA_GamePadTriggers triggers = {9.0F, 9.0F};
    CNA_GamePadTriggers other_triggers = {0.0F, 0.0F};
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;
    int32_t other_hash = 9;

    if (cna_gamepad_thumb_sticks_init(&sticks) != CNA_RESULT_SUCCESS ||
        sticks.left.x != 0.0F || sticks.left.y != 0.0F ||
        sticks.right.x != 0.0F || sticks.right.y != 0.0F ||
        cna_gamepad_thumb_sticks_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical public constructor keeps in-range positions and square-clamps the rest. */
    if (cna_gamepad_thumb_sticks_init_from_positions(
            &left_position, &right_position, &sticks) != CNA_RESULT_SUCCESS ||
        sticks.left.x != 0.25F || sticks.left.y != -0.5F ||
        sticks.right.x != -0.75F || sticks.right.y != 0.125F ||
        cna_gamepad_thumb_sticks_init_from_positions(&out_of_range, &out_of_range, &clamped) !=
            CNA_RESULT_SUCCESS ||
        clamped.left.x != 1.0F || clamped.left.y != -1.0F ||
        clamped.right.x != 1.0F || clamped.right.y != -1.0F ||
        cna_gamepad_thumb_sticks_init_from_positions(0, &right_position, &sticks) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_thumb_sticks_init_from_positions(&left_position, 0, &sticks) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_thumb_sticks_init_from_positions(&left_position, &right_position, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    other_sticks = sticks;
    if (cna_gamepad_thumb_sticks_equals(&sticks, &other_sticks, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_gamepad_thumb_sticks_not_equals(&sticks, &other_sticks, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_gamepad_thumb_sticks_get_hash_code(&sticks, &hash) != CNA_RESULT_SUCCESS ||
        cna_gamepad_thumb_sticks_get_hash_code(&other_sticks, &other_hash) !=
            CNA_RESULT_SUCCESS || hash != other_hash) {
        return 0;
    }
    other_sticks.left.x = 0.5F;
    if (cna_gamepad_thumb_sticks_equals(&sticks, &other_sticks, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_gamepad_thumb_sticks_not_equals(&sticks, &other_sticks, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_thumb_sticks_equals(&sticks, &other_sticks, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_thumb_sticks_not_equals(0, &other_sticks, &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_thumb_sticks_get_hash_code(0, &hash) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_thumb_sticks_get_hash_code(&sticks, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_gamepad_triggers_init(&triggers) != CNA_RESULT_SUCCESS ||
        triggers.left != 0.0F || triggers.right != 0.0F ||
        cna_gamepad_triggers_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_triggers_init_from_positions(0.25F, 0.75F, &triggers) !=
            CNA_RESULT_SUCCESS ||
        triggers.left != 0.25F || triggers.right != 0.75F ||
        cna_gamepad_triggers_init_from_positions(0.0F, 0.0F, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical constructor clamps each trigger to the inclusive range zero through one. */
    if (cna_gamepad_triggers_init_from_positions(-1.0F, 2.0F, &other_triggers) !=
            CNA_RESULT_SUCCESS ||
        other_triggers.left != 0.0F || other_triggers.right != 1.0F) {
        return 0;
    }
    other_triggers = triggers;
    if (cna_gamepad_triggers_equals(&triggers, &other_triggers, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_gamepad_triggers_get_hash_code(&triggers, &hash) != CNA_RESULT_SUCCESS ||
        cna_gamepad_triggers_get_hash_code(&other_triggers, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }
    /* The canonical comparison is an epsilon comparison, not an exact one: the next representable
       float above 0.25 is a genuinely different value that still compares equal. */
    other_triggers = triggers;
    other_triggers.left = next_float_above(triggers.left);
    if (other_triggers.left == triggers.left ||
        cna_gamepad_triggers_equals(&triggers, &other_triggers, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    other_triggers.left = 0.5F;
    if (cna_gamepad_triggers_equals(&triggers, &other_triggers, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_gamepad_triggers_not_equals(&triggers, &other_triggers, &flag) !=
            CNA_RESULT_SUCCESS || flag != CNA_TRUE ||
        cna_gamepad_triggers_equals(0, &other_triggers, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_triggers_not_equals(&triggers, 0, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_triggers_get_hash_code(&triggers, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_pure_state_value_helpers(void)
{
    static const CNA_GamePadButtonFlags pressed[2] = {
        CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_DPAD_UP
    };
    const CNA_Vector2 centred = {0.0F, 0.0F};
    const CNA_Vector2 pushed_right = {1.0F, 0.0F};
    CNA_GamePadThumbSticks sticks = {{0.0F, 0.0F}, {0.0F, 0.0F}};
    CNA_GamePadTriggers triggers = {0.0F, 0.0F};
    CNA_GamePadState state;
    CNA_GamePadState other;
    CNA_GamePadButtonFlags mask = UINT32_C(0xFFFFFFFF);
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;
    int32_t other_hash = 9;
    uint64_t bytes = UINT64_C(0);
    char text[64];

    if (cna_gamepad_state_init(&state) != CNA_RESULT_SUCCESS ||
        state.struct_size != sizeof(CNA_GamePadState) || state.struct_version != UINT32_C(1) ||
        state.is_connected != CNA_FALSE || state.packet_number != 0 ||
        state.pressed_buttons != CNA_GAMEPAD_BUTTON_NONE ||
        state.analog.left_trigger != 0.0F || state.analog.right_trigger != 0.0F ||
        cna_gamepad_state_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* A snapshot built from components reports itself connected, merges the directional pad into
       its single button mask, and derives extra button bits: a trigger past the threshold adds its
       trigger bit, and a stick past its dead zone adds the matching virtual direction bit. */
    if (cna_gamepad_thumb_sticks_init_from_positions(&pushed_right, &centred, &sticks) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_triggers_init_from_positions(1.0F, 0.0F, &triggers) != CNA_RESULT_SUCCESS ||
        cna_gamepad_state_init_from_components(
            &sticks, &triggers, CNA_GAMEPAD_BUTTON_A, CNA_GAMEPAD_BUTTON_DPAD_UP, &state) !=
            CNA_RESULT_SUCCESS ||
        state.is_connected != CNA_TRUE ||
        (state.pressed_buttons & CNA_GAMEPAD_BUTTON_A) == 0U ||
        (state.pressed_buttons & CNA_GAMEPAD_BUTTON_DPAD_UP) == 0U ||
        (state.pressed_buttons & CNA_GAMEPAD_BUTTON_LEFT_TRIGGER) == 0U ||
        (state.pressed_buttons & CNA_GAMEPAD_BUTTON_RIGHT_TRIGGER) != 0U ||
        (state.pressed_buttons & CNA_GAMEPAD_BUTTON_LEFT_THUMBSTICK_RIGHT) == 0U) {
        return 0;
    }
    if (cna_gamepad_state_get_dpad(&state, &mask) != CNA_RESULT_SUCCESS ||
        mask != CNA_GAMEPAD_BUTTON_DPAD_UP) {
        return 0;
    }
    if (cna_gamepad_state_init_from_components(0, &triggers, 0U, 0U, &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_init_from_components(&sticks, 0, 0U, 0U, &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_init_from_components(&sticks, &triggers, 0U, 0U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_init_from_components(
            &sticks, &triggers, UINT32_C(0x80000000), 0U, &state) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The convenience constructor feeds the same array to both the button set and the pad. */
    if (cna_gamepad_state_init_from_values(
            &centred, &centred, 0.0F, 0.0F, pressed, UINT64_C(2), &state) !=
            CNA_RESULT_SUCCESS ||
        state.pressed_buttons != (CNA_GAMEPAD_BUTTON_A | CNA_GAMEPAD_BUTTON_DPAD_UP) ||
        cna_gamepad_state_init_from_values(
            0, &centred, 0.0F, 0.0F, pressed, UINT64_C(2), &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_init_from_values(
            &centred, &centred, 0.0F, 0.0F, 0, UINT64_C(1), &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_init_from_values(
            &centred, &centred, 0.0F, 0.0F, pressed, UINT64_C(2), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_gamepad_state_get_buttons(&state, &mask) != CNA_RESULT_SUCCESS ||
        mask != state.pressed_buttons ||
        cna_gamepad_state_get_dpad(&state, &mask) != CNA_RESULT_SUCCESS ||
        mask != CNA_GAMEPAD_BUTTON_DPAD_UP ||
        cna_gamepad_state_get_thumb_sticks(&state, &sticks) != CNA_RESULT_SUCCESS ||
        sticks.left.x != 0.0F || sticks.right.y != 0.0F ||
        cna_gamepad_state_get_triggers(&state, &triggers) != CNA_RESULT_SUCCESS ||
        triggers.left != 0.0F || triggers.right != 0.0F ||
        cna_gamepad_state_get_buttons(&state, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_get_dpad(0, &mask) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_get_thumb_sticks(&state, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_get_triggers(0, &triggers) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    other = state;
    if (cna_gamepad_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_gamepad_state_not_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_gamepad_state_set_packet_number_ext(&other, 7) != CNA_RESULT_SUCCESS ||
        other.packet_number != 7 ||
        cna_gamepad_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_gamepad_state_set_packet_number_ext(0, 7) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_equals(0, &other, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_not_equals(&state, 0, &flag) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical hash mixes only the button set and the packet number, so a snapshot that
       differs solely in its analog values hashes the same. */
    other = state;
    other.analog.left_trigger = 0.5F;
    if (cna_gamepad_state_get_hash_code(&state, &hash) != CNA_RESULT_SUCCESS ||
        cna_gamepad_state_get_hash_code(&other, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash ||
        cna_gamepad_state_get_hash_code(0, &hash) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_state_get_hash_code(&state, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The canonical type does not override its string conversion, so the text is the fixed
       fully-qualified type name and never reflects a field value. */
    memset(text, 0, sizeof(text));
    if (cna_gamepad_state_get_string_size(&state, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen("Microsoft.Xna.Framework.Input.GamePadState") ||
        cna_gamepad_state_copy_string(&state, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Input.GamePadState") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_gamepad_state_copy_string(&state, text, UINT64_C(2), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        bytes == (uint64_t)strlen("Microsoft.Xna.Framework.Input.GamePadState") &&
        text[0] == '\0' &&
        cna_gamepad_state_get_string_size(0, &bytes) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_state_copy_string(&state, text, UINT64_C(64), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_keyboard_value_helpers(void)
{
    static const CNA_Key keys[3] = {CNA_KEY_A, CNA_KEY_B, CNA_KEY_A};
    static const CNA_Key out_of_range[1] = {UINT32_C(256)};
    CNA_KeyboardState state;
    CNA_KeyboardState other;
    CNA_KeyState key_state = UINT32_C(999);
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;
    int32_t other_hash = 9;
    uint64_t bytes = UINT64_C(0);
    uint32_t count = UINT32_C(9);
    char text[64];

    if (cna_keyboard_state_init(&state) != CNA_RESULT_SUCCESS ||
        state.struct_size != sizeof(CNA_KeyboardState) ||
        state.struct_version != UINT32_C(1) ||
        cna_keyboard_state_get_pressed_key_count(&state, &count) != CNA_RESULT_SUCCESS ||
        count != 0U ||
        cna_keyboard_state_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A duplicate key contributes once, exactly as the canonical set would. */
    if (cna_keyboard_state_init_from_keys(keys, UINT64_C(3), &state) != CNA_RESULT_SUCCESS ||
        cna_keyboard_state_get_pressed_key_count(&state, &count) != CNA_RESULT_SUCCESS ||
        count != 2U ||
        cna_keyboard_state_is_key_down(&state, CNA_KEY_A, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_keyboard_state_is_key_down(&state, CNA_KEY_C, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* Documented deviation: the canonical constructor drops an out-of-range key silently, C
       refuses so a caller can never lose one without being told. */
    if (cna_keyboard_state_init_from_keys(out_of_range, UINT64_C(1), &other) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_init_from_keys(0, UINT64_C(1), &other) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_init_from_keys(keys, UINT64_C(1), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_keyboard_state_get_key_state(&state, CNA_KEY_A, &key_state) != CNA_RESULT_SUCCESS ||
        key_state != CNA_KEY_STATE_DOWN ||
        cna_keyboard_state_get_key_state(&state, CNA_KEY_C, &key_state) != CNA_RESULT_SUCCESS ||
        key_state != CNA_KEY_STATE_UP ||
        cna_keyboard_state_get_key_state(&state, UINT32_C(256), &key_state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_get_key_state(&state, CNA_KEY_A, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_get_key_state(0, CNA_KEY_A, &key_state) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    other = state;
    if (cna_keyboard_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_keyboard_state_not_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_keyboard_state_get_hash_code(&state, &hash) != CNA_RESULT_SUCCESS ||
        cna_keyboard_state_get_hash_code(&other, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }
    if (cna_keyboard_state_init_from_keys(keys, UINT64_C(1), &other) != CNA_RESULT_SUCCESS ||
        cna_keyboard_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_keyboard_state_not_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_keyboard_state_equals(&state, &other, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_not_equals(0, &other, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_get_hash_code(0, &hash) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The canonical type does not override its string conversion. */
    memset(text, 0, sizeof(text));
    if (cna_keyboard_state_get_string_size(&state, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != (uint64_t)strlen("Microsoft.Xna.Framework.Input.KeyboardState") ||
        cna_keyboard_state_copy_string(&state, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Input.KeyboardState") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_keyboard_state_copy_string(&state, text, UINT64_C(2), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        text[0] == '\0' &&
        cna_keyboard_state_get_string_size(0, &bytes) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_keyboard_state_copy_string(&state, text, UINT64_C(64), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_mouse_value_helpers(void)
{
    CNA_MouseState state;
    CNA_MouseState other;
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 9;
    int32_t other_hash = 9;
    uint64_t bytes = UINT64_C(0);
    char text[128];

    if (cna_mouse_state_init(&state) != CNA_RESULT_SUCCESS ||
        state.struct_size != sizeof(CNA_MouseState) || state.struct_version != UINT32_C(1) ||
        state.x != 0 || state.y != 0 || state.scroll_wheel != 0 ||
        state.horizontal_scroll_wheel != 0 ||
        state.pressed_buttons != 0U || state.reserved != 0U ||
        cna_mouse_state_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The eight-argument canonical constructor leaves the horizontal wheel at zero. */
    if (cna_mouse_state_init_from_values(
            3, 4, 120, CNA_MOUSE_BUTTON_LEFT | CNA_MOUSE_BUTTON_X2, &state) !=
            CNA_RESULT_SUCCESS ||
        state.x != 3 || state.y != 4 || state.scroll_wheel != 120 ||
        state.horizontal_scroll_wheel != 0 ||
        state.pressed_buttons != (CNA_MOUSE_BUTTON_LEFT | CNA_MOUSE_BUTTON_X2) ||
        cna_mouse_state_init_from_values(0, 0, 0, UINT32_C(0x40), &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_state_init_from_values(0, 0, 0, 0U, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_mouse_state_init_from_values_ext(
            3, 4, 120, -240, CNA_MOUSE_BUTTON_MIDDLE, &state) != CNA_RESULT_SUCCESS ||
        state.horizontal_scroll_wheel != -240 ||
        state.pressed_buttons != CNA_MOUSE_BUTTON_MIDDLE ||
        cna_mouse_state_init_from_values_ext(0, 0, 0, 0, UINT32_C(0x40), &state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_state_init_from_values_ext(0, 0, 0, 0, 0U, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    other = state;
    if (cna_mouse_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_mouse_state_not_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    other.x = 99;
    if (cna_mouse_state_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_mouse_state_not_equals(&state, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_mouse_state_equals(&state, &other, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_state_not_equals(0, &other, &flag) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical hash mixes only the position and the vertical wheel, so two snapshots that
       differ only in their buttons or horizontal wheel hash alike. */
    other = state;
    other.pressed_buttons = CNA_MOUSE_BUTTON_RIGHT;
    other.horizontal_scroll_wheel = 999;
    if (cna_mouse_state_get_hash_code(&state, &hash) != CNA_RESULT_SUCCESS ||
        cna_mouse_state_get_hash_code(&other, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash ||
        cna_mouse_state_get_hash_code(0, &hash) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_state_get_hash_code(&state, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Unlike the gamepad and keyboard snapshots, this one really does describe its fields. */
    memset(text, 0, sizeof(text));
    if (cna_mouse_state_init_from_values(
            3, 4, 120, CNA_MOUSE_BUTTON_LEFT | CNA_MOUSE_BUTTON_RIGHT, &state) !=
            CNA_RESULT_SUCCESS ||
        cna_mouse_state_get_string_size(&state, &bytes) != CNA_RESULT_SUCCESS ||
        cna_mouse_state_copy_string(&state, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "[MouseState X=3, Y=4, Buttons=Left Right, Wheel=120]") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_mouse_state_init(&state) != CNA_RESULT_SUCCESS ||
        cna_mouse_state_copy_string(&state, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "[MouseState X=0, Y=0, Buttons=None, Wheel=0]") != 0) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    return cna_mouse_state_copy_string(&state, text, UINT64_C(2), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        text[0] == '\0' &&
        cna_mouse_state_get_string_size(0, &bytes) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_mouse_state_copy_string(&state, text, UINT64_C(128), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_dead_zone_exclusion(void)
{
    float value = 9.0F;

    /* Inside the dead zone the axis reads zero; outside it, the remainder is rescaled so the
       range stays continuous up to one. */
    if (cna_gamepad_exclude_axis_dead_zone(
            CNA_GAMEPAD_LEFT_DEAD_ZONE, CNA_GAMEPAD_LEFT_DEAD_ZONE, &value) !=
            CNA_RESULT_SUCCESS || value != 0.0F ||
        cna_gamepad_exclude_axis_dead_zone(1.0F, CNA_GAMEPAD_LEFT_DEAD_ZONE, &value) !=
            CNA_RESULT_SUCCESS || !nearly_equal(value, 1.0F) ||
        cna_gamepad_exclude_axis_dead_zone(-1.0F, CNA_GAMEPAD_LEFT_DEAD_ZONE, &value) !=
            CNA_RESULT_SUCCESS || !nearly_equal(value, -1.0F)) {
        return 0;
    }
    value = 9.0F;
    return cna_gamepad_exclude_axis_dead_zone(1.0F, 0.0F, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_exclude_axis_dead_zone(1.0F / 0.0F - 1.0F / 0.0F, 0.0F, &value) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        value == 9.0F;
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

static int gesture_is_empty(const CNA_GestureSample* const sample)
{
    return sample->struct_size == sizeof(CNA_GestureSample) &&
        sample->struct_version == UINT32_C(1) &&
        sample->gesture_type == CNA_GESTURE_TYPE_NONE &&
        sample->finger_id_ext == CNA_TOUCH_NO_FINGER &&
        sample->finger_id2_ext == CNA_TOUCH_NO_FINGER &&
        sample->reserved == UINT32_C(0) &&
        sample->timestamp_ticks == INT64_C(0) &&
        sample->position.x == 0.0F && sample->position.y == 0.0F &&
        sample->position2.x == 0.0F && sample->position2.y == 0.0F &&
        sample->delta.x == 0.0F && sample->delta.y == 0.0F &&
        sample->delta2.x == 0.0F && sample->delta2.y == 0.0F;
}

static int validate_pure_gesture_value_helpers(void)
{
    const CNA_Vector2 position = {1.0F, 2.0F};
    const CNA_Vector2 position2 = {3.0F, 4.0F};
    const CNA_Vector2 delta = {5.0F, 6.0F};
    const CNA_Vector2 delta2 = {7.0F, 8.0F};
    CNA_GestureSample sample;
    CNA_GestureSample fingered;
    CNA_GestureType type = CNA_GESTURE_TYPE_NONE;

    memset(&sample, 9, sizeof(sample));
    if (cna_gesture_sample_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_gesture_sample_init(&sample) != CNA_RESULT_SUCCESS ||
        !gesture_is_empty(&sample)) {
        return 0;
    }

    /* The value-taking construction leaves both finger identifiers unset, exactly as the canonical
       constructor does -- that is the only difference between the two public constructions. */
    if (cna_gesture_sample_init_from_values(
            CNA_GESTURE_TYPE_PINCH,
            INT64_C(1234567),
            position, position2, delta, delta2,
            &sample) != CNA_RESULT_SUCCESS ||
        sample.gesture_type != CNA_GESTURE_TYPE_PINCH ||
        sample.timestamp_ticks != INT64_C(1234567) ||
        sample.finger_id_ext != CNA_TOUCH_NO_FINGER ||
        sample.finger_id2_ext != CNA_TOUCH_NO_FINGER ||
        sample.reserved != UINT32_C(0) ||
        sample.position.x != 1.0F || sample.position.y != 2.0F ||
        sample.position2.x != 3.0F || sample.position2.y != 4.0F ||
        sample.delta.x != 5.0F || sample.delta.y != 6.0F ||
        sample.delta2.x != 7.0F || sample.delta2.y != 8.0F) {
        return 0;
    }
    if (cna_gesture_sample_init_from_values_ext(
            CNA_GESTURE_TYPE_FLICK | CNA_GESTURE_TYPE_TAP,
            INT64_C(-5),
            position, position2, delta, delta2,
            11, 12,
            &fingered) != CNA_RESULT_SUCCESS ||
        fingered.gesture_type != (CNA_GESTURE_TYPE_FLICK | CNA_GESTURE_TYPE_TAP) ||
        fingered.timestamp_ticks != INT64_C(-5) ||
        fingered.finger_id_ext != 11 || fingered.finger_id2_ext != 12) {
        return 0;
    }

    /* Every defined bit is accepted on its own and every combination of them together; the first
       undefined bit is refused. C composes the identity with its own bitwise operators, which is
       why the canonical flag operators need no route of their own. */
    for (type = CNA_GESTURE_TYPE_TAP;
         type <= CNA_GESTURE_TYPE_PINCH_COMPLETE;
         type = (CNA_GestureType)(type << 1)) {
        if (cna_gesture_sample_init_from_values(
                type, INT64_C(0), position, position2, delta, delta2, &sample) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    if (cna_gesture_sample_init_from_values(
            CNA_GESTURE_TYPE_ALL, INT64_C(0), position, position2, delta, delta2, &sample) !=
            CNA_RESULT_SUCCESS ||
        sample.gesture_type != CNA_GESTURE_TYPE_ALL) {
        return 0;
    }
    return cna_gesture_sample_init_from_values(
            CNA_GESTURE_TYPE_PINCH_COMPLETE << 1, INT64_C(0),
            position, position2, delta, delta2, &sample) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gesture_sample_init_from_values_ext(
            CNA_GESTURE_TYPE_PINCH_COMPLETE << 1, INT64_C(0),
            position, position2, delta, delta2, 0, 0, &sample) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gesture_sample_init_from_values(
            CNA_GESTURE_TYPE_TAP, INT64_C(0), position, position2, delta, delta2, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gesture_sample_init_from_values_ext(
            CNA_GESTURE_TYPE_TAP, INT64_C(0), position, position2, delta, delta2, 0, 0, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_pure_touch_value_helpers(void)
{
    const CNA_Vector2 position = {3.0F, 4.0F};
    const CNA_Vector2 previous_position = {1.0F, 2.0F};
    CNA_TouchCapabilities capabilities;
    CNA_TouchLocation plain;
    CNA_TouchLocation with_previous;
    CNA_TouchLocation pressed;
    CNA_TouchLocation other;
    CNA_Bool flag = UINT8_C(9);
    int32_t hash = 0;
    int32_t other_hash = 0;
    uint64_t bytes = UINT64_C(0);
    char text[64];

    /* Both canonical capability constructions, including the refusal C adds. */
    memset(&capabilities, 9, sizeof(capabilities));
    if (cna_touch_capabilities_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_capabilities_init(&capabilities) != CNA_RESULT_SUCCESS ||
        capabilities.struct_size != sizeof(CNA_TouchCapabilities) ||
        capabilities.struct_version != UINT32_C(1) ||
        capabilities.is_connected != CNA_FALSE ||
        capabilities.maximum_touch_count != UINT32_C(0) ||
        capabilities.reserved[0] != UINT8_C(0) || capabilities.reserved[1] != UINT8_C(0) ||
        capabilities.reserved[2] != UINT8_C(0)) {
        return 0;
    }
    if (cna_touch_capabilities_init_from_values_ext(CNA_TRUE, 4, &capabilities) !=
            CNA_RESULT_SUCCESS ||
        capabilities.is_connected != CNA_TRUE || capabilities.maximum_touch_count != UINT32_C(4) ||
        cna_touch_capabilities_init_from_values_ext(CNA_TRUE, -1, &capabilities) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_capabilities_init_from_values_ext(CNA_TRUE, 4, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The canonical default location carries identifier zero, not the -1 the find-by-id miss
       sentinel uses -- the two are deliberately different values and C reproduces both. */
    if (cna_touch_location_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_init(&plain) != CNA_RESULT_SUCCESS ||
        plain.id != 0 || plain.state != CNA_TOUCH_LOCATION_INVALID ||
        plain.position.x != 0.0F || plain.position.y != 0.0F ||
        plain.previous_state != CNA_TOUCH_LOCATION_INVALID || plain.pressure != 0.0F) {
        return 0;
    }
    if (cna_touch_location_init_from_values(7, CNA_TOUCH_LOCATION_MOVED, position, &plain) !=
            CNA_RESULT_SUCCESS ||
        plain.id != 7 || plain.state != CNA_TOUCH_LOCATION_MOVED ||
        plain.position.x != 3.0F || plain.position.y != 4.0F ||
        plain.previous_state != CNA_TOUCH_LOCATION_INVALID ||
        plain.previous_position.x != 0.0F || plain.pressure != 0.0F) {
        return 0;
    }
    if (cna_touch_location_init_with_previous(
            7, CNA_TOUCH_LOCATION_MOVED, position,
            CNA_TOUCH_LOCATION_PRESSED, previous_position, &with_previous) !=
            CNA_RESULT_SUCCESS ||
        with_previous.previous_state != CNA_TOUCH_LOCATION_PRESSED ||
        with_previous.previous_position.x != 1.0F || with_previous.pressure != 0.0F) {
        return 0;
    }
    if (cna_touch_location_init_from_values_ext(
            7, CNA_TOUCH_LOCATION_MOVED, position, 0.5F, &pressed) != CNA_RESULT_SUCCESS ||
        pressed.pressure != 0.5F ||
        cna_touch_location_init_with_previous_ext(
            7, CNA_TOUCH_LOCATION_MOVED, position,
            CNA_TOUCH_LOCATION_PRESSED, previous_position, 1.0F, &other) != CNA_RESULT_SUCCESS ||
        other.pressure != 1.0F) {
        return 0;
    }
    if (cna_touch_location_init_from_values(7, UINT32_C(4), position, &plain) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_init_with_previous(
            7, CNA_TOUCH_LOCATION_MOVED, position, UINT32_C(4), previous_position, &plain) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_init_from_values_ext(
            7, CNA_TOUCH_LOCATION_MOVED, position, 1.5F, &plain) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_init_from_values_ext(
            7, CNA_TOUCH_LOCATION_MOVED, position, -0.5F, &plain) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_init_from_values(7, CNA_TOUCH_LOCATION_MOVED, position, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Equality and the hash deliberately ignore the pressure extension, exactly as the canonical
       ones do -- two locations differing only in pressure are the same location. */
    if (cna_touch_location_init_from_values(7, CNA_TOUCH_LOCATION_MOVED, position, &plain) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_location_init_from_values_ext(
            7, CNA_TOUCH_LOCATION_MOVED, position, 0.75F, &pressed) != CNA_RESULT_SUCCESS ||
        cna_touch_location_equals(&plain, &pressed, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_location_get_hash_code(&plain, &hash) != CNA_RESULT_SUCCESS ||
        cna_touch_location_get_hash_code(&pressed, &other_hash) != CNA_RESULT_SUCCESS ||
        hash != other_hash) {
        return 0;
    }
    /* A different identifier, state or previous location does separate them. */
    if (cna_touch_location_init_from_values(8, CNA_TOUCH_LOCATION_MOVED, position, &other) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_location_equals(&plain, &other, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_location_equals(&plain, &with_previous, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_touch_location_equals(0, &plain, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_equals(&plain, 0, &flag) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_equals(&plain, &other, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_get_hash_code(0, &hash) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_get_hash_code(&plain, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The canonical text carries only the position -- not the identifier, state or pressure. */
    memset(text, 0, sizeof(text));
    if (cna_touch_location_get_string_size(&plain, &bytes) != CNA_RESULT_SUCCESS ||
        bytes == UINT64_C(0) || bytes >= (uint64_t)sizeof(text) ||
        cna_touch_location_copy_string(&plain, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes ||
        strcmp(text, "{Position:{X:3 Y:4}}") != 0) {
        return 0;
    }
    if (cna_touch_location_copy_string(&plain, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        bytes != (uint64_t)strlen("{Position:{X:3 Y:4}}") ||
        cna_touch_location_get_string_size(&plain, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_location_get_string_size(0, &bytes) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

static int validate_pure_touch_collection_helpers(void)
{
    const CNA_Vector2 origin = {0.0F, 0.0F};
    CNA_TouchState state;
    CNA_TouchState built;
    CNA_TouchLocation locations[3];
    CNA_TouchLocation destination[8];
    CNA_TouchLocation probe;
    CNA_Bool flag = UINT8_C(9);
    int32_t index = 99;
    uint64_t count = UINT64_C(0);
    uint32_t slot = 0U;

    for (slot = 0U; slot < 3U; ++slot) {
        const CNA_Vector2 position = {(float)slot, (float)slot};
        if (cna_touch_location_init_from_values(
                (int32_t)slot, CNA_TOUCH_LOCATION_PRESSED, position, &locations[slot]) !=
            CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    if (cna_touch_state_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_init(&state) != CNA_RESULT_SUCCESS ||
        state.struct_size != sizeof(CNA_TouchState) || state.struct_version != UINT32_C(1) ||
        state.touch_count != 0U || state.is_connected != CNA_FALSE) {
        return 0;
    }
    /* An empty collection reports itself read-only anyway: the canonical flag is hard-coded true
       while the mutation routes below still succeed, and C is faithful to that rather than making
       the flag mean something it does not mean. */
    if (cna_touch_state_get_is_read_only(&state, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_state_get_is_empty_ext(&state, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_state_get_is_read_only(&state, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_get_is_empty_ext(0, &flag) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Both vector-taking constructions are one array in C. */
    if (cna_touch_state_init_from_locations(locations, 3U, &built) != CNA_RESULT_SUCCESS ||
        built.touch_count != 3U ||
        memcmp(&built.touches[0], &locations[0], sizeof(locations[0])) != 0 ||
        memcmp(&built.touches[2], &locations[2], sizeof(locations[2])) != 0 ||
        cna_touch_state_init_from_locations(0, 0U, &built) != CNA_RESULT_SUCCESS ||
        built.touch_count != 0U ||
        cna_touch_state_init_from_locations(0, 1U, &built) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_init_from_locations(
            locations, CNA_TOUCH_MAX_TOUCHES + 1U, &built) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_init_from_locations(locations, 3U, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Mutation still succeeds on the read-only-reporting collection. */
    if (cna_touch_state_add(&state, &locations[0]) != CNA_RESULT_SUCCESS ||
        cna_touch_state_add(&state, &locations[1]) != CNA_RESULT_SUCCESS ||
        state.touch_count != 2U ||
        cna_touch_state_get_is_empty_ext(&state, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_state_contains(&state, &locations[1], &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_state_contains(&state, &locations[2], &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_state_index_of(&state, &locations[1], &index) != CNA_RESULT_SUCCESS ||
        index != 1 ||
        cna_touch_state_index_of(&state, &locations[2], &index) != CNA_RESULT_SUCCESS ||
        index != -1) {
        return 0;
    }
    /* The search uses the canonical comparison, so pressure does not separate two locations. */
    if (cna_touch_location_init_from_values_ext(
            0, CNA_TOUCH_LOCATION_PRESSED, origin, 0.9F, &probe) != CNA_RESULT_SUCCESS ||
        cna_touch_state_contains(&state, &probe, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }

    /* An index equal to the count appends, exactly as the canonical insertion does. */
    if (cna_touch_state_insert(&state, 0, &locations[2]) != CNA_RESULT_SUCCESS ||
        state.touch_count != 3U || state.touches[0].id != 2 || state.touches[1].id != 0 ||
        cna_touch_state_insert(&state, (int32_t)state.touch_count, &locations[2]) !=
            CNA_RESULT_SUCCESS ||
        state.touch_count != 4U || state.touches[3].id != 2 ||
        cna_touch_state_insert(&state, -1, &locations[0]) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_insert(&state, 5, &locations[0]) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_touch_state_remove(&state, &locations[2], &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE || state.touch_count != 3U || state.touches[0].id != 0 ||
        cna_touch_state_remove(&state, &locations[1], &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE || state.touch_count != 2U ||
        cna_touch_state_remove(&state, &locations[1], &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE || state.touch_count != 2U) {
        return 0;
    }
    if (cna_touch_state_remove_at(&state, 1) != CNA_RESULT_SUCCESS ||
        state.touch_count != 1U || state.touches[0].id != 0 ||
        cna_touch_state_remove_at(&state, 1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_remove_at(&state, -1) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_clear(&state) != CNA_RESULT_SUCCESS || state.touch_count != 0U) {
        return 0;
    }

    /* The fixed capacity is exactly the canonical touch-panel maximum, and an append beyond it is
       refused rather than silently dropping a touch. */
    for (slot = 0U; slot < CNA_TOUCH_MAX_TOUCHES; ++slot) {
        if (cna_touch_state_add(&state, &locations[0]) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    if (cna_touch_state_add(&state, &locations[0]) != CNA_RESULT_BUFFER_TOO_SMALL ||
        state.touch_count != CNA_TOUCH_MAX_TOUCHES ||
        cna_touch_state_insert(&state, 0, &locations[0]) != CNA_RESULT_BUFFER_TOO_SMALL ||
        cna_touch_state_clear(&state) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The copy INSERTS at its index and shifts what is already there, because the canonical
       destination is a growable vector whose copy operation does exactly that. */
    if (cna_touch_state_init_from_locations(locations, 2U, &state) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    destination[0] = locations[2];
    destination[1] = locations[2];
    if (cna_touch_state_copy_to(&state, destination, UINT64_C(2), UINT64_C(8), 1, &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(4) ||
        destination[0].id != 2 || destination[1].id != 0 || destination[2].id != 1 ||
        destination[3].id != 2) {
        return 0;
    }
    if (cna_touch_state_copy_to(&state, destination, UINT64_C(2), UINT64_C(3), 0, &count) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        count != UINT64_C(4) ||
        cna_touch_state_copy_to(&state, destination, UINT64_C(2), UINT64_C(8), 3, &count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_copy_to(&state, destination, UINT64_C(2), UINT64_C(8), -1, &count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_copy_to(&state, destination, UINT64_C(9), UINT64_C(8), 0, &count) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_state_copy_to(&state, destination, UINT64_C(2), UINT64_C(8), 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

/* Every device query below is answered for an empty slot too: an absent controller is an ordinary
   answer -- false, zero, empty or Unknown -- never a failure. No verification tree has a real
   controller, so these assert the shape of the answer and the refusals, not a specific device. */
static int validate_device_queries(const CNA_Handle game)
{
    CNA_Vector3 reading = {9.0F, 9.0F, 9.0F};
    CNA_GamePadTouchpadFinger finger = {UINT8_C(9), {9U, 9U, 9U}, 9.0F, 9.0F, 9.0F};
    CNA_GamePadButtonLabel label = UINT32_C(999);
    CNA_GamePadConnectionState connection = UINT32_C(999);
    CNA_PowerState power = UINT32_C(999);
    CNA_Bool flag = UINT8_C(9);
    int32_t number = 9;
    int32_t percent = 9;
    uint16_t firmware = UINT16_C(9);
    uint64_t handle = UINT64_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[64];

    if (cna_gamepad_set_vibration(game, CNA_PLAYER_INDEX_ONE, 0.5F, 0.5F, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_set_trigger_vibration_ext(game, CNA_PLAYER_INDEX_ONE, 0.5F, 0.5F, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_set_light_bar_ext(
            game, CNA_PLAYER_INDEX_ONE, (CNA_Color){255U, 0U, 0U, 255U}) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_gamepad_get_gyro_ext(game, CNA_PLAYER_INDEX_ONE, &reading, &flag) !=
            CNA_RESULT_SUCCESS ||
        (flag == CNA_FALSE && reading.x != 9.0F) ||
        cna_gamepad_get_accelerometer_ext(game, CNA_PLAYER_INDEX_ONE, &reading, &flag) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_gamepad_get_player_index_ext(game, CNA_PLAYER_INDEX_ONE, &number) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_set_player_index_ext(game, CNA_PLAYER_INDEX_ONE, 0, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_get_power_info_ext(game, CNA_PLAYER_INDEX_ONE, &power, &percent) !=
            CNA_RESULT_SUCCESS ||
        power > CNA_POWER_STATE_CHARGED ||
        cna_gamepad_get_button_label_ext(
            game, CNA_PLAYER_INDEX_ONE, CNA_GAMEPAD_BUTTON_A, &label) != CNA_RESULT_SUCCESS ||
        label > CNA_GAMEPAD_BUTTON_LABEL_TRIANGLE ||
        cna_gamepad_get_connection_state_ext(game, CNA_PLAYER_INDEX_ONE, &connection) !=
            CNA_RESULT_SUCCESS ||
        connection > CNA_GAMEPAD_CONNECTION_STATE_WIRELESS) {
        return 0;
    }
    if (cna_gamepad_get_firmware_version_ext(game, CNA_PLAYER_INDEX_ONE, &firmware) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_get_steam_handle_ext(game, CNA_PLAYER_INDEX_ONE, &handle) !=
            CNA_RESULT_SUCCESS ||
        cna_gamepad_get_touchpad_count_ext(game, CNA_PLAYER_INDEX_ONE, &number) !=
            CNA_RESULT_SUCCESS || number < 0 ||
        cna_gamepad_get_touchpad_finger_count_ext(game, CNA_PLAYER_INDEX_ONE, 0, &number) !=
            CNA_RESULT_SUCCESS || number < 0 ||
        cna_gamepad_get_touchpad_finger_ext(
            game, CNA_PLAYER_INDEX_ONE, 0, 0, &finger, &flag) != CNA_RESULT_SUCCESS ||
        (flag == CNA_FALSE && finger.is_down != UINT8_C(9))) {
        return 0;
    }

    /* All four identity strings use the same count/copy protocol. */
    {
        typedef CNA_Result (*size_route)(CNA_Handle, CNA_PlayerIndex, uint64_t*);
        typedef CNA_Result (*copy_route)(CNA_Handle, CNA_PlayerIndex, char*, uint64_t, uint64_t*);
        static const size_route sizes[4] = {
            cna_gamepad_get_guid_size_ext, cna_gamepad_get_name_size_ext,
            cna_gamepad_get_path_size_ext, cna_gamepad_get_serial_size_ext
        };
        static const copy_route copies[4] = {
            cna_gamepad_copy_guid_ext, cna_gamepad_copy_name_ext,
            cna_gamepad_copy_path_ext, cna_gamepad_copy_serial_ext
        };
        size_t index = 0U;
        for (index = 0U; index < 4U; ++index) {
            uint64_t copied = UINT64_C(9);
            memset(text, 0, sizeof(text));
            if (sizes[index](game, CNA_PLAYER_INDEX_ONE, &bytes) != CNA_RESULT_SUCCESS ||
                bytes >= (uint64_t)sizeof(text) ||
                copies[index](game, CNA_PLAYER_INDEX_ONE, text, (uint64_t)sizeof(text), &copied) !=
                    CNA_RESULT_SUCCESS ||
                copied != bytes || (uint64_t)strlen(text) != bytes) {
                return 0;
            }
            if (sizes[index](game, UINT32_C(4), &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
                sizes[index](game, CNA_PLAYER_INDEX_ONE, 0) != CNA_RESULT_INVALID_ARGUMENT ||
                copies[index](game, CNA_PLAYER_INDEX_ONE, text, (uint64_t)sizeof(text), 0) !=
                    CNA_RESULT_INVALID_ARGUMENT) {
                return 0;
            }
        }
    }

    /* An out-of-range player slot is refused by every route, and so is a null output. */
    return cna_gamepad_set_vibration(game, UINT32_C(4), 0.0F, 0.0F, &flag) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_set_vibration(game, CNA_PLAYER_INDEX_ONE, 0.0F, 0.0F, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_set_light_bar_ext(
            game, UINT32_C(4), (CNA_Color){0U, 0U, 0U, 0U}) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_gyro_ext(game, CNA_PLAYER_INDEX_ONE, 0, &flag) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_accelerometer_ext(game, UINT32_C(9), &reading, &flag) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_power_info_ext(game, CNA_PLAYER_INDEX_ONE, &power, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_button_label_ext(
            game, CNA_PLAYER_INDEX_ONE, UINT32_C(0x80000000), &label) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_connection_state_ext(game, CNA_PLAYER_INDEX_ONE, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_touchpad_finger_ext(
            game, CNA_PLAYER_INDEX_ONE, 0, 0, &finger, 0) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_firmware_version_ext(game, UINT32_C(4), &firmware) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_gamepad_get_steam_handle_ext(game, CNA_PLAYER_INDEX_ONE, 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

/* The keyboard queries need a running game for the same reason every other device query does. No
   verification tree types anything, so these assert the shape of the answer and the refusals. */
static int validate_keyboard_queries(const CNA_Handle game)
{
    CNA_KeyboardState state;
    CNA_KeyboardState per_player;
    CNA_KeyModifiers modifiers = UINT32_C(0xFFFFFFFF);
    CNA_Key key = UINT32_C(999);
    CNA_PlayerIndex player = CNA_PLAYER_INDEX_ONE;
    uint64_t bytes = UINT64_C(9);
    char text[64];

    if (cna_keyboard_state_init(&state) != CNA_RESULT_SUCCESS ||
        cna_keyboard_get_state(game, &state) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* CNA has one keyboard, so every slot reports the same snapshot. */
    for (player = CNA_PLAYER_INDEX_ONE; player <= CNA_PLAYER_INDEX_FOUR; ++player) {
        if (cna_keyboard_state_init(&per_player) != CNA_RESULT_SUCCESS ||
            cna_keyboard_get_state_for_player(game, player, &per_player) !=
                CNA_RESULT_SUCCESS ||
            memcmp(&per_player, &state, sizeof(state)) != 0) {
            return 0;
        }
    }
    if (cna_keyboard_get_state_for_player(game, UINT32_C(4), &per_player) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_get_state_for_player(game, CNA_PLAYER_INDEX_ONE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_keyboard_get_mod_state_ext(game, &modifiers) != CNA_RESULT_SUCCESS ||
        (modifiers & ~CNA_KEY_MODIFIER_ALL) != 0U ||
        cna_keyboard_get_mod_state_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_keyboard_get_key_from_scancode_ext(game, CNA_KEY_A, &key) != CNA_RESULT_SUCCESS ||
        key >= UINT32_C(256) ||
        cna_keyboard_get_key_from_scancode_ext(game, UINT32_C(256), &key) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_get_key_from_scancode_ext(game, CNA_KEY_A, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Both name families use the same count/copy protocol, and both look back up by name. */
    memset(text, 0, sizeof(text));
    if (cna_keyboard_get_key_name_size_ext(game, CNA_KEY_A, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_keyboard_copy_key_name_ext(game, CNA_KEY_A, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes) {
        return 0;
    }
    if (bytes != UINT64_C(0)) {
        CNA_StringView name;
        name.data = text;
        name.byte_length = bytes;
        if (cna_keyboard_get_key_from_name_ext(game, name, &key) != CNA_RESULT_SUCCESS ||
            key != CNA_KEY_A) {
            return 0;
        }
    }
    memset(text, 0, sizeof(text));
    if (cna_keyboard_get_scancode_name_size_ext(game, CNA_KEY_A, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_keyboard_copy_scancode_name_ext(
            game, CNA_KEY_A, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        (uint64_t)strlen(text) != bytes) {
        return 0;
    }
    if (bytes != UINT64_C(0)) {
        CNA_StringView name;
        name.data = text;
        name.byte_length = bytes;
        if (cna_keyboard_get_scancode_from_name_ext(game, name, &key) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    {
        CNA_StringView unknown;
        CNA_StringView invalid;
        unknown.data = "not a key name at all";
        unknown.byte_length = (uint64_t)strlen("not a key name at all");
        invalid.data = 0;
        invalid.byte_length = UINT64_C(4);
        /* An unknown name is an ordinary answer -- the canonical none identity -- not a failure. */
        if (cna_keyboard_get_key_from_name_ext(game, unknown, &key) != CNA_RESULT_SUCCESS ||
            key != CNA_KEY_NONE ||
            cna_keyboard_get_scancode_from_name_ext(game, unknown, &key) !=
                CNA_RESULT_SUCCESS ||
            key != CNA_KEY_NONE ||
            cna_keyboard_get_key_from_name_ext(game, invalid, &key) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_keyboard_get_key_from_name_ext(game, unknown, 0) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    return cna_keyboard_get_key_name_size_ext(game, UINT32_C(256), &bytes) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_keyboard_get_scancode_name_size_ext(game, CNA_KEY_A, 0) ==
            CNA_RESULT_INVALID_ARGUMENT &&
        cna_keyboard_copy_key_name_ext(game, CNA_KEY_A, text, UINT64_C(64), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

typedef struct ClickRecord {
    int32_t last_button;
    int32_t count;
} ClickRecord;

static void on_mouse_clicked(const int32_t button, void* const context)
{
    ClickRecord* const record = (ClickRecord*)context;
    record->last_button = button;
    record->count += 1;
}

static int validate_mouse_queries(const CNA_Handle game)
{
    ClickRecord record = {0, 0};
    CNA_MouseEventRegistrationHandle registration = CNA_INVALID_HANDLE;
    CNA_MouseEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
    uint64_t window = UINT64_C(9);
    uint64_t restored = UINT64_C(0);
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool restored_relative = CNA_FALSE;
    int32_t x = 9;
    int32_t y = 9;

    if (cna_mouse_get_window_handle(game, &restored) != CNA_RESULT_SUCCESS ||
        cna_mouse_get_window_handle(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The window handle is process-wide state, so it is put back afterwards. */
    if (cna_mouse_set_window_handle(game, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        cna_mouse_get_window_handle(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0) ||
        cna_mouse_set_window_handle(game, restored) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_mouse_set_position(game, 0, 0) != CNA_RESULT_SUCCESS ||
        cna_mouse_get_global_position_ext(game, &x, &y) != CNA_RESULT_SUCCESS ||
        cna_mouse_get_global_position_ext(game, 0, &y) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_warp_global_ext(game, x, y, &flag) != CNA_RESULT_SUCCESS ||
        cna_mouse_warp_global_ext(game, 0, 0, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_set_capture_ext(game, CNA_FALSE, &flag) != CNA_RESULT_SUCCESS ||
        cna_mouse_set_capture_ext(game, CNA_FALSE, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    if (cna_mouse_get_is_relative_mouse_mode_ext(game, &restored_relative) !=
            CNA_RESULT_SUCCESS ||
        cna_mouse_get_is_relative_mouse_mode_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_set_is_relative_mouse_mode_ext(game, restored_relative) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The clicked event is static, so the subscription takes no game handle; the raise route is
       what makes it observable without a real device. */
    if (cna_mouse_subscribe_clicked_ext(0, &record, &registration) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE ||
        cna_mouse_subscribe_clicked_ext(on_mouse_clicked, &record, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_subscribe_clicked_ext(on_mouse_clicked, &record, &registration) !=
            CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_mouse_raise_clicked_ext(game, 3) != CNA_RESULT_SUCCESS ||
        record.count != 1 || record.last_button != 3) {
        (void)cna_mouse_unsubscribe_clicked_ext(registration);
        return 0;
    }
    if (cna_mouse_unsubscribe_clicked_ext(registration) != CNA_RESULT_SUCCESS ||
        cna_mouse_unsubscribe_clicked_ext(registration) != CNA_RESULT_INVALID_HANDLE ||
        cna_mouse_unsubscribe_clicked_ext(rejected) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    /* After release the handler no longer runs. */
    if (cna_mouse_raise_clicked_ext(game, 4) != CNA_RESULT_SUCCESS || record.count != 1) {
        return 0;
    }

    /* The canonical reset drops every subscription, including one this API handed out, so a
       registration released afterwards is a no-op rather than a failure. */
    record.count = 0;
    if (cna_mouse_subscribe_clicked_ext(on_mouse_clicked, &record, &registration) !=
            CNA_RESULT_SUCCESS ||
        cna_mouse_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_mouse_raise_clicked_ext(game, 5) != CNA_RESULT_SUCCESS || record.count != 0 ||
        cna_mouse_unsubscribe_clicked_ext(registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_mouse_set_window_handle(game, restored) == CNA_RESULT_SUCCESS;
}

/* The cursor family is probed by behavior, never by renderer identity: a backend with no real
   video device still hands back usable handles, and a texture-derived cursor either works or
   reports a documented failure. Both outcomes are asserted, neither is assumed. */
/* Installing a cursor is a capability, not a certainty. Since the platform-separation merge the
   request goes through CNA::Platform, and an implementation that cannot honour it -- SDL under the
   dummy video driver cannot create a system cursor -- refuses deterministically with
   CNA_RESULT_PLATFORM instead of pretending to have set one. Both answers are correct here; what
   would not be is a silent no-op, or CNA_RESULT_INTERNAL, which would say the ABI broke rather than
   that the platform declined. */
static int cursor_install_is_answered(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_PLATFORM;
}

static int validate_cursor_family(const CNA_Handle game)
{
    static const CNA_Color pixels[4] = {
        {255U, 0U, 0U, 255U}, {0U, 255U, 0U, 255U},
        {0U, 0U, 255U, 255U}, {255U, 255U, 255U, 255U}
    };
    CNA_MouseCursorHandle cursor = CNA_INVALID_HANDLE;
    CNA_MouseCursorHandle again = CNA_INVALID_HANDLE;
    CNA_MouseCursorHandle empty = CNA_INVALID_HANDLE;
    CNA_MouseCursorHandle rejected = CNA_INVALID_HANDLE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_MouseCursorStock stock = UINT32_C(0);

    /* An empty cursor is a real handle that can be set and released. */
    if (cna_mouse_cursor_create_ext(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_cursor_create_ext(&empty) != CNA_RESULT_SUCCESS ||
        empty == CNA_INVALID_HANDLE ||
        !cursor_install_is_answered(cna_mouse_set_cursor_ext(game, empty)) ||
        cna_mouse_cursor_dispose(empty) != CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_dispose(empty) != CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_destroy(empty) != CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_destroy(empty) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    /* All twelve stock identities resolve; a thirteenth does not. */
    for (stock = UINT32_C(0); stock <= CNA_MOUSE_CURSOR_STOCK_WAIT_ARROW; ++stock) {
        if (cna_mouse_cursor_get_stock_ext(game, stock, &cursor) != CNA_RESULT_SUCCESS ||
            cursor == CNA_INVALID_HANDLE ||
            !cursor_install_is_answered(cna_mouse_set_cursor_ext(game, cursor)) ||
            cna_mouse_cursor_destroy(cursor) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    if (cna_mouse_cursor_get_stock_ext(game, UINT32_C(12), &cursor) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_mouse_cursor_get_stock_ext(game, CNA_MOUSE_CURSOR_STOCK_ARROW, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Disposing a stock cursor is a deliberate no-op: the singleton must survive, so a second
       handle for the same identity is still usable afterwards. */
    if (cna_mouse_cursor_get_stock_ext(game, CNA_MOUSE_CURSOR_STOCK_ARROW, &cursor) !=
            CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_dispose(cursor) != CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_destroy(cursor) != CNA_RESULT_SUCCESS ||
        cna_mouse_cursor_get_stock_ext(game, CNA_MOUSE_CURSOR_STOCK_ARROW, &again) !=
            CNA_RESULT_SUCCESS ||
        !cursor_install_is_answered(cna_mouse_set_cursor_ext(game, again)) ||
        cna_mouse_cursor_destroy(again) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    if (cna_mouse_set_cursor_ext(game, rejected) != CNA_RESULT_INVALID_HANDLE ||
        cna_mouse_cursor_dispose(rejected) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }

    /* A texture-derived cursor either works or refuses; both are documented answers. */
    if (cna_texture2d_create_cpu_only_rgba8(
            2U, 2U, CNA_SURFACE_FORMAT_COLOR, pixels, UINT64_C(4), &texture) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        const CNA_Result created =
            cna_mouse_cursor_create_from_texture2d(game, texture, 0, 0, &cursor);
        if (created == CNA_RESULT_SUCCESS) {
            if (cursor == CNA_INVALID_HANDLE ||
                !cursor_install_is_answered(cna_mouse_set_cursor_ext(game, cursor)) ||
                cna_mouse_cursor_dispose(cursor) != CNA_RESULT_SUCCESS ||
                cna_mouse_cursor_destroy(cursor) != CNA_RESULT_SUCCESS) {
                (void)cna_texture2d_destroy(texture);
                return 0;
            }
        } else if (created != CNA_RESULT_PLATFORM && created != CNA_RESULT_NOT_SUPPORTED &&
            created != CNA_RESULT_INVALID_ARGUMENT) {
            (void)cna_texture2d_destroy(texture);
            return 0;
        } else if (cursor != CNA_INVALID_HANDLE) {
            (void)cna_texture2d_destroy(texture);
            return 0;
        }
    }
    if (cna_mouse_cursor_create_from_texture2d(game, rejected, 0, 0, &cursor) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_mouse_cursor_create_from_texture2d(game, texture, 0, 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        (void)cna_texture2d_destroy(texture);
        return 0;
    }
    return cna_texture2d_destroy(texture) == CNA_RESULT_SUCCESS;
}

typedef struct TextRecord {
    int32_t code_unit_count;
    uint16_t last_code_unit;
    int32_t editing_count;
    int32_t last_start;
    int32_t last_length;
    uint64_t last_text_bytes;
    char last_text[32];
    int32_t candidates_count;
    int32_t last_candidate_count;
    int32_t last_selected;
    CNA_Bool last_horizontal;
    char last_candidate[32];
    int malformed;
} TextRecord;

static void on_text_input(const uint16_t code_unit, void* const context)
{
    TextRecord* const record = (TextRecord*)context;
    record->last_code_unit = code_unit;
    record->code_unit_count += 1;
}

static void on_text_editing(const CNA_TextEditingEventInfo* const info, void* const context)
{
    TextRecord* const record = (TextRecord*)context;
    if (info == 0 || info->struct_size != sizeof(CNA_TextEditingEventInfo) ||
        info->struct_version != UINT32_C(1) ||
        (info->text.data == 0 && info->text.byte_length != UINT64_C(0)) ||
        info->text.byte_length >= sizeof(record->last_text)) {
        record->malformed = 1;
        return;
    }
    record->editing_count += 1;
    record->last_start = info->start;
    record->last_length = info->length;
    record->last_text_bytes = info->text.byte_length;
    memset(record->last_text, 0, sizeof(record->last_text));
    if (info->text.byte_length != UINT64_C(0)) {
        memcpy(record->last_text, info->text.data, (size_t)info->text.byte_length);
    }
}

static void on_text_editing_candidates(
    const CNA_TextEditingCandidatesEventInfo* const info,
    void* const context)
{
    TextRecord* const record = (TextRecord*)context;
    if (info == 0 || info->struct_size != sizeof(CNA_TextEditingCandidatesEventInfo) ||
        info->struct_version != UINT32_C(1) || info->candidate_count < 0 ||
        (info->candidates == 0 && info->candidate_count != 0) ||
        info->reserved[0] != UINT8_C(0) || info->reserved[1] != UINT8_C(0) ||
        info->reserved[2] != UINT8_C(0)) {
        record->malformed = 1;
        return;
    }
    record->candidates_count += 1;
    record->last_candidate_count = info->candidate_count;
    record->last_selected = info->selected;
    record->last_horizontal = info->horizontal;
    memset(record->last_candidate, 0, sizeof(record->last_candidate));
    if (info->candidate_count > 0) {
        const CNA_StringView first = info->candidates[0];
        if (first.byte_length >= sizeof(record->last_candidate)) {
            record->malformed = 1;
            return;
        }
        if (first.byte_length != UINT64_C(0)) {
            memcpy(record->last_candidate, first.data, (size_t)first.byte_length);
        }
    }
}

/* Text input is probed by behavior, never by renderer identity. A backend that creates a real
   window publishes it into the canonical static, so this suite must not assume either state: it
   forces the unbound case to prove the documented null-guarded contract, then restores whatever was
   bound and exercises the real path for whichever answer that backend gives. The events are
   observable on every backend through the raise routes, which is exactly why the canonical class
   has them. */
static int validate_text_input_family(const CNA_Handle game)
{
    /* A three-byte code point and a two-byte one, so the borrowed view is real UTF-8, not ASCII. */
    static const char composition[] = "\xE6\x97\xA5\xC3\xA9";
    static const char invalid_utf8[] = "\xC0\xAF";
    static const CNA_StringView candidates[2] = {
        {"\xE6\x97\xA5", UINT64_C(3)}, {"ok", UINT64_C(2)}
    };
    static const CNA_StringView malformed_candidates[2] = {
        {"ok", UINT64_C(2)}, {invalid_utf8, UINT64_C(2)}
    };
    TextRecord record;
    CNA_TextInputRegistrationHandle text_registration = CNA_INVALID_HANDLE;
    CNA_TextInputRegistrationHandle editing_registration = CNA_INVALID_HANDLE;
    CNA_TextInputRegistrationHandle candidates_registration = CNA_INVALID_HANDLE;
    CNA_TextInputRegistrationHandle rejected = CNA_INVALID_HANDLE;
    CNA_TextInputRegistrationHandle scratch = CNA_INVALID_HANDLE;
    const CNA_Rectangle area = {4, 8, 32, 16};
    CNA_Bool flag = UINT8_C(9);
    CNA_Bool after_start = UINT8_C(9);
    CNA_Bool after_stop = UINT8_C(9);
    uint64_t window = UINT64_C(9);
    uint64_t bound = UINT64_C(0);
    CNA_TextInputType type = UINT32_C(0);

    memset(&record, 0, sizeof(record));

    /* Whatever this backend bound is process-wide state that must be put back. A windowed backend
       publishes its real window here; a windowless one leaves zero. Neither is assumed. */
    if (cna_text_input_get_window_handle_ext(game, &bound) != CNA_RESULT_SUCCESS ||
        cna_text_input_get_window_handle_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Forcing the unbound case makes the canonical null guard a deterministic contract on every
       backend: every query answers false and every activation succeeds unchanged. */
    if (cna_text_input_set_window_handle_ext(game, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        cna_text_input_get_window_handle_ext(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0)) {
        return 0;
    }
    if (cna_text_input_is_active_ext(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_text_input_is_active_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_is_screen_keyboard_shown_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_text_input_is_screen_keyboard_shown_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_is_screen_keyboard_shown_for_window_ext(game, UINT64_C(0), &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_text_input_is_screen_keyboard_shown_for_window_ext(game, UINT64_C(0), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_text_input_start_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_is_active_ext(game, &flag) != CNA_RESULT_SUCCESS || flag != CNA_FALSE ||
        cna_text_input_stop_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_set_input_rectangle_ext(game, area) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Every defined hint is accepted; an undefined one is refused rather than silently becoming
       plain text, which is where C deliberately differs from the canonical fallback. */
    for (type = CNA_TEXT_INPUT_TYPE_TEXT; type <= CNA_TEXT_INPUT_TYPE_MAXIMUM; ++type) {
        if (cna_text_input_start_with_type_ext(game, type) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    if (cna_text_input_start_with_type_ext(game, CNA_TEXT_INPUT_TYPE_MAXIMUM + UINT32_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_stop_ext(game) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* A bogus handle round trips because the C API never dereferences it, and the canonical reset
       is what clears it -- which is also how a stale handle can never reach the backend. */
    if (cna_text_input_set_window_handle_ext(game, UINT64_C(0xDEADBEEF)) != CNA_RESULT_SUCCESS ||
        cna_text_input_get_window_handle_ext(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0xDEADBEEF) ||
        cna_text_input_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_get_window_handle_ext(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0)) {
        return 0;
    }

    /* With whatever this backend really bound put back, the same routes reach the platform. The
       answer is the backend's to give: only the relationship between the answers is asserted, so a
       backend with a live window supplies real activation evidence and one without stays honest. */
    if (cna_text_input_set_window_handle_ext(game, bound) != CNA_RESULT_SUCCESS ||
        cna_text_input_start_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_is_active_ext(game, &after_start) != CNA_RESULT_SUCCESS ||
        (after_start != CNA_FALSE && after_start != CNA_TRUE) ||
        cna_text_input_set_input_rectangle_ext(game, area) != CNA_RESULT_SUCCESS ||
        cna_text_input_stop_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_is_active_ext(game, &after_stop) != CNA_RESULT_SUCCESS ||
        (after_stop != CNA_FALSE && after_stop != CNA_TRUE)) {
        return 0;
    }
    /* Activation that took effect must be undone by stopping it. */
    if (after_start == CNA_TRUE && after_stop != CNA_FALSE) {
        return 0;
    }
    for (type = CNA_TEXT_INPUT_TYPE_TEXT; type <= CNA_TEXT_INPUT_TYPE_MAXIMUM; ++type) {
        if (cna_text_input_start_with_type_ext(game, type) != CNA_RESULT_SUCCESS ||
            cna_text_input_is_active_ext(game, &flag) != CNA_RESULT_SUCCESS ||
            (flag != CNA_FALSE && flag != CNA_TRUE)) {
            return 0;
        }
    }
    if (cna_text_input_stop_ext(game) != CNA_RESULT_SUCCESS ||
        cna_text_input_is_screen_keyboard_shown_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_text_input_is_screen_keyboard_shown_for_window_ext(game, bound, &flag) !=
            CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE)) {
        return 0;
    }

    /* All three subscriptions are static, so none takes a game handle. */
    if (cna_text_input_subscribe_text_input_ext(0, &record, &scratch) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        scratch != CNA_INVALID_HANDLE ||
        cna_text_input_subscribe_text_input_ext(on_text_input, &record, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_subscribe_text_editing_ext(0, &record, &scratch) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        scratch != CNA_INVALID_HANDLE ||
        cna_text_input_subscribe_text_editing_ext(on_text_editing, &record, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_subscribe_text_editing_candidates_ext(0, &record, &scratch) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        scratch != CNA_INVALID_HANDLE ||
        cna_text_input_subscribe_text_editing_candidates_ext(
            on_text_editing_candidates, &record, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_text_input_subscribe_text_input_ext(on_text_input, &record, &text_registration) !=
            CNA_RESULT_SUCCESS ||
        text_registration == CNA_INVALID_HANDLE ||
        cna_text_input_subscribe_text_editing_ext(
            on_text_editing, &record, &editing_registration) != CNA_RESULT_SUCCESS ||
        editing_registration == CNA_INVALID_HANDLE ||
        cna_text_input_subscribe_text_editing_candidates_ext(
            on_text_editing_candidates, &record, &candidates_registration) !=
            CNA_RESULT_SUCCESS ||
        candidates_registration == CNA_INVALID_HANDLE) {
        return 0;
    }

    /* A code point above U+FFFF arrives as a surrogate pair -- two calls, not one. */
    if (cna_text_input_raise_text_input_ext(game, UINT16_C(0x0041)) != CNA_RESULT_SUCCESS ||
        record.code_unit_count != 1 || record.last_code_unit != UINT16_C(0x0041) ||
        cna_text_input_raise_text_input_ext(game, UINT16_C(0xD83D)) != CNA_RESULT_SUCCESS ||
        record.last_code_unit != UINT16_C(0xD83D) ||
        cna_text_input_raise_text_input_ext(game, UINT16_C(0xDE00)) != CNA_RESULT_SUCCESS ||
        record.code_unit_count != 3 || record.last_code_unit != UINT16_C(0xDE00)) {
        return 0;
    }

    /* start and length are byte offsets forwarded verbatim: the canonical dispatch does not
       validate them against the text, and neither does C. */
    {
        const CNA_StringView text = {composition, sizeof(composition) - 1U};
        const CNA_StringView empty = {0, UINT64_C(0)};
        const CNA_StringView malformed = {invalid_utf8, sizeof(invalid_utf8) - 1U};
        if (cna_text_input_raise_text_editing_ext(game, text, 1000, -5) != CNA_RESULT_SUCCESS ||
            record.editing_count != 1 || record.malformed != 0 ||
            record.last_start != 1000 || record.last_length != -5 ||
            record.last_text_bytes != (uint64_t)(sizeof(composition) - 1U) ||
            memcmp(record.last_text, composition, sizeof(composition) - 1U) != 0) {
            return 0;
        }
        if (cna_text_input_raise_text_editing_ext(game, empty, 0, 0) != CNA_RESULT_SUCCESS ||
            record.editing_count != 2 || record.last_text_bytes != UINT64_C(0)) {
            return 0;
        }
        if (cna_text_input_raise_text_editing_ext(game, malformed, 0, 0) !=
                CNA_RESULT_ENCODING ||
            record.editing_count != 2) {
            return 0;
        }
    }

    /* The candidate list crosses as borrowed views valid only for the callback. */
    if (cna_text_input_raise_text_editing_candidates_ext(game, candidates, 2, 1, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        record.candidates_count != 1 || record.malformed != 0 ||
        record.last_candidate_count != 2 || record.last_selected != 1 ||
        record.last_horizontal != CNA_TRUE ||
        memcmp(record.last_candidate, "\xE6\x97\xA5", 3U) != 0) {
        return 0;
    }
    if (cna_text_input_raise_text_editing_candidates_ext(game, 0, 0, -1, CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        record.candidates_count != 2 || record.last_candidate_count != 0 ||
        record.last_selected != -1 || record.last_horizontal != CNA_FALSE) {
        return 0;
    }
    if (cna_text_input_raise_text_editing_candidates_ext(game, candidates, -1, 0, CNA_FALSE) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_raise_text_editing_candidates_ext(game, 0, 1, 0, CNA_FALSE) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_text_input_raise_text_editing_candidates_ext(
            game, malformed_candidates, 2, 0, CNA_FALSE) != CNA_RESULT_ENCODING ||
        record.candidates_count != 2) {
        return 0;
    }

    /* One release route covers all three kinds, and a released handler stops running. */
    if (cna_text_input_unsubscribe_ext(text_registration) != CNA_RESULT_SUCCESS ||
        cna_text_input_unsubscribe_ext(text_registration) != CNA_RESULT_INVALID_HANDLE ||
        cna_text_input_unsubscribe_ext(rejected) != CNA_RESULT_INVALID_HANDLE ||
        cna_text_input_raise_text_input_ext(game, UINT16_C(0x0042)) != CNA_RESULT_SUCCESS ||
        record.code_unit_count != 3) {
        return 0;
    }
    if (cna_text_input_unsubscribe_ext(editing_registration) != CNA_RESULT_SUCCESS ||
        cna_text_input_unsubscribe_ext(candidates_registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* The canonical reset drops every subscription to all three events, including ones this API
       handed out, so a release afterwards is a no-op rather than a failure. */
    record.code_unit_count = 0;
    record.editing_count = 0;
    record.candidates_count = 0;
    if (cna_text_input_subscribe_text_input_ext(on_text_input, &record, &text_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_text_input_subscribe_text_editing_ext(
            on_text_editing, &record, &editing_registration) != CNA_RESULT_SUCCESS ||
        cna_text_input_subscribe_text_editing_candidates_ext(
            on_text_editing_candidates, &record, &candidates_registration) !=
            CNA_RESULT_SUCCESS ||
        cna_text_input_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    {
        const CNA_StringView text = {composition, sizeof(composition) - 1U};
        if (cna_text_input_raise_text_input_ext(game, UINT16_C(0x0043)) != CNA_RESULT_SUCCESS ||
            cna_text_input_raise_text_editing_ext(game, text, 0, 0) != CNA_RESULT_SUCCESS ||
            cna_text_input_raise_text_editing_candidates_ext(game, candidates, 2, 0, CNA_FALSE) !=
                CNA_RESULT_SUCCESS ||
            record.code_unit_count != 0 || record.editing_count != 0 ||
            record.candidates_count != 0 || record.malformed != 0) {
            return 0;
        }
    }
    if (cna_text_input_unsubscribe_ext(text_registration) != CNA_RESULT_SUCCESS ||
        cna_text_input_unsubscribe_ext(editing_registration) != CNA_RESULT_SUCCESS ||
        cna_text_input_unsubscribe_ext(candidates_registration) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* The reset above cleared process-wide state this suite does not own, so it goes back. */
    return cna_text_input_set_window_handle_ext(game, bound) == CNA_RESULT_SUCCESS;
}

/* The touch panel is process-wide state this suite does not own, so everything it changes is put
   back. The window handle gets the same treatment the text-input family taught: a windowed backend
   publishes a real window into the canonical static, so the unbound case is forced to pin the
   contract and whatever was bound is restored afterwards. */
static int validate_touch_panel_family(const CNA_Handle game)
{
    const CNA_Vector2 finger_position = {12.0F, 34.0F};
    const CNA_Vector2 zero = {0.0F, 0.0F};
    CNA_GestureSample sample;
    CNA_GestureSample read_back;
    CNA_TouchState touches;
    CNA_DisplayOrientation orientation = CNA_DISPLAY_ORIENTATION_DEFAULT;
    uint64_t restored_window = UINT64_C(0);
    CNA_DisplayOrientation restored_orientation = CNA_DISPLAY_ORIENTATION_DEFAULT;
    CNA_GestureType gestures = CNA_GESTURE_TYPE_NONE;
    CNA_Bool flag = UINT8_C(9);
    uint64_t window = UINT64_C(9);
    uint64_t bound = UINT64_C(0);
    int32_t restored_width = 0;
    int32_t restored_height = 0;
    int32_t value = 0;

    if (cna_touch_state_init(&touches) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_touch_panel_get_display_width(game, &restored_width) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_height(game, &restored_height) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_orientation(game, &restored_orientation) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_window_handle(game, &bound) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_touch_panel_get_display_width(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_display_height(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_display_orientation(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_window_handle(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_enabled_gestures(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_is_gesture_available(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_get_touch_device_exists_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* The display size is stored verbatim, including a nonpositive value, exactly as the canonical
       property stores it. */
    if (cna_touch_panel_set_display_width(game, 640) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_width(game, &value) != CNA_RESULT_SUCCESS || value != 640 ||
        cna_touch_panel_set_display_height(game, 480) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_height(game, &value) != CNA_RESULT_SUCCESS || value != 480 ||
        cna_touch_panel_set_display_width(game, 0) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_width(game, &value) != CNA_RESULT_SUCCESS || value != 0) {
        return 0;
    }

    if (cna_touch_panel_set_display_orientation(game, CNA_DISPLAY_ORIENTATION_PORTRAIT) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_display_orientation(game, &orientation) != CNA_RESULT_SUCCESS ||
        orientation != CNA_DISPLAY_ORIENTATION_PORTRAIT ||
        cna_touch_panel_set_display_orientation(
            game,
            CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT | CNA_DISPLAY_ORIENTATION_PORTRAIT) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_display_orientation(game, UINT32_C(8)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Enabled gestures are a real bit set, so C combines them with its own operators; only the
       undefined bit is refused. */
    if (cna_touch_panel_set_enabled_gestures(
            game, CNA_GESTURE_TYPE_TAP | CNA_GESTURE_TYPE_FLICK) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_enabled_gestures(game, &gestures) != CNA_RESULT_SUCCESS ||
        gestures != (CNA_GESTURE_TYPE_TAP | CNA_GESTURE_TYPE_FLICK) ||
        cna_touch_panel_set_enabled_gestures(game, CNA_GESTURE_TYPE_ALL) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_enabled_gestures(game, &gestures) != CNA_RESULT_SUCCESS ||
        gestures != CNA_GESTURE_TYPE_ALL ||
        cna_touch_panel_set_enabled_gestures(game, CNA_GESTURE_TYPE_PINCH_COMPLETE << 1) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* Forcing the unbound case pins the contract on every backend; whatever a windowed backend
       really published is restored at the end. */
    if (cna_touch_panel_set_window_handle(game, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_window_handle(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0) ||
        cna_touch_panel_set_window_handle(game, UINT64_C(0xFEEDFACE)) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_window_handle(game, &window) != CNA_RESULT_SUCCESS ||
        window != UINT64_C(0xFEEDFACE)) {
        return 0;
    }

    /* The device-exists flag is what the collection's connection getter reads live. */
    if (cna_touch_panel_get_touch_device_exists_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_touch_panel_set_touch_device_exists_ext(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_touch_device_exists_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.is_connected != CNA_TRUE ||
        cna_touch_panel_set_touch_device_exists_ext(game, CNA_FALSE) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* An empty queue is a refusal, not an empty sample: the canonical read throws. */
    memset(&sample, 0, sizeof(sample));
    sample.struct_size = sizeof(CNA_GestureSample);
    sample.struct_version = UINT32_C(1);
    read_back = sample;
    if (cna_touch_panel_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_panel_read_gesture(game, &read_back) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }

    /* Enqueueing is what makes the queue observable with no touch device at all. */
    if (cna_gesture_sample_init_from_values_ext(
            CNA_GESTURE_TYPE_FLICK,
            INT64_C(4242),
            finger_position, zero, finger_position, zero,
            3, CNA_TOUCH_NO_FINGER,
            &sample) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_enqueue_gesture_ext(game, &sample) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_touch_panel_read_gesture(game, &read_back) != CNA_RESULT_SUCCESS ||
        read_back.gesture_type != CNA_GESTURE_TYPE_FLICK ||
        read_back.timestamp_ticks != INT64_C(4242) ||
        read_back.finger_id_ext != 3 || read_back.finger_id2_ext != CNA_TOUCH_NO_FINGER ||
        read_back.position.x != 12.0F || read_back.position.y != 34.0F ||
        read_back.delta.x != 12.0F || read_back.delta.y != 34.0F) {
        return 0;
    }
    /* Reading consumed it, so the queue is empty again and refuses again. */
    if (cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_panel_read_gesture(game, &read_back) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    {
        CNA_GestureSample bad = sample;
        bad.struct_version = UINT32_C(2);
        if (cna_touch_panel_enqueue_gesture_ext(game, &bad) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_touch_panel_enqueue_gesture_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        bad = sample;
        bad.gesture_type = CNA_GESTURE_TYPE_PINCH_COMPLETE << 1;
        if (cna_touch_panel_enqueue_gesture_ext(game, &bad) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        /* A separate scratch value for the expected-failure call: reusing read_back here would
           leave a bad version behind and make every later read refuse. */
        CNA_GestureSample scratch = read_back;
        scratch.struct_version = UINT32_C(2);
        if (cna_touch_panel_read_gesture(game, &scratch) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_touch_panel_read_gesture(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    /* A slot written by hand becomes visible only after the frame is advanced. */
    if (cna_touch_panel_set_finger_ext(game, 0, 5, finger_position) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_finger_ext(game, -1, 5, finger_position) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_set_finger_ext(
            game, (int32_t)CNA_TOUCH_MAX_TOUCHES, 5, finger_position) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_update_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.touch_count != 1U || touches.touches[0].id != 5 ||
        touches.touches[0].position.x != 12.0F || touches.touches[0].position.y != 34.0F) {
        return 0;
    }
    /* Clearing a slot does not make the touch vanish: it is reported once more as released, with
       its previous state carried over, which is the XNA contract for a lifted finger. */
    if (cna_touch_panel_set_finger_ext(game, 0, CNA_TOUCH_NO_FINGER, finger_position) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_update_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.touch_count != 1U ||
        touches.touches[0].state != CNA_TOUCH_LOCATION_RELEASED ||
        touches.touches[0].previous_state != CNA_TOUCH_LOCATION_PRESSED) {
        return 0;
    }

    /* The raised event feeds gesture detection, NOT the slot array the snapshot reports -- the two
       are separate sources and the snapshot stays empty either way, which is asserted rather than
       assumed. While no display size is published the event is dropped outright, because the
       canonical dispatch scales by that size and refuses to collapse every touch onto the origin;
       that drop is a successful no-op, not a failure. */
    if (cna_touch_panel_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_enabled_gestures(game, CNA_GESTURE_TYPE_TAP) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, CNA_TOUCH_LOCATION_PRESSED, 0.5F, 0.5F, 0.0F, 0.0F) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, CNA_TOUCH_LOCATION_RELEASED, 0.5F, 0.5F, 0.0F, 0.0F) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_update_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.touch_count != 0U ||
        cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }

    /* With a real display size the same press/release round trip reaches the detector. Whether it
       decides a gesture happened is the detector's business, so both documented answers are
       accepted -- but if one IS queued, reading it must succeed and must consume it. */
    if (cna_touch_panel_set_display_width(game, 640) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_display_height(game, 480) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, CNA_TOUCH_LOCATION_PRESSED, 0.5F, 0.5F, 0.0F, 0.0F) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, CNA_TOUCH_LOCATION_MOVED, 0.5F, 0.5F, 0.0F, 0.0F) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, CNA_TOUCH_LOCATION_RELEASED, 0.5F, 0.5F, 0.0F, 0.0F) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_raise_touch_event_ext(
            game, 6, UINT32_C(4), 0.5F, 0.5F, 0.0F, 0.0F) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_touch_panel_update_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_get_state(game, &touches) != CNA_RESULT_SUCCESS ||
        touches.touch_count != 0U ||
        cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (flag == CNA_TRUE) {
        if (cna_touch_panel_read_gesture(game, &read_back) != CNA_RESULT_SUCCESS ||
            (read_back.gesture_type & ~CNA_GESTURE_TYPE_ALL) != 0U ||
            cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* Whatever the detector left behind is drained, so nothing leaks into a later validator. */
    while (flag == CNA_TRUE) {
        if (cna_touch_panel_read_gesture(game, &read_back) != CNA_RESULT_SUCCESS ||
            cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }

    /* The reset clears everything the panel owns -- queue, device flag, enabled gestures AND the
       display metrics and window handle. The canonical class comment claims the display size and
       orientation survive; the implementation clears them on purpose, so a leaked display size
       cannot corrupt another test's scaled coordinates. This asserts the behavior, not the
       comment. */
    if (cna_touch_panel_set_display_width(game, 321) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_display_height(game, 123) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_display_orientation(game, CNA_DISPLAY_ORIENTATION_PORTRAIT) !=
            CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_window_handle(game, UINT64_C(0xFEEDFACE)) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_set_enabled_gestures(game, CNA_GESTURE_TYPE_TAP) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_enqueue_gesture_ext(game, &sample) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_reset_for_tests_ext(game) != CNA_RESULT_SUCCESS ||
        cna_touch_panel_get_enabled_gestures(game, &gestures) != CNA_RESULT_SUCCESS ||
        gestures != CNA_GESTURE_TYPE_NONE ||
        cna_touch_panel_get_is_gesture_available(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_panel_get_touch_device_exists_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_touch_panel_get_display_width(game, &value) != CNA_RESULT_SUCCESS || value != 0 ||
        cna_touch_panel_get_display_height(game, &value) != CNA_RESULT_SUCCESS || value != 0 ||
        cna_touch_panel_get_display_orientation(game, &orientation) != CNA_RESULT_SUCCESS ||
        orientation != CNA_DISPLAY_ORIENTATION_DEFAULT ||
        cna_touch_panel_get_window_handle(game, &restored_window) != CNA_RESULT_SUCCESS ||
        restored_window != UINT64_C(0)) {
        return 0;
    }

    /* Everything this suite changed goes back, including whatever window the backend had bound. */
    return cna_touch_panel_set_display_width(game, restored_width) == CNA_RESULT_SUCCESS &&
        cna_touch_panel_set_display_height(game, restored_height) == CNA_RESULT_SUCCESS &&
        cna_touch_panel_set_display_orientation(game, restored_orientation) ==
            CNA_RESULT_SUCCESS &&
        cna_touch_panel_set_window_handle(game, bound) == CNA_RESULT_SUCCESS;
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

    for (CNA_PlayerIndex player = CNA_PLAYER_INDEX_ONE;
         player <= CNA_PLAYER_INDEX_FOUR;
         ++player) {
        CNA_GamePadCapabilities pad_capabilities;
        CNA_GamePadState pad_state = {
            sizeof(CNA_GamePadState), UINT32_C(1), CNA_FALSE, {0U, 0U, 0U}, 0, 0U, 0U,
            {{0.0F, 0.0F}, {0.0F, 0.0F}, 0.0F, 0.0F}
        };
        if (cna_gamepad_capabilities_init(&pad_capabilities) != CNA_RESULT_SUCCESS ||
            cna_gamepad_get_capabilities(game, player, &pad_capabilities) !=
                CNA_RESULT_SUCCESS ||
            pad_capabilities.gamepad_type > CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD ||
            pad_capabilities.reserved[0] != UINT8_C(0) ||
            cna_gamepad_get_state(game, player, &pad_state) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* Connection is one answer, not two: capabilities and state must agree. */
        if (pad_capabilities.is_connected != pad_state.is_connected) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* Nothing is connected in any verification tree, so the canonical disconnected value is
           the expected answer -- but it is asserted only when the slot really is empty. */
        if (pad_capabilities.is_connected == CNA_FALSE &&
            !capabilities_are_disconnected(&pad_capabilities)) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    if (!validate_device_queries(game) || !validate_keyboard_queries(game) ||
        !validate_mouse_queries(game) || !validate_cursor_family(game) ||
        !validate_text_input_family(game) || !validate_touch_panel_family(game)) {
        return CNA_RESULT_INVALID_STATE;
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
    CNA_GamePadCapabilities invalid_capabilities;
    CNA_MouseState invalid_mouse = mouse;
    invalid_mouse.struct_version = UINT32_C(2);
    if (cna_gamepad_capabilities_init(&invalid_capabilities) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (cna_gamepad_get_capabilities(game, UINT32_C(4), &invalid_capabilities) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_gamepad_get_capabilities(game, CNA_PLAYER_INDEX_ONE, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
    invalid_capabilities.struct_version = UINT32_C(2);
    if (cna_gamepad_get_capabilities(game, CNA_PLAYER_INDEX_ONE, &invalid_capabilities) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
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
    CNA_GamePadCapabilities capabilities;
    CNA_TouchState touch = {0};
    touch.struct_size = sizeof(CNA_TouchState);
    touch.struct_version = UINT32_C(1);
    (void)cna_gamepad_capabilities_init(&capabilities);
    state->mouse_result = cna_mouse_get_state(state->game, &mouse);
    state->gamepad_result = cna_gamepad_get_state(
        state->game,
        CNA_PLAYER_INDEX_ONE,
        &gamepad);
    state->capabilities_result = cna_gamepad_get_capabilities(
        state->game,
        CNA_PLAYER_INDEX_ONE,
        &capabilities);
    {
        CNA_Bool applied = CNA_FALSE;
        state->vibration_result = cna_gamepad_set_vibration(
            state->game,
            CNA_PLAYER_INDEX_ONE,
            0.0F,
            0.0F,
            &applied);
    }
    state->touch_result = cna_touch_get_state(state->game, &touch);
    {
        CNA_Bool active = CNA_FALSE;
        state->text_input_result = cna_text_input_is_active_ext(state->game, &active);
        state->touch_panel_result =
            cna_touch_panel_get_is_gesture_available(state->game, &active);
    }
    return 0;
}

int main(void)
{
    /* One code per validator, so a failure names the family it came from. */
    if (!validate_pure_gamepad_helpers()) {
        return 1;
    }
    if (!validate_pure_capabilities_helpers()) {
        return 10;
    }
    if (!validate_pure_button_set_helpers()) {
        return 11;
    }
    if (!validate_pure_dpad_helpers()) {
        return 12;
    }
    if (!validate_pure_analog_value_helpers()) {
        return 13;
    }
    if (!validate_pure_state_value_helpers()) {
        return 14;
    }
    if (!validate_pure_keyboard_value_helpers()) {
        return 15;
    }
    if (!validate_pure_mouse_value_helpers()) {
        return 17;
    }
    if (!validate_pure_dead_zone_exclusion()) {
        return 18;
    }
    if (!validate_pure_touch_helpers()) {
        return 16;
    }
    if (!validate_pure_gesture_value_helpers()) {
        return 19;
    }
    if (!validate_pure_touch_value_helpers()) {
        return 20;
    }
    if (!validate_pure_touch_collection_helpers()) {
        return 21;
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
        game, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS,
        CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS
    };
    thrd_t thread;
    if (thrd_create(&thread, capture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.mouse_result != CNA_RESULT_THREAD ||
        wrong_thread.gamepad_result != CNA_RESULT_THREAD ||
        wrong_thread.capabilities_result != CNA_RESULT_THREAD ||
        wrong_thread.vibration_result != CNA_RESULT_THREAD ||
        wrong_thread.touch_result != CNA_RESULT_THREAD ||
        wrong_thread.text_input_result != CNA_RESULT_THREAD ||
        wrong_thread.touch_panel_result != CNA_RESULT_THREAD) {
        return 3;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    return 0;
}
