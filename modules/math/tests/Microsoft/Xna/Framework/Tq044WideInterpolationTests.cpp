// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
using namespace Microsoft::Xna::Framework;

namespace {
constexpr std::array<float Matrix::*,16> Tq044WideInterpolationTestsFields = {
    &Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,
    &Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,
    &Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,
    &Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44};
void Tq044WideInterpolationTestsExpectMatrix(const Matrix& actual, const Matrix& expected) {
    for (const auto f : Tq044WideInterpolationTestsFields) EXPECT_EQ(actual.*f, expected.*f);
}
}

#include "Microsoft/Xna/Framework/MathHelper.hpp"

TEST(Tq044WideInterpolationTest, Vector2LerpValue)
{
    // XNA Vector2.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    const V actual=V::Lerp(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector2LerpOut)
{
    // XNA Vector2.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Lerp(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector2SmoothStepValue)
{
    // XNA Vector2.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    const V actual=V::SmoothStep(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector2SmoothStepOut)
{
    // XNA Vector2.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    V actual(99);
    V::SmoothStep(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector2BarycentricValue)
{
    // XNA Vector2.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    const V actual=V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector2BarycentricOut)
{
    // XNA Vector2.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector2CatmullRomValue)
{
    // XNA Vector2.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    const V actual=V::CatmullRom(V(h),V(h),V(h),V(h),0.5f);
    EXPECT_EQ(actual,V(h));
}

TEST(Tq044WideInterpolationTest, Vector2CatmullRomOut)
{
    // XNA Vector2.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    V actual(99);
    V::CatmullRom(V(h),V(h),V(h),V(h),0.5f,actual);
    EXPECT_EQ(actual,V(h));
}

TEST(Tq044WideInterpolationTest, Vector2HermiteValue)
{
    // XNA Vector2.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    const V actual=V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector2HermiteOut)
{
    // XNA Vector2.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector2;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector3LerpValue)
{
    // XNA Vector3.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    const V actual=V::Lerp(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector3LerpOut)
{
    // XNA Vector3.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Lerp(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector3SmoothStepValue)
{
    // XNA Vector3.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    const V actual=V::SmoothStep(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector3SmoothStepOut)
{
    // XNA Vector3.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    V actual(99);
    V::SmoothStep(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector3BarycentricValue)
{
    // XNA Vector3.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    const V actual=V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector3BarycentricOut)
{
    // XNA Vector3.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector3CatmullRomValue)
{
    // XNA Vector3.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    const V actual=V::CatmullRom(V(h),V(h),V(h),V(h),0.5f);
    EXPECT_EQ(actual,V(h));

    // Catmull(-h,0,h,0,1/2)=1/2*(h+h/2-h/4)=5h/8, exactly 0x1.4p126.
    // The scalar helper must not store (value3-value1)=2h as a Single.
    EXPECT_EQ(V::CatmullRom(V(-h),V(0),V(h),V(0),0.5f),V(0x1.4p126f));
}

TEST(Tq044WideInterpolationTest, Vector3CatmullRomOut)
{
    // XNA Vector3.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    V actual(99);
    V::CatmullRom(V(h),V(h),V(h),V(h),0.5f,actual);
    EXPECT_EQ(actual,V(h));

    // Catmull(-h,0,h,0,1/2)=1/2*(h+h/2-h/4)=5h/8, exactly 0x1.4p126.
    // The scalar helper must not store (value3-value1)=2h as a Single.
    V::CatmullRom(V(-h),V(0),V(h),V(0),0.5f,actual);
    EXPECT_EQ(actual,V(0x1.4p126f));
}

TEST(Tq044WideInterpolationTest, Vector3HermiteValue)
{
    // XNA Vector3.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    const V actual=V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector3HermiteOut)
{
    // XNA Vector3.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector3;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector4LerpValue)
{
    // XNA Vector4.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    const V actual=V::Lerp(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector4LerpOut)
{
    // XNA Vector4.Lerp: expression stays wide until the component store.
    // Hand oracle: h+(-h-h)/2=0; subtraction must not overflow. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Lerp(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector4SmoothStepValue)
{
    // XNA Vector4.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    const V actual=V::SmoothStep(V(h),V(-h),0.5f);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector4SmoothStepOut)
{
    // XNA Vector4.SmoothStep: expression stays wide until the component store.
    // Hand oracle: cubic weight at 1/2 is 1/2; same finite cancellation as Lerp. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    V actual(99);
    V::SmoothStep(V(h),V(-h),0.5f,actual);
    EXPECT_EQ(actual,V(0));
}

TEST(Tq044WideInterpolationTest, Vector4BarycentricValue)
{
    // XNA Vector4.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    const V actual=V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector4BarycentricOut)
{
    // XNA Vector4.Barycentric: expression stays wide until the component store.
    // Hand oracle: h+(-2h)/4+0/4=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Barycentric(V(h),V(-h),V(h),0.25f,0.25f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector4CatmullRomValue)
{
    // XNA Vector4.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    const V actual=V::CatmullRom(V(h),V(h),V(h),V(h),0.5f);
    EXPECT_EQ(actual,V(h));
}

TEST(Tq044WideInterpolationTest, Vector4CatmullRomOut)
{
    // XNA Vector4.CatmullRom: expression stays wide until the component store.
    // Hand oracle: constant spline: 0.5*(2h+0+0+0)=h. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    V actual(99);
    V::CatmullRom(V(h),V(h),V(h),V(h),0.5f,actual);
    EXPECT_EQ(actual,V(h));
}

TEST(Tq044WideInterpolationTest, Vector4HermiteValue)
{
    // XNA Vector4.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    const V actual=V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, Vector4HermiteOut)
{
    // XNA Vector4.Hermite: expression stays wide until the component store.
    // Hand oracle: at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2. h=2^127, all expected values exactly binary32.
    using V=Vector4;
    constexpr float h=0x1p127f;
    V actual(99);
    V::Hermite(V(h/2),V(0),V(h/2),V(0),2.0f,actual);
    EXPECT_EQ(actual,V(h/2));
}

TEST(Tq044WideInterpolationTest, MatrixLerpValue)
{
    // XNA Matrix.Lerp: per-field h+(-h-h)/2=0 without a premature Single difference.
    Matrix a,b;
    for (const auto f : Tq044WideInterpolationTestsFields) { a.*f=0x1p127f; b.*f=-0x1p127f; }
    const Matrix actual=Matrix::Lerp(a,b,0.5f);
    Tq044WideInterpolationTestsExpectMatrix(actual,Matrix());
}

TEST(Tq044WideInterpolationTest, MatrixLerpOut)
{
    // XNA Matrix.Lerp: per-field h+(-h-h)/2=0 without a premature Single difference.
    Matrix a,b;
    for (const auto f : Tq044WideInterpolationTestsFields) { a.*f=0x1p127f; b.*f=-0x1p127f; }
    Matrix actual;
    Matrix::Lerp(a,b,0.5f,actual);
    Tq044WideInterpolationTestsExpectMatrix(actual,Matrix());
}

TEST(Tq044WideInterpolationTest, MathHelperLerp)
{
    // XNA MathHelper.Lerp; h+(-h-h)/2=0; subtraction must not overflow.
    constexpr float h=0x1p127f;
    EXPECT_EQ(MathHelper::Lerp(h,-h,0.5f),0);
}

TEST(Tq044WideInterpolationTest, MathHelperSmoothStep)
{
    // XNA MathHelper.SmoothStep; cubic weight at 1/2 is 1/2; same finite cancellation as Lerp.
    constexpr float h=0x1p127f;
    EXPECT_EQ(MathHelper::SmoothStep(h,-h,0.5f),0);
}

TEST(Tq044WideInterpolationTest, MathHelperBarycentric)
{
    // XNA MathHelper.Barycentric; h+(-2h)/4+0/4=h/2.
    constexpr float h=0x1p127f;
    EXPECT_EQ(MathHelper::Barycentric(h,-h,h,0.25f,0.25f),h/2);
}

TEST(Tq044WideInterpolationTest, MathHelperCatmullRom)
{
    // XNA MathHelper.CatmullRom; constant spline: 0.5*(2h+0+0+0)=h.
    constexpr float h=0x1p127f;
    EXPECT_EQ(MathHelper::CatmullRom(h,h,h,h,0.5f),h);

    // Same hand-derived 5h/8 witness, with a wide value3-value1 difference.
    EXPECT_EQ(MathHelper::CatmullRom(-h,0,h,0,0.5f),0x1.4p126f);
}

TEST(Tq044WideInterpolationTest, MathHelperHermite)
{
    // XNA MathHelper.Hermite; at t=2 the basis is (5,-4,2,4); (5-4)*h/2=h/2.
    constexpr float h=0x1p127f;
    EXPECT_EQ(MathHelper::Hermite(h/2,0,h/2,0,2.0f),h/2);
}
