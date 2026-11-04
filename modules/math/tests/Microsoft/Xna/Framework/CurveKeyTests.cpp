// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <any>
#include <limits>
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

using namespace Microsoft::Xna::Framework;

// --- constructors ---

TEST(CurveKeyTest, ConstructorTwoArgs)
{
    CurveKey k(1.0f, 2.0f);
    EXPECT_FLOAT_EQ(k.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 2.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 0.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), 0.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
}

TEST(CurveKeyTest, ConstructorFourArgs)
{
    CurveKey k(2.0f, 5.0f, 1.0f, -1.0f);
    // Refresh CurveKey::.ctor(float,float,float,float) IL_0006-000f stores both.
    EXPECT_EQ(k.getPositionProperty(),2.0f);
    EXPECT_EQ(k.getValueProperty(),5.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), -1.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
}

TEST(CurveKeyTest, ConstructorFiveArgs)
{
    CurveKey k(3.0f, 7.0f, 1.0f, -1.0f, CurveContinuity::Step);
    EXPECT_FLOAT_EQ(k.getPositionProperty(), 3.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 7.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 1.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), -1.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Step);
}

// --- setters ---

TEST(CurveKeyTest, SetValue)
{
    CurveKey k(0.0f, 3.0f);
    k.setValueProperty(10.0f);
    EXPECT_FLOAT_EQ(k.getValueProperty(), 10.0f);
}

TEST(CurveKeyTest, ContinuitySetterRoundTrips)
{
    CurveKey k(0.0f, 0.0f);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Smooth);
    k.setContinuityProperty(CurveContinuity::Step);
    EXPECT_EQ(k.getContinuityProperty(), CurveContinuity::Step);
}

TEST(CurveKeyTest, SetTangentIn)
{
    CurveKey k(0.0f, 0.0f);
    k.setTangentInProperty(7.0f);
    EXPECT_FLOAT_EQ(k.getTangentInProperty(), 7.0f);
}

TEST(CurveKeyTest, SetTangentOut)
{
    CurveKey k(0.0f, 0.0f);
    k.setTangentOutProperty(8.0f);
    EXPECT_FLOAT_EQ(k.getTangentOutProperty(), 8.0f);
}

// --- Clone ---

TEST(CurveKeyTest, Clone)
{
    CurveKey k(1.0f, 2.0f, 0.5f, -0.5f);
    CurveKey c = k.Clone();
    EXPECT_FLOAT_EQ(c.getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(c.getTangentInProperty(), 0.5f);
    EXPECT_FLOAT_EQ(c.getTangentOutProperty(), -0.5f);
}

TEST(CurveKeyTest, CloneIsIndependent)
{
    CurveKey k(1.0f, 2.0f);
    CurveKey c = k.Clone();
    c.setValueProperty(99.0f);
    EXPECT_NE(k.getValueProperty(), 99.0f);
    // FNA CurveKey.Clone: changing the copy cannot alter the original value.
    EXPECT_EQ(k.getValueProperty(), 2.0f);
}

// --- CompareTo ---

TEST(CurveKeyTest, CompareTo)
{
    CurveKey a(1.0f, 0.0f);
    CurveKey b(2.0f, 0.0f);
    EXPECT_LT(a.CompareTo(b), 0);
    EXPECT_GT(b.CompareTo(a), 0);
    EXPECT_EQ(a.CompareTo(a), 0);
}

TEST(CurveKeyTest, CompareToPreservesXnaUnorderedNaNResult)
{
    const CurveKey nan(std::numeric_limits<float>::quiet_NaN(), 0.0f);
    const CurveKey real(1.0f, 0.0f);
    EXPECT_EQ(nan.CompareTo(real), 1);
    EXPECT_EQ(real.CompareTo(nan), 1);
    EXPECT_EQ(nan.CompareTo(nan), 1);
    EXPECT_EQ(CurveKey(-0.0f, 0.0f).CompareTo(CurveKey(0.0f, 1.0f)), 0);
}

// --- Equals ---

TEST(CurveKeyTest, EqualsDirectCall)
{
    CurveKey a(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Smooth);
    CurveKey b(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Smooth);
    CurveKey c(1.0f, 2.0f, 0.5f, -0.5f, CurveContinuity::Step);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_FALSE(a.Equals(c));
}

TEST(CurveKeyTest, EqualsFalsePosition)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(2.0f, 2.0f);
    EXPECT_FALSE(a.Equals(b));
}

TEST(CurveKeyTest, EqualsFalseValue)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 3.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- operators ---

TEST(CurveKeyTest, EqualityOperators)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 2.0f);
    CurveKey c(1.0f, 3.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a != b);
}

// --- GetHashCode ---

