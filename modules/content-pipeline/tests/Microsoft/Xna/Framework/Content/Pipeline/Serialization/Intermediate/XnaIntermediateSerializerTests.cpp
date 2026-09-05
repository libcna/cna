// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-070..074: CNA's IntermediateSerializer against the corpus
// the genuine XNA 4.0 IntermediateSerializer wrote (tests/reference/xna40/intermediate/). The
// types below mirror tools/xna-pipeline-oracle/intermediate/IntermediateOracle.cs member for
// member and value for value, so every `written` case can be compared byte for byte, every
// `accepted` case must normalize to the same text, and every `rejected` case must be refused with
// the same message.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ExternalReference.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Serialization/Intermediate/IntermediateSerializer.hpp"
#include "System/DateTime.hpp"
#include "System/DateTimeKind.hpp"
#include "System/Decimal.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "System/Xml/XmlReader.hpp"
#include "System/Xml/XmlWriter.hpp"
#include "System/Xml/XmlWriterSettings.hpp"

namespace Intermediate = Microsoft::Xna::Framework::Content::Pipeline::Serialization::Intermediate;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::BoundingSphere;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Plane;
using Microsoft::Xna::Framework::Point;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Ray;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Content::Pipeline::Box;
using Microsoft::Xna::Framework::Content::Pipeline::Carrier;
using Microsoft::Xna::Framework::Content::Pipeline::ContentObject;
using Microsoft::Xna::Framework::Content::Pipeline::ExternalReference;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;
using Intermediate::ContentTypeDescriptor;
using Intermediate::IntermediateSerializer;

// -------------------------------------------------------------------------------------------------
// The corpus types (IntermediateOracle.cs, namespace Cna.Xna40.IntermediateOracle)
// -------------------------------------------------------------------------------------------------
namespace Cna::Xna40::IntermediateOracle
{
#define ORACLE_OBJECT(Type)                                                                       \
    static constexpr std::string_view XnaTypeName = "Cna.Xna40.IntermediateOracle." #Type;        \
    [[nodiscard]] const std::string& GetTypeName() const override                                 \
    {                                                                                             \
        static const std::string name(XnaTypeName);                                               \
        return name;                                                                              \
    }

    inline System::DateTime UtcDateTime(int year, int month, int day, int hour, int minute, int second)
    {
        return System::DateTime(System::DateTime(year, month, day, hour, minute, second).getTicksProperty(),
                                System::DateTimeKind::Utc);
    }

    inline System::Decimal ParseDecimal(const char* text)
    {
        System::Decimal value;
        if (!System::Decimal::TryParse(text, value))
        {
            throw std::runtime_error(std::string("bad decimal literal ") + text);
        }
        return value;
    }

    struct Texture2DContentStandIn
    {
        static constexpr std::string_view XnaTypeName = "Microsoft.Xna.Framework.Content.Pipeline.Graphics.Texture2DContent";
    };
    using Texture2DReference = ExternalReference<Texture2DContentStandIn>;

    enum class Mood { Happy, Sad, Angry };
    enum class Toppings { None = 0, Cheese = 1, Ham = 2, Olives = 4 };

    struct Primitives : System::Object
    {
        ORACLE_OBJECT(Primitives)
        bool Bool = true;
        std::uint8_t Byte = 200;
        std::int8_t SByte = -5;
        std::int16_t Short = -30000;
        std::uint16_t UShort = 60000;
        std::int32_t Int = -123456789;
        std::uint32_t UInt = 4000000000u;
        std::int64_t Long = -9000000000000000000LL;
        std::uint64_t ULong = 18000000000000000000ULL;
        float Float = 1.5f;
        double Double = 2.25;
        char16_t Char = u'x';
        std::string String = "hello world";
        System::Decimal Decimal = ParseDecimal("12.34");
        System::TimeSpan TimeSpan = System::TimeSpan::FromMilliseconds(1500);
        System::DateTime DateTime = UtcDateTime(2010, 9, 16, 12, 30, 45);
        static void DescribeContent(ContentTypeDescriptor<Primitives>& d)
        {
            d.Field("Bool", &Primitives::Bool);
            d.Field("Byte", &Primitives::Byte);
            d.Field("SByte", &Primitives::SByte);
            d.Field("Short", &Primitives::Short);
            d.Field("UShort", &Primitives::UShort);
            d.Field("Int", &Primitives::Int);
            d.Field("UInt", &Primitives::UInt);
            d.Field("Long", &Primitives::Long);
            d.Field("ULong", &Primitives::ULong);
            d.Field("Float", &Primitives::Float);
            d.Field("Double", &Primitives::Double);
            d.Field("Char", &Primitives::Char);
            d.Field("String", &Primitives::String);
            d.Field("Decimal", &Primitives::Decimal);
            d.Field("TimeSpan", &Primitives::TimeSpan);
            d.Field("DateTime", &Primitives::DateTime);
        }
    };

    struct FloatEdges : System::Object
    {
        ORACLE_OBJECT(FloatEdges)
        float NegativeZero = -0.0f;
        float NaN = std::numeric_limits<float>::quiet_NaN();
        float PositiveInfinity = std::numeric_limits<float>::infinity();
        float NegativeInfinity = -std::numeric_limits<float>::infinity();
        float Tiny = 1.17549435E-38f;
        float Precise = 0.1f;
        float Third = 1.0f / 3.0f;
        double DoublePrecise = 0.1;
        double DoubleThird = 1.0 / 3.0;
        float Large = 16777217.0f;
        float Whole = 42.0f;
        static void DescribeContent(ContentTypeDescriptor<FloatEdges>& d)
        {
            d.Field("NegativeZero", &FloatEdges::NegativeZero);
            d.Field("NaN", &FloatEdges::NaN);
            d.Field("PositiveInfinity", &FloatEdges::PositiveInfinity);
            d.Field("NegativeInfinity", &FloatEdges::NegativeInfinity);
            d.Field("Tiny", &FloatEdges::Tiny);
            d.Field("Precise", &FloatEdges::Precise);
            d.Field("Third", &FloatEdges::Third);
            d.Field("DoublePrecise", &FloatEdges::DoublePrecise);
            d.Field("DoubleThird", &FloatEdges::DoubleThird);
            d.Field("Large", &FloatEdges::Large);
            d.Field("Whole", &FloatEdges::Whole);
        }
    };

    struct StringEdges : System::Object
    {
        ORACLE_OBJECT(StringEdges)
        std::string Empty;
        std::optional<std::string> Null;
        std::string Spaces = "  padded  ";
        std::string Newlines = "line1\nline2\r\nline3";
        std::string Escapes = "<tag> & \"quotes\" 'apostrophes'";
        std::string Unicode = "caf\u00e9 \u4e2d\u6587 \U0001F600";
        char16_t Tab = u'\t';
        char16_t Lt = u'<';
        char16_t Amp = u'&';
        char16_t Space = u' ';
        static void DescribeContent(ContentTypeDescriptor<StringEdges>& d)
        {
            d.Field("Empty", &StringEdges::Empty);
            d.Field("Null", &StringEdges::Null);
            d.Field("Spaces", &StringEdges::Spaces);
            d.Field("Newlines", &StringEdges::Newlines);
            d.Field("Escapes", &StringEdges::Escapes);
            d.Field("Unicode", &StringEdges::Unicode);
            d.Field("Tab", &StringEdges::Tab);
            d.Field("Lt", &StringEdges::Lt);
            d.Field("Amp", &StringEdges::Amp);
            d.Field("Space", &StringEdges::Space);
        }
    };

    struct NulCharacter : System::Object
    {
        ORACLE_OBJECT(NulCharacter)
        char16_t Nul = u'\0';
        static void DescribeContent(ContentTypeDescriptor<NulCharacter>& d) { d.Field("Nul", &NulCharacter::Nul); }
    };

    inline Curve MakeCurve()
    {
        Curve curve;
        curve.setPreLoopProperty(CurveLoopType::Constant);
        curve.setPostLoopProperty(CurveLoopType::Linear);
        curve.getKeysProperty().Add(CurveKey(0, 0, 0, 1, CurveContinuity::Smooth));
        curve.getKeysProperty().Add(CurveKey(1, 2, 1, 0, CurveContinuity::Step));
        return curve;
    }

