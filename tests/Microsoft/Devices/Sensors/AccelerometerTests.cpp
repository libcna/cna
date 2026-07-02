// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::AccelerometerReadingEventArgs;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

// NOTE: Unlike Compass/Motion, the Accelerometer sensor can genuinely be
// supported on platforms/devices that expose SDL_SENSOR_ACCEL. These tests
// branch on the live getIsSupportedProperty() result so they pass both on
// typical headless CI/dev machines (no accelerometer hardware) and on real
// hardware.

TEST(AccelerometerTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = Accelerometer::getIsSupportedProperty();
    (void)supported;
}

TEST(AccelerometerTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Accelerometer a; (void)a; });
}

TEST(AccelerometerTests, GetStatePropertyReflectsSupportStatus)
{
    const Accelerometer a;
    if (Accelerometer::getIsSupportedProperty())
    {
        EXPECT_EQ(a.getStateProperty(), SensorState::Initializing);
    }
    else
    {
        EXPECT_EQ(a.getStateProperty(), SensorState::NotSupported);
    }
}

TEST(AccelerometerTests, StartOnUnsupportedPlatformThrows)
{
    if (Accelerometer::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Accelerometer is supported on this platform; unsupported-path test not applicable.";
    }

    Accelerometer a;
    EXPECT_THROW(a.Start(), AccelerometerFailedException);
}

TEST(AccelerometerTests, StopDoesNotCrash)
{
    Accelerometer a;

    if (Accelerometer::getIsSupportedProperty())
    {
        a.Start();
    }
    else
    {
        EXPECT_THROW(a.Start(), AccelerometerFailedException);
    }

    EXPECT_NO_THROW(a.Stop());
}

TEST(AccelerometerTests, DisposeSucceedsAndSecondDisposeThrows)
{
    Accelerometer a;
    EXPECT_NO_THROW(a.Dispose());
    EXPECT_THROW(a.Dispose(), System::ObjectDisposedException);
}

TEST(AccelerometerTests, EleventhSimultaneousInstanceThrows)
{
    std::vector<std::unique_ptr<Accelerometer>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Accelerometer>());
    }

    EXPECT_THROW({ const Accelerometer overflow; (void)overflow; }, SensorFailedException);
}

TEST(AccelerometerTests, GetTypeName)
{
    const Accelerometer a;
    EXPECT_EQ(a.GetTypeName(), "Microsoft.Devices.Sensors.Accelerometer");
}

// NOTE: Actually observing ReadingChanged/CurrentValueChanged fire together
// requires a real accelerometer delivering an SDL_EVENT_SENSOR_UPDATE
// through the SDL event pump, which this headless dev container cannot
// produce (same limitation as VibrateControllerTests' gamepad-conflict
// scenario). This test only confirms that subscribing to the legacy
// ReadingChanged event does not crash and that Start()/Stop() still behave
// correctly with a subscriber attached; both events are raised from the
// same ProcessSensorUpdateEvent() call site (see Accelerometer.cpp), so a
// real device that raises one raises the other.
TEST(AccelerometerTests, ReadingChangedSubscriptionDoesNotThrow)
{
    Accelerometer a;
    bool invoked = false;
    a.ReadingChanged += [&invoked](System::Object*, const AccelerometerReadingEventArgs&)
    {
        invoked = true;
    };
    (void)invoked;

    if (Accelerometer::getIsSupportedProperty())
    {
        EXPECT_NO_THROW(a.Start());
        EXPECT_NO_THROW(a.Stop());
    }
    else
    {
        EXPECT_THROW(a.Start(), AccelerometerFailedException);
    }
}
