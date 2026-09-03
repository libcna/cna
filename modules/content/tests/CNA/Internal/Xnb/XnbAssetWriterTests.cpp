// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-23..2B, XNAP-40, XNAP-45: the built-in asset type writers.
//
// Every round-trip here goes out through the writer, to a real file, and back in through
// DecodeXnbCanonicalAsset() -- CNA's own independent reader path, which additionally proves the
// whole body was consumed with nothing left over. The writer and that reader share no code.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using namespace CNA::Internal::Xnb;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnb_writer_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    /** @brief Writes one asset to a scratch file and decodes it back through CNA's own reader. */
    template<typename T>
    XnbCanonicalAsset RoundTrip(const ScratchDirectory& scratch, const std::string& name,
                                const T& value, const XnbFileOptions& options = {})
    {
        const std::filesystem::path path = scratch.Path() / (name + ".xnb");
        WriteXnbAssetFile(path, value, options, name);
        return DecodeXnbCanonicalAsset(path);
    }

    /** @brief Builds a solid-colour Rgba8 level of the requested size. */
    std::vector<std::uint8_t> ColorLevel(const std::uint32_t width, const std::uint32_t height,
                                         const std::uint8_t seed)
    {
        std::vector<std::uint8_t> level(
            static_cast<std::size_t>(width) * height * 4u, 0u);
        for (std::size_t index = 0u; index < level.size(); ++index)
        {
            level[index] = static_cast<std::uint8_t>((index + seed) & 0xFFu);
        }
        return level;
    }

    XnbTextureData MakeTexture2D()
    {
        XnbTextureData texture;
        texture.kind = XnbTextureKind::Texture2D;
        texture.surfaceFormat = SurfaceFormat::Color;
        texture.width = 4u;
        texture.height = 2u;
        texture.depth = 1u;
        texture.faceCount = 1u;
        texture.mipCount = 3u;
        texture.levels = {ColorLevel(4u, 2u, 1u), ColorLevel(2u, 1u, 2u), ColorLevel(1u, 1u, 3u)};
        return texture;
    }

    XnbVertexDeclarationData MakePositionDeclaration()
    {
        XnbVertexDeclarationData declaration;
        declaration.stride = 12;
        declaration.elements.emplace_back(0, VertexElementFormat::Vector3,
                                          VertexElementUsage::Position, 0);
        return declaration;
    }

    XnbModelData MakeModel()
    {
        XnbModelData model;
        XnbModelBoneData root;
        root.name = "RootNode";
        root.transform = Matrix::getIdentityProperty();
        root.parent = -1;
        root.children = {1};
        model.bones.push_back(root);

        XnbModelBoneData child;
        child.name = "Cube";
        child.transform = Matrix::CreateTranslation(Vector3{1.0f, 2.0f, 3.0f});
        child.parent = 0;
        model.bones.push_back(child);
        model.rootBone = 0;

        XnbModelPartData part;
        part.vertexOffset = 0;
        part.vertexCount = 3;
        part.startIndex = 0;
        part.primitiveCount = 1;
        part.vertexBufferResource = 0;
        part.indexBufferResource = 1;
        part.effectResource = 2;

        XnbModelMeshData mesh;
        mesh.name = "Cube";
        mesh.parentBone = 1;
        mesh.boundingSphere = BoundingSphere(Vector3{0.0f, 0.0f, 0.0f}, 1.75f);
        mesh.parts.push_back(part);
        model.meshes.push_back(mesh);

        XnbVertexBufferData vertexBuffer;
        vertexBuffer.declaration = MakePositionDeclaration();
        vertexBuffer.vertexCount = 3u;
        vertexBuffer.bytes.assign(36u, 0u);
        for (std::size_t index = 0u; index < vertexBuffer.bytes.size(); ++index)
        {
            vertexBuffer.bytes[index] = static_cast<std::uint8_t>(index);
        }

        XnbIndexBufferData indexBuffer;
        indexBuffer.indexElementSize = 2u;
        indexBuffer.bytes = {0u, 0u, 1u, 0u, 2u, 0u};

        XnbBasicEffectData effect;
        effect.textureReference = "Textures/wood";
        effect.diffuseColor = Vector3{0.25f, 0.5f, 0.75f};
        effect.emissiveColor = Vector3{0.1f, 0.2f, 0.3f};
        effect.specularColor = Vector3{1.0f, 0.9f, 0.8f};
        effect.specularPower = 32.0f;
        effect.alpha = 0.5f;
        effect.vertexColorEnabled = true;

        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.VertexBufferReader", vertexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.IndexBufferReader", indexBuffer});
        model.sharedResources.push_back(
            {"Microsoft.Xna.Framework.Content.BasicEffectReader", effect});
        return model;
    }
}

