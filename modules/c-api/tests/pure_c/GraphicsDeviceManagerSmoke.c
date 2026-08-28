// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

typedef struct ManagerSmokeState {
    int validated;
} ManagerSmokeState;

typedef struct EventState {
    int calls;
} EventState;

typedef struct SettingsState {
    int calls;
    int32_t last_back_buffer_width;
} SettingsState;

static void on_manager_event(void* const context)
{
    ++((EventState*)context)->calls;
}

static void on_preparing_device_settings(
    const CNA_GraphicsDeviceInformation* const information,
    void* const context)
{
    SettingsState* const state = (SettingsState*)context;
    ++state->calls;
    state->last_back_buffer_width = information->presentation_parameters.back_buffer_width;
}

/* CBIND-057: the mutable half of the same event. XNA's PreparingDeviceSettings exists to override
   the settings before the device is created, and this handler does exactly that. */
typedef struct MutatorState {
    int calls;
    int32_t observed_width;
    int32_t requested_width;
    int corrupt_next;
} MutatorState;

static void on_preparing_device_settings_mutable(
    CNA_GraphicsDeviceInformation* const information,
    void* const context)
{
    MutatorState* const state = (MutatorState*)context;
    ++state->calls;
    state->observed_width = information->presentation_parameters.back_buffer_width;
    if (state->corrupt_next) {
        /* A structure this handler has broken must be ignored rather than half-applied. */
        information->struct_version = UINT32_C(0);
        return;
    }
    information->presentation_parameters.back_buffer_width = state->requested_width;
}

/* An accepted request and a platform that declines are both correct answers: a headless video driver
   refuses a reconfiguration it has nothing to reconfigure. */
static int accepted_or_refused_by_platform(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_PLATFORM;
}

static int validate_identities(void)
{
    return CNA_PRESENTATION_MODE_LETTERBOX == UINT32_C(0) &&
        CNA_PRESENTATION_MODE_OVERSCAN == UINT32_C(1) &&
        CNA_PRESENTATION_MODE_STRETCH == UINT32_C(2) &&
        CNA_PRESENTATION_MODE_NATIVE_BACK_BUFFER == UINT32_C(3) &&
        CNA_PRESENTATION_MODE_FIXED_HEIGHT_DYNAMIC_WIDTH == UINT32_C(4) &&
        CNA_PRESENTATION_MODE_MAXIMUM == CNA_PRESENTATION_MODE_FIXED_HEIGHT_DYNAMIC_WIDTH &&
        CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_WIDTH == INT32_C(800) &&
        CNA_GRAPHICS_DEVICE_MANAGER_DEFAULT_BACK_BUFFER_HEIGHT == INT32_C(480) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DISPOSED == UINT32_C(0) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_CREATED == UINT32_C(1) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_DISPOSING == UINT32_C(2) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET == UINT32_C(3) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING == UINT32_C(4) &&
        CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM ==
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING;
}

/* The configuration value names its adapter by index, because a pointer into the runtime's adapter
   list is nothing a C caller could hold. */
