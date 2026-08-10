// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/Runtime/InteropServices/ExternalException.hpp"
#include <exception>
#include <string>
#include <utility>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when the maximum number of simultaneous sound instances is exceeded. */
    class InstancePlayLimitException final
        : public System::Runtime::InteropServices::ExternalException
    {
    public:
        /** @brief Constructs an InstancePlayLimitException with the default external-component message. */
        InstancePlayLimitException() = default;

        /**
         * @brief Constructs an InstancePlayLimitException with the given message.
         *
         * @param message Error description.
         */
        explicit InstancePlayLimitException(const std::string& message)
            : System::Runtime::InteropServices::ExternalException(message) {}

        /**
         * @brief Constructs an InstancePlayLimitException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        InstancePlayLimitException(const std::string& message, std::exception_ptr innerException)
            : System::Runtime::InteropServices::ExternalException(message, std::move(innerException)) {}
    };
}
