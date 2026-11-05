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

// XNA 4.0's constructor sets only IsFullScreen = true; the back buffer is 0x0 (museum cx-capi).
TEST(PresentationParametersTest, DefaultBackBufferWidth)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getBackBufferWidthProperty(), 0);
}

TEST(PresentationParametersTest, DefaultBackBufferHeight)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getBackBufferHeightProperty(), 0);
}

TEST(PresentationParametersTest, DefaultDepthStencilFormat)
{
    PresentationParameters pp;
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::None);
}

TEST(PresentationParametersTest, DefaultIsFullScreenTrue)
{
    PresentationParameters pp;
    EXPECT_TRUE(pp.getIsFullScreenProperty());
}

TEST(PresentationParametersTest, CreateWindowedEXTSetsOnlyTheSizeAndWindowedMode)
{
    const PresentationParameters defaults;
    const PresentationParameters windowed = PresentationParameters::CreateWindowedEXT(640, 360);
    EXPECT_EQ(windowed.getBackBufferWidthProperty(), 640);
    EXPECT_EQ(windowed.getBackBufferHeightProperty(), 360);
    EXPECT_FALSE(windowed.getIsFullScreenProperty());
    EXPECT_EQ(windowed.getBackBufferFormatProperty(), defaults.getBackBufferFormatProperty());
    EXPECT_EQ(windowed.getDepthStencilFormatProperty(), defaults.getDepthStencilFormatProperty());
    EXPECT_EQ(windowed.getMultiSampleCountProperty(), defaults.getMultiSampleCountProperty());
    EXPECT_EQ(windowed.getPresentationIntervalProperty(), defaults.getPresentationIntervalProperty());
    EXPECT_EQ(windowed.getDisplayOrientationProperty(), defaults.getDisplayOrientationProperty());
    EXPECT_EQ(windowed.getRenderTargetUsageProperty(), defaults.getRenderTargetUsageProperty());
    EXPECT_EQ(windowed.getDeviceWindowHandleProperty(), defaults.getDeviceWindowHandleProperty());
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

TEST(PresentationParametersTest, SetDepthStencilFormatNone)
{
    PresentationParameters pp;
    pp.setDepthStencilFormatProperty(DepthFormat::None);
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::None);
}

TEST(PresentationParametersTest, SetDepthStencilFormatDepth16)
{
    PresentationParameters pp;
    pp.setDepthStencilFormatProperty(DepthFormat::Depth16);
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::Depth16);
}

TEST(PresentationParametersTest, SetDepthStencilFormatDepth24Stencil8)
{
    PresentationParameters pp;
    pp.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    EXPECT_EQ(pp.getDepthStencilFormatProperty(), DepthFormat::Depth24Stencil8);
}

TEST(PresentationParametersTest, SetIsFullScreen)
{
    PresentationParameters pp;
    // XNA set_IsFullScreen IL_0000..000d writes the supplied boolean.
    pp.setIsFullScreenProperty(false);
    EXPECT_FALSE(pp.getIsFullScreenProperty());
    pp.setIsFullScreenProperty(true);
    EXPECT_TRUE(pp.getIsFullScreenProperty());
}

TEST(PresentationParametersTest, SetMultiSampleCount)
{
    PresentationParameters pp;
    pp.setMultiSampleCountProperty(4);
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 4);
}

TEST(PresentationParametersTest, SetMultiSampleCountZero)
{
    PresentationParameters pp;
    pp.setMultiSampleCountProperty(4);
    pp.setMultiSampleCountProperty(0);
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 0);
}

TEST(PresentationParametersTest, SetMultiSampleCountOne)
{
    PresentationParameters pp;
    pp.setMultiSampleCountProperty(1);
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 1);
}

TEST(PresentationParametersTest, SetMultiSampleCountEight)
{
    PresentationParameters pp;
    pp.setMultiSampleCountProperty(8);
    EXPECT_EQ(pp.getMultiSampleCountProperty(), 8);
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

TEST(PresentationParametersTest, EveryXnaEnumValueAndSignedScalarSurvivesSetAndClone)
{
    // Refresh setters IL_0000-000c store without validation; Clone IL_0006-0011
    // copies Settings. Enum ordinals are the Refresh literal metadata.
    PresentationParameters parameters;
    parameters.setIsFullScreenProperty(false); // Nondefault, unlike CloneCopiesAllFields.
    parameters.setBackBufferWidthProperty(-7);
    parameters.setBackBufferHeightProperty(-11);
    parameters.setDeviceWindowHandleProperty(0x1234u);
    for (int i = 0; i < 20; ++i)
    {
        SCOPED_TRACE(i);
        parameters.setBackBufferFormatProperty(static_cast<SurfaceFormat>(i));
        EXPECT_EQ(static_cast<int>(parameters.getBackBufferFormatProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.Clone().getBackBufferFormatProperty()), i);
    }
    for (int i = 0; i < 4; ++i)
    {
        parameters.setDepthStencilFormatProperty(static_cast<DepthFormat>(i));
        parameters.setPresentationIntervalProperty(static_cast<PresentInterval>(i));
        EXPECT_EQ(static_cast<int>(parameters.getDepthStencilFormatProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.getPresentationIntervalProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.Clone().getDepthStencilFormatProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.Clone().getPresentationIntervalProperty()), i);
    }
    for (int i : {0, 1, 2, 4, 7})
    {
        parameters.setDisplayOrientationProperty(static_cast<DisplayOrientation>(i));
        EXPECT_EQ(static_cast<int>(parameters.getDisplayOrientationProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.Clone().getDisplayOrientationProperty()), i);
    }
    for (int i = 0; i < 3; ++i)
    {
        parameters.setRenderTargetUsageProperty(static_cast<RenderTargetUsage>(i));
        EXPECT_EQ(static_cast<int>(parameters.getRenderTargetUsageProperty()), i);
        EXPECT_EQ(static_cast<int>(parameters.Clone().getRenderTargetUsageProperty()), i);
    }
    for (int i : {-1, 0, 1, 3, 16, 17})
    {
        parameters.setMultiSampleCountProperty(i);
        EXPECT_EQ(parameters.getMultiSampleCountProperty(), i);
        EXPECT_EQ(parameters.Clone().getMultiSampleCountProperty(), i);
    }
    const auto clone = parameters.Clone();
    EXPECT_FALSE(clone.getIsFullScreenProperty());
    EXPECT_EQ(clone.getBoundsProperty(), Rectangle(0, 0, -7, -11));
    EXPECT_EQ(clone.getDeviceWindowHandleProperty(), 0x1234u);
}

TEST(PresentationParametersTest, ClonePreservesHeadlessExtensionAndItsIndependence)
{
    // CNA public Clone documentation promises all fields; HeadlessEXT defaults false.
    PresentationParameters parameters;
    EXPECT_FALSE(parameters.getHeadlessEXTProperty());
    parameters.setHeadlessEXTProperty(true);
    auto clone = parameters.Clone();
    EXPECT_TRUE(clone.getHeadlessEXTProperty());
    clone.setHeadlessEXTProperty(false);
    EXPECT_TRUE(parameters.getHeadlessEXTProperty());
    EXPECT_FALSE(clone.getHeadlessEXTProperty());
}
