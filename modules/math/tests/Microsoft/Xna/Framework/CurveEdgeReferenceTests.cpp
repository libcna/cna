// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    Curve TangentCurve()
    {
        Curve value;
        value.getKeysProperty().Add(CurveKey(0,1,11,12));
        value.getKeysProperty().Add(CurveKey(1,5,21,22));
        value.getKeysProperty().Add(CurveKey(3,2,31,32));
        return value;
    }
}

TEST(CurveEdgeReferenceTest, EveryPrePostLoopPairHasIndependentLiteralOutsideValues)
{
    Curve value;
    value.getKeysProperty().Add(CurveKey(0,0,1,0));
    value.getKeysProperty().Add(CurveKey(2,4,0,3));
    const float before[] = {0,2,-2,2,-1}, after[] = {4,2,6,2,7};
    for (int pre=0; pre<5; ++pre) for (int post=0; post<5; ++post)
    {
        value.setPreLoopProperty(static_cast<CurveLoopType>(pre));
        value.setPostLoopProperty(static_cast<CurveLoopType>(post));
        EXPECT_EQ(value.getPreLoopProperty(), static_cast<CurveLoopType>(pre));
        EXPECT_EQ(value.getPostLoopProperty(), static_cast<CurveLoopType>(post));
        EXPECT_EQ(value.Evaluate(-1), before[pre]);
        EXPECT_EQ(value.Evaluate(3), after[post]);
        EXPECT_EQ(value.Evaluate(0), 0);
        EXPECT_EQ(value.Evaluate(1), 2);
        EXPECT_EQ(value.Evaluate(2), 4);
    }
}

TEST(CurveEdgeReferenceTest, EmptyAndSingleCurvesIgnoreLoopTypeAndQueryPosition)
{
    Curve empty, single;
    single.getKeysProperty().Add(CurveKey(2,7,3,4));
    EXPECT_TRUE(empty.getIsConstantProperty()); EXPECT_TRUE(single.getIsConstantProperty());
    EXPECT_EQ(empty.getPreLoopProperty(), CurveLoopType::Constant);
    EXPECT_EQ(empty.getPostLoopProperty(), CurveLoopType::Constant);
    for (int mode=0; mode<5; ++mode)
    {
        empty.setPreLoopProperty(static_cast<CurveLoopType>(mode));
        empty.setPostLoopProperty(static_cast<CurveLoopType>(mode));
        single.setPreLoopProperty(static_cast<CurveLoopType>(mode));
        single.setPostLoopProperty(static_cast<CurveLoopType>(mode));
        for (const float point : {-10.0f, 0.0f, 2.0f, 10.0f, std::numeric_limits<float>::quiet_NaN()})
        {
            EXPECT_EQ(empty.Evaluate(point), 0);
            EXPECT_EQ(single.Evaluate(point), 7);
        }
    }
}

TEST(CurveEdgeReferenceTest, StepAndDegenerateSpansRetainTheEstablishedXnaDecision)
{
    Curve step;
    step.getKeysProperty().Add(CurveKey(2,1,0,0,CurveContinuity::Step));
    step.getKeysProperty().Add(CurveKey(4,9,0,0,CurveContinuity::Step));
    EXPECT_EQ(step.Evaluate(2),1); EXPECT_EQ(step.Evaluate(3),1); EXPECT_EQ(step.Evaluate(4),9);
    EXPECT_EQ(step.Evaluate(std::numeric_limits<float>::quiet_NaN()),9);
    Curve tiny;
    tiny.getKeysProperty().Add(CurveKey(0,7)); tiny.getKeysProperty().Add(CurveKey(1e-11f,9));
    EXPECT_EQ(tiny.Evaluate(5e-12f),7);
    EXPECT_EQ(tiny.Evaluate(1e-11f),7);
    Curve duplicate;
    duplicate.getKeysProperty().Add(CurveKey(0,3)); duplicate.getKeysProperty().Add(CurveKey(0,9));
    duplicate.getKeysProperty().setItemProperty(0,CurveKey(0,3));
    duplicate.getKeysProperty().setItemProperty(1,CurveKey(0,9));
    EXPECT_EQ(duplicate.Evaluate(0),3);
}

