// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>

#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when no suitable graphics device can be found or created. */
    class NoSuitableGraphicsDeviceException : public System::Exception
    {
    public:
        /** @brief Constructs a NoSuitableGraphicsDeviceException with a default message. */
        NoSuitableGraphicsDeviceException() : System::Exception("No suitable graphics device found.") {}
        /**
         * @brief Constructs a NoSuitableGraphicsDeviceException with a custom message.
         * @param message Description of why no suitable device was found.
         */
        explicit NoSuitableGraphicsDeviceException(const std::string& message) : System::Exception(message) {}

        /**
         * @brief Constructs a NoSuitableGraphicsDeviceException with a custom message and an inner cause.
         *
         * The documented `(String, Exception)` constructor. A CLR exception reference is a
         * `std::exception_ptr` here, which is what `System::Exception` stores and
         * `getInnerExceptionProperty()` returns; an empty pointer means no inner cause, as a null
         * reference does in XNA.
         *
         * @param message Description of why no device could be created.
         * @param innerException The exception that caused this one, or an empty pointer.
         */
        NoSuitableGraphicsDeviceException(const std::string& message, std::exception_ptr innerException)
            : System::Exception(message, std::move(innerException)) {}
    };
}
