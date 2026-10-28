// SPDX-License-Identifier: MS-PL
// Texture2D: loose images, .cnj descriptors, .cnb and .xnb.
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

        class Texture2DTypeReader : public LooseFileContentTypeReader<Graphics::Texture2D>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                // Every extension here must be one ImageLoader can decode. TIFF is not (docs/texture-stream-formats.md:
                // the decoder is deliberately dependency-free), so ".tif"/".tiff" are not offered: a .tif file is
                // reported as a missing asset instead of being found and then failing with "unknown image type".
                return {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".qoi"};
            }

            Graphics::Texture2D Read(const std::string& path, ContentManager& cm) override
            {
                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();

                if (CNA::Internal::PathFromUtf8(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                return Graphics::Texture2D(path, gd);
            }

        private:
            static Graphics::Texture2D ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Texture2D", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: Texture2D .cnj '" + path + "' has no 'sourceFile' field "
                        "(a self-contained, non-sourceFile Texture2D .cnj is not supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                Graphics::Texture2D result = cm.Load<Graphics::Texture2D>(resolved.logicalName);

                // CNBF-118: through the shared canonical reader, not a substring
                // scan with std::stoi. The scan found "colorKey" anywhere in the file including
                // inside another string, read digits without checking they belonged to an array,
                // and threw std::out_of_range rather than a ContentLoadException on a large
                // number -- and the .cnj -> .cnb compiler read the same member a fourth,
                // different way. One reader means the two routes agree by construction.
                if (const std::optional<std::array<std::uint8_t, 3>> colorKey =
                        CNA::Internal::ReadCnjColorKey(CNA::Internal::ParseJson(json),
                                                        "Texture2D .cnj '" + path + "'");
                    colorKey.has_value())
                {
                    ApplyColorKey(result, *colorKey);
                }

                return result;
            }
        };


        // ---------------------------------------------------------------------------
        // Compiled .cnb texture loaders (CNBF-101A/B/C)
        //
        // Same split as the Model loader above: the bytes-to-CnbTextureData half is in
        // modules/content/src/Cnb/CnbTextureCodec.cpp and needs no GraphicsDevice, and only the
        // step that creates a real GPU texture lives here.
        //
        // The representation SELECTION is what makes this more than a decoder. A texture .cnb may
        // carry the same image several times over, so the runtime asks for the first encoding it
        // can actually upload rather than assuming the file holds the one it wants. Schema 1
        // writes Rgba8 alone, so today the predicate has exactly one true case -- but the
        // selection is real, and a file carrying Bc7 first and Rgba8 second already loads
        // correctly on this build by falling through to the second representation.
        // ---------------------------------------------------------------------------


        Graphics::Texture2D BuildTexture2DFromCnbEXT(const CNA::Content::Cnb::CnbTextureData& data,
                                                      ContentManager& cm,
                                                      const std::string& assetName)
        {
            const auto& representation = RequireUploadableRepresentationEXT(data, assetName);
            Graphics::Texture2D texture(cm.getGraphicsDeviceInternal(),
                                        static_cast<int>(data.width),
                                        static_cast<int>(data.height), data.mipCount > 1u,
                                        Graphics::SurfaceFormat::Color);
            for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
            {
                const std::vector<Color> colors = ColorsFromRgba8EXT(representation.levels[mip]);
                texture.SetData(static_cast<int>(mip), nullptr, colors.data(), 0,
                                static_cast<int>(colors.size()));
            }
            return texture;
        }
    } // anonymous namespace

    void detail::BuiltinContent<Graphics::Texture2D>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::Texture2D,
            "Microsoft.Xna.Framework.Graphics.Texture2D",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildTexture2DFromCnbEXT(
                    CNA::Content::Cnb::DecodeTexture2DFromCnb(document), contentManager,
                    assetName));
            });
    }

    void detail::BuiltinContent<Graphics::Texture2D>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Graphics::Texture2D, Texture2DTypeReader>();
        RegisterCnbLoader();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterTexture2DXnbReader);
    }
}
