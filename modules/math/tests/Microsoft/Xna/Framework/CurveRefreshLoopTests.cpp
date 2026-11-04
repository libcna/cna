// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Curve.hpp"

using namespace Microsoft::Xna::Framework;

TEST(CurveRefreshLoopTest, CycleMultipliesByRoundedReciprocalBeforeTruncating)
{
    // Refresh ComputeCacheValues IL_0057-0071, CalcCycle IL_0012-0035:
    // float(1/41)=0x1.8f9c18p-6; float(82*that)=1.9999998807907104,
    // so cycle=1, wrapped position=41 and the Step result is 10 (not 2).
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(0,2,0,0,CurveContinuity::Step));
    curve.getKeysProperty().Add(CurveKey(41,10,0,0,CurveContinuity::Step));
    curve.setPostLoopProperty(CurveLoopType::Cycle);
    EXPECT_EQ(curve.Evaluate(82),10);
}

TEST(CurveRefreshLoopTest, WrappedPositionsRetainRefreshRoundingOnBothSides)
{
    // Refresh Evaluate IL_00be-0134 / IL_0196-0201 computes
    // remainder=position-float(first+float(cycle*span)), then first+remainder
    // (or last-remainder on odd Oscillate cycles). Keys have span=2.
    // At -2^24 cycle=-8388609, remainder=0; at 16777220 cycle=8388610,
    // remainder=0. CycleOffset adds float(cycle*8) to the wrapped value 2.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(1,2));
    curve.getKeysProperty().Add(CurveKey(3,10));
    struct Sample { float position,offset,oscillate; };
    const Sample cases[]={{-16777216.0f,-67108872.0f,10}, {16777220.0f,67108880.0f,2}};
    for (const auto& sample : cases)
    {
        SCOPED_TRACE(sample.position);
        curve.setPreLoopProperty(CurveLoopType::Cycle);
        curve.setPostLoopProperty(CurveLoopType::Cycle);
        EXPECT_EQ(curve.Evaluate(sample.position),2);
        curve.setPreLoopProperty(CurveLoopType::CycleOffset);
        curve.setPostLoopProperty(CurveLoopType::CycleOffset);
        EXPECT_EQ(curve.Evaluate(sample.position),sample.offset);
        curve.setPreLoopProperty(CurveLoopType::Oscillate);
        curve.setPostLoopProperty(CurveLoopType::Oscillate);
        EXPECT_EQ(curve.Evaluate(sample.position),sample.oscillate);
    }
}

TEST(CurveRefreshLoopTest, MinimumSubnormalTimeRangeKeepsReciprocalZero)
{
    // Refresh ComputeCacheValues IL_0057-0076 leaves inverse=0 unless
    // TimeRange > Single.Epsilon (the minimum positive subnormal).
    // Thus cycle=0, the query stays beyond both keys, and Step returns the last value.
    // CycleOffset adds zero; an infinite reciprocal would instead add int.MinValue*8.
    const float d=std::numeric_limits<float>::denorm_min();
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(0,2,0,0,CurveContinuity::Step));
    curve.getKeysProperty().Add(CurveKey(d,10,0,0,CurveContinuity::Step));
    curve.setPostLoopProperty(CurveLoopType::CycleOffset);
    EXPECT_EQ(curve.Evaluate(2*d),10);
}

TEST(CurveRefreshLoopTest, ConstantAndLinearUseTheirOwnEndpointFarOutsideShiftedKeys)
{
    // Refresh Evaluate IL_006e-009c / IL_0146-0174: Constant selects the endpoint;
    // Linear uses first incoming=3 or last outgoing=5, with no span normalization.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(1,2,3,101));
    curve.getKeysProperty().Add(CurveKey(3,10,201,5));
    EXPECT_EQ(curve.Evaluate(-99),2);
    EXPECT_EQ(curve.Evaluate(103),10);
    curve.setPreLoopProperty(CurveLoopType::Linear);
    curve.setPostLoopProperty(CurveLoopType::Linear);
    EXPECT_EQ(curve.Evaluate(-99),-298); // 2-3*100.
    EXPECT_EQ(curve.Evaluate(103),510); // 10+5*100.
    EXPECT_EQ(curve.Evaluate(1),2);
    EXPECT_EQ(curve.Evaluate(3),10);
}
