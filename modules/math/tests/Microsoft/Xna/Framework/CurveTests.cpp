// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

using namespace Microsoft::Xna::Framework;

static constexpr float kEps = 1e-5f;

// -----------------------------------------------------------------------
// Curve — IsConstant
// -----------------------------------------------------------------------

TEST(CurveTest, EmptyIsConstant)
{
    Curve c;
    EXPECT_TRUE(c.getIsConstantProperty());
}

TEST(CurveTest, OneKeyIsConstant)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 5.0f));
    EXPECT_TRUE(c.getIsConstantProperty());
}

TEST(CurveTest, TwoKeysNotConstant)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    EXPECT_FALSE(c.getIsConstantProperty());
}

// -----------------------------------------------------------------------
// Curve — Evaluate
// -----------------------------------------------------------------------

TEST(CurveTest, EvaluateEmptyReturnsZero)
{
    Curve c;
    EXPECT_FLOAT_EQ(c.Evaluate(5.0f), 0.0f);
}

TEST(CurveTest, EvaluateSingleKeyReturnsValue)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 7.0f));
    EXPECT_FLOAT_EQ(c.Evaluate(0.0f), 7.0f);
    EXPECT_FLOAT_EQ(c.Evaluate(100.0f), 7.0f);
    EXPECT_FLOAT_EQ(c.Evaluate(-100.0f), 7.0f);
}

TEST(CurveTest, EvaluateAtKeyPositionExact)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    EXPECT_NEAR(c.Evaluate(0.0f), 0.0f, kEps);
    EXPECT_NEAR(c.Evaluate(1.0f), 1.0f, kEps);
}

TEST(CurveTest, EvaluateMidpointWithFlatTangents)
{
    // With flat (zero) tangents the Hermite spline evaluates to 0.5 at midpoint
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f, 0.0f, 0.0f));
    float mid = c.Evaluate(0.5f);
    EXPECT_NEAR(mid, 0.5f, kEps);
}

TEST(CurveTest, EvaluateBeforeFirstKeyConstantLoop)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(1.0f, 3.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 5.0f));
    EXPECT_FLOAT_EQ(c.Evaluate(0.0f), 3.0f);
}

TEST(CurveTest, EvaluateAfterLastKeyConstantLoop)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 1.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 9.0f));
    EXPECT_FLOAT_EQ(c.Evaluate(100.0f), 9.0f);
}

// -----------------------------------------------------------------------
// Curve — Clone and loop types
// -----------------------------------------------------------------------

TEST(CurveTest, CloneProducesIndependentCopy)
{
    Curve c;
    c.setPreLoopProperty(CurveLoopType::Cycle);
    c.setPostLoopProperty(CurveLoopType::Oscillate);
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));

    Curve copy = c.Clone();
    EXPECT_EQ(copy.getPreLoopProperty(), CurveLoopType::Cycle);
    EXPECT_EQ(copy.getPostLoopProperty(), CurveLoopType::Oscillate);
    EXPECT_EQ(copy.getKeysProperty().getCountProperty(), 1);

    copy.getKeysProperty().Clear();
    EXPECT_EQ(c.getKeysProperty().getCountProperty(), 1);
}

TEST(CurveTest, LoopTypeGetterSetter)
{
    Curve c;
    c.setPreLoopProperty(CurveLoopType::Linear);
    c.setPostLoopProperty(CurveLoopType::Cycle);
    EXPECT_EQ(c.getPreLoopProperty(), CurveLoopType::Linear);
    EXPECT_EQ(c.getPostLoopProperty(), CurveLoopType::Cycle);
}

// -----------------------------------------------------------------------
// Curve — ComputeTangents
// -----------------------------------------------------------------------

TEST(CurveTest, ComputeTangentsFlat)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 0.0f));
    c.ComputeTangents(CurveTangent::Flat);
    for (int i = 0; i < c.getKeysProperty().getCountProperty(); ++i)
    {
        EXPECT_FLOAT_EQ(c.getKeysProperty()[i].getTangentInProperty(), 0.0f);
        EXPECT_FLOAT_EQ(c.getKeysProperty()[i].getTangentOutProperty(), 0.0f);
    }
}

TEST(CurveTest, ComputeTangentLinear)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 2.0f));
    c.getKeysProperty().Add(CurveKey(3.0f, 4.0f));
    c.ComputeTangents(CurveTangent::Linear);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 2.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 2.0f, kEps);
}

TEST(CurveTest, ComputeTangentSmoothMiddleKey)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 0.0f));
    c.ComputeTangents(CurveTangent::Smooth);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 0.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 0.0f, kEps);
}

TEST(CurveTest, ComputeTangentOutOfRangeThrows)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    EXPECT_THROW(c.ComputeTangent(5, CurveTangent::Flat), std::out_of_range);
}

