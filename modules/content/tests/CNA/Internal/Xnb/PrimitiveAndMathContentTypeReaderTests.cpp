// SPDX-License-Identifier: MS-PL
//
// XNB-18A/XNB-19: unit tests for the primitive and math .xnb readers -- registration
// under the real FNA canonical name, and correct field-order round-trips via ReadUntyped().

#include <any>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "CNA/Internal/Xnb/MathContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/PrimitiveContentTypeReaders.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/BinaryWriter.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class PrimitiveAndMathReaderTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterPrimitiveXnbReaders();
            CNA::Internal::Xnb::RegisterMathXnbReaders();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        // Builds a MemoryStream over exactly the bytes written by `write`, then a ContentReader
        // over it (no header/type-table involved -- these tests call ReadUntyped() directly).
        template <typename F>
        std::unique_ptr<ContentReader> MakeReader(F&& write)
        {
            auto ms = std::make_unique<System::IO::MemoryStream>();
            System::IO::BinaryWriter writer(ms.get(), true);
            write(writer);
            writer.Flush();
            auto buf = ms->ToArray();
            streams_.push_back(std::make_unique<System::IO::MemoryStream>(buf.data(), (int32_t)buf.size()));
            return std::make_unique<ContentReader>(nullptr, streams_.back().get(), "test", 5, 'w');
        }

        // As MakeReader(), over literal bytes -- for expectations that must not depend on the
        // BinaryWriter the readers' own BinaryReader mirrors.
        std::unique_ptr<ContentReader> MakeLiteralReader(const std::vector<uint8_t>& bytes)
        {
            streams_.push_back(std::make_unique<System::IO::MemoryStream>(
                bytes.data(), static_cast<int32_t>(bytes.size())));
            return std::make_unique<ContentReader>(nullptr, streams_.back().get(), "test", 5, 'w');
        }

        [[nodiscard]] const System::IO::MemoryStream& LastStream() const { return *streams_.back(); }

    private:
        std::vector<std::unique_ptr<System::IO::MemoryStream>> streams_;
    };

    // Little-endian IEEE-754 binary32 encodings of the integers 1 through 16 (1.0f = 0x3f800000,
    // 2.0f = 0x40000000, ... 16.0f = 0x41800000), written out by hand.
    std::vector<uint8_t> FloatsOneTo(int count)
    {
        static constexpr uint32_t bits[16]{
            0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x40a00000, 0x40c00000,
            0x40e00000, 0x41000000, 0x41100000, 0x41200000, 0x41300000, 0x41400000,
            0x41500000, 0x41600000, 0x41700000, 0x41800000};
        std::vector<uint8_t> bytes;
        for (int i = 0; i < count; ++i)
            for (int shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<uint8_t>(bits[i] >> shift));
        return bytes;
    }

    template <typename T>
    T ReadViaRegisteredReader(const std::string& canonicalName, ContentReader& reader)
    {
        auto typeReader = ContentTypeReaderManager::CreateReader(canonicalName);
        return std::any_cast<T>(typeReader->ReadUntyped(reader, std::any{}));
    }
}

TEST_F(PrimitiveAndMathReaderTest, AllPrimitiveReadersAreRegistered)
{
    for (const char* name : {
             "Microsoft.Xna.Framework.Content.BooleanReader", "Microsoft.Xna.Framework.Content.ByteReader",
             "Microsoft.Xna.Framework.Content.SByteReader", "Microsoft.Xna.Framework.Content.Int16Reader",
             "Microsoft.Xna.Framework.Content.UInt16Reader", "Microsoft.Xna.Framework.Content.Int32Reader",
             "Microsoft.Xna.Framework.Content.UInt32Reader", "Microsoft.Xna.Framework.Content.Int64Reader",
             "Microsoft.Xna.Framework.Content.UInt64Reader", "Microsoft.Xna.Framework.Content.SingleReader",
             "Microsoft.Xna.Framework.Content.DoubleReader", "Microsoft.Xna.Framework.Content.CharReader",
             "Microsoft.Xna.Framework.Content.StringReader"})
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(name)) << name;
    }
}