// -- textures (XNAP-23) ------------------------------------------------------------------------

TEST(XnbAssetWriterTest, ATexture2DRoundTripsWithEveryMipLevel)
{
    ScratchDirectory scratch("texture2d");
    const XnbTextureData source = MakeTexture2D();
    const XnbCanonicalAsset asset =
        RoundTrip(scratch, "texture", XnbTexture2DContent{source});

    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.Texture2DReader");
    const auto& read = std::get<XnbTextureData>(asset.value);
    EXPECT_EQ(read.kind, XnbTextureKind::Texture2D);
    EXPECT_EQ(read.surfaceFormat, source.surfaceFormat);
    EXPECT_EQ(read.width, source.width);
    EXPECT_EQ(read.height, source.height);
    EXPECT_EQ(read.mipCount, source.mipCount);
    EXPECT_EQ(read.levels, source.levels);
}

TEST(XnbAssetWriterTest, ADxtTexture2DRoundTripsWithItsExactBlockBytes)
{
    ScratchDirectory scratch("dxt");
    XnbTextureData source;
    source.kind = XnbTextureKind::Texture2D;
    source.surfaceFormat = SurfaceFormat::Dxt5;
    source.width = 4u;
    source.height = 4u;
    source.mipCount = 3u;
    source.levels = {std::vector<std::uint8_t>(16u, 0xABu),
                     std::vector<std::uint8_t>(16u, 0xCDu),
                     std::vector<std::uint8_t>(16u, 0xEFu)};

    const XnbCanonicalAsset asset = RoundTrip(scratch, "dxt", XnbTexture2DContent{source});
    const auto& read = std::get<XnbTextureData>(asset.value);
    EXPECT_EQ(read.surfaceFormat, SurfaceFormat::Dxt5);
    EXPECT_EQ(read.levels, source.levels);
}

TEST(XnbAssetWriterTest, ATexture3DRoundTrips)
{
    ScratchDirectory scratch("texture3d");
    XnbTextureData source;
    source.kind = XnbTextureKind::Texture3D;
    source.surfaceFormat = SurfaceFormat::Color;
    source.width = 2u;
    source.height = 2u;
    source.depth = 2u;
    source.faceCount = 1u;
    source.mipCount = 1u;
    source.levels = {std::vector<std::uint8_t>(2u * 2u * 2u * 4u, 0x11u)};

    const XnbCanonicalAsset asset = RoundTrip(scratch, "volume", XnbTexture3DContent{source});
    const auto& read = std::get<XnbTextureData>(asset.value);
    EXPECT_EQ(read.kind, XnbTextureKind::Texture3D);
    EXPECT_EQ(read.depth, 2u);
    EXPECT_EQ(read.levels, source.levels);
}

TEST(XnbAssetWriterTest, ATextureCubeRoundTripsWithSixDistinctFaces)
{
    ScratchDirectory scratch("cube");
    XnbTextureData source;
    source.kind = XnbTextureKind::TextureCube;
    source.surfaceFormat = SurfaceFormat::Color;
    source.width = 2u;
    source.height = 2u;
    source.depth = 1u;
    source.faceCount = 6u;
    source.mipCount = 1u;
    for (std::uint8_t face = 0u; face < 6u; ++face)
    {
        source.levels.push_back(ColorLevel(2u, 2u, static_cast<std::uint8_t>(face * 7u + 1u)));
    }

    const XnbCanonicalAsset asset = RoundTrip(scratch, "cube", XnbTextureCubeContent{source});
    const auto& read = std::get<XnbTextureData>(asset.value);
    EXPECT_EQ(read.kind, XnbTextureKind::TextureCube);
    EXPECT_EQ(read.faceCount, 6u);
    EXPECT_EQ(read.levels, source.levels);
}

