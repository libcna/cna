// SPDX-License-Identifier: MS-PL

/*
 * BINDFIX-050 (RUST-UPSTREAM-023): cna_graphics_device_create documents no thread affinity, so
 * several threads may each build and release a device at the same time. On the windowed renderers
 * that corrupted the heap or stalled: the preset state objects every device copies shared one
 * unguarded identity list, and window, GL-context and camera-subsystem work ran outside the lock
 * SDL's process-global state needs. Every thread must get the same answer and the process must end
 * normally. The race is probabilistic, so the work is repeated.
 */

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include "CnaTestThreads.h"

enum { ThreadCount = 6, Rounds = 4 };

static int attempt(void* const argument)
{
    CNA_Result* const result = (CNA_Result*)argument;
    CNA_PresentationParameters parameters;
    CNA_Handle device = CNA_INVALID_HANDLE;
    if (cna_presentation_parameters_init(&parameters) != CNA_RESULT_SUCCESS) {
        *result = CNA_RESULT_INTERNAL;
        return 0;
    }
    parameters.back_buffer_width = 64;
    parameters.back_buffer_height = 64;
    *result = cna_graphics_device_create(0U, CNA_GRAPHICS_PROFILE_HI_DEF, &parameters, &device);
    if (*result == CNA_RESULT_SUCCESS && cna_graphics_device_destroy(device) != CNA_RESULT_SUCCESS) {
        *result = CNA_RESULT_INTERNAL;
    }
    return 0;
}

int main(void)
{
    for (int round = 0; round < Rounds; ++round) {
        thrd_t threads[ThreadCount];
        CNA_Result results[ThreadCount];
        for (int index = 0; index < ThreadCount; ++index) {
            results[index] = CNA_RESULT_INTERNAL;
            if (thrd_create(&threads[index], attempt, &results[index]) != thrd_success) {
                return CNA_TEST_FAIL(1);
            }
        }
        for (int index = 0; index < ThreadCount; ++index) {
            (void)thrd_join(threads[index], 0);
        }
        /* A renderer that cannot supply a device refuses every thread alike; none may differ. */
        for (int index = 1; index < ThreadCount; ++index) {
            if (!CNA_TEST_STAGE(results[index] == results[0])) {
                return CNA_TEST_FAIL(2);
            }
        }
        if (!CNA_TEST_STAGE(results[0] != CNA_RESULT_INTERNAL)) {
            return CNA_TEST_FAIL(3);
        }
    }
    return 0;
}
