// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_TOUCH_H
#define CNA_C_INPUT_TOUCH_H

#include "CNA/C/display.h"
#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width gesture-type identity, used as a bit set. */
typedef uint32_t CNA_GestureType;

/** @brief No gesture. */
#define CNA_GESTURE_TYPE_NONE UINT32_C(0)
/** @brief Single tap gesture. */
#define CNA_GESTURE_TYPE_TAP UINT32_C(1)
/** @brief Double tap gesture. */
#define CNA_GESTURE_TYPE_DOUBLE_TAP UINT32_C(2)
/** @brief Hold gesture. */
#define CNA_GESTURE_TYPE_HOLD UINT32_C(4)
/** @brief Horizontal drag gesture. */
#define CNA_GESTURE_TYPE_HORIZONTAL_DRAG UINT32_C(8)
/** @brief Vertical drag gesture. */
#define CNA_GESTURE_TYPE_VERTICAL_DRAG UINT32_C(16)
/** @brief Free-form drag gesture. */
#define CNA_GESTURE_TYPE_FREE_DRAG UINT32_C(32)
/** @brief Pinch gesture. */
#define CNA_GESTURE_TYPE_PINCH UINT32_C(64)
/** @brief Flick gesture. */
#define CNA_GESTURE_TYPE_FLICK UINT32_C(128)
/** @brief Drag completion gesture. */
#define CNA_GESTURE_TYPE_DRAG_COMPLETE UINT32_C(256)
/** @brief Pinch completion gesture. */
#define CNA_GESTURE_TYPE_PINCH_COMPLETE UINT32_C(512)
/** @brief Every defined gesture bit combined. */
#define CNA_GESTURE_TYPE_ALL UINT32_C(0x000003FF)

/**
 * @brief Marker used when no finger is present for a touch slot.
 *
 * This is the canonical `TouchPanel::NO_FINGER` value.
 */
#define CNA_TOUCH_NO_FINGER INT32_C(-1)

/**
 * @brief Describes one gesture reported by the touch panel.
 *
 * A copyable snapshot with no identity, so it crosses as a value rather than a handle. Every
 * canonical property is a plain getter and appears here as a directly readable field.
 */
typedef struct CNA_GestureSample {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief The `CNA_GESTURE_TYPE_*` identity of this gesture. */
    CNA_GestureType gesture_type;

    /** @brief CNA finger identifier of the primary touch point, or @ref CNA_TOUCH_NO_FINGER. */
    int32_t finger_id_ext;

    /** @brief CNA finger identifier of the secondary touch point, or @ref CNA_TOUCH_NO_FINGER. */
    int32_t finger_id2_ext;

    /** @brief Reserved; always zero. */
    uint32_t reserved;

    /** @brief Gesture timestamp in 100-nanosecond ticks. */
    int64_t timestamp_ticks;

    /** @brief Position of the primary touch point. */
    CNA_Vector2 position;

    /** @brief Position of the secondary touch point. */
    CNA_Vector2 position2;

    /** @brief Delta of the primary touch point since the previous sample. */
    CNA_Vector2 delta;

    /** @brief Delta of the secondary touch point since the previous sample. */
    CNA_Vector2 delta2;
} CNA_GestureSample;

/**
 * @brief Initializes an empty gesture sample.
 *
 * @param out_sample Receives the canonical default-constructed value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_gesture_sample_init(CNA_GestureSample* out_sample);

/**
 * @brief Initializes a gesture sample, leaving both finger identifiers unset.
 *
 * @param gesture_type One `CNA_GESTURE_TYPE_*` identity or combination.
 * @param timestamp_ticks Gesture timestamp in 100-nanosecond ticks.
 * @param position Position of the primary touch point.
 * @param position2 Position of the secondary touch point.
 * @param delta Delta of the primary touch point.
 * @param delta2 Delta of the secondary touch point.
 * @param out_sample Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined gesture bit or a
 *         null output.
 *
 * Both finger identifiers become @ref CNA_TOUCH_NO_FINGER, exactly as the canonical constructor
 * leaves them. This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_gesture_sample_init_from_values(
    CNA_GestureType gesture_type,
    int64_t timestamp_ticks,
    CNA_Vector2 position,
    CNA_Vector2 position2,
    CNA_Vector2 delta,
    CNA_Vector2 delta2,
    CNA_GestureSample* out_sample);

/**
 * @brief Initializes a gesture sample with explicit finger identifiers.
 *
 * @param gesture_type One `CNA_GESTURE_TYPE_*` identity or combination.
 * @param timestamp_ticks Gesture timestamp in 100-nanosecond ticks.
 * @param position Position of the primary touch point.
 * @param position2 Position of the secondary touch point.
 * @param delta Delta of the primary touch point.
 * @param delta2 Delta of the secondary touch point.
 * @param finger_id Primary finger identifier, or @ref CNA_TOUCH_NO_FINGER.
 * @param finger_id2 Secondary finger identifier, or @ref CNA_TOUCH_NO_FINGER.
 * @param out_sample Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined gesture bit or a
 *         null output.
 *
 * The finger identifiers are stored verbatim and are not validated against any live touch, exactly
 * as the canonical constructor stores them. This pure POD operation touches no runtime state and
 * may run on any thread.
 */
