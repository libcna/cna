// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_HAPTICS_H
#define CNA_C_INPUT_HAPTICS_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Owned handle to an opened force-feedback device.
 *
 * A device that failed to open is still a real handle: the open routes always succeed and
 * `cna_haptic_device_get_is_open` reports whether hardware is actually behind it. Every other
 * route on a closed device is a safe no-op that reports `CNA_FALSE`, exactly as the canonical
 * class behaves.
 */
typedef CNA_Handle CNA_HapticDeviceHandle;

/** @brief Fixed-width haptic capability bit set. */
typedef uint32_t CNA_HapticFeature;

/** @brief No effect families or global capabilities supported. */
#define CNA_HAPTIC_FEATURE_NONE UINT32_C(0)
/** @brief Constant-force effects. */
#define CNA_HAPTIC_FEATURE_CONSTANT UINT32_C(0x00000001)
/** @brief Sine-wave periodic effects. */
#define CNA_HAPTIC_FEATURE_SINE UINT32_C(0x00000002)
/** @brief Square-wave periodic effects. */
#define CNA_HAPTIC_FEATURE_SQUARE UINT32_C(0x00000004)
/** @brief Triangle-wave periodic effects. */
#define CNA_HAPTIC_FEATURE_TRIANGLE UINT32_C(0x00000008)
/** @brief Upward-sawtooth periodic effects. */
#define CNA_HAPTIC_FEATURE_SAWTOOTH_UP UINT32_C(0x00000010)
/** @brief Downward-sawtooth periodic effects. */
#define CNA_HAPTIC_FEATURE_SAWTOOTH_DOWN UINT32_C(0x00000020)
/** @brief Ramp effects. */
#define CNA_HAPTIC_FEATURE_RAMP UINT32_C(0x00000040)
/** @brief Spring condition effects. */
#define CNA_HAPTIC_FEATURE_SPRING UINT32_C(0x00000080)
/** @brief Damper condition effects. */
#define CNA_HAPTIC_FEATURE_DAMPER UINT32_C(0x00000100)
/** @brief Inertia condition effects. */
#define CNA_HAPTIC_FEATURE_INERTIA UINT32_C(0x00000200)
/** @brief Friction condition effects. */
#define CNA_HAPTIC_FEATURE_FRICTION UINT32_C(0x00000400)
/** @brief Left/right large- and small-motor effects. */
#define CNA_HAPTIC_FEATURE_LEFT_RIGHT UINT32_C(0x00000800)
/** @brief Custom raw-sample-buffer effects. */
#define CNA_HAPTIC_FEATURE_CUSTOM UINT32_C(0x00008000)
/** @brief Overall effect gain can be set. */
#define CNA_HAPTIC_FEATURE_GAIN UINT32_C(0x00010000)
/** @brief Autocenter strength can be set. */
#define CNA_HAPTIC_FEATURE_AUTOCENTER UINT32_C(0x00020000)
/** @brief Effect play/stop status can be queried. */
#define CNA_HAPTIC_FEATURE_STATUS UINT32_C(0x00040000)
/** @brief Effects can be paused and resumed. */
#define CNA_HAPTIC_FEATURE_PAUSE UINT32_C(0x00080000)
/** @brief Every defined capability bit combined. */
#define CNA_HAPTIC_FEATURE_ALL UINT32_C(0x000F8FFF)

/** @brief Fixed-width haptic effect-family identity. */
typedef uint32_t CNA_HapticEffectType;