    struct MathTypes : System::Object
    {
        ORACLE_OBJECT(MathTypes)
        Vector2 vector2{1.5f, -2.5f};
        Vector3 vector3{1, 2, 3};
        Vector4 vector4{0.1f, 0.2f, 0.3f, 0.4f};
        Matrix matrix = Matrix::CreateTranslation(1, 2, 3);
        Quaternion quaternion{0, 0.7071068f, 0, 0.7071068f};
        Color color{10, 20, 30, 40};
        Rectangle rectangle{1, 2, 30, 40};
        Point point{-7, 8};
        BoundingBox boundingBox{Vector3(-1, -2, -3), Vector3(1, 2, 3)};
        BoundingSphere boundingSphere{Vector3(1, 1, 1), 5};
        Plane plane{Vector3(0, 1, 0), 3};
        Ray ray{Vector3(0, 0, 0), Vector3(0, 0, 1)};
        Curve curve = MakeCurve();
        static void DescribeContent(ContentTypeDescriptor<MathTypes>& d)
        {
            d.Field("Vector2", &MathTypes::vector2);
            d.Field("Vector3", &MathTypes::vector3);
            d.Field("Vector4", &MathTypes::vector4);
            d.Field("Matrix", &MathTypes::matrix);
            d.Field("Quaternion", &MathTypes::quaternion);
            d.Field("Color", &MathTypes::color);
            d.Field("Rectangle", &MathTypes::rectangle);
            d.Field("Point", &MathTypes::point);
            d.Field("BoundingBox", &MathTypes::boundingBox);
            d.Field("BoundingSphere", &MathTypes::boundingSphere);
            d.Field("Plane", &MathTypes::plane);
            d.Field("Ray", &MathTypes::ray);
            d.Field("Curve", &MathTypes::curve);
        }
    };

    struct Enums : System::Object
    {
        ORACLE_OBJECT(Enums)
        Mood mood = Mood::Sad;
        Toppings One = Toppings::Ham;
        Toppings Several = static_cast<Toppings>(static_cast<int>(Toppings::Cheese) | static_cast<int>(Toppings::Olives));
        Toppings Zero = Toppings::None;
        CurveLoopType FrameworkEnum = CurveLoopType::Oscillate;
        static void DescribeContent(ContentTypeDescriptor<Enums>& d)
        {
            d.Field("Mood", &Enums::mood);
            d.Field("One", &Enums::One);
            d.Field("Several", &Enums::Several);
            d.Field("Zero", &Enums::Zero);
            d.Field("FrameworkEnum", &Enums::FrameworkEnum);
        }
    };

    struct Nested : System::Object
    {
        ORACLE_OBJECT(Nested)
        // A C# string is nullable; std::optional<std::string> keeps that (a plain std::string
        // reads Null="true" as the empty string, a recorded divergence).
        std::optional<std::string> Name = std::string("nested");
        std::int32_t Value = 7;
        static void DescribeContent(ContentTypeDescriptor<Nested>& d)
        {
            d.Field("Name", &Nested::Name);
            d.Field("Value", &Nested::Value);
        }
    };

    struct Vertex
    {
        static constexpr std::string_view XnaTypeName = "Cna.Xna40.IntermediateOracle.Vertex";
        Vector3 Position;
        float Weight = 0;
        static void DescribeContent(ContentTypeDescriptor<Vertex>& d)
        {
            d.Field("Position", &Vertex::Position);
            d.Field("Weight", &Vertex::Weight);
        }
    };

    struct Collections : System::Object
    {
        ORACLE_OBJECT(Collections)
        std::vector<std::int32_t> IntArray{1, 2, 3};
        std::vector<std::string> StringArray{"a", "b"};
        std::vector<float> FloatArray{0.5f, 1.5f};
        std::vector<Vector3> Vector3Array{Vector3(1, 2, 3), Vector3(4, 5, 6)};
        std::vector<std::uint8_t> Bytes{0, 127, 255};
        std::vector<std::int32_t> IntList{4, 5, 6};
        std::vector<std::string> StringList{"x", "y"};
        std::vector<Vector3> Vector3List{Vector3(7, 8, 9)};
        std::vector<std::shared_ptr<Nested>> NestedList{std::make_shared<Nested>(), Second()};
        std::vector<Vertex> StructList{Vertex{Vector3(1, 1, 1), 0.25f}};
        std::map<std::string, std::int32_t> StringIntMap{{"one", 1}, {"two", 2}};
        std::map<std::int32_t, std::string> IntStringMap{{10, "ten"}};
        std::map<std::string, std::shared_ptr<Nested>> NestedMap{{"k", std::make_shared<Nested>()}};
        std::vector<std::vector<std::int32_t>> ListOfLists{{1}, {2, 3}};
        std::map<std::string, std::vector<std::int32_t>> MapOfLists{{"a", {9}}};
        std::vector<std::int32_t> EmptyArray;
        std::vector<std::int32_t> EmptyList;
        std::vector<std::optional<std::string>> ListWithNull{std::string("present"), std::nullopt};
        std::shared_ptr<std::vector<std::int32_t>> NullList;
        static std::shared_ptr<Nested> Second()
        {
            auto nested = std::make_shared<Nested>();
            nested->Name = std::string("second");
            nested->Value = 8;
            return nested;
        }
        static void DescribeContent(ContentTypeDescriptor<Collections>& d)
        {
            d.Field("IntArray", &Collections::IntArray);
            d.Field("StringArray", &Collections::StringArray);
            d.Field("FloatArray", &Collections::FloatArray);
            d.Field("Vector3Array", &Collections::Vector3Array);
            d.Field("Bytes", &Collections::Bytes);
            d.Field("IntList", &Collections::IntList);
            d.Field("StringList", &Collections::StringList);
            d.Field("Vector3List", &Collections::Vector3List);
            d.Field("NestedList", &Collections::NestedList);
            d.Field("StructList", &Collections::StructList);
            d.Field("StringIntMap", &Collections::StringIntMap);
            d.Field("IntStringMap", &Collections::IntStringMap);
            d.Field("NestedMap", &Collections::NestedMap);
            d.Field("ListOfLists", &Collections::ListOfLists);
            d.Field("MapOfLists", &Collections::MapOfLists);
            d.Field("EmptyArray", &Collections::EmptyArray);
            d.Field("EmptyList", &Collections::EmptyList);
            d.Field("ListWithNull", &Collections::ListWithNull);
            d.Field("NullList", &Collections::NullList);
        }
    };

    struct Nullables : System::Object
    {
        ORACLE_OBJECT(Nullables)
        std::optional<std::int32_t> HasValue = 5;
        std::optional<std::int32_t> NoValue;
        std::optional<Vector3> Vector = Vector3(1, 2, 3);
        std::optional<Mood> Enum = Mood::Angry;
        std::vector<std::optional<std::int32_t>> ListOfNullable{1, std::nullopt, 3};
        static void DescribeContent(ContentTypeDescriptor<Nullables>& d)
        {
            d.Field("HasValue", &Nullables::HasValue);
            d.Field("NoValue", &Nullables::NoValue);
            d.Field("Vector", &Nullables::Vector);
            d.Field("Enum", &Nullables::Enum);
            d.Field("ListOfNullable", &Nullables::ListOfNullable);
        }
    };

    struct Animal : System::Object
    {
        ORACLE_OBJECT(Animal)
        std::string Name = "generic";
        static void DescribeContent(ContentTypeDescriptor<Animal>& d) { d.Field("Name", &Animal::Name); }
    };

    struct Dog : Animal
    {
        ORACLE_OBJECT(Dog)
        std::int32_t Tricks = 3;
        static void DescribeContent(ContentTypeDescriptor<Dog>& d)
        {
            d.BaseType<Animal>();
            d.Field("Tricks", &Dog::Tricks);
        }
    };

