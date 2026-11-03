// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <any>
#include <bit>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Design.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/Collections/Hashtable.hpp"
#include "System/ComponentModel/Design/Serialization/InstanceDescriptor.hpp"
#include "System/ComponentModel/TypeDescriptor.hpp"
#include "System/Globalization/CultureInfo.hpp"
#include "System/NotSupportedException.hpp"
#include "System/Reflection/ConstructorInfo.hpp"

namespace {

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Design;
using System::ComponentModel::Design::Serialization::InstanceDescriptor;

struct PropertyShape {
    std::string name;
    System::Type type;
};

template<class TValue, class TConverter>
void expectCommonBehavior(const TValue& value,
                          const std::vector<PropertyShape>& shape,
                          bool acceptsString,
                          std::size_t instanceDescriptorArgumentCount = 0) {
    static_assert(std::is_base_of_v<MathTypeConverter, TConverter>);
    const TConverter converter;

    const auto associated = System::ComponentModel::TypeDescriptor::GetConverter(
        System::Type::From<TValue>());
    ASSERT_NE(associated, nullptr);
    EXPECT_NE(dynamic_cast<TConverter*>(associated.get()), nullptr);

    EXPECT_EQ(converter.CanConvertFrom(System::Type::From<std::string>()), acceptsString);
    EXPECT_EQ(converter.CanConvertFrom(System::Type::From<const char*>()), acceptsString);
    EXPECT_EQ(converter.CanConvertFrom(System::Type::From<char*>()), acceptsString);
    EXPECT_EQ(converter.CanConvertFrom(System::Type::From<std::string_view>()), acceptsString);
    EXPECT_TRUE(converter.CanConvertFrom(System::Type::From<InstanceDescriptor>()));
    EXPECT_TRUE(converter.CanConvertTo(System::Type::From<std::string>()));
    EXPECT_TRUE(converter.CanConvertTo(System::Type::From<InstanceDescriptor>()));
    EXPECT_FALSE(converter.CanConvertTo(System::Type::From<SharpRuntime::intcs>()));
    EXPECT_TRUE(converter.GetPropertiesSupported());
    EXPECT_TRUE(converter.GetCreateInstanceSupported());

    const auto properties = converter.GetProperties(std::any(value));
    ASSERT_EQ(properties.getCountProperty(), static_cast<SharpRuntime::intcs>(shape.size()));
    System::Collections::Hashtable values;
    for (std::size_t i = 0; i < shape.size(); ++i) {
        const auto& property = properties.getItem(static_cast<SharpRuntime::intcs>(i));
        ASSERT_NE(property, nullptr);
        EXPECT_EQ(property->getNameProperty(), shape[i].name);
        EXPECT_EQ(property->getComponentTypeProperty(), System::Type::From<TValue>());
        EXPECT_EQ(property->getPropertyTypeProperty(), shape[i].type);
        EXPECT_FALSE(property->getIsReadOnlyProperty());
        EXPECT_FALSE(property->CanResetValue(std::any(value)));
        EXPECT_TRUE(property->ShouldSerializeValue(std::any(value)));
        values.setItem(shape[i].name, property->GetValue(std::any(value)));
    }

    const TValue created = std::any_cast<TValue>(converter.CreateInstance(values));
    EXPECT_TRUE(created == value);

    const std::any described = converter.ConvertTo(std::any(value), System::Type::From<InstanceDescriptor>());
    const auto* descriptor = std::any_cast<InstanceDescriptor>(&described);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(descriptor->getIsCompleteProperty());
    if (instanceDescriptorArgumentCount == 0) instanceDescriptorArgumentCount = shape.size();
    EXPECT_EQ(descriptor->getArgumentsProperty().size(), instanceDescriptorArgumentCount);
    EXPECT_TRUE(std::any_cast<TValue>(descriptor->Invoke()) == value);
    EXPECT_TRUE(std::any_cast<TValue>(converter.ConvertFrom(described)) == value);

    EXPECT_THROW((void)converter.ConvertTo(std::any(value), System::Type()), System::ArgumentNullException);
    EXPECT_THROW((void)converter.ConvertTo(std::any(value), System::Type::From<SharpRuntime::intcs>()),
                 System::NotSupportedException);
}

template<class TValue, class TConverter>
void expectInvariantStringRoundTrip(const TValue& value, const std::string& expected) {
    const TConverter converter;
    EXPECT_EQ(converter.ConvertToInvariantString(std::any(value)), expected);
    EXPECT_TRUE(std::any_cast<TValue>(converter.ConvertFromInvariantString(expected)) == value);
    EXPECT_THROW((void)converter.ConvertFromInvariantString("not a valid component list"),
                 System::ArgumentException);
    EXPECT_THROW((void)converter.ConvertFromInvariantString(expected + ", 0"),
                 System::ArgumentException);
}

template<class TValue, class TConverter>
void expectStringInputDisabled(const TValue& value) {
    const TConverter converter;
    EXPECT_FALSE(converter.CanConvertFrom(System::Type::From<std::string>()));
    EXPECT_THROW((void)converter.ConvertFromInvariantString("1, 2"), System::NotSupportedException);
    EXPECT_EQ(converter.ConvertToString(std::any(value)), value.ToString());
}

std::vector<PropertyShape> floatShape(std::initializer_list<const char*> names) {
    std::vector<PropertyShape> result;
    for (const char* name : names) result.push_back({name, System::Type::From<float>()});
    return result;
}

std::vector<PropertyShape> intShape(std::initializer_list<const char*> names) {
    std::vector<PropertyShape> result;
    for (const char* name : names) result.push_back({name, System::Type::From<SharpRuntime::intcs>()});
    return result;
}

} // namespace

