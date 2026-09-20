// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>

#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Thrown when a draw call is attempted while the graphics device has not been reset. */
    class DeviceNotResetException : public System::Exception
    {
    public:
        /** @brief Constructs a DeviceNotResetException with a default message. */
        DeviceNotResetException() : System::Exception("The graphics device has not been reset.") {}
        /**
         * @brief Constructs a DeviceNotResetException with a custom message.
         * @param message Description of the not-reset condition.
         */
        explicit DeviceNotResetException(const std::string& message) : System::Exception(message) {}

        /**
         * @brief Constructs a DeviceNotResetException with a custom message and an inner cause.
         *
         * The documented `(String, Exception)` constructor. A CLR exception reference is a
         * `std::exception_ptr` here, which is what `System::Exception` stores and
         * `getInnerExceptionProperty()` returns; an empty pointer means no inner cause, as a null
         * reference does in XNA.
         *
         * @param message Description of the device-not-reset condition.
         * @param innerException The exception that caused this one, or an empty pointer.
         */
        DeviceNotResetException(const std::string& message, std::exception_ptr innerException)
            : System::Exception(message, std::move(innerException)) {}
    };
}
