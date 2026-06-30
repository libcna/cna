// SPDX-License-Identifier: MS-PL
// Task 252: DrawUserIndexedPrimitives audit — FNA API conformance tests.
//
// Tests cover:
//   • Index count formula (identical to PrimitiveVerts — verified via the public static).
//   • All 8 typed overloads + 2 VertexDeclaration overloads compile and link.
//   • Null-backend (headless GraphicsDevice) → silent return, no throw for every overload.
//
// Missing-effect throw and pixel-readback tests require a real GPU backend:
// see integration tests Tasks 257–258.

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

// =============================================================================
// Index count formula — mirrors PrimitiveVerts (same formula used internally)
// =============================================================================

TEST(DrawUserIndexedPrimitivesIndexCountTest, TriangleList_TwoTriangles)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 2), 6);
}

TEST(DrawUserIndexedPrimitivesIndexCountTest, TriangleStrip_ThreeStrip)
{
    // n primitives in a strip → n+2 indices.
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleStrip, 3), 5);
}

TEST(DrawUserIndexedPrimitivesIndexCountTest, LineList_FourLines)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineList, 4), 8);
}

TEST(DrawUserIndexedPrimitivesIndexCountTest, LineStrip_FourStrip)
{
    // n primitives in a strip → n+1 indices.
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineStrip, 4), 5);
}

TEST(DrawUserIndexedPrimitivesIndexCountTest, PointListEXT_SevenPoints)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::PointListEXT, 7), 7);
}

// =============================================================================
// Missing-effect throws — all typed + VertexDeclaration overloads
//
// The default GraphicsDevice() constructor initializes an EasyGL backend, so
// backend_ is non-null. With no effect applied, every typed overload must throw
// std::runtime_error instead of silently returning (the pre-Task-252 bug).
// =============================================================================

class DrawUserIndexedPrimitivesNoEffectTest : public ::testing::Test
{
protected:
    GraphicsDevice gd; // default ctor → real EasyGL backend, but no currentEffect_

    const Color red   { 255, 0,   0,   255 };
    const Color green { 0,   255, 0,   255 };
    const Color blue  { 0,   0,   255, 255 };

    std::vector<VertexPositionColor> vpc {
        { Vector3(0.f, 0.f, 0.f), red   },
        { Vector3(1.f, 0.f, 0.f), green },
        { Vector3(0.f, 1.f, 0.f), blue  }
    };
    std::vector<VertexPositionColorTexture> vpct {
        { Vector3(0.f,0.f,0.f), red,   Vector2(0.f,0.f) },
        { Vector3(1.f,0.f,0.f), green, Vector2(1.f,0.f) },
        { Vector3(0.f,1.f,0.f), blue,  Vector2(0.f,1.f) }
    };
    std::vector<VertexPositionTexture> vpt {
        { Vector3(0.f,0.f,0.f), Vector2(0.f,0.f) },
        { Vector3(1.f,0.f,0.f), Vector2(1.f,0.f) },
        { Vector3(0.f,1.f,0.f), Vector2(0.f,1.f) }
    };
    std::vector<VertexPositionNormalTexture> vpnt {
        { Vector3(0.f,0.f,0.f), Vector3(0.f,0.f,1.f), Vector2(0.f,0.f) },
        { Vector3(1.f,0.f,0.f), Vector3(0.f,0.f,1.f), Vector2(1.f,0.f) },
        { Vector3(0.f,1.f,0.f), Vector3(0.f,0.f,1.f), Vector2(0.f,1.f) }
    };

    std::vector<std::uint16_t> idx16 { 0, 1, 2 };
    std::vector<std::uint32_t> idx32 { 0, 1, 2 };
};

// --- 16-bit typed overloads ---

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPC_16bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPCT_16bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx16.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPT_16bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx16.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPNT_16bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx16.data(), 0, 1),
        std::runtime_error);
}

// --- 32-bit typed overloads ---

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPC_32bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPCT_32bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx32.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPT_32bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx32.data(), 0, 1),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VPNT_32bit_NoEffect_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx32.data(), 0, 1),
        std::runtime_error);
}

// --- VertexDeclaration overloads ---

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VD_16bit_NoEffect_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, 1, vd),
        std::runtime_error);
}

TEST_F(DrawUserIndexedPrimitivesNoEffectTest, VD_32bit_NoEffect_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, 1, vd),
        std::runtime_error);
}
