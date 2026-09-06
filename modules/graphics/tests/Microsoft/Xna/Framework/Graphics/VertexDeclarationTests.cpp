// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <vector>
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

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

// SAMPLE-066: XNA's VertexDeclaration(params VertexElement[]) takes an array variable as readily
// as an array literal, and a C++ game holding its elements in a vector -- which is what
// GetVertexElements() hands back -- has no literal to spell. BoxCollider's debug box builds its
// declaration from exactly that.
TEST(VertexDeclarationTest, ElementVectorDerivesTheSameStrideAsTheInitializerList)
{
    const std::vector<VertexElement> elements{
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)};

    VertexDeclaration fromVector(elements);
    VertexDeclaration fromLiteral{
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0)};

    EXPECT_EQ(fromVector.getVertexStrideProperty(), 16);
    EXPECT_EQ(fromVector.getVertexStrideProperty(), fromLiteral.getVertexStrideProperty());
    EXPECT_EQ(fromVector.GetVertexElements(), fromLiteral.GetVertexElements());
}

TEST(VertexDeclarationTest, AnEmptyElementVectorThrowsArgumentNullException)
{
    EXPECT_THROW(VertexDeclaration(std::vector<VertexElement>{}), System::ArgumentNullException);
}

TEST(VertexDeclarationTest, EmptyAutoStrideElementsThrowArgumentNullException)
{
    EXPECT_THROW(
        VertexDeclaration(std::initializer_list<VertexElement>{}),
        System::ArgumentNullException);
}

TEST(VertexDeclarationTest, EmptyExplicitStrideElementsThrowArgumentNullException)
{
    EXPECT_THROW(
        VertexDeclaration(16, std::initializer_list<VertexElement>{}),
        System::ArgumentNullException);
}

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

TEST(VertexDeclarationTest, EmptyVectorElementsThrowArgumentNullException)
{
    EXPECT_THROW(
        VertexDeclaration(16, std::vector<VertexElement>{}),
        System::ArgumentNullException);
}

TEST(VertexDeclarationTest, ZeroExplicitStrideThrowsArgumentOutOfRangeException)
{
    EXPECT_THROW(
        VertexDeclaration(0, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        }),
        System::ArgumentOutOfRangeException);
}

TEST(VertexDeclarationTest, NegativeExplicitStrideThrowsArgumentOutOfRangeException)
{
    EXPECT_THROW(
        VertexDeclaration(-1, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        }),
        System::ArgumentOutOfRangeException);
}

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

// ── Task 246: tangent and binormal usages ────────────────────────────────────

TEST(VertexDeclarationTest, TangentUsageStoredInDeclaration)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getVertexElementUsageProperty(),
              VertexElementUsage::Tangent);
}

TEST(VertexDeclarationTest, BinormalUsageStoredInDeclaration)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Binormal, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getVertexElementUsageProperty(),
              VertexElementUsage::Binormal);
}

// Tangent Vector3 at offset 0 → stride = 12
TEST(VertexDeclarationTest, AutoStrideTangentVector3)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 12);
}

// Binormal Vector3 at offset 0 → stride = 12
TEST(VertexDeclarationTest, AutoStrideBinormalVector3)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Binormal, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 12);
}

// Position(12) + Normal(12) + Tangent at 24(12) → stride = 36
TEST(VertexDeclarationTest, AutoStridePositionNormalTangent)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,   0),
        VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent,  0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 36);
    EXPECT_EQ(vd.GetVertexElements()[2].getVertexElementUsageProperty(),
              VertexElementUsage::Tangent);
}

// Full PBR-style vertex: Pos(12)+Normal(12)+Tangent(12)+Binormal(12)+TexCoord(8)
// offsets: 0, 12, 24, 36, 48 → stride = 56
TEST(VertexDeclarationTest, FullPbrVertexDeclaration)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
        VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent,           0),
        VertexElement(36, VertexElementFormat::Vector3, VertexElementUsage::Binormal,          0),
        VertexElement(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 56);
    EXPECT_EQ(vd.GetVertexElements().size(), 5u);
    EXPECT_EQ(vd.GetVertexElements()[2].getVertexElementUsageProperty(), VertexElementUsage::Tangent);
    EXPECT_EQ(vd.GetVertexElements()[3].getVertexElementUsageProperty(), VertexElementUsage::Binormal);
}

// ── Task 245: color and byte/short/half element format sizes ─────────────────
// Each test places a single element at offset 0 and checks that the auto-stride
// equals the expected byte size for that format (matches FNA GetTypeSize).