/** @brief A steady directional push. */
#define CNA_HAPTIC_EFFECT_TYPE_CONSTANT UINT32_C(0)
/** @brief A sine-wave periodic effect. */
#define CNA_HAPTIC_EFFECT_TYPE_SINE UINT32_C(1)
/** @brief A square-wave periodic effect. */
#define CNA_HAPTIC_EFFECT_TYPE_SQUARE UINT32_C(2)
/** @brief A triangle-wave periodic effect. */
#define CNA_HAPTIC_EFFECT_TYPE_TRIANGLE UINT32_C(3)
/** @brief An upward-sawtooth periodic effect. */
#define CNA_HAPTIC_EFFECT_TYPE_SAWTOOTH_UP UINT32_C(4)
/** @brief A downward-sawtooth periodic effect. */
#define CNA_HAPTIC_EFFECT_TYPE_SAWTOOTH_DOWN UINT32_C(5)
/** @brief A linear start-to-end magnitude ramp. */
#define CNA_HAPTIC_EFFECT_TYPE_RAMP UINT32_C(6)
/** @brief Spring-like resistance based on axis position. */
#define CNA_HAPTIC_EFFECT_TYPE_SPRING UINT32_C(7)
/** @brief Damper-like resistance based on axis velocity. */
#define CNA_HAPTIC_EFFECT_TYPE_DAMPER UINT32_C(8)
/** @brief Inertia-like resistance based on axis acceleration. */
#define CNA_HAPTIC_EFFECT_TYPE_INERTIA UINT32_C(9)
/** @brief Friction-like resistance based on axis movement. */
#define CNA_HAPTIC_EFFECT_TYPE_FRICTION UINT32_C(10)
/** @brief Explicit large- and small-motor control. */
#define CNA_HAPTIC_EFFECT_TYPE_LEFT_RIGHT UINT32_C(11)
/** @brief A caller-defined raw waveform sample buffer. */
#define CNA_HAPTIC_EFFECT_TYPE_CUSTOM UINT32_C(12)
/** @brief Highest defined effect-family identity. */
#define CNA_HAPTIC_EFFECT_TYPE_MAXIMUM CNA_HAPTIC_EFFECT_TYPE_CUSTOM

/** @brief Fixed-width haptic direction coordinate-system identity. */
typedef uint32_t CNA_HapticDirectionType;

/** @brief A single polar angle in hundredths of a degree, clockwise from north. */
#define CNA_HAPTIC_DIRECTION_TYPE_POLAR UINT32_C(0)
/** @brief An (X, Y, Z) cartesian vector. */
#define CNA_HAPTIC_DIRECTION_TYPE_CARTESIAN UINT32_C(1)
/** @brief Two spherical rotation angles. */
#define CNA_HAPTIC_DIRECTION_TYPE_SPHERICAL UINT32_C(2)
/** @brief The device's steering-wheel axis. */
#define CNA_HAPTIC_DIRECTION_TYPE_STEERING_AXIS UINT32_C(3)
/** @brief Highest defined direction-type identity. */
#define CNA_HAPTIC_DIRECTION_TYPE_MAXIMUM CNA_HAPTIC_DIRECTION_TYPE_STEERING_AXIS

/** @brief An effect length meaning "play forever". */
#define CNA_HAPTIC_EFFECT_INFINITE_LENGTH UINT32_C(4294967295)

/**
 * @brief The direction a haptic effect's force comes from.
 *
 * How many of @ref values are meaningful depends on @ref type: one for polar and steering-axis,
 * three for cartesian, two for spherical. The unused components are carried unchanged.
 */
typedef struct CNA_HapticDirection {
    /** @brief One `CNA_HAPTIC_DIRECTION_TYPE_*` identity. */
    CNA_HapticDirectionType type;

    /** @brief The encoded direction components. */
    int32_t values[3];
} CNA_HapticDirection;

/**
 * @brief The static hardware shape of a haptic device.
 *
 * The canonical value also carries the device name. C deliberately leaves it out of this fixed
 * value and exposes it through `cna_haptic_device_get_name_size`/`_copy_name` instead, so no
 * string lives inside a POD. `cna_haptic_capabilities_equals` therefore takes the two names
 * alongside the two values, and reproduces the canonical comparison exactly.
 */
typedef struct CNA_HapticCapabilities {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The effect families and global capabilities the device supports. */
    CNA_HapticFeature features;

    /** @brief Number of axes the device reports. */
    int32_t axis_count;

    /** @brief Maximum stored effects, or -1 when the device is closed or the count is unknown. */
    int32_t max_effects;

    /** @brief Maximum simultaneously playing effects, or -1 when closed or unknown. */
    int32_t max_effects_playing;

    /** @brief `CNA_TRUE` when the device handle is currently open. */
    CNA_Bool is_open;

    /** @brief `CNA_TRUE` when the simple rumble convenience is supported. */
    CNA_Bool rumble_supported;

    /** @brief Reserved padding; always zero. */
    uint8_t reserved[2];
} CNA_HapticCapabilities;

