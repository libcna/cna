// SPDX-License-Identifier: MS-PL
#pragma once

#include <stdexcept>
#include <string>

namespace Microsoft::Xna::Framework::Content
{
    /** @brief Exception thrown when an asset cannot be loaded by the content manager. */
    class ContentLoadException : public std::runtime_error
    {
    public:
        /**
         * @brief Constructs a ContentLoadException with the given error message.
         *
         * @param message Description of the load failure.
         */
        /**
         * @brief Creates a ContentLoadException with the default message.
         *
         * The documented parameterless constructor. XNA leaves the message to
         * `System.Exception`'s parameterless constructor, which produces .NET's fallback text
         * naming the exception's own type, so that is the message here.
         */
        ContentLoadException();

        explicit ContentLoadException(const std::string& message);

        /**
         * @brief Constructs a ContentLoadException with a message and an inner exception.
         *
         * @param message Description of the load failure.
         * @param inner   Exception that caused this one.
         */
        ContentLoadException(const std::string& message, const std::exception& inner);
    };
}
