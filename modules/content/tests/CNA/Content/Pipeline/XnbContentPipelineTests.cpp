// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbContentPipeline.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
namespace Xnb = CNA::Internal::Xnb;

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SpriteFont;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Media::Song;
using Microsoft::Xna::Framework::Media::Video;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_xnb_" + tag + "_" +
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

    void WriteBytes(const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& bytes)
    {
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::filesystem::path FindXnbFixture(const std::string& relative)
    {
        for (const char* prefix : {"tests/assets/xnb/", "../tests/assets/xnb/",
                                   "../../tests/assets/xnb/"})
        {
            const std::filesystem::path candidate = std::string(prefix) + relative;
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::weakly_canonical(candidate);
            }
        }
        return {};
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        Pipeline::RegisterSoundEffectContentPipeline(*registry);
        Pipeline::RegisterSongContentPipeline(*registry);
        Pipeline::RegisterVideoContentPipeline(*registry);
        Pipeline::RegisterCnjContentPipeline(*registry);
        Pipeline::RegisterXnbContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult Build(
        const std::filesystem::path& sourceRoot,
        const std::filesystem::path& source,
        const std::string& logicalName)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = sourceRoot;
        request.source = source;
        request.logicalName = logicalName;
        return pipeline.Build(request);
    }

    Pipeline::ContentBuildResult BuildFixture(
        const std::filesystem::path& fixture, const std::string& logicalName)
    {
        return Build(fixture.parent_path(), fixture.filename(), logicalName);
    }

    Cnb::CnbDocument ParseOutput(const Pipeline::ContentBuildResult& result)
    {
        return Cnb::CnbDocument::Parse(result.output.bytes, "XNB pipeline test output");
    }

    void ExpectTextureEqual(const Cnb::CnbTextureData& expected,
                            const Cnb::CnbTextureData& actual)
    {
        EXPECT_EQ(actual.width, expected.width);
        EXPECT_EQ(actual.height, expected.height);
        EXPECT_EQ(actual.depth, expected.depth);
        EXPECT_EQ(actual.faceCount, expected.faceCount);
        EXPECT_EQ(actual.mipCount, expected.mipCount);
        ASSERT_EQ(actual.representations.size(), expected.representations.size());
        for (std::size_t representation = 0u;
             representation < expected.representations.size(); ++representation)
        {
            EXPECT_EQ(actual.representations[representation].format,
                      expected.representations[representation].format);
            EXPECT_EQ(actual.representations[representation].levels,
                      expected.representations[representation].levels);
        }
    }

    class ByteWriter
    {
    public:
        void U8(const std::uint8_t value) { bytes.push_back(value); }

        void U32(const std::uint32_t value)
        {
            for (unsigned int shift = 0u; shift < 32u; shift += 8u)
            {
                U8(static_cast<std::uint8_t>(value >> shift));
            }
        }

        void I32(const std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }

        void F32(const float value) { U32(std::bit_cast<std::uint32_t>(value)); }

        void SevenBit(std::uint32_t value)
        {
            while (value >= 0x80u)
            {
                U8(static_cast<std::uint8_t>(value | 0x80u));
                value >>= 7u;
            }
            U8(static_cast<std::uint8_t>(value));
        }

        void String(const std::string& value)
        {
            SevenBit(static_cast<std::uint32_t>(value.size()));
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        void Bytes(const std::vector<std::uint8_t>& value)
        {
            bytes.insert(bytes.end(), value.begin(), value.end());
        }

        std::vector<std::uint8_t> bytes;
    };

    std::vector<std::uint8_t> MakeXnb(
        const std::vector<std::string>& readers, const std::vector<std::uint8_t>& payload,
        const std::uint32_t sharedResources = 0u, const char platform = 'w',
        const std::uint8_t version = 5u)
    {
        ByteWriter body;
        body.SevenBit(static_cast<std::uint32_t>(readers.size()));
        for (const std::string& reader : readers)
        {
            body.String(reader);
            body.I32(0);
        }
        body.SevenBit(sharedResources);
        body.SevenBit(1u);
        body.Bytes(payload);

        std::vector<std::uint8_t> result{
            'X', 'N', 'B', static_cast<std::uint8_t>(platform), version, 0u, 0u, 0u, 0u, 0u};
        result.insert(result.end(), body.bytes.begin(), body.bytes.end());
        const std::uint32_t size = static_cast<std::uint32_t>(result.size());
        for (unsigned int byte = 0u; byte < 4u; ++byte)
        {
            result[6u + byte] = static_cast<std::uint8_t>(size >> (byte * 8u));
        }
        return result;
    }

    std::vector<std::uint8_t> MakeTexture2DXnb(
        const std::int32_t format, const std::int32_t width, const std::int32_t height,
        const std::int32_t declaredLevelBytes, const std::vector<std::uint8_t>& level,
        const char platform = 'w', const std::uint8_t version = 5u)
    {
        ByteWriter payload;
        payload.I32(format);
        payload.I32(width);
        payload.I32(height);
        payload.I32(1);
        payload.I32(declaredLevelBytes);
        payload.Bytes(level);
        return MakeXnb(
            {"Microsoft.Xna.Framework.Content.Texture2DReader"}, payload.bytes,
            0u, platform, version);
    }

    std::vector<std::uint8_t> MakeTexture3DXnb()
    {
        ByteWriter payload;
        payload.I32(0); // SurfaceFormat.Color
        payload.I32(4);
        payload.I32(2);
        payload.I32(2);
        payload.I32(3);
        for (const std::size_t size : {64u, 8u, 4u})
        {
            payload.I32(static_cast<std::int32_t>(size));
            for (std::size_t index = 0u; index < size; ++index)
            {
                payload.U8(static_cast<std::uint8_t>(index * 17u + size));
            }
        }
        return MakeXnb(
            {"Microsoft.Xna.Framework.Content.Texture3DReader"}, payload.bytes);
    }

    std::vector<std::uint8_t> MakeCurveXnb()
    {
        ByteWriter payload;
        payload.I32(1); // CurveLoopType::Cycle
        payload.I32(3); // CurveLoopType::Oscillate
        payload.I32(2);
        payload.F32(0.0f); payload.F32(2.0f); payload.F32(-0.5f); payload.F32(0.5f); payload.I32(0);
        payload.F32(4.0f); payload.F32(8.0f); payload.F32(1.5f); payload.F32(2.5f); payload.I32(1);
        return MakeXnb(
            {"Microsoft.Xna.Framework.Content.CurveReader"}, payload.bytes);
    }

    std::vector<std::uint8_t> MakeVideoXnb(const std::string& mediaPath)
    {
        ByteWriter payload;
        payload.SevenBit(2u);
        payload.String(mediaPath);
        payload.SevenBit(3u); payload.I32(2345);
        payload.SevenBit(3u); payload.I32(320);
        payload.SevenBit(3u); payload.I32(180);
        payload.SevenBit(4u); payload.F32(24.0f);
        payload.SevenBit(3u); payload.I32(2);
        return MakeXnb(
            {"Microsoft.Xna.Framework.Content.VideoReader",
             "Microsoft.Xna.Framework.Content.StringReader",
             "Microsoft.Xna.Framework.Content.Int32Reader",
             "Microsoft.Xna.Framework.Content.SingleReader"},
            payload.bytes);
    }

    void ExpectCurveEqual(const Microsoft::Xna::Framework::Curve& expected,
                          const Microsoft::Xna::Framework::Curve& actual)
    {
        EXPECT_EQ(actual.getPreLoopProperty(), expected.getPreLoopProperty());
        EXPECT_EQ(actual.getPostLoopProperty(), expected.getPostLoopProperty());
        ASSERT_EQ(actual.getKeysProperty().getCountProperty(),
                  expected.getKeysProperty().getCountProperty());
        for (int index = 0; index < expected.getKeysProperty().getCountProperty(); ++index)
        {
            const auto& a = actual.getKeysProperty().getItemProperty(index);
            const auto& e = expected.getKeysProperty().getItemProperty(index);
            EXPECT_FLOAT_EQ(a.getPositionProperty(), e.getPositionProperty());
            EXPECT_FLOAT_EQ(a.getValueProperty(), e.getValueProperty());
            EXPECT_FLOAT_EQ(a.getTangentInProperty(), e.getTangentInProperty());
            EXPECT_FLOAT_EQ(a.getTangentOutProperty(), e.getTangentOutProperty());
            EXPECT_EQ(a.getContinuityProperty(), e.getContinuityProperty());
        }
    }
}

