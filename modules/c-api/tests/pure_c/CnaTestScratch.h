/* SPDX-License-Identifier: MS-PL */
#ifndef CNA_TEST_SCRATCH_H
#define CNA_TEST_SCRATCH_H
#ifdef __cplusplus
extern "C" {
#endif
/* Test-only bridge to the shared header-only owner. Fail before any fixture write. */
int cna_test_scratch_enter(void);
/* UTF-8 directory path with a trailing separator, matching TitleLocation base paths. */
const char* cna_test_scratch_root(void);
const char* cna_test_scratch_music(void);
const char* cna_test_scratch_pictures(void);
#ifdef __cplusplus
}
#endif
#endif
