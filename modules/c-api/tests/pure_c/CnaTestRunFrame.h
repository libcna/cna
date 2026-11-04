// SPDX-License-Identifier: MS-PL
#ifndef CNA_TEST_RUN_FRAME_H
#define CNA_TEST_RUN_FRAME_H
#include <CNA/C/cna.h>
/* XNA WindowsGameHost.RunOneFrame IL_0000-0028 only ticks. Tests needing
   initialization use the published host-run extension and retain all their assertions. */
static inline CNA_Result cna_test_run_initialized_frame(CNA_Handle game)
{
    CNA_Bool running = CNA_FALSE;
    return cna_game_run_frame_ext(game, &running);
}
#endif
