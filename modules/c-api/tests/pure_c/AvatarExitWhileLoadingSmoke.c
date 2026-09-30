// SPDX-License-Identifier: MS-PL

/*
 * BINDFIX-046 (RUST-UPSTREAM-031): a process may end while the shared avatar loader thread is still
 * assembling models. The first completed load built function-local lookup tables after the loader
 * itself, so exit() destroyed them first and the still-running worker read a destroyed map: a
 * segfault in most runs, an endless walk over freed nodes in others. Loading one avatar to
 * completion and then returning with several more queued must simply exit zero.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <threads.h>

static int queue_avatar(CNA_AvatarRendererHandle* const out_renderer)
{
    CNA_AvatarDescriptionHandle description = CNA_INVALID_HANDLE;
    const int created =
        cna_avatar_description_create_random(&description) == CNA_RESULT_SUCCESS &&
        cna_avatar_renderer_create(description, CNA_TRUE, out_renderer) == CNA_RESULT_SUCCESS;
    if (description != CNA_INVALID_HANDLE) {
        (void)cna_avatar_description_destroy(description);
    }
    return created;
}

int main(void)
{
    CNA_AvatarRendererHandle first = CNA_INVALID_HANDLE;
    CNA_AvatarRendererInfo info = {sizeof(CNA_AvatarRendererInfo), UINT32_C(1), 0, 0, {0, 0, 0}};
    if (!CNA_TEST_STAGE(queue_avatar(&first))) {
        return CNA_TEST_FAIL(1);
    }
    for (int attempt = 0; attempt < 1000; ++attempt) {
        if (cna_avatar_renderer_get_info(first, &info) != CNA_RESULT_SUCCESS ||
            info.state != CNA_AVATAR_RENDERER_STATE_LOADING) {
            break;
        }
        const struct timespec pause = {0, 10000000};
        (void)thrd_sleep(&pause, 0);
    }
    if (!CNA_TEST_STAGE(info.state == CNA_AVATAR_RENDERER_STATE_READY)) {
        return CNA_TEST_FAIL(2);
    }
    for (int index = 0; index < 4; ++index) {
        CNA_AvatarRendererHandle queued = CNA_INVALID_HANDLE;
        if (!CNA_TEST_STAGE(queue_avatar(&queued))) {
            return CNA_TEST_FAIL(3);
        }
    }
    /* Deliberately returns with four loads in flight and nothing destroyed. */
    return 0;
}
