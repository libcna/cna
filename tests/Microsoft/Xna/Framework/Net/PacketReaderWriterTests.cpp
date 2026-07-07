// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/IO/EndOfStreamException.hpp"
#include "Microsoft/Xna/Framework/Net/PacketReader.hpp"
#include "Microsoft/Xna/Framework/Net/PacketWriter.hpp"

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

// --- PacketReader ---

TEST(PacketReaderTest, DefaultCtorIsEmpty) {
    PacketReader r;
    EXPECT_EQ(r.getLengthProperty(), 0);
    EXPECT_EQ(r.getPositionProperty(), 0);
}

TEST(PacketReaderTest, CapacityCtorIsEmpty) {
    PacketReader r(64);
    EXPECT_EQ(r.getLengthProperty(), 0);
    EXPECT_EQ(r.getPositionProperty(), 0);
}

// Task 5.11: real .NET's MemoryStream(int capacity) - which FNA's PacketReader(int capacity)
// constructs internally - throws ArgumentOutOfRangeException for a negative value, regardless of
// capacity being otherwise just a preallocation hint with no observable effect.
TEST(PacketReaderTest, NegativeCapacityThrowsArgumentOutOfRangeException) {
    EXPECT_THROW(PacketReader r(-1), System::ArgumentOutOfRangeException);
}

TEST(PacketReaderTest, SetPositionSeeks) {
    PacketReader r;
    uint8_t bytes[] = {1, 2, 3, 4};
    r.getBaseStreamProperty()->Write(bytes, 0, 4);
    r.setPositionProperty(2);
    EXPECT_EQ(r.getPositionProperty(), 2);
    EXPECT_EQ(r.ReadByte(), 3);
}

TEST(PacketReaderTest, ReadSingleForwardsToBase) {
    PacketReader r;
    float payload[] = {3.5f};
    r.getBaseStreamProperty()->Write(reinterpret_cast<uint8_t*>(payload), 0, sizeof(float));
    r.setPositionProperty(0);
    EXPECT_FLOAT_EQ(r.ReadSingle(), 3.5f);
}

TEST(PacketReaderTest, ReadDoubleForwardsToBase) {
    PacketReader r;
    double payload[] = {2.25};
    r.getBaseStreamProperty()->Write(reinterpret_cast<uint8_t*>(payload), 0, sizeof(double));
    r.setPositionProperty(0);
    EXPECT_DOUBLE_EQ(r.ReadDouble(), 2.25);
}

TEST(PacketReaderTest, InheritedReadInt32Works) {
    PacketReader r;
    int32_t payload[] = {12345};
    r.getBaseStreamProperty()->Write(reinterpret_cast<uint8_t*>(payload), 0, sizeof(int32_t));
    r.setPositionProperty(0);
    EXPECT_EQ(r.ReadInt32(), 12345);
}

// --- PacketWriter ---

TEST(PacketWriterTest, DefaultCtorIsEmpty) {
    PacketWriter w;
    EXPECT_EQ(w.getLengthProperty(), 0);
    EXPECT_EQ(w.getPositionProperty(), 0);
}

TEST(PacketWriterTest, CapacityCtorIsEmpty) {
    PacketWriter w(64);
    EXPECT_EQ(w.getLengthProperty(), 0);
    EXPECT_EQ(w.getPositionProperty(), 0);
}

// Task 5.11: same reasoning as PacketReaderTest.NegativeCapacityThrowsArgumentOutOfRangeException -
// FNA's PacketWriter(int capacity) constructs a MemoryStream(capacity) internally, which real .NET
// throws ArgumentOutOfRangeException for when capacity is negative.
TEST(PacketWriterTest, NegativeCapacityThrowsArgumentOutOfRangeException) {
    EXPECT_THROW(PacketWriter w(-1), System::ArgumentOutOfRangeException);
}

TEST(PacketWriterTest, SetPositionSeeksForRewrite) {
    PacketWriter w;
    w.Write(static_cast<int32_t>(1));
    w.Write(static_cast<int32_t>(2));
    w.setPositionProperty(0);
    w.Write(static_cast<int32_t>(99));
    EXPECT_EQ(w.getLengthProperty(), 8);
    EXPECT_EQ(w.getPositionProperty(), 4);
}

TEST(PacketWriterTest, WriteFloatForwardsToBase) {
    PacketWriter w;
    w.Write(1.5f);
    EXPECT_EQ(w.getLengthProperty(), 4);
}

TEST(PacketWriterTest, WriteDoubleForwardsToBase) {
    PacketWriter w;
    w.Write(1.5);
    EXPECT_EQ(w.getLengthProperty(), 8);
}

