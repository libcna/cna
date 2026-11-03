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
 * @param out_bounds Receives the client rectangle; unchanged on failure.
 * @return CNA_RESULT_SUCCESS, CNA_RESULT_CALLBACK for a logical-size provider failure, or a
 * documented argument/handle/thread failure.
 *
 * As in XNA, `x` and `y` are the client area's position on the desktop; a platform with no desktop
 * placement to report (Wayland, a browser canvas) answers zero. A session with no native window answers the bounds the window object was last given, which in a
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
 * @brief Returns the canonical `Handle` property of the game window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_handle Receives the platform's round-trip window token, or zero when the renderer
 *        never creates a window.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * **This is not the native window.** The header used to claim this property and the canonical
 * native-window accessor answer the same pointer; they do not. This one maps XNA's integer `Handle`
 * property, whose value is the platform's own round-trip token -- under SDL3 the platform window
 * pointer widened to an integer -- and the platform layer that mints it says outright that new
 * interop code should not use it. It is meaningful only to the platform instance that created it,
 * and its one documented use is the round trip: handing it back through
 * `PresentationParameters.DeviceWindowHandle` to adopt an existing window.
 *
 * Zero here means *this renderer never creates a window* -- headless, software and stub renderers
 * all report it -- rather than *this call failed*.
 *
 * For interop, use @ref cna_game_window_get_native_window_ext, which answers the actual native
 * window and says which windowing system it belongs to.
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
 *         documented argument/handle/thread failure. A logical-size provider failure returns
 *         CNA_RESULT_CALLBACK with its first retained diagnostic.
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

/** @brief Fixed-width identity of the windowing system a native window belongs to. */
typedef uint32_t CNA_NativeWindowSystem;

/** @brief No native window, or a window whose system could not be determined. */
#define CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN UINT32_C(0)
/** @brief Microsoft Windows; @ref CNA_NativeWindowHandle::window is an `HWND`. */
#define CNA_NATIVE_WINDOW_SYSTEM_WIN32 UINT32_C(1)
/** @brief X11; carries a `Display*` plus an integer `Window` XID. */
#define CNA_NATIVE_WINDOW_SYSTEM_X11 UINT32_C(2)
/** @brief Wayland; carries a `wl_display*` plus a `wl_surface*`. */
#define CNA_NATIVE_WINDOW_SYSTEM_WAYLAND UINT32_C(3)
/** @brief macOS; @ref CNA_NativeWindowHandle::window is an `NSWindow*`. */
#define CNA_NATIVE_WINDOW_SYSTEM_COCOA UINT32_C(4)
/** @brief Android; @ref CNA_NativeWindowHandle::window is an `ANativeWindow*`. */
#define CNA_NATIVE_WINDOW_SYSTEM_ANDROID UINT32_C(5)
/** @brief Web/Emscripten; the drawing target is a canvas chosen by the host page, not a pointer. */
#define CNA_NATIVE_WINDOW_SYSTEM_WEB UINT32_C(6)
/** @brief A platform that renders with no native window at all; every pointer is null. */
#define CNA_NATIVE_WINDOW_SYSTEM_HEADLESS UINT32_C(7)
/** @brief A character-cell terminal with no native window; every pointer is null. */
#define CNA_NATIVE_WINDOW_SYSTEM_TERMINAL UINT32_C(8)
/** @brief iOS/iPadOS; @ref CNA_NativeWindowHandle::window is a `UIWindow*`. */
#define CNA_NATIVE_WINDOW_SYSTEM_UIKIT UINT32_C(9)
/** @brief Highest defined native windowing-system identity. */
#define CNA_NATIVE_WINDOW_SYSTEM_MAXIMUM CNA_NATIVE_WINDOW_SYSTEM_UIKIT

/**
 * @brief Describes the game's native window well enough to hand it to another library.
 *
 * This is the one place the ABI publishes a backend detail on purpose. A binding that hosts a CNA
 * window inside another toolkit, attaches a native input method, or passes the surface to another
 * graphics API has no portable way to do it, and CNA's own C++ surface already publishes the same
 * accessor as an extension. What is kept out of a consumer's program is the *platform layer* -- no
 * SDL type appears here -- not the window's identity.
 *
 * @ref system says which of the other fields carry anything; **never read one without checking it
 * first**, because a field that does not apply to the reported system is null or zero rather than
 * absent, and a null `wl_display*` is indistinguishable from an `HWND` that happens to be null.
 *
 * Every pointer here is **borrowed and owned by the platform**. It is valid only while the game's
 * window is alive, this ABI neither validates nor frees any of it, and storing one past the game's
 * destruction is a use-after-free in the consumer's program rather than a failure this ABI can
 * report.
 */
