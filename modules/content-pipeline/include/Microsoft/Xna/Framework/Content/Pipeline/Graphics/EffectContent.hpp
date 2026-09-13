// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides properties for maintaining an effect: the source code an effect processor
     *        compiles.
     */
    class EffectContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.EffectContent";

        /** @brief Initializes a new instance of EffectContent. */
        EffectContent() = default;

        /**
         * @brief Gets the effect source code.
         *
         * @return The source code, or an empty optional when none has been set. XNA's property is
         *         a nullable string and the difference is observable: an effect with no source
         *         serializes as `<EffectCode Null="true" />` (measured,
         *         tests/reference/xna40/graphics case effectcontent/serialize_null_code), which a
         *         plain std::string could not express.
         */
        [[nodiscard]] const std::optional<std::string>& getEffectCodeProperty() const noexcept;

        /**
         * @brief Sets the effect source code.
         *
         * @param value The source code, or an empty optional for none.
         */
        void setEffectCodeProperty(std::optional<std::string> value);

        /**
         * @brief Describes the effect for the intermediate serializer: the members of ContentItem,
         *        then the source code.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<EffectContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::optional<std::string> effectCode_;
    };
}