TEST(MathTypeConverterTests, ImplementsTheExpandableXnaBaseContract) {
    const MathTypeConverter converter;
    static_assert(std::is_base_of_v<System::ComponentModel::ExpandableObjectConverter, MathTypeConverter>);
    EXPECT_TRUE(converter.CanConvertFrom(System::Type::From<std::string>()));
    EXPECT_FALSE(converter.CanConvertTo(System::Type::From<InstanceDescriptor>()));
    EXPECT_THROW((void)converter.ConvertTo(std::any(1), System::Type::From<InstanceDescriptor>()),
                 System::NotSupportedException);
    EXPECT_TRUE(converter.GetCreateInstanceSupported());
    EXPECT_TRUE(converter.GetPropertiesSupported());
    EXPECT_EQ(converter.GetProperties(std::any(1)).getCountProperty(), 0);
}

TEST(PointConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Point value(12, -7);
    expectCommonBehavior<Point, PointConverter>(value, intShape({"X", "Y"}), true);
    expectInvariantStringRoundTrip<Point, PointConverter>(value, "12, -7");
}

TEST(RectangleConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Rectangle value(1, 2, 30, 40);
    expectCommonBehavior<Rectangle, RectangleConverter>(
        value, intShape({"X", "Y", "Width", "Height"}), false);
    expectStringInputDisabled<Rectangle, RectangleConverter>(value);
}

TEST(Vector2ConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Vector2 value(1.5F, -2.25F);
    expectCommonBehavior<Vector2, Vector2Converter>(value, floatShape({"X", "Y"}), true);
    expectInvariantStringRoundTrip<Vector2, Vector2Converter>(value, "1.5, -2.25");
}

TEST(Vector3ConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Vector3 value(1.5F, 2.25F, -3.75F);
    expectCommonBehavior<Vector3, Vector3Converter>(value, floatShape({"X", "Y", "Z"}), true);
    expectInvariantStringRoundTrip<Vector3, Vector3Converter>(value, "1.5, 2.25, -3.75");

    const Vector3Converter converter;
    std::any boxed = value;
    const auto properties = converter.GetProperties(boxed);
    properties.getItem("X")->SetValue(boxed, std::any(9.0F));
    EXPECT_EQ(std::any_cast<Vector3>(boxed), Vector3(9.0F, 2.25F, -3.75F));
}

TEST(Vector3ConverterTests, UsesCultureDecimalAndListSeparators) {
    const Vector3Converter converter;
    const Vector3 value(1.5F, 2.25F, -3.75F);
    const System::Globalization::CultureInfo invariant("");
    const System::Globalization::CultureInfo english("en-US");
    const System::Globalization::CultureInfo czech("cs-CZ");

    EXPECT_EQ(converter.ConvertToString(nullptr, &invariant, std::any(value)), "1.5, 2.25, -3.75");
    EXPECT_EQ(converter.ConvertToString(nullptr, &english, std::any(value)), "1.5, 2.25, -3.75");
    EXPECT_EQ(converter.ConvertToString(nullptr, &czech, std::any(value)), "1,5; 2,25; -3,75");
    EXPECT_EQ(std::any_cast<Vector3>(converter.ConvertFromString(nullptr, &czech, " 1,5; 2,25; -3,75 ")), value);
    EXPECT_THROW((void)converter.ConvertFromString(nullptr, &czech, "1,5, 2,25, -3,75"),
                 System::ArgumentException);
}

TEST(Vector4ConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Vector4 value(1.0F, 2.0F, 3.0F, 4.0F);
    expectCommonBehavior<Vector4, Vector4Converter>(value, floatShape({"X", "Y", "Z", "W"}), true);
    expectInvariantStringRoundTrip<Vector4, Vector4Converter>(value, "1, 2, 3, 4");
}

TEST(QuaternionConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Quaternion value(0.1F, 0.2F, 0.3F, 0.4F);
    expectCommonBehavior<Quaternion, QuaternionConverter>(value, floatShape({"X", "Y", "Z", "W"}), true);
    expectInvariantStringRoundTrip<Quaternion, QuaternionConverter>(value, "0.1, 0.2, 0.3, 0.4");
}

