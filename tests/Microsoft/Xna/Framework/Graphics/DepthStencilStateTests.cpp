// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::StencilOperation;

// --- Default constructor ---

TEST(DepthStencilStateTest, DefaultDepthBufferEnabled)
{
    DepthStencilState ds;
    EXPECT_TRUE(ds.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, DefaultDepthBufferWriteEnabled)
{
    DepthStencilState ds;
    EXPECT_TRUE(ds.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, DefaultDepthBufferFunctionLessEqual)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

TEST(DepthStencilStateTest, DefaultStencilDisabled)
{
    DepthStencilState ds;
    EXPECT_FALSE(ds.getStencilEnableProperty());
}

TEST(DepthStencilStateTest, DefaultStencilFunctionAlways)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilFunctionProperty(), CompareFunction::Always);
}

TEST(DepthStencilStateTest, DefaultStencilMask)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilMaskProperty(), 0x7FFFFFFF);
}

TEST(DepthStencilStateTest, DefaultStencilWriteMask)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilWriteMaskProperty(), 0x7FFFFFFF);
}

TEST(DepthStencilStateTest, DefaultReferenceStencilZero)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getReferenceStencilProperty(), 0);
}

TEST(DepthStencilStateTest, DefaultStencilOperationsKeep)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getStencilFailProperty(),            StencilOperation::Keep);
    EXPECT_EQ(ds.getStencilDepthBufferFailProperty(), StencilOperation::Keep);
    EXPECT_EQ(ds.getStencilPassProperty(),            StencilOperation::Keep);
}

TEST(DepthStencilStateTest, DefaultTwoSidedStencilModeDisabled)
{
    DepthStencilState ds;
    EXPECT_FALSE(ds.getTwoSidedStencilModeProperty());
}

TEST(DepthStencilStateTest, DefaultCCWStencilFunctionAlways)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getCounterClockwiseStencilFunctionProperty(), CompareFunction::Always);
}

TEST(DepthStencilStateTest, DefaultCCWStencilOperationsKeep)
{
    DepthStencilState ds;
    EXPECT_EQ(ds.getCounterClockwiseStencilFailProperty(),            StencilOperation::Keep);
    EXPECT_EQ(ds.getCounterClockwiseStencilDepthBufferFailProperty(), StencilOperation::Keep);
    EXPECT_EQ(ds.getCounterClockwiseStencilPassProperty(),            StencilOperation::Keep);
}

// --- Predefined: Default ---

TEST(DepthStencilStateTest, PresetDefaultDepthEnabled)
{
    EXPECT_TRUE(DepthStencilState::Default.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetDefaultDepthWriteEnabled)
{
    EXPECT_TRUE(DepthStencilState::Default.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, PresetDefaultDepthFunctionLessEqual)
{
    EXPECT_EQ(DepthStencilState::Default.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

// --- Predefined: DepthRead ---

TEST(DepthStencilStateTest, PresetDepthReadDepthEnabled)
{
    EXPECT_TRUE(DepthStencilState::DepthRead.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetDepthReadWriteDisabled)
{
    EXPECT_FALSE(DepthStencilState::DepthRead.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, PresetDepthReadDepthFunctionLessEqual)
{
    EXPECT_EQ(DepthStencilState::DepthRead.getDepthBufferFunctionProperty(), CompareFunction::LessEqual);
}

// --- Predefined: None ---

TEST(DepthStencilStateTest, PresetNoneDepthDisabled)
{
    EXPECT_FALSE(DepthStencilState::None.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, PresetNoneDepthWriteDisabled)
{
    EXPECT_FALSE(DepthStencilState::None.getDepthBufferWriteEnableProperty());
}

// --- Setters ---

TEST(DepthStencilStateTest, SetDepthBufferEnable)
{
    DepthStencilState ds;
    ds.setDepthBufferEnableProperty(false);
    EXPECT_FALSE(ds.getDepthBufferEnableProperty());
}

TEST(DepthStencilStateTest, SetDepthBufferWriteEnable)
{
    DepthStencilState ds;
    ds.setDepthBufferWriteEnableProperty(false);
    EXPECT_FALSE(ds.getDepthBufferWriteEnableProperty());
}

TEST(DepthStencilStateTest, SetDepthBufferFunction)
{
    DepthStencilState ds;
    ds.setDepthBufferFunctionProperty(CompareFunction::Less);
    EXPECT_EQ(ds.getDepthBufferFunctionProperty(), CompareFunction::Less);
}

TEST(DepthStencilStateTest, SetStencilEnable)
{
    DepthStencilState ds;
    ds.setStencilEnableProperty(true);
    EXPECT_TRUE(ds.getStencilEnableProperty());
}

TEST(DepthStencilStateTest, SetStencilFunction)
{
    DepthStencilState ds;
    ds.setStencilFunctionProperty(CompareFunction::Equal);
    EXPECT_EQ(ds.getStencilFunctionProperty(), CompareFunction::Equal);
}

TEST(DepthStencilStateTest, SetStencilPass)
{
    DepthStencilState ds;
    ds.setStencilPassProperty(StencilOperation::Replace);
    EXPECT_EQ(ds.getStencilPassProperty(), StencilOperation::Replace);
}

TEST(DepthStencilStateTest, SetReferenceStencil)
{
    DepthStencilState ds;
    ds.setReferenceStencilProperty(42);
    EXPECT_EQ(ds.getReferenceStencilProperty(), 42);
}

TEST(DepthStencilStateTest, SetTwoSidedStencilMode)
{
    DepthStencilState ds;
    ds.setTwoSidedStencilModeProperty(true);
    EXPECT_TRUE(ds.getTwoSidedStencilModeProperty());
}
