// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_DEVICES_H
#define CNA_C_INPUT_DEVICES_H

#include "CNA/C/input_gamepad.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned handle to one input-device hot-plug event subscription. */
typedef CNA_Handle CNA_InputDeviceEventRegistrationHandle;

/** @brief Fixed-width identity of a host-device motion sensor's kind. */
typedef uint32_t CNA_SensorType;

/** @brief Unknown or unrecognized sensor. */
#define CNA_SENSOR_TYPE_UNKNOWN UINT32_C(0)
/** @brief Accelerometer, reporting acceleration in metres per second squared. */
#define CNA_SENSOR_TYPE_ACCELEROMETER UINT32_C(1)
/** @brief Gyroscope, reporting angular velocity in radians per second. */
#define CNA_SENSOR_TYPE_GYROSCOPE UINT32_C(2)
/** @brief Left-side accelerometer of a dual-sensor controller. */
#define CNA_SENSOR_TYPE_ACCELEROMETER_LEFT UINT32_C(3)
/** @brief Left-side gyroscope of a dual-sensor controller. */
#define CNA_SENSOR_TYPE_GYROSCOPE_LEFT UINT32_C(4)
/** @brief Right-side accelerometer of a dual-sensor controller. */
#define CNA_SENSOR_TYPE_ACCELEROMETER_RIGHT UINT32_C(5)
/** @brief Right-side gyroscope of a dual-sensor controller. */
#define CNA_SENSOR_TYPE_GYROSCOPE_RIGHT UINT32_C(6)
/** @brief Highest defined sensor-kind identity. */
#define CNA_SENSOR_TYPE_MAXIMUM CNA_SENSOR_TYPE_GYROSCOPE_RIGHT

/**
 * @brief Identity of one enumerated host-device motion sensor.
 *
 * As with the joystick descriptor, the canonical `name` field is left out of this fixed value — a
 * string does not belong inside a POD — and is read through `cna_sensors_get_name_size_at`/
 * `_copy_name_at` instead, so `cna_sensor_info_equals` takes the two names alongside the two
 * values.
 */
typedef struct CNA_SensorInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The native sensor instance identifier. */
    uint32_t id;

    /** @brief One `CNA_SENSOR_TYPE_*` identity. */
    CNA_SensorType type;
} CNA_SensorInfo;

/**
 * @brief Identity of one enumerated input device: a mouse, keyboard or touch device.
 *
 * This is metadata only. XNA input state stays merged across devices, so an identifier here does
 * not select a device to read from. The canonical `name` field is again read through its own
 * count/copy routes rather than living in the value.
 */
typedef struct CNA_InputDeviceInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The native device instance identifier. Wider than the sensor and joystick ones,
     *         because a touch-device identifier is 64-bit natively. */
    uint64_t id;
} CNA_InputDeviceInfo;

/**
 * @brief Handler invoked when an input device is connected or disconnected.
 *
 * @param device_id The native device identifier the event carries.
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_InputDeviceHotplugCallback)(uint32_t device_id, void* context);

/**
 * @brief Initializes a sensor descriptor to the canonical default.
 *
 * @param out_info Receives identifier zero and `CNA_SENSOR_TYPE_UNKNOWN`.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_sensor_info_init(CNA_SensorInfo* out_info);

/**
 * @brief Compares two sensor descriptors, including their names.
 *
 * @param left First descriptor.
 * @param left_name First descriptor's sensor name.
 * @param right Second descriptor.
 * @param right_name Second descriptor's sensor name.
 * @param out_equal Receives `CNA_TRUE` when the identifier, name and kind all match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument, an invalid
 *         structure, an undefined kind identity or a name that is not valid UTF-8.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_sensor_info_equals(
    const CNA_SensorInfo* left,
    CNA_StringView left_name,
    const CNA_SensorInfo* right,
    CNA_StringView right_name,
    CNA_Bool* out_equal);

/**
 * @brief Returns how many host-device motion sensors are currently enumerated.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the sensor count; zero is an ordinary answer, and the usual one on a
 *        desktop machine.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The enumeration is a point-in-time snapshot taken by each call, so an index is valid only until
 * the sensor set changes.
 */
CNA_C_API CNA_Result cna_sensors_get_count(CNA_Handle game, uint32_t* out_count);