TEST(XnbContentPipelineTest, Texture2DUncompressedAndLzxProduceCanonicalNativeCnbDeterministically)
{
    for (const std::string& relative : {
             "monogame/windows/uncompressed/white-1.xnb",
             "monogame/windows/lzx/Explosion.xnb"})
    {
        const std::filesystem::path fixture = FindXnbFixture(relative);
        ASSERT_FALSE(fixture.empty());
        const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(fixture);
        const Cnb::CnbTextureData expected = Xnb::ConvertXnbTextureToCnbRgba8(
            std::get<Xnb::XnbTextureData>(canonical.value));

        const Pipeline::ContentBuildResult first = BuildFixture(fixture, "Textures/legacy");
        const Pipeline::ContentBuildResult second = BuildFixture(fixture, "Textures/legacy");
        EXPECT_EQ(first.importer,
                  (Pipeline::ContentComponentIdentity{"CNA.XnbImporter", "1"}));
        EXPECT_EQ(first.processor,
                  (Pipeline::ContentComponentIdentity{"CNA.TextureProcessor", "1"}));
        EXPECT_EQ(first.output.bytes, second.output.bytes);
        ExpectTextureEqual(expected, Cnb::DecodeTexture2DFromCnb(ParseOutput(first)));
        EXPECT_EQ(canonical.compression,
                  relative.find("/lzx/") == std::string::npos
                      ? Xnb::XnbCompression::None
                      : Xnb::XnbCompression::Lzx);
    }
}

