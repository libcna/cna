// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_GAMEPAD_H
#define CNA_C_INPUT_GAMEPAD_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of a gamepad controller type. */
typedef uint32_t CNA_GamePadType;

/** @brief Unknown controller type. */
#define CNA_GAMEPAD_TYPE_UNKNOWN UINT32_C(0)
/** @brief Standard gamepad. */
#define CNA_GAMEPAD_TYPE_GAMEPAD UINT32_C(1)
/** @brief Racing wheel controller. */
#define CNA_GAMEPAD_TYPE_WHEEL UINT32_C(2)
/** @brief Arcade stick controller. */
#define CNA_GAMEPAD_TYPE_ARCADE_STICK UINT32_C(3)
/** @brief Flight stick controller. */
#define CNA_GAMEPAD_TYPE_FLIGHT_STICK UINT32_C(4)
/** @brief Dance pad controller. */
#define CNA_GAMEPAD_TYPE_DANCE_PAD UINT32_C(5)
/** @brief Guitar controller. */
#define CNA_GAMEPAD_TYPE_GUITAR UINT32_C(6)
/** @brief Alternate guitar controller. */
#define CNA_GAMEPAD_TYPE_ALTERNATE_GUITAR UINT32_C(7)
/** @brief Drum kit controller. */
#define CNA_GAMEPAD_TYPE_DRUM_KIT UINT32_C(8)
/** @brief Big button pad controller. */
#define CNA_GAMEPAD_TYPE_BIG_BUTTON_PAD UINT32_C(9)

/**
 * @brief Describes what a gamepad controller physically has.
 *
 * Every field is directly readable and writable, because the canonical type exposes a getter and
 * a setter for each of them. The ten `_ext` fields are CNA extensions with no XNA 4.0 counterpart.
 *
 * A disconnected controller reports every field false with @ref gamepad_type
 * `CNA_GAMEPAD_TYPE_UNKNOWN`, which is exactly what `cna_gamepad_capabilities_init` produces.
 */
typedef struct CNA_GamePadCapabilities {
    /** @brief Size of this structure in bytes; set by @ref cna_gamepad_capabilities_init. */
    uint32_t struct_size;

    /** @brief Structure version; set by @ref cna_gamepad_capabilities_init. */
    uint32_t struct_version;

    /** @brief One `CNA_GAMEPAD_TYPE_*` identity. */
    CNA_GamePadType gamepad_type;

    /** @brief Nonzero when the controller is connected. */
    CNA_Bool is_connected;

    /** @brief Nonzero when the controller has an A button. */
    CNA_Bool has_a_button;
    /** @brief Nonzero when the controller has a B button. */
    CNA_Bool has_b_button;
    /** @brief Nonzero when the controller has an X button. */
    CNA_Bool has_x_button;
    /** @brief Nonzero when the controller has a Y button. */
    CNA_Bool has_y_button;
    /** @brief Nonzero when the controller has a Back button. */
    CNA_Bool has_back_button;
    /** @brief Nonzero when the controller has a Start button. */
    CNA_Bool has_start_button;
    /** @brief Nonzero when the controller has a Big Button. */
    CNA_Bool has_big_button;

    /** @brief Nonzero when the controller has a directional-pad Up button. */
    CNA_Bool has_dpad_up_button;
    /** @brief Nonzero when the controller has a directional-pad Down button. */
    CNA_Bool has_dpad_down_button;
    /** @brief Nonzero when the controller has a directional-pad Left button. */
    CNA_Bool has_dpad_left_button;
    /** @brief Nonzero when the controller has a directional-pad Right button. */
    CNA_Bool has_dpad_right_button;

    /** @brief Nonzero when the controller has a left shoulder button. */
    CNA_Bool has_left_shoulder_button;
    /** @brief Nonzero when the controller has a right shoulder button. */
    CNA_Bool has_right_shoulder_button;
    /** @brief Nonzero when the controller has a left stick button. */
    CNA_Bool has_left_stick_button;
    /** @brief Nonzero when the controller has a right stick button. */
    CNA_Bool has_right_stick_button;

    /** @brief Nonzero when the controller has a left stick X axis. */
    CNA_Bool has_left_x_thumb_stick;
    /** @brief Nonzero when the controller has a left stick Y axis. */
    CNA_Bool has_left_y_thumb_stick;
    /** @brief Nonzero when the controller has a right stick X axis. */
    CNA_Bool has_right_x_thumb_stick;
    /** @brief Nonzero when the controller has a right stick Y axis. */
    CNA_Bool has_right_y_thumb_stick;

    /** @brief Nonzero when the controller has a left trigger. */
    CNA_Bool has_left_trigger;
    /** @brief Nonzero when the controller has a right trigger. */
    CNA_Bool has_right_trigger;
    /** @brief Nonzero when the controller has a left vibration motor. */
    CNA_Bool has_left_vibration_motor;
    /** @brief Nonzero when the controller has a right vibration motor. */
    CNA_Bool has_right_vibration_motor;
    /** @brief Nonzero when the controller supports voice. */
    CNA_Bool has_voice_support;

    /** @brief CNA extension: nonzero when the controller has a light bar. */
    CNA_Bool has_light_bar_ext;
    /** @brief CNA extension: nonzero when the controller has trigger vibration motors. */
    CNA_Bool has_trigger_vibration_motors_ext;
    /** @brief CNA extension: nonzero when the controller has a Misc1 button. */
    CNA_Bool has_misc1_ext;
    /** @brief CNA extension: nonzero when the controller has paddle 1. */
    CNA_Bool has_paddle1_ext;
    /** @brief CNA extension: nonzero when the controller has paddle 2. */
    CNA_Bool has_paddle2_ext;
    /** @brief CNA extension: nonzero when the controller has paddle 3. */
    CNA_Bool has_paddle3_ext;
    /** @brief CNA extension: nonzero when the controller has paddle 4. */
    CNA_Bool has_paddle4_ext;
    /** @brief CNA extension: nonzero when the controller has a touchpad. */
    CNA_Bool has_touchpad_ext;
    /** @brief CNA extension: nonzero when the controller has a gyroscope. */
    CNA_Bool has_gyro_ext;
    /** @brief CNA extension: nonzero when the controller has an accelerometer. */
    CNA_Bool has_accelerometer_ext;

    /** @brief Reserved; set to zero. */
    uint8_t reserved[1];
} CNA_GamePadCapabilities;

