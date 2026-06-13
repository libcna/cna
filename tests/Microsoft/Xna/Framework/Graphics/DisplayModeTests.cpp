// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

// --- Constructors ---

TEST(DisplayModeTest, DefaultConstructorZeroWidthHeight)
{
    DisplayMode dm;
    EXPECT_EQ(dm.getWidthProperty(), 0);
    EXPECT_EQ(dm.getHeightProperty(), 0);
}

TEST(DisplayModeTest, DefaultConstructorColorFormat)
{
    DisplayMode dm;
    EXPECT_EQ(dm.getFormatProperty(), SurfaceFormat::Color);
}

TEST(DisplayModeTest, ParameterizedConstructorStoresValues)
{
    DisplayMode dm(1920, 1080, SurfaceFormat::Color);
    EXPECT_EQ(dm.getWidthProperty(), 1920);
    EXPECT_EQ(dm.getHeightProperty(), 1080);
    EXPECT_EQ(dm.getFormatProperty(), SurfaceFormat::Color);
}

TEST(DisplayModeTest, ParameterizedConstructorDifferentFormat)
{
    DisplayMode dm(1280, 720, SurfaceFormat::Bgr565);
    EXPECT_EQ(dm.getWidthProperty(), 1280);
    EXPECT_EQ(dm.getHeightProperty(), 720);
    EXPECT_EQ(dm.getFormatProperty(), SurfaceFormat::Bgr565);
}

// --- Width / Height ---

TEST(DisplayModeTest, WidthReturnsCorrectValue)
{
    DisplayMode dm(800, 600, SurfaceFormat::Color);
    EXPECT_EQ(dm.getWidthProperty(), 800);
}

TEST(DisplayModeTest, HeightReturnsCorrectValue)
{
    DisplayMode dm(800, 600, SurfaceFormat::Color);
    EXPECT_EQ(dm.getHeightProperty(), 600);
}

// --- AspectRatio ---

TEST(DisplayModeTest, AspectRatio16x9)
{
    DisplayMode dm(1920, 1080, SurfaceFormat::Color);
    EXPECT_NEAR(dm.getAspectRatioProperty(), 1920.0f / 1080.0f, 1e-5f);
}

TEST(DisplayModeTest, AspectRatio4x3)
{
    DisplayMode dm(800, 600, SurfaceFormat::Color);
    EXPECT_NEAR(dm.getAspectRatioProperty(), 800.0f / 600.0f, 1e-5f);
}

TEST(DisplayModeTest, AspectRatioZeroHeightReturnsZero)
{
    DisplayMode dm(1920, 0, SurfaceFormat::Color);
    EXPECT_FLOAT_EQ(dm.getAspectRatioProperty(), 0.0f);
}

TEST(DisplayModeTest, AspectRatioDefaultIsZero)
{
    DisplayMode dm;
    EXPECT_FLOAT_EQ(dm.getAspectRatioProperty(), 0.0f);
}

// --- Format ---

TEST(DisplayModeTest, FormatReturnsColorForDefault)
{
    DisplayMode dm;
    EXPECT_EQ(dm.getFormatProperty(), SurfaceFormat::Color);
}

TEST(DisplayModeTest, FormatReturnsBgr565WhenSet)
{
    DisplayMode dm(640, 480, SurfaceFormat::Bgr565);
    EXPECT_EQ(dm.getFormatProperty(), SurfaceFormat::Bgr565);
}

// --- Equality ---

TEST(DisplayModeTest, EqualModesCompareEqual)
{
    DisplayMode a(1920, 1080, SurfaceFormat::Color);
    DisplayMode b(1920, 1080, SurfaceFormat::Color);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(DisplayModeTest, DifferentWidthNotEqual)
{
    DisplayMode a(1920, 1080, SurfaceFormat::Color);
    DisplayMode b(1280, 1080, SurfaceFormat::Color);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(DisplayModeTest, DifferentHeightNotEqual)
{
    DisplayMode a(1920, 1080, SurfaceFormat::Color);
    DisplayMode b(1920,  720, SurfaceFormat::Color);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(DisplayModeTest, DifferentFormatNotEqual)
{
    DisplayMode a(800, 600, SurfaceFormat::Color);
    DisplayMode b(800, 600, SurfaceFormat::Bgr565);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}
