// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_RUNTIME_WINDOW_H
#define CNA_C_RUNTIME_WINDOW_H

#include "CNA/C/display.h"
#include "CNA/C/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fixed-width identity of a game-window event. */
typedef uint32_t CNA_GameWindowEvent;

/** @brief The window's client area changed size. */
#define CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED UINT32_C(0)
/** @brief The window's display orientation changed. */
#define CNA_GAME_WINDOW_EVENT_ORIENTATION_CHANGED UINT32_C(1)
/** @brief The window moved to a different display. */
#define CNA_GAME_WINDOW_EVENT_SCREEN_DEVICE_NAME_CHANGED UINT32_C(2)
/** @brief Highest defined game-window event identity. */
#define CNA_GAME_WINDOW_EVENT_MAXIMUM CNA_GAME_WINDOW_EVENT_SCREEN_DEVICE_NAME_CHANGED

/**
 * @brief Reports whether the user may resize the window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_allowed Receives `CNA_TRUE` when resizing is allowed.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A game owns exactly one window, so every route here addresses the game's rather than taking a
 * window handle — the same answer this ABI already gives for the component collection, the service
 * container and the display metrics.
 */
CNA_C_API CNA_Result cna_game_window_get_allow_user_resizing(CNA_Handle game, CNA_Bool* out_allowed);

/**
 * @brief Allows or forbids the user resizing the window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param allowed Whether resizing is allowed.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the change, or a
 *         documented handle/thread failure.
 *
 * Every window state change here is a **request to the platform**, and a platform that cannot honour
 * it says so: a headless video driver refuses changes to a window it never really showed. That is a
 * platform failure rather than a fault in the call.
 */
CNA_C_API CNA_Result cna_game_window_set_allow_user_resizing(CNA_Handle game, CNA_Bool allowed);

/**
 * @brief Returns the window's client area.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bounds Receives the client rectangle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * A session with no native window answers the bounds the window object was last given, which in a
 * headless tree is the empty rectangle it started with.
 */
CNA_C_API CNA_Result cna_game_window_get_client_bounds(CNA_Handle game, CNA_Rectangle* out_bounds);

/**
 * @brief Returns the window's current display orientation.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_orientation Receives one `CNA_DISPLAY_ORIENTATION_*` identity.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_get_current_orientation(
    CNA_Handle game,
    CNA_DisplayOrientation* out_orientation);

/**
 * @brief Returns the platform handle of the native window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_handle Receives the native handle as an integer, or zero when there is no native
 *        window.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical property and the canonical native-window accessor answer the **same pointer**, so
 * this ABI has one route for both. It is an opaque platform value: passing it to a windowing library
 * is the only thing a caller can do with it, and this ABI neither validates nor owns it.
 */
CNA_C_API CNA_Result cna_game_window_get_native_handle_ext(CNA_Handle game, uint64_t* out_handle);

/**
 * @brief Returns the byte count of the display's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_get_screen_device_name_size(
    CNA_Handle game,
    uint64_t* out_bytes);

/**
 * @brief Copies the display's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_copy_screen_device_name(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the window title.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The title is set with `cna_game_set_window_title`, which this ABI has had since its first
 * release; this pair reads back what it set.
 */
CNA_C_API CNA_Result cna_game_window_get_title_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the window title.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_copy_title(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Reports whether the window is borderless.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_borderless Receives `CNA_TRUE` when the window has no border.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_get_is_borderless_ext(
    CNA_Handle game,
    CNA_Bool* out_borderless);

/**
 * @brief Shows or hides the window border.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param borderless Whether the window has no border.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the change, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_set_is_borderless_ext(CNA_Handle game, CNA_Bool borderless);

/**
 * @brief Minimizes the window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the change, or a
 *         documented handle/thread failure.
 *
 * A session with **no** native window accepts the request and does nothing, which is the canonical
 * behavior rather than a failure this ABI invents; a session with a window the platform will not
 * minimize reports the platform's refusal.
 */
CNA_C_API CNA_Result cna_game_window_minimize_ext(CNA_Handle game);

/**
 * @brief Restores the window from minimized.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the change, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_restore_ext(CNA_Handle game);

/**
 * @brief Begins a change of display or full-screen state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param will_be_full_screen Whether the window will be full screen when the change ends.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical pair brackets a device change: this route records the intent and
 * `cna_game_window_end_screen_device_change` applies it. Ending without beginning is allowed and
 * does nothing, exactly as canonically.
 */
CNA_C_API CNA_Result cna_game_window_begin_screen_device_change(
    CNA_Handle game,
    CNA_Bool will_be_full_screen);

/**
 * @brief Ends a change of display or full-screen state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param screen_device_name UTF-8 display name the window moves to.
 * @param client_width New client width, or a value less than one to keep the current width.
 * @param client_height New client height, or a value less than one to keep the current height.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_PLATFORM` when the platform refuses the change, or a
 *         documented argument/handle/thread failure.
 *
 * The canonical class has two overloads; the second is this one with the current client size, so C
 * has a single route and a caller passes a non-positive size to mean "keep it".
 */
CNA_C_API CNA_Result cna_game_window_end_screen_device_change(
    CNA_Handle game,
    CNA_StringView screen_device_name,
    int32_t client_width,
    int32_t client_height);

/**
 * @brief Returns the byte count of the window's .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_get_type_name_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the window's fully-qualified .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_window_copy_type_name(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to one of the window's events.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param event One `CNA_GAME_WINDOW_EVENT_*` identity.
 * @param callback Handler invoked when the event is raised.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or a null
 *         handler, or a documented handle/thread failure.
 *
 * Every canonical window event carries nothing but its sender, so the handler receives only its
 * context. Release the registration with `cna_game_unsubscribe`: a window registration and a game
 * registration are the same kind of thing and one route releases both.
 */
CNA_C_API CNA_Result cna_game_window_subscribe(
    CNA_Handle game,
    CNA_GameWindowEvent event,
    CNA_GameEventCallback callback,
    void* context,
    CNA_GameEventRegistrationHandle* out_registration);

#ifdef __cplusplus
}
#endif

#endif
