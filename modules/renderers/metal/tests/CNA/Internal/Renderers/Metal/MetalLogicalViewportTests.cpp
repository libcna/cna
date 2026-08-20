// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-34-style extraction: ComputeMetalLogicalViewport() is pure C++ arithmetic
// (plain ints/floats and CnaPresentationMode, a shared enum with zero Objective-C dependency) --
// only fetching the real physical window size needs a live SDL window, and that step stays in
// MetalRenderer.mm's own thin wrapper. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// plans/plan_metal.md METAL-153-159: this exact formula had a real, previously-shipping bug (SpriteBatch
// completely bypassed virtual-resolution/letterbox scaling, reading raw physical drawable pixels
// instead). These tests exercise every CnaPresentationMode with concrete, hand-computed expected
// numbers, to lock the fixed behavior in against a future regression.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalLogicalViewport.hpp"

using namespace CNA::Internal::Renderers::Metal;
using CNA::Internal::Renderers::CnaPresentationMode;

namespace
{
    constexpr float kEps = 1e-4f;
}

TEST(MetalLogicalViewport, InvalidPhysicalAxisIsClampedToZeroIndependently)
{
    // Each axis is clamped independently at the top of the function before the early-return check
    // even runs -- a zero/negative width does NOT force height to zero too (and vice versa); the
    // early return just skips the letterbox math, it doesn't re-zero whatever was already computed.
    auto vp = ComputeMetalLogicalViewport(0, 600, CnaPresentationMode::Letterbox, 800, 600);
    EXPECT_EQ(vp.width, 0.0f); EXPECT_EQ(vp.height, 600.0f);
    EXPECT_EQ(vp.logicalWidth, 0.0f); EXPECT_EQ(vp.logicalHeight, 600.0f);

    auto vp2 = ComputeMetalLogicalViewport(800, -1, CnaPresentationMode::Letterbox, 800, 600);
    EXPECT_EQ(vp2.width, 800.0f); EXPECT_EQ(vp2.height, 0.0f);
}

TEST(MetalLogicalViewport, NoVirtualResolutionRequestedDegradesToPhysicalSize)
{
    // virtualW/virtualH <= 0 means "none requested" for every mode, including the non-default ones.
    auto vp = ComputeMetalLogicalViewport(1000, 600, CnaPresentationMode::Letterbox, 0, 0);
    EXPECT_FLOAT_EQ(vp.width, 1000.0f); EXPECT_FLOAT_EQ(vp.height, 600.0f);
    EXPECT_FLOAT_EQ(vp.logicalWidth, 1000.0f); EXPECT_FLOAT_EQ(vp.logicalHeight, 600.0f);
    EXPECT_FLOAT_EQ(vp.x, 0.0f); EXPECT_FLOAT_EQ(vp.y, 0.0f);
}

TEST(MetalLogicalViewport, NativeBackBufferIgnoresVirtualResolutionEntirely)
{
    // Even with a real virtual resolution requested, NativeBackBuffer must report the raw physical
    // size unscaled -- the one mode that opts entirely out of virtual-resolution behavior.
    auto vp = ComputeMetalLogicalViewport(1234, 567, CnaPresentationMode::NativeBackBuffer, 800, 600);
    EXPECT_FLOAT_EQ(vp.width, 1234.0f); EXPECT_FLOAT_EQ(vp.height, 567.0f);
    EXPECT_FLOAT_EQ(vp.logicalWidth, 1234.0f); EXPECT_FLOAT_EQ(vp.logicalHeight, 567.0f);
    EXPECT_FLOAT_EQ(vp.x, 0.0f); EXPECT_FLOAT_EQ(vp.y, 0.0f);
}

TEST(MetalLogicalViewport, StretchFillsThePhysicalWindowIgnoringAspectRatio)
{
    // Stretch: the logical canvas is exactly the requested virtual resolution, but it's stretched
    // (not letterboxed) to cover the entire physical window -- no bars, aspect ratio not preserved.
    auto vp = ComputeMetalLogicalViewport(1000, 600, CnaPresentationMode::Stretch, 800, 600);
    EXPECT_FLOAT_EQ(vp.width, 1000.0f); EXPECT_FLOAT_EQ(vp.height, 600.0f);
    EXPECT_FLOAT_EQ(vp.logicalWidth, 800.0f); EXPECT_FLOAT_EQ(vp.logicalHeight, 600.0f);
    EXPECT_FLOAT_EQ(vp.x, 0.0f); EXPECT_FLOAT_EQ(vp.y, 0.0f);
}

