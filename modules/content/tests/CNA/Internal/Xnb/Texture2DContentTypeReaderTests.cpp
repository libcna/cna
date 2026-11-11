// SPDX-License-Identifier: MS-PL
//
// XNB-23/24: unit tests for Texture2DReader's own behavior (registration, and the
// unsupported-SurfaceFormat error path) -- see ContentManagerTexture2DXnbTests.cpp for the
// full end-to-end milestone test through ContentManager.

#include <array>
#include <utility>
#include <iterator>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <stdexcept>
#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte2;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte4;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class Texture2DContentTypeReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTexture2DXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        GraphicsDevice gd;
    };

    Texture2D ReadHandConstructedTexture2D(
        GraphicsDevice& device, SurfaceFormat format, int32_t width, int32_t height,
        const std::vector<std::vector<std::uint8_t>>& levels)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(static_cast<int32_t>(format));
        writer.Write(width);
        writer.Write(height);
        writer.Write(static_cast<int32_t>(levels.size()));
        for (const auto& bytes : levels)
        {
            writer.Write(static_cast<int32_t>(bytes.size()));
            for (const std::uint8_t value : bytes) writer.Write(value);
        }
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture2DReader");
        if (!typeReader) return {};
        return std::any_cast<Texture2D>(typeReader->ReadUntyped(reader, std::any{}));
    }
}

namespace
{
    /// Reads a Texture2D body holding only the four header fields and returns what it threw.
    std::exception_ptr ReadTexture2DHeaderOnly(GraphicsDevice& device, const int32_t format,
                                               const int32_t width, const int32_t height,
                                               const int32_t levelCount)
    {
        System::IO::MemoryStream stream;
        System::IO::BinaryWriter writer(&stream, true);
        writer.Write(format);
        writer.Write(width);
        writer.Write(height);
        writer.Write(levelCount);
        writer.Flush();
        const auto serialized = stream.ToArray();

        ContentManager content;
        content.setGraphicsDevice(device);
        System::IO::MemoryStream body(serialized.data(), static_cast<int32_t>(serialized.size()));
        ContentReader reader(&content, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader(
            "Microsoft.Xna.Framework.Content.Texture2DReader");
        try
        {
            (void)typeReader->ReadUntyped(reader, std::any{});
        }
        catch (...)
        {
            return std::current_exception();
        }
        return nullptr;
    }

    /// Expects Refresh's ArgumentOutOfRangeException for @p parameter, with its resource text.
    void ExpectSizeRefusal(const std::exception_ptr& thrown, const std::string& parameter)
    {
        ASSERT_NE(thrown, nullptr);
        try
        {
            std::rethrow_exception(thrown);
        }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(error.getParamNameProperty(), parameter);
            EXPECT_EQ(error.getMessageProperty().rfind("Resource size must be greater than zero.", 0), 0u)
                << error.getMessageProperty();
        }
        catch (const std::exception& other)
        {
            ADD_FAILURE() << "expected ArgumentOutOfRangeException, got: " << other.what();
        }
    }
}

TEST_F(Texture2DContentTypeReaderTest, IsRegisteredUnderRealFnaCanonicalName)
{
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.TextureReader"));
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture2DReader"));
}

TEST_F(Texture2DContentTypeReaderTest, BaseTextureReaderReturnsItsExistingInstanceWithoutReadingData)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream stream;
    ContentReader reader(&cm, &stream, "test", 5, 'w');
    Texture2D texture(gd, 1, 1);
    Texture* expected = &texture;

    auto typeReader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.TextureReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_EQ(std::any_cast<Texture*>(typeReader->ReadUntyped(reader, std::any(expected))), expected);
    EXPECT_EQ(std::any_cast<Texture*>(typeReader->ReadUntyped(reader, std::any{})), nullptr);
}

// HND-TQ-16: Refresh's Texture2DReader hands the format to the Texture2D constructor, whose
// ValidateCreationParameters (IL_0034-005d) refuses it with NotSupportedException
// (ProfileCapabilities.ThrowNotSupportedException), which ReadAsset does not wrap.
TEST_F(Texture2DContentTypeReaderTest, UnsupportedSurfaceFormatThrowsNotSupportedException)
{
    ContentManager cm; // no root directory access needed for this direct-reader test
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)999);  // not a SurfaceFormat value
    writer.Write((int32_t)4);  // width
    writer.Write((int32_t)4);  // height
    writer.Write((int32_t)1);  // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
}

