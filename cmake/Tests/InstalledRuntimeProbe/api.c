// SPDX-License-Identifier: MS-PL
#include <CNA/C/core_ext.h>

int main(void)
{
    CNA_LogLevel level = CNA_LOG_LEVEL_DEBUG;
    if (cna_get_abi_version() != CNA_ABI_VERSION) return 1;
    if (cna_logger_set_minimum_level(CNA_LOG_LEVEL_WARN) != CNA_RESULT_SUCCESS) return 2;
    if (cna_logger_get_minimum_level(&level) != CNA_RESULT_SUCCESS) return 3;
    return level == CNA_LOG_LEVEL_WARN ? 0 : 4;
}
