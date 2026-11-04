// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using namespace Microsoft::Xna::Framework;

TEST(CurveKeyCollectionTest, EmptyCollection)
{
    CurveKeyCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(CurveKeyCollectionTest, IsReadOnlyIsFalse)
{
    CurveKeyCollection col;
    EXPECT_FALSE(col.getIsReadOnlyProperty());
}

TEST(CurveKeyCollectionTest, AddSortsAscendingByPosition)
{
    CurveKeyCollection col;
    col.Add(CurveKey(3.0f, 3.0f));
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    EXPECT_FLOAT_EQ(col[0].getPositionProperty(), 1.0f);
    EXPECT_FLOAT_EQ(col[1].getPositionProperty(), 2.0f);
    EXPECT_FLOAT_EQ(col[2].getPositionProperty(), 3.0f);
}

TEST(CurveKeyCollectionTest, AddAndCount)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(0.0f, 5.0f));
    EXPECT_EQ(col.getCountProperty(), 2);
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
    EXPECT_TRUE(col.Remove(k));
    EXPECT_EQ(col.getCountProperty(), 1);
}

TEST(CurveKeyCollectionTest, RemoveNotFoundReturnsFalse)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    EXPECT_FALSE(col.Remove(CurveKey(9.0f, 9.0f)));
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

TEST(CurveKeyCollectionTest, IndexOf)
{
    CurveKeyCollection col;
    CurveKey k(1.0f, 5.0f);
    col.Add(k);
    col.Add(CurveKey(2.0f, 6.0f));
    EXPECT_EQ(col.IndexOf(k), 0);
    EXPECT_EQ(col.IndexOf(CurveKey(9.0f, 9.0f)), -1);
}

TEST(CurveKeyCollectionTest, RemoveAt)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    col.RemoveAt(0);
    EXPECT_EQ(col.getCountProperty(), 1);
    EXPECT_FLOAT_EQ(col[0].getPositionProperty(), 2.0f);
}

TEST(CurveKeyCollectionTest, RemoveAtOutOfRangeThrows)
{
    CurveKeyCollection col;
    EXPECT_THROW(col.RemoveAt(0), System::ArgumentOutOfRangeException);
}

TEST(CurveKeyCollectionTest, SetItemPropertySamePositionReplacesInPlace)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    col.setItemProperty(0, CurveKey(1.0f, 99.0f));
    EXPECT_FLOAT_EQ(col[0].getValueProperty(), 99.0f);
    EXPECT_EQ(col.getCountProperty(), 2);
}

TEST(CurveKeyCollectionTest, SetItemPropertyDifferentPositionReinserts)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(3.0f, 3.0f));
    col.setItemProperty(0, CurveKey(2.0f, 99.0f));
    EXPECT_EQ(col.getCountProperty(), 2);
    EXPECT_FLOAT_EQ(col[0].getPositionProperty(), 2.0f);
}

TEST(CurveKeyCollectionTest, NearbyReplacementReinsertsInExactPositionOrder)
{
    // CNA-DEMOS-ISSUE-097: epsilon equality must not bypass sorted insertion.
    const float epsilon = MathHelper::MachineEpsilonFloat;
    CurveKeyCollection col;
    col.Add(CurveKey(0.0f, 0.0f));
    col.Add(CurveKey(epsilon * 0.5f, 1.0f));
    col.setItemProperty(0, CurveKey(epsilon * 0.75f, 2.0f));
    ASSERT_EQ(col.getCountProperty(), 2);
    EXPECT_EQ(col[0].getPositionProperty(), epsilon * 0.5f);
    EXPECT_EQ(col[1].getPositionProperty(), epsilon * 0.75f);
    EXPECT_EQ(col[0].getValueProperty(), 1.0f);
    EXPECT_EQ(col[1].getValueProperty(), 2.0f);
}

TEST(CurveKeyCollectionTest, ReplacementMayAliasAnotherElement)
{
    CurveKeyCollection col;
    col.Add(CurveKey(0.0f, 0.0f));
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(2.0f, 20.0f));
    col.setItemProperty(0, col[1]);
    ASSERT_EQ(col.getCountProperty(), 3);
    EXPECT_EQ(col[0], CurveKey(1.0f, 10.0f));
    EXPECT_EQ(col[1], CurveKey(1.0f, 10.0f));
    EXPECT_EQ(col[2], CurveKey(2.0f, 20.0f));
}

TEST(CurveKeyCollectionTest, ReplacementMayAliasTheLastElement)
{
    // Erase destroys the old last object; an ASan/UBSan build must not observe a stale vptr.
    CurveKeyCollection col;
    col.Add(CurveKey(0.0f, 0.0f));
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(2.0f, 20.0f));
    col.setItemProperty(0, col[2]);
    ASSERT_EQ(col.getCountProperty(), 3);
    EXPECT_EQ(col[0], CurveKey(1.0f, 10.0f));
    EXPECT_EQ(col[1], CurveKey(2.0f, 20.0f));
    EXPECT_EQ(col[2], CurveKey(2.0f, 20.0f));
}