    struct Cat : Animal
    {
        ORACLE_OBJECT(Cat)
        bool Indoor = true;
        static void DescribeContent(ContentTypeDescriptor<Cat>& d)
        {
            d.BaseType<Animal>();
            d.Field("Indoor", &Cat::Indoor);
        }
    };

    struct Shape : System::Object
    {
        ORACLE_OBJECT(Shape)
        float Area = 1;
        virtual void Draw() const = 0;
        static void DescribeContent(ContentTypeDescriptor<Shape>& d) { d.Field("Area", &Shape::Area); }
    };

    struct Circle : Shape
    {
        ORACLE_OBJECT(Circle)
        float Radius = 2;
        void Draw() const override {}
        static void DescribeContent(ContentTypeDescriptor<Circle>& d)
        {
            d.BaseType<Shape>();
            d.Field("Radius", &Circle::Radius);
        }
    };

    struct Polymorphism : System::Object
    {
        ORACLE_OBJECT(Polymorphism)
        std::shared_ptr<Animal> DeclaredBase = Rex();
        std::shared_ptr<Animal> ExactType = Plain();
        std::shared_ptr<Animal> NullAnimal;
        std::shared_ptr<Shape> ViaAbstract = std::make_shared<Circle>();
        ContentObject BoxedInt = Box<std::int32_t>(42);
        ContentObject BoxedVector = Box<Vector3>(Vector3(1, 2, 3));
        ContentObject BoxedString = Box<std::string>("boxed");
        ContentObject BoxedNested = Box<Nested>(std::make_shared<Nested>());
        ContentObject NullObject;
        std::vector<std::shared_ptr<Animal>> Mixed{std::make_shared<Dog>(), std::make_shared<Cat>(), std::make_shared<Animal>()};
        std::vector<ContentObject> Objects{Box<std::int32_t>(1), Box<std::string>("two"), Box<Vector2>(Vector2(3, 3)), ContentObject{}};
        static std::shared_ptr<Animal> Rex()
        {
            auto dog = std::make_shared<Dog>();
            dog->Name = "rex";
            return dog;
        }
        static std::shared_ptr<Animal> Plain()
        {
            auto animal = std::make_shared<Animal>();
            animal->Name = "plain";
            return animal;
        }
        static void DescribeContent(ContentTypeDescriptor<Polymorphism>& d)
        {
            d.Field("DeclaredBase", &Polymorphism::DeclaredBase);
            d.Field("ExactType", &Polymorphism::ExactType);
            d.Field("NullAnimal", &Polymorphism::NullAnimal);
            d.Field("ViaAbstract", &Polymorphism::ViaAbstract);
            d.Field("BoxedInt", &Polymorphism::BoxedInt);
            d.Field("BoxedVector", &Polymorphism::BoxedVector);
            d.Field("BoxedString", &Polymorphism::BoxedString);
            d.Field("BoxedNested", &Polymorphism::BoxedNested);
            d.Field("NullObject", &Polymorphism::NullObject);
            d.Field("Mixed", &Polymorphism::Mixed);
            d.Field("Objects", &Polymorphism::Objects);
        }
    };

    struct Referenced : System::Object
    {
        ORACLE_OBJECT(Referenced)
        std::string Label = "shared";
        std::int32_t Count = 2;
        static void DescribeContent(ContentTypeDescriptor<Referenced>& d)
        {
            d.Field("Label", &Referenced::Label);
            d.Field("Count", &Referenced::Count);
        }
    };

    struct SharedResources : System::Object
    {
        ORACLE_OBJECT(SharedResources)
        std::shared_ptr<Referenced> First;
        std::shared_ptr<Referenced> Second;
        std::shared_ptr<Referenced> Other;
        std::shared_ptr<Referenced> NullShared;
        std::shared_ptr<std::vector<std::shared_ptr<Referenced>>> SharedList;
        std::shared_ptr<Referenced> Inline;
        SharedResources()
        {
            auto one = std::make_shared<Referenced>();
            First = one;
            Second = one;
            Other = std::make_shared<Referenced>();
            Other->Label = "other";
            Other->Count = 3;
            NullShared = nullptr;
            SharedList = std::make_shared<std::vector<std::shared_ptr<Referenced>>>(
                std::vector<std::shared_ptr<Referenced>>{one, Other});
            Inline = std::make_shared<Referenced>();
            Inline->Label = "inline";
            Inline->Count = 1;
        }
        static void DescribeContent(ContentTypeDescriptor<SharedResources>& d)
        {
            d.Field("First", &SharedResources::First).SharedResource();
            d.Field("Second", &SharedResources::Second).SharedResource();
            d.Field("Other", &SharedResources::Other).SharedResource();
            d.Field("NullShared", &SharedResources::NullShared).SharedResource();
            d.Field("SharedList", &SharedResources::SharedList).SharedResource();
            d.Field("Inline", &SharedResources::Inline);
        }
    };

    struct ExternalReferences : System::Object
    {
        ORACLE_OBJECT(ExternalReferences)
        std::shared_ptr<Texture2DReference> Texture = std::make_shared<Texture2DReference>("Textures/wall.png");
        std::shared_ptr<Texture2DReference> Again = std::make_shared<Texture2DReference>("Textures/wall.png");
        std::shared_ptr<Texture2DReference> Other = std::make_shared<Texture2DReference>("..\\Shared\\other.dds");
        std::shared_ptr<Texture2DReference> Null;
        std::vector<std::shared_ptr<Texture2DReference>> List{std::make_shared<Texture2DReference>("Textures/a.png"),
                                                              std::make_shared<Texture2DReference>("Textures/b.png")};
        static void DescribeContent(ContentTypeDescriptor<ExternalReferences>& d)
        {
            d.Field("Texture", &ExternalReferences::Texture);
            d.Field("Again", &ExternalReferences::Again);
            d.Field("Other", &ExternalReferences::Other);
            d.Field("Null", &ExternalReferences::Null);
            d.Field("List", &ExternalReferences::List);
        }
    };

