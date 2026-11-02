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
static int worker(void)
{
    uint64_t count = 99;
    CNA_Handle registration = 99;
    return cna_clipboard_get_mime_type_count_ext(game, &count) == CNA_RESULT_THREAD && count == 99 &&
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
