// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentImporterAttribute.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline
{
    /**
     * @brief Reads an image file into a `Texture2DContent`.
     *
     * The file's own bytes decide how it is read, not its extension: a PNG named `.xyz` is read as
     * a PNG, which is what XNA does.
     */
    class TextureImporter final : public ContentImporter<Graphics::TextureContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.TextureImporter";

        /** @brief Initializes a new importer. */
        TextureImporter() = default;

        /**
         * @brief Reads the file.
         *
         * @param filename Path to the image.
         * @param context The importer context; nothing is added to it, as XNA's adds nothing.
         * @return A `Texture2DContent` of one face and one level: `Color` pixels for an integer
         *         format, `Vector4` for a portable float map.
         * @throws System::IO::FileNotFoundException when the file does not exist.
         * @throws InvalidContentException when the bytes are not an image this build reads.
         */
        [[nodiscard]] std::shared_ptr<Graphics::TextureContent> Import(
            const std::string& filename, ContentImporterContext& context) override;

        /**
         * @brief The descriptor XNA declares on this importer: nine extensions, processed by
         *        `SpriteTextureProcessor`.
         *
         * @return The attribute.
         */
        CNAEXT [[nodiscard]] static ContentImporterAttribute Attribute();

        /** @brief Returns the type's stable name. */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };
}
