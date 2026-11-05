// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/Object.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::GameComponentCollection;
using Microsoft::Xna::Framework::GameComponentCollectionEventArgs;
using Microsoft::Xna::Framework::IGameComponent;

namespace
{
    struct StubComponent : IGameComponent
    {
        void Initialize() override {}
    };

    /// Reports its own destruction, so a test can state when the collection let go.
    struct TracedComponent final : IGameComponent
    {
        explicit TracedComponent(bool& aliveFlag) : alive_(aliveFlag) { alive_ = true; }
        ~TracedComponent() override { alive_ = false; }
        void Initialize() override {}

    private:
        bool& alive_;
    };
}

// XNA's collection is a Collection<IGameComponent> and holds a strong reference, so a component
// stays alive for exactly as long as it is registered and a game that drops its own last
// reference leaves a live component rather than a dangling one. SAMPLE-065 needs that: the
// original NinjAcademy menu abandons a loading screen -- and with it a GameplayScreen that has
// already registered its components -- on every frame it is still transitioning off.
TEST(GameComponentCollectionTest, AnAddedSharedComponentOutlivesTheCallersOwnReference)
{
    bool alive = false;
    GameComponentCollection c;
    {
        auto component = std::make_shared<TracedComponent>(alive);
        c.Add(component);
        ASSERT_TRUE(alive);
    }

    // The caller's reference is gone; the registration still holds one.
    EXPECT_TRUE(alive);
    EXPECT_EQ(c.getCountProperty(), 1u);
}

TEST(GameComponentCollectionTest, RemovingASharedComponentReleasesItAfterTheEvent)
{
    bool alive = false;
    GameComponentCollection c;
    auto component = std::make_shared<TracedComponent>(alive);
    IGameComponent* const raw = component.get();
    c.Add(std::move(component));

    bool aliveInsideHandler = false;
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&)
    {
        aliveInsideHandler = alive;
    };

    EXPECT_TRUE(c.Remove(raw));
    EXPECT_TRUE(aliveInsideHandler) << "a ComponentRemoved handler must still see a live component";
    EXPECT_FALSE(alive);
}

TEST(GameComponentCollectionTest, ClearReleasesEverySharedComponent)
{
    bool first = false;
    bool second = false;
    GameComponentCollection c;
    c.Add(std::make_shared<TracedComponent>(first));
    c.Add(std::make_shared<TracedComponent>(second));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    c.Clear();
    EXPECT_FALSE(first);
    EXPECT_FALSE(second);
    EXPECT_EQ(c.getCountProperty(), 0u);
}

TEST(GameComponentCollectionTest, RemovingInsideAddedCallbackKeepsTheComponentAliveUntilDispatchReturns)
{
    bool alive = false;
    GameComponentCollection c;
    c.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs& args)
    {
        EXPECT_TRUE(alive);
        EXPECT_TRUE(c.Remove(args.getGameComponentProperty()));
        EXPECT_TRUE(alive);
    };
    c.Add(std::make_shared<TracedComponent>(alive));
    EXPECT_FALSE(alive);
    EXPECT_EQ(c.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, ThrowingRemovedCallbackStillReleasesTheRemovedOwnership)
{
    bool alive = false;
    GameComponentCollection c;
    auto component = std::make_shared<TracedComponent>(alive);
    IGameComponent* raw = component.get();
    c.Add(std::move(component));
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&)
    {
        EXPECT_TRUE(alive);
        throw std::runtime_error("handler");
    };
    EXPECT_THROW((void)c.Remove(raw), std::runtime_error);
    EXPECT_EQ(c.getCountProperty(), 0U);
    EXPECT_FALSE(alive);
}

TEST(GameComponentCollectionTest, ReaddingDuringRemovedCallbackPreservesTheNewRegistrationOwnership)
{
    bool alive = false;
    GameComponentCollection c;
    auto component = std::make_shared<TracedComponent>(alive);
    std::weak_ptr<TracedComponent> weak = component;
    IGameComponent* raw = component.get();
    c.Add(std::move(component));
    bool readded = false;
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&)
    {
        if (!readded)
        {
            readded = true;
            auto retained = weak.lock();
            ASSERT_NE(retained, nullptr);
            c.Add(std::move(retained));
        }
    };
    EXPECT_TRUE(c.Remove(raw));
    EXPECT_EQ(c.getCountProperty(), 1U);
    EXPECT_TRUE(alive);
    EXPECT_FALSE(weak.expired());
    c.Clear(); // Handler is guarded; pre-fix stale pointer is never dereferenced.
    EXPECT_FALSE(alive);
}

