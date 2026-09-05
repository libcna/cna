// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "System/Attribute.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Identifies the components of a content processor.
     *
     * A descriptor object rather than a CLR attribute (docs/xna-content-pipeline-compat-api.md
     * §4); it is handed to the registry when the processor class is registered.
     */
    class ContentProcessorAttribute : public System::Attribute
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.ContentProcessorAttribute";

        /** @brief Initializes an attribute with no display name. */
        ContentProcessorAttribute() = default;

        /**
         * @brief Gets the display name shown to users.
         *
         * @return The display name, or empty when none was given.
         */
        [[nodiscard]] const std::string& getDisplayNameProperty() const noexcept;

        /**
         * @brief Sets the display name shown to users.
         *
         * @param value The display name.
         */
        void setDisplayNameProperty(std::string value);

    private:
        std::string displayName_;
    };
}
