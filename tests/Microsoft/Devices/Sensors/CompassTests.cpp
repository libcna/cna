// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Compass;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

TEST(CompassTests, GetIsSupportedPropertyDoesNotCrash)
{
    EXPECT_FALSE(Compass::getIsSupportedProperty());
}

TEST(CompassTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Compass c; (void)c; });
}

TEST(CompassTests, GetStatePropertyReturnsNotSupported)
{
    const Compass c;
    EXPECT_EQ(c.getStateProperty(), SensorState::NotSupported);
}

TEST(CompassTests, StartThrowsSensorFailedException)
{
    Compass c;
    EXPECT_THROW(c.Start(), SensorFailedException);
}

TEST(CompassTests, StopAfterNoOpStartDoesNotCrash)
{
    Compass c;
    EXPECT_THROW(c.Start(), SensorFailedException);
    EXPECT_NO_THROW(c.Stop());
    EXPECT_EQ(c.getStateProperty(), SensorState::Disabled);
}

TEST(CompassTests, DisposeSucceedsAndSecondDisposeThrows)
{
    Compass c;
    EXPECT_NO_THROW(c.Dispose());
    EXPECT_THROW(c.Dispose(), System::ObjectDisposedException);
}

TEST(CompassTests, EleventhSimultaneousInstanceThrows)
{
    std::vector<std::unique_ptr<Compass>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Compass>());
    }

    EXPECT_THROW({ const Compass overflow; (void)overflow; }, SensorFailedException);
}

TEST(CompassTests, GetCurrentValuePropertyThrowsInvalidOperationException)
{
    const Compass c;
    EXPECT_THROW((void)c.getCurrentValueProperty(), System::InvalidOperationException);
}
