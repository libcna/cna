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
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::AccelerometerReading;
using Microsoft::Devices::Sensors::AccelerometerReadingEventArgs;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Vector3;

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

// Task P4-3: exercises the real CurrentValueChanged dispatch path via the
// Task P4-2 synthetic-injection hooks (this environment has no real
// hardware to deliver a genuine SDL_EVENT_SENSOR_UPDATE, so every
// preceding "subscription" test in this file only proved subscribing
// doesn't crash, never that the event actually carries correct data).
// SetStartedForTesting(true) bypasses the real Start()'s hardware
// requirement; getCurrentValueProperty() is deliberately not asserted
// here since it independently throws when the platform is genuinely
// unsupported (Task P3-1), orthogonal to what this test verifies.
TEST(AccelerometerTests, CurrentValueChangedReceivesExpectedReading)
{
    Accelerometer a;
    a.SetStartedForTesting(true);

    bool invoked = false;
    AccelerometerReading receivedReading;
    a.CurrentValueChanged += [&invoked, &receivedReading](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args)
    {
        invoked = true;
        receivedReading = args.getSensorReadingProperty();
    };

    constexpr float StandardGravity = 9.80665f;
    const float rawX = StandardGravity;
    const float rawY = 0.0f;
    const float rawZ = StandardGravity * 0.5f;

    a.InjectSyntheticSensorUpdate(rawX, rawY, rawZ);

    ASSERT_TRUE(invoked);
    const Vector3 expectedAcceleration(rawX / StandardGravity, rawY / StandardGravity, rawZ / StandardGravity);
    EXPECT_EQ(receivedReading.getAccelerationProperty(), expectedAcceleration);
}

// Task P4-7: Timestamp is now the real wall-clock time of the call
// (System::DateTimeOffset::getUtcNowProperty()), not a bogus near-year-1
// value derived from SDL_GetTicksNS(). Asserts "close to now" with a
// generous tolerance rather than an exact value, since wall-clock time is
// inherently non-deterministic between the call and the assertion.
TEST(AccelerometerTests, CurrentValueChangedReceivesWallClockTimestamp)
{
    Accelerometer a;
    a.SetStartedForTesting(true);

    System::DateTimeOffset receivedTimestamp;
    a.CurrentValueChanged += [&receivedTimestamp](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args)
    {
        receivedTimestamp = args.getSensorReadingProperty().getTimestampProperty();
    };

    const System::DateTimeOffset beforeInjection = System::DateTimeOffset::getUtcNowProperty();
    a.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);
    const System::DateTimeOffset afterInjection = System::DateTimeOffset::getUtcNowProperty();

    EXPECT_GE(receivedTimestamp, beforeInjection);
    EXPECT_LE(receivedTimestamp, afterInjection);
}

// Task P4-4: legacy ReadingChanged must receive the same converted
// X/Y/Z as CurrentValueChanged's AccelerometerReading.Acceleration, since
// both are raised from the same DispatchSensorReading() call site (see
// Accelerometer.cpp) — this finally verifies that with real data instead
// of only confirming subscription doesn't crash.
TEST(AccelerometerTests, ReadingChangedReceivesMatchingXYZ)
{
    Accelerometer a;
    a.SetStartedForTesting(true);

    bool invoked = false;
    AccelerometerReadingEventArgs receivedArgs;
    a.ReadingChanged += [&invoked, &receivedArgs](System::Object*, const AccelerometerReadingEventArgs& args)
    {
        invoked = true;
        receivedArgs = args;
    };

    constexpr float StandardGravity = 9.80665f;
    const float rawX = StandardGravity;
    const float rawY = -StandardGravity * 0.25f;
    const float rawZ = StandardGravity * 0.75f;

    a.InjectSyntheticSensorUpdate(rawX, rawY, rawZ);

    ASSERT_TRUE(invoked);
    EXPECT_DOUBLE_EQ(receivedArgs.getXProperty(), static_cast<double>(rawX / StandardGravity));
    EXPECT_DOUBLE_EQ(receivedArgs.getYProperty(), static_cast<double>(rawY / StandardGravity));
    EXPECT_DOUBLE_EQ(receivedArgs.getZProperty(), static_cast<double>(rawZ / StandardGravity));
}

// Task P4-6: confirms Stop() actually disables further synthetic-event
// dispatch (not just that Start() throws headless — StopDoesNotCrash
// above already covers that). started_ is cleared by the real Stop()
// regardless of how it was set, so this also exercises Stop()'s effect on
// a SetStartedForTesting()-simulated instance.
TEST(AccelerometerTests, StopPreventsSubsequentSyntheticEventFromDispatching)
{
    Accelerometer a;
    a.SetStartedForTesting(true);

    int invokedCount = 0;
    Vector3 lastAcceleration;
    a.CurrentValueChanged += [&invokedCount, &lastAcceleration](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args)
    {
        ++invokedCount;
        lastAcceleration = args.getSensorReadingProperty().getAccelerationProperty();
    };

    constexpr float StandardGravity = 9.80665f;
    a.InjectSyntheticSensorUpdate(StandardGravity, 0.0f, 0.0f);
    ASSERT_EQ(invokedCount, 1);
    const Vector3 accelerationBeforeStop = lastAcceleration;

    a.Stop();

    a.InjectSyntheticSensorUpdate(0.0f, StandardGravity, 0.0f);
    EXPECT_EQ(invokedCount, 1);
    EXPECT_EQ(lastAcceleration, accelerationBeforeStop);
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
