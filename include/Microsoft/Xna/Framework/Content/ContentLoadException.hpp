// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Content
{
    /// Exception thrown when an asset cannot be loaded by the content manager.
    class ContentLoadException : public std::runtime_error
    {
    public:
        /// Constructs a ContentLoadException with the given message.
        explicit ContentLoadException(const std::string& message);
        /// Constructs a ContentLoadException with the given message and inner exception.
        ContentLoadException(const std::string& message, const std::exception& inner);
    };
}