TEST(MatrixConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Matrix value(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    auto shape = floatShape({"M11", "M12", "M13", "M14", "M21", "M22", "M23", "M24",
                             "M31", "M32", "M33", "M34", "M41", "M42", "M43", "M44"});
    shape.insert(shape.begin(), {"Translation", System::Type::From<Vector3>()});
    expectCommonBehavior<Matrix, MatrixConverter>(value, shape, false, 16);
    expectStringInputDisabled<Matrix, MatrixConverter>(value);
}

TEST(MatrixConverterTests, CreateInstanceUsesEveryElementInRowMajorOrder) {
    MatrixConverter converter;
    System::Collections::Hashtable values;
    SharpRuntime::intcs next = 1;
    for (const auto& property : converter.GetProperties(std::any(Matrix{}))) {
        if (property->getNameProperty() == "Translation") continue;
        values.setItem(property->getNameProperty(), std::any(static_cast<float>(next++)));
    }
    values.setItem("M34", std::any(99.0F));
    const Matrix result = std::any_cast<Matrix>(converter.CreateInstance(values));
    EXPECT_FLOAT_EQ(result.M11, 1.0F);
    EXPECT_FLOAT_EQ(result.M24, 8.0F);
    EXPECT_FLOAT_EQ(result.M34, 99.0F);
    EXPECT_FLOAT_EQ(result.M44, 16.0F);
}

TEST(MatrixConverterTests, TranslationDescriptorEditsTheTranslationRowButIsNotAConstructorArgument) {
    MatrixConverter converter;
    std::any boxed = Matrix::getIdentityProperty();
    const auto properties = converter.GetProperties(boxed);
    ASSERT_EQ(properties.getCountProperty(), 17);
    const auto translation = properties.getItem(0);
    ASSERT_NE(translation, nullptr);
    EXPECT_EQ(translation->getNameProperty(), "Translation");
    EXPECT_EQ(std::any_cast<Vector3>(translation->GetValue(boxed)), Vector3::Zero);
    translation->SetValue(boxed, std::any(Vector3(7.0F, 8.0F, 9.0F)));
    const Matrix changed = std::any_cast<Matrix>(boxed);
    EXPECT_EQ(changed.getTranslationProperty(), Vector3(7.0F, 8.0F, 9.0F));

    const auto descriptor = std::any_cast<InstanceDescriptor>(
        converter.ConvertTo(boxed, System::Type::From<InstanceDescriptor>()));
    EXPECT_EQ(descriptor.getArgumentsProperty().size(), 16);
    EXPECT_EQ(std::any_cast<Matrix>(descriptor.Invoke()), changed);
}

TEST(BoundingBoxConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const BoundingBox value(Vector3(-1, -2, -3), Vector3(4, 5, 6));
    expectCommonBehavior<BoundingBox, BoundingBoxConverter>(value,
        {{"Min", System::Type::From<Vector3>()}, {"Max", System::Type::From<Vector3>()}}, false);
    expectStringInputDisabled<BoundingBox, BoundingBoxConverter>(value);
}

TEST(BoundingSphereConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const BoundingSphere value(Vector3(1, 2, 3), 4.5F);
    expectCommonBehavior<BoundingSphere, BoundingSphereConverter>(value,
        {{"Center", System::Type::From<Vector3>()}, {"Radius", System::Type::From<float>()}}, false);
    expectStringInputDisabled<BoundingSphere, BoundingSphereConverter>(value);
}

TEST(PlaneConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Plane value(Vector3(1, 2, 3), 4.0F);
    expectCommonBehavior<Plane, PlaneConverter>(value,
        {{"Normal", System::Type::From<Vector3>()}, {"D", System::Type::From<float>()}}, false);
    expectStringInputDisabled<Plane, PlaneConverter>(value);
}

TEST(RayConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Ray value(Vector3(1, 2, 3), Vector3(4, 5, 6));
    expectCommonBehavior<Ray, RayConverter>(value,
        {{"Position", System::Type::From<Vector3>()}, {"Direction", System::Type::From<Vector3>()}}, false);
    expectStringInputDisabled<Ray, RayConverter>(value);
}

TEST(ColorConverterTests, ImplementsXnaConversionDescriptorAndCreationBehavior) {
    const Color value(SharpRuntime::bytecs{10}, SharpRuntime::bytecs{20},
                      SharpRuntime::bytecs{30}, SharpRuntime::bytecs{40});
    const auto byteType = System::Type::From<SharpRuntime::bytecs>();
    expectCommonBehavior<Color, ColorConverter>(value,
        {{"R", byteType}, {"G", byteType}, {"B", byteType}, {"A", byteType}}, true);
    expectInvariantStringRoundTrip<Color, ColorConverter>(value, "10, 20, 30, 40");
}

TEST(ColorConverterTests, RejectsComponentsOutsideTheByteRange) {
    const ColorConverter converter;
    EXPECT_THROW((void)converter.ConvertFromInvariantString("0, 1, 2, 256"), System::ArgumentException);
    EXPECT_THROW((void)converter.ConvertFromInvariantString("0, 1, -1, 2"), System::ArgumentException);
}

// ── The XNA component-list string contract ──────────────────────────────────────────────────
//
// Oracle: Microsoft.Xna.Framework.dll (XNA 4.0), MathTypeConverter.ConvertToValues<T>:
//   IL_000e  the text is String.Trim()med;
//   IL_0014  a null culture becomes CultureInfo.CurrentCulture;
//   IL_0039  String.Split(new[] { TextInfo.ListSeparator }, StringSplitOptions.None);
//   IL_005f..IL_00c2  every part goes through the component type's converter BEFORE the count
//            is compared; a converter failure is rethrown as ArgumentException(message, inner);
//   IL_00c4..IL_0101  a count mismatch throws ArgumentException(message), with no inner exception.
// ConvertFromValues joins the converted components with ListSeparator + " ".

namespace {

std::optional<System::ArgumentException> CaptureArgumentException(const std::function<void()>& action) {
    try {
        action();
    } catch (const System::ArgumentException& exception) {
        return exception;
    } catch (...) {
        ADD_FAILURE() << "threw something other than System::ArgumentException";
        return std::nullopt;
    }
    ADD_FAILURE() << "did not throw";
    return std::nullopt;
}

void ExpectCountMismatch(const System::ComponentModel::TypeConverter& converter, const std::string& text) {
    SCOPED_TRACE("text: \"" + text + "\"");
    const auto thrown = CaptureArgumentException([&] { (void)converter.ConvertFromInvariantString(text); });
    ASSERT_TRUE(thrown.has_value());
    EXPECT_EQ(thrown->getInnerExceptionProperty(), nullptr)
        << "a component-count mismatch is reported without an inner exception (IL_00fc)";
}

void ExpectComponentParseFailure(const System::ComponentModel::TypeConverter& converter, const std::string& text) {
    SCOPED_TRACE("text: \"" + text + "\"");
    const auto thrown = CaptureArgumentException([&] { (void)converter.ConvertFromInvariantString(text); });
    ASSERT_TRUE(thrown.has_value());
    EXPECT_NE(thrown->getInnerExceptionProperty(), nullptr)
        << "a component the element converter refuses is wrapped with that failure as inner (IL_00b2)";
}

Color ByteColor(int r, int g, int b, int a) {
    return Color(static_cast<SharpRuntime::bytecs>(r), static_cast<SharpRuntime::bytecs>(g),
                 static_cast<SharpRuntime::bytecs>(b), static_cast<SharpRuntime::bytecs>(a));
}

/// Runs @p body on a fresh thread whose current culture is @p culture. CultureInfo's current
/// culture is per thread, so nothing leaks into the other tests of the process.
void WithCurrentCulture(const System::Globalization::CultureInfo& culture, const std::function<void()>& body) {
    std::thread([&] {
        System::Globalization::CultureInfo::setCurrentCultureProperty(culture);
        // An exception escaping a std::thread terminates the process; report it as a failure.
        try {
            body();
        } catch (const std::exception& exception) {
            ADD_FAILURE() << "unexpected exception: " << exception.what();
        } catch (...) {
            ADD_FAILURE() << "unexpected non-standard exception";
        }
    }).join();
}

} // namespace

TEST(MathTypeConverterStringTests, AWrongComponentCountIsAnArgumentExceptionWithoutAnInnerException) {
    ExpectCountMismatch(PointConverter(), "1");
    ExpectCountMismatch(PointConverter(), "1, 2, 3");
    ExpectCountMismatch(Vector2Converter(), "1");
    ExpectCountMismatch(Vector3Converter(), "1, 2");
    ExpectCountMismatch(Vector4Converter(), "1, 2, 3");
    ExpectCountMismatch(QuaternionConverter(), "1, 2, 3, 4, 5");
    ExpectCountMismatch(ColorConverter(), "1, 2, 3");
}

TEST(MathTypeConverterStringTests, AnUnparsableComponentIsWrappedEvenWhenTheCountIsAlsoWrong) {
    ExpectComponentParseFailure(PointConverter(), "1, x");
    // Three parts, the third unparsable: XNA parses every part before it counts them, so this is
    // the parse failure (with inner), not the count mismatch.
    ExpectComponentParseFailure(PointConverter(), "1, 2, x");
    ExpectComponentParseFailure(PointConverter(), "1.5, 2");          // Int32 takes no fraction
    ExpectComponentParseFailure(PointConverter(), "2147483648, 0");   // Int32 overflow
    ExpectComponentParseFailure(Vector3Converter(), "1, 2, three");
    ExpectComponentParseFailure(ColorConverter(), "0, 1, 2, 256");    // Byte overflow
    ExpectComponentParseFailure(ColorConverter(), "1, 2, 3, x, 5");
}

TEST(MathTypeConverterStringTests, EmptyTextAndEmptyComponentsReachTheElementConverter) {
    // StringSplitOptions.None keeps empty parts, and an element converter refuses "".
    ExpectComponentParseFailure(PointConverter(), "");
    ExpectComponentParseFailure(PointConverter(), "   \t ");
    ExpectComponentParseFailure(PointConverter(), "1, 2,");
    ExpectComponentParseFailure(PointConverter(), ",1, 2");
    ExpectComponentParseFailure(Vector3Converter(), "1,,2");
    ExpectComponentParseFailure(Vector2Converter(), "1, ");
}

TEST(MathTypeConverterStringTests, WhitespaceAroundTheTextAndAroundEachComponentIsIgnored) {
    // The whole text is trimmed (IL_000e) and each number converter trims its own part.
    EXPECT_EQ(std::any_cast<Point>(PointConverter().ConvertFromInvariantString("\t 12 ,\n -7 \r\n")),
              Point(12, -7));
    EXPECT_EQ(std::any_cast<Vector2>(Vector2Converter().ConvertFromInvariantString("  1.5 ,   -2.25  ")),
              Vector2(1.5F, -2.25F));
    EXPECT_TRUE(std::any_cast<Color>(ColorConverter().ConvertFromInvariantString(" 10,20 ,30,  40 "))
                == ByteColor(10, 20, 30, 40));
}

TEST(MathTypeConverterStringTests, IntegerComponentsAcceptTheNumberConvertersHexForms) {
    // .NET BaseNumberConverter.ConvertFrom: with AllowHex (Int32Converter, ByteConverter) a "#"
    // or "0x"/"0X" prefix parses base 16. SingleConverter sets AllowHex false.
    EXPECT_EQ(std::any_cast<Point>(PointConverter().ConvertFromInvariantString("0x10, #20")), Point(16, 32));
    EXPECT_TRUE(std::any_cast<Color>(ColorConverter().ConvertFromInvariantString("#0A, 0x14, 30, 0X28"))
                == ByteColor(10, 20, 30, 40));
    ExpectComponentParseFailure(Vector2Converter(), "0x10, 1");
}

TEST(MathTypeConverterStringTests, ANullCultureMeansTheCallingThreadsCurrentCulture) {
    // cs-CZ: list separator ";" and decimal separator ",". The invariant entry points pass the
    // invariant culture explicitly and must not follow the thread.
    WithCurrentCulture(System::Globalization::CultureInfo("cs-CZ"), [] {
        const Vector2Converter vectors;
        const Vector2 value(1.5F, -2.25F);
        EXPECT_EQ(vectors.ConvertToString(nullptr, nullptr, std::any(value)), "1,5; -2,25");
        EXPECT_EQ(std::any_cast<Vector2>(vectors.ConvertFromString(nullptr, nullptr, "1,5; -2,25")), value);
        EXPECT_EQ(std::any_cast<Point>(PointConverter().ConvertFromString(nullptr, nullptr, "3; 4")), Point(3, 4));
        EXPECT_THROW((void)PointConverter().ConvertFromString(nullptr, nullptr, "3, 4"), System::ArgumentException);

        EXPECT_EQ(vectors.ConvertToInvariantString(std::any(value)), "1.5, -2.25");
        EXPECT_EQ(std::any_cast<Vector2>(vectors.ConvertFromInvariantString("1.5, -2.25")), value);
    });
}

TEST(MathTypeConverterStringTests, TheCultureListSeparatorSplitsAndJoinsEveryComponentType) {
    const System::Globalization::CultureInfo german("de-DE");   // ";" and ","
    const System::Globalization::CultureInfo english("en-US");  // "," and "."
    const PointConverter points;
    EXPECT_EQ(points.ConvertToString(nullptr, &german, std::any(Point(12, -7))), "12; -7");
    EXPECT_EQ(std::any_cast<Point>(points.ConvertFromString(nullptr, &german, "12; -7")), Point(12, -7));
    EXPECT_THROW((void)points.ConvertFromString(nullptr, &german, "12, -7"), System::ArgumentException);
    EXPECT_THROW((void)points.ConvertFromString(nullptr, &english, "12; -7"), System::ArgumentException);

    EXPECT_EQ(ColorConverter().ConvertToString(nullptr, &german, std::any(ByteColor(10, 20, 30, 40))),
              "10; 20; 30; 40");
    const QuaternionConverter quaternions;
    const Quaternion quaternion(0.5F, -1.25F, 2.0F, 4.0F);
    EXPECT_EQ(quaternions.ConvertToString(nullptr, &german, std::any(quaternion)), "0,5; -1,25; 2; 4");
    EXPECT_EQ(std::any_cast<Quaternion>(quaternions.ConvertFromString(nullptr, &german, "0,5; -1,25; 2; 4")),
              quaternion);
}

TEST(MathTypeConverterStringTests, ComponentTextRoundTripsEveryBit) {
    // SingleConverter formats with the round-trip "R" specifier. Each value needs more than
    // seven significant digits, so a "G"/printf-style rendering would not reproduce it:
    // 1/3, 0.1, 2^24 + 2 = 16777218, FLT_MAX, the smallest normal float, -2/7.
    const float awkward[] = {1.0F / 3.0F, 0.1F, 16777218.0F, std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::min(), -2.0F / 7.0F};
    const Vector4Converter converter;
    for (const float component : awkward) {
        const Vector4 value(component, -component, 0.0F, 1.0F);
        const std::string text = converter.ConvertToInvariantString(std::any(value));
        const Vector4 parsed = std::any_cast<Vector4>(converter.ConvertFromInvariantString(text));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(parsed.X), std::bit_cast<std::uint32_t>(value.X)) << text;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(parsed.Y), std::bit_cast<std::uint32_t>(value.Y)) << text;
    }

    const PointConverter points;
    const Point extremes(std::numeric_limits<SharpRuntime::intcs>::min(),
                         std::numeric_limits<SharpRuntime::intcs>::max());
    EXPECT_EQ(points.ConvertToInvariantString(std::any(extremes)), "-2147483648, 2147483647");
    EXPECT_EQ(std::any_cast<Point>(points.ConvertFromInvariantString("-2147483648, 2147483647")), extremes);
    EXPECT_TRUE(std::any_cast<Color>(ColorConverter().ConvertFromInvariantString("0, 255, 0, 255"))
                == ByteColor(0, 255, 0, 255));
}