/**
 * @brief Returns the descriptor of one enumerated sensor.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_info Receives the identifier and kind; the name has its own routes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an index at or
 *         past the count, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sensors_get_info_at(
    CNA_Handle game,
    uint32_t index,
    CNA_SensorInfo* out_info);

/**
 * @brief Returns the byte count of one enumerated sensor's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an out-of-range
 *         index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sensors_get_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated sensor's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sensors_copy_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reads the first accelerometer the host device reports.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_acceleration Receives the acceleration in metres per second squared; **left unchanged**
 *        when no reading was produced, exactly as the canonical query leaves its reference.
 * @param out_available Receives `CNA_TRUE` when a reading was produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Having no sensor is an ordinary answer, not a failure: the call succeeds and reports
 * `CNA_FALSE`. These are the machine's own sensors, not a controller's — a gamepad's gyro and
 * accelerometer are `cna_gamepad_get_gyro_ext` and `cna_gamepad_get_accelerometer_ext`.
 */
CNA_C_API CNA_Result cna_sensors_get_accelerometer(
    CNA_Handle game,
    CNA_Vector3* out_acceleration,
    CNA_Bool* out_available);

/**
 * @brief Reads the first gyroscope the host device reports.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_angular_velocity Receives the angular velocity in radians per second; **left
 *        unchanged** when no reading was produced.
 * @param out_available Receives `CNA_TRUE` when a reading was produced.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_sensors_get_gyroscope(
    CNA_Handle game,
    CNA_Vector3* out_angular_velocity,
    CNA_Bool* out_available);

/**
 * @brief Initializes an input-device descriptor to the canonical default.
 *
 * @param out_info Receives identifier zero.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_input_device_info_init(CNA_InputDeviceInfo* out_info);

/**
 * @brief Compares two input-device descriptors, including their names.
 *
 * @param left First descriptor.
 * @param left_name First descriptor's device name.
 * @param right Second descriptor.
 * @param right_name Second descriptor's device name.
 * @param out_equal Receives `CNA_TRUE` when both the identifier and the name match.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument, an invalid
 *         structure or a name that is not valid UTF-8.
 *
 * The canonical `!=` is this comparison negated and needs no route of its own. This pure POD
 * operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_input_device_info_equals(
    const CNA_InputDeviceInfo* left,
    CNA_StringView left_name,
    const CNA_InputDeviceInfo* right,
    CNA_StringView right_name,
    CNA_Bool* out_equal);

/**
 * @brief Returns how many mice are currently enumerated.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the device count; zero is an ordinary answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_mouse_count(CNA_Handle game, uint32_t* out_count);

/**
 * @brief Returns the descriptor of one enumerated mouse.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_info Receives the identifier; the name has its own routes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null output or an out-of-range
 *         index, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_mouse_info_at(
    CNA_Handle game,
    uint32_t index,
    CNA_InputDeviceInfo* out_info);

/**
 * @brief Returns the byte count of one enumerated mouse's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_mouse_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated mouse's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_copy_mouse_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns how many keyboards are currently enumerated.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the device count; zero is an ordinary answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_keyboard_count(CNA_Handle game, uint32_t* out_count);

/**
 * @brief Returns the descriptor of one enumerated keyboard.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_info Receives the identifier; the name has its own routes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_keyboard_info_at(
    CNA_Handle game,
    uint32_t index,
    CNA_InputDeviceInfo* out_info);

/**
 * @brief Returns the byte count of one enumerated keyboard's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_keyboard_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated keyboard's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_copy_keyboard_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns how many touch devices are currently enumerated.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the device count; zero is an ordinary answer.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_touch_device_count(
    CNA_Handle game,
    uint32_t* out_count);

/**
 * @brief Returns the descriptor of one enumerated touch device.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_info Receives the identifier; the name has its own routes.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_touch_device_info_at(
    CNA_Handle game,
    uint32_t index,
    CNA_InputDeviceInfo* out_info);

/**
 * @brief Returns the byte count of one enumerated touch device's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, or a documented
 *         handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_get_touch_device_name_size_at(
    CNA_Handle game,
    uint32_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one enumerated touch device's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below the current count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_copy_touch_device_name_at(
    CNA_Handle game,
    uint32_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to the mouse-connected event.
 *
 * @param callback Handler invoked with the connected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event is **static**, so the subscription belongs to the process rather than to a
 * game and takes no game handle. Release it with `cna_input_devices_unsubscribe_ext`.
 */
