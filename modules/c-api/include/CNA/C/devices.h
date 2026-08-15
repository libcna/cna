// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_DEVICES_H
#define CNA_C_DEVICES_H

#include "CNA/C/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reports whether this build contains the extended device layer.
 *
 * @param out_available Receives `CNA_TRUE` when the `_ext` routes below answer for real.
 * @return `CNA_RESULT_SUCCESS`, or `CNA_RESULT_INVALID_ARGUMENT` for a null output.
 *
 * Every route in this header is exported in both build states, so the ABI's symbol set never depends
 * on a build option; the ones the extension layer implements report `CNA_RESULT_NOT_SUPPORTED` when
 * it is compiled out. Ask this first rather than reading a refusal as a device that is missing. The
 * vibration routes are not part of that layer and answer in every build.
 */
CNA_C_API CNA_Result cna_devices_ext_is_available(CNA_Bool* out_available);

/* ---- Vibration ---- */

/**
 * @brief Records what this ABI's own vibration backend was asked to do.
 *
 * No verification machine has a rumble motor, so a C consumer cannot otherwise tell a working
 * vibration call from one that silently reached nothing. Installing the test backend and reading
 * this description back is how a caller — and this suite — proves the request arrived, with the
 * arguments it was given.
 */
typedef struct CNA_VibrationTestLog {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Number of single-motor start requests received. */
    uint32_t start_calls;

    /** @brief Number of stop requests received. */
    uint32_t stop_calls;

    /** @brief Number of two-motor start requests received. */
    uint32_t left_right_calls;

    /** @brief Padding; always zero. */
    uint32_t reserved;

    /** @brief Duration of the most recent start request, in 100-nanosecond ticks. */
    int64_t last_duration_ticks;

    /** @brief Intensity of the most recent single-motor start request. */
    float last_intensity;

    /** @brief Large-motor strength of the most recent two-motor start request. */
    float last_large_motor;

    /** @brief Small-motor strength of the most recent two-motor start request. */
    float last_small_motor;

    /** @brief Padding; always zero. */
    float reserved_float;
} CNA_VibrationTestLog;

/**
 * @brief Starts vibration for a duration.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param duration_ticks Duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a duration outside the closed
 *         interval of zero to five seconds, or a documented handle/thread/native failure.
 *
 * The canonical controller is a process-wide singleton, so every route here addresses it and none
 * takes a controller handle. The duration bounds are the canonical ones and both ends are inclusive.
 * A machine with no vibration motor still succeeds: the request is made and reaches nothing.
 */
CNA_C_API CNA_Result cna_vibrate_controller_start(CNA_Handle game, int64_t duration_ticks);

/**
 * @brief Starts vibration for a duration at a given intensity.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param duration_ticks Duration in 100-nanosecond ticks.
 * @param intensity Strength in the closed interval zero to one.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a duration outside the canonical
 *         bounds, or a documented handle/thread/native failure.
 *
 * The duration is bounded but the intensity is **clamped**, which is the canonical asymmetry: a
 * strength outside the interval is silently corrected rather than refused, and a not-a-number
 * strength becomes zero — no vibration — rather than reaching the platform as an undefined value.
 */
CNA_C_API CNA_Result cna_vibrate_controller_start_with_intensity_ext(
    CNA_Handle game,
    int64_t duration_ticks,
    float intensity);

/**
 * @brief Starts a two-motor vibration for a duration.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param large_motor Large-motor strength, clamped as the single-motor intensity is.
 * @param small_motor Small-motor strength, clamped as the single-motor intensity is.
 * @param duration_ticks Duration in 100-nanosecond ticks.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a duration outside the canonical
 *         bounds, or a documented handle/thread/native failure.
 *
 * The platform backend stops one kind of vibration before starting the other, so the two never run
 * at once; a backend installed for testing answers for itself.
 */
CNA_C_API CNA_Result cna_vibrate_controller_start_left_right_ext(
    CNA_Handle game,
    float large_motor,
    float small_motor,
    int64_t duration_ticks);

/**
 * @brief Stops any vibration in progress.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_vibrate_controller_stop(CNA_Handle game);

/**
 * @brief Reports whether this device can vibrate.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when a vibration device is present.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The probe does not hold the device open as a side effect, which is the canonical contract.
 */
CNA_C_API CNA_Result cna_vibrate_controller_get_is_supported_ext(
    CNA_Handle game,
    CNA_Bool* out_supported);

