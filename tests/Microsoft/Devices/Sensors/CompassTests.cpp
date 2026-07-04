// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <thread>
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

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(CompassTests, StopAfterDisposeThrows)
{
    Compass c;
    c.Dispose();
    EXPECT_THROW(c.Stop(), System::ObjectDisposedException);
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

// Task P6-1: Compass's instanceCount_ is a plain static int with the same
// unguarded increment/decrement pattern Accelerometer/Gyroscope had — now
// guarded by its own instanceCountMutex_. See AccelerometerTests.cpp's
// identical test for the full rationale.
TEST(CompassTests, ConcurrentConstructDestroyKeepsInstanceCountBalanced)
{
    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 50;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                const Compass c;
                (void)c;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    std::vector<std::unique_ptr<Compass>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Compass>()));
    }

    EXPECT_THROW({ const Compass overflow; (void)overflow; }, SensorFailedException);
}

// Task P6-3: ClaimDisposalOnce() (SensorBase.hpp) closes a race where two
// threads calling Dispose() on the same instance concurrently could both
// decrement instanceCount_ for what should be a single logical disposal.
// See AccelerometerTests.cpp's identical test for the full rationale.
TEST(CompassTests, ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount)
{
    constexpr int Rounds = 30;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Compass>();

        std::thread t1([instance]()
        {
            try
            {
                instance->Dispose();
            }
            catch (const System::ObjectDisposedException&)
            {
            }
        });
        std::thread t2([instance]()
        {
            try
            {
                instance->Dispose();
            }
            catch (const System::ObjectDisposedException&)
            {
            }
        });

        t1.join();
        t2.join();
    }

    std::vector<std::unique_ptr<Compass>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Compass>()));
    }

    EXPECT_THROW({ const Compass overflow; (void)overflow; }, SensorFailedException);
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
