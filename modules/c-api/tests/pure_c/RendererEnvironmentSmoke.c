// SPDX-License-Identifier: MS-PL

/*
 * Run with CNA_GRAPHICS_RENDERER naming a renderer this build refuses. Loading the library used to
 * throw from a static initializer, so every host process -- a JVM, a Python interpreter, Node --
 * died in std::terminate before its first call. The refusal belongs to the first call that needs a
 * renderer, and it must stay a refusal: a second attempt may not quietly take the default instead.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

static int last_error_names_the_variable(void)
{
    char message[512];
    uint64_t size = 0U;
    uint64_t written = 0U;
    if (cna_error_get_last_message_size(&size) != CNA_RESULT_SUCCESS || size == 0U ||
        size >= sizeof(message) ||
        cna_error_copy_last_message(message, sizeof(message), &written) != CNA_RESULT_SUCCESS ||
        written != size) {
        return 0;
    }
    message[written] = '\0';
    return strstr(message, "CNA_GRAPHICS_RENDERER") != NULL ||
        strstr(message, "not compiled into this build") != NULL;
}

static int refuses_the_selection(void)
{
    CNA_GraphicsRendererType selected = CNA_GRAPHICS_RENDERER_UNKNOWN;
    return cna_graphics_renderer_get_selected_ext(&selected) == CNA_RESULT_INVALID_STATE &&
        last_error_names_the_variable();
}

static int refuses_a_game(void)
{
    static const char Title[] = "C API renderer environment";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, 0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    const CNA_Result created = cna_game_create(&create_info, &game);
    if (created == CNA_RESULT_SUCCESS) {
        const CNA_Result frame = cna_game_run_one_frame(game);
        const int named = last_error_names_the_variable();
        (void)cna_game_destroy(game);
        return frame != CNA_RESULT_SUCCESS && named;
    }
    return game == CNA_INVALID_HANDLE && last_error_names_the_variable();
}

int main(void)
{
    if (!CNA_TEST_STAGE(cna_get_abi_version() == CNA_ABI_VERSION)) {
        return CNA_TEST_FAIL(1);
    }
    if (!CNA_TEST_STAGE(refuses_the_selection())) {
        return CNA_TEST_FAIL(2);
    }
    if (!CNA_TEST_STAGE(refuses_a_game())) {
        return CNA_TEST_FAIL(3);
    }
    if (!CNA_TEST_STAGE(refuses_a_game())) {
        return CNA_TEST_FAIL(4);
    }
    return CNA_TEST_STAGE(refuses_the_selection()) ? 0 : CNA_TEST_FAIL(5);
}
