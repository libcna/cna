// SPDX-License-Identifier: MS-PL
// Song and Video: loose media files, .cnb and .xnb.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/ContentManagerAccess.hpp"
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
        using CNA::Internal::ContentManagerAccess;

        class SongTypeReader : public LooseFileContentTypeReader<Media::Song>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".mp3", ".ogg", ".wav", ".flac", ".opus", ".aac", ".wma"};
            }

            Media::Song Read(const std::string& path, ContentManager& /*cm*/) override
            {
                const std::string name =
                    CNA::Internal::PathToUtf8(CNA::Internal::PathFromUtf8(path).stem());
                return Media::Song(path, name);
            }
        };

        class VideoTypeReader : public LooseFileContentTypeReader<Media::Video>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".mp4", ".ogv", ".webm", ".mkv", ".avi", ".mov"};
            }

            Media::Video Read(const std::string& path, ContentManager& cm) override
            {
                return Media::Video(path, &cm.getGraphicsDeviceInternal());
            }
        };


        // ---------------------------------------------------------------------------
        // Compiled .cnb Song and Video loaders (plans/plan_cnb.md CNBF-103B)
        //
        // Both are metadata plus a streaming reference, so what these do is resolve the reference
        // to a real path and hand it to the runtime type's trusted-metadata constructor. The
        // media file is never read here: Video's own constructor deliberately does not touch the
        // file at construction time either.
        // ---------------------------------------------------------------------------

        std::string ResolveMediaStreamPathEXT(ContentManager& cm, const std::string& reference,
                                              const std::string& assetName)
        {
            const std::string resolved = ContentManagerAccess::ResolveExistingAssetPath(
                cm, ContentManagerAccess::BuildAssetPath(cm, reference));
            if (!std::filesystem::exists(CNA::Internal::PathFromUtf8(resolved)))
            {
                throw ContentLoadException("'" + assetName + "' streams '" + reference +
                                           "', which was not found beside it.");
            }
            return resolved;
        }

        Media::Song BuildSongFromCnbEXT(const CNA::Content::Cnb::CnbSongData& data,
                                         ContentManager& cm, const std::string& assetName)
        {
            return Media::Song(ResolveMediaStreamPathEXT(cm, data.streamReference, assetName),
                               data.name.empty() ? assetName : data.name,
                               static_cast<SharpRuntime::intcs>(data.durationMs));
        }

        Media::Video BuildVideoFromCnbEXT(const CNA::Content::Cnb::CnbVideoData& data,
                                           ContentManager& cm, const std::string& assetName)
        {
            return Media::Video(ResolveMediaStreamPathEXT(cm, data.streamReference, assetName),
                                &cm.getGraphicsDeviceInternal(),
                                static_cast<SharpRuntime::intcs>(data.durationMs),
                                static_cast<SharpRuntime::intcs>(data.width),
                                static_cast<SharpRuntime::intcs>(data.height),
                                data.framesPerSecond,
                                static_cast<Media::VideoSoundtrackType>(data.soundtrackType));
        }
    } // anonymous namespace

    void detail::BuiltinContent<Media::Song>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::Song, "Microsoft.Xna.Framework.Media.Song",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildSongFromCnbEXT(
                    CNA::Content::Cnb::DecodeSongFromCnb(document), contentManager, assetName));
            });
    }

    void detail::BuiltinContent<Media::Song>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Media::Song, SongTypeReader>();
        RegisterCnbLoader();
        // A real content pipeline dispatches the song's duration through Int32Reader, so an .xnb
        // song's type-reader table names a value reader beside SongReader.
        detail::RegisterValueXnbReaders();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterSongXnbReader);
    }

    void detail::BuiltinContent<Media::Video>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::Video, "Microsoft.Xna.Framework.Media.Video",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildVideoFromCnbEXT(
                    CNA::Content::Cnb::DecodeVideoFromCnb(document), contentManager, assetName));
            });
    }

    void detail::BuiltinContent<Media::Video>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Media::Video, VideoTypeReader>();
        RegisterCnbLoader();
        // The reader and metadata-only Video constructor are backend-independent; playback is
        // what reports NotSupportedException in a no-video build. A real content pipeline
        // dispatches the video's file name, size, frame rate and duration through StringReader,
        // Int32Reader and SingleReader, so the table names the value readers beside VideoReader.
        detail::RegisterValueXnbReaders();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterVideoXnbReader);
    }
}
