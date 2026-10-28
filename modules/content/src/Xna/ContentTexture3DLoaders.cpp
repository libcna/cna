// SPDX-License-Identifier: MS-PL
// Texture3D: .cnj volume descriptors, .cnb and .xnb.
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

        class Texture3DTypeReader : public LooseFileContentTypeReader<std::shared_ptr<Graphics::Texture3D>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            // Targets std::shared_ptr<Texture3D>, not a bare Texture3D: Texture3D is move-only
            // (copy deleted), and ContentManager's generic asset cache stores results in a
            // std::any, which requires CopyConstructible -- the same reason the .xnb-side
            // Texture3DReader/StockEffectContentTypeReaders already return shared_ptr instead of
            // a bare value.
            std::shared_ptr<Graphics::Texture3D> Read(const std::string& path, ContentManager& cm) override
            {
                using CNA::Internal::JsonValue;

                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Texture3D", path);
                RejectSourceFileForSelfContainedCnj(envelope, "Texture3D", path);

                // CNBF-118: one canonical reading of these four members, shared
                // with the .cnj -> .cnb compiler. Previously each route cast numberValue straight
                // to int -- so 3.7 became 3, 1e400 was undefined behaviour, and the two routes
                // could accept different documents.
                const CNA::Internal::CnjTexture3DDescription description =
                    CNA::Internal::ReadCnjTexture3DDescription(CNA::Internal::ParseJson(json),
                                                                "Texture3D .cnj '" + path + "'");
                const auto width  = static_cast<int>(description.width);
                const auto height = static_cast<int>(description.height);
                const auto depth  = static_cast<int>(description.depth);

                const std::string dataPath = ResolveRootRelativeSidecarPath(
                    cm, path, "data", description.dataFile);
                const auto bytes = ReadBinaryFile(dataPath);
                const auto expectedBytes = static_cast<std::size_t>(description.expectedByteCount);
                if (bytes.size() != expectedBytes)
                {
                    throw ContentLoadException(
                        "Texture3D .cnj '" + path + "': 'data' sidecar has " + std::to_string(bytes.size()) +
                        " bytes, expected " + std::to_string(expectedBytes) + " for " +
                        std::to_string(width) + "x" + std::to_string(height) + "x" + std::to_string(depth) + ".");
                }

                std::vector<Color> colors;
                colors.reserve(expectedBytes / 4);
                for (std::size_t i = 0; i < expectedBytes; i += 4)
                {
                    colors.emplace_back(bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]);
                }

                auto texture = std::make_shared<Graphics::Texture3D>(
                    cm.getGraphicsDeviceInternal(), width, height, depth, false, Graphics::SurfaceFormat::Color);
                texture->SetData(colors.data(), static_cast<int>(colors.size()));
                return texture;
            }
        };


        std::shared_ptr<Graphics::Texture3D> BuildTexture3DFromCnbEXT(
            const CNA::Content::Cnb::CnbTextureData& data, ContentManager& cm,
            const std::string& assetName)
        {
            const auto& representation = RequireUploadableRepresentationEXT(data, assetName);
            auto texture = std::make_shared<Graphics::Texture3D>(
                cm.getGraphicsDeviceInternal(), static_cast<int>(data.width),
                static_cast<int>(data.height), static_cast<int>(data.depth), data.mipCount > 1u,
                Graphics::SurfaceFormat::Color);
            for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
            {
                std::uint32_t w = 0u;
                std::uint32_t h = 0u;
                std::uint32_t d = 0u;
                CNA::Content::Cnb::CnbTextureLevelDimensions(data, mip, w, h, d);
                const std::vector<Color> colors = ColorsFromRgba8EXT(representation.levels[mip]);
                texture->SetData(static_cast<int>(mip), 0, 0, static_cast<int>(w),
                                 static_cast<int>(h), 0, static_cast<int>(d), colors.data(), 0,
                                 static_cast<int>(colors.size()));
            }
            return texture;
        }
    } // anonymous namespace

    void detail::BuiltinContent<std::shared_ptr<Graphics::Texture3D>>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::Texture3D,
            "Microsoft.Xna.Framework.Graphics.Texture3D",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildTexture3DFromCnbEXT(
                    CNA::Content::Cnb::DecodeTexture3DFromCnb(document), contentManager,
                    assetName));
            });
    }

    void detail::BuiltinContent<std::shared_ptr<Graphics::Texture3D>>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<std::shared_ptr<Graphics::Texture3D>, Texture3DTypeReader>();
        // The boxed type must be exactly what Load<T> asks for -- a shared_ptr, because Texture3D
        // is non-copyable and the .xnb reader registers it the same way.
        RegisterCnbLoader();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterTexture3DXnbReader);
    }
}