TEST(XnbAssetWriterTest, ACnaOnlySurfaceFormatIsRefusedForAVersion5Container)
{
    ScratchDirectory scratch("bad_format");
    XnbTextureData source = MakeTexture2D();
    source.surfaceFormat = SurfaceFormat::ColorBgraEXT;
    EXPECT_THROW((void)WriteXnbAsset(XnbTexture2DContent{source}, {}, "bad"), XnbWriteException);
}

TEST(XnbAssetWriterTest, AMismatchedLevelCountIsRefusedInsteadOfWritingATruncatedTexture)
{
    XnbTextureData source = MakeTexture2D();
    source.levels.pop_back();
    EXPECT_THROW((void)WriteXnbAsset(XnbTexture2DContent{source}, {}, "short"),
                 XnbWriteException);
}

// -- SpriteFont (XNAP-24) ----------------------------------------------------------------------

TEST(XnbAssetWriterTest, ASpriteFontRoundTripsWithEveryGlyphList)
{
    ScratchDirectory scratch("spritefont");
    XnbSpriteFontData source;
    source.atlas = MakeTexture2D();
    source.glyphs = {Rectangle(0, 0, 2, 2), Rectangle(2, 0, 2, 2)};
    source.cropping = {Rectangle(0, 1, 2, 1), Rectangle(1, 0, 1, 2)};
    source.characters = {u'A', u'B'};
    source.lineSpacing = 14;
    source.spacing = 1.5f;
    source.kerning = {Vector3{0.0f, 2.0f, 0.5f}, Vector3{-1.0f, 2.0f, 1.0f}};
    source.defaultCharacter = u'?';

    const XnbCanonicalAsset asset = RoundTrip(scratch, "font", source);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.SpriteFontReader");
    const auto& read = std::get<XnbSpriteFontData>(asset.value);
    EXPECT_EQ(read.glyphs, source.glyphs);
    EXPECT_EQ(read.cropping, source.cropping);
    EXPECT_EQ(read.characters, source.characters);
    EXPECT_EQ(read.lineSpacing, source.lineSpacing);
    EXPECT_FLOAT_EQ(read.spacing, source.spacing);
    ASSERT_EQ(read.kerning.size(), source.kerning.size());
    EXPECT_FLOAT_EQ(read.kerning[1].X, -1.0f);
    EXPECT_FLOAT_EQ(read.kerning[1].Y, 2.0f);
    EXPECT_FLOAT_EQ(read.kerning[1].Z, 1.0f);
    ASSERT_TRUE(read.defaultCharacter.has_value());
    EXPECT_EQ(*read.defaultCharacter, u'?');
    EXPECT_EQ(read.atlas.levels, source.atlas.levels);
}

TEST(XnbAssetWriterTest, ASpriteFontWithoutADefaultCharacterRoundTrips)
{
    ScratchDirectory scratch("spritefont_nodefault");
    XnbSpriteFontData source;
    source.atlas = MakeTexture2D();
    source.glyphs = {Rectangle(0, 0, 1, 1)};
    source.cropping = {Rectangle(0, 0, 1, 1)};
    source.characters = {u'x'};
    source.lineSpacing = 8;
    source.spacing = 0.0f;
    source.kerning = {Vector3{0.0f, 1.0f, 0.0f}};

    const XnbCanonicalAsset asset = RoundTrip(scratch, "font", source);
    const auto& read = std::get<XnbSpriteFontData>(asset.value);
    EXPECT_FALSE(read.defaultCharacter.has_value());
}

TEST(XnbAssetWriterTest, MismatchedSpriteFontListLengthsAreRefused)
{
    XnbSpriteFontData source;
    source.atlas = MakeTexture2D();
    source.glyphs = {Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1)};
    source.cropping = {Rectangle(0, 0, 1, 1)};
    source.characters = {u'x'};
    source.kerning = {Vector3{}};
    EXPECT_THROW((void)WriteXnbAsset(source, {}, "font"), XnbWriteException);
}

