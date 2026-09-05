// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/NamedValueDictionary.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Graphics
{
    /**
     * @brief Provides a collection of named references to texture files; items are serialized as
     *        `<Texture>` elements (`[ContentSerializerCollectionItemName("Texture")]`).
     */
    class TextureReferenceDictionary final
        : public NamedValueDictionary<std::shared_ptr<ExternalReference<TextureContent>>>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureReferenceDictionary";

        /** @brief The item element name the intermediate format uses for this collection. */
        CNAEXT static constexpr std::string_view CollectionItemName = "Texture";

        /** @brief Initializes a new instance of TextureReferenceDictionary. */
        TextureReferenceDictionary() = default;

        /** @brief Returns the type's full name, as XNA's `ToString` does. */
        CNAEXT [[nodiscard]] std::string ToString() const;
    };
}
