// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

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

// Task 2.10: a documented, structural divergence from FNA (see the non-const operator[]'s own
// doc comment) - real XNA's separate get/set indexer accessors only auto-append on `set`; `get`
// throws for an out-of-range index. The ElementReference proxy the non-const overload now
// returns must bind an already-existing slot at construction time, before it can know whether
// the caller will read or write through it, so even a read-only access (no assignment at all)
// silently grows the list here, unlike the correctly-throwing const overload and getItem().
// This test locks in that documented behavior so a future change doesn't silently regress it
// back to a crash (or silently "fix" it without updating the doc comment).
TEST(NetworkSessionPropertiesTest, MutableIndexerBareOutOfRangeReadAlsoAppends) {
    NetworkSessionProperties props;
    props.Add(1);
    NetworkSessionProperties& mutableRef = props;
    const std::optional<int>& read = mutableRef[10]; // no assignment - a bare read
    EXPECT_EQ(props.getCountProperty(), 2);
    EXPECT_EQ(read, std::nullopt);

    // The const overload, by contrast, correctly throws for the same out-of-range index.
    const NetworkSessionProperties& constRef = props;
    EXPECT_THROW((void) constRef[10], std::out_of_range);
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

// Task 4.5: NetworkSessionProperties implements CopyTo directly (sharp-runtime's generic
// ICollection<T> has no CopyTo member of its own), matching real .NET's
// ICollection<int?>.CopyTo(int?[], int) semantics exactly.
TEST(NetworkSessionPropertiesTest, CopyToCopiesAllElementsStartingAtIndex) {
    NetworkSessionProperties props;
    props.Add(10);
    props.Add(20);
    props.Add(30);

    std::vector<std::optional<int>> destination(5, std::nullopt);
    props.CopyTo(destination, 1);

    EXPECT_EQ(destination[0], std::nullopt);
    EXPECT_EQ(destination[1], std::optional<int>(10));
    EXPECT_EQ(destination[2], std::optional<int>(20));
    EXPECT_EQ(destination[3], std::optional<int>(30));
    EXPECT_EQ(destination[4], std::nullopt);
}

TEST(NetworkSessionPropertiesTest, CopyToNegativeIndexThrows) {
    NetworkSessionProperties props;
    props.Add(1);
    std::vector<std::optional<int>> destination(5);
    EXPECT_THROW(props.CopyTo(destination, -1), System::ArgumentOutOfRangeException);
}

TEST(NetworkSessionPropertiesTest, CopyToDestinationTooSmallThrows) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);
    std::vector<std::optional<int>> destination(2);
    EXPECT_THROW(props.CopyTo(destination, 1), System::ArgumentException);
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

// Modular sharp-runtime adaptation: IList<T>'s non-const indexer returns a tracked
// System::Collections::detail::ElementReference<T> proxy instead of a plain T&, and the
// interface grew getItem/setItem pure virtuals. The tests below pin the adapted behavior.

TEST(NetworkSessionPropertiesTest, ProxyReadsKeepPlainSpellings) {
    NetworkSessionProperties props;
    props.Add(10);
    props.Add(std::nullopt);

    // Ordinary value read through the implicit const T& conversion.
    std::optional<int> byValue = props[0];
    EXPECT_EQ(std::optional<int>(10), byValue);

    // Const-reference binding through the proxy stays valid: it aliases the slot, not the proxy.
    const std::optional<int>& byRef = props[0];
    EXPECT_EQ(10, byRef.value());

    // Read-only member access through the proxy's operator->.
    EXPECT_TRUE(props[0]->has_value());
    EXPECT_FALSE(props[1]->has_value());
}

TEST(NetworkSessionPropertiesTest, ProxyWriteStoresThroughToTheList) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);

    props[1] = 42;
    EXPECT_EQ(42, props[1]);

    props[0] = std::nullopt;
    EXPECT_FALSE(props[0]->has_value());
    EXPECT_EQ(2, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, ProxyAssignThroughCopiesTheValue) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);

    // list[i] = list[j] assigns through to the element; it never rebinds the proxy.
    props[0] = props[1];
    EXPECT_EQ(2, props[0]);
    EXPECT_EQ(2, props[1]);
}

TEST(NetworkSessionPropertiesTest, MutableIndexerOnEmptyListAppendsOneSlot) {
    NetworkSessionProperties props;
    props[0] = 7;
    EXPECT_EQ(1, props.getCountProperty());
    EXPECT_EQ(7, props[0]);
}

TEST(NetworkSessionPropertiesTest, GetItemReadsWithoutAppending) {
    NetworkSessionProperties props;
    props.Add(10);
    props.Add(std::nullopt);

    EXPECT_EQ(std::optional<int>(10), props.getItem(0));
    EXPECT_FALSE(props.getItem(1).has_value());
    EXPECT_EQ(2, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, GetItemOutOfRangeThrowsInsteadOfAppending) {
    NetworkSessionProperties props;
    EXPECT_THROW((void) props.getItem(0), std::out_of_range);
    props.Add(1);
    EXPECT_THROW((void) props.getItem(1), std::out_of_range);
    EXPECT_EQ(1, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, SetItemReplacesInRange) {
    NetworkSessionProperties props;
    props.Add(1);
    props.Add(2);

    props.setItem(0, 99);
    props.setItem(1, std::nullopt);
    EXPECT_EQ(99, props.getItem(0));
    EXPECT_FALSE(props.getItem(1).has_value());
    EXPECT_EQ(2, props.getCountProperty());
}

TEST(NetworkSessionPropertiesTest, SetItemOutOfRangeAppendsLikeXnaSet) {
    // Real XNA's indexer `set` appends past the end instead of extending out to the index;
    // setItem is the direct spelling of that accessor.
    NetworkSessionProperties props;
    props.Add(1);
    props.setItem(10, 42);
    EXPECT_EQ(2, props.getCountProperty());
    EXPECT_EQ(42, props.getItem(1));

    NetworkSessionProperties empty;
    empty.setItem(0, 5);
    EXPECT_EQ(1, empty.getCountProperty());
    EXPECT_EQ(5, empty.getItem(0));
}