/**
 * @brief Initializes gamepad capabilities to the canonical disconnected value.
 *
 * @param out_capabilities Receives the versioned structure with every flag cleared and
 *        @ref CNA_GamePadCapabilities::gamepad_type set to `CNA_GAMEPAD_TYPE_UNKNOWN`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This reproduces the canonical default constructor. It touches no runtime state and may run on
 * any thread.
 */
CNA_C_API CNA_Result cna_gamepad_capabilities_init(CNA_GamePadCapabilities* out_capabilities);

/**
 * @brief Reads what the controller in a player slot physically has.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_capabilities Caller-provided versioned structure to receive the answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A slot with nothing connected reports the same value `cna_gamepad_capabilities_init` produces,
 * so an absent controller is an ordinary answer rather than a failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_capabilities(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_GamePadCapabilities* out_capabilities);


/**
 * @brief Contains the two thumbstick positions of a gamepad snapshot.
 *
 * A plain fixed value with no version prefix, exactly like `CNA_Vector2`, because the canonical
 * type is an immutable pair of positions. It is byte-identical to the first half of
 * @ref CNA_GamePadAnalogState, which is asserted rather than assumed.
 */
typedef struct CNA_GamePadThumbSticks {
    /** @brief Left thumbstick position. */
    CNA_Vector2 left;

    /** @brief Right thumbstick position. */
    CNA_Vector2 right;
} CNA_GamePadThumbSticks;

/**
 * @brief Contains the two trigger positions of a gamepad snapshot.
 *
 * A plain fixed value with no version prefix, for the same reason as
 * @ref CNA_GamePadThumbSticks. It is byte-identical to the second half of
 * @ref CNA_GamePadAnalogState.
 */
typedef struct CNA_GamePadTriggers {
    /** @brief Left trigger in the inclusive range zero through one. */
    float left;

    /** @brief Right trigger in the inclusive range zero through one. */
    float right;
} CNA_GamePadTriggers;