// -- audio and media (XNAP-25, XNAP-26) --------------------------------------------------------

TEST(XnbAssetWriterTest, ASoundEffectRoundTripsIncludingItsFormatExtensionAndLoop)
{
    ScratchDirectory scratch("sound");
    XnbSoundEffectData source;
    source.formatTag = 1u;
    source.channels = 2u;
    source.sampleRate = 44100u;
    source.averageBytesPerSecond = 176400u;
    source.blockAlign = 4u;
    source.bitsPerSample = 16u;
    source.samples.assign(64u, 0x7Fu);
    source.loopStart = 4;
    source.loopLength = 8;
    source.storedDurationMs = 1;

    const XnbCanonicalAsset asset = RoundTrip(scratch, "tone", source);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.SoundEffectReader");
    const auto& read = std::get<XnbSoundEffectData>(asset.value);
    EXPECT_EQ(read.formatTag, source.formatTag);
    EXPECT_EQ(read.channels, source.channels);
    EXPECT_EQ(read.sampleRate, source.sampleRate);
    EXPECT_EQ(read.averageBytesPerSecond, source.averageBytesPerSecond);
    EXPECT_EQ(read.blockAlign, source.blockAlign);
    EXPECT_EQ(read.bitsPerSample, source.bitsPerSample);
    EXPECT_EQ(read.samples, source.samples);
    EXPECT_EQ(read.loopStart, source.loopStart);
    EXPECT_EQ(read.loopLength, source.loopLength);
    EXPECT_EQ(read.storedDurationMs, source.storedDurationMs);
    EXPECT_TRUE(read.extensionData.empty());
}

TEST(XnbAssetWriterTest, ASoundEffectWithExtensionBytesRoundTrips)
{
    ScratchDirectory scratch("sound_ext");
    XnbSoundEffectData source;
    source.formatTag = 2u;
    source.channels = 1u;
    source.sampleRate = 22050u;
    source.averageBytesPerSecond = 11314u;
    source.blockAlign = 1024u;
    source.bitsPerSample = 4u;
    source.extensionData.assign(32u, 0x5Au);
    source.samples.assign(1024u, 0x3Cu);
    source.storedDurationMs = 500u;

    const XnbCanonicalAsset asset = RoundTrip(scratch, "adpcm", source);
    const auto& read = std::get<XnbSoundEffectData>(asset.value);
    EXPECT_EQ(read.extensionData, source.extensionData);
    EXPECT_EQ(read.samples, source.samples);
}

TEST(XnbAssetWriterTest, ASoundEffectTargetingXbox360ByteSwapsItsWaveFormat)
{
    ScratchDirectory scratch("sound_xbox");
    XnbSoundEffectData source;
    source.formatTag = 1u;
    source.channels = 1u;
    source.sampleRate = 8000u;
    source.averageBytesPerSecond = 16000u;
    source.blockAlign = 2u;
    source.bitsPerSample = 16u;
    source.samples.assign(16u, 0x01u);

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Xbox360;
    const XnbCanonicalAsset asset = RoundTrip(scratch, "xbox", source, options);
    const auto& read = std::get<XnbSoundEffectData>(asset.value);
    EXPECT_EQ(read.platform, 'x');
    EXPECT_EQ(read.sampleRate, 8000u);
    EXPECT_EQ(read.channels, 1u);
    EXPECT_EQ(read.bitsPerSample, 16u);
}

TEST(XnbAssetWriterTest, ASongRoundTripsWithItsDurationDispatchedThroughInt32Reader)
{
    ScratchDirectory scratch("song");
    const XnbSongData source{"music/theme.ogg", 3005};
    const XnbCanonicalAsset asset = RoundTrip(scratch, "theme", source);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.SongReader");
    const auto& read = std::get<XnbSongData>(asset.value);
    EXPECT_EQ(read.mediaPath, source.mediaPath);
    EXPECT_EQ(read.durationMs, source.durationMs);
}