    struct PackedCollections : System::Object
    {
        ORACLE_OBJECT(PackedCollections)
        std::vector<bool> Bools{true, false};
        std::vector<Mood> Enums{Mood::Happy, Mood::Sad};
        std::vector<Toppings> Flags{static_cast<Toppings>(5), Toppings::None};
        std::vector<Color> Colors{Color::Red, Color::CornflowerBlue};
        std::vector<System::TimeSpan> Spans{System::TimeSpan::FromSeconds(1.5), System::TimeSpan::FromMinutes(2)};
        std::vector<System::DateTime> Dates{UtcDateTime(2010, 9, 16, 12, 30, 45)};
        std::vector<System::Decimal> Decimals{ParseDecimal("1.5"), ParseDecimal("2")};
        std::vector<double> Doubles{0.1, 1e300};
        std::vector<std::int64_t> Longs{-1LL, std::numeric_limits<std::int64_t>::max()};
        std::vector<std::uint64_t> Ulongs{std::numeric_limits<std::uint64_t>::max()};
        std::vector<std::int16_t> Shorts{-3, 3};
        std::vector<std::uint16_t> Ushorts{65535};
        std::vector<std::int8_t> Sbytes{-128, 127};
        std::vector<std::uint32_t> Uints{4000000000u};
        std::vector<Vector2> Vector2s{Vector2(1, 2), Vector2(3, 4)};
        std::vector<Vector4> Vector4s{Vector4(1, 2, 3, 4)};
        std::vector<Quaternion> Quaternions{Quaternion::Identity};
        std::vector<Rectangle> Rectangles{Rectangle(1, 2, 3, 4), Rectangle(5, 6, 7, 8)};
        std::vector<Point> Points{Point(1, 2)};
        std::vector<Matrix> Matrices{Matrix::getIdentityProperty()};
        std::vector<Plane> Planes{Plane(0, 1, 0, 3)};
        std::vector<BoundingBox> Boxes{BoundingBox(Vector3::Zero, Vector3::One)};
        std::vector<BoundingSphere> Spheres{BoundingSphere(Vector3::Zero, 2)};
        std::vector<Ray> Rays{Ray(Vector3::Zero, Vector3::UnitZ)};
        std::vector<Curve> Curves{Curve()};
        std::vector<std::optional<Vector3>> NullableVectors{Vector3::One, std::nullopt};
        std::vector<ContentObject> BoxedPrimitives{
            Box<std::int64_t>(1), Box<std::int16_t>(2), Box<std::uint8_t>(3), Box<char16_t>(u'c'),
            Box<System::Decimal>(ParseDecimal("1.5")), Box<double>(2.5), Box<bool>(true), Box<Mood>(Mood::Sad),
            Box<System::TimeSpan>(System::TimeSpan::FromSeconds(1)), Box<std::uint64_t>(4), Box<std::uint16_t>(5),
            Box<std::int8_t>(6), Box<std::uint32_t>(7), Box<float>(8.0f)};
        std::map<std::int32_t, std::int32_t> IntIntMap{{1, 2}};
        std::map<Mood, Vector3> EnumVectorMap{{Mood::Happy, Vector3::One}};
        std::vector<bool> BoolArray{false, true};
        std::vector<char16_t> CharArray{u'a', u'b'};
        std::vector<std::string> StringWithSpaces{"a b", " c "};
        static void DescribeContent(ContentTypeDescriptor<PackedCollections>& d)
        {
            d.Field("Bools", &PackedCollections::Bools);
            d.Field("Enums", &PackedCollections::Enums);
            d.Field("Flags", &PackedCollections::Flags);
            d.Field("Colors", &PackedCollections::Colors);
            d.Field("Spans", &PackedCollections::Spans);
            d.Field("Dates", &PackedCollections::Dates);
            d.Field("Decimals", &PackedCollections::Decimals);
            d.Field("Doubles", &PackedCollections::Doubles);
            d.Field("Longs", &PackedCollections::Longs);
            d.Field("Ulongs", &PackedCollections::Ulongs);
            d.Field("Shorts", &PackedCollections::Shorts);
            d.Field("Ushorts", &PackedCollections::Ushorts);
            d.Field("Sbytes", &PackedCollections::Sbytes);
            d.Field("Uints", &PackedCollections::Uints);
            d.Field("Vector2s", &PackedCollections::Vector2s);
            d.Field("Vector4s", &PackedCollections::Vector4s);
            d.Field("Quaternions", &PackedCollections::Quaternions);
            d.Field("Rectangles", &PackedCollections::Rectangles);
            d.Field("Points", &PackedCollections::Points);
            d.Field("Matrices", &PackedCollections::Matrices);
            d.Field("Planes", &PackedCollections::Planes);
            d.Field("Boxes", &PackedCollections::Boxes);
            d.Field("Spheres", &PackedCollections::Spheres);
            d.Field("Rays", &PackedCollections::Rays);
            d.Field("Curves", &PackedCollections::Curves);
            d.Field("NullableVectors", &PackedCollections::NullableVectors);
            d.Field("BoxedPrimitives", &PackedCollections::BoxedPrimitives);
            d.Field("IntIntMap", &PackedCollections::IntIntMap);
            d.Field("EnumVectorMap", &PackedCollections::EnumVectorMap);
            d.Field("BoolArray", &PackedCollections::BoolArray);
            d.Field("CharArray", &PackedCollections::CharArray);
            d.Field("StringWithSpaces", &PackedCollections::StringWithSpaces);
        }
    };

    struct Both : System::Object
    {
        ORACLE_OBJECT(Both)
        std::shared_ptr<Referenced> Shared = std::make_shared<Referenced>();
        std::shared_ptr<Texture2DReference> Texture = std::make_shared<Texture2DReference>("Textures/wall.png");
        static void DescribeContent(ContentTypeDescriptor<Both>& d)
        {
            d.Field("Shared", &Both::Shared).SharedResource();
            d.Field("Texture", &Both::Texture);
        }
    };

    struct Node : System::Object
    {
        ORACLE_OBJECT(Node)
        std::string Name = "node";
        std::shared_ptr<Node> Next;
        static void DescribeContent(ContentTypeDescriptor<Node>& d)
        {
            d.Field("Name", &Node::Name);
            d.Field("Next", &Node::Next).SharedResource();
        }
    };

    struct Attributes : System::Object
    {
        ORACLE_OBJECT(Attributes)
        std::int32_t Original = 1;
        std::optional<std::string> OptionalPresent = std::string("here");
        std::optional<std::string> OptionalNull;
        std::int32_t OptionalDefault = 0;
        std::string NeverNull = "value";
        std::shared_ptr<Nested> Flattened = std::make_shared<Nested>();
        std::vector<std::int32_t> FlattenedList{1, 2};
        std::vector<std::int32_t> RenamedItems{3, 4};
        std::vector<std::int32_t> FlattenedRenamed{5, 6};
        std::int32_t Ignored = 99;
        std::vector<std::int32_t> Named{7, 8};
        std::int32_t PublicProperty = 2;
        std::vector<std::int32_t> getOnlyList{11, 12};
        [[nodiscard]] std::int32_t getPublicPropertyProperty() const noexcept { return PublicProperty; }
        void setPublicPropertyProperty(std::int32_t value) noexcept { PublicProperty = value; }
        [[nodiscard]] std::vector<std::int32_t>& getGetOnlyListProperty() noexcept { return getOnlyList; }
        static void DescribeContent(ContentTypeDescriptor<Attributes>& d)
        {
            d.Property("PublicProperty", &Attributes::getPublicPropertyProperty, &Attributes::setPublicPropertyProperty);
            d.ReadOnlyProperty("GetOnlyList", &Attributes::getGetOnlyListProperty);
            d.Field("Original", &Attributes::Original).ElementName("Renamed");
            d.Field("OptionalPresent", &Attributes::OptionalPresent).Optional();
            d.Field("OptionalNull", &Attributes::OptionalNull).Optional();
            d.Field("OptionalDefault", &Attributes::OptionalDefault).Optional();
            d.Field("NeverNull", &Attributes::NeverNull).AllowNull(false);
            d.Field("Flattened", &Attributes::Flattened).FlattenContent();
            d.Field("FlattenedList", &Attributes::FlattenedList).FlattenContent();
            d.Field("RenamedItems", &Attributes::RenamedItems).CollectionItemName("Number");
            d.Field("FlattenedRenamed", &Attributes::FlattenedRenamed).FlattenContent().CollectionItemName("Loose");
            d.Field("Ignored", &Attributes::Ignored).Ignore();
            d.Field("Named", &Attributes::Named);
        }
    };

    struct Runtime : System::Object
    {
        ORACLE_OBJECT(Runtime)
        Vector3 Value = Vector3::One;
        static void DescribeContent(ContentTypeDescriptor<Runtime>& d) { d.Field("Value", &Runtime::Value); }
    };

    struct WithRuntimeType : System::Object
    {
        ORACLE_OBJECT(WithRuntimeType)
        std::int32_t Sides = 3;
        static void DescribeContent(ContentTypeDescriptor<WithRuntimeType>& d)
        {
            d.RuntimeType("MyGame.RuntimeShape, MyGame");
            d.Field("Sides", &WithRuntimeType::Sides);
        }
    };

    struct WithVersion : System::Object
    {
        ORACLE_OBJECT(WithVersion)
        std::int32_t Field = 1;
        static void DescribeContent(ContentTypeDescriptor<WithVersion>& d)
        {
            d.TypeVersion(4);
            d.Field("Field", &WithVersion::Field);
        }
    };

    struct Deep : System::Object
    {
        ORACLE_OBJECT(Deep)
        std::shared_ptr<Deep> Child;
        std::int32_t Depth = 0;
        static std::shared_ptr<Deep> Build(int levels)
        {
            auto root = std::make_shared<Deep>();
            std::shared_ptr<Deep> current = root;
            for (int i = 1; i < levels; ++i)
            {
                current->Child = std::make_shared<Deep>();
                current->Child->Depth = i;
                current = current->Child;
            }
            return root;
        }
        static void DescribeContent(ContentTypeDescriptor<Deep>& d)
        {
            d.Field("Child", &Deep::Child);
            d.Field("Depth", &Deep::Depth);
        }
    };

