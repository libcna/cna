// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnb.md XNB-18A/XNB-19: unit tests for the primitive and math .xnb readers -- registration
// under the real FNA canonical name, and correct field-order round-trips via ReadUntyped().

#include <any>
#include <gtest/gtest.h>

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

    private:
        std::vector<std::unique_ptr<System::IO::MemoryStream>> streams_;
    };

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
