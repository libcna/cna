// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Processes a texture: keys out a colour, resizes, premultiplies, builds mipmaps and
     *        converts to the wanted format.
     *
     * Every default and every step is measured on the XNA 4.0 runtime
     * (`tests/reference/xna40/graphics/graphics-content-oracle.json`, cases `processor/*` and
     * `textureprocessor/*`), including the order they run in: the colour key first, then the
     * resize, then the premultiply, then the mipmaps, and the format last.
     */
    class TextureProcessor : public ContentProcessor<Graphics::TextureContent, Graphics::TextureContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.TextureProcessor";

        /** @brief Initializes a processor with the measured defaults. */
        TextureProcessor() = default;

        /** @brief Destroys the processor. */
        ~TextureProcessor() override = default;

        /**
         * @brief Gets the colour keyed out of the texture.
         *
         * @return The key colour; magenta by default.
         */
        [[nodiscard]] Color getColorKeyColorProperty() const noexcept;

        /**
         * @brief Sets the colour keyed out of the texture.
         *
         * @param value The key colour.
         */
        void setColorKeyColorProperty(Color value) noexcept;

        /**
         * @brief Gets whether the key colour is turned transparent.
         *
         * @return true by default.
         */
        [[nodiscard]] bool getColorKeyEnabledProperty() const noexcept;

        /**
         * @brief Sets whether the key colour is turned transparent.
         *
         * @param value true to key the colour out.
         */
        void setColorKeyEnabledProperty(bool value) noexcept;

        /**
         * @brief Gets whether a mipmap chain is built.
         *
         * @return false by default; true for a model texture.
         */
        [[nodiscard]] bool getGenerateMipmapsProperty() const noexcept;

        /**
         * @brief Sets whether a mipmap chain is built.
         *
         * @param value true to build one.
         */
        void setGenerateMipmapsProperty(bool value) noexcept;

        /**
         * @brief Gets whether the colour channels are multiplied by the alpha channel.
         *
         * @return true by default.
         */
        [[nodiscard]] bool getPremultiplyAlphaProperty() const noexcept;

        /**
         * @brief Sets whether the colour channels are multiplied by the alpha channel.
         *
         * @param value true to premultiply.
         */
        void setPremultiplyAlphaProperty(bool value) noexcept;

        /**
         * @brief Gets whether the texture is resized to the next power of two.
         *
         * @return false by default.
         */
        [[nodiscard]] bool getResizeToPowerOfTwoProperty() const noexcept;

        /**
         * @brief Sets whether the texture is resized to the next power of two.
         *
         * @param value true to resize.
         */
        void setResizeToPowerOfTwoProperty(bool value) noexcept;

        /**
         * @brief Gets the format the texture is converted to.
         *
         * @return `Color` by default; `DxtCompressed` for a model texture.
         */
        [[nodiscard]] TextureProcessorOutputFormat getTextureFormatProperty() const noexcept;

        /**
         * @brief Sets the format the texture is converted to.
         *
         * @param value The wanted format.
         */
        void setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept;

        /**
         * @brief Processes the texture.
         *
         * @param input The texture to process.
         * @param context The processor context.
         * @return The processed texture, which is the input object itself.
         * @throws System::ArgumentNullException when the input is null.
         */
        [[nodiscard]] std::shared_ptr<Graphics::TextureContent> Process(
            const std::shared_ptr<Graphics::TextureContent>& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] virtual const std::string& GetTypeName() const;

    private:
        Color colorKeyColor_{255, 0, 255, 255};
        bool colorKeyEnabled_ = true;
        bool generateMipmaps_ = false;
        bool premultiplyAlpha_ = true;
        bool resizeToPowerOfTwo_ = false;
        TextureProcessorOutputFormat textureFormat_ = TextureProcessorOutputFormat::Color;
    };

    /**
     * @brief Processes a texture a sprite is drawn from: the texture processor's defaults exactly.
     */
    class SpriteTextureProcessor : public TextureProcessor
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.SpriteTextureProcessor";

        /** @brief Initializes a processor with the measured defaults. */
        SpriteTextureProcessor() = default;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };

    /**
     * @brief Processes a texture a model is drawn with: mipmapped and compressed by default.
     */
    class ModelTextureProcessor : public TextureProcessor
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelTextureProcessor";

        /** @brief Initializes a processor that builds mipmaps and compresses, as measured. */
        ModelTextureProcessor();

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;
    };
}