TEST_F(Texture2DContentTypeReaderTest, CnaExtensionFormatInAVersion5ContainerIsRefusedLikeAnyNonXnaFormat)
{
    // TQ-018 (found by cna_xnb_reader_fuzzer): ordinal 20 is CNA's ColorBgraEXT, which only a
    // version-4 container's legacy format 1 maps to. A version-5 container names XNA 4.0 ordinals
    // (0..19) only -- CNA's writer refuses to put ColorBgraEXT there -- and XNA's
    // Texture2D.ValidateCreationParameters (IL_0034-005d) refuses any format outside the profile's
    // list. It used to pass the reader and reach the renderer, which on SOFTWARE threw
    // System::InvalidOperationException out of Load<T>. Ordinal 21, ColorSrgbEXT, is the control:
    // a CNA extension format the reader already refused; both must refuse the same way.
    for (const int ordinal : {20, 21})
    {
        SCOPED_TRACE(ordinal);
        EXPECT_THROW((void)ReadHandConstructedTexture2D(
                         gd, static_cast<SurfaceFormat>(ordinal), 1, 1, {{0x10, 0x20, 0x30, 0x40}}),
                     System::NotSupportedException);
    }
}

// SOFTWARE-275: FNA's Texture2DReader retains every uncompressed XNA SurfaceFormat and uploads
// the serialized level bytes unchanged. CNA's former allow-list rejected everything here except
// Color and the two signed-normalized formats even though Software and EasyGL already store all
// seventeen representations exactly.
TEST_F(Texture2DContentTypeReaderTest,
       PreservesEveryClassicUncompressedFormatAndExactBytes)
{
    using namespace CNA::Testing::Renderers;
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(800, 480)};
    constexpr std::array<SurfaceFormat, 17> formats{{
        SurfaceFormat::Color,
        SurfaceFormat::Bgr565,
        SurfaceFormat::Bgra5551,
        SurfaceFormat::Bgra4444,
        SurfaceFormat::NormalizedByte2,
        SurfaceFormat::NormalizedByte4,
        SurfaceFormat::Rgba1010102,
        SurfaceFormat::Rg32,
        SurfaceFormat::Rgba64,
        SurfaceFormat::Alpha8,
        SurfaceFormat::Single,
        SurfaceFormat::Vector2,
        SurfaceFormat::Vector4,
        SurfaceFormat::HalfSingle,
        SurfaceFormat::HalfVector2,
        SurfaceFormat::HalfVector4,
        SurfaceFormat::HdrBlendable,
    }};

    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount =
            static_cast<std::size_t>(Texture::GetFormatSizeEXT(format)) * 2u;
        std::vector<std::uint8_t> expected(byteCount);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            expected[index] = static_cast<std::uint8_t>(
                0x31u + index * 19u + static_cast<std::size_t>(format));
        }

        Texture2D texture = ReadHandConstructedTexture2D(
            device, format, 2, 1, {expected});
        EXPECT_EQ(texture.getFormatProperty(), format);
        EXPECT_EQ(texture.getLevelCountProperty(), 1);

        std::vector<std::uint8_t> actual(expected.size(), 0xCDu);
        texture.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);
    }
}

TEST_F(Texture2DContentTypeReaderTest,
       PreservesEveryClassicCompressedFormatAndExactBlocks)
{
    using namespace CNA::Testing::Renderers;
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(800, 480)};
    constexpr std::array<SurfaceFormat, 3> formats{{
        SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
    }};
    for (const SurfaceFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));
        const std::size_t byteCount =
            static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
        std::vector<std::uint8_t> expected(byteCount);
        for (std::size_t index = 0; index < expected.size(); ++index)
            expected[index] = static_cast<std::uint8_t>(0x17u + index * 13u);

        Texture2D texture = ReadHandConstructedTexture2D(
            device, format, 4, 4, {expected});
        EXPECT_EQ(texture.getFormatProperty(), format);
        std::vector<std::uint8_t> actual(byteCount, 0xCDu);
        texture.GetData(actual.data(), static_cast<int>(actual.size()));
        EXPECT_EQ(actual, expected);
    }
}

