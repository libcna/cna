// SPDX-License-Identifier: MS-PL
//
// XNAP-23..2B, XNAP-40, XNAP-45: the built-in asset type writers.
//
// Every round-trip here goes out through the writer, to a real file, and back in through
// DecodeXnbCanonicalAsset() -- CNA's own independent reader path, which additionally proves the
// whole body was consumed with nothing left over. The writer and that reader share no code.

#include "CNA/TestSupport/TestScratch.hpp"
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>
#include "CNA/TestSupport/TestPaths.hpp"

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
            : path_(CNA::TestSupport::FixtureRoot() /
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
    // In the character table on purpose: SpriteFont::setDefaultCharacterProperty rejects a
    // fallback the font cannot render, so U+003F here would have produced an
    // .xnb that CNA's own runtime refuses to load. The writer now refuses it instead
    // (XNAP-45).
    source.defaultCharacter = u'A';

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
    EXPECT_EQ(*read.defaultCharacter, u'A');
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

    // XNAP-82: an Xbox 360 target is refused by default, because the
    // WAVEFORMATEX swap below is the only Xbox byte-order handling in this build -- the sample
    // bytes are not swapped, and CNA's own reader already refuses to transcode Xbox samples for
    // that reason. The opt-in exists so somebody with real hardware can produce a candidate.
    try
    {
        (void)WriteXnbAsset(source, options, "xbox");
        FAIL() << "an Xbox 360 target must be refused unless explicitly opted into";
    }
    catch (const XnbWriteException& error)
    {
        EXPECT_NE(std::string(error.what()).find("XNAP-82"), std::string::npos) << error.what();
    }

    options.allowUnverifiedXboxPayloads = true;
    const XnbCanonicalAsset asset = RoundTrip(scratch, "xbox", source, options);
    const auto& read = std::get<XnbSoundEffectData>(asset.value);
    EXPECT_EQ(read.platform, 'x');
    EXPECT_EQ(read.sampleRate, 8000u);
    EXPECT_EQ(read.channels, 1u);
    EXPECT_EQ(read.bitsPerSample, 16u);
}

// A Texture2D is converted for the Xbox 360 rather than refused: its payload is reversed by the
// format's component width, which is measured against the genuine pipeline
// (XNAPP-252). The types nobody has converted still refuse, which
// is what XNAP-82 asks for and why the refusal lives in code.
TEST(XnbAssetWriterTest, ATextureIsByteSwappedForXbox360RatherThanRefused)
{
    XnbFileOptions windows;
    XnbFileOptions xbox;
    xbox.platform = XnbTargetPlatform::Xbox360;

    const std::vector<std::uint8_t> onWindows =
        WriteXnbAsset(XnbTexture2DContent{MakeTexture2D()}, windows, "windows");
    const std::vector<std::uint8_t> onXbox =
        WriteXnbAsset(XnbTexture2DContent{MakeTexture2D()}, xbox, "windows");
    ASSERT_EQ(onWindows.size(), onXbox.size());

    // Same length, same header except the platform byte, and every four-byte texel reversed. The
    // container's own fields stay little-endian on both, which is why this compares the payload
    // rather than the file: a whole-file comparison would also catch the header.
    std::size_t reversedTexels = 0u;
    std::size_t identicalBytes = 0u;
    for (std::size_t at = 0u; at + 4u <= onWindows.size(); ++at)
    {
        if (onWindows[at] == onXbox[at]) { ++identicalBytes; }
    }
    // Every offset, not every fourth: where the payload begins in the file is the container's
    // business and not this test's.
    for (std::size_t at = 0u; at + 4u <= onWindows.size(); ++at)
    {
        if (onWindows[at] == onXbox[at + 3u] && onWindows[at + 1u] == onXbox[at + 2u] &&
            onWindows[at + 2u] == onXbox[at + 1u] && onWindows[at + 3u] == onXbox[at] &&
            onWindows[at] != onWindows[at + 3u])
        {
            ++reversedTexels;
        }
    }
    EXPECT_GT(reversedTexels, 0u) << "no texel was reversed, so nothing was converted";
    EXPECT_GT(identicalBytes, onWindows.size() / 2u)
        << "the whole file changed, so more than the payload was swapped";

    // Windows Phone is little-endian and has no known payload difference from Windows.
    XnbFileOptions phone;
    phone.platform = XnbTargetPlatform::WindowsPhone;
    EXPECT_EQ(WriteXnbAsset(XnbTexture2DContent{MakeTexture2D()}, phone, "windows").size(),
              onWindows.size());
}