/**
 * @brief Returns the byte count of the vibration device's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * A machine with no vibration device answers zero bytes, which is an ordinary answer.
 */
CNA_C_API CNA_Result cna_vibrate_controller_get_device_name_size_ext(
    CNA_Handle game,
    uint64_t* out_bytes);

/**
 * @brief Copies the vibration device's name.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or a
 *         documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_vibrate_controller_copy_device_name_ext(
    CNA_Handle game,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Installs or removes this ABI's own vibration backend for testing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param installed `CNA_TRUE` to install the test backend, `CNA_FALSE` to restore the platform one.
 * @param supported Whether the installed backend claims a vibration device; ignored when
 *        @p installed is `CNA_FALSE`.
 * @param device_name Name the installed backend reports; ignored when @p installed is `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 *
 * The canonical hook takes a caller-implemented backend object, which C cannot write, so this ABI
 * supplies the backend and exposes the switch. Installing resets the log.
 */
CNA_C_API CNA_Result cna_vibrate_controller_set_test_backend_ext(
    CNA_Handle game,
    CNA_Bool installed,
    CNA_Bool supported,
    CNA_StringView device_name);

/**
 * @brief Reads what the installed test backend has been asked to do.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_log Receives the counts and the most recent arguments.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed, or a
 *         documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_vibrate_controller_get_test_log_ext(
    CNA_Handle game,
    CNA_VibrationTestLog* out_log);

/* ---- Host power ---- */

/** @brief Fixed-width identity of the host's power state. */
typedef uint32_t CNA_PowerState;

/** @brief The power state could not be determined because the query failed. */
#define CNA_POWER_STATE_ERROR UINT32_C(0)
/** @brief The power state is not known on this platform. */
#define CNA_POWER_STATE_UNKNOWN UINT32_C(1)
/** @brief Running on battery and discharging. */
#define CNA_POWER_STATE_ON_BATTERY UINT32_C(2)
/** @brief Plugged in with no battery present. */
#define CNA_POWER_STATE_NO_BATTERY UINT32_C(3)
/** @brief Plugged in and charging. */
#define CNA_POWER_STATE_CHARGING UINT32_C(4)
/** @brief Plugged in and fully charged. */
#define CNA_POWER_STATE_CHARGED UINT32_C(5)
/** @brief Highest defined power-state identity. */
#define CNA_POWER_STATE_MAXIMUM CNA_POWER_STATE_CHARGED

/**
 * @brief Returns the host's power state.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_state Receives one `CNA_POWER_STATE_*` identity.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * This is the host machine's power, distinct from a controller's `cna_gamepad_get_battery_level_ext`.
 */
CNA_C_API CNA_Result cna_power_get_state_ext(CNA_Handle game, CNA_PowerState* out_state);

/**
 * @brief Returns the remaining battery charge as a percentage.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_percent Receives the percentage, or **-1** when it is unknown.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * The canonical sentinel is preserved rather than replaced by a separate availability flag: -1 means
 * unknown, and a machine with no battery reports it.
 */
CNA_C_API CNA_Result cna_power_get_battery_percent_ext(CNA_Handle game, int32_t* out_percent);

/**
 * @brief Returns the remaining battery time in seconds.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_seconds Receives the seconds remaining, or **-1** when it is unknown.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_power_get_seconds_remaining_ext(CNA_Handle game, int32_t* out_seconds);

/* ---- Host machine ---- */

/**
 * @brief Returns the number of logical CPU cores.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the core count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_system_info_get_logical_cpu_core_count_ext(
    CNA_Handle game,
    int32_t* out_count);

/**
 * @brief Returns the amount of system memory in megabytes.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_megabytes Receives the memory size.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_system_info_get_system_ram_megabytes_ext(
    CNA_Handle game,
    int32_t* out_megabytes);

/* ---- Preferred locales ---- */

/**
 * @brief Returns how many locales the host prefers, most preferred first.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_count Receives the locale count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * The canonical value is a pair of strings and nothing else, so it has no structure in C: the two
 * fields are read with the count/copy pairs below. A host that reports no locales answers zero,
 * which is an ordinary answer.
 */
CNA_C_API CNA_Result cna_locale_get_preferred_count_ext(CNA_Handle game, uint64_t* out_count);

