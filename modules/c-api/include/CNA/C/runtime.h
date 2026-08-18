// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_RUNTIME_H
#define CNA_C_RUNTIME_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents the game-loop timing snapshot passed to a frame callback.
 */
typedef struct CNA_GameTime {
    /** @brief Total elapsed game time in 100-nanosecond ticks. */
    int64_t total_game_time_ticks;

    /** @brief Elapsed game time for the current update in 100-nanosecond ticks. */
    int64_t elapsed_game_time_ticks;

    /** @brief `CNA_TRUE` when the native game loop is running slowly. */
    CNA_Bool is_running_slowly;

    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[7];
} CNA_GameTime;

/**
 * @brief Carries an optional diagnostic from a failing C lifecycle callback.
 */
typedef struct CNA_CallbackError {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Borrowed UTF-8 diagnostic bytes valid until the callback returns. */
    CNA_StringView message;
} CNA_CallbackError;

/**
 * @brief Receives a lifecycle notification from a CNA-owned game.
 *
 * @param game Borrowed game handle valid only for the callback duration.
 * @param game_time Current timing snapshot, or null for load, unload and exit notifications.
 * @param context Caller-owned context supplied in @ref CNA_GameCallbacks.
 * @param out_error CNA-initialized callback diagnostic structure that may be populated when the
 * callback returns a failure result.
 * @return `CNA_RESULT_SUCCESS` to continue; any other result stops the game and is reported to
 * the enclosing C API caller as `CNA_RESULT_CALLBACK`.
 */
typedef CNA_Result (*CNA_GameLifecycleCallback)(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error);

/**
 * @brief Receives the pre-draw notification and decides whether the frame draws.
 *
 * @param game Borrowed game handle valid only for the callback duration.
 * @param game_time Always null: the canonical hook takes no timing snapshot, and this ABI does not
 * invent one.
 * @param context Caller-owned context supplied in @ref CNA_GameCallbacks.
 * @param out_should_draw Pre-set to `CNA_TRUE`; set it to `CNA_FALSE` to skip this frame's drawing.
 * @param out_error CNA-initialized callback diagnostic structure that may be populated when the
 * callback returns a failure result.
 * @return `CNA_RESULT_SUCCESS` to continue; any other result stops the game.
 */
typedef CNA_Result (*CNA_GameBeginDrawCallback)(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_Bool* out_should_draw,
    CNA_CallbackError* out_error);

/**
 * @brief Defines the optional C lifecycle callbacks for a CNA game.
 */
typedef struct CNA_GameCallbacks {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Invoked once during native content loading. */
    CNA_GameLifecycleCallback load_content;

    /** @brief Invoked for each native update. */
    CNA_GameLifecycleCallback update;

    /** @brief Invoked for each native draw. */
    CNA_GameLifecycleCallback draw;

    /** @brief Invoked once while shutting down loaded content. */
    CNA_GameLifecycleCallback unload_content;

    /** @brief Invoked once when the game exits. */
    CNA_GameLifecycleCallback exiting;

    /** @brief Caller-owned context passed unchanged to every callback. */
    void* context;
} CNA_GameCallbacks;

/**
 * @brief The canonical frame hooks a game exposes beyond @ref CNA_GameCallbacks.
 *
 * These are a **second** table rather than more members on the first one, and deliberately so: the
 * published callback table is what every existing consumer already writes, and appending to it would
 * make every positional initializer in the world incomplete. A separate table costs one extra call
 * and leaves the established one untouched.
 *
 * Install it after `cna_game_create` and before the first frame; a hook installed later simply
 * starts being called from the next frame that reaches it.
 */
