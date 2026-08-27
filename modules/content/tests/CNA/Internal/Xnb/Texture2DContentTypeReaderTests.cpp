// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-23/24: unit tests for Texture2DReader's own behavior (registration, and the
// unsupported-SurfaceFormat error path) -- see ContentManagerTexture2DXnbTests.cpp for the
// full end-to-end milestone test through ContentManager.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte2;
using Microsoft::Xna::Framework::Graphics::PackedVector::NormalizedByte4;
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

TEST_F(Texture2DContentTypeReaderTest, UnsupportedSurfaceFormatThrowsContentLoadException)
{
    ContentManager cm; // no root directory access needed for this direct-reader test
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)1);  // SurfaceFormat.Bgr565 -- not yet implemented
    writer.Write((int32_t)4);  // width
    writer.Write((int32_t)4);  // height
    writer.Write((int32_t)1);  // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// A NormalizedByte2 texture is TWO bytes per texel, not four. Found by porting Microsoft's
// DistortionSample, whose own content pipeline extension ends DisplacementMapProcessor with
// ConvertBitmapType(PixelBitmapContent<NormalizedByte2>): a 2D displacement map has an X and a Y
// and nothing else. The reader used to size both its decoded-byte bound and its per-level check
// at a fixed four bytes per texel, so this file could not be read at all.
TEST_F(Texture2DContentTypeReaderTest, NormalizedByte2ReadsTwoBytesPerTexel)
{
    using namespace CNA::Testing::Renderers;
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(OpenGLES3, OpenGL33, WebGL2, Skia);

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
    // before it validates the per-level byte count, so on a renderer that cannot create a
    // NormalizedByte2 texture at all (e.g. WebGPU) construction throws a runtime_error first and the
    // expected ContentLoadException is never reached. The malformed-level rejection this test pins
    // is renderer-independent in intent, but only observable where the format is supported.
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(OpenGLES3, OpenGL33, WebGL2, Skia);

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
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(OpenGLES3, OpenGL33, WebGL2, Skia);

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

// plans/plan_xnb.md XNB-43/47: found via a whole-container fuzz test that mutated a real .xnb's own
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

TEST_F(Texture2DContentTypeReaderTest, NegativeWidthThrowsContentLoadException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);  // SurfaceFormat.Color
    writer.Write((int32_t)-1); // width
    writer.Write((int32_t)4);  // height
    writer.Write((int32_t)1);  // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture2DContentTypeReaderTest, ZeroWidthThrowsContentLoadException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);  // SurfaceFormat.Color
    writer.Write((int32_t)0);  // width -- zero, not just negative
    writer.Write((int32_t)4);  // height
    writer.Write((int32_t)1);  // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture2DContentTypeReaderTest, ZeroHeightThrowsContentLoadException)
{
    ContentManager cm;
    cm.setGraphicsDevice(gd);

    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write((int32_t)0);  // SurfaceFormat.Color
    writer.Write((int32_t)4);  // width
    writer.Write((int32_t)0);  // height -- zero, not just negative
    writer.Write((int32_t)1);  // levelCount
    writer.Flush();
    auto buf = ms.ToArray();

    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    ContentReader reader(&cm, &ms2, "test", 5, 'w');

    auto typeReader = ContentTypeReaderManager::CreateReader("Microsoft.Xna.Framework.Content.Texture2DReader");
    ASSERT_NE(typeReader, nullptr);

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

TEST_F(Texture2DContentTypeReaderTest, AbsurdlyLargeDimensionsThrowContentLoadExceptionNotBadAlloc)
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

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// REMED-CONTENT-001: reproduces the fuzz-discovered shape exactly -- one axis huge enough to
// exceed any real device's maximum texture dimension, but with the OTHER axis small enough that
// the width*height*4 product stays comfortably under CheckDecodedByteSize's own 256MB ceiling
// (confirmed: 500000*1*4 = 2,000,000 bytes). Before this task's fix, this shape reached
// Texture2D's renderer-specific construction unchecked; the byte-size check alone never caught it.
TEST_F(Texture2DContentTypeReaderTest, SingleAxisExceedingMaxTextureDimensionThrowsContentLoadException)
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

    EXPECT_THROW(typeReader->ReadUntyped(reader, std::any{}), ContentLoadException);
}

// REMED-CONTENT-001: reproduces the fuzz-discovered "mipLevels=25 against a 15-level maximum"
// shape -- a declared levelCount exceeding what a real width x height texture can ever have. Before
// this task's fix, the read loop below would call Texture2D::SetData() for mip levels the actual
// constructed texture never allocated, the confirmed root cause of the Vulkan/WebGPU crashes.
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
