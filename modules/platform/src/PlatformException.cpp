// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformException.hpp"

namespace CNA::Platform {

    namespace {

        std::string ComposeMessage(const std::string& operation, const std::string& detail)
        {
            std::string message = "CNA::Platform: " + operation + " failed";
            if (!detail.empty())
            {
                message += ": " + detail;
            }
            return message;
        }

        std::string ComposeRefusal(const PlatformCapability capability, const std::string& platformName)
        {
            return "CNA::Platform: " + platformName + " does not support " + ToString(capability);
        }

    } // namespace

    PlatformException::PlatformException(const std::string& operation, const std::string& detail)
        : std::runtime_error(ComposeMessage(operation, detail))
        , operation_(operation)
        , detail_(detail)
    {
    }

    PlatformException::PlatformException(const std::string& operation)
        : PlatformException(operation, std::string())
    {
    }

    PlatformException::PlatformException(const std::string& composedMessage,
                                         const std::string& operation, int)
        : std::runtime_error(composedMessage)
        , operation_(operation)
    {
    }

    const std::string& PlatformException::GetOperation() const
    {
        return operation_;
    }

    const std::string& PlatformException::GetDetail() const
    {
        return detail_;
    }

    PlatformNotSupportedException::PlatformNotSupportedException(
        const PlatformCapability capability, const std::string& platformName)
        // Uses the composed-message constructor so the sentence is not wrapped a second time;
        // GetCapability() is the machine-readable half a caller or test should branch on.
        : PlatformException(ComposeRefusal(capability, platformName), ToString(capability), 0)
        , capability_(capability)
    {
    }

    PlatformCapability PlatformNotSupportedException::GetCapability() const
    {
        return capability_;
    }

} // namespace CNA::Platform
