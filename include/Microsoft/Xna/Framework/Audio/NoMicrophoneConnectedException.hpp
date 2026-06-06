#pragma once

#include <stdexcept>

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
    };
}
