// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

// --- Default constructor ---

TEST(VertexPositionTextureTest, DefaultPositionZero)
{
    VertexPositionTexture v;
    EXPECT_FLOAT_EQ(v.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 0.0f);
}

TEST(VertexPositionTextureTest, DefaultTexCoordZero)
{
    VertexPositionTexture v;
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.0f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.0f);
}

// --- Parameterized constructor ---

TEST(VertexPositionTextureTest, CtorPosition)
{
    VertexPositionTexture v(Vector3(1.0f, 2.0f, 3.0f), Vector2(0.5f, 0.25f));
    EXPECT_FLOAT_EQ(v.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 3.0f);
}

TEST(VertexPositionTextureTest, CtorTexCoord)
{
    VertexPositionTexture v(Vector3(0, 0, 0), Vector2(0.5f, 0.75f));
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.5f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.75f);
}

// --- Equality operators ---

TEST(VertexPositionTextureTest, EqualityTrue)
{
    VertexPositionTexture a(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    VertexPositionTexture b(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionTextureTest, EqualityFalse)
{
    VertexPositionTexture a(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    VertexPositionTexture b(Vector3(1, 2, 4), Vector2(0.5f, 0.5f));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Vertex declaration ---
// NOTE: XNA specifies stride=20 (Vector3=12 + Vector2=8), but sizeof(VertexPositionTexture)=32
// because IVertexType adds a vtable pointer. Element offsets in the declaration are hardcoded.

TEST(VertexPositionTextureTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionTexture::getVertexDeclarationStatic().GetVertexElements().size(), 2u);
}

TEST(VertexPositionTextureTest, DeclarationPositionElement)
{
    const auto& elems = VertexPositionTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].getOffsetProperty(), 0);
    EXPECT_EQ(elems[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].getVertexElementUsageProperty(),  VertexElementUsage::Position);
}

TEST(VertexPositionTextureTest, DeclarationTexCoordElement)
{
    const auto& elems = VertexPositionTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].getOffsetProperty(), 12);
    EXPECT_EQ(elems[1].getVertexElementFormatProperty(), VertexElementFormat::Vector2);
    EXPECT_EQ(elems[1].getVertexElementUsageProperty(),  VertexElementUsage::TextureCoordinate);
}

// --- Equals ---

TEST(VertexPositionTextureTest, EqualsMethod)
{
    VertexPositionTexture a(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    VertexPositionTexture b(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    EXPECT_TRUE(a.Equals(b));
    VertexPositionTexture c(Vector3(1, 2, 4), Vector2(0.5f, 0.5f));
    EXPECT_FALSE(a.Equals(c));
}

// --- GetHashCode ---

TEST(VertexPositionTextureTest, GetHashCodeConsistent)
{
    VertexPositionTexture a(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    VertexPositionTexture b(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(VertexPositionTextureTest, ToStringContainsPosition)
{
    VertexPositionTexture v(Vector3(1, 2, 3), Vector2(0.5f, 0.5f));
    EXPECT_NE(v.ToString().find("Position:"), std::string::npos);
}

TEST(VertexPositionTextureTest, ToStringContainsTextureCoordinate)
{
    VertexPositionTexture v(Vector3(0, 0, 0), Vector2(0.3f, 0.7f));
    EXPECT_NE(v.ToString().find("TextureCoordinate:"), std::string::npos);
}

TEST(VertexPositionTextureTest, ToStringDoubleBraces)
{
    VertexPositionTexture v(Vector3(0, 0, 0), Vector2(0, 0));
    const std::string s = v.ToString();
    EXPECT_EQ(s.substr(0, 2), "{{");
    EXPECT_EQ(s.substr(s.size() - 2), "}}");
}