TEST(PacketWriterTest, InheritedWriteInt32Works) {
    PacketWriter w;
    w.Write(static_cast<int32_t>(42));
    EXPECT_EQ(w.getLengthProperty(), 4);
}

TEST(PacketWriterTest, InheritedWriteBoolWorks) {
    PacketWriter w;
    w.Write(true);
    EXPECT_EQ(w.getLengthProperty(), 1);
}

// --- Round-trip: PacketWriter output fed into a PacketReader ---

namespace {
    PacketReader MakeReaderFromWriter(PacketWriter& w) {
        w.setPositionProperty(0);
        uint8_t buffer[256];
        int n = w.getBaseStreamProperty()->Read(buffer, 0, w.getLengthProperty());

        PacketReader r;
        r.getBaseStreamProperty()->Write(buffer, 0, n);
        r.setPositionProperty(0);
        return r;
    }
}

// PacketWriter::Write(Color) writes 4 bytes; PacketReader::ReadColor() reads 4 floats
// (16 bytes). This asymmetry exists upstream and is preserved rather than symmetrized,
// so Color is not round-trippable through these two methods alone.

TEST(PacketWriterTest, WriteColorWritesFourBytes) {
    PacketWriter w;
    w.Write(Color(10, 20, 30, 40));
    EXPECT_EQ(w.getLengthProperty(), 4);
}

TEST(PacketReaderTest, ReadColorReadsSixteenBytesAsFloats) {
    PacketReader r;
    float payload[] = {0.1f, 0.2f, 0.3f, 0.4f};
    r.getBaseStreamProperty()->Write(reinterpret_cast<uint8_t*>(payload), 0, sizeof(payload));
    r.setPositionProperty(0);
    Color result = r.ReadColor();
    EXPECT_EQ(r.getPositionProperty(), 16);
    EXPECT_EQ(result, Color(0.1f, 0.2f, 0.3f, 0.4f));
}

TEST(PacketReaderWriterTest, MatrixRoundtrip) {
    PacketWriter w;
    Matrix m(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    w.Write(m);
    PacketReader r = MakeReaderFromWriter(w);
    Matrix result = r.ReadMatrix();
    EXPECT_EQ(result, m);
}

TEST(PacketReaderWriterTest, QuaternionRoundtrip) {
    PacketWriter w;
    Quaternion q(1.0f, 2.0f, 3.0f, 4.0f);
    w.Write(q);
    PacketReader r = MakeReaderFromWriter(w);
    Quaternion result = r.ReadQuaternion();
    EXPECT_EQ(result, q);
}

TEST(PacketReaderWriterTest, Vector2Roundtrip) {
    PacketWriter w;
    Vector2 v(1.0f, 2.0f);
    w.Write(v);
    PacketReader r = MakeReaderFromWriter(w);
    Vector2 result = r.ReadVector2();
    EXPECT_EQ(result, v);
}

TEST(PacketReaderWriterTest, Vector3Roundtrip) {
    PacketWriter w;
    Vector3 v(1.0f, 2.0f, 3.0f);
    w.Write(v);
    PacketReader r = MakeReaderFromWriter(w);
    Vector3 result = r.ReadVector3();
    EXPECT_EQ(result, v);
}

TEST(PacketReaderWriterTest, Vector4Roundtrip) {
    PacketWriter w;
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    w.Write(v);
    PacketReader r = MakeReaderFromWriter(w);
    Vector4 result = r.ReadVector4();
    EXPECT_EQ(result, v);
}

// Task 5.10: round-trip coverage for the primitive overloads PacketReader/PacketWriter inherit
// from BinaryReader/BinaryWriter, exercised through PacketReader/PacketWriter themselves rather
// than only relying on sharp-runtime's own separate BinaryReader/BinaryWriter test suite - so a
// regression in how PacketReader/PacketWriter wire up to those bases (e.g. a bad `using` or a
// hiding override) would be caught here even if the base classes' own tests still pass.

TEST(PacketReaderWriterTest, ByteRoundtrip) {
    PacketWriter w;
    w.Write(static_cast<uint8_t>(200));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadByte(), 200);
}

TEST(PacketReaderWriterTest, SByteRoundtrip) {
    PacketWriter w;
    w.Write(static_cast<int8_t>(-100));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadSByte(), -100);
}

TEST(PacketReaderWriterTest, Int16Roundtrip) {
    PacketWriter w;
    w.Write(static_cast<int16_t>(-12345));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadInt16(), -12345);
}

TEST(PacketReaderWriterTest, UInt16Roundtrip) {
    PacketWriter w;
    w.Write(static_cast<uint16_t>(60000));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadUInt16(), 60000);
}

TEST(PacketReaderWriterTest, UInt32Roundtrip) {
    PacketWriter w;
    w.Write(static_cast<uint32_t>(4000000000U));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadUInt32(), 4000000000U);
}

