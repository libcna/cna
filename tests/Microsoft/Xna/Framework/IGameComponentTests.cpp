// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/IGameComponent.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    struct StubComponent : IGameComponent
    {
        int initCount = 0;
        void Initialize() override { ++initCount; }
    };
}

TEST(IGameComponentTest, InitializeIsCalled)
{
    StubComponent c;
    c.Initialize();
    EXPECT_EQ(c.initCount, 1);
}

TEST(IGameComponentTest, InitializeCanBeCalledMultipleTimes)
{
    StubComponent c;
    c.Initialize();
    c.Initialize();
    EXPECT_EQ(c.initCount, 2);
}
