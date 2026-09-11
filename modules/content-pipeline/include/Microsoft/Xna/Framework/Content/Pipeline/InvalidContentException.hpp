// SPDX-License-Identifier: MS-PL
#pragma once

#include <exception>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "System/Exception.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Thrown when errors are encountered in content during processing.
     *
     * Importers and processors throw it to report a problem in the *content* (as opposed to a
     * bug in the component); the identity says which source, and where in it.
     */
    class InvalidContentException : public System::Exception
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.InvalidContentException";

        /** @brief Initializes an exception with a generic message and no identity. */
        InvalidContentException();

        /**
         * @brief Initializes an exception with a message.
         *
         * @param message Description of the problem.
         */
        explicit InvalidContentException(const std::string& message);

        /**
         * @brief Initializes an exception with a message and the identity of the offending
         *        content.
         *
         * @param message Description of the problem.
         * @param contentIdentity Identity of the content in error.
         */
        InvalidContentException(const std::string& message, ContentIdentity contentIdentity);

        /**
         * @brief Initializes an exception with a message, an identity and an inner exception.
         *
         * @param message Description of the problem.
         * @param contentIdentity Identity of the content in error.
         * @param innerException The exception that caused this one.
         */
        InvalidContentException(const std::string& message, ContentIdentity contentIdentity,
                                std::exception_ptr innerException);

        /**
         * @brief Initializes an exception with a message and an inner exception.
         *
         * @param message Description of the problem.
         * @param innerException The exception that caused this one.
         */
        InvalidContentException(const std::string& message, std::exception_ptr innerException);

        /**
         * @brief Gets the identity of the content in error.
         *
         * @return The identity; empty when none was given.
         */
        [[nodiscard]] const ContentIdentity& getContentIdentityProperty() const noexcept;

        /**
         * @brief Sets the identity of the content in error.
         *
         * @param value The identity.
         */
        void setContentIdentityProperty(ContentIdentity value);

    private:
        ContentIdentity contentIdentity_;
    };
}
