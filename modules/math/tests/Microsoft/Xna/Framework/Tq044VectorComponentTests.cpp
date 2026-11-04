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

TEST(Tq044VectorComponentsTest, Vector3LerpValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 7/4, 1, -1; dyadic, exact binary32.
    const auto actual=Vector3::Lerp(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),0.25f);
    EXPECT_EQ(actual,Vector3(1.75f,1.0f,-1.0f));
}

TEST(Tq044VectorComponentsTest, Vector3LerpOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 7/4, 1, -1; dyadic, exact binary32.
    Vector3 actual(99);
    Vector3::Lerp(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector3(1.75f,1.0f,-1.0f));
}

TEST(Tq044VectorComponentsTest, Vector3SmoothStepValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 47/32, 11/8, -17/8; dyadic, exact binary32.
    const auto actual=Vector3::SmoothStep(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),0.25f);
    EXPECT_EQ(actual,Vector3(1.46875f,1.375f,-2.125f));
}

TEST(Tq044VectorComponentsTest, Vector3SmoothStepOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 47/32, 11/8, -17/8; dyadic, exact binary32.
    Vector3 actual(99);
    Vector3::SmoothStep(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector3(1.46875f,1.375f,-2.125f));
}

TEST(Tq044VectorComponentsTest, Vector3BarycentricValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 1/4, 3, 3; dyadic, exact binary32.
    const auto actual=Vector3::Barycentric(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),Vector3(-2.0f,6.0f,4.0f),0.25f,0.5f);
    EXPECT_EQ(actual,Vector3(0.25f,3.0f,3.0f));
}

TEST(Tq044VectorComponentsTest, Vector3BarycentricOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 1/4, 3, 3; dyadic, exact binary32.
    Vector3 actual(99);
    Vector3::Barycentric(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),Vector3(-2.0f,6.0f,4.0f),0.25f,0.5f,actual);
    EXPECT_EQ(actual,Vector3(0.25f,3.0f,3.0f));
}

TEST(Tq044VectorComponentsTest, Vector3CatmullRomValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 371/128, -27/64, 65/8; dyadic, exact binary32.
    const auto actual=Vector3::CatmullRom(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),Vector3(-2.0f,6.0f,4.0f),Vector3(2.0f,-4.0f,0.0f),0.25f);
    EXPECT_EQ(actual,Vector3(2.8984375f,-0.421875f,8.125f));
}

TEST(Tq044VectorComponentsTest, Vector3CatmullRomOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 371/128, -27/64, 65/8; dyadic, exact binary32.
    Vector3 actual(99);
    Vector3::CatmullRom(Vector3(1.0f,2.0f,-4.0f),Vector3(4.0f,-2.0f,8.0f),Vector3(-2.0f,6.0f,4.0f),Vector3(2.0f,-4.0f,0.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector3(2.8984375f,-0.421875f,8.125f));
}

TEST(Tq044VectorComponentsTest, Vector4LerpValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 7/4, 1, -1, 6; dyadic, exact binary32.
    const auto actual=Vector4::Lerp(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),0.25f);
    EXPECT_EQ(actual,Vector4(1.75f,1.0f,-1.0f,6.0f));
}

TEST(Tq044VectorComponentsTest, Vector4LerpOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 7/4, 1, -1, 6; dyadic, exact binary32.
    Vector4 actual(99);
    Vector4::Lerp(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector4(1.75f,1.0f,-1.0f,6.0f));
}

TEST(Tq044VectorComponentsTest, Vector4SmoothStepValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 47/32, 11/8, -17/8, 27/4; dyadic, exact binary32.
    const auto actual=Vector4::SmoothStep(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),0.25f);
    EXPECT_EQ(actual,Vector4(1.46875f,1.375f,-2.125f,6.75f));
}

TEST(Tq044VectorComponentsTest, Vector4SmoothStepOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 47/32, 11/8, -17/8, 27/4; dyadic, exact binary32.
    Vector4 actual(99);
    Vector4::SmoothStep(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector4(1.46875f,1.375f,-2.125f,6.75f));
}

TEST(Tq044VectorComponentsTest, Vector4BarycentricValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 1/4, 3, 3, -2; dyadic, exact binary32.
    const auto actual=Vector4::Barycentric(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),Vector4(-2.0f,6.0f,4.0f,-8.0f),0.25f,0.5f);
    EXPECT_EQ(actual,Vector4(0.25f,3.0f,3.0f,-2.0f));
}

TEST(Tq044VectorComponentsTest, Vector4BarycentricOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 1/4, 3, 3, -2; dyadic, exact binary32.
    Vector4 actual(99);
    Vector4::Barycentric(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),Vector4(-2.0f,6.0f,4.0f,-8.0f),0.25f,0.5f,actual);
    EXPECT_EQ(actual,Vector4(0.25f,3.0f,3.0f,-2.0f));
}

TEST(Tq044VectorComponentsTest, Vector4CatmullRomValue)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 371/128, -27/64, 65/8, -79/32; dyadic, exact binary32.
    const auto actual=Vector4::CatmullRom(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),Vector4(-2.0f,6.0f,4.0f,-8.0f),Vector4(2.0f,-4.0f,0.0f,4.0f),0.25f);
    EXPECT_EQ(actual,Vector4(2.8984375f,-0.421875f,8.125f,-2.46875f));
}

TEST(Tq044VectorComponentsTest, Vector4CatmullRomOut)
{
    // Hand polynomial at t=1/4; Barycentric uses amounts 1/4,1/2.
    // Expected coordinates: 371/128, -27/64, 65/8, -79/32; dyadic, exact binary32.
    Vector4 actual(99);
    Vector4::CatmullRom(Vector4(1.0f,2.0f,-4.0f,8.0f),Vector4(4.0f,-2.0f,8.0f,0.0f),Vector4(-2.0f,6.0f,4.0f,-8.0f),Vector4(2.0f,-4.0f,0.0f,4.0f),0.25f,actual);
    EXPECT_EQ(actual,Vector4(2.8984375f,-0.421875f,8.125f,-2.46875f));
}