/**
 * @brief Returns the byte count of one preferred locale's language code.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based locale index below the reported count.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an index at or past the count,
 *         `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_locale_get_language_size_at_ext(
    CNA_Handle game,
    uint64_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one preferred locale's language code.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based locale index below the reported count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT` for an index at or past the count,
 *         `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_locale_copy_language_at_ext(
    CNA_Handle game,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/**
 * @brief Returns the byte count of one preferred locale's country code.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based locale index below the reported count.
 * @param out_bytes Receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT`, `CNA_RESULT_NOT_SUPPORTED`, or a
 *         documented handle/thread failure.
 *
 * A locale with no country part answers zero bytes rather than failing.
 */
CNA_C_API CNA_Result cna_locale_get_country_size_at_ext(
    CNA_Handle game,
    uint64_t index,
    uint64_t* out_bytes);

/**
 * @brief Copies one preferred locale's country code.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param index Zero-based locale index below the reported count.
 * @param destination Buffer receiving the UTF-8 bytes; may be null only when @p capacity is zero.
 * @param capacity Bytes available in @p destination.
 * @param out_bytes Always receives the required byte count, without a terminator.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**,
 *         `CNA_RESULT_INVALID_ARGUMENT` for an index at or past the count,
 *         `CNA_RESULT_NOT_SUPPORTED`, or a documented failure.
 */
CNA_C_API CNA_Result cna_locale_copy_country_at_ext(
    CNA_Handle game,
    uint64_t index,
    char* destination,
    uint64_t capacity,
    uint64_t* out_bytes);

/* ---- Display ---- */

/**
 * @brief Returns the game window's display content scale.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_scale Receives the scale factor, or zero when there is no native window.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * The canonical query takes a window; this ABI has no window handle of its own, and the game owns
 * exactly one window, so the game handle addresses it. A headless or windowless session answers zero,
 * which is the canonical answer rather than a failure.
 */
CNA_C_API CNA_Result cna_display_info_get_content_scale_ext(CNA_Handle game, float* out_scale);

/**
 * @brief Returns the game window's safe area.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_area Receives the safe area, or an empty rectangle when there is no native window or
 *        the platform has no safe-area concept.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_display_info_get_safe_area_ext(CNA_Handle game, CNA_Rectangle* out_area);

/* ---- Clipboard acceptance ---- */

/**
 * @brief Places text on the system clipboard and reports whether the platform accepted it.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param text UTF-8 text to place on the clipboard; borrowed for the duration of the call.
 * @param out_accepted Receives `CNA_TRUE` when the platform accepted the text.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * This is the **same clipboard** `cna_clipboard_set_text` writes and `cna_clipboard_get_text_size`
 * reads — both canonical types wrap one platform clipboard, so this ABI does not duplicate the
 * reads. The one difference the extension layer adds is this acceptance flag, which the other route
 * discards. Acceptance still means the platform took the request, not that a later read returns it.
 */
CNA_C_API CNA_Result cna_devices_clipboard_set_text_ext(
    CNA_Handle game,
    CNA_StringView text,
    CNA_Bool* out_accepted);

/* ---- URL launcher ---- */

/**
 * @brief Opens a URL in the host's default handler.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param url UTF-8 URL; must not be empty.
 * @param out_opened Receives `CNA_TRUE` when the platform accepted the request.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an empty URL,
 *         `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a documented
 *         handle/thread failure.
 *
 * Acceptance means the platform took the request; it says nothing about what the handler then did.
 * This route hands control to another application, so nothing in this ABI's own test suite calls it
 * with a real URL — only its refusals are exercised.
 */
CNA_C_API CNA_Result cna_url_launcher_open_ext(
    CNA_Handle game,
    CNA_StringView url,
    CNA_Bool* out_opened);

/* ---- Message box ---- */

/** @brief Fixed-width identity of a message box's severity. */
typedef uint32_t CNA_MessageBoxType;

/** @brief An error message. */
#define CNA_MESSAGE_BOX_TYPE_ERROR UINT32_C(0)
/** @brief A warning message. */
#define CNA_MESSAGE_BOX_TYPE_WARNING UINT32_C(1)
/** @brief An informational message. */
#define CNA_MESSAGE_BOX_TYPE_INFORMATION UINT32_C(2)
/** @brief Highest defined message-box severity identity. */
#define CNA_MESSAGE_BOX_TYPE_MAXIMUM CNA_MESSAGE_BOX_TYPE_INFORMATION

/**
 * @brief Records what this ABI's own message-box backend was asked to show.
 *
 * A real message box is modal and waits for a person, so no automated test can complete one. The
 * test backend answers immediately and records the request instead.
 */
