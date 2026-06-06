#pragma once

#include <stdexcept>

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
    };
}
