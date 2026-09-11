// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorAttribute.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    const std::string& ContentProcessorAttribute::getDisplayNameProperty() const noexcept
    {
        return displayName_;
    }

    void ContentProcessorAttribute::setDisplayNameProperty(std::string value)
    {
        displayName_ = std::move(value);
    }
}
