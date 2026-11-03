/* SPDX-License-Identifier: MS-PL */
#ifndef CNA_C_API_RECON_SMOKE_H
#define CNA_C_API_RECON_SMOKE_H

#include "CnaRuntimeSmoke.h"

/* Each witness owns a smoke tracker, a failed flag and a cleanup label. Operands
 * are evaluated once and retained so failure reports include actual values. */
#define REC_EQ(actual, expected) do { \
    const uint64_t rec_actual = (uint64_t)(actual); \
    const uint64_t rec_expected = (uint64_t)(expected); \
    if (rec_actual != rec_expected) { \
        cna_smoke_value(&smoke, #actual, rec_actual); \
        cna_smoke_value(&smoke, #expected, rec_expected); \
        cna_smoke_failure(&smoke, __FILE__, __LINE__, __func__, #actual " == " #expected); \
        failed = 1; goto cleanup; \
    } \
} while (0)

#define REC_FLOAT(actual, expected) do { \
    const double rec_actual = (double)(actual); \
    const double rec_expected = (double)(expected); \
    if (!(rec_actual == rec_expected)) { \
        fprintf(stderr, "%s:%d: %s = %.17g; expected %.17g\n", \
                __FILE__, __LINE__, #actual, rec_actual, rec_expected); \
        failed = 1; goto cleanup; \
    } \
} while (0)

#define REC_CREATED(call, handle, destroy, expected) \
    REC_EQ(cna_smoke_publish(&smoke, #call, (call), &(handle), destroy, NULL, NULL), expected)
#define REC_OWN(call, handle, destroy) \
    REC_CREATED(call, handle, destroy, CNA_RESULT_SUCCESS)
#define REC_DROP(call, handle) \
    REC_EQ(cna_smoke_drop(&smoke, #call, (call), (handle)), CNA_RESULT_SUCCESS)

#endif
