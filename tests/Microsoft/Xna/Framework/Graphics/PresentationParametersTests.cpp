// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/DisplayOrientation.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using Microsoft::Xna::Framework::DisplayOrientation;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::PresentInterval;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// --- Default constructor ---

TEST(PresentationParametersTest, DefaultBackBufferFormat)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getBackBufferFormatProperty(), SurfaceFormat::Color);
}

TEST(PresentationParametersTest, DefaultBackBufferWidth)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getBackBufferWidthProperty(), 800);
}

TEST(PresentationParametersTest, DefaultBackBufferHeight)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getBackBufferHeightProperty(), 480);
}

TEST(PresentationParametersTest, DefaultDepthStencilFormat)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::None);
}

TEST(PresentationParametersTest, DefaultIsFullScreenFalse)
{
    PresentationParameters pp;
    EXPECT_FALSE(pp.getIsFullScreenProperty());
}

TEST(PresentationParametersTest, DefaultMultiSampleCountZero)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 0);
}

TEST(PresentationParametersTest, DefaultPresentationInterval)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getPresentationIntervalProperty(), PresentInterval::Default);
}

TEST(PresentationParametersTest, DefaultDisplayOrientation)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getDisplayOrientationProperty(), DisplayOrientation::Default);
}

TEST(PresentationParametersTest, DefaultRenderTargetUsage)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getRenderTargetUsageProperty(), RenderTargetUsage::DiscardContents);
}

TEST(PresentationParametersTest, DefaultDeviceWindowHandleZero)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getDeviceWindowHandleProperty(), 0u);
}

// --- Bounds ---

TEST(PresentationParametersTest, DefaultBoundsMatchesDefaultDimensions)
{
    PresentationParameters pp;
    Rectangle b = pp.getBoundsProperty();
    EXPECT_EQ(b.X, 0);
    EXPECT_EQ(b.Y, 0);
    EXPECT_EQ(b.Width,  pp.getBackBufferWidthProperty());
    EXPECT_EQ(b.Height, pp.getBackBufferHeightProperty());
}

TEST(PresentationParametersTest, BoundsUpdatesWhenWidthChanges)
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(1920);
    EXPECT_EQ(pp.getBoundsProperty().Width, 1920);
}

TEST(PresentationParametersTest, BoundsUpdatesWhenHeightChanges)
{
    PresentationParameters pp;
    pp.setBackBufferHeightProperty(1080);
    EXPECT_EQ(pp.getBoundsProperty().Height, 1080);
}

TEST(PresentationParametersTest, BoundsOriginAlwaysZero)
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(800);
    pp.setBackBufferHeightProperty(600);
    Rectangle b = pp.getBoundsProperty();
    EXPECT_EQ(b.X, 0);
    EXPECT_EQ(b.Y, 0);
}

// --- Setters round-trip ---

TEST(PresentationParametersTest, SetBackBufferFormat)
{
    PresentationParameters pp;
    pp.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
    EXPECT_EQ(pp.getBackBufferFormatProperty(), SurfaceFormat::Bgr565);
}

TEST(PresentationParametersTest, SetBackBufferWidth)
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(1280);
    EXPECT_EQ(pp.getBackBufferWidthProperty(), 1280);
}

TEST(PresentationParametersTest, SetBackBufferHeight)
{
    PresentationParameters pp;
    pp.setBackBufferHeightProperty(720);
    EXPECT_EQ(pp.getBackBufferHeightProperty(), 720);
}

TEST(PresentationParametersTest, SetDepthStencilFormat)
{
    PresentationParameters pp;
    pp.setDepthStencilFormatProperty(DepthFormat::Depth24);
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::Depth24);
}

TEST(PresentationParametersTest, SetIsFullScreen)
{
    PresentationParameters pp;
    pp.setIsFullScreenProperty(true);
    EXPECT_TRUE(pp.getIsFullScreenProperty());
}

TEST(PresentationParametersTest, SetMultiSampleCount)
{
    PresentationParameters pp;
    pp.setMultiSampleCountProperty(4);
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 4);
}

TEST(PresentationParametersTest, SetPresentationInterval)
{
    PresentationParameters pp;
    pp.setPresentationIntervalProperty(PresentInterval::One);
    EXPECT_EQ(pp.getPresentationIntervalProperty(), PresentInterval::One);
}

TEST(PresentationParametersTest, SetRenderTargetUsage)
{
    PresentationParameters pp;
    pp.setRenderTargetUsageProperty(RenderTargetUsage::PreserveContents);
    EXPECT_EQ(pp.getRenderTargetUsageProperty(), RenderTargetUsage::PreserveContents);
}

TEST(PresentationParametersTest, SetDisplayOrientation)
{
    PresentationParameters pp;
    pp.setDisplayOrientationProperty(DisplayOrientation::LandscapeLeft);
    EXPECT_EQ(pp.getDisplayOrientationProperty(), DisplayOrientation::LandscapeLeft);
}

TEST(PresentationParametersTest, SetDeviceWindowHandle)
{
    PresentationParameters pp;
    pp.setDeviceWindowHandleProperty(0xDEADBEEFu);
    EXPECT_EQ(pp.getDeviceWindowHandleProperty(), 0xDEADBEEFu);
}

// --- Clone ---

TEST(PresentationParametersTest, CloneCopiesAllFields)
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(1920);
    pp.setBackBufferHeightProperty(1080);
    pp.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
    pp.setIsFullScreenProperty(true);
    pp.setDepthStencilFormatProperty(DepthFormat::Depth24);
    pp.setMultiSampleCountProperty(2);
    pp.setPresentationIntervalProperty(PresentInterval::Two);
    pp.setDisplayOrientationProperty(DisplayOrientation::Portrait);
    pp.setRenderTargetUsageProperty(RenderTargetUsage::PreserveContents);
    pp.setDeviceWindowHandleProperty(0xCAFEBABEu);

    PresentationParameters clone = pp.Clone();
    EXPECT_EQ(clone.getBackBufferWidthProperty(),  1920);
    EXPECT_EQ(clone.getBackBufferHeightProperty(), 1080);
    EXPECT_EQ(clone.getBackBufferFormatProperty(), SurfaceFormat::Bgr565);
    EXPECT_TRUE(clone.getIsFullScreenProperty());
    EXPECT_EQ(clone.getDepthStencilFormatProperty(), DepthFormat::Depth24);
    EXPECT_EQ(clone.getMultiSampleCountProperty(), 2);
    EXPECT_EQ(clone.getPresentationIntervalProperty(), PresentInterval::Two);
    EXPECT_EQ(clone.getDisplayOrientationProperty(), DisplayOrientation::Portrait);
    EXPECT_EQ(clone.getRenderTargetUsageProperty(), RenderTargetUsage::PreserveContents);
    EXPECT_EQ(clone.getDeviceWindowHandleProperty(), 0xCAFEBABEu);
}

TEST(PresentationParametersTest, CloneIsIndependent)
{
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(800);

    PresentationParameters clone = pp.Clone();
    clone.setBackBufferWidthProperty(1280);

    EXPECT_EQ(pp.getBackBufferWidthProperty(), 800);
    EXPECT_EQ(clone.getBackBufferWidthProperty(), 1280);
}
