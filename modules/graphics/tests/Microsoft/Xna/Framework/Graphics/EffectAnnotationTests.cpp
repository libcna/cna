// SPDX-License-Identifier: MS-PL
// Task 188: EffectAnnotation and EffectAnnotationCollection unit tests.

#include <gtest/gtest.h>
#include <cstring>
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotation.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectAnnotationCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    EffectAnnotation MakeScalarFloat(const std::string& name, float value)
    {
        return EffectAnnotation(name, "", 1, 1,
                                EffectParameterClass::Scalar,
                                EffectParameterType::Single,
                                {value});
    }

    EffectAnnotation MakeStringAnnotation(const std::string& name, const std::string& str)
    {
        return EffectAnnotation(name, "", 1, 1,
                                EffectParameterClass::Object,
                                EffectParameterType::String,
                                {}, str);
    }
}

// ============================================================
// EffectAnnotation — metadata
// ============================================================

TEST(EffectAnnotationTest, NameReturnsConstructorValue)
{
    auto a = MakeScalarFloat("hint", 1.0f);
    EXPECT_EQ(a.getNameProperty(), "hint");
}

TEST(EffectAnnotationTest, SemanticReturnsConstructorValue)
{
    EffectAnnotation a("a", "MYSEM", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Single);
    EXPECT_EQ(a.getSemanticProperty(), "MYSEM");
}

TEST(EffectAnnotationTest, EmptySemanticDefaultsToEmpty)
{
    auto a = MakeScalarFloat("x", 0.0f);
    EXPECT_EQ(a.getSemanticProperty(), "");
}

TEST(EffectAnnotationTest, RowColumnCountReturned)
{
    EffectAnnotation a("m", "", 4, 4,
                       EffectParameterClass::Matrix,
                       EffectParameterType::Single);
    EXPECT_EQ(a.getRowCountProperty(),    4);
    EXPECT_EQ(a.getColumnCountProperty(), 4);
}

TEST(EffectAnnotationTest, ParameterClassReturned)
{
    auto a = MakeScalarFloat("x", 0.0f);
    EXPECT_EQ(a.getParameterClassProperty(), EffectParameterClass::Scalar);
}

TEST(EffectAnnotationTest, ParameterTypeReturned)
{
    auto a = MakeScalarFloat("x", 0.0f);
    EXPECT_EQ(a.getParameterTypeProperty(), EffectParameterType::Single);
}

// ============================================================
// EffectAnnotation — GetValue* accessors
// ============================================================

TEST(EffectAnnotationTest, GetValueSingleRoundTrip)
{
    auto a = MakeScalarFloat("f", 3.14f);
    EXPECT_NEAR(a.GetValueSingle(), 3.14f, 1e-5f);
}

TEST(EffectAnnotationTest, GetValueSingleEmptyDataReturnsZero)
{
    EffectAnnotation a("x", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Single);
    EXPECT_NEAR(a.GetValueSingle(), 0.0f, 1e-7f);
}

TEST(EffectAnnotationTest, GetValueBooleanTrueWhenNonZero)
{
    // Store a non-zero int bit-pattern as a float element.
    int one = 1;
    float f;
    std::memcpy(&f, &one, sizeof(f));
    EffectAnnotation a("b", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Bool,
                       {f});
    EXPECT_TRUE(a.GetValueBoolean());
}

TEST(EffectAnnotationTest, GetValueBooleanFalseWhenZero)
{
    int zero = 0;
    float f;
    std::memcpy(&f, &zero, sizeof(f));
    EffectAnnotation a("b", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Bool,
                       {f});
    EXPECT_FALSE(a.GetValueBoolean());
}

TEST(EffectAnnotationTest, GetValueBooleanEmptyDataReturnsFalse)
{
    EffectAnnotation a("b", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Bool);
    EXPECT_FALSE(a.GetValueBoolean());
}

TEST(EffectAnnotationTest, GetValueInt32RoundTrip)
{
    int val = 42;
    float f;
    std::memcpy(&f, &val, sizeof(f));
    EffectAnnotation a("i", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Int32,
                       {f});
    EXPECT_EQ(a.GetValueInt32(), 42);
}

TEST(EffectAnnotationTest, GetValueInt32EmptyDataReturnsZero)
{
    EffectAnnotation a("i", "", 1, 1,
                       EffectParameterClass::Scalar,
                       EffectParameterType::Int32);
    EXPECT_EQ(a.GetValueInt32(), 0);
}

TEST(EffectAnnotationTest, GetValueVector2RoundTrip)
{
    EffectAnnotation a("v2", "", 1, 2,
                       EffectParameterClass::Vector,
                       EffectParameterType::Single,
                       {1.0f, 2.0f});
    const auto v = a.GetValueVector2();
    EXPECT_NEAR(v.X, 1.0f, 1e-5f);
    EXPECT_NEAR(v.Y, 2.0f, 1e-5f);
}

