// SPDX-License-Identifier: MS-PL

#include <cmath>
#include <string>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using Microsoft::Xna::Framework::Rectangle;
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

// --- TitleSafeArea / ToString ---

TEST(DisplayModeTest, TitleSafeAreaCoversFullMode)
{
    const DisplayMode dm(1920, 1080, SurfaceFormat::Color);
    EXPECT_EQ(dm.getTitleSafeAreaProperty(), Rectangle(0, 0, 1920, 1080));
}

TEST(DisplayModeTest, ToStringMatchesXnaFieldOrderAndSinglePrecision)
{
    const DisplayMode dm(1920, 1080, SurfaceFormat::Color);
    EXPECT_EQ(dm.ToString(),
              "{Width:1920 Height:1080 Format:Color AspectRatio:1.777778}");
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

// XNA's DisplayMode is a reference type that compares by value: Equals(object) agrees with the
// operators and GetHashCode is derived from the same three fields, so two distinct instances
// describing one mode hash alike.
TEST(DisplayModeTest, EqualsObjectComparesByValueNotIdentity)
{
    const DisplayMode a(1920, 1080, SurfaceFormat::Color);
    const DisplayMode b(1920, 1080, SurfaceFormat::Color);
    const DisplayMode c(1280, 1080, SurfaceFormat::Color);
    EXPECT_TRUE(a.Equals(&b));
    EXPECT_TRUE(a.Equals(&a));
    EXPECT_FALSE(a.Equals(&c));
    EXPECT_FALSE(a.Equals(nullptr));
}

TEST(DisplayModeTest, EqualModesProduceEqualHashCodes)
{
    const DisplayMode a(1920, 1080, SurfaceFormat::Color);
    const DisplayMode b(1920, 1080, SurfaceFormat::Color);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(DisplayModeTest, HashCodeCombinesWidthHeightAndFormat)
{
    const DisplayMode dm(1920, 1080, SurfaceFormat::Bgr565);
    EXPECT_EQ(dm.GetHashCode(), 1920 ^ 1080 ^ static_cast<int>(SurfaceFormat::Bgr565));
    EXPECT_NE(dm.GetHashCode(), DisplayMode(1920, 1080, SurfaceFormat::Color).GetHashCode());
}

TEST(DisplayModeTest, ZeroWidthWithNegativeHeightReturnsPositiveZero)
{
    // Refresh DisplayMode.get_AspectRatio IL_0008-0015 explicitly returns +0
    // when width is zero, even if a division would produce -0.
    const DisplayMode mode(0, -2, SurfaceFormat::Color);
    EXPECT_EQ(mode.getAspectRatioProperty(), 0.0f);
    EXPECT_FALSE(std::signbit(mode.getAspectRatioProperty()));
    EXPECT_EQ(mode.getTitleSafeAreaProperty(), Rectangle(0, 0, 0, -2));
}

TEST(DisplayModeTest, SignedDimensionsKeepSinglePrecisionRatioAndFullSafeArea)
{
    // Refresh get_AspectRatio IL_0016-0025; TitleSafeArea IL_0000-0013.
    const DisplayMode mode(-3, 2, SurfaceFormat::Bgr565);
    EXPECT_EQ(mode.getAspectRatioProperty(), -1.5f);
    EXPECT_EQ(mode.getTitleSafeAreaProperty(), Rectangle(0, 0, -3, 2));
    EXPECT_EQ(mode.ToString(), "{Width:-3 Height:2 Format:Bgr565 AspectRatio:-1.5}");
}

TEST(DisplayModeTest, ToStringSpellsEveryXnaSurfaceFormatAndUnknownOrdinal)
{
    // Refresh DisplayMode.ToString IL_002d-003a boxes the enum (named or numeric).
    const char* names[] = {"Color", "Bgr565", "Bgra5551", "Bgra4444", "Dxt1", "Dxt3", "Dxt5",
        "NormalizedByte2", "NormalizedByte4", "Rgba1010102", "Rg32", "Rgba64", "Alpha8",
        "Single", "Vector2", "Vector4", "HalfSingle", "HalfVector2", "HalfVector4", "HdrBlendable"};
    for (int i = 0; i < 20; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(DisplayMode(3, 2, static_cast<SurfaceFormat>(i)).ToString(),
                  std::string("{Width:3 Height:2 Format:") + names[i] + " AspectRatio:1.5}");
    }
    EXPECT_EQ(DisplayMode(3, 2, static_cast<SurfaceFormat>(-1)).ToString(),
              "{Width:3 Height:2 Format:-1 AspectRatio:1.5}");
}
