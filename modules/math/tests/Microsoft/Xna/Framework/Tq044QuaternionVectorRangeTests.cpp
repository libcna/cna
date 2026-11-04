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

TEST(Tq044QuaternionVectorRangeTest, Vector2Value)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector2 input=Vector2(1.7014118346046923e+38f,0.0f);
    const Quaternion q(0,0,1,0);
    EXPECT_EQ(Vector2::Transform(input,q),Vector2(-0x1p127f,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector2Out)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector2 input=Vector2(1.7014118346046923e+38f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector2 actual;
    Vector2::Transform(input,q,actual);
    EXPECT_EQ(actual,Vector2(-0x1p127f,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector2AliasedOut)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector2 input=Vector2(1.7014118346046923e+38f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector2 actual=input;
    Vector2::Transform(actual,q,actual);
    EXPECT_EQ(actual,Vector2(-0x1p127f,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector2WholeArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector2 input=Vector2(1.7014118346046923e+38f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::Transform(source,q,destination);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(-0x1p127f,0),Vector2(99)}));
}

TEST(Tq044QuaternionVectorRangeTest, Vector2IndexedArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector2 input=Vector2(1.7014118346046923e+38f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector2> source{input};
    std::vector<Vector2> destination(2,Vector2(99));
    Vector2::Transform(source,0,q,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector2>{Vector2(-0x1p127f,0),Vector2(99)}));
}

TEST(Tq044QuaternionVectorRangeTest, Vector3Value)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector3 input=Vector3(1.7014118346046923e+38f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    EXPECT_EQ(Vector3::Transform(input,q),Vector3(-0x1p127f,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector3Out)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector3 input=Vector3(1.7014118346046923e+38f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector3 actual;
    Vector3::Transform(input,q,actual);
    EXPECT_EQ(actual,Vector3(-0x1p127f,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector3AliasedOut)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector3 input=Vector3(1.7014118346046923e+38f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector3 actual=input;
    Vector3::Transform(actual,q,actual);
    EXPECT_EQ(actual,Vector3(-0x1p127f,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector3WholeArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector3 input=Vector3(1.7014118346046923e+38f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::Transform(source,q,destination);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(-0x1p127f,0,0),Vector3(99)}));
}

TEST(Tq044QuaternionVectorRangeTest, Vector3IndexedArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector3 input=Vector3(1.7014118346046923e+38f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector3> source{input};
    std::vector<Vector3> destination(2,Vector3(99));
    Vector3::Transform(source,0,q,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector3>{Vector3(-0x1p127f,0,0),Vector3(99)}));
}

TEST(Tq044QuaternionVectorRangeTest, Vector4Value)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector4 input=Vector4(1.7014118346046923e+38f,0.0f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    EXPECT_EQ(Vector4::Transform(input,q),Vector4(-0x1p127f,0,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector4Out)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector4 input=Vector4(1.7014118346046923e+38f,0.0f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector4 actual;
    Vector4::Transform(input,q,actual);
    EXPECT_EQ(actual,Vector4(-0x1p127f,0,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector4AliasedOut)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector4 input=Vector4(1.7014118346046923e+38f,0.0f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    Vector4 actual=input;
    Vector4::Transform(actual,q,actual);
    EXPECT_EQ(actual,Vector4(-0x1p127f,0,0,0));
}

TEST(Tq044QuaternionVectorRangeTest, Vector4WholeArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector4 input=Vector4(1.7014118346046923e+38f,0.0f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector4> source{input};
    std::vector<Vector4> destination(2,Vector4(99));
    Vector4::Transform(source,q,destination);
    EXPECT_EQ(destination,(std::vector<Vector4>{Vector4(-0x1p127f,0,0,0),Vector4(99)}));
}

TEST(Tq044QuaternionVectorRangeTest, Vector4IndexedArray)
{
    // XNA quaternion Transform builds coefficients from q first: diag(-1,-1,1).
    // Thus (h,0,0) -> (-h,0,0), h=2^127; no 2*h vector temporary exists.
    const Vector4 input=Vector4(1.7014118346046923e+38f,0.0f,0.0f,0.0f);
    const Quaternion q(0,0,1,0);
    const std::vector<Vector4> source{input};
    std::vector<Vector4> destination(2,Vector4(99));
    Vector4::Transform(source,0,q,destination,0,1);
    EXPECT_EQ(destination,(std::vector<Vector4>{Vector4(-0x1p127f,0,0,0),Vector4(99)}));
}
