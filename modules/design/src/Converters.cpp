// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Design.hpp"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CNA/Internal/Design/PropertyDescriptors.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ComponentModel/Design/Serialization/InstanceDescriptor.hpp"
#include "System/Reflection/ConstructorInfo.hpp"

namespace Microsoft::Xna::Framework::Design {
namespace {

using CNA::Internal::Design::MakeAccessorProperty;
using CNA::Internal::Design::MakeFieldProperty;
using System::ComponentModel::Design::Serialization::InstanceDescriptor;

template<class TValue>
TValue propertyValue(const System::Collections::Hashtable& values, const std::string& name) {
    return std::any_cast<TValue>(values.at(name));
}

template<class T, class... TArgs>
std::optional<std::any> tryInstanceDescriptor(const std::any& value,
                                              const System::Type& destinationType,
                                              std::vector<std::string> parameterNames,
                                              TArgs&&... arguments) {
    if (destinationType != System::Type::From<InstanceDescriptor>() || std::any_cast<T>(&value) == nullptr) {
        return std::nullopt;
    }
    static const auto constructor = System::Reflection::ConstructorInfo::Of<T, std::decay_t<TArgs>...>(
        std::move(parameterNames));
    return std::any(InstanceDescriptor(constructor, {std::any(std::forward<TArgs>(arguments))...}));
}

void requireDestinationType(const System::Type& destinationType) {
    if (destinationType == System::Type()) throw System::ArgumentNullException("destinationType");
}

} // namespace

PointConverter::PointConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Point>("X", &Point::X), MakeFieldProperty<Point>("Y", &Point::Y),
    }).Sort(std::vector<std::string>{"X", "Y"});
}

std::any PointConverter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Globalization::CultureInfo* culture,
                                     const std::any& value) const {
    static const std::array<std::string, 2> names{"X", "Y"};
    if (auto values = ConvertToValues<SharpRuntime::intcs>(context, culture, value, 2, names)) {
        return std::any(Point((*values)[0], (*values)[1]));
    }
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any PointConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                   const System::Globalization::CultureInfo* culture,
                                   const std::any& value,
                                   const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* point = std::any_cast<Point>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<SharpRuntime::intcs, 2> values{point->X, point->Y};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Point>(value, destinationType, {"x", "y"}, point->X, point->Y)) {
            return *result;
        }
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any PointConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                        const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Point(propertyValue<SharpRuntime::intcs>(propertyValues, "X"),
                          propertyValue<SharpRuntime::intcs>(propertyValues, "Y")));
}

RectangleConverter::RectangleConverter() {
    propertyDescriptions_ = {
        MakeFieldProperty<Rectangle>("X", &Rectangle::X),
        MakeFieldProperty<Rectangle>("Y", &Rectangle::Y),
        MakeFieldProperty<Rectangle>("Width", &Rectangle::Width),
        MakeFieldProperty<Rectangle>("Height", &Rectangle::Height),
    };
    supportStringConvert_ = false;
}

std::any RectangleConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Globalization::CultureInfo* culture,
                                       const std::any& value,
                                       const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* rectangle = std::any_cast<Rectangle>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(rectangle->ToString());
        if (auto result = tryInstanceDescriptor<Rectangle>(value, destinationType,
                {"x", "y", "width", "height"},
                rectangle->X, rectangle->Y, rectangle->Width, rectangle->Height)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any RectangleConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                            const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Rectangle(propertyValue<SharpRuntime::intcs>(propertyValues, "X"),
                              propertyValue<SharpRuntime::intcs>(propertyValues, "Y"),
                              propertyValue<SharpRuntime::intcs>(propertyValues, "Width"),
                              propertyValue<SharpRuntime::intcs>(propertyValues, "Height")));
}

Vector2Converter::Vector2Converter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Vector2>("X", &Vector2::X), MakeFieldProperty<Vector2>("Y", &Vector2::Y),
    }).Sort(std::vector<std::string>{"X", "Y"});
}