TEST(XnbContentPipelineTest, ContainerVariantsAndMalformedTexturePayloadsAreHandledExplicitly)
{
    ScratchDirectory scratch("container_matrix");
    const std::filesystem::path legacy = scratch.Path() / "legacy_dxt1.xnb";
    WriteBytes(legacy, MakeTexture2DXnb(
        28, 4, 4, 8, std::vector<std::uint8_t>(8u, 0u), 'l', 4u));
    const Xnb::XnbCanonicalAsset decoded = Xnb::DecodeXnbCanonicalAsset(legacy);
    EXPECT_EQ(decoded.version, 4);
    EXPECT_EQ(decoded.platform, 'l');
    EXPECT_EQ(decoded.compression, Xnb::XnbCompression::None);
    EXPECT_EQ(Build(scratch.Path(), "legacy_dxt1.xnb", "legacy_dxt1")
                  .output.assetTypeId,
              Cnb::CnbAssetTypeId::Texture2D);

    WriteBytes(scratch.Path() / "bad_dimensions.xnb",
               MakeTexture2DXnb(0, 0, 1, 0, {}));
    EXPECT_THROW(
        static_cast<void>(Build(scratch.Path(), "bad_dimensions.xnb", "bad_dimensions")),
        Pipeline::ContentPipelineError);

    WriteBytes(scratch.Path() / "unsupported_format.xnb",
               MakeTexture2DXnb(999, 1, 1, 4, {0u, 0u, 0u, 0u}));
    EXPECT_THROW(
        static_cast<void>(Build(scratch.Path(), "unsupported_format.xnb", "unsupported_format")),
        Pipeline::ContentPipelineError);

    WriteBytes(scratch.Path() / "truncated_level.xnb",
               MakeTexture2DXnb(0, 1, 1, 4, {0u, 0u, 0u}));
    EXPECT_THROW(
        static_cast<void>(Build(scratch.Path(), "truncated_level.xnb", "truncated_level")),
        Pipeline::ContentPipelineError);

    std::vector<std::uint8_t> lz4 = MakeTexture2DXnb(0, 1, 1, 4, {0u, 0u, 0u, 0u});
    lz4[5] = 0x40u;
    WriteBytes(scratch.Path() / "lz4.xnb", lz4);
    try
    {
        static_cast<void>(Build(scratch.Path(), "lz4.xnb", "lz4"));
        FAIL() << "LZ4 must not be mistaken for the implemented LZX format";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_NE(std::string(error.what()).find("LZ4"), std::string::npos);
    }
}

