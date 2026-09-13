// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentSerializerRuntimeTypeAttribute.hpp"

#include <utility>

namespace Microsoft::Xna::Framework::Content
{
    ContentSerializerRuntimeTypeAttribute::ContentSerializerRuntimeTypeAttribute(std::string runtimeType)
        : runtimeType_(std::move(runtimeType))
    {
    }

    const std::string& ContentSerializerRuntimeTypeAttribute::getRuntimeTypeProperty() const noexcept
    {
        return runtimeType_;
    }
}