typedef struct CNA_GameFrameHooks {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Invoked once while the game initializes, before content loads. */
    CNA_GameLifecycleCallback initialize;

    /** @brief Invoked once before the first frame of a run. */
    CNA_GameLifecycleCallback begin_run;

    /** @brief Invoked once after the last frame of a run. */
    CNA_GameLifecycleCallback end_run;

    /**
     * @brief Invoked before each draw; reports whether drawing should happen at all.
     *
     * The canonical hook answers a boolean, so this one does too: leave @p out_should_draw as the
     * `CNA_TRUE` this ABI initializes it to in order to draw, or set it to `CNA_FALSE` to skip the
     * frame's drawing. A null handler draws.
     */
    CNA_GameBeginDrawCallback begin_draw;

    /** @brief Invoked after each draw, before the frame is presented. */
    CNA_GameLifecycleCallback end_draw;

    /** @brief Caller-owned context passed unchanged to every hook above. */
    void* context;
} CNA_GameFrameHooks;

/**
 * @brief Installs or removes the canonical frame hooks for a game.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param hooks Versioned hook table copied during this call, or null to remove every hook.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an invalid structure, or a
 *         documented handle/thread failure.
 *
 * Each member may be null and a null member is simply not called, the same rule the component
 * callback set follows.
 */
CNA_C_API CNA_Result cna_game_set_frame_hooks_ext(
    CNA_Handle game,
    const CNA_GameFrameHooks* hooks);

/**
 * @brief Configures creation of a C-owned CNA game.
 */
typedef struct CNA_GameCreateInfo {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Selects fixed (`CNA_TRUE`) or variable (`CNA_FALSE`) native timing. */
    CNA_Bool is_fixed_time_step;

    /** @brief Reserved bytes; callers must initialize them to zero. */
    uint8_t reserved[7];

    /** @brief Positive target elapsed time in 100-nanosecond ticks. */
    int64_t target_elapsed_time_ticks;

    /** @brief Initial UTF-8 window title; empty is valid and embedded NUL is rejected. */
    CNA_StringView window_title;

    /** @brief Optional versioned callback table copied during game creation. */
    const CNA_GameCallbacks* callbacks;
} CNA_GameCreateInfo;

/**
 * @brief Creates the process's active C-owned CNA game.
 *
 * @param create_info Versioned game configuration and optional callback table.
 * @param out_game Receives an owned game handle on success.
 * @return `CNA_RESULT_SUCCESS`, a documented validation failure, or a translated native failure.
 */
CNA_C_API CNA_Result cna_game_create(
    const CNA_GameCreateInfo* create_info,
    CNA_Handle* out_game);

/**
 * @brief Runs one native game frame on the game-creation thread.
 *
 * @param game Owned game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_CALLBACK`, or a documented handle/thread/native
 * failure.
 */
CNA_C_API CNA_Result cna_game_run_one_frame(CNA_Handle game);

/**
 * @brief Runs native frames until @ref cna_game_request_exit is called or a callback fails.
 *
 * @param game Owned game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_CALLBACK`, or a documented handle/thread/native
 * failure.
 */
CNA_C_API CNA_Result cna_game_run(CNA_Handle game);

/**
 * @brief Requests that the native game loop exit at its next safe point.
 *
 * @param game Owned game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_request_exit(CNA_Handle game);

/**
 * @brief Clears the current game graphics target to an RGBA color.
 *
 * @param game Owned game handle, or the callback-borrowed handle during a lifecycle callback.
 * @param color Unpacked 8-bit-per-channel clear color.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_game_clear(CNA_Handle game, CNA_Color color);

/**
 * @brief Sets a game window's UTF-8 title.
 *
 * @param game Owned game handle.
 * @param title UTF-8 title bytes without an embedded NUL scalar value.
 * @return `CNA_RESULT_SUCCESS`, an encoding/handle/thread failure, or a translated native
 * failure.
 */
CNA_C_API CNA_Result cna_game_set_window_title(
    CNA_Handle game,
    CNA_StringView title);

/**
 * @brief Shuts down and releases an owned game handle.
 *
 * @param game Owned game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_CALLBACK`, or a documented handle/thread/native
 * failure. The handle is invalid after this function successfully releases it, including when a
 * shutdown callback reports `CNA_RESULT_CALLBACK`.
 */