    struct ArraysOfArrays : System::Object
    {
        ORACLE_OBJECT(ArraysOfArrays)
        std::vector<std::vector<std::int32_t>> Jagged{{1, 2}, {3}};
        std::vector<Vector2> Empty;
        static void DescribeContent(ContentTypeDescriptor<ArraysOfArrays>& d)
        {
            d.Field("Jagged", &ArraysOfArrays::Jagged);
            d.Field("Empty", &ArraysOfArrays::Empty);
        }
    };
#undef ORACLE_OBJECT
}

CNA_XNA_CONTENT_ENUM(Cna::Xna40::IntermediateOracle::Mood, "Cna.Xna40.IntermediateOracle.Mood", false,
                     {Cna::Xna40::IntermediateOracle::Mood::Happy, "Happy"},
                     {Cna::Xna40::IntermediateOracle::Mood::Sad, "Sad"},
                     {Cna::Xna40::IntermediateOracle::Mood::Angry, "Angry"});
CNA_XNA_CONTENT_ENUM(Cna::Xna40::IntermediateOracle::Toppings, "Cna.Xna40.IntermediateOracle.Toppings", true,
                     {Cna::Xna40::IntermediateOracle::Toppings::None, "None"},
                     {Cna::Xna40::IntermediateOracle::Toppings::Cheese, "Cheese"},
                     {Cna::Xna40::IntermediateOracle::Toppings::Ham, "Ham"},
                     {Cna::Xna40::IntermediateOracle::Toppings::Olives, "Olives"});

// -------------------------------------------------------------------------------------------------
// Harness
// -------------------------------------------------------------------------------------------------
namespace
{
    using namespace Cna::Xna40::IntermediateOracle;

    // The oracle ran with the repository root as its Windows current directory.
    const std::string kOracleRelocation = "Z:\\rv\\data\\development\\github.com\\openeggbert\\cnatmp\\Levels\\level.xml";

