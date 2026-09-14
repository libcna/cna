// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

namespace CNA::Platform::Win32 {

    /**
     * @brief Describes a Win32 error code as readable text.
     *
     * `GetLastError()` returns a number, and a diagnostic that only carries the number makes the
     * reader look it up. `PlatformException`'s `detail` string is exactly the place for the
     * system's own message, so every failure path here goes through this.
     *
     * @param error The code from `GetLastError()`.
     * @return The system's message plus the numeric code, e.g. `"The parameter is incorrect. (87)"`.
     */
    [[nodiscard]] std::string DescribeError(unsigned long error);

    /**
     * @brief Describes the calling thread's last Win32 error as readable text.
     *
     * @return The message for `GetLastError()`.
     */
    [[nodiscard]] std::string DescribeLastError();

    /**
     * @brief Describes an `HRESULT` as readable text.
     *
     * @param result The `HRESULT` to describe.
     * @return The system's message plus the hexadecimal code.
     */
    [[nodiscard]] std::string DescribeHResult(long result);

    /**
     * @brief Throws a `PlatformException` carrying the calling thread's last Win32 error.
     *
     * @param operation The operation that failed, in CNA's own vocabulary.
     * @throws PlatformException Always.
     */
    [[noreturn]] void ThrowLastError(const std::string& operation);

    /**
     * @brief Throws a `PlatformException` carrying a specific Win32 error code.
     *
     * @param operation The operation that failed, in CNA's own vocabulary.
     * @param error The code from `GetLastError()`.
     * @throws PlatformException Always.
     */
    [[noreturn]] void ThrowError(const std::string& operation, unsigned long error);

    /**
     * @brief Throws a `PlatformException` carrying an `HRESULT`.
     *
     * @param operation The operation that failed, in CNA's own vocabulary.
     * @param result The failing `HRESULT`.
     * @throws PlatformException Always.
     */
    [[noreturn]] void ThrowHResult(const std::string& operation, long result);

} // namespace CNA::Platform::Win32
