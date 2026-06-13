// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

// --- Default constructor ---

TEST(VertexPositionNormalTextureTest, DefaultPositionZero)
{
    VertexPositionNormalTexture v;
    EXPECT_FLOAT_EQ(v.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 0.0f);
}

TEST(VertexPositionNormalTextureTest, DefaultNormalZero)
{
    VertexPositionNormalTexture v;
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 0.0f);
}

TEST(VertexPositionNormalTextureTest, DefaultTexCoordZero)
{
    VertexPositionNormalTexture v;
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.0f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.0f);
}

// --- Parameterized constructor ---

TEST(VertexPositionNormalTextureTest, CtorPosition)
{
    VertexPositionNormalTexture v(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f));
    EXPECT_FLOAT_EQ(v.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 3.0f);
}

TEST(VertexPositionNormalTextureTest, CtorNormal)
{
    VertexPositionNormalTexture v(Vector3(0, 0, 0), Vector3(0, 0, 1), Vector2(0, 0));
    EXPECT_FLOAT_EQ(v.Normal.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Normal.Z, 1.0f);
}

TEST(VertexPositionNormalTextureTest, CtorTexCoord)
{
    VertexPositionNormalTexture v(Vector3(0, 0, 0), Vector3(0, 1, 0), Vector2(0.25f, 0.75f));
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.25f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.75f);
}

// --- Equality operators ---

TEST(VertexPositionNormalTextureTest, EqualityTrue)
{
    VertexPositionNormalTexture a(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f));
    VertexPositionNormalTexture b(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionNormalTextureTest, EqualityFalse)
{
    VertexPositionNormalTexture a(Vector3(1, 2, 3), Vector3(0, 1, 0), Vector2(0.5f, 0.5f));
    VertexPositionNormalTexture b(Vector3(1, 2, 3), Vector3(0, 0, 1), Vector2(0.5f, 0.5f));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Vertex declaration ---
// NOTE: XNA specifies stride=32 (Vector3=12 + Vector3=12 + Vector2=8), but sizeof=40
// because IVertexType adds a vtable pointer. Element offsets are hardcoded in the declaration.

TEST(VertexPositionNormalTextureTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionNormalTexture::getVertexDeclarationStatic().GetVertexElements().size(), 3u);
}

TEST(VertexPositionNormalTextureTest, DeclarationPositionElement)
{
    const auto& elems = VertexPositionNormalTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].Offset, 0);
    EXPECT_EQ(elems[0].VertexElementFormatValue, VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].VertexElementUsageValue,  VertexElementUsage::Position);
}

TEST(VertexPositionNormalTextureTest, DeclarationNormalElement)
{
    const auto& elems = VertexPositionNormalTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].Offset, 12);
    EXPECT_EQ(elems[1].VertexElementFormatValue, VertexElementFormat::Vector3);
    EXPECT_EQ(elems[1].VertexElementUsageValue,  VertexElementUsage::Normal);
}

TEST(VertexPositionNormalTextureTest, DeclarationTexCoordElement)
{
    const auto& elems = VertexPositionNormalTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[2].Offset, 24);
    EXPECT_EQ(elems[2].VertexElementFormatValue, VertexElementFormat::Vector2);
    EXPECT_EQ(elems[2].VertexElementUsageValue,  VertexElementUsage::TextureCoordinate);
}