/**
 * @brief Describes one force-feedback effect of any family.
 *
 * This is one flattened value rather than six near-identical ones: @ref type selects the family
 * and only the fields documented for that family are meaningful, exactly as the canonical
 * descriptor works. The remaining fields are carried unchanged and ignored when the effect is
 * built.
 *
 * Field applicability by @ref type:
 * - **Constant**: `direction`, `length`, `delay`, `button`, `interval`, `level`, envelope.
 * - **Sine/Square/Triangle/SawtoothUp/SawtoothDown**: `direction`, `length`, `delay`, `button`,
 *   `interval`, `period`, `magnitude`, `offset`, `phase`, envelope.
 * - **Ramp**: `direction`, `length`, `delay`, `button`, `interval`, `ramp_start`, `ramp_end`,
 *   envelope.
 * - **Spring/Damper/Inertia/Friction**: `length`, `delay`, `button`, `interval` and the six
 *   per-axis condition arrays. No envelope, and the direction is unused because the condition
 *   internals handle direction per axis.
 * - **LeftRight**: `length`, `large_magnitude`, `small_magnitude`. Nothing else applies.
 * - **Custom**: `direction`, `length`, `delay`, `button`, `interval`, `custom_channels`,
 *   `custom_period`, the separately supplied sample buffer, envelope.
 *
 * The canonical descriptor carries its custom waveform inside itself. C passes that buffer
 * **alongside** the value instead — every route taking an effect takes a `custom_data` pointer and
 * a sample count — so this structure stays a plain value a caller can copy and compare without
 * owning heap memory.
 */
typedef struct CNA_HapticEffect {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief One `CNA_HAPTIC_EFFECT_TYPE_*` identity. */
    CNA_HapticEffectType type;

    /** @brief Reserved padding; always zero. */
    uint32_t reserved;

    /** @brief Direction the force comes from. */
    CNA_HapticDirection direction;

    /** @brief Duration in milliseconds, or @ref CNA_HAPTIC_EFFECT_INFINITE_LENGTH. */
    uint32_t length;

    /** @brief Delay before the effect starts, in milliseconds. */
    uint16_t delay;

    /** @brief One-based button index that triggers the effect, or zero for none. */
    uint16_t button;

    /** @brief Minimum time between button-triggered replays, in milliseconds. */
    uint16_t interval;

    /** @brief Strength of a constant effect. */
    int16_t level;

    /** @brief Wave period of a periodic effect, in milliseconds. */
    uint16_t period;

    /** @brief Peak value of a periodic effect; a negative value shifts the phase by 180 degrees. */
    int16_t magnitude;

    /** @brief Mean offset of a periodic effect's wave. */
    int16_t offset;

    /** @brief Positive phase shift of a periodic effect, in hundredths of a degree. */
    uint16_t phase;

    /** @brief Starting strength of a ramp effect. */
    int16_t ramp_start;

    /** @brief Ending strength of a ramp effect. */
    int16_t ramp_end;

    /** @brief Per-axis positive-side saturation, for condition effects. */
    uint16_t right_saturation[3];

    /** @brief Per-axis negative-side saturation, for condition effects. */
    uint16_t left_saturation[3];

    /** @brief Per-axis positive-side force growth rate, for condition effects. */
    int16_t right_coefficient[3];

    /** @brief Per-axis negative-side force growth rate, for condition effects. */
    int16_t left_coefficient[3];

    /** @brief Per-axis dead-zone size, for condition effects. */
    uint16_t deadband[3];

    /** @brief Per-axis dead-zone center, for condition effects. */
    int16_t center[3];

    /** @brief Large (low-frequency) motor strength, for a left/right effect. */
    uint16_t large_magnitude;

    /** @brief Small (high-frequency) motor strength, for a left/right effect. */
    uint16_t small_magnitude;

    /** @brief Sample period of a custom waveform, in milliseconds. */
    uint16_t custom_period;

    /** @brief Number of axes a custom waveform drives; one rotates using the direction. */
    uint8_t custom_channels;

    /** @brief Reserved padding; always zero. */
    uint8_t reserved2;

    /** @brief Duration of the attack ramp-in, in milliseconds. */
    uint16_t attack_length;

    /** @brief Effect level at the start of the attack. */
    uint16_t attack_level;

    /** @brief Duration of the fade ramp-out, in milliseconds. */
    uint16_t fade_length;

    /** @brief Effect level at the end of the fade. */
    uint16_t fade_level;
} CNA_HapticEffect;

