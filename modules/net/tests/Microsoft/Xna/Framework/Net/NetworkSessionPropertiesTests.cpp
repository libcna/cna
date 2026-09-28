// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <memory>
using namespace Microsoft::Xna::Framework::Net;

TEST(NetworkSessionPropertiesTest, DefaultConstructedHasEightUnspecifiedSlots)
{
    NetworkSessionProperties props;
    EXPECT_EQ(8, props.getCountProperty());
    for (int index = 0; index < 8; ++index)
        EXPECT_FALSE(props.getItem(index).has_value());
}
TEST(NetworkSessionPropertiesTest, AddAlwaysRejectsStructuralGrowth)
{
    NetworkSessionProperties props;
    EXPECT_THROW(props.Add(1), System::NotSupportedException);
    EXPECT_THROW(props.Add(std::nullopt), System::NotSupportedException);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, ConstIndexerReadsValues)
{
    NetworkSessionProperties props;
    props.setItem(0, 10);
    const auto &values = props;
    EXPECT_EQ(10, values[0]);
    EXPECT_FALSE(values[1].has_value());
}
TEST(NetworkSessionPropertiesTest, ConstIndexerOutOfRangeThrows)
{
    const NetworkSessionProperties props;
    EXPECT_THROW((void)props[-1], System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)props[8], System::ArgumentOutOfRangeException);
}
TEST(NetworkSessionPropertiesTest, MutableIndexerOverwritesInPlace)
{
    NetworkSessionProperties props;
    props[0] = 1;
    props[0] = 99;
    EXPECT_EQ(99, props[0]);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, MutableIndexerOutOfRangeNeverGrows)
{
    NetworkSessionProperties props;
    EXPECT_THROW(props[8] = 42, System::ArgumentOutOfRangeException);
    EXPECT_THROW(props[-1] = 42, System::ArgumentOutOfRangeException);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, BareMutableReadsDoNotAppend)
{
    NetworkSessionProperties props;
    const std::optional<int> &value = props[7];
    EXPECT_FALSE(value.has_value());
    EXPECT_THROW((void)props[10], System::ArgumentOutOfRangeException);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, IndexOfFoundAndNotFound)
{
    NetworkSessionProperties props;
    props[0] = 1;
    props[1] = 2;
    EXPECT_EQ(1, props.IndexOf(2));
    EXPECT_EQ(2, props.IndexOf(std::nullopt));
    EXPECT_EQ(-1, props.IndexOf(999));
}
TEST(NetworkSessionPropertiesTest, InsertAlwaysRejectsStructuralChange)
{
    NetworkSessionProperties props;
    EXPECT_THROW(props.Insert(1, 2), System::NotSupportedException);
    EXPECT_THROW(props.Insert(-1, 2), System::NotSupportedException);
}
TEST(NetworkSessionPropertiesTest, RemoveAtAlwaysRejectsStructuralChange)
{
    NetworkSessionProperties props;
    EXPECT_THROW(props.RemoveAt(0), System::NotSupportedException);
    EXPECT_THROW(props.RemoveAt(99), System::NotSupportedException);
}
TEST(NetworkSessionPropertiesTest, CollectionInterfaceReportsNotReadOnly)
{
    NetworkSessionProperties props;
    EXPECT_FALSE(props.getIsReadOnlyProperty());
    props[0] = 7;
    EXPECT_FALSE(props.getIsReadOnlyProperty());
}
TEST(NetworkSessionPropertiesTest, RemoveExistingValueThrows)
{
    NetworkSessionProperties props;
    props[0] = 1;
    EXPECT_THROW((void)props.Remove(1), System::NotSupportedException);
    EXPECT_EQ(1, props.getItem(0));
}
TEST(NetworkSessionPropertiesTest, RemoveMissingValueAlsoThrows)
{
    NetworkSessionProperties props;
    EXPECT_THROW((void)props.Remove(999), System::NotSupportedException);
}
TEST(NetworkSessionPropertiesTest, ContainsChecksValuesIncludingNull)
{
    NetworkSessionProperties props;
    props[0] = 1;
    EXPECT_TRUE(props.Contains(1));
    EXPECT_TRUE(props.Contains(std::nullopt));
    EXPECT_FALSE(props.Contains(9));
}
TEST(NetworkSessionPropertiesTest, ClearRejectsAndRetainsValues)
{
    NetworkSessionProperties props;
    props[0] = 1;
    EXPECT_THROW(props.Clear(), System::NotSupportedException);
    EXPECT_EQ(1, props.getItem(0));
}
TEST(NetworkSessionPropertiesTest, CopyToCopiesAllEightSlotsWithOffset)
{
    NetworkSessionProperties props;
    props[0] = 10;
    props[7] = 30;
    std::vector<std::optional<int>> output(10, 99);
    props.CopyTo(output, 1);
    EXPECT_EQ(99, output[0]);
    EXPECT_EQ(10, output[1]);
    EXPECT_FALSE(output[2].has_value());
    EXPECT_EQ(30, output[8]);
    EXPECT_EQ(99, output[9]);
}
TEST(NetworkSessionPropertiesTest, CopyToRejectsNegativeOffset)
{
    NetworkSessionProperties props;
    std::vector<std::optional<int>> output(8);
    EXPECT_THROW(props.CopyTo(output, -1), System::ArgumentOutOfRangeException);
}
TEST(NetworkSessionPropertiesTest, CopyToRejectsShortOrOverflowingDestination)
{
    NetworkSessionProperties props;
    std::vector<std::optional<int>> output(8);
    EXPECT_THROW(props.CopyTo(output, 1), System::ArgumentException);
    EXPECT_THROW(props.CopyTo(output, std::numeric_limits<int>::max()), System::ArgumentException);
}
TEST(NetworkSessionPropertiesTest, EnumeratorVisitsAllEightValues)
{
    NetworkSessionProperties props;
    props[0] = 10;
    props[7] = 30;
    std::unique_ptr<System::Collections::Generic::IEnumerator<std::optional<int>>> e(props.GetEnumerator());
    int count = 0;
    while (e->MoveNext())
    {
        EXPECT_EQ(props.getItem(count), e->Current());
        ++count;
    }
    EXPECT_EQ(8, count);
    EXPECT_FALSE(e->MoveNext());
}
TEST(NetworkSessionPropertiesTest, EnumeratorResetRestartsAtFirstSlot)
{
    NetworkSessionProperties props;
    props[0] = 7;
    std::unique_ptr<System::Collections::Generic::IEnumerator<std::optional<int>>> e(props.GetEnumerator());
    EXPECT_TRUE(e->MoveNext());
    e->Reset();
    EXPECT_TRUE(e->MoveNext());
    EXPECT_EQ(7, e->Current());
}
TEST(NetworkSessionPropertiesTest, EnumeratorCurrentGuardsBothInvalidPositions)
{
    NetworkSessionProperties props;
    std::unique_ptr<System::Collections::Generic::IEnumerator<std::optional<int>>> e(props.GetEnumerator());
    EXPECT_THROW((void)e->Current(), System::InvalidOperationException);
    while (e->MoveNext())
    {
    }
    EXPECT_THROW((void)e->Current(), System::InvalidOperationException);
}
TEST(NetworkSessionPropertiesTest, EnumerationObservesInPlaceWritesWithoutStructuralInvalidation)
{
    NetworkSessionProperties props;
    std::unique_ptr<System::Collections::Generic::IEnumerator<std::optional<int>>> e(props.GetEnumerator());
    EXPECT_TRUE(e->MoveNext());
    props[0] = 3;
    EXPECT_EQ(3, e->Current());
    EXPECT_TRUE(e->MoveNext());
}
TEST(NetworkSessionPropertiesTest, RangeIteratorsAreReadOnlyAndCoverEightSlots)
{
    NetworkSessionProperties props;
    static_assert(std::is_const_v<std::remove_reference_t<decltype(*props.begin())>>);
    const auto &view = props;
    EXPECT_EQ(std::distance(props.begin(), props.end()), 8);
    EXPECT_EQ(std::distance(view.begin(), view.end()), 8);
}
TEST(NetworkSessionPropertiesTest, ProxyAssignThroughCopiesTheValue)
{
    NetworkSessionProperties props;
    props[0] = 1;
    props[1] = 2;
    props[0] = props[1];
    EXPECT_EQ(2, props[0]);
    EXPECT_EQ(2, props[1]);
}
TEST(NetworkSessionPropertiesTest, EverySlotAcceptsSignedIntegerBoundsAndNull)
{
    NetworkSessionProperties props;
    for (int slot = 0; slot < 8; ++slot)
    {
        props.setItem(slot, std::numeric_limits<int>::min());
        EXPECT_EQ(std::numeric_limits<int>::min(), props.getItem(slot));
        props[slot] = std::numeric_limits<int>::max();
        EXPECT_EQ(std::numeric_limits<int>::max(), props.getItem(slot));
        props[slot] = std::nullopt;
        EXPECT_FALSE(props.getItem(slot).has_value());
    }
}
TEST(NetworkSessionPropertiesTest, GetItemOutOfRangeThrowsWithoutChangingCount)
{
    NetworkSessionProperties props;
    EXPECT_THROW((void)props.getItem(-1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)props.getItem(8), System::ArgumentOutOfRangeException);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, SetItemReplacesInRange)
{
    NetworkSessionProperties props;
    props.setItem(0, 99);
    props.setItem(1, std::nullopt);
    EXPECT_EQ(99, props.getItem(0));
    EXPECT_FALSE(props.getItem(1).has_value());
}
TEST(NetworkSessionPropertiesTest, SetItemOutOfRangeRejectsInsteadOfAppending)
{
    NetworkSessionProperties props;
    EXPECT_THROW(props.setItem(8, 42), System::ArgumentOutOfRangeException);
    EXPECT_THROW(props.setItem(-1, 42), System::ArgumentOutOfRangeException);
    EXPECT_EQ(8, props.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, ValueCopiesAndMovesRetainTheFixedShapeAndIndependentData)
{
    NetworkSessionProperties original;
    original[0] = 7;
    NetworkSessionProperties copy(original);
    copy[0] = 9;
    EXPECT_EQ(7, original.getItem(0));
    NetworkSessionProperties moved(std::move(copy));
    EXPECT_EQ(8, copy.getCountProperty());
    EXPECT_EQ(9, moved.getItem(0));
    copy = original;
    moved = std::move(original);
    EXPECT_EQ(7, copy.getItem(0));
    EXPECT_EQ(7, moved.getItem(0));
    EXPECT_EQ(8, original.getCountProperty());
}
TEST(NetworkSessionPropertiesTest, AdvertisedSnapshotCopiesRemainReadOnly)
{
    auto available = AvailableNetworkSession::CreateInternal(1, "Host", 0, 7, NetworkSessionProperties{},
                                                             QualityOfService::CreateInternal());
    NetworkSessionProperties copy(available.getSessionPropertiesProperty());
    EXPECT_FALSE(copy.getIsReadOnlyProperty());
    EXPECT_FALSE(copy.getItem(0).has_value());
    EXPECT_THROW(copy[0] = 7, System::NotSupportedException);
    EXPECT_THROW(copy.setItem(0, 7), System::NotSupportedException);
    EXPECT_THROW(copy = NetworkSessionProperties{}, System::NotSupportedException);
}