TEST(CurveKeyCollectionTest, ExactReplacementPreservesDuplicateOrder)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(1.0f, 11.0f));
    col.Add(CurveKey(1.0f, 12.0f));
    col.setItemProperty(1, CurveKey(1.0f, 99.0f));
    EXPECT_EQ(col[0].getValueProperty(), 10.0f);
    EXPECT_EQ(col[1].getValueProperty(), 99.0f);
    EXPECT_EQ(col[2].getValueProperty(), 12.0f);
}

TEST(CurveKeyCollectionTest, SetItemPropertyOutOfRangeThrows)
{
    CurveKeyCollection col;
    EXPECT_THROW(col.setItemProperty(0, CurveKey(1.0f, 1.0f)), System::ArgumentOutOfRangeException);
}

TEST(CurveKeyCollectionTest, CopyTo)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(2.0f, 20.0f));
    std::vector<CurveKey> dest(3, CurveKey(0.0f, 0.0f));
    col.CopyTo(dest, 1);
    EXPECT_FLOAT_EQ(dest[1].getValueProperty(), 10.0f);
    EXPECT_FLOAT_EQ(dest[2].getValueProperty(), 20.0f);
}

TEST(CurveKeyCollectionTest, CopyToNegativeIndexThrows)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    std::vector<CurveKey> dest(2, CurveKey(0.0f, 0.0f));
    EXPECT_THROW(col.CopyTo(dest, -1), System::ArgumentOutOfRangeException);
}

TEST(CurveKeyCollectionTest, CopyToArrayTooSmallThrows)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    std::vector<CurveKey> dest(1, CurveKey(0.0f, 0.0f));
    EXPECT_THROW(col.CopyTo(dest, 0), System::ArgumentException);
}

TEST(CurveKeyCollectionTest, GetItemPropertyOutOfRangeThrows)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    EXPECT_THROW(col.getItemProperty(1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(col.getItemProperty(-1), System::ArgumentOutOfRangeException);
}

TEST(CurveKeyCollectionTest, CloneIsIndependent)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    CurveKeyCollection c = col.Clone();
    c.Clear();
    EXPECT_EQ(col.getCountProperty(), 1);
}

TEST(CurveKeyCollectionTest, RangeForIteration)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 10.0f));
    col.Add(CurveKey(2.0f, 20.0f));
    float sum = 0.0f;
    for (const CurveKey& k : col)
    {
        sum += k.getValueProperty();
    }
    EXPECT_FLOAT_EQ(sum, 30.0f);
}

TEST(CurveKeyCollectionTest, ReplacementMovesAcrossKeysAndJoinsAfterEqualPositions)
{
    // Refresh set_Item IL_0021-004b: exact-position replacement stays in place;
    // changed positions remove then Add, whose IL_0020-005a advances past equals.
    CurveKeyCollection keys;
    keys.Add(CurveKey(1,10,11,12));
    keys.Add(CurveKey(2,20,21,22));
    keys.Add(CurveKey(3,30,31,32));
    keys.setItemProperty(0,CurveKey(4,40,41,42,CurveContinuity::Step));
    ASSERT_EQ(keys.getCountProperty(),3);
    EXPECT_EQ(keys[0],CurveKey(2,20,21,22));
    EXPECT_EQ(keys[1],CurveKey(3,30,31,32));
    EXPECT_EQ(keys[2],CurveKey(4,40,41,42,CurveContinuity::Step));
    keys.setItemProperty(2,CurveKey(0,50,51,52));
    EXPECT_EQ(keys[0],CurveKey(0,50,51,52));
    EXPECT_EQ(keys[1],CurveKey(2,20,21,22));
    EXPECT_EQ(keys[2],CurveKey(3,30,31,32));
    keys.setItemProperty(0,CurveKey(2,60,61,62));
    EXPECT_EQ(keys[0],CurveKey(2,20,21,22));
    EXPECT_EQ(keys[1],CurveKey(2,60,61,62));
    EXPECT_EQ(keys[2],CurveKey(3,30,31,32));
}

TEST(CurveKeyCollectionTest, SearchAndRemoveUseWholeKeyEqualityAndFirstMatchingIndex)
{
    // Refresh IndexOf IL_0000-000c, Contains IL_0000-000c, Remove IL_0000-0013
    // delegate to List<CurveKey>, whose equality comparer calls CurveKey.Equals.
    CurveKeyCollection keys;
    const CurveKey a(1,2,3,4), different(1,2,30,4), tail(2,9);
    keys.Add(a); keys.Add(different); keys.Add(a); keys.Add(tail);
    EXPECT_TRUE(keys.Contains(a.Clone()));
    EXPECT_EQ(keys.IndexOf(a.Clone()),0);
    EXPECT_EQ(keys.IndexOf(different),1);
    EXPECT_FALSE(keys.Contains(CurveKey(1,2,3,40)));
    EXPECT_EQ(keys.IndexOf(CurveKey(1,2,3,40)),-1);
    EXPECT_FALSE(keys.Remove(CurveKey(1,2,3,40)));
    ASSERT_EQ(keys.getCountProperty(),4);
    EXPECT_TRUE(keys.Remove(a.Clone()));
    ASSERT_EQ(keys.getCountProperty(),3);
    EXPECT_EQ(keys[0],different);
    EXPECT_EQ(keys[1],a);
    EXPECT_EQ(keys[2],tail);
    EXPECT_EQ(keys.IndexOf(a),1);
    keys.RemoveAt(1);
    ASSERT_EQ(keys.getCountProperty(),2);
    EXPECT_EQ(keys[0],different);
    EXPECT_EQ(keys[1],tail);
    keys.Clear();
    EXPECT_FALSE(keys.Contains(different));
    EXPECT_EQ(keys.IndexOf(different),-1);
    keys.Add(tail);
    EXPECT_EQ(keys.getCountProperty(),1);
    EXPECT_EQ(keys[0],tail);
}