TEST_F(PrimitiveAndMathReaderTest, AllMathReadersAreRegistered)
{
    for (const char* name : {
             "Microsoft.Xna.Framework.Content.Vector2Reader", "Microsoft.Xna.Framework.Content.Vector3Reader",
             "Microsoft.Xna.Framework.Content.Vector4Reader", "Microsoft.Xna.Framework.Content.MatrixReader",
             "Microsoft.Xna.Framework.Content.QuaternionReader", "Microsoft.Xna.Framework.Content.ColorReader",
             "Microsoft.Xna.Framework.Content.PlaneReader", "Microsoft.Xna.Framework.Content.PointReader",
             "Microsoft.Xna.Framework.Content.RectangleReader", "Microsoft.Xna.Framework.Content.BoundingBoxReader",
             "Microsoft.Xna.Framework.Content.BoundingSphereReader",
             "Microsoft.Xna.Framework.Content.BoundingFrustumReader", "Microsoft.Xna.Framework.Content.RayReader"})
    {
        EXPECT_TRUE(ContentTypeReaderManager::IsRegistered(name)) << name;
    }
}

TEST_F(PrimitiveAndMathReaderTest, BooleanReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(true); });
    EXPECT_TRUE(ReadViaRegisteredReader<bool>("Microsoft.Xna.Framework.Content.BooleanReader", *reader));
}

TEST_F(PrimitiveAndMathReaderTest, ByteReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((uint8_t)200); });
    EXPECT_EQ(ReadViaRegisteredReader<uint8_t>("Microsoft.Xna.Framework.Content.ByteReader", *reader), 200);
}

TEST_F(PrimitiveAndMathReaderTest, SByteReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int8_t)-100); });
    EXPECT_EQ(ReadViaRegisteredReader<int8_t>("Microsoft.Xna.Framework.Content.SByteReader", *reader), -100);
}

TEST_F(PrimitiveAndMathReaderTest, Int16ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int16_t)-1234); });
    EXPECT_EQ(ReadViaRegisteredReader<int16_t>("Microsoft.Xna.Framework.Content.Int16Reader", *reader), -1234);
}

TEST_F(PrimitiveAndMathReaderTest, UInt16ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((uint16_t)60000); });
    EXPECT_EQ(ReadViaRegisteredReader<uint16_t>("Microsoft.Xna.Framework.Content.UInt16Reader", *reader), 60000);
}

TEST_F(PrimitiveAndMathReaderTest, Int32ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int32_t)-123456); });
    EXPECT_EQ(ReadViaRegisteredReader<int32_t>("Microsoft.Xna.Framework.Content.Int32Reader", *reader), -123456);
}

TEST_F(PrimitiveAndMathReaderTest, UInt32ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((uint32_t)4000000000u); });
    EXPECT_EQ(ReadViaRegisteredReader<uint32_t>("Microsoft.Xna.Framework.Content.UInt32Reader", *reader), 4000000000u);
}

TEST_F(PrimitiveAndMathReaderTest, Int64ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int64_t)-123456789012345LL); });
    EXPECT_EQ(ReadViaRegisteredReader<int64_t>("Microsoft.Xna.Framework.Content.Int64Reader", *reader),
              -123456789012345LL);
}

TEST_F(PrimitiveAndMathReaderTest, UInt64ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((uint64_t)18000000000000000000ULL); });
    EXPECT_EQ(ReadViaRegisteredReader<uint64_t>("Microsoft.Xna.Framework.Content.UInt64Reader", *reader),
              18000000000000000000ULL);
}

TEST_F(PrimitiveAndMathReaderTest, SingleReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(3.25f); });
    EXPECT_FLOAT_EQ(ReadViaRegisteredReader<float>("Microsoft.Xna.Framework.Content.SingleReader", *reader), 3.25f);
}

TEST_F(PrimitiveAndMathReaderTest, DoubleReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(3.14159265358979); });
    EXPECT_DOUBLE_EQ(ReadViaRegisteredReader<double>("Microsoft.Xna.Framework.Content.DoubleReader", *reader),
                      3.14159265358979);
}

TEST_F(PrimitiveAndMathReaderTest, CharReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((uint8_t)'A'); });
    EXPECT_EQ(ReadViaRegisteredReader<char16_t>("Microsoft.Xna.Framework.Content.CharReader", *reader), u'A');
}

TEST_F(PrimitiveAndMathReaderTest, StringReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(std::string("hello xnb")); });
    EXPECT_EQ(ReadViaRegisteredReader<std::string>("Microsoft.Xna.Framework.Content.StringReader", *reader),
              "hello xnb");
}

TEST_F(PrimitiveAndMathReaderTest, Vector2ReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(1.0f); w.Write(2.0f); });
    const auto v = ReadViaRegisteredReader<Vector2>("Microsoft.Xna.Framework.Content.Vector2Reader", *reader);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
}

