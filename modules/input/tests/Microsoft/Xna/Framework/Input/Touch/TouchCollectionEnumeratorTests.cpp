// SPDX-License-Identifier: MS-PL
#include <any>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input::Touch;

TEST(TouchCollectionEnumeratorTest, EmptyOneManyCopyResetAndCppIteration)
{
    using Enumerator = TouchCollection::Enumerator;
    static_assert(std::is_copy_constructible_v<Enumerator>);
    static_assert(std::is_default_constructible_v<Enumerator>);
    static_assert(std::is_same_v<decltype(std::declval<const TouchCollection&>().GetEnumerator()),
                                 Enumerator>);
    static_assert(std::is_base_of_v<System::Collections::Generic::IEnumerator<TouchLocation>,
                                    Enumerator>);
    static_assert(std::is_base_of_v<System::IDisposable, Enumerator>);

    Enumerator defaultValue;
    EXPECT_THROW((void)defaultValue.Current(), System::ArgumentOutOfRangeException);
    EXPECT_FALSE(defaultValue.MoveNext());
    defaultValue.Reset();
    EXPECT_FALSE(defaultValue.MoveNext());

    TouchCollection empty;
    auto emptyCursor = empty.GetEnumerator();
    EXPECT_THROW((void)emptyCursor.Current(), System::ArgumentOutOfRangeException);
    EXPECT_FALSE(emptyCursor.MoveNext());
    EXPECT_FALSE(emptyCursor.MoveNext());
    EXPECT_THROW((void)emptyCursor.Current(), System::ArgumentOutOfRangeException);
    emptyCursor.Reset();
    EXPECT_FALSE(emptyCursor.MoveNext());

    TouchLocation first{1, TouchLocationState::Pressed, Vector2{10, 20}};
    TouchLocation second{2, TouchLocationState::Moved, Vector2{30, 40}};
    TouchCollection one{{first}};
    auto single = one.GetEnumerator();
    EXPECT_THROW((void)single.Current(), System::ArgumentOutOfRangeException);
    ASSERT_TRUE(single.MoveNext());
    EXPECT_EQ(single.Current().getIdProperty(), 1);
    EXPECT_EQ(std::any_cast<TouchLocation>(single.getCurrentProperty()).getIdProperty(), 1);
    EXPECT_FALSE(single.MoveNext());
    EXPECT_THROW((void)single.Current(), System::ArgumentOutOfRangeException);
    single.Reset();
    ASSERT_TRUE(single.MoveNext());
    EXPECT_EQ(single.Current().getIdProperty(), 1);
    single.Dispose();
    EXPECT_EQ(single.Current().getIdProperty(), 1);
    auto polymorphicCursor = one.GetEnumerator();
    System::Collections::IEnumerator& erased = polymorphicCursor;
    EXPECT_THROW((void)erased.getCurrentProperty(), System::ArgumentOutOfRangeException);
    ASSERT_TRUE(erased.MoveNext());
    EXPECT_EQ(std::any_cast<TouchLocation>(erased.getCurrentProperty()).getIdProperty(), 1);
    erased.Reset();
    EXPECT_THROW((void)erased.getCurrentProperty(), System::ArgumentOutOfRangeException);

    TouchCollection many{{first, second}};
    auto a = many.GetEnumerator();
    auto b = many.GetEnumerator();
    ASSERT_TRUE(a.MoveNext());
    auto copied = a;
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current().getIdProperty(), 2);
    EXPECT_EQ(copied.Current().getIdProperty(), 1);
    ASSERT_TRUE(copied.MoveNext());
    EXPECT_EQ(copied.Current().getIdProperty(), 2);
    ASSERT_TRUE(b.MoveNext());
    EXPECT_EQ(b.Current().getIdProperty(), 1);
    ASSERT_TRUE(b.MoveNext());
    EXPECT_EQ(b.Current().getIdProperty(), 2);
    EXPECT_FALSE(a.MoveNext());
    EXPECT_FALSE(a.MoveNext());
    a.Reset();
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current().getIdProperty(), 1);

    EXPECT_EQ(many.begin()->getIdProperty(), 1);
    EXPECT_EQ((many.end() - 1)->getIdProperty(), 2);
    std::vector<int> ids;
    for (const auto& touch : many)
    {
        ids.push_back(touch.getIdProperty());
    }
    EXPECT_EQ(ids, (std::vector<int>{1, 2}));
}

TEST(TouchCollectionEnumeratorTest, SnapshotIsIndependentOfLaterCollectionMutation)
{
    TouchLocation first{1, TouchLocationState::Pressed, Vector2{10, 20}};
    TouchLocation second{2, TouchLocationState::Moved, Vector2{30, 40}};
    TouchCollection touches{{first}};
    auto cursor = touches.GetEnumerator();
    touches.Add(second);
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_EQ(cursor.Current().getIdProperty(), 1);
    EXPECT_FALSE(cursor.MoveNext());
    EXPECT_EQ(touches.getCountProperty(), 2);
}