std::any Vector2Converter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Globalization::CultureInfo* culture,
                                       const std::any& value) const {
    static const std::array<std::string, 2> names{"X", "Y"};
    if (auto v = ConvertToValues<float>(context, culture, value, 2, names)) return std::any(Vector2((*v)[0], (*v)[1]));
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any Vector2Converter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Globalization::CultureInfo* culture,
                                     const std::any& value,
                                     const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Vector2>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<float, 2> values{v->X, v->Y};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Vector2>(value, destinationType, {"x", "y"}, v->X, v->Y)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any Vector2Converter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                          const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Vector2(propertyValue<float>(propertyValues, "X"), propertyValue<float>(propertyValues, "Y")));
}

Vector3Converter::Vector3Converter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Vector3>("X", &Vector3::X), MakeFieldProperty<Vector3>("Y", &Vector3::Y),
        MakeFieldProperty<Vector3>("Z", &Vector3::Z),
    }).Sort(std::vector<std::string>{"X", "Y", "Z"});
}

std::any Vector3Converter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Globalization::CultureInfo* culture,
                                       const std::any& value) const {
    static const std::array<std::string, 3> names{"X", "Y", "Z"};
    if (auto v = ConvertToValues<float>(context, culture, value, 3, names)) return std::any(Vector3((*v)[0], (*v)[1], (*v)[2]));
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any Vector3Converter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Globalization::CultureInfo* culture,
                                     const std::any& value,
                                     const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Vector3>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<float, 3> values{v->X, v->Y, v->Z};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Vector3>(value, destinationType, {"x", "y", "z"}, v->X, v->Y, v->Z)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any Vector3Converter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                          const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Vector3(propertyValue<float>(propertyValues, "X"), propertyValue<float>(propertyValues, "Y"),
                            propertyValue<float>(propertyValues, "Z")));
}

Vector4Converter::Vector4Converter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Vector4>("X", &Vector4::X), MakeFieldProperty<Vector4>("Y", &Vector4::Y),
        MakeFieldProperty<Vector4>("Z", &Vector4::Z), MakeFieldProperty<Vector4>("W", &Vector4::W),
    }).Sort(std::vector<std::string>{"X", "Y", "Z", "W"});
}

std::any Vector4Converter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                       const System::Globalization::CultureInfo* culture,
                                       const std::any& value) const {
    static const std::array<std::string, 4> names{"X", "Y", "Z", "W"};
    if (auto v = ConvertToValues<float>(context, culture, value, 4, names)) return std::any(Vector4((*v)[0], (*v)[1], (*v)[2], (*v)[3]));
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any Vector4Converter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Globalization::CultureInfo* culture,
                                     const std::any& value,
                                     const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Vector4>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<float, 4> values{v->X, v->Y, v->Z, v->W};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Vector4>(value, destinationType, {"x", "y", "z", "w"}, v->X, v->Y, v->Z, v->W)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any Vector4Converter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                          const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Vector4(propertyValue<float>(propertyValues, "X"), propertyValue<float>(propertyValues, "Y"),
                            propertyValue<float>(propertyValues, "Z"), propertyValue<float>(propertyValues, "W")));
}

QuaternionConverter::QuaternionConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Quaternion>("X", &Quaternion::X), MakeFieldProperty<Quaternion>("Y", &Quaternion::Y),
        MakeFieldProperty<Quaternion>("Z", &Quaternion::Z), MakeFieldProperty<Quaternion>("W", &Quaternion::W),
    }).Sort(std::vector<std::string>{"X", "Y", "Z", "W"});
}

std::any QuaternionConverter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                          const System::Globalization::CultureInfo* culture,
                                          const std::any& value) const {
    static const std::array<std::string, 4> names{"X", "Y", "Z", "W"};
    if (auto v = ConvertToValues<float>(context, culture, value, 4, names)) return std::any(Quaternion((*v)[0], (*v)[1], (*v)[2], (*v)[3]));
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any QuaternionConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                        const System::Globalization::CultureInfo* culture,
                                        const std::any& value,
                                        const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Quaternion>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<float, 4> values{v->X, v->Y, v->Z, v->W};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Quaternion>(value, destinationType, {"x", "y", "z", "w"}, v->X, v->Y, v->Z, v->W)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any QuaternionConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                             const System::Collections::Hashtable& propertyValues) const {
    (void)context;
    return std::any(Quaternion(propertyValue<float>(propertyValues, "X"), propertyValue<float>(propertyValues, "Y"),
                               propertyValue<float>(propertyValues, "Z"), propertyValue<float>(propertyValues, "W")));
}