// Compressed content a renderer keeps compressed lives in that renderer's block store, so the
// loaded texture -- a whole mip chain, as content usually has -- holds no level-0 CPU shadow.
TEST_F(Texture2DContentTypeReaderTest, CompressedContentKeptCompressedHoldsNoLevelZeroCpuShadow)
{
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(800, 480)};
    int loaded = 0;
    for (const SurfaceFormat format : {SurfaceFormat::Dxt1, SurfaceFormat::Dxt3,
                                       SurfaceFormat::Dxt5})
    {
        SCOPED_TRACE(static_cast<int>(format));
        if (!device.GetRenderer().LoadsCompressedContentNativelyEXT() ||
            !device.GetRenderer().IsCompressedTransferFormatEXT(static_cast<int>(format)))
            continue;
        ++loaded;

        // 8x8 is two by two blocks; 4x4, 2x2 and 1x1 are one block each.
        const auto blockBytes = static_cast<std::size_t>(Texture::GetFormatSizeEXT(format));
        std::vector<std::vector<std::uint8_t>> levels;
        for (const std::size_t blocks : {4u, 1u, 1u, 1u})
        {
            std::vector<std::uint8_t> level(blocks * blockBytes);
            for (std::size_t index = 0; index < level.size(); ++index)
                level[index] = static_cast<std::uint8_t>(0x29u + levels.size() * 7u + index * 11u);
            levels.push_back(std::move(level));
        }

        Texture2D texture = ReadHandConstructedTexture2D(device, format, 8, 8, levels);
        ASSERT_EQ(texture.getFormatProperty(), format);
        ASSERT_EQ(texture.getLevelCountProperty(), 4);
        EXPECT_TRUE(texture.GetCpuPixelsWeak().expired());

        std::vector<std::uint8_t> level0(levels[0].size(), 0xCDu);
        texture.GetData(level0.data(), static_cast<int>(level0.size()));
        EXPECT_EQ(level0, levels[0]);
        std::vector<std::uint8_t> level1(levels[1].size(), 0xCDu);
        texture.GetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));
        EXPECT_EQ(level1, levels[1]);
    }
    if (loaded == 0)
        GTEST_SKIP() << "the active renderer decodes compressed content to Color";
}

// A NormalizedByte2 texture is TWO bytes per texel, not four. Found by porting Microsoft's
// DistortionSample, whose own content pipeline extension ends DisplacementMapProcessor with
// ConvertBitmapType(PixelBitmapContent<NormalizedByte2>): a 2D displacement map has an X and a Y
// and nothing else. The reader used to size both its decoded-byte bound and its per-level check
// at a fixed four bytes per texel, so this file could not be read at all.
TEST_F(Texture2DContentTypeReaderTest, NormalizedByte2ReadsTwoBytesPerTexel)
{
    using namespace CNA::Testing::Renderers;
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    ContentManager cm;
    cm.setGraphicsDevice(gd);

    constexpr uint16_t leftPacked = 0x4081u;
    constexpr uint16_t rightPacked = 0x7FC0u;
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::NormalizedByte2));
    writer.Write((int32_t)2);
    writer.Write((int32_t)1);
    writer.Write((int32_t)1);
    writer.Write((int32_t)4);  // two texels, two bytes each
    for (const uint16_t packed : {leftPacked, rightPacked})
    {
        writer.Write(static_cast<uint8_t>(packed));
        writer.Write(static_cast<uint8_t>(packed >> 8u));
    }
    writer.Flush();
    const auto bytes = ms.ToArray();

    System::IO::MemoryStream input(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(&cm, &input, "normalized-byte2", 5, 'w');
    auto typeReader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    Texture2D texture = std::any_cast<Texture2D>(
        typeReader->ReadUntyped(reader, std::any{}));
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::NormalizedByte2);
    EXPECT_EQ(texture.getWidthProperty(), 2);
    EXPECT_EQ(texture.getHeightProperty(), 1);

    NormalizedByte2 actual[2];
    texture.GetData(actual, 2);
    EXPECT_EQ(actual[0].getPackedValueProperty(), leftPacked);
    EXPECT_EQ(actual[1].getPackedValueProperty(), rightPacked);
    // 0x81 is -127 as a signed byte, so the first texel's X is negative and its Y positive.
    EXPECT_LT(actual[0].ToVector4().X, 0.0f);
    EXPECT_GT(actual[0].ToVector4().Y, 0.0f);
}