TEST(GameComponentCollectionTest, ClearUsesXnasLiveIndexLoopWhenACallbackRemovesAnEntry)
{
    GameComponentCollection c;
    StubComponent first, second, third;
    c.Add(&first); c.Add(&second); c.Add(&third);
    std::vector<IGameComponent*> trace;
    bool removed = false;
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs& args)
    {
        trace.push_back(args.getGameComponentProperty());
        if (!removed && args.getGameComponentProperty() == &first)
        {
            removed = true;
            EXPECT_TRUE(c.Remove(&first));
        }
    };
    c.Clear();
    EXPECT_EQ(trace, (std::vector<IGameComponent*>{&first, &first, &third}));
    EXPECT_EQ(c.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, ClearRetainsAnOwnedEventArgumentAcrossNestedRemoval)
{
    bool alive = false, removed = false;
    GameComponentCollection c;
    c.Add(std::make_shared<TracedComponent>(alive));
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs& args)
    {
        if (!removed)
        {
            removed = true;
            EXPECT_TRUE(c.Remove(args.getGameComponentProperty()));
            EXPECT_TRUE(alive);
        }
    };
    c.Clear();
    EXPECT_FALSE(alive);
    EXPECT_EQ(c.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, ARejectedSharedAddLeavesNoOwnershipBehind)
{
    bool alive = false;
    GameComponentCollection c;
    auto component = std::make_shared<TracedComponent>(alive);
    c.Add(component);

    // The same component twice is the raw overload's error and must stay so here.
    EXPECT_THROW(c.Add(component), System::ArgumentException);

    EXPECT_TRUE(c.Remove(component.get()));
    component.reset();
    EXPECT_FALSE(alive) << "the rejected add must not have left a second reference behind";
}

TEST(GameComponentCollectionTest, ARejectedDuplicateSharedAddKeepsTheFirstAddsOwnership)
{
    // The rejected second add used to release the reference the first add took, leaving the
    // collection listing a component nothing kept alive.
    bool alive = false;
    GameComponentCollection c;
    {
        auto component = std::make_shared<TracedComponent>(alive);
        c.Add(component);
        EXPECT_THROW(c.Add(component), System::ArgumentException);
    }
    EXPECT_TRUE(alive) << "the collection still lists the component, so it must still own it";
    EXPECT_EQ(c.getCountProperty(), 1u);
}

TEST(GameComponentCollectionTest, AThrowingComponentAddedHandlerLeavesTheSharedComponentOwned)
{
    // XNA inserts before raising ComponentAdded, so a handler that throws leaves the component in
    // the collection -- and the collection must still own what it lists.
    bool alive = false;
    GameComponentCollection c;
    c.ComponentAdded += [](System::Object*, const GameComponentCollectionEventArgs&)
    {
        throw std::runtime_error("handler");
    };
    EXPECT_THROW(c.Add(std::make_shared<TracedComponent>(alive)), std::runtime_error);
    EXPECT_EQ(c.getCountProperty(), 1u);
    EXPECT_TRUE(alive);
}

TEST(GameComponentCollectionTest, TheCollectionReleasesWhatItStillOwnsWhenItGoesAway)
{
    bool alive = false;
    {
        GameComponentCollection c;
        c.Add(std::make_shared<TracedComponent>(alive));
        ASSERT_TRUE(alive);
    }
    EXPECT_FALSE(alive);
}

TEST(GameComponentCollectionTest, DefaultCountIsZero)
{
    GameComponentCollection c;
    EXPECT_EQ(c.getCountProperty(), 0u);
}

TEST(GameComponentCollectionTest, XnaInvalidInsertIndexPrecedesDuplicateValidation)
{
    GameComponentCollection c;
    StubComponent first, second;
    c.Add(&first);
    EXPECT_THROW(c.Insert(2U, &first), System::ArgumentOutOfRangeException);
    EXPECT_THROW(c.Insert(2U, &second), System::ArgumentOutOfRangeException);
    EXPECT_THROW(c.Insert(0U, &first), System::ArgumentException);
    ASSERT_EQ(c.getCountProperty(), 1U);
    EXPECT_EQ(c[0], &first);
}

