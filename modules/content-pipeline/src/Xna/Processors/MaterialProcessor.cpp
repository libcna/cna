// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/MaterialProcessor.hpp"

#include <memory>
#include <string>
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

    namespace
    {
        /**
         * @brief Copies a stock material into another stock material of the target's type.
         *
         * The opaque data is carried across entry for entry, in the order it was given, and so are
         * the texture references. What the target type does *not* have is simply not read back out
         * of it later -- the genuine processor's `EnvironmentMapMaterialContent` still carries the
         * `SpecularColor` and `SpecularPower` its `BasicMaterialContent` had, and the writer
         * ignores them (measured, `tools/xna-pipeline-oracle/modelroot/` with
         * `CNA_MODELROOT_MATERIALS=1`; plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-175`).
         */
        /**
         * @brief Whether a material's own effect reads the texture stored under a key.
         *
         * An `EffectMaterialContent` names its own parameters, so every texture it carries is one
         * of them; a stock material reads only the keys its effect declares.
         */
        [[nodiscard]] bool BuildsTexture(const Graphics::MaterialContent& material,
                                         const std::string& key)
        {
            if (dynamic_cast<const Graphics::EffectMaterialContent*>(&material) != nullptr)
            {
                return true;
            }
            if (key == std::string(Graphics::BasicMaterialContent::TextureKey))
            {
                return true;
            }
            if (const auto* dual = dynamic_cast<const Graphics::DualTextureMaterialContent*>(&material))
            {
                (void)dual;
                return key == std::string(Graphics::DualTextureMaterialContent::Texture2Key);
            }
            if (const auto* environment =
                    dynamic_cast<const Graphics::EnvironmentMapMaterialContent*>(&material))
            {
                (void)environment;
                return key == std::string(Graphics::EnvironmentMapMaterialContent::EnvironmentMapKey);
            }
            return false;
        }

        template<typename T>
        [[nodiscard]] std::shared_ptr<Graphics::MaterialContent> CopyMaterialInto(
            const std::shared_ptr<Graphics::MaterialContent>& source)
        {
            auto made = std::make_shared<T>();
            made->setNameProperty(source->getNameProperty());
            made->setIdentityProperty(source->getIdentityProperty());
            for (const std::string& key : source->getOpaqueDataProperty().getKeysProperty())
            {
                ContentObject stored;
                if (source->getOpaqueDataProperty().TryGetValue(key, stored))
                {
                    made->getOpaqueDataProperty().Set(key, stored);
                }
            }
            for (const std::string& key : source->getTexturesProperty().getKeysProperty())
            {
                std::shared_ptr<ExternalReference<Graphics::TextureContent>> texture;
                if (source->getTexturesProperty().TryGetValue(key, texture))
                {
                    made->getTexturesProperty().Set(key, texture);
                }
            }
            return made;
        }
    }

    std::shared_ptr<Graphics::MaterialContent> MaterialProcessor::Process(
        const std::shared_ptr<Graphics::MaterialContent>& input, ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // `DefaultEffect` decides what a stock material becomes: a `BasicMaterialContent` is
        // answered as itself for `BasicEffect` and as the matching content type for each of the
        // other four, carrying the same opaque data entry for entry and the same textures --
        // including the ones the new type has no property for, which are simply never read back
        // out of it. Measured on the genuine processor over all five values
        // (tests/reference/xna40/graphics/graphics-content-oracle.json, `materialprocessor/default_effect_*`;
        // plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-175`).
        std::shared_ptr<Graphics::MaterialContent> converted = input;
        if (std::dynamic_pointer_cast<Graphics::BasicMaterialContent>(input) != nullptr)
        {
            switch (getDefaultEffectProperty())
            {
                case MaterialProcessorDefaultEffect::BasicEffect:
                    break;
                case MaterialProcessorDefaultEffect::SkinnedEffect:
                    converted = CopyMaterialInto<Graphics::SkinnedMaterialContent>(input);
                    break;
                case MaterialProcessorDefaultEffect::EnvironmentMapEffect:
                    converted = CopyMaterialInto<Graphics::EnvironmentMapMaterialContent>(input);
                    break;
                case MaterialProcessorDefaultEffect::DualTextureEffect:
                    converted = CopyMaterialInto<Graphics::DualTextureMaterialContent>(input);
                    break;
                case MaterialProcessorDefaultEffect::AlphaTestEffect:
                    converted = CopyMaterialInto<Graphics::AlphaTestMaterialContent>(input);
                    break;
            }
        }
        // Each texture is rebuilt and its reference replaced, in the order the collection holds
        // them (measured, materialprocessor/two_textures) -- but only the ones the effect this
        // material *is* actually uses. A stock material carries whatever channels the importer
        // answered, and the genuine processor opens none of the others: SAMPLE-037's `head.fbx`
        // names a `Head_Spec.TGA` the sample does not ship, its material carries the reference,
        // and the genuine `MaterialProcessor` builds `Head_Diff.tga` and nothing else. Measured
        // through `tools/xna-pipeline-oracle/modelroot/` with `CNA_MODELROOT_MATERIALS=1`, which
        // records every `BuildAsset` the processor asks for
        // (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-175`).
        Graphics::TextureReferenceDictionary& textures = converted->getTexturesProperty();
        for (const std::string& key : textures.getKeysProperty())
        {
            if (!BuildsTexture(*converted, key))
            {
                continue;
            }
            std::shared_ptr<ExternalReference<Graphics::TextureContent>> texture;
            if (!textures.TryGetValue(key, texture) || texture == nullptr)
            {
                continue;
            }
            textures.Set(key, BuildTexture(key, texture, context));
        }
        if (const auto effectMaterial = std::dynamic_pointer_cast<Graphics::EffectMaterialContent>(converted))
        {
            if (const std::shared_ptr<ExternalReference<Graphics::EffectContent>> effect =
                    effectMaterial->getEffectProperty())
            {
                effectMaterial->setCompiledEffectProperty(BuildEffect(effect, context));
            }
        }
        return converted;
    }

    std::shared_ptr<ExternalReference<Graphics::TextureContent>> MaterialProcessor::BuildTexture(
        const std::string& textureName, const std::shared_ptr<ExternalReference<Graphics::TextureContent>>& texture,
        ContentProcessorContext& context)
    {
        (void)textureName;
        // The parameters the runtime passes on, name for name (measured,
        // materialprocessor/properties_forwarded).
        OpaqueDataDictionary parameters;
        parameters.SetValue<Color>("ColorKeyColor", getColorKeyColorProperty());
        parameters.SetValue<bool>("ColorKeyEnabled", getColorKeyEnabledProperty());
        parameters.SetValue<bool>("GenerateMipmaps", getGenerateMipmapsProperty());
        parameters.SetValue<bool>("PremultiplyAlpha", getPremultiplyTextureAlphaProperty());
        parameters.SetValue<bool>("ResizeToPowerOfTwo", getResizeTexturesToPowerOfTwoProperty());
        parameters.SetValue<TextureProcessorOutputFormat>("TextureFormat", getTextureFormatProperty());
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