    std::filesystem::path CorpusDirectory()
    {
        const std::filesystem::path relative = "tests/reference/xna40/intermediate";
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative / "manifest.json"))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        std::filesystem::path source(__FILE__);
        for (std::filesystem::path dir = source.parent_path(); !dir.empty(); dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative / "manifest.json"))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    std::string ReadCorpus(const std::string& file)
    {
        std::ifstream in(CorpusDirectory() / file, std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        std::erase(text, '\r');
        return text;
    }

    bool CorpusHas(const std::string& file) { return std::filesystem::exists(CorpusDirectory() / file); }

    struct ManifestCase
    {
        std::string name;
        std::string rootType;
        std::string status;
        std::string note;
    };

    std::string Unescape(std::string text)
    {
        std::string out;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '\\' && i + 1 < text.size())
            {
                const char next = text[++i];
                out += next == 'n' ? '\n' : next;
            }
            else
            {
                out += text[i];
            }
        }
        return out;
    }

    std::vector<ManifestCase> ReadManifest()
    {
        std::vector<ManifestCase> cases;
        std::istringstream lines(ReadCorpus("manifest.json"));
        const std::regex pattern("\\{\"case\": \"([^\"]*)\", \"rootType\": \"((?:[^\"\\\\]|\\\\.)*)\", \"status\": \"([^\"]*)\", \"note\": \"((?:[^\"\\\\]|\\\\.)*)\"\\}");
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, pattern))
            {
                cases.push_back(ManifestCase{match[1], Unescape(match[2]), match[3], Unescape(match[4])});
            }
        }
        return cases;
    }

    template<typename T>
    std::string SerializeToString(const Carrier<T>& value, const std::string& relocation = std::string())
    {
        System::Xml::XmlWriterSettings settings;
        settings.Indent = true;
        settings.NewLineChars = "\n";
        std::unique_ptr<System::Xml::XmlWriter> writer(System::Xml::XmlWriter::CreateToString(settings));
        IntermediateSerializer::Serialize<T>(*writer, value, relocation);
        return writer->ToString();
    }

    template<typename T>
    Carrier<T> DeserializeString(const std::string& xml, const std::string& relocation = std::string())
    {
        std::unique_ptr<System::Xml::XmlReader> reader(System::Xml::XmlReader::CreateFromString(xml));
        return IntermediateSerializer::Deserialize<T>(*reader, relocation);
    }

    /** @brief Re-serializes what the reader produced: the corpus's own round-trip and normalization. */
    using RoundTrip = std::function<std::string(const std::string& xml, const std::string& relocation)>;

    template<typename T>
    RoundTrip MakeRoundTrip()
    {
        return [](const std::string& xml, const std::string& relocation)
        {
            Carrier<T> value = DeserializeString<T>(xml, relocation);
            return SerializeToString<T>(value, relocation);
        };
    }

    std::map<std::string, RoundTrip> RoundTrips()
    {
        std::map<std::string, RoundTrip> map;
        const auto add = [&map](const std::string& dotNetName, RoundTrip trip)
        { map[IntermediateSerializer::CanonicalTypeName(dotNetName)] = std::move(trip); };
        add("Cna.Xna40.IntermediateOracle.Primitives", MakeRoundTrip<Primitives>());
        add("Cna.Xna40.IntermediateOracle.FloatEdges", MakeRoundTrip<FloatEdges>());
        add("Cna.Xna40.IntermediateOracle.StringEdges", MakeRoundTrip<StringEdges>());
        add("Cna.Xna40.IntermediateOracle.NulCharacter", MakeRoundTrip<NulCharacter>());
        add("Cna.Xna40.IntermediateOracle.MathTypes", MakeRoundTrip<MathTypes>());
        add("Cna.Xna40.IntermediateOracle.Enums", MakeRoundTrip<Enums>());
        add("Cna.Xna40.IntermediateOracle.Nested", MakeRoundTrip<Nested>());
        add("Cna.Xna40.IntermediateOracle.Vertex", MakeRoundTrip<Vertex>());
        add("Cna.Xna40.IntermediateOracle.Collections", MakeRoundTrip<Collections>());
        add("Cna.Xna40.IntermediateOracle.Nullables", MakeRoundTrip<Nullables>());
        add("Cna.Xna40.IntermediateOracle.Animal", MakeRoundTrip<Animal>());
        add("Cna.Xna40.IntermediateOracle.Dog", MakeRoundTrip<Dog>());
        add("Cna.Xna40.IntermediateOracle.Shape", MakeRoundTrip<Shape>());
        add("Cna.Xna40.IntermediateOracle.Polymorphism", MakeRoundTrip<Polymorphism>());
        add("Cna.Xna40.IntermediateOracle.SharedResources", MakeRoundTrip<SharedResources>());
        add("Cna.Xna40.IntermediateOracle.ExternalReferences", MakeRoundTrip<ExternalReferences>());
        add("Cna.Xna40.IntermediateOracle.PackedCollections", MakeRoundTrip<PackedCollections>());
        add("Cna.Xna40.IntermediateOracle.Both", MakeRoundTrip<Both>());
        add("Cna.Xna40.IntermediateOracle.Node", MakeRoundTrip<Node>());
        add("Cna.Xna40.IntermediateOracle.Attributes", MakeRoundTrip<Attributes>());
        add("Cna.Xna40.IntermediateOracle.Runtime", MakeRoundTrip<Runtime>());
        add("Cna.Xna40.IntermediateOracle.WithRuntimeType", MakeRoundTrip<WithRuntimeType>());
        add("Cna.Xna40.IntermediateOracle.WithVersion", MakeRoundTrip<WithVersion>());
        add("Cna.Xna40.IntermediateOracle.Deep", MakeRoundTrip<Deep>());
        add("Cna.Xna40.IntermediateOracle.ArraysOfArrays", MakeRoundTrip<ArraysOfArrays>());
        add("Cna.Xna40.IntermediateOracle.Mood", MakeRoundTrip<Mood>());
        add("Cna.Xna40.IntermediateOracle.Toppings", MakeRoundTrip<Toppings>());
        add("System.Boolean", MakeRoundTrip<bool>());
        add("System.Byte", MakeRoundTrip<std::uint8_t>());
        add("System.SByte", MakeRoundTrip<std::int8_t>());
        add("System.Int16", MakeRoundTrip<std::int16_t>());
        add("System.UInt16", MakeRoundTrip<std::uint16_t>());
        add("System.Int32", MakeRoundTrip<std::int32_t>());
        add("System.UInt32", MakeRoundTrip<std::uint32_t>());
        add("System.Int64", MakeRoundTrip<std::int64_t>());
        add("System.UInt64", MakeRoundTrip<std::uint64_t>());
        add("System.Single", MakeRoundTrip<float>());
        add("System.Double", MakeRoundTrip<double>());
        add("System.Char", MakeRoundTrip<char16_t>());
        add("System.String", MakeRoundTrip<std::string>());
        add("System.Decimal", MakeRoundTrip<System::Decimal>());
        add("System.TimeSpan", MakeRoundTrip<System::TimeSpan>());
        add("System.DateTime", MakeRoundTrip<System::DateTime>());
        add("System.Object", MakeRoundTrip<ContentObject>());
        add("System.Nullable`1[[System.Int32]]", MakeRoundTrip<std::optional<std::int32_t>>());
        add("Microsoft.Xna.Framework.Vector2", MakeRoundTrip<Vector2>());
        add("Microsoft.Xna.Framework.Vector3", MakeRoundTrip<Vector3>());
        add("Microsoft.Xna.Framework.Vector4", MakeRoundTrip<Vector4>());
        add("Microsoft.Xna.Framework.Quaternion", MakeRoundTrip<Quaternion>());
        add("Microsoft.Xna.Framework.Matrix", MakeRoundTrip<Matrix>());
        add("Microsoft.Xna.Framework.Color", MakeRoundTrip<Color>());
        add("Microsoft.Xna.Framework.Rectangle", MakeRoundTrip<Rectangle>());
        add("Microsoft.Xna.Framework.Point", MakeRoundTrip<Point>());
        add("Microsoft.Xna.Framework.Plane", MakeRoundTrip<Plane>());
        add("Microsoft.Xna.Framework.BoundingBox", MakeRoundTrip<BoundingBox>());
        add("Microsoft.Xna.Framework.BoundingSphere", MakeRoundTrip<BoundingSphere>());
        add("Microsoft.Xna.Framework.Ray", MakeRoundTrip<Ray>());
        add("Microsoft.Xna.Framework.Curve", MakeRoundTrip<Curve>());
        add("System.Collections.Generic.List`1[[System.Int32]]", MakeRoundTrip<std::vector<std::int32_t>>());
        add("System.Int32[]", MakeRoundTrip<std::vector<std::int32_t>>());
        add("System.String[]", MakeRoundTrip<std::vector<std::string>>());
        add("System.Byte[]", MakeRoundTrip<std::vector<std::uint8_t>>());
        add("System.Collections.Generic.List`1[[System.Char]]", MakeRoundTrip<std::vector<char16_t>>());
        add("System.Collections.Generic.List`1[[System.Object]]", MakeRoundTrip<std::vector<ContentObject>>());
        add("System.Collections.Generic.List`1[[Microsoft.Xna.Framework.Vector3]]", MakeRoundTrip<std::vector<Vector3>>());
        add("System.Collections.Generic.Dictionary`2[[System.String],[Microsoft.Xna.Framework.Vector2]]",
            MakeRoundTrip<std::map<std::string, Vector2>>());
        add("System.Collections.Generic.Dictionary`2[[System.String],[System.Int32]]",
            MakeRoundTrip<std::map<std::string, std::int32_t>>());
        return map;
    }

    /** @brief Fresh objects with the oracle's initial values, by case name. */
    std::map<std::string, std::function<std::string()>> FreshWriters()
    {
        std::map<std::string, std::function<std::string()>> map;
        map["primitives"] = [] { return SerializeToString<Primitives>(std::make_shared<Primitives>()); };
        map["float_edges"] = [] { return SerializeToString<FloatEdges>(std::make_shared<FloatEdges>()); };
        map["string_edges"] = [] { return SerializeToString<StringEdges>(std::make_shared<StringEdges>()); };
        map["math_types"] = [] { return SerializeToString<MathTypes>(std::make_shared<MathTypes>()); };
        map["enums"] = [] { return SerializeToString<Enums>(std::make_shared<Enums>()); };
        map["collections"] = [] { return SerializeToString<Collections>(std::make_shared<Collections>()); };
        map["nullables"] = [] { return SerializeToString<Nullables>(std::make_shared<Nullables>()); };
        map["polymorphism"] = [] { return SerializeToString<Polymorphism>(std::make_shared<Polymorphism>()); };
        map["shared_resources"] = [] { return SerializeToString<SharedResources>(std::make_shared<SharedResources>()); };
        map["attributes"] = [] { return SerializeToString<Attributes>(std::make_shared<Attributes>()); };
        map["packed_collections"] = [] { return SerializeToString<PackedCollections>(std::make_shared<PackedCollections>()); };
        map["both_sections"] = [] { return SerializeToString<Both>(std::make_shared<Both>()); };
        map["shared_cycle"] = []
        {
            auto a = std::make_shared<Node>();
            a->Name = "a";
            auto b = std::make_shared<Node>();
            b->Name = "b";
            a->Next = b;
            b->Next = a;
            return SerializeToString<Node>(a);
        };
        map["shared_self"] = []
        {
            auto self = std::make_shared<Node>();
            self->Name = "self";
            self->Next = self;
            return SerializeToString<Node>(self);
        };
        map["runtime_type"] = [] { return SerializeToString<WithRuntimeType>(std::make_shared<WithRuntimeType>()); };
        map["type_version"] = [] { return SerializeToString<WithVersion>(std::make_shared<WithVersion>()); };
        map["deep"] = [] { return SerializeToString<Deep>(Deep::Build(20)); };
        map["arrays_of_arrays"] = [] { return SerializeToString<ArraysOfArrays>(std::make_shared<ArraysOfArrays>()); };
        map["root_int"] = [] { return SerializeToString<std::int32_t>(42); };
        map["root_string"] = [] { return SerializeToString<std::string>("just a string"); };
        map["root_vector3"] = [] { return SerializeToString<Vector3>(Vector3(1, 2, 3)); };
        map["root_list_int"] = [] { return SerializeToString<std::vector<std::int32_t>>({1, 2, 3}); };
        map["root_dictionary"] = [] { return SerializeToString<std::map<std::string, Vector2>>({{"a", Vector2(1, 2)}}); };
        map["root_nested"] = [] { return SerializeToString<Nested>(std::make_shared<Nested>()); };
        map["root_struct"] = [] { return SerializeToString<Vertex>(Vertex{Vector3(1, 2, 3), 1}); };
        map["root_enum"] = [] { return SerializeToString<Mood>(Mood::Happy); };
        map["root_object_int"] = [] { return SerializeToString<ContentObject>(Box<std::int32_t>(7)); };
        map["root_dog_as_animal"] = [] { return SerializeToString<Animal>(std::make_shared<Dog>()); };
        map["root_color"] = [] { return SerializeToString<Color>(Color::CornflowerBlue); };
        map["root_matrix"] = [] { return SerializeToString<Matrix>(Matrix::getIdentityProperty()); };
        map["root_char_list"] = [] { return SerializeToString<std::vector<char16_t>>({u'a', u' ', u'<'}); };
        map["root_bool"] = [] { return SerializeToString<bool>(true); };
        map["root_float_nan"] = [] { return SerializeToString<float>(std::numeric_limits<float>::quiet_NaN()); };
        map["root_timespan"] = [] { return SerializeToString<System::TimeSpan>(System::TimeSpan::FromSeconds(90)); };
        map["root_datetime"] = [] { return SerializeToString<System::DateTime>(UtcDateTime(2000, 1, 2, 3, 4, 5)); };
        map["root_decimal"] = [] { return SerializeToString<System::Decimal>(ParseDecimal("1.5")); };
        map["root_uint"] = [] { return SerializeToString<std::uint32_t>(3000000000u); };
        map["root_long"] = [] { return SerializeToString<std::int64_t>(-5); };
        map["root_ulong"] = [] { return SerializeToString<std::uint64_t>(std::numeric_limits<std::uint64_t>::max()); };
        map["root_short"] = [] { return SerializeToString<std::int16_t>(-7); };
        map["root_ushort"] = [] { return SerializeToString<std::uint16_t>(7); };
        map["root_sbyte"] = [] { return SerializeToString<std::int8_t>(-8); };
        map["root_byte"] = [] { return SerializeToString<std::uint8_t>(9); };
        map["root_object_string"] = [] { return SerializeToString<ContentObject>(Box<std::string>("boxed")); };
        map["root_object_enum"] = [] { return SerializeToString<ContentObject>(Box<Mood>(Mood::Sad)); };
        map["root_object_vector3"] = [] { return SerializeToString<ContentObject>(Box<Vector3>(Vector3::One)); };
        map["root_list_object"] = [] { return SerializeToString<std::vector<ContentObject>>({Box<std::int32_t>(1), Box<std::string>("s")}); };
        map["root_float_negzero"] = [] { return SerializeToString<float>(-0.0f); };
        map["root_double_third"] = [] { return SerializeToString<double>(1.0 / 3.0); };
        map["root_float_third"] = [] { return SerializeToString<float>(1.0f / 3.0f); };
        map["root_string_whitespace"] = [] { return SerializeToString<std::string>("  keep  "); };
        map["root_string_empty"] = [] { return SerializeToString<std::string>(""); };
        map["root_double"] = [] { return SerializeToString<double>(2.5); };
        map["root_char"] = [] { return SerializeToString<char16_t>(u'q'); };
        map["root_nullable_int"] = [] { return SerializeToString<std::optional<std::int32_t>>(5); };
        map["root_rectangle"] = [] { return SerializeToString<Rectangle>(Rectangle(1, 2, 3, 4)); };
        map["root_point"] = [] { return SerializeToString<Point>(Point(5, 6)); };
        map["root_quaternion"] = [] { return SerializeToString<Quaternion>(Quaternion::Identity); };
        map["root_vector2"] = [] { return SerializeToString<Vector2>(Vector2(1, 2)); };
        map["root_vector4"] = [] { return SerializeToString<Vector4>(Vector4(1, 2, 3, 4)); };
        map["root_boundingbox"] = [] { return SerializeToString<BoundingBox>(BoundingBox(Vector3::Zero, Vector3::One)); };
        map["root_boundingsphere"] = [] { return SerializeToString<BoundingSphere>(BoundingSphere(Vector3::Zero, 2)); };
        map["root_plane"] = [] { return SerializeToString<Plane>(Plane(Vector3::Up, 1)); };
        map["root_ray"] = [] { return SerializeToString<Ray>(Ray(Vector3::Zero, Vector3::Forward)); };
        map["root_curve"] = [] { return SerializeToString<Curve>(MakeCurve()); };
        map["root_runtime"] = [] { return SerializeToString<Runtime>(std::make_shared<Runtime>()); };
        // C# arrays and lists are one C++ type; the writer spells `List` (documented divergence).
        map["root_array_string"] = [] { return SerializeToString<std::vector<std::string>>({"a", "b"}); };
        map["root_bytes"] = [] { return SerializeToString<std::vector<std::uint8_t>>({1, 2, 3, 250}); };
        return map;
    }

    /** @brief Cases whose text depends on the machine the oracle ran on (absolute paths) or on a
     *  C#-only spelling; compared structurally. */
    std::string NeutralizePaths(std::string xml)
    {
        static const std::regex entry("(<ExternalReference ID=\"#External[0-9]+\" TargetType=\"[^\"]*\">)[^<]*(</ExternalReference>)");
        xml = std::regex_replace(xml, entry, "$1<path>$2");
        // `T[]` spellings in the corpus are C# arrays; CNA's std::vector is a List.
        xml = std::regex_replace(xml, std::regex("Type=\"string\\[\\]\""), "Type=\"Generic:List[string]\"");
        xml = std::regex_replace(xml, std::regex("Type=\"byte\\[\\]\""), "Type=\"Generic:List[byte]\"");
        xml = std::regex_replace(xml, std::regex("Type=\"int\\[\\]\""), "Type=\"Generic:List[int]\"");
        xml = std::regex_replace(xml, std::regex("<XnaContent>\n  <Asset Type=\"Generic:List"),
                                 "<XnaContent xmlns:Generic=\"System.Collections.Generic\">\n  <Asset Type=\"Generic:List");
        return xml;
    }

    std::string StripLocation(std::string message)
    {
        static const std::regex location(" ?Line [0-9]+, position [0-9]+\\.");
        message = std::regex_replace(message, location, "");
        while (!message.empty() && (message.back() == ' ' || message.back() == '\n' || message.back() == '\r'))
        {
            message.pop_back();
        }
        return message;
    }

    /** @brief The message the manifest recorded, without the exception type XNA threw. */
    std::string ExpectedMessage(const std::string& note)
    {
        std::string message = note;
        const std::size_t colon = message.find(": ");
        if (colon != std::string::npos)
        {
            message = message.substr(colon + 2);
        }
        return StripLocation(message);
    }

    std::string RelocationFor(const std::string& caseName)
    {
        if (caseName.rfind("accept_external_relocated_", 0) == 0 || caseName.rfind("accept_both_", 0) == 0)
        {
            return kOracleRelocation;
        }
        return std::string();
    }

    void RegisterOracleTypes()
    {
        // The oracle's wrong-target-type case names a real XNA type CNA does not model yet (Phase 6);
        // XNA knows it exists, so the name is registered as known.
        IntermediateSerializer::RegisterKnownTypeName("Microsoft.Xna.Framework.Content.Pipeline.Graphics.TextureContent");
        IntermediateSerializer::TypeSerializerFor<Dog>();
        IntermediateSerializer::TypeSerializerFor<Cat>();
        IntermediateSerializer::TypeSerializerFor<Circle>();
        IntermediateSerializer::TypeSerializerFor<Nested>();
        IntermediateSerializer::TypeSerializerFor<Referenced>();
        IntermediateSerializer::TypeSerializerFor<Primitives>();
        IntermediateSerializer::TypeSerializerFor<std::vector<std::shared_ptr<Referenced>>>();
    }

    class XnaIntermediateSerializerCorpus : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            RegisterOracleTypes();
            ASSERT_TRUE(CorpusHas("manifest.json")) << "corpus not found from " << std::filesystem::current_path();
        }
    };
}

