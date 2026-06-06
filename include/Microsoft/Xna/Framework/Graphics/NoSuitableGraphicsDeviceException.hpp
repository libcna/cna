#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /// Thrown when no suitable graphics device is found.
    class NoSuitableGraphicsDeviceException : public std::runtime_error
    {
    public:
        NoSuitableGraphicsDeviceException() : std::runtime_error("No suitable graphics device found.") {}
        explicit NoSuitableGraphicsDeviceException(const std::string& message) : std::runtime_error(message) {}
    };
}
