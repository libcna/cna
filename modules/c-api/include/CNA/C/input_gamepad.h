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

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_GAMEPAD_H
