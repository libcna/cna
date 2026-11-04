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

TEST(Tq044TransformRangeTest, Vector2MatrixValue)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector2 input=Vector2(1.0f,1.0f);
    EXPECT_EQ(Vector2::Transform(input,matrix),Vector2(1.0f,2.0f));
}

TEST(Tq044TransformRangeTest, Vector2MatrixOut)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector2 input=Vector2(1.0f,1.0f);
    Vector2 actual(99);
    Vector2::Transform(input,matrix,actual);
    EXPECT_EQ(actual,Vector2(1.0f,2.0f));
}

TEST(Tq044TransformRangeTest, Vector2MatrixWholeArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector2 input=Vector2(1.0f,1.0f);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::Transform(source,matrix,destination);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(1.0f,2.0f),Vector2(99)}));
}

TEST(Tq044TransformRangeTest, Vector2MatrixIndexedArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector2 input=Vector2(1.0f,1.0f);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::Transform(source,0,matrix,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(1.0f,2.0f),Vector2(99)}));
}

TEST(Tq044TransformRangeTest, Vector3MatrixValue)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector3 input=Vector3(1.0f,1.0f,0.0f);
    EXPECT_EQ(Vector3::Transform(input,matrix),Vector3(1.0f,2.0f,3.0f));
}

TEST(Tq044TransformRangeTest, Vector3MatrixOut)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector3 input=Vector3(1.0f,1.0f,0.0f);
    Vector3 actual(99);
    Vector3::Transform(input,matrix,actual);
    EXPECT_EQ(actual,Vector3(1.0f,2.0f,3.0f));
}

TEST(Tq044TransformRangeTest, Vector3MatrixWholeArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector3 input=Vector3(1.0f,1.0f,0.0f);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::Transform(source,matrix,destination);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(1.0f,2.0f,3.0f),Vector3(99)}));
}

TEST(Tq044TransformRangeTest, Vector3MatrixIndexedArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector3 input=Vector3(1.0f,1.0f,0.0f);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::Transform(source,0,matrix,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(1.0f,2.0f,3.0f),Vector3(99)}));
}

TEST(Tq044TransformRangeTest, Vector4MatrixValue)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector4 input=Vector4(1.0f,1.0f,0.0f,1.0f);
    EXPECT_EQ(Vector4::Transform(input,matrix),Vector4(1.0f,2.0f,3.0f,4.0f));
}

TEST(Tq044TransformRangeTest, Vector4MatrixOut)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector4 input=Vector4(1.0f,1.0f,0.0f,1.0f);
    Vector4 actual(99);
    Vector4::Transform(input,matrix,actual);
    EXPECT_EQ(actual,Vector4(1.0f,2.0f,3.0f,4.0f));
}

TEST(Tq044TransformRangeTest, Vector4MatrixWholeArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector4 input=Vector4(1.0f,1.0f,0.0f,1.0f);
    const std::vector<Vector4> source{input};
    std::vector<Vector4> destination(2,Vector4(99));
    Vector4::Transform(source,matrix,destination);
    EXPECT_EQ(destination,(std::vector<Vector4>{Vector4(1.0f,2.0f,3.0f,4.0f),Vector4(99)}));
}

TEST(Tq044TransformRangeTest, Vector4MatrixIndexedArray)
{
    // XNA Transform: (2^24+k)-2^24=k, k=1..4; only final store rounds.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const Vector4 input=Vector4(1.0f,1.0f,0.0f,1.0f);
    const std::vector<Vector4> source{input};
    std::vector<Vector4> destination(2,Vector4(99));
    Vector4::Transform(source,0,matrix,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector4>{Vector4(1.0f,2.0f,3.0f,4.0f),Vector4(99)}));
}

TEST(Tq044TransformRangeTest, Vector4FromVector2Value)
{
    // Same literal 2^24 cancellation, including homogeneous W.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const auto actual=Vector4::Transform(Vector2(1,1),matrix);
    EXPECT_EQ(actual,Vector4(1,2,3,4));
}

TEST(Tq044TransformRangeTest, Vector4FromVector2Out)
{
    // Same literal 2^24 cancellation, including homogeneous W.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    Vector4 actual;
    Vector4::Transform(Vector2(1,1),matrix,actual);
    EXPECT_EQ(actual,Vector4(1,2,3,4));
}

TEST(Tq044TransformRangeTest, Vector4FromVector3Value)
{
    // Same literal 2^24 cancellation, including homogeneous W.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    const auto actual=Vector4::Transform(Vector3(1,1,0),matrix);
    EXPECT_EQ(actual,Vector4(1,2,3,4));
}

TEST(Tq044TransformRangeTest, Vector4FromVector3Out)
{
    // Same literal 2^24 cancellation, including homogeneous W.
    const Matrix matrix=Matrix(16777216,16777216,16777216,16777216, 1,2,3,4, 0,0,0,0, -16777216,-16777216,-16777216,-16777216);
    Vector4 actual;
    Vector4::Transform(Vector3(1,1,0),matrix,actual);
    EXPECT_EQ(actual,Vector4(1,2,3,4));
}

TEST(Tq044TransformRangeTest, Vector2NormalValue)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector2 input=Vector2(4097.0f,4096.0f);
    EXPECT_EQ(Vector2::TransformNormal(input,matrix),Vector2(1.0f,2.0f));
}

TEST(Tq044TransformRangeTest, Vector2NormalOut)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector2 input=Vector2(4097.0f,4096.0f);
    Vector2 actual;
    Vector2::TransformNormal(input,matrix,actual);
    EXPECT_EQ(actual,Vector2(1.0f,2.0f));
}

TEST(Tq044TransformRangeTest, Vector2NormalWholeArray)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector2 input=Vector2(4097.0f,4096.0f);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::TransformNormal(source,matrix,destination);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(1.0f,2.0f),Vector2(99)}));
}

TEST(Tq044TransformRangeTest, Vector2NormalIndexedArray)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector2 input=Vector2(4097.0f,4096.0f);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::TransformNormal(source,0,matrix,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(1.0f,2.0f),Vector2(99)}));
}

TEST(Tq044TransformRangeTest, Vector3NormalValue)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector3 input=Vector3(4097.0f,4096.0f,0.0f);
    EXPECT_EQ(Vector3::TransformNormal(input,matrix),Vector3(1.0f,2.0f,3.0f));
}

TEST(Tq044TransformRangeTest, Vector3NormalOut)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector3 input=Vector3(4097.0f,4096.0f,0.0f);
    Vector3 actual;
    Vector3::TransformNormal(input,matrix,actual);
    EXPECT_EQ(actual,Vector3(1.0f,2.0f,3.0f));
}

TEST(Tq044TransformRangeTest, Vector3NormalWholeArray)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector3 input=Vector3(4097.0f,4096.0f,0.0f);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::TransformNormal(source,matrix,destination);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(1.0f,2.0f,3.0f),Vector3(99)}));
}

TEST(Tq044TransformRangeTest, Vector3NormalIndexedArray)
{
    // Hand dot: 4097^2-4096*4098=1; columns multiply this by 1,2,3.
    // Translation 99 is excluded. Both products must remain wide before subtraction.
    const Matrix matrix=Matrix(4097,8194,12291,0, -4098,-8196,-12294,0, 0,0,0,0, 99,99,99,1);
    const Vector3 input=Vector3(4097.0f,4096.0f,0.0f);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::TransformNormal(source,0,matrix,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(1.0f,2.0f,3.0f),Vector3(99)}));
}