MatrixConverter::MatrixConverter() {
    propertyDescriptions_ = {
        MakeAccessorProperty<Matrix, Vector3>(
            "Translation", &Matrix::getTranslationProperty, &Matrix::setTranslationProperty),
        MakeFieldProperty<Matrix>("M11", &Matrix::M11), MakeFieldProperty<Matrix>("M12", &Matrix::M12),
        MakeFieldProperty<Matrix>("M13", &Matrix::M13), MakeFieldProperty<Matrix>("M14", &Matrix::M14),
        MakeFieldProperty<Matrix>("M21", &Matrix::M21), MakeFieldProperty<Matrix>("M22", &Matrix::M22),
        MakeFieldProperty<Matrix>("M23", &Matrix::M23), MakeFieldProperty<Matrix>("M24", &Matrix::M24),
        MakeFieldProperty<Matrix>("M31", &Matrix::M31), MakeFieldProperty<Matrix>("M32", &Matrix::M32),
        MakeFieldProperty<Matrix>("M33", &Matrix::M33), MakeFieldProperty<Matrix>("M34", &Matrix::M34),
        MakeFieldProperty<Matrix>("M41", &Matrix::M41), MakeFieldProperty<Matrix>("M42", &Matrix::M42),
        MakeFieldProperty<Matrix>("M43", &Matrix::M43), MakeFieldProperty<Matrix>("M44", &Matrix::M44),
    };
    supportStringConvert_ = false;
}

std::any MatrixConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                    const System::Globalization::CultureInfo* culture,
                                    const std::any& value,
                                    const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* m = std::any_cast<Matrix>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(m->ToString());
        if (auto result = tryInstanceDescriptor<Matrix>(value, destinationType,
                {"m11", "m12", "m13", "m14", "m21", "m22", "m23", "m24",
                 "m31", "m32", "m33", "m34", "m41", "m42", "m43", "m44"},
                m->M11, m->M12, m->M13, m->M14, m->M21, m->M22, m->M23, m->M24,
                m->M31, m->M32, m->M33, m->M34, m->M41, m->M42, m->M43, m->M44)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any MatrixConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                         const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(Matrix(
        propertyValue<float>(v, "M11"), propertyValue<float>(v, "M12"), propertyValue<float>(v, "M13"), propertyValue<float>(v, "M14"),
        propertyValue<float>(v, "M21"), propertyValue<float>(v, "M22"), propertyValue<float>(v, "M23"), propertyValue<float>(v, "M24"),
        propertyValue<float>(v, "M31"), propertyValue<float>(v, "M32"), propertyValue<float>(v, "M33"), propertyValue<float>(v, "M34"),
        propertyValue<float>(v, "M41"), propertyValue<float>(v, "M42"), propertyValue<float>(v, "M43"), propertyValue<float>(v, "M44")));
}

BoundingBoxConverter::BoundingBoxConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<BoundingBox>("Min", &BoundingBox::Min), MakeFieldProperty<BoundingBox>("Max", &BoundingBox::Max),
    }).Sort(std::vector<std::string>{"Min", "Max"});
    supportStringConvert_ = false;
}

std::any BoundingBoxConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<BoundingBox>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(v->ToString());
        if (auto result = tryInstanceDescriptor<BoundingBox>(value, destinationType, {"min", "max"}, v->Min, v->Max)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any BoundingBoxConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(BoundingBox(propertyValue<Vector3>(v, "Min"), propertyValue<Vector3>(v, "Max")));
}

BoundingSphereConverter::BoundingSphereConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<BoundingSphere>("Center", &BoundingSphere::Center), MakeFieldProperty<BoundingSphere>("Radius", &BoundingSphere::Radius),
    }).Sort(std::vector<std::string>{"Center", "Radius"});
    supportStringConvert_ = false;
}

std::any BoundingSphereConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<BoundingSphere>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(v->ToString());
        if (auto result = tryInstanceDescriptor<BoundingSphere>(value, destinationType, {"center", "radius"}, v->Center, v->Radius)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any BoundingSphereConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(BoundingSphere(propertyValue<Vector3>(v, "Center"), propertyValue<float>(v, "Radius")));
}

