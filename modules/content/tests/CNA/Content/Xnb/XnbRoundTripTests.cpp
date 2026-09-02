// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-006/007/008/009: every built-in primitive, math, system and
// collection type written by CNA's .xnb writer is read back by CNA's own unmodified runtime
// reader, through a real container. A writer that is not the reader's inverse fails here.

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"
#include "CNA/Internal/Xnb/CollectionContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/XnbBuiltInReaders.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "System/IO/MemoryStream.hpp"

using namespace CNA::Content::Xnb;
using namespace Microsoft::Xna::Framework;
using Microsoft::Xna::Framework::Content::ContentReader;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class XnbRoundTripTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterAllBuiltInXnbReaders();
            RegisterBuiltInXnbTypeWriters(registry_);
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }

        /** @brief Writes @p value as a root asset and reads it back through the runtime reader. */
        template <typename TWrite, typename TRead = TWrite>
        [[nodiscard]] TRead RoundTrip(const TWrite& value)
        {
            return RoundTripAs<TRead>(XnbTypeKey<TWrite>::Name(), std::any(value));
        }

        /** @brief Writes a boxed root of @p typeName and reads it back as `TRead`. */
        template <typename TRead>
        [[nodiscard]] TRead RoundTripAs(const std::string& typeName, const std::any& value)
        {
            const std::vector<std::uint8_t> file =
                WriteXnbFile(registry_, options_, typeName, value);
            EXPECT_GE(file.size(), 10u);
            System::IO::MemoryStream body(file.data() + 10,
                                          static_cast<std::int32_t>(file.size() - 10u));
            ContentReader reader(nullptr, &body, "roundtrip", options_.version,
                                 XnbPlatformByte(options_.platform));
            return reader.ReadAsset<TRead>();
        }

        XnbTypeWriterRegistry registry_;
        XnbFileOptions options_{};
    };
}

// -- Primitives --

TEST_F(XnbRoundTripTest, IntegralPrimitivesSurviveTheirExtremes)
{
    EXPECT_EQ(RoundTrip<std::uint8_t>(0u), 0u);
    EXPECT_EQ(RoundTrip<std::uint8_t>(255u), 255u);
    EXPECT_EQ(RoundTrip<std::int8_t>(-128), -128);
    EXPECT_EQ(RoundTrip<std::int8_t>(127), 127);
    EXPECT_EQ(RoundTrip<std::int16_t>(-32768), -32768);
    EXPECT_EQ(RoundTrip<std::uint16_t>(65535u), 65535u);
    EXPECT_EQ(RoundTrip<std::int32_t>(-2147483647 - 1), -2147483647 - 1);
    EXPECT_EQ(RoundTrip<std::uint32_t>(4294967295u), 4294967295u);
    EXPECT_EQ(RoundTrip<std::int64_t>(-9223372036854775807LL - 1),
              -9223372036854775807LL - 1);
    EXPECT_EQ(RoundTrip<std::uint64_t>(18446744073709551615ULL), 18446744073709551615ULL);
}

