// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief A custom Attribute that marks a content type with the version number its automatic
     *        type writer records beside the reader name.
     */
    class ContentSerializerTypeVersionAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.ContentSerializerTypeVersionAttribute";

        /**
         * @brief Creates a new instance of ContentSerializerTypeVersionAttribute.
         *
         * @param typeVersion The run-time type version for the object.
         */
        explicit ContentSerializerTypeVersionAttribute(std::int32_t typeVersion) noexcept;

        /**
         * @brief Gets the run-time type version for the object.
         *
         * @return The version number.
         */
        [[nodiscard]] std::int32_t getTypeVersionProperty() const noexcept;

    private:
        std::int32_t typeVersion_;
    };
}
