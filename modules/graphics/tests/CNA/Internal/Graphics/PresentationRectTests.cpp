// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/PresentationRect.hpp"

using CNA::Internal::Graphics::MapLogicalRectToPresentation;
using CNA::Internal::Graphics::PresentationRect;

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
