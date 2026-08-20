// SPDX-License-Identifier: MS-PL
//
// plans/plan_fna3d.md FNA3D-16: GTest coverage for the FNA3D renderer's presentation geometry.
//
// FNA3D presents through FNA3D_SwapBuffers(device, sourceRect, destinationRect, window), so CNA's
// presentation modes are expressed directly as a destination rectangle rather than through a
// scaling pass of CNA's own. That arithmetic -- and the window/logical coordinate mapping
// Mouse::SetPosition and the input bridge depend on -- is pure and device-free, so it is verified
// here rather than by squinting at a scaled window.
#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dPresentation.hpp"

namespace
{
    using CNA::Internal::Renderers::CnaPresentationMode;
    using CNA::Internal::Renderers::Fna3d::ComputePresentationLayout;
    using CNA::Internal::Renderers::Fna3d::LogicalToWindow;
    using CNA::Internal::Renderers::Fna3d::PresentationLayout;
    using CNA::Internal::Renderers::Fna3d::ResolveLogicalSize;
    using CNA::Internal::Renderers::Fna3d::WindowToLogical;
}

TEST(Fna3dPresentationTests, ResolveLogicalSizeKeepsThePreferredSizeForMostModes)
{
    for (const CnaPresentationMode mode : { CnaPresentationMode::Letterbox,
                                            CnaPresentationMode::Overscan,
                                            CnaPresentationMode::Stretch,
                                            CnaPresentationMode::NativeBackBuffer })
    {
        int width = 0;
        int height = 0;
        ResolveLogicalSize(mode, 320, 240, 1280, 720, width, height);
        EXPECT_EQ(width, 320);
        EXPECT_EQ(height, 240);
    }
}

TEST(Fna3dPresentationTests, FixedHeightDynamicWidthDerivesWidthFromTheWindowAspect)
{
    int width = 0;
    int height = 0;
    ResolveLogicalSize(CnaPresentationMode::FixedHeightDynamicWidth, 800, 480, 1600, 900, width,
                       height);
    EXPECT_EQ(height, 480);
    // 1600 * 480 / 900 = 853.33 -> 853
    EXPECT_EQ(width, 853);
}

TEST(Fna3dPresentationTests, ResolveLogicalSizeNeverProducesADegenerateSize)
{
    int width = 0;
    int height = 0;
    ResolveLogicalSize(CnaPresentationMode::Letterbox, 0, 0, 0, 0, width, height);
    EXPECT_EQ(width, 1);
    EXPECT_EQ(height, 1);

    ResolveLogicalSize(CnaPresentationMode::FixedHeightDynamicWidth, 800, 480, 0, 0, width,
                       height);
    EXPECT_EQ(width, 800);
    EXPECT_EQ(height, 480);
}

TEST(Fna3dPresentationTests, NativeBackBufferPresentsOneToOneAtTheOrigin)
{
    const PresentationLayout layout = ComputePresentationLayout(
        CnaPresentationMode::NativeBackBuffer, 320, 240, 1280, 720);
    EXPECT_EQ(layout.destinationX, 0);
    EXPECT_EQ(layout.destinationY, 0);
    EXPECT_EQ(layout.destinationWidth, 320);
    EXPECT_EQ(layout.destinationHeight, 240);
}

TEST(Fna3dPresentationTests, StretchFillsTheWholeWindow)
{
    const PresentationLayout layout =
        ComputePresentationLayout(CnaPresentationMode::Stretch, 320, 240, 1280, 720);
    EXPECT_EQ(layout.destinationX, 0);
    EXPECT_EQ(layout.destinationY, 0);
    EXPECT_EQ(layout.destinationWidth, 1280);
    EXPECT_EQ(layout.destinationHeight, 720);
}

TEST(Fna3dPresentationTests, LetterboxFitsAndCentres)
{
    // 4:3 content in a 16:9 window: scale = min(1280/320, 720/240) = 3, giving 960x720 centred
    // horizontally with 160-pixel bars.
    const PresentationLayout layout =
        ComputePresentationLayout(CnaPresentationMode::Letterbox, 320, 240, 1280, 720);
    EXPECT_EQ(layout.destinationWidth, 960);
    EXPECT_EQ(layout.destinationHeight, 720);
    EXPECT_EQ(layout.destinationX, 160);
    EXPECT_EQ(layout.destinationY, 0);
}

