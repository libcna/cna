// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/Runtime/InteropServices/ExternalException.hpp"
#include <exception>
#include <string>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when no audio hardware is available on the current system. */
    class NoAudioHardwareException final
        : public System::Runtime::InteropServices::ExternalException
    {
    public:
        /** @brief Constructs a NoAudioHardwareException with the default external-component message. */
        NoAudioHardwareException() = default;

        /**
         * @brief Constructs a NoAudioHardwareException with the given message.
         *
         * @param message Error description.
         */
        explicit NoAudioHardwareException(const std::string& message)
            : System::Runtime::InteropServices::ExternalException(message) {}

        /**
         * @brief Constructs a NoAudioHardwareException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        NoAudioHardwareException(const std::string& message, std::exception_ptr innerException)
            : System::Runtime::InteropServices::ExternalException(message, std::move(innerException)) {}
    };
}