TEST(XnbContentPipelineTest, Texture2DRuntimeXnbAndTranscodedCnbHaveIdenticalPixels)
{
    const std::filesystem::path fixture =
        FindXnbFixture("monogame/windows/lzx/Explosion.xnb");
    ASSERT_FALSE(fixture.empty());
    const Pipeline::ContentBuildResult built = BuildFixture(fixture, "Explosion");
    ScratchDirectory scratch("runtime_texture");
    WriteBytes(scratch.Path() / "Explosion.cnb", built.output.bytes);

    GraphicsDevice device;
    Xnb::RegisterAllBuiltInXnbReaders();
    ContentManager xnbContent(nullptr, fixture.parent_path().string());
    xnbContent.setGraphicsDevice(device);
    const Texture2D xnbTexture = xnbContent.Load<Texture2D>("Explosion");
    ContentManager cnbContent(nullptr, scratch.Path().string());
    cnbContent.setGraphicsDevice(device);
    const Texture2D cnbTexture = cnbContent.Load<Texture2D>("Explosion");

    EXPECT_EQ(cnbTexture.getWidthProperty(), xnbTexture.getWidthProperty());
    EXPECT_EQ(cnbTexture.getHeightProperty(), xnbTexture.getHeightProperty());
    EXPECT_EQ(cnbTexture.getFormatProperty(), xnbTexture.getFormatProperty());
    EXPECT_EQ(cnbTexture.getLevelCountProperty(), xnbTexture.getLevelCountProperty());
    for (int level = 0; level < xnbTexture.getLevelCountProperty(); ++level)
    {
        const int width = std::max(1, xnbTexture.getWidthProperty() >> level);
        const int height = std::max(1, xnbTexture.getHeightProperty() >> level);
        std::vector<Color> a(static_cast<std::size_t>(width) * height);
        std::vector<Color> b(a.size());
        xnbTexture.GetData(level, nullptr, a.data(), 0, static_cast<int>(a.size()));
        cnbTexture.GetData(level, nullptr, b.data(), 0, static_cast<int>(b.size()));
        EXPECT_EQ(a, b);
    }
}

TEST(XnbContentPipelineTest, SpriteFontUncompressedAndMultiBlockLzxPreserveAllCanonicalFields)
{
    for (const std::string& relative : {
             "monogame/windows/uncompressed/Default.xnb",
             "monogame/windows/lzx/FontCalibri14.xnb"})
    {
        const std::filesystem::path fixture = FindXnbFixture(relative);
        ASSERT_FALSE(fixture.empty());
        const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(fixture);
        const Xnb::XnbSpriteFontData& expected =
            std::get<Xnb::XnbSpriteFontData>(canonical.value);
        const Pipeline::ContentBuildResult built = BuildFixture(fixture, "Fonts/legacy");
        EXPECT_EQ(built.processor,
                  (Pipeline::ContentComponentIdentity{"CNA.SpriteFontProcessor", "1"}));
        const Cnb::CnbSpriteFontData actual = Cnb::DecodeSpriteFontFromCnb(ParseOutput(built));

        ExpectTextureEqual(Xnb::ConvertXnbTextureToCnbRgba8(expected.atlas), actual.atlas);
        EXPECT_EQ(actual.glyphBounds, expected.glyphs);
        EXPECT_EQ(actual.cropping, expected.cropping);
        EXPECT_EQ(actual.characters, expected.characters);
        EXPECT_EQ(actual.kerning, expected.kerning);
        EXPECT_EQ(actual.lineSpacing, expected.lineSpacing);
        EXPECT_FLOAT_EQ(actual.spacing, expected.spacing);
        EXPECT_EQ(actual.defaultCharacter, expected.defaultCharacter);
    }
}

