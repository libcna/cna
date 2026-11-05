// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <typeinfo>
#include <vector>
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(Tq083PacketArrays, WriteSliceHasNoPrefixAndZeroCountPreservesPosition) {
    // Refresh PacketWriter inherits BinaryWriter.Write(byte[],index,count), which forwards
    // to MemoryStream.Write (BCL IL_0000..000e). Raw bytes have no string length prefix.
    PacketWriter writer;
    const uint8_t source[] = {0x99, 0x10, 0x20, 0x30, 0xee};
    writer.Write(source, 1, 3);
    EXPECT_EQ(writer.getPositionProperty(), 3);
    ASSERT_EQ(writer.getLengthProperty(), 3);
    writer.setPositionProperty(2);
    writer.Write(source, 5, 0); // index == array length, count == 0 is valid.
    EXPECT_EQ(writer.getPositionProperty(), 2);
    EXPECT_EQ(writer.getLengthProperty(), 3);
    writer.setPositionProperty(0);
    uint8_t actual[3] = {};
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(actual, 0, 3), 3);
    EXPECT_EQ((std::vector<uint8_t>(actual, actual + 3)),
              (std::vector<uint8_t>{0x10, 0x20, 0x30}));
}

TEST(Tq083PacketArrays, ReadSlicePreservesGuardsAndReturnsPartialCountAtEof) {
    // BCL BinaryReader.Read(byte[],index,count) IL_006f..007d forwards the index and
    // returns the actual count. MemoryStream.Read IL_006f..0088 determines the EOF count.
    PacketReader reader;
    const uint8_t wire[] = {0x10, 0x20, 0x30};
    reader.getBaseStreamProperty()->Write(wire, 0, 3);
    reader.setPositionProperty(0);
    uint8_t destination[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
    EXPECT_EQ(reader.Read(destination, 2, 2), 2);
    EXPECT_EQ(reader.getPositionProperty(), 2);
    EXPECT_EQ(reader.Read(destination, 4, 2), 1);
    EXPECT_EQ((std::vector<uint8_t>(destination, destination + 6)),
              (std::vector<uint8_t>{0xa5, 0xa5, 0x10, 0x20, 0x30, 0xa5}));
    EXPECT_EQ(reader.getPositionProperty(), 3);
    EXPECT_EQ(reader.Read(destination, 0, 1), 0);
    EXPECT_EQ(reader.Read(destination, 6, 0), 0);
    EXPECT_EQ(reader.getPositionProperty(), 3);
    EXPECT_EQ(reader.getLengthProperty(), 3);
    EXPECT_TRUE(reader.ReadBytes(0).empty());
    EXPECT_TRUE(reader.ReadBytes(4).empty());
    EXPECT_EQ(reader.getPositionProperty(), 3);
}

TEST(Tq083PacketArrays, RefusedRangesPreservePacketAndDestination) {
    // BCL BinaryWriter.Write(byte[],index,count) -> MemoryStream.Write IL_0000..0049;
    // BinaryReader.Read IL_0000..0049, ReadBytes IL_0000..0018: reject before I/O.
    // Full Framework diagnostic text and index-vs-offset gaps are tracked by TQ-083.
    PacketWriter writer;
    const uint8_t source[] = {0x10, 0x20};
    writer.Write(source, 0, 2);
    EXPECT_THROW(writer.Write(static_cast<const uint8_t*>(nullptr), 0, 0),
                 System::ArgumentNullException);
    EXPECT_THROW(writer.Write(source, -1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(writer.Write(source, 0, -1), System::ArgumentOutOfRangeException);
    EXPECT_EQ(writer.getPositionProperty(), 2);
    EXPECT_EQ(writer.getLengthProperty(), 2);
    writer.setPositionProperty(0);
    uint8_t actual[2] = {};
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(actual, 0, 2), 2);
    EXPECT_EQ(actual[0], 0x10);
    EXPECT_EQ(actual[1], 0x20);

    PacketReader reader;
    reader.getBaseStreamProperty()->Write(source, 0, 2);
    reader.setPositionProperty(1);
    uint8_t destination[] = {0xa5, 0xa5};
    EXPECT_THROW(reader.Read(static_cast<uint8_t*>(nullptr), 0, 0),
                 System::ArgumentNullException);
    EXPECT_THROW(reader.Read(destination, -1, 0), System::ArgumentOutOfRangeException);
    EXPECT_THROW(reader.Read(destination, 0, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)reader.ReadBytes(-1), System::ArgumentOutOfRangeException);
    EXPECT_EQ(reader.getPositionProperty(), 1);
    EXPECT_EQ(reader.getLengthProperty(), 2);
    EXPECT_EQ(destination[0], 0xa5);
    EXPECT_EQ(destination[1], 0xa5);
    EXPECT_EQ(reader.ReadByte(), 0x20);
}
