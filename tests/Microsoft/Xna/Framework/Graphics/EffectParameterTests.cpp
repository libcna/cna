// SPDX-License-Identifier: MS-PL
// Task 25: EffectParameter unit tests.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Helper: build a scalar float parameter
static EffectParameter MakeScalar(const std::string& name = "param",
                                   const std::string& sem  = "")
{
    return EffectParameter(name, sem, 1, 1,
                           EffectParameterClass::Scalar,
                           EffectParameterType::Single);
}

// Helper: build a matrix parameter (4×4)
static EffectParameter MakeMatrix(const std::string& name = "mat")
{
    return EffectParameter(name, "", 4, 4,
                           EffectParameterClass::Matrix,
                           EffectParameterType::Single);
}

// Helper: build a vector3 parameter
static EffectParameter MakeVec3(const std::string& name = "vec")
{
    return EffectParameter(name, "", 1, 3,
                           EffectParameterClass::Vector,
                           EffectParameterType::Single);
}

// --- Metadata ---

TEST(EffectParameterTest, NameReturnsConstructorName)
{
    auto p = MakeScalar("MyParam");
    EXPECT_EQ(p.getNameProperty(), "MyParam");
}

TEST(EffectParameterTest, SemanticReturnsConstructorSemantic)
{
    EffectParameter p("pos", "POSITION", 1, 3,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    EXPECT_EQ(p.getSemanticProperty(), "POSITION");
}

TEST(EffectParameterTest, RowColumnCountReturnedCorrectly)
{
    EffectParameter p("m", "", 4, 4,
                      EffectParameterClass::Matrix,
                      EffectParameterType::Single);
    EXPECT_EQ(p.getRowCountProperty(),    4);
    EXPECT_EQ(p.getColumnCountProperty(), 4);
}

TEST(EffectParameterTest, ParameterClassReturnedCorrectly)
{
    auto p = MakeScalar();
    EXPECT_EQ(p.getParameterClassProperty(), EffectParameterClass::Scalar);
}

TEST(EffectParameterTest, ParameterTypeReturnedCorrectly)
{
    auto p = MakeScalar();
    EXPECT_EQ(p.getParameterTypeProperty(), EffectParameterType::Single);
}

// --- GetValueSingle / SetValue(float) ---

TEST(EffectParameterTest, SetValueSingleRoundTrip)
{
    auto p = MakeScalar();
    p.SetValue(3.14f);
    EXPECT_NEAR(p.GetValueSingle(), 3.14f, 1e-5f);
}

TEST(EffectParameterTest, SetValueSingleArrayRoundTrip)
{
    auto p = MakeScalar();
    std::vector<float> vals = {1.0f, 2.0f, 3.0f};
    p.SetValue(vals);
    auto got = p.GetValueSingleArray(3);
    ASSERT_EQ(got.size(), 3u);
    for (int i = 0; i < 3; ++i)
        EXPECT_NEAR(got[static_cast<std::size_t>(i)], vals[static_cast<std::size_t>(i)], 1e-5f);
}

// --- GetValueBoolean / SetValue(bool) ---

TEST(EffectParameterTest, SetValueBoolRoundTrip)
{
    EffectParameter p("b", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Bool);
    p.SetValue(true);
    EXPECT_TRUE(p.GetValueBoolean());
    p.SetValue(false);
    EXPECT_FALSE(p.GetValueBoolean());
}

// --- GetValueInt32 / SetValue(int) ---

TEST(EffectParameterTest, SetValueIntRoundTrip)
{
    EffectParameter p("i", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Int32);
    p.SetValue(42);
    EXPECT_EQ(p.GetValueInt32(), 42);
    p.SetValue(-7);
    EXPECT_EQ(p.GetValueInt32(), -7);
}

// --- GetValueVector3 / SetValue(Vector3) ---

TEST(EffectParameterTest, SetValueVector3RoundTrip)
{
    auto p = MakeVec3();
    const Vector3 v{1.0f, 2.0f, 3.0f};
    p.SetValue(v);
    auto got = p.GetValueVector3();
    EXPECT_NEAR(got.X, 1.0f, 1e-5f);
    EXPECT_NEAR(got.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(got.Z, 3.0f, 1e-5f);
}

TEST(EffectParameterTest, SetValueVector3ArrayRoundTrip)
{
    auto p = MakeVec3();
    std::vector<Vector3> vecs = {{1,0,0},{0,1,0},{0,0,1}};
    p.SetValue(vecs);
    auto got = p.GetValueVector3Array(3);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_NEAR(got[0].X, 1.0f, 1e-5f);
    EXPECT_NEAR(got[1].Y, 1.0f, 1e-5f);
    EXPECT_NEAR(got[2].Z, 1.0f, 1e-5f);
}

// --- GetValueVector2 / SetValue(Vector2) ---

TEST(EffectParameterTest, SetValueVector2RoundTrip)
{
    EffectParameter p("uv", "", 1, 2,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    const Vector2 v{0.5f, 0.75f};
    p.SetValue(v);
    auto got = p.GetValueVector2();
    EXPECT_NEAR(got.X, 0.5f,  1e-5f);
    EXPECT_NEAR(got.Y, 0.75f, 1e-5f);
}

// --- GetValueVector4 / SetValue(Vector4) ---

TEST(EffectParameterTest, SetValueVector4RoundTrip)
{
    EffectParameter p("col", "", 1, 4,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    const Vector4 v{0.1f, 0.2f, 0.3f, 0.4f};
    p.SetValue(v);
    auto got = p.GetValueVector4();
    EXPECT_NEAR(got.X, 0.1f, 1e-5f);
    EXPECT_NEAR(got.Y, 0.2f, 1e-5f);
    EXPECT_NEAR(got.Z, 0.3f, 1e-5f);
    EXPECT_NEAR(got.W, 0.4f, 1e-5f);
}

// --- GetValueMatrix / SetValue(Matrix) ---

TEST(EffectParameterTest, SetValueMatrixRoundTrip)
{
    auto p = MakeMatrix();
    const Matrix m = Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    p.SetValue(m);
    const Matrix got = p.GetValueMatrix();
    EXPECT_NEAR(got.M41, 5.0f, 1e-5f);
    EXPECT_NEAR(got.M42, 6.0f, 1e-5f);
    EXPECT_NEAR(got.M43, 7.0f, 1e-5f);
    EXPECT_NEAR(got.M44, 1.0f, 1e-5f);
}

TEST(EffectParameterTest, SetValueMatrixArrayRoundTrip)
{
    auto p = MakeMatrix();
    std::vector<Matrix> mats = {
        Matrix::CreateTranslation(1, 0, 0),
        Matrix::CreateTranslation(0, 2, 0),
    };
    p.SetValue(mats);
    auto got = p.GetValueMatrixArray(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_NEAR(got[0].M41, 1.0f, 1e-5f);
    EXPECT_NEAR(got[1].M42, 2.0f, 1e-5f);
}

// --- SetValueTranspose / GetValueMatrixTranspose ---

TEST(EffectParameterTest, SetValueTransposeRoundTrip)
{
    auto p = MakeMatrix();
    const Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    p.SetValueTranspose(m);
    const Matrix transposed = p.GetValueMatrixTranspose();
    // SetValueTranspose stores Transpose(m); GetValueMatrixTranspose returns Transpose again → m
    EXPECT_NEAR(transposed.M41, 1.0f, 1e-5f);
    EXPECT_NEAR(transposed.M42, 2.0f, 1e-5f);
    EXPECT_NEAR(transposed.M43, 3.0f, 1e-5f);
}

// --- GetValueQuaternion / SetValue(Quaternion) ---

TEST(EffectParameterTest, SetValueQuaternionRoundTrip)
{
    EffectParameter p("q", "", 1, 4,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    const Quaternion q{0.1f, 0.2f, 0.3f, 0.9f};
    p.SetValue(q);
    auto got = p.GetValueQuaternion();
    EXPECT_NEAR(got.X, 0.1f, 1e-5f);
    EXPECT_NEAR(got.Y, 0.2f, 1e-5f);
    EXPECT_NEAR(got.Z, 0.3f, 1e-5f);
    EXPECT_NEAR(got.W, 0.9f, 1e-5f);
}

// --- GetValueString / SetValue(string) ---

TEST(EffectParameterTest, SetValueStringRoundTrip)
{
    EffectParameter p("s", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::String);
    p.SetValue(std::string("hello"));
    EXPECT_EQ(p.GetValueString(), "hello");
}

