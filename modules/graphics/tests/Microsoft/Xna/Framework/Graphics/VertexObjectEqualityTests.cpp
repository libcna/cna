// SPDX-License-Identifier: MS-PL
//
// XNA's vertex value types override Equals(object). Each Microsoft implementation rejects null
// and any object whose runtime type differs, then delegates to the typed comparison
// (xna4-decomp/.../Microsoft.Xna.Framework.Graphics/Vertex*.cs). In CNA a boxed CLR object is a
// std::any, the convention settled in XNA-MISSING-001/002.

#include <gtest/gtest.h>

#include <any>
#include <string>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    template <typename T>
    void ExpectObjectEquality(const T& value, const T& equalToValue, const T& different)
    {
        ASSERT_TRUE(value.Equals(equalToValue));
        ASSERT_FALSE(value.Equals(different));

        EXPECT_TRUE(value.Equals(std::any(equalToValue)));
        EXPECT_FALSE(value.Equals(std::any(different)));
        EXPECT_FALSE(value.Equals(std::any(0)));
        EXPECT_FALSE(value.Equals(std::any(std::string("vertex"))));
        EXPECT_FALSE(value.Equals(std::any()));

        EXPECT_EQ(value.GetHashCode(), equalToValue.GetHashCode());
    }

    VertexPositionColor MakePositionColor(float z)
    {
        return VertexPositionColor(Vector3(1.0f, 2.0f, z), Color(10, 20, 30, 40));
    }

    VertexPositionColorTexture MakePositionColorTexture(float z)
    {
        return VertexPositionColorTexture(Vector3(1.0f, 2.0f, z), Color(10, 20, 30, 40),
                                          Vector2(0.5f, 0.25f));
    }

    VertexPositionNormalTexture MakePositionNormalTexture(float z)
    {
        return VertexPositionNormalTexture(Vector3(1.0f, 2.0f, z), Vector3(0.0f, 1.0f, 0.0f),
                                           Vector2(0.5f, 0.25f));
    }

    VertexPositionTexture MakePositionTexture(float z)
    {
        return VertexPositionTexture(Vector3(1.0f, 2.0f, z), Vector2(0.5f, 0.25f));
    }
}

TEST(VertexObjectEqualityTest, VertexElement)
{
    ExpectObjectEquality(
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Position, 0));
}

TEST(VertexObjectEqualityTest, VertexPositionColor)
{
    ExpectObjectEquality(MakePositionColor(3.0f), MakePositionColor(3.0f), MakePositionColor(4.0f));
}

TEST(VertexObjectEqualityTest, VertexPositionColorTexture)
{
    ExpectObjectEquality(MakePositionColorTexture(3.0f), MakePositionColorTexture(3.0f),
                         MakePositionColorTexture(4.0f));
}

TEST(VertexObjectEqualityTest, VertexPositionNormalTexture)
{
    ExpectObjectEquality(MakePositionNormalTexture(3.0f), MakePositionNormalTexture(3.0f),
                         MakePositionNormalTexture(4.0f));
}

TEST(VertexObjectEqualityTest, VertexPositionTexture)
{
    ExpectObjectEquality(MakePositionTexture(3.0f), MakePositionTexture(3.0f),
                         MakePositionTexture(4.0f));
}

TEST(VertexObjectEqualityTest, BoxedEqualityRejectsAnotherVertexType)
{
    // VertexPositionTexture and VertexPositionColorTexture share their position and texture
    // coordinate, so only the runtime type separates them through a boxed object.
    const VertexPositionTexture plain = MakePositionTexture(3.0f);
    const VertexPositionColorTexture coloured = MakePositionColorTexture(3.0f);
    EXPECT_FALSE(plain.Equals(std::any(coloured)));
    EXPECT_FALSE(coloured.Equals(std::any(plain)));
}