TEST(GameComponentCollectionTest, XnaClearIncludesNullWhileIndividualNullChangesRemainSilent)
{
    GameComponentCollection c;
    StubComponent first, second;
    c.Add(&first);
    int added = 0, removed = 0;
    c.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++added; };
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++removed; };
    c.Add(nullptr);
    EXPECT_TRUE(c.Remove(nullptr));
    EXPECT_EQ(added, 0);
    EXPECT_EQ(removed, 0);
    c.Add(nullptr);
    c.Add(&second);
    std::vector<IGameComponent*> order;
    c.ComponentRemoved += [&](System::Object* sender, const GameComponentCollectionEventArgs& args)
    {
        EXPECT_EQ(sender, &c);
        EXPECT_EQ(c.getCountProperty(), 3U);
        order.push_back(args.getGameComponentProperty());
    };
    c.Clear();
    EXPECT_EQ(order, (std::vector<IGameComponent*>{&first, nullptr, &second}));
    EXPECT_EQ(removed, 3);
    EXPECT_EQ(c.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, AddIncreasesCount)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    EXPECT_EQ(c.getCountProperty(), 1u);
}

TEST(GameComponentCollectionTest, AddDuplicateThrows)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    EXPECT_THROW(c.Add(&comp), System::ArgumentException);
}

TEST(GameComponentCollectionTest, AddFiresComponentAddedEvent)
{
    GameComponentCollection c;
    StubComponent comp;
    IGameComponent* received = nullptr;
    c.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs& a)
    {
        received = a.getGameComponentProperty();
    };
    c.Add(&comp);
    EXPECT_EQ(received, &comp);
}

TEST(GameComponentCollectionTest, RemoveDecreasesCount)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    EXPECT_TRUE(c.Remove(&comp));
    EXPECT_EQ(c.getCountProperty(), 0u);
}

TEST(GameComponentCollectionTest, RemoveFiresComponentRemovedEvent)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    IGameComponent* received = nullptr;
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs& a)
    {
        received = a.getGameComponentProperty();
    };
    EXPECT_TRUE(c.Remove(&comp));
    EXPECT_EQ(received, &comp);
}

TEST(GameComponentCollectionTest, RemoveAbsentReturnsFalse)
{
    GameComponentCollection c;
    StubComponent comp;
    EXPECT_FALSE(c.Remove(&comp));
}

TEST(GameComponentCollectionTest, RemoveAtDecreasesCount)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    c.RemoveAt(0);
    EXPECT_EQ(c.getCountProperty(), 0u);
}

TEST(GameComponentCollectionTest, RemoveAtOutOfRangeThrows)
{
    GameComponentCollection c;
    EXPECT_THROW(c.RemoveAt(0), System::ArgumentOutOfRangeException);
}

TEST(GameComponentCollectionTest, ClearRemovesAll)
{
    GameComponentCollection c;
    StubComponent a, b;
    c.Add(&a);
    c.Add(&b);
    c.Clear();
    EXPECT_EQ(c.getCountProperty(), 0u);
}

TEST(GameComponentCollectionTest, ClearFiresRemovedEventsForEach)
{
    GameComponentCollection c;
    StubComponent a, b;
    c.Add(&a);
    c.Add(&b);
    int fired = 0;
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++fired; };
    c.Clear();
    EXPECT_EQ(fired, 2);
}

TEST(GameComponentCollectionTest, ContainsTrueAfterAdd)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    EXPECT_TRUE(c.Contains(&comp));
}

TEST(GameComponentCollectionTest, ContainsFalseWhenAbsent)
{
    GameComponentCollection c;
    StubComponent comp;
    EXPECT_FALSE(c.Contains(&comp));
}

TEST(GameComponentCollectionTest, IndexOfReturnsCorrectIndex)
{
    GameComponentCollection c;
    StubComponent a, b;
    c.Add(&a);
    c.Add(&b);
    EXPECT_EQ(c.IndexOf(&a), 0);
    EXPECT_EQ(c.IndexOf(&b), 1);
}

TEST(GameComponentCollectionTest, IndexOfAbsentReturnsMinusOne)
{
    GameComponentCollection c;
    StubComponent comp;
    EXPECT_EQ(c.IndexOf(&comp), -1);
}

TEST(GameComponentCollectionTest, OperatorIndexReturnsComponent)
{
    GameComponentCollection c;
    StubComponent comp;
    c.Add(&comp);
    EXPECT_EQ(c[0], &comp);
}

TEST(GameComponentCollectionTest, OperatorIndexOutOfRangeThrows)
{
    GameComponentCollection c;
    EXPECT_THROW((void)c[0], System::ArgumentOutOfRangeException);
}

TEST(GameComponentCollectionTest, InsertAtIndexFiresAdded)
{
    GameComponentCollection c;
    StubComponent a, b;
    c.Add(&a);
    IGameComponent* received = nullptr;
    c.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs& e)
    {
        received = e.getGameComponentProperty();
    };
    c.Insert(0, &b);
    EXPECT_EQ(received, &b);
    EXPECT_EQ(c[0], &b);
    EXPECT_EQ(c[1], &a);
}

