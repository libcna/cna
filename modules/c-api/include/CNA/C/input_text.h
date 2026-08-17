// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_INPUT_TEXT_H
#define CNA_C_INPUT_TEXT_H

#include "CNA/C/input.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Owned registration for one of the text-input events. */
typedef CNA_Handle CNA_TextInputRegistrationHandle;

/** @brief Fixed-width hint describing the kind of text being entered. */
typedef uint32_t CNA_TextInputType;

/** @brief The input is plain text. */
#define CNA_TEXT_INPUT_TYPE_TEXT UINT32_C(0)
/** @brief The input is a person's name. */
#define CNA_TEXT_INPUT_TYPE_TEXT_NAME UINT32_C(1)
/** @brief The input is an e-mail address. */
#define CNA_TEXT_INPUT_TYPE_TEXT_EMAIL UINT32_C(2)
/** @brief The input is a username. */
#define CNA_TEXT_INPUT_TYPE_TEXT_USERNAME UINT32_C(3)
/** @brief The input is a secure password that should be hidden. */
#define CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_HIDDEN UINT32_C(4)
/** @brief The input is a secure password that should be visible. */
#define CNA_TEXT_INPUT_TYPE_TEXT_PASSWORD_VISIBLE UINT32_C(5)
/** @brief The input is a number. */
#define CNA_TEXT_INPUT_TYPE_NUMBER UINT32_C(6)
/** @brief The input is a secure PIN that should be hidden. */
#define CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_HIDDEN UINT32_C(7)
/** @brief The input is a secure PIN that should be visible. */
#define CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_VISIBLE UINT32_C(8)
/** @brief Highest defined text-input type identity. */
#define CNA_TEXT_INPUT_TYPE_MAXIMUM CNA_TEXT_INPUT_TYPE_NUMBER_PASSWORD_VISIBLE

/**
 * @brief Describes one composition update reported while an IME is active.
 */
typedef struct CNA_TextEditingEventInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this structure. */
    uint32_t struct_version;

    /**
     * @brief The draft composition as UTF-8 bytes, borrowed for the duration of the callback.
     *
     * The view is not NUL-terminated and must be copied before the callback returns.
     */
    CNA_StringView text;

    /** @brief Byte offset of the active editing region inside @ref text. */
    int32_t start;

    /** @brief Byte length of the active editing region inside @ref text. */
    int32_t length;
} CNA_TextEditingEventInfo;

/**
 * @brief Describes the IME candidate list reported during composition.
 */
typedef struct CNA_TextEditingCandidatesEventInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this structure. */
    uint32_t struct_version;

    /**
     * @brief The candidate strings as UTF-8 views, borrowed for the duration of the callback.
     *
     * The array and every view in it must be copied before the callback returns. The pointer is
     * null only when @ref candidate_count is zero.
     */
    const CNA_StringView* candidates;

    /** @brief Number of entries in @ref candidates. */
    int32_t candidate_count;

    /** @brief Index of the pre-selected candidate, or -1 when none is selected. */
    int32_t selected;

    /** @brief `CNA_TRUE` when the candidate list is laid out horizontally. */
    CNA_Bool horizontal;

    /** @brief Reserved padding; always zero. */
    uint8_t reserved[3];
} CNA_TextEditingCandidatesEventInfo;

/**
 * @brief Receives one committed UTF-16 code unit.
 *
 * @param code_unit The UTF-16 code unit that was entered.
 * @param context The caller's own context, copied when the handler was registered.
 *
 * The callback runs synchronously on the thread that raised the event. A code point above U+FFFF
 * arrives as two calls — a high surrogate then a low surrogate — exactly as the canonical event
 * delivers it. The callback must not re-enter the C API's text-input routes.
 */
typedef void (*CNA_TextInputCallback)(uint16_t code_unit, void* context);

/**
 * @brief Receives one IME composition update.
 *
 * @param info The event description, valid only for the duration of this call.
 * @param context The caller's own context, copied when the handler was registered.
 */
typedef void (*CNA_TextEditingCallback)(
    const CNA_TextEditingEventInfo* info,
    void* context);

