// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// ── Default constructor ─────────────────────────────────────────────────────

TEST(VertexElementTest, DefaultOffsetZero)
{
    VertexElement ve;
    EXPECT_EQ(ve.getOffsetProperty(), 0);
}

TEST(VertexElementTest, DefaultFormatSingle)
{
    VertexElement ve;
    EXPECT_EQ(ve.getVertexElementFormatProperty(), VertexElementFormat::Single);
}

TEST(VertexElementTest, DefaultUsagePosition)
{
    VertexElement ve;
    EXPECT_EQ(ve.getVertexElementUsageProperty(), VertexElementUsage::Position);
}

TEST(VertexElementTest, DefaultUsageIndexZero)
{
    VertexElement ve;
    EXPECT_EQ(ve.getUsageIndexProperty(), 0);
}

// ── Parameterized constructor ───────────────────────────────────────────────

TEST(VertexElementTest, CtorOffset)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.getOffsetProperty(), 12);
}

TEST(VertexElementTest, CtorFormat)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.getVertexElementFormatProperty(), VertexElementFormat::Vector3);
}

TEST(VertexElementTest, CtorUsage)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.getVertexElementUsageProperty(), VertexElementUsage::Normal);
}

TEST(VertexElementTest, CtorUsageIndex)
{
    VertexElement ve(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    EXPECT_EQ(ve.getUsageIndexProperty(), 1);
}

TEST(VertexElementTest, CtorColorFormat)
{
    VertexElement ve(12, VertexElementFormat::Color, VertexElementUsage::Color, 0);
    EXPECT_EQ(ve.getVertexElementFormatProperty(), VertexElementFormat::Color);
    EXPECT_EQ(ve.getVertexElementUsageProperty(), VertexElementUsage::Color);
}

TEST(VertexElementTest, CtorSecondTexcoord)
{
    VertexElement ve(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    EXPECT_EQ(ve.getOffsetProperty(), 20);
    EXPECT_EQ(ve.getUsageIndexProperty(), 1);
}

// ── Property setters ────────────────────────────────────────────────────────

TEST(VertexElementTest, SetOffset)
{
    VertexElement ve;
    ve.setOffsetProperty(8);
    EXPECT_EQ(ve.getOffsetProperty(), 8);
}

TEST(VertexElementTest, SetFormat)
{
    VertexElement ve;
    ve.setVertexElementFormatProperty(VertexElementFormat::Vector4);
    EXPECT_EQ(ve.getVertexElementFormatProperty(), VertexElementFormat::Vector4);
}

TEST(VertexElementTest, SetUsage)
{
    VertexElement ve;
    ve.setVertexElementUsageProperty(VertexElementUsage::Normal);
    EXPECT_EQ(ve.getVertexElementUsageProperty(), VertexElementUsage::Normal);
}

TEST(VertexElementTest, SetUsageIndex)
{
    VertexElement ve;
    ve.setUsageIndexProperty(2);
    EXPECT_EQ(ve.getUsageIndexProperty(), 2);
}

// ── Equality ────────────────────────────────────────────────────────────────

TEST(VertexElementTest, EqualSameValues)
{
    VertexElement a(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    VertexElement b(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexElementTest, NotEqualDifferentOffset)
{
    VertexElement a(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    VertexElement b(12, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexElementTest, NotEqualDifferentFormat)
{
    VertexElement a(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    VertexElement b(0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexElementTest, NotEqualDifferentUsage)
{
    VertexElement a(0, VertexElementFormat::Color, VertexElementUsage::Position, 0);
    VertexElement b(0, VertexElementFormat::Color, VertexElementUsage::Color,    0);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexElementTest, NotEqualDifferentUsageIndex)
{
    VertexElement a(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0);
    VertexElement b(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexElementTest, EqualsMethod)
{
    VertexElement a(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    VertexElement b(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_TRUE(a.Equals(b));
}

// ── GetHashCode ─────────────────────────────────────────────────────────────

TEST(VertexElementTest, GetHashCodeConsistentEqualElements)
{
    VertexElement a(12, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    VertexElement b(12, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

// ── ToString ────────────────────────────────────────────────────────────────

TEST(VertexElementTest, ToStringContainsOffset)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    EXPECT_NE(ve.ToString().find("12"), std::string::npos);
}

TEST(VertexElementTest, ToStringContainsFormat)
{
    VertexElement ve(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0);
    EXPECT_NE(ve.ToString().find("Vector3"), std::string::npos);
}

TEST(VertexElementTest, ToStringContainsUsage)
{
    VertexElement ve(0, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_NE(ve.ToString().find("Normal"), std::string::npos);
}

TEST(VertexElementTest, ToStringDefaultFormat)
{
    VertexElement ve(0, VertexElementFormat::Color, VertexElementUsage::TextureCoordinate, 1);
    const std::string s = ve.ToString();
    EXPECT_NE(s.find("Offset:0"), std::string::npos);
    EXPECT_NE(s.find("Format:Color"), std::string::npos);
    EXPECT_NE(s.find("Usage:TextureCoordinate"), std::string::npos);
    EXPECT_NE(s.find("UsageIndex: 1"), std::string::npos);
}

// ── VertexElementFormat numeric values ──────────────────────────────────────

TEST(VertexElementFormatTest, SingleIsZero)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Single), 0);
}

TEST(VertexElementFormatTest, Vector2IsOne)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Vector2), 1);
}

TEST(VertexElementFormatTest, Vector3IsTwo)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Vector3), 2);
}

TEST(VertexElementFormatTest, Vector4IsThree)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Vector4), 3);
}

TEST(VertexElementFormatTest, ColorIsFour)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Color), 4);
}

