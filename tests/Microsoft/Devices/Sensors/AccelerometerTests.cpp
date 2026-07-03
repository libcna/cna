// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::AccelerometerReading;
using Microsoft::Devices::Sensors::AccelerometerReadingEventArgs;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
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

// Task P3-11: only Start()-after-Dispose() and Dispose()-after-Dispose()
// were tested before; Stop()-after-Dispose() is a distinct, separately
// guarded code path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(AccelerometerTests, StopAfterDisposeThrows)
{
    Accelerometer a;
    a.Dispose();
    EXPECT_THROW(a.Stop(), System::ObjectDisposedException);
}

// Task P3-11: DisposeSucceedsAndSecondDisposeThrows above disposes a
// never-started instance. This covers the separate started-then-disposed
// cleanup path (Dispose(bool) calls Stop() internally when started_).
TEST(AccelerometerTests, StartThenDisposeDoesNotCrash)
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

    EXPECT_NO_THROW(a.Dispose());
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

// Task P3-9: the eleventh-instance test above only proves the cap
// triggers; this proves instanceCount_ actually decrements on Dispose().
TEST(AccelerometerTests, DisposingOneOfTenAllowsAnotherConstruction)
{
    std::vector<std::unique_ptr<Accelerometer>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Accelerometer>());
    }

    instances.front()->Dispose();
    instances.erase(instances.begin());

    EXPECT_NO_THROW({ const Accelerometer eleventh; (void)eleventh; });
}

TEST(AccelerometerTests, GetTypeName)
{
    const Accelerometer a;
    EXPECT_EQ(a.GetTypeName(), "Microsoft.Devices.Sensors.Accelerometer");
}

TEST(AccelerometerTests, GetCurrentValuePropertyThrowsWhenUnsupported)
{
    if (Accelerometer::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Accelerometer is supported on this platform; unsupported-path test not applicable.";
    }

    const Accelerometer a;
    EXPECT_THROW((void)a.getCurrentValueProperty(), System::InvalidOperationException);
}

TEST(AccelerometerTests, GetCurrentValuePropertyDoesNotThrowWhenSupported)
{
    if (!Accelerometer::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Accelerometer is not supported on this platform; supported-path test not applicable.";
    }

    const Accelerometer a;
    EXPECT_NO_THROW((void)a.getCurrentValueProperty());
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

// Task P3-6: CurrentValueChanged is the primary, non-deprecated event
// (unlike the legacy ReadingChanged above). Same headless limitation:
// actually observing it fire needs a real accelerometer delivering an
// SDL_EVENT_SENSOR_UPDATE, which this environment cannot produce. This
// test confirms subscribing doesn't crash and Start()/Stop() still behave
// correctly with a subscriber attached.
TEST(AccelerometerTests, CurrentValueChangedSubscriptionDoesNotThrow)
{
    Accelerometer a;
    bool invoked = false;
    a.CurrentValueChanged += [&invoked](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
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