typedef struct CNA_MessageBoxTestLog {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Number of dismiss-only message boxes requested. */
    uint32_t simple_calls;

    /** @brief Number of button-answering message boxes requested. */
    uint32_t choice_calls;

    /** @brief Severity of the most recent request. */
    CNA_MessageBoxType last_type;

    /** @brief Number of button labels in the most recent button-answering request. */
    uint32_t last_button_count;
} CNA_MessageBoxTestLog;

/**
 * @brief Reports whether this platform can show a message box.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when message boxes are available.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_message_box_get_is_supported_ext(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Shows a message box the reader can only dismiss.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param type One `CNA_MESSAGE_BOX_TYPE_*` identity.
 * @param title Dialog title; borrowed for the duration of the call.
 * @param message Dialog body; borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined severity,
 *         `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a documented
 *         handle/thread/native failure.
 *
 * **This blocks until a person dismisses the dialog** unless a test backend is installed.
 */
CNA_C_API CNA_Result cna_message_box_show_simple_ext(
    CNA_Handle game,
    CNA_MessageBoxType type,
    CNA_StringView title,
    CNA_StringView message);

/**
 * @brief Shows a message box with buttons and reports which one was chosen.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param type One `CNA_MESSAGE_BOX_TYPE_*` identity.
 * @param title Dialog title; borrowed for the duration of the call.
 * @param message Dialog body; borrowed for the duration of the call.
 * @param button_labels Array of @p button_count labels; borrowed for the duration of the call.
 * @param button_count Number of labels, which must not be zero.
 * @param out_chosen Receives the zero-based index of the chosen button, or -1 when the dialog was
 *        closed without choosing.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for an undefined severity or an empty
 *         label array, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or
 *         a documented handle/thread/native failure.
 *
 * **This blocks until a person answers** unless a test backend is installed.
 */
CNA_C_API CNA_Result cna_message_box_show_ext(
    CNA_Handle game,
    CNA_MessageBoxType type,
    CNA_StringView title,
    CNA_StringView message,
    const CNA_StringView* button_labels,
    uint64_t button_count,
    int32_t* out_chosen);

/**
 * @brief Installs or removes this ABI's own message-box backend for testing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param installed `CNA_TRUE` to install the test backend, `CNA_FALSE` to restore the platform one.
 * @param chosen_button Index the installed backend answers for a button-answering request; ignored
 *        when @p installed is `CNA_FALSE`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented handle/thread failure.
 *
 * The canonical backend is **process-wide**, so this switch is too: it is not scoped to the game
 * handle it validates. Installing resets the log.
 */
CNA_C_API CNA_Result cna_message_box_set_test_backend_ext(
    CNA_Handle game,
    CNA_Bool installed,
    int32_t chosen_button);

/**
 * @brief Reads what the installed message-box test backend was asked to show.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_log Receives the counts and the most recent request's shape.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when no test backend is installed,
 *         `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a documented
 *         argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_message_box_get_test_log_ext(
    CNA_Handle game,
    CNA_MessageBoxTestLog* out_log);

/* ---- File dialogs ---- */

/**
 * @brief One file-type filter offered by a file dialog.
 *
 * Both members are borrowed for the duration of the call that receives them, which is why they are
 * views rather than owned strings: a filter list is written by the caller and read immediately.
 */
typedef struct CNA_FileDialogFilter {
    /** @brief Size of this caller-provided structure in bytes. */
    uint32_t struct_size;

    /** @brief Version of this caller-provided structure. */
    uint32_t struct_version;

    /** @brief Human-readable filter name. */
    CNA_StringView name;

    /** @brief Platform filter pattern. */
    CNA_StringView pattern;
} CNA_FileDialogFilter;

/**
 * @brief Handler invoked with a file dialog's result.
 *
 * @param files Array of @p count chosen paths, borrowed for the duration of the call; empty when the
 *        dialog was cancelled.
 * @param count Number of paths.
 * @param context The caller context supplied when the dialog was shown.
 *
 * The canonical dialog reports cancellation as an empty result rather than a separate signal, and
 * this ABI reports it the same way.
 */
typedef void (*CNA_FileDialogResultCallback)(
    const CNA_StringView* files,
    uint64_t count,
    void* context);

