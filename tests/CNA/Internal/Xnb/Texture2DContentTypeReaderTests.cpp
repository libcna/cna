// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-23/24: unit tests for Texture2DReader's own behavior (registration, and the
// unsupported-SurfaceFormat error path) -- see ContentManagerTexture2DXnbTests.cpp for the
// full end-to-end milestone test through ContentManager.

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
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
    EXPECT_TRUE(ContentTypeReaderManager::IsRegistered("Microsoft.Xna.Framework.Content.Texture2DReader"));
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

// plan_xnb.md XNB-43/47: found via a whole-container fuzz test that mutated a real .xnb's own
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
