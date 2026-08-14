// SPDX-License-Identifier: MS-PL

#include "CNA/C/core.h"
#include "CnaCApiDetail.hpp"

#include <cstring>

namespace {

[[nodiscard]] bool IsSupportedErrorInfo(const CNA_ErrorInfo* const errorInfo)
{
    return errorInfo != nullptr &&
        errorInfo->struct_size >= sizeof(CNA_ErrorInfo) &&
        errorInfo->struct_version == UINT32_C(1);
}

} // namespace

uint32_t cna_get_abi_version(void)
{
    return CNA_ABI_VERSION;
}

CNA_Result cna_error_get_last_info(CNA_ErrorInfo* const outInfo)
{
    if (!IsSupportedErrorInfo(outInfo)) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    const CNA::C::Detail::LastError& lastError = CNA::C::Detail::GetLastError();
    outInfo->result = lastError.result;
    outInfo->category = lastError.category;
    outInfo->message_byte_length = lastError.messageByteLength;
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_error_get_last_message_size(uint64_t* const outBytes)
{
    if (outBytes == nullptr) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    *outBytes = CNA::C::Detail::GetLastError().messageByteLength;
    return CNA_RESULT_SUCCESS;
}

CNA_Result cna_error_copy_last_message(
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != 0U)) {
        return CNA_RESULT_INVALID_ARGUMENT;
    }

    const CNA::C::Detail::LastError& lastError = CNA::C::Detail::GetLastError();
    *outBytes = lastError.messageByteLength;
    if (capacity < lastError.messageByteLength) {
        return CNA_RESULT_BUFFER_TOO_SMALL;
    }

    if (lastError.messageByteLength != 0U) {
        std::memcpy(destination, lastError.message, lastError.messageByteLength);
    }
    return CNA_RESULT_SUCCESS;
}
