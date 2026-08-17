// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

int main(void)
{
    return cna_get_abi_version() == CNA_ABI_VERSION ? 0 : 1;
}
