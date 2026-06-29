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

// ── Task 243: unusual offsets ────────────────────────────────────────────────

// Auto-stride with a single element that does not start at offset 0.
// stride = max(4 + 4) = 8
TEST(VertexDeclarationTest, AutoStrideNonZeroStart)
{
    VertexDeclaration vd({
        VertexElement(4, VertexElementFormat::Single, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 8);
}

// Auto-stride with a large leading padding before the only element.
// stride = max(8 + 8) = 16
TEST(VertexDeclarationTest, AutoStrideLeadingPadding)
{
    VertexDeclaration vd({
        VertexElement(8, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

// Auto-stride where there is an explicit gap (padding) between two elements.
// elem0: offset=0, Vector2 (8B) → end=8; elem1: offset=12, Single (4B) → end=16
// stride = max(8, 16) = 16; bytes 8..11 are padding
TEST(VertexDeclarationTest, AutoStridePaddingGapBetweenElements)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector2, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Single,  VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

// Auto-stride with elements supplied in reverse offset order.
// elem0: offset=12, Color (4B) → end=16; elem1: offset=0, Vector3 (12B) → end=12
// stride = max(16, 12) = 16
TEST(VertexDeclarationTest, AutoStrideElementsOutOfOffsetOrder)
{
    VertexDeclaration vd({
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

// GetVertexElements() preserves insertion order even when offsets are not ascending.
TEST(VertexDeclarationTest, ElementInsertionOrderPreservedWhenOutOfOffset)
{
    VertexDeclaration vd({
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0),
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getOffsetProperty(), 12);
    EXPECT_EQ(vd.GetVertexElements()[1].getOffsetProperty(), 0);
}

// Explicit stride is respected even when the element layout ends before it.
// Elements cover only 12 bytes; explicit stride=24 adds 12 bytes of trailing padding.
TEST(VertexDeclarationTest, ExplicitStrideAllowsTrailingPadding)
{
    VertexDeclaration vd(24, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 24);
}

// Explicit stride is respected even when the first element has a non-zero offset.
// The layout has 4 bytes of leading padding + Vector3 (12B) = 16B used, stride=32.
TEST(VertexDeclarationTest, ExplicitStrideWithNonZeroStartElement)
{
    VertexDeclaration vd(32, {
        VertexElement(4, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 32);
}