TEST(MathTypeConverterStringTests, NonStringSourcesFallThroughToTheBaseConverter) {
    // ConvertToValues returns null for anything that is not a string (IL_0001 isinst String), so
    // ConvertFrom reaches TypeConverter.ConvertFrom, which accepts only an InstanceDescriptor.
    const auto expectRefused = [](const System::ComponentModel::TypeConverter& converter) {
        EXPECT_THROW((void)converter.ConvertFrom(std::any(SharpRuntime::intcs{5})), System::NotSupportedException);
        EXPECT_THROW((void)converter.ConvertFrom(std::any()), System::NotSupportedException);
        EXPECT_THROW((void)converter.ConvertFrom(std::any(Vector3(1.0F, 2.0F, 3.0F))), System::NotSupportedException);
    };
    expectRefused(PointConverter());
    expectRefused(Vector2Converter());
    expectRefused(Vector4Converter());
    expectRefused(QuaternionConverter());
    expectRefused(ColorConverter());
}

TEST(MathTypeConverterStringTests, NullOrForeignValuesFallThroughToTheBaseConverter) {
    // TypeConverter.ConvertTo(null, typeof(string)) is String.Empty; no converter builds an
    // InstanceDescriptor for a value that is not its own type.
    EXPECT_EQ(PointConverter().ConvertToString(std::any()), "");
    EXPECT_EQ(Vector3Converter().ConvertToString(std::any()), "");
    EXPECT_EQ(ColorConverter().ConvertToString(std::any()), "");
    EXPECT_EQ(MatrixConverter().ConvertToString(std::any()), "");
    const auto descriptorType = System::Type::From<InstanceDescriptor>();
    EXPECT_THROW((void)Vector2Converter().ConvertTo(std::any(Point(1, 2)), descriptorType),
                 System::NotSupportedException);
    EXPECT_THROW((void)PointConverter().ConvertTo(std::any(), descriptorType), System::NotSupportedException);
    EXPECT_THROW((void)MatrixConverter().ConvertTo(std::any(Vector4()), descriptorType),
                 System::NotSupportedException);
}