// The per-level byte count has to follow the format too: a NormalizedByte2 level carrying four
// bytes per texel is a truncated or mislabelled file, not a wider texture.
TEST_F(Texture2DContentTypeReaderTest, NormalizedByte2RejectsAFourBytePerTexelLevel)
{
    using namespace CNA::Testing::Renderers;
    // Same guard as NormalizedByte2ReadsTwoBytesPerTexel above: the reader constructs the texture
    // before it validates the per-level byte count, so the malformed-level rejection is observable
    // only on renderers that can create a NormalizedByte2 texture.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::NormalizedByte2));
    writer.Write((int32_t)2);
    writer.Write((int32_t)1);
    writer.Write((int32_t)1);
    writer.Write((int32_t)8);  // four bytes per texel, which this format is not
    for (int i = 0; i < 8; ++i) writer.Write(static_cast<uint8_t>(i));
    writer.Flush();
    const auto bytes = ms.ToArray();

    System::IO::MemoryStream input(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(&cm, &input, "normalized-byte2-wrong-stride", 5, 'w');
    auto typeReader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture2DContentTypeReaderTest, NormalizedByte4PreservesSignedPackedTexels)
{
    using namespace CNA::Testing::Renderers;
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGLES3, OpenGL33, WebGL2);

    ContentManager cm;
    cm.setGraphicsDevice(gd);

    constexpr uint32_t leftPacked = 0x7F004081u;
    constexpr uint32_t rightPacked = 0xC07F00C0u;
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write(static_cast<int32_t>(SurfaceFormat::NormalizedByte4));
    writer.Write((int32_t)2);
    writer.Write((int32_t)1);
    writer.Write((int32_t)1);
    writer.Write((int32_t)8);
    for (const uint32_t packed : {leftPacked, rightPacked})
    {
        writer.Write(static_cast<uint8_t>(packed));
        writer.Write(static_cast<uint8_t>(packed >> 8u));
        writer.Write(static_cast<uint8_t>(packed >> 16u));
        writer.Write(static_cast<uint8_t>(packed >> 24u));
    }
    writer.Flush();
    const auto bytes = ms.ToArray();

    System::IO::MemoryStream input(bytes.data(), static_cast<int32_t>(bytes.size()));
    ContentReader reader(&cm, &input, "normalized-byte4", 5, 'w');
    auto typeReader = ContentTypeReaderManager::CreateReader(
        "Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    Texture2D texture = std::any_cast<Texture2D>(
        typeReader->ReadUntyped(reader, std::any{}));
    EXPECT_EQ(texture.getFormatProperty(), SurfaceFormat::NormalizedByte4);
    EXPECT_EQ(texture.getWidthProperty(), 2);
    EXPECT_EQ(texture.getHeightProperty(), 1);

    NormalizedByte4 actual[2];
    texture.GetData(actual, 2);
    EXPECT_EQ(actual[0].getPackedValueProperty(), leftPacked);
    EXPECT_EQ(actual[1].getPackedValueProperty(), rightPacked);
    EXPECT_LT(actual[0].ToVector4().X, 0.0f);
    EXPECT_GT(actual[0].ToVector4().W, 0.0f);
}

// XNB-43/47: found via a whole-container fuzz test that mutated a real .xnb's own
// declared byteCount field independently of width/height -- confirmed as a real
// heap-buffer-overflow under -DCNA_SANITIZE=address,undefined (the pixel-unpack loop indexed into
// `bytes` using width*height*4, not the byte count actually available).