TEST(CurveKeyCollectionTest, ConstIndexersAndInvalidMutationsPreserveAllFields)
{
    // Refresh get_Item IL_0000-000c / RemoveAt IL_0000-0013 use List bounds checks;
    // set_Item IL_000f-0016 reads the old element before changing anything.
    CurveKeyCollection keys;
    const CurveKey first(1,2,3,4,CurveContinuity::Step), last(5,6,7,8);
    keys.Add(first); keys.Add(last);
    const CurveKeyCollection& readOnly=keys;
    EXPECT_EQ(readOnly.getItemProperty(0),first);
    EXPECT_EQ(readOnly[1],last);
    for (const int index : {-1,2})
    {
        EXPECT_THROW((void)readOnly.getItemProperty(index),System::ArgumentOutOfRangeException);
        EXPECT_THROW((void)readOnly[index],System::ArgumentOutOfRangeException);
        EXPECT_THROW(keys.RemoveAt(index),System::ArgumentOutOfRangeException);
        EXPECT_THROW(keys.setItemProperty(index,CurveKey(9,10)),System::ArgumentOutOfRangeException);
        ASSERT_EQ(keys.getCountProperty(),2);
        EXPECT_EQ(keys[0],first);
        EXPECT_EQ(keys[1],last);
    }
}

TEST(CurveKeyCollectionTest, CopyToPreservesSentinelsAndRefusedDestinations)
{
    // Refresh CopyTo IL_0000-0014 delegates to List.CopyTo/Array.Copy:
    // copy complete keys at the requested offset; bounds failures precede all writes.
    CurveKeyCollection keys;
    const CurveKey sentinel(-1,-2,-3,-4,CurveContinuity::Step);
    const CurveKey first(1,2,3,4,CurveContinuity::Step), last(5,6,7,8);
    keys.Add(first); keys.Add(last);
    std::vector<CurveKey> target(4,sentinel);
    keys.CopyTo(target,1);
    EXPECT_EQ(target[0],sentinel);
    EXPECT_EQ(target[1],first);
    EXPECT_EQ(target[2],last);
    EXPECT_EQ(target[3],sentinel);
    for (const int index : {-1,3,5})
    {
        target.assign(4,sentinel);
        if (index<0) EXPECT_THROW(keys.CopyTo(target,index),System::ArgumentOutOfRangeException);
        else EXPECT_THROW(keys.CopyTo(target,index),System::ArgumentException);
        EXPECT_EQ(target,std::vector<CurveKey>(4,sentinel));
    }
    CurveKeyCollection empty;
    empty.CopyTo(target,4);
    EXPECT_EQ(target,std::vector<CurveKey>(4,sentinel));
    EXPECT_THROW(empty.CopyTo(target,5),System::ArgumentException);
    EXPECT_EQ(target,std::vector<CurveKey>(4,sentinel));
}

TEST(CurveKeyCollectionTest, CloneKeepsFieldsAndKeyMutationsIndependentByCnaValueContract)
{
    // CNA's documented value-copy contract (CurveKeyCollection.hpp Clone), explicitly
    // requested by TQ-049. Refresh Clone IL_0006-0012 copies key references instead;
    // this ownership divergence is recorded in the lane design, not claimed as XNA parity.
    CurveKeyCollection source;
    source.Add(CurveKey(1,2,3,4,CurveContinuity::Step));
    source.Add(CurveKey(5,6,7,8));
    auto copy=source.Clone();
    ASSERT_EQ(copy.getCountProperty(),2);
    EXPECT_EQ(copy[0],CurveKey(1,2,3,4,CurveContinuity::Step));
    EXPECT_EQ(copy[1],CurveKey(5,6,7,8));
    copy[0].setValueProperty(20);
    copy[0].setTangentInProperty(30);
    copy[0].setTangentOutProperty(40);
    copy[0].setContinuityProperty(CurveContinuity::Smooth);
    EXPECT_EQ(source[0],CurveKey(1,2,3,4,CurveContinuity::Step));
    source[1].setValueProperty(60);
    EXPECT_EQ(copy[1],CurveKey(5,6,7,8));
}
