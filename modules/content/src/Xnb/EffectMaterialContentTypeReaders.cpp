// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Quaternion;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Content::ContentLoadException;
    using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
    using Microsoft::Xna::Framework::Graphics::EffectMaterial;
    using Microsoft::Xna::Framework::Graphics::EffectParameter;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace
    {
        // Applies one type-erased parameter value, covering the types the pipeline can store
        // in an EffectMaterial's table. An unknown type is refused rather than dropped: a
        // silently skipped parameter renders as a wrong colour with no diagnostic at all.
        void ApplyParameterValue(EffectParameter& parameter, const std::string& name,
                                 const std::any& value, const std::string& assetName)
        {
            if (const auto* texture = std::any_cast<std::shared_ptr<Texture2D>>(&value))
            {
                parameter.SetValue(texture->get());
                return;
            }
            if (const auto* texture = std::any_cast<Texture2D>(&value))
            {
                parameter.SetValue(const_cast<Texture2D*>(texture));
                return;
            }
            if (const auto* v = std::any_cast<std::int32_t>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<bool>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<float>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<Vector2>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<Vector3>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<Vector4>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<Matrix>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<Quaternion>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<std::vector<std::int32_t>>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<std::vector<float>>(&value)) { parameter.SetValue(*v); return; }
            if (const auto* v = std::any_cast<std::vector<Matrix>>(&value)) { parameter.SetValue(*v); return; }

            throw ContentLoadException(
                "'" + assetName + "': EffectMaterialReader cannot apply parameter '" + name +
                "': its stored value type is not one this build converts.");
        }
    }

    std::shared_ptr<Effect> EffectMaterialReader::Read(
        ContentReader& input, std::optional<std::shared_ptr<Effect>> existingInstance)
    {
        (void)existingInstance;

        // The referenced .fx asset loads as a shared_ptr<Effect>, the same erased type every
        // effect reader targets, so the reference is read at that type rather than at Effect
        // itself -- which is non-copyable and could not live in a std::optional at all.
        std::optional<std::shared_ptr<Effect>> source =
            input.ReadExternalReference<std::shared_ptr<Effect>>();
        if (!source.has_value() || *source == nullptr)
        {
            throw ContentLoadException(
                "'" + input.getAssetNameProperty() +
                "': EffectMaterialReader found no effect reference to clone.");
        }

        auto material = std::make_shared<EffectMaterial>(**source);

        // FNA reads this as Dictionary<string, object>; the keys name effect parameters the
        // pipeline resolved at build time, and a name the effect does not declare is skipped
        // rather than treated as an error, matching FNA's own Debug.WriteLine path.
        std::map<std::string, std::any> values =
            input.ReadObject<std::map<std::string, std::any>>();
        for (const auto& [name, value] : values)
        {
            EffectParameter* parameter = material->getParametersProperty()[name];
            if (parameter == nullptr) continue;
            ApplyParameterValue(*parameter, name, value, input.getAssetNameProperty());
        }
        return material;
    }

    std::map<std::string, std::any> StringObjectDictionaryReader::Read(
        ContentReader& input, std::optional<std::map<std::string, std::any>> existingInstance)
    {
        const std::int32_t count = input.ReadInt32();
        input.CheckCollectionElementCount(count, getTargetTypeNameProperty());

        std::map<std::string, std::any> map =
            existingInstance.value_or(std::map<std::string, std::any>{});
        for (std::int32_t i = 0; i < count; ++i)
        {
            // The key type is System.String, whose reader is a plain length-prefixed read.
            std::string key = input.ReadObject<std::string>();
            map.emplace(std::move(key), input.ReadObject());
        }
        return map;
    }

    std::any ExternalReferenceReader::Read(ContentReader& input,
                                           std::optional<std::any> existingInstance)
    {
        (void)existingInstance;

        std::optional<Texture2D> texture = input.ReadExternalReference<Texture2D>();
        if (!texture.has_value()) return {};
        return std::any(std::make_shared<Texture2D>(std::move(*texture)));
    }

    void RegisterEffectMaterialXnbReaders()
    {
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.EffectMaterialReader",
            [] { return std::make_unique<EffectMaterialReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.DictionaryReader`2[[System.String],[System.Object]]",
            [] { return std::make_unique<StringObjectDictionaryReader>(); });
        ContentTypeReaderManager::AddTypeCreator(
            "Microsoft.Xna.Framework.Content.ExternalReferenceReader",
            [] { return std::make_unique<ExternalReferenceReader>(); });
    }
}