TEST_F(Texture2DContentTypeReaderTest, ByteCountMismatchedWithWidthHeightThrowsContentLoadException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);  // SurfaceFormat.Color
    writer.Write((int32_t)4);  // width
    writer.Write((int32_t)4);  // height
    writer.Write((int32_t)1);  // levelCount
    writer.Write((int32_t)4);  // byteCount -- should be 4*4*4=64, not 4
    writer.Write((uint8_t)0); writer.Write((uint8_t)0); writer.Write((uint8_t)0); writer.Write((uint8_t)0);
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// KI-CONTENT-TEXDIM: Refresh's Texture2DReader.Read reads all four header fields (IL_0000-001e)
// and constructs the texture (IL_002f), whose Texture2D.ValidateCreationParameters refuses a width
// (IL_0000-0013) or height (IL_0014-0027) of zero or less with ArgumentOutOfRangeException and the
// ResourcesMustBeGreaterThanZeroSize text, which ContentReader.ReadAsset does not wrap.
TEST_F(Texture2DContentTypeReaderTest, NonPositiveWidthOrHeightThrowsArgumentOutOfRangeException)
{
    const int32_t color = static_cast<int32_t>(SurfaceFormat::Color);
    for (const int32_t value : {0, -1, std::numeric_limits<int32_t>::min()})
    {
        SCOPED_TRACE(value);
        ExpectSizeRefusal(ReadTexture2DHeaderOnly(gd, color, value, 4, 1), "width");
        ExpectSizeRefusal(ReadTexture2DHeaderOnly(gd, color, 4, value, 1), "height");
    }
    // Width is checked first (IL_0000), then height (IL_0014).
    ExpectSizeRefusal(ReadTexture2DHeaderOnly(gd, color, 0, 0, 1), "width");
}

TEST_F(Texture2DContentTypeReaderTest, TheSizeIsRefusedBeforeTheFormatAndAfterTheWholeHeaderIsRead)
{
    // ValidateCreationParameters checks the size (IL_0000-0027) before the profile's format list
    // (IL_0034-005d), and Texture2DReader.Read reads every header field before constructing.
    ExpectSizeRefusal(ReadTexture2DHeaderOnly(gd, 999, 0, 4, 1), "width");
    ExpectSizeRefusal(ReadTexture2DHeaderOnly(gd, 999, 4, -2, 1), "height");
    // A positive size leaves the format refusal in place.
    EXPECT_THROW(std::rethrow_exception(ReadTexture2DHeaderOnly(gd, 999, 4, 4, 1)),
                 System::NotSupportedException);

    // A header that ends after its format field ends the read before any validation.
    ContentManager cm;
    cm.setGraphicsDevice(gd);
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)999);
    writer.Flush();
    const auto buf = ms.ToArray();
    System::IO::MemoryStream body(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &body, "test", 5, 'w');
    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);
    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::IO::EndOfStreamException);
}

namespace
{
    /// Serves one uncompressed Texture2D .xnb whose header declares @p width x 4.
    class ZeroWidthTextureManager final : public ContentManager
    {
    public:
        ZeroWidthTextureManager(GraphicsDevice& device, const bool strict, const int32_t width)
        {
            setGraphicsDevice(device);
            RouteLoadsThroughOpenStreamEXT(strict);
            System::IO::MemoryStream stream;
            System::IO::BinaryWriter writer(&stream, true);
            writer.Write7BitEncodedInt(1);
            writer.Write(std::string("Microsoft.Xna.Framework.Content.Texture2DReader"));
            writer.Write(int32_t{0});
            writer.Write7BitEncodedInt(0);
            writer.Write7BitEncodedInt(1);
            writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
            writer.Write(width);
            writer.Write(int32_t{4});
            writer.Write(int32_t{1});
            writer.Flush();
            const auto body = stream.ToArray();
            file_ = {'X', 'N', 'B', 'w', 5, 0, 0, 0, 0, 0};
            file_.insert(file_.end(), body.begin(), body.end());
            const auto total = static_cast<uint32_t>(file_.size());
            for (int i = 0; i < 4; ++i) { file_[6 + i] = static_cast<uint8_t>(total >> (8 * i)); }
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            return std::make_unique<System::IO::MemoryStream>(
                file_.data(), static_cast<int32_t>(file_.size()), false);
        }

    private:
        std::vector<uint8_t> file_;
    };
}

TEST_F(Texture2DContentTypeReaderTest, LoadReportsANonPositiveSizeUnwrapped)
{
    // ContentReader.ReadAsset<T> IL_0019 catches only IOException; Load<T> has no handler.
    for (const bool strict : {false, true})
    {
        SCOPED_TRACE(strict);
        ZeroWidthTextureManager manager(gd, strict, 0);
        std::exception_ptr thrown;
        try { (void)manager.Load<Texture2D>("texture"); }
        catch (...) { thrown = std::current_exception(); }
        ExpectSizeRefusal(thrown, "width");
    }
}

