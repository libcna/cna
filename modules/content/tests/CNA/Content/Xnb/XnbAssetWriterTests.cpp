// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-011/012/013/014/016/017: the asset-level .xnb writers and the
// pipeline's second output format. Each asset is written, then decoded by CNA's own headless
// canonical XNB decoder -- the same one the .xnb -> CNB compatibility importer uses -- so a
// writer that disagrees with the reader about a field order or a wire form fails here.

#include <any>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutput.hpp"
#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

using namespace CNA::Content::Xnb;
using namespace Microsoft::Xna::Framework;
namespace Cnb = CNA::Content::Cnb;
namespace Pipeline = CNA::Content::Pipeline;
namespace InternalXnb = CNA::Internal::Xnb;

namespace
{
    /** @brief Writes bytes to a unique temporary file and removes it on destruction. */
    class TemporaryXnbFile
    {
    public:
        explicit TemporaryXnbFile(const std::vector<std::uint8_t>& bytes)
        {
            static std::atomic_uint counter{0u};
            path_ = std::filesystem::temp_directory_path() /
                    ("cna_xnb_writer_" + std::to_string(counter.fetch_add(1u)) + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this)) + ".xnb");
            std::ofstream file(path_, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            file.close();
        }

        ~TemporaryXnbFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }

        TemporaryXnbFile(const TemporaryXnbFile&) = delete;
        TemporaryXnbFile& operator=(const TemporaryXnbFile&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

    private:
        std::filesystem::path path_;
    };

    class XnbAssetWriterTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RegisterBuiltInXnbTypeWriters(registry_);
            RegisterXnbAssetTypeWriters(registry_);
        }

        /** @brief Writes a root asset, then decodes it with the headless canonical decoder. */
        [[nodiscard]] InternalXnb::XnbCanonicalAsset WriteAndDecode(const std::string& typeName,
                                                                    const std::any& value)
        {
            const std::vector<std::uint8_t> file =
                WriteXnbFile(registry_, options_, typeName, value);
            const TemporaryXnbFile temporary(file);
            return InternalXnb::DecodeXnbCanonicalAsset(temporary.Path());
        }

        XnbTypeWriterRegistry registry_;
        XnbFileOptions options_{};
    };

    /** @brief Builds an opaque RGBA gradient of the requested size. */
    [[nodiscard]] std::vector<std::uint8_t> MakeGradient(const std::uint32_t width,
                                                         const std::uint32_t height)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
                pixels[offset + 0u] = static_cast<std::uint8_t>(x * 8u + 1u);
                pixels[offset + 1u] = static_cast<std::uint8_t>(y * 8u + 2u);
                pixels[offset + 2u] = static_cast<std::uint8_t>((x + y) * 4u + 3u);
                pixels[offset + 3u] = 255u;
            }
        }
        return pixels;
    }
}

// -- Texture2D --

TEST_F(XnbAssetWriterTest, ATexture2DRoundTripsThroughTheCanonicalDecoder)
{
    const Cnb::CnbTextureData texture = Cnb::MakeRgba8Texture2DData(4u, 3u, MakeGradient(4u, 3u));

    XnbTextureAsset asset;
    asset.shape = XnbTextureAsset::Shape::Texture2D;
    asset.data = texture;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTextureTypeName(XnbTextureAsset::Shape::Texture2D), std::any(asset));

    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.Texture2DReader");
    EXPECT_EQ(decoded.version, 5);
    EXPECT_EQ(decoded.platform, 'w');
    EXPECT_EQ(decoded.compression, InternalXnb::XnbCompression::None);

    const auto& value = std::get<InternalXnb::XnbTextureData>(decoded.value);
    EXPECT_EQ(value.kind, InternalXnb::XnbTextureKind::Texture2D);
    EXPECT_EQ(value.surfaceFormat, Graphics::SurfaceFormat::Color);
    EXPECT_EQ(value.width, 4u);
    EXPECT_EQ(value.height, 3u);
    EXPECT_EQ(value.mipCount, 1u);
    ASSERT_EQ(value.levels.size(), 1u);
    EXPECT_EQ(value.levels[0], texture.representations[0].levels[0]);
}

