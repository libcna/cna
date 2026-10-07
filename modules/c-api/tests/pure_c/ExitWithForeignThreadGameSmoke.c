// SPDX-License-Identifier: MS-PL

/*
 * BINDFIX-041 (JAVA-UPSTREAM-014): a process may exit with a game still alive, and the thread that
 * created it may have ended first -- the `java` launcher runs main on a thread of its own, so every
 * JVM does exactly this. On the GL renderers the exit used to destroy the game from the exiting
 * thread, whose GL function table is empty, and the process died in std::terminate. Leaving the
 * graph to the operating system is the only correct answer; the test is that this exits zero.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>
#include "CnaTestThreads.h"

static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    int* const created = (int*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_VertexElement element;
    CNA_VertexDeclarationHandle declaration = CNA_INVALID_HANDLE;
    CNA_VertexBufferCreateInfo create_info;
    CNA_VertexBufferHandle buffer = CNA_INVALID_HANDLE;
    (void)game_time;
    (void)out_error;
    if (*created != 0) {
        return CNA_RESULT_SUCCESS;
    }
    memset(&element, 0, sizeof(element));
    element.format = CNA_VERTEX_ELEMENT_FORMAT_VECTOR3;
    element.usage = CNA_VERTEX_ELEMENT_USAGE_POSITION;
    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.vertex_count = 3;
    create_info.buffer_usage = CNA_BUFFER_USAGE_WRITE_ONLY;
    create_info.dynamic = CNA_TRUE;
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_vertex_declaration_create(&element, 1U, &declaration) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    create_info.vertex_declaration = declaration;
    /* Deliberately never destroyed, and neither is anything else. */
    *created = cna_vertex_buffer_create(device, &create_info, &buffer) == CNA_RESULT_SUCCESS
        ? 1 : -1;
    return CNA_RESULT_SUCCESS;
}

static int owner(void* const argument)
{
    int* const created = (int*)argument;
    const CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks), UINT32_C(1), 0, on_update, 0, 0, 0, created
    };
    static const char Title[] = "C API exit with a foreign-thread game";
    const CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo), UINT32_C(1), CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U}, INT64_C(166667),
        {Title, sizeof(Title) - 1U}, &callbacks
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    return CNA_TEST_STAGE(cna_game_create(&create_info, &game) == CNA_RESULT_SUCCESS) &&
        CNA_TEST_STAGE(cna_game_run_one_frame(game) == CNA_RESULT_SUCCESS);
}

int main(void)
{
    int created = 0;
    int succeeded = 0;
    thrd_t thread;
    if (thrd_create(&thread, owner, &created) != thrd_success ||
        thrd_join(thread, &succeeded) != thrd_success ||
        !CNA_TEST_STAGE(succeeded == 1) ||
        !CNA_TEST_STAGE(created == 1)) {
        return CNA_TEST_FAIL(1);
    }
    /* The owning thread has ended; the game and its buffer are still alive. */
    return 0;
}