TEST(CurveKeyTest, GetHashCodeConsistency)
{
    CurveKey a(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
    // Refresh CurveKey.GetHashCode IL_0000-0040 sums Single hashes modulo 2^32:
    // 0x3f800000 + 0x40000000 + 0x40400000 + 0x40800000 = 0x00400000.
    EXPECT_EQ(a.GetHashCode(),std::size_t{0x00400000});
}

TEST(CurveKeyTest, GetHashCodeEqualForEqualKeys)
{
    CurveKey a(1.0f, 2.0f, 0.5f, -0.5f);
    CurveKey b(1.0f, 2.0f, 0.5f, -0.5f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(CurveKeyTest, GetHashCodeDifferentForDifferentKeys)
{
    CurveKey a(1.0f, 2.0f);
    CurveKey b(1.0f, 3.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

TEST(CurveKeyTest, EveryFieldParticipatesInEqualityAndObjectEquals)
{
    // Refresh CurveKey.Equals(CurveKey) IL_0009-004f compares all five fields;
    // Equals(object) IL_0000-000c accepts only CurveKey objects.
    const CurveKey original(1,2,3,4,CurveContinuity::Step);
    const CurveKey variants[]={CurveKey(9,2,3,4,CurveContinuity::Step),
        CurveKey(1,9,3,4,CurveContinuity::Step),CurveKey(1,2,9,4,CurveContinuity::Step),
        CurveKey(1,2,3,9,CurveContinuity::Step),CurveKey(1,2,3,4,CurveContinuity::Smooth)};
    EXPECT_TRUE(original.Equals(std::any(original)));
    EXPECT_FALSE(original.Equals(std::any{}));
    EXPECT_FALSE(original.Equals(std::any(42)));
    EXPECT_TRUE(original==original.Clone());
    EXPECT_FALSE(original!=original.Clone());
    for (const auto& changed : variants)
    {
        EXPECT_FALSE(original.Equals(changed));
        EXPECT_FALSE(changed.Equals(original));
        EXPECT_FALSE(original.Equals(std::any(changed)));
        EXPECT_FALSE(original==changed);
        EXPECT_FALSE(changed==original);
        EXPECT_TRUE(original!=changed);
        EXPECT_TRUE(changed!=original);
    }
}

TEST(CurveKeyTest, EqualPositionsCompareAsZeroRegardlessOfOtherFields)
{
    // Refresh CurveKey.CompareTo IL_0000-0021 compares only positions.
    const CurveKey a(2,3,4,5,CurveContinuity::Step), b(2,-30,-40,-50);
    EXPECT_EQ(a.CompareTo(b),0);
    EXPECT_EQ(b.CompareTo(a),0);
    EXPECT_FALSE(a.Equals(b));
}

TEST(CurveKeyTest, NaNInEachFloatFieldIsUnequalAndZeroSignsAreEqual)
{
    // Refresh Equals IL_0015/0023/0031/003f uses unordered-not-equal branches.
    const float n=std::numeric_limits<float>::quiet_NaN();
    const CurveKey nanKeys[]={CurveKey(n,2,3,4),CurveKey(1,n,3,4),
        CurveKey(1,2,n,4),CurveKey(1,2,3,n)};
    for (const auto& key : nanKeys)
    {
        EXPECT_FALSE(key.Equals(key));
        EXPECT_FALSE(key.Equals(std::any(key)));
        EXPECT_FALSE(key==key);
        EXPECT_TRUE(key!=key);
    }
    const CurveKey zero(0,0,0,0), negativeZero(-0.0f,-0.0f,-0.0f,-0.0f);
    EXPECT_TRUE(zero.Equals(negativeZero));
    EXPECT_TRUE(zero==negativeZero);
    EXPECT_FALSE(zero!=negativeZero);
    EXPECT_EQ(zero.GetHashCode(),std::size_t{0});
    EXPECT_EQ(negativeZero.GetHashCode(),std::size_t{0});
    // Check each zero sign separately: four sign bits would cancel in a wrapping sum.
    for (const auto& key : {CurveKey(-0.0f,0,0,0),CurveKey(0,-0.0f,0,0),
                            CurveKey(0,0,-0.0f,0),CurveKey(0,0,0,-0.0f)})
    {
        EXPECT_TRUE(key.Equals(zero));
        EXPECT_EQ(key.GetHashCode(),std::size_t{0});
    }
}

TEST(CurveKeyTest, HashIncludesEveryFieldAndWrapsTheSumAt32Bits)
{
    // Refresh GetHashCode IL_0000-0040 adds the four Single hashes and continuity;
    // .NET 4 Single hashes finite nonzero floats by their binary32 bit patterns.
    const CurveKey keys[]={CurveKey(1,0),CurveKey(0,2),CurveKey(0,0,3,0),
        CurveKey(0,0,0,4),CurveKey(0,0,0,0,CurveContinuity::Step),
        CurveKey(1,2,3,4,CurveContinuity::Step)};
    const std::size_t hashes[]={0x3f800000,0x40000000,0x40400000,0x40800000,1,0x00400001};
    for (int i=0; i<6; ++i) EXPECT_EQ(keys[i].GetHashCode(),hashes[i]);
}

TEST(CurveKeyTest, CloneRetainsAllFiveFieldsAndOwnsItsMutableData)
{
    // Refresh CurveKey.Clone IL_0000-0023 constructs a key with all five original fields.
    CurveKey original(2,3,4,5,CurveContinuity::Step);
    auto copy=original.Clone();
    EXPECT_EQ(copy,CurveKey(2,3,4,5,CurveContinuity::Step));
    copy.setValueProperty(30);
    copy.setTangentInProperty(40);
    copy.setTangentOutProperty(50);
    copy.setContinuityProperty(CurveContinuity::Smooth);
    EXPECT_EQ(original,CurveKey(2,3,4,5,CurveContinuity::Step));
    EXPECT_EQ(copy,CurveKey(2,30,40,50,CurveContinuity::Smooth));
}