TEST(XnbAssetWriterTest, AVideoRoundTripsWithEveryMetadataField)
{
    ScratchDirectory scratch("video");
    XnbSongData unused;
    (void)unused;
    XnbVideoData source;
    source.mediaPath = "clips/intro.wmv";
    source.durationMs = 12345;
    source.width = 640;
    source.height = 360;
    source.framesPerSecond = 29.97f;
    source.soundtrackType = 1;

    const XnbCanonicalAsset asset = RoundTrip(scratch, "intro", source);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.VideoReader");
    const auto& read = std::get<XnbVideoData>(asset.value);
    EXPECT_EQ(read.mediaPath, source.mediaPath);
    EXPECT_EQ(read.durationMs, source.durationMs);
    EXPECT_EQ(read.width, source.width);
    EXPECT_EQ(read.height, source.height);
    EXPECT_FLOAT_EQ(read.framesPerSecond, source.framesPerSecond);
    EXPECT_EQ(read.soundtrackType, source.soundtrackType);
}

// -- Model and its shared resources (XNAP-27, XNAP-28, XNAP-2A) --------------------------------

TEST(XnbAssetWriterTest, AModelRoundTripsItsGraphAndEverySharedResource)
{
    ScratchDirectory scratch("model");
    const XnbModelData source = MakeModel();
    const XnbCanonicalAsset asset = RoundTrip(scratch, "cube", source);

    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.ModelReader");
    const auto& read = std::get<XnbModelData>(asset.value);

    ASSERT_EQ(read.bones.size(), 2u);
    EXPECT_EQ(read.bones[0].name, "RootNode");
    EXPECT_EQ(read.bones[0].parent, -1);
    EXPECT_EQ(read.bones[0].children, std::vector<std::int32_t>{1});
    EXPECT_EQ(read.bones[1].name, "Cube");
    EXPECT_EQ(read.bones[1].parent, 0);
    EXPECT_TRUE(read.bones[1].children.empty());
    EXPECT_FLOAT_EQ(read.bones[1].transform.M41, 1.0f);
    EXPECT_FLOAT_EQ(read.bones[1].transform.M42, 2.0f);
    EXPECT_FLOAT_EQ(read.bones[1].transform.M43, 3.0f);
    EXPECT_EQ(read.rootBone, 0);

    ASSERT_EQ(read.meshes.size(), 1u);
    EXPECT_EQ(read.meshes[0].name, "Cube");
    EXPECT_EQ(read.meshes[0].parentBone, 1);
    EXPECT_FLOAT_EQ(read.meshes[0].boundingSphere.Radius, 1.75f);
    ASSERT_EQ(read.meshes[0].parts.size(), 1u);
    const XnbModelPartData& part = read.meshes[0].parts[0];
    EXPECT_EQ(part.vertexCount, 3);
    EXPECT_EQ(part.primitiveCount, 1);
    EXPECT_EQ(part.vertexBufferResource, 0);
    EXPECT_EQ(part.indexBufferResource, 1);
    EXPECT_EQ(part.effectResource, 2);

    ASSERT_EQ(read.sharedResources.size(), 3u);
    const auto& vertexBuffer = std::get<XnbVertexBufferData>(read.sharedResources[0].value);
    EXPECT_EQ(vertexBuffer.vertexCount, 3u);
    EXPECT_EQ(vertexBuffer.declaration.stride, 12);
    ASSERT_EQ(vertexBuffer.declaration.elements.size(), 1u);
    EXPECT_EQ(vertexBuffer.declaration.elements[0].getVertexElementUsageProperty(),
              VertexElementUsage::Position);
    EXPECT_EQ(vertexBuffer.bytes,
              std::get<XnbVertexBufferData>(source.sharedResources[0].value).bytes);

    const auto& indexBuffer = std::get<XnbIndexBufferData>(read.sharedResources[1].value);
    EXPECT_EQ(indexBuffer.indexElementSize, 2u);
    EXPECT_EQ(indexBuffer.bytes,
              std::get<XnbIndexBufferData>(source.sharedResources[1].value).bytes);

    const auto& effect = std::get<XnbBasicEffectData>(read.sharedResources[2].value);
    EXPECT_EQ(effect.textureReference, "Textures/wood");
    EXPECT_FLOAT_EQ(effect.diffuseColor.Y, 0.5f);
    EXPECT_FLOAT_EQ(effect.specularPower, 32.0f);
    EXPECT_FLOAT_EQ(effect.alpha, 0.5f);
    EXPECT_TRUE(effect.vertexColorEnabled);
}

