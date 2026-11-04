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
constexpr std::array<float Matrix::*,16> Tq044MatrixQuaternionTransformTestsFields = {
    &Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,
    &Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,
    &Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,
    &Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44};
}

TEST(Tq044MatrixQuaternionTransformTest, FiniteValue)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    const Matrix actual=Matrix::Transform(input,Quaternion(0,0,1,1));
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }

    // Refresh Matrix.Transform value IL_0000-00eb / out IL_0000-00dc:
    // x=2^-75; x*(x+x)=2^-149, whereas Single(x*x)*2 first rounds to zero.
    // Row X=2^127 therefore contributes exactly 2^-22 to Y.
    Matrix large;
    large.M11=0x1p127f; large.M44=1;
    const Quaternion tiny(0x1p-75f,0x1p-75f,0,0);
    Matrix tinyExpected;
    tinyExpected.M11=0x1p127f; tinyExpected.M12=0x1p-22f; tinyExpected.M44=1;
    const Matrix tinyActual=Matrix::Transform(large,tiny);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) EXPECT_EQ(tinyActual.*f,tinyExpected.*f);
}

TEST(Tq044MatrixQuaternionTransformTest, NonfiniteColumnValue)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    input.M14=expected.M14=std::numeric_limits<float>::quiet_NaN();
    input.M24=expected.M24=std::numeric_limits<float>::infinity();
    input.M34=expected.M34=-std::numeric_limits<float>::infinity();
    input.M44=expected.M44=-0.0f;
    const Matrix actual=Matrix::Transform(input,Quaternion(0,0,1,1));
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }
    EXPECT_TRUE(std::signbit(actual.M44));
}

TEST(Tq044MatrixQuaternionTransformTest, FiniteOut)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    Matrix actual;
    Matrix::Transform(input,Quaternion(0,0,1,1),actual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }

    // Refresh Matrix.Transform value IL_0000-00eb / out IL_0000-00dc:
    // x=2^-75; x*(x+x)=2^-149, whereas Single(x*x)*2 first rounds to zero.
    // Row X=2^127 therefore contributes exactly 2^-22 to Y.
    Matrix large;
    large.M11=0x1p127f; large.M44=1;
    const Quaternion tiny(0x1p-75f,0x1p-75f,0,0);
    Matrix tinyExpected;
    tinyExpected.M11=0x1p127f; tinyExpected.M12=0x1p-22f; tinyExpected.M44=1;
    Matrix tinyActual;
    Matrix::Transform(large,tiny,tinyActual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) EXPECT_EQ(tinyActual.*f,tinyExpected.*f);
}

TEST(Tq044MatrixQuaternionTransformTest, NonfiniteColumnOut)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    input.M14=expected.M14=std::numeric_limits<float>::quiet_NaN();
    input.M24=expected.M24=std::numeric_limits<float>::infinity();
    input.M34=expected.M34=-std::numeric_limits<float>::infinity();
    input.M44=expected.M44=-0.0f;
    Matrix actual;
    Matrix::Transform(input,Quaternion(0,0,1,1),actual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }
    EXPECT_TRUE(std::signbit(actual.M44));
}

TEST(Tq044MatrixQuaternionTransformTest, FiniteAliasedOut)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    Matrix actual=input;
    Matrix::Transform(actual,Quaternion(0,0,1,1),actual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }

    // Refresh Matrix.Transform value IL_0000-00eb / out IL_0000-00dc:
    // x=2^-75; x*(x+x)=2^-149, whereas Single(x*x)*2 first rounds to zero.
    // Row X=2^127 therefore contributes exactly 2^-22 to Y.
    Matrix large;
    large.M11=0x1p127f; large.M44=1;
    const Quaternion tiny(0x1p-75f,0x1p-75f,0,0);
    Matrix tinyExpected;
    tinyExpected.M11=0x1p127f; tinyExpected.M12=0x1p-22f; tinyExpected.M44=1;
    Matrix tinyActual=large;
    Matrix::Transform(tinyActual,tiny,tinyActual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) EXPECT_EQ(tinyActual.*f,tinyExpected.*f);
}

TEST(Tq044MatrixQuaternionTransformTest, NonfiniteColumnAliasedOut)
{
    // XNA Transform: each XYZ row maps to (-x-2y,2x-y,z); column 4 is copied.
    // Value IL_015e-0166,01dd-01e5,025c-0264,02db-02e3 copy M14/M24/M34/M44.
    Matrix input(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    Matrix expected(-5,0,3,4, -17,4,7,8, -29,8,11,12, -41,12,15,16);
    input.M14=expected.M14=std::numeric_limits<float>::quiet_NaN();
    input.M24=expected.M24=std::numeric_limits<float>::infinity();
    input.M34=expected.M34=-std::numeric_limits<float>::infinity();
    input.M44=expected.M44=-0.0f;
    Matrix actual=input;
    Matrix::Transform(actual,Quaternion(0,0,1,1),actual);
    for (const auto f : Tq044MatrixQuaternionTransformTestsFields) {
        if (std::isnan(expected.*f)) EXPECT_TRUE(std::isnan(actual.*f));
        else EXPECT_EQ(actual.*f,expected.*f);
    }
    EXPECT_TRUE(std::signbit(actual.M44));
}