TEST(MathTypeConverterStringTests, StructuredTypesDisplayTheXnaToStringText) {
    // These converters clear supportStringConvert and do not override ConvertTo(string), so XNA
    // answers with TypeConverter.ConvertTo -> value.ToString() (none of them is IFormattable, so a
    // culture argument is ignored and the current culture formats). Expected texts are the
    // string.Format patterns of XNA's Rectangle/BoundingBox/BoundingSphere/Plane/Ray/Matrix.ToString.
    WithCurrentCulture(System::Globalization::CultureInfo::getInvariantCultureProperty(), [] {
        EXPECT_EQ(RectangleConverter().ConvertToString(std::any(Rectangle(1, 2, 30, 40))),
                  "{X:1 Y:2 Width:30 Height:40}");
        EXPECT_EQ(BoundingBoxConverter().ConvertToString(
                      std::any(BoundingBox(Vector3(-1.0F, -2.0F, -3.0F), Vector3(4.0F, 5.0F, 6.0F)))),
                  "{Min:{X:-1 Y:-2 Z:-3} Max:{X:4 Y:5 Z:6}}");
        EXPECT_EQ(BoundingSphereConverter().ConvertToString(
                      std::any(BoundingSphere(Vector3(1.0F, 2.0F, 3.0F), 4.5F))),
                  "{Center:{X:1 Y:2 Z:3} Radius:4.5}");
        EXPECT_EQ(PlaneConverter().ConvertToString(std::any(Plane(Vector3(1.0F, 2.0F, 3.0F), 4.0F))),
                  "{Normal:{X:1 Y:2 Z:3} D:4}");
        EXPECT_EQ(RayConverter().ConvertToString(
                      std::any(Ray(Vector3(1.0F, 2.0F, 3.0F), Vector3(4.0F, 5.0F, 6.0F)))),
                  "{Position:{X:1 Y:2 Z:3} Direction:{X:4 Y:5 Z:6}}");
        EXPECT_EQ(MatrixConverter().ConvertToString(
                      std::any(Matrix(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16))),
                  "{ {M11:1 M12:2 M13:3 M14:4} {M21:5 M22:6 M23:7 M24:8} "
                  "{M31:9 M32:10 M33:11 M34:12} {M41:13 M42:14 M43:15 M44:16} }");

        const System::Globalization::CultureInfo german("de-DE");
        EXPECT_EQ(BoundingSphereConverter().ConvertToString(
                      nullptr, &german, std::any(BoundingSphere(Vector3(1.0F, 2.0F, 3.0F), 4.5F))),
                  "{Center:{X:1 Y:2 Z:3} Radius:4.5}");
    });
}

