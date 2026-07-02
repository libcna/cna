// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

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
