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

// Task P5-1: getIsSupportedProperty() previously called
// EnsureSensorSubsystemInitialized() (a real SDL_InitSubSystem(SDL_INIT_SENSOR)
// call as of Task P4-8) on every call/every construction with no matching
// SDL_QuitSubSystem() — an unbounded ref-count leak. This can't assert on
// SDL's internal ref-count directly (no public API exposes it), so this
// instead proves the observable consequence of a leak-free fix: hammering
// the probe path (many discarded constructions plus many direct
// getIsSupportedProperty() calls) doesn't change subsequent behavior —
// getIsSupportedProperty()'s result and a fresh construction's state_ stay
// consistent before and after, and neither crashes nor throws unexpectedly.
TEST(AccelerometerTests, RepeatedSupportProbingDoesNotChangeSubsequentBehavior)
{
    const bool supportedBefore = Accelerometer::getIsSupportedProperty();

    for (int i = 0; i < 50; ++i)
    {
        const Accelerometer probe;
        (void)probe;
        (void)Accelerometer::getIsSupportedProperty();
    }

    const bool supportedAfter = Accelerometer::getIsSupportedProperty();
    EXPECT_EQ(supportedBefore, supportedAfter);

    const Accelerometer fresh;
    if (supportedAfter)
    {
        EXPECT_EQ(fresh.getStateProperty(), SensorState::Initializing);
    }
    else
    {
        EXPECT_EQ(fresh.getStateProperty(), SensorState::NotSupported);
    }
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

// Task P6-2: OpenDefaultSensorLocked() failing (no sensor hardware, but
// SDL_INIT_SENSOR itself initializes fine) after subsystemHeld_ was just
// set true previously left it true until this instance's eventual
// Dispose() — a real subsystem-hold leak. GetSubsystemHeldForTesting()
// (Task P6-2) lets this test directly confirm the hold is released
// immediately on failure instead.
TEST(AccelerometerTests, FailedStartReleasesSubsystemHoldItAcquired)
{
    if (Accelerometer::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Accelerometer is supported on this platform; Start()-failure path not exercised.";
    }

    Accelerometer a;
    EXPECT_FALSE(a.GetSubsystemHeldForTesting());
    EXPECT_THROW(a.Start(), AccelerometerFailedException);
    EXPECT_FALSE(a.GetSubsystemHeldForTesting());
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

// Task P6-1: instanceCount_'s check+increment (constructor) and decrement
// (Dispose()) previously used an inconsistent locking discipline (unlocked
// vs. locked) — a real data race under concurrent construct/destroy. If the
// race regresses, concurrent contention could leak/lose increments, either
// falsely tripping the 10-instance cap below or falsely allowing more than
// 10 to coexist; either would make this test fail.
TEST(AccelerometerTests, ConcurrentConstructDestroyKeepsInstanceCountBalanced)
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
                const Accelerometer a;
                (void)a;
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    std::vector<std::unique_ptr<Accelerometer>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Accelerometer>()));
    }

    EXPECT_THROW({ const Accelerometer overflow; (void)overflow; }, SensorFailedException);
}

// Task P6-3: started_/state_/subsystemHeld_ previously had an inconsistent
// locking discipline (some reads/writes under subsystem.mutex_, some not).
// Stresses concurrent Start()/Stop()/getStateProperty() calls on one shared
// instance from many threads; a regression would most likely show up as a
// crash, TSan-detectable race, or hang, not a specific assertion failure —
// this test's value is in running clean under real concurrent contention.
TEST(AccelerometerTests, ConcurrentStartStopFromMultipleThreadsDoesNotCrash)
{
    Accelerometer a;

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&a]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                try
                {
                    a.Start();
                }
                catch (const AccelerometerFailedException&)
                {
                    // Expected: either unsupported hardware, or another
                    // thread already started it first.
                }

                a.Stop();
                (void)a.getStateProperty();
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_NO_THROW(a.Dispose());
}

// Task P6-3: ClaimDisposalOnce() (SensorBase.hpp) closes a race where two
// threads calling Dispose() on the *same* instance concurrently could both
// pass the getIsDisposedProperty() check and both decrement
// subsystem.instanceCount_ for what should be a single logical disposal. If
// this regresses, repeated rounds of this would eventually corrupt
// instanceCount_ enough to make the final 10-instance check below fail.
TEST(AccelerometerTests, ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount)
{
    constexpr int Rounds = 30;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Accelerometer>();

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

    std::vector<std::unique_ptr<Accelerometer>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Accelerometer>()));
    }

    EXPECT_THROW({ const Accelerometer overflow; (void)overflow; }, SensorFailedException);
}