/**
 * @brief Initializes a haptic direction to the canonical default.
 *
 * @param out_direction Receives a polar direction with zeroed components.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_haptic_direction_init(CNA_HapticDirection* out_direction);

/**
 * @brief Compares two haptic directions.
 *
 * @param left First direction.
 * @param right Second direction.
 * @param out_equal Receives `CNA_TRUE` when the type and all three components match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument.
 *
 * All three components are compared whatever the type says is meaningful, exactly as the canonical
 * comparison does. The canonical `!=` is this comparison negated and needs no route of its own.
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_haptic_direction_equals(
    const CNA_HapticDirection* left,
    const CNA_HapticDirection* right,
    CNA_Bool* out_equal);

/**
 * @brief Initializes a haptic effect to the canonical defaults.
 *
 * @param out_effect Receives a zeroed constant effect with a polar direction.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_haptic_effect_init(CNA_HapticEffect* out_effect);

/**
 * @brief Compares two haptic effects, including their custom sample buffers.
 *
 * @param left First effect.
 * @param left_custom_data First effect's custom samples, or null when @p left_custom_count is zero.
 * @param left_custom_count Number of samples in @p left_custom_data.
 * @param right Second effect.
 * @param right_custom_data Second effect's custom samples, or null when its count is zero.
 * @param right_custom_count Number of samples in @p right_custom_data.
 * @param out_equal Receives `CNA_TRUE` when every field and every sample matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure or a
 *         null argument.
 *
 * Every field is compared regardless of which family the type selects, exactly as the canonical
 * comparison does — two effects that would play identically can still compare unequal because a
 * field their family ignores differs. This pure POD operation touches no runtime state and may run
 * on any thread.
 */
CNA_C_API CNA_Result cna_haptic_effect_equals(
    const CNA_HapticEffect* left,
    const uint16_t* left_custom_data,
    uint64_t left_custom_count,
    const CNA_HapticEffect* right,
    const uint16_t* right_custom_data,
    uint64_t right_custom_count,
    CNA_Bool* out_equal);

/**
 * @brief Initializes haptic capabilities to the canonical defaults.
 *
 * @param out_capabilities Receives a closed device's capabilities.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The two effect-count fields default to **-1**, not zero, exactly as the canonical value does:
 * a closed device reports "unknown", which is deliberately distinguishable from "none".
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_haptic_capabilities_init(CNA_HapticCapabilities* out_capabilities);

/**
 * @brief Compares two haptic capability snapshots, including their names.
 *
 * @param left First value.
 * @param left_name First value's device name.
 * @param right Second value.
 * @param right_name Second value's device name.
 * @param out_equal Receives `CNA_TRUE` when every field and both names match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure or a
 *         null argument.
 *
 * The names are arguments because the C value does not carry one; supplying them reproduces the
 * canonical comparison exactly. This pure POD operation touches no runtime state and may run on
 * any thread.
 */
CNA_C_API CNA_Result cna_haptic_capabilities_equals(
    const CNA_HapticCapabilities* left,
    CNA_StringView left_name,
    const CNA_HapticCapabilities* right,
    CNA_StringView right_name,
    CNA_Bool* out_equal);

