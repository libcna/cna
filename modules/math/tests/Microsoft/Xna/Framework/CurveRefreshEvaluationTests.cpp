// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Curve.hpp"

using namespace Microsoft::Xna::Framework;

TEST(CurveRefreshEvaluationTest, HermiteAddsValuesBeforeTangentsInSinglePrecision)
{
    // Refresh Hermite IL_0045-0083, t=.5: value terms are +2^24 and -2^24,
    // then tangentOut*.125=1 and tangentIn*(-.125)=0, yielding exactly 1.
    // Adding 1 to +2^24 before cancelling loses it to ties-to-even rounding.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(2,33554432.0f,0,8));
    curve.getKeysProperty().Add(CurveKey(6,-33554432.0f,0,0));
    EXPECT_EQ(curve.Evaluate(4),1.0f);
    EXPECT_EQ(curve.Evaluate(2),33554432.0f);
    EXPECT_EQ(curve.Evaluate(6),-33554432.0f);
}

TEST(CurveRefreshEvaluationTest, HermiteUsesOutgoingAndIncomingTangentsWithTheirSigns)
{
    // Refresh Hermite IL_001f-0083. At t=1/4 the four bases are
    // 27/32, 5/32, 9/64, -3/64; at t=3/4: 5/32,27/32,3/64,-9/64.
    // Keys (2,4,in=99,out=8),(6,12,in=-16,out=-99) therefore give 7.125,13.375.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(2,4,99,8));
    curve.getKeysProperty().Add(CurveKey(6,12,-16,-99));
    EXPECT_EQ(curve.Evaluate(3),7.125f);
    EXPECT_EQ(curve.Evaluate(5),13.375f);
}

TEST(CurveRefreshEvaluationTest, StepUsesLeftContinuityAtInteriorBoundaries)
{
    // Refresh FindSegment IL_0022-0061 chooses the segment ending at the exact key;
    // Hermite IL_0000-001e jumps only at normalized t>=1, ignoring all tangents.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(-2,7,101,102,CurveContinuity::Step));
    curve.getKeysProperty().Add(CurveKey(2,9,201,202,CurveContinuity::Step));
    curve.getKeysProperty().Add(CurveKey(10,13,301,302));
    EXPECT_EQ(curve.Evaluate(-2),7);
    // At the immediately preceding float, (position+2)/4 rounds to 1.0f.
    EXPECT_EQ(curve.Evaluate(2.0f-0x1p-21f),7);
    EXPECT_EQ(curve.Evaluate(std::nextafter(2.0f,-2.0f)),9);
    EXPECT_EQ(curve.Evaluate(2),9);
    EXPECT_EQ(curve.Evaluate(std::nextafter(2.0f,10.0f)),9);
    EXPECT_EQ(curve.Evaluate(6),9);
    EXPECT_EQ(curve.Evaluate(std::nextafter(10.0f,2.0f)),9);
    EXPECT_EQ(curve.Evaluate(10),13);
}

TEST(CurveRefreshEvaluationTest, IsConstantDependsOnCountEvenWhenValuesAreIdentical)
{
    // Refresh get_IsConstant IL_0000-0011 tests Count <= 1, not equality of values.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(2,7));
    curve.getKeysProperty().Add(CurveKey(6,7));
    EXPECT_FALSE(curve.getIsConstantProperty());
    curve.getKeysProperty().RemoveAt(0);
    EXPECT_TRUE(curve.getIsConstantProperty());
    curve.getKeysProperty().Clear();
    EXPECT_TRUE(curve.getIsConstantProperty());
}

TEST(CurveRefreshEvaluationTest, ConstKeysGetterReturnsTheLiveCollection)
{
    // Refresh get_Keys IL_0000-0006 returns the actual keys field. The C++ const
    // overload retains this identity and exposes the same complete key values.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(2,3,4,5,CurveContinuity::Step));
    const Curve& readOnly=curve;
    const auto& keys=readOnly.getKeysProperty();
    EXPECT_EQ(&keys,&curve.getKeysProperty());
    ASSERT_EQ(keys.getCountProperty(),1);
    EXPECT_EQ(keys[0],CurveKey(2,3,4,5,CurveContinuity::Step));
    curve.getKeysProperty()[0].setValueProperty(30);
    EXPECT_EQ(keys[0],CurveKey(2,30,4,5,CurveContinuity::Step));
}