TEST(GameComponentCollectionTest, InsertOutOfRangeThrows)
{
    GameComponentCollection c;
    StubComponent comp;
    EXPECT_THROW(c.Insert(5, &comp), System::ArgumentOutOfRangeException);
}

TEST(GameComponentCollectionTest, RangeForIteratesAllComponents)
{
    GameComponentCollection c;
    StubComponent a, b;
    c.Add(&a);
    c.Add(&b);
    int count = 0;
    for (IGameComponent* p : c)
    {
        (void)p;
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST(GameComponentCollectionTest, FailedMutationsPreserveOrderAndRaiseNoEvents)
{
    GameComponentCollection collection;
    StubComponent first, second, absent;
    collection.Add(&first);
    collection.Add(&second);
    int events = 0;
    collection.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++events; };
    collection.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++events; };
    const auto unchanged = [&]
    {
        ASSERT_EQ(collection.getCountProperty(), 2U);
        EXPECT_EQ(collection[0], &first);
        EXPECT_EQ(collection[1], &second);
        EXPECT_EQ(events, 0);
    };

    EXPECT_THROW(collection.Add(&first), System::ArgumentException);
    unchanged();
    EXPECT_THROW(collection.Insert(1, &second), System::ArgumentException);
    unchanged();
    EXPECT_THROW(collection.Insert(3, &absent), System::ArgumentOutOfRangeException);
    unchanged();
    EXPECT_THROW(collection.RemoveAt(2), System::ArgumentOutOfRangeException);
    unchanged();
    EXPECT_FALSE(collection.Remove(&absent));
    unchanged();
    collection.Insert(1, &absent);
    ASSERT_EQ(collection.getCountProperty(), 3U);
    EXPECT_EQ(collection[0], &first);
    EXPECT_EQ(collection[1], &absent);
    EXPECT_EQ(collection[2], &second);
    EXPECT_EQ(events, 1);
}

TEST(GameComponentCollectionTest, CallbacksSeeInsertedAndRemovedStateButClearNotifiesBeforeErasing)
{
    GameComponentCollection collection;
    StubComponent first, second;
    int added = 0;
    collection.ComponentAdded += [&](System::Object* sender, const GameComponentCollectionEventArgs& args)
    {
        EXPECT_EQ(sender, &collection);
        EXPECT_TRUE(collection.Contains(args.getGameComponentProperty()));
        EXPECT_EQ(collection.getCountProperty(), static_cast<std::size_t>(++added));
    };
    collection.Add(&first);
    collection.Add(&second);
    bool clearing = false;
    int removed = 0;
    collection.ComponentRemoved += [&](System::Object* sender, const GameComponentCollectionEventArgs& args)
    {
        EXPECT_EQ(sender, &collection);
        EXPECT_EQ(collection.Contains(args.getGameComponentProperty()), clearing);
        EXPECT_EQ(collection.getCountProperty(), 1U);
        EXPECT_EQ(args.getGameComponentProperty(), clearing ? &second : &first);
        ++removed;
    };
    EXPECT_TRUE(collection.Remove(&first));
    clearing = true;
    collection.Clear();
    EXPECT_EQ(removed, 2);
    EXPECT_EQ(collection.getCountProperty(), 0U);
    collection.Clear();
    EXPECT_EQ(removed, 2);
}