/**
 * @brief Reports how many haptic devices are connected.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the device count, which is zero when none is connected.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The enumeration is a point-in-time snapshot taken by each call, so an index is only valid until
 * the device set changes. No verification tree has force-feedback hardware, so zero is the
 * ordinary answer there — it is not a failure.
 */
CNA_C_API CNA_Result cna_haptics_get_count(
    CNA_Handle game,
    uint32_t* out_count);

/**
 * @brief Reads the identifier of one enumerated haptic device.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_id Receives the device identifier.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index or a null
 *         output; or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_haptics_get_id_at(
    CNA_Handle game,
    uint32_t index,
    uint32_t* out_id);

/**
 * @brief Reports the byte length of one enumerated device's name, without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range index or a null
 *         output; or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_haptics_get_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated device's name without a terminator.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or `CNA_RESULT_INVALID_ARGUMENT`.
 *         No partial value is written.
 *
 * An empty name is an ordinary answer: the platform reports none for some devices.
 */
CNA_C_API CNA_Result cna_haptics_copy_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Opens a standalone haptic device by its identifier.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id A device identifier from `cna_haptics_get_id_at`.
 * @param out_device Receives an owned device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A failure to open is **not** an error: the route succeeds and hands back a real handle whose
 * `cna_haptic_device_get_is_open` reports `CNA_FALSE`, exactly as the canonical factory returns a
 * closed device. Release the handle with `cna_haptic_device_destroy` either way.
 */
CNA_C_API CNA_Result cna_haptics_open(
    CNA_Handle game,
    uint32_t id,
    CNA_HapticDeviceHandle* out_device);

/**
 * @brief Opens the haptic device backing an already-connected joystick.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param joystick_id Joystick instance identifier.
 * @param out_device Receives an owned device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A joystick that is not connected, has no haptic capability, or fails to open yields a closed
 * device rather than a failure.
 */
CNA_C_API CNA_Result cna_haptics_open_from_joystick(
    CNA_Handle game,
    uint32_t joystick_id,
    CNA_HapticDeviceHandle* out_device);

/**
 * @brief Opens the default mouse's haptic device.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_device Receives an owned device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A mouse with no haptic capability yields a closed device rather than a failure.
 */
CNA_C_API CNA_Result cna_haptics_open_from_mouse(
    CNA_Handle game,
    CNA_HapticDeviceHandle* out_device);

/**
 * @brief Reports whether a connected joystick has haptic capability.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param joystick_id Joystick instance identifier.
 * @param out_haptic Receives `CNA_TRUE` when the joystick is haptic.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An unknown or disconnected joystick answers `CNA_FALSE` rather than failing.
 */
CNA_C_API CNA_Result cna_haptics_get_is_joystick_haptic(
    CNA_Handle game,
    uint32_t joystick_id,
    CNA_Bool* out_haptic);

/**
 * @brief Reports whether the default mouse has haptic capability.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_haptic Receives `CNA_TRUE` when the mouse is haptic.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_haptics_get_is_mouse_haptic(
    CNA_Handle game,
    CNA_Bool* out_haptic);

/**
 * @brief Reports whether a device handle holds an open device.
 *
 * @param device Owned device handle.
 * @param out_open Receives `CNA_TRUE` when hardware is behind the handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_get_is_open(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_open);

/**
 * @brief Reports the byte length of a device's name, without a terminator.
 *
 * @param device Owned device handle.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A closed device reports zero bytes.
 */
CNA_C_API CNA_Result cna_haptic_device_get_name_size(
    CNA_HapticDeviceHandle device,
    uint64_t* out_bytes);

/**
 * @brief Copies a device's name without a terminator.
 *
 * @param device Owned device handle.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or `CNA_RESULT_INVALID_ARGUMENT`.
 *         No partial value is written.
 */
