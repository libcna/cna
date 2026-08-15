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
    CNA_Result touch_result;
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
    state->touch_result = cna_touch_get_state(state->game, &touch);
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
    if (!validate_pure_touch_helpers()) {
        return 15;
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
        game, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS
    };
    thrd_t thread;
    if (thrd_create(&thread, capture_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.mouse_result != CNA_RESULT_THREAD ||
        wrong_thread.gamepad_result != CNA_RESULT_THREAD ||
        wrong_thread.capabilities_result != CNA_RESULT_THREAD ||
        wrong_thread.touch_result != CNA_RESULT_THREAD) {
        return 3;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 4;
    }
    return 0;
}