TEST(XnbContentPipelineTest, SpriteFontRuntimeXnbAndTranscodedCnbHaveEquivalentSemantics)
{
    const std::filesystem::path fixture =
        FindXnbFixture("monogame/windows/uncompressed/Default.xnb");
    ASSERT_FALSE(fixture.empty());
    const Pipeline::ContentBuildResult built = BuildFixture(fixture, "Default");
    ScratchDirectory scratch("runtime_font");
    WriteBytes(scratch.Path() / "Default.cnb", built.output.bytes);

    GraphicsDevice device;
    Xnb::RegisterAllBuiltInXnbReaders();
    ContentManager xnbContent(nullptr, fixture.parent_path().string());
    xnbContent.setGraphicsDevice(device);
    const SpriteFont a = xnbContent.Load<SpriteFont>("Default");
    ContentManager cnbContent(nullptr, scratch.Path().string());
    cnbContent.setGraphicsDevice(device);
    const SpriteFont b = cnbContent.Load<SpriteFont>("Default");

    EXPECT_EQ(b.getCharactersProperty(), a.getCharactersProperty());
    EXPECT_EQ(b.getGlyphBoundsEXT(), a.getGlyphBoundsEXT());
    EXPECT_EQ(b.getCroppingEXT(), a.getCroppingEXT());
    EXPECT_EQ(b.getKerningEXT(), a.getKerningEXT());
    EXPECT_EQ(b.getLineSpacingProperty(), a.getLineSpacingProperty());
    EXPECT_FLOAT_EQ(b.getSpacingProperty(), a.getSpacingProperty());
    EXPECT_EQ(b.getDefaultCharacterProperty(), a.getDefaultCharacterProperty());
    std::vector<Color> atlasA(
        static_cast<std::size_t>(a.getTextureEXT().getWidthProperty()) *
        a.getTextureEXT().getHeightProperty());
    std::vector<Color> atlasB(atlasA.size());
    a.getTextureEXT().GetData(atlasA.data(), static_cast<int>(atlasA.size()));
    b.getTextureEXT().GetData(atlasB.data(), static_cast<int>(atlasB.size()));
    EXPECT_EQ(atlasB, atlasA);
}

TEST(XnbContentPipelineTest, SoundEffectCodecMatrixPreservesDecodedPcmAndMetadata)
{
    for (const std::string& name : {
             "tone_mono_44khz_16bit", "tone_mono_44khz_8bit",
             "tone_mono_44khz_float", "tone_mono_44khz_imaadpcm",
             "tone_mono_44khz_msadpcm", "tone_stereo_44khz_16bit"})
    {
        const std::filesystem::path fixture = FindXnbFixture(
            "monogame/windows/uncompressed/audio/" + name + ".xnb");
        ASSERT_FALSE(fixture.empty());
        const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(fixture);
        const auto imported = Xnb::ConvertXnbSoundToImportedSound(
            std::get<Xnb::XnbSoundEffectData>(canonical.value), name);
        const Cnb::CnbSoundEffectData expected = Cnb::ProcessImportedSoundEffect(imported);
        const Pipeline::ContentBuildResult built = BuildFixture(fixture, "Sounds/" + name);
        const Cnb::CnbSoundEffectData actual =
            Cnb::DecodeSoundEffectFromCnb(ParseOutput(built));

        EXPECT_EQ(actual.format, expected.format);
        EXPECT_EQ(actual.sampleRate, expected.sampleRate);
        EXPECT_EQ(actual.channels, expected.channels);
        EXPECT_EQ(actual.frameCount, expected.frameCount);
        EXPECT_EQ(actual.loopStart, expected.loopStart);
        EXPECT_EQ(actual.loopLength, expected.loopLength);
        EXPECT_EQ(actual.samples, expected.samples);
        EXPECT_EQ(built.output.bytes,
                  BuildFixture(fixture, "Sounds/" + name).output.bytes);
    }
}

TEST(XnbContentPipelineTest, SoundEffectRuntimeXnbAndTranscodedCnbHaveEquivalentPublicSemantics)
{
    const std::filesystem::path fixture = FindXnbFixture(
        "monogame/windows/uncompressed/audio/tone_mono_44khz_16bit.xnb");
    ASSERT_FALSE(fixture.empty());
    const std::string name = "tone_mono_44khz_16bit";
    const Pipeline::ContentBuildResult built = BuildFixture(fixture, name);
    ScratchDirectory scratch("runtime_sound");
    WriteBytes(scratch.Path() / (name + ".cnb"), built.output.bytes);

    Xnb::RegisterAllBuiltInXnbReaders();
    ContentManager xnbContent(nullptr, fixture.parent_path().string());
    const SoundEffect a = xnbContent.Load<SoundEffect>(name);
    ContentManager cnbContent(nullptr, scratch.Path().string());
    const SoundEffect b = cnbContent.Load<SoundEffect>(name);
    EXPECT_EQ(b.getNameProperty(), a.getNameProperty());
    EXPECT_EQ(b.getDurationProperty(), a.getDurationProperty());
}

