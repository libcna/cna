// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <vector>

#include "CNA/Internal/Graphics/PresentationRect.hpp"

using CNA::Internal::Graphics::MapLogicalRectToPresentation;
using CNA::Internal::Graphics::PresentationRect;
using CNA::Internal::Graphics::PresentedPixelSpan;

namespace
{
    // 1280x720 of logical content letterboxed into a 1600x900 window: the content keeps its 16:9
    // aspect, so it fills the window here. A 1600x1000 window is the interesting one -- see below.
    constexpr int kLogicalWidth = 1280;
    constexpr int kLogicalHeight = 720;
}

TEST(PresentationRectTest, IdentityWhenThePresentationRectangleMatchesTheLogicalSize)
{
    const PresentationRect logical{100, 50, 400, 300};
    const PresentationRect presentation{0, 0, kLogicalWidth, kLogicalHeight};

    const PresentationRect mapped =
        MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight, presentation);

    EXPECT_EQ(mapped.x, logical.x);
    EXPECT_EQ(mapped.y, logical.y);
    EXPECT_EQ(mapped.width, logical.width);
    EXPECT_EQ(mapped.height, logical.height);
}

TEST(PresentationRectTest, FullLogicalViewportBecomesTheWholePresentationRectangle)
{
    // 1280x720 letterboxed into a 1600x1000 window: 1600/1280 = 1.25, so the content is
    // 1600x900 and sits 50 px below the top.
    const PresentationRect presentation{0, 50, 1600, 900};
    const PresentationRect logical{0, 0, kLogicalWidth, kLogicalHeight};

    const PresentationRect mapped =
        MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight, presentation);

    EXPECT_EQ(mapped.x, presentation.x);
    EXPECT_EQ(mapped.y, presentation.y);
    EXPECT_EQ(mapped.width, presentation.width);
    EXPECT_EQ(mapped.height, presentation.height);
}

TEST(PresentationRectTest, SubViewportIsScaledAndOffsetIntoThePresentationRectangle)
{
    const PresentationRect presentation{0, 50, 1600, 900};
    // The right half of a split screen.
    const PresentationRect logical{640, 0, 640, 720};

    const PresentationRect mapped =
        MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight, presentation);

    EXPECT_EQ(mapped.x, 800);
    EXPECT_EQ(mapped.y, 50);
    EXPECT_EQ(mapped.width, 800);
    EXPECT_EQ(mapped.height, 900);
}

TEST(PresentationRectTest, AdjacentLogicalRectanglesStayAdjacentAfterScaling)
{
    // A scale that does not divide evenly: 1000/1280 = 0.78125. Mapping the origin and the size
    // independently would round each half's width down and leave a seam column unpainted.
    const PresentationRect presentation{0, 0, 1000, 563};
    const PresentationRect left{0, 0, 640, 720};
    const PresentationRect right{640, 0, 640, 720};

    const PresentationRect mappedLeft =
        MapLogicalRectToPresentation(left, kLogicalWidth, kLogicalHeight, presentation);
    const PresentationRect mappedRight =
        MapLogicalRectToPresentation(right, kLogicalWidth, kLogicalHeight, presentation);

    EXPECT_EQ(mappedLeft.x + mappedLeft.width, mappedRight.x);
    EXPECT_EQ(mappedRight.x + mappedRight.width, presentation.x + presentation.width);
    EXPECT_EQ(mappedLeft.x, presentation.x);
}

TEST(PresentationRectTest, OverscanPresentationRectangleWithANegativeOriginStillMaps)
{
    // Overscan crops: the logical content is drawn larger than the window and centred, so the
    // presentation rectangle starts outside the window.
    const PresentationRect presentation{-100, -50, 1480, 820};
    const PresentationRect logical{0, 0, kLogicalWidth, kLogicalHeight};

    const PresentationRect mapped =
        MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight, presentation);

    EXPECT_EQ(mapped.x, -100);
    EXPECT_EQ(mapped.y, -50);
    EXPECT_EQ(mapped.width, 1480);
    EXPECT_EQ(mapped.height, 820);
}

TEST(PresentationRectTest, DegenerateInputsAreReturnedUnchanged)
{
    const PresentationRect logical{10, 20, 30, 40};

    EXPECT_EQ(MapLogicalRectToPresentation(logical, 0, kLogicalHeight,
                                           PresentationRect{0, 0, 800, 600}).width,
              logical.width);
    EXPECT_EQ(MapLogicalRectToPresentation(logical, kLogicalWidth, 0,
                                           PresentationRect{0, 0, 800, 600}).height,
              logical.height);
    EXPECT_EQ(MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight,
                                           PresentationRect{0, 0, 0, 600}).x,
              logical.x);
    EXPECT_EQ(MapLogicalRectToPresentation(logical, kLogicalWidth, kLogicalHeight,
                                           PresentationRect{0, 0, 800, 0}).y,
              logical.y);
}

TEST(PresentationRectTest, PresentedPixelSpanIsTheIdentityWhenTheAxisIsPresentedOneToOne)
{
    EXPECT_EQ(PresentedPixelSpan(5, 4, 320, 0, 320, 320), (std::vector<int>{5, 6, 7, 8}));
    // Degenerate sizes leave the span alone rather than dividing by zero.
    EXPECT_EQ(PresentedPixelSpan(5, 2, 0, 0, 320, 320), (std::vector<int>{5, 6}));
    EXPECT_EQ(PresentedPixelSpan(5, 2, 320, 10, 0, 320), (std::vector<int>{5, 6}));
}

TEST(PresentationRectTest, PresentedPixelSpanReadsEachLogicalPixelWhereItsCentreIsPresented)
{
    // A 320-wide back buffer letterboxed into a 640x360 window: scale 1.5, an 80-pixel bar.
    // Logical column 0's centre (0.5) lands on drawable 80 + 0.75, column 1's (1.5) on 80 + 2.25.
    EXPECT_EQ(PresentedPixelSpan(0, 3, 320, 80, 480, 640), (std::vector<int>{80, 82, 83}));
    EXPECT_EQ(PresentedPixelSpan(319, 1, 320, 80, 480, 640), (std::vector<int>{559}));
    // A display scale of 2 reads every other drawable pixel, the second of each pair.
    EXPECT_EQ(PresentedPixelSpan(0, 3, 100, 0, 200, 200), (std::vector<int>{1, 3, 5}));
}

TEST(PresentationRectTest, PresentedPixelSpanClampsAnOverscanCropIntoTheDrawable)
{
    // 100 logical rows drawn 200 high and centred in a 100-high drawable: rows 0..24 and 75..99
    // were cropped away, and read the nearest presented edge instead of leaving the surface.
    const std::vector<int> rows = PresentedPixelSpan(0, 100, 100, -50, 200, 100);
    EXPECT_EQ(rows.front(), 0);
    EXPECT_EQ(rows[25], 1);
    EXPECT_EQ(rows[50], 51);
    EXPECT_EQ(rows.back(), 99);
    // Without a known drawable only the far side stays unclamped.
    EXPECT_EQ(PresentedPixelSpan(0, 1, 100, -50, 200, 0), (std::vector<int>{0}));
}
