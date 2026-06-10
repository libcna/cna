// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/IGraphicsDeviceManager.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    struct StubManager : IGraphicsDeviceManager
    {
        int createCount = 0;
        int beginCount = 0;
        int endCount = 0;
        bool beginResult = true;

        void CreateDevice() override { ++createCount; }
        bool BeginDraw() override { ++beginCount; return beginResult; }
        void EndDraw() override { ++endCount; }
    };
}

TEST(IGraphicsDeviceManagerTest, CreateDeviceIsCalled)
{
    StubManager m;
    m.CreateDevice();
    EXPECT_EQ(m.createCount, 1);
}

TEST(IGraphicsDeviceManagerTest, BeginDrawReturnsTrue)
{
    StubManager m;
    EXPECT_TRUE(m.BeginDraw());
    EXPECT_EQ(m.beginCount, 1);
}

TEST(IGraphicsDeviceManagerTest, BeginDrawReturnsFalse)
{
    StubManager m;
    m.beginResult = false;
    EXPECT_FALSE(m.BeginDraw());
}

TEST(IGraphicsDeviceManagerTest, EndDrawIsCalled)
{
    StubManager m;
    m.EndDraw();
    EXPECT_EQ(m.endCount, 1);
}