CNA_C_API CNA_Result cna_gesture_sample_init_from_values_ext(
    CNA_GestureType gesture_type,
    int64_t timestamp_ticks,
    CNA_Vector2 position,
    CNA_Vector2 position2,
    CNA_Vector2 delta,
    CNA_Vector2 delta2,
    int32_t finger_id,
    int32_t finger_id2,
    CNA_GestureSample* out_sample);

/**
 * @brief Initializes disconnected touch capabilities with a zero touch count.
 *
 * @param out_capabilities Receives the canonical default-constructed value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_capabilities_init(CNA_TouchCapabilities* out_capabilities);

/**
 * @brief Initializes touch capabilities from an explicit connection state and touch count.
 *
 * @param is_connected Nonzero when a touch device is connected.
 * @param maximum_touch_count Maximum simultaneous touch points; must not be negative.
 * @param out_capabilities Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a negative count or a null
 *         output.
 *
 * **Documented deviation:** the canonical constructor accepts any `int`, including a negative one;
 * C refuses instead, because the C structure reports the count as an unsigned field and a negative
 * value could not be represented without silently changing it. This pure POD operation touches no
 * runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_capabilities_init_from_values_ext(
    CNA_Bool is_connected,
    int32_t maximum_touch_count,
    CNA_TouchCapabilities* out_capabilities);

/**
 * @brief Initializes an invalid touch location.
 *
 * @param out_location Receives the canonical default-constructed value.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_init(CNA_TouchLocation* out_location);

/**
 * @brief Initializes a touch location with an identifier, state and position.
 *
 * @param id Touch identifier.
 * @param state One `CNA_TOUCH_LOCATION_*` identity.
 * @param position Current position.
 * @param out_location Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined state or a null
 *         output.
 *
 * The previous state becomes `CNA_TOUCH_LOCATION_INVALID` and the pressure zero, exactly as the
 * canonical constructor leaves them. This pure POD operation touches no runtime state and may run
 * on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_init_from_values(
    int32_t id,
    CNA_TouchLocationState state,
    CNA_Vector2 position,
    CNA_TouchLocation* out_location);

/**
 * @brief Initializes a touch location including its previous state and position.
 *
 * @param id Touch identifier.
 * @param state One `CNA_TOUCH_LOCATION_*` identity.
 * @param position Current position.
 * @param previous_state Previous `CNA_TOUCH_LOCATION_*` identity.
 * @param previous_position Previous position.
 * @param out_location Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined state or a null
 *         output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_init_with_previous(
    int32_t id,
    CNA_TouchLocationState state,
    CNA_Vector2 position,
    CNA_TouchLocationState previous_state,
    CNA_Vector2 previous_position,
    CNA_TouchLocation* out_location);

/**
 * @brief Initializes a touch location with an identifier, state, position and pressure.
 *
 * @param id Touch identifier.
 * @param state One `CNA_TOUCH_LOCATION_*` identity.
 * @param position Current position.
 * @param pressure Touch pressure in the inclusive range zero through one.
 * @param out_location Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined state, a pressure
 *         outside zero through one, or a null output.
 *
 * **Documented deviation:** the canonical constructor stores any pressure verbatim; C refuses a
 * value outside the documented range, so a snapshot can never claim a pressure its own contract
 * excludes. This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_init_from_values_ext(
    int32_t id,
    CNA_TouchLocationState state,
    CNA_Vector2 position,
    float pressure,
    CNA_TouchLocation* out_location);

/**
 * @brief Initializes a touch location with previous location information and pressure.
 *
 * @param id Touch identifier.
 * @param state One `CNA_TOUCH_LOCATION_*` identity.
 * @param position Current position.
 * @param previous_state Previous `CNA_TOUCH_LOCATION_*` identity.
 * @param previous_position Previous position.
 * @param pressure Touch pressure in the inclusive range zero through one.
 * @param out_location Receives the constructed value.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined state, a pressure
 *         outside zero through one, or a null output.
 *
 * This pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_init_with_previous_ext(
    int32_t id,
    CNA_TouchLocationState state,
    CNA_Vector2 position,
    CNA_TouchLocationState previous_state,
    CNA_Vector2 previous_position,
    float pressure,
    CNA_TouchLocation* out_location);

/**
 * @brief Compares two touch locations for equality.
 *
 * @param left First location.
 * @param right Second location.
 * @param out_equal Receives `CNA_TRUE` when the two are equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument.
 *
 * The comparison covers the identifier, state, position, previous state and previous position, and
 * deliberately **ignores the pressure extension**, exactly as the canonical comparison does — two
 * locations differing only in pressure are equal. The canonical `==` and `!=` operators are this
 * same comparison and its negation, so C needs no separate route for them. This pure POD operation
 * touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_equals(
    const CNA_TouchLocation* left,
    const CNA_TouchLocation* right,
    CNA_Bool* out_equal);

/**
 * @brief Computes a touch location's hash code.
 *
 * @param location Location to hash.
 * @param out_hash Receives the hash code.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument.
 *
 * The hash mixes only the identifier and the position, so it ignores the pressure extension for
 * the same reason the comparison does. This pure POD operation touches no runtime state and may
 * run on any thread.
 */