/**
 * @brief Initializes an empty gamepad button set.
 *
 * @param out_buttons Receives `CNA_GAMEPAD_BUTTON_NONE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical button set is a single bit field, so C represents it as the
 * `CNA_GamePadButtonFlags` mask it already had rather than as a second structure. Composing and
 * masking use C's own `|`, `&`, `~`, `|=` and `&=`, which is what the canonical flag operators
 * exist to provide in C++.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_init(CNA_GamePadButtonFlags* out_buttons);

/**
 * @brief Initializes a gamepad button set from an explicit mask.
 *
 * @param buttons Zero or more `CNA_GAMEPAD_BUTTON_*` bits.
 * @param out_buttons Receives the validated mask.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a bit
 *         outside `CNA_GAMEPAD_BUTTON_ALL`. The output is unchanged on failure.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_init_from_mask(
    CNA_GamePadButtonFlags buttons,
    CNA_GamePadButtonFlags* out_buttons);

/**
 * @brief Initializes a gamepad button set by combining an array of button identities.
 *
 * @param buttons Array of `CNA_GAMEPAD_BUTTON_*` identities, or null only when @p count is zero.
 * @param count Number of identities.
 * @param out_buttons Receives the combined mask.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid array, a null
 *         output or a bit outside `CNA_GAMEPAD_BUTTON_ALL`. The output is unchanged on failure.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_init_from_button_array(
    const CNA_GamePadButtonFlags* buttons,
    uint64_t count,
    CNA_GamePadButtonFlags* out_buttons);

/**
 * @brief Tests whether a named button is pressed in a gamepad button set.
 *
 * @param buttons The button set to query.
 * @param button One `CNA_GAMEPAD_BUTTON_*` identity.
 * @param out_is_pressed Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a bit
 *         outside `CNA_GAMEPAD_BUTTON_ALL`.
 *
 * All eleven canonical named property getters collapse into this one route, because the canonical
 * implementation performs the same masked test for each of them.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_is_pressed(
    CNA_GamePadButtonFlags buttons,
    CNA_GamePadButtonFlags button,
    CNA_Bool* out_is_pressed);

/**
 * @brief Compares two gamepad button sets for equality.
 *
 * @param left First button set.
 * @param right Second button set.
 * @param out_equals Receives `CNA_TRUE` when the two sets are equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_equals(
    CNA_GamePadButtonFlags left,
    CNA_GamePadButtonFlags right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two gamepad button sets for inequality.
 *
 * @param left First button set.
 * @param right Second button set.
 * @param out_not_equals Receives `CNA_TRUE` when the two sets differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_not_equals(
    CNA_GamePadButtonFlags left,
    CNA_GamePadButtonFlags right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a gamepad button set.
 *
 * @param buttons The button set to hash.
 * @param out_hash Receives the hash, which is the mask reinterpreted as a signed value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_buttons_get_hash_code(
    CNA_GamePadButtonFlags buttons,
    int32_t* out_hash);

/**
 * @brief Initializes a released directional pad.
 *
 * @param out_dpad Receives `CNA_GAMEPAD_BUTTON_NONE`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * A directional pad is four button states, so C represents it as the same
 * `CNA_GamePadButtonFlags` mask restricted to the four `CNA_GAMEPAD_BUTTON_DPAD_*` bits.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_init(CNA_GamePadButtonFlags* out_dpad);

/**
 * @brief Initializes a directional pad from four explicit button states.
 *
 * @param up Nonzero when the Up direction is pressed.
 * @param down Nonzero when the Down direction is pressed.
 * @param left Nonzero when the Left direction is pressed.
 * @param right Nonzero when the Right direction is pressed.
 * @param out_dpad Receives the resulting mask.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_init_from_states(
    CNA_Bool up,
    CNA_Bool down,
    CNA_Bool left,
    CNA_Bool right,
    CNA_GamePadButtonFlags* out_dpad);

/**
 * @brief Initializes a directional pad by combining an array of button identities.
 *
 * @param buttons Array of `CNA_GAMEPAD_BUTTON_*` identities, or null only when @p count is zero.
 * @param count Number of identities.
 * @param out_dpad Receives the mask of whichever directional bits appear.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid array, a null
 *         output or a bit outside `CNA_GAMEPAD_BUTTON_ALL`. The output is unchanged on failure.
 *
 * Non-directional identities are ignored, exactly as the canonical factory ignores them.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_init_from_button_array(
    const CNA_GamePadButtonFlags* buttons,
    uint64_t count,
    CNA_GamePadButtonFlags* out_dpad);

/**
 * @brief Tests whether a directional-pad direction is pressed.
 *
 * @param dpad The directional pad to query.
 * @param button One `CNA_GAMEPAD_BUTTON_DPAD_*` identity.
 * @param out_is_pressed Receives the answer.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a button
 *         that is not one of the four directional identities.
 *
 * All four canonical named property getters collapse into this one route.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_is_pressed(
    CNA_GamePadButtonFlags dpad,
    CNA_GamePadButtonFlags button,
    CNA_Bool* out_is_pressed);

/**
 * @brief Compares two directional pads for equality.
 *
 * @param left First directional pad.
 * @param right Second directional pad.
 * @param out_equals Receives `CNA_TRUE` when the two compare equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * Only the four directional bits participate, because they are the only state the canonical type
 * holds.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_equals(
    CNA_GamePadButtonFlags left,
    CNA_GamePadButtonFlags right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two directional pads for inequality.
 *
 * @param left First directional pad.
 * @param right Second directional pad.
 * @param out_not_equals Receives `CNA_TRUE` when the two differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_not_equals(
    CNA_GamePadButtonFlags left,
    CNA_GamePadButtonFlags right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a directional pad.
 *
 * @param dpad The directional pad to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical hash weights the directions Down 1, Left 2, Right 4 and Up 8 — its own numbering,
 * not the button bits — and that weighting is reproduced rather than replaced by the raw mask.
 */
