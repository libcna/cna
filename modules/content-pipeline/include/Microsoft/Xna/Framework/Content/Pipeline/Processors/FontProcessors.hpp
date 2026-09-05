// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/FontDescription.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/SpriteFontContent.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Rasterizes a font description into a sprite font.
     *
     * The rasterization is the canonical one this repository already ships
     * (`CNA::Content::Pipeline::RasterizeFontDescription`), and the font is resolved the way the
     * canonical importer resolves one. The refusals are XNA's own, measured
     * (`tests/reference/xna40/graphics/graphics-content-oracle.json`, `fontprocessor/*`).
     */
    class FontDescriptionProcessor : public ContentProcessor<Graphics::FontDescription, SpriteFontContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.FontDescriptionProcessor";

        /** @brief Initializes a new instance of FontDescriptionProcessor. */
        FontDescriptionProcessor() = default;

        /** @brief Destroys the processor. */
        ~FontDescriptionProcessor() override = default;

        /**
         * @brief Rasterizes the description.
         *
         * @param input The font description.
         * @param context The processor context.
         * @return The rasterized sprite font.
         * @throws System::ArgumentNullException when the input is null.
         * @throws InvalidContentException when the font family cannot be found or the description
         *         names no characters.
         */
        [[nodiscard]] std::shared_ptr<SpriteFontContent> Process(
            const std::shared_ptr<Graphics::FontDescription>& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;
    };

    /**
     * @brief Turns a texture of glyphs into a sprite font, taking the glyphs from the regions the
     *        texture's own border colour separates.
     */
    class FontTextureProcessor : public ContentProcessor<Graphics::Texture2DContent, SpriteFontContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.FontTextureProcessor";

        /** @brief Initializes a processor with the measured defaults. */
        FontTextureProcessor() = default;

        /** @brief Destroys the processor. */
        ~FontTextureProcessor() override = default;

        /**
         * @brief Gets the character the first glyph in the texture stands for.
         *
         * @return A space by default, measured.
         */
        [[nodiscard]] SharpRuntime::charcs getFirstCharacterProperty() const noexcept;

        /**
         * @brief Sets the character the first glyph in the texture stands for.
         *
         * @param value The first character.
         */
        void setFirstCharacterProperty(SharpRuntime::charcs value) noexcept;

        /**
         * @brief Gets whether the glyphs' colour is multiplied by their alpha.
         *
         * @return true by default.
         */
        [[nodiscard]] bool getPremultiplyAlphaProperty() const noexcept;

        /**
         * @brief Sets whether the glyphs' colour is multiplied by their alpha.
         *
         * @param value true to premultiply.
         */
        void setPremultiplyAlphaProperty(bool value) noexcept;

        /**
         * @brief Gets the format the glyph atlas is converted to.
         *
         * @return `Color` by default.
         */
        [[nodiscard]] TextureProcessorOutputFormat getTextureFormatProperty() const noexcept;

        /**
         * @brief Sets the format the glyph atlas is converted to.
         *
         * @param value The wanted format.
         */
        void setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept;

        /**
         * @brief Builds the sprite font from the texture.
         *
         * @param input The texture holding the glyphs.
         * @param context The processor context.
         * @return The sprite font.
         * @throws System::ArgumentNullException when the input is null.
         * @throws InvalidContentException when the texture holds no glyphs.
         */
        [[nodiscard]] std::shared_ptr<SpriteFontContent> Process(
            const std::shared_ptr<Graphics::Texture2DContent>& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

    protected:
        /**
         * @brief Gets the character the glyph at the given index stands for.
         *
         * @param index The glyph index, counted from the first glyph in the texture.
         * @return The character, which is `FirstCharacter` plus the index (measured).
         */
        [[nodiscard]] virtual SharpRuntime::charcs GetCharacterForIndex(SharpRuntime::intcs index) const;

    private:
        SharpRuntime::charcs firstCharacter_ = u' ';
        bool premultiplyAlpha_ = true;
        TextureProcessorOutputFormat textureFormat_ = TextureProcessorOutputFormat::Color;
    };
}
