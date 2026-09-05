// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentIdentity.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/OpaqueDataDictionary.hpp"
#include "System/Object.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate
{
    template<typename T>
    class ContentTypeDescriptor;
}

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Provides properties that define various aspects of content stored in the pipeline:
     *        its identity, its name, and a dictionary of opaque, importer-specific data.
     *
     * Every intermediate content type derives from this class, which is why it derives
     * `System::Object`: a `ContentItem` travels through the pipeline as a shared, mutable
     * reference (docs/xna-content-pipeline-compat-api.md §2).
     */
    class ContentItem : public System::Object
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentItem";

        /** @brief Initializes an item with an empty identity, an empty name and no opaque data. */
        ContentItem() = default;

        /**
         * @brief Gets the identity of the content item.
         *
         * @return The identity; empty (the null identity) until an importer sets one.
         */
        [[nodiscard]] const ContentIdentity& getIdentityProperty() const noexcept;

        /**
         * @brief Sets the identity of the content item.
         *
         * @param value The identity to record.
         */
        void setIdentityProperty(ContentIdentity value);

        /**
         * @brief Gets the name of the content item.
         *
         * @return The name, or empty when the item is unnamed.
         */
        [[nodiscard]] const std::string& getNameProperty() const noexcept;

        /**
         * @brief Sets the name of the content item.
         *
         * @param value The name.
         */
        void setNameProperty(std::string value);

        /**
         * @brief Gets the opaque data of the content item.
         *
         * @return The mutable dictionary owned by this item.
         */
        [[nodiscard]] OpaqueDataDictionary& getOpaqueDataProperty() noexcept;

        /**
         * @brief Gets the opaque data of the content item.
         *
         * @return The read-only dictionary owned by this item.
         */
        [[nodiscard]] const OpaqueDataDictionary& getOpaqueDataProperty() const noexcept;

        /**
         * @brief Describes the members every content item serializes: its name and its opaque
         *        data, each written only once it has one.
         *
         * Measured on the runtime (tests/reference/xna40/graphics, cases
         * material/serialize_with_name and effectcontent/serialize_with_opaquedata): `Name` and
         * `OpaqueData` are serialized members of this base and come before a derived type's own,
         * while `Identity` is not serialized at all.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<ContentItem>& d);

        /** @brief Returns the .NET full name of this class. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        ContentIdentity identity_;
        std::string name_;
        OpaqueDataDictionary opaqueData_;
    };
}