TEST(XnbAssetWriterTest, TheModelTypeReaderTableListsVertexDeclarationReaderItNeverDispatchesTo)
{
    // The committed BlenderDefaultCube fixture has exactly this shape: VertexDeclarationReader
    // sits in the table because VertexBufferWriter interns it while writing the declaration
    // inline, not because anything ever emits its dispatch index.
    ScratchDirectory scratch("model_table");
    const std::filesystem::path path = scratch.Path() / "cube.xnb";
    WriteXnbAssetFile(path, MakeModel(), {}, "cube");

    std::ifstream stream(path, std::ios::binary);
    const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(stream),
                                         std::istreambuf_iterator<char>()};
    const std::string text(file.begin(), file.end());
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.VertexDeclarationReader"),
              std::string::npos);
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.ModelReader"), std::string::npos);
    EXPECT_NE(text.find("Microsoft.Xna.Framework.Content.BasicEffectReader"), std::string::npos);
}

TEST(XnbAssetWriterTest, AModelPartMissingASharedResourceIsRefusedWithAnActionableMessage)
{
    XnbModelData model = MakeModel();
    model.meshes[0].parts[0].effectResource = -1;
    try
    {
        (void)WriteXnbAsset(model, {}, "cube");
        FAIL() << "a mesh part without an effect must be refused";
    }
    catch (const XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("an effect shared resource"), std::string::npos) << message;
        EXPECT_NE(message.find("cube"), std::string::npos) << message;
    }
}

TEST(XnbAssetWriterTest, AnOutOfRangeBoneReferenceIsRefused)
{
    XnbModelData model = MakeModel();
    model.meshes[0].parentBone = 9;
    EXPECT_THROW((void)WriteXnbAsset(model, {}, "cube"), XnbWriteException);
}

TEST(XnbAssetWriterTest, AVertexBufferWhoseBytesDisagreeWithItsStrideIsRefused)
{
    XnbModelData model = MakeModel();
    std::get<XnbVertexBufferData>(model.sharedResources[0].value).bytes.pop_back();
    EXPECT_THROW((void)WriteXnbAsset(model, {}, "cube"), XnbWriteException);
}

TEST(XnbAssetWriterTest, AnIndexBufferWithAPartialIndexIsRefused)
{
    XnbModelData model = MakeModel();
    std::get<XnbIndexBufferData>(model.sharedResources[1].value).bytes.pop_back();
    EXPECT_THROW((void)WriteXnbAsset(model, {}, "cube"), XnbWriteException);
}

// -- compiled effects (XNAP-29) ----------------------------------------------------------------

TEST(XnbAssetWriterTest, ACompiledEffectWritesItsBytecodeVerbatim)
{
    ScratchDirectory scratch("effect");
    XnbCompiledEffectContent effect;
    effect.bytecode = {0x01u, 0x02u, 0x03u, 0x04u, 0x05u};
    const std::filesystem::path path = scratch.Path() / "shader.xnb";
    WriteXnbAssetFile(path, effect, {}, "shader");

    std::ifstream stream(path, std::ios::binary);
    const std::vector<std::uint8_t> file{std::istreambuf_iterator<char>(stream),
                                         std::istreambuf_iterator<char>()};
    ASSERT_GE(file.size(), 5u);
    EXPECT_EQ(std::vector<std::uint8_t>(file.end() - 5, file.end()), effect.bytecode);
}

TEST(XnbAssetWriterTest, AnEmptyCompiledEffectIsRefusedRatherThanWrittenAsALoadableAsset)
{
    EXPECT_THROW((void)WriteXnbAsset(XnbCompiledEffectContent{}, {}, "shader"),
                 XnbWriteException);
}

