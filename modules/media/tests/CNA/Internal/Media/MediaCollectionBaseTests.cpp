// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/Internal/Media/MediaCollectionBase.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::Internal::Media::MediaCollectionBase;

// plans/plan_media.md MEDIA-55: generic storage/indexer/enumerator/dispose correctness for 2 distinct
// element types, independent of any one concrete public XNA collection type (those get their own
// tests in Phase 4/6, once they wrap this template).
TEST(MediaCollectionBaseTest, WorksForIntPointers)
{
    int a = 1, b = 2;
    MediaCollectionBase<int> collection({&a, &b});
    EXPECT_EQ(collection.Count(), 2);
    EXPECT_FALSE(collection.IsDisposed());
    EXPECT_EQ(*collection.At(0), 1);
    EXPECT_EQ(*collection.At(1), 2);

    int count = 0;
    for (int* p : collection) { EXPECT_NE(p, nullptr); ++count; }
    EXPECT_EQ(count, 2);
}

struct DummyElement { std::string name; };

TEST(MediaCollectionBaseTest, WorksForACustomStructType)
{
    DummyElement x{"x"}, y{"y"}, z{"z"};
    MediaCollectionBase<DummyElement> collection({&x, &y, &z});
    EXPECT_EQ(collection.Count(), 3);
    EXPECT_EQ(collection.At(2)->name, "z");
}

TEST(MediaCollectionBaseTest, IndexerThrowsArgumentOutOfRangeExceptionWhenOutOfBounds)
{
    int a = 1;
    MediaCollectionBase<int> collection({&a});
    EXPECT_THROW((void)collection.At(-1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)collection.At(1), System::ArgumentOutOfRangeException);
}

TEST(MediaCollectionBaseTest, DisposeClearsAndMarksDisposed)
{
    int a = 1;
    MediaCollectionBase<int> collection({&a});
    collection.Dispose();
    EXPECT_TRUE(collection.IsDisposed());
    EXPECT_EQ(collection.Count(), 0);
}

TEST(MediaCollectionBaseTest, DefaultConstructedIsEmpty)
{
    MediaCollectionBase<int> collection;
    EXPECT_EQ(collection.Count(), 0);
    EXPECT_FALSE(collection.IsDisposed());
}
