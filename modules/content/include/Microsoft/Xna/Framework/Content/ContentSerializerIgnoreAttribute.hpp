// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief A custom Attribute that marks public fields or properties to prevent them from being
     *        serialized.
     */
    class ContentSerializerIgnoreAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.ContentSerializerIgnoreAttribute";

        /** @brief Creates a new instance of ContentSerializerIgnoreAttribute. */
        ContentSerializerIgnoreAttribute();
    };
}
