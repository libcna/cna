// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(Tq083PacketStrings, SupplementaryUtf8AndEmbeddedNulHaveLiteralFrames) {
    // U+1F3AE = surrogate pair D83C DFAE in XNA, UTF-8 F0 9F 8E AE.
    // BinaryWriter.Write(string) BCL IL_000e..0021 counts bytes: 1 + 1 + 4 = 6.
    // Embedded NUL belongs to the string; there is no BOM or trailing terminator.
    const std::string text("A\0\xf0\x9f\x8e\xae", 6);
    const std::vector<uint8_t> wire{6, 'A', 0, 0xf0, 0x9f, 0x8e, 0xae, 0xa5};
    PacketWriter writer;
    writer.Write(text);
    writer.Write(uint8_t{0xa5});
    EXPECT_EQ(writer.getPositionProperty(), 8);
    ASSERT_EQ(writer.getLengthProperty(), 8);
    writer.setPositionProperty(0);
    std::vector<uint8_t> actual(8);
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(actual.data(), 0, 8), 8);
    EXPECT_EQ(actual, wire);

    PacketReader reader;
    reader.getBaseStreamProperty()->Write(wire.data(), 0, 8); // literal input, never writer output
    reader.setPositionProperty(0);
    EXPECT_EQ(reader.ReadString(), text);
    EXPECT_EQ(reader.getPositionProperty(), 7);
    EXPECT_EQ(reader.ReadByte(), 0xa5);
}

TEST(Tq083PacketStrings, ReadCharDecodesBmpUtf8WithoutAStringPrefix) {
    // XNA's BinaryReader defaults to UTF8Encoding (BCL .ctor(Stream) IL_0002..0007).
    // U+0159 = C5 99, U+20AC = E2 82 AC; each ReadChar consumes one UTF-16 unit.
    const uint8_t wire[] = {'A', 0, 0xc5, 0x99, 0xe2, 0x82, 0xac, 0x5a};
    PacketReader reader;
    reader.getBaseStreamProperty()->Write(wire, 0, 8);
    reader.setPositionProperty(0);
    EXPECT_EQ(reader.ReadChar(), u'A');
    EXPECT_EQ(reader.getPositionProperty(), 1);
    EXPECT_EQ(reader.ReadChar(), u'\0');
    EXPECT_EQ(reader.getPositionProperty(), 2);
    EXPECT_EQ(reader.ReadChar(), u'\u0159');
    EXPECT_EQ(reader.getPositionProperty(), 4);
    EXPECT_EQ(reader.ReadChar(), u'\u20ac');
    EXPECT_EQ(reader.getPositionProperty(), 7);
    EXPECT_EQ(reader.ReadByte(), 0x5a);
}

TEST(Tq083PacketStrings, ReadCharsCountsUtf16UnitsAndIndexedReadPreservesGuards) {
    // BCL ReadChars / Read(char[],index,count) use Decoder.GetChars. U+1F3AE:
    // 0x1F3AE-0x10000=0xF3AE; high=D800+(F3AE>>10)=D83C, low=DC00+3AE=DFAE.
    const uint8_t wire[] = {0xf0, 0x9f, 0x8e, 0xae, 0xc5, 0x99, 'Z'};
    PacketReader reader;
    reader.getBaseStreamProperty()->Write(wire, 0, 7);
    reader.setPositionProperty(0);
    EXPECT_EQ(reader.ReadChars(2), (std::vector<SharpRuntime::charcs>{0xd83c, 0xdfae}));
    EXPECT_EQ(reader.getPositionProperty(), 4);
    SharpRuntime::charcs chars[] = {u'!', u'!', u'!', u'!'};
    EXPECT_EQ(reader.Read(chars, 1, 2), 2);
    EXPECT_EQ((std::vector<SharpRuntime::charcs>(chars, chars + 4)),
              (std::vector<SharpRuntime::charcs>{u'!', u'\u0159', u'Z', u'!'}));
    EXPECT_EQ(reader.getPositionProperty(), 7);
    EXPECT_EQ(reader.Read(chars, 0, 1), 0);
    EXPECT_TRUE(reader.ReadChars(3).empty());
}
