/* SPDX-License-Identifier: MS-PL */
#include "CnaTestScratch.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    if (!cna_test_scratch_enter()) return 2;
    /* Oracle: TitleLocation base directories end in a separator (ContentSmoke contract). */
    const char* root = cna_test_scratch_root();
    const size_t size = strlen(root);
    if (size == 0 || (root[size - 1] != '/' && root[size - 1] != '\\')) return 6;
    FILE* file = fopen("same-name.bin", "wb");
    if (!file) return 3;
    if (fwrite("scratch-oracle", 1, 14, file) != 14) { fclose(file); return 4; }
    if (fclose(file) != 0) return 5;
    puts(cna_test_scratch_root());
    fflush(stdout);
    return getchar() == 'f' ? 23 : 0;
}
