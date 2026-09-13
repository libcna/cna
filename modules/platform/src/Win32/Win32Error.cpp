// SPDX-License-Identifier: MS-PL

#include "Win32Error.hpp"

#include "Win32Common.hpp"
#include "Win32Utf.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <sstream>

namespace CNA::Platform::Win32 {

    namespace {

        std::string FormatSystemMessage(const DWORD error)
        {
            LPWSTR buffer = nullptr;
            const DWORD length = FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

            if (length == 0 || buffer == nullptr)
            {
                if (buffer != nullptr)
                    LocalFree(buffer);
                return {};
            }

            std::wstring message(buffer, length);
            LocalFree(buffer);

            // FormatMessageW terminates its text with CRLF, which turns a one-line diagnostic
            // into three lines the moment it is embedded in a longer sentence.
            while (!message.empty() &&
                   (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
            {
                message.pop_back();
            }
            return ToUtf8(message);
        }

    } // namespace

    std::string DescribeError(const unsigned long error)
    {
        const std::string message = FormatSystemMessage(static_cast<DWORD>(error));
        std::ostringstream text;
        if (message.empty())
            text << "Win32 error " << error;
        else
            text << message << " (" << error << ")";
        return text.str();
    }

    std::string DescribeLastError()
    {
        return DescribeError(GetLastError());
    }

    std::string DescribeHResult(const long result)
    {
        const std::string message = FormatSystemMessage(static_cast<DWORD>(result));
        std::ostringstream text;
        text << (message.empty() ? std::string("HRESULT") : message) << " (0x" << std::hex
             << static_cast<unsigned long>(static_cast<unsigned int>(result)) << ")";
        return text.str();
    }

    void ThrowLastError(const std::string& operation)
    {
        ThrowError(operation, GetLastError());
    }

    void ThrowError(const std::string& operation, const unsigned long error)
    {
        throw PlatformException(operation, DescribeError(error));
    }

    void ThrowHResult(const std::string& operation, const long result)
    {
        throw PlatformException(operation, DescribeHResult(result));
    }

} // namespace CNA::Platform::Win32
