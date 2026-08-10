// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"

using Microsoft::Xna::Framework::GameServiceContainer;

namespace
{
    struct IFakeService { virtual ~IFakeService() = default; };
    struct IOtherService { virtual ~IOtherService() = default; };
    struct FakeServiceImpl : IFakeService {};
}

TEST(GameServiceContainerTest, GetServiceAbsentReturnsNull)
{
    GameServiceContainer c;
    EXPECT_EQ(c.GetService<IFakeService>(), nullptr);
}

TEST(GameServiceContainerTest, AddThenGetReturnsService)
{
    GameServiceContainer c;
    FakeServiceImpl impl;
    c.AddService<IFakeService>(&impl);
    EXPECT_EQ(c.GetService<IFakeService>(), &impl);
}

TEST(GameServiceContainerTest, AddNullThrows)
{
    GameServiceContainer c;
    EXPECT_THROW(c.AddService<IFakeService>(nullptr), std::invalid_argument);
}

TEST(GameServiceContainerTest, AddDuplicateTypeThrows)
{
    GameServiceContainer c;
    FakeServiceImpl a, b;
    c.AddService<IFakeService>(&a);
    EXPECT_THROW(c.AddService<IFakeService>(&b), std::invalid_argument);
}

TEST(GameServiceContainerTest, RemoveServiceMakesGetReturnNull)
{
    GameServiceContainer c;
    FakeServiceImpl impl;
    c.AddService<IFakeService>(&impl);
    c.RemoveService<IFakeService>();
    EXPECT_EQ(c.GetService<IFakeService>(), nullptr);
}

TEST(GameServiceContainerTest, RemoveAbsentServiceIsNoOp)
{
    GameServiceContainer c;
    EXPECT_NO_THROW(c.RemoveService<IFakeService>());
}

TEST(GameServiceContainerTest, RemoveOnlyAffectsRequestedType)
{
    GameServiceContainer c;
    FakeServiceImpl fake;
    IOtherService other;
    c.AddService<IFakeService>(&fake);
    c.AddService<IOtherService>(&other);
    c.RemoveService<IFakeService>();
    EXPECT_EQ(c.GetService<IFakeService>(), nullptr);
    EXPECT_EQ(c.GetService<IOtherService>(), &other);
}

TEST(GameServiceContainerTest, AddAfterRemoveSucceeds)
{
    GameServiceContainer c;
    FakeServiceImpl a, b;
    c.AddService<IFakeService>(&a);
    c.RemoveService<IFakeService>();
    c.AddService<IFakeService>(&b);
    EXPECT_EQ(c.GetService<IFakeService>(), &b);
}