// -- legacy container version 4 (XNAP-13) ------------------------------------------------------

TEST(XnbAssetWriterTest, AVersion4ContainerUsesTheLegacySurfaceFormatNumbering)
{
    ScratchDirectory scratch("legacy");
    XnbTextureData source;
    source.kind = XnbTextureKind::Texture2D;
    source.surfaceFormat = SurfaceFormat::Dxt1;
    source.width = 4u;
    source.height = 4u;
    source.mipCount = 3u;
    source.levels = {std::vector<std::uint8_t>(8u, 0x10u),
                     std::vector<std::uint8_t>(8u, 0x20u),
                     std::vector<std::uint8_t>(8u, 0x30u)};

    XnbFileOptions options;
    options.version = XnbContainerVersion::Legacy4;
    const XnbCanonicalAsset asset =
        RoundTrip(scratch, "legacy", XnbTexture2DContent{source}, options);
    EXPECT_EQ(asset.version, 4);
    const auto& read = std::get<XnbTextureData>(asset.value);
    EXPECT_EQ(read.surfaceFormat, SurfaceFormat::Dxt1);
    EXPECT_EQ(read.levels, source.levels);
}

TEST(XnbAssetWriterTest, AFormatVersion4CannotExpressIsRefused)
{
    XnbTextureData source = MakeTexture2D();
    source.surfaceFormat = SurfaceFormat::Color;
    XnbFileOptions options;
    options.version = XnbContainerVersion::Legacy4;
    EXPECT_THROW((void)WriteXnbAsset(XnbTexture2DContent{source}, options, "legacy"),
                 XnbWriteException);
}

// -- untrusted-input hardening (XNAP-85) -------------------------------------------------------

TEST(XnbAssetWriterTest, ATextureLevelWhoseByteCountDisagreesWithItsDimensionsIsRefused)
{
    XnbTextureData source;
    source.kind = XnbTextureKind::Texture2D;
    source.surfaceFormat = SurfaceFormat::Color;
    source.width = 4u;
    source.height = 4u;
    source.mipCount = 1u;
    source.levels = {std::vector<std::uint8_t>(4u * 4u * 4u - 1u, 0u)};

    try
    {
        (void)WriteXnbAsset(XnbTexture2DContent{source}, {}, "short");
        FAIL() << "a level shorter than its own declared dimensions must be refused";
    }
    catch (const XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("carries 63 bytes"), std::string::npos) << message;
        EXPECT_NE(message.find("needs 64"), std::string::npos) << message;
    }

    // The same check has to follow the mip chain down, or only level zero is ever validated.
    XnbTextureData chain;
    chain.kind = XnbTextureKind::Texture2D;
    chain.surfaceFormat = SurfaceFormat::Color;
    chain.width = 4u;
    chain.height = 4u;
    chain.mipCount = 3u;
    chain.levels = {std::vector<std::uint8_t>(4u * 4u * 4u, 0u),
                    std::vector<std::uint8_t>(2u * 2u * 4u, 0u),
                    std::vector<std::uint8_t>(9u, 0u)};
    EXPECT_THROW((void)WriteXnbAsset(XnbTexture2DContent{chain}, {}, "chain"), XnbWriteException);

    // A block-compressed level rounds each dimension up to a whole 4x4 block, so a 2x2 DXT1
    // level is eight bytes rather than one.
    XnbTextureData block;
    block.kind = XnbTextureKind::Texture2D;
    block.surfaceFormat = SurfaceFormat::Dxt1;
    block.width = 2u;
    block.height = 2u;
    block.mipCount = 1u;
    block.levels = {std::vector<std::uint8_t>(8u, 0u)};
    EXPECT_NO_THROW((void)WriteXnbAsset(XnbTexture2DContent{block}, {}, "block"));
    block.levels = {std::vector<std::uint8_t>(4u, 0u)};
    EXPECT_THROW((void)WriteXnbAsset(XnbTexture2DContent{block}, {}, "block"), XnbWriteException);
}
