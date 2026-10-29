// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "CNA/Internal/Media/MediaCollectionBase.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ObjectDisposedException.hpp"

using CNA::Internal::Media::MediaCollectionBase;

// MEDIA-55: generic storage/indexer/enumerator/dispose correctness for 2 distinct
// element types, independent of any one concrete public XNA collection type (those get their own
// tests in Phase 4/6, once they wrap this template).
TEST(MediaCollectionBaseTest, WorksForIntPointers)
{
    int a = 1, b = 2;
    MediaCollectionBase<int> collection({&a, &b}, "IntCollection");
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
    MediaCollectionBase<DummyElement> collection({&x, &y, &z}, "DummyCollection");
    EXPECT_EQ(collection.Count(), 3);
    EXPECT_EQ(collection.At(2)->name, "z");
}

TEST(MediaCollectionBaseTest, IndexerThrowsArgumentOutOfRangeExceptionWhenOutOfBounds)
{
    int a = 1;
    MediaCollectionBase<int> collection({&a}, "IntCollection");
    EXPECT_THROW((void)collection.At(-1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)collection.At(1), System::ArgumentOutOfRangeException);
}

// CNA-DEMOS-ISSUE-098: XNA's collections check disposal before Count, the indexer and
// GetEnumerator; Dispose itself stays repeatable.
TEST(MediaCollectionBaseTest, DisposeRefusesCountIndexerAndEnumeration)
{
    int a = 1;
    MediaCollectionBase<int> collection({&a}, "IntCollection");
    collection.Dispose();
    collection.Dispose();
    EXPECT_TRUE(collection.IsDisposed());
    EXPECT_THROW((void)collection.Count(), System::ObjectDisposedException);
    EXPECT_THROW((void)collection.At(0), System::ObjectDisposedException);
    EXPECT_THROW((void)collection.At(-1), System::ObjectDisposedException);
    EXPECT_THROW((void)collection.begin(), System::ObjectDisposedException);
    EXPECT_THROW((void)collection.end(), System::ObjectDisposedException);
    const auto& view = collection;
    EXPECT_THROW((void)view.begin(), System::ObjectDisposedException);
    EXPECT_THROW((void)view.end(), System::ObjectDisposedException);
}

TEST(MediaCollectionBaseTest, AnEmptyCollectionIsEmpty)
{
    MediaCollectionBase<int> collection({}, "IntCollection");
    EXPECT_EQ(collection.Count(), 0);
    EXPECT_FALSE(collection.IsDisposed());
}