TEST_F(XnbAssetWriterTest, ATexture2DMipChainKeepsEveryLevelInOrder)
{
    Cnb::CnbTextureData texture;
    texture.width = 4u;
    texture.height = 4u;
    texture.mipCount = 3u;
    Cnb::CnbTextureRepresentation representation;
    representation.format = Cnb::CnbTextureFormat::Rgba8;
    representation.levels = {MakeGradient(4u, 4u), MakeGradient(2u, 2u), MakeGradient(1u, 1u)};
    texture.representations.push_back(representation);

    XnbTextureAsset asset;
    asset.data = texture;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTextureTypeName(XnbTextureAsset::Shape::Texture2D), std::any(asset));
    const auto& value = std::get<InternalXnb::XnbTextureData>(decoded.value);
    ASSERT_EQ(value.levels.size(), 3u);
    EXPECT_EQ(value.levels[0].size(), 4u * 4u * 4u);
    EXPECT_EQ(value.levels[1].size(), 2u * 2u * 4u);
    EXPECT_EQ(value.levels[2].size(), 1u * 1u * 4u);
    EXPECT_EQ(value.levels[1], representation.levels[1]);
}

TEST_F(XnbAssetWriterTest, ATextureCubeWritesSixFacesInTheFormatsOwnOrder)
{
    Cnb::CnbTextureData texture;
    texture.width = 2u;
    texture.height = 2u;
    texture.faceCount = 6u;
    texture.mipCount = 1u;
    Cnb::CnbTextureRepresentation representation;
    representation.format = Cnb::CnbTextureFormat::Rgba8;
    for (std::uint32_t face = 0u; face < 6u; ++face)
    {
        std::vector<std::uint8_t> pixels(2u * 2u * 4u, static_cast<std::uint8_t>(face + 1u));
        representation.levels.push_back(std::move(pixels));
    }
    texture.representations.push_back(representation);

    XnbTextureAsset asset;
    asset.shape = XnbTextureAsset::Shape::TextureCube;
    asset.data = texture;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTextureTypeName(XnbTextureAsset::Shape::TextureCube), std::any(asset));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.TextureCubeReader");
    const auto& value = std::get<InternalXnb::XnbTextureData>(decoded.value);
    EXPECT_EQ(value.kind, InternalXnb::XnbTextureKind::TextureCube);
    EXPECT_EQ(value.faceCount, 6u);
    ASSERT_EQ(value.levels.size(), 6u);
    for (std::uint32_t face = 0u; face < 6u; ++face)
    {
        EXPECT_EQ(value.levels[face][0], static_cast<std::uint8_t>(face + 1u)) << face;
    }
}

TEST_F(XnbAssetWriterTest, ATexture3DKeepsItsDepth)
{
    Cnb::CnbTextureData texture;
    texture.width = 2u;
    texture.height = 2u;
    texture.depth = 2u;
    texture.mipCount = 1u;
    Cnb::CnbTextureRepresentation representation;
    representation.format = Cnb::CnbTextureFormat::Rgba8;
    representation.levels = {std::vector<std::uint8_t>(2u * 2u * 2u * 4u, 7u)};
    texture.representations.push_back(representation);

    XnbTextureAsset asset;
    asset.shape = XnbTextureAsset::Shape::Texture3D;
    asset.data = texture;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTextureTypeName(XnbTextureAsset::Shape::Texture3D), std::any(asset));
    const auto& value = std::get<InternalXnb::XnbTextureData>(decoded.value);
    EXPECT_EQ(value.kind, InternalXnb::XnbTextureKind::Texture3D);
    EXPECT_EQ(value.depth, 2u);
    ASSERT_EQ(value.levels.size(), 1u);
    EXPECT_EQ(value.levels[0].size(), 2u * 2u * 2u * 4u);
}

TEST_F(XnbAssetWriterTest, AShapeMismatchIsRefusedRatherThanWritten)
{
    Cnb::CnbTextureData texture = Cnb::MakeRgba8Texture2DData(2u, 2u, MakeGradient(2u, 2u));
    XnbTextureAsset asset;
    asset.shape = XnbTextureAsset::Shape::TextureCube;   // but the data has one face
    asset.data = texture;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTextureTypeName(XnbTextureAsset::Shape::TextureCube),
                                    std::any(asset)),
                 XnbWriteException);
}