/**
 * @brief Reports whether this platform can show file dialogs.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when file dialogs are available.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_file_dialog_get_is_supported_ext(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Shows a dialog for choosing files to open.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param on_result Handler invoked with the chosen paths.
 * @param context Caller context passed back to @p on_result.
 * @param filters Array of @p filter_count filters, or null when @p filter_count is zero.
 * @param filter_count Number of filters.
 * @param default_location Starting directory, or an empty view for the platform default.
 * @param allow_multiple Whether more than one file may be chosen.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null handler or an invalid
 *         filter, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a
 *         documented handle/thread/native failure.
 *
 * The canonical dialog is **asynchronous**: this route returns once the request has been made, and
 * the handler runs whenever the platform answers — which may be long afterwards, or never if the
 * process exits first. A test backend answers immediately instead.
 */
CNA_C_API CNA_Result cna_file_dialog_show_open_file_ext(
    CNA_Handle game,
    CNA_FileDialogResultCallback on_result,
    void* context,
    const CNA_FileDialogFilter* filters,
    uint64_t filter_count,
    CNA_StringView default_location,
    CNA_Bool allow_multiple);

/**
 * @brief Shows a dialog for choosing where to save a file.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param on_result Handler invoked with the chosen path.
 * @param context Caller context passed back to @p on_result.
 * @param filters Array of @p filter_count filters, or null when @p filter_count is zero.
 * @param filter_count Number of filters.
 * @param default_location Starting directory, or an empty view for the platform default.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null handler or an invalid
 *         filter, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a
 *         documented handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_file_dialog_show_save_file_ext(
    CNA_Handle game,
    CNA_FileDialogResultCallback on_result,
    void* context,
    const CNA_FileDialogFilter* filters,
    uint64_t filter_count,
    CNA_StringView default_location);

/**
 * @brief Shows a dialog for choosing folders.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param on_result Handler invoked with the chosen paths.
 * @param context Caller context passed back to @p on_result.
 * @param default_location Starting directory, or an empty view for the platform default.
 * @param allow_multiple Whether more than one folder may be chosen.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_ARGUMENT` for a null handler,
 *         `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension layer, or a documented
 *         handle/thread/native failure.
 *
 * Folder dialogs take no filters, which is the canonical shape rather than an omission here.
 */
CNA_C_API CNA_Result cna_file_dialog_show_open_folder_ext(
    CNA_Handle game,
    CNA_FileDialogResultCallback on_result,
    void* context,
    CNA_StringView default_location,
    CNA_Bool allow_multiple);

/**
 * @brief Installs or removes this ABI's own file-dialog backend for testing.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param installed `CNA_TRUE` to install the test backend, `CNA_FALSE` to restore the platform one.
 * @param results Array of @p result_count paths the installed backend answers with; null answers an
 *        empty result, which is how the canonical dialog reports cancellation.
 * @param result_count Number of paths.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * The canonical backend is **process-wide**, so this switch is too. The paths are copied, so the
 * caller's array need not outlive this call.
 */
CNA_C_API CNA_Result cna_file_dialog_set_test_backend_ext(
    CNA_Handle game,
    CNA_Bool installed,
    const CNA_StringView* results,
    uint64_t result_count);

/* ---- System tray ---- */

/** @brief Owned handle to one system-tray icon. */
typedef CNA_Handle CNA_SystemTrayHandle;

/**
 * @brief Handler invoked when a tray entry is activated.
 *
 * @param context The caller context supplied when the entry was added.
 */
typedef void (*CNA_TrayEntryClickCallback)(void* context);

/**
 * @brief Reports whether this platform has a system tray.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param out_supported Receives `CNA_TRUE` when a tray is available.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_system_tray_get_is_supported_ext(CNA_Handle game, CNA_Bool* out_supported);

/**
 * @brief Creates a tray icon on the platform tray.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param tooltip Tooltip text; borrowed for the duration of the call.
 * @param out_tray Receives an owned tray handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread/native failure.
 *
 * This puts a real icon in the host's tray. Releasing the handle removes it.
 */
CNA_C_API CNA_Result cna_system_tray_create(
    CNA_Handle game,
    CNA_StringView tooltip,
    CNA_SystemTrayHandle* out_tray);

