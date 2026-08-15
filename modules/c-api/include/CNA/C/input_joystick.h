// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_JOYSTICK_H
#define CNA_C_INPUT_JOYSTICK_H

#include "CNA/C/input_gamepad.h"
#include "CNA/C/math_values.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Owned handle to one captured raw-joystick snapshot.
 *
 * The canonical snapshot carries four heterogeneous variable-length arrays with no fixed maximum,
 * so C captures it into an owned handle instead of a fixed value: the four arrays then all come
 * from one instant, and no arbitrary capacity can silently truncate a device with more axes or
 * buttons than the ABI guessed. Release it with `cna_joystick_state_destroy`.
 */
typedef CNA_Handle CNA_JoystickStateHandle;

/** @brief Owned handle to one raw-joystick hot-plug event subscription. */
typedef CNA_Handle CNA_JoystickEventRegistrationHandle;

/** @brief Fixed-width identity of a raw joystick's physical category. */
typedef uint32_t CNA_JoystickType;

/** @brief Unknown or unrecognized joystick type. */
#define CNA_JOYSTICK_TYPE_UNKNOWN UINT32_C(0)
/** @brief A device the native layer also maps as a gamepad. */
#define CNA_JOYSTICK_TYPE_GAMEPAD UINT32_C(1)
/** @brief A steering wheel. */
#define CNA_JOYSTICK_TYPE_WHEEL UINT32_C(2)
/** @brief An arcade-style stick. */
#define CNA_JOYSTICK_TYPE_ARCADE_STICK UINT32_C(3)
/** @brief A flight stick (HOTAS-style). */
#define CNA_JOYSTICK_TYPE_FLIGHT_STICK UINT32_C(4)
/** @brief A dance pad. */
#define CNA_JOYSTICK_TYPE_DANCE_PAD UINT32_C(5)
/** @brief A guitar-shaped controller. */
#define CNA_JOYSTICK_TYPE_GUITAR UINT32_C(6)
/** @brief A drum-kit controller. */
#define CNA_JOYSTICK_TYPE_DRUM_KIT UINT32_C(7)
/** @brief An arcade-cabinet pad. */
#define CNA_JOYSTICK_TYPE_ARCADE_PAD UINT32_C(8)
/** @brief A throttle quadrant. */
#define CNA_JOYSTICK_TYPE_THROTTLE UINT32_C(9)
/** @brief Highest defined joystick-type identity. */
#define CNA_JOYSTICK_TYPE_MAXIMUM CNA_JOYSTICK_TYPE_THROTTLE

/**
 * @brief Fixed-width identity of a joystick POV hat's position.
 *
 * The native layer encodes a hat as an up/down bit combined with a left/right bit; the canonical
 * type enumerates the nine reachable combinations instead, and so does this identity. It is
 * therefore **not** a bit set: `CNA_JOYSTICK_HAT_POSITION_RIGHT_UP` is the identity 5, not
 * `RIGHT | UP`.
 */
typedef uint32_t CNA_JoystickHatPosition;

/** @brief The hat is not pressed in any direction. */
#define CNA_JOYSTICK_HAT_POSITION_CENTERED UINT32_C(0)
/** @brief The hat is pressed up. */
#define CNA_JOYSTICK_HAT_POSITION_UP UINT32_C(1)
/** @brief The hat is pressed right. */
#define CNA_JOYSTICK_HAT_POSITION_RIGHT UINT32_C(2)
/** @brief The hat is pressed down. */
#define CNA_JOYSTICK_HAT_POSITION_DOWN UINT32_C(3)
/** @brief The hat is pressed left. */
#define CNA_JOYSTICK_HAT_POSITION_LEFT UINT32_C(4)
/** @brief The hat is pressed up and to the right. */
#define CNA_JOYSTICK_HAT_POSITION_RIGHT_UP UINT32_C(5)
/** @brief The hat is pressed down and to the right. */
#define CNA_JOYSTICK_HAT_POSITION_RIGHT_DOWN UINT32_C(6)
/** @brief The hat is pressed up and to the left. */
#define CNA_JOYSTICK_HAT_POSITION_LEFT_UP UINT32_C(7)
/** @brief The hat is pressed down and to the left. */
#define CNA_JOYSTICK_HAT_POSITION_LEFT_DOWN UINT32_C(8)
/** @brief Highest defined hat-position identity. */
#define CNA_JOYSTICK_HAT_POSITION_MAXIMUM CNA_JOYSTICK_HAT_POSITION_LEFT_DOWN