CNA_C_API CNA_Result cna_game_destroy(CNA_Handle game);

/** @brief Owned handle to one game event subscription. */
typedef CNA_Handle CNA_GameEventRegistrationHandle;

/** @brief Fixed-width identity of a game event. */
typedef uint32_t CNA_GameEvent;

/** @brief The game gained focus. */
#define CNA_GAME_EVENT_ACTIVATED UINT32_C(0)
/** @brief The game lost focus. */
#define CNA_GAME_EVENT_DEACTIVATED UINT32_C(1)
/** @brief The game was disposed. */
#define CNA_GAME_EVENT_DISPOSED UINT32_C(2)
/** @brief The game is exiting. */
#define CNA_GAME_EVENT_EXITING UINT32_C(3)
/** @brief Highest defined game-event identity. */
#define CNA_GAME_EVENT_MAXIMUM CNA_GAME_EVENT_EXITING

/**
 * @brief Handler invoked for a game event that carries no data.
 *
 * @param context The caller context supplied at subscription time.
 */
typedef void (*CNA_GameEventCallback)(void* context);

/**
 * @brief Reports whether the game currently has focus.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_active Receives `CNA_TRUE` while the game is the active application.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_is_active(CNA_Handle game, CNA_Bool* out_active);

/**
 * @brief Reports whether the mouse cursor is shown over the game window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_visible Receives `CNA_TRUE` when the cursor is shown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_is_mouse_visible(CNA_Handle game, CNA_Bool* out_visible);

/**
 * @brief Shows or hides the mouse cursor over the game window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param visible Whether the cursor is shown.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_game_set_is_mouse_visible(CNA_Handle game, CNA_Bool visible);

/**
 * @brief Reports whether the game uses fixed timing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_fixed Receives `CNA_TRUE` for fixed timing.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_is_fixed_time_step(CNA_Handle game, CNA_Bool* out_fixed);

/**
 * @brief Chooses fixed or variable timing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param fixed Whether the loop targets a fixed step.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_set_is_fixed_time_step(CNA_Handle game, CNA_Bool fixed);

/**
 * @brief Returns the targeted time between updates, in 100-nanosecond ticks.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_ticks Receives the target step.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_target_elapsed_time_ticks(CNA_Handle game, int64_t* out_ticks);

/**
 * @brief Sets the targeted time between updates, in 100-nanosecond ticks.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param ticks Positive target step.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a step that is not positive, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_set_target_elapsed_time_ticks(CNA_Handle game, int64_t ticks);

/**
 * @brief Returns how long the loop sleeps while the game is inactive, in 100-nanosecond ticks.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_ticks Receives the sleep duration.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_inactive_sleep_time_ticks(CNA_Handle game, int64_t* out_ticks);

/**
 * @brief Sets how long the loop sleeps while the game is inactive, in 100-nanosecond ticks.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param ticks Sleep duration; must not be negative.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a negative duration, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_set_inactive_sleep_time_ticks(CNA_Handle game, int64_t ticks);

/**
 * @brief Returns the frame rate the current target step implies.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_fps Receives frames per second.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_target_fps_ext(CNA_Handle game, double* out_fps);

/**
 * @brief Returns the current target step in milliseconds.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_milliseconds Receives the frame time.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_target_ms_frame_time_ext(
    CNA_Handle game,
    double* out_milliseconds);

/**
 * @brief Converts a frame rate to a frame time in milliseconds.
 *
 * @param frames_per_second Frames per second.
 * @param out_milliseconds Receives the frame time.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * The canonical helper is static and takes no game, so neither does this. A frame rate of zero or
 * less is the canonical caller's problem: the conversion answers what the canonical one answers
 * rather than refusing.
 */
CNA_C_API CNA_Result cna_game_fps_to_milliseconds_per_frame_ext(
    int32_t frames_per_second,
    double* out_milliseconds);

