// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// --- Default constructor ---

TEST(VertexDeclarationTest, DefaultStrideZero)
{
    VertexDeclaration vd;
    EXPECT_EQ(vd.getVertexStrideProperty(), 0);
}

TEST(VertexDeclarationTest, DefaultElementsEmpty)
{
    VertexDeclaration vd;
    EXPECT_TRUE(vd.GetVertexElements().empty());
}

// --- Initializer-list constructor ---

TEST(VertexDeclarationTest, InitListStride)
{
    VertexDeclaration vd(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

TEST(VertexDeclarationTest, InitListElementCount)
{
    VertexDeclaration vd(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.GetVertexElements().size(), 2u);
}

TEST(VertexDeclarationTest, InitListFirstElementOffset)
{
    VertexDeclaration vd(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getOffsetProperty(), 0);
}

TEST(VertexDeclarationTest, InitListSecondElementOffset)
{
    VertexDeclaration vd(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.GetVertexElements()[1].getOffsetProperty(), 12);
}

TEST(VertexDeclarationTest, InitListFirstElementFormat)
{
    VertexDeclaration vd(12, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
}

TEST(VertexDeclarationTest, InitListFirstElementUsage)
{
    VertexDeclaration vd(12, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getVertexElementUsageProperty(), VertexElementUsage::Position);
}

// --- Vector constructor ---

TEST(VertexDeclarationTest, VectorCtorStride)
{
    std::vector<VertexElement> elems = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };
    VertexDeclaration vd(20, elems);
    EXPECT_EQ(vd.getVertexStrideProperty(), 20);
}

TEST(VertexDeclarationTest, VectorCtorElementCount)
{
    std::vector<VertexElement> elems = {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };
    VertexDeclaration vd(20, elems);
    EXPECT_EQ(vd.GetVertexElements().size(), 2u);
}

// --- Stride values matching built-in vertex types ---

TEST(VertexDeclarationTest, PositionColorStride16)
{
    // VertexPositionColor: Vector3(12) + Color(4) = 16
    VertexDeclaration vd(16, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

TEST(VertexDeclarationTest, PositionTextureStride20)
{
    // VertexPositionTexture: Vector3(12) + Vector2(8) = 20
    VertexDeclaration vd(20, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 20);
}

TEST(VertexDeclarationTest, PositionNormalTextureStride32)
{
    // VertexPositionNormalTexture: Vector3(12) + Vector3(12) + Vector2(8) = 32
    VertexDeclaration vd(32, {
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 32);
    EXPECT_EQ(vd.GetVertexElements().size(), 3u);
}

// ── Auto-stride constructor (no explicit stride) ────────────────────────────

TEST(VertexDeclarationTest, AutoStridePositionColor)
{
    // Vector3(12) + Color at offset 12 (4 bytes) → stride = 16
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
    EXPECT_EQ(vd.GetVertexElements().size(), 2u);
}

TEST(VertexDeclarationTest, AutoStridePositionTexture)
{
    // Vector3(12) + Vector2 at offset 12 (8 bytes) → stride = 20
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 20);
}

TEST(VertexDeclarationTest, AutoStridePositionNormalTexture)
{
    // Vector3(12) + Vector3 at 12(12) + Vector2 at 24(8) → stride = 32
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,          0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,             0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 32);
}

TEST(VertexDeclarationTest, AutoStrideElementsPreserved)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getOffsetProperty(), 0);
    EXPECT_EQ(vd.GetVertexElements()[1].getOffsetProperty(), 12);
}

// ── GetTypeName ─────────────────────────────────────────────────────────────

TEST(VertexDeclarationTest, GetTypeNameReturnsXnaName)
{
    VertexDeclaration vd;
    EXPECT_EQ(vd.GetTypeName(), "Microsoft.Xna.Framework.Graphics.VertexDeclaration");
}
