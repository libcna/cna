// SPDX-License-Identifier: MS-PL
#pragma once

#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    /**
     * @brief Marks a class as a ContentTypeSerializer to be registered with the intermediate
     *        serializer.
     *
     * XNA discovers decorated classes by reflection; CNA registers them with
     * `IntermediateSerializer::AddTypeSerializer<TSerializer>()`, which takes an instance of this
     * class in place of the attribute (docs/xna-content-pipeline-compat-api.md §3).
     */
    class ContentTypeSerializerAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Serialization.Intermediate.ContentTypeSerializerAttribute";

        /** @brief Initializes a new instance of the ContentTypeSerializerAttribute class. */
        ContentTypeSerializerAttribute();
    };
}
