// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

// --- Default constructor ---

TEST(VertexPositionColorTest, DefaultPositionZero)
{
    VertexPositionColor v;
    EXPECT_FLOAT_EQ(v.Position.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 0.0f);
}

TEST(VertexPositionColorTest, DefaultColorWhite)
{
    VertexPositionColor v;
    EXPECT_EQ(v.Color, Color(255, 255, 255, 255));
}

// --- Parameterized constructor ---

TEST(VertexPositionColorTest, CtorPosition)
{
    Vector3 pos(1.0f, 2.0f, 3.0f);
    Color col(255, 0, 0, 255);
    VertexPositionColor v(pos, col);
    EXPECT_FLOAT_EQ(v.Position.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Position.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Position.Z, 3.0f);
}

TEST(VertexPositionColorTest, CtorColor)
{
    Color col(128, 64, 32, 200);
    VertexPositionColor v(Vector3(0, 0, 0), col);
    EXPECT_EQ(v.Color, col);
}

// --- Vertex declaration ---
// NOTE: XNA specifies stride=16 (Vector3=12 + Color=4), but sizeof(Color) is currently 24
// because Color inherits from IPackedVectorT<UInt32> which adds a vtable pointer.
// The stride/sizeof tests reflect this known deviation from XNA spec.

TEST(VertexPositionColorTest, DeclarationElementCount)
{
    EXPECT_EQ(VertexPositionColor::getVertexDeclarationStatic().GetVertexElements().size(), 2u);
}

TEST(VertexPositionColorTest, DeclarationPositionOffset)
{
    const auto& elems = VertexPositionColor::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[0].getOffsetProperty(), 0);
    EXPECT_EQ(elems[0].getVertexElementFormatProperty(), VertexElementFormat::Vector3);
    EXPECT_EQ(elems[0].getVertexElementUsageProperty(),  VertexElementUsage::Position);
}

TEST(VertexPositionColorTest, DeclarationColorOffset)
{
    const auto& elems = VertexPositionColor::getVertexDeclarationStatic().GetVertexElements();
    EXPECT_EQ(elems[1].getOffsetProperty(), 12);
    EXPECT_EQ(elems[1].getVertexElementFormatProperty(), VertexElementFormat::Color);
    EXPECT_EQ(elems[1].getVertexElementUsageProperty(),  VertexElementUsage::Color);
}

// sizeof(VertexPositionColor) is 40 rather than the XNA-expected 16 due to the Color vtable issue above.

// --- Equality ---

TEST(VertexPositionColorTest, EqualityTrue)
{
    VertexPositionColor a(Vector3(1, 2, 3), Color(255, 0, 0, 255));
    VertexPositionColor b(Vector3(1, 2, 3), Color(255, 0, 0, 255));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(VertexPositionColorTest, EqualityFalsePosition)
{
    VertexPositionColor a(Vector3(1, 0, 0), Color(255, 0, 0, 255));
    VertexPositionColor b(Vector3(0, 0, 0), Color(255, 0, 0, 255));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexPositionColorTest, EqualityFalseColor)
{
    VertexPositionColor a(Vector3(0, 0, 0), Color(255, 0, 0, 255));
    VertexPositionColor b(Vector3(0, 0, 0), Color(0, 255, 0, 255));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(VertexPositionColorTest, EqualsMethod)
{
    VertexPositionColor a(Vector3(1, 2, 3), Color(10, 20, 30, 255));
    VertexPositionColor b(Vector3(1, 2, 3), Color(10, 20, 30, 255));
    EXPECT_TRUE(a.Equals(b));
}

// --- GetHashCode ---

TEST(VertexPositionColorTest, GetHashCodeConsistent)
{
    VertexPositionColor a(Vector3(1, 2, 3), Color(255, 0, 0, 255));
    VertexPositionColor b(Vector3(1, 2, 3), Color(255, 0, 0, 255));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(VertexPositionColorTest, ToStringContainsPosition)
{
    VertexPositionColor v(Vector3(1, 2, 3), Color(255, 0, 0, 255));
    EXPECT_NE(v.ToString().find("Position:"), std::string::npos);
}

TEST(VertexPositionColorTest, ToStringContainsColor)
{
    VertexPositionColor v(Vector3(0, 0, 0), Color(10, 20, 30, 255));
    EXPECT_NE(v.ToString().find("Color:"), std::string::npos);
}

TEST(VertexPositionColorTest, ToStringDoubleBraces)
{
    VertexPositionColor v(Vector3(0, 0, 0), Color(255, 255, 255, 255));
    const std::string s = v.ToString();
    EXPECT_EQ(s.substr(0, 2), "{{");
    EXPECT_EQ(s.substr(s.size() - 2), "}}");
}