static int validate_device_information(void)
{
    CNA_GraphicsDeviceInformation information;
    CNA_GraphicsDeviceInformation clone;
    uint64_t bytes = UINT64_C(9);
    char text[128];

    memset(&information, 9, sizeof(information));
    if (cna_graphics_device_information_init(&information) != CNA_RESULT_SUCCESS ||
        information.struct_size != (uint32_t)sizeof(information) ||
        information.struct_version != UINT32_C(1) ||
        information.presentation_parameters.struct_version != UINT32_C(1) ||
        cna_graphics_device_information_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    information.presentation_parameters.back_buffer_width = 1024;
    information.presentation_parameters.back_buffer_height = 768;
    memset(&clone, 0, sizeof(clone));
    if (cna_graphics_device_information_clone(&information, &clone) != CNA_RESULT_SUCCESS ||
        clone.presentation_parameters.back_buffer_width != 1024 ||
        clone.presentation_parameters.back_buffer_height != 768 ||
        clone.adapter_index != information.adapter_index ||
        cna_graphics_device_information_clone(&information, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_information_clone(0, &clone) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An unversioned configuration, and one carrying an undefined identity, are both refused. */
    {
        CNA_GraphicsDeviceInformation broken = information;
        broken.struct_version = UINT32_C(0);
        if (cna_graphics_device_information_clone(&broken, &clone) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        broken = information;
        broken.graphics_profile = UINT32_C(99);
        if (cna_graphics_device_information_clone(&broken, &clone) != CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    memset(text, 0, sizeof(text));
    return cna_graphics_device_information_get_type_name_size(&bytes) == CNA_RESULT_SUCCESS &&
        bytes < (uint64_t)sizeof(text) &&
        cna_graphics_device_information_copy_type_name(text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Xna.Framework.GraphicsDeviceInformation") == 0 &&
        cna_graphics_device_information_copy_type_name(text, UINT64_C(2), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        cna_graphics_device_information_get_type_name_size(0) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_preferences(const CNA_GraphicsDeviceManagerHandle manager)
{
    CNA_GraphicsProfile profile = UINT32_C(99);
    CNA_SurfaceFormat surface = UINT32_C(99);
    CNA_DepthFormat depth = UINT32_C(99);
    CNA_DisplayOrientation orientations = UINT32_C(99);
    CNA_PresentationMode mode = UINT32_C(99);
    CNA_Bool flag = UINT8_C(9);
    int32_t value = -1;

    if (cna_graphics_device_manager_get_graphics_profile(manager, &profile) !=
            CNA_RESULT_SUCCESS ||
        profile > CNA_GRAPHICS_PROFILE_HI_DEF ||
        cna_graphics_device_manager_set_graphics_profile(manager, CNA_GRAPHICS_PROFILE_HI_DEF) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_graphics_profile(manager, &profile) !=
            CNA_RESULT_SUCCESS ||
        profile != CNA_GRAPHICS_PROFILE_HI_DEF ||
        cna_graphics_device_manager_set_graphics_profile(manager, UINT32_C(99)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_manager_get_graphics_profile(manager, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_graphics_device_manager_get_is_full_screen(manager, &flag) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_set_is_full_screen(manager, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_is_full_screen(manager, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_graphics_device_manager_set_is_full_screen(manager, CNA_FALSE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_graphics_device_manager_get_prefer_multi_sampling(manager, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_set_prefer_multi_sampling(manager, CNA_TRUE) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_prefer_multi_sampling(manager, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_graphics_device_manager_set_prefer_multi_sampling(manager, CNA_FALSE) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_graphics_device_manager_get_preferred_back_buffer_format(manager, &surface) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_set_preferred_back_buffer_format(
            manager, CNA_SURFACE_FORMAT_COLOR) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_preferred_back_buffer_format(manager, &surface) !=
            CNA_RESULT_SUCCESS ||
        surface != CNA_SURFACE_FORMAT_COLOR ||
        cna_graphics_device_manager_set_preferred_back_buffer_format(manager, UINT32_C(9999)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_graphics_device_manager_get_preferred_depth_stencil_format(manager, &depth) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_set_preferred_depth_stencil_format(
            manager, CNA_DEPTH_FORMAT_DEPTH24_STENCIL8) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_preferred_depth_stencil_format(manager, &depth) !=
            CNA_RESULT_SUCCESS ||
        depth != CNA_DEPTH_FORMAT_DEPTH24_STENCIL8 ||
        cna_graphics_device_manager_set_preferred_depth_stencil_format(manager, UINT32_C(99)) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The canonical setter records whatever it is given; negotiation happens on apply, not here. */
    if (cna_graphics_device_manager_set_preferred_back_buffer_width(manager, 640) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_preferred_back_buffer_width(manager, &value) !=
            CNA_RESULT_SUCCESS ||
        value != 640 ||
        cna_graphics_device_manager_set_preferred_back_buffer_height(manager, 400) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_preferred_back_buffer_height(manager, &value) !=
            CNA_RESULT_SUCCESS ||
        value != 400) {
        return 0;
    }
    if (cna_graphics_device_manager_get_synchronize_with_vertical_retrace(manager, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_set_synchronize_with_vertical_retrace(manager, CNA_FALSE) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_synchronize_with_vertical_retrace(manager, &flag) !=
            CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_graphics_device_manager_set_synchronize_with_vertical_retrace(manager, CNA_TRUE) !=
            CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Supported orientations are a bit set, so a combination is accepted where a single-identity
       route would refuse one. */
    if (cna_graphics_device_manager_set_supported_orientations(
            manager,
            CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT | CNA_DISPLAY_ORIENTATION_PORTRAIT) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_get_supported_orientations(manager, &orientations) !=
            CNA_RESULT_SUCCESS ||
        orientations !=
            (CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT | CNA_DISPLAY_ORIENTATION_PORTRAIT)) {
        return 0;
    }
    return cna_graphics_device_manager_get_preferred_presentation_mode_ext(manager, &mode) ==
            CNA_RESULT_SUCCESS &&
        mode <= CNA_PRESENTATION_MODE_MAXIMUM &&
        cna_graphics_device_manager_set_preferred_presentation_mode_ext(
            manager, CNA_PRESENTATION_MODE_STRETCH) == CNA_RESULT_SUCCESS &&
        cna_graphics_device_manager_get_preferred_presentation_mode_ext(manager, &mode) ==
            CNA_RESULT_SUCCESS &&
        mode == CNA_PRESENTATION_MODE_STRETCH &&
        cna_graphics_device_manager_set_preferred_presentation_mode_ext(
            manager, CNA_PRESENTATION_MODE_MAXIMUM + UINT32_C(1)) == CNA_RESULT_INVALID_ARGUMENT;
}

static int validate_manager(const CNA_Handle game)
{
    CNA_GraphicsDeviceManagerHandle manager = CNA_INVALID_HANDLE;
    CNA_GraphicsDeviceManagerHandle second = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle created = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle disposing = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle reset = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle resetting = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle disposed = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle settings = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle mutable_settings = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle rejected = CNA_INVALID_HANDLE;
    CNA_Handle device = CNA_INVALID_HANDLE;
    EventState events = {0};
    EventState disposal = {0};
    SettingsState prepared = {0, 0};
    MutatorState mutated = {0, 0, 640, 0};
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[128];

    /* A game has no manager until one is created, and creating it registers both services -- which
       is exactly what the service query added by the component slice reports. */
    if (cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_graphics_device_manager_create(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_manager_create(game, &manager) != CNA_RESULT_SUCCESS ||
        manager == CNA_INVALID_HANDLE ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_SERVICE, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_TRUE) {
        return 0;
    }
    /* A game accepts exactly one manager; the canonical constructor refuses a second. */
    if (cna_graphics_device_manager_create(game, &second) != CNA_RESULT_INVALID_ARGUMENT ||
        second != CNA_INVALID_HANDLE) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_graphics_device_manager_get_type_name_size(manager, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_graphics_device_manager_copy_type_name(manager, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.GraphicsDeviceManager") != 0 ||
        cna_graphics_device_manager_copy_type_name(manager, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    /* The managed device is the game's own, borrowed on the same terms. */
    if (cna_graphics_device_manager_get_graphics_device(manager, &device) != CNA_RESULT_SUCCESS ||
        device == CNA_INVALID_HANDLE ||
        cna_graphics_device_manager_get_graphics_device(manager, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (!validate_preferences(manager)) {
        return 0;
    }
    if (cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_CREATED,
            on_manager_event,
            &events,
            &created) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_DISPOSING,
            on_manager_event,
            &events,
            &disposing) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET,
            on_manager_event,
            &events,
            &reset) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESETTING,
            on_manager_event,
            &events,
            &resetting) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DISPOSED,
            on_manager_event,
            &disposal,
            &disposed) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_manager_subscribe_preparing_device_settings(
            manager, on_preparing_device_settings, &prepared, &settings) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* A refused subscription clears its output first, so these take a handle of their own. */
    if (cna_graphics_device_manager_subscribe(
            manager,
            CNA_GRAPHICS_DEVICE_MANAGER_EVENT_MAXIMUM + UINT32_C(1),
            on_manager_event,
            &events,
            &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_graphics_device_manager_subscribe(
            manager, CNA_GRAPHICS_DEVICE_MANAGER_EVENT_DEVICE_RESET, 0, &events, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_manager_subscribe_preparing_device_settings(
            manager, 0, &prepared, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_graphics_device_manager_subscribe_preparing_device_settings_ext(
            manager, 0, &mutated, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_graphics_device_manager_subscribe_preparing_device_settings_ext(
            manager, on_preparing_device_settings_mutable, &mutated, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_graphics_device_manager_subscribe_preparing_device_settings_ext(
            manager, on_preparing_device_settings_mutable, &mutated, &mutable_settings) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* Applying the recorded preferences is where they reach the device and the window; a platform
       that declines the reconfiguration is a correct answer, not a fault. */
    if (!accepted_or_refused_by_platform(cna_graphics_device_manager_apply_changes(manager)) ||
        !accepted_or_refused_by_platform(cna_graphics_device_manager_create_device(manager)) ||
        !accepted_or_refused_by_platform(cna_graphics_device_manager_toggle_full_screen(manager))) {
        return 0;
    }
    /* The read-only handler saw the configuration the device was prepared with. */
    if (prepared.calls > 0 && prepared.last_back_buffer_width <= 0) {
        return 0;
    }
    /* The mutable handler ran on the same event, and what it wrote is what the next preparation
       observes -- which is the whole point of the route and what the const one cannot do. Both
       handlers being on one event is also the assertion that adding the mutable one did not
       displace the published observation-only route. */
    if (mutated.calls != prepared.calls || (mutated.calls > 0 && mutated.observed_width <= 0)) {
        return 0;
    }
    if (mutated.calls > 0) {
        const int32_t written = mutated.requested_width;
        mutated.observed_width = 0;
        if (!accepted_or_refused_by_platform(
                cna_graphics_device_manager_apply_changes(manager)) ||
            !accepted_or_refused_by_platform(
                cna_graphics_device_manager_create_device(manager))) {
            return 0;
        }
        if (mutated.observed_width != written) {
            return 0;
        }
        /* A handler that corrupts the structure changes nothing rather than being obeyed. */
        mutated.corrupt_next = 1;
        mutated.observed_width = 0;
        if (!accepted_or_refused_by_platform(
                cna_graphics_device_manager_apply_changes(manager)) ||
            !accepted_or_refused_by_platform(
                cna_graphics_device_manager_create_device(manager))) {
            return 0;
        }
        mutated.corrupt_next = 0;
        if (mutated.observed_width != written) {
            return 0;
        }
    }
    if (cna_graphics_device_manager_begin_draw(manager, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_graphics_device_manager_begin_draw(manager, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        !accepted_or_refused_by_platform(cna_graphics_device_manager_end_draw(manager))) {
        return 0;
    }
    /* Disposal unregisters both services and raises its event once; a second disposal is a no-op,
       which is this canonical type's own idempotence. */
    if (cna_graphics_device_manager_dispose(manager) != CNA_RESULT_SUCCESS ||
        disposal.calls != 1 ||
        cna_graphics_device_manager_dispose(manager) != CNA_RESULT_SUCCESS ||
        disposal.calls != 1 ||
        cna_game_services_contains_ext(
            game, CNA_GAME_SERVICE_TYPE_GRAPHICS_DEVICE_MANAGER, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE) {
        return 0;
    }
    if (cna_game_unsubscribe(created) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(disposing) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(reset) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(resetting) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(disposed) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(settings) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(mutable_settings) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_graphics_device_manager_destroy(manager) == CNA_RESULT_SUCCESS &&
        cna_graphics_device_manager_destroy(manager) == CNA_RESULT_INVALID_HANDLE &&
        cna_graphics_device_manager_get_is_full_screen(manager, &flag) ==
            CNA_RESULT_INVALID_HANDLE;
}

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    (void)out_error;
    ManagerSmokeState* const state = (ManagerSmokeState*)context;
    if (game_time == 0 || !validate_identities() || !validate_device_information() ||
        !validate_manager(game)) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->validated = 1;
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    ManagerSmokeState smoke_state = {0};
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, &smoke_state
    };
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API device manager smoke", UINT64_C(26)},
        &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS || smoke_state.validated != 1) {
        return CNA_TEST_FAIL(1);
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 2;
}
