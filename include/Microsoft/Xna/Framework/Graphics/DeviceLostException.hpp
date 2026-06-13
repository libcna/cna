// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when the graphics device is lost. */
    class DeviceLostException : public std::runtime_error
    {
    public:
        /** @brief Constructs a DeviceLostException with a default message. */
        DeviceLostException() : std::runtime_error("The graphics device was lost.") {}
        /**
         * @brief Constructs a DeviceLostException with a custom message.
         * @param message Description of the device-lost condition.
         */
        explicit DeviceLostException(const std::string& message) : std::runtime_error(message) {}
    };
}
