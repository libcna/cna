// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentItem.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/ContentTypeDescription.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Represents a compiled effect: the byte code an effect processor produced, which an
     *        effect material references.
     */
    class CompiledEffectContent : public ContentItem
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.CompiledEffectContent";

        /**
         * @brief Initializes an empty instance, for the intermediate serializer to fill.
         *
         * XNA reaches the private parameterless constructor its type carries by reflection; C++
         * has none, so the serializer needs a constructor it can call.
         */
        CNAEXT CompiledEffectContent() = default;

        /**
         * @brief Initializes a new instance of CompiledEffectContent holding the given byte code.
         *
         * XNA refuses a null array here; a std::vector cannot be null, so an empty one is simply
         * an effect with no byte code.
         *
         * @param effectCode The compiled effect byte code.
         */
        explicit CompiledEffectContent(std::vector<SharpRuntime::bytecs> effectCode);

        /**
         * @brief Gets the compiled effect byte code.
         *
         * @return The byte code the constructor received.
         */
        [[nodiscard]] const std::vector<SharpRuntime::bytecs>& GetEffectCode() const noexcept;

        /**
         * @brief Describes the compiled effect for the intermediate serializer: ContentItem's own
         *        members, then the byte code as a packed `EffectCode` element.
         *
         * @param d The descriptor being filled.
         */
        static void DescribeContent(Serialization::Intermediate::ContentTypeDescriptor<CompiledEffectContent>& d);

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

    private:
        std::vector<SharpRuntime::bytecs> effectCode_;
    };
}
