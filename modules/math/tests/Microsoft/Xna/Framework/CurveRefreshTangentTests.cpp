// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Curve.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    Curve SeededCurve()
    {
        Curve curve;
        curve.getKeysProperty().Add(CurveKey(2, 5, 101, 102));
        curve.getKeysProperty().Add(CurveKey(4, 9, 201, 202));
        curve.getKeysProperty().Add(CurveKey(10, 21, 301, 302));
        return curve;
    }
}

TEST(CurveRefreshTangentTest, AllModesAndOverloadsReplaceSeededTangentsAtEveryKey)
{
    // Refresh ComputeTangent IL_002a-00a7 selects neighbors, IL_00ac-015e computes:
    // Flat [0,0,0]/[0,0,0]; Linear [0,4,12]/[4,12,0];
    // Smooth middle: (21-5)*(4-2)/(10-2)=4, outgoing=16*6/8=12.
    const float incoming[3][3] = {{0,0,0}, {0,4,12}, {0,4,12}};
    const float outgoing[3][3] = {{0,0,0}, {4,12,0}, {4,12,0}};
    for (int mode=0; mode<3; ++mode) for (int form=0; form<4; ++form)
    {
        SCOPED_TRACE(::testing::Message() << "mode=" << mode << " form=" << form);
        auto curve=SeededCurve();
        const auto type=static_cast<CurveTangent>(mode);
        if (form==0) curve.ComputeTangents(type);
        if (form==1) curve.ComputeTangents(type,type);
        if (form>=2) for (int i=0; i<3; ++i)
        {
            if (form==2) curve.ComputeTangent(i,type);
            else curve.ComputeTangent(i,type,type);
        }
        for (int i=0; i<3; ++i)
        {
            EXPECT_EQ(curve.getKeysProperty()[i].getTangentInProperty(),incoming[mode][i]);
            EXPECT_EQ(curve.getKeysProperty()[i].getTangentOutProperty(),outgoing[mode][i]);
        }
    }
}

TEST(CurveRefreshTangentTest, MixedModesUseDifferentPositionRatiosAndLeaveOtherKeysAlone)
{
    // Refresh ComputeTangent IL_00d2-00e2 and IL_012d-013e:
    // keys (0,1),(1,5),(3,8) give Smooth 7*1/3 and 7*2/3 at index 1.
    // Round AFTER multiplying, then divide. Exact binary32 literals; no ULP allowance.
    const float in[3]={0,4,0x1.2aaaaap+1f};
    const float out[3]={0,3,0x1.2aaaaap+2f};
    for (int ti=0; ti<3; ++ti) for (int to=0; to<3; ++to)
    {
        SCOPED_TRACE(::testing::Message() << "in=" << ti << " out=" << to);
        for (int bulk=0; bulk<2; ++bulk)
        {
            Curve curve;
            curve.getKeysProperty().Add(CurveKey(0,1,11,12));
            curve.getKeysProperty().Add(CurveKey(1,5,21,22));
            curve.getKeysProperty().Add(CurveKey(3,8,31,32));
            if (bulk) curve.ComputeTangents(static_cast<CurveTangent>(ti),static_cast<CurveTangent>(to));
            else curve.ComputeTangent(1,static_cast<CurveTangent>(ti),static_cast<CurveTangent>(to));
            EXPECT_EQ(curve.getKeysProperty()[1].getTangentInProperty(),in[ti]);
            EXPECT_EQ(curve.getKeysProperty()[1].getTangentOutProperty(),out[to]);
            if (!bulk)
            {
                EXPECT_EQ(curve.getKeysProperty()[0],CurveKey(0,1,11,12));
                EXPECT_EQ(curve.getKeysProperty()[2],CurveKey(3,8,31,32));
            }
        }
    }
}

TEST(CurveRefreshTangentTest, SmoothGuardsValueDeltaStrictlyBelowTwoToMinus23)
{
    // Refresh ComputeTangent IL_00b2-00c3, IL_010e-011f guard abs(value delta),
    // strictly below 2^-23. At the boundary, endpoint nonzero tangents equal delta.
    for (const float delta : {0x1p-24f, -0x1p-24f, 0x1p-23f, -0x1p-23f})
    {
        Curve curve;
        curve.getKeysProperty().Add(CurveKey(0,0,11,12));
        curve.getKeysProperty().Add(CurveKey(1,delta,21,22));
        curve.ComputeTangents(CurveTangent::Smooth);
        const float expected=std::fabs(delta)<0x1p-23f ? 0.0f : delta;
        EXPECT_EQ(curve.getKeysProperty()[0].getTangentInProperty(),0);
        EXPECT_EQ(curve.getKeysProperty()[0].getTangentOutProperty(),expected);
        EXPECT_EQ(curve.getKeysProperty()[1].getTangentInProperty(),expected);
        EXPECT_EQ(curve.getKeysProperty()[1].getTangentOutProperty(),0);
    }
}

TEST(CurveRefreshTangentTest, TinyPositionSpanDoesNotSuppressIncomingSmoothTangent)
{
    // Refresh ComputeTangent IL_00b2-00e2: delta=2 is not below 2^-23;
    // incoming at the last key = (2*1e-11f)/1e-11f = 2 exactly.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(0,7,11,12));
    curve.getKeysProperty().Add(CurveKey(1e-11f,9,21,22));
    curve.ComputeTangents(CurveTangent::Smooth);
    EXPECT_EQ(curve.getKeysProperty()[0].getTangentInProperty(),0);
    EXPECT_EQ(curve.getKeysProperty()[0].getTangentOutProperty(),2);
    EXPECT_EQ(curve.getKeysProperty()[1].getTangentInProperty(),2);
    EXPECT_EQ(curve.getKeysProperty()[1].getTangentOutProperty(),0);
}

TEST(CurveRefreshTangentTest, EqualPositionsWithUnequalValuesProduceNaNSmoothTangents)
{
    // Refresh ComputeTangent IL_00d2-00e2, IL_012d-013e: delta=8 passes the
    // value guard; each tangent is (8*abs(1-1))/(1-1) = 0/0, hence NaN.
    Curve curve;
    curve.getKeysProperty().Add(CurveKey(1,2));
    curve.getKeysProperty().Add(CurveKey(1,10));
    curve.ComputeTangents(CurveTangent::Smooth);
    for (int i=0; i<2; ++i)
    {
        EXPECT_TRUE(std::isnan(curve.getKeysProperty()[i].getTangentInProperty()));
        EXPECT_TRUE(std::isnan(curve.getKeysProperty()[i].getTangentOutProperty()));
    }
}

TEST(CurveRefreshTangentTest, EmptyAndSingleCurvesHandleEveryTangentMode)
{
    // Refresh ComputeTangents IL_0000-001f does zero iterations on an empty curve.
    // Single-key neighbors coincide in value, so Smooth's value guard returns 0.
    Curve empty;
    for (const auto type : {CurveTangent::Flat,CurveTangent::Linear,CurveTangent::Smooth})
    {
        empty.ComputeTangents(type);
        empty.ComputeTangents(type,type);
        EXPECT_EQ(empty.getKeysProperty().getCountProperty(),0);
        Curve single;
        single.getKeysProperty().Add(CurveKey(2,7,11,12));
        single.ComputeTangent(0,type);
        EXPECT_EQ(single.getKeysProperty()[0],CurveKey(2,7,0,0));
    }
}