// ── InstanceDescriptor: XNA's typed constructors ────────────────────────────────────────────
//
// Oracle: every XNA converter's ConvertTo builds
//   new InstanceDescriptor(typeof(T).GetConstructor(new Type[] { ... }), new object[] { ... })
// with the parameter types below, boxing each argument as that parameter type, in field order.

namespace {

template<class TValue, class TConverter>
std::vector<std::any> ExpectTypedConstructor(const TValue& value, const std::vector<System::Type>& parameterTypes) {
    const TConverter converter;
    const auto descriptor = std::any_cast<InstanceDescriptor>(
        converter.ConvertTo(std::any(value), System::Type::From<InstanceDescriptor>()));
    const auto member = descriptor.getMemberInfoProperty();
    EXPECT_NE(member, nullptr);
    if (member == nullptr) return {};
    const auto* constructor = dynamic_cast<const System::Reflection::ConstructorInfo*>(member.get());
    EXPECT_NE(constructor, nullptr) << "the descriptor's member is not a constructor";
    if (constructor == nullptr) return {};
    EXPECT_EQ(constructor->getDeclaringTypeProperty(), System::Type::From<TValue>());
    const auto& parameters = constructor->GetParameters();
    const auto& arguments = descriptor.getArgumentsProperty();
    EXPECT_EQ(parameters.size(), parameterTypes.size());
    EXPECT_EQ(arguments.size(), parameterTypes.size());
    for (std::size_t i = 0; i < parameterTypes.size() && i < parameters.size() && i < arguments.size(); ++i) {
        EXPECT_EQ(parameters[i].getParameterTypeProperty(), parameterTypes[i]) << "parameter " << i;
        EXPECT_EQ(System::Type::FromTypeInfo(arguments[i].type()), parameterTypes[i]) << "argument " << i;
    }
    return arguments;
}

} // namespace

