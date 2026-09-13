// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    ContentIdentity::ContentIdentity(std::string sourceFilename)
        : sourceFilename_(std::move(sourceFilename))
    {
    }

    ContentIdentity::ContentIdentity(std::string sourceFilename, std::string sourceTool)
        : sourceFilename_(std::move(sourceFilename)), sourceTool_(std::move(sourceTool))
    {
    }

    ContentIdentity::ContentIdentity(std::string sourceFilename, std::string sourceTool,
                                     std::string fragmentIdentifier)
        : sourceFilename_(std::move(sourceFilename))
        , sourceTool_(std::move(sourceTool))
        , fragmentIdentifier_(std::move(fragmentIdentifier))
    {
    }

    const std::string& ContentIdentity::getFragmentIdentifierProperty() const noexcept
    {
        return fragmentIdentifier_;
    }

    void ContentIdentity::setFragmentIdentifierProperty(std::string value)
    {
        fragmentIdentifier_ = std::move(value);
    }

    const std::string& ContentIdentity::getSourceFilenameProperty() const noexcept
    {
        return sourceFilename_;
    }

    void ContentIdentity::setSourceFilenameProperty(std::string value)
    {
        sourceFilename_ = std::move(value);
    }

    const std::string& ContentIdentity::getSourceToolProperty() const noexcept
    {
        return sourceTool_;
    }

    void ContentIdentity::setSourceToolProperty(std::string value)
    {
        sourceTool_ = std::move(value);
    }

    bool ContentIdentity::IsEmpty() const noexcept
    {
        return sourceFilename_.empty() && sourceTool_.empty() && fragmentIdentifier_.empty();
    }

    std::string ContentIdentity::ToString() const
    {
        if (fragmentIdentifier_.empty())
        {
            return sourceFilename_;
        }
        return sourceFilename_ + "#" + fragmentIdentifier_;
    }
}
