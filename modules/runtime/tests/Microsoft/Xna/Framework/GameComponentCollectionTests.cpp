// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <utility>
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"
#include "System/Object.hpp"

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

TEST(GameComponentCollectionTest, ARejectedSharedAddLeavesNoOwnershipBehind)
{
    bool alive = false;
    GameComponentCollection c;
    auto component = std::make_shared<TracedComponent>(alive);
    c.Add(component);

    // The same component twice is the raw overload's error and must stay so here.
    EXPECT_THROW(c.Add(component), std::invalid_argument);

    EXPECT_TRUE(c.Remove(component.get()));
    component.reset();
    EXPECT_FALSE(alive) << "the rejected add must not have left a second reference behind";
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
    EXPECT_THROW(c.Add(&comp), std::invalid_argument);
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
    c.Remove(&comp);
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
    c.Remove(&comp);
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
    EXPECT_THROW(c.RemoveAt(0), std::out_of_range);
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
    EXPECT_THROW((void)c[0], std::out_of_range);
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
    EXPECT_THROW(c.Insert(5, &comp), std::out_of_range);
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
