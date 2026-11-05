// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <limits>
#include <typeinfo>
#include <vector>
#include "System/IO/EndOfStreamException.hpp"
#include "System/IO/IOException.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(Tq083PacketBoundaries, EveryPrimitiveUnderrunConsumesOnlyAvailableBytes) {
    // Refresh ReadSingle/Double IL_0000..000b read UInt32/UInt64; the inherited
    // BCL FillBuffer IL_0061..0087 consumes available bytes, then throws at EOF.
    // Message parity is a separate known SharpRuntime defect tracked by TQ-083.
    const std::vector<std::pair<int, std::function<void(PacketReader&)>>> cases{
        {1, [](auto& r) { (void)r.ReadBoolean(); }},
        {1, [](auto& r) { (void)r.ReadByte(); }},
        {1, [](auto& r) { (void)r.ReadSByte(); }},
        {2, [](auto& r) { (void)r.ReadInt16(); }},
        {2, [](auto& r) { (void)r.ReadUInt16(); }},
        {4, [](auto& r) { (void)r.ReadInt32(); }},
        {4, [](auto& r) { (void)r.ReadUInt32(); }},
        {8, [](auto& r) { (void)r.ReadInt64(); }},
        {8, [](auto& r) { (void)r.ReadUInt64(); }},
        {4, [](auto& r) { (void)r.ReadSingle(); }},
        {8, [](auto& r) { (void)r.ReadDouble(); }}};
    for (std::size_t method = 0; method < cases.size(); ++method) {
        const auto& [width, read] = cases[method];
        for (int length = 0; length < width; ++length) {
            SCOPED_TRACE(::testing::Message() << "method=" << method << " length=" << length);
            PacketReader reader;
            const uint8_t wire[8] = {1,2,3,4,5,6,7,8};
            if (length != 0) reader.getBaseStreamProperty()->Write(wire, 0, length);
            reader.setPositionProperty(0);
            try {
                read(reader);
                ADD_FAILURE() << "an incomplete primitive was accepted";
            } catch (const System::IO::EndOfStreamException& error) {
                EXPECT_EQ(typeid(error), typeid(System::IO::EndOfStreamException));
            }
            EXPECT_EQ(reader.getPositionProperty(), length);
            EXPECT_EQ(reader.getLengthProperty(), length);
        }
    }
}

TEST(Tq083PacketBoundaries, GrowthPreservesBytesAndLengthTracksTruncationAndZeroExtension) {
    // PacketWriter .ctor(int) IL_0000..000c creates expandable MemoryStream;
    // BCL SetLength IL_006a..00a3 zero-fills extension and clamps Position on shrink.
    PacketWriter writer(3);
    std::vector<uint8_t> expected(300);
    for (int i = 0; i < 300; ++i) expected[i] = static_cast<uint8_t>((37 * i + 11) & 255);
    writer.Write(expected.data(), 0, 3);
    writer.Write(expected.data(), 3, 297); // cross both requested capacity and 256 bytes
    ASSERT_EQ(writer.getLengthProperty(), 300);
    EXPECT_EQ(writer.getPositionProperty(), 300);
    writer.setPositionProperty(0);
    std::vector<uint8_t> actual(300);
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(actual.data(), 0, 300), 300);
    EXPECT_EQ(actual, expected);
    writer.getBaseStreamProperty()->SetLength(2);
    EXPECT_EQ(writer.getLengthProperty(), 2);
    EXPECT_EQ(writer.getPositionProperty(), 2);
    writer.getBaseStreamProperty()->SetLength(5);
    EXPECT_EQ(writer.getLengthProperty(), 5);
    EXPECT_EQ(writer.getPositionProperty(), 2);
    writer.setPositionProperty(0);
    uint8_t regrown[5] = {};
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(regrown, 0, 5), 5);
    // 37*0+11=11, 37*1+11=48; truncated bytes cannot reappear on extension.
    EXPECT_EQ((std::vector<uint8_t>(regrown, regrown + 5)), (std::vector<uint8_t>{11,48,0,0,0}));
    PacketReader reader(1);
    reader.getBaseStreamProperty()->Write(regrown, 0, 5);
    reader.setPositionProperty(4);
    reader.getBaseStreamProperty()->SetLength(1);
    EXPECT_EQ(reader.getLengthProperty(), 1);
    EXPECT_EQ(reader.getPositionProperty(), 1);
    reader.getBaseStreamProperty()->SetLength(3);
    EXPECT_EQ(reader.getLengthProperty(), 3);
    EXPECT_EQ(reader.getPositionProperty(), 1);
    EXPECT_EQ(reader.ReadBytes(4), (std::vector<uint8_t>{0,0}));
}

TEST(Tq083PacketBoundaries, OverflowingWriteRefusesBeforeAllocationAndKeepsState) {
    // MemoryStream.Write BCL IL_007c..0098 checks Position+count and throws
    // IOException(IO.IO_StreamTooLong). No giant allocation is required by this input.
    PacketWriter writer;
    writer.Write(uint8_t{0x5a});
    writer.setPositionProperty(std::numeric_limits<int>::max());
    try {
        writer.Write(int16_t{7});
        ADD_FAILURE() << "a stream longer than Int32.MaxValue was accepted";
    } catch (const System::IO::IOException& error) {
        EXPECT_EQ(typeid(error), typeid(System::IO::IOException));
        EXPECT_EQ(error.getMessageProperty(), "Stream was too long.");
    }
    EXPECT_EQ(writer.getPositionProperty(), std::numeric_limits<int>::max());
    EXPECT_EQ(writer.getLengthProperty(), 1);
    writer.setPositionProperty(0);
    uint8_t actual = 0;
    ASSERT_EQ(writer.getBaseStreamProperty()->Read(&actual, 0, 1), 1);
    EXPECT_EQ(actual, 0x5a);
}