TEST_F(XnbAssetWriterTest, ALevelWhoseByteCountDisagreesWithItsDimensionsIsRefused)
{
    Cnb::CnbTextureData texture;
    texture.width = 4u;
    texture.height = 4u;
    texture.mipCount = 1u;
    Cnb::CnbTextureRepresentation representation;
    representation.format = Cnb::CnbTextureFormat::Rgba8;
    representation.levels = {std::vector<std::uint8_t>(10u, 0u)};   // not 4*4*4
    texture.representations.push_back(representation);

    XnbTextureAsset asset;
    asset.data = texture;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTextureTypeName(XnbTextureAsset::Shape::Texture2D),
                                    std::any(asset)),
                 XnbWriteException);
}

TEST_F(XnbAssetWriterTest, ACnaOnlyTextureFormatIsRefusedWithANamedReason)
{
    EXPECT_EQ(XnbSurfaceFormatFor(Cnb::CnbTextureFormat::Rgba8), Graphics::SurfaceFormat::Color);
    EXPECT_EQ(XnbSurfaceFormatFor(Cnb::CnbTextureFormat::Bc1), Graphics::SurfaceFormat::Dxt1);
    EXPECT_EQ(XnbSurfaceFormatFor(Cnb::CnbTextureFormat::Bc3), Graphics::SurfaceFormat::Dxt5);

    // Bc7 is real in CNB and has no XNA 4.0 SurfaceFormat at all.
    EXPECT_THROW((void)XnbSurfaceFormatFor(Cnb::CnbTextureFormat::Bc7), XnbWriteException);
    EXPECT_THROW((void)XnbSurfaceFormatValue(Graphics::SurfaceFormat::Bc7EXT), XnbWriteException);
    EXPECT_THROW((void)XnbSurfaceFormatValue(Graphics::SurfaceFormat::ColorBgraEXT),
                 XnbWriteException);
}

// -- SpriteFont --

TEST_F(XnbAssetWriterTest, ASpriteFontRoundTripsEveryParallelListAndItsAtlas)
{
    Cnb::CnbSpriteFontData font;
    font.atlas = Cnb::MakeRgba8Texture2DData(4u, 4u, MakeGradient(4u, 4u));
    font.glyphBounds = {Rectangle(0, 0, 2, 2), Rectangle(2, 0, 2, 2)};
    font.cropping = {Rectangle(0, 0, 0, 0), Rectangle(1, 1, 0, 0)};
    font.kerning = {Vector3(0.0f, 2.0f, 0.0f), Vector3(-1.0f, 2.0f, 1.0f)};
    font.characters = {u'A', u'B'};
    font.lineSpacing = 12;
    font.spacing = 1.5f;
    font.defaultCharacter = u'A';

    XnbSpriteFontAsset asset;
    asset.data = font;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<XnbSpriteFontAsset>::Name(), std::any(asset));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.SpriteFontReader");

    const auto& value = std::get<InternalXnb::XnbSpriteFontData>(decoded.value);
    EXPECT_EQ(value.atlas.width, 4u);
    EXPECT_EQ(value.atlas.height, 4u);
    ASSERT_EQ(value.glyphs.size(), 2u);
    EXPECT_EQ(value.glyphs[1].X, 2);
    ASSERT_EQ(value.cropping.size(), 2u);
    EXPECT_EQ(value.cropping[1].Y, 1);
    ASSERT_EQ(value.characters.size(), 2u);
    EXPECT_EQ(value.characters[0], u'A');
    EXPECT_EQ(value.lineSpacing, 12);
    EXPECT_FLOAT_EQ(value.spacing, 1.5f);
    ASSERT_EQ(value.kerning.size(), 2u);
    EXPECT_FLOAT_EQ(value.kerning[1].X, -1.0f);
    ASSERT_TRUE(value.defaultCharacter.has_value());
    EXPECT_EQ(*value.defaultCharacter, u'A');
}