TEST(PacketReaderWriterTest, Int64RoundtripBoundaryValues) {
    PacketWriter wMin;
    wMin.Write(std::numeric_limits<int64_t>::min());
    PacketReader rMin = MakeReaderFromWriter(wMin);
    EXPECT_EQ(rMin.ReadInt64(), std::numeric_limits<int64_t>::min());

    PacketWriter wMax;
    wMax.Write(std::numeric_limits<int64_t>::max());
    PacketReader rMax = MakeReaderFromWriter(wMax);
    EXPECT_EQ(rMax.ReadInt64(), std::numeric_limits<int64_t>::max());
}

TEST(PacketReaderWriterTest, UInt64Roundtrip) {
    PacketWriter w;
    w.Write(std::numeric_limits<uint64_t>::max());
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadUInt64(), std::numeric_limits<uint64_t>::max());
}

TEST(PacketReaderWriterTest, StringRoundtripAscii) {
    PacketWriter w;
    w.Write(std::string("hello packet"));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadString(), "hello packet");
}

TEST(PacketReaderWriterTest, StringRoundtripEmpty) {
    PacketWriter w;
    w.Write(std::string(""));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadString(), "");
}

TEST(PacketReaderWriterTest, StringRoundtripMultiByteUnicodeContent) {
    // UTF-8 multi-byte content: Czech "Příliš žluťoučký kůň" plus a 4-byte emoji, so the
    // 7-bit-encoded length prefix must count encoded bytes, not code points.
    const std::string unicode = "P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd k\xc5\xaf\xc5\x88 \xf0\x9f\x8e\xae";
    PacketWriter w;
    w.Write(unicode);
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_EQ(r.ReadString(), unicode);
}

// Task 4.4: PacketReader inherits System::IO::BinaryReader::ReadBytes(int) (array-returning)
// with no overriding declaration of its own, so this exercises the real inherited method through
// the concrete PacketReader type actual game code uses - not just sharp-runtime's own
// BinaryReader test suite, so a future regression in how PacketReader wires up to that base
// would be caught here too.
TEST(PacketReaderWriterTest, ReadBytesReturnsExactCountRequested) {
    PacketWriter w;
    w.Write(static_cast<int32_t>(0x11223344));
    w.Write(static_cast<uint8_t>(0xAB));
    PacketReader r = MakeReaderFromWriter(w);

    std::vector<uint8_t> chunk = r.ReadBytes(4);
    ASSERT_EQ(chunk.size(), 4u);
    // Written little-endian by Write(int32_t).
    EXPECT_EQ(chunk[0], 0x44);
    EXPECT_EQ(chunk[1], 0x33);
    EXPECT_EQ(chunk[2], 0x22);
    EXPECT_EQ(chunk[3], 0x11);
    // Confirms the read position genuinely advanced, not just that the bytes were correct.
    EXPECT_EQ(r.ReadByte(), 0xAB);
}

// Task 4.4: real .NET's BinaryReader.ReadBytes(int) does not throw at end-of-stream (unlike
// ReadByte/ReadInt32/etc, which all throw EndOfStreamException) - it returns a shorter array
// trimmed to however many bytes were actually available. Confirmed against sharp-runtime's own
// BinaryReader::ReadBytes implementation before writing this test.
TEST(PacketReaderWriterTest, ReadBytesTrimsResultAtEndOfStreamWithoutThrowing) {
    PacketWriter w;
    w.Write(static_cast<uint8_t>(1));
    w.Write(static_cast<uint8_t>(2));
    PacketReader r = MakeReaderFromWriter(w);

    std::vector<uint8_t> chunk = r.ReadBytes(10);
    ASSERT_EQ(chunk.size(), 2u);
    EXPECT_EQ(chunk[0], 1);
    EXPECT_EQ(chunk[1], 2);
}

TEST(PacketReaderTest, ReadingPastEndOfBufferThrows) {
    PacketWriter w;
    w.Write(static_cast<int32_t>(1));
    PacketReader r = MakeReaderFromWriter(w);
    (void) r.ReadInt32();
    EXPECT_THROW((void) r.ReadByte(), System::IO::EndOfStreamException);
}

TEST(PacketReaderTest, ReadingPartialValueAtEndOfBufferThrows) {
    PacketWriter w;
    // Two bytes on the wire, but ReadInt32 needs four - an underrun mid-value, not just at
    // the exact boundary the previous test covers.
    w.Write(static_cast<int16_t>(7));
    PacketReader r = MakeReaderFromWriter(w);
    EXPECT_THROW((void) r.ReadInt32(), System::IO::EndOfStreamException);
}
