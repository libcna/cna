// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
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
    EXPECT_TRUE(converter.CanConvertTo(System::Type::From<InstanceDescriptor>()));
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