TEST(VertexDeclarationTest, AutoStrideColorFormat)       { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::Color,             VertexElementUsage::Color,            0) }).getVertexStrideProperty(),  4); }
TEST(VertexDeclarationTest, AutoStrideByte4Format)       { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::Byte4,             VertexElementUsage::BlendIndices,     0) }).getVertexStrideProperty(),  4); }
TEST(VertexDeclarationTest, AutoStrideShort2Format)      { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::Short2,            VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  4); }
TEST(VertexDeclarationTest, AutoStrideShort4Format)      { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::Short4,            VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  8); }
TEST(VertexDeclarationTest, AutoStrideNormalizedShort2)  { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::NormalizedShort2,  VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  4); }
TEST(VertexDeclarationTest, AutoStrideNormalizedShort4)  { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::NormalizedShort4,  VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  8); }
TEST(VertexDeclarationTest, AutoStrideHalfVector2Format) { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::HalfVector2,       VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  4); }
TEST(VertexDeclarationTest, AutoStrideHalfVector4Format) { EXPECT_EQ(VertexDeclaration({ VertexElement(0, VertexElementFormat::HalfVector4,       VertexElementUsage::TextureCoordinate,0) }).getVertexStrideProperty(),  8); }

// Combined: Vector3(12) + Byte4 at 12 → stride = 16
TEST(VertexDeclarationTest, AutoStridePositionPlusByte4)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,     0),
        VertexElement(12, VertexElementFormat::Byte4,   VertexElementUsage::BlendIndices, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

// Combined: Vector3(12) + NormalizedShort4 at 12 → stride = 20
TEST(VertexDeclarationTest, AutoStridePositionPlusNormalizedShort4)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3,           VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::NormalizedShort4,  VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 20);
}

// Combined: Vector3(12) + HalfVector2 at 12 → stride = 16
TEST(VertexDeclarationTest, AutoStridePositionPlusHalfVector2)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3,    VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::HalfVector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 16);
}

// ── Task 244: multiple texture coordinate channels ───────────────────────────

// usageIndex=0 is stored and retrieved correctly.
TEST(VertexDeclarationTest, TexCoordUsageIndexZeroStored)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getUsageIndexProperty(), 0);
}

// Two TextureCoordinate channels: usageIndex 0 and 1 are stored independently.
TEST(VertexDeclarationTest, TexCoordTwoChannelsUsageIndices)
{
    VertexDeclaration vd({
        VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(8, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getUsageIndexProperty(), 0);
    EXPECT_EQ(vd.GetVertexElements()[1].getUsageIndexProperty(), 1);
}

// Three TextureCoordinate channels: usageIndex 0, 1, 2 all stored correctly.
TEST(VertexDeclarationTest, TexCoordThreeChannelsUsageIndices)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(8,  VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 2),
    });
    EXPECT_EQ(vd.GetVertexElements()[0].getUsageIndexProperty(), 0);
    EXPECT_EQ(vd.GetVertexElements()[1].getUsageIndexProperty(), 1);
    EXPECT_EQ(vd.GetVertexElements()[2].getUsageIndexProperty(), 2);
}

// usageIndex is independent of VertexElementUsage — Position and Color with index 0
// do not affect TextureCoordinate usageIndex.
TEST(VertexDeclarationTest, TexCoordUsageIndexIndependentOfOtherUsages)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,             0),
        VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    });
    EXPECT_EQ(vd.GetVertexElements()[2].getUsageIndexProperty(), 1);
    EXPECT_EQ(vd.GetVertexElements()[2].getVertexElementUsageProperty(),
              VertexElementUsage::TextureCoordinate);
}

// Auto-stride with three Vector2 texture coordinate channels.
// 3 × Vector2 (8B) packed: stride = max(0+8, 8+8, 16+8) = 24
TEST(VertexDeclarationTest, AutoStrideThreeTexCoordChannels)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(8,  VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        VertexElement(16, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 2),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 24);
    EXPECT_EQ(vd.GetVertexElements().size(), 3u);
}

// Mixed: Position + Normal + two texture coordinates; usageIndex 0 and 1 preserved.
TEST(VertexDeclarationTest, MixedDeclWithTwoTexCoordChannels)
{
    VertexDeclaration vd({
        VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position,         0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal,            0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(32, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    });
    EXPECT_EQ(vd.getVertexStrideProperty(), 40);
    EXPECT_EQ(vd.GetVertexElements()[2].getUsageIndexProperty(), 0);
    EXPECT_EQ(vd.GetVertexElements()[3].getUsageIndexProperty(), 1);
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
