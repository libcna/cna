// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    InvalidContentException::InvalidContentException()
        : System::Exception("Invalid content.")
    {
    }

    InvalidContentException::InvalidContentException(const std::string& message)
        : System::Exception(message)
    {
    }

    InvalidContentException::InvalidContentException(const std::string& message,
                                                     ContentIdentity contentIdentity)
        : System::Exception(message), contentIdentity_(std::move(contentIdentity))
    {
    }

    InvalidContentException::InvalidContentException(const std::string& message,
                                                     ContentIdentity contentIdentity,
                                                     std::exception_ptr innerException)
        : System::Exception(message, std::move(innerException))
        , contentIdentity_(std::move(contentIdentity))
    {
    }

    InvalidContentException::InvalidContentException(const std::string& message,
                                                     std::exception_ptr innerException)
        : System::Exception(message, std::move(innerException))
    {
    }

    const ContentIdentity& InvalidContentException::getContentIdentityProperty() const noexcept
    {
        return contentIdentity_;
    }

    void InvalidContentException::setContentIdentityProperty(ContentIdentity value)
    {
        contentIdentity_ = std::move(value);
    }
}
