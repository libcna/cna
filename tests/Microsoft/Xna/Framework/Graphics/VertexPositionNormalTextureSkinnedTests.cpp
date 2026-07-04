// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTextureSkinned;

namespace
{
    VertexPositionNormalTextureSkinned MakeSample()
    {
        return VertexPositionNormalTextureSkinned(
            Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f),
            Vector4(0.25f, 0.25f, 0.25f, 0.25f), {1, 2, 3, 4});
    }
}

// --- Default constructor ---

TEST(VertexPositionNormalTextureSkinnedTest, DefaultPositionZero)
{
    VertexPositionNormalTextureSkinned v;
    EXPECT_FLOAT_EQ(v.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 0.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, DefaultNormalZero)
{
    VertexPositionNormalTextureSkinned v;
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 0.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, DefaultTexCoordZero)
{
    VertexPositionNormalTextureSkinned v;
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.0f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, DefaultBlendWeightZero)
{
    VertexPositionNormalTextureSkinned v;
    EXPECT_FLOAT_EQ(v.BlendWeight.X, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.W, 0.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, DefaultBlendIndicesZero)
{
    VertexPositionNormalTextureSkinned v;
    EXPECT_EQ(v.BlendIndices[0], 0);
    EXPECT_EQ(v.BlendIndices[1], 0);
    EXPECT_EQ(v.BlendIndices[2], 0);
    EXPECT_EQ(v.BlendIndices[3], 0);
}

// --- Parameterized constructor ---

TEST(VertexPositionNormalTextureSkinnedTest, CtorPosition)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 3.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, CtorNormal)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 0.0f);
}

TEST(VertexPositionNormalTextureSkinnedTest, CtorTexCoord)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.5f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.5f);
}

TEST(VertexPositionNormalTextureSkinnedTest, CtorBlendWeight)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.BlendWeight.X, 0.25f);
    EXPECT_FLOAT_EQ(v.BlendWeight.W, 0.25f);
}

TEST(VertexPositionNormalTextureSkinnedTest, CtorBlendIndices)
{
    auto v = MakeSample();
    EXPECT_EQ(v.BlendIndices[0], 1);
    EXPECT_EQ(v.BlendIndices[1], 2);
    EXPECT_EQ(v.BlendIndices[2], 3);
    EXPECT_EQ(v.BlendIndices[3], 4);
}

// --- Equality operators ---

TEST(VertexPositionNormalTextureSkinnedTest, EqualityTrue)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionNormalTextureSkinnedTest, EqualityFalse)
{
    auto a = MakeSample();
    VertexPositionNormalTextureSkinned b(
        Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f),
        Vector4(0.25f, 0.25f, 0.25f, 0.25f), {9, 9, 9, 9});
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Vertex declaration ---
// NOTE: logical GPU layout is 52 bytes (Vector3=12 + Vector3=12 + Vector2=8 + Vector4=16 +
// Byte4=4), but sizeof() is larger because IVertexType adds a vtable pointer. Offsets below
// are hardcoded to the logical 52-byte layout, matching VertexBuffer::SetData's packing.

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements().size(), 5u);
}

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationPositionElement)
{
    const auto& elems = VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].getOffsetProperty(), 0);
    EXPECT_EQ(elems[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].getVertexElementUsageProperty(),  VertexElementUsage::Position);
}

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationNormalElement)
{
    const auto& elems = VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].getOffsetProperty(), 12);
    EXPECT_EQ(elems[1].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[1].getVertexElementUsageProperty(),  VertexElementUsage::Normal);
}

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationTexCoordElement)
{
    const auto& elems = VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[2].getOffsetProperty(), 24);
    EXPECT_EQ(elems[2].getVertexElementFormatProperty(), VertexElementFormat::Vector2);
    EXPECT_EQ(elems[2].getVertexElementUsageProperty(),  VertexElementUsage::TextureCoordinate);
}

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationBlendWeightElement)
{
    const auto& elems = VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[3].getOffsetProperty(), 32);
    EXPECT_EQ(elems[3].getVertexElementFormatProperty(), VertexElementFormat::Vector4);
    EXPECT_EQ(elems[3].getVertexElementUsageProperty(),  VertexElementUsage::BlendWeight);
}

TEST(VertexPositionNormalTextureSkinnedTest, DeclarationBlendIndicesElement)
{
    const auto& elems = VertexPositionNormalTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[4].getOffsetProperty(), 48);
    EXPECT_EQ(elems[4].getVertexElementFormatProperty(), VertexElementFormat::Byte4);
    EXPECT_EQ(elems[4].getVertexElementUsageProperty(),  VertexElementUsage::BlendIndices);
}

// --- Equals ---

TEST(VertexPositionNormalTextureSkinnedTest, EqualsMethod)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_TRUE(a.Equals(b));
    VertexPositionNormalTextureSkinned c(
        Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f),
        Vector4(0.25f, 0.25f, 0.25f, 0.25f), {9, 9, 9, 9});
    EXPECT_FALSE(a.Equals(c));
}

// --- GetHashCode ---

TEST(VertexPositionNormalTextureSkinnedTest, GetHashCodeConsistent)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(VertexPositionNormalTextureSkinnedTest, ToStringContainsPosition)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("Position:"), std::string::npos);
}

TEST(VertexPositionNormalTextureSkinnedTest, ToStringContainsBlendWeight)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("BlendWeight:"), std::string::npos);
}

TEST(VertexPositionNormalTextureSkinnedTest, ToStringContainsBlendIndices)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("BlendIndices:"), std::string::npos);
}

TEST(VertexPositionNormalTextureSkinnedTest, ToStringDoubleBraces)
{
    auto v = MakeSample();
    const std::string s = v.ToString();
    EXPECT_EQ(s.substr(0, 2), "{{");
    EXPECT_EQ(s.substr(s.size() - 2), "}}");
}
