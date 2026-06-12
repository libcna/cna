// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /// Thrown when a draw call is attempted while the graphics device is not reset.
    class DeviceNotResetException : public std::runtime_error
    {
    public:
        DeviceNotResetException() : std::runtime_error("The graphics device has not been reset.") {}
        explicit DeviceNotResetException(const std::string& message) : std::runtime_error(message) {}
    };
}