TEST_F(XnaIntermediateSerializerCorpus, FreshObjectsSerializeExactlyAsXnaWrote)
{
    const auto writers = FreshWriters();
    std::vector<std::string> covered;
    for (const ManifestCase& item : ReadManifest())
    {
        if (item.status != "written" && item.status != "round-trip-differs")
        {
            continue;
        }
        const auto writer = writers.find(item.name);
        if (writer == writers.end())
        {
            continue;
        }
        covered.push_back(item.name);
        std::string expected = ReadCorpus(item.name + ".xml");
        std::string actual;
        try
        {
            actual = writer->second();
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << item.name << ": serialization threw " << error.what();
            continue;
        }
        EXPECT_EQ(NeutralizePaths(actual), NeutralizePaths(expected)) << item.name;
    }
    EXPECT_GE(covered.size(), 60u);
}

TEST_F(XnaIntermediateSerializerCorpus, WrittenDocumentsRoundTripThroughCnaUnchanged)
{
    const auto trips = RoundTrips();
    std::size_t covered = 0;
    for (const ManifestCase& item : ReadManifest())
    {
        if (item.status != "written" && item.status != "round-trip-differs")
        {
            continue;
        }
        const auto trip = trips.find(IntermediateSerializer::CanonicalTypeName(item.rootType));
        if (trip == trips.end())
        {
            ADD_FAILURE() << item.name << ": no round-trip handler for " << item.rootType;
            continue;
        }
        ++covered;
        const std::string relocation = item.name.rfind("external_references_same", 0) == 0 ? kOracleRelocation : std::string();
        const std::string source = ReadCorpus(item.name + ".xml");
        // XNA appends to a get-only collection instead of replacing it, so its own round trip of
        // `attributes` doubled GetOnlyList; the corpus keeps that text as the expectation.
        // The oracle re-serialized through a StringWriter, whose declaration says utf-16.
        const std::string expected = CorpusHas(item.name + ".roundtrip.xml")
                                         ? std::regex_replace(ReadCorpus(item.name + ".roundtrip.xml"), std::regex("encoding=\"utf-16\""), "encoding=\"utf-8\"")
                                         : source;
        std::string actual;
        try
        {
            actual = trip->second(source, relocation);
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << item.name << ": round trip threw " << error.what();
            continue;
        }
        EXPECT_EQ(NeutralizePaths(actual), NeutralizePaths(expected)) << item.name;
    }
    EXPECT_GE(covered, 70u);
}

