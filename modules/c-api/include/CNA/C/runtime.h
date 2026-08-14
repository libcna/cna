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

#ifdef __cplusplus
}
#endif

#endif
