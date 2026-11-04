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

#include "Microsoft/Xna/Framework/MathHelper.hpp"
namespace {
constexpr std::array<float Matrix::*,16> Tq044NonunitAxisTestsFields = {
    &Matrix::M11,&Matrix::M12,&Matrix::M13,&Matrix::M14,
    &Matrix::M21,&Matrix::M22,&Matrix::M23,&Matrix::M24,
    &Matrix::M31,&Matrix::M32,&Matrix::M33,&Matrix::M34,
    &Matrix::M41,&Matrix::M42,&Matrix::M43,&Matrix::M44};
}

TEST(Tq044NonunitAxisTest, MatrixValue)
{
    // Rodrigues coefficients with raw axis=(2,1,2), sin(pi/2)=1,cos(pi/2)=0.
    // Input Single(pi/2) differs by <5e-8 radians; 2e-6 covers trig and product rounding.
    const auto actual=Matrix::CreateFromAxisAngle(Vector3(2,1,2),MathHelper::PiOver2);
    const Matrix expected(4,4,3,0, 0,1,4,0, 5,0,4,0, 0,0,0,1);
    for (const auto f : Tq044NonunitAxisTestsFields) EXPECT_NEAR(actual.*f,expected.*f,2e-6f);
}

TEST(Tq044NonunitAxisTest, QuaternionValue)
{
    // XNA uses raw axis*sin(angle/2),cos(angle/2); angle=Single(pi).
    // Single(pi)/2 differs from pi/2 by <5e-8; 3e-7 is conservative absolute tolerance.
    const auto actual=Quaternion::CreateFromAxisAngle(Vector3(2,1,2),MathHelper::Pi);
    EXPECT_NEAR(actual.X,2,3e-7f); EXPECT_NEAR(actual.Y,1,3e-7f);
    EXPECT_NEAR(actual.Z,2,3e-7f); EXPECT_NEAR(actual.W,0,3e-7f);
}

TEST(Tq044NonunitAxisTest, MatrixOut)
{
    // Rodrigues coefficients with raw axis=(2,1,2), sin(pi/2)=1,cos(pi/2)=0.
    // Input Single(pi/2) differs by <5e-8 radians; 2e-6 covers trig and product rounding.
    Matrix actual;
    Matrix::CreateFromAxisAngle(Vector3(2,1,2),MathHelper::PiOver2,actual);
    const Matrix expected(4,4,3,0, 0,1,4,0, 5,0,4,0, 0,0,0,1);
    for (const auto f : Tq044NonunitAxisTestsFields) EXPECT_NEAR(actual.*f,expected.*f,2e-6f);
}

TEST(Tq044NonunitAxisTest, QuaternionOut)
{
    // XNA uses raw axis*sin(angle/2),cos(angle/2); angle=Single(pi).
    // Single(pi)/2 differs from pi/2 by <5e-8; 3e-7 is conservative absolute tolerance.
    Quaternion actual;
    Quaternion::CreateFromAxisAngle(Vector3(2,1,2),MathHelper::Pi,actual);
    EXPECT_NEAR(actual.X,2,3e-7f); EXPECT_NEAR(actual.Y,1,3e-7f);
    EXPECT_NEAR(actual.Z,2,3e-7f); EXPECT_NEAR(actual.W,0,3e-7f);
}
