// SPDX-License-Identifier: MS-PL
#include <CNA/C/cnb.h>
#include <CNA/C/core_ext.h>

#include <stddef.h>

int main(void)
{
    static const uint8_t garbage[] = {1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    static const char origin[] = "garbage";
    CNA_StringView origin_view;
    CNA_CnbDocumentHandle document = UINT64_MAX;
    CNA_LogLevel level = CNA_LOG_LEVEL_DEBUG;
    uint64_t type_bytes = 0U;
    if (cna_get_abi_version() != CNA_ABI_VERSION) return 1;
    if (cna_logger_set_minimum_level(CNA_LOG_LEVEL_WARN) != CNA_RESULT_SUCCESS) return 2;
    if (cna_logger_get_minimum_level(&level) != CNA_RESULT_SUCCESS) return 3;
    if (level != CNA_LOG_LEVEL_WARN) return 4;
    // The parser throws for bytes that are not a container, and the library's exception barrier
    // must catch it: the program links the C++ runtime and exception ABI the library needs.
    origin_view.data = origin;
    origin_view.byte_length = sizeof(origin) - 1U;
    if (cna_cnb_document_parse(garbage, sizeof(garbage), origin_view, NULL, &document) !=
        CNA_RESULT_IO) return 5;
    if (document != CNA_INVALID_HANDLE) return 6;
    if (cna_error_get_last_exception_type_size_ext(&type_bytes) != CNA_RESULT_SUCCESS ||
        type_bytes == 0U) return 7;
    return 0;
}
