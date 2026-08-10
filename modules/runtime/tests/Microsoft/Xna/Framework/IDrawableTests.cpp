// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/IDrawable.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    struct StubDrawable : IDrawable
    {
        int drawOrder = 0;
        bool visible = true;
        int drawCallCount = 0;
        System::EventHandler<System::EventArgs> drawOrderChanged;
        System::EventHandler<System::EventArgs> visibleChanged;

        [[nodiscard]] SharpRuntime::intcs getDrawOrderProperty() const override { return drawOrder; }
        [[nodiscard]] bool getVisibleProperty() const override { return visible; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDrawOrderChangedEvent() override { return drawOrderChanged; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getVisibleChangedEvent() override { return visibleChanged; }
        void Draw(const GameTime&) override { ++drawCallCount; }
    };
}

TEST(IDrawableTest, DrawOrderPropertyRoundTrip)
{
    StubDrawable d;
    d.drawOrder = 5;
    EXPECT_EQ(d.getDrawOrderProperty(), 5);
}

TEST(IDrawableTest, VisiblePropertyRoundTrip)
{
    StubDrawable d;
    d.visible = false;
    EXPECT_FALSE(d.getVisibleProperty());
}

TEST(IDrawableTest, DrawIsCalled)
{
    StubDrawable d;
    GameTime gt;
    d.Draw(gt);
    EXPECT_EQ(d.drawCallCount, 1);
}

TEST(IDrawableTest, DrawOrderChangedEventIsAccessible)
{
    StubDrawable d;
    int fired = 0;
    d.getDrawOrderChangedEvent() += [&](System::Object*, const System::EventArgs&) { ++fired; };
    d.drawOrderChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(fired, 1);
}

TEST(IDrawableTest, VisibleChangedEventIsAccessible)
{
    StubDrawable d;
    int fired = 0;
    d.getVisibleChangedEvent() += [&](System::Object*, const System::EventArgs&) { ++fired; };
    d.visibleChanged.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(fired, 1);
}
