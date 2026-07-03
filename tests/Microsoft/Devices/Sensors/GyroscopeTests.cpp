// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::GyroscopeReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
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

// Task P3-7: catches the dot-vs-colon GetTypeNameCPP naming-convention
// mistake that Task P2-4 fixed for Accelerometer; nothing previously
// verified this for Gyroscope.
TEST(GyroscopeTests, GetTypeName)
{
    const Gyroscope g;
    EXPECT_EQ(g.GetTypeName(), "Microsoft.Devices.Sensors.Gyroscope");
}

// Task P3-9: the eleventh-instance test above only proves the cap
// triggers; this proves instanceCount_ actually decrements on Dispose().
TEST(GyroscopeTests, DisposingOneOfTenAllowsAnotherConstruction)
{
    std::vector<std::unique_ptr<Gyroscope>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Gyroscope>());
    }

    instances.front()->Dispose();
    instances.erase(instances.begin());

    EXPECT_NO_THROW({ const Gyroscope eleventh; (void)eleventh; });
}

TEST(GyroscopeTests, GetCurrentValuePropertyThrowsWhenUnsupported)
{
    if (Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is supported on this platform; unsupported-path test not applicable.";
    }

    const Gyroscope g;
    EXPECT_THROW((void)g.getCurrentValueProperty(), System::InvalidOperationException);
}

TEST(GyroscopeTests, GetCurrentValuePropertyDoesNotThrowWhenSupported)
{
    if (!Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is not supported on this platform; supported-path test not applicable.";
    }

    const Gyroscope g;
    EXPECT_NO_THROW((void)g.getCurrentValueProperty());
}

// Task P3-6: CurrentValueChanged is the primary sensor-update event on
// Gyroscope. Actually observing it fire needs a real gyroscope delivering
// an SDL_EVENT_SENSOR_UPDATE, which this headless environment cannot
// produce. This test confirms subscribing doesn't crash and Start()/Stop()
// still behave correctly with a subscriber attached.
TEST(GyroscopeTests, CurrentValueChangedSubscriptionDoesNotThrow)
{
    Gyroscope g;
    bool invoked = false;
    g.CurrentValueChanged += [&invoked](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        invoked = true;
    };
    (void)invoked;

    if (Gyroscope::getIsSupportedProperty())
    {
        EXPECT_NO_THROW(g.Start());
        EXPECT_NO_THROW(g.Stop());
    }
    else
    {
        EXPECT_THROW(g.Start(), SensorFailedException);
    }
}
