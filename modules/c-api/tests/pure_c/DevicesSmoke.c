// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <math.h>
#include <string.h>

typedef struct DevicesSmokeState {
    int validated;
} DevicesSmokeState;

typedef struct DialogState {
    int calls;
    uint64_t last_count;
    char first[256];
} DialogState;

typedef struct ClickState {
    int calls;
} ClickState;

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static void on_dialog_result(
    const CNA_StringView* const files,
    const uint64_t count,
    void* const context)
{
    DialogState* const state = (DialogState*)context;
    ++state->calls;
    state->last_count = count;
    state->first[0] = '\0';
    if (count != UINT64_C(0) && files != 0 && files[0].byte_length < sizeof(state->first)) {
        memcpy(state->first, files[0].data, (size_t)files[0].byte_length);
        state->first[files[0].byte_length] = '\0';
    }
}

static void on_entry_click(void* const context)
{
    ++((ClickState*)context)->calls;
}

/* No verification machine has a rumble motor, so the requests are proved by installing this ABI's
   own backend and reading back what it was asked to do. */
static int validate_vibration(const CNA_Handle game)
{
    CNA_VibrationTestLog log;
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[128];

    /* The real controller answers before any test backend exists. */
    if (cna_vibrate_controller_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_vibrate_controller_get_is_supported_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vibrate_controller_get_device_name_size_ext(game, &bytes) != CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_INVALID_STATE) {
        return 0;
    }
    if (cna_vibrate_controller_set_test_backend_ext(
            game, CNA_TRUE, CNA_TRUE, view("CNA test rumble")) != CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_vibrate_controller_get_device_name_size_ext(game, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(15) ||
        cna_vibrate_controller_copy_device_name_ext(game, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "CNA test rumble") != 0 ||
        cna_vibrate_controller_copy_device_name_ext(game, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    /* A one-second request arrives with the full intensity the single-argument route implies. */
    if (cna_vibrate_controller_start(game, INT64_C(10000000)) != CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.struct_size != (uint32_t)sizeof(log) || log.struct_version != UINT32_C(1) ||
        log.start_calls != UINT32_C(1) || log.last_duration_ticks != INT64_C(10000000) ||
        log.last_intensity != 1.0F) {
        return 0;
    }
    /* The duration is bounded and the intensity is clamped: the canonical asymmetry. Six seconds is
       refused, a strength above one is silently corrected, and a not-a-number strength becomes no
       vibration at all rather than reaching the platform undefined. */
    if (cna_vibrate_controller_start(game, INT64_C(60000000)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vibrate_controller_start(game, INT64_C(-1)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_vibrate_controller_start_with_intensity_ext(game, INT64_C(20000000), 4.0F) !=
            CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.start_calls != UINT32_C(2) || log.last_intensity != 1.0F ||
        cna_vibrate_controller_start_with_intensity_ext(game, INT64_C(10000000), NAN) !=
            CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.last_intensity != 0.0F) {
        return 0;
    }
    /* Both motors are carried separately, and the two-motor route bounds its duration too. */
    if (cna_vibrate_controller_start_left_right_ext(game, 0.25F, 0.75F, INT64_C(5000000)) !=
            CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.left_right_calls != UINT32_C(1) || log.last_large_motor != 0.25F ||
        log.last_small_motor != 0.75F || log.last_duration_ticks != INT64_C(5000000) ||
        cna_vibrate_controller_start_left_right_ext(game, 0.5F, 0.5F, INT64_C(60000000)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_vibrate_controller_stop(game) != CNA_RESULT_SUCCESS ||
        cna_vibrate_controller_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.stop_calls != UINT32_C(1) ||
        cna_vibrate_controller_get_test_log_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Removing the backend restores the platform one, and the log is gone with it. */
    return cna_vibrate_controller_set_test_backend_ext(
               game, CNA_FALSE, CNA_FALSE, view("")) == CNA_RESULT_SUCCESS &&
        cna_vibrate_controller_get_test_log_ext(game, &log) == CNA_RESULT_INVALID_STATE;
}

static int validate_host_queries(const CNA_Handle game)
{
    CNA_PowerState power = UINT32_C(99);
    CNA_Rectangle area;
    int32_t value = -99;
    float scale = -1.0F;
    uint64_t count = UINT64_C(99);

    if (cna_power_get_state_ext(game, &power) != CNA_RESULT_SUCCESS ||
        power > CNA_POWER_STATE_MAXIMUM ||
        cna_power_get_state_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical sentinel is preserved: -1 means unknown, and a machine with no battery says so
       rather than answering a made-up number. */
    if (cna_power_get_battery_percent_ext(game, &value) != CNA_RESULT_SUCCESS ||
        value < -1 || value > 100 ||
        cna_power_get_seconds_remaining_ext(game, &value) != CNA_RESULT_SUCCESS || value < -1 ||
        cna_power_get_battery_percent_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_power_get_seconds_remaining_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_system_info_get_logical_cpu_core_count_ext(game, &value) != CNA_RESULT_SUCCESS ||
        value < 1 ||
        cna_system_info_get_system_ram_megabytes_ext(game, &value) != CNA_RESULT_SUCCESS ||
        value < 1 ||
        cna_system_info_get_logical_cpu_core_count_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_system_info_get_system_ram_megabytes_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Whatever this host reports, every locale index below the count answers and the first one past
       it is refused. A host with no locales is an ordinary zero. */
    if (cna_locale_get_preferred_count_ext(game, &count) != CNA_RESULT_SUCCESS ||
        cna_locale_get_preferred_count_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    {
        uint64_t index = UINT64_C(0);
        uint64_t bytes = UINT64_C(9);
        char text[128];
        for (index = UINT64_C(0); index < count; ++index) {
            memset(text, 0, sizeof(text));
            if (cna_locale_get_language_size_at_ext(game, index, &bytes) != CNA_RESULT_SUCCESS ||
                bytes >= (uint64_t)sizeof(text) ||
                cna_locale_copy_language_at_ext(
                    game, index, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
                cna_locale_get_country_size_at_ext(game, index, &bytes) != CNA_RESULT_SUCCESS ||
                bytes >= (uint64_t)sizeof(text) ||
                cna_locale_copy_country_at_ext(
                    game, index, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS) {
                return 0;
            }
        }
        if (cna_locale_get_language_size_at_ext(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_locale_get_country_size_at_ext(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_locale_copy_language_at_ext(
                game, count, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    /* A session with no native window answers zero and an empty rectangle rather than failing. */
    memset(&area, 9, sizeof(area));
    if (cna_display_info_get_content_scale_ext(game, &scale) != CNA_RESULT_SUCCESS || scale < 0.0F ||
        cna_display_info_get_safe_area_ext(game, &area) != CNA_RESULT_SUCCESS ||
        area.width < 0 || area.height < 0 ||
        cna_display_info_get_content_scale_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_display_info_get_safe_area_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return 1;
}

/* The extension layer's clipboard is the same platform clipboard the input module's is, so the only
   thing this route adds is whether the platform accepted the request. */
static int validate_clipboard_and_url(const CNA_Handle game)
{
    CNA_Bool accepted = UINT8_C(9);
    CNA_Bool opened = UINT8_C(9);

    if (cna_devices_clipboard_set_text_ext(game, view("cna devices"), &accepted) !=
            CNA_RESULT_SUCCESS ||
        (accepted != CNA_FALSE && accepted != CNA_TRUE) ||
        cna_devices_clipboard_set_text_ext(game, view("cna devices"), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Opening a URL hands control to another application, so the suite exercises only the refusals:
       succeeding here would launch a browser on whatever machine runs the tests. */
    return cna_url_launcher_open_ext(game, view(""), &opened) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_url_launcher_open_ext(game, view("https://example.invalid"), 0) ==
            CNA_RESULT_INVALID_ARGUMENT;
}

/* A real message box waits for a person, so every request here goes to this ABI's own backend. */
static int validate_message_box(const CNA_Handle game)
{
    CNA_MessageBoxTestLog log;
    CNA_StringView labels[2];
    CNA_Bool flag = UINT8_C(9);
    int32_t chosen = -99;

    labels[0] = view("Keep");
    labels[1] = view("Discard");

    if (cna_message_box_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_message_box_get_test_log_ext(game, &log) != CNA_RESULT_INVALID_STATE ||
        cna_message_box_set_test_backend_ext(game, CNA_TRUE, 1) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_message_box_show_simple_ext(
            game, CNA_MESSAGE_BOX_TYPE_WARNING, view("Title"), view("Body")) !=
            CNA_RESULT_SUCCESS ||
        cna_message_box_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.simple_calls != UINT32_C(1) || log.last_type != CNA_MESSAGE_BOX_TYPE_WARNING) {
        return 0;
    }
    if (cna_message_box_show_ext(
            game,
            CNA_MESSAGE_BOX_TYPE_INFORMATION,
            view("Title"),
            view("Body"),
            labels,
            UINT64_C(2),
            &chosen) != CNA_RESULT_SUCCESS ||
        chosen != 1 ||
        cna_message_box_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.choice_calls != UINT32_C(1) || log.last_button_count != UINT32_C(2) ||
        log.last_type != CNA_MESSAGE_BOX_TYPE_INFORMATION) {
        return 0;
    }
    /* An undefined severity, a missing label array and a null output are all refused before the
       platform ever sees the request. */
    if (cna_message_box_show_simple_ext(
            game, CNA_MESSAGE_BOX_TYPE_MAXIMUM + UINT32_C(1), view("T"), view("B")) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_message_box_show_ext(
            game, CNA_MESSAGE_BOX_TYPE_ERROR, view("T"), view("B"), labels, UINT64_C(0), &chosen) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_message_box_show_ext(
            game, CNA_MESSAGE_BOX_TYPE_ERROR, view("T"), view("B"), 0, UINT64_C(1), &chosen) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_message_box_show_ext(
            game, CNA_MESSAGE_BOX_TYPE_ERROR, view("T"), view("B"), labels, UINT64_C(2), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_message_box_get_test_log_ext(game, &log) != CNA_RESULT_SUCCESS ||
        log.simple_calls != UINT32_C(1) || log.choice_calls != UINT32_C(1)) {
        return 0;
    }
    return cna_message_box_set_test_backend_ext(game, CNA_FALSE, 0) == CNA_RESULT_SUCCESS &&
        cna_message_box_get_test_log_ext(game, &log) == CNA_RESULT_INVALID_STATE;
}

/* A real file dialog is asynchronous and driven by a person; the test backend answers immediately,
   which is the only way this ABI's own suite can observe the result handler at all. */
static int validate_file_dialog(const CNA_Handle game)
{
    CNA_FileDialogFilter filters[1];
    CNA_StringView results[1];
    DialogState state;
    CNA_Bool flag = UINT8_C(9);

    memset(&state, 0, sizeof(state));
    memset(filters, 0, sizeof(filters));
    filters[0].struct_size = (uint32_t)sizeof(filters[0]);
    filters[0].struct_version = UINT32_C(1);
    filters[0].name = view("Saves");
    filters[0].pattern = view("sav");
    results[0] = view("/tmp/cna-c-api-chosen.sav");

    if (cna_file_dialog_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_file_dialog_get_is_supported_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_file_dialog_set_test_backend_ext(game, CNA_TRUE, results, UINT64_C(1)) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_file_dialog_show_open_file_ext(
            game, on_dialog_result, &state, filters, UINT64_C(1), view(""), CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        state.calls != 1 || state.last_count != UINT64_C(1) ||
        strcmp(state.first, "/tmp/cna-c-api-chosen.sav") != 0) {
        return 0;
    }
    if (cna_file_dialog_show_save_file_ext(
            game, on_dialog_result, &state, filters, UINT64_C(1), view("/tmp")) !=
            CNA_RESULT_SUCCESS ||
        state.calls != 2 ||
        cna_file_dialog_show_open_folder_ext(
            game, on_dialog_result, &state, view(""), CNA_FALSE) != CNA_RESULT_SUCCESS ||
        state.calls != 3) {
        return 0;
    }
    /* Cancellation is an empty result rather than a separate signal, which is the canonical shape. */
    if (cna_file_dialog_set_test_backend_ext(game, CNA_TRUE, 0, UINT64_C(0)) !=
            CNA_RESULT_SUCCESS ||
        cna_file_dialog_show_open_file_ext(
            game, on_dialog_result, &state, 0, UINT64_C(0), view(""), CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        state.calls != 4 || state.last_count != UINT64_C(0)) {
        return 0;
    }
    /* A null handler, a missing filter array and an unversioned filter are all refused. */
    {
        CNA_FileDialogFilter broken = filters[0];
        broken.struct_version = UINT32_C(0);
        if (cna_file_dialog_show_open_file_ext(
                game, 0, &state, 0, UINT64_C(0), view(""), CNA_FALSE) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_file_dialog_show_open_file_ext(
                game, on_dialog_result, &state, 0, UINT64_C(1), view(""), CNA_FALSE) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_file_dialog_show_save_file_ext(
                game, on_dialog_result, &state, &broken, UINT64_C(1), view("")) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            cna_file_dialog_show_open_folder_ext(game, 0, &state, view(""), CNA_FALSE) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            state.calls != 4) {
            return 0;
        }
    }
    return cna_file_dialog_set_test_backend_ext(game, CNA_FALSE, 0, UINT64_C(0)) ==
        CNA_RESULT_SUCCESS;
}

/* The tray takes its backend as a constructor argument, so the test tray is a second creation route
   rather than a switch, and nothing reaches the host's own tray. */
static int validate_system_tray(const CNA_Handle game)
{
    CNA_SystemTrayHandle tray = CNA_INVALID_HANDLE;
    ClickState clicks;
    CNA_Bool flag = UINT8_C(9);
    uint64_t first = UINT64_C(99);
    uint64_t second = UINT64_C(99);

    memset(&clicks, 0, sizeof(clicks));
    /* CBIND-065: the real-backend constructors. Neither can be relied on to succeed -- no
       verification machine has a camera, and a tray needs a desktop environment that publishes
       one -- so what is asserted is that each gives a *documented* answer rather than an
       undefined one. The test backends below are what then drive the behaviour. */
    {
        CNA_CameraHandle real_camera = UINT64_C(9);
        CNA_SystemTrayHandle real_tray = UINT64_C(9);
        const CNA_Result camera_result = cna_camera_create(game, &real_camera);
        const CNA_Result tray_result = cna_system_tray_create(game, view("CNA"), &real_tray);
        if (camera_result != CNA_RESULT_SUCCESS && camera_result != CNA_RESULT_NOT_SUPPORTED &&
            camera_result != CNA_RESULT_PLATFORM) {
            return 0;
        }
        if (camera_result == CNA_RESULT_SUCCESS) {
            if (cna_camera_destroy(real_camera) != CNA_RESULT_SUCCESS) {
                return 0;
            }
        } else if (real_camera != CNA_INVALID_HANDLE) {
            return 0;
        }
        if (tray_result != CNA_RESULT_SUCCESS && tray_result != CNA_RESULT_NOT_SUPPORTED &&
            tray_result != CNA_RESULT_PLATFORM) {
            return 0;
        }
        if (tray_result == CNA_RESULT_SUCCESS) {
            if (cna_system_tray_destroy(real_tray) != CNA_RESULT_SUCCESS) {
                return 0;
            }
        } else if (real_tray != CNA_INVALID_HANDLE) {
            return 0;
        }
        /* A null output is refused whichever way the backend answers. */
        if (cna_camera_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_system_tray_create(game, view("CNA"), 0) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }

    if (cna_system_tray_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_system_tray_get_is_supported_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_system_tray_create_with_test_backend_ext(game, view("CNA"), 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_system_tray_create_with_test_backend_ext(game, view("CNA"), &tray) !=
            CNA_RESULT_SUCCESS ||
        tray == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_system_tray_set_tooltip(tray, view("CNA test tray")) != CNA_RESULT_SUCCESS ||
        cna_system_tray_add_entry(
            tray, view("Open"), CNA_FALSE, CNA_FALSE, CNA_TRUE, on_entry_click, &clicks, &first) !=
            CNA_RESULT_SUCCESS ||
        first != UINT64_C(0) ||
        cna_system_tray_add_entry(
            tray, view("Mute"), CNA_TRUE, CNA_TRUE, CNA_TRUE, 0, 0, &second) !=
            CNA_RESULT_SUCCESS ||
        second != UINT64_C(1) ||
        cna_system_tray_add_entry(
            tray, view("Open"), CNA_FALSE, CNA_FALSE, CNA_TRUE, on_entry_click, &clicks, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Entry state round-trips, and the entry added checked reports itself checked. */
    if (cna_system_tray_get_entry_checked(tray, second, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_system_tray_set_entry_checked(tray, second, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_system_tray_get_entry_checked(tray, second, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_system_tray_get_entry_enabled(tray, first, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_system_tray_set_entry_enabled(tray, first, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_system_tray_get_entry_enabled(tray, first, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_system_tray_set_entry_label(tray, first, view("Show")) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* An index past the last entry is ignored by the mutators and reads false, which is the platform
       backend's own behavior rather than a refusal invented here. */
    if (cna_system_tray_set_entry_label(tray, UINT64_C(9), view("Ghost")) != CNA_RESULT_SUCCESS ||
        cna_system_tray_set_entry_checked(tray, UINT64_C(9), CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_system_tray_set_entry_enabled(tray, UINT64_C(9), CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_system_tray_get_entry_checked(tray, UINT64_C(9), &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_system_tray_get_entry_enabled(tray, UINT64_C(9), &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    /* Activating an entry runs its handler; the entry added without one is silent, and clicking past
       the last entry is refused. */
    if (cna_system_tray_click_entry_for_tests_ext(tray, first) != CNA_RESULT_SUCCESS ||
        clicks.calls != 1 ||
        cna_system_tray_click_entry_for_tests_ext(tray, second) != CNA_RESULT_SUCCESS ||
        clicks.calls != 1 ||
        cna_system_tray_click_entry_for_tests_ext(tray, UINT64_C(9)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_system_tray_destroy(tray) == CNA_RESULT_SUCCESS &&
        cna_system_tray_destroy(tray) == CNA_RESULT_INVALID_HANDLE &&
        cna_system_tray_set_tooltip(tray, view("gone")) == CNA_RESULT_INVALID_HANDLE;
}

/* The real camera is deliberately never opened by this suite: on a machine that has one, creating it
   switches on the user's webcam, which is not something a test may do. Everything below either only
   enumerates or runs on this ABI's own backend. */
static int validate_camera(const CNA_Handle game, const CNA_Handle graphics_device)
{
    CNA_CameraHandle camera = CNA_INVALID_HANDLE;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Handle mismatched = CNA_INVALID_HANDLE;
    CNA_CameraDeviceInfo info;
    CNA_CameraState state = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    CNA_Color pixels[4];
    CNA_Color read_back[4];
    uint64_t count = UINT64_C(99);
    uint64_t bytes = UINT64_C(9);
    int32_t value = -1;
    char text[256];

    pixels[0].r = 10U; pixels[0].g = 20U; pixels[0].b = 30U; pixels[0].a = 255U;
    pixels[1].r = 40U; pixels[1].g = 50U; pixels[1].b = 60U; pixels[1].a = 255U;
    pixels[2].r = 70U; pixels[2].g = 80U; pixels[2].b = 90U; pixels[2].a = 255U;
    pixels[3].r = 100U; pixels[3].g = 110U; pixels[3].b = 120U; pixels[3].a = 255U;

    /* A driver is not a camera, so the probe and the enumeration are separate questions. */
    if (cna_camera_get_is_supported_ext(game, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_camera_get_is_supported_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_get_count_ext(game, &count) != CNA_RESULT_SUCCESS ||
        cna_camera_get_count_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    {
        uint64_t index = UINT64_C(0);
        for (index = UINT64_C(0); index < count; ++index) {
            memset(text, 0, sizeof(text));
            if (cna_camera_get_info_at_ext(game, index, &info) != CNA_RESULT_SUCCESS ||
                info.position > CNA_CAMERA_POSITION_MAXIMUM ||
                cna_camera_get_name_size_at_ext(game, index, &bytes) != CNA_RESULT_SUCCESS ||
                bytes >= (uint64_t)sizeof(text) ||
                cna_camera_copy_name_at_ext(
                    game, index, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS) {
                return 0;
            }
        }
    }
    if (cna_camera_get_info_at_ext(game, count, &info) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_get_name_size_at_ext(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_copy_name_at_ext(
            game, count, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    /* A camera on this ABI's own backend starts with no frame at all, and reports the platform's
       permission-pending answer rather than "closed": since the platform separation a device that
       is not open is never handed out, so nothing can report closed and the two identities that
       say so are refused rather than silently changed into something else. */
    if (cna_camera_create_with_test_backend_ext(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_create_with_test_backend_ext(game, &camera) != CNA_RESULT_SUCCESS ||
        camera == CNA_INVALID_HANDLE ||
        cna_camera_get_state_ext(camera, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_CAMERA_STATE_OPENING ||
        cna_camera_set_test_state_ext(camera, CNA_CAMERA_STATE_CLOSED) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_set_test_state_ext(camera, CNA_CAMERA_STATE_NOT_SUPPORTED) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_get_frame_width_ext(camera, &value) != CNA_RESULT_SUCCESS || value != 0 ||
        cna_camera_get_frame_height_ext(camera, &value) != CNA_RESULT_SUCCESS || value != 0) {
        return 0;
    }
    /* The refused and lost states exist and are only reachable through the backend here. */
    if (cna_camera_set_test_state_ext(camera, CNA_CAMERA_STATE_DENIED) != CNA_RESULT_SUCCESS ||
        cna_camera_get_state_ext(camera, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_CAMERA_STATE_DENIED ||
        cna_camera_set_test_state_ext(camera, CNA_CAMERA_STATE_MAXIMUM + UINT32_C(1)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* A published frame reports the camera ready and fixes its frame size. */
    if (cna_camera_set_test_frame_ext(camera, 2, 2, pixels, UINT64_C(3)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_set_test_frame_ext(camera, -1, 2, pixels, UINT64_C(4)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_camera_set_test_frame_ext(camera, 2, 2, pixels, UINT64_C(4)) != CNA_RESULT_SUCCESS ||
        cna_camera_get_state_ext(camera, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_CAMERA_STATE_READY ||
        cna_camera_get_frame_width_ext(camera, &value) != CNA_RESULT_SUCCESS || value != 2 ||
        cna_camera_get_frame_height_ext(camera, &value) != CNA_RESULT_SUCCESS || value != 2) {
        return 0;
    }
    {
        const CNA_Texture2DCreateInfo matching = {
            sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 2U, 2U, CNA_FALSE, {0U, 0U, 0U},
            CNA_SURFACE_FORMAT_COLOR};
        const CNA_Texture2DCreateInfo other = {
            sizeof(CNA_Texture2DCreateInfo), UINT32_C(1), 4U, 4U, CNA_FALSE, {0U, 0U, 0U},
            CNA_SURFACE_FORMAT_COLOR};
        if (cna_texture2d_create(graphics_device, &matching, &texture) != CNA_RESULT_SUCCESS ||
            cna_texture2d_create(graphics_device, &other, &mismatched) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* The frame lands in a texture the caller owns and keeps -- nothing here is invalidated by the
       next call, unlike a video player's borrowed frame. */
    if (cna_camera_try_acquire_frame_ext(camera, texture, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_texture2d_get_data_rgba8(texture, read_back, UINT64_C(4), &count) !=
            CNA_RESULT_SUCCESS ||
        count != UINT64_C(4) || read_back[0].r != 10U || read_back[3].b != 120U ||
        read_back[3].a != 255U) {
        return 0;
    }
    /* A texture whose size does not match the frame is refused exactly the way an absent frame is:
       the canonical route neither resizes it nor says why. */
    if (cna_camera_try_acquire_frame_ext(camera, mismatched, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_camera_try_acquire_frame_ext(camera, CNA_INVALID_HANDLE, &flag) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_camera_try_acquire_frame_ext(camera, texture, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Clearing the frame returns the camera to permission-pending, and having no frame is an
       ordinary false. */
    if (cna_camera_set_test_frame_ext(camera, 0, 0, 0, UINT64_C(0)) != CNA_RESULT_SUCCESS ||
        cna_camera_get_state_ext(camera, &state) != CNA_RESULT_SUCCESS ||
        state != CNA_CAMERA_STATE_OPENING ||
        cna_camera_try_acquire_frame_ext(camera, texture, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_texture2d_destroy(mismatched) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(texture) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_camera_destroy(camera) == CNA_RESULT_SUCCESS &&
        cna_camera_destroy(camera) == CNA_RESULT_INVALID_HANDLE &&
        cna_camera_get_state_ext(camera, &state) == CNA_RESULT_INVALID_HANDLE;
}

/* Compiled without the extension layer every route above still exists and reports the layer as
   missing, so a consumer links against one symbol set either way. */
static int validate_unavailable(const CNA_Handle game)
{
    CNA_SystemTrayHandle tray = CNA_INVALID_HANDLE;
    CNA_PowerState power = UINT32_C(99);
    CNA_MessageBoxTestLog log;
    CNA_Rectangle area;
    CNA_Bool flag = UINT8_C(9);
    int32_t value = 0;
    float scale = 0.0F;
    uint64_t count = UINT64_C(0);

    CNA_CameraHandle camera = CNA_INVALID_HANDLE;
    CNA_CameraDeviceInfo camera_info;

    return cna_power_get_state_ext(game, &power) == CNA_RESULT_NOT_SUPPORTED &&
        cna_power_get_battery_percent_ext(game, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_power_get_seconds_remaining_ext(game, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_system_info_get_logical_cpu_core_count_ext(game, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_system_info_get_system_ram_megabytes_ext(game, &value) == CNA_RESULT_NOT_SUPPORTED &&
        cna_locale_get_preferred_count_ext(game, &count) == CNA_RESULT_NOT_SUPPORTED &&
        cna_locale_get_language_size_at_ext(game, UINT64_C(0), &count) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_locale_get_country_size_at_ext(game, UINT64_C(0), &count) == CNA_RESULT_NOT_SUPPORTED &&
        cna_display_info_get_content_scale_ext(game, &scale) == CNA_RESULT_NOT_SUPPORTED &&
        cna_display_info_get_safe_area_ext(game, &area) == CNA_RESULT_NOT_SUPPORTED &&
        cna_devices_clipboard_set_text_ext(game, view("x"), &flag) == CNA_RESULT_NOT_SUPPORTED &&
        cna_url_launcher_open_ext(game, view("https://example.invalid"), &flag) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_message_box_get_is_supported_ext(game, &flag) == CNA_RESULT_NOT_SUPPORTED &&
        cna_message_box_show_simple_ext(
            game, CNA_MESSAGE_BOX_TYPE_ERROR, view("T"), view("B")) == CNA_RESULT_NOT_SUPPORTED &&
        cna_message_box_set_test_backend_ext(game, CNA_TRUE, 0) == CNA_RESULT_NOT_SUPPORTED &&
        cna_message_box_get_test_log_ext(game, &log) == CNA_RESULT_NOT_SUPPORTED &&
        cna_file_dialog_get_is_supported_ext(game, &flag) == CNA_RESULT_NOT_SUPPORTED &&
        cna_file_dialog_set_test_backend_ext(game, CNA_TRUE, 0, UINT64_C(0)) ==
            CNA_RESULT_NOT_SUPPORTED &&
        cna_system_tray_get_is_supported_ext(game, &flag) == CNA_RESULT_NOT_SUPPORTED &&
        cna_system_tray_create_with_test_backend_ext(game, view("CNA"), &tray) ==
            CNA_RESULT_NOT_SUPPORTED &&
        tray == CNA_INVALID_HANDLE &&
        cna_system_tray_destroy(tray) == CNA_RESULT_NOT_SUPPORTED &&
        cna_camera_get_is_supported_ext(game, &flag) == CNA_RESULT_NOT_SUPPORTED &&
        cna_camera_get_count_ext(game, &count) == CNA_RESULT_NOT_SUPPORTED &&
        cna_camera_get_info_at_ext(game, UINT64_C(0), &camera_info) == CNA_RESULT_NOT_SUPPORTED &&
        cna_camera_get_name_size_at_ext(game, UINT64_C(0), &count) == CNA_RESULT_NOT_SUPPORTED &&
        cna_camera_create_with_test_backend_ext(game, &camera) == CNA_RESULT_NOT_SUPPORTED &&
        camera == CNA_INVALID_HANDLE &&
        cna_camera_destroy(camera) == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_identities(void)
{
    CNA_DeviceType device_type = UINT32_MAX;
#if defined(__EMSCRIPTEN__)
    const CNA_DeviceType expected_device_type = CNA_DEVICE_TYPE_EMULATOR;
#else
    const CNA_DeviceType expected_device_type = CNA_DEVICE_TYPE_DEVICE;
#endif
    return sizeof(CNA_DeviceType) == sizeof(uint32_t) &&
        CNA_DEVICE_TYPE_DEVICE == UINT32_C(0) && CNA_DEVICE_TYPE_EMULATOR == UINT32_C(1) &&
        CNA_DEVICE_TYPE_MAXIMUM == CNA_DEVICE_TYPE_EMULATOR &&
        cna_environment_get_device_type(&device_type) == CNA_RESULT_SUCCESS &&
        device_type == expected_device_type &&
        cna_environment_get_device_type(0) == CNA_RESULT_INVALID_ARGUMENT &&
        CNA_POWER_STATE_ERROR == UINT32_C(0) && CNA_POWER_STATE_UNKNOWN == UINT32_C(1) &&
        CNA_POWER_STATE_ON_BATTERY == UINT32_C(2) && CNA_POWER_STATE_NO_BATTERY == UINT32_C(3) &&
        CNA_POWER_STATE_CHARGING == UINT32_C(4) && CNA_POWER_STATE_CHARGED == UINT32_C(5) &&
        CNA_POWER_STATE_MAXIMUM == CNA_POWER_STATE_CHARGED &&
        CNA_MESSAGE_BOX_TYPE_ERROR == UINT32_C(0) && CNA_MESSAGE_BOX_TYPE_WARNING == UINT32_C(1) &&
        CNA_MESSAGE_BOX_TYPE_INFORMATION == UINT32_C(2) &&
        CNA_MESSAGE_BOX_TYPE_MAXIMUM == CNA_MESSAGE_BOX_TYPE_INFORMATION &&
        CNA_CAMERA_STATE_NOT_SUPPORTED == UINT32_C(0) && CNA_CAMERA_STATE_CLOSED == UINT32_C(1) &&
        CNA_CAMERA_STATE_OPENING == UINT32_C(2) && CNA_CAMERA_STATE_DENIED == UINT32_C(3) &&
        CNA_CAMERA_STATE_READY == UINT32_C(4) && CNA_CAMERA_STATE_LOST == UINT32_C(5) &&
        CNA_CAMERA_STATE_MAXIMUM == CNA_CAMERA_STATE_LOST &&
        CNA_CAMERA_POSITION_UNKNOWN == UINT32_C(0) &&
        CNA_CAMERA_POSITION_FRONT_FACING == UINT32_C(1) &&
        CNA_CAMERA_POSITION_BACK_FACING == UINT32_C(2) &&
        CNA_CAMERA_POSITION_MAXIMUM == CNA_CAMERA_POSITION_BACK_FACING;
}

/* The descriptor value answers in every build, like every other pure value operation in this ABI. */
static int validate_camera_descriptor(void)
{
    CNA_CameraDeviceInfo info;
    memset(&info, 9, sizeof(info));
    return cna_camera_device_info_init(&info) == CNA_RESULT_SUCCESS &&
        info.struct_size == (uint32_t)sizeof(info) && info.struct_version == UINT32_C(1) &&
        info.position == CNA_CAMERA_POSITION_UNKNOWN &&
        cna_camera_device_info_init(0) == CNA_RESULT_INVALID_ARGUMENT;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    DevicesSmokeState* const state = (DevicesSmokeState*)context;
    CNA_Bool available = UINT8_C(9);
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (game_time == 0 || !validate_identities() || !validate_camera_descriptor() ||
        cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_devices_ext_is_available(&available) != CNA_RESULT_SUCCESS ||
        (available != CNA_TRUE && available != CNA_FALSE) ||
        cna_devices_ext_is_available(0) != CNA_RESULT_INVALID_ARGUMENT ||
        !validate_vibration(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (available == CNA_TRUE) {
        if (!validate_host_queries(game) || !validate_clipboard_and_url(game) ||
            !validate_message_box(game) || !validate_file_dialog(game) ||
            !validate_system_tray(game) || !validate_camera(game, graphics_device)) {
            return CNA_RESULT_INVALID_STATE;
        }
    } else if (!validate_unavailable(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    DevicesSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API device smoke", UINT64_C(18)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        smoke_state.validated != 1) {
        return CNA_TEST_FAIL(1);
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 2;
}
