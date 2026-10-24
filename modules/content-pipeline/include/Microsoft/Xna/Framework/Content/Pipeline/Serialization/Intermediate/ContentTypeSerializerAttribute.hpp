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

        /**
         * @brief Determines whether another attribute is equal to this one.
         *
         * .NET's Attribute.Equals compares the type and every instance field; this attribute has
         * no fields, so every instance of the same type is equal, and Match (which defaults to
         * Equals) agrees.
         *
         * @param other The attribute to compare with.
         * @return true when @p other is a ContentTypeSerializerAttribute of the same type.
         */
        bool Equals(const System::Attribute& other) const override;

        /**
         * @brief Returns a hash code shared by every equal instance.
         *
         * @return A hash of the attribute's type.
         */
        int GetHashCode() const override;
    };
}
