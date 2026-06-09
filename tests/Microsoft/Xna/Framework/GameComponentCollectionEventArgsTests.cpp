// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameComponentCollectionEventArgs.hpp"
#include "Microsoft/Xna/Framework/IGameComponent.hpp"

using Microsoft::Xna::Framework::GameComponentCollectionEventArgs;
using Microsoft::Xna::Framework::IGameComponent;

namespace
{
    struct StubComponent : IGameComponent
    {
        void Initialize() override {}
    };
}

TEST(GameComponentCollectionEventArgsTest, ConstructorStoresComponent)
{
    StubComponent comp;
    GameComponentCollectionEventArgs args(&comp);
    EXPECT_EQ(args.getGameComponentProperty(), &comp);
}

TEST(GameComponentCollectionEventArgsTest, ConstructorWithNullStoresNull)
{
    GameComponentCollectionEventArgs args(nullptr);
    EXPECT_EQ(args.getGameComponentProperty(), nullptr);
}

TEST(GameComponentCollectionEventArgsTest, TwoArgsWithDifferentComponentsAreDistinct)
{
    StubComponent a, b;
    GameComponentCollectionEventArgs argsA(&a);
    GameComponentCollectionEventArgs argsB(&b);
    EXPECT_NE(argsA.getGameComponentProperty(), argsB.getGameComponentProperty());
}