CNA_C_API CNA_Result cna_gamepad_dpad_get_hash_code(
    CNA_GamePadButtonFlags dpad,
    int32_t* out_hash);

/**
 * @brief Initializes both thumbsticks to the centre.
 *
 * @param out_thumb_sticks Receives two zero positions.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_thumb_sticks_init(CNA_GamePadThumbSticks* out_thumb_sticks);

/**
 * @brief Initializes both thumbsticks from explicit positions.
 *
 * @param left Left thumbstick position.
 * @param right Right thumbstick position.
 * @param out_thumb_sticks Receives the pair.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 *
 * The canonical public constructor applies a **square clamp**: each component is limited to the
 * inclusive range minus one through one. It applies no dead zone — that belongs to
 * `cna_gamepad_apply_dead_zone` and to the capture routes, not here.
 */
CNA_C_API CNA_Result cna_gamepad_thumb_sticks_init_from_positions(
    const CNA_Vector2* left,
    const CNA_Vector2* right,
    CNA_GamePadThumbSticks* out_thumb_sticks);

/**
 * @brief Compares two thumbstick pairs for equality.
 *
 * @param left First pair.
 * @param right Second pair.
 * @param out_equals Receives `CNA_TRUE` when both positions compare equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 *
 * Positions are compared exactly, component by component; only the trigger comparison uses an
 * epsilon.
 */
