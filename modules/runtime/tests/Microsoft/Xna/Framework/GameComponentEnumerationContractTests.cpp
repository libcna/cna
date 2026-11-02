// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"

#include <functional>
#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;

namespace
{
    struct EnumerationComponent final : IGameComponent
    {
        void Initialize() override {}
    };
}

TEST(GameComponentEnumerationContractTest, CursorPositionsCopiesResetAndDisposeMatchListEnumeration)
{
    GameComponentCollection empty;
    auto cursor = empty.GetEnumerator();
    EXPECT_EQ(cursor.Current(), nullptr);
    EXPECT_THROW((void)cursor.getCurrentProperty(), System::InvalidOperationException);
    EXPECT_FALSE(cursor.MoveNext());
    EXPECT_FALSE(cursor.MoveNext());
    EXPECT_THROW((void)cursor.getCurrentProperty(), System::InvalidOperationException);
    cursor.Reset();
    EXPECT_FALSE(cursor.MoveNext());

    EnumerationComponent first;
    GameComponentCollection items;
    items.Add(&first);
    items.Add(nullptr);
    auto a = items.GetEnumerator();
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current(), &first);
    System::Collections::IEnumerator& erased = a;
    EXPECT_EQ(std::any_cast<IGameComponent*>(erased.getCurrentProperty()), &first);
    auto b = a;
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current(), nullptr);
    EXPECT_EQ(std::any_cast<IGameComponent*>(a.getCurrentProperty()), nullptr);
    EXPECT_EQ(b.Current(), &first);
    b.Dispose();
    EXPECT_EQ(b.Current(), &first);
    ASSERT_TRUE(b.MoveNext());
    EXPECT_FALSE(a.MoveNext());
    EXPECT_EQ(a.Current(), nullptr);
    EXPECT_THROW((void)a.getCurrentProperty(), System::InvalidOperationException);
    a.Reset();
    EXPECT_EQ(a.Current(), nullptr);
    ASSERT_TRUE(a.MoveNext());
    EXPECT_EQ(a.Current(), &first);
}

TEST(GameComponentEnumerationContractTest, EveryStructuralMutationInvalidatesMoveNextAndReset)
{
    EnumerationComponent first, second, third;
    const std::function<void(GameComponentCollection&)> mutations[] = {
        [&](auto& c) { c.Add(&third); },
        [&](auto& c) { c.Insert(0, &third); },
        [&](auto& c) { EXPECT_TRUE(c.Remove(&second)); },
        [&](auto& c) { c.RemoveAt(1); },
        [&](auto& c) { c.Clear(); }
    };
    for (const auto& mutate : mutations)
    {
        GameComponentCollection items;
        items.Add(&first);
        items.Add(&second);
        auto cursor = items.GetEnumerator();
        ASSERT_TRUE(cursor.MoveNext());
        mutate(items);
        EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
        EXPECT_THROW(cursor.Reset(), System::InvalidOperationException);
        EXPECT_EQ(cursor.Current(), &first);
        EXPECT_EQ(std::any_cast<IGameComponent*>(cursor.getCurrentProperty()), &first);
    }
    GameComponentCollection empty;
    auto cursor = empty.GetEnumerator();
    empty.Clear();
    EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
}

TEST(GameComponentEnumerationContractTest, RefusedAndAbsentMutationsLeaveCursorUsable)
{
    EnumerationComponent first, second, absent;
    GameComponentCollection items;
    items.Add(&first);
    items.Add(&second);
    auto cursor = items.GetEnumerator();
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_THROW(items.Add(&first), System::ArgumentException);
    EXPECT_THROW(items.Insert(4, &absent), System::ArgumentOutOfRangeException);
    EXPECT_THROW(items.RemoveAt(2), System::ArgumentOutOfRangeException);
    EXPECT_FALSE(items.Remove(&absent));
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_EQ(cursor.Current(), &second);
    EXPECT_NO_THROW(cursor.Reset());
}

TEST(GameComponentEnumerationContractTest, CallbackExceptionsExposeTheMutationStage)
{
    EnumerationComponent first, second;
    GameComponentCollection items;
    items.Add(&first);
    auto cursor = items.GetEnumerator();
    items.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs&)
    {
        EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
        throw std::runtime_error("added callback refusal");
    };
    EXPECT_THROW(items.Add(&second), std::runtime_error);
    EXPECT_EQ(items.getCountProperty(), 2U);
    cursor = items.GetEnumerator();
    items.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&)
    {
        EXPECT_NO_THROW(cursor.Reset());
        ASSERT_TRUE(cursor.MoveNext());
        EXPECT_EQ(cursor.Current(), &first);
        throw std::runtime_error("clear callback refusal");
    };
    EXPECT_THROW(items.Clear(), std::runtime_error);
    EXPECT_EQ(items.getCountProperty(), 2U);
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_EQ(cursor.Current(), &second);
}

TEST(GameComponentEnumerationContractTest, CurrentRetainsAnOwnedComponentAfterRemoval)
{
    GameComponentCollection items;
    auto component = std::make_shared<EnumerationComponent>();
    std::weak_ptr<IGameComponent> weak = component;
    auto* const raw = component.get();
    items.Add(std::move(component));
    {
        auto cursor = items.GetEnumerator();
        ASSERT_TRUE(cursor.MoveNext());
        EXPECT_TRUE(items.Remove(raw));
        EXPECT_FALSE(weak.expired());
        EXPECT_EQ(cursor.Current(), raw);
        EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
        cursor.Dispose();
        EXPECT_FALSE(weak.expired());
    }
    EXPECT_TRUE(weak.expired());
}

TEST(GameComponentEnumerationContractTest, UnboundCursorRefusesTraversal)
{
    GameComponentCollection::Enumerator cursor;
    EXPECT_EQ(cursor.Current(), nullptr);
    EXPECT_THROW((void)cursor.getCurrentProperty(), System::InvalidOperationException);
    EXPECT_THROW((void)cursor.MoveNext(), System::NullReferenceException);
    EXPECT_THROW(cursor.Reset(), System::NullReferenceException);
    EXPECT_NO_THROW(cursor.Dispose());
}
