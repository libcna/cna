// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md: DescribeMetalSamplerFilter() only reads a plain XNA TextureFilter ordinal and
// returns a plain C++ struct -- zero Objective-C dependency, genuinely unit-tested on this Linux
// machine. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
//
// This fixes a real bug in the previous metalMinFilter()/metalMagFilter()/metalMipFilter()
// implementation: three independently-maintained case-set memberships, transcribed wrong for
// filter values 3, 6, and 7 (their mag/min component didn't match what the filter's own name says).
// Every expected value below is derived two independent ways: (1) directly from the XNA
// TextureFilter enum's own self-documenting names (e.g. MinLinearMagPointMipPoint literally spells
// out min=Linear, mag=Point, mip=Point), and (2) cross-checked against
// EasyGLRenderer::ApplySamplerState()'s own already-shipping GL filter table (read directly
// from its source, not from memory) -- both derivations agree on every one of the 9 real filter
// values, including the 3 this test suite specifically locks in as regression guards.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalSamplerFilter.hpp"

using namespace CNA::Internal::Renderers::Metal;

TEST(MetalSamplerFilter, LinearIsAllLinear)
{
    EXPECT_EQ(DescribeMetalSamplerFilter(0), (MetalSamplerFilterPlan{false, false, false}));
}

TEST(MetalSamplerFilter, PointIsAllPoint)
{
    EXPECT_EQ(DescribeMetalSamplerFilter(1), (MetalSamplerFilterPlan{true, true, true}));
}

TEST(MetalSamplerFilter, AnisotropicIsAllLinearLikePlainLinear)
{
    // Anisotropic filtering itself is a separate maxAnisotropy setting layered on top of a
    // Linear/Linear/Linear base filter, matching EasyGLRenderer::ApplySamplerState()'s own
    // case 2 (minF=LinearMipmapLinear, magF=Linear).
    EXPECT_EQ(DescribeMetalSamplerFilter(2), (MetalSamplerFilterPlan{false, false, false}));
}

TEST(MetalSamplerFilter, LinearMipPointIsLinearMinMagPointMip)
{
    // Regression guard: the original buggy implementation put filter 3 in the mag-Nearest set,
    // producing (min=Linear, mag=Point, mip=Point) -- wrong, since the name says mag should stay
    // Linear (only Mip is Point).
    EXPECT_EQ(DescribeMetalSamplerFilter(3), (MetalSamplerFilterPlan{false, false, true}));
}

TEST(MetalSamplerFilter, PointMipLinearIsPointMinMagLinearMip)
{
    EXPECT_EQ(DescribeMetalSamplerFilter(4), (MetalSamplerFilterPlan{true, true, false}));
}

TEST(MetalSamplerFilter, MinLinearMagPointMipLinearMatchesItsOwnName)
{
    EXPECT_EQ(DescribeMetalSamplerFilter(5), (MetalSamplerFilterPlan{false, true, false}));
}

TEST(MetalSamplerFilter, MinLinearMagPointMipPointMatchesItsOwnName)
{
    // Regression guard: the original buggy implementation had this exactly backwards --
    // (min=Point, mag=Linear, mip=Point) -- min and mag were swapped relative to what the name
    // "MinLinearMagPoint..." actually specifies.
    EXPECT_EQ(DescribeMetalSamplerFilter(6), (MetalSamplerFilterPlan{false, true, true}));
}

TEST(MetalSamplerFilter, MinPointMagLinearMipLinearMatchesItsOwnName)
{
    // Regression guard: the original buggy implementation gave (min=Linear, mag=Linear, mip=Linear)
    // -- indistinguishable from plain Linear filtering, silently dropping the "MinPoint" part of
    // the name entirely.
    EXPECT_EQ(DescribeMetalSamplerFilter(7), (MetalSamplerFilterPlan{true, false, false}));
}

TEST(MetalSamplerFilter, MinPointMagLinearMipPointMatchesItsOwnName)
{
    EXPECT_EQ(DescribeMetalSamplerFilter(8), (MetalSamplerFilterPlan{true, false, true}));
}

TEST(MetalSamplerFilter, EveryMinLinearMagPointOrMinPointMagLinearNameIsInternallyConsistent)
{
    // A structural check across all 4 "Min*Mag*" filters (5/6/7/8): whichever component the name
    // says is Point must actually be true, and the other must be false -- catches exactly the
    // class of min/mag-swap bug filter 6 had.
    EXPECT_TRUE(DescribeMetalSamplerFilter(5).magIsPoint);
    EXPECT_FALSE(DescribeMetalSamplerFilter(5).minIsPoint);
    EXPECT_TRUE(DescribeMetalSamplerFilter(6).magIsPoint);
    EXPECT_FALSE(DescribeMetalSamplerFilter(6).minIsPoint);
    EXPECT_TRUE(DescribeMetalSamplerFilter(7).minIsPoint);
    EXPECT_FALSE(DescribeMetalSamplerFilter(7).magIsPoint);
    EXPECT_TRUE(DescribeMetalSamplerFilter(8).minIsPoint);
    EXPECT_FALSE(DescribeMetalSamplerFilter(8).magIsPoint);
}
