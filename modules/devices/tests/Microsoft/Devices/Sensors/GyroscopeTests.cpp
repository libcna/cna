// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../Detail/ProcSelfResourceCounters.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::Gyroscope;
using Microsoft::Devices::Sensors::GyroscopeReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Vector3;
using System::TimeSpan;

// NOTE: Unlike Compass, the Gyroscope sensor can genuinely be supported on
// platforms/devices that expose a gyroscope. These tests branch on the
// live getIsSupportedProperty() result so they pass both on typical headless
// CI/dev machines (no gyroscope hardware) and on real hardware.

TEST(GyroscopeTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = Gyroscope::getIsSupportedProperty();
    (void)supported;
}

// Task P5-1: mirrors AccelerometerTests.RepeatedSupportProbingDoesNotChangeSubsequentBehavior
// — see that test for the full rationale (a leaked sensor-subsystem
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

// Task SENSORBASE-002: see AccelerometerTests.cpp's identical test for the
// full rationale (MonoGame cross-check confirms the real WP7 SensorBase<T>'s
// single shared 2ms default, not a per-sensor-class override).
TEST(GyroscopeTests, DefaultTimeBetweenUpdatesIsTwoMilliseconds)
{
    const Gyroscope g;
    EXPECT_EQ(g.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(2.0));
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

// Task SDLCORE-003 (2026-07-17): see AccelerometerTests.cpp's identical test
// (FailedEventWatchRegistrationRollsBackAndReportsFailure) for the full
// rationale.
TEST(GyroscopeTests, FailedEventWatchRegistrationRollsBackAndReportsFailure)
{
    if (!Gyroscope::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Gyroscope is not supported on this platform; supported-path test not applicable.";
    }

    Gyroscope::SetEventWatchRegistrationFailureForTesting(true);
    struct ResetGuard
    {
        ~ResetGuard() { Gyroscope::SetEventWatchRegistrationFailureForTesting(false); }
    } resetGuard;

    Gyroscope g;
    EXPECT_FALSE(g.GetSubsystemHeldForTesting());
    EXPECT_THROW(g.Start(), SensorFailedException);
    EXPECT_FALSE(g.GetSubsystemHeldForTesting());
    EXPECT_EQ(g.getStateProperty(), SensorState::NotSupported);
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

// Task SENSORBASE-006: mirrors AccelerometerTests.
// DisposeWhileStartedForTestingDoesNotCrash -- see that test for the full
// rationale. This exact scenario (Dispose() while started_) had no test at
// all for Gyroscope before this task, not even a hardware-conditional one.
TEST(GyroscopeTests, DisposeWhileStartedForTestingDoesNotCrash)
{
    Gyroscope g;
    g.SetSupportedForTesting(true);
    g.SetStartedForTesting(true);

    EXPECT_NO_THROW(g.Dispose());
}

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(GyroscopeTests, StopAfterDisposeThrows)
{
    Gyroscope g;
    g.Dispose();
    EXPECT_THROW(g.Stop(), System::ObjectDisposedException);
}

// Task DEVICES-0056: no test anywhere asserted Start()-after-Dispose() throws
// — only Stop()-after-Dispose() and Dispose()-after-Dispose() were.
TEST(GyroscopeTests, StartAfterDisposeThrows)
{
    Gyroscope g;
    g.Dispose();
    EXPECT_THROW(g.Start(), System::ObjectDisposedException);
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

// Task P7-2: see AccelerometerTests's identical test for the full
// rationale — a losing Dispose() call must wait for the winner's cleanup to
// actually finish (via WaitForDisposalToComplete()) rather than racing
// ahead and flipping disposed_ true while the winner's own Stop() call is
// still relying on it being false.
TEST(GyroscopeTests, ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed)
{
    constexpr int Rounds = 15;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Gyroscope>();
        instance->SetStartedForTesting(true);

        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool winnerPaused = false;
        bool releaseWinner = false;

        instance->SetDisposalCleanupHookForTesting([&]()
        {
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                winnerPaused = true;
            }
            gateCv.notify_all();

            std::unique_lock<std::mutex> lock(gateMutex);
            gateCv.wait(lock, [&] { return releaseWinner; });
        });

        std::thread winnerThread([instance]()
        {
            EXPECT_NO_THROW(instance->Dispose());
        });

        {
            std::unique_lock<std::mutex> lock(gateMutex);
            gateCv.wait(lock, [&] { return winnerPaused; });
        }

        std::atomic<bool> loserReturned{false};
        std::thread loserThread([instance, &loserReturned]()
        {
            EXPECT_NO_THROW(instance->Dispose());
            loserReturned.store(true);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_FALSE(loserReturned.load());

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            releaseWinner = true;
        }
        gateCv.notify_all();

        winnerThread.join();
        loserThread.join();

        EXPECT_TRUE(loserReturned.load());
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
// a platform sensor-update event, which this headless environment cannot
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

// Task DEVPERF-004 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// mirrors AccelerometerTests.RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt
// -- Gyroscope dispatches CurrentValueChanged through the same
// SensorBase<T>::SetCurrentValueAndMarkDataValid()/System::EventHandler<T>::Raise()
// path as Accelerometer, so it must honor the same snapshot-based handler-list
// mutation guarantee: a handler mid-dispatch removing a different, not-yet-invoked
// handler in the same batch does not stop that handler from running in *this*
// dispatch (its removal only takes effect for the next one).
TEST(GyroscopeTests, RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt)
{
    Gyroscope g;
    g.SetStartedForTesting(true);

    using Args = SensorReadingEventArgs<GyroscopeReading>;
    using Token = System::EventHandler<Args>::Token;

    Token secondToken{};
    bool secondHandlerInvoked = false;

    g.CurrentValueChanged.Add(
        [&g, &secondToken](System::Object*, const Args&)
        {
            g.CurrentValueChanged.Remove(secondToken);
        });

    secondToken = g.CurrentValueChanged.Add(
        [&secondHandlerInvoked](System::Object*, const Args&)
        {
            secondHandlerInvoked = true;
        });

    EXPECT_NO_THROW(g.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));

    EXPECT_TRUE(secondHandlerInvoked);

    secondHandlerInvoked = false;
    EXPECT_NO_THROW(g.InjectSyntheticSensorUpdate(2.0f, 0.0f, 0.0f));
    EXPECT_FALSE(secondHandlerInvoked);
}

// Task DEVPERF-004: mirrors AccelerometerTests.HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState
// -- proves the same reentrancy guarantee holds for Gyroscope's CurrentValueChanged
// dispatch: a handler that triggers a brand-new dispatch from within itself does not
// deadlock or corrupt state, and the inner (reentrant) dispatch completes fully
// before the outer handler returns.
TEST(GyroscopeTests, HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState)
{
    Gyroscope g;
    g.SetSupportedForTesting(true);
    g.SetStartedForTesting(true);

    bool reentered = false;
    std::vector<float> observedXValues;

    g.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args)
    {
        observedXValues.push_back(args.getSensorReadingProperty().getRotationRateProperty().X);
        if (!reentered)
        {
            reentered = true;
            g.InjectSyntheticSensorUpdate(2.0f, 0.0f, 0.0f);
        }
    };

    EXPECT_NO_THROW(g.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));

    ASSERT_EQ(observedXValues.size(), 2u);
    EXPECT_FLOAT_EQ(observedXValues[0], 1.0f);
    EXPECT_FLOAT_EQ(observedXValues[1], 2.0f);
    EXPECT_FLOAT_EQ(g.getCurrentValueProperty().getRotationRateProperty().X, 2.0f);
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

// Task SENSORBASE-005: mirrors AccelerometerTests.
// CurrentValueAndIsDataValidRetainLastReadingAfterStop -- see that test for
// the full rationale.
TEST(GyroscopeTests, CurrentValueAndIsDataValidRetainLastReadingAfterStop)
{
    Gyroscope g;
    g.SetSupportedForTesting(true);
    g.SetStartedForTesting(true);

    const Vector3 expectedRotationRate(0.5f, -1.25f, 2.0f);
    g.InjectSyntheticSensorUpdate(0.5f, -1.25f, 2.0f);

    ASSERT_TRUE(g.getIsDataValidProperty());
    ASSERT_EQ(g.getCurrentValueProperty().getRotationRateProperty(), expectedRotationRate);

    g.Stop();

    EXPECT_TRUE(g.getIsDataValidProperty());
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
// value derived from a native monotonic tick count. Asserts "close to now" with a
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

// Task P6-4: see AccelerometerTests.cpp's identical test for the full
// rationale — a throwing CurrentValueChanged handler must not leave
// dispatchingThreadIds_ corrupted and hang a future Dispose() call.
TEST(GyroscopeTests, ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose)
{
    auto gyroscope = std::make_unique<Gyroscope>();
    gyroscope->SetStartedForTesting(true);

    gyroscope->CurrentValueChanged += [](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        throw std::runtime_error("synthetic handler failure");
    };

    EXPECT_THROW(gyroscope->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f), std::runtime_error);

    EXPECT_NO_THROW(gyroscope->Dispose());
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

// Task P7-3: see AccelerometerTests's identical test for the full
// rationale — reproduces the use-after-free the audit found in
// SdlSensorSubsystem<TSensor>::SensorEventWatch()'s old bookkeeping, where
// disposing (and freeing) a not-yet-dispatched instance from a different
// instance's callback, within the same simulated dispatch batch, could
// leave the dispatch loop holding a dangling pointer to it.
TEST(GyroscopeTests, DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree)
{
    auto a = std::make_unique<Gyroscope>();
    auto b = std::make_unique<Gyroscope>();

    a->SetStartedForTesting(true);
    b->SetStartedForTesting(true);
    Gyroscope::RegisterStartedInstanceForTesting(*a);
    Gyroscope::RegisterStartedInstanceForTesting(*b);

    Gyroscope* bRawPtr = b.get();
    bool bCallbackCalled = false;
    bRawPtr->CurrentValueChanged += [&bCallbackCalled](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        bCallbackCalled = true;
    };

    bool aCallbackCalled = false;
    a->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        aCallbackCalled = true;
        b.reset();
    };

    const std::vector<Gyroscope*> batch{a.get(), bRawPtr};
    EXPECT_NO_THROW(Gyroscope::DispatchToInstancesForTesting(batch, 1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(aCallbackCalled);
    EXPECT_FALSE(bCallbackCalled);

    EXPECT_NO_THROW(a->Dispose());
}

// Task SDLCORE-004 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// see AccelerometerTests's identical test for the full rationale — a
// deterministic (placement-new-based) address-reuse (ABA) regression test.
// b is disposed AND destroyed mid-batch, then a brand-new, unrelated
// Gyroscope `c` is placement-constructed at b's exact freed address and
// started, before the dispatch loop reaches its already-snapshotted (stale)
// entry for b. DispatchRegistration must prevent that stale entry from
// being delivered to `c`, regardless of the shared address.
TEST(GyroscopeTests, DispatchDoesNotDeliverStaleEventToUnrelatedInstanceReusingSameAddress)
{
    auto a = std::make_unique<Gyroscope>();
    a->SetStartedForTesting(true);
    Gyroscope::RegisterStartedInstanceForTesting(*a);

    alignas(Gyroscope) unsigned char storage[sizeof(Gyroscope)];
    Gyroscope* b = new (static_cast<void*>(storage)) Gyroscope();
    b->SetStartedForTesting(true);
    Gyroscope::RegisterStartedInstanceForTesting(*b);

    Gyroscope* c = nullptr;
    bool cCallbackCalled = false;

    bool aCallbackCalled = false;
    a->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        aCallbackCalled = true;

        b->~Gyroscope();
        c = new (static_cast<void*>(storage)) Gyroscope();
        c->SetStartedForTesting(true);
        Gyroscope::RegisterStartedInstanceForTesting(*c);

        c->CurrentValueChanged += [&cCallbackCalled](
            System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
        {
            cCallbackCalled = true;
        };
    };

    const std::vector<Gyroscope*> batch{a.get(), b};
    EXPECT_NO_THROW(Gyroscope::DispatchToInstancesForTesting(batch, 1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(aCallbackCalled);
    EXPECT_FALSE(cCallbackCalled);

    ASSERT_NE(c, nullptr);
    c->~Gyroscope();

    EXPECT_NO_THROW(a->Dispose());
}

// Task P8-1: Gyroscope::DispatchSensorReading() raises CurrentValueChanged as its
// last statement and touches `this` for nothing afterward, so — with the
// dispatchToken_ fix — a handler that destroys (not just Dispose()s) this exact
// instance from within its own CurrentValueChanged handler is fully supported for
// this class. Uses unique_ptr::reset() (not Dispose()) so the object's memory is
// genuinely freed before InjectSyntheticSensorUpdate()'s cleanup guard runs — if
// the guard still touched `this`/the old plain dispatchingThreadIds_ member instead
// of the token, this would be a real use-after-free.
TEST(GyroscopeTests, SelfDestroyingFromOwnCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree)
{
    auto gyroscope = std::make_unique<Gyroscope>();
    gyroscope->SetStartedForTesting(true);

    Gyroscope* rawPtr = gyroscope.get();
    bool handlerRan = false;
    rawPtr->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        handlerRan = true;
        gyroscope.reset();
    };

    EXPECT_NO_THROW(rawPtr->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(handlerRan);
}

// Task P8-1: same as above, but through the batch-dispatch path
// (DispatchToInstancesForTesting(), which shares the exact same
// DispatchToInstances() bookkeeping the real SDL event-watch path uses) rather than
// InjectSyntheticSensorUpdate() — proves the token fix covers both dispatch entry
// points, not just one.
TEST(GyroscopeTests, SelfDestroyingFromOwnCallbackDuringBatchDispatchDoesNotUseAfterFree)
{
    auto gyroscope = std::make_unique<Gyroscope>();
    gyroscope->SetStartedForTesting(true);
    Gyroscope::RegisterStartedInstanceForTesting(*gyroscope);

    Gyroscope* rawPtr = gyroscope.get();
    bool handlerRan = false;
    rawPtr->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        handlerRan = true;
        gyroscope.reset();
    };

    const std::vector<Gyroscope*> batch{rawPtr};
    EXPECT_NO_THROW(Gyroscope::DispatchToInstancesForTesting(batch, 1.0f, 0.0f, 0.0f));

    EXPECT_TRUE(handlerRan);
}

// Task P8-5: see AccelerometerTests's identical test for the full rationale —
// DispatchToInstances()'s own doc comment claims a throwing handler doesn't prevent
// the next instance in the same batch from being dispatched to; this proves it.
//
// Task SDLCORE-009 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// extended with GetDispatchExceptionCountForTesting()/
// GetLastDispatchExceptionMessageForTesting() assertions — see
// AccelerometerTests's identically-extended test for the full rationale; this
// gives Gyroscope's own public static test hooks at least one direct test
// each, per this project's own per-method test coverage rule, rather than
// relying solely on Accelerometer's coverage of the shared underlying
// Detail::SdlSensorSubsystem<TSensor>::DispatchToInstances() template logic.
TEST(GyroscopeTests, ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent)
{
    auto a = std::make_unique<Gyroscope>();
    auto b = std::make_unique<Gyroscope>();

    a->SetStartedForTesting(true);
    b->SetStartedForTesting(true);
    Gyroscope::RegisterStartedInstanceForTesting(*a);
    Gyroscope::RegisterStartedInstanceForTesting(*b);

    bool aCallbackCalled = false;
    a->CurrentValueChanged += [&aCallbackCalled](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        aCallbackCalled = true;
        throw std::runtime_error("a's handler deliberately fails");
    };

    bool bCallbackCalled = false;
    b->CurrentValueChanged += [&bCallbackCalled](
        System::Object*, const SensorReadingEventArgs<GyroscopeReading>&)
    {
        bCallbackCalled = true;
    };

    const int countBefore = Gyroscope::GetDispatchExceptionCountForTesting();

    const std::vector<Gyroscope*> batch{a.get(), b.get()};
    EXPECT_NO_THROW(Gyroscope::DispatchToInstancesForTesting(batch, 1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(aCallbackCalled);
    EXPECT_TRUE(bCallbackCalled);
    EXPECT_EQ(Gyroscope::GetDispatchExceptionCountForTesting(), countBefore + 1);
    EXPECT_EQ(Gyroscope::GetLastDispatchExceptionMessageForTesting(), "a's handler deliberately fails");

    EXPECT_NO_THROW(a->Dispose());
    EXPECT_NO_THROW(b->Dispose());
}

// Task SDLCORE-005 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// see AccelerometerTests's identical test for the full rationale — this
// container never has a real SDL sensor open, so this only proves
// IsSensorConnectedForTesting()'s plumbing/logic (reaches platform enumeration
// and correctly reports "not found"), not a genuine hardware
// remove/re-add/default-device-change scenario.
TEST(GyroscopeTests, IsSensorConnectedForTestingReportsNotConnectedWhenNoRealSensorIsOpen)
{
    EXPECT_FALSE(Gyroscope::IsSensorConnectedForTesting(0));
    EXPECT_FALSE(Gyroscope::IsSensorConnectedForTesting(-1));
    EXPECT_FALSE(Gyroscope::IsSensorConnectedForTesting(123456789));
}

// Task PERF2-002 (2026-07-18, external audit `audit_devices_2026-07-17.md`): mirrors
// AccelerometerTests.OneHundredThousandConstructProbeStartStopDisposeCyclesLeaveNoResourceLeak --
// see that test for the full rationale (LeakSanitizer non-functional in this container, no
// existing test in this file runs anywhere near this scale).
TEST(GyroscopeTests, OneHundredThousandConstructProbeStartStopDisposeCyclesLeaveNoResourceLeak)
{
#if !defined(__linux__)
    GTEST_SKIP() << "FD/thread leak tracking is Linux-specific (/proc/self/fd, /proc/self/status)";
#else
    constexpr int Cycles = 100000;

    {
        const Gyroscope warmup;
        (void)warmup;
    }

    const int fdBefore = CnaTestSupport::CountOpenFileDescriptors();
    const int threadsBefore = CnaTestSupport::GetThreadCount();
    ASSERT_GE(fdBefore, 0);
    ASSERT_GE(threadsBefore, 0);

    for (int i = 0; i < Cycles; ++i)
    {
        Gyroscope g;
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

    EXPECT_LE(CnaTestSupport::CountOpenFileDescriptors(), fdBefore)
        << "open file descriptor count grew after " << Cycles << " cycles -- possible leak";
    EXPECT_LE(CnaTestSupport::GetThreadCount(), threadsBefore)
        << "thread count grew after " << Cycles << " cycles -- possible leak";
#endif
}
