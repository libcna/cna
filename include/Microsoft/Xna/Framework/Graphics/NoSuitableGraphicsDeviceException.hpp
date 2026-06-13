// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when no suitable graphics device can be found or created. */
    class NoSuitableGraphicsDeviceException : public std::runtime_error
    {
    public:
        /** @brief Constructs a NoSuitableGraphicsDeviceException with a default message. */
        NoSuitableGraphicsDeviceException() : std::runtime_error("No suitable graphics device found.") {}
        /**
         * @brief Constructs a NoSuitableGraphicsDeviceException with a custom message.
         * @param message Description of why no suitable device was found.
         */
        explicit NoSuitableGraphicsDeviceException(const std::string& message) : std::runtime_error(message) {}
    };
}