/**
 * @brief Reports whether the canonical run loop keeps running.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_running Receives `CNA_TRUE` while the loop should keep going.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_run_application_ext(CNA_Handle game, CNA_Bool* out_running);

/**
 * @brief Stops or resumes the canonical run loop.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param running Whether the loop should keep going.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * This is the flag `cna_game_run` reads between frames; `cna_game_request_exit` is the canonical way
 * to stop, and this is the raw switch behind it.
 */
CNA_C_API CNA_Result cna_game_set_run_application_ext(CNA_Handle game, CNA_Bool running);

/**
 * @brief Forgets the time accumulated since the last update.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 *
 * Call this after a long blocking operation so the next frame is not reported as running slowly.
 */
CNA_C_API CNA_Result cna_game_reset_elapsed_time(CNA_Handle game);

/**
 * @brief Skips the drawing of the next frame.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_suppress_draw(CNA_Handle game);

/**
 * @brief Runs one update and, unless drawing was suppressed, one draw.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_CALLBACK`, or a documented handle/thread/native failure.
 *
 * This is the canonical frame step `cna_game_run_one_frame` wraps; it does not process host events.
 * Like running and destroying the game, it is **refused from inside a lifecycle callback**, because
 * a frame step called from within a frame would re-enter the loop it is part of.
 */
CNA_C_API CNA_Result cna_game_tick(CNA_Handle game);

/**
 * @brief Returns the byte count of the game's .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_get_type_name_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the game's fully-qualified .NET type name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_copy_type_name(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Subscribes to one of the game's events.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param event One `CNA_GAME_EVENT_*` identity.
 * @param callback Handler invoked when the event is raised.
 * @param context Caller context passed back to @p callback.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity or a null
 *         handler, or a documented handle/thread failure.
 *
 * Every canonical game event carries nothing but its sender, so the handler receives only its
 * context. The exiting event is declared with the plain event-argument type even though a named
 * `ExitingEventArgs` exists, so nothing is lost here. The exiting **callback** in
 * `CNA_GameCallbacks` is a different thing: it can stop the game by failing, while these handlers
 * only observe.
 */
CNA_C_API CNA_Result cna_game_subscribe(
    CNA_Handle game,
    CNA_GameEvent event,
    CNA_GameEventCallback callback,
    void* context,
    CNA_GameEventRegistrationHandle* out_registration);

/**
 * @brief Releases a game event registration.
 *
 * @param registration Owned registration handle from `cna_game_subscribe`.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 */
CNA_C_API CNA_Result cna_game_unsubscribe(CNA_GameEventRegistrationHandle registration);