TEST(XnbAssetWriterTest, AnUnconvertedAssetTypeStillRefusesAnXbox360TargetOutright)
{
    // XNAP-82. Writing 'x' into the header over Windows-layout payloads
    // would claim a compatibility this build cannot deliver, so the refusal is in code rather
    // than only in documentation, and it names the task that would close the gap.
    XnbFileOptions options;
    options.platform = XnbTargetPlatform::Xbox360;

    // A vertex *declaration* is one of the types nobody has converted: unlike the buffer beside
    // it, no fixture proves what the console expects of its element table.
    const XnbVertexDeclarationData declaration = MakePositionDeclaration();
    try
    {
        (void)WriteXnbAsset(XnbVertexDeclarationData{declaration}, options, "xbox");
        FAIL() << "a vertex declaration targeting Xbox 360 must be refused";
    }
    catch (const XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("big-endian"), std::string::npos) << message;
        EXPECT_NE(message.find("XNAP-82"), std::string::npos) << message;
    }

    options.allowUnverifiedXboxPayloads = true;
    EXPECT_NO_THROW((void)WriteXnbAsset(XnbVertexDeclarationData{declaration}, options, "opted"));
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

TEST(XnbAssetWriterTest, TextureContentEqualityIncludesShapeAndPayload)
{
    const XnbTextureData source = MakeTexture2D();
    XnbTextureData changed = source;
    changed.levels[0][0] ^= 0xFFu;

    EXPECT_EQ(XnbTexture2DContent{source}, XnbTexture2DContent{source});
    EXPECT_NE(XnbTexture2DContent{source}, XnbTexture2DContent{changed});
    EXPECT_EQ(XnbTexture3DContent{source}, XnbTexture3DContent{source});
    EXPECT_NE(XnbTexture3DContent{source}, XnbTexture3DContent{changed});
    EXPECT_EQ(XnbTextureCubeContent{source}, XnbTextureCubeContent{source});
    EXPECT_NE(XnbTextureCubeContent{source}, XnbTextureCubeContent{changed});
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


namespace
{
    std::filesystem::path Tq063AssetFixture(const std::string& name)
    {
        const auto path=CNA::TestSupport::RepositoryRoot()/"tests/reference/xna40/differential"/name;
        if (std::filesystem::is_regular_file(path)) { return path; }
        ADD_FAILURE() << "Missing genuine XNA fixture " << name;
        return {};
    }
    std::vector<std::uint8_t> Tq063AssetBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path,std::ios::binary);
        EXPECT_TRUE(input.good()) << path;
        return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    }
}

TEST(XnbAssetWriterTest, Tq063CanonicalAssetsReproduceCompleteGenuineXnaFrames)
{
    // Oracle: committed genuine XNA output, never CNA output. The runtime decoder
    // supplies input values only; it does not construct or normalize the expected bytes.
    // This complements source-to-output corpus tests by entering the canonical writer directly.
    for (const std::string fixture : {"texture_png4x4_no_premultiply.xnb", "texture_dds_cube.xnb",
            "texture_volume_dxt.xnb", "audio_wav_soundeffect.xnb", "font_description.xnb",
            "model_x_textured.xnb"})
    {
        SCOPED_TRACE(fixture);
        const auto path=Tq063AssetFixture(fixture);
        const auto expected=Tq063AssetBytes(path);
        ASSERT_GE(expected.size(),10u);
        ASSERT_EQ(expected[3],'w'); ASSERT_EQ(expected[4],5); ASSERT_EQ(expected[5]&128,0);
        const auto decoded=DecodeXnbCanonicalAsset(path);
        XnbFileOptions options;
        options.graphicsProfile=(expected[5]&1) ? XnbGraphicsProfile::HiDef : XnbGraphicsProfile::Reach;
        const auto written=std::visit([&](const auto& value) -> std::vector<std::uint8_t> {
            using T=std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T,XnbTextureData>)
            {
                switch (value.kind)
                {
                    case XnbTextureKind::Texture2D: return WriteXnbAsset(XnbTexture2DContent{value},options);
                    case XnbTextureKind::Texture3D: return WriteXnbAsset(XnbTexture3DContent{value},options);
                    case XnbTextureKind::TextureCube: return WriteXnbAsset(XnbTextureCubeContent{value},options);
                }
                throw std::runtime_error("unrecognized canonical texture kind");
            }
            else { return WriteXnbAsset(value,options); }
        },decoded.value);
        EXPECT_EQ(written,expected);
    }
}

