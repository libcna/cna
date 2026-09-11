// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentSerializerAttribute.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content
{
    ContentSerializerAttribute::ContentSerializerAttribute() = default;

    bool ContentSerializerAttribute::getAllowNullProperty() const noexcept { return allowNull_; }

    void ContentSerializerAttribute::setAllowNullProperty(bool value) noexcept { allowNull_ = value; }

    std::string ContentSerializerAttribute::getCollectionItemNameProperty() const
    {
        return collectionItemName_.empty() ? std::string("Item") : collectionItemName_;
    }

    void ContentSerializerAttribute::setCollectionItemNameProperty(std::string value)
    {
        collectionItemName_ = std::move(value);
    }

    const std::string& ContentSerializerAttribute::getElementNameProperty() const noexcept
    {
        return elementName_;
    }

    void ContentSerializerAttribute::setElementNameProperty(std::string value) { elementName_ = std::move(value); }

    bool ContentSerializerAttribute::getFlattenContentProperty() const noexcept { return flattenContent_; }

    void ContentSerializerAttribute::setFlattenContentProperty(bool value) noexcept { flattenContent_ = value; }

    bool ContentSerializerAttribute::getHasCollectionItemNameProperty() const noexcept
    {
        return !collectionItemName_.empty();
    }

    bool ContentSerializerAttribute::getOptionalProperty() const noexcept { return optional_; }

    void ContentSerializerAttribute::setOptionalProperty(bool value) noexcept { optional_ = value; }

    bool ContentSerializerAttribute::getSharedResourceProperty() const noexcept { return sharedResource_; }

    void ContentSerializerAttribute::setSharedResourceProperty(bool value) noexcept { sharedResource_ = value; }

    ContentSerializerAttribute ContentSerializerAttribute::Clone() const { return *this; }
}
