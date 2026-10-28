// SPDX-License-Identifier: MS-PL
// SpriteFont: the .cnj font descriptor, .cnb and .xnb.
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

        class SpriteFontTypeReader : public LooseFileContentTypeReader<Graphics::SpriteFont>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            Graphics::SpriteFont Read(const std::string& path, ContentManager& cm) override
            {
                using Graphics::SpriteFont;
                using SharpRuntime::charcs;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "SpriteFont", path);
                RejectSourceFileForSelfContainedCnj(envelope, "SpriteFont", path);

                // CNBF-118: read through the shared canonical reader rather than
                // by scanning the document text. The scan found "glyphs" anywhere including inside
                // a string, counted braces without knowing about string literals, read
                // `JsonIntArray4` without checking the array's length or its elements' types, and
                // threw std::invalid_argument out of std::stoi on a malformed number instead of a
                // ContentLoadException -- while the .cnj -> .cnb compiler parsed the same document
                // as real JSON. Two readings of one document is what CNBF-118 exists to remove.
                const CNA::Internal::CnjSpriteFontDescription description =
                    CNA::Internal::ReadCnjSpriteFontDescription(CNA::Internal::ParseJson(json),
                                                                 "SpriteFont .cnj '" + path + "'");

                // Atlas texture — loaded and cached via ContentManager so it stays alive.
                Graphics::Texture2D atlas = cm.Load<Graphics::Texture2D>(
                    ResolveRootRelativeAssetName(cm, path, "texture", description.textureName));

                std::vector<Rectangle>          glyphBounds;
                std::vector<Rectangle>          cropping;
                std::vector<charcs>             characters;
                std::vector<Vector3>            kerningData;
                glyphBounds.reserve(description.glyphs.size());
                cropping.reserve(description.glyphs.size());
                characters.reserve(description.glyphs.size());
                kerningData.reserve(description.glyphs.size());
                for (const CNA::Internal::CnjSpriteFontGlyph& glyph : description.glyphs)
                {
                    characters.push_back(glyph.character);
                    glyphBounds.push_back(glyph.source);
                    cropping.push_back(glyph.crop);
                    kerningData.push_back(glyph.kerning);
                }

                return SpriteFont(
                    std::move(atlas),
                    std::move(glyphBounds),
                    std::move(cropping),
                    std::move(characters),
                    description.lineSpacing,
                    description.spacing,
                    std::move(kerningData),
                    description.defaultCharacter);
            }
        };


        // ---------------------------------------------------------------------------
        // Compiled .cnb SpriteFont loader (CNBF-102)
        //
        // The atlas is EMBEDDED rather than XREF'd, so this needs no second asset load: the font
        // and its atlas arrive in one file, and the atlas is built with the same code path a
        // standalone Texture2D .cnb uses.
        // ---------------------------------------------------------------------------

        Graphics::SpriteFont BuildSpriteFontFromCnbEXT(
            const CNA::Content::Cnb::CnbSpriteFontData& data, ContentManager& cm,
            const std::string& assetName)
        {
            const auto& representation = RequireUploadableRepresentationEXT(data.atlas, assetName);
            Graphics::Texture2D atlas(cm.getGraphicsDeviceInternal(),
                                      static_cast<int>(data.atlas.width),
                                      static_cast<int>(data.atlas.height),
                                      data.atlas.mipCount > 1u, Graphics::SurfaceFormat::Color);
            for (std::uint32_t mip = 0u; mip < data.atlas.mipCount; ++mip)
            {
                const std::vector<Color> colors = ColorsFromRgba8EXT(representation.levels[mip]);
                atlas.SetData(static_cast<int>(mip), nullptr, colors.data(), 0,
                              static_cast<int>(colors.size()));
            }
            return Graphics::SpriteFont(std::move(atlas), data.glyphBounds, data.cropping,
                                        data.characters, data.lineSpacing, data.spacing,
                                        data.kerning, data.defaultCharacter);
        }
    } // anonymous namespace

    void detail::BuiltinContent<Graphics::SpriteFont>::RegisterCnbLoader()
    {
        ContentManager::RegisterBuiltinCnbLoader(
            CNA::Content::Cnb::CnbAssetTypeId::SpriteFont,
            "Microsoft.Xna.Framework.Graphics.SpriteFont",
            [](const CNA::Content::Cnb::CnbDocument& document, ContentManager& contentManager,
               const std::string& assetName) -> std::any
            {
                auto font = BuildSpriteFontFromCnbEXT(
                    CNA::Content::Cnb::DecodeSpriteFontFromCnb(document), contentManager,
                    assetName);
                contentManager.RecordLoadedAsset(font.getTextureEXT());
                return std::any(std::move(font));
            });
    }

    void detail::BuiltinContent<Graphics::SpriteFont>::Register(ContentManager& manager)
    {
        manager.RegisterBuiltinTypeReader<Graphics::SpriteFont, SpriteFontTypeReader>();
        RegisterCnbLoader();
        // An .xnb font holds its glyph texture and lists of rectangles, vectors and characters.
        detail::RegisterValueXnbReaders();
        detail::RegisterDeclaredXnbReaders([] {
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
            CNA::Internal::Xnb::RegisterSpriteFontXnbReader();
        });
    }
}
