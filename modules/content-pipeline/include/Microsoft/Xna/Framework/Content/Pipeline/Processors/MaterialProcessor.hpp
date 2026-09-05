// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/EffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MaterialContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/TextureContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/CompiledEffectContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Processes a material: builds each texture it names and, for an effect material, the
     *        effect it names.
     *
     * The material comes back as the same object, with each texture reference replaced by the
     * built one and an effect material's `CompiledEffect` filled in -- measured on the XNA 4.0
     * runtime against a build context that records what it is asked to build
     * (`tests/reference/xna40/graphics/graphics-content-oracle.json`, cases `materialprocessor/*`).
     */
    class MaterialProcessor : public ContentProcessor<Graphics::MaterialContent, Graphics::MaterialContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.MaterialProcessor";

        /** @brief Initializes a processor with the measured defaults. */
        MaterialProcessor() = default;

        /** @brief Destroys the processor. */
        ~MaterialProcessor() override = default;

        /**
         * @brief Gets the colour keyed out of every texture this material names.
         * @return The key colour; magenta by default.
         */
        [[nodiscard]] Color getColorKeyColorProperty() const noexcept;
        /**
         * @brief Sets the colour keyed out of every texture this material names.
         * @param value The key colour.
         */
        void setColorKeyColorProperty(Color value) noexcept;

        /**
         * @brief Gets whether the key colour is turned transparent.
         * @return true by default.
         */
        [[nodiscard]] bool getColorKeyEnabledProperty() const noexcept;
        /**
         * @brief Sets whether the key colour is turned transparent.
         * @param value true to key the colour out.
         */
        void setColorKeyEnabledProperty(bool value) noexcept;

        /**
         * @brief Gets the effect a material with none of its own is given.
         * @return `BasicEffect` by default.
         */
        [[nodiscard]] MaterialProcessorDefaultEffect getDefaultEffectProperty() const noexcept;
        /**
         * @brief Sets the effect a material with none of its own is given.
         * @param value The wanted effect.
         */
        void setDefaultEffectProperty(MaterialProcessorDefaultEffect value) noexcept;

        /**
         * @brief Gets whether each texture is given a mipmap chain.
         * @return true by default, unlike the texture processor's own default.
         */
        [[nodiscard]] bool getGenerateMipmapsProperty() const noexcept;
        /**
         * @brief Sets whether each texture is given a mipmap chain.
         * @param value true to build one.
         */
        void setGenerateMipmapsProperty(bool value) noexcept;

        /**
         * @brief Gets whether each texture's colour is multiplied by its alpha.
         * @return true by default.
         */
        [[nodiscard]] bool getPremultiplyTextureAlphaProperty() const noexcept;
        /**
         * @brief Sets whether each texture's colour is multiplied by its alpha.
         * @param value true to premultiply.
         */
        void setPremultiplyTextureAlphaProperty(bool value) noexcept;

        /**
         * @brief Gets whether each texture is resized to the next power of two.
         * @return false by default.
         */
        [[nodiscard]] bool getResizeTexturesToPowerOfTwoProperty() const noexcept;
        /**
         * @brief Sets whether each texture is resized to the next power of two.
         * @param value true to resize.
         */
        void setResizeTexturesToPowerOfTwoProperty(bool value) noexcept;

        /**
         * @brief Gets the format each texture is converted to.
         * @return `DxtCompressed` by default, unlike the texture processor's own default.
         */
        [[nodiscard]] TextureProcessorOutputFormat getTextureFormatProperty() const noexcept;
        /**
         * @brief Sets the format each texture is converted to.
         * @param value The wanted format.
         */
        void setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept;

        /**
         * @brief Processes the material.
         *
         * @param input The material to process.
         * @param context The processor context, which builds the textures and the effect.
         * @return The material, which is the input object itself.
         * @throws System::ArgumentNullException when the input is null.
         */
        [[nodiscard]] std::shared_ptr<Graphics::MaterialContent> Process(
            const std::shared_ptr<Graphics::MaterialContent>& input, ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] virtual const std::string& GetTypeName() const;

    protected:
        /**
         * @brief Builds one texture the material names, through the texture processor.
         *
         * @param textureName The key the material holds the texture under.
         * @param texture The reference to build.
         * @param context The processor context.
         * @return The built texture's reference.
         */
        [[nodiscard]] virtual std::shared_ptr<ExternalReference<Graphics::TextureContent>> BuildTexture(
            const std::string& textureName, const std::shared_ptr<ExternalReference<Graphics::TextureContent>>& texture,
            ContentProcessorContext& context);

        /**
         * @brief Builds the effect the material names, through the effect processor.
         *
         * @param effect The reference to build.
         * @param context The processor context.
         * @return The compiled effect's reference.
         */
        [[nodiscard]] virtual std::shared_ptr<ExternalReference<CompiledEffectContent>> BuildEffect(
            const std::shared_ptr<ExternalReference<Graphics::EffectContent>>& effect,
            ContentProcessorContext& context);

    private:
        Color colorKeyColor_{255, 0, 255, 255};
        bool colorKeyEnabled_ = true;
        MaterialProcessorDefaultEffect defaultEffect_ = MaterialProcessorDefaultEffect::BasicEffect;
        bool generateMipmaps_ = true;
        bool premultiplyTextureAlpha_ = true;
        bool resizeTexturesToPowerOfTwo_ = false;
        TextureProcessorOutputFormat textureFormat_ = TextureProcessorOutputFormat::DxtCompressed;
    };
}