TEST(VertexElementFormatTest, Byte4IsFive)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Byte4), 5);
}

TEST(VertexElementFormatTest, Short2IsSix)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Short2), 6);
}

TEST(VertexElementFormatTest, Short4IsSeven)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::Short4), 7);
}

TEST(VertexElementFormatTest, NormalizedShort2IsEight)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::NormalizedShort2), 8);
}

TEST(VertexElementFormatTest, NormalizedShort4IsNine)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::NormalizedShort4), 9);
}

TEST(VertexElementFormatTest, HalfVector2IsTen)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::HalfVector2), 10);
}

TEST(VertexElementFormatTest, HalfVector4IsEleven)
{
    EXPECT_EQ(static_cast<int>(VertexElementFormat::HalfVector4), 11);
}

// ── VertexElementUsage numeric values ───────────────────────────────────────

TEST(VertexElementUsageTest, PositionIsZero)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Position), 0);
}

TEST(VertexElementUsageTest, ColorIsOne)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Color), 1);
}

TEST(VertexElementUsageTest, TextureCoordinateIsTwo)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::TextureCoordinate), 2);
}

TEST(VertexElementUsageTest, NormalIsThree)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Normal), 3);
}

TEST(VertexElementUsageTest, BinormalIsFour)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Binormal), 4);
}

TEST(VertexElementUsageTest, TangentIsFive)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Tangent), 5);
}

TEST(VertexElementUsageTest, BlendIndicesIsSix)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::BlendIndices), 6);
}

TEST(VertexElementUsageTest, BlendWeightIsSeven)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::BlendWeight), 7);
}

TEST(VertexElementUsageTest, DepthIsEight)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Depth), 8);
}

TEST(VertexElementUsageTest, FogIsNine)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Fog), 9);
}

TEST(VertexElementUsageTest, PointSizeIsTen)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::PointSize), 10);
}

TEST(VertexElementUsageTest, SampleIsEleven)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::Sample), 11);
}

TEST(VertexElementUsageTest, TessellateFactorIsTwelve)
{
    EXPECT_EQ(static_cast<int>(VertexElementUsage::TessellateFactor), 12);
}
