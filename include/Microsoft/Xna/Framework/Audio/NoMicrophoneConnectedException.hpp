// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when a requested microphone device is not connected. */
    class NoMicrophoneConnectedException final : public std::runtime_error
    {
    public:
        /** @brief Constructs a NoMicrophoneConnectedException with a default message. */
        NoMicrophoneConnectedException()
            : std::runtime_error("No microphone is connected") {}

        /**
         * @brief Constructs a NoMicrophoneConnectedException with the given message.
         *
         * @param message Error description.
         */
        explicit NoMicrophoneConnectedException(const std::string& message)
            : std::runtime_error(message) {}

        /**
         * @brief Constructs a NoMicrophoneConnectedException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        NoMicrophoneConnectedException(const std::string& message, std::exception_ptr innerException)
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