TEST(CurveEdgeReferenceTest, BulkTangentOverloadsHaveLiteralLinearAndMixedResults)
{
    auto linear=TangentCurve(); linear.ComputeTangents(CurveTangent::Linear);
    const float incoming[]={0,4,-3}, outgoing[]={4,-3,0};
    for (int i=0; i<3; ++i)
    {
        EXPECT_EQ(linear.getKeysProperty()[i].getTangentInProperty(),incoming[i]);
        EXPECT_EQ(linear.getKeysProperty()[i].getTangentOutProperty(),outgoing[i]);
    }
    auto mixed=TangentCurve(); mixed.ComputeTangents(CurveTangent::Flat,CurveTangent::Linear);
    for (int i=0; i<3; ++i)
    {
        EXPECT_EQ(mixed.getKeysProperty()[i].getTangentInProperty(),0);
        EXPECT_EQ(mixed.getKeysProperty()[i].getTangentOutProperty(),outgoing[i]);
    }
    auto smooth=TangentCurve(); smooth.ComputeTangents(CurveTangent::Smooth);
    EXPECT_EQ(smooth.getKeysProperty()[1].getTangentInProperty(),0.3333333432674408f);
    EXPECT_EQ(smooth.getKeysProperty()[1].getTangentOutProperty(),0.6666666865348816f);
}

TEST(CurveEdgeReferenceTest, SingleKeyTangentOverloadsChangeOnlyTheRequestedKey)
{
    auto value=TangentCurve(); value.ComputeTangent(1,CurveTangent::Linear);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentInProperty(),4);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentOutProperty(),-3);
    EXPECT_EQ(value.getKeysProperty()[0].getTangentInProperty(),11);
    EXPECT_EQ(value.getKeysProperty()[2].getTangentOutProperty(),32);
    value=TangentCurve(); value.ComputeTangent(1,CurveTangent::Flat,CurveTangent::Linear);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentInProperty(),0);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentOutProperty(),-3);
    EXPECT_EQ(value.getKeysProperty()[0].getTangentInProperty(),11);
    EXPECT_EQ(value.getKeysProperty()[2].getTangentOutProperty(),32);
}

TEST(CurveEdgeReferenceTest, TangentIndexRefusalsPreserveExistingKeyData)
{
    auto value=TangentCurve();
    EXPECT_THROW(value.ComputeTangent(-1,CurveTangent::Flat),System::ArgumentOutOfRangeException);
    EXPECT_THROW(value.ComputeTangent(3,CurveTangent::Linear,CurveTangent::Smooth),System::ArgumentOutOfRangeException);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentInProperty(),21);
    EXPECT_EQ(value.getKeysProperty()[1].getTangentOutProperty(),22);
    Curve empty;
    empty.ComputeTangents(CurveTangent::Smooth);
    empty.ComputeTangents(CurveTangent::Flat,CurveTangent::Linear);
    EXPECT_THROW(empty.ComputeTangent(0,CurveTangent::Flat),System::ArgumentOutOfRangeException);
    Curve single; single.getKeysProperty().Add(CurveKey(0,7,3,4));
    single.ComputeTangents(CurveTangent::Smooth);
    EXPECT_EQ(single.getKeysProperty()[0].getTangentInProperty(),0);
    EXPECT_EQ(single.getKeysProperty()[0].getTangentOutProperty(),0);
}

TEST(CurveEdgeReferenceTest, EqualPositionInsertionPreservesWholeKeysInArrivalOrder)
{
    Curve duplicate;
    duplicate.getKeysProperty().Add(CurveKey(0,0));
    duplicate.getKeysProperty().Add(CurveKey(0,10));
    duplicate.getKeysProperty().Add(CurveKey(1,20));
    for (int i=0; i<3; ++i)
        EXPECT_EQ(duplicate.getKeysProperty()[i].getValueProperty(), i*10);
    Curve equal;
    equal.getKeysProperty().Add(CurveKey(0,3,1,2));
    equal.getKeysProperty().Add(CurveKey(0,9,4,5));
    const float values[]={3,9}, incoming[]={1,4}, outgoing[]={2,5};
    for (int i=0; i<2; ++i)
    {
        EXPECT_EQ(equal.getKeysProperty()[i].getValueProperty(),values[i]);
        EXPECT_EQ(equal.getKeysProperty()[i].getTangentInProperty(),incoming[i]);
        EXPECT_EQ(equal.getKeysProperty()[i].getTangentOutProperty(),outgoing[i]);
    }
}