TEST(Fna3dPresentationTests, OverscanFillsAndCrops)
{
    // Same content and window: scale = max(4, 3) = 4, giving 1280x960 -- taller than the window,
    // so it is centred with the excess cropped off both top and bottom.
    const PresentationLayout layout =
        ComputePresentationLayout(CnaPresentationMode::Overscan, 320, 240, 1280, 720);
    EXPECT_EQ(layout.destinationWidth, 1280);
    EXPECT_EQ(layout.destinationHeight, 960);
    EXPECT_EQ(layout.destinationX, 0);
    EXPECT_EQ(layout.destinationY, -120);
}

TEST(Fna3dPresentationTests, FixedHeightDynamicWidthLandsExactlyOnTheWindow)
{
    // The whole point of this mode: once ResolveLogicalSize has derived the width from the window
    // aspect, the Letterbox fit leaves no bars at all.
    int width = 0;
    int height = 0;
    ResolveLogicalSize(CnaPresentationMode::FixedHeightDynamicWidth, 800, 480, 1600, 900, width,
                       height);
    const PresentationLayout layout = ComputePresentationLayout(
        CnaPresentationMode::FixedHeightDynamicWidth, width, height, 1600, 900);
    EXPECT_EQ(layout.destinationY, 0);
    EXPECT_EQ(layout.destinationHeight, 900);
    EXPECT_LE(layout.destinationX, 1);
    EXPECT_GE(layout.destinationWidth, 1598);
}

TEST(Fna3dPresentationTests, WindowToLogicalMapsTheDestinationRectangleOntoTheBackBuffer)
{
    const PresentationLayout layout =
        ComputePresentationLayout(CnaPresentationMode::Letterbox, 320, 240, 1280, 720);

    float logX = 0.0f;
    float logY = 0.0f;
    ASSERT_TRUE(WindowToLogical(layout, 160.0f, 0.0f, logX, logY));
    EXPECT_FLOAT_EQ(logX, 0.0f);
    EXPECT_FLOAT_EQ(logY, 0.0f);

    ASSERT_TRUE(WindowToLogical(layout, 1120.0f, 720.0f, logX, logY));
    EXPECT_FLOAT_EQ(logX, 320.0f);
    EXPECT_FLOAT_EQ(logY, 240.0f);

    // A point inside the letterbox bar maps to a negative logical X -- outside the game surface,
    // which is exactly what a caller needs to know rather than a clamped lie.
    ASSERT_TRUE(WindowToLogical(layout, 0.0f, 360.0f, logX, logY));
    EXPECT_LT(logX, 0.0f);
}

TEST(Fna3dPresentationTests, LogicalToWindowIsTheInverseOfWindowToLogical)
{
    for (const CnaPresentationMode mode : { CnaPresentationMode::Letterbox,
                                            CnaPresentationMode::Overscan,
                                            CnaPresentationMode::Stretch,
                                            CnaPresentationMode::NativeBackBuffer })
    {
        const PresentationLayout layout = ComputePresentationLayout(mode, 320, 240, 1280, 720);
        for (const float logicalX : { 0.0f, 17.5f, 319.0f })
        {
            for (const float logicalY : { 0.0f, 99.25f, 239.0f })
            {
                float windowX = 0.0f;
                float windowY = 0.0f;
                ASSERT_TRUE(LogicalToWindow(layout, logicalX, logicalY, windowX, windowY));

                float backX = 0.0f;
                float backY = 0.0f;
                ASSERT_TRUE(WindowToLogical(layout, windowX, windowY, backX, backY));
                EXPECT_NEAR(backX, logicalX, 0.01f);
                EXPECT_NEAR(backY, logicalY, 0.01f);
            }
        }
    }
}

TEST(Fna3dPresentationTests, DegenerateLayoutsAreRefusedRatherThanDividingByZero)
{
    PresentationLayout layout{};
    layout.logicalWidth = 320;
    layout.logicalHeight = 240;
    layout.destinationWidth = 0;
    layout.destinationHeight = 0;

    float x = 0.0f;
    float y = 0.0f;
    EXPECT_FALSE(WindowToLogical(layout, 10.0f, 10.0f, x, y));

    PresentationLayout noLogical{};
    EXPECT_FALSE(LogicalToWindow(noLogical, 10.0f, 10.0f, x, y));
}

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
