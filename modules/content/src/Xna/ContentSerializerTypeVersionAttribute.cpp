// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentSerializerTypeVersionAttribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    ContentSerializerTypeVersionAttribute::ContentSerializerTypeVersionAttribute(std::int32_t typeVersion) noexcept
        : typeVersion_(typeVersion)
    {
    }

    std::int32_t ContentSerializerTypeVersionAttribute::getTypeVersionProperty() const noexcept
    {
        return typeVersion_;
    }
}
