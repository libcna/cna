// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    const ContentIdentity& ContentItem::getIdentityProperty() const noexcept
    {
        return identity_;
    }

    void ContentItem::setIdentityProperty(ContentIdentity value)
    {
        identity_ = std::move(value);
    }

    const std::string& ContentItem::getNameProperty() const noexcept
    {
        return name_;
    }

    void ContentItem::setNameProperty(std::string value)
    {
        name_ = std::move(value);
    }

    OpaqueDataDictionary& ContentItem::getOpaqueDataProperty() noexcept
    {
        return opaqueData_;
    }

    const OpaqueDataDictionary& ContentItem::getOpaqueDataProperty() const noexcept
    {
        return opaqueData_;
    }

    const std::string& ContentItem::GetTypeName() const
    {
        static const std::string typeName{XnaTypeName};
        return typeName;
    }
}
