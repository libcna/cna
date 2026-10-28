// SPDX-License-Identifier: MS-PL
// Effect: the .cnj effect reader (stock effects and compiled shaders) and the .xnb effect readers.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/ContentRoot.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Internal/GltfImport/CgltfFileCallbacks.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/Graphics/ModelMaterialVariantsEXT.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <filesystem>
#include <optional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "BuiltinContentLoaders.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        using namespace detail;

        Microsoft::Xna::Framework::Graphics::CompareFunction ParseCompareFunctionEXT(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::Graphics::CompareFunction;
            if (value == "Always") { return CompareFunction::Always; }
            if (value == "Never") { return CompareFunction::Never; }
            if (value == "Less") { return CompareFunction::Less; }
            if (value == "LessEqual") { return CompareFunction::LessEqual; }
            if (value == "Equal") { return CompareFunction::Equal; }
            if (value == "GreaterEqual") { return CompareFunction::GreaterEqual; }
            if (value == "Greater") { return CompareFunction::Greater; }
            if (value == "NotEqual") { return CompareFunction::NotEqual; }
            throw ContentLoadException(
                "Effect .cnj '" + path + "' has an unrecognized CompareFunction '" + value + "'.");
        }

        Vector3 ReadVector3FieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                                     Vector3 def, const std::string& path)
        {
            std::vector<float> arr;
            if (!TryReadFloatArrayField(obj, field, 3, arr, path)) { return def; }
            return Vector3(arr[0], arr[1], arr[2]);
        }

        float ReadFloatFieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                                 float def, const std::string& path)
        {
            const CNA::Internal::JsonValue* v = obj.FindMember(field);
            if (v == nullptr) { return def; }
            if (!v->IsNumber())
            {
                throw ContentLoadException(
                    "Effect .cnj '" + path + "': '" + field + "' must be numeric.");
            }
            return static_cast<float>(v->numberValue);
        }

        bool ReadBoolFieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                               bool def, const std::string& path)
        {
            const CNA::Internal::JsonValue* v = obj.FindMember(field);
            if (v == nullptr) { return def; }
            if (v->type != CNA::Internal::JsonType::Boolean)
            {
                throw ContentLoadException(
                    "Effect .cnj '" + path + "': '" + field + "' must be a boolean.");
            }
            return v->boolValue;
        }

        // CNJ CNB-45: RegisterTypeReader<T>() allows exactly one reader per T, and this
        // class already owns std::shared_ptr<Effect> for the pre-existing custom-GLSL "Effect"
        // .cnj shape -- so the 5 stock effects (BasicEffect/AlphaTestEffect/DualTextureEffect/
        // EnvironmentMapEffect/SkinnedEffect) are dispatched from inside this same reader by
        // .cnj "type", rather than each getting its own registration (which would collide).
        // Field lists below are a direct JSON port of StockEffectContentTypeReaders.cpp's
        // already-FNA-verified per-effect field order.
        class EffectTypeReader : public LooseFileContentTypeReader<std::shared_ptr<Graphics::Effect>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            std::shared_ptr<Graphics::Effect> Read(const std::string& path, ContentManager& cm) override
            {
                // If path doesn't already end with .cnj, append it.
                std::string jsonPath = path;
                const std::string ext = ".cnj";
                if (jsonPath.size() < ext.size() ||
                    jsonPath.substr(jsonPath.size() - ext.size()) != ext)
                {
                    jsonPath += ext;
                }

                const std::string jsonText = ReadTextFile(jsonPath);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(jsonText);
                CNA::Internal::ValidateCnjEnvelopeBaseline(envelope, jsonPath);

                // Confirm envelope.type is one of the 6 valid names *before* any other check --
                // otherwise a .cnj with both an unrecognized type and a 'sourceFile' field gets
                // the misleading "sourceFile not supported" message instead of the real problem
                // (an unrecognized 'type'). Found by adversarial review, 2026-07-17.
                const bool isStockEffect =
                    envelope.type == "BasicEffect" || envelope.type == "AlphaTestEffect" ||
                    envelope.type == "DualTextureEffect" || envelope.type == "EnvironmentMapEffect" ||
                    envelope.type == "SkinnedEffect";
                if (envelope.type != "Effect" && !isStockEffect)
                {
                    throw ContentLoadException(
                        "ContentManager: '" + jsonPath + "' has type '" + envelope.type + "', but an "
                        "Effect .cnj must be one of 'Effect', 'BasicEffect', 'AlphaTestEffect', "
                        "'DualTextureEffect', 'EnvironmentMapEffect', or 'SkinnedEffect'.");
                }

                RejectSourceFileForSelfContainedCnj(envelope, envelope.type, jsonPath);

                if (envelope.type == "Effect")
                {
                    return ReadCustomGlslEffect(jsonText, jsonPath, cm);
                }
                return ReadStockEffect(CNA::Internal::ParseJson(jsonText), envelope.type, jsonPath, cm);
            }

        private:
            static std::shared_ptr<Graphics::Effect> ReadCustomGlslEffect(
                const std::string& jsonText, const std::string& jsonPath, ContentManager& cm)
            {
                const std::string vertRel = ExtractJsonStringField(jsonText, "vertex");
                const std::string fragRel = ExtractJsonStringField(jsonText, "fragment");

                if (vertRel.empty() || fragRel.empty())
                {
                    throw ContentLoadException(
                        "ShaderEffect descriptor missing 'vertex' or 'fragment' field: " + jsonPath);
                }

                const std::string vertPath = ResolveRootRelativeSidecarPath(
                    cm, jsonPath, "vertex", vertRel);
                const std::string fragPath = ResolveRootRelativeSidecarPath(
                    cm, jsonPath, "fragment", fragRel);

                return std::make_shared<Graphics::ShaderEffect>(
                    cm.getGraphicsDeviceInternal(),
                    ReadTextFile(vertPath),
                    ReadTextFile(fragPath));
            }

            static std::optional<Graphics::Texture2D> LoadOptionalTexture2D(
                const CNA::Internal::JsonValue& root, const char* field,
                const std::string& jsonPath, ContentManager& cm)
            {
                const CNA::Internal::JsonValue* v = root.FindMember(field);
                if (v == nullptr) { return std::nullopt; }
                if (!v->IsString() || v->stringValue.empty())
                {
                    throw ContentLoadException(std::string("Effect .cnj: '") + field + "' must be a non-empty string.");
                }
                return cm.Load<Graphics::Texture2D>(ResolveRootRelativeAssetName(
                    cm, jsonPath, field, v->stringValue));
            }

            static std::shared_ptr<Graphics::Effect> ReadStockEffect(
                const CNA::Internal::JsonValue& root, const std::string& type,
                const std::string& jsonPath, ContentManager& cm)
            {
                using namespace Graphics;
                GraphicsDevice& gd = cm.getGraphicsDeviceInternal();

                if (type == "BasicEffect")
                {
                    auto effect = std::make_shared<BasicEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                        effect->setTextureEnabledProperty(true);
                    }
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                    effect->setSpecularColorProperty(ReadVector3FieldEXT(root, "specularColor", Vector3::One, jsonPath));
                    effect->setSpecularPowerProperty(ReadFloatFieldEXT(root, "specularPower", 16.0f, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->VertexColorEnabled = ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath);
                    return effect;
                }
                if (type == "AlphaTestEffect")
                {
                    auto effect = std::make_shared<AlphaTestEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    const CNA::Internal::JsonValue* alphaFn = root.FindMember("alphaFunction");
                    effect->setAlphaFunctionProperty(
                        alphaFn != nullptr && alphaFn->IsString()
                            ? ParseCompareFunctionEXT(alphaFn->stringValue, jsonPath)
                            : CompareFunction::Greater);
                    effect->setReferenceAlphaProperty(
                        static_cast<int32_t>(ReadFloatFieldEXT(root, "referenceAlpha", 0.0f, jsonPath)));
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->setVertexColorEnabledProperty(ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath));
                    return effect;
                }
                if (type == "DualTextureEffect")
                {
                    auto effect = std::make_shared<DualTextureEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    if (auto tex2 = LoadOptionalTexture2D(root, "texture2", jsonPath, cm))
                    {
                        effect->SetOwnedTexture2(std::make_shared<Texture2D>(std::move(*tex2)));
                    }
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->setVertexColorEnabledProperty(ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath));
                    return effect;
                }
                if (type == "EnvironmentMapEffect")
                {
                    auto effect = std::make_shared<EnvironmentMapEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    if (const CNA::Internal::JsonValue* envMap = root.FindMember("environmentMap"))
                    {
                        if (!envMap->IsString() || envMap->stringValue.empty())
                        {
                            throw ContentLoadException(
                                "Effect .cnj '" + jsonPath + "': 'environmentMap' must be a non-empty string.");
                        }
                        Graphics::TextureCube cube = cm.Load<Graphics::TextureCube>(
                            ResolveRootRelativeAssetName(
                                cm, jsonPath, "environmentMap", envMap->stringValue));
                        effect->SetOwnedEnvironmentMap(std::make_shared<TextureCube>(std::move(cube)));
                    }
                    effect->setEnvironmentMapAmountProperty(ReadFloatFieldEXT(root, "environmentMapAmount", 1.0f, jsonPath));
                    effect->setEnvironmentMapSpecularProperty(ReadVector3FieldEXT(root, "environmentMapSpecular", Vector3::Zero, jsonPath));
                    effect->setFresnelFactorProperty(ReadFloatFieldEXT(root, "fresnelFactor", 1.0f, jsonPath));
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    return effect;
                }
                // SkinnedEffect (the only remaining branch this method is ever called with).
                auto effect = std::make_shared<SkinnedEffect>(gd);
                if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                {
                    effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                }
                effect->setWeightsPerVertexProperty(
                    static_cast<int32_t>(ReadFloatFieldEXT(root, "weightsPerVertex", 4.0f, jsonPath)));
                effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                effect->setSpecularColorProperty(ReadVector3FieldEXT(root, "specularColor", Vector3::One, jsonPath));
                effect->setSpecularPowerProperty(ReadFloatFieldEXT(root, "specularPower", 16.0f, jsonPath));
                effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                return effect;
            }
        };
    } // anonymous namespace

    void detail::BuiltinContent<std::shared_ptr<Graphics::Effect>>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<std::shared_ptr<Graphics::Effect>, EffectTypeReader>();
        // An .xnb effect may be a stock effect or an effect material, whose parameters name
        // textures of every kind.
        detail::RegisterValueXnbReaders();
        detail::RegisterDeclaredXnbReaders([] {
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
            CNA::Internal::Xnb::RegisterTexture3DXnbReader();
            CNA::Internal::Xnb::RegisterTextureCubeXnbReader();
            CNA::Internal::Xnb::RegisterStockEffectXnbReaders();
            CNA::Internal::Xnb::RegisterEffectXnbReader();
            CNA::Internal::Xnb::RegisterEffectMaterialXnbReaders();
        });
    }
}
