// SPDX-License-Identifier: MS-PL
// SoundEffect: loose files, .cnb and .xnb, and Load<SoundEffect>'s own specialisation.
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
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace
    {
        using namespace detail;

        class SoundEffectTypeReader : public LooseFileContentTypeReader<Audio::SoundEffect>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".wav"};
            }

            Audio::SoundEffect Read(const std::string& path, ContentManager& cm) override
            {
                if (CNA::Internal::PathFromUtf8(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                return Audio::SoundEffect(path);
            }

        private:
            static Audio::SoundEffect ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "SoundEffect", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: SoundEffect .cnj '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile SoundEffect .cnj is not "
                        "supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                return cm.Load<Audio::SoundEffect>(resolved.logicalName);
            }
        };

        // ---------------------------------------------------------------------------
        // Compiled .cnb SoundEffect loader (plans/plan_cnb.md CNBF-103A)
        //
        // Boxed as a shared_ptr rather than by value, and not for the reason Texture3D is:
        // SoundEffect is MOVE-ONLY, and std::any requires its contained type to be copy
        // constructible, so a bare SoundEffect cannot be put in one at all. The specialisation of
        // Load<SoundEffect> below unwraps and moves out of it.
        // ---------------------------------------------------------------------------

        std::shared_ptr<Audio::SoundEffect> BuildSoundEffectFromCnbEXT(
            const CNA::Content::Cnb::CnbSoundEffectData& data, const std::string& assetName)
        {
            const auto channels = data.channels == 2u ? Audio::AudioChannels::Stereo
                                                      : Audio::AudioChannels::Mono;
            // A schema-2 file may store its samples 8-bit, which is what the source declared and
            // what the build now preserves; `SoundEffect` takes 16-bit PCM, so the width is given
            // up here and nowhere earlier (plans/plan_xna_sample_xnb_sweep.md `XNASWEEP-197`).
            const std::vector<std::uint8_t> samples =
                CNA::Content::Cnb::CnbSoundEffectSamplesAsPcm16(data);
            std::shared_ptr<Audio::SoundEffect> result;
            if (data.loopLength == 0u)
            {
                result = std::make_shared<Audio::SoundEffect>(
                    samples, static_cast<SharpRuntime::intcs>(data.sampleRate), channels);
            }
            else
            {
                result = std::make_shared<Audio::SoundEffect>(
                    samples, 0, static_cast<SharpRuntime::intcs>(samples.size()),
                    static_cast<SharpRuntime::intcs>(data.sampleRate), channels,
                    static_cast<SharpRuntime::intcs>(data.loopStart),
                    static_cast<SharpRuntime::intcs>(data.loopLength));
            }
            result->setNameProperty(assetName);
            return result;
        }
    } // anonymous namespace

    void detail::BuiltinContent<Audio::SoundEffect>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::SoundEffect,
            "Microsoft.Xna.Framework.Audio.SoundEffect",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager&,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildSoundEffectFromCnbEXT(
                    CNA::Content::Cnb::DecodeSoundEffectFromCnb(document), assetName));
            });
    }

    void detail::BuiltinContent<Audio::SoundEffect>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Audio::SoundEffect, SoundEffectTypeReader>();
        RegisterCnbLoader();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterSoundEffectXnbReader);
    }

    template<>
    Audio::SoundEffect ContentManager::Load<Audio::SoundEffect>(const std::string& assetName)
    {
        if (disposed_)
            throw System::ObjectDisposedException("ContentManager");

        log::Debug(std::string("Loading asset: ") + assetName);

        // Preserve the explicit strict stream-only option, as in the generic Load<T>().
        if (loadsThroughOpenStream_)
            return ReadAsset<Audio::SoundEffect>(assetName, {});

        detail::BuiltinContent<Audio::SoundEffect>::Register(*this);

        // Move-only SoundEffect skips the generic cache, but shares its virtual stream seam
        // and .xnb-first precedence. Only the base missing-XNB case permits lower tiers.
        if (const auto stream = TryOpenCompiledStream(assetName))
        {
            const auto xnbBytes = ReadStreamToEnd(*stream, assetName);
            const std::string xnbCandidate =
                ResolveExistingAssetPath(BuildAssetPath(assetName) + ".xnb");
            return LoadXnbAsset<Audio::SoundEffect>(xnbBytes, xnbCandidate, assetName);
        }

        // plans/plan_cnb.md CNBF-103A. This tier was MISSING: because SoundEffect has its own
        // Load<> specialisation, it never ran the generic template's .cnb step, so a
        // SoundEffect.cnb would have fallen straight through to the loose-file reader and the
        // documented precedence (xnb > cnb > literal > cnj > loose) would have held for every
        // asset type except this one. Added with the same two shapes the generic tier has: the
        // implicit "<name>.cnb", and a caller who spells the extension out.
        //
        // Through shared_ptr because SoundEffect is move-only and std::any cannot hold it.
        const std::string cnbCandidate =
            ResolveExistingAssetPath(BuildAssetPath(assetName) + ".cnb");
        std::vector<std::uint8_t> cnbBytes;
        if (TryReadAssetBytes(cnbCandidate, cnbBytes))
        {
            auto result = LoadCnbAsset<std::shared_ptr<Audio::SoundEffect>>(
                std::move(cnbBytes), cnbCandidate, assetName);
            RecordLoadedAsset(result);
            return std::move(*result);
        }
        if (assetName.size() > 4 && assetName.compare(assetName.size() - 4, 4, ".cnb") == 0)
        {
            const std::string literalCnb = ResolveExistingAssetPath(BuildAssetPath(assetName));
            std::vector<std::uint8_t> literalBytes;
            if (TryReadAssetBytes(literalCnb, literalBytes))
            {
                auto result = LoadCnbAsset<std::shared_ptr<Audio::SoundEffect>>(
                    std::move(literalBytes), literalCnb, assetName);
                RecordLoadedAsset(result);
                return std::move(*result);
            }
        }

        auto readerIt = typeReaders_.find(std::type_index(typeid(Audio::SoundEffect)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<LooseFileContentTypeReader<Audio::SoundEffect>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): Reader is null for asset '")
                + assetName + "'.");

        LooseFileContentTypeReader<Audio::SoundEffect>& reader = **readerPtr;
        const std::string resolvedPath = ResolveAssetPath(assetName, reader);

        auto result = reader.Read(resolvedPath, *this);
        RecordLoadedAsset(result);
        return result;
    }
}
