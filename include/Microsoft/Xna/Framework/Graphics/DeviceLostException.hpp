// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /// Thrown when the graphics device is lost.
    class DeviceLostException : public std::runtime_error
    {
    public:
        DeviceLostException() : std::runtime_error("The graphics device was lost.") {}
        explicit DeviceLostException(const std::string& message) : std::runtime_error(message) {}
    };
}
