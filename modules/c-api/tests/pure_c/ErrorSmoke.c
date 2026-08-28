// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

int main(void)
{
    CNA_ErrorInfo error_info = {
        sizeof(CNA_ErrorInfo),
        UINT32_C(1),
        UINT32_C(99),
        UINT32_C(99),
        UINT64_C(99)
    };
    uint64_t message_bytes = UINT64_C(99);

    if (cna_error_get_last_info(&error_info) != CNA_RESULT_SUCCESS ||
        error_info.result != CNA_RESULT_SUCCESS ||
        error_info.category != CNA_ERROR_CATEGORY_NONE ||
        error_info.message_byte_length != 0U) {
        return CNA_TEST_FAIL(1);
    }

    if (cna_error_get_last_message_size(&message_bytes) != CNA_RESULT_SUCCESS ||
        message_bytes != 0U) {
        return CNA_TEST_FAIL(2);
    }

    if (cna_error_copy_last_message(0, 0U, &message_bytes) != CNA_RESULT_SUCCESS ||
        message_bytes != 0U) {
        return CNA_TEST_FAIL(3);
    }

    if (cna_error_get_last_info(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_TEST_FAIL(4);
    }

    if (cna_error_get_last_info(&error_info) != CNA_RESULT_SUCCESS ||
        error_info.result != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(5);
    }

    return 0;
}
