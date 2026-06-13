// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when no audio hardware is available on the current system. */
    class NoAudioHardwareException final : public std::runtime_error
    {
    public:
        /** @brief Constructs a NoAudioHardwareException with a default message. */
        NoAudioHardwareException()
            : std::runtime_error("No audio hardware is available") {}

        /**
         * @brief Constructs a NoAudioHardwareException with the given message.
         *
         * @param message Error description.
         */
        explicit NoAudioHardwareException(const std::string& message)
            : std::runtime_error(message) {}

        /**
         * @brief Constructs a NoAudioHardwareException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        NoAudioHardwareException(const std::string& message, std::exception_ptr innerException)
            : std::runtime_error(message), innerException_(innerException) {}

        /**
         * @brief Returns the inner exception that caused this exception, if any.
         *
         * @return Inner exception pointer, or null.
         */
        [[nodiscard]] std::exception_ptr InnerException() const { return innerException_; }

    private:
        std::exception_ptr innerException_;
    };
}