TEST(XnbContentPipelineTest, TextureCubeDxtFacesAndFullMipChainBecomeNativeRgba8)
{
    const std::filesystem::path fixture = FindXnbFixture(
        "monogame/windows/uncompressed/SampleCube64DXT1Mips.xnb");
    ASSERT_FALSE(fixture.empty());
    const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(fixture);
    const Cnb::CnbTextureData expected = Xnb::ConvertXnbTextureToCnbRgba8(
        std::get<Xnb::XnbTextureData>(canonical.value));
    const Pipeline::ContentBuildResult built = BuildFixture(fixture, "Sky/legacy");
    EXPECT_EQ(built.output.assetTypeId, Cnb::CnbAssetTypeId::TextureCube);
    const Cnb::CnbTextureData actual = Cnb::DecodeTextureCubeFromCnb(ParseOutput(built));
    ExpectTextureEqual(expected, actual);
    EXPECT_EQ(actual.faceCount, 6u);
    EXPECT_EQ(actual.mipCount, 7u);
    ASSERT_EQ(actual.representations.size(), 1u);
    EXPECT_EQ(actual.representations[0].levels.size(), 42u);
}

TEST(XnbContentPipelineTest, Texture3DHandBuiltWireRoutePreservesEveryMipByte)
{
    ScratchDirectory scratch("texture3d");
    const std::filesystem::path source = scratch.Path() / "volume.xnb";
    WriteBytes(source, MakeTexture3DXnb());
    const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(source);
    const Cnb::CnbTextureData expected = Xnb::ConvertXnbTextureToCnbRgba8(
        std::get<Xnb::XnbTextureData>(canonical.value));
    const Pipeline::ContentBuildResult built = Build(scratch.Path(), "volume.xnb", "Volumes/v");
    EXPECT_EQ(built.output.assetTypeId, Cnb::CnbAssetTypeId::Texture3D);
    ExpectTextureEqual(expected, Cnb::DecodeTexture3DFromCnb(ParseOutput(built)));
}

TEST(XnbContentPipelineTest, CurveUsesTheExistingNativeCurveSchemaWithoutASecondRepresentation)
{
    ScratchDirectory scratch("curve");
    const std::filesystem::path source = scratch.Path() / "motion.xnb";
    WriteBytes(source, MakeCurveXnb());
    const Xnb::XnbCanonicalAsset canonical = Xnb::DecodeXnbCanonicalAsset(source);
    const auto& expected = std::get<Microsoft::Xna::Framework::Curve>(canonical.value);
    const Pipeline::ContentBuildResult built = Build(scratch.Path(), "motion.xnb", "Curves/motion");
    EXPECT_EQ(built.output.assetTypeId, Cnb::CnbAssetTypeId::Curve);
    ExpectCurveEqual(expected, Cnb::DecodeCurveFromCnb(ParseOutput(built)));

    const std::filesystem::path nativeRoot = scratch.Path() / "native";
    WriteBytes(nativeRoot / "motion.cnb", built.output.bytes);
    Xnb::RegisterAllBuiltInXnbReaders();
    ContentManager xnbContent(nullptr, scratch.Path().string());
    const Curve a = xnbContent.Load<Curve>("motion");
    ContentManager cnbContent(nullptr, nativeRoot.string());
    const Curve b = cnbContent.Load<Curve>("motion");
    ExpectCurveEqual(a, b);
}

