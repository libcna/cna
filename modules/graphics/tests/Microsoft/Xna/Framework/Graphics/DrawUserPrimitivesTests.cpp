// SPDX-License-Identifier: MS-PL
// Task 251: DrawUserPrimitives audit — FNA API conformance tests.
// Task 259: DrawUserPrimitives argument-guard tests (primitiveCount <= 0).
//
// Tests cover:
//   • GraphicsDevice::PrimitiveVerts() vertex-count formula for all five topologies.
//   • Invalid primitiveType throws InvalidOperationException.
//   • primitiveCount ≤ 0 throws ArgumentOutOfRangeException for every typed overload
//     (VPC/VPT/VPCT/VPNT + VertexDeclaration), exercised with a real EasyGL renderer
//     and an applied BasicEffect so the check under test is reached.
//
// Draw-call pixel-readback tests live in tasks 255–256 (integration tests).

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
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
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

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
// PrimitiveVerts — vertex count formula (mirrors FNA PrimitiveVerts())
// =============================================================================

TEST(PrimitiveVertsTest, TriangleList_OneTriangle)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 1), 3);
}

TEST(PrimitiveVertsTest, TriangleList_FourTriangles)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 4), 12);
}

TEST(PrimitiveVertsTest, TriangleStrip_OneStrip)
{
    // n primitives in a strip need n+2 vertices.
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleStrip, 1), 3);
}

TEST(PrimitiveVertsTest, TriangleStrip_FourStrip)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleStrip, 4), 6);
}

TEST(PrimitiveVertsTest, LineList_OneLine)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineList, 1), 2);
}

TEST(PrimitiveVertsTest, LineList_FiveLines)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineList, 5), 10);
}

TEST(PrimitiveVertsTest, LineStrip_OneLine)
{
    // n primitives in a strip need n+1 vertices.
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineStrip, 1), 2);
}

TEST(PrimitiveVertsTest, LineStrip_FiveStrip)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::LineStrip, 5), 6);
}

TEST(PrimitiveVertsTest, PointListEXT_TenPoints)
{
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::PointListEXT, 10), 10);
}

TEST(PrimitiveVertsTest, InvalidPrimitiveType_Throws)
{
    EXPECT_THROW(
        GraphicsDevice::PrimitiveVerts(static_cast<PrimitiveType>(99), 1),
        System::InvalidOperationException
    );
}

TEST(PrimitiveVertsTest, TriangleList_ZeroPrimitives)
{
    // FNA allows zero; vertex count is zero (no draw would occur).
    EXPECT_EQ(GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 0), 0);
}

// =============================================================================
// API surface — overload signatures compile (static assertions / instantiation)
// =============================================================================

// These tests just verify the overloads exist and can be referenced.
// They do not call them (that would require a GPU device).
TEST(DrawUserPrimitivesAPITest, PrimitiveVertsIsPublicStaticCNAEXT)
{
    // If this compiles the static method is accessible.
    const int n = GraphicsDevice::PrimitiveVerts(PrimitiveType::TriangleList, 2);
    EXPECT_EQ(n, 6);
}

// =============================================================================
// Task 259: primitiveCount <= 0 throws ArgumentOutOfRangeException
//
// The default GraphicsDevice() constructor initializes a real EasyGL renderer
// (renderer_ non-null), and BasicEffect::Apply() performs no GPU calls, so
// applying an effect here reaches the primitiveCount guard without requiring
// an actual draw.
// =============================================================================

class DrawUserPrimitivesArgumentGuardTest : public ::testing::Test
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

    void SetUp() override
    {
        // DrawUserPrimitives is inherently a 3D-pipeline entry point -- a renderer that honestly
        // reports no 3D pipeline (GraphicsCapability::ThreeD) rejects it before ever reaching the
        // primitiveCount argument guard this fixture exercises.
        if (!gd.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
        fx.Apply();
    }
};

// --- VertexPositionColor ---

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPC_ZeroCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPC_NegativeCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionTexture ---

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPT_ZeroCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpt.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPT_NegativeCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpt.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionColorTexture ---

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPCT_ZeroCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpct.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPCT_NegativeCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpct.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- VertexPositionNormalTexture ---

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPNT_ZeroCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpnt.data(), 0, 0),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserPrimitivesArgumentGuardTest, VPNT_NegativeCount_Throws)
{
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpnt.data(), 0, -1),
        System::ArgumentOutOfRangeException);
}

// --- Explicit VertexDeclaration overload ---

TEST_F(DrawUserPrimitivesArgumentGuardTest, VD_ZeroCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc.data(), 0, 0, vd),
        System::ArgumentOutOfRangeException);
}

TEST_F(DrawUserPrimitivesArgumentGuardTest, VD_NegativeCount_Throws)
{
    VertexDeclaration vd {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)
    };
    EXPECT_THROW(
        gd.DrawUserPrimitives(PrimitiveType::TriangleList, vpc.data(), 0, -1, vd),
        System::ArgumentOutOfRangeException);
}
