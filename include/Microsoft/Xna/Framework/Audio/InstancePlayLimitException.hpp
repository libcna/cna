#pragma once

#include <stdexcept>

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
    };
}