TEST(XnbAssetWriterTest, Tq063CompiledEffectPreservesGenuineLengthManifestAndBytecode)
{
    // Genuine effect_fx_source_reach.xnb: root payload begins at 163, index=1,
    // LE length 0x055c=1372 at 164, code begins at 168. No CNA reader parses the oracle.
    const auto expected=Tq063AssetBytes(Tq063AssetFixture("effect_fx_source_reach.xnb"));
    ASSERT_EQ(expected.size(),1540u);
    ASSERT_EQ(expected[163],1);
    EXPECT_EQ((std::vector<std::uint8_t>(expected.begin()+164,expected.begin()+168)),
              (std::vector<std::uint8_t>{0x5c,0x05,0,0}));
    XnbCompiledEffectContent source;
    source.bytecode.assign(expected.begin()+168,expected.end());
    EXPECT_EQ(WriteXnbAsset(source),expected);
}

TEST(XnbAssetWriterTest, Tq063ModelBoneIndexChangesWidthAt255Bones)
{
    // Refresh ModelContent.WriteBoneIndex IL_0000-0035: count+1<=255 uses
    // one byte, otherwise Int32. No names, meshes, or shared resources in this graph.
    for (const std::uint32_t count : {254u,255u})
    {
        XnbModelData model;
        for (std::uint32_t i=0;i<count;++i)
        {
            XnbModelBoneData bone;
            bone.nameIsNull=true; bone.transform=Matrix::getIdentityProperty();
            bone.parent=(i==0 ? -1 : 0);
            if (i==0) { for (std::uint32_t child=1;child<count;++child) { bone.children.push_back(child); } }
            model.bones.push_back(bone);
        }
        model.rootBone=0;
        std::vector<std::uint8_t> payload{1};
        const auto word=[&](std::uint32_t value) {
            for (int shift : {0,8,16,24}) { payload.push_back(static_cast<std::uint8_t>(value>>shift)); }
        };
        const auto reference=[&](std::uint32_t value) {
            if (count==254) { payload.push_back(static_cast<std::uint8_t>(value)); }
            else { word(value); }
        };
        word(count);
        for (std::uint32_t i=0;i<count;++i)
        {
            payload.push_back(0); // null name
            for (std::uint32_t component=0;component<16;++component)
            { word(component%5==0 ? 0x3f800000 : 0); }
        }
        for (std::uint32_t i=0;i<count;++i)
        {
            reference(i==0 ? 0 : 1);
            word(i==0 ? count-1 : 0);
            if (i==0) { for (std::uint32_t child=1;child<count;++child) { reference(child+1); } }
        }
        word(0); reference(1); payload.push_back(0); // mesh count, root, null tag
        const std::string reader="Microsoft.Xna.Framework.Content.ModelReader, Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553";
        // Manifest name is 144 bytes; v5 7-bit string length=90 01.
        ASSERT_EQ(reader.size(),144u);
        std::vector<std::uint8_t> expected{'X','N','B','w',5,0};
        const std::uint32_t total=10+1+2+144+4+1+static_cast<std::uint32_t>(payload.size());
        for (int shift : {0,8,16,24}) { expected.push_back(static_cast<std::uint8_t>(total>>shift)); }
        expected.insert(expected.end(),{1,0x90,1});
        expected.insert(expected.end(),reader.begin(),reader.end());
        expected.insert(expected.end(),{0,0,0,0,0});
        expected.insert(expected.end(),payload.begin(),payload.end());
        EXPECT_EQ(WriteXnbAsset(model),expected) << count;
    }
}


namespace
{
    template<typename T>
    void Tq063ExpectStockEffect(const T& source,const std::string& reader,
                               std::vector<std::uint8_t> payload)
    {
        SCOPED_TRACE(reader);
        const std::string name="Microsoft.Xna.Framework.Content."+reader+
            ", Microsoft.Xna.Framework.Graphics, Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553";
        std::vector<std::uint8_t> body{1,static_cast<std::uint8_t>((name.size()&127)|128),
            static_cast<std::uint8_t>(name.size()>>7)};
        body.insert(body.end(),name.begin(),name.end());
        body.insert(body.end(),{0,0,0,0,0,1}); // version 0, shared 0, root 1
        body.insert(body.end(),payload.begin(),payload.end());
        std::vector<std::uint8_t> expected{'X','N','B','w',5,0};
        const auto total=static_cast<std::uint32_t>(10+body.size());
        for (int shift : {0,8,16,24}) { expected.push_back(static_cast<std::uint8_t>(total>>shift)); }
        expected.insert(expected.end(),body.begin(),body.end());
        EXPECT_EQ(WriteXnbAsset(source),expected);
    }
    std::vector<std::uint8_t> Tq063EffectFields(std::vector<std::uint8_t> prefix,
                                             std::initializer_list<std::uint32_t> words)
    {
        for (const auto word : words)
        { for (int shift : {0,8,16,24}) { prefix.push_back(static_cast<std::uint8_t>(word>>shift)); } }
        return prefix;
    }
}