TEST_F(XnbAssetWriterTest, ASpriteFontWithoutADefaultCharacterRoundTrips)
{
    Cnb::CnbSpriteFontData font;
    font.atlas = Cnb::MakeRgba8Texture2DData(2u, 2u, MakeGradient(2u, 2u));
    font.glyphBounds = {Rectangle(0, 0, 1, 1)};
    font.cropping = {Rectangle(0, 0, 0, 0)};
    font.kerning = {Vector3(0.0f, 1.0f, 0.0f)};
    font.characters = {u'x'};
    font.lineSpacing = 8;
    font.spacing = 0.0f;

    XnbSpriteFontAsset asset;
    asset.data = font;
    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<XnbSpriteFontAsset>::Name(), std::any(asset));
    const auto& value = std::get<InternalXnb::XnbSpriteFontData>(decoded.value);
    EXPECT_FALSE(value.defaultCharacter.has_value());
}

TEST_F(XnbAssetWriterTest, AnInconsistentSpriteFontIsRefused)
{
    Cnb::CnbSpriteFontData font;
    font.atlas = Cnb::MakeRgba8Texture2DData(2u, 2u, MakeGradient(2u, 2u));
    font.glyphBounds = {Rectangle(0, 0, 1, 1), Rectangle(1, 0, 1, 1)};
    font.cropping = {Rectangle(0, 0, 0, 0)};   // one short
    font.kerning = {Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)};
    font.characters = {u'a', u'b'};

    XnbSpriteFontAsset asset;
    asset.data = font;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<XnbSpriteFontAsset>::Name(), std::any(asset)),
                 XnbWriteException);

    // A character map that is not strictly ascending breaks the runtime's binary search.
    font.cropping = {Rectangle(0, 0, 0, 0), Rectangle(0, 0, 0, 0)};
    font.characters = {u'b', u'a'};
    asset.data = font;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<XnbSpriteFontAsset>::Name(), std::any(asset)),
                 XnbWriteException);

    // A default character that is not part of the font would never be found.
    font.characters = {u'a', u'b'};
    font.defaultCharacter = u'z';
    asset.data = font;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<XnbSpriteFontAsset>::Name(), std::any(asset)),
                 XnbWriteException);
}

// -- SoundEffect --

TEST_F(XnbAssetWriterTest, ASoundEffectRoundTripsItsWaveFormatAndSamples)
{
    Cnb::CnbSoundEffectData sound;
    sound.format = Cnb::CnbAudioFormat::Pcm16;
    sound.sampleRate = 44100u;
    sound.channels = 2u;
    sound.frameCount = 8u;
    sound.loopStart = 2u;
    sound.loopLength = 4u;
    sound.samples.resize(8u * 2u * 2u);
    for (std::size_t index = 0u; index < sound.samples.size(); ++index)
    {
        sound.samples[index] = static_cast<std::uint8_t>(index * 3u);
    }

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<Cnb::CnbSoundEffectData>::Name(), std::any(sound));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.SoundEffectReader");

    const auto& value = std::get<InternalXnb::XnbSoundEffectData>(decoded.value);
    EXPECT_EQ(value.formatTag, 1u);
    EXPECT_EQ(value.channels, 2u);
    EXPECT_EQ(value.sampleRate, 44100u);
    EXPECT_EQ(value.blockAlign, 4u);
    EXPECT_EQ(value.bitsPerSample, 16u);
    EXPECT_EQ(value.averageBytesPerSecond, 44100u * 4u);
    EXPECT_EQ(value.samples, sound.samples);
    EXPECT_EQ(value.loopStart, 2 * 4);
    EXPECT_EQ(value.loopLength, 4 * 4);
}

TEST_F(XnbAssetWriterTest, AnInconsistentSoundEffectIsRefused)
{
    Cnb::CnbSoundEffectData sound;
    sound.format = Cnb::CnbAudioFormat::Pcm16;
    sound.sampleRate = 22050u;
    sound.channels = 1u;
    sound.frameCount = 4u;
    sound.samples.resize(3u);   // not frameCount * blockAlign
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<Cnb::CnbSoundEffectData>::Name(), std::any(sound)),
                 XnbWriteException);

    sound.samples.resize(8u);
    sound.loopStart = 3u;
    sound.loopLength = 4u;   // runs past the end
    EXPECT_THROW((void)WriteXnbFile(registry_, options_,
                                    XnbTypeKey<Cnb::CnbSoundEffectData>::Name(), std::any(sound)),
                 XnbWriteException);
}

// -- Curve, Song, Video --

