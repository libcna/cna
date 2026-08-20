// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerFailedException.hpp"
#include "Microsoft/Devices/Sensors/Detail/PlatformSensorSubsystem.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"

using Microsoft::Devices::Sensors::Accelerometer;
using Microsoft::Devices::Sensors::AccelerometerFailedException;
using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorState;

// Task P4-8: Accelerometer and Gyroscope both wrap the same
// native sensor subsystem. Before this task, each class guarded its own
// acquire/release calls with a global initialized query, which
// bypassed SDL's own internal ref-counting — one class's last instance
// disposing could tear the subsystem down while the other class's
// instances still expected it alive. This can't observe SDL's internal
// ref-count directly (headless, no real sensors — Start() always throws
// here before reaching the subsystem calls), so this only proves the
// cross-class construct/dispose code path doesn't crash or corrupt either
// class's own state, per plans/plan_devices_phase4.md Task P4-8's test guidance.
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

// Task P7-1: getIsSupportedProperty() previously locked each class's *own*
// SdlSensorSubsystem<T>::mutex_ around its real SDL sensor-subsystem calls —
// two different mutexes for Accelerometer and Gyroscope, so nothing actually
// serialized their real subsystem/enumerate/open/type/close/release calls against each
// other. Unlike plans/plan_devices_phase6.md's P6-1 addendum test (which only
// stressed one class at a time and still reliably reproduced heap
// corruption), this test constructs/destroys/probes *both* classes
// concurrently, from separate thread pools, in the same test — the scenario
// the shared GetGlobalSdlSensorMutex() fix (Task P7-1) exists for. This
// test's value is in running clean under real concurrent cross-class
// contention (see docs/devices-build.md's stress-loop guidance — a single
// green run does not by itself prove this is fixed).
TEST(SensorSubsystemOwnershipTests, ConcurrentCrossClassConstructDestroyProbeDoesNotCrash)
{
    constexpr int ThreadCountPerClass = 8;
    constexpr int IterationsPerThread = 50;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCountPerClass * 4);

    for (int t = 0; t < ThreadCountPerClass; ++t)
    {
        threads.emplace_back([]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                const Accelerometer a;
                (void)a;
            }
        });

        threads.emplace_back([]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                const Gyroscope g;
                (void)g;
            }
        });

        threads.emplace_back([]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                const bool supported = Accelerometer::getIsSupportedProperty();
                (void)supported;
            }
        });

        threads.emplace_back([]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                const bool supported = Gyroscope::getIsSupportedProperty();
                (void)supported;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    // Sanity check: both classes' own instance-count bookkeeping must still
    // be internally consistent after the cross-class contention above —
    // each independently allows exactly MaxSensorCount (10) simultaneous
    // instances and rejects the 11th.
    std::vector<std::unique_ptr<Accelerometer>> accelerometers;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(accelerometers.push_back(std::make_unique<Accelerometer>()));
    }
    EXPECT_THROW({ const Accelerometer overflow; (void)overflow; }, SensorFailedException);

    std::vector<std::unique_ptr<Gyroscope>> gyroscopes;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(gyroscopes.push_back(std::make_unique<Gyroscope>()));
    }
    EXPECT_THROW({ const Gyroscope overflow; (void)overflow; }, SensorFailedException);
}
