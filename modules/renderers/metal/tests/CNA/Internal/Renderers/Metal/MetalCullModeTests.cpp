// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-19/METAL-5: DescribeMetalCullMode() only reads a plain XNA CullMode ordinal
// and returns a plain C++ enum -- zero Objective-C dependency, genuinely unit-tested on this Linux
// machine. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalCullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"

using namespace CNA::Internal::Renderers::Metal;
using CM = Microsoft::Xna::Framework::Graphics::CullMode;

TEST(MetalCullMode, NoneMapsToNone)
{
    EXPECT_EQ(DescribeMetalCullMode(static_cast<int>(CM::None)), MetalCullModeKind::None);
}

TEST(MetalCullMode, CullClockwiseFaceMapsToFront)
{
    // plans/plan_metal.md METAL-5: explicit, not relied-on-by-accident.
    EXPECT_EQ(DescribeMetalCullMode(static_cast<int>(CM::CullClockwiseFace)), MetalCullModeKind::Front);
}

TEST(MetalCullMode, CullCounterClockwiseFaceMapsToBack)
{
    EXPECT_EQ(DescribeMetalCullMode(static_cast<int>(CM::CullCounterClockwiseFace)), MetalCullModeKind::Back);
}

TEST(MetalCullMode, UnknownOrdinalDefaultsToNone)
{
    EXPECT_EQ(DescribeMetalCullMode(999), MetalCullModeKind::None);
}
