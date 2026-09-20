// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>

#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when the graphics device is lost. */
    class DeviceLostException : public System::Exception
    {
    public:
        /** @brief Constructs a DeviceLostException with a default message. */
        DeviceLostException() : System::Exception("The graphics device was lost.") {}
        /**
         * @brief Constructs a DeviceLostException with a custom message.
         * @param message Description of the device-lost condition.
         */
        explicit DeviceLostException(const std::string& message) : System::Exception(message) {}

        /**
         * @brief Constructs a DeviceLostException with a custom message and an inner cause.
         *
         * The documented `(String, Exception)` constructor. A CLR exception reference is a
         * `std::exception_ptr` here, which is what `System::Exception` stores and
         * `getInnerExceptionProperty()` returns; an empty pointer means no inner cause, as a null
         * reference does in XNA.
         *
         * @param message Description of the device-lost condition.
         * @param innerException The exception that caused this one, or an empty pointer.
         */
        DeviceLostException(const std::string& message, std::exception_ptr innerException)
            : System::Exception(message, std::move(innerException)) {}
    };
}