TEST(InstanceDescriptorContractTests, EachConverterNamesXnasTypedConstructorWithArgumentsInFieldOrder) {
    const auto intType = System::Type::From<SharpRuntime::intcs>();
    const auto floatType = System::Type::From<float>();
    const auto byteType = System::Type::From<SharpRuntime::bytecs>();
    const auto vectorType = System::Type::From<Vector3>();

    auto args = ExpectTypedConstructor<Point, PointConverter>(Point(12, -7), {intType, intType});
    ASSERT_EQ(args.size(), 2U);
    EXPECT_EQ(std::any_cast<SharpRuntime::intcs>(args[0]), 12);
    EXPECT_EQ(std::any_cast<SharpRuntime::intcs>(args[1]), -7);

    args = ExpectTypedConstructor<Rectangle, RectangleConverter>(Rectangle(1, 2, 30, 40),
                                                                 {intType, intType, intType, intType});
    ASSERT_EQ(args.size(), 4U);
    EXPECT_EQ(std::any_cast<SharpRuntime::intcs>(args[2]), 30);
    EXPECT_EQ(std::any_cast<SharpRuntime::intcs>(args[3]), 40);

    (void)ExpectTypedConstructor<Vector2, Vector2Converter>(Vector2(1, 2), {floatType, floatType});
    (void)ExpectTypedConstructor<Vector3, Vector3Converter>(Vector3(1, 2, 3), {floatType, floatType, floatType});
    (void)ExpectTypedConstructor<Vector4, Vector4Converter>(Vector4(1, 2, 3, 4),
                                                            {floatType, floatType, floatType, floatType});
    args = ExpectTypedConstructor<Quaternion, QuaternionConverter>(Quaternion(1, 2, 3, 4),
                                                                   {floatType, floatType, floatType, floatType});
    ASSERT_EQ(args.size(), 4U);
    EXPECT_FLOAT_EQ(std::any_cast<float>(args[3]), 4.0F);

    args = ExpectTypedConstructor<Matrix, MatrixConverter>(
        Matrix(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16), std::vector<System::Type>(16, floatType));
    ASSERT_EQ(args.size(), 16U);
    for (std::size_t i = 0; i < 16; ++i)
        EXPECT_FLOAT_EQ(std::any_cast<float>(args[i]), static_cast<float>(i + 1)) << "M" << (i / 4 + 1) << (i % 4 + 1);

    args = ExpectTypedConstructor<Color, ColorConverter>(ByteColor(10, 20, 30, 40),
                                                         {byteType, byteType, byteType, byteType});
    ASSERT_EQ(args.size(), 4U);
    EXPECT_EQ(std::any_cast<SharpRuntime::bytecs>(args[0]), 10);
    EXPECT_EQ(std::any_cast<SharpRuntime::bytecs>(args[3]), 40);

    (void)ExpectTypedConstructor<BoundingBox, BoundingBoxConverter>(
        BoundingBox(Vector3(-1, -2, -3), Vector3(4, 5, 6)), {vectorType, vectorType});
    args = ExpectTypedConstructor<BoundingSphere, BoundingSphereConverter>(
        BoundingSphere(Vector3(1, 2, 3), 4.5F), {vectorType, floatType});
    ASSERT_EQ(args.size(), 2U);
    EXPECT_FLOAT_EQ(std::any_cast<float>(args[1]), 4.5F);
    (void)ExpectTypedConstructor<Plane, PlaneConverter>(Plane(Vector3(1, 2, 3), 4.0F), {vectorType, floatType});
    args = ExpectTypedConstructor<Ray, RayConverter>(Ray(Vector3(1, 2, 3), Vector3(4, 5, 6)), {vectorType, vectorType});
    ASSERT_EQ(args.size(), 2U);
    EXPECT_EQ(std::any_cast<Vector3>(args[1]), Vector3(4, 5, 6));
}

// ── Property descriptors write their own member ────────────────────────────────────────────
//
// Oracle: XNA builds each descriptor from the member of the same name (typeof(T).GetField("X"),
// GetProperty("R"), ...), so writing through one descriptor changes that member and no other.

namespace {

template<class TValue, class TConverter>
void ExpectWriteChangesOnly(const TValue& start, const std::string& name, const std::any& written,
                            const TValue& expected) {
    SCOPED_TRACE("descriptor " + name);
    const TConverter converter;
    std::any boxed = start;
    const auto descriptor = converter.GetProperties(boxed).getItem(name);
    ASSERT_NE(descriptor, nullptr);
    descriptor->SetValue(boxed, written);
    EXPECT_TRUE(std::any_cast<TValue>(boxed) == expected);
    const std::any readBack = descriptor->GetValue(boxed);
    EXPECT_EQ(readBack.type(), written.type());
}

} // namespace

