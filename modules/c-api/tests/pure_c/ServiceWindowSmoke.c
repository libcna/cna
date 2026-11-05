/* SPDX-License-Identifier: MS-PL */
#include "CnaRuntimeSmoke.h"
#define SV(text) ((CNA_StringView){text, sizeof(text)-1})
int cna_test_window_begin(void);
int cna_test_window_end(void);
int cna_test_window_absent(int absent);
int cna_test_window_owner(void);
int cna_test_window_worker(int (*test)(void));
int cna_test_window_native(const char* text, const uint8_t* bytes, uint64_t count);
int cna_test_drop_native(int files, const CNA_StringView* items, uint64_t count);
static CNA_Handle game;
static CNA_Handle text_registration;
static CNA_Handle file_registration;
static CNA_Handle cancelled_registration;
static int cancelled_calls;
static void on_cancelled(void* context, const CNA_StringView* items, uint64_t count)
{
    (void)context; (void)items; (void)count; ++cancelled_calls;
}
static int callback_ok = 1, text_calls, file_calls;
static void on_text(void* context, const CNA_StringView* items, uint64_t count)
{
    CNA_Rectangle bounds;
    ++text_calls;
    callback_ok &= context == &text_calls && cna_test_window_owner() && cna_test_drop_native(0, items, count);
    callback_ok &= cna_game_window_get_client_bounds(game, &bounds) == CNA_RESULT_SUCCESS;
    callback_ok &= cna_game_destroy(game) == CNA_RESULT_INVALID_STATE;
}
static void on_file(void* context, const CNA_StringView* items, uint64_t count)
{
    ++file_calls;
    callback_ok &= context == &file_calls && cna_test_window_owner() && cna_test_drop_native(1, items, count);
    /* Self removal must suppress the next dispatch; querying and nested text dispatch are allowed. */
    callback_ok &= cna_game_unsubscribe(file_registration) == CNA_RESULT_SUCCESS;
    callback_ok &= cna_game_unsubscribe(cancelled_registration) == CNA_RESULT_SUCCESS;
    cancelled_registration = 0;
    callback_ok &= cna_game_window_raise_text_drop_ext(game, SV("drop-\xc3\xa9")) == CNA_RESULT_SUCCESS;
}
typedef struct ProviderState {
    int alive, calls, ok, mode;
    int32_t expected_width, expected_height, width, height;
    struct ProviderState* next;
} ProviderState;
static ProviderState final_provider;
static char provider_diagnostic[] = "logical provider failed";
static CNA_Result logical_size(CNA_Handle source, void* context, int32_t* width,
    int32_t* height, CNA_Bool* available, CNA_CallbackError* error)
{
    ProviderState* state = (ProviderState*)context;
    CNA_Rectangle bounds;
    ++state->calls;
    state->ok &= state->alive && source == game && cna_test_window_owner();
    state->ok &= *width == state->expected_width && *height == state->expected_height;
    state->ok &= *available == CNA_FALSE && error->struct_size == sizeof(*error) && error->struct_version == 1;
    state->ok &= cna_game_destroy(source) == CNA_RESULT_INVALID_STATE;
    state->ok &= cna_game_run_one_frame(source) == CNA_RESULT_INVALID_STATE;
    state->ok &= cna_game_window_get_client_bounds(source, &bounds) == CNA_RESULT_SUCCESS;
    *width = state->width;
    *height = state->height;
    *available = state->mode == 1 ? CNA_FALSE : state->mode == 6 ? (CNA_Bool)9 : CNA_TRUE;
    if (state->mode == 3)
        state->ok &= cna_game_window_set_logical_size_provider_ext(source, logical_size, state->next) == CNA_RESULT_SUCCESS;
    if (state->mode == 4)
        state->ok &= cna_game_window_set_logical_size_provider_ext(source, NULL, NULL) == CNA_RESULT_SUCCESS;
    if (state->mode == 5) {
        error->message = SV(provider_diagnostic);
        return CNA_RESULT_IO;
    }
    return CNA_RESULT_SUCCESS;
}
static int provider_worker(void)
{
    return cna_game_window_set_logical_size_provider_ext(game, logical_size, NULL) == CNA_RESULT_THREAD;
}
static int refresh_provider(void)
{
    return cna_game_window_begin_screen_device_change(game, CNA_FALSE) == CNA_RESULT_SUCCESS &&
        cna_game_window_end_screen_device_change(game, SV(""), 0, 0) == CNA_RESULT_SUCCESS;
}
static int validate_provider(void)
{
    ProviderState first = {1, 0, 1, 0, 0, 0, 480, 800, NULL};
    CNA_Rectangle bounds;
    CNA_DisplayOrientation orientation = 0;
    if (cna_game_window_get_client_bounds(game, &bounds) != CNA_RESULT_SUCCESS) return 0;
    if (cna_game_set_foreign_thread_calls_ext(game, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        !cna_test_window_worker(provider_worker) ||
        cna_game_set_foreign_thread_calls_ext(game, CNA_FALSE) != CNA_RESULT_SUCCESS) return 0;
    first.expected_width = bounds.width;
    first.expected_height = bounds.height;
    if (cna_game_window_set_logical_size_provider_ext(0, logical_size, &first) != CNA_RESULT_INVALID_HANDLE ||
        cna_game_window_set_logical_size_provider_ext(game, NULL, &first) != CNA_RESULT_SUCCESS ||
        cna_game_window_set_logical_size_provider_ext(game, logical_size, &first) != CNA_RESULT_SUCCESS || first.calls != 0 ||
        !refresh_provider() || first.calls != 1 || !first.ok ||
        cna_game_window_get_current_orientation(game, &orientation) != CNA_RESULT_SUCCESS || orientation != CNA_DISPLAY_ORIENTATION_PORTRAIT) return 0;
    first.expected_width = 480; first.expected_height = 800;
    first.mode = 1; first.width = 900; first.height = 100;
    if (!refresh_provider() || first.calls != 2 || !first.ok ||
        cna_game_window_get_current_orientation(game, &orientation) != CNA_RESULT_SUCCESS || orientation != CNA_DISPLAY_ORIENTATION_PORTRAIT) return 0;
    first.mode = 0; first.width = 0;
    if (!refresh_provider() || first.calls != 3 || !first.ok ||
        cna_game_window_get_current_orientation(game, &orientation) != CNA_RESULT_SUCCESS || orientation != CNA_DISPLAY_ORIENTATION_PORTRAIT) return 0;
    first.width = 900; first.height = -1;
    if (!refresh_provider() || first.calls != 4 || !first.ok) return 0;
    first.mode = 3; first.width = 480; first.height = 800;
    final_provider = (ProviderState){1, 0, 1, 0, 480, 800, 800, 480, NULL};
    first.next = &final_provider;
    if (!refresh_provider() || first.calls != 5 || final_provider.calls != 0 || !first.ok) return 0;
    first.alive = 0; /* The replaced context can now be reclaimed; only the new one is called. */
    if (!refresh_provider() || first.calls != 5 || final_provider.calls != 1 || !final_provider.ok ||
        cna_game_window_get_current_orientation(game, &orientation) != CNA_RESULT_SUCCESS || orientation != CNA_DISPLAY_ORIENTATION_LANDSCAPE_LEFT) return 0;
    final_provider.expected_width = 800; final_provider.expected_height = 480;
    final_provider.mode = 4;
    if (!refresh_provider() || final_provider.calls != 2 || !final_provider.ok ||
        !refresh_provider() || final_provider.calls != 2) return 0;
    final_provider.mode = 0;
    if (cna_game_window_set_logical_size_provider_ext(game, logical_size, &final_provider) != CNA_RESULT_SUCCESS ||
        !refresh_provider() || final_provider.calls != 3 || !final_provider.ok ||
        cna_game_window_set_logical_size_provider_ext(game, NULL, NULL) != CNA_RESULT_SUCCESS ||
        !refresh_provider() || final_provider.calls != 3) return 0;
    /* Remains live through game destruction; source destruction must retire it. */
    return cna_game_window_set_logical_size_provider_ext(game, logical_size, &final_provider) == CNA_RESULT_SUCCESS;
}
static int provider_failure(int mode)
{
    ProviderState state = {1, 0, 1, 0, 0, 0, 480, 800, NULL};
    CNA_ErrorInfo error = {sizeof(error), 1, 0, 0, 0};
    char diagnostic[128]; uint64_t bytes = 0;
    CNA_Rectangle initial;
    const CNA_GameCreateInfo info = {sizeof(info), 1, CNA_TRUE, {0}, 166667, {NULL, 0}, NULL};
    state.mode = mode;
    if (cna_game_create(&info, &game) != CNA_RESULT_SUCCESS ||
        cna_test_window_begin() != 1 ||
        cna_game_window_get_client_bounds(game, &initial) != CNA_RESULT_SUCCESS) return 0;
    state.expected_width = initial.width; state.expected_height = initial.height;
    if (cna_game_window_set_logical_size_provider_ext(game, logical_size, &state) != CNA_RESULT_SUCCESS ||
        cna_game_window_begin_screen_device_change(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_game_window_end_screen_device_change(game, SV(""), 0, 0) != CNA_RESULT_CALLBACK ||
        state.calls != 1 || !state.ok || cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS ||
        error.result != CNA_RESULT_CALLBACK || error.category != CNA_ERROR_CATEGORY_CALLBACK ||
        cna_error_copy_last_message(diagnostic, sizeof(diagnostic), &bytes) != CNA_RESULT_SUCCESS) return 0;
    if (mode == 5 && (bytes != sizeof("logical provider failed")-1 || memcmp(diagnostic, "logical provider failed", (size_t)bytes))) return 0;
    if (mode == 6 && (bytes != sizeof("A logical-size callback returned a noncanonical boolean.")-1 ||
        memcmp(diagnostic, "A logical-size callback returned a noncanonical boolean.", (size_t)bytes))) return 0;
    /* The failure diagnostic must have been copied before the caller reuses its storage. */
    if (mode == 5) memset(provider_diagnostic, '?', sizeof(provider_diagnostic) - 1U);
    /* Oracle: CLOSURE_060 first failure is latched, callbacks stop, failed bounds
     * queries preserve caller output. Replacing the provider cannot clear that failure. */
    const CNA_Rectangle sentinel = {17, 19, -77, -88};
    CNA_Rectangle refused = sentinel;
    if (cna_game_window_get_client_bounds(game, &refused) != CNA_RESULT_CALLBACK ||
        memcmp(&refused, &sentinel, sizeof(refused)) != 0 || state.calls != 1 ||
        cna_game_window_set_logical_size_provider_ext(game, logical_size, &state) != CNA_RESULT_SUCCESS ||
        /* Refresh WindowsGameForm.EndScreenDeviceChange IL_0109..0115 consumed the first Begin:
         * the C provider latched its failure without throwing through the native End call. */
        cna_game_window_begin_screen_device_change(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_game_window_end_screen_device_change(game, SV(""), 0, 0) != CNA_RESULT_CALLBACK ||
        state.calls != 1) return 0;
    if (mode == 5 && (cna_error_copy_last_message(diagnostic, sizeof(diagnostic), &bytes) != CNA_RESULT_SUCCESS ||
        bytes != sizeof("logical provider failed") - 1U ||
        memcmp(diagnostic, "logical provider failed", (size_t)bytes) != 0)) return 0;
    if (cna_game_window_set_logical_size_provider_ext(game, NULL, NULL) != CNA_RESULT_SUCCESS ||
        cna_test_window_end() != 1 || cna_game_destroy(game) != CNA_RESULT_CALLBACK ||
        cna_game_window_set_logical_size_provider_ext(game, logical_size, &state) != CNA_RESULT_INVALID_HANDLE) return 0;
    state.alive = 0;
    return state.calls == 1;
}
static int worker(void)
{
    uint64_t count = 99;
    CNA_Handle registration = 99;
    return cna_game_window_set_logical_size_provider_ext(game, logical_size, NULL) == CNA_RESULT_THREAD &&
        cna_clipboard_get_mime_type_count_ext(game, &count) == CNA_RESULT_THREAD && count == 99 &&
        cna_game_window_subscribe_text_drop_ext(game, on_text, NULL, &registration) == CNA_RESULT_THREAD && registration == 0 &&
        cna_game_unsubscribe(text_registration) == CNA_RESULT_THREAD &&
        cna_game_window_raise_text_drop_ext(game, SV("drop-\xc3\xa9")) == CNA_RESULT_THREAD;
}
int main(void)
{
    CnaRuntimeSmoke smoke = {0};
    int status = 1;
    CNA_Handle component = 0, after_death = 0;
    const CNA_GameCreateInfo info = {sizeof(info), 1, CNA_TRUE, {0}, 166667, {NULL, 0}, NULL};
    const uint8_t bytes[] = {0, 255, 127, 0};
    const CNA_ClipboardOffer formats[] = {{SV("application/x-cna"), bytes, sizeof(bytes)}, {SV("text/plain"), (const uint8_t*)"hello", 5}};
    const CNA_StringView files[] = {SV("first"), SV("second")};
    char text[96]; uint8_t copy[8]; uint64_t count = 99; CNA_Bool accepted = 99;
    RUNTIME_CHECK(SMOKE_OWN(cna_game_create(&info, &game), &game, cna_game_destroy) == CNA_RESULT_SUCCESS);
    /* Game creation installs its own ambient platform; override it only afterwards. */
    RUNTIME_CHECK(SMOKE_VALUE(cna_test_window_begin()) == 1);
    RUNTIME_CHECK(SMOKE_OWN(cna_gamer_services_component_create(game, &component), &component, cna_game_component_destroy) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_component_get_type_name_size(component, &count)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_component_copy_type_name(component, text, sizeof(text), &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == strlen("Microsoft.Xna.Framework.GamerServices.GamerServicesComponent") && memcmp(text, "Microsoft.Xna.Framework.GamerServices.GamerServicesComponent", (size_t)count) == 0);
    RUNTIME_CHECK(SMOKE_DROP(cna_game_component_destroy(component), component) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_OWN(cna_game_window_subscribe_text_drop_ext(game, on_text, &text_calls, &text_registration), &text_registration, cna_game_unsubscribe) == CNA_RESULT_SUCCESS);
    /* Self-unsubscribed registration is released by the callback, not the cleanup stack. */
    RUNTIME_CHECK(SMOKE_API(cna_game_window_subscribe_file_drop_ext(game, on_file, &file_calls, &file_registration)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_subscribe_file_drop_ext(game, on_cancelled, NULL, &cancelled_registration)) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_file_drop_ext(game, files, 2)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(file_calls) == 1 && SMOKE_VALUE(text_calls) == 1 && SMOKE_VALUE(cancelled_calls) == 0 && callback_ok);
    file_registration = 0;
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_file_drop_ext(game, files, 2)) == CNA_RESULT_SUCCESS && file_calls == 1);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_text_drop_ext(game, SV("\xff"))) == CNA_RESULT_ENCODING && text_calls == 1);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_file_drop_ext(game, files, UINT64_MAX)) == CNA_RESULT_OVERFLOW && file_calls == 1);
    RUNTIME_CHECK(cna_test_window_worker(worker));
    RUNTIME_CHECK(validate_provider());
    RUNTIME_CHECK(SMOKE_DROP(cna_game_unsubscribe(text_registration), text_registration) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_text_drop_ext(game, SV("drop-\xc3\xa9"))) == CNA_RESULT_SUCCESS && text_calls == 1);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_subscribe_text_drop_ext(game, NULL, NULL, &after_death)) == CNA_RESULT_INVALID_ARGUMENT && after_death == 0);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_formats_ext(game, formats, 2, &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_mime_type_count_ext(game, &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == 2);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_mime_type_size_at_ext(game, 0, &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == strlen("application/x-cna"));
    memset(text, '!', sizeof(text));
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_mime_type_at_ext(game, 0, text, 1, &count)) == CNA_RESULT_BUFFER_TOO_SMALL && text[0] == '!');
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_mime_type_at_ext(game, 0, text, sizeof(text), &count)) == CNA_RESULT_SUCCESS && memcmp(text, "application/x-cna", (size_t)count) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_mime_type_size_at_ext(game, 2, &count)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_has_data_ext(game, SV("application/x-cna"), &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_data_size_ext(game, SV("application/x-cna"), &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == sizeof(bytes));
    memset(copy, 99, sizeof(copy));
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_data_ext(game, SV("application/x-cna"), copy, 1, &count)) == CNA_RESULT_BUFFER_TOO_SMALL && copy[0] == 99);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_data_ext(game, SV("application/x-cna"), copy, sizeof(copy), &count)) == CNA_RESULT_SUCCESS && memcmp(copy, bytes, sizeof(bytes)) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_primary_selection_text_ext(game, SV("selected"))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_primary_selection_text_size_ext(game, &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == 8);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_primary_selection_text_ext(game, text, sizeof(text), &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == 8 && memcmp(text, "selected", 8) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_has_primary_selection_text_ext(game, &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_TRUE);
    RUNTIME_CHECK(cna_test_window_native("selected", bytes, sizeof(bytes)));
    accepted = 99;
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(0, SV("a"), bytes, sizeof(bytes), NULL)) == CNA_RESULT_INVALID_ARGUMENT);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(0, SV("a"), bytes, sizeof(bytes), &accepted)) == CNA_RESULT_INVALID_HANDLE && SMOKE_VALUE(accepted) == 99);
    {
        const CNA_ClipboardOffer duplicate[] = {formats[0], formats[0]};
        RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_formats_ext(game, duplicate, 2, &accepted)) == CNA_RESULT_INVALID_ARGUMENT && SMOKE_VALUE(accepted) == 99);
    }
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(game, SV("\xff"), bytes, sizeof(bytes), &accepted)) == CNA_RESULT_ENCODING && SMOKE_VALUE(accepted) == 99);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(game, SV("a"), NULL, 1, &accepted)) == CNA_RESULT_INVALID_ARGUMENT && SMOKE_VALUE(accepted) == 99);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_formats_ext(game, formats, UINT64_MAX, &accepted)) == CNA_RESULT_OVERFLOW && SMOKE_VALUE(accepted) == 99);
    RUNTIME_CHECK(cna_test_window_native("selected", bytes, sizeof(bytes)));
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(game, SV("application/x-cna"), bytes, sizeof(bytes), &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_TRUE);
    RUNTIME_CHECK(SMOKE_VALUE(cna_test_window_absent(1)) == 1);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_data_ext(game, SV("application/x-cna"), bytes, sizeof(bytes), &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_set_primary_selection_text_ext(game, SV("ignored"))) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_has_primary_selection_text_ext(game, &accepted)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(accepted) == CNA_FALSE);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_get_mime_type_count_ext(game, &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == 0);
    RUNTIME_CHECK(SMOKE_API(cna_clipboard_copy_data_ext(game, SV("missing"), NULL, 0, &count)) == CNA_RESULT_SUCCESS && SMOKE_VALUE(count) == 0);
    RUNTIME_CHECK(SMOKE_VALUE(cna_test_window_absent(0)) == 1);
    RUNTIME_CHECK(cna_test_window_native("selected", bytes, sizeof(bytes)));
    RUNTIME_CHECK(SMOKE_OWN(cna_game_window_subscribe_text_drop_ext(game, on_text, &text_calls, &after_death), &after_death, cna_game_unsubscribe) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_VALUE(cna_test_window_end()) == 1);
    RUNTIME_CHECK(SMOKE_DROP(cna_game_destroy(game), game) == CNA_RESULT_SUCCESS);
    RUNTIME_CHECK(SMOKE_API(cna_game_window_raise_text_drop_ext(game, SV("drop-\xc3\xa9"))) == CNA_RESULT_INVALID_HANDLE && text_calls == 1);
    RUNTIME_CHECK(SMOKE_DROP(cna_game_unsubscribe(after_death), after_death) == CNA_RESULT_SUCCESS);
    final_provider.alive = 0;
    RUNTIME_CHECK(final_provider.calls == 3);
    RUNTIME_CHECK(provider_failure(5));
    RUNTIME_CHECK(provider_failure(6));
    status = 0;
cleanup:
    if (cancelled_registration) (void)cna_game_unsubscribe(cancelled_registration);
    if (file_registration) { CNA_Result result = cna_game_unsubscribe(file_registration); if (result != CNA_RESULT_SUCCESS && result != CNA_RESULT_INVALID_HANDLE) status = 1; }
    /* Early exits also restore the ambient platform before releasing the game. */
    if (!cna_test_window_end()) {
        fprintf(stderr, "fixture cleanup failed to restore the game platform\n");
        status = 1;
    }
    status |= cna_smoke_cleanup(&smoke);
    puts(status ? "ServiceWindowSmoke: FAIL" : "ServiceWindowSmoke: PASS (injected clipboard, drop lifecycle)");
    return status;
}
