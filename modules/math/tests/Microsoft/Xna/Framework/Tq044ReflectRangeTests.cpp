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

TEST(Tq044ReflectRangeTest, Vector2Value)
{
    // XNA Reflect stores dot=h, then computes h-2*h=-h without storing 2*h.
    // For other coordinates, 2*h*0 remains zero while h is finite in the wide expression.
    Vector2 input(0); input.X=0x1p127f;
    const Vector2 normal=Vector2(1.0f,0.0f);
    const auto actual=Vector2::Reflect(input,normal);
    EXPECT_EQ(actual,Vector2(-0x1p127f,0));
}

TEST(Tq044ReflectRangeTest, Vector2Out)
{
    // XNA Reflect stores dot=h, then computes h-2*h=-h without storing 2*h.
    // For other coordinates, 2*h*0 remains zero while h is finite in the wide expression.
    Vector2 input(0); input.X=0x1p127f;
    const Vector2 normal=Vector2(1.0f,0.0f);
    Vector2 actual;
    Vector2::Reflect(input,normal,actual);
    EXPECT_EQ(actual,Vector2(-0x1p127f,0));
}

TEST(Tq044ReflectRangeTest, Vector3Value)
{
    // XNA Reflect stores dot=h, then computes h-2*h=-h without storing 2*h.
    // For other coordinates, 2*h*0 remains zero while h is finite in the wide expression.
    Vector3 input(0); input.X=0x1p127f;
    const Vector3 normal=Vector3(1.0f,0.0f,0.0f);
    const auto actual=Vector3::Reflect(input,normal);
    EXPECT_EQ(actual,Vector3(-0x1p127f,0,0));
}

TEST(Tq044ReflectRangeTest, Vector3Out)
{
    // XNA Reflect stores dot=h, then computes h-2*h=-h without storing 2*h.
    // For other coordinates, 2*h*0 remains zero while h is finite in the wide expression.
    Vector3 input(0); input.X=0x1p127f;
    const Vector3 normal=Vector3(1.0f,0.0f,0.0f);
    Vector3 actual;
    Vector3::Reflect(input,normal,actual);
    EXPECT_EQ(actual,Vector3(-0x1p127f,0,0));
}
