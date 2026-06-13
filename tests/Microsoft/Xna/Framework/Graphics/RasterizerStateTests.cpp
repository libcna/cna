// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"

using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::FillMode;
using Microsoft::Xna::Framework::Graphics::RasterizerState;

// --- Default constructor ---

TEST(RasterizerStateTest, DefaultCullMode)
{
    RasterizerState rs;
    EXPECT_EQ(rs.getCullModeProperty(), CullMode::CullCounterClockwiseFace);
}

TEST(RasterizerStateTest, DefaultDepthBiasZero)
{
    RasterizerState rs;
    EXPECT_FLOAT_EQ(rs.getDepthBiasProperty(), 0.0f);
}

TEST(RasterizerStateTest, DefaultFillModeSolid)
{
    RasterizerState rs;
    EXPECT_EQ(rs.getFillModeProperty(), FillMode::Solid);
}

TEST(RasterizerStateTest, DefaultMultiSampleAntiAliasEnabled)
{
    RasterizerState rs;
    EXPECT_TRUE(rs.getMultiSampleAntiAliasProperty());
}

TEST(RasterizerStateTest, DefaultScissorTestDisabled)
{
    RasterizerState rs;
    EXPECT_FALSE(rs.getScissorTestEnableProperty());
}

TEST(RasterizerStateTest, DefaultSlopeScaleDepthBiasZero)
{
    RasterizerState rs;
    EXPECT_FLOAT_EQ(rs.getSlopeScaleDepthBiasProperty(), 0.0f);
}

// --- Predefined: CullCounterClockwise ---

TEST(RasterizerStateTest, CullCounterClockwiseCullMode)
{
    EXPECT_EQ(RasterizerState::CullCounterClockwise.getCullModeProperty(),
              CullMode::CullCounterClockwiseFace);
}

TEST(RasterizerStateTest, CullCounterClockwiseFillModeSolid)
{
    EXPECT_EQ(RasterizerState::CullCounterClockwise.getFillModeProperty(), FillMode::Solid);
}

TEST(RasterizerStateTest, CullCounterClockwiseDepthBiasZero)
{
    EXPECT_FLOAT_EQ(RasterizerState::CullCounterClockwise.getDepthBiasProperty(), 0.0f);
}

// --- Predefined: CullClockwise ---

TEST(RasterizerStateTest, CullClockwiseCullMode)
{
    EXPECT_EQ(RasterizerState::CullClockwise.getCullModeProperty(),
              CullMode::CullClockwiseFace);
}

TEST(RasterizerStateTest, CullClockwiseFillModeSolid)
{
    EXPECT_EQ(RasterizerState::CullClockwise.getFillModeProperty(), FillMode::Solid);
}

// --- Predefined: CullNone ---

TEST(RasterizerStateTest, CullNoneCullMode)
{
    EXPECT_EQ(RasterizerState::CullNone.getCullModeProperty(), CullMode::None);
}

TEST(RasterizerStateTest, CullNoneFillModeSolid)
{
    EXPECT_EQ(RasterizerState::CullNone.getFillModeProperty(), FillMode::Solid);
}

// --- Setters ---

TEST(RasterizerStateTest, SetCullMode)
{
    RasterizerState rs;
    rs.setCullModeProperty(CullMode::None);
    EXPECT_EQ(rs.getCullModeProperty(), CullMode::None);
}

TEST(RasterizerStateTest, SetFillMode)
{
    RasterizerState rs;
    rs.setFillModeProperty(FillMode::WireFrame);
    EXPECT_EQ(rs.getFillModeProperty(), FillMode::WireFrame);
}

TEST(RasterizerStateTest, SetDepthBias)
{
    RasterizerState rs;
    rs.setDepthBiasProperty(0.001f);
    EXPECT_FLOAT_EQ(rs.getDepthBiasProperty(), 0.001f);
}

TEST(RasterizerStateTest, SetSlopeScaleDepthBias)
{
    RasterizerState rs;
    rs.setSlopeScaleDepthBiasProperty(1.5f);
    EXPECT_FLOAT_EQ(rs.getSlopeScaleDepthBiasProperty(), 1.5f);
}

TEST(RasterizerStateTest, SetMultiSampleAntiAlias)
{
    RasterizerState rs;
    rs.setMultiSampleAntiAliasProperty(false);
    EXPECT_FALSE(rs.getMultiSampleAntiAliasProperty());
}

TEST(RasterizerStateTest, SetScissorTestEnable)
{
    RasterizerState rs;
    rs.setScissorTestEnableProperty(true);
    EXPECT_TRUE(rs.getScissorTestEnableProperty());
}
