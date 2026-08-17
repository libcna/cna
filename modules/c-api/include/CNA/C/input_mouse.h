// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_MOUSE_H
#define CNA_C_INPUT_MOUSE_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned registration for the mouse clicked event. */
typedef CNA_Handle CNA_MouseEventRegistrationHandle;

/**
 * @brief Receives a mouse click.
 *
 * @param button Native button number the click came from.
 * @param context The caller's own context, copied when the handler was registered.
 *
 * The callback runs synchronously on the thread that raised the click. It must not re-enter the
 * C API's mouse routes.
 */
typedef void (*CNA_MouseClickedCallback)(int32_t button, void* context);

/**
 * @brief Initializes a mouse snapshot with the canonical default value.
 *
 * @param out_state Receives the versioned structure: origin position, no button pressed and both
 *        wheels at zero.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 */
CNA_C_API CNA_Result cna_mouse_state_init(CNA_MouseState* out_state);

/**
 * @brief Initializes a mouse snapshot from explicit values.
 *
 * @param x Horizontal position.
 * @param y Vertical position.
 * @param scroll_wheel Cumulative vertical wheel value.
 * @param pressed_buttons Zero or more `CNA_MOUSE_BUTTON_*` bits.
 * @param out_state Receives the versioned structure.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a bit
 *         outside the defined mouse buttons. The output is unchanged on failure.
 *
 * The canonical constructor takes five separate button states; C takes the same bit set the
 * snapshot already carries, so a consumer never has to spell five arguments in the right order.
 * The horizontal wheel is left at zero, exactly as the canonical eight-argument constructor does.
 */
CNA_C_API CNA_Result cna_mouse_state_init_from_values(
    int32_t x,
    int32_t y,
    int32_t scroll_wheel,
    CNA_MouseButtonFlags pressed_buttons,
    CNA_MouseState* out_state);

/**
 * @brief Initializes a mouse snapshot from explicit values including the horizontal wheel.
 *
 * @param x Horizontal position.
 * @param y Vertical position.
 * @param scroll_wheel Cumulative vertical wheel value.
 * @param horizontal_scroll_wheel Cumulative horizontal wheel value.
 * @param pressed_buttons Zero or more `CNA_MOUSE_BUTTON_*` bits.
 * @param out_state Receives the versioned structure.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output or a bit
 *         outside the defined mouse buttons. The output is unchanged on failure.
 *
 * This maps the CNA extension constructor, which is why it keeps an `_ext` suffix.
 */
CNA_C_API CNA_Result cna_mouse_state_init_from_values_ext(
    int32_t x,
    int32_t y,
    int32_t scroll_wheel,
    int32_t horizontal_scroll_wheel,
    CNA_MouseButtonFlags pressed_buttons,
    CNA_MouseState* out_state);

/**
 * @brief Compares two mouse snapshots for equality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_equals Receives `CNA_TRUE` when every canonical field compares equal.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_mouse_state_equals(
    const CNA_MouseState* left,
    const CNA_MouseState* right,
    CNA_Bool* out_equals);

/**
 * @brief Compares two mouse snapshots for inequality.
 *
 * @param left First snapshot.
 * @param right Second snapshot.
 * @param out_not_equals Receives `CNA_TRUE` when the snapshots differ.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_mouse_state_not_equals(
    const CNA_MouseState* left,
    const CNA_MouseState* right,
    CNA_Bool* out_not_equals);

/**
 * @brief Computes the canonical hash of a mouse snapshot.
 *
 * @param state Snapshot to hash.
 * @param out_hash Receives the hash.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 *
 * The canonical hash mixes only the position and the vertical wheel; the buttons and the
 * horizontal wheel do not contribute, so two snapshots differing only in those hash alike.
 */
CNA_C_API CNA_Result cna_mouse_state_get_hash_code(
    const CNA_MouseState* state,
    int32_t* out_hash);

/**
 * @brief Reports the byte length of a mouse snapshot's text, without a terminator.
 *
 * @param state Snapshot to describe.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for an invalid snapshot or a
 *         null output.
 */
CNA_C_API CNA_Result cna_mouse_state_get_string_size(
    const CNA_MouseState* state,
    uint64_t* out_bytes);

