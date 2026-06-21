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

// --- GetValueBooleanArray / SetValue(vector<bool>) ---

TEST(EffectParameterTest, SetValueBoolArrayRoundTrip)
{
    EffectParameter p("b", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Bool);
    p.SetValue(std::vector<bool>{true, false, true});
    auto got = p.GetValueBooleanArray(3);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_TRUE(got[0]);
    EXPECT_FALSE(got[1]);
    EXPECT_TRUE(got[2]);
}

TEST(EffectParameterTest, GetValueBooleanArrayPartialCount)
{
    EffectParameter p("b", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Bool);
    p.SetValue(std::vector<bool>{true, false, true});
    auto got = p.GetValueBooleanArray(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_TRUE(got[0]);
    EXPECT_FALSE(got[1]);
}

// --- GetValueInt32Array / SetValue(vector<int>) ---

TEST(EffectParameterTest, SetValueInt32ArrayRoundTrip)
{
    EffectParameter p("i", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Int32);
    p.SetValue(std::vector<int>{10, -5, 100});
    auto got = p.GetValueInt32Array(3);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0],  10);
    EXPECT_EQ(got[1],  -5);
    EXPECT_EQ(got[2], 100);
}

TEST(EffectParameterTest, GetValueInt32ArrayPartialCount)
{
    EffectParameter p("i", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Int32);
    p.SetValue(std::vector<int>{1, 2, 3, 4});
    auto got = p.GetValueInt32Array(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_EQ(got[0], 1);
    EXPECT_EQ(got[1], 2);
}

// --- GetValueVector2Array / SetValue(vector<Vector2>) ---

TEST(EffectParameterTest, SetValueVector2ArrayRoundTrip)
{
    EffectParameter p("uv", "", 1, 2,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    std::vector<Vector2> vecs = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    p.SetValue(vecs);
    auto got = p.GetValueVector2Array(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_NEAR(got[0].X, 1.0f, 1e-5f);
    EXPECT_NEAR(got[0].Y, 2.0f, 1e-5f);
    EXPECT_NEAR(got[1].X, 3.0f, 1e-5f);
    EXPECT_NEAR(got[1].Y, 4.0f, 1e-5f);
}

// --- GetValueVector4Array / SetValue(vector<Vector4>) ---

TEST(EffectParameterTest, SetValueVector4ArrayRoundTrip)
{
    EffectParameter p("col", "", 1, 4,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    std::vector<Vector4> vecs = {{1,0,0,1}, {0,1,0,1}, {0,0,1,1}};
    p.SetValue(vecs);
    auto got = p.GetValueVector4Array(3);
    ASSERT_EQ(got.size(), 3u);
    EXPECT_NEAR(got[0].X, 1.0f, 1e-5f);
    EXPECT_NEAR(got[1].Y, 1.0f, 1e-5f);
    EXPECT_NEAR(got[2].Z, 1.0f, 1e-5f);
}

// --- GetValueQuaternionArray / SetValue(vector<Quaternion>) ---

TEST(EffectParameterTest, SetValueQuaternionArrayRoundTrip)
{
    EffectParameter p("q", "", 1, 4,
                      EffectParameterClass::Vector,
                      EffectParameterType::Single);
    std::vector<Quaternion> qs = {{0.1f, 0.2f, 0.3f, 0.9f},
                                   {0.5f, 0.5f, 0.5f, 0.5f}};
    p.SetValue(qs);
    auto got = p.GetValueQuaternionArray(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_NEAR(got[0].X, 0.1f, 1e-5f);
    EXPECT_NEAR(got[0].W, 0.9f, 1e-5f);
    EXPECT_NEAR(got[1].X, 0.5f, 1e-5f);
    EXPECT_NEAR(got[1].W, 0.5f, 1e-5f);
}

// --- SetValueTranspose(vector<Matrix>) / GetValueMatrixTransposeArray ---

TEST(EffectParameterTest, SetValueTransposeArrayRoundTrip)
{
    auto p = MakeMatrix();
    std::vector<Matrix> mats = {
        Matrix::CreateTranslation(1.0f, 0.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 2.0f, 0.0f),
    };
    p.SetValueTranspose(mats);
    // GetValueMatrixTransposeArray transposes stored values back → should match originals
    auto got = p.GetValueMatrixTransposeArray(2);
    ASSERT_EQ(got.size(), 2u);
    EXPECT_NEAR(got[0].M41, 1.0f, 1e-5f);
    EXPECT_NEAR(got[1].M42, 2.0f, 1e-5f);
}

// --- GetValueTexture2D / SetValue(Texture2D*) ---

TEST(EffectParameterTest, SetValueTexture2DNullRoundTrip)
{
    EffectParameter p("tex", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture2D);
    p.SetValue(static_cast<Texture2D*>(nullptr));
    EXPECT_EQ(p.GetValueTexture2D(), nullptr);
}

TEST(EffectParameterTest, SetValueTexture2DNonNullRoundTrip)
{
    EffectParameter p("tex", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture2D);
    // Use a sentinel address (no GPU resources needed for pointer round-trip)
    Texture2D* sentinel = reinterpret_cast<Texture2D*>(std::uintptr_t{0xDEAD});
    p.SetValue(sentinel);
    EXPECT_EQ(p.GetValueTexture2D(), sentinel);
}

// --- GetValueTexture3D / SetValue(Texture3D*) ---

TEST(EffectParameterTest, SetValueTexture3DNullRoundTrip)
{
    EffectParameter p("tex3d", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture3D);
    p.SetValue(static_cast<Texture3D*>(nullptr));
    EXPECT_EQ(p.GetValueTexture3D(), nullptr);
}

TEST(EffectParameterTest, SetValueTexture3DNonNullRoundTrip)
{
    EffectParameter p("tex3d", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture3D);
    Texture3D* sentinel = reinterpret_cast<Texture3D*>(std::uintptr_t{0xBEEF});
    p.SetValue(sentinel);
    EXPECT_EQ(p.GetValueTexture3D(), sentinel);
}

// --- GetValueTextureCube / SetValue(TextureCube*) ---

TEST(EffectParameterTest, SetValueTextureCubeNullRoundTrip)
{
    EffectParameter p("cube", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::TextureCube);
    p.SetValue(static_cast<TextureCube*>(nullptr));
    EXPECT_EQ(p.GetValueTextureCube(), nullptr);
}

TEST(EffectParameterTest, SetValueTextureCubeNonNullRoundTrip)
{
    EffectParameter p("cube", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::TextureCube);
    TextureCube* sentinel = reinterpret_cast<TextureCube*>(std::uintptr_t{0xCAFE});
    p.SetValue(sentinel);
    EXPECT_EQ(p.GetValueTextureCube(), sentinel);
}

// --- Default/initial state ---

TEST(EffectParameterTest, DefaultBooleanIsFalse)
{
    EffectParameter p("b", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Bool);
    EXPECT_FALSE(p.GetValueBoolean());
}

TEST(EffectParameterTest, DefaultInt32IsZero)
{
    EffectParameter p("i", "", 1, 1,
                      EffectParameterClass::Scalar,
                      EffectParameterType::Int32);
    EXPECT_EQ(p.GetValueInt32(), 0);
}

TEST(EffectParameterTest, DefaultSingleIsZero)
{
    auto p = MakeScalar();
    EXPECT_NEAR(p.GetValueSingle(), 0.0f, 1e-7f);
}

TEST(EffectParameterTest, DefaultStringIsEmpty)
{
    EffectParameter p("s", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::String);
    EXPECT_EQ(p.GetValueString(), "");
}

TEST(EffectParameterTest, DefaultTexture2DIsNull)
{
    EffectParameter p("tex", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture2D);
    EXPECT_EQ(p.GetValueTexture2D(), nullptr);
}

TEST(EffectParameterTest, DefaultTexture3DIsNull)
{
    EffectParameter p("tex3d", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::Texture3D);
    EXPECT_EQ(p.GetValueTexture3D(), nullptr);
}

TEST(EffectParameterTest, DefaultTextureCubeIsNull)
{
    EffectParameter p("cube", "", 1, 1,
                      EffectParameterClass::Object,
                      EffectParameterType::TextureCube);
    EXPECT_EQ(p.GetValueTextureCube(), nullptr);
}

TEST(EffectParameterTest, DefaultMatrixIsIdentity)
{
    auto p = MakeMatrix();
    const Matrix got = p.GetValueMatrix();
    EXPECT_NEAR(got.M11, 1.0f, 1e-5f);
    EXPECT_NEAR(got.M22, 1.0f, 1e-5f);
    EXPECT_NEAR(got.M33, 1.0f, 1e-5f);
    EXPECT_NEAR(got.M44, 1.0f, 1e-5f);
    EXPECT_NEAR(got.M12, 0.0f, 1e-5f);
}