// KI-CONTENT-OVERSIZE: Refresh's Texture2D.ValidateCreationParameters refuses a width or height
// past the profile's MaxTextureSize with ProfileCapabilities.ThrowNotSupportedException
// (IL_0062-0090), in the constructor Texture2DReader.Read calls before reading any level
// (IL_001f-002f). These two used to expect CNA's own ContentLoadException.
TEST_F(Texture2DContentTypeReaderTest, AbsurdlyLargeDimensionsThrowNotSupportedExceptionNotBadAlloc)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);           // SurfaceFormat.Color
    writer.Write((int32_t)0x7FFFFFFF);  // width -- adversarially huge
    writer.Write((int32_t)0x7FFFFFFF);  // height -- adversarially huge
    writer.Write((int32_t)1);           // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
}

// Reproduces the fuzz-discovered shape exactly -- one axis huge enough to
// exceed any real device's maximum texture dimension, but with the OTHER axis small enough that
// the width*height*4 product stays comfortably under CheckDecodedByteSize's own 256MB ceiling
// (confirmed: 500000*1*4 = 2,000,000 bytes). Before this task's fix, this shape reached
// Texture2D's renderer-specific construction unchecked; the byte-size check alone never caught it.
TEST_F(Texture2DContentTypeReaderTest, SingleAxisExceedingMaxTextureDimensionThrowsNotSupportedException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);       // SurfaceFormat.Color
    writer.Write((int32_t)500000);  // width -- exceeds any real device's max texture dimension
    writer.Write((int32_t)1);       // height -- small, so width*height*4 stays well under the byte-size cap
    writer.Write((int32_t)1);       // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), System::NotSupportedException);
}

TEST_F(Texture2DContentTypeReaderTest, AnOversizedTextureIsRefusedBeforeItsLevelsAreRead)
{
    // One texel past the profile's MaxTextureSize, on either axis, with no level bytes at all:
    // the constructor refuses it before the reader asks for them.
    const int32_t maximum = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    for (const auto& [width, height] : {std::pair<int32_t, int32_t>{maximum + 1, 1},
                                        std::pair<int32_t, int32_t>{1, maximum + 1}})
    {
        SCOPED_TRACE(width);
        const std::exception_ptr thrown =
            ReadTexture2DHeaderOnly(gd, static_cast<int32_t>(SurfaceFormat::Color), width, height, 1);
        ASSERT_NE(thrown, nullptr);
        try
        {
            std::rethrow_exception(thrown);
        }
        catch (const System::NotSupportedException& error)
        {
            EXPECT_NE(error.getMessageProperty().find("maximum Texture2D size of " + std::to_string(maximum)),
                      std::string::npos) << error.getMessageProperty();
        }
        catch (const std::exception& other)
        {
            ADD_FAILURE() << "expected NotSupportedException, got: " << other.what();
        }
    }
    // The limit itself is a size the profile allows.
    const std::exception_ptr atLimit =
        ReadTexture2DHeaderOnly(gd, static_cast<int32_t>(SurfaceFormat::Color), maximum, 1, 1);
    ASSERT_NE(atLimit, nullptr);
    EXPECT_THROW(std::rethrow_exception(atLimit), System::IO::EndOfStreamException);
}

TEST_F(Texture2DContentTypeReaderTest, LoadReportsAnOversizedTextureAsNotSupported)
{
    const int32_t maximum = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    for (const bool strict : {false, true})
    {
        SCOPED_TRACE(strict);
        ZeroWidthTextureManager manager(gd, strict, maximum + 1);
        EXPECT_THROW((void)manager.Load<Texture2D>("texture"), System::NotSupportedException);
    }
}

TEST_F(Texture2DContentTypeReaderTest, AnOversizedSpriteFontAtlasIsRefusedAsATexture2D)
{
    // Refresh's SpriteFontReader reads its atlas through Texture2DReader, so the same refusal.
    CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
    std::ifstream in("tests/assets/xnb/cna/windows/uncompressed/spritefont_two_glyphs.xnb", std::ios::binary);
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(file.empty());
    // The atlas header: Color, 8 x 8, one level.
    const std::vector<uint8_t> header{0, 0, 0, 0, 8, 0, 0, 0, 8, 0, 0, 0, 1, 0, 0, 0};
    const auto at = std::search(file.begin(), file.end(), header.begin(), header.end());
    ASSERT_NE(at, file.end());
    const int32_t maximum = gd.GetRenderer().GetMaxTextureSizeForProfileEXT(
        static_cast<int>(gd.getGraphicsProfileProperty()));
    const int32_t width = maximum + 1;
    std::memcpy(&*(at + 4), &width, sizeof(width));

    class FontManager final : public ContentManager
    {
    public:
        explicit FontManager(std::vector<uint8_t> bytes) : bytes_(std::move(bytes))
        {
            RouteLoadsThroughOpenStreamEXT(true);
        }

    protected:
        std::unique_ptr<System::IO::Stream> OpenStream(const std::string&) override
        {
            return std::make_unique<System::IO::MemoryStream>(
                bytes_.data(), static_cast<int32_t>(bytes_.size()), false);
        }

    private:
        std::vector<uint8_t> bytes_;
    };
    FontManager manager(file);
    manager.setGraphicsDevice(gd);
    EXPECT_THROW((void)manager.Load<Microsoft::Xna::Framework::Graphics::SpriteFont>("font"),
                 System::NotSupportedException);
}

