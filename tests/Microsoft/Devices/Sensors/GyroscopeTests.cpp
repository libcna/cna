// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

// NOTE: Unlike Compass, the Gyroscope sensor can genuinely be supported on
// platforms/devices that expose SDL_SENSOR_GYRO. These tests branch on the
// live getIsSupportedProperty() result so they pass both on typical headless
// CI/dev machines (no gyroscope hardware) and on real hardware.

TEST(GyroscopeTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = Gyroscope::getIsSupportedProperty();
    (void)supported;
}

TEST(GyroscopeTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Gyroscope g; (void)g; });
}

TEST(GyroscopeTests, GetStatePropertyReflectsSupportStatus)
{
    const Gyroscope g;
    if (Gyroscope::getIsSupportedProperty())
    {
        EXPECT_EQ(g.getStateProperty(), SensorState::Initializing);
    }
    else
    {
        EXPECT_EQ(g.getStateProperty(), SensorState::NotSupported);
    }
}

TEST(GyroscopeTests, StartOnUnsupportedPlatformThrows)
{
    if (Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is supported on this platform; unsupported-path test not applicable.";
    }

    Gyroscope g;
    EXPECT_THROW(g.Start(), SensorFailedException);
}

TEST(GyroscopeTests, StopDoesNotCrash)
{
    Gyroscope g;

    if (Gyroscope::getIsSupportedProperty())
    {
        g.Start();
    }
    else
    {
        EXPECT_THROW(g.Start(), SensorFailedException);
    }

    EXPECT_NO_THROW(g.Stop());
}

TEST(GyroscopeTests, DisposeSucceedsAndSecondDisposeThrows)
{
    Gyroscope g;
    EXPECT_NO_THROW(g.Dispose());
    EXPECT_THROW(g.Dispose(), System::ObjectDisposedException);
}

TEST(GyroscopeTests, EleventhSimultaneousInstanceThrows)
{
    std::vector<std::unique_ptr<Gyroscope>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Gyroscope>());
    }

    EXPECT_THROW({ const Gyroscope overflow; (void)overflow; }, SensorFailedException);
}