PlaneConverter::PlaneConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Plane>("Normal", &Plane::Normal), MakeFieldProperty<Plane>("D", &Plane::D),
    }).Sort(std::vector<std::string>{"Normal", "D"});
    supportStringConvert_ = false;
}

std::any PlaneConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Plane>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(v->ToString());
        if (auto result = tryInstanceDescriptor<Plane>(value, destinationType, {"normal", "d"}, v->Normal, v->D)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any PlaneConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(Plane(propertyValue<Vector3>(v, "Normal"), propertyValue<float>(v, "D")));
}

RayConverter::RayConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeFieldProperty<Ray>("Position", &Ray::Position), MakeFieldProperty<Ray>("Direction", &Ray::Direction),
    }).Sort(std::vector<std::string>{"Position", "Direction"});
    supportStringConvert_ = false;
}

std::any RayConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context, const System::Globalization::CultureInfo* culture, const std::any& value, const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Ray>(&value)) {
        if (destinationType == System::Type::From<std::string>()) return std::any(v->ToString());
        if (auto result = tryInstanceDescriptor<Ray>(value, destinationType, {"position", "direction"}, v->Position, v->Direction)) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any RayConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context, const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(Ray(propertyValue<Vector3>(v, "Position"), propertyValue<Vector3>(v, "Direction")));
}

ColorConverter::ColorConverter() {
    propertyDescriptions_ = System::ComponentModel::PropertyDescriptorCollection({
        MakeAccessorProperty<Color, SharpRuntime::bytecs>("R", &Color::getRProperty, &Color::setRProperty),
        MakeAccessorProperty<Color, SharpRuntime::bytecs>("G", &Color::getGProperty, &Color::setGProperty),
        MakeAccessorProperty<Color, SharpRuntime::bytecs>("B", &Color::getBProperty, &Color::setBProperty),
        MakeAccessorProperty<Color, SharpRuntime::bytecs>("A", &Color::getAProperty, &Color::setAProperty),
    }).Sort(std::vector<std::string>{"R", "G", "B", "A"});
}

std::any ColorConverter::ConvertFrom(System::ComponentModel::ITypeDescriptorContext* context,
                                     const System::Globalization::CultureInfo* culture,
                                     const std::any& value) const {
    static const std::array<std::string, 4> names{"R", "G", "B", "A"};
    if (auto v = ConvertToValues<SharpRuntime::bytecs>(context, culture, value, 4, names)) {
        return std::any(Color((*v)[0], (*v)[1], (*v)[2], (*v)[3]));
    }
    return System::ComponentModel::TypeConverter::ConvertFrom(context, culture, value);
}

std::any ColorConverter::ConvertTo(System::ComponentModel::ITypeDescriptorContext* context,
                                   const System::Globalization::CultureInfo* culture,
                                   const std::any& value,
                                   const System::Type& destinationType) const {
    requireDestinationType(destinationType);
    if (const auto* v = std::any_cast<Color>(&value)) {
        if (destinationType == System::Type::From<std::string>()) {
            const std::array<SharpRuntime::bytecs, 4> values{
                v->getRProperty(), v->getGProperty(), v->getBProperty(), v->getAProperty()};
            return std::any(ConvertFromValues(context, culture, std::span(values)));
        }
        if (auto result = tryInstanceDescriptor<Color>(value, destinationType, {"r", "g", "b", "alpha"},
                v->getRProperty(), v->getGProperty(), v->getBProperty(), v->getAProperty())) return *result;
    }
    return System::ComponentModel::TypeConverter::ConvertTo(context, culture, value, destinationType);
}

std::any ColorConverter::CreateInstance(System::ComponentModel::ITypeDescriptorContext* context,
                                        const System::Collections::Hashtable& v) const {
    (void)context;
    return std::any(Color(propertyValue<SharpRuntime::bytecs>(v, "R"), propertyValue<SharpRuntime::bytecs>(v, "G"),
                          propertyValue<SharpRuntime::bytecs>(v, "B"), propertyValue<SharpRuntime::bytecs>(v, "A")));
}

} // namespace Microsoft::Xna::Framework::Design