TEST(XnbContentPipelineTest, SongPreservesMetadataAndRecordsExternalMediaAsDependencyAndXref)
{
    const std::filesystem::path fixture = FindXnbFixture(
        "monogame/windows/uncompressed/song/one_two_three.xnb");
    ASSERT_FALSE(fixture.empty());
    const Xnb::XnbSongData expected = std::get<Xnb::XnbSongData>(
        Xnb::DecodeXnbCanonicalAsset(fixture).value);
    const Pipeline::ContentBuildResult built = BuildFixture(fixture, "one_two_three");
    const Cnb::CnbSongData actual = Cnb::DecodeSongFromCnb(ParseOutput(built));
    EXPECT_EQ(actual.durationMs, static_cast<std::uint32_t>(expected.durationMs));
    EXPECT_EQ(actual.name, "one_two_three");
    EXPECT_EQ(actual.streamReference, "one_two_three.ogg");
    ASSERT_EQ(built.dependencies.size(), 2u);
    EXPECT_EQ(built.dependencies[1].kind, Pipeline::ContentDependencyKind::SourceFile);
    ASSERT_EQ(built.runtimeReferences.size(), 1u);
    EXPECT_EQ(built.runtimeReferences[0].logicalName, "one_two_three.ogg");

    ScratchDirectory scratch("runtime_song");
    WriteBytes(scratch.Path() / "one_two_three.cnb", built.output.bytes);
    WriteBytes(scratch.Path() / "one_two_three.ogg",
               ReadBytes(fixture.parent_path() / "one_two_three.ogg"));
    Xnb::RegisterAllBuiltInXnbReaders();
    ContentManager xnbContent(nullptr, fixture.parent_path().string());
    const Song a = xnbContent.Load<Song>("one_two_three");
    ContentManager cnbContent(nullptr, scratch.Path().string());
    const Song b = cnbContent.Load<Song>("one_two_three");
    EXPECT_EQ(b.getNameProperty(), a.getNameProperty());
    EXPECT_EQ(b.getDurationProperty(), a.getDurationProperty());
    EXPECT_EQ(std::filesystem::path(b.getHandle()).filename(),
              std::filesystem::path(a.getHandle()).filename());
}

TEST(XnbContentPipelineTest, VideoUsesFnaObjectReferencesAndPreservesNativeMetadata)
{
    ScratchDirectory scratch("video");
    WriteBytes(scratch.Path() / "clip.xnb", MakeVideoXnb("clip.wmv"));
    WriteBytes(scratch.Path() / "clip.ogv", {1u, 2u, 3u, 4u});
    const Xnb::XnbCanonicalAsset canonical =
        Xnb::DecodeXnbCanonicalAsset(scratch.Path() / "clip.xnb");
    const Xnb::XnbVideoData expected = std::get<Xnb::XnbVideoData>(canonical.value);
    const Pipeline::ContentBuildResult built = Build(scratch.Path(), "clip.xnb", "clip");
    const Cnb::CnbVideoData actual = Cnb::DecodeVideoFromCnb(ParseOutput(built));
    EXPECT_EQ(actual.streamReference, "clip.ogv");
    EXPECT_EQ(actual.durationMs, static_cast<std::uint32_t>(expected.durationMs));
    EXPECT_EQ(actual.width, static_cast<std::uint32_t>(expected.width));
    EXPECT_EQ(actual.height, static_cast<std::uint32_t>(expected.height));
    EXPECT_FLOAT_EQ(actual.framesPerSecond, expected.framesPerSecond);
    EXPECT_EQ(actual.soundtrackType, static_cast<std::uint32_t>(expected.soundtrackType));
    ASSERT_EQ(built.dependencies.size(), 2u);
    ASSERT_EQ(built.runtimeReferences.size(), 1u);

    const std::filesystem::path nativeRoot = scratch.Path() / "native";
    WriteBytes(nativeRoot / "clip.cnb", built.output.bytes);
    WriteBytes(nativeRoot / "clip.ogv", {1u, 2u, 3u, 4u});
    Xnb::RegisterAllBuiltInXnbReaders();
    GraphicsDevice device;
    ContentManager xnbContent(nullptr, scratch.Path().string());
    xnbContent.setGraphicsDevice(device);
    const Video a = xnbContent.Load<Video>("clip");
    ContentManager cnbContent(nullptr, nativeRoot.string());
    cnbContent.setGraphicsDevice(device);
    const Video b = cnbContent.Load<Video>("clip");
    EXPECT_EQ(b.getDurationProperty(), a.getDurationProperty());
    EXPECT_EQ(b.getWidthProperty(), a.getWidthProperty());
    EXPECT_EQ(b.getHeightProperty(), a.getHeightProperty());
    EXPECT_FLOAT_EQ(b.getFramesPerSecondProperty(), a.getFramesPerSecondProperty());
    EXPECT_EQ(b.getVideoSoundtrackTypeProperty(), a.getVideoSoundtrackTypeProperty());
    EXPECT_EQ(std::filesystem::path(b.getFileNameProperty()).filename(),
              std::filesystem::path(a.getFileNameProperty()).filename());
}

