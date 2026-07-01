// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Motion;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

TEST(MotionTests, GetIsSupportedPropertyIsFalse)
{
    EXPECT_FALSE(Motion::getIsSupportedProperty());
}

TEST(MotionTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Motion m; (void)m; });
}

TEST(MotionTests, GetStatePropertyReturnsNotSupported)
{
    const Motion m;
    EXPECT_EQ(m.getStateProperty(), SensorState::NotSupported);
}

TEST(MotionTests, StartThrowsSensorFailedException)
{
    Motion m;
    EXPECT_THROW(m.Start(), SensorFailedException);
}

TEST(MotionTests, StopDoesNotCrash)
{
    Motion m;
    EXPECT_THROW(m.Start(), SensorFailedException);
    EXPECT_NO_THROW(m.Stop());
    EXPECT_EQ(m.getStateProperty(), SensorState::Disabled);
}

TEST(MotionTests, DisposeSucceedsAndSecondDisposeThrows)
{
    Motion m;
    EXPECT_NO_THROW(m.Dispose());
    EXPECT_THROW(m.Dispose(), System::ObjectDisposedException);
}

TEST(MotionTests, EleventhSimultaneousInstanceThrows)
{
    std::vector<std::unique_ptr<Motion>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Motion>());
    }

    EXPECT_THROW({ const Motion overflow; (void)overflow; }, SensorFailedException);
}