/**
 * @brief Receives the current IME candidate list.
 *
 * @param info The event description, valid only for the duration of this call.
 * @param context The caller's own context, copied when the handler was registered.
 */
typedef void (*CNA_TextEditingCandidatesCallback)(
    const CNA_TextEditingCandidatesEventInfo* info,
    void* context);

/**
 * @brief Subscribes to the committed-text event.
 *
 * @param callback Handler invoked for each committed UTF-16 code unit.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 *
 * The canonical event is **static**, so the subscription belongs to the process rather than to a
 * game and takes no game handle. Release it with `cna_text_input_unsubscribe_ext`.
 */
CNA_C_API CNA_Result cna_text_input_subscribe_text_input_ext(
    CNA_TextInputCallback callback,
    void* context,
    CNA_TextInputRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the IME composition event.
 *
 * @param callback Handler invoked for each composition update.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_text_input_subscribe_text_editing_ext(
    CNA_TextEditingCallback callback,
    void* context,
    CNA_TextInputRegistrationHandle* out_registration);

/**
 * @brief Subscribes to the IME candidate-list event.
 *
 * @param callback Handler invoked for each candidate-list update.
 * @param context Caller context passed back to the handler; may be null.
 * @param out_registration Receives an owned registration handle.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null callback or output.
 */
CNA_C_API CNA_Result cna_text_input_subscribe_text_editing_candidates_ext(
    CNA_TextEditingCandidatesCallback callback,
    void* context,
    CNA_TextInputRegistrationHandle* out_registration);

/**
 * @brief Releases a text-input event registration.
 *
 * @param registration Owned registration handle from any of the three subscribe routes.
 * @return `CNA_RESULT_SUCCESS` or a documented handle failure.
 *
 * One route releases all three event families, because a registration already knows which event it
 * came from. Releasing it after `cna_text_input_reset_for_tests_ext` cleared the process event is a
 * no-op rather than a failure.
 */
CNA_C_API CNA_Result cna_text_input_unsubscribe_ext(
    CNA_TextInputRegistrationHandle registration);

/**
 * @brief Raises the committed-text event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param code_unit The UTF-16 code unit to report.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical internal dispatch, which exists so a platform layer — or a test — can
 * deliver text without a real keyboard. Every subscribed handler runs synchronously before the call
 * returns.
 */
CNA_C_API CNA_Result cna_text_input_raise_text_input_ext(
    CNA_Handle game,
    uint16_t code_unit);

/**
 * @brief Raises the IME composition event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param text Borrowed UTF-8 draft composition, copied before dispatch.
 * @param start Byte offset of the active editing region.
 * @param length Byte length of the active editing region.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_ENCODING` when @p text is not valid UTF-8; or a
 *         documented argument/handle/thread/native failure.
 *
 * `start` and `length` are passed straight through, exactly as the canonical dispatch does. They
 * are byte offsets into a multi-byte string, not character counts, and are deliberately not
 * validated against the text length — the canonical implementation forwards SDL's IME values
 * unchanged and a C consumer must index the text carefully.
 */
CNA_C_API CNA_Result cna_text_input_raise_text_editing_ext(
    CNA_Handle game,
    CNA_StringView text,
    int32_t start,
    int32_t length);

/**
 * @brief Raises the IME candidate-list event.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param candidates Borrowed array of UTF-8 candidate strings, copied before dispatch; may be null
 *        only when @p candidate_count is zero.
 * @param candidate_count Number of entries in @p candidates.
 * @param selected Index of the pre-selected candidate, or -1 when none is selected.
 * @param horizontal Nonzero when the candidate list is laid out horizontally.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_ENCODING` when a candidate is not valid UTF-8; or a
 *         documented argument/handle/thread/native failure.
 *
 * A negative count is refused. `selected` is passed straight through and is not range-checked
 * against the candidate count, because the canonical dispatch does not check it either.
 */
CNA_C_API CNA_Result cna_text_input_raise_text_editing_candidates_ext(
    CNA_Handle game,
    const CNA_StringView* candidates,
    int32_t candidate_count,
    int32_t selected,
    CNA_Bool horizontal);

/**
 * @brief Reads the native window handle the text-input APIs are bound to.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_window Receives the opaque native window value, which is zero when none is bound.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The value is an opaque native pointer-sized identifier, not a `CNA_Handle`, and the C API never
 * dereferences it.
 */
CNA_C_API CNA_Result cna_text_input_get_window_handle_ext(
    CNA_Handle game,
    uint64_t* out_window);

/**
 * @brief Binds the text-input APIs to a native window handle.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param window Opaque native window value; zero unbinds.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * Only zero is guarded. A nonzero value is handed to the platform backend unvalidated, exactly as
 * the canonical property does, so it must be a live native window supplied by the platform layer.
 * With no window bound, every activation and placement route below is a successful no-op and every
 * query answers false.
 */
CNA_C_API CNA_Result cna_text_input_set_window_handle_ext(
    CNA_Handle game,
    uint64_t window);

/**
 * @brief Reports whether text input mode is currently active.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_active Receives `CNA_TRUE` when text input is active.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * On some platforms this stays true after an external event closed the on-screen keyboard; check
 * `cna_text_input_is_screen_keyboard_shown_ext` instead in that case.
 */
CNA_C_API CNA_Result cna_text_input_is_active_ext(
    CNA_Handle game,
    CNA_Bool* out_active);

/**
 * @brief Reports whether the on-screen keyboard is visible for the bound window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_shown Receives `CNA_TRUE` when the keyboard is shown.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_text_input_is_screen_keyboard_shown_ext(
    CNA_Handle game,
    CNA_Bool* out_shown);

/**
 * @brief Reports whether the on-screen keyboard is visible for a specific native window.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param window Opaque native window value to query; zero always answers `CNA_FALSE`.
 * @param out_shown Receives `CNA_TRUE` when the keyboard is shown for that window.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical explicit-window overload, which ignores the bound window handle.
 */
CNA_C_API CNA_Result cna_text_input_is_screen_keyboard_shown_for_window_ext(
    CNA_Handle game,
    uint64_t window,
    CNA_Bool* out_shown);

/**
 * @brief Activates text input mode and shows the on-screen keyboard where applicable.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * With no window bound this succeeds and changes nothing, matching the canonical null guard.
 */
CNA_C_API CNA_Result cna_text_input_start_ext(CNA_Handle game);

/**
 * @brief Deactivates text input mode.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_text_input_stop_ext(CNA_Handle game);

/**
 * @brief Activates text input mode with a hint for the on-screen keyboard or IME.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param type One `CNA_TEXT_INPUT_TYPE_*` identity.
 * @return `CNA_RESULT_SUCCESS`; `CNA_RESULT_INVALID_ARGUMENT` for an undefined identity; or a
 *         documented handle/thread/native failure.
 *
 * The canonical conversion falls back to plain text for an out-of-range value; C refuses instead,
 * so a consumer is told about a bad identity rather than silently getting a different keyboard.
 * With no window bound this behaves like `cna_text_input_start_ext` and changes nothing.
 */
CNA_C_API CNA_Result cna_text_input_start_with_type_ext(
    CNA_Handle game,
    CNA_TextInputType type);

/**
 * @brief Hints where text is being entered, for IME popup placement.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param rectangle Text-input location relative to the game window's client bounds.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The canonical operation passes a zero text-cursor offset to the backend and reports nothing back,
 * so a backend that ignores the hint — and an unbound window — succeed and change nothing.
 */
CNA_C_API CNA_Result cna_text_input_set_input_rectangle_ext(
    CNA_Handle game,
    CNA_Rectangle rectangle);

/**
 * @brief Resets process-wide text-input state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * This maps the canonical test-support reset: it clears the bound window handle and drops every
 * subscription to all three events, including ones this API handed out. A registration released
 * afterwards is a no-op.
 */
CNA_C_API CNA_Result cna_text_input_reset_for_tests_ext(CNA_Handle game);

#ifdef __cplusplus
}
#endif

#endif // CNA_C_INPUT_TEXT_H
