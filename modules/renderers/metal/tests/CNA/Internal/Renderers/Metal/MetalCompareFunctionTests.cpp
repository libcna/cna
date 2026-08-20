// SPDX-License-Identifier: MS-PL
//
// plans/plan_metal.md METAL-19: DescribeMetalCompareFunction() only reads a plain XNA CompareFunction
// ordinal and returns a plain C++ enum -- zero Objective-C dependency, genuinely unit-tested on
// this Linux machine. No #if defined(CNA_RENDERER_METAL) gate, deliberately.
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Metal/MetalCompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"

using namespace CNA::Internal::Renderers::Metal;
using CF = Microsoft::Xna::Framework::Graphics::CompareFunction;

TEST(MetalCompareFunction, AlwaysMapsToAlways)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::Always)), MetalCompareFunctionKind::Always);
}

TEST(MetalCompareFunction, NeverMapsToNever)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::Never)), MetalCompareFunctionKind::Never);
}

TEST(MetalCompareFunction, LessMapsToLess)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::Less)), MetalCompareFunctionKind::Less);
}

TEST(MetalCompareFunction, LessEqualMapsToLessEqual)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::LessEqual)), MetalCompareFunctionKind::LessEqual);
}

TEST(MetalCompareFunction, EqualMapsToEqual)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::Equal)), MetalCompareFunctionKind::Equal);
}

TEST(MetalCompareFunction, GreaterEqualMapsToGreaterEqual)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::GreaterEqual)), MetalCompareFunctionKind::GreaterEqual);
}

TEST(MetalCompareFunction, GreaterMapsToGreater)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::Greater)), MetalCompareFunctionKind::Greater);
}

TEST(MetalCompareFunction, NotEqualMapsToNotEqual)
{
    EXPECT_EQ(DescribeMetalCompareFunction(static_cast<int>(CF::NotEqual)), MetalCompareFunctionKind::NotEqual);
}

TEST(MetalCompareFunction, UnknownOrdinalDefaultsToAlways)
{
    // Matches the previous inline switch's own default (CompareFunction::Always = 0), preserved as
    // an explicit, tested contract rather than an implicit fallthrough.
    EXPECT_EQ(DescribeMetalCompareFunction(999), MetalCompareFunctionKind::Always);
}