// Task P7-2: previously, a Dispose() call that lost ClaimDisposalOnce() (the
// "loser") fell through immediately to the base SensorBase<T>::Dispose(),
// flipping disposed_ true while the winner's own cleanup (which calls the
// public Stop(), guarded by the same disposed-state precondition) could
// still be running — causing the winner's own Stop() call to observe
// disposed_ == true and throw ObjectDisposedException mid-cleanup, escaping
// Dispose(bool) with instanceCount_/subsystemHeld_/the open sensor never
// released. This test deterministically reproduces the interleaving (real
// thread scheduling alone cannot reliably hit the narrow window between
// ClaimDisposalOnce() succeeding and Stop() being called) via a NOXNA test
// hook that pauses the winner right after it claims disposal, until this
// test confirms a concurrently-racing loser is genuinely blocked — proving
// the loser actually waits rather than racing ahead. If this regresses, the
// loser's Dispose() call returns before the winner does, and the final
// 10-instance check below would fail once instanceCount_ has leaked enough
// across repeated rounds.
TEST(AccelerometerTests, ConcurrentDisposeLoserWaitsForWinnerCleanupToFinishBeforeStateAppearsDisposed)
{
    constexpr int Rounds = 15;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Accelerometer>();
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

        // Wait until the winner has claimed disposal and is paused inside
        // its own cleanup, before starting the loser — otherwise both
        // threads could race for ClaimDisposalOnce() itself, which this
        // test does not care about (that race is exercised separately by
        // ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount).
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

        // The loser must NOT have returned yet — the winner is still
        // paused inside cleanup, so disposed_ must still be false, and the
        // loser must be blocked in WaitForDisposalToComplete().
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_FALSE(loserReturned.load());

        // Release the winner; both threads must now complete cleanly.
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            releaseWinner = true;
        }
        gateCv.notify_all();

        winnerThread.join();
        loserThread.join();

        EXPECT_TRUE(loserReturned.load());
    }

    std::vector<std::unique_ptr<Accelerometer>> instances;
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_NO_THROW(instances.push_back(std::make_unique<Accelerometer>()));
    }

    EXPECT_THROW({ const Accelerometer overflow; (void)overflow; }, SensorFailedException);
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

// Task P5-6: CurrentValueChangedReceivesExpectedReading above only proves
// the *event* carries the right data — it never asserts on
// getCurrentValueProperty()/getIsDataValidProperty() themselves, since
// those independently throw on unsupported hardware (Task P3-1) regardless
// of any synthetic injection. SetSupportedForTesting(true) closes that gap
// by making isSupported_ true without needing real hardware, so this test
// can assert on the getters directly.
TEST(AccelerometerTests, InjectSyntheticSensorUpdateUpdatesCurrentValueWhenMarkedSupported)
{
    Accelerometer a;
    a.SetSupportedForTesting(true);
    a.SetStartedForTesting(true);

    // Before any dispatch, isDataValid_ is still its default (false) and
    // CurrentValue is still a default-constructed reading — confirms this
    // test's later assertions are actually detecting InjectSyntheticSensorUpdate()'s
    // effect, not a value that was already there beforehand.
    EXPECT_FALSE(a.getIsDataValidProperty());
    EXPECT_EQ(a.getCurrentValueProperty().getAccelerationProperty(), Vector3::Zero);

    constexpr float StandardGravity = 9.80665f;
    const float rawX = 0.0f;
    const float rawY = StandardGravity;
    const float rawZ = 0.0f;

    a.InjectSyntheticSensorUpdate(rawX, rawY, rawZ);

    EXPECT_TRUE(a.getIsDataValidProperty());
    const Vector3 expectedAcceleration(rawX / StandardGravity, rawY / StandardGravity, rawZ / StandardGravity);
    EXPECT_EQ(a.getCurrentValueProperty().getAccelerationProperty(), expectedAcceleration);
}

// Task P5-6: without SetSupportedForTesting(), getCurrentValueProperty()
// must still throw on unsupported hardware — confirming
// SetStartedForTesting()+InjectSyntheticSensorUpdate() alone never bypass
// the isSupported_ gate. This is the real, intentional contract (Task
// P3-1), not a gap; this test makes it explicit and enforced rather than
// only implied by a code comment.
TEST(AccelerometerTests, GetCurrentValuePropertyStillThrowsAfterSyntheticUpdateWhenNotMarkedSupported)
{
    if (Accelerometer::getIsSupportedProperty())
    {
        GTEST_SKIP() << "Accelerometer is supported on this platform; unsupported-path test not applicable.";
    }

    Accelerometer a;
    a.SetStartedForTesting(true);
    a.InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);

    EXPECT_THROW((void)a.getCurrentValueProperty(), System::InvalidOperationException);
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

