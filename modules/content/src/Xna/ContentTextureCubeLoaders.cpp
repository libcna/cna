// SPDX-License-Identifier: MS-PL
// TextureCube: DDS cube maps, .cnj descriptors, .cnb and .xnb.
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

        class TextureCubeTypeReader : public LooseFileContentTypeReader<Graphics::TextureCube>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".dds"};
            }

            Graphics::TextureCube Read(const std::string& path, ContentManager& cm) override
            {
                if (CNA::Internal::PathFromUtf8(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();
                System::IO::FileStream stream(path);
                return Graphics::TextureCube::DDSFromStreamEXT(gd, stream);
            }

        private:
            static Graphics::TextureCube ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "TextureCube", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: TextureCube .cnj '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile TextureCube .cnj is not "
                        "supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                return cm.Load<Graphics::TextureCube>(resolved.logicalName);
            }
        };

        // CNJ CNB-43: no native "3D texture" file format exists in CNA the way .dds serves
        // TextureCube (Texture3D has no FromStream/DDSFromStream equivalent) -- so unlike
        // TextureCubeTypeReader, this is self-contained JSON + a raw binary pixel-data sidecar
        // (mirrors Model's own vertex/index binary-sidecar convention), not a sourceFile delegation.
        // Single mip level only; deliberately skips the .xnb Texture3DReader's mip-chain/DXT
        // handling -- no real sample content needs either (CNJ CNB-43's own survey), and
        // .cnj content is hand-authored, so pre-compressed DXT data has no natural place to come
        // from here the way it does inside a real XNA content-pipeline build.

        Graphics::TextureCube BuildTextureCubeFromCnbEXT(
            const CNA::Content::Cnb::CnbTextureData& data, ContentManager& cm,
            const std::string& assetName)
        {
            const auto& representation = RequireUploadableRepresentationEXT(data, assetName);
            Graphics::TextureCube texture(cm.getGraphicsDeviceInternal(),
                                          static_cast<int>(data.width), data.mipCount > 1u,
                                          Graphics::SurfaceFormat::Color);
            for (std::uint32_t face = 0u; face < data.faceCount; ++face)
            {
                for (std::uint32_t mip = 0u; mip < data.mipCount; ++mip)
                {
                    const std::size_t index =
                        static_cast<std::size_t>(face) * data.mipCount + mip;
                    const std::vector<Color> colors =
                        ColorsFromRgba8EXT(representation.levels[index]);
                    texture.SetData(static_cast<Graphics::CubeMapFace>(face),
                                    static_cast<int>(mip), nullptr, colors.data(), 0,
                                    static_cast<int>(colors.size()));
                }
            }
            return texture;
        }
    } // anonymous namespace

    void detail::BuiltinContent<Graphics::TextureCube>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::TextureCube,
            "Microsoft.Xna.Framework.Graphics.TextureCube",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                return std::any(BuildTextureCubeFromCnbEXT(
                    CNA::Content::Cnb::DecodeTextureCubeFromCnb(document), contentManager,
                    assetName));
            });
    }

    void detail::BuiltinContent<Graphics::TextureCube>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Graphics::TextureCube, TextureCubeTypeReader>();
        RegisterCnbLoader();
        detail::RegisterDeclaredXnbReaders(&CNA::Internal::Xnb::RegisterTextureCubeXnbReader);
    }
}