TEST_F(XnbRoundTripTest, FloatingPointPrimitivesKeepTheirExactBitPattern)
{
    EXPECT_FLOAT_EQ(RoundTrip<float>(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(RoundTrip<float>(-1.5f), -1.5f);
    EXPECT_FLOAT_EQ(RoundTrip<float>(3.4028235e38f), 3.4028235e38f);
    EXPECT_DOUBLE_EQ(RoundTrip<double>(2.718281828459045), 2.718281828459045);
    EXPECT_DOUBLE_EQ(RoundTrip<double>(-1.7976931348623157e308), -1.7976931348623157e308);
}

TEST_F(XnbRoundTripTest, BooleanCharAndStringSurvive)
{
    EXPECT_TRUE(RoundTrip<bool>(true));
    EXPECT_FALSE(RoundTrip<bool>(false));
    EXPECT_EQ(RoundTrip<char16_t>(u'A'), u'A');
    EXPECT_EQ(RoundTrip<char16_t>(u'ž'), u'ž');
    EXPECT_EQ(RoundTrip<char16_t>(u'中'), u'中');
    EXPECT_EQ(RoundTrip<std::string>(""), "");
    EXPECT_EQ(RoundTrip<std::string>("hello"), "hello");
    EXPECT_EQ(RoundTrip<std::string>("\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD"),
              "\xC5\xBElu\xC5\xA5ou\xC4\x8Dk\xC3\xBD");
}

// -- Math value types --

TEST_F(XnbRoundTripTest, VectorsMatrixAndQuaternionKeepEveryComponentInOrder)
{
    const Vector2 v2(1.0f, 2.0f);
    const Vector2 r2 = RoundTrip<Vector2>(v2);
    EXPECT_FLOAT_EQ(r2.X, 1.0f);
    EXPECT_FLOAT_EQ(r2.Y, 2.0f);

    const Vector3 r3 = RoundTrip<Vector3>(Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(r3.X, 1.0f);
    EXPECT_FLOAT_EQ(r3.Y, 2.0f);
    EXPECT_FLOAT_EQ(r3.Z, 3.0f);

    const Vector4 r4 = RoundTrip<Vector4>(Vector4(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_FLOAT_EQ(r4.X, 1.0f);
    EXPECT_FLOAT_EQ(r4.W, 4.0f);

    Matrix m;
    float next = 1.0f;
    for (float* field : {&m.M11, &m.M12, &m.M13, &m.M14, &m.M21, &m.M22, &m.M23, &m.M24,
                         &m.M31, &m.M32, &m.M33, &m.M34, &m.M41, &m.M42, &m.M43, &m.M44})
    {
        *field = next;
        next += 1.0f;
    }
    const Matrix rm = RoundTrip<Matrix>(m);
    EXPECT_FLOAT_EQ(rm.M11, 1.0f);
    EXPECT_FLOAT_EQ(rm.M12, 2.0f);
    EXPECT_FLOAT_EQ(rm.M21, 5.0f);
    EXPECT_FLOAT_EQ(rm.M44, 16.0f);

    const Quaternion rq = RoundTrip<Quaternion>(Quaternion(0.1f, 0.2f, 0.3f, 0.4f));
    EXPECT_FLOAT_EQ(rq.X, 0.1f);
    EXPECT_FLOAT_EQ(rq.W, 0.4f);
}

TEST_F(XnbRoundTripTest, ColorIsWrittenAsRedGreenBlueAlphaBytes)
{
    const Color result = RoundTrip<Color>(Color(10, 20, 30, 40));
    EXPECT_EQ(result.getRProperty(), 10);
    EXPECT_EQ(result.getGProperty(), 20);
    EXPECT_EQ(result.getBProperty(), 30);
    EXPECT_EQ(result.getAProperty(), 40);
}

TEST_F(XnbRoundTripTest, TheRemainingMathValueTypesSurvive)
{
    Plane plane;
    plane.Normal = Vector3(0.0f, 1.0f, 0.0f);
    plane.D = 5.0f;
    const Plane rp = RoundTrip<Plane>(plane);
    EXPECT_FLOAT_EQ(rp.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(rp.D, 5.0f);

    const Point rpt = RoundTrip<Point>(Point(-3, 7));
    EXPECT_EQ(rpt.X, -3);
    EXPECT_EQ(rpt.Y, 7);

    const Rectangle rr = RoundTrip<Rectangle>(Rectangle(1, 2, 3, 4));
    EXPECT_EQ(rr.X, 1);
    EXPECT_EQ(rr.Y, 2);
    EXPECT_EQ(rr.Width, 3);
    EXPECT_EQ(rr.Height, 4);

    BoundingBox box;
    box.Min = Vector3(-1.0f, -2.0f, -3.0f);
    box.Max = Vector3(1.0f, 2.0f, 3.0f);
    const BoundingBox rb = RoundTrip<BoundingBox>(box);
    EXPECT_FLOAT_EQ(rb.Min.X, -1.0f);
    EXPECT_FLOAT_EQ(rb.Max.Z, 3.0f);

    BoundingSphere sphere;
    sphere.Center = Vector3(1.0f, 2.0f, 3.0f);
    sphere.Radius = 4.0f;
    const BoundingSphere rs = RoundTrip<BoundingSphere>(sphere);
    EXPECT_FLOAT_EQ(rs.Center.Y, 2.0f);
    EXPECT_FLOAT_EQ(rs.Radius, 4.0f);

    Ray ray;
    ray.Position = Vector3(1.0f, 0.0f, 0.0f);
    ray.Direction = Vector3(0.0f, 0.0f, 1.0f);
    const Ray rray = RoundTrip<Ray>(ray);
    EXPECT_FLOAT_EQ(rray.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(rray.Direction.Z, 1.0f);
}

TEST_F(XnbRoundTripTest, BoundingFrustumIsWrittenAsItsMatrix)
{
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f),
                                             Vector3(0.0f, 1.0f, 0.0f));
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(1.0f, 1.3333f, 0.1f, 100.0f);
    const BoundingFrustum frustum(view * projection);
    const BoundingFrustum result = RoundTrip<BoundingFrustum>(frustum);
    EXPECT_FLOAT_EQ(result.getMatrixProperty().M11, frustum.getMatrixProperty().M11);
    EXPECT_FLOAT_EQ(result.getMatrixProperty().M44, frustum.getMatrixProperty().M44);
}

// -- System types --

TEST_F(XnbRoundTripTest, TimeSpanAndDateTimeSurviveAsTickCounts)
{
    const System::TimeSpan span(1234567890LL);
    EXPECT_EQ(RoundTrip<System::TimeSpan>(span).getTicksProperty(), 1234567890LL);

    const System::DateTime moment(638000000000000000LL);
    EXPECT_EQ(RoundTrip<System::DateTime>(moment).getTicksProperty(), 638000000000000000LL);
}

#if SHARP_RUNTIME_HAS_NATIVE_INT128
TEST_F(XnbRoundTripTest, DecimalSurvivesItsFourWordBitPattern)
{
    const System::Decimal value(123456789, 0, 0, true, 4);
    const System::Decimal result = RoundTrip<System::Decimal>(value);
    EXPECT_EQ(result.getScaleProperty(), value.getScaleProperty());
    EXPECT_EQ(result.getIsNegativeProperty(), value.getIsNegativeProperty());
    EXPECT_TRUE(result == value);
}
#endif

TEST_F(XnbRoundTripTest, CurveKeepsItsLoopModesAndEveryKeyField)
{
    Curve curve;
    curve.setPreLoopProperty(CurveLoopType::Cycle);
    curve.setPostLoopProperty(CurveLoopType::Oscillate);
    curve.getKeysProperty().Add(CurveKey(0.0f, 1.0f, 2.0f, 3.0f, CurveContinuity::Smooth));
    curve.getKeysProperty().Add(CurveKey(1.0f, 4.0f, 5.0f, 6.0f, CurveContinuity::Step));

    const Curve result = RoundTrip<Curve>(curve);
    EXPECT_EQ(result.getPreLoopProperty(), CurveLoopType::Cycle);
    EXPECT_EQ(result.getPostLoopProperty(), CurveLoopType::Oscillate);
    ASSERT_EQ(result.getKeysProperty().getCountProperty(), 2);
    EXPECT_FLOAT_EQ(result.getKeysProperty()[0].getPositionProperty(), 0.0f);
    EXPECT_FLOAT_EQ(result.getKeysProperty()[0].getTangentInProperty(), 2.0f);
    EXPECT_FLOAT_EQ(result.getKeysProperty()[0].getTangentOutProperty(), 3.0f);
    EXPECT_EQ(result.getKeysProperty()[1].getContinuityProperty(), CurveContinuity::Step);
    EXPECT_FLOAT_EQ(result.getKeysProperty()[1].getValueProperty(), 4.0f);
}

// -- Collections --

TEST_F(XnbRoundTripTest, AListOfValueTypesRoundTrips)
{
    ContentTypeReaderManager::AddTypeCreator(
        XnbListReaderName("Microsoft.Xna.Framework.Rectangle"),
        []
        {
            return std::make_unique<CNA::Internal::Xnb::ListReader<Rectangle>>(
                "System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Rectangle]]",
                "Microsoft.Xna.Framework.Content.RectangleReader");
        });
    RegisterXnbListWriter(registry_, XnbTypeKey<Rectangle>::Name());

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<Rectangle>::Name();
    list.elements = {std::any(Rectangle(1, 2, 3, 4)), std::any(Rectangle(5, 6, 7, 8))};

    const auto result = RoundTripAs<std::vector<Rectangle>>(
        XnbListTypeName(XnbTypeKey<Rectangle>::Name()), std::any(list));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].Width, 3);
    EXPECT_EQ(result[1].X, 5);
}

TEST_F(XnbRoundTripTest, AListOfReferenceTypesCarriesAPerElementTypeIdentifier)
{
    ContentTypeReaderManager::AddTypeCreator(
        XnbListReaderName("System.String"),
        []
        {
            return std::make_unique<CNA::Internal::Xnb::ListReader<std::string>>(
                "System.Collections.Generic.List`1[[System.String]]",
                "Microsoft.Xna.Framework.Content.StringReader");
        });
    RegisterXnbListWriter(registry_, XnbTypeKey<std::string>::Name());

    XnbBoxedList list;
    list.elementTypeName = XnbTypeKey<std::string>::Name();
    list.elements = {std::any(std::string("alpha")), std::any(std::string("beta"))};

    const auto result = RoundTripAs<std::vector<std::string>>(
        XnbListTypeName(XnbTypeKey<std::string>::Name()), std::any(list));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "alpha");
    EXPECT_EQ(result[1], "beta");
}

TEST_F(XnbRoundTripTest, AnArrayRoundTripsThroughItsOwnReaderName)
{
    ContentTypeReaderManager::AddTypeCreator(
        XnbArrayReaderName("Microsoft.Xna.Framework.Vector3"),
        []
        {
            return std::make_unique<CNA::Internal::Xnb::ArrayReader<Vector3>>(
                "Microsoft.Xna.Framework.Vector3[]",
                "Microsoft.Xna.Framework.Content.Vector3Reader");
        });
    RegisterXnbArrayWriter(registry_, XnbTypeKey<Vector3>::Name());

    XnbBoxedList array;
    array.elementTypeName = XnbTypeKey<Vector3>::Name();
    array.elements = {std::any(Vector3(1.0f, 0.0f, 0.0f)), std::any(Vector3(0.0f, 2.0f, 0.0f))};

    const auto result = RoundTripAs<std::vector<Vector3>>(
        XnbArrayTypeName(XnbTypeKey<Vector3>::Name()), std::any(array));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_FLOAT_EQ(result[0].X, 1.0f);
    EXPECT_FLOAT_EQ(result[1].Y, 2.0f);
}

TEST_F(XnbRoundTripTest, ADictionaryRoundTripsEveryEntry)
{
    ContentTypeReaderManager::AddTypeCreator(
        XnbDictionaryReaderName("System.String", "System.Int32"),
        []
        {
            return std::make_unique<CNA::Internal::Xnb::DictionaryReader<std::string, std::int32_t>>(
                "System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
                "Microsoft.Xna.Framework.Content.StringReader",
                "Microsoft.Xna.Framework.Content.Int32Reader");
        });
    RegisterXnbDictionaryWriter(registry_, XnbTypeKey<std::string>::Name(),
                                XnbTypeKey<std::int32_t>::Name());

    XnbBoxedDictionary dictionary;
    dictionary.keyTypeName = XnbTypeKey<std::string>::Name();
    dictionary.valueTypeName = XnbTypeKey<std::int32_t>::Name();
    dictionary.entries = {{std::any(std::string("one")), std::any(std::int32_t{1})},
                          {std::any(std::string("two")), std::any(std::int32_t{2})}};

    const auto result = RoundTripAs<std::unordered_map<std::string, std::int32_t>>(
        XnbDictionaryTypeName(XnbTypeKey<std::string>::Name(), XnbTypeKey<std::int32_t>::Name()),
        std::any(dictionary));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result.at("one"), 1);
    EXPECT_EQ(result.at("two"), 2);
}

TEST_F(XnbRoundTripTest, ANullableRoundTripsBothStates)
{
    ContentTypeReaderManager::AddTypeCreator(
        XnbNullableReaderName("System.Char"),
        []
        {
            return std::make_unique<CNA::Internal::Xnb::NullableReader<char16_t>>(
                "System.Nullable`1[[System.Char]]",
                "Microsoft.Xna.Framework.Content.CharReader");
        });
    RegisterXnbNullableWriter(registry_, XnbTypeKey<char16_t>::Name());

    XnbBoxedNullable present;
    present.valueTypeName = XnbTypeKey<char16_t>::Name();
    present.value = std::any(char16_t{u'Z'});
    const auto withValue = RoundTripAs<std::optional<char16_t>>(
        XnbNullableTypeName(XnbTypeKey<char16_t>::Name()), std::any(present));
    ASSERT_TRUE(withValue.has_value());
    EXPECT_EQ(*withValue, u'Z');

    XnbBoxedNullable absent;
    absent.valueTypeName = XnbTypeKey<char16_t>::Name();
    const auto withoutValue = RoundTripAs<std::optional<char16_t>>(
        XnbNullableTypeName(XnbTypeKey<char16_t>::Name()), std::any(absent));
    EXPECT_FALSE(withoutValue.has_value());
}

TEST_F(XnbRoundTripTest, NullableRefusesAReferenceValueTypeAsDotNetDoes)
{
    EXPECT_THROW(RegisterXnbNullableWriter(registry_, XnbTypeKey<std::string>::Name()),
                 XnbWriteException);
}

TEST_F(XnbRoundTripTest, AnEnumIsWrittenAsItsThirtyTwoBitUnderlyingValue)
{
    RegisterXnbEnumWriter(registry_, "Microsoft.Xna.Framework.Graphics.SurfaceFormat");
    XnbBoxedEnum value;
    value.enumTypeName = "Microsoft.Xna.Framework.Graphics.SurfaceFormat";
    value.value = 4;   // Dxt1 in the XNA 4.0 numbering

    const std::vector<std::uint8_t> file = WriteXnbFile(
        registry_, options_, "Microsoft.Xna.Framework.Graphics.SurfaceFormat", std::any(value));
    System::IO::MemoryStream body(file.data() + 10,
                                  static_cast<std::int32_t>(file.size() - 10u));
    ContentReader reader(nullptr, &body, "enum", options_.version, 'w');
    // The enum reader is generic over T, so the payload is checked directly rather than through a
    // registered closed reader: one type-table entry, then the raw 32-bit value.
    EXPECT_EQ(reader.Read7BitEncodedInt(), 1);
    EXPECT_EQ(reader.ReadString(),
              XnbEnumReaderName("Microsoft.Xna.Framework.Graphics.SurfaceFormat"));
    EXPECT_EQ(reader.ReadInt32(), 0);          // reader version
    EXPECT_EQ(reader.Read7BitEncodedInt(), 0); // shared-resource count
    EXPECT_EQ(reader.Read7BitEncodedInt(), 1); // the root's type identifier
    EXPECT_EQ(reader.ReadInt32(), 4);
}
