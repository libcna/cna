#pragma once

#include <exception>
#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Audio
{
    /// Thrown when the maximum number of simultaneous sound instances is exceeded.
    class InstancePlayLimitException : public std::runtime_error
    {
    public:
        InstancePlayLimitException()
            : std::runtime_error("Sound instance play limit exceeded") {}

        explicit InstancePlayLimitException(const std::string& message)
            : std::runtime_error(message) {}

        InstancePlayLimitException(const std::string& message, std::exception_ptr innerException)
            : std::runtime_error(message), innerException_(innerException) {}

        [[nodiscard]] std::exception_ptr InnerException() const { return innerException_; }

    private:
        std::exception_ptr innerException_;
    };
}