TEST_F(XnaIntermediateSerializerCorpus, AcceptedInputsNormalizeExactlyAsXnaDid)
{
    const auto trips = RoundTrips();
    std::size_t covered = 0;
    for (const ManifestCase& item : ReadManifest())
    {
        if (item.status != "accepted")
        {
            continue;
        }
        const auto trip = trips.find(IntermediateSerializer::CanonicalTypeName(item.rootType));
        if (trip == trips.end())
        {
            ADD_FAILURE() << item.name << ": no handler for " << item.rootType;
            continue;
        }
        ++covered;
        const std::string input = ReadCorpus(item.name + ".input.xml");
        const std::string expected = ReadCorpus(item.name + ".normalized.xml");
        std::string actual;
        // tinyxml2 cannot parse a processing instruction inside an element (a substrate limit,
        // recorded in docs/xna-intermediate-xml-format.md).
        static const std::set<std::string> substrateLimits = {"accept_processing_instruction"};
        try
        {
            actual = trip->second(input, RelocationFor(item.name));
        }
        catch (const std::exception& error)
        {
            if (substrateLimits.count(item.name) == 0)
            {
                ADD_FAILURE() << item.name << ": XNA accepted this input; CNA threw " << error.what();
            }
            continue;
        }
        EXPECT_EQ(NeutralizePaths(actual), NeutralizePaths(expected)) << item.name;
    }
    EXPECT_GE(covered, 80u);
}

TEST_F(XnaIntermediateSerializerCorpus, RejectedInputsAreRefusedWithXnaMessages)
{
    const auto trips = RoundTrips();
    std::size_t covered = 0;
    for (const ManifestCase& item : ReadManifest())
    {
        if (item.status != "rejected")
        {
            continue;
        }
        const auto trip = trips.find(IntermediateSerializer::CanonicalTypeName(item.rootType));
        if (trip == trips.end())
        {
            ADD_FAILURE() << item.name << ": no handler for " << item.rootType;
            continue;
        }
        ++covered;
        const std::string input = ReadCorpus(item.name + ".input.xml");
        try
        {
            const std::string produced = trip->second(input, RelocationFor(item.name));
            // Recorded lenience: CNA accepts these inputs that XNA refuses (docs/xna-intermediate-xml-format.md).
            static const std::set<std::string> deliberatelyAccepted = {
                "accept_empty_resources_selfclosing", "accept_empty_externals_selfclosing",
                "accept_resources_then_externals", "accept_int_array_from_list_type", "accept_list_from_array_type",
                // tinyxml2 tolerates a byte-order mark inside the text handed to it; .NET's string
                // reader does not (substrate difference, recorded).
                "accept_bom_and_declaration"};
            if (deliberatelyAccepted.count(item.name) == 0)
            {
                ADD_FAILURE() << item.name << ": XNA refused this input (" << item.note << "); CNA produced\n" << produced;
            }
        }
        catch (const InvalidContentException& error)
        {
            // XNA crashes with a NullReferenceException on Null="true" for a value type; CNA refuses
            // with a message that names the member and type (recorded divergence).
            static const std::set<std::string> ownMessage = {"accept_null_on_value_type"};
            if (ownMessage.count(item.name) == 0)
            {
                EXPECT_EQ(StripLocation(error.getMessageProperty()), ExpectedMessage(item.note)) << item.name;
            }
        }
        catch (const std::exception& error)
        {
            ADD_FAILURE() << item.name << ": expected InvalidContentException, got " << error.what();
        }
    }
    EXPECT_GE(covered, 90u);
}

TEST_F(XnaIntermediateSerializerCorpus, GraphsXnaCouldNotWriteAreRefusedTooWhereTheyExist)
{
    EXPECT_ANY_THROW(SerializeToString<NulCharacter>(std::make_shared<NulCharacter>()));
    EXPECT_ANY_THROW(SerializeToString<std::optional<std::string>>(std::nullopt));
}

TEST_F(XnaIntermediateSerializerCorpus, DeserializedValuesCarryTheCorpusData)
{
    auto primitives = DeserializeString<Primitives>(ReadCorpus("primitives.xml"));
    EXPECT_EQ(primitives->Int, -123456789);
    EXPECT_EQ(primitives->ULong, 18000000000000000000ULL);
    EXPECT_EQ(primitives->String, "hello world");
    EXPECT_EQ(primitives->Char, u'x');
    EXPECT_EQ(primitives->TimeSpan, System::TimeSpan::FromMilliseconds(1500));

    auto shared = DeserializeString<SharedResources>(ReadCorpus("shared_resources.xml"));
    EXPECT_EQ(shared->First, shared->Second) << "one resource, two references";
    EXPECT_NE(shared->First, shared->Other);
    ASSERT_TRUE(shared->SharedList);
    ASSERT_EQ(shared->SharedList->size(), 2u);
    // The list's items are not shared-resource members, so XNA wrote them inline as copies
    // (shared_resources.xml) and reading gives distinct objects with equal values.
    EXPECT_NE((*shared->SharedList)[0], shared->First);
    EXPECT_EQ((*shared->SharedList)[0]->Label, "shared");
    EXPECT_EQ((*shared->SharedList)[1]->Label, "other");
    EXPECT_EQ(shared->NullShared, nullptr);

    auto cycle = DeserializeString<Node>(ReadCorpus("shared_cycle.xml"));
    ASSERT_TRUE(cycle->Next);
    ASSERT_TRUE(cycle->Next->Next);
    EXPECT_EQ(cycle->Next->Next->Next, cycle->Next) << "the cycle closes on the resource, not the root";

    auto poly = DeserializeString<Polymorphism>(ReadCorpus("polymorphism.xml"));
    ASSERT_TRUE(std::dynamic_pointer_cast<Dog>(poly->DeclaredBase));
    EXPECT_EQ(std::dynamic_pointer_cast<Dog>(poly->DeclaredBase)->Name, "rex");
    ASSERT_TRUE(std::dynamic_pointer_cast<Circle>(poly->ViaAbstract));
    EXPECT_TRUE(Microsoft::Xna::Framework::Content::Pipeline::Holds<std::int32_t>(poly->BoxedInt));
    EXPECT_TRUE(poly->NullObject.Empty());
    ASSERT_EQ(poly->Mixed.size(), 3u);
    EXPECT_TRUE(std::dynamic_pointer_cast<Cat>(poly->Mixed[1]));

    auto attributes = DeserializeString<Attributes>(ReadCorpus("attributes.xml"));
    EXPECT_EQ(attributes->Ignored, 99) << "an ignored member keeps its constructor value";
    EXPECT_FALSE(attributes->OptionalNull.has_value());
    EXPECT_EQ(attributes->FlattenedList, (std::vector<std::int32_t>{1, 2}));
    EXPECT_EQ(attributes->getOnlyList, (std::vector<std::int32_t>{11, 12, 11, 12})) << "XNA appends to a get-only collection";

    auto externals = DeserializeString<ExternalReferences>(ReadCorpus("external_references_samedrive.xml"), kOracleRelocation);
    EXPECT_EQ(externals->Texture->getFilenameProperty(), "Z:/rv/data/development/github.com/openeggbert/cnatmp/Textures/wall.png");
    EXPECT_EQ(externals->Null, nullptr);
    ASSERT_EQ(externals->List.size(), 2u);
}