CNA_C_API CNA_Result cna_touch_location_get_hash_code(
    const CNA_TouchLocation* location,
    int32_t* out_hash);

/**
 * @brief Reports the byte length of a touch location's text, without a terminator.
 *
 * @param location Location to describe.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null argument.
 */
CNA_C_API CNA_Result cna_touch_location_get_string_size(
    const CNA_TouchLocation* location,
    uint64_t* out_bytes);

/**
 * @brief Copies a touch location's text without a terminator.
 *
 * @param location Location to describe.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or
 *         `CNA_RESULT_INVALID_ARGUMENT`. No partial value is written.
 *
 * The canonical text reports **only the position**, as `{Position:{X:… Y:…}}`; it mentions neither
 * the identifier, the state nor the pressure. C reproduces that exactly rather than adding fields.
 */
CNA_C_API CNA_Result cna_touch_location_copy_string(
    const CNA_TouchLocation* location,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Initializes an empty touch collection snapshot.
 *
 * @param out_state Receives an empty snapshot.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The connection flag becomes `CNA_FALSE`. **Documented deviation:** the canonical
 * `getIsConnectedProperty` is a live read of the touch-device flag rather than stored state, so it
 * cannot be reproduced by a value with no runtime behind it. A locally constructed snapshot
 * therefore reports `CNA_FALSE`; use `cna_touch_get_state` for a captured snapshot, or
 * `cna_touch_panel_get_touch_device_exists_ext` for the live answer. This pure POD operation
 * touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_state_init(CNA_TouchState* out_state);

/**
 * @brief Initializes a touch collection snapshot from an array of locations.
 *
 * @param locations Touch locations to copy, or null only when @p count is zero.
 * @param count Number of locations; at most @ref CNA_TOUCH_MAX_TOUCHES.
 * @param out_state Receives the constructed snapshot.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for a null array with a nonzero
 *         count, a count above the fixed capacity, an undefined state in any location, or a null
 *         output.
 *
 * This maps **both** canonical vector-taking constructors: a copy and a move of the same sequence
 * are one array in C. The connection flag behaves as described for `cna_touch_state_init`. This
 * pure POD operation touches no runtime state and may run on any thread.
 */
CNA_C_API CNA_Result cna_touch_state_init_from_locations(
    const CNA_TouchLocation* locations,
    uint32_t count,
    CNA_TouchState* out_state);

/**
 * @brief Reports whether a touch collection describes itself as read-only.
 *
 * @param state Snapshot to inspect.
 * @param out_read_only Receives `CNA_TRUE`, always.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a null
 *         output.
 *
 * The answer is always `CNA_TRUE`, and it is **advisory**: the canonical property is hard-coded to
 * true while the mutation routes below still succeed. C is faithful to that rather than making the
 * flag mean something it does not mean.
 */
CNA_C_API CNA_Result cna_touch_state_get_is_read_only(
    const CNA_TouchState* state,
    CNA_Bool* out_read_only);

/**
 * @brief Reports whether a touch collection holds no locations.
 *
 * @param state Snapshot to inspect.
 * @param out_empty Receives `CNA_TRUE` when the snapshot holds no locations.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a null
 *         output.
 */
CNA_C_API CNA_Result cna_touch_state_get_is_empty_ext(
    const CNA_TouchState* state,
    CNA_Bool* out_empty);

/**
 * @brief Reports whether a touch collection contains a given location.
 *
 * @param state Snapshot to search.
 * @param item Location to find.
 * @param out_contains Receives `CNA_TRUE` when a matching location is present.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a null
 *         argument.
 *
 * The search uses the canonical comparison, which ignores the pressure extension.
 */
CNA_C_API CNA_Result cna_touch_state_contains(
    const CNA_TouchState* state,
    const CNA_TouchLocation* item,
    CNA_Bool* out_contains);

/**
 * @brief Reports the index of a given location in a touch collection.
 *
 * @param state Snapshot to search.
 * @param item Location to find.
 * @param out_index Receives the zero-based index, or -1 when the location is absent.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a null
 *         argument.
 *
 * An absent location answers -1 rather than failing, and the search ignores the pressure extension
 * for the same reason `cna_touch_state_contains` does.
 */
CNA_C_API CNA_Result cna_touch_state_index_of(
    const CNA_TouchState* state,
    const CNA_TouchLocation* item,
    int32_t* out_index);

/**
 * @brief Inserts a touch collection's locations into a caller-owned array.
 *
 * @param state Snapshot to copy from.
 * @param destination Caller-owned array, or null only when @p capacity is zero.
 * @param count Number of locations already present in @p destination.
 * @param capacity Total capacity of @p destination in locations.
 * @param destination_index Insertion point; must be between zero and @p count inclusive.
 * @param out_count Receives the resulting number of locations in @p destination.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_BUFFER_TOO_SMALL` when the result would not fit; or
 *         `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot, an out-of-range insertion point
 *         or a null argument. No partial value is written.
 *
 * This **inserts** rather than overwrites, which is a canonical behavior worth stating plainly: the
 * canonical destination is a growable vector and its copy operation shifts existing elements up.
 * C reproduces that on a caller-owned array, which is why the destination's current element count
 * is an argument rather than being inferred.
 */
CNA_C_API CNA_Result cna_touch_state_copy_to(
    const CNA_TouchState* state,
    CNA_TouchLocation* destination,
    uint64_t count,
    uint64_t capacity,
    int32_t destination_index,
    uint64_t* out_count);

/**
 * @brief Appends a location to a touch collection.
 *
 * @param state Snapshot to modify.
 * @param item Location to append.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_BUFFER_TOO_SMALL` when the snapshot is already full;
 *         or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot, an undefined state or a null
 *         argument.
 *
 * **Documented deviation:** the canonical collection grows without bound; the C snapshot has the
 * fixed capacity @ref CNA_TOUCH_MAX_TOUCHES, which is exactly the canonical touch-panel maximum.
 * An append beyond it is refused rather than silently dropping a touch.
 */
CNA_C_API CNA_Result cna_touch_state_add(
    CNA_TouchState* state,
    const CNA_TouchLocation* item);

/** @brief Removes every location from a touch collection.
 *
 * @param state Snapshot to modify.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot.
 */
CNA_C_API CNA_Result cna_touch_state_clear(CNA_TouchState* state);

/**
 * @brief Removes the first matching location from a touch collection.
 *
 * @param state Snapshot to modify.
 * @param item Location to remove.
 * @param out_removed Receives `CNA_TRUE` when a location was removed.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a null
 *         argument.
 *
 * An absent location is an ordinary `CNA_FALSE` answer rather than a failure, and the match
 * ignores the pressure extension.
 */
CNA_C_API CNA_Result cna_touch_state_remove(
    CNA_TouchState* state,
    const CNA_TouchLocation* item,
    CNA_Bool* out_removed);

/**
 * @brief Removes the location at a given index from a touch collection.
 *
 * @param state Snapshot to modify.
 * @param index Zero-based index; must be less than the current count.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or an
 *         out-of-range index.
 */
CNA_C_API CNA_Result cna_touch_state_remove_at(
    CNA_TouchState* state,
    int32_t index);

/**
 * @brief Inserts a location into a touch collection at a given index.
 *
 * @param state Snapshot to modify.
 * @param index Zero-based index; must be between zero and the current count inclusive.
 * @param item Location to insert.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_BUFFER_TOO_SMALL` when the snapshot is already full;
 *         or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot, an out-of-range index, an
 *         undefined state or a null argument.
 *
 * An index equal to the current count appends, exactly as the canonical insertion does.
 */
CNA_C_API CNA_Result cna_touch_state_insert(
    CNA_TouchState* state,
    int32_t index,
    const CNA_TouchLocation* item);

/**
 * @brief Reads the display width used for touch coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_width Receives the display width in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_touch_panel_get_display_width(
    CNA_Handle game,
    int32_t* out_width);

/**
 * @brief Sets the display width used for touch coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param width Display width in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is process-wide state a caller is expected to restore. It is stored verbatim, exactly
 * as the canonical property stores it, so a nonpositive value is accepted rather than refused.
 */
CNA_C_API CNA_Result cna_touch_panel_set_display_width(
    CNA_Handle game,
    int32_t width);

/**
 * @brief Reads the display height used for touch coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_height Receives the display height in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_touch_panel_get_display_height(
    CNA_Handle game,
    int32_t* out_height);

/**
 * @brief Sets the display height used for touch coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param height Display height in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is process-wide state a caller is expected to restore, and is stored verbatim.
 */
CNA_C_API CNA_Result cna_touch_panel_set_display_height(
    CNA_Handle game,
    int32_t height);

/**
 * @brief Reads the current display orientation.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_orientation Receives one `CNA_DISPLAY_ORIENTATION_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_touch_panel_get_display_orientation(
    CNA_Handle game,
    CNA_DisplayOrientation* out_orientation);

/**
 * @brief Sets the current display orientation.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param orientation One `CNA_DISPLAY_ORIENTATION_*` identity or combination.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined bit; or a
 *         documented handle/thread/native failure.
 *
 * The value is process-wide state a caller is expected to restore.
 */
CNA_C_API CNA_Result cna_touch_panel_set_display_orientation(
    CNA_Handle game,
    CNA_DisplayOrientation orientation);

/**
 * @brief Reads the gesture types currently enabled for detection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_gestures Receives a `CNA_GESTURE_TYPE_*` bit set.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_touch_panel_get_enabled_gestures(
    CNA_Handle game,
    CNA_GestureType* out_gestures);

/**
 * @brief Sets the gesture types enabled for detection.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param gestures A `CNA_GESTURE_TYPE_*` bit set, or `CNA_GESTURE_TYPE_NONE`.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined bit; or a
 *         documented handle/thread/native failure.
 *
 * The value is process-wide state a caller is expected to restore. The canonical flag operators
 * need no C route: this identity is a real bit set, so C combines and masks it with its own
 * bitwise operators, and every route here validates against @ref CNA_GESTURE_TYPE_ALL.
 */
CNA_C_API CNA_Result cna_touch_panel_set_enabled_gestures(
    CNA_Handle game,
    CNA_GestureType gestures);

/**
 * @brief Reports whether a gesture sample is ready to be read.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_available Receives `CNA_TRUE` when a gesture is queued.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Check this before `cna_touch_panel_read_gesture`, which refuses on an empty queue.
 */
CNA_C_API CNA_Result cna_touch_panel_get_is_gesture_available(
    CNA_Handle game,
    CNA_Bool* out_available);

/**
 * @brief Reads the native window handle the touch panel is bound to.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_window Receives the opaque native window value, which is zero when none is bound.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is an opaque native pointer-sized identifier, not a `CNA_Handle`, and the C API never
 * dereferences it. A backend that creates a real window publishes one here automatically, so do
 * not assume it is zero; query it, and restore whatever was bound.
 */
CNA_C_API CNA_Result cna_touch_panel_get_window_handle(
    CNA_Handle game,
    uint64_t* out_window);

/**
 * @brief Binds the touch panel to a native window handle.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param window Opaque native window value; zero unbinds.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A nonzero value is stored unvalidated, exactly as the canonical property stores it. This is
 * process-wide state a caller is expected to restore.
 */
CNA_C_API CNA_Result cna_touch_panel_set_window_handle(
    CNA_Handle game,
    uint64_t window);

/**
 * @brief Reports whether a touch device is currently known to exist.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_exists Receives `CNA_TRUE` when a touch device exists.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This is the live answer the canonical touch-collection connection getter reads.
 */
CNA_C_API CNA_Result cna_touch_panel_get_touch_device_exists_ext(
    CNA_Handle game,
    CNA_Bool* out_exists);

/**
 * @brief Sets whether a touch device is currently known to exist.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param exists Nonzero when a touch device exists.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This is process-wide state owned by the platform input bridge; a caller that changes it is
 * expected to put it back.
 */
CNA_C_API CNA_Result cna_touch_panel_set_touch_device_exists_ext(
    CNA_Handle game,
    CNA_Bool exists);

/**
 * @brief Removes and returns the oldest queued gesture sample.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_sample Caller-provided versioned structure to receive the gesture.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_STATE` when no gesture is queued; or a
 *         documented argument/handle/thread/native failure.
 *
 * An empty queue is a **refusal**, not an empty sample: the canonical operation throws, and C
 * preserves that rather than inventing a default answer. Check
 * `cna_touch_panel_get_is_gesture_available` first.
 */
CNA_C_API CNA_Result cna_touch_panel_read_gesture(
    CNA_Handle game,
    CNA_GestureSample* out_sample);

/**
 * @brief Queues a gesture sample for later retrieval.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param sample Gesture to queue.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure or an
 *         undefined gesture bit; or a documented handle/thread/native failure.
 *
 * This is what makes the gesture queue observable on a backend with no touch device, in the same
 * way the mouse and text-input raise routes make their events observable.
 */
CNA_C_API CNA_Result cna_touch_panel_enqueue_gesture_ext(
    CNA_Handle game,
    const CNA_GestureSample* sample);

/**
 * @brief Reports a normalized platform touch event to gesture processing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param finger_id Finger identifier.
 * @param state One `CNA_TOUCH_LOCATION_*` identity.
 * @param x Normalized x coordinate.
 * @param y Normalized y coordinate.
 * @param dx X delta since the previous event.
 * @param dy Y delta since the previous event.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined state; or a
 *         documented handle/thread/native failure.
 *
 * The coordinates are normalized and are forwarded verbatim; the canonical dispatch does not range
 * check them and neither does C.
 *
 * This feeds **gesture detection and the event-driven touch map, not the slot array**
 * `cna_touch_get_state` reports — the two are separate sources, and a raised event never appears
 * in that snapshot. Use `cna_touch_panel_set_finger_ext` to populate the snapshot.
 *
 * While no display size is published the event is **dropped**: the canonical dispatch scales the
 * normalized coordinates by the display size and refuses to collapse every touch onto the origin.
 * That drop is a successful no-op rather than a failure, so publish a width and height first.
 */
CNA_C_API CNA_Result cna_touch_panel_raise_touch_event_ext(
    CNA_Handle game,
    int32_t finger_id,
    CNA_TouchLocationState state,
    float x,
    float y,
    float dx,
    float dy);

/**
 * @brief Updates one touch slot with a finger identifier and pixel position.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Slot index below @ref CNA_TOUCH_MAX_TOUCHES.
 * @param finger_id Finger identifier, or @ref CNA_TOUCH_NO_FINGER to clear the slot.
 * @param position Current finger position in pixels.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an out-of-range slot; or a
 *         documented handle/thread/native failure.
 *
 * A slot written here becomes visible to `cna_touch_get_state` only after
 * `cna_touch_panel_update_ext` advances the frame. This is the source that snapshot reports;
 * `cna_touch_panel_raise_touch_event_ext` feeds gesture detection instead.
 *
 * Clearing a slot with @ref CNA_TOUCH_NO_FINGER does not make the touch disappear at once: the
 * next frame reports it one final time as released, with its previous state carried over, which is
 * the XNA contract for a lifted finger.
 */
CNA_C_API CNA_Result cna_touch_panel_set_finger_ext(
    CNA_Handle game,
    int32_t index,
    int32_t finger_id,
    CNA_Vector2 position);

/**
 * @brief Advances touch panel state by one frame.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This promotes pressed touches to moved, retires released ones, snapshots the previous frame and
 * runs gesture detection. Call it at most once per frame; capturing a state does not advance it.
 */
CNA_C_API CNA_Result cna_touch_panel_update_ext(CNA_Handle game);

/**
 * @brief Resets process-wide touch and gesture state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This clears **everything** the touch panel owns: the current and previous touch arrays, the
 * gesture queue, the touch-device-exists flag, the enabled gestures, the display width and height,
 * the display orientation and the bound window handle. A caller that depended on any of those must
 * set them again afterwards.
 *
 * The canonical class comment claims the display size and orientation survive the reset; its
 * implementation clears them, deliberately, so that a leaked display size cannot silently corrupt
 * another test's scaled touch coordinates. This description follows the behavior, not the comment.
 */
CNA_C_API CNA_Result cna_touch_panel_reset_for_tests_ext(CNA_Handle game);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_TOUCH_H
