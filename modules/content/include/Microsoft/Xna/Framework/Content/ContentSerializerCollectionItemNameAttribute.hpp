// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief A custom Attribute that marks a collection class to specify the XML element name for
     *        each item in the collection.
     */
    class ContentSerializerCollectionItemNameAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.ContentSerializerCollectionItemNameAttribute";

        /**
         * @brief Creates a new instance of ContentSerializerCollectionItemNameAttribute.
         *
         * @param collectionItemName The name that will be used for each item in the collection.
         */
        explicit ContentSerializerCollectionItemNameAttribute(std::string collectionItemName);

        /**
         * @brief Gets the name that will be used for each item in the collection.
         *
         * @return The item element name.
         */
        [[nodiscard]] const std::string& getCollectionItemNameProperty() const noexcept;

    private:
        std::string collectionItemName_;
    };
}
