// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTangentTextureSkinned;

namespace
{
    VertexPositionNormalTangentTextureSkinned MakeSample()
    {
        return VertexPositionNormalTangentTextureSkinned(
            Vector3(1, 2, 3), Vector3(0, 1, 0), Vector4(1, 0, 0, 1), Vector2(0.5f, 0.5f),
            Vector4(0.25f, 0.25f, 0.25f, 0.25f), {1, 2, 3, 4});
    }
}

// --- Default constructor ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultPositionZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_FLOAT_EQ(v.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultNormalZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultTangentZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_FLOAT_EQ(v.Tangent.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Tangent.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Tangent.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.Tangent.W, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultTexCoordZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.0f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultBlendWeightZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_FLOAT_EQ(v.BlendWeight.X, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.BlendWeight.W, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DefaultBlendIndicesZero)
{
    VertexPositionNormalTangentTextureSkinned v;
    EXPECT_EQ(v.BlendIndices[0], 0);
    EXPECT_EQ(v.BlendIndices[1], 0);
    EXPECT_EQ(v.BlendIndices[2], 0);
    EXPECT_EQ(v.BlendIndices[3], 0);
}

// --- Parameterized constructor ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorPosition)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 3.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorNormal)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 1.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 0.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorTangent)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.Tangent.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Tangent.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Tangent.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.Tangent.W, 1.0f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorTexCoord)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.5f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.5f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorBlendWeight)
{
    auto v = MakeSample();
    EXPECT_FLOAT_EQ(v.BlendWeight.X, 0.25f);
    EXPECT_FLOAT_EQ(v.BlendWeight.W, 0.25f);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, CtorBlendIndices)
{
    auto v = MakeSample();
    EXPECT_EQ(v.BlendIndices[0], 1);
    EXPECT_EQ(v.BlendIndices[1], 2);
    EXPECT_EQ(v.BlendIndices[2], 3);
    EXPECT_EQ(v.BlendIndices[3], 4);
}

// --- Equality operators ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, EqualityTrue)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, EqualityFalse)
{
    auto a = MakeSample();
    VertexPositionNormalTangentTextureSkinned b(
        Vector3(1, 2, 3), Vector3(0, 1, 0), Vector4(1, 0, 0, 1), Vector2(0.5f, 0.5f),
        Vector4(0.25f, 0.25f, 0.25f, 0.25f), {9, 9, 9, 9});
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Vertex declaration ---
// Logical GPU layout is 68 bytes (Vector3=12 + Vector3=12 + Vector4=16 + Vector2=8 + Vector4=16 +
// Byte4=4); offsets below match VertexBuffer::SetDataRaw's own packing.

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements().size(), 6u);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationPositionElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].getOffsetProperty(), 0);
    EXPECT_EQ(elems[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].getVertexElementUsageProperty(),  VertexElementUsage::Position);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationNormalElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].getOffsetProperty(), 12);
    EXPECT_EQ(elems[1].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[1].getVertexElementUsageProperty(),  VertexElementUsage::Normal);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationTangentElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[2].getOffsetProperty(), 24);
    EXPECT_EQ(elems[2].getVertexElementFormatProperty(), VertexElementFormat::Vector4);
    EXPECT_EQ(elems[2].getVertexElementUsageProperty(),  VertexElementUsage::Tangent);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationTexCoordElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[3].getOffsetProperty(), 40);
    EXPECT_EQ(elems[3].getVertexElementFormatProperty(), VertexElementFormat::Vector2);
    EXPECT_EQ(elems[3].getVertexElementUsageProperty(),  VertexElementUsage::TextureCoordinate);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationBlendWeightElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[4].getOffsetProperty(), 48);
    EXPECT_EQ(elems[4].getVertexElementFormatProperty(), VertexElementFormat::Vector4);
    EXPECT_EQ(elems[4].getVertexElementUsageProperty(),  VertexElementUsage::BlendWeight);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, DeclarationBlendIndicesElement)
{
    const auto& elems = VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[5].getOffsetProperty(), 64);
    EXPECT_EQ(elems[5].getVertexElementFormatProperty(), VertexElementFormat::Byte4);
    EXPECT_EQ(elems[5].getVertexElementUsageProperty(),  VertexElementUsage::BlendIndices);
}

// --- Equals ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, EqualsMethod)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_TRUE(a.Equals(b));
    VertexPositionNormalTangentTextureSkinned c(
        Vector3(1, 2, 3), Vector3(0, 1, 0), Vector4(1, 0, 0, 1), Vector2(0.5f, 0.5f),
        Vector4(0.25f, 0.25f, 0.25f, 0.25f), {9, 9, 9, 9});
    EXPECT_FALSE(a.Equals(c));
}

// --- GetHashCode ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, GetHashCodeConsistent)
{
    auto a = MakeSample();
    auto b = MakeSample();
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(VertexPositionNormalTangentTextureSkinnedTest, ToStringContainsPosition)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("Position:"), std::string::npos);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, ToStringContainsTangent)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("Tangent:"), std::string::npos);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, ToStringContainsBlendWeight)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("BlendWeight:"), std::string::npos);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, ToStringContainsBlendIndices)
{
    auto v = MakeSample();
    EXPECT_NE(v.ToString().find("BlendIndices:"), std::string::npos);
}

TEST(VertexPositionNormalTangentTextureSkinnedTest, ToStringDoubleBraces)
{
    auto v = MakeSample();
    const std::string s = v.ToString();
    EXPECT_EQ(s.substr(0, 2), "{{");
    EXPECT_EQ(s.substr(s.size() - 2), "}}");
}
