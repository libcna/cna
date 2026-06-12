// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /// Thrown when a requested microphone is not connected.
    class NoMicrophoneConnectedException : public std::runtime_error
    {
    public:
        NoMicrophoneConnectedException()
            : std::runtime_error("No microphone is connected") {}

        explicit NoMicrophoneConnectedException(const std::string& message)
            : std::runtime_error(message) {}

        NoMicrophoneConnectedException(const std::string& message, std::exception_ptr innerException)
            : std::runtime_error(message), innerException_(innerException) {}

        [[nodiscard]] std::exception_ptr InnerException() const { return innerException_; }

    private:
        std::exception_ptr innerException_;
    };
}
