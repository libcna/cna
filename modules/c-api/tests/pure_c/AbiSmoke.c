// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

int main(void)
{
    return CNA_TEST_STAGE(cna_get_abi_version() == CNA_ABI_VERSION) ? 0 : 1;
}
