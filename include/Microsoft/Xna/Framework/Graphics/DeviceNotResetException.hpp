// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when a draw call is attempted while the graphics device has not been reset. */
    class DeviceNotResetException : public std::runtime_error
    {
    public:
        /** @brief Constructs a DeviceNotResetException with a default message. */
        DeviceNotResetException() : std::runtime_error("The graphics device has not been reset.") {}
        /**
         * @brief Constructs a DeviceNotResetException with a custom message.
         * @param message Description of the not-reset condition.
         */
        explicit DeviceNotResetException(const std::string& message) : std::runtime_error(message) {}
    };
}