TEST(XnbAssetWriterTest, Tq063StockEffectPayloadsPreserveEveryField)
{
    // Refresh Basic/AlphaTest/DualTexture/EnvironmentMap/SkinnedEffectWriter.Write:
    // strings have no dispatch id; all property floats are written unchanged.
    // Distinct IEEE754 integer words 1..11 and 0.5 are exact, so tolerance is zero.
    XnbBasicEffectData basic;
    basic.textureReference="q"; basic.diffuseColor={1,2,3}; basic.emissiveColor={4,5,6};
    basic.specularColor={7,8,9}; basic.specularPower=10; basic.alpha=0.5f; basic.vertexColorEnabled=true;
    auto b=Tq063EffectFields({1,'q'},{0x3f800000,0x40000000,0x40400000,0x40800000,
        0x40a00000,0x40c00000,0x40e00000,0x41000000,0x41100000,0x41200000,0x3f000000});
    b.push_back(1); Tq063ExpectStockEffect(basic,"BasicEffectReader",b);
    XnbAlphaTestEffectData alpha;
    alpha.textureReference="q"; alpha.alphaFunction=7; alpha.referenceAlpha=0x12345678;
    alpha.diffuseColor={1,2,3}; alpha.alpha=0.5f; alpha.vertexColorEnabled=true;
    auto a=Tq063EffectFields({1,'q'},{7,0x12345678,0x3f800000,0x40000000,0x40400000,0x3f000000});
    a.push_back(1); Tq063ExpectStockEffect(alpha,"AlphaTestEffectReader",a);
    XnbDualTextureEffectData dual;
    dual.textureReference="q"; dual.texture2Reference="r"; dual.diffuseColor={1,2,3};
    dual.alpha=0.5f; dual.vertexColorEnabled=true;
    auto d=Tq063EffectFields({1,'q',1,'r'},{0x3f800000,0x40000000,0x40400000,0x3f000000});
    d.push_back(1); Tq063ExpectStockEffect(dual,"DualTextureEffectReader",d);
    XnbEnvironmentMapEffectData environment;
    environment.textureReference="q"; environment.environmentMapReference="r";
    environment.environmentMapAmount=1; environment.environmentMapSpecular={2,3,4};
    environment.fresnelFactor=5; environment.diffuseColor={6,7,8}; environment.emissiveColor={9,10,11};
    environment.alpha=0.5f;
    Tq063ExpectStockEffect(environment,"EnvironmentMapEffectReader",Tq063EffectFields({1,'q',1,'r'},
        {0x3f800000,0x40000000,0x40400000,0x40800000,0x40a00000,0x40c00000,
         0x40e00000,0x41000000,0x41100000,0x41200000,0x41300000,0x3f000000}));
    XnbSkinnedEffectData skinned;
    skinned.textureReference="q"; skinned.weightsPerVertex=4; skinned.diffuseColor={1,2,3};
    skinned.emissiveColor={4,5,6}; skinned.specularColor={7,8,9}; skinned.specularPower=10; skinned.alpha=0.5f;
    Tq063ExpectStockEffect(skinned,"SkinnedEffectReader",Tq063EffectFields({1,'q'},
        {4,0x3f800000,0x40000000,0x40400000,0x40800000,0x40a00000,0x40c00000,
         0x40e00000,0x41000000,0x41100000,0x41200000,0x3f000000}));
}

TEST(XnbAssetWriterTest, Tq063SpriteFontDefaultCharacterIsAnOptionalUtf8Tail)
{
    // Refresh SpriteFontWriter.Write IL_0055-008c: Boolean, then UTF-8 Char iff present.
    XnbSpriteFontData font;
    font.atlas=MakeTexture2D(); font.glyphs={Rectangle{0,0,1,1}};
    font.cropping=font.glyphs; font.characters={u'\u03a9'}; font.lineSpacing=1;
    font.kerning={Vector3{0,1,0}};
    const auto absent=WriteXnbAsset(font);
    font.defaultCharacter=u'\u03a9';
    const auto present=WriteXnbAsset(font);
    ASSERT_GE(absent.size(),11u); ASSERT_GE(present.size(),13u);
    EXPECT_EQ(absent.back(),0);
    EXPECT_EQ((std::vector<std::uint8_t>(present.end()-3,present.end())),
              (std::vector<std::uint8_t>{1,0xce,0xa9}));
    EXPECT_EQ(present.size(),absent.size()+2);
    EXPECT_TRUE(std::equal(absent.begin()+10,absent.end()-1,present.begin()+10,present.end()-3));
}