typedef struct CNA_NativeWindowHandle {
    /** @brief Size of this structure in bytes; set by @ref cna_native_window_handle_init. */
    uint32_t struct_size;

    /** @brief Structure version; set by @ref cna_native_window_handle_init. */
    uint32_t struct_version;

    /** @brief One `CNA_NATIVE_WINDOW_SYSTEM_*` identity; decides which fields below mean anything. */
    CNA_NativeWindowSystem system;

    /** @brief X11 `Display*` or Wayland `wl_display*`; null on every other system. Borrowed. */
    void* display;

    /** @brief Win32 `HWND`, Cocoa `NSWindow*` or Android `ANativeWindow*`; null elsewhere. Borrowed. */
    void* window;

    /** @brief Wayland `wl_surface*`; null on every other system. Borrowed. */
    void* surface;

    /**
     * @brief X11 `Window` XID; zero on every other system.
     *
     * An XID is a server-side integer resource identifier, not an address, which is why it has its
     * own field and why @ref window stays null under X11.
     */
    uint64_t window_id;
} CNA_NativeWindowHandle;

/**
 * @brief Initializes a native-window-handle structure to the empty, no-native-window state.
 *
 * @param handle Structure to initialize.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` when @p handle is null.
 *
 * Produces @ref CNA_NATIVE_WINDOW_SYSTEM_UNKNOWN with every pointer null and the XID zero, which is
 * also what a query against a platform with no native window answers.
 */
CNA_C_API CNA_Result cna_native_window_handle_init(CNA_NativeWindowHandle* handle);

/**
 * @brief Describes the game's native window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param handle Structure initialized by @ref cna_native_window_handle_init, receiving the
 *        description. Its pointers are borrowed; see @ref CNA_NativeWindowHandle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when @p handle is null or was not
 *         initialized for this ABI version, or a documented handle/thread failure.
 *
 * A platform with no native window -- headless, or a terminal -- is not a failure here: the call
 * succeeds and reports its own system identity with null pointers, so a caller can tell *this
 * platform has no window* apart from *this call did not work*.
 */
CNA_C_API CNA_Result cna_game_window_get_native_window_ext(
    CNA_Handle game,
    CNA_NativeWindowHandle* handle);

/**
 * @brief Gets keyboard orientation emulation enabled.
 *
 * Window-local flag, false on a new window.
 *
 * @param game Active game on its owner thread; no token is retained.
 * @param out_enabled Required canonical boolean output, unchanged on failure.
 * @return CNA_RESULT_SUCCESS, or argument/handle/thread/native state errors.
 */
CNA_C_API CNA_Result cna_game_window_get_keyboard_orientation_emulation_enabled_ext(
    CNA_Handle game,
    CNA_Bool* out_enabled);

/**
 * @brief Sets keyboard orientation emulation enabled.
 *
 * Window-local flag, false on a new window.
 *
 * @param game Active game on its owner thread; no token is retained.
 * @param enabled Canonical CNA_FALSE or CNA_TRUE; invalid values preserve native state.
 * @return CNA_RESULT_SUCCESS, or argument/handle/thread/native state errors.
 */
CNA_C_API CNA_Result cna_game_window_set_keyboard_orientation_emulation_enabled_ext(
    CNA_Handle game,
    CNA_Bool enabled);


/**
 * @brief Supplies the game's logical surface dimensions synchronously on its owner thread.
 *
 * @param game Callback-borrowed game handle; callback-safe queries are permitted.
 * @param context Caller-owned context copied at invocation start.
 * @param inout_width Previous accepted logical width; write a proposed width.
 * @param inout_height Previous accepted logical height; write a proposed height.
 * @param out_available Initially CNA_FALSE; set CNA_TRUE to accept positive proposed dimensions.
 * @param out_error Initialized callback error; diagnostic storage must survive callback return.
 * @return CNA_RESULT_SUCCESS to answer/decline; another result records CNA_RESULT_CALLBACK,
 * requests game exit and preserves the first diagnostic, as for lifecycle callbacks.
 *
 * CNA_FALSE or nonpositive dimensions preserve the previous size (initially the client size).
 * A noncanonical availability boolean is a callback failure. Dimensions are used for native
 * window orientation/fullscreen client bounds, not to resize graphics render targets. Runs inside
 * game callback scope: recursive game driving/destruction is refused. Nested size queries use the
 * previous accepted dimensions without recursively invoking this provider. Return normally;
 * no C++ exception or longjmp may cross CNA. Output pointers expire on return.
 */