TEST(CurveTest, ComputeTangentNegativeIndexThrows)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    EXPECT_THROW(c.ComputeTangent(-1, CurveTangent::Flat), std::out_of_range);
}

TEST(CurveTest, ComputeTangentSingleArgDelegates)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 2.0f));
    c.ComputeTangent(1, CurveTangent::Linear);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 1.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 1.0f, kEps);
}

TEST(CurveTest, ComputeTangentTwoArgSetsInOutIndependently)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 2.0f));
    c.getKeysProperty().Add(CurveKey(3.0f, 4.0f));
    c.ComputeTangent(1, CurveTangent::Flat, CurveTangent::Linear);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 0.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 2.0f, kEps);
}

TEST(CurveTest, ComputeTangentsTwoArgOverload)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 3.0f));
    c.ComputeTangents(CurveTangent::Flat, CurveTangent::Linear);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 0.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 2.0f, kEps);
}

// -----------------------------------------------------------------------
// Curve — loop types beyond Constant
// -----------------------------------------------------------------------

TEST(CurveTest, EvaluatePreLoopLinear)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(1.0f, 3.0f, 2.0f, 2.0f));
    c.getKeysProperty().Add(CurveKey(2.0f, 5.0f));
    c.setPreLoopProperty(CurveLoopType::Linear);
    // first.Value - first.TangentIn * (first.Position - position) = 3 - 2*(1-0) = 1
    EXPECT_NEAR(c.Evaluate(0.0f), 1.0f, kEps);
}

TEST(CurveTest, EvaluatePostLoopLinear)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 2.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f));
    c.setPostLoopProperty(CurveLoopType::Linear);
    // last.Value + first.TangentOut * (position - last.Position) = 1 + 2*(2-1) = 3
    EXPECT_NEAR(c.Evaluate(2.0f), 3.0f, kEps);
}

TEST(CurveTest, EvaluatePreLoopCycleRepeats)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f, 0.0f, 0.0f));
    c.setPreLoopProperty(CurveLoopType::Cycle);
    EXPECT_NEAR(c.Evaluate(-0.5f), c.Evaluate(0.5f), kEps);
}

TEST(CurveTest, EvaluatePostLoopCycleRepeats)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f, 0.0f, 0.0f));
    c.setPostLoopProperty(CurveLoopType::Cycle);
    EXPECT_NEAR(c.Evaluate(1.5f), c.Evaluate(0.5f), kEps);
}

TEST(CurveTest, EvaluatePostLoopCycleOffsetShifts)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 2.0f, 0.0f, 0.0f));
    c.setPostLoopProperty(CurveLoopType::CycleOffset);
    // cycle=1: GetCurvePosition(0.5) + 1*(2-0)
    EXPECT_NEAR(c.Evaluate(1.5f), c.Evaluate(0.5f) + 2.0f, kEps);
}

TEST(CurveTest, EvaluatePostLoopOscillateReversesOnOddCycle)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f, 0.0f, 0.0f));
    c.setPostLoopProperty(CurveLoopType::Oscillate);
    // position=1.25, cycle=1 (odd) → mirrors to Evaluate(0.75)
    EXPECT_NEAR(c.Evaluate(1.25f), c.Evaluate(0.75f), kEps);
}

TEST(CurveTest, EvaluatePreLoopCycleOffsetShifts)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 2.0f, 0.0f, 0.0f));
    c.setPreLoopProperty(CurveLoopType::CycleOffset);
    // position=-0.5, cycle=-1: GetCurvePosition(0.5) + (-1)*(2-0)
    EXPECT_NEAR(c.Evaluate(-0.5f), c.Evaluate(0.5f) - 2.0f, kEps);
}

TEST(CurveTest, EvaluatePreLoopOscillateReversesOnOddCycle)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 1.0f, 0.0f, 0.0f));
    c.setPreLoopProperty(CurveLoopType::Oscillate);
    // position=-0.25, cycle=-1 (odd) → virtualPos=0.25
    EXPECT_NEAR(c.Evaluate(-0.25f), c.Evaluate(0.25f), kEps);
}

// -----------------------------------------------------------------------
// Curve — Step continuity
// -----------------------------------------------------------------------

TEST(CurveTest, StepContinuityReturnsCurrentSegmentValue)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f, 0.0f, 0.0f, CurveContinuity::Step));
    c.getKeysProperty().Add(CurveKey(1.0f, 5.0f, 0.0f, 0.0f, CurveContinuity::Step));
    c.getKeysProperty().Add(CurveKey(2.0f, 9.0f));
    EXPECT_NEAR(c.Evaluate(0.5f), 0.0f, kEps);
}
