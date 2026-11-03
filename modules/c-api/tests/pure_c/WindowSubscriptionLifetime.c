/* SPDX-License-Identifier: MS-PL */
#include <CNA/C/cna.h>
#include "CnaTestReport.h"
#include "CnaRuntimeSmoke.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { cna_smoke_error(); return CNA_TEST_FAIL(1); } } while (0)
static CNA_Handle game;
static CNA_Handle registrations[4];
static unsigned calls;
static void event(void* context) { (void)context; ++calls; }
static void drop(void* context, const CNA_StringView* items, uint64_t count)
{
    (void)context; (void)items; (void)count; ++calls;
}
static void exit_witness(void)
{
    CNA_Rectangle bounds = {0};
    /* Installed before game creation: cleanup must already have emptied the registry.
     * Querying the dead handles makes omission of cleanup observable even without ASan. */
    if (cna_game_window_get_client_bounds(game, &bounds) != CNA_RESULT_INVALID_HANDLE) _Exit(1);
    for (unsigned i = 0; i < 4; ++i)
        if (cna_game_unsubscribe(registrations[i]) != CNA_RESULT_INVALID_HANDLE) _Exit(1);
    fputs("window subscription exit cleanup preceded witness: PASS\n", stderr);
}
static CNA_Handle device;
static CNA_Handle device_registrations[3];
static unsigned device_disposals;
static void device_event(CNA_Handle handle, void* context)
{
    (void)handle; (void)context; ++device_disposals;
}
static void device_created(CNA_Handle handle, const CNA_ResourceCreatedEventInfo* info, void* context)
{
    (void)handle; (void)info; (void)context;
}
static void device_destroyed(CNA_Handle handle, const CNA_ResourceDestroyedEventInfo* info, void* context)
{
    (void)handle; (void)info; (void)context;
}
static void device_exit_witness(void)
{
    CNA_Bool disposed = 99;
    if (cna_graphics_device_get_is_disposed(device, &disposed) != CNA_RESULT_INVALID_HANDLE || device_disposals > 1) _Exit(1);
    for (unsigned i = 0; i < 3; ++i)
        if (cna_graphics_device_unsubscribe(device_registrations[i]) != CNA_RESULT_INVALID_HANDLE) _Exit(1);
    fputs("device subscription cleanup preceded witness: PASS\n", stderr);
}
static int device_lifetime(const char* mode)
{
    const int at_exit = strcmp(mode, "device-exit") == 0;
    if (at_exit) CHECK(atexit(device_exit_witness) == 0);
    CNA_PresentationParameters parameters;
    CHECK(cna_presentation_parameters_init(&parameters) == CNA_RESULT_SUCCESS);
    parameters.back_buffer_width = 32;
    parameters.back_buffer_height = 32;
    parameters.is_full_screen = CNA_FALSE;
    CHECK(cna_graphics_device_create(0, CNA_GRAPHICS_PROFILE_REACH, &parameters, &device) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_subscribe_event(device, CNA_GRAPHICS_DEVICE_EVENT_DISPOSING, device_event, NULL, &device_registrations[0]) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_subscribe_resource_created(device, device_created, NULL, &device_registrations[1]) == CNA_RESULT_SUCCESS);
    CHECK(cna_graphics_device_subscribe_resource_destroyed(device, device_destroyed, NULL, &device_registrations[2]) == CNA_RESULT_SUCCESS);
    if (at_exit) return 0;
    const int device_first = strcmp(mode, "device-first") == 0;
    CHECK(device_first || strcmp(mode, "device-subscription-first") == 0);
    if (device_first) CHECK(cna_graphics_device_destroy(device) == CNA_RESULT_SUCCESS && device_disposals == 1);
    for (unsigned i = 0; i < 3; ++i)
        CHECK(cna_graphics_device_unsubscribe(device_registrations[i]) == CNA_RESULT_SUCCESS);
    if (!device_first) CHECK(cna_graphics_device_destroy(device) == CNA_RESULT_SUCCESS && device_disposals == 0);
    puts("device subscription release order: PASS");
    return 0;
}
int main(int argc, char** argv)
{
    CHECK(argc == 2);
    if (strncmp(argv[1], "device-", 7) == 0) return device_lifetime(argv[1]);
    const int at_exit = strncmp(argv[1], "exit", 4) == 0;
    const int reused_slot = strcmp(argv[1], "exit-reused-slot") == 0;
    CNA_Handle placeholders[4] = {0};
    if (at_exit) CHECK(atexit(exit_witness) == 0);
    /* Reuse a slot preceding the game, so the suite covers registrations on both
     * sides of the game in the registry, rather than assuming destruction order. */
    if (reused_slot) {
        const CNA_ShaderDiagnosticInfoEXT info = {sizeof(info), 1, 2, 0, 0, 0};
        for (unsigned i = 0; i < 4; ++i)
            CHECK(cna_shader_diagnostics_create_ext(&info, (CNA_StringView){NULL, 0},
                (CNA_StringView){"placeholder", 11}, &placeholders[i]) == CNA_RESULT_SUCCESS);
    }
    const CNA_GameCreateInfo info = {sizeof(info), 1, CNA_TRUE, {0}, 166667, {NULL, 0}, NULL};
    const CNA_Result created = cna_game_create(&info, &game);
    if (created == CNA_RESULT_NOT_SUPPORTED) {
        /* A renderer that needs a native window (WEBGPU) refuses the offscreen video driver. */
        cna_smoke_error();
        return 77;
    }
    CHECK(created == CNA_RESULT_SUCCESS);
    if (reused_slot)
        for (unsigned i = 0; i < 4; ++i)
            CHECK(cna_shader_diagnostics_destroy_ext(placeholders[i]) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_subscribe(game, CNA_GAME_EVENT_DISPOSED, event, NULL, &registrations[0]) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_window_subscribe(game, CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED, event, NULL, &registrations[1]) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_window_subscribe_file_drop_ext(game, drop, NULL, &registrations[2]) == CNA_RESULT_SUCCESS);
    CHECK(cna_game_window_subscribe_text_drop_ext(game, drop, NULL, &registrations[3]) == CNA_RESULT_SUCCESS);
    if (at_exit) return 0;
    const int game_first = strcmp(argv[1], "game-first") == 0;
    CHECK(game_first || strcmp(argv[1], "subscription-first") == 0);
    if (game_first) {
        CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
        CHECK(calls == 1);
    }
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(cna_game_unsubscribe(registrations[i]) == CNA_RESULT_SUCCESS);
        CHECK(cna_game_unsubscribe(registrations[i]) == CNA_RESULT_INVALID_HANDLE);
    }
    if (!game_first) {
        /* Removed callbacks must stay removed while the source is still alive. */
        CHECK(cna_game_window_raise_file_drop_ext(game, NULL, 0) == CNA_RESULT_SUCCESS);
        CHECK(cna_game_window_raise_text_drop_ext(game, (CNA_StringView){"text", 4}) == CNA_RESULT_SUCCESS);
        CHECK(cna_game_destroy(game) == CNA_RESULT_SUCCESS);
        CHECK(calls == 0);
    }
    puts("window subscription release order: PASS");
    return 0;
}
