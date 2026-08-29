// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    namespace
    {
        GraphicsDevice& RequireGraphicsDevice(ContentReader& input, const char* readerName)
        {
            if (!input.getContentManagerProperty())
            {
                throw ContentLoadException(
                    std::string(readerName) +
                    ": no GraphicsDevice available (ContentManager was not set on this ContentReader).");
            }
            return input.getContentManagerProperty()->getGraphicsDeviceInternal();
        }
    }

    std::shared_ptr<Effect> BasicEffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance; // never provided: CanDeserializeIntoExistingObject defaults false, matching FNA

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        const XnbBasicEffectData decoded = DecodeBasicEffectXnbData(input, {});
        auto effect = std::make_shared<BasicEffect>(RequireGraphicsDevice(input, "BasicEffectReader"));
        if (texture.has_value())
        {
            effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*texture)));
            effect->setTextureEnabledProperty(true);
        }
        effect->setDiffuseColorProperty(decoded.diffuseColor);
        effect->setEmissiveColorProperty(decoded.emissiveColor);
        effect->setSpecularColorProperty(decoded.specularColor);
        effect->setSpecularPowerProperty(decoded.specularPower);
        effect->setAlphaProperty(decoded.alpha);
        effect->VertexColorEnabled = decoded.vertexColorEnabled;
        return effect;
    }

    std::shared_ptr<Effect> AlphaTestEffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance;

        auto effect = std::make_shared<AlphaTestEffect>(RequireGraphicsDevice(input, "AlphaTestEffectReader"));

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        if (texture.has_value())
        {
            effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*texture)));
        }
        effect->setAlphaFunctionProperty(static_cast<Microsoft::Xna::Framework::Graphics::CompareFunction>(input.ReadInt32()));
        effect->setReferenceAlphaProperty(static_cast<int32_t>(input.ReadUInt32()));
        effect->setDiffuseColorProperty(input.ReadVector3());
        effect->setAlphaProperty(input.ReadSingle());
        effect->setVertexColorEnabledProperty(input.ReadBoolean());
        return effect;
    }

    std::shared_ptr<Effect> DualTextureEffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance;

        auto effect = std::make_shared<DualTextureEffect>(RequireGraphicsDevice(input, "DualTextureEffectReader"));

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        if (texture.has_value())
        {
            effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*texture)));
        }
        std::optional<Texture2D> texture2 = input.ReadExternalReference<Texture2D>();
        if (texture2.has_value())
        {
            effect->SetOwnedTexture2(std::make_shared<Texture2D>(std::move(*texture2)));
        }
        effect->setDiffuseColorProperty(input.ReadVector3());
        effect->setAlphaProperty(input.ReadSingle());
        effect->setVertexColorEnabledProperty(input.ReadBoolean());
        return effect;
    }

    std::shared_ptr<Effect> EnvironmentMapEffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance;

        auto effect = std::make_shared<EnvironmentMapEffect>(RequireGraphicsDevice(input, "EnvironmentMapEffectReader"));

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        if (texture.has_value())
        {
            effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*texture)));
        }
        std::optional<TextureCube> environmentMap = input.ReadExternalReference<TextureCube>();
        if (environmentMap.has_value())
        {
            effect->SetOwnedEnvironmentMap(std::make_shared<TextureCube>(std::move(*environmentMap)));
        }
        effect->setEnvironmentMapAmountProperty(input.ReadSingle());
        effect->setEnvironmentMapSpecularProperty(input.ReadVector3());
        effect->setFresnelFactorProperty(input.ReadSingle());
        effect->setDiffuseColorProperty(input.ReadVector3());
        effect->setEmissiveColorProperty(input.ReadVector3());
        effect->setAlphaProperty(input.ReadSingle());
        return effect;
    }

    std::shared_ptr<Effect> SkinnedEffectReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance;

        auto effect = std::make_shared<SkinnedEffect>(RequireGraphicsDevice(input, "SkinnedEffectReader"));

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        if (texture.has_value())
        {
            effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*texture)));
        }
        effect->setWeightsPerVertexProperty(input.ReadInt32());
        effect->setDiffuseColorProperty(input.ReadVector3());
        effect->setEmissiveColorProperty(input.ReadVector3());
        effect->setSpecularColorProperty(input.ReadVector3());
        effect->setSpecularPowerProperty(input.ReadSingle());
        effect->setAlphaProperty(input.ReadSingle());
        return effect;
    }

    void RegisterStockEffectXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.BasicEffectReader",
            [] { return std::make_unique<BasicEffectReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.AlphaTestEffectReader",
            [] { return std::make_unique<AlphaTestEffectReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DualTextureEffectReader",
            [] { return std::make_unique<DualTextureEffectReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.EnvironmentMapEffectReader",
            [] { return std::make_unique<EnvironmentMapEffectReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.SkinnedEffectReader",
            [] { return std::make_unique<SkinnedEffectReader>(); });
    }
}
