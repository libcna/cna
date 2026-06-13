// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// --- Default constructor ---

TEST(VertexElementTest, DefaultOffsetZero)
{
    VertexElement ve;
    EXPECT_EQ(ve.Offset, 0);
}

TEST(VertexElementTest, DefaultFormatSingle)
{
    VertexElement ve;
    EXPECT_EQ(ve.VertexElementFormatValue, VertexElementFormat::Single);
}

TEST(VertexElementTest, DefaultUsagePosition)
{
    VertexElement ve;
    EXPECT_EQ(ve.VertexElementUsageValue, VertexElementUsage::Position);
}

TEST(VertexElementTest, DefaultUsageIndexZero)
{
    VertexElement ve;
    EXPECT_EQ(ve.UsageIndex, 0);
}

// --- Parameterized constructor ---

TEST(VertexElementTest, CtorOffset)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.Offset, 12);
}

TEST(VertexElementTest, CtorFormat)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.VertexElementFormatValue, VertexElementFormat::Vector3);
}

TEST(VertexElementTest, CtorUsage)
{
    VertexElement ve(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0);
    EXPECT_EQ(ve.VertexElementUsageValue, VertexElementUsage::Normal);
}

TEST(VertexElementTest, CtorUsageIndex)
{
    VertexElement ve(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    EXPECT_EQ(ve.UsageIndex, 1);
}

TEST(VertexElementTest, CtorColorFormat)
{
    VertexElement ve(12, VertexElementFormat::Color, VertexElementUsage::Color, 0);
    EXPECT_EQ(ve.VertexElementFormatValue, VertexElementFormat::Color);
    EXPECT_EQ(ve.VertexElementUsageValue, VertexElementUsage::Color);
}

TEST(VertexElementTest, CtorSecondTexcoord)
{
    VertexElement ve(20, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1);
    EXPECT_EQ(ve.Offset, 20);
    EXPECT_EQ(ve.UsageIndex, 1);
}
