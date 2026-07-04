// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Motion;
using Microsoft::Devices::Sensors::MotionReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;

TEST(MotionTests, GetIsSupportedPropertyIsFalse)
{
    EXPECT_FALSE(Motion::getIsSupportedProperty());
}

TEST(MotionTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Motion m; (void)m; });
}

TEST(MotionTests, GetStatePropertyReturnsNotSupported)
{
    const Motion m;
    EXPECT_EQ(m.getStateProperty(), SensorState::NotSupported);
}

TEST(MotionTests, StartThrowsSensorFailedException)
{
    Motion m;
    EXPECT_THROW(m.Start(), SensorFailedException);
}

TEST(MotionTests, StopDoesNotCrash)
{
    Motion m;
    EXPECT_THROW(m.Start(), SensorFailedException);
    EXPECT_NO_THROW(m.Stop());
    EXPECT_EQ(m.getStateProperty(), SensorState::Disabled);
}

TEST(MotionTests, DisposeSucceedsAndSecondDisposeThrows)
{
    Motion m;
    EXPECT_NO_THROW(m.Dispose());
    EXPECT_THROW(m.Dispose(), System::ObjectDisposedException);
}

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(MotionTests, StopAfterDisposeThrows)
{
    Motion m;
    m.Dispose();
    EXPECT_THROW(m.Stop(), System::ObjectDisposedException);
}

TEST(MotionTests, EleventhSimultaneousInstanceThrows)
{
    std::vector<std::unique_ptr<Motion>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Motion>());
    }

    EXPECT_THROW({ const Motion overflow; (void)overflow; }, SensorFailedException);
}

// Task P3-9: the eleventh-instance test above only proves the cap
// triggers; this proves instanceCount_ actually decrements on Dispose().
TEST(MotionTests, DisposingOneOfTenAllowsAnotherConstruction)
{
    std::vector<std::unique_ptr<Motion>> instances;
    for (int i = 0; i < 10; ++i)
    {
        instances.push_back(std::make_unique<Motion>());
    }

    instances.front()->Dispose();
    instances.erase(instances.begin());

    EXPECT_NO_THROW({ const Motion eleventh; (void)eleventh; });
}

// Task P6-1: Motion's instanceCount_ is a plain static int with the same
// unguarded increment/decrement pattern Accelerometer/Gyroscope had — now
// guarded by its own instanceCountMutex_. See AccelerometerTests.cpp's
// identical test for the full rationale.
TEST(MotionTests, ConcurrentConstructDestroyKeepsInstanceCountBalanced)
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
                const Motion m;
                (void)m;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    std::vector<std::unique_ptr<Motion>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Motion>()));
    }

    EXPECT_THROW({ const Motion overflow; (void)overflow; }, SensorFailedException);
}

TEST(MotionTests, GetCurrentValuePropertyThrowsInvalidOperationException)
{
    const Motion m;
    EXPECT_THROW((void)m.getCurrentValueProperty(), System::InvalidOperationException);
}

// Task P3-7: catches the dot-vs-colon GetTypeNameCPP naming-convention
// mistake that Task P2-4 fixed for Accelerometer; nothing previously
// verified this for Motion.
TEST(MotionTests, GetTypeName)
{
    const Motion m;
    EXPECT_EQ(m.GetTypeName(), "Microsoft.Devices.Sensors.Motion");
}

// Task P3-6: CurrentValueChanged subscription. Motion is a permanent
// NotSupported stub (requires Compass), so the event can never actually
// fire in this environment; this only confirms subscribing doesn't crash.
TEST(MotionTests, CurrentValueChangedSubscriptionDoesNotThrow)
{
    Motion m;
    bool invoked = false;
    m.CurrentValueChanged += [&invoked](System::Object*, const SensorReadingEventArgs<MotionReading>&)
    {
        invoked = true;
    };
    (void)invoked;

    EXPECT_THROW(m.Start(), SensorFailedException);
}

// Task P3-8: Calibrate subscription. Same permanent-stub limitation as
// above — Motion.Calibrate is never raised by this implementation, so this
// only confirms subscribing doesn't crash.
TEST(MotionTests, CalibrateSubscriptionDoesNotThrow)
{
    Motion m;
    bool invoked = false;
    m.Calibrate += [&invoked](System::Object*, const CalibrationEventArgs&)
    {
        invoked = true;
    };
    (void)invoked;

    EXPECT_THROW(m.Start(), SensorFailedException);
}