// Task P6-4: a CurrentValueChanged handler that throws during
// InjectSyntheticSensorUpdate() previously left dispatchingThreadIds_
// permanently corrupted (its cleanup was a plain post-call statement,
// skipped entirely on an exception) — any current or future Dispose() call
// on the same instance would then hang forever waiting for a dispatch that
// can never finish draining. If this test hangs, ScopeExit's cleanup-on-throw
// guarantee (Task P6-4) has regressed — gtest/CI will show it as a timeout.
TEST(AccelerometerTests, ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose)
{
    auto accelerometer = std::make_unique<Accelerometer>();
    accelerometer->SetStartedForTesting(true);

    accelerometer->CurrentValueChanged += [](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        throw std::runtime_error("synthetic handler failure");
    };

    EXPECT_THROW(accelerometer->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f), std::runtime_error);

    EXPECT_NO_THROW(accelerometer->Dispose());
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

// Task P5-2: exercises inFlightCallbackCount_ under real concurrent
// contention (this environment has no real hardware to deliver genuinely
// concurrent SDL_EVENT_SENSOR_UPDATE events, so this uses real std::thread
// concurrency on InjectSyntheticSensorUpdate() instead, which goes through
// the exact same counter/lock/condition_variable machinery as the real SDL
// event-watch path). Confirms: no crash/UB when the count legitimately
// goes above 1 (multiple threads dispatching to the same instance at
// once), every dispatch's event is still received, and Dispose() called
// afterward correctly waits for the count to fully drain rather than
// returning early on some intermediate zero crossing.
TEST(AccelerometerTests, ConcurrentSyntheticUpdatesDoNotCrashAndDrainBeforeDispose)
{
    auto accelerometer = std::make_unique<Accelerometer>();
    accelerometer->SetStartedForTesting(true);

    std::atomic<int> receivedCount{0};
    accelerometer->CurrentValueChanged += [&receivedCount](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        // Widens the in-flight window so concurrent threads are likely to
        // genuinely overlap rather than happen to serialize.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++receivedCount;
    };

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 10;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&accelerometer]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                accelerometer->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f);
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_EQ(receivedCount.load(), ThreadCount * IterationsPerThread);
    EXPECT_NO_THROW(accelerometer->Dispose());
}