/**
 * @brief Identity of one enumerated raw joystick device.
 *
 * The canonical descriptor also carries the device name. C leaves it out of this fixed value —
 * a string does not belong inside a POD — and exposes it through
 * `cna_joysticks_get_name_size_at`/`_copy_name_at` instead, so `cna_joystick_info_equals` takes
 * the two names alongside the two values.
 */
typedef struct CNA_JoystickInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The native joystick instance identifier. */
    uint32_t id;

    /** @brief One `CNA_JOYSTICK_TYPE_*` identity. */
    CNA_JoystickType type;
} CNA_JoystickInfo;

/**
 * @brief The static hardware shape and identity of a raw joystick device.
 *
 * Like @ref CNA_JoystickInfo this leaves the canonical name out of the value, and the device GUID
 * with it; both are read through their own count/copy routes, and
 * `cna_joystick_capabilities_equals` takes both strings alongside the two values so it reproduces
 * the canonical comparison exactly.
 */
typedef struct CNA_JoystickCapabilities {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Number of axes the device reports. */
    int32_t axis_count;

    /** @brief Number of buttons the device reports. */
    int32_t button_count;

    /** @brief Number of POV hats the device reports. */
    int32_t hat_count;

    /** @brief Number of trackballs the device reports. */
    int32_t ball_count;

    /** @brief One `CNA_JOYSTICK_TYPE_*` identity. */
    CNA_JoystickType type;

    /** @brief One `CNA_POWER_STATE_*` identity; `CNA_POWER_STATE_UNKNOWN` when disconnected. */
    CNA_PowerState power_state;

    /** @brief Battery charge percent from 0 through 100, or -1 when unknown or disconnected. */
    int32_t power_percent;

    /** @brief `CNA_TRUE` when a joystick with this instance identifier is currently connected. */
    CNA_Bool is_connected;

    /** @brief Reserved padding; always zero. */
    uint8_t reserved[3];
} CNA_JoystickCapabilities;

/**
 * @brief Handler invoked when a raw joystick is connected or disconnected.
 *
 * @param joystick_id The native joystick instance identifier the event carries.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_JoystickHotplugCallback)(uint32_t joystick_id, void* context);

/**
 * @brief Initializes a joystick descriptor to the canonical default.
 *
 * @param out_info Receives identifier zero and `CNA_JOYSTICK_TYPE_UNKNOWN`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_joystick_info_init(CNA_JoystickInfo* out_info);

/**
 * @brief Compares two joystick descriptors, including their names.
 *
 * @param left First descriptor.
 * @param left_name First descriptor's device name.
 * @param right Second descriptor.
 * @param right_name Second descriptor's device name.
 * @param out_equal Receives `CNA_TRUE` when the identifier, name and type all match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument, an invalid
 *         structure, an undefined type identity or a name that is not valid UTF-8.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_joystick_info_equals(
    const CNA_JoystickInfo* left,
    CNA_StringView left_name,
    const CNA_JoystickInfo* right,
    CNA_StringView right_name,
    CNA_Bool* out_equal);

/**
 * @brief Initializes a joystick capability value to the canonical defaults.
 *
 * @param out_capabilities Receives a disconnected device: zero counts, an unknown type, an unknown
 *        power state and a power percent of **-1** rather than zero, because "unknown charge" is
 *        deliberately distinguishable from "empty battery".
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_joystick_capabilities_init(CNA_JoystickCapabilities* out_capabilities);

/**
 * @brief Compares two joystick capability values, including their names and GUIDs.
 *
 * @param left First capability value.
 * @param left_name First device's name.
 * @param left_guid First device's GUID text.
 * @param right Second capability value.
 * @param right_name Second device's name.
 * @param right_guid Second device's GUID text.
 * @param out_equal Receives `CNA_TRUE` when every canonical field matches.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument, an invalid
 *         structure, an undefined type or power-state identity, or text that is not valid UTF-8.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_joystick_capabilities_equals(
    const CNA_JoystickCapabilities* left,
    CNA_StringView left_name,
    CNA_StringView left_guid,
    const CNA_JoystickCapabilities* right,
    CNA_StringView right_name,
    CNA_StringView right_guid,
    CNA_Bool* out_equal);

/**
 * @brief Returns how many raw joysticks are currently enumerated.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the device count; zero is an ordinary answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The enumeration reports the joysticks the native layer currently holds open, so it is a
 * point-in-time snapshot: an index is valid only until the device set changes.
 */
