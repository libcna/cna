// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::GyroscopeReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Vector3;

// NOTE: Unlike Compass, the Gyroscope sensor can genuinely be supported on
// platforms/devices that expose SDL_SENSOR_GYRO. These tests branch on the
// live getIsSupportedProperty() result so they pass both on typical headless
// CI/dev machines (no gyroscope hardware) and on real hardware.

TEST(GyroscopeTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = Gyroscope::getIsSupportedProperty();
    (void)supported;
}

// Task P5-1: mirrors AccelerometerTests.RepeatedSupportProbingDoesNotChangeSubsequentBehavior
// — see that test for the full rationale (a leaked SDL_INIT_SENSOR
// ref-count from getIsSupportedProperty() can't be asserted on directly,
// so this instead proves repeated probing doesn't change subsequent
// behavior).
TEST(GyroscopeTests, RepeatedSupportProbingDoesNotChangeSubsequentBehavior)
{
    const bool supportedBefore = Gyroscope::getIsSupportedProperty();

    for (int i = 0; i < 50; ++i)
    {
        const Gyroscope probe;
        (void)probe;
        (void)Gyroscope::getIsSupportedProperty();
    }

    const bool supportedAfter = Gyroscope::getIsSupportedProperty();
    EXPECT_EQ(supportedBefore, supportedAfter);

    const Gyroscope fresh;
    if (supportedAfter)
    {
        EXPECT_EQ(fresh.getStateProperty(), SensorState::Initializing);
    }
    else
    {
        EXPECT_EQ(fresh.getStateProperty(), SensorState::NotSupported);
    }
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

// Task P6-2: see AccelerometerTests.cpp's identical test for the full
// rationale — a failed Start() must release the subsystem hold it just
// acquired instead of leaking it until Dispose().
TEST(GyroscopeTests, FailedStartReleasesSubsystemHoldItAcquired)
{
    if (Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is supported on this platform; Start()-failure path not exercised.";
    }

    Gyroscope g;
    EXPECT_FALSE(g.GetSubsystemHeldForTesting());
    EXPECT_THROW(g.Start(), SensorFailedException);
    EXPECT_FALSE(g.GetSubsystemHeldForTesting());
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

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(GyroscopeTests, StopAfterDisposeThrows)
{
    Gyroscope g;
    g.Dispose();
    EXPECT_THROW(g.Stop(), System::ObjectDisposedException);
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

// Task P6-1: see AccelerometerTests.cpp's identical test for the full
// rationale — instanceCount_'s check+increment/decrement previously used an
// inconsistent locking discipline, a real data race under concurrent
// construct/destroy.
TEST(GyroscopeTests, ConcurrentConstructDestroyKeepsInstanceCountBalanced)
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
                const Gyroscope g;
                (void)g;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    std::vector<std::unique_ptr<Gyroscope>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Gyroscope>()));
    }

    EXPECT_THROW({ const Gyroscope overflow; (void)overflow; }, SensorFailedException);
}

// Task P6-3: see AccelerometerTests.cpp's identical test for the full
// rationale — started_/state_/subsystemHeld_ previously had an
// inconsistent locking discipline.
TEST(GyroscopeTests, ConcurrentStartStopFromMultipleThreadsDoesNotCrash)
{
    Gyroscope g;

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&g]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                try
                {
                    g.Start();
                }
                catch (const SensorFailedException&)
                {
                }

                g.Stop();
                (void)g.getStateProperty();
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_NO_THROW(g.Dispose());
}

// Task P6-3: see AccelerometerTests.cpp's identical test for the full
// rationale — ClaimDisposalOnce() closes a race where two threads calling
// Dispose() on the same instance concurrently could both decrement
// instanceCount_ for one logical disposal.
TEST(GyroscopeTests, ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount)
{
    constexpr int Rounds = 30;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Gyroscope>();

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

    std::vector<std::unique_ptr<Gyroscope>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Gyroscope>()));
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

// Task P4-5: mirrors AccelerometerTests.CurrentValueChangedReceivesExpectedReading
// — exercises the real CurrentValueChanged dispatch path via the Task P4-2
// synthetic-injection hooks. No unit conversion for Gyroscope (unlike
// Accelerometer's g-normalization): RotationRate is the raw SDL value,
// in radians/second.
TEST(GyroscopeTests, CurrentValueChangedReceivesExpectedReading)
{
    Gyroscope g;
    g.SetStartedForTesting(true);

    bool invoked = false;
    GyroscopeReading receivedReading;
    g.CurrentValueChanged += [&invoked, &receivedReading](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args)
    {
        invoked = true;
        receivedReading = args.getSensorReadingProperty();
    };

    const float rawX = 0.5f;
    const float rawY = -1.25f;
    const float rawZ = 2.0f;

    g.InjectSyntheticSensorUpdate(rawX, rawY, rawZ);

    ASSERT_TRUE(invoked);
    const Vector3 expectedRotationRate(rawX, rawY, rawZ);
    EXPECT_EQ(receivedReading.getRotationRateProperty(), expectedRotationRate);
}

