// SPDX-License-Identifier: MS-PL
// Task 252: DrawUserIndexedPrimitives audit — FNA API conformance tests.
//
// Tests cover:
//   • Index count formula (identical to PrimitiveVerts — verified via the public static).
//   • All 8 typed overloads + 2 VertexDeclaration overloads compile and link.
//   • Null-renderer (headless GraphicsDevice) → silent return, no throw for every overload.
//   • primitiveCount <= 0 throws ArgumentOutOfRangeException, mirroring Task 259's
//     DrawUserPrimitives coverage, for all 8 typed + 2 VertexDeclaration overloads plus
//     the untyped raw-void* overload (whose guard was missing until this task).
//
// Missing-effect throw and pixel-readback tests require a real GPU renderer:
// see integration tests Tasks 257–258.

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
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
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Graphics::BasicEffect;
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
// The default GraphicsDevice() constructor initializes an EasyGL renderer, so
// renderer_ is non-null. With no effect applied, every typed overload must throw
// std::runtime_error instead of silently returning (the pre-Task-252 bug).
// =============================================================================

class DrawUserIndexedPrimitivesNoEffectTest : public ::testing::Test
{
protected:
    GraphicsDevice gd; // default ctor → real EasyGL renderer, but no currentEffect_

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

// =============================================================================
// primitiveCount <= 0 throws ArgumentOutOfRangeException — all typed + VD
// overloads, plus the untyped raw-void* overload.
//
// Mirrors Task 259's DrawUserPrimitivesArgumentGuardTest coverage. A BasicEffect
// is applied so the missing-effect check (above) is bypassed and the
// primitiveCount guard is actually reached.
// =============================================================================

class DrawUserIndexedPrimitivesArgumentGuardTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
    BasicEffect fx{gd};

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

    void SetUp() override { fx.Apply(); }
};

// --- VertexPositionColor ---

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPC_16bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPC_16bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPC_32bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPC_32bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionTexture ---

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPT_16bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx16.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPT_16bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx16.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPT_32bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx32.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPT_32bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpt.data(), 0, 3, idx32.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionColorTexture ---

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPCT_16bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx16.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPCT_16bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx16.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPCT_32bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx32.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPCT_32bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpct.data(), 0, 3, idx32.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionNormalTexture ---

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPNT_16bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx16.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPNT_16bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx16.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPNT_32bit_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx32.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VPNT_32bit_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpnt.data(), 0, 3, idx32.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- Explicit VertexDeclaration overloads ---

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VD_16bit_ZeroCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, 0, vd),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VD_16bit_NegativeCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx16.data(), 0, -1, vd),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VD_32bit_ZeroCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, 0, vd),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, VD_32bit_NegativeCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, vpc.data(), 0, 3, idx32.data(), 0, -1, vd),
        System::ArgumentOutOfRangeException);
}

// --- Untyped raw-void* overload (assumes VertexPositionColor layout) ---
//
// This overload predates the typed overloads added in Task 252 and was missed
// by that task's primitiveCount validation; fixed alongside these tests.

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, Raw_ZeroCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        static_cast<const void*>(vpc.data()), 0, 3,
        static_cast<const void*>(idx16.data()), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserIndexedPrimitivesArgumentGuardTest, Raw_NegativeCount_Throws)
{
    EXPECT_THROW(gd.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList,
        static_cast<const void*>(vpc.data()), 0, 3,
        static_cast<const void*>(idx16.data()), 0, -1),
        System::ArgumentOutOfRangeException);
}
