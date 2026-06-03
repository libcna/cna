#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief Exception thrown when an asset cannot be loaded by the content manager.
     */
    class ContentLoadException : public std::runtime_error
    {
    public:
        explicit ContentLoadException(const std::string& message);
        ContentLoadException(const std::string& message, const std::exception& inner);
    };
}
