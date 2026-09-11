// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief A custom Attribute that marks a field or property to control how it is serialized or
     *        to indicate that protected or private data should be included in serialization.
     *
     * C++ has no attributes: an instance of this class is attached to a member in the type's
     * content description (docs/xna-content-pipeline-compat-api.md §8) and handed to the
     * intermediate serializers exactly as XNA hands them the reflected attribute.
     */
    class ContentSerializerAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.ContentSerializerAttribute";

        /** @brief Creates a new instance of ContentSerializerAttribute. */
        ContentSerializerAttribute();

        /**
         * @brief Get or set a value indicating whether this member can have a null value
         *        (default=true).
         *
         * @return True when a null value is allowed.
         */
        [[nodiscard]] bool getAllowNullProperty() const noexcept;

        /**
         * @brief Sets whether this member can have a null value.
         *
         * @param value True to allow a null value.
         */
        void setAllowNullProperty(bool value) noexcept;

        /**
         * @brief Gets or sets the XML element name for each item in a collection (default = "Item").
         *
         * @return The item element name; "Item" when none was set.
         */
        [[nodiscard]] std::string getCollectionItemNameProperty() const;

        /**
         * @brief Sets the XML element name for each item in a collection.
         *
         * @param value The item element name; empty restores the default.
         */
        void setCollectionItemNameProperty(std::string value);

        /**
         * @brief Gets or sets the XML element name (default=name of the managed type member).
         *
         * @return The element name; empty when the member's own name is used.
         */
        [[nodiscard]] const std::string& getElementNameProperty() const noexcept;

        /**
         * @brief Sets the XML element name.
         *
         * @param value The element name.
         */
        void setElementNameProperty(std::string value);

        /**
         * @brief Gets or sets a value indicating whether to write member contents directly into
         *        the current XML context rather than wrapping the member in a new XML element
         *        (default=false).
         *
         * @return True when the member is flattened.
         */
        [[nodiscard]] bool getFlattenContentProperty() const noexcept;

        /**
         * @brief Sets whether the member's contents are written directly into the current XML
         *        context.
         *
         * @param value True to flatten.
         */
        void setFlattenContentProperty(bool value) noexcept;

        /**
         * @brief Indicates whether an explicit CollectionItemName string is being used or the
         *        default value.
         *
         * @return True when an explicit item name was set.
         */
        [[nodiscard]] bool getHasCollectionItemNameProperty() const noexcept;

        /**
         * @brief Indicates whether to write this element if the member is null and skip past it if
         *        not found when deserializing XML (default=false).
         *
         * @return True when the member is optional.
         */
        [[nodiscard]] bool getOptionalProperty() const noexcept;

        /**
         * @brief Sets whether the member is optional.
         *
         * @param value True to make the member optional.
         */
        void setOptionalProperty(bool value) noexcept;

        /**
         * @brief Indicates whether this member is referenced from multiple parents and should be
         *        serialized as a unique ID reference (default=false).
         *
         * @return True when the member is a shared resource.
         */
        [[nodiscard]] bool getSharedResourceProperty() const noexcept;

        /**
         * @brief Sets whether the member is serialized as a shared resource.
         *
         * @param value True for a shared resource.
         */
        void setSharedResourceProperty(bool value) noexcept;

        /**
         * @brief Creates a copy of the ContentSerializerAttribute.
         *
         * @return The copy.
         */
        [[nodiscard]] ContentSerializerAttribute Clone() const;

    private:
        bool allowNull_ = true;
        std::string collectionItemName_;
        std::string elementName_;
        bool flattenContent_ = false;
        bool optional_ = false;
        bool sharedResource_ = false;
    };
}