TEST(EffectAnnotationTest, GetValueVector3RoundTrip)
{
    EffectAnnotation a("v3", "", 1, 3,
                       EffectParameterClass::Vector,
                       EffectParameterType::Single,
                       {1.0f, 2.0f, 3.0f});
    const auto v = a.GetValueVector3();
    EXPECT_NEAR(v.X, 1.0f, 1e-5f);
    EXPECT_NEAR(v.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(v.Z, 3.0f, 1e-5f);
}

TEST(EffectAnnotationTest, GetValueVector4RoundTrip)
{
    EffectAnnotation a("v4", "", 1, 4,
                       EffectParameterClass::Vector,
                       EffectParameterType::Single,
                       {1.0f, 2.0f, 3.0f, 4.0f});
    const auto v = a.GetValueVector4();
    EXPECT_NEAR(v.X, 1.0f, 1e-5f);
    EXPECT_NEAR(v.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(v.Z, 3.0f, 1e-5f);
    EXPECT_NEAR(v.W, 4.0f, 1e-5f);
}

TEST(EffectAnnotationTest, GetValueStringRoundTrip)
{
    auto a = MakeStringAnnotation("hint", "UIWidget=Slider");
    EXPECT_EQ(a.GetValueString(), "UIWidget=Slider");
}

TEST(EffectAnnotationTest, GetValueStringDefaultIsEmpty)
{
    auto a = MakeScalarFloat("f", 1.0f);
    EXPECT_EQ(a.GetValueString(), "");
}

TEST(EffectAnnotationTest, GetValueMatrixRoundTrip)
{
    // Column-major layout: data_[0..3] = first column, etc.
    // GetValueMatrix reads col-major → Matrix(M11,M12,...) row-major constructor.
    // Row 1 = (data_[0], data_[4], data_[8], data_[12]) in FNA column-major.
    std::vector<float> d(16, 0.0f);
    d[0] = 1.0f;   // M11 stored at data_[0]
    d[5] = 6.0f;   // M22
    EffectAnnotation a("mat", "", 4, 4,
                       EffectParameterClass::Matrix,
                       EffectParameterType::Single, d);
    const Matrix m = a.GetValueMatrix();
    EXPECT_NEAR(m.M11, 1.0f, 1e-5f);
    EXPECT_NEAR(m.M22, 6.0f, 1e-5f);
}

TEST(EffectAnnotationTest, GetValueMatrixEmptyDataReturnsIdentity)
{
    EffectAnnotation a("mat", "", 4, 4,
                       EffectParameterClass::Matrix,
                       EffectParameterType::Single);
    const Matrix m = a.GetValueMatrix();
    EXPECT_NEAR(m.M11, 1.0f, 1e-5f);
    EXPECT_NEAR(m.M22, 1.0f, 1e-5f);
    EXPECT_NEAR(m.M33, 1.0f, 1e-5f);
    EXPECT_NEAR(m.M44, 1.0f, 1e-5f);
    EXPECT_NEAR(m.M12, 0.0f, 1e-5f);
}

// ============================================================
// EffectAnnotationCollection
// ============================================================

TEST(EffectAnnotationCollectionTest, EmptyCollectionHasZeroCount)
{
    EffectAnnotationCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(EffectAnnotationCollectionTest, AddIncreasesCount)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("a", 1.0f));
    EXPECT_EQ(col.getCountProperty(), 1);
    col.Add(MakeScalarFloat("b", 2.0f));
    EXPECT_EQ(col.getCountProperty(), 2);
}

TEST(EffectAnnotationCollectionTest, IndexByIntReturnsAnnotation)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("first",  1.0f));
    col.Add(MakeScalarFloat("second", 2.0f));
    EXPECT_EQ(col[0].getNameProperty(), "first");
    EXPECT_EQ(col[1].getNameProperty(), "second");
}

TEST(EffectAnnotationCollectionTest, IndexByNameReturnsCorrectAnnotation)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("x", 10.0f));
    col.Add(MakeScalarFloat("y", 20.0f));
    const EffectAnnotation* a = col["y"];
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->getNameProperty(), "y");
    EXPECT_NEAR(a->GetValueSingle(), 20.0f, 1e-5f);
}

TEST(EffectAnnotationCollectionTest, IndexByNameReturnsNullForMissingName)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("x", 1.0f));
    EXPECT_EQ(col["NoSuch"], nullptr);
}

TEST(EffectAnnotationCollectionTest, ConstIndexByIntReturnsAnnotation)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("z", 3.0f));
    const EffectAnnotationCollection& cref = col;
    EXPECT_EQ(cref[0].getNameProperty(), "z");
}

TEST(EffectAnnotationCollectionTest, ConstIndexByNameReturnsAnnotation)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("z", 3.0f));
    const EffectAnnotationCollection& cref = col;
    const EffectAnnotation* a = cref["z"];
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->getNameProperty(), "z");
}

TEST(EffectAnnotationCollectionTest, StringAnnotationLookupAndGetValueString)
{
    EffectAnnotationCollection col;
    col.Add(MakeStringAnnotation("hint", "UIWidget=Slider"));
    const EffectAnnotation* a = col["hint"];
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->GetValueString(), "UIWidget=Slider");
}

TEST(EffectAnnotationCollectionTest, IterationVisitsAllAnnotations)
{
    EffectAnnotationCollection col;
    col.Add(MakeScalarFloat("a", 1.0f));
    col.Add(MakeScalarFloat("b", 2.0f));
    col.Add(MakeScalarFloat("c", 3.0f));
    int count = 0;
    for (const auto& a : col) { (void)a; ++count; }
    EXPECT_EQ(count, 3);
}

// ============================================================
// Annotations on EffectTechnique and EffectPass start empty
// ============================================================

TEST(EffectAnnotationCollectionTest, TechniqueAnnotationsEmptyOnConstruction)
{
    EffectTechnique t(nullptr, "T");
    EXPECT_EQ(t.getAnnotationsProperty().getCountProperty(), 0);
}

TEST(EffectAnnotationCollectionTest, PassAnnotationsEmptyOnConstruction)
{
    EffectPass p(nullptr, "P0");
    EXPECT_EQ(p.getAnnotationsProperty().getCountProperty(), 0);
}