// Reproduces the fuzz-discovered "mipLevels=25 against a 15-level maximum"
// shape -- a declared levelCount exceeding what a real width x height texture can ever have. Before
// this task's fix, the read loop below would call Texture2D::SetData() for mip levels the actual
// constructed texture never allocated, the confirmed root cause of the Vulkan/WebGPU crashes.
// KI-CONTENT-LEVELS: Refresh's Texture2DReader.Read asks for mipmaps when levelCount > 1
// (IL_0027-002b) and then reads and sets exactly levelCount levels (IL_0035-0064). A count of zero
// or less creates the texture and reads no level; fewer levels than the full chain set only those.
TEST_F(Texture2DContentTypeReaderTest, ALevelCountOfZeroOrLessCreatesTheTextureAndReadsNoLevel)
{
    for (const int32_t levelCount : {0, -1, std::numeric_limits<int32_t>::min()})
    {
        SCOPED_TRACE(levelCount);
        ContentManager cm;
        cm.setGraphicsDevice(gd);
        System::IO::MemoryStream ms;
        System::IO::BinaryWriter writer(&ms, true);
        writer.Write(static_cast<int32_t>(SurfaceFormat::Color));
        writer.Write(int32_t{4});
        writer.Write(int32_t{4});
        writer.Write(levelCount);
        writer.Write(int32_t{0x5EED}); // not part of the texture: the next object's first field
        writer.Flush();
        const auto buf = ms.ToArray();
        System::IO::MemoryStream body(buf.data(), static_cast<int32_t>(buf.size()));
        ContentReader reader(&cm, &body, "test", 5, 'w');
        auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
        ASSERT_NE(typeReader, nullptr);

        const Texture2D texture = std::any_cast<Texture2D>(typeReader->ReadUntyped(reader, std::any{}));
        EXPECT_EQ(texture.getWidthProperty(), 4);
        EXPECT_EQ(texture.getHeightProperty(), 4);
        EXPECT_EQ(texture.getLevelCountProperty(), 1);
        EXPECT_EQ(reader.ReadInt32(), 0x5EED);
    }
}

TEST_F(Texture2DContentTypeReaderTest, APartialMipChainSetsOnlyTheLevelsItHolds)
{
    // 4x4 has a three-level chain (4, 2, 1); the file holds two levels.
    const std::vector<std::vector<std::uint8_t>> levels{
        std::vector<std::uint8_t>(4 * 4 * 4, 0x11), std::vector<std::uint8_t>(2 * 2 * 4, 0x22)};
    Texture2D texture = ReadHandConstructedTexture2D(gd, SurfaceFormat::Color, 4, 4, levels);
    ASSERT_EQ(texture.getLevelCountProperty(), 3);
    std::vector<std::uint8_t> level0(levels[0].size(), 0xCD);
    texture.GetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));
    EXPECT_EQ(level0, levels[0]);
    std::vector<std::uint8_t> level1(levels[1].size(), 0xCD);
    texture.GetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));
    EXPECT_EQ(level1, levels[1]);
}

TEST_F(Texture2DContentTypeReaderTest, MipLevelCountExceedingCeilingThrowsContentLoadException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);   // SurfaceFormat.Color
    writer.Write((int32_t)4);   // width
    writer.Write((int32_t)4);   // height -- a real 4x4 texture has at most 3 mip levels (4,2,1)
    writer.Write((int32_t)25);  // levelCount -- adversarially exceeds that ceiling
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}
