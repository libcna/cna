// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentSerializerCollectionItemNameAttribute.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content
{
    ContentSerializerCollectionItemNameAttribute::ContentSerializerCollectionItemNameAttribute(
        std::string collectionItemName)
        : collectionItemName_(std::move(collectionItemName))
    {
    }

    const std::string& ContentSerializerCollectionItemNameAttribute::getCollectionItemNameProperty() const noexcept
    {
        return collectionItemName_;
    }
}
