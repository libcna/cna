// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameComponentCollection.hpp"
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"

using Microsoft::Xna::Framework::GameComponentCollection;
using Microsoft::Xna::Framework::GameComponentCollectionEventArgs;
using Microsoft::Xna::Framework::IGameComponent;

namespace
{
    struct StubComponent : IGameComponent
    {
        void Initialize() override {}
    };
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