CNA_C_API CNA_Result cna_joysticks_get_count(CNA_Handle game, uint32_t* out_count);

/**
 * @brief Returns the descriptor of one enumerated joystick.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_info Receives the identifier and type; the name has its own routes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_get_info_at(
    CNA_Handle game,
    uint32_t index,
    CNA_JoystickInfo* out_info);

/**
 * @brief Returns the byte count of one enumerated joystick's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an out-of-range
 *         index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_get_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated joystick's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_copy_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the static hardware shape of one joystick.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param out_capabilities Receives the capability value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An identifier that is not connected is **not an error**: the value comes back with
 * `is_connected` false and canonical defaults elsewhere, exactly as the canonical query behaves.
 */
CNA_C_API CNA_Result cna_joysticks_get_capabilities(
    CNA_Handle game,
    uint32_t id,
    CNA_JoystickCapabilities* out_capabilities);

/**
 * @brief Returns the byte count of one joystick's device name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param out_bytes Receives the UTF-8 byte count; zero when the device is not connected.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_get_capabilities_name_size(
    CNA_Handle game,
    uint32_t id,
    uint64_t* out_bytes);

/**
 * @brief Copies one joystick's device name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_copy_capabilities_name(
    CNA_Handle game,
    uint32_t id,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of one joystick's GUID text.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param out_bytes Receives the byte count; zero when the device is not connected.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The GUID is the canonical lowercase hexadecimal text, not a binary value.
 */
CNA_C_API CNA_Result cna_joysticks_get_capabilities_guid_size(
    CNA_Handle game,
    uint32_t id,
    uint64_t* out_bytes);

/**
 * @brief Copies one joystick's GUID text.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param destination Buffer receiving the bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_copy_capabilities_guid(
    CNA_Handle game,
    uint32_t id,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Captures one joystick's current axis, button, hat and trackball state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The native joystick instance identifier.
 * @param out_state Receives an owned snapshot handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An identifier that is not connected is **not an error**: the capture succeeds and every array
 * is empty, exactly as the canonical query behaves. Trackball values are relative motion since the
 * previous read, so capturing consumes them. Release the snapshot with
 * `cna_joystick_state_destroy`.
 */
CNA_C_API CNA_Result cna_joysticks_capture_state(
    CNA_Handle game,
    uint32_t id,
    CNA_JoystickStateHandle* out_state);

/**
 * @brief Returns how many axes a captured snapshot carries.
 *
 * @param state Owned snapshot handle.
 * @param out_count Receives the axis count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_get_axis_count(
    CNA_JoystickStateHandle state,
    uint32_t* out_count);

/**
 * @brief Copies a captured snapshot's axis values.
 *
 * @param state Owned snapshot handle.
 * @param destination Buffer receiving the values; may be null only when @p capacity is zero.
 * @param capacity Elements available in @p destination.
 * @param out_count Always receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 *
 * Values are raw and unmapped, in the native range -32768 through 32767.
 */
