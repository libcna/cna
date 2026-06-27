// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColorTexture;

// Default constructor is effectively deleted because Color has no default constructor.

// --- Parameterized constructor ---

TEST(VertexPositionColorTextureTest, CtorPosition)
{
    VertexPositionColorTexture v(Vector3(4.0f, 5.0f, 6.0f), Color(255, 0, 0, 255), Vector2(1.0f, 0.0f));
    EXPECT_FLOAT_EQ(v.Position.X, 4.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 5.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 6.0f);
}

TEST(VertexPositionColorTextureTest, CtorColor)
{
    Color col(128, 64, 32, 200);
    VertexPositionColorTexture v(Vector3(0, 0, 0), col, Vector2(0, 0));
    EXPECT_EQ(v.Color, col);
}

TEST(VertexPositionColorTextureTest, CtorTexCoord)
{
    VertexPositionColorTexture v(Vector3(0, 0, 0), Color(255, 255, 255, 255), Vector2(0.3f, 0.7f));
    EXPECT_FLOAT_EQ(v.TextureCoordinate.X, 0.3f);
    EXPECT_FLOAT_EQ(v.TextureCoordinate.Y, 0.7f);
}

// --- Equality operators ---

TEST(VertexPositionColorTextureTest, EqualityTrue)
{
    Color c(255, 0, 0, 255);
    VertexPositionColorTexture a(Vector3(1, 2, 3), c, Vector2(0.5f, 0.5f));
    VertexPositionColorTexture b(Vector3(1, 2, 3), c, Vector2(0.5f, 0.5f));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionColorTextureTest, EqualityFalse)
{
    Color c(255, 0, 0, 255);
    VertexPositionColorTexture a(Vector3(1, 2, 3), c, Vector2(0.5f, 0.5f));
    VertexPositionColorTexture b(Vector3(1, 2, 3), c, Vector2(0.5f, 0.6f));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Vertex declaration ---
// NOTE: XNA specifies stride=24 (Vector3=12 + Color=4 + Vector2=8), but sizeof=56 due to
// IVertexType and Color both having vtable pointers. Element offsets are hardcoded in the declaration.

TEST(VertexPositionColorTextureTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionColorTexture::getVertexDeclarationStatic().GetVertexElements().size(), 3u);
}

TEST(VertexPositionColorTextureTest, DeclarationPositionElement)
{
    const auto& elems = VertexPositionColorTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].getOffsetProperty(), 0);
    EXPECT_EQ(elems[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].getVertexElementUsageProperty(),  VertexElementUsage::Position);
}

TEST(VertexPositionColorTextureTest, DeclarationColorElement)
{
    const auto& elems = VertexPositionColorTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].getOffsetProperty(), 12);
    EXPECT_EQ(elems[1].getVertexElementFormatProperty(), VertexElementFormat::Color);
    EXPECT_EQ(elems[1].getVertexElementUsageProperty(),  VertexElementUsage::Color);
}

TEST(VertexPositionColorTextureTest, DeclarationTexCoordElement)
{
    const auto& elems = VertexPositionColorTexture::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[2].getOffsetProperty(), 16);
    EXPECT_EQ(elems[2].getVertexElementFormatProperty(), VertexElementFormat::Vector2);
    EXPECT_EQ(elems[2].getVertexElementUsageProperty(),  VertexElementUsage::TextureCoordinate);
}
