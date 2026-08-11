// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GameServiceContainer.hpp"

using Microsoft::Xna::Framework::GameServiceContainer;

namespace
{
    struct IFakeService { virtual ~IFakeService() = default; };
    struct IOtherService { virtual ~IOtherService() = default; };
    struct FakeServiceImpl : IFakeService {};

    // A hostile multiple-inheritance shape mirroring GraphicsDeviceManager's own
    // (Object, IGraphicsDeviceService, IDisposable, IGraphicsDeviceManager) layout: three
    // non-empty polymorphic padding bases ahead of the interface under test, so that a
    // conversion from the concrete type to that interface requires a real, non-zero pointer
    // adjustment. Regression coverage for a GameServiceContainer defect that was suspected
    // (but ultimately ruled out -- see emscripten-mainloop-stack-spike/README.md) as the cause
    // of a real Emscripten Game::BeginDraw() crash.
    struct PaddingBaseA { virtual ~PaddingBaseA() = default; virtual int a() { return 1; } int padA[4]{}; };
    struct PaddingBaseB { virtual ~PaddingBaseB() = default; virtual int b() { return 2; } int padB[4]{}; };
    struct PaddingBaseC { virtual ~PaddingBaseC() = default; virtual int c() { return 3; } int padC[4]{}; };

    struct INonPrimaryService
    {
        virtual ~INonPrimaryService() = default;
        virtual int Value() const = 0;
    };

    struct HostileMultipleInheritanceImpl : PaddingBaseA, PaddingBaseB, PaddingBaseC, INonPrimaryService
    {
        int Value() const override { return 42; }
    };
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

// Non-primary-base interface lookup: registering and retrieving a service whose interface is
// NOT the first base (requiring a real, non-zero pointer adjustment between the concrete type
// and the interface, exactly like GraphicsDeviceManager -> IGraphicsDeviceManager) must return
// the correctly-adjusted pointer, not the concrete object's own address.
TEST(GameServiceContainerTest, GetServiceForNonPrimaryBaseReturnsCorrectlyAdjustedPointer)
{
    GameServiceContainer c;
    HostileMultipleInheritanceImpl impl;

    auto* expected = static_cast<INonPrimaryService*>(&impl);
    ASSERT_NE(static_cast<void*>(expected), static_cast<void*>(&impl))
        << "test setup invariant: INonPrimaryService must not be impl's primary base, or this "
           "test exercises no adjustment at all";

    c.AddService<INonPrimaryService>(&impl);
    INonPrimaryService* retrieved = c.GetService<INonPrimaryService>();

    EXPECT_EQ(retrieved, expected);
}

// The retrieved, adjusted pointer must be genuinely usable for virtual dispatch -- not merely
// numerically correct. This is the exact shape of Game::BeginDraw() calling through a pointer
// obtained from GameServiceContainer::GetService<IGraphicsDeviceManager>().
TEST(GameServiceContainerTest, VirtualCallThroughRetrievedNonPrimaryBaseServiceDispatchesCorrectly)
{
    GameServiceContainer c;
    HostileMultipleInheritanceImpl impl;
    c.AddService<INonPrimaryService>(&impl);

    INonPrimaryService* retrieved = c.GetService<INonPrimaryService>();
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->Value(), 42);
}

// A service registered under one interface must not be retrievable/aliased under a different,
// simultaneously-registered interface of the same concrete object -- guards against a
// type_index/typeid collision silently returning the wrong adjusted pointer.
TEST(GameServiceContainerTest, TwoInterfacesOnSameObjectRetrieveIndependentlyAdjustedPointers)
{
    GameServiceContainer c;
    HostileMultipleInheritanceImpl impl;
    c.AddService<INonPrimaryService>(&impl);
    c.AddService<PaddingBaseB>(&impl);

    INonPrimaryService* svc = c.GetService<INonPrimaryService>();
    PaddingBaseB* padB = c.GetService<PaddingBaseB>();

    ASSERT_NE(svc, nullptr);
    ASSERT_NE(padB, nullptr);
    EXPECT_NE(static_cast<void*>(svc), static_cast<void*>(padB));
    EXPECT_EQ(svc->Value(), 42);
    EXPECT_EQ(padB->b(), 2);
}

// Repeated GetService<T>() calls must be idempotent and keep returning the same pointer.
TEST(GameServiceContainerTest, RepeatedGetServiceReturnsSamePointerEveryTime)
{
    GameServiceContainer c;
    FakeServiceImpl impl;
    c.AddService<IFakeService>(&impl);

    IFakeService* first = c.GetService<IFakeService>();
    IFakeService* second = c.GetService<IFakeService>();
    IFakeService* third = c.GetService<IFakeService>();

    EXPECT_EQ(first, second);
    EXPECT_EQ(second, third);
}

// The container itself does not own services; destroying a registered object without calling
// RemoveService first leaves a dangling entry -- this documents that existing (correct)
// contract rather than a container defect: callers (GraphicsDeviceManager::unregisterServices())
// are responsible for removing their own registration before they are destroyed.
TEST(GameServiceContainerTest, ContainerDoesNotOwnRegisteredServiceLifetime)
{
    GameServiceContainer c;
    {
        FakeServiceImpl impl;
        c.AddService<IFakeService>(&impl);
        EXPECT_EQ(c.GetService<IFakeService>(), &impl);
        c.RemoveService<IFakeService>();
    }
    // impl is now destroyed; the container must not retain a lookup for it.
    EXPECT_EQ(c.GetService<IFakeService>(), nullptr);
}
