// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/IUpdateable.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    struct StubUpdateable : IUpdateable
    {
        bool enabled = true;
        int updateOrder = 0;
        int updateCount = 0;
        System::EventHandler<System::EventArgs> enabledChanged;
        System::EventHandler<System::EventArgs> updateOrderChanged;

        [[nodiscard]] bool getEnabledProperty() const override { return enabled; }
        [[nodiscard]] SharpRuntime::intcs getUpdateOrderProperty() const override { return updateOrder; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getEnabledChangedEvent() override { return enabledChanged; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getUpdateOrderChangedEvent() override { return updateOrderChanged; }
        void Update(GameTime&) override { ++updateCount; }
    };
}

TEST(IUpdateableTest, EnabledPropertyRoundTrip)
{
    StubUpdateable u;
    u.enabled = false;
    EXPECT_FALSE(u.getEnabledProperty());
}

TEST(IUpdateableTest, UpdateOrderPropertyRoundTrip)
{
    StubUpdateable u;
    u.updateOrder = 42;
    EXPECT_EQ(u.getUpdateOrderProperty(), 42);
}

TEST(IUpdateableTest, UpdateIsCalled)
{
    StubUpdateable u;
    GameTime gt;
    u.Update(gt);
    EXPECT_EQ(u.updateCount, 1);
}

TEST(IUpdateableTest, EnabledChangedEventIsAccessible)
{
    StubUpdateable u;
    int fired = 0;
    u.getEnabledChangedEvent() += [&](System::Object*, const System::EventArgs&) { ++fired; };
    u.enabledChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(fired, 1);
}

TEST(IUpdateableTest, UpdateOrderChangedEventIsAccessible)
{
    StubUpdateable u;
    int fired = 0;
    u.getUpdateOrderChangedEvent() += [&](System::Object*, const System::EventArgs&) { ++fired; };
    u.updateOrderChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(fired, 1);
}
