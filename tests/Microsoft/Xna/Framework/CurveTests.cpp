#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveLoopType.hpp"
#include "Microsoft/Xna/Framework/CurveTangent.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

using namespace Microsoft::Xna::Framework;

static constexpr float kEps = 1e-5f;

// -----------------------------------------------------------------------
// CurveKey
// -----------------------------------------------------------------------

TEST(CurveKeyTest, ConstructorTwoArgs)
{
    CurveKey k(1.0f, 2.0f);
    EXPECT_FLOAT_EQ(k.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 2.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 0.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), 0.0f);
}

TEST(CurveKeyTest, ConstructorFourArgs)
{
    CurveKey k(2.0f, 5.0f, 1.0f, -1.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), -1.0f);
}

TEST(CurveKeyTest, SetValue)
{
    CurveKey k(0.0f, 3.0f);
    k.setValueProperty(10.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 10.0f);
}

TEST(CurveKeyTest, Clone)
{
    CurveKey k(1.0f, 2.0f, 0.5f, -0.5f);
    CurveKey c = k.Clone();
    EXPECT_FLOAT_EQ(c.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(c.getTangentInProperty(), 0.5f);
    EXPECT_FLOAT_EQ(c.getTangentOutProperty(), -0.5f);
}

TEST(CurveKeyTest, EqualityOperators)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 2.0f);
    CurveKey c(1.0f, 3.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
}

TEST(CurveKeyTest, CompareTo)
{
    CurveKey a(1.0f, 0.0f);
    CurveKey b(2.0f, 0.0f);
    EXPECT_LT(a.CompareTo(b), 0);
    EXPECT_GT(b.CompareTo(a), 0);
    EXPECT_EQ(a.CompareTo(a), 0);
}

// -----------------------------------------------------------------------
// CurveKeyCollection
// -----------------------------------------------------------------------

TEST(CurveKeyCollectionTest, EmptyCollection)
{
    CurveKeyCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(CurveKeyCollectionTest, AddAndCount)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(0.0f, 5.0f));  // added out of order
    EXPECT_EQ(col.getCountProperty(), 2);
    // Should be sorted by position
    EXPECT_FLOAT_EQ(col[0].getPositionProperty(), 0.0f);
    EXPECT_FLOAT_EQ(col[1].getPositionProperty(), 1.0f);
}

TEST(CurveKeyCollectionTest, Contains)
{
    CurveKeyCollection col;
    CurveKey k(1.0f, 2.0f);
    col.Add(k);
    EXPECT_TRUE(col.Contains(k));
    EXPECT_FALSE(col.Contains(CurveKey(9.0f, 9.0f)));
}

TEST(CurveKeyCollectionTest, Remove)
{
    CurveKeyCollection col;
    CurveKey k(1.0f, 2.0f);
    col.Add(k);
    col.Add(CurveKey(2.0f, 4.0f));
    bool removed = col.Remove(k);
    EXPECT_TRUE(removed);
    EXPECT_EQ(col.getCountProperty(), 1);
}

TEST(CurveKeyCollectionTest, Clear)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    col.Clear();
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(CurveKeyCollectionTest, Clone)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    CurveKeyCollection c = col.Clone();
    EXPECT_EQ(c.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(c[0].getValueProperty(), 10.0f);
}

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
    // Default preLoop = Constant → returns first key value
    EXPECT_FLOAT_EQ(c.Evaluate(0.0f), 3.0f);
}

TEST(CurveTest, EvaluateAfterLastKeyConstantLoop)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 1.0f));
    c.getKeysProperty().Add(CurveKey(1.0f, 9.0f));
    // Default postLoop = Constant → returns last key value
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

    // Mutating copy does not affect original
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
    // Middle key: tangentIn = value[1] - value[0] = 2, tangentOut = value[2] - value[1] = 2
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
    // Flat curve → all smooth tangents must be 0
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentInProperty(), 0.0f, kEps);
    EXPECT_NEAR(c.getKeysProperty()[1].getTangentOutProperty(), 0.0f, kEps);
}

TEST(CurveTest, ComputeTangentOutOfRangeThrows)
{
    Curve c;
    c.getKeysProperty().Add(CurveKey(0.0f, 0.0f));
    EXPECT_THROW(c.ComputeTangent(5, CurveTangent::Flat), std::out_of_range);
}
