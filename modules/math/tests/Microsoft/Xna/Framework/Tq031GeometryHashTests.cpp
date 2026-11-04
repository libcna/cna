// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/BoundingFrustum.hpp"
#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Ray.hpp"
using namespace Microsoft::Xna::Framework;

TEST(Tq031GeometryHashTest, WrappedAdditionHasLiteralWordsForEveryGeometryType)
{
    // XNA Plane/Sphere.GetHashCode IL_0017..001d and Ray/Box IL_001d..0023:
    // add .NET4 float/vector hashes modulo 2^32. float(1) bits=0x3f800000;
    // 2*0x3f800000=0x7f000000 (XOR would be zero), 6*=0x7d000000 mod 2^32.
    EXPECT_EQ(static_cast<std::uint32_t>(Plane(Vector3(1,0,0),1).GetHashCode()),0x7f000000u);
    EXPECT_EQ(static_cast<std::uint32_t>(BoundingSphere(Vector3(1,0,0),1).GetHashCode()),0x7f000000u);
    EXPECT_EQ(static_cast<std::uint32_t>(Ray(Vector3(1,0,0),Vector3(1,0,0)).GetHashCode()),0x7f000000u);
    EXPECT_EQ(static_cast<std::uint32_t>(BoundingBox(Vector3(1),Vector3(1)).GetHashCode()),0x7d000000u);
    // -1 bits=0xbf800000; 1 + -1 hashes wrap to 0xff000000.
    EXPECT_EQ(static_cast<std::uint32_t>(Plane(Vector3(1,0,0),-1).GetHashCode()),0xff000000u);
    EXPECT_EQ(static_cast<std::uint32_t>(Ray(Vector3(1,0,0),Vector3(-1,0,0)).GetHashCode()),0xff000000u);
    // Frustum delegates to Matrix.GetHashCode, IL_0000..0011. Four 1s wrap to 0xfe000000.
    EXPECT_EQ(static_cast<std::uint32_t>(BoundingFrustum(Matrix::getIdentityProperty()).GetHashCode()),0xfe000000u);
}

TEST(Tq031GeometryHashTest, ZeroSignsAndNanPayloadsKeepTheNet4ScalarContract)
{
    // Same IL calls Single.GetHashCode: both zero signs hash to zero; other bits survive.
    for (float zero : {0.0f,-0.0f})
    {
        EXPECT_EQ(static_cast<std::uint32_t>(Plane(Vector3(zero),zero).GetHashCode()),0u);
        EXPECT_EQ(static_cast<std::uint32_t>(BoundingSphere(Vector3(zero),zero).GetHashCode()),0u);
        EXPECT_EQ(static_cast<std::uint32_t>(Ray(Vector3(zero),Vector3(zero)).GetHashCode()),0u);
        EXPECT_EQ(static_cast<std::uint32_t>(BoundingBox(Vector3(zero),Vector3(zero)).GetHashCode()),0u);
    }
    for (std::uint32_t bits : {0x3f800000u,0xbf800000u,0x7f800000u,0x7fc01234u,0xffc01234u})
    {
        const float value=std::bit_cast<float>(bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Plane(Vector3::Zero,value).GetHashCode()),bits);
        BoundingSphere sphere; sphere.Radius=value; // Public field admits negative values too.
        EXPECT_EQ(static_cast<std::uint32_t>(sphere.GetHashCode()),bits);
        EXPECT_EQ(static_cast<std::uint32_t>(Ray(Vector3::Zero,Vector3(value,0,0)).GetHashCode()),bits);
        EXPECT_EQ(static_cast<std::uint32_t>(BoundingBox(Vector3::Zero,Vector3(value,0,0)).GetHashCode()),bits);
    }
}