CNA_C_API CNA_Result cna_input_devices_subscribe_mouse_connected_ext(
    CNA_InputDeviceHotplugCallback callback,
    void* context,
    CNA_InputDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the mouse-disconnected event.
 *
 * @param callback Handler invoked with the disconnected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_input_devices_subscribe_mouse_disconnected_ext(
    CNA_InputDeviceHotplugCallback callback,
    void* context,
    CNA_InputDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the keyboard-connected event.
 *
 * @param callback Handler invoked with the connected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_input_devices_subscribe_keyboard_connected_ext(
    CNA_InputDeviceHotplugCallback callback,
    void* context,
    CNA_InputDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the keyboard-disconnected event.
 *
 * @param callback Handler invoked with the disconnected device's identifier.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_input_devices_subscribe_keyboard_disconnected_ext(
    CNA_InputDeviceHotplugCallback callback,
    void* context,
    CNA_InputDeviceEventRegistrationHandle* out_registration);

/**
 * @brief Releases an input-device hot-plug registration.
 *
 * @param registration Owned registration handle from any of the four subscribe routes.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 *
 * One route releases all four events, because a registration already knows which one it came from.
 * Releasing it after `cna_input_devices_reset_for_tests_ext` cleared the process event is a no-op
 * rather than a failure.
 */
CNA_C_API CNA_Result cna_input_devices_unsubscribe_ext(
    CNA_InputDeviceEventRegistrationHandle registration);

/**
 * @brief Raises the mouse-connected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical event is a public multicast field the platform layer invokes on hot-plug; this
 * route invokes the same field, so a C application can exercise its own wiring without hardware.
 * Every subscribed handler runs synchronously before the call returns.
 */
CNA_C_API CNA_Result cna_input_devices_raise_mouse_connected_ext(CNA_Handle game, uint32_t id);

/**
 * @brief Raises the mouse-disconnected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_raise_mouse_disconnected_ext(CNA_Handle game, uint32_t id);

/**
 * @brief Raises the keyboard-connected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_raise_keyboard_connected_ext(CNA_Handle game, uint32_t id);

/**
 * @brief Raises the keyboard-disconnected event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param id The device identifier to report.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_input_devices_raise_keyboard_disconnected_ext(
    CNA_Handle game,
    uint32_t id);

/**
 * @brief Clears every input-device hot-plug subscriber.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * This maps the canonical test-support helper, which clears all four events at once. The subscriber
 * lists are process-wide state the caller does not own alone, so a caller that clears them must
 * expect other subscribers to be gone.
 */
CNA_C_API CNA_Result cna_input_devices_reset_for_tests_ext(CNA_Handle game);

/**
 * @brief Returns the byte count of the system clipboard's current text.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the UTF-8 byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An empty or unavailable clipboard is an ordinary answer of zero bytes, not a failure. The
 * clipboard is process-external state: another application can change it between this call and the
 * copy, so treat the count as a hint and always check the copy's own byte count.
 */
CNA_C_API CNA_Result cna_clipboard_get_text_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the system clipboard's current text.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_clipboard_copy_text(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Places text on the system clipboard.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param text UTF-8 text to place on the clipboard; borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_ENCODING` for text that is not valid UTF-8, or a
 *         documented argument/handle/thread/native failure.
 *
 * The canonical operation returns nothing and so reports no platform outcome, and this route
 * reproduces that: **success means the request was made, not that the clipboard changed**. A
 * platform that ignores the request — a headless session with no clipboard, or a browser that
 * requires a user gesture — leaves the clipboard untouched and this call still succeeds. Read it
 * back if the outcome matters.
 */
CNA_C_API CNA_Result cna_clipboard_set_text(CNA_Handle game, CNA_StringView text);

/**
 * @brief Reports whether the system clipboard currently holds non-empty text.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_has_text Receives `CNA_TRUE` when the clipboard holds non-empty text.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_clipboard_get_has_text(CNA_Handle game, CNA_Bool* out_has_text);

/**
 * @brief Reads the host system's power state, remaining runtime and charge level.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_state Receives one `CNA_POWER_STATE_*` identity.
 * @param out_seconds_left Receives the seconds of runtime remaining, or **-1** when unknown.
 * @param out_percent Receives the battery charge from 0 through 100, or **-1** when unknown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * All three outputs are always written. `CNA_POWER_STATE_UNKNOWN` and `CNA_POWER_STATE_ERROR` are
 * canonical answers the query itself produces, not C failures, and -1 means "unknown" rather than
 * "none left". This reports the machine's own power source; a controller's battery is
 * `cna_gamepad_get_power_info_ext`.
 */
CNA_C_API CNA_Result cna_power_get_info(
    CNA_Handle game,
    CNA_PowerState* out_state,
    int32_t* out_seconds_left,
    int32_t* out_percent);

#ifdef __cplusplus
}
#endif

#endif