TEST(CurveEdgeReferenceTest, TinySpanSmoothOutgoingTangentIsTwoThroughEveryOverload)
{
    for (int mode=0; mode<3; ++mode) for (int form=0; form<4; ++form)
    {
        SCOPED_TRACE(::testing::Message() << "mode=" << mode << " form=" << form);
        Curve value;
        value.getKeysProperty().Add(CurveKey(0,7));
        value.getKeysProperty().Add(CurveKey(1e-11f,9));
        const auto incoming=static_cast<CurveTangent>(mode);
        switch (form)
        {
            case 0: value.ComputeTangents(incoming,CurveTangent::Smooth); break;
            case 1: value.ComputeTangent(0,incoming,CurveTangent::Smooth); break;
            case 2: value.ComputeTangents(CurveTangent::Smooth); break;
            case 3: value.ComputeTangent(0,CurveTangent::Smooth); break;
        }
        EXPECT_EQ(value.getKeysProperty()[0].getTangentOutProperty(),2.0f);
    }
}

TEST(CurveEdgeReferenceTest, ShiftedMultiCycleQueriesPreserveParityOffsetsAndExactNegativeCycles)
{
    // FNA Curve.Evaluate/GetNumberOfCycle; XNA Curve.CalcCycle IL_0020–0035:
    // negative cycle count is truncated after subtracting 1, including exact integers.
    // Keys (10,2),(14,10), zero tangents: u=1/4 gives 3.25, u=3/4 gives 8.75.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(10,2));
    curve.getKeysProperty().Add(CurveKey(14,10));
    struct Sample { float position,cycle,offset,oscillate; };
    const Sample cases[] = {
        {1,8.75f,-15.25f,3.25f}, {2,10,-14,2}, {5,8.75f,-7.25f,8.75f},
        {6,10,-6,10}, {7,3.25f,-4.75f,8.75f}, {9,8.75f,0.75f,3.25f},
        {15,3.25f,11.25f,8.75f}, {18,2,18,2}, {19,3.25f,19.25f,3.25f},
        {22,2,26,10},
    };
    for (const auto& sample : cases)
    {
        SCOPED_TRACE(sample.position);
        curve.setPreLoopProperty(CurveLoopType::Cycle);
        curve.setPostLoopProperty(CurveLoopType::Cycle);
        EXPECT_EQ(curve.Evaluate(sample.position),sample.cycle);
        curve.setPreLoopProperty(CurveLoopType::CycleOffset);
        curve.setPostLoopProperty(CurveLoopType::CycleOffset);
        EXPECT_EQ(curve.Evaluate(sample.position),sample.offset);
        curve.setPreLoopProperty(CurveLoopType::Oscillate);
        curve.setPostLoopProperty(CurveLoopType::Oscillate);
        EXPECT_EQ(curve.Evaluate(sample.position),sample.oscillate);
    }
}

TEST(CurveEdgeReferenceTest, ClonePreservesAllKeyFieldsAndMutationsAreIndependent)
{
    // FNA Curve.Clone and CurveKey.Clone: copy loop modes, key values/tangents/continuity.
    Curve original;
    original.setPreLoopProperty(CurveLoopType::CycleOffset);
    original.setPostLoopProperty(CurveLoopType::Linear);
    original.getKeysProperty().Add(CurveKey(2,3,4,5,CurveContinuity::Step));
    original.getKeysProperty().Add(CurveKey(6,7,8,9));
    auto clone=original.Clone();
    EXPECT_EQ(clone.getPreLoopProperty(),CurveLoopType::CycleOffset);
    EXPECT_EQ(clone.getPostLoopProperty(),CurveLoopType::Linear);
    ASSERT_EQ(clone.getKeysProperty().getCountProperty(),2);
    EXPECT_EQ(clone.getKeysProperty()[0],CurveKey(2,3,4,5,CurveContinuity::Step));
    EXPECT_EQ(clone.getKeysProperty()[1],CurveKey(6,7,8,9));
    clone.getKeysProperty()[0].setValueProperty(99);
    clone.getKeysProperty()[1].setTangentOutProperty(-99);
    EXPECT_EQ(original.getKeysProperty()[0],CurveKey(2,3,4,5,CurveContinuity::Step));
    EXPECT_EQ(original.getKeysProperty()[1],CurveKey(6,7,8,9));
}