/**
 * @brief Creates a tray icon backed by this ABI's own test backend.
 *
 * @param game Active owned or callback-borrowed game handle.
 * @param tooltip Tooltip text; borrowed for the duration of the call.
 * @param out_tray Receives an owned tray handle.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_NOT_SUPPORTED` when this build has no device extension
 *         layer, or a documented argument/handle/thread failure.
 *
 * The canonical class takes the backend as a constructor argument rather than through a switch, so
 * this ABI mirrors that with a second creation route instead of a mode flag. Nothing reaches the
 * host's tray, and `cna_system_tray_click_entry_for_tests_ext` can then activate an entry.
 */
CNA_C_API CNA_Result cna_system_tray_create_with_test_backend_ext(
    CNA_Handle game,
    CNA_StringView tooltip,
    CNA_SystemTrayHandle* out_tray);

/**
 * @brief Sets the tray icon's tooltip.
 *
 * @param tray Owned tray handle.
 * @param tooltip Tooltip text; borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_system_tray_set_tooltip(CNA_SystemTrayHandle tray, CNA_StringView tooltip);

/**
 * @brief Adds an entry to the tray icon's menu.
 *
 * @param tray Owned tray handle.
 * @param label Entry label; borrowed for the duration of the call.
 * @param checkable Whether the entry carries a check mark.
 * @param initially_checked Whether a checkable entry starts checked.
 * @param initially_enabled Whether the entry starts enabled.
 * @param on_click Handler invoked when the entry is activated, or null for none.
 * @param context Caller context passed back to @p on_click.
 * @param out_index Receives the entry's zero-based index.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * The handler runs on whatever thread the platform delivers the activation on.
 */
CNA_C_API CNA_Result cna_system_tray_add_entry(
    CNA_SystemTrayHandle tray,
    CNA_StringView label,
    CNA_Bool checkable,
    CNA_Bool initially_checked,
    CNA_Bool initially_enabled,
    CNA_TrayEntryClickCallback on_click,
    void* context,
    uint64_t* out_index);

/**
 * @brief Changes one entry's label.
 *
 * @param tray Owned tray handle.
 * @param index Zero-based entry index.
 * @param label New label; borrowed for the duration of the call.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 *
 * An index past the last entry is **ignored**, which is the canonical backend's own behavior rather
 * than a refusal invented here.
 */
CNA_C_API CNA_Result cna_system_tray_set_entry_label(
    CNA_SystemTrayHandle tray,
    uint64_t index,
    CNA_StringView label);

/**
 * @brief Changes one entry's check mark.
 *
 * @param tray Owned tray handle.
 * @param index Zero-based entry index.
 * @param checked Whether the entry is checked.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_system_tray_set_entry_checked(
    CNA_SystemTrayHandle tray,
    uint64_t index,
    CNA_Bool checked);

/**
 * @brief Reports whether one entry is checked.
 *
 * @param tray Owned tray handle.
 * @param index Zero-based entry index.
 * @param out_checked Receives the check state; `CNA_FALSE` for an index past the last entry.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_system_tray_get_entry_checked(
    CNA_SystemTrayHandle tray,
    uint64_t index,
    CNA_Bool* out_checked);

/**
 * @brief Enables or disables one entry.
 *
 * @param tray Owned tray handle.
 * @param index Zero-based entry index.
 * @param enabled Whether the entry is enabled.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_system_tray_set_entry_enabled(
    CNA_SystemTrayHandle tray,
    uint64_t index,
    CNA_Bool enabled);

/**
 * @brief Reports whether one entry is enabled.
 *
 * @param tray Owned tray handle.
 * @param index Zero-based entry index.
 * @param out_enabled Receives the enabled state; `CNA_FALSE` for an index past the last entry.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/native failure.
 */
CNA_C_API CNA_Result cna_system_tray_get_entry_enabled(
    CNA_SystemTrayHandle tray,
    uint64_t index,
    CNA_Bool* out_enabled);

/**
 * @brief Activates one entry through the test backend.
 *
 * @param tray Owned tray handle created with the test backend.
 * @param index Zero-based entry index.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_INVALID_STATE` when the tray does not use the test
 *         backend, `CNA_RESULT_INVALID_ARGUMENT` for an index past the last entry, or a documented
 *         handle/thread failure.
 */
CNA_C_API CNA_Result cna_system_tray_click_entry_for_tests_ext(
    CNA_SystemTrayHandle tray,
    uint64_t index);

/**
 * @brief Releases a tray handle and removes its icon.
 *
 * @param tray Owned tray handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_system_tray_destroy(CNA_SystemTrayHandle tray);

#ifdef __cplusplus
}
#endif

#endif