/**
 * @brief Returns how many launch parameters the game holds.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the parameter count.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical value is a string-to-string map the game owns, so it has no handle of its own. The
 * default parameters are parsed from the process command line when the game is created.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_get_count(CNA_Handle game, uint64_t* out_count);

/**
 * @brief Reports whether one launch parameter is present.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key UTF-8 parameter name.
 * @param out_present Receives `CNA_TRUE` when the parameter is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_contains_key(
    CNA_Handle game,
    CNA_StringView key,
    CNA_Bool* out_present);

/**
 * @brief Returns the byte count of one launch parameter's value.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key UTF-8 parameter name.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` when the parameter is absent, or a
 *         documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_get_value_size(
    CNA_Handle game,
    CNA_StringView key,
    uint64_t* out_bytes);

/**
 * @brief Copies one launch parameter's value.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key UTF-8 parameter name.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT` when the parameter is absent, or a documented failure.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_copy_value(
    CNA_Handle game,
    CNA_StringView key,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of the launch parameter name at one index.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below `cna_game_launch_parameters_get_count`.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index at or above the count
 *         or a null output, or a documented handle/thread failure.
 *
 * Every other accessor here is keyed, which is enough to *read* a parameter a caller can already
 * name and not enough to *materialize the map* -- and the canonical value is a
 * `Dictionary<string, string>` that games enumerate. This pair is that enumeration.
 *
 * **The order is by name, ordinal byte order, ascending** -- deliberately not the canonical
 * container's own order. That container is a hash map, so its traversal order is unspecified and a
 * single insertion may rehash and reorder every element; an index into it would be meaningless
 * between calls. Sorting makes the sequence a function of the key set alone: for a fixed set of
 * parameters the same index always names the same key, in this process and the next. Each name
 * appears exactly once.
 *
 * Adding or replacing a parameter still invalidates indices obtained before it, exactly as it
 * would for any snapshot: read the count and walk it without adding in between.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_get_key_size(
    CNA_Handle game,
    uint64_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies the launch parameter name at one index.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based index below `cna_game_launch_parameters_get_count`.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT` for an index at or above the count, or a documented
 *         handle/thread failure.
 *
 * The index order is the one `cna_game_launch_parameters_get_key_size` documents. Pair the name
 * this returns with `cna_game_launch_parameters_copy_value` to read the whole map.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_copy_key(
    CNA_Handle game,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Adds or replaces one launch parameter.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param key UTF-8 parameter name.
 * @param value UTF-8 parameter value.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical add overwrites an existing entry rather than refusing, and so does this.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_add(
    CNA_Handle game,
    CNA_StringView key,
    CNA_StringView value);

/**
 * @brief Replaces the game's launch parameters by parsing an argument list.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param arguments Array of @p argument_count UTF-8 arguments, or null when the count is zero.
 * @param argument_count Number of arguments.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * This is the canonical argument-list constructor, and its parsing rules are the canonical ones
 * rather than the ones a reader might expect: leading flag markers are trimmed, the name and value
 * are split on the first **colon** — not an equals sign — an argument shorter than three characters
 * or without a colon is skipped silently, and the **first** occurrence of a name wins. An empty list
 * leaves the game with no parameters at all rather than re-reading the command line.
 */
CNA_C_API CNA_Result cna_game_launch_parameters_parse_ext(
    CNA_Handle game,
    const CNA_StringView* arguments,
    uint64_t argument_count);

/**
 * @brief Pumps the framework-wide per-frame work the game loop normally drives.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 *
 * The canonical dispatcher is static and exists for applications that do not run the game loop; a
 * game handle is taken here only for thread affinity. Calling it while the loop runs is harmless
 * and does the work twice.
 */
CNA_C_API CNA_Result cna_framework_dispatcher_update(CNA_Handle game);

/**
 * @brief Returns the byte count of the title's base path.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical accessor is static and resolves the executable's directory on first use; the game
 * handle is taken for thread affinity only.
 */
CNA_C_API CNA_Result cna_title_location_get_path_size(CNA_Handle game, uint64_t* out_bytes);

/**
 * @brief Copies the title's base path.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT`, or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_title_location_copy_path(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Overrides the title's base path.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param path UTF-8 base path.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The path is **process-wide**, exactly as canonically: it is not scoped to the game handle this
 * route validates, and it changes where every later title read looks.
 */
CNA_C_API CNA_Result cna_title_location_set_path_ext(CNA_Handle game, CNA_StringView path);

/**
 * @brief Reads a whole file from the title's content location.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param name UTF-8 file name relative to the title path, or an absolute path.
 * @param destination Buffer receiving the bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the file's byte count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_IO` when the file cannot be opened, or a documented argument/handle/thread
 *         failure.
 *
 * The canonical operation hands back an open stream. This ABI has no stream handle for title
 * content, and a title asset is read to use it, so the count/copy pair delivers the **whole file**
 * instead. That is a deliberate narrowing: incremental reads over a title stream are not available.
 * The canonical failure for a missing file is a plain runtime error, which this route reports as
 * `CNA_RESULT_IO` rather than letting it reach a caller as an internal failure.
 */
CNA_C_API CNA_Result cna_title_container_read_ext(
    CNA_Handle game,
    CNA_StringView name,
    uint8_t* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

#ifdef __cplusplus
}
#endif

#endif
