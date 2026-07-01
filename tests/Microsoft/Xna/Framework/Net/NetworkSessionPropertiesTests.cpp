// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"

using namespace Microsoft::Xna::Framework::Net;

TEST(NetworkSessionPropertiesTest, DefaultConstructedIsEmpty) {
    NetworkSessionProperties props;
    EXPECT_EQ(0, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, AddIncreasesCount) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(std::nullopt);
    props.Add(3);
    EXPECT_EQ(3, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, ConstIndexerReadsValues) {
    NetworkSessionProperties props;
    props.Add(10);
    props.Add(std::nullopt);
    const NetworkSessionProperties& constProps = props;
    EXPECT_EQ(10, constProps[0]);
    EXPECT_FALSE(constProps[1].has_value());
}

TEST(NetworkSessionPropertiesTest, ConstIndexerOutOfRangeThrows) {
    NetworkSessionProperties props;
    const NetworkSessionProperties& constProps = props;
    EXPECT_THROW((void)constProps[0], std::out_of_range);
}

TEST(NetworkSessionPropertiesTest, MutableIndexerOverwritesInPlace) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props[0] = 99;
    EXPECT_EQ(99, props[0]);
    EXPECT_EQ(2, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, MutableIndexerOutOfRangeAppendsInsteadOfExtending) {
    // Matches FNA's own quirky behavior: assigning at an out-of-range index appends a new
    // element at the end rather than extending the list out to that index.
    NetworkSessionProperties props;
    props.Add(1);
    props[10] = 42;
    EXPECT_EQ(2, props.getCountProperty());
    EXPECT_EQ(42, props[1]);
}

TEST(NetworkSessionPropertiesTest, IndexOfFoundAndNotFound) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Add(std::nullopt);
    EXPECT_EQ(1, props.IndexOf(2));
    EXPECT_EQ(2, props.IndexOf(std::nullopt));
    EXPECT_EQ(-1, props.IndexOf(999));
}

TEST(NetworkSessionPropertiesTest, InsertShiftsSubsequentElements) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(3);
    props.Insert(1, 2);
    EXPECT_EQ(3, props.getCountProperty());
    EXPECT_EQ(1, props[0]);
    EXPECT_EQ(2, props[1]);
    EXPECT_EQ(3, props[2]);
}

TEST(NetworkSessionPropertiesTest, RemoveAtShiftsSubsequentElements) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Add(3);
    props.RemoveAt(1);
    EXPECT_EQ(2, props.getCountProperty());
    EXPECT_EQ(1, props[0]);
    EXPECT_EQ(3, props[1]);
}

TEST(NetworkSessionPropertiesTest, IsReadOnlyAlwaysTrue) {
    NetworkSessionProperties props;
    EXPECT_TRUE(props.getIsReadOnlyProperty());
    props.Add(1);
    EXPECT_TRUE(props.getIsReadOnlyProperty());
}

TEST(NetworkSessionPropertiesTest, RemoveFirstOccurrence) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Add(2);
    EXPECT_TRUE(props.Remove(2));
    EXPECT_EQ(2, props.getCountProperty());
    EXPECT_EQ(1, props[0]);
    EXPECT_EQ(2, props[1]);
}

TEST(NetworkSessionPropertiesTest, RemoveNotFoundReturnsFalse) {
    NetworkSessionProperties props;
    props.Add(1);
    EXPECT_FALSE(props.Remove(999));
    EXPECT_EQ(1, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, ContainsTrueAndFalse) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(std::nullopt);
    EXPECT_TRUE(props.Contains(1));
    EXPECT_TRUE(props.Contains(std::nullopt));
    EXPECT_FALSE(props.Contains(999));
}

TEST(NetworkSessionPropertiesTest, ClearEmptiesList) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Clear();
    EXPECT_EQ(0, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, GetEnumeratorIteratesAllValues) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Add(std::nullopt);

    auto* enumerator = props.GetEnumerator();
    ASSERT_NE(nullptr, enumerator);

    int count = 0;
    while (enumerator->MoveNext())
    {
        ++count;
    }
    EXPECT_EQ(3, count);
    delete enumerator;
}

TEST(NetworkSessionPropertiesTest, RangeForViaBeginEnd) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    props.Add(3);

    int sum = 0;
    for (const auto& value : props)
    {
        sum += value.value_or(0);
    }
    EXPECT_EQ(6, sum);
}
