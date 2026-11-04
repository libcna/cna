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

TEST(Tq044QuaternionTraceTest, PositiveTraceValue)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(1,5,1,0, 2,1,5,0, 3,4,1,0, 0,0,0,1);
    const auto actual=Quaternion::CreateFromRotationMatrix(input);
    EXPECT_EQ(actual,Quaternion(0.25f,0.5f,0.75f,1));
}

TEST(Tq044QuaternionTraceTest, PositiveTraceOut)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(1,5,1,0, 2,1,5,0, 3,4,1,0, 0,0,0,1);
    Quaternion actual(99,99,99,99);
    Quaternion::CreateFromRotationMatrix(input,actual);
    EXPECT_EQ(actual,Quaternion(0.25f,0.5f,0.75f,1));
}

TEST(Tq044QuaternionTraceTest, LargestXValue)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(1,3,4,0, -2,-1,5,0, -2,2,-1,0, 0,0,0,1);
    const auto actual=Quaternion::CreateFromRotationMatrix(input);
    EXPECT_EQ(actual,Quaternion(1,0.25f,0.5f,0.75f));
}

TEST(Tq044QuaternionTraceTest, LargestXOut)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(1,3,4,0, -2,-1,5,0, -2,2,-1,0, 0,0,0,1);
    Quaternion actual(99,99,99,99);
    Quaternion::CreateFromRotationMatrix(input,actual);
    EXPECT_EQ(actual,Quaternion(1,0.25f,0.5f,0.75f));
}

TEST(Tq044QuaternionTraceTest, LargestYValue)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(-1,3,2,0, -2,1,4,0, 5,-2,-1,0, 0,0,0,1);
    const auto actual=Quaternion::CreateFromRotationMatrix(input);
    EXPECT_EQ(actual,Quaternion(0.25f,1,0.5f,0.75f));
}

TEST(Tq044QuaternionTraceTest, LargestYOut)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(-1,3,2,0, -2,1,4,0, 5,-2,-1,0, 0,0,0,1);
    Quaternion actual(99,99,99,99);
    Quaternion::CreateFromRotationMatrix(input,actual);
    EXPECT_EQ(actual,Quaternion(0.25f,1,0.5f,0.75f));
}

TEST(Tq044QuaternionTraceTest, LargestZValue)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(-1,5,3,0, 2,-1,4,0, -2,-2,1,0, 0,0,0,1);
    const auto actual=Quaternion::CreateFromRotationMatrix(input);
    EXPECT_EQ(actual,Quaternion(0.25f,0.5f,1,0.75f));
}

TEST(Tq044QuaternionTraceTest, LargestZOut)
{
    // Oracle: XNA CreateFromRotationMatrix; selected sqrt is 2, multiplier=1/4.
    // Diagonal selects the branch; sums/differences 1,2,3 exercise every component.
    const Matrix input(-1,5,3,0, 2,-1,4,0, -2,-2,1,0, 0,0,0,1);
    Quaternion actual(99,99,99,99);
    Quaternion::CreateFromRotationMatrix(input,actual);
    EXPECT_EQ(actual,Quaternion(0.25f,0.5f,1,0.75f));
}

TEST(Tq044QuaternionTraceTest, InverseMixedSignsAliasesInput)
{
    // sum(q_i^2)=4; inverse=(-x,-y,-z,w)/4.
    Quaternion q(1,-1,1,-1);
    Quaternion::Inverse(q,q);
    EXPECT_EQ(q,Quaternion(-0.25f,0.25f,-0.25f,-0.25f));
}

TEST(Tq044QuaternionTraceTest, ConjugateMixedSignsAliasesInput)
{
    // Conjugate negates the vector part only.
    Quaternion q(1,-2,3,-4);
    Quaternion::Conjugate(q,q);
    EXPECT_EQ(q,Quaternion(-1,2,-3,-4));
}