// Task P5-6: mirrors AccelerometerTests.InjectSyntheticSensorUpdateUpdatesCurrentValueWhenMarkedSupported
// — see that test for the full rationale.
TEST(GyroscopeTests, InjectSyntheticSensorUpdateUpdatesCurrentValueWhenMarkedSupported)
{
    Gyroscope g;
    g.SetSupportedForTesting(true);
    g.SetStartedForTesting(true);

    EXPECT_FALSE(g.getIsDataValidProperty());
    EXPECT_EQ(g.getCurrentValueProperty().getRotationRateProperty(), Vector3::Zero);

    const float rawX = 0.5f;
    const float rawY = -1.25f;
    const float rawZ = 2.0f;

    g.InjectSyntheticSensorUpdate(rawX, rawY, rawZ);

    EXPECT_TRUE(g.getIsDataValidProperty());
    const Vector3 expectedRotationRate(rawX, rawY, rawZ);
    EXPECT_EQ(g.getCurrentValueProperty().getRotationRateProperty(), expectedRotationRate);
}

// Task P5-6: mirrors AccelerometerTests.GetCurrentValuePropertyStillThrowsAfterSyntheticUpdateWhenNotMarkedSupported
// — see that test for the full rationale (this is the real, intentional
// contract, Task P3-1, not a gap).
TEST(GyroscopeTests, GetCurrentValuePropertyStillThrowsAfterSyntheticUpdateWhenNotMarkedSupported)
{
    if (Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is supported on this platform; unsupported-path test not applicable.";
    }

    Gyroscope g;
    g.SetStartedForTesting(true);
    g.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);

    EXPECT_THROW((void)g.getCurrentValueProperty(), System::InvalidOperationException);
}

// Task P4-7: Timestamp is now the real wall-clock time of the call
// (System::DateTimeOffset::getUtcNowProperty()), not a bogus near-year-1
// value derived from SDL_GetTicksNS(). Asserts "close to now" with a
// generous tolerance rather than an exact value, since wall-clock time is
// inherently non-deterministic between the call and the assertion.
TEST(GyroscopeTests, CurrentValueChangedReceivesWallClockTimestamp)
{
    Gyroscope g;
    g.SetStartedForTesting(true);

    System::DateTimeOffset receivedTimestamp;
    g.CurrentValueChanged += [&receivedTimestamp](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args)
    {
        receivedTimestamp = args.getSensorReadingProperty().getTimestampProperty();
    };

    const System::DateTimeOffset beforeInjection = System::DateTimeOffset::getUtcNowProperty();
    g.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);
    const System::DateTimeOffset afterInjection = System::DateTimeOffset::getUtcNowProperty();

    EXPECT_GE(receivedTimestamp, beforeInjection);
    EXPECT_LE(receivedTimestamp, afterInjection);
}

// Task P4-6: confirms Stop() actually disables further synthetic-event
// dispatch (not just that Start() throws headless — StopDoesNotCrash
// above already covers that).
TEST(GyroscopeTests, StopPreventsSubsequentSyntheticEventFromDispatching)
{
    Gyroscope g;
    g.SetStartedForTesting(true);

    int invokedCount = 0;
    Vector3 lastRotationRate;
    g.CurrentValueChanged += [&invokedCount, &lastRotationRate](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args)
    {
        ++invokedCount;
        lastRotationRate = args.getSensorReadingProperty().getRotationRateProperty();
    };

    g.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);
    ASSERT_EQ(invokedCount, 1);
    const Vector3 rotationRateBeforeStop = lastRotationRate;

    g.Stop();

    g.InjectSyntheticSensorUpdate(0.0f, 1.0f, 0.0f);
    EXPECT_EQ(invokedCount, 1);
    EXPECT_EQ(lastRotationRate, rotationRateBeforeStop);
}

// Task P5-2: mirrors AccelerometerTests.ConcurrentSyntheticUpdatesDoNotCrashAndDrainBeforeDispose
// — see that test for the full rationale.
TEST(GyroscopeTests, ConcurrentSyntheticUpdatesDoNotCrashAndDrainBeforeDispose)
{
    auto gyroscope = std::make_unique<Gyroscope>();
    gyroscope->SetStartedForTesting(true);

    std::atomic<int> receivedCount{0};
    gyroscope->CurrentValueChanged += [&receivedCount](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++receivedCount;
    };

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 10;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&gyroscope]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                gyroscope->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(receivedCount.load(), ThreadCount * IterationsPerThread);
    EXPECT_NO_THROW(gyroscope->Dispose());
}

// Task P5-3: mirrors AccelerometerTests.DisposeFromWithinOwnCallbackDoesNotDeadlock
// — see that test for the full rationale. If this test hangs, the fix has
// regressed (shows as a timeout, not a clean assertion failure).
TEST(GyroscopeTests, DisposeFromWithinOwnCallbackDoesNotDeadlock)
{
    auto gyroscope = std::make_unique<Gyroscope>();
    gyroscope->SetStartedForTesting(true);

    bool handlerRan = false;
    gyroscope->CurrentValueChanged += [&](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        handlerRan = true;
        gyroscope->Dispose();
    };

    EXPECT_NO_THROW(gyroscope->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(handlerRan);
    EXPECT_THROW(gyroscope->Dispose(), System::ObjectDisposedException);
}