TEST(XnbContentPipelineTest, ModelSharedGraphAndCustomRootFailWithReaderIdentity)
{
    const std::filesystem::path model = FindXnbFixture(
        "monogame/windows/uncompressed/BlenderDefaultCube.xnb");
    ASSERT_FALSE(model.empty());
    try
    {
        static_cast<void>(BuildFixture(model, "Models/cube"));
        FAIL() << "Model XNB should be rejected until its shared GPU/effect graph is representable";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_NE(std::string(error.what()).find("Microsoft.Xna.Framework.Content.ModelReader"),
                  std::string::npos);
        EXPECT_NE(std::string(error.what()).find("3 shared resource"), std::string::npos);
    }

    ScratchDirectory scratch("custom");
    WriteBytes(
        scratch.Path() / "level.xnb",
        MakeXnb({"Game.Content.MyCustomLevelReader"}, {}));
    try
    {
        static_cast<void>(Build(scratch.Path(), "level.xnb", "Legacy/level"));
        FAIL() << "custom reader should not be partially transcoded";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_NE(std::string(error.what()).find("Game.Content.MyCustomLevelReader"),
                  std::string::npos);
        EXPECT_NE(std::string(error.what()).find("not supported for native CNB transcoding"),
                  std::string::npos);
    }
}

TEST(XnbContentPipelineTest, TruncationAndExternalMediaEscapeFailBeforePublication)
{
    ScratchDirectory scratch("negative");
    const std::filesystem::path white = FindXnbFixture(
        "monogame/windows/uncompressed/white-1.xnb");
    ASSERT_FALSE(white.empty());
    std::vector<std::uint8_t> truncated = ReadBytes(white);
    truncated.pop_back();
    WriteBytes(scratch.Path() / "truncated.xnb", truncated);
    EXPECT_THROW(
        static_cast<void>(Build(scratch.Path(), "truncated.xnb", "bad")),
        Pipeline::ContentPipelineError);

    WriteBytes(scratch.Path() / "escape.xnb", MakeVideoXnb("../outside.wmv"));
    try
    {
        static_cast<void>(Build(scratch.Path(), "escape.xnb", "escape"));
        FAIL() << "external media path escape should fail";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_NE(std::string(error.what()).find("outside the source root"),
                  std::string::npos);
    }

    for (const auto& [name, embedded] :
         std::array<std::pair<const char*, const char*>, 2>{
             std::pair{"backslash_escape", "..\\outside.wmv"},
             std::pair{"windows_absolute", "C:\\outside.wmv"}})
    {
        WriteBytes(scratch.Path() / (std::string(name) + ".xnb"), MakeVideoXnb(embedded));
        EXPECT_THROW(
            static_cast<void>(Build(
                scratch.Path(), std::string(name) + ".xnb", name)),
            Pipeline::ContentPipelineError);
    }

    const std::filesystem::path sourceRoot = scratch.Path() / "symlink_source";
    const std::filesystem::path outsideMedia = scratch.Path() / "outside.ogv";
    WriteBytes(outsideMedia, {1u});
    std::filesystem::create_directories(sourceRoot);
    std::error_code symlinkError;
    std::filesystem::create_symlink(
        outsideMedia, sourceRoot / "linked.ogv", symlinkError);
    if (!symlinkError)
    {
        WriteBytes(sourceRoot / "linked.xnb", MakeVideoXnb("linked.wmv"));
        EXPECT_THROW(
            static_cast<void>(Build(sourceRoot, "linked.xnb", "linked")),
            Pipeline::ContentPipelineError);
    }
}