TEST_F(XnbAssetWriterTest, ACurveRoundTripsAsARootAsset)
{
    Curve curve;
    curve.setPreLoopProperty(CurveLoopType::Linear);
    curve.setPostLoopProperty(CurveLoopType::Constant);
    curve.getKeysProperty().Add(CurveKey(0.5f, 1.5f, 0.25f, 0.75f, CurveContinuity::Smooth));

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<Curve>::Name(), std::any(curve));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.CurveReader");
    const auto& value = std::get<Curve>(decoded.value);
    EXPECT_EQ(value.getPreLoopProperty(), CurveLoopType::Linear);
    ASSERT_EQ(value.getKeysProperty().getCountProperty(), 1);
    EXPECT_FLOAT_EQ(value.getKeysProperty()[0].getTangentOutProperty(), 0.75f);
}

TEST_F(XnbAssetWriterTest, ASongIsWrittenWithItsDurationAsADispatchedInt32)
{
    Cnb::CnbSongData song;
    song.streamReference = "music/theme.ogg";
    song.durationMs = 3005u;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<Cnb::CnbSongData>::Name(), std::any(song));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.SongReader");
    const auto& value = std::get<InternalXnb::XnbSongData>(decoded.value);
    EXPECT_EQ(value.mediaPath, "music/theme.ogg");
    EXPECT_EQ(value.durationMs, 3005);
}

TEST_F(XnbAssetWriterTest, AVideoIsWrittenWithEveryFieldAsADispatchedObject)
{
    Cnb::CnbVideoData video;
    video.streamReference = "movies/intro.wmv";
    video.durationMs = 12000u;
    video.width = 640u;
    video.height = 480u;
    video.framesPerSecond = 29.97f;
    video.soundtrackType = 2u;

    const InternalXnb::XnbCanonicalAsset decoded =
        WriteAndDecode(XnbTypeKey<Cnb::CnbVideoData>::Name(), std::any(video));
    EXPECT_EQ(decoded.rootReader, "Microsoft.Xna.Framework.Content.VideoReader");
    const auto& value = std::get<InternalXnb::XnbVideoData>(decoded.value);
    EXPECT_EQ(value.mediaPath, "movies/intro.wmv");
    EXPECT_EQ(value.durationMs, 12000);
    EXPECT_EQ(value.width, 640);
    EXPECT_EQ(value.height, 480);
    EXPECT_FLOAT_EQ(value.framesPerSecond, 29.97f);
    EXPECT_EQ(value.soundtrackType, 2);
}

TEST_F(XnbAssetWriterTest, AnEmptyStreamingReferenceIsRefused)
{
    Cnb::CnbSongData song;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_, XnbTypeKey<Cnb::CnbSongData>::Name(),
                                    std::any(song)),
                 XnbWriteException);

    Cnb::CnbVideoData video;
    EXPECT_THROW((void)WriteXnbFile(registry_, options_, XnbTypeKey<Cnb::CnbVideoData>::Name(),
                                    std::any(video)),
                 XnbWriteException);
}

// -- Output-format helpers --

TEST(ContentOutputFormatTest, StableSpellingsAndExtensionsRoundTrip)
{
    EXPECT_STREQ(Pipeline::ContentOutputFormatName(Pipeline::ContentOutputFormat::Cnb), "cnb");
    EXPECT_STREQ(Pipeline::ContentOutputFormatName(Pipeline::ContentOutputFormat::Xnb), "xnb");
    EXPECT_STREQ(Pipeline::ContentOutputFormatExtension(Pipeline::ContentOutputFormat::Cnb),
                 ".cnb");
    EXPECT_STREQ(Pipeline::ContentOutputFormatExtension(Pipeline::ContentOutputFormat::Xnb),
                 ".xnb");
    EXPECT_EQ(Pipeline::ParseContentOutputFormat("cnb"), Pipeline::ContentOutputFormat::Cnb);
    EXPECT_EQ(Pipeline::ParseContentOutputFormat("xnb"), Pipeline::ContentOutputFormat::Xnb);
    EXPECT_THROW((void)Pipeline::ParseContentOutputFormat("CNB"), std::invalid_argument);
    EXPECT_THROW((void)Pipeline::ParseContentOutputFormat("cnj"), std::invalid_argument);
}
