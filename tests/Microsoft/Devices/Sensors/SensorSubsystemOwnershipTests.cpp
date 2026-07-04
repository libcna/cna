// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

// Task P4-8: Accelerometer and Gyroscope both wrap the same
// SDL_INIT_SENSOR subsystem. Before this task, each class guarded its own
// SDL_InitSubSystem()/SDL_QuitSubSystem() calls with SDL_WasInit(), which
// bypassed SDL's own internal ref-counting — one class's last instance
// disposing could tear the subsystem down while the other class's
// instances still expected it alive. This can't observe SDL's internal
// ref-count directly (headless, no real sensors — Start() always throws
// here before reaching the subsystem calls), so this only proves the
// cross-class construct/dispose code path doesn't crash or corrupt either
// class's own state, per plan_devices_phase4.md Task P4-8's test guidance.
TEST(SensorSubsystemOwnershipTests, DisposingAccelerometerDoesNotAffectGyroscopeState)
{
    Accelerometer accelerometer;
    Gyroscope gyroscope;

    const bool accelerometerSupported = Accelerometer::getIsSupportedProperty();
    const bool gyroscopeSupported = Gyroscope::getIsSupportedProperty();

    if (accelerometerSupported)
    {
        EXPECT_NO_THROW(accelerometer.Start());
    }
    else
    {
        EXPECT_THROW(accelerometer.Start(), AccelerometerFailedException);
    }

    if (gyroscopeSupported)
    {
        EXPECT_NO_THROW(gyroscope.Start());
    }
    else
    {
        EXPECT_THROW(gyroscope.Start(), SensorFailedException);
    }

    const SensorState gyroscopeStateBeforeDispose = gyroscope.getStateProperty();

    EXPECT_NO_THROW(accelerometer.Dispose());

    EXPECT_EQ(gyroscope.getStateProperty(), gyroscopeStateBeforeDispose);
    EXPECT_NO_THROW(gyroscope.Dispose());
}

TEST(SensorSubsystemOwnershipTests, DisposingGyroscopeDoesNotAffectAccelerometerState)
{
    Gyroscope gyroscope;
    Accelerometer accelerometer;

    const bool gyroscopeSupported = Gyroscope::getIsSupportedProperty();
    const bool accelerometerSupported = Accelerometer::getIsSupportedProperty();

    if (gyroscopeSupported)
    {
        EXPECT_NO_THROW(gyroscope.Start());
    }
    else
    {
        EXPECT_THROW(gyroscope.Start(), SensorFailedException);
    }

    if (accelerometerSupported)
    {
        EXPECT_NO_THROW(accelerometer.Start());
    }
    else
    {
        EXPECT_THROW(accelerometer.Start(), AccelerometerFailedException);
    }

    const SensorState accelerometerStateBeforeDispose = accelerometer.getStateProperty();

    EXPECT_NO_THROW(gyroscope.Dispose());

    EXPECT_EQ(accelerometer.getStateProperty(), accelerometerStateBeforeDispose);
    EXPECT_NO_THROW(accelerometer.Dispose());
}