typedef CNA_Result (*CNA_GameWindowLogicalSizeProviderEXT)(
    CNA_Handle game, void* context, int32_t* inout_width, int32_t* inout_height,
    CNA_Bool* out_available, CNA_CallbackError* out_error);

/**
 * @brief Installs, replaces or removes a retained logical-size provider.
 *
 * @param game Active game on its owner thread.
 * @param callback Provider, or null to remove and restore native client-size fallback.
 * @param context Caller-owned context; ignored when callback is null.
 * @return Success or a documented handle, thread or native failure. Installation does not invoke
 * the provider. An enclosing game drive or screen-device-change route reports callback failures
 * as CNA_RESULT_CALLBACK with the first copied diagnostic.
 *
 * Keep function/context alive until replacement, removal or game destruction and completion of
 * any in-flight callback. Replacement inside a callback affects later invocations only; the old
 * context must remain valid until that invocation returns. Registration does not block game
 * destruction and makes no call after source destruction. The caller owns all context memory.
 */
CNA_C_API CNA_Result cna_game_window_set_logical_size_provider_ext(
    CNA_Handle game, CNA_GameWindowLogicalSizeProviderEXT callback, void* context);

/**
 * @brief Receives file/text drops synchronously on the game owner thread.
 * @param context Caller context.
 * @param items Borrowed UTF-8 views, valid only for this invocation; text drops have one item.
 * @param count Number of items.
 *
 * May query game state and unsubscribe, including itself. Do not destroy the game or
 * throw/longjmp through CNA. Context remains valid through callback return and until
 * unregister/replacement/source destruction; no new invocation starts after these return.
 */
typedef void (*CNA_GameWindowDropCallback)(void* context, const CNA_StringView* items, uint64_t count);
/**
 * @brief Registers the native window file drop.
 *
 * @param game Active game handle; owner thread.
 * @param callback Required callback.
 * @param context Caller-owned context, possibly null.
 * @param out_registration Owned registration released with cna_game_unsubscribe; failed output is invalid.
 * @return CNA_RESULT_SUCCESS or a documented argument, handle, thread or native failure.
 *
 * Registration does not block game teardown and remains releasable after source death.
 */
CNA_C_API CNA_Result cna_game_window_subscribe_file_drop_ext(CNA_Handle game, CNA_GameWindowDropCallback callback, void* context, CNA_GameEventRegistrationHandle* out_registration);
/**
 * @brief Registers the native window text drop.
 *
 * @param game Active game handle; owner thread.
 * @param callback Required callback.
 * @param context Caller-owned context, possibly null.
 * @param out_registration Owned registration released with cna_game_unsubscribe; failed output is invalid.
 * @return CNA_RESULT_SUCCESS or a documented argument, handle, thread or native failure.
 *
 * Registration does not block game teardown and remains releasable after source death.
 */
CNA_C_API CNA_Result cna_game_window_subscribe_text_drop_ext(CNA_Handle game, CNA_GameWindowDropCallback callback, void* context, CNA_GameEventRegistrationHandle* out_registration);
/**
 * @brief Raises a native file drop synchronously.
 *
 * @param game Active game handle; owner thread.
 * @param files Call-only array of UTF-8 paths; null only for zero count.
 * @param count Number of paths.
 * @return CNA_RESULT_SUCCESS or a documented argument, handle, thread or native failure.
 *
 * All input is validated and copied before dispatch. Embedded NUL is rejected.
 * Subscribers may unsubscribe during dispatch; cancelled subscribers are not invoked later.
 */
CNA_C_API CNA_Result cna_game_window_raise_file_drop_ext(CNA_Handle game, const CNA_StringView* files, uint64_t count);
/**
 * @brief Raises a native text drop synchronously.
 *
 * @param game Active game handle; owner thread.
 * @param text Call-only UTF-8 drop text.
 * @return CNA_RESULT_SUCCESS or a documented argument, handle, thread or native failure.
 *
 * All input is validated and copied before dispatch. Embedded NUL is rejected.
 * Subscribers may unsubscribe during dispatch; cancelled subscribers are not invoked later.
 */
CNA_C_API CNA_Result cna_game_window_raise_text_drop_ext(CNA_Handle game, CNA_StringView text);
#ifdef __cplusplus
}
#endif

#endif
