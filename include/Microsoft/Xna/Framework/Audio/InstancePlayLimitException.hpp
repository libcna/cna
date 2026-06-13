// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Thrown when the maximum number of simultaneous sound instances is exceeded. */
    class InstancePlayLimitException final : public std::runtime_error
    {
    public:
        /** @brief Constructs an InstancePlayLimitException with a default message. */
        InstancePlayLimitException()
            : std::runtime_error("Sound instance play limit exceeded") {}

        /**
         * @brief Constructs an InstancePlayLimitException with the given message.
         *
         * @param message Error description.
         */
        explicit InstancePlayLimitException(const std::string& message)
            : std::runtime_error(message) {}

        /**
         * @brief Constructs an InstancePlayLimitException with a message and an inner exception.
         *
         * @param message        Error description.
         * @param innerException Underlying exception that caused this one.
         */
        InstancePlayLimitException(const std::string& message, std::exception_ptr innerException)
            : std::runtime_error(message), innerException_(innerException) {}

        /**
         * @brief Returns the inner exception that caused this exception, if any.
         *
         * @return Inner exception pointer, or null.
         */
        [[nodiscard]] std::exception_ptr InnerException() const { return innerException_; }

    private:
        std::exception_ptr innerException_;
    };
}
