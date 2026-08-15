// SPDX-License-Identifier: MS-PL

#include "CnaCApiDetail.hpp"

#include "System/IO/FileNotFoundException.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

[[nodiscard]] bool HasLastError(
    const CNA_Result result,
    const CNA_ErrorCategory category,
    const std::string_view message)
{
    const CNA::C::Detail::LastError& error = CNA::C::Detail::GetLastError();
    return error.result == result && error.category == category && error.message == message;
}

} // namespace

int main()
{
    using namespace CNA::C::Detail;

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw std::invalid_argument("argument failure");
        }) != CNA_RESULT_INVALID_ARGUMENT ||
        !HasLastError(
            CNA_RESULT_INVALID_ARGUMENT,
            CNA_ERROR_CATEGORY_ARGUMENT,
            "argument failure")) {
        return 1;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw std::bad_alloc();
        }) != CNA_RESULT_OUT_OF_MEMORY ||
        !HasLastError(
            CNA_RESULT_OUT_OF_MEMORY,
            CNA_ERROR_CATEGORY_MEMORY,
            "Native allocation failed.")) {
        return 2;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw System::NotSupportedException("unsupported operation");
        }) != CNA_RESULT_NOT_SUPPORTED ||
        !HasLastError(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "unsupported operation")) {
        return 3;
    }

    SetLastError(
        CNA_RESULT_INTERNAL,
        CNA_ERROR_CATEGORY_INTERNAL,
        "boundary diagnostic");
    CNA_ErrorInfo errorInfo = {
        sizeof(CNA_ErrorInfo),
        UINT32_C(1),
        CNA_RESULT_SUCCESS,
        CNA_ERROR_CATEGORY_NONE,
        0U
    };
    char message[19] = {};
    uint64_t requiredBytes = 0U;
    if (cna_error_get_last_info(&errorInfo) != CNA_RESULT_SUCCESS ||
        errorInfo.result != CNA_RESULT_INTERNAL ||
        errorInfo.category != CNA_ERROR_CATEGORY_INTERNAL ||
        errorInfo.message_byte_length != 19U ||
        cna_error_copy_last_message(message, 18U, &requiredBytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        requiredBytes != 19U || message[0] != '\0' ||
        cna_error_copy_last_message(message, sizeof(message), &requiredBytes) != CNA_RESULT_SUCCESS ||
        requiredBytes != 19U || std::string_view(message, requiredBytes) != "boundary diagnostic") {
        return 4;
    }

    const char validUtf8[] = "CNA \xF0\x9F\x8E\xAE";
    const CNA_StringView valid = {validUtf8, sizeof(validUtf8) - 1U};
    std::string copied;
    if (ValidateStringView(valid, true) != CNA_RESULT_SUCCESS ||
        CopyStringView(valid, true, &copied) != CNA_RESULT_SUCCESS ||
        copied != validUtf8) {
        return 5;
    }

    const char overlong[] = "\xC0\x80";
    const CNA_StringView invalid = {overlong, sizeof(overlong) - 1U};
    if (ValidateStringView(invalid, false) != CNA_RESULT_ENCODING ||
        ValidateStringView({nullptr, 1U}, false) != CNA_RESULT_INVALID_ARGUMENT) {
        return 6;
    }

    const char embeddedNul[] = {'a', '\0', 'b'};
    if (ValidateStringView({embeddedNul, sizeof(embeddedNul)}, true) != CNA_RESULT_ENCODING ||
        ValidateStringView({embeddedNul, sizeof(embeddedNul)}, false) != CNA_RESULT_SUCCESS) {
        return 7;
    }

    std::size_t byteCount = 0U;
    const uint32_t values[3] = {0U, 0U, 0U};
    if (CheckedElementByteCount(values, 3U, sizeof(uint32_t), &byteCount) != CNA_RESULT_SUCCESS ||
        byteCount != sizeof(values) ||
        CheckedElementByteCount(nullptr, 1U, sizeof(uint32_t), &byteCount) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        CheckedElementByteCount(nullptr, 0U, sizeof(uint32_t), &byteCount) !=
            CNA_RESULT_SUCCESS ||
        CheckedElementByteCount(
            values,
            std::numeric_limits<uint64_t>::max(),
            2U,
            &byteCount) != CNA_RESULT_OVERFLOW) {
        return 8;
    }

    // Canonical graphics-device failures must not fall through to the generic internal arm.
    using Microsoft::Xna::Framework::Graphics::DeviceLostException;
    using Microsoft::Xna::Framework::Graphics::DeviceNotResetException;
    using Microsoft::Xna::Framework::Graphics::NoSuitableGraphicsDeviceException;

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw DeviceLostException();
        }) != CNA_RESULT_INVALID_STATE ||
        !HasLastError(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "The graphics device was lost.")) {
        return 9;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw DeviceNotResetException("device not reset");
        }) != CNA_RESULT_INVALID_STATE ||
        !HasLastError(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, "device not reset")) {
        return 10;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw NoSuitableGraphicsDeviceException();
        }) != CNA_RESULT_NOT_SUPPORTED ||
        !HasLastError(
            CNA_RESULT_NOT_SUPPORTED,
            CNA_ERROR_CATEGORY_NOT_SUPPORTED,
            "No suitable graphics device found.")) {
        return 11;
    }

    // File-facing failures must not land in the generic internal arm either: the storage, content
    // and texture routes all reach real filesystem calls.
    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw std::filesystem::filesystem_error(
                "listing failed",
                std::make_error_code(std::errc::no_such_file_or_directory));
        }) != CNA_RESULT_IO ||
        GetLastError().category != CNA_ERROR_CATEGORY_IO) {
        return 12;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw System::IO::FileNotFoundException("missing asset");
        }) != CNA_RESULT_IO ||
        !HasLastError(CNA_RESULT_IO, CNA_ERROR_CATEGORY_IO, "missing asset")) {
        return 13;
    }

    using Microsoft::Xna::Framework::Storage::StorageDeviceNotConnectedException;

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw StorageDeviceNotConnectedException();
        }) != CNA_RESULT_INVALID_STATE ||
        GetLastError().category != CNA_ERROR_CATEGORY_STATE) {
        return 14;
    }

    if (CallWithExceptionBarrier([]() -> CNA_Result {
            throw StorageDeviceNotConnectedException("storage device removed");
        }) != CNA_RESULT_INVALID_STATE ||
        !HasLastError(
            CNA_RESULT_INVALID_STATE,
            CNA_ERROR_CATEGORY_STATE,
            "storage device removed")) {
        return 15;
    }

    return 0;
}