/**
 * @brief Copies a mouse snapshot's text without a terminator.
 *
 * @param state Snapshot to describe.
 * @param destination Caller-owned destination, or null only when @p capacity is zero.
 * @param capacity Destination capacity in bytes.
 * @param out_bytes Receives the required byte count without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or
 *         `CNA_RESULT_INVALID_ARGUMENT`. No partial value is written.
 *
 * Unlike the gamepad and keyboard snapshots, this type **does** override its string conversion:
 * the text reports the position, the pressed buttons by name — or `None` — and the vertical wheel,
 * and it does not mention the horizontal wheel.
 */
CNA_C_API CNA_Result cna_mouse_state_copy_string(
    const CNA_MouseState* state,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reads the native window handle mouse input is bound to.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_window Receives the opaque native window value, which is zero when none is bound.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is an opaque native pointer-sized identifier, not a `CNA_Handle`, and the C API never
 * dereferences it.
 */
CNA_C_API CNA_Result cna_mouse_get_window_handle(
    CNA_Handle game,
    uint64_t* out_window);

/**
 * @brief Binds mouse input to a native window handle.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param window Opaque native window value; zero unbinds.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_mouse_set_window_handle(
    CNA_Handle game,
    uint64_t window);

/**
 * @brief Moves the cursor to a position inside the bound window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param x Horizontal position.
 * @param y Vertical position.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical operation returns nothing, so a request no backend can satisfy succeeds and
 * changes nothing.
 */
CNA_C_API CNA_Result cna_mouse_set_position(CNA_Handle game, int32_t x, int32_t y);

/**
 * @brief Reads whether relative mouse mode is active.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_enabled Receives `CNA_TRUE` while relative mode is active.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_mouse_get_is_relative_mouse_mode_ext(
    CNA_Handle game,
    CNA_Bool* out_enabled);

/**
 * @brief Turns relative mouse mode on or off.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param enabled Nonzero to enable relative mode.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_mouse_set_is_relative_mouse_mode_ext(
    CNA_Handle game,
    CNA_Bool enabled);

/**
 * @brief Turns mouse capture on or off.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param enabled Nonzero to capture the mouse.
 * @param out_applied Receives `CNA_TRUE` when the backend accepted the request.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A backend that cannot capture reports `CNA_FALSE` rather than failing.
 */
CNA_C_API CNA_Result cna_mouse_set_capture_ext(
    CNA_Handle game,
    CNA_Bool enabled,
    CNA_Bool* out_applied);

/**
 * @brief Reads the cursor position in desktop coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_x Receives the horizontal position.
 * @param out_y Receives the vertical position.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical query returns nothing and reports both coordinates through output references, so
 * a backend that cannot answer leaves them at zero rather than failing.
 */
CNA_C_API CNA_Result cna_mouse_get_global_position_ext(
    CNA_Handle game,
    int32_t* out_x,
    int32_t* out_y);

/**
 * @brief Moves the cursor to a position in desktop coordinates.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param x Horizontal position.
 * @param y Vertical position.
 * @param out_applied Receives `CNA_TRUE` when the backend accepted the request.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_mouse_warp_global_ext(
    CNA_Handle game,
    int32_t x,
    int32_t y,
    CNA_Bool* out_applied);

/**
 * @brief Subscribes to the mouse clicked event.
 *
 * @param callback Handler invoked for each click.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event is **static**, so the subscription belongs to the process rather than to a
 * game and takes no game handle. Release the registration with
 * `cna_mouse_unsubscribe_clicked_ext`; releasing it after the process event has been cleared is a
 * no-op rather than a failure.
 */
CNA_C_API CNA_Result cna_mouse_subscribe_clicked_ext(
    CNA_MouseClickedCallback callback,
    void* context,
    CNA_MouseEventRegistrationHandle* out_registration);

/**
 * @brief Releases a mouse clicked-event registration.
 *
 * @param registration Owned registration handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 */
CNA_C_API CNA_Result cna_mouse_unsubscribe_clicked_ext(
    CNA_MouseEventRegistrationHandle registration);

/**
 * @brief Raises the mouse clicked event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param button Native button number to report.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical internal raise, which exists so a platform layer — or a test — can
 * deliver a click without a real device. Every subscribed handler runs synchronously before the
 * call returns.
 */
CNA_C_API CNA_Result cna_mouse_raise_clicked_ext(CNA_Handle game, int32_t button);

/**
 * @brief Resets process-wide mouse state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical test-support reset: it clears the bound window handle and drops every
 * clicked-event subscription, including ones this API handed out. A registration released
 * afterwards is a no-op.
 */
CNA_C_API CNA_Result cna_mouse_reset_for_tests_ext(CNA_Handle game);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_MOUSE_H
