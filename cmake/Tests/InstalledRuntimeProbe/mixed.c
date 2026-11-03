// SPDX-License-Identifier: MS-PL
#include <CNA/C/core_ext.h>
#include <string.h>

int installed_c_check_state(void)
{
    char buffer[64] = {0};
    uint64_t count = 0;
    CNA_LogLevel level = CNA_LOG_LEVEL_FATAL;
    if (cna_assembly_copy_title_ext(buffer, sizeof(buffer), &count) != CNA_RESULT_SUCCESS ||
        count != strlen("installed C++ runtime") ||
        memcmp(buffer, "installed C++ runtime", (size_t)count) != 0)
        return 1;
    return cna_logger_get_minimum_level(&level) == CNA_RESULT_SUCCESS &&
        level == CNA_LOG_LEVEL_WARN ? 0 : 2;
}

int installed_c_set_level(void)
{
    return cna_logger_set_minimum_level(CNA_LOG_LEVEL_FATAL) == CNA_RESULT_SUCCESS ? 0 : 1;
}

int installed_c_check_level(void)
{
    CNA_LogLevel level = CNA_LOG_LEVEL_WARN;
#ifdef CNA_PACKAGE_TEST_STATIC_ISOLATION
    const CNA_LogLevel expected = CNA_LOG_LEVEL_FATAL;
#else
    const CNA_LogLevel expected = CNA_LOG_LEVEL_DEBUG;
#endif
    return cna_logger_get_minimum_level(&level) == CNA_RESULT_SUCCESS && level == expected ? 0 : 1;
}