TEST(PropertyDescriptorContractTests, EachDescriptorWritesOnlyItsOwnMember) {
    const auto i = [](int v) { return std::any(SharpRuntime::intcs{v}); };
    const auto f = [](float v) { return std::any(v); };
    const auto b = [](int v) { return std::any(static_cast<SharpRuntime::bytecs>(v)); };

    const Point point(12, -7);
    ExpectWriteChangesOnly<Point, PointConverter>(point, "X", i(99), Point(99, -7));
    ExpectWriteChangesOnly<Point, PointConverter>(point, "Y", i(99), Point(12, 99));

    const Rectangle rectangle(1, 2, 30, 40);
    ExpectWriteChangesOnly<Rectangle, RectangleConverter>(rectangle, "X", i(9), Rectangle(9, 2, 30, 40));
    ExpectWriteChangesOnly<Rectangle, RectangleConverter>(rectangle, "Y", i(9), Rectangle(1, 9, 30, 40));
    ExpectWriteChangesOnly<Rectangle, RectangleConverter>(rectangle, "Width", i(9), Rectangle(1, 2, 9, 40));
    ExpectWriteChangesOnly<Rectangle, RectangleConverter>(rectangle, "Height", i(9), Rectangle(1, 2, 30, 9));

    const Vector4 vector(1, 2, 3, 4);
    ExpectWriteChangesOnly<Vector4, Vector4Converter>(vector, "X", f(9), Vector4(9, 2, 3, 4));
    ExpectWriteChangesOnly<Vector4, Vector4Converter>(vector, "Y", f(9), Vector4(1, 9, 3, 4));
    ExpectWriteChangesOnly<Vector4, Vector4Converter>(vector, "Z", f(9), Vector4(1, 2, 9, 4));
    ExpectWriteChangesOnly<Vector4, Vector4Converter>(vector, "W", f(9), Vector4(1, 2, 3, 9));
    ExpectWriteChangesOnly<Vector2, Vector2Converter>(Vector2(1, 2), "Y", f(9), Vector2(1, 9));
    ExpectWriteChangesOnly<Vector3, Vector3Converter>(Vector3(1, 2, 3), "Z", f(9), Vector3(1, 2, 9));

    const Quaternion quaternion(1, 2, 3, 4);
    ExpectWriteChangesOnly<Quaternion, QuaternionConverter>(quaternion, "X", f(9), Quaternion(9, 2, 3, 4));
    ExpectWriteChangesOnly<Quaternion, QuaternionConverter>(quaternion, "W", f(9), Quaternion(1, 2, 3, 9));

    const Color color = ByteColor(10, 20, 30, 40);
    ExpectWriteChangesOnly<Color, ColorConverter>(color, "R", b(99), ByteColor(99, 20, 30, 40));
    ExpectWriteChangesOnly<Color, ColorConverter>(color, "G", b(99), ByteColor(10, 99, 30, 40));
    ExpectWriteChangesOnly<Color, ColorConverter>(color, "B", b(99), ByteColor(10, 20, 99, 40));
    ExpectWriteChangesOnly<Color, ColorConverter>(color, "A", b(99), ByteColor(10, 20, 30, 99));

    float elements[16];
    for (int e = 0; e < 16; ++e) elements[e] = static_cast<float>(e + 1);
    const auto matrixOf = [](const float (&m)[16]) {
        return Matrix(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                      m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    };
    const Matrix matrix = matrixOf(elements);
    for (int e = 0; e < 16; ++e) {
        float expected[16];
        for (int k = 0; k < 16; ++k) expected[k] = elements[k];
        expected[e] = 99.0F;
        const std::string name = "M" + std::to_string(e / 4 + 1) + std::to_string(e % 4 + 1);
        ExpectWriteChangesOnly<Matrix, MatrixConverter>(matrix, name, f(99), matrixOf(expected));
    }

    const BoundingBox box(Vector3(-1, -2, -3), Vector3(4, 5, 6));
    ExpectWriteChangesOnly<BoundingBox, BoundingBoxConverter>(box, "Min", std::any(Vector3(-7, -7, -7)),
                                                              BoundingBox(Vector3(-7, -7, -7), Vector3(4, 5, 6)));
    ExpectWriteChangesOnly<BoundingBox, BoundingBoxConverter>(box, "Max", std::any(Vector3(7, 7, 7)),
                                                              BoundingBox(Vector3(-1, -2, -3), Vector3(7, 7, 7)));
    const BoundingSphere sphere(Vector3(1, 2, 3), 4.5F);
    ExpectWriteChangesOnly<BoundingSphere, BoundingSphereConverter>(
        sphere, "Center", std::any(Vector3(7, 8, 9)), BoundingSphere(Vector3(7, 8, 9), 4.5F));
    ExpectWriteChangesOnly<BoundingSphere, BoundingSphereConverter>(
        sphere, "Radius", f(9), BoundingSphere(Vector3(1, 2, 3), 9.0F));
    const Plane plane(Vector3(1, 2, 3), 4.0F);
    ExpectWriteChangesOnly<Plane, PlaneConverter>(plane, "Normal", std::any(Vector3(0, 1, 0)),
                                                  Plane(Vector3(0, 1, 0), 4.0F));
    ExpectWriteChangesOnly<Plane, PlaneConverter>(plane, "D", f(-2), Plane(Vector3(1, 2, 3), -2.0F));
    const Ray ray(Vector3(1, 2, 3), Vector3(4, 5, 6));
    ExpectWriteChangesOnly<Ray, RayConverter>(ray, "Position", std::any(Vector3(7, 8, 9)),
                                              Ray(Vector3(7, 8, 9), Vector3(4, 5, 6)));
    ExpectWriteChangesOnly<Ray, RayConverter>(ray, "Direction", std::any(Vector3(0, 0, 1)),
                                              Ray(Vector3(1, 2, 3), Vector3(0, 0, 1)));
}

TEST(PropertyDescriptorContractTests, CreateInstanceRefusesMissingOrMistypedComponents) {
    // XNA unboxes every entry: a missing key is (int)null (NullReferenceException) and a value
    // boxed as another type is an InvalidCastException. No value may be produced either way.
    System::Collections::Hashtable onlyX;
    onlyX.setItem("X", std::any(SharpRuntime::intcs{1}));
    EXPECT_ANY_THROW((void)PointConverter().CreateInstance(onlyX));

    System::Collections::Hashtable floats;
    floats.setItem("X", std::any(1.0F));
    floats.setItem("Y", std::any(2.0F));
    EXPECT_ANY_THROW((void)PointConverter().CreateInstance(floats));

    System::Collections::Hashtable doubles;
    doubles.setItem("X", std::any(1.0));
    doubles.setItem("Y", std::any(2.0));
    EXPECT_ANY_THROW((void)Vector2Converter().CreateInstance(doubles));

    System::Collections::Hashtable ints;
    for (const char* name : {"R", "G", "B", "A"}) ints.setItem(name, std::any(SharpRuntime::intcs{1}));
    EXPECT_ANY_THROW((void)ColorConverter().CreateInstance(ints));
}