TEST_F(PrimitiveAndMathReaderTest, ColorReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) {
        w.Write((uint8_t)10); w.Write((uint8_t)20); w.Write((uint8_t)30); w.Write((uint8_t)40);
    });
    const auto c = ReadViaRegisteredReader<Color>("Microsoft.Xna.Framework.Content.ColorReader", *reader);
    EXPECT_EQ(c.getRProperty(), 10);
    EXPECT_EQ(c.getGProperty(), 20);
    EXPECT_EQ(c.getBProperty(), 30);
    EXPECT_EQ(c.getAProperty(), 40);
}

TEST_F(PrimitiveAndMathReaderTest, PlaneReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(1.0f); w.Write(0.0f); w.Write(0.0f); w.Write(5.0f); });
    const auto p = ReadViaRegisteredReader<Plane>("Microsoft.Xna.Framework.Content.PlaneReader", *reader);
    EXPECT_FLOAT_EQ(p.Normal.X, 1.0f);
    EXPECT_FLOAT_EQ(p.Normal.Y, 0.0f);
    EXPECT_FLOAT_EQ(p.Normal.Z, 0.0f);
    EXPECT_FLOAT_EQ(p.D, 5.0f);
}

TEST_F(PrimitiveAndMathReaderTest, PointReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write((int32_t)7); w.Write((int32_t)9); });
    const auto pt = ReadViaRegisteredReader<Point>("Microsoft.Xna.Framework.Content.PointReader", *reader);
    EXPECT_EQ(pt.X, 7);
    EXPECT_EQ(pt.Y, 9);
}

TEST_F(PrimitiveAndMathReaderTest, RectangleReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) {
        w.Write((int32_t)1); w.Write((int32_t)2); w.Write((int32_t)3); w.Write((int32_t)4);
    });
    const auto r = ReadViaRegisteredReader<Rectangle>("Microsoft.Xna.Framework.Content.RectangleReader", *reader);
    EXPECT_EQ(r.X, 1);
    EXPECT_EQ(r.Y, 2);
    EXPECT_EQ(r.Width, 3);
    EXPECT_EQ(r.Height, 4);
}

TEST_F(PrimitiveAndMathReaderTest, BoundingBoxReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) {
        w.Write(0.0f); w.Write(0.0f); w.Write(0.0f);
        w.Write(1.0f); w.Write(1.0f); w.Write(1.0f);
    });
    const auto bb = ReadViaRegisteredReader<BoundingBox>("Microsoft.Xna.Framework.Content.BoundingBoxReader", *reader);
    EXPECT_FLOAT_EQ(bb.Min.X, 0.0f);
    EXPECT_FLOAT_EQ(bb.Max.X, 1.0f);
}

TEST_F(PrimitiveAndMathReaderTest, BoundingSphereReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) { w.Write(1.0f); w.Write(2.0f); w.Write(3.0f); w.Write(4.0f); });
    const auto bs = ReadViaRegisteredReader<BoundingSphere>(
        "Microsoft.Xna.Framework.Content.BoundingSphereReader", *reader);
    EXPECT_FLOAT_EQ(bs.Center.X, 1.0f);
    EXPECT_FLOAT_EQ(bs.Radius, 4.0f);
}