TEST(MetalLogicalViewport, LetterboxCentersAndPillarboxesAWiderWindow)
{
    // 800x600 (4:3) virtual content in a 1000x600 (5:3, wider) physical window: Letterbox picks the
    // SMALLER of the two axis scale factors (sx=1000/800=1.25, sy=600/600=1.0 -> scale=1.0) so the
    // content fits without cropping, centering it with vertical bars on the sides (pillarboxing).
    auto vp = ComputeMetalLogicalViewport(1000, 600, CnaPresentationMode::Letterbox, 800, 600);
    EXPECT_FLOAT_EQ(vp.logicalWidth, 800.0f); EXPECT_FLOAT_EQ(vp.logicalHeight, 600.0f);
    EXPECT_NEAR(vp.width, 800.0f, kEps); EXPECT_NEAR(vp.height, 600.0f, kEps);
    EXPECT_NEAR(vp.x, 100.0f, kEps); EXPECT_NEAR(vp.y, 0.0f, kEps);
}

TEST(MetalLogicalViewport, OverscanCropsToFillAWiderWindow)
{
    // Same 800x600-in-1000x600 setup, but Overscan picks the LARGER scale factor (1.25) instead --
    // the content overflows the window on the vertical axis (a real, intentional negative y offset,
    // meaning the top/bottom get cropped by whatever viewport/scissor consumes this rect) rather
    // than leaving bars.
    auto vp = ComputeMetalLogicalViewport(1000, 600, CnaPresentationMode::Overscan, 800, 600);
    EXPECT_FLOAT_EQ(vp.logicalWidth, 800.0f); EXPECT_FLOAT_EQ(vp.logicalHeight, 600.0f);
    EXPECT_NEAR(vp.width, 1000.0f, kEps); EXPECT_NEAR(vp.height, 750.0f, kEps);
    EXPECT_NEAR(vp.x, 0.0f, kEps); EXPECT_NEAR(vp.y, -75.0f, kEps);
}

TEST(MetalLogicalViewport, FixedHeightDynamicWidthComputesWidthFromPhysicalAspectAndFillsTheWindow)
{
    // XNA/Windows-Phone convention: logical height is pinned at the requested virtual height, and
    // logical width is derived from the REAL physical aspect ratio (wider windows simply show more
    // horizontal content) -- the physical rect always fills the window exactly, no bars, unlike
    // Letterbox/Overscan.
    auto vp = ComputeMetalLogicalViewport(1000, 500, CnaPresentationMode::FixedHeightDynamicWidth, 480, 320);
    // logicalWidth = 320 * (1000/500) = 640, logicalHeight stays 320.
    EXPECT_NEAR(vp.logicalWidth, 640.0f, kEps); EXPECT_NEAR(vp.logicalHeight, 320.0f, kEps);
    EXPECT_FLOAT_EQ(vp.width, 1000.0f); EXPECT_FLOAT_EQ(vp.height, 500.0f);
    EXPECT_FLOAT_EQ(vp.x, 0.0f); EXPECT_FLOAT_EQ(vp.y, 0.0f);
}

TEST(MetalLogicalViewport, LetterboxWithMatchingAspectRatioProducesNoBars)
{
    // When the physical window's aspect ratio already matches the virtual resolution exactly,
    // Letterbox and Overscan should both degrade to a full-window, bar-free result (scale=1 on
    // both axes simultaneously -- min and max coincide).
    auto vp = ComputeMetalLogicalViewport(800, 600, CnaPresentationMode::Letterbox, 800, 600);
    EXPECT_NEAR(vp.width, 800.0f, kEps); EXPECT_NEAR(vp.height, 600.0f, kEps);
    EXPECT_NEAR(vp.x, 0.0f, kEps); EXPECT_NEAR(vp.y, 0.0f, kEps);
}