CNA_C_API CNA_Result cna_joystick_state_copy_axes(
    CNA_JoystickStateHandle state,
    int16_t* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns how many buttons a captured snapshot carries.
 *
 * @param state Owned snapshot handle.
 * @param out_count Receives the button count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_get_button_count(
    CNA_JoystickStateHandle state,
    uint32_t* out_count);

/**
 * @brief Copies a captured snapshot's button states.
 *
 * @param state Owned snapshot handle.
 * @param destination Buffer receiving one `CNA_TRUE`/`CNA_FALSE` per button; may be null only when
 *        @p capacity is zero.
 * @param capacity Elements available in @p destination.
 * @param out_count Always receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 *
 * Button numbering is whatever the hardware reports; it carries no XNA semantics.
 */
CNA_C_API CNA_Result cna_joystick_state_copy_buttons(
    CNA_JoystickStateHandle state,
    CNA_Bool* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns how many POV hats a captured snapshot carries.
 *
 * @param state Owned snapshot handle.
 * @param out_count Receives the hat count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_get_hat_count(
    CNA_JoystickStateHandle state,
    uint32_t* out_count);

/**
 * @brief Copies a captured snapshot's hat positions.
 *
 * @param state Owned snapshot handle.
 * @param destination Buffer receiving one `CNA_JOYSTICK_HAT_POSITION_*` identity per hat; may be
 *        null only when @p capacity is zero.
 * @param capacity Elements available in @p destination.
 * @param out_count Always receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_copy_hats(
    CNA_JoystickStateHandle state,
    CNA_JoystickHatPosition* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Returns how many trackballs a captured snapshot carries.
 *
 * @param state Owned snapshot handle.
 * @param out_count Receives the trackball count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_get_ball_count(
    CNA_JoystickStateHandle state,
    uint32_t* out_count);

/**
 * @brief Copies a captured snapshot's trackball motion.
 *
 * @param state Owned snapshot handle.
 * @param destination Buffer receiving one relative motion point per trackball; may be null only
 *        when @p capacity is zero.
 * @param capacity Elements available in @p destination.
 * @param out_count Always receives the required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_joystick_state_copy_balls(
    CNA_JoystickStateHandle state,
    CNA_Point* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Compares two captured snapshots.
 *
 * @param left First snapshot handle.
 * @param right Second snapshot handle.
 * @param out_equal Receives `CNA_TRUE` when all four arrays match element for element.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * Two snapshots of different lengths are unequal. The canonical `!=` is this comparison negated
 * and needs no route of its own.
 */
CNA_C_API CNA_Result cna_joystick_state_equals(
    CNA_JoystickStateHandle left,
    CNA_JoystickStateHandle right,
    CNA_Bool* out_equal);

/**
 * @brief Releases a captured snapshot.
 *
 * @param state Owned snapshot handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 */
CNA_C_API CNA_Result cna_joystick_state_destroy(CNA_JoystickStateHandle state);

/**
 * @brief Subscribes to the joystick-connected event.
 *
 * @param callback Handler invoked with the connected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event is **static**, so the subscription belongs to the process rather than to a
 * game and takes no game handle. Release it with `cna_joysticks_unsubscribe_ext`.
 */
CNA_C_API CNA_Result cna_joysticks_subscribe_connected_ext(
    CNA_JoystickHotplugCallback callback,
    void* context,
    CNA_JoystickEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the joystick-disconnected event.
 *
 * @param callback Handler invoked with the disconnected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_joysticks_subscribe_disconnected_ext(
    CNA_JoystickHotplugCallback callback,
    void* context,
    CNA_JoystickEventRegistrationHandle* out_registration);

/**
 * @brief Releases a joystick hot-plug registration.
 *
 * @param registration Owned registration handle from either subscribe route.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 *
 * One route releases both events, because a registration already knows which one it came from.
 * Releasing it after `cna_joysticks_reset_for_tests_ext` cleared the process event is a no-op
 * rather than a failure.
 */
CNA_C_API CNA_Result cna_joysticks_unsubscribe_ext(
    CNA_JoystickEventRegistrationHandle registration);

/**
 * @brief Raises the joystick-connected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical event is a public multicast field the platform layer invokes on hot-plug; this
 * route invokes the same field, so a C application can exercise its own wiring without hardware.
 * Every subscribed handler runs synchronously before the call returns.
 */
CNA_C_API CNA_Result cna_joysticks_raise_connected_ext(CNA_Handle game, uint32_t id);

/**
 * @brief Raises the joystick-disconnected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_joysticks_raise_disconnected_ext(CNA_Handle game, uint32_t id);

/**
 * @brief Clears every joystick hot-plug subscriber.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This maps the canonical test-support helper. The subscriber list is process-wide state the
 * caller does not own alone, so a caller that clears it must expect other subscribers to be gone.
 */
CNA_C_API CNA_Result cna_joysticks_reset_for_tests_ext(CNA_Handle game);

#ifdef __cplusplus
}
#endif

#endif
