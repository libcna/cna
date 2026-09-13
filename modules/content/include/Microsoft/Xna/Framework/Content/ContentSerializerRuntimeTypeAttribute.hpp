// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief A custom Attribute that marks a content type with the run-time type its data is loaded
     *        as, so the automatic type writer can name a run-time type that differs from the
     *        design-time one.
     */
    class ContentSerializerRuntimeTypeAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.ContentSerializerRuntimeTypeAttribute";

        /**
         * @brief Creates a new instance of ContentSerializerRuntimeTypeAttribute.
         *
         * @param runtimeType The run-time type for the object.
         */
        explicit ContentSerializerRuntimeTypeAttribute(std::string runtimeType);

        /**
         * @brief Gets the run-time type for the object.
         *
         * @return The assembly-qualified run-time type name.
         */
        [[nodiscard]] const std::string& getRuntimeTypeProperty() const noexcept;

    private:
        std::string runtimeType_;
    };
}
