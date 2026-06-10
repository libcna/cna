// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/CurveKeyCollection.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

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
    EXPECT_THROW(col.RemoveAt(0), std::out_of_range);
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

TEST(CurveKeyCollectionTest, SetItemPropertyOutOfRangeThrows)
{
    CurveKeyCollection col;
    EXPECT_THROW(col.setItemProperty(0, CurveKey(1.0f, 1.0f)), std::out_of_range);
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
    EXPECT_THROW(col.CopyTo(dest, -1), std::out_of_range);
}

TEST(CurveKeyCollectionTest, CopyToArrayTooSmallThrows)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    col.Add(CurveKey(2.0f, 2.0f));
    std::vector<CurveKey> dest(1, CurveKey(0.0f, 0.0f));
    EXPECT_THROW(col.CopyTo(dest, 0), std::out_of_range);
}

TEST(CurveKeyCollectionTest, GetItemPropertyOutOfRangeThrows)
{
    CurveKeyCollection col;
    col.Add(CurveKey(1.0f, 1.0f));
    EXPECT_THROW(col.getItemProperty(1), std::out_of_range);
    EXPECT_THROW(col.getItemProperty(-1), std::out_of_range);
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
