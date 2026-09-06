// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"

#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    void MaterialProcessor::DescribeParameters(ProcessorParameterBindings<MaterialProcessor>& bindings)
    {
        bindings.Add<Color>("ColorKeyColor", &MaterialProcessor::getColorKeyColorProperty,
                            &MaterialProcessor::setColorKeyColorProperty);
        bindings.Add<bool>("ColorKeyEnabled", &MaterialProcessor::getColorKeyEnabledProperty,
                           &MaterialProcessor::setColorKeyEnabledProperty);
        bindings.AddEnum<MaterialProcessorDefaultEffect>(
            "DefaultEffect", &MaterialProcessor::getDefaultEffectProperty,
            &MaterialProcessor::setDefaultEffectProperty,
            DeclaredEnumSpellings<MaterialProcessorDefaultEffect>());
        bindings.Add<bool>("GenerateMipmaps", &MaterialProcessor::getGenerateMipmapsProperty,
                           &MaterialProcessor::setGenerateMipmapsProperty);
        bindings.Add<bool>("PremultiplyTextureAlpha", &MaterialProcessor::getPremultiplyTextureAlphaProperty,
                           &MaterialProcessor::setPremultiplyTextureAlphaProperty);
        bindings.Add<bool>("ResizeTexturesToPowerOfTwo", &MaterialProcessor::getResizeTexturesToPowerOfTwoProperty,
                           &MaterialProcessor::setResizeTexturesToPowerOfTwoProperty);
        bindings.AddEnum<TextureProcessorOutputFormat>(
            "TextureFormat", &MaterialProcessor::getTextureFormatProperty,
            &MaterialProcessor::setTextureFormatProperty,
            DeclaredEnumSpellings<TextureProcessorOutputFormat>());
    }

    Color MaterialProcessor::getColorKeyColorProperty() const noexcept { return colorKeyColor_; }

    void MaterialProcessor::setColorKeyColorProperty(Color value) noexcept { colorKeyColor_ = value; }

    bool MaterialProcessor::getColorKeyEnabledProperty() const noexcept { return colorKeyEnabled_; }

    void MaterialProcessor::setColorKeyEnabledProperty(bool value) noexcept { colorKeyEnabled_ = value; }

    MaterialProcessorDefaultEffect MaterialProcessor::getDefaultEffectProperty() const noexcept
    {
        return defaultEffect_;
    }

    void MaterialProcessor::setDefaultEffectProperty(MaterialProcessorDefaultEffect value) noexcept
    {
        defaultEffect_ = value;
    }

    bool MaterialProcessor::getGenerateMipmapsProperty() const noexcept { return generateMipmaps_; }

    void MaterialProcessor::setGenerateMipmapsProperty(bool value) noexcept { generateMipmaps_ = value; }

    bool MaterialProcessor::getPremultiplyTextureAlphaProperty() const noexcept { return premultiplyTextureAlpha_; }

    void MaterialProcessor::setPremultiplyTextureAlphaProperty(bool value) noexcept
    {
        premultiplyTextureAlpha_ = value;
    }

    bool MaterialProcessor::getResizeTexturesToPowerOfTwoProperty() const noexcept
    {
        return resizeTexturesToPowerOfTwo_;
    }

    void MaterialProcessor::setResizeTexturesToPowerOfTwoProperty(bool value) noexcept
    {
        resizeTexturesToPowerOfTwo_ = value;
    }

    TextureProcessorOutputFormat MaterialProcessor::getTextureFormatProperty() const noexcept
    {
        return textureFormat_;
    }

    void MaterialProcessor::setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept
    {
        textureFormat_ = value;
    }

    std::shared_ptr<Graphics::MaterialContent> MaterialProcessor::Process(
        const std::shared_ptr<Graphics::MaterialContent>& input, ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // Each texture is rebuilt and its reference replaced, in the order the collection holds
        // them (measured, materialprocessor/two_textures).
        Graphics::TextureReferenceDictionary& textures = input->getTexturesProperty();
        for (const std::string& key : textures.getKeysProperty())
        {
            std::shared_ptr<ExternalReference<Graphics::TextureContent>> texture;
            if (!textures.TryGetValue(key, texture) || texture == nullptr)
            {
                continue;
            }
            textures.Set(key, BuildTexture(key, texture, context));
        }
        if (const auto effectMaterial = std::dynamic_pointer_cast<Graphics::EffectMaterialContent>(input))
        {
            if (const std::shared_ptr<ExternalReference<Graphics::EffectContent>> effect =
                    effectMaterial->getEffectProperty())
            {
                effectMaterial->setCompiledEffectProperty(BuildEffect(effect, context));
            }
        }
        return input;
    }

    std::shared_ptr<ExternalReference<Graphics::TextureContent>> MaterialProcessor::BuildTexture(
        const std::string& textureName, const std::shared_ptr<ExternalReference<Graphics::TextureContent>>& texture,
        ContentProcessorContext& context)
    {
        (void)textureName;
        // The parameters the runtime passes on, name for name (measured,
        // materialprocessor/properties_forwarded).
        OpaqueDataDictionary parameters;
        parameters.SetValue<Color>("ColorKeyColor", colorKeyColor_);
        parameters.SetValue<bool>("ColorKeyEnabled", colorKeyEnabled_);
        parameters.SetValue<bool>("GenerateMipmaps", generateMipmaps_);
        parameters.SetValue<bool>("PremultiplyAlpha", premultiplyTextureAlpha_);
        parameters.SetValue<bool>("ResizeToPowerOfTwo", resizeTexturesToPowerOfTwo_);
        parameters.SetValue<TextureProcessorOutputFormat>("TextureFormat", textureFormat_);
        return std::make_shared<ExternalReference<Graphics::TextureContent>>(
            context.BuildAsset<Graphics::TextureContent, Graphics::TextureContent>(*texture, "TextureProcessor",
                                                                                  parameters));
    }

    std::shared_ptr<ExternalReference<CompiledEffectContent>> MaterialProcessor::BuildEffect(
        const std::shared_ptr<ExternalReference<Graphics::EffectContent>>& effect, ContentProcessorContext& context)
    {
        // The effect is built with no parameters at all (measured, materialprocessor/effect_material).
        return std::make_shared<ExternalReference<CompiledEffectContent>>(
            context.BuildAsset<Graphics::EffectContent, CompiledEffectContent>(*effect, "EffectProcessor"));
    }

    const std::string& MaterialProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
