// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"

#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidCastException.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"

#include <cstdint>
#include <optional>
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
    using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::Texture3D;
    using Microsoft::Xna::Framework::Graphics::TextureCube;

    namespace
    {
        // XNA EffectMaterialReader.IsAVectorOrASingle IL_0000-005f: a non-array Vector parameter
        // of one row and two to four columns, or a non-array 1x1 Scalar.
        bool IsAVectorOrASingle(const EffectParameter& parameter)
        {
            if (parameter.getElementsProperty().getCountProperty() != 0 ||
                parameter.getRowCountProperty() != 1)
            {
                return false;
            }
            const int columns = parameter.getColumnCountProperty();
            const EffectParameterClass parameterClass = parameter.getParameterClassProperty();
            return (parameterClass == EffectParameterClass::Vector && columns >= 2 && columns <= 4) ||
                   (parameterClass == EffectParameterClass::Scalar && columns == 1);
        }

        // XNA EffectMaterialReader.TryToSetParameter IL_001a-022c: one SetValue per stored type --
        // int[], bool[], float[], Vector2[]..Vector4[], Matrix[], int, bool, float,
        // Vector2..Vector4, Matrix, string, Texture -- and nothing at all for any other type
        // (a Quaternion or a Color is ignored there, not refused). Returns false for those.
        bool SetValueOfItsOwnType(EffectMaterial& material, EffectParameter& parameter,
                                  const std::any& value)
        {
            if (const auto* v = std::any_cast<std::vector<std::int32_t>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<bool>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<float>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<Vector2>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<Vector3>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<Vector4>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::vector<Matrix>>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::int32_t>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<bool>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<float>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<Vector2>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<Vector3>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<Vector4>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<Matrix>(&value)) { parameter.SetValue(*v); return true; }
            if (const auto* v = std::any_cast<std::string>(&value)) { parameter.SetValue(*v); return true; }

            // EffectParameter stores a raw Texture*, and the value table these come out of is a
            // local that dies with this reader. Pointing a parameter straight into it left every
            // textured material dereferencing freed memory at its first draw -- a crash inside
            // dynamic_cast, from whichever draw happened to reuse the block first. The material
            // takes ownership, so the pointer stays valid for as long as it can be drawn.
            if (const auto* texture = std::any_cast<std::shared_ptr<Texture2D>>(&value))
            {
                material.RetainParameterTextureEXT(*texture);
                parameter.SetValue(texture->get());
                return true;
            }
            if (const auto* texture = std::any_cast<Texture2D>(&value))
            {
                auto owned = std::make_shared<Texture2D>(*texture);
                parameter.SetValue(owned.get());
                material.RetainParameterTextureEXT(std::move(owned));
                return true;
            }
            if (const auto* texture = std::any_cast<std::shared_ptr<Texture3D>>(&value))
            {
                material.RetainParameterTextureEXT(*texture);
                parameter.SetValue(texture->get());
                return true;
            }
            if (const auto* texture = std::any_cast<TextureCube>(&value))
            {
                auto owned = std::make_shared<TextureCube>(*texture);
                parameter.SetValue(owned.get());
                material.RetainParameterTextureEXT(std::move(owned));
                return true;
            }
            return false;
        }

        // XNA EffectMaterialReader.TryToSetParameter: the SetValue of the stored type, and when it
        // is InvalidCastException (IL_0231-0340) a Vector2/3/4 widened to Vector4 -- (x, y, 0, 1),
        // (x, y, z, 1), as is -- then narrowed to a vector-or-scalar parameter's column count.
        // Any other InvalidCastException is rethrown (IL_033e).
        void ApplyParameterValue(EffectMaterial& material, EffectParameter& parameter,
                                 const std::any& value)
        {
            try
            {
                (void)SetValueOfItsOwnType(material, parameter, value);
                return;
            }
            catch (const System::InvalidCastException&)
            {
                std::optional<Vector4> widened;
                if (const auto* v2 = std::any_cast<Vector2>(&value)) { widened = Vector4(*v2, 0.0f, 1.0f); }
                else if (const auto* v3 = std::any_cast<Vector3>(&value)) { widened = Vector4(*v3, 1.0f); }
                else if (const auto* v4 = std::any_cast<Vector4>(&value)) { widened = *v4; }
                if (!widened.has_value() || !IsAVectorOrASingle(parameter)) { throw; }

                switch (parameter.getColumnCountProperty())
                {
                    case 1: parameter.SetValue(widened->X); break;
                    case 2: parameter.SetValue(Vector2(widened->X, widened->Y)); break;
                    case 3: parameter.SetValue(Vector3(widened->X, widened->Y, widened->Z)); break;
                    default: parameter.SetValue(*widened); break;
                }
            }
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
            // XNA passes the null reference to new EffectMaterial(null), whose Effect clone
            // constructor refuses it (Effect .ctor(Effect) IL_0006-0018).
            throw System::ArgumentNullException(
                "cloneSource", "EffectMaterialReader: the material names no effect to clone.");
        }

        auto material = std::make_shared<EffectMaterial>(**source);

        // XNA reads this as Dictionary<string, object> (EffectMaterialReader.Read IL_001c) and
        // skips a name the effect does not declare (TryToSetParameter IL_0000-000e).
        std::map<std::string, std::any> values =
            input.ReadObject<std::map<std::string, std::any>>();
        for (const auto& [name, value] : values)
        {
            EffectParameter* parameter = material->getParametersProperty()[name];
            if (parameter == nullptr) continue;
            ApplyParameterValue(*material, *parameter, value);
        }
        return material;
    }

    std::map<std::string, std::any> StringObjectDictionaryReader::Read(
        ContentReader& input, std::optional<std::map<std::string, std::any>> existingInstance)
    {
        const std::int32_t count = input.ReadInt32();
        // XNA DictionaryReader`2.Read IL_000c-000d: `new Dictionary(count)` refuses a negative
        // count with ArgumentOutOfRangeException; an existing instance gains nothing (TQ-033).
        if (count < 0)
        {
            if (!existingInstance.has_value()) throw System::ArgumentOutOfRangeException("capacity");
            return std::move(*existingInstance);
        }
        input.CheckCollectionElementCount(count, getTargetTypeNameProperty());

        std::map<std::string, std::any> map =
            existingInstance.value_or(std::map<std::string, std::any>{});
        for (std::int32_t i = 0; i < count; ++i)
        {
            // The key type is System.String, whose reader is a plain length-prefixed read.
            std::string key = input.ReadObject<std::string>();
            std::any value = input.ReadObject();
            // XNA DictionaryReader`2.Read IL_0032 adds through Dictionary.Add: a repeated key --
            // in the stream or already in existingInstance -- is ArgumentException.
            if (!map.emplace(std::move(key), std::move(value)).second)
            {
                throw System::ArgumentException("An item with the same key has already been added.");
            }
        }
        return map;
    }

    std::any ExternalReferenceReader::Read(ContentReader& input,
                                           std::optional<std::any> existingInstance)
    {
        (void)existingInstance;

        return input.ReadExternalReference();
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