CNA_C_API CNA_Result cna_gamepad_thumb_sticks_equals(
    const CNA_GamePadThumbSticks* left,
    const CNA_GamePadThumbSticks* right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two thumbstick pairs for inequality.
 *
 * @param left First pair.
 * @param right Second pair.
 * @param out_not_equals Receives `CNA_TRUE` when the pairs differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 */
CNA_C_API CNA_Result cna_gamepad_thumb_sticks_not_equals(
    const CNA_GamePadThumbSticks* left,
    const CNA_GamePadThumbSticks* right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a thumbstick pair.
 *
 * @param thumb_sticks The pair to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 */
CNA_C_API CNA_Result cna_gamepad_thumb_sticks_get_hash_code(
    const CNA_GamePadThumbSticks* thumb_sticks,
    int32_t* out_hash);

/**
 * @brief Initializes both triggers to zero.
 *
 * @param out_triggers Receives two zero positions.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_triggers_init(CNA_GamePadTriggers* out_triggers);

/**
 * @brief Initializes both triggers from explicit positions.
 *
 * @param left Left trigger position.
 * @param right Right trigger position.
 * @param out_triggers Receives the pair.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical public constructor **clamps** each value to the inclusive range zero through one.
 * It applies no dead zone.
 */
CNA_C_API CNA_Result cna_gamepad_triggers_init_from_positions(
    float left,
    float right,
    CNA_GamePadTriggers* out_triggers);

/**
 * @brief Compares two trigger pairs for equality.
 *
 * @param left First pair.
 * @param right Second pair.
 * @param out_equals Receives `CNA_TRUE` when both triggers compare equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 *
 * The canonical comparison is an epsilon comparison, not an exact one: two distinct floats
 * closer together than the machine epsilon still compare equal.
 */
CNA_C_API CNA_Result cna_gamepad_triggers_equals(
    const CNA_GamePadTriggers* left,
    const CNA_GamePadTriggers* right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two trigger pairs for inequality.
 *
 * @param left First pair.
 * @param right Second pair.
 * @param out_not_equals Receives `CNA_TRUE` when the pairs differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 */
CNA_C_API CNA_Result cna_gamepad_triggers_not_equals(
    const CNA_GamePadTriggers* left,
    const CNA_GamePadTriggers* right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a trigger pair.
 *
 * @param triggers The pair to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output.
 */
CNA_C_API CNA_Result cna_gamepad_triggers_get_hash_code(
    const CNA_GamePadTriggers* triggers,
    int32_t* out_hash);

/**
 * @brief Initializes a disconnected gamepad snapshot.
 *
 * @param out_state Receives the versioned structure with every field cleared.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_init(CNA_GamePadState* out_state);

/**
 * @brief Initializes a connected gamepad snapshot from its four component values.
 *
 * @param thumb_sticks Thumbstick positions.
 * @param triggers Trigger positions.
 * @param buttons Pressed button mask.
 * @param dpad Directional-pad mask.
 * @param out_state Receives the versioned structure.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null input or output or a
 *         bit outside `CNA_GAMEPAD_BUTTON_ALL`. The output is unchanged on failure.
 *
 * The canonical constructor reports the snapshot as connected and **derives extra button bits**:
 * a trigger past `CNA_GAMEPAD_TRIGGER_THRESHOLD` adds its trigger bit, and a stick past its
 * dead zone adds the matching virtual direction bits. Those derived bits are part of the value,
 * not a convenience, so they are reproduced here.
 *
 * The C snapshot carries a **single** button mask, and so does every state CNA itself builds: the
 * capture path derives both the button set and the directional pad from one raw mask. @p dpad is
 * therefore merged into the button set rather than kept beside it, which is what makes it readable
 * again through `cna_gamepad_state_get_dpad`. A snapshot whose pad disagrees with its button set
 * is not representable here, and is not a state CNA produces.
 */
CNA_C_API CNA_Result cna_gamepad_state_init_from_components(
    const CNA_GamePadThumbSticks* thumb_sticks,
    const CNA_GamePadTriggers* triggers,
    CNA_GamePadButtonFlags buttons,
    CNA_GamePadButtonFlags dpad,
    CNA_GamePadState* out_state);

/**
 * @brief Initializes a connected gamepad snapshot from raw positions and a button array.
 *
 * @param left_thumb_stick Left thumbstick position.
 * @param right_thumb_stick Right thumbstick position.
 * @param left_trigger Left trigger position.
 * @param right_trigger Right trigger position.
 * @param buttons Array of `CNA_GAMEPAD_BUTTON_*` identities, or null only when @p count is zero.
 * @param count Number of identities.
 * @param out_state Receives the versioned structure.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid array, a null
 *         input or output, or a bit outside `CNA_GAMEPAD_BUTTON_ALL`. The output is unchanged on
 *         failure.
 *
 * This is the canonical convenience constructor: the array feeds both the button set and the
 * directional pad, and the same derived trigger and stick bits are added.
 */
CNA_C_API CNA_Result cna_gamepad_state_init_from_values(
    const CNA_Vector2* left_thumb_stick,
    const CNA_Vector2* right_thumb_stick,
    float left_trigger,
    float right_trigger,
    const CNA_GamePadButtonFlags* buttons,
    uint64_t count,
    CNA_GamePadState* out_state);

/**
 * @brief Reads the button set of a gamepad snapshot.
 *
 * @param state Snapshot to query.
 * @param out_buttons Receives the pressed-button mask.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_buttons(
    const CNA_GamePadState* state,
    CNA_GamePadButtonFlags* out_buttons);

/**
 * @brief Reads the directional pad of a gamepad snapshot.
 *
 * @param state Snapshot to query.
 * @param out_dpad Receives the four directional bits.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_dpad(
    const CNA_GamePadState* state,
    CNA_GamePadButtonFlags* out_dpad);

/**
 * @brief Reads the thumbstick pair of a gamepad snapshot.
 *
 * @param state Snapshot to query.
 * @param out_thumb_sticks Receives the pair.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_thumb_sticks(
    const CNA_GamePadState* state,
    CNA_GamePadThumbSticks* out_thumb_sticks);

/**
 * @brief Reads the trigger pair of a gamepad snapshot.
 *
 * @param state Snapshot to query.
 * @param out_triggers Receives the pair.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_triggers(
    const CNA_GamePadState* state,
    CNA_GamePadTriggers* out_triggers);

/**
 * @brief Sets the packet number of a gamepad snapshot.
 *
 * @param state Snapshot to modify.
 * @param packet_number New packet number.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot.
 *
 * This maps a CNA extension setter: the canonical XNA 4.0 packet number is read-only.
 */
CNA_C_API CNA_Result cna_gamepad_state_set_packet_number_ext(
    CNA_GamePadState* state,
    int32_t packet_number);

/**
 * @brief Compares two gamepad snapshots for equality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_equals Receives `CNA_TRUE` when every canonical component compares equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_equals(
    const CNA_GamePadState* left,
    const CNA_GamePadState* right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two gamepad snapshots for inequality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_not_equals Receives `CNA_TRUE` when the snapshots differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_not_equals(
    const CNA_GamePadState* left,
    const CNA_GamePadState* right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a gamepad snapshot.
 *
 * @param state Snapshot to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 *
 * The canonical hash mixes only the button set and the packet number; the sticks, triggers and
 * directional pad do not contribute, so two snapshots that differ only in those fields hash
 * alike.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_hash_code(
    const CNA_GamePadState* state,
    int32_t* out_hash);

/**
 * @brief Reports the byte length of a gamepad snapshot's text, without a terminator.
 *
 * @param state Snapshot to describe.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_gamepad_state_get_string_size(
    const CNA_GamePadState* state,
    uint64_t* out_bytes);

/**
 * @brief Copies a gamepad snapshot's text without a terminator.
 *
 * @param state Snapshot to describe.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or
 *         `CNA_RESULT_INVALID_ARGUMENT`. No partial value is written.
 *
 * The canonical type does not override its string conversion, so the text is the fixed
 * fully-qualified type name and never reflects any field value.
 */
CNA_C_API CNA_Result cna_gamepad_state_copy_string(
    const CNA_GamePadState* state,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/** @brief Fixed-width identity of the glyph a controller prints on a face button. */
typedef uint32_t CNA_GamePadButtonLabel;

/** @brief The label is unknown. */
#define CNA_GAMEPAD_BUTTON_LABEL_UNKNOWN UINT32_C(0)
/** @brief Labelled `A`. */
#define CNA_GAMEPAD_BUTTON_LABEL_A UINT32_C(1)
/** @brief Labelled `B`. */
#define CNA_GAMEPAD_BUTTON_LABEL_B UINT32_C(2)
/** @brief Labelled `X`. */
#define CNA_GAMEPAD_BUTTON_LABEL_X UINT32_C(3)
/** @brief Labelled `Y`. */
#define CNA_GAMEPAD_BUTTON_LABEL_Y UINT32_C(4)
/** @brief Labelled with a cross. */
#define CNA_GAMEPAD_BUTTON_LABEL_CROSS UINT32_C(5)
/** @brief Labelled with a circle. */
#define CNA_GAMEPAD_BUTTON_LABEL_CIRCLE UINT32_C(6)
/** @brief Labelled with a square. */
#define CNA_GAMEPAD_BUTTON_LABEL_SQUARE UINT32_C(7)
/** @brief Labelled with a triangle. */
#define CNA_GAMEPAD_BUTTON_LABEL_TRIANGLE UINT32_C(8)

/** @brief Fixed-width identity of how a controller is attached. */
typedef uint32_t CNA_GamePadConnectionState;

/** @brief The attachment is unknown. */
#define CNA_GAMEPAD_CONNECTION_STATE_UNKNOWN UINT32_C(0)
/** @brief Attached by cable. */
#define CNA_GAMEPAD_CONNECTION_STATE_WIRED UINT32_C(1)
/** @brief Attached wirelessly. */
#define CNA_GAMEPAD_CONNECTION_STATE_WIRELESS UINT32_C(2)

/** @brief Fixed-width identity of a device's power situation. */
typedef uint32_t CNA_PowerState;

/** @brief The power state could not be determined because the query failed. */
#define CNA_POWER_STATE_ERROR UINT32_C(0)
/** @brief The power state is unknown. */
#define CNA_POWER_STATE_UNKNOWN UINT32_C(1)
/** @brief Running on battery. */
#define CNA_POWER_STATE_ON_BATTERY UINT32_C(2)
/** @brief No battery is present. */
#define CNA_POWER_STATE_NO_BATTERY UINT32_C(3)
/** @brief Charging. */
#define CNA_POWER_STATE_CHARGING UINT32_C(4)
/** @brief Fully charged. */
#define CNA_POWER_STATE_CHARGED UINT32_C(5)

/**
 * @brief Describes one finger on a controller touchpad.
 *
 * A plain fixed value, because the canonical query reports the same four numbers through separate
 * output references rather than through an object.
 */
typedef struct CNA_GamePadTouchpadFinger {
    /** @brief Nonzero while the finger is touching the pad. */
    CNA_Bool is_down;

    /** @brief Reserved; set to zero. */
    uint8_t reserved[3];

    /** @brief Normalized X position on the pad. */
    float x;

    /** @brief Normalized Y position on the pad. */
    float y;

    /** @brief Normalized pressure. */
    float pressure;
} CNA_GamePadTouchpadFinger;

/**
 * @brief Applies CNA's canonical axis dead-zone exclusion to a single value.
 *
 * @param value Raw axis value.
 * @param dead_zone Dead-zone threshold, such as `CNA_GAMEPAD_LEFT_DEAD_ZONE`.
 * @param out_value Receives the processed value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a
 *         non-finite input. The output is unchanged on failure.
 *
 * This pure value operation touches no runtime state and may run on any thread. The three
 * canonical thresholds are already exposed as `CNA_GAMEPAD_LEFT_DEAD_ZONE`,
 * `CNA_GAMEPAD_RIGHT_DEAD_ZONE` and `CNA_GAMEPAD_TRIGGER_THRESHOLD`.
 */
CNA_C_API CNA_Result cna_gamepad_exclude_axis_dead_zone(
    float value,
    float dead_zone,
    float* out_value);

/**
 * @brief Sets the two rumble motors of a controller.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param left_motor Left motor strength.
 * @param right_motor Right motor strength.
 * @param out_applied Receives `CNA_TRUE` when the device accepted the request.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A slot with nothing connected, or a controller without rumble, reports `CNA_FALSE` rather than
 * failing — refusal is the canonical answer, not an error.
 */
CNA_C_API CNA_Result cna_gamepad_set_vibration(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    float left_motor,
    float right_motor,
    CNA_Bool* out_applied);

/**
 * @brief Sets the light bar color of a controller.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param color Requested color.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical operation returns nothing, so a controller without a light bar is silently
 * unaffected and this route still succeeds.
 */
CNA_C_API CNA_Result cna_gamepad_set_light_bar_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_Color color);

/**
 * @brief Sets the two trigger rumble motors of a controller.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param left_trigger Left trigger motor strength.
 * @param right_trigger Right trigger motor strength.
 * @param out_applied Receives `CNA_TRUE` when the device accepted the request.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_set_trigger_vibration_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    float left_trigger,
    float right_trigger,
    CNA_Bool* out_applied);

/**
 * @brief Reads a controller's gyroscope.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_gyro Receives the reading; left unchanged when none is available.
 * @param out_available Receives `CNA_TRUE` when a reading was produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical query reports availability through its return value and the reading through an
 * output reference, so the C route keeps both answers separate rather than folding "no sensor"
 * into a failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_gyro_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_Vector3* out_gyro,
    CNA_Bool* out_available);

/**
 * @brief Reads a controller's accelerometer.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_acceleration Receives the reading; left unchanged when none is available.
 * @param out_available Receives `CNA_TRUE` when a reading was produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_accelerometer_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_Vector3* out_acceleration,
    CNA_Bool* out_available);

/**
 * @brief Reads the player index a controller reports for itself.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_index Receives the device's own index, which is negative when it has none.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_player_index_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    int32_t* out_index);

/**
 * @brief Sets the player index a controller reports for itself.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param index New device index.
 * @param out_applied Receives `CNA_TRUE` when the device accepted the request.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_set_player_index_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    int32_t index,
    CNA_Bool* out_applied);

/**
 * @brief Reads a controller's power situation and remaining charge.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_state Receives one `CNA_POWER_STATE_*` identity.
 * @param out_percent Receives the remaining charge percentage, which is negative when unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * `CNA_POWER_STATE_ERROR` is a canonical answer the query itself produces, not a C failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_power_info_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_PowerState* out_state,
    int32_t* out_percent);

/**
 * @brief Reads the glyph a controller prints on a face button.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param button One `CNA_GAMEPAD_BUTTON_*` identity.
 * @param out_label Receives one `CNA_GAMEPAD_BUTTON_LABEL_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A button the controller does not label reports `CNA_GAMEPAD_BUTTON_LABEL_UNKNOWN`.
 */
CNA_C_API CNA_Result cna_gamepad_get_button_label_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_GamePadButtonFlags button,
    CNA_GamePadButtonLabel* out_label);

/**
 * @brief Reports the length of a controller's stable device identifier, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_guid_size_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a controller's stable device identifier without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 *
 * An empty slot reports zero bytes, which is an ordinary answer rather than a failure.
 */
CNA_C_API CNA_Result cna_gamepad_copy_guid_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the length of a controller's display name, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_name_size_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a controller's display name without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 */
CNA_C_API CNA_Result cna_gamepad_copy_name_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the length of a controller's device path, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_path_size_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a controller's device path without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 */
CNA_C_API CNA_Result cna_gamepad_copy_path_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports the length of a controller's serial number, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_serial_size_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint64_t* out_bytes);

/**
 * @brief Copies a controller's serial number without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No
 *         partial value is written.
 */
CNA_C_API CNA_Result cna_gamepad_copy_serial_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reads a controller's firmware version.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_version Receives the version, which is zero when the device reports none.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_firmware_version_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint16_t* out_version);

/**
 * @brief Reads a controller's Steam input handle.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_handle Receives the handle, which is zero when the device has none.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is an opaque native identifier, not a `CNA_Handle`, and the C API never dereferences
 * it.
 */
CNA_C_API CNA_Result cna_gamepad_get_steam_handle_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    uint64_t* out_handle);

/**
 * @brief Reads how a controller is attached.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_state Receives one `CNA_GAMEPAD_CONNECTION_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_connection_state_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    CNA_GamePadConnectionState* out_state);

/**
 * @brief Reads how many touchpads a controller has.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param out_count Receives the touchpad count, which is zero when there are none.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_touchpad_count_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    int32_t* out_count);

/**
 * @brief Reads how many fingers one touchpad can track.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param touchpad Zero-based touchpad index.
 * @param out_count Receives the finger count, which is zero when the touchpad does not exist.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_touchpad_finger_count_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    int32_t touchpad,
    int32_t* out_count);

/**
 * @brief Reads one finger on one touchpad.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param player_index Player slot in the inclusive range one through four.
 * @param touchpad Zero-based touchpad index.
 * @param finger Zero-based finger index.
 * @param out_finger Receives the reading; left unchanged when none is available.
 * @param out_available Receives `CNA_TRUE` when a reading was produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical query reports four separate values through output references; C collects them into
 * one fixed value and keeps the availability answer separate, so an absent touchpad or finger is
 * an ordinary answer rather than a failure.
 */
CNA_C_API CNA_Result cna_gamepad_get_touchpad_finger_ext(
    CNA_Handle game,
    CNA_PlayerIndex player_index,
    int32_t touchpad,
    int32_t finger,
    CNA_GamePadTouchpadFinger* out_finger,
    CNA_Bool* out_available);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_GAMEPAD_H