CNA_C_API CNA_Result cna_haptic_device_copy_name(
    CNA_HapticDeviceHandle device,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Captures a device's static hardware capabilities.
 *
 * @param device Owned device handle.
 * @param out_capabilities Caller-provided versioned structure to receive the capabilities.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A closed device reports the canonical default value, whose effect counts are -1 rather than
 * zero. The name is not part of this value; read it with `cna_haptic_device_copy_name`.
 */
CNA_C_API CNA_Result cna_haptic_device_get_capabilities(
    CNA_HapticDeviceHandle device,
    CNA_HapticCapabilities* out_capabilities);

/**
 * @brief Reports whether a device could play a given effect.
 *
 * @param device Owned device handle.
 * @param effect Effect template to test.
 * @param custom_data Custom waveform samples, or null when @p custom_sample_count is zero.
 * @param custom_sample_count Number of samples in @p custom_data.
 * @param out_supported Receives `CNA_TRUE` when the effect is supported.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure, an
 *         undefined identity or a null argument; or a documented handle/thread failure.
 *
 * A closed device answers `CNA_FALSE` rather than failing.
 */
CNA_C_API CNA_Result cna_haptic_device_get_is_effect_supported(
    CNA_HapticDeviceHandle device,
    const CNA_HapticEffect* effect,
    const uint16_t* custom_data,
    uint64_t custom_sample_count,
    CNA_Bool* out_supported);

/**
 * @brief Initializes the device's simple rumble feature.
 *
 * @param device Owned device handle.
 * @param out_applied Receives `CNA_TRUE` when initialization succeeded.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This must succeed before the rumble play and stop routes can do anything. A device that cannot
 * rumble answers `CNA_FALSE` through the output rather than failing.
 */
CNA_C_API CNA_Result cna_haptic_device_init_rumble(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_applied);

/**
 * @brief Plays the simple rumble.
 *
 * @param device Owned device handle.
 * @param strength Rumble strength from zero through one.
 * @param length_ms Duration in milliseconds.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 * @param out_applied Receives `CNA_TRUE` when the rumble started.
 *
 * The strength is passed through unvalidated, exactly as the canonical operation passes it; the
 * platform decides what an out-of-range value means.
 */
CNA_C_API CNA_Result cna_haptic_device_play_rumble(
    CNA_HapticDeviceHandle device,
    float strength,
    uint32_t length_ms,
    CNA_Bool* out_applied);

/**
 * @brief Stops the simple rumble.
 *
 * @param device Owned device handle.
 * @param out_applied Receives `CNA_TRUE` when the rumble stopped.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_stop_rumble(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_applied);

/**
 * @brief Uploads a new effect to the device.
 *
 * @param device Owned device handle.
 * @param effect Effect template to upload.
 * @param custom_data Custom waveform samples, or null when @p custom_sample_count is zero.
 * @param custom_sample_count Number of samples in @p custom_data.
 * @param out_effect_id Receives the new effect's identifier, or **-1** on failure.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure, an
 *         undefined identity or a null argument; or a documented handle/thread failure.
 *
 * A device that cannot store the effect reports -1 through the output rather than failing, which
 * is what the canonical operation does. An identifier is only meaningful on the device that
 * produced it.
 */
CNA_C_API CNA_Result cna_haptic_device_create_effect(
    CNA_HapticDeviceHandle device,
    const CNA_HapticEffect* effect,
    const uint16_t* custom_data,
    uint64_t custom_sample_count,
    int32_t* out_effect_id);

/**
 * @brief Updates the parameters of a previously created effect.
 *
 * @param device Owned device handle.
 * @param effect_id Identifier from `cna_haptic_device_create_effect`.
 * @param effect New effect parameters.
 * @param custom_data Custom waveform samples, or null when @p custom_sample_count is zero.
 * @param custom_sample_count Number of samples in @p custom_data.
 * @param out_applied Receives `CNA_TRUE` when the update succeeded.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure, an
 *         undefined identity or a null argument; or a documented handle/thread failure.
 *
 * An unknown identifier answers `CNA_FALSE` rather than failing, exactly as the canonical
 * operation does.
 */
CNA_C_API CNA_Result cna_haptic_device_update_effect(
    CNA_HapticDeviceHandle device,
    int32_t effect_id,
    const CNA_HapticEffect* effect,
    const uint16_t* custom_data,
    uint64_t custom_sample_count,
    CNA_Bool* out_applied);

/**
 * @brief Plays a previously created effect.
 *
 * @param device Owned device handle.
 * @param effect_id Identifier from `cna_haptic_device_create_effect`.
 * @param iterations Number of repetitions; pass one for the canonical default.
 * @param out_applied Receives `CNA_TRUE` when playback started.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * C has no defaulted arguments, so the canonical default of one repetition is passed explicitly.
 */
CNA_C_API CNA_Result cna_haptic_device_run_effect(
    CNA_HapticDeviceHandle device,
    int32_t effect_id,
    uint32_t iterations,
    CNA_Bool* out_applied);

/**
 * @brief Stops a playing effect.
 *
 * @param device Owned device handle.
 * @param effect_id Identifier from `cna_haptic_device_create_effect`.
 * @param out_applied Receives `CNA_TRUE` when the effect stopped.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_stop_effect(
    CNA_HapticDeviceHandle device,
    int32_t effect_id,
    CNA_Bool* out_applied);

/**
 * @brief Frees a previously created effect.
 *
 * @param device Owned device handle.
 * @param effect_id Identifier from `cna_haptic_device_create_effect`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical operation reports nothing, so an unknown identifier is a successful no-op.
 */
CNA_C_API CNA_Result cna_haptic_device_destroy_effect(
    CNA_HapticDeviceHandle device,
    int32_t effect_id);

/**
 * @brief Reports whether an effect is currently playing.
 *
 * @param device Owned device handle.
 * @param effect_id Identifier from `cna_haptic_device_create_effect`.
 * @param out_playing Receives `CNA_TRUE` when the effect is playing.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A device without the status capability answers `CNA_FALSE` rather than failing.
 */
CNA_C_API CNA_Result cna_haptic_device_get_effect_status(
    CNA_HapticDeviceHandle device,
    int32_t effect_id,
    CNA_Bool* out_playing);

/**
 * @brief Stops every effect currently playing on the device.
 *
 * @param device Owned device handle.
 * @param out_applied Receives `CNA_TRUE` when the request succeeded.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_stop_all_effects(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_applied);

/**
 * @brief Sets the overall effect gain.
 *
 * @param device Owned device handle.
 * @param gain Gain from zero through one hundred.
 * @param out_applied Receives `CNA_TRUE` when the gain was applied.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The value is passed through unvalidated, exactly as the canonical operation passes it. A device
 * without the gain capability answers `CNA_FALSE`.
 */
CNA_C_API CNA_Result cna_haptic_device_set_gain(
    CNA_HapticDeviceHandle device,
    int32_t gain,
    CNA_Bool* out_applied);

/**
 * @brief Sets the autocenter strength.
 *
 * @param device Owned device handle.
 * @param autocenter Strength from zero through one hundred.
 * @param out_applied Receives `CNA_TRUE` when the strength was applied.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_set_autocenter(
    CNA_HapticDeviceHandle device,
    int32_t autocenter,
    CNA_Bool* out_applied);

/**
 * @brief Pauses all effect playback on the device.
 *
 * @param device Owned device handle.
 * @param out_applied Receives `CNA_TRUE` when playback was paused.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_pause(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_applied);

/**
 * @brief Resumes effect playback after a pause.
 *
 * @param device Owned device handle.
 * @param out_applied Receives `CNA_TRUE` when playback resumed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_haptic_device_resume(
    CNA_HapticDeviceHandle device,
    CNA_Bool* out_applied);

/**
 * @brief Closes the device without releasing its handle.
 *
 * @param device Owned device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Idempotent, exactly as the canonical disposal is. Afterwards the handle is still valid and
 * reports itself closed, and every other route on it is a safe no-op.
 */
CNA_C_API CNA_Result cna_haptic_device_dispose(CNA_HapticDeviceHandle device);

/**
 * @brief Closes the device if open and releases its handle.
 *
 * @param device Owned device handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This maps the canonical destructor. A second release reports `CNA_RESULT_INVALID_HANDLE`.
 */
CNA_C_API CNA_Result cna_haptic_device_destroy(CNA_HapticDeviceHandle device);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_HAPTICS_H
