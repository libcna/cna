// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-12: unit tests for ParseXnbTypeReaderTable().

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "System/IO/BinaryReader.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using CNA::Internal::Xnb::ParseXnbTypeReaderTable;
using CNA::Internal::Xnb::XnbReadLimits;
using Microsoft::Xna::Framework::Content::ContentLoadException;

TEST(XnbTypeReaderTableTest, ParsesTwoEntriesInOrder)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);

    writer.Write7BitEncodedInt(2);

    writer.Write(std::string(
        "Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics, "
        "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553"));
    writer.Write((int32_t)0);

    writer.Write(std::string(
        "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3, "
        "Microsoft.Xna.Framework, Version=4.0.0.0]], Microsoft.Xna.Framework.Graphics, "
        "Version=4.0.0.0"));
    writer.Write((int32_t)0);

    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    const auto table = ParseXnbTypeReaderTable(reader, "test.xnb");

    ASSERT_EQ(table.size(), 2u);
    EXPECT_EQ(table[0].normalizedName, "Microsoft.Xna.Framework.Content.Texture2DReader");
    EXPECT_EQ(table[0].version, 0);
    EXPECT_EQ(table[1].normalizedName,
              "Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3]]");
}

TEST(XnbTypeReaderTableTest, ZeroEntriesParsesEmptyTable)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write7BitEncodedInt(0);
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    const auto table = ParseXnbTypeReaderTable(reader, "test.xnb");

    EXPECT_TRUE(table.empty());
}

TEST(XnbTypeReaderTableTest, CountExceedingLimitThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write7BitEncodedInt(10); // exceeds a deliberately tiny limit below
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    XnbReadLimits tinyLimits;
    tinyLimits.maxTypeReaderCount = 5;

    EXPECT_THROW(ParseXnbTypeReaderTable(reader, "test.xnb", tinyLimits), ContentLoadException);
}

TEST(XnbTypeReaderTableTest, RealMonoGameFixturePreservesRawNameAlongsideNormalized)
{
    // Bytes 10..165 of MonoGame's own Tests/Assets/Textures/white-1.xnb (191-byte file):
    // the type-reader table immediately following the 10-byte container header, extracted
    // byte-for-byte from that real, externally-produced fixture (not hand-invented).
    const std::vector<uint8_t> bytes{
        0x01, 0x94, 0x01, 0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x2e, 0x58, 0x6e,
        0x61, 0x2e, 0x46, 0x72, 0x61, 0x6d, 0x65, 0x77, 0x6f, 0x72, 0x6b, 0x2e, 0x43, 0x6f, 0x6e,
        0x74, 0x65, 0x6e, 0x74, 0x2e, 0x54, 0x65, 0x78, 0x74, 0x75, 0x72, 0x65, 0x32, 0x44, 0x52,
        0x65, 0x61, 0x64, 0x65, 0x72, 0x2c, 0x20, 0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66,
        0x74, 0x2e, 0x58, 0x6e, 0x61, 0x2e, 0x46, 0x72, 0x61, 0x6d, 0x65, 0x77, 0x6f, 0x72, 0x6b,
        0x2e, 0x47, 0x72, 0x61, 0x70, 0x68, 0x69, 0x63, 0x73, 0x2c, 0x20, 0x56, 0x65, 0x72, 0x73,
        0x69, 0x6f, 0x6e, 0x3d, 0x34, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x30, 0x2c, 0x20, 0x43, 0x75,
        0x6c, 0x74, 0x75, 0x72, 0x65, 0x3d, 0x6e, 0x65, 0x75, 0x74, 0x72, 0x61, 0x6c, 0x2c, 0x20,
        0x50, 0x75, 0x62, 0x6c, 0x69, 0x63, 0x4b, 0x65, 0x79, 0x54, 0x6f, 0x6b, 0x65, 0x6e, 0x3d,
        0x38, 0x34, 0x32, 0x63, 0x66, 0x38, 0x62, 0x65, 0x31, 0x64, 0x65, 0x35, 0x30, 0x35, 0x35,
        0x33, 0x00, 0x00, 0x00, 0x00
    };

    System::IO::MemoryStream ms(bytes.data(), (int32_t)bytes.size());
    System::IO::BinaryReader reader(&ms, true);

    const auto table = ParseXnbTypeReaderTable(reader, "test.xnb");

    ASSERT_EQ(table.size(), 1u);
    EXPECT_EQ(table[0].rawName,
              "Microsoft.Xna.Framework.Content.Texture2DReader, Microsoft.Xna.Framework.Graphics, "
              "Version=4.0.0.0, Culture=neutral, PublicKeyToken=842cf8be1de50553");
    EXPECT_EQ(table[0].normalizedName, "Microsoft.Xna.Framework.Content.Texture2DReader");
    EXPECT_EQ(table[0].version, 0);
}

// plans/plan_xnb.md XNB-43: a malformed generic-argument name (XnbTypeName's own std::invalid_argument)
// must surface as this pipeline's one consistent malformed-input error type, not leak the
// lower-level exception a caller catching ContentLoadException around Load<T>() would miss.
// REMED-CONTENT-006: maxStringBytes previously had zero consumers anywhere -- only
// BinaryReader::ReadString()'s own much coarser whole-stream clamp (up to maxFileSize) applied to
// this field. Uses a deliberately tiny limit so the test itself stays fast and deterministic
// rather than constructing a real 1MB+ string.
TEST(XnbTypeReaderTableTest, RawNameExceedingMaxStringBytesThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write7BitEncodedInt(1);
    writer.Write(std::string("Microsoft.Xna.Framework.Content.Texture2DReader"));
    writer.Write((int32_t)0);
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    XnbReadLimits tinyLimits;
    tinyLimits.maxStringBytes = 10; // shorter than the real name above

    EXPECT_THROW(ParseXnbTypeReaderTable(reader, "test.xnb", tinyLimits), ContentLoadException);
}

// REMED-CONTENT-006: confirms the maxObjectNestingDepth limit propagates all the way from
// ParseXnbTypeReaderTable's own `limits` parameter through to XnbTypeName::ParseOne(), not just
// when ParseXnbTypeName() is called directly (see XnbTypeNameTests.cpp for that half).
TEST(XnbTypeReaderTableTest, NestingDepthExceedingLimitThrowsContentLoadException)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write7BitEncodedInt(1);
    // "X[[X[[X]]]]" -- 2 levels of nesting.
    writer.Write(std::string("X[[X[[X]]]]"));
    writer.Write((int32_t)0);
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    XnbReadLimits tightLimits;
    tightLimits.maxObjectNestingDepth = 1; // the fixture above nests 2 levels deep

    EXPECT_THROW(ParseXnbTypeReaderTable(reader, "test.xnb", tightLimits), ContentLoadException);
}

TEST(XnbTypeReaderTableTest, MalformedGenericTypeNameThrowsContentLoadExceptionNotInvalidArgument)
{
    System::IO::MemoryStream ms;
    System::IO::BinaryWriter writer(&ms, true);
    writer.Write7BitEncodedInt(1);
    // Unbalanced bracket: opens a generic argument list but never closes it.
    writer.Write(std::string("Microsoft.Xna.Framework.Content.ListReader`1[[Microsoft.Xna.Framework.Vector3"));
    writer.Write((int32_t)0);
    writer.Flush();
    auto buf = ms.ToArray();
    System::IO::MemoryStream ms2(buf.data(), (int32_t)buf.size());
    System::IO::BinaryReader reader(&ms2, true);

    EXPECT_THROW(ParseXnbTypeReaderTable(reader, "test.xnb"), ContentLoadException);
}
