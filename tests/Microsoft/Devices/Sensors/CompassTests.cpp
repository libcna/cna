// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Compass;
using Microsoft::Devices::Sensors::CompassReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
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

// Task P3-9: the eleventh-instance test above only proves the cap
// triggers; this proves instanceCount_ actually decrements on Dispose().
TEST(CompassTests, DisposingOneOfTenAllowsAnotherConstruction)
{
    std::vector<std::unique_ptr<Compass>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Compass>());
    }

    instances.front()->Dispose();
    instances.erase(instances.begin());

    EXPECT_NO_THROW({ const Compass eleventh; (void)eleventh; });
}

TEST(CompassTests, GetCurrentValuePropertyThrowsInvalidOperationException)
{
    const Compass c;
    EXPECT_THROW((void)c.getCurrentValueProperty(), System::InvalidOperationException);
}

// Task P3-7: catches the dot-vs-colon GetTypeNameCPP naming-convention
// mistake that Task P2-4 fixed for Accelerometer; nothing previously
// verified this for Compass.
TEST(CompassTests, GetTypeName)
{
    const Compass c;
    EXPECT_EQ(c.GetTypeName(), "Microsoft.Devices.Sensors.Compass");
}

// Task P3-6: CurrentValueChanged subscription. Compass is a permanent
// NotSupported stub, so the event can never actually fire in this or any
// other environment until SDL3 gains magnetometer support; this only
// confirms subscribing doesn't crash.
TEST(CompassTests, CurrentValueChangedSubscriptionDoesNotThrow)
{
    Compass c;
    bool invoked = false;
    c.CurrentValueChanged += [&invoked](System::Object*, const SensorReadingEventArgs<CompassReading>&)
    {
        invoked = true;
    };
    (void)invoked;

    EXPECT_THROW(c.Start(), SensorFailedException);
}

// Task P3-8: Calibrate subscription. Same permanent-stub limitation as
// above — Compass.Calibrate is never raised by this implementation, so
// this only confirms subscribing doesn't crash.
TEST(CompassTests, CalibrateSubscriptionDoesNotThrow)
{
    Compass c;
    bool invoked = false;
    c.Calibrate += [&invoked](System::Object*, const CalibrationEventArgs&)
    {
        invoked = true;
    };
    (void)invoked;

    EXPECT_THROW(c.Start(), SensorFailedException);
}