TEST(GameComponentCollectionTest, ThrowingClearCallbackPreservesMembershipAndCanBeRetried)
{
    GameComponentCollection collection;
    StubComponent first, second;
    collection.Add(&first);
    collection.Add(&second);
    bool fail = true;
    std::vector<IGameComponent*> removed;
    collection.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs& args)
    {
        removed.push_back(args.getGameComponentProperty());
        if (fail) throw std::runtime_error("clear callback");
    };

    EXPECT_THROW(collection.Clear(), std::runtime_error);
    ASSERT_EQ(collection.getCountProperty(), 2U);
    EXPECT_EQ(collection[0], &first);
    EXPECT_EQ(collection[1], &second);
    EXPECT_EQ(removed, (std::vector<IGameComponent*>{&first}));
    fail = false;
    removed.clear();
    EXPECT_NO_THROW(collection.Clear());
    EXPECT_EQ(removed, (std::vector<IGameComponent*>{&first, &second}));
    EXPECT_EQ(collection.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, Test010RemovedDuringRejectedAddReleasesOwnershipAndCanRetry)
{
    bool alive = false;
    bool survivorAlive = false;
    GameComponentCollection items;
    auto survivor = std::make_shared<TracedComponent>(survivorAlive);
    items.Add(survivor);
    bool refuse = true;
    items.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs& args)
    {
        if (!refuse) return;
        EXPECT_TRUE(alive);
        EXPECT_TRUE(items.Remove(args.getGameComponentProperty()));
        EXPECT_TRUE(alive) << "in-flight Add must retain its argument until callback unwinds";
        throw std::runtime_error("removed during rejected add");
    };
    try
    {
        items.Add(std::make_shared<TracedComponent>(alive));
        ADD_FAILURE() << "callback refusal was swallowed";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "removed during rejected add");
    }
    EXPECT_FALSE(alive);
    ASSERT_EQ(items.getCountProperty(), 1U);
    EXPECT_EQ(items[0], survivor.get());
    EXPECT_TRUE(survivorAlive);
    refuse = false;
    items.Add(std::make_shared<TracedComponent>(alive));
    EXPECT_TRUE(alive);
    EXPECT_EQ(items.getCountProperty(), 2U);
    items.Clear();
    EXPECT_FALSE(alive);
    EXPECT_TRUE(survivorAlive);
    EXPECT_EQ(items.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, ThrowingRemoveAtCallbackLeavesOnlyTheSurvivorAndInvalidatesCursor)
{
    // XNA Refresh RemoveItem IL_0008..001e erases before notifying; no rollback.
    StubComponent first, second;
    GameComponentCollection c;
    c.Add(&first);
    c.Add(&second);
    auto cursor = c.GetEnumerator();
    ASSERT_TRUE(cursor.MoveNext());
    int calls = 0;
    c.ComponentRemoved += [&](System::Object* sender, const GameComponentCollectionEventArgs& args) {
        ++calls;
        EXPECT_EQ(sender, &c);
        EXPECT_EQ(args.getGameComponentProperty(), &first);
        ASSERT_EQ(c.getCountProperty(), 1U);
        EXPECT_EQ(c[0], &second);
        throw std::runtime_error("remove refused");
    };
    EXPECT_THROW(c.RemoveAt(0), std::runtime_error);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(c.getCountProperty(), 1U);
    EXPECT_EQ(c[0], &second);
    EXPECT_FALSE(c.Contains(&first));
    EXPECT_THROW((void)cursor.MoveNext(), System::InvalidOperationException);
    EXPECT_FALSE(c.Remove(&first));
    EXPECT_EQ(calls, 1);
}

TEST(GameComponentCollectionTest, ClearVisitsComponentsAppendedByTheRemovedCallback)
{
    // XNA Refresh ClearItems IL_0004..0024 rereads Count on each iteration.
    StubComponent first, appended;
    GameComponentCollection c;
    c.Add(&first);
    std::vector<IGameComponent*> removed;
    c.ComponentRemoved += [&](System::Object* sender, const GameComponentCollectionEventArgs& args) {
        EXPECT_EQ(sender, &c);
        removed.push_back(args.getGameComponentProperty());
        if (args.getGameComponentProperty() == &first) c.Add(&appended);
    };
    c.Clear();
    EXPECT_EQ(removed, (std::vector<IGameComponent*>{&first, &appended}));
    EXPECT_EQ(c.getCountProperty(), 0U);
}

TEST(GameComponentCollectionTest, DuplicateNullRefusalPreservesCursorAndRaisesNoIndividualEvents)
{
    // Refresh InsertItem IL_0000..0014 treats null as an ordinary duplicate;
    // IL_001d..002c suppresses the event for the first successful null insertion.
    GameComponentCollection c;
    int added = 0, removed = 0;
    c.ComponentAdded += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++added; };
    c.ComponentRemoved += [&](System::Object*, const GameComponentCollectionEventArgs&) { ++removed; };
    c.Add(nullptr);
    auto cursor = c.GetEnumerator();
    EXPECT_THROW(c.Insert(0, nullptr), System::ArgumentException);
    EXPECT_THROW(c.Add(std::shared_ptr<IGameComponent>()), System::ArgumentException);
    EXPECT_EQ(c.getCountProperty(), 1U);
    EXPECT_EQ(c[0], nullptr);
    ASSERT_TRUE(cursor.MoveNext());
    EXPECT_EQ(cursor.Current(), nullptr);
    EXPECT_FALSE(cursor.MoveNext());
    EXPECT_TRUE(c.Remove(nullptr));
    EXPECT_EQ(c.getCountProperty(), 0U);
    EXPECT_EQ(added, 0);
    EXPECT_EQ(removed, 0);
}