// Task P5-3: a CurrentValueChanged handler that disposes its own sender
// reentrantly, from within the same dispatch that's invoking it, used to
// be a documented, accepted deadlock (Accelerometer.hpp's own comment
// history, Task P4-2). If this test hangs, it has regressed — gtest/CI
// will show it as a timeout, not a clean assertion failure, which is the
// nature of testing a deadlock fix.
TEST(AccelerometerTests, DisposeFromWithinOwnCallbackDoesNotDeadlock)
{
    auto accelerometer = std::make_unique<Accelerometer>();
    accelerometer->SetStartedForTesting(true);

    bool handlerRan = false;
    accelerometer->CurrentValueChanged += [&](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        handlerRan = true;
        accelerometer->Dispose();
    };

    EXPECT_NO_THROW(accelerometer->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(handlerRan);

    // The reentrant call above already disposed it; a second, external
    // Dispose() call must still throw exactly as it would for any other
    // already-disposed instance.
    EXPECT_THROW(accelerometer->Dispose(), System::ObjectDisposedException);
}

// Task P7-3: reproduces the use-after-free the audit found in
// SdlSensorSubsystem<TSensor>::SensorEventWatch()'s old bookkeeping. Two
// started instances A and B are dispatched in the same simulated batch
// (A first). A's CurrentValueChanged handler disposes B *by resetting B's
// owning unique_ptr* — not merely calling Dispose() — so B's underlying
// memory is actually freed before the loop reaches its second (B's) turn,
// giving a genuine dangling pointer rather than a merely-logically-disposed
// object. If DispatchToInstances()'s per-instance re-validation against the
// live startedInstances_ list ever regresses back to the old "mark
// everything up front" bookkeeping, this test either crashes/corrupts the
// heap (calling ProcessSensorUpdateEvent-equivalent code through
// DispatchSensorReading on freed memory) or, at minimum, fails the
// bCallbackCalled assertion below.
TEST(AccelerometerTests, DisposingDifferentInstanceDuringSameBatchDispatchDoesNotUseAfterFree)
{
    auto a = std::make_unique<Accelerometer>();
    auto b = std::make_unique<Accelerometer>();

    // started_ (not just subsystem-level registration) must be true so
    // that b's own Dispose(bool) cleanup actually calls Stop() — which is
    // what removes b from startedInstances_. Without this, b would remain
    // in startedInstances_ after being freed below, and the fix's
    // re-validation step would (correctly, given that stale state) still
    // find it "present" and dispatch into freed memory — this test needs
    // b's unregistration to actually happen for the fix to have anything
    // to check.
    a->SetStartedForTesting(true);
    b->SetStartedForTesting(true);
    Accelerometer::RegisterStartedInstanceForTesting(*a);
    Accelerometer::RegisterStartedInstanceForTesting(*b);

    Accelerometer* bRawPtr = b.get();
    bool bCallbackCalled = false;
    bRawPtr->CurrentValueChanged += [&bCallbackCalled](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        bCallbackCalled = true;
    };

    bool aCallbackCalled = false;
    a->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        aCallbackCalled = true;
        // Disposes AND frees B's memory (unlike a bare Dispose() call,
        // which would leave B's memory allocated but logically disposed —
        // this makes bRawPtr a genuinely dangling pointer afterward).
        b.reset();
    };

    const std::vector<Accelerometer*> batch{a.get(), bRawPtr};
    EXPECT_NO_THROW(Accelerometer::DispatchToInstancesForTesting(batch, 1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(aCallbackCalled);
    EXPECT_FALSE(bCallbackCalled);

    EXPECT_NO_THROW(a->Dispose());
}

// Task P8-1: DispatchSensorReading() raises CurrentValueChanged, then
// unconditionally touches `this` again (getIsDataValidProperty()) before deciding
// whether to also raise the legacy ReadingChanged event — so destroying this same
// instance from within a *CurrentValueChanged* handler is NOT safe for
// Accelerometer specifically (documented, not tested — see this class's own
// dispatchToken_ doc comment and plan_devices_phase8.md Task P8-1; the project has
// no death-test convention to exercise the known-unsafe path directly). ReadingChanged
// is the *last* statement DispatchSensorReading() executes, though — nothing touches
// `this` afterward — so destroying the instance from within ReadingChanged's handler
// is safe, exactly like Gyroscope's single-event case. This test proves that specific,
// safe boundary.
TEST(AccelerometerTests, SelfDestroyingFromOwnReadingChangedCallbackDuringInjectSyntheticSensorUpdateDoesNotUseAfterFree)
{
    auto accelerometer = std::make_unique<Accelerometer>();
    accelerometer->SetStartedForTesting(true);

    Accelerometer* rawPtr = accelerometer.get();
    bool handlerRan = false;
    rawPtr->ReadingChanged += [&](System::Object*, const AccelerometerReadingEventArgs&)
    {
        handlerRan = true;
        accelerometer.reset();
    };

    EXPECT_NO_THROW(rawPtr->InjectSyntheticSensorUpdate(1.0f, 0.0f, 0.0f));
    EXPECT_TRUE(handlerRan);
}

// Task P8-5: DispatchToInstances()'s own doc comment claims that if one instance's
// callback throws, the exception is swallowed per-instance and the dispatch loop
// still moves on to the next instance in the batch — but until this task, nothing
// exercised that claim at the *batch* level. Task P6-4's
// ThrowingCallbackDuringSyntheticUpdateStillCleansUpAndDoesNotHangDispose only
// proves a single instance's own dispatch survives its own handler throwing; it
// says nothing about whether a *different*, later instance in the same batch still
// gets dispatched to.
TEST(AccelerometerTests, ThrowingHandlerInBatchDispatchDoesNotPreventNextInstanceFromReceivingItsEvent)
{
    auto a = std::make_unique<Accelerometer>();
    auto b = std::make_unique<Accelerometer>();

    a->SetStartedForTesting(true);
    b->SetStartedForTesting(true);
    Accelerometer::RegisterStartedInstanceForTesting(*a);
    Accelerometer::RegisterStartedInstanceForTesting(*b);

    bool aCallbackCalled = false;
    a->CurrentValueChanged += [&aCallbackCalled](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        aCallbackCalled = true;
        throw std::runtime_error("a's handler deliberately fails");
    };

    bool bCallbackCalled = false;
    b->CurrentValueChanged += [&bCallbackCalled](
        System::Object*, const SensorReadingEventArgs<AccelerometerReading>&)
    {
        bCallbackCalled = true;
    };

    const std::vector<Accelerometer*> batch{a.get(), b.get()};
    EXPECT_NO_THROW(Accelerometer::DispatchToInstancesForTesting(batch, 1.0f, 2.0f, 3.0f));

    EXPECT_TRUE(aCallbackCalled);
    EXPECT_TRUE(bCallbackCalled);

    // Also confirms A's own dispatch-tracking state was cleaned up despite the
    // throw (no hang, matching Task P6-4's single-instance guarantee).
    EXPECT_NO_THROW(a->Dispose());
    EXPECT_NO_THROW(b->Dispose());
}