TEST_F(PrimitiveAndMathReaderTest, RayReaderRoundTrips)
{
    auto reader = MakeReader([](auto& w) {
        w.Write(0.0f); w.Write(0.0f); w.Write(0.0f);
        w.Write(0.0f); w.Write(1.0f); w.Write(0.0f);
    });
    const auto ray = ReadViaRegisteredReader<Ray>("Microsoft.Xna.Framework.Content.RayReader", *reader);
    EXPECT_FLOAT_EQ(ray.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(ray.Direction.Y, 1.0f);
}

// TQ-004: the readers below are only thin wrappers, so what can go wrong is the order in which
// they consume fields and which class a canonical name creates. Every field gets a distinct
// literal value (1, 2, 3, ... encoded by hand above), so a swapped or skipped field shows.
// Field orders: FNA src/Content/ContentReaders/*Reader.cs and XNA's matching readers.

TEST_F(PrimitiveAndMathReaderTest, BoundingBoxReaderReadsMinThenMaxFieldByField)
{
    auto reader = MakeLiteralReader(FloatsOneTo(6));
    const auto box = ReadViaRegisteredReader<BoundingBox>(
        "Microsoft.Xna.Framework.Content.BoundingBoxReader", *reader);
    EXPECT_EQ(box.Min, Vector3(1, 2, 3));
    EXPECT_EQ(box.Max, Vector3(4, 5, 6));
    EXPECT_EQ(LastStream().getPositionProperty(), 24);
}

TEST_F(PrimitiveAndMathReaderTest, BoundingSphereReaderReadsWholeCenterThenRadius)
{
    auto reader = MakeLiteralReader(FloatsOneTo(4));
    const auto sphere = ReadViaRegisteredReader<BoundingSphere>(
        "Microsoft.Xna.Framework.Content.BoundingSphereReader", *reader);
    EXPECT_EQ(sphere.Center, Vector3(1, 2, 3));
    EXPECT_EQ(sphere.Radius, 4.0f);
    EXPECT_EQ(LastStream().getPositionProperty(), 16);
}

TEST_F(PrimitiveAndMathReaderTest, RayReaderReadsPositionThenDirectionFieldByField)
{
    auto reader = MakeLiteralReader(FloatsOneTo(6));
    const auto ray = ReadViaRegisteredReader<Ray>("Microsoft.Xna.Framework.Content.RayReader", *reader);
    EXPECT_EQ(ray.Position, Vector3(1, 2, 3));
    EXPECT_EQ(ray.Direction, Vector3(4, 5, 6));
    EXPECT_EQ(LastStream().getPositionProperty(), 24);
}

TEST_F(PrimitiveAndMathReaderTest, PlaneReaderReadsNormalThenDistanceUnnormalized)
{
    auto reader = MakeLiteralReader(FloatsOneTo(4));
    const auto plane = ReadViaRegisteredReader<Plane>("Microsoft.Xna.Framework.Content.PlaneReader", *reader);
    EXPECT_EQ(plane.Normal, Vector3(1, 2, 3)); // stored as written, not normalized
    EXPECT_EQ(plane.D, 4.0f);
}

TEST_F(PrimitiveAndMathReaderTest, Vector3Vector4AndQuaternionReadersReadTheirOwnFieldCounts)
{
    {
        auto reader = MakeLiteralReader(FloatsOneTo(4));
        EXPECT_EQ(ReadViaRegisteredReader<Vector3>("Microsoft.Xna.Framework.Content.Vector3Reader", *reader),
                  Vector3(1, 2, 3));
        EXPECT_EQ(LastStream().getPositionProperty(), 12);
    }
    {
        auto reader = MakeLiteralReader(FloatsOneTo(5));
        EXPECT_EQ(ReadViaRegisteredReader<Vector4>("Microsoft.Xna.Framework.Content.Vector4Reader", *reader),
                  Vector4(1, 2, 3, 4));
        EXPECT_EQ(LastStream().getPositionProperty(), 16);
    }
    {
        auto reader = MakeLiteralReader(FloatsOneTo(5));
        EXPECT_EQ(ReadViaRegisteredReader<Quaternion>(
                      "Microsoft.Xna.Framework.Content.QuaternionReader", *reader),
                  Quaternion(1, 2, 3, 4));
        EXPECT_EQ(LastStream().getPositionProperty(), 16);
    }
}

TEST_F(PrimitiveAndMathReaderTest, MatrixReaderReadsSixteenRowMajorFields)
{
    auto reader = MakeLiteralReader(FloatsOneTo(16));
    const auto m = ReadViaRegisteredReader<Matrix>("Microsoft.Xna.Framework.Content.MatrixReader", *reader);
    EXPECT_EQ(m, Matrix(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16));
    EXPECT_EQ(LastStream().getPositionProperty(), 64);
}

// FNA BoundingFrustumReader: `new BoundingFrustum(input.ReadMatrix())`.
TEST_F(PrimitiveAndMathReaderTest, BoundingFrustumReaderBuildsTheFrustumFromItsSerializedMatrix)
{
    auto reader = MakeLiteralReader(FloatsOneTo(16));
    const auto frustum = ReadViaRegisteredReader<BoundingFrustum>(
        "Microsoft.Xna.Framework.Content.BoundingFrustumReader", *reader);
    EXPECT_EQ(frustum.getMatrixProperty(),
              Matrix(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16));
    EXPECT_EQ(LastStream().getPositionProperty(), 64);
}

// .NET BinaryReader.ReadBoolean is `byte != 0`: any nonzero byte is true.
TEST_F(PrimitiveAndMathReaderTest, BooleanReaderTreatsZeroAsFalseAndAnyOtherByteAsTrue)
{
    auto reader = MakeLiteralReader({0x00, 0x01, 0x02, 0xff});
    for (const bool expected : {false, true, true, true})
        EXPECT_EQ(ReadViaRegisteredReader<bool>("Microsoft.Xna.Framework.Content.BooleanReader", *reader),
                  expected);
}

// XNA/FNA CharReader is BinaryReader.ReadChar() with the default UTF-8 encoding: one character,
// however many bytes UTF-8 needs for it (U+00E9 = C3 A9, U+20AC = E2 82 AC).
TEST_F(PrimitiveAndMathReaderTest, CharReaderDecodesOneUtf8EncodedCharacter)
{
    auto reader = MakeLiteralReader({0x41, 0xc3, 0xa9, 0xe2, 0x82, 0xac, 0x5a});
    EXPECT_EQ(ReadViaRegisteredReader<char16_t>("Microsoft.Xna.Framework.Content.CharReader", *reader), u'A');
    EXPECT_EQ(ReadViaRegisteredReader<char16_t>("Microsoft.Xna.Framework.Content.CharReader", *reader),
              u'\u00e9');
    EXPECT_EQ(ReadViaRegisteredReader<char16_t>("Microsoft.Xna.Framework.Content.CharReader", *reader),
              u'\u20ac');
    EXPECT_EQ(LastStream().getPositionProperty(), 6);
}

// System.String is a 7-bit-encoded byte length followed by UTF-8 bytes: an empty string is the
// single byte 0, and a 128-byte string needs the two-byte length 0x80 0x01.
TEST_F(PrimitiveAndMathReaderTest, StringReaderHonoursEmptyMultiByteAndTwoByteLengthPrefixes)
{
    std::vector<uint8_t> bytes{0x00, 0x03, 0xc3, 0xa9, 0x21, 0x80, 0x01};
    bytes.insert(bytes.end(), 128, uint8_t{'q'});
    auto reader = MakeLiteralReader(bytes);
    const char* name = "Microsoft.Xna.Framework.Content.StringReader";
    EXPECT_EQ(ReadViaRegisteredReader<std::string>(name, *reader), "");
    EXPECT_EQ(ReadViaRegisteredReader<std::string>(name, *reader), "\xc3\xa9!");
    EXPECT_EQ(ReadViaRegisteredReader<std::string>(name, *reader), std::string(128, 'q'));
    EXPECT_EQ(LastStream().getPositionProperty(), static_cast<int64_t>(bytes.size()));
}

// Little-endian integers with every byte distinct (and sign bits set) catch byte-order and
// sign-extension slips that small symmetric round-trip values cannot.
TEST_F(PrimitiveAndMathReaderTest, IntegerReadersDecodeLittleEndianLiteralBytes)
{
    auto reader = MakeLiteralReader({
        0xfe, 0xca,                                     // Int16  0xcafe -> -13570
        0xfe, 0xca,                                     // UInt16 0xcafe -> 51966
        0x78, 0x56, 0x34, 0x92,                         // Int32  0x92345678
        0x78, 0x56, 0x34, 0x92,                         // UInt32 0x92345678
        0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x81, // Int64  0x8123456789abcdef
        0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x81, // UInt64 0x8123456789abcdef
        0x80});                                         // SByte  -128
    const std::string ns = "Microsoft.Xna.Framework.Content.";
    EXPECT_EQ(ReadViaRegisteredReader<int16_t>(ns + "Int16Reader", *reader), int16_t{-13570});
    EXPECT_EQ(ReadViaRegisteredReader<uint16_t>(ns + "UInt16Reader", *reader), uint16_t{0xcafe});
    EXPECT_EQ(ReadViaRegisteredReader<int32_t>(ns + "Int32Reader", *reader), static_cast<int32_t>(0x92345678u));
    EXPECT_EQ(ReadViaRegisteredReader<uint32_t>(ns + "UInt32Reader", *reader), 0x92345678u);
    EXPECT_EQ(ReadViaRegisteredReader<int64_t>(ns + "Int64Reader", *reader),
              static_cast<int64_t>(0x8123456789abcdefULL));
    EXPECT_EQ(ReadViaRegisteredReader<uint64_t>(ns + "UInt64Reader", *reader), 0x8123456789abcdefULL);
    EXPECT_EQ(ReadViaRegisteredReader<int8_t>(ns + "SByteReader", *reader), int8_t{-128});
}
