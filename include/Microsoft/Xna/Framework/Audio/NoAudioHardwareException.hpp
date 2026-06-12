// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /// Thrown when no audio hardware is available.
    class NoAudioHardwareException : public std::runtime_error
    {
    public:
        NoAudioHardwareException()
            : std::runtime_error("No audio hardware is available") {}

        explicit NoAudioHardwareException(const std::string& message)
            : std::runtime_error(message) {}

        NoAudioHardwareException(const std::string& message, std::exception_ptr innerException)
            : std::runtime_error(message), innerException_(innerException) {}

        [[nodiscard]] std::exception_ptr InnerException() const { return innerException_; }

    private:
        std::exception_ptr innerException_;
    };
}
