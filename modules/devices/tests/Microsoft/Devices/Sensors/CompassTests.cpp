// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "../Detail/ProcSelfResourceCounters.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Detail/ICompassBackend.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Compass;
using Microsoft::Devices::Sensors::CompassReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Vector3;
using System::TimeSpan;

namespace
{
    // Task DEVICES-0096: a fake ICompassBackend (not Detail::AndroidCompassBackend,
    // which is #ifdef __ANDROID__-only and cannot compile on this host) —
    // lets these tests exercise Compass::Start()/CurrentValueChanged/Calibrate's
    // delegation to *any* backend without needing real Android hardware.
    class FakeCompassBackend final : public Microsoft::Devices::Sensors::Detail::ICompassBackend
    {
    public:
        bool SupportedResult = true;
        bool StartResult = true;
        // Task TEST2-001 (2026-07-17): atomic, not plain bool/int -- LIFE-001's
        // own design deliberately allows a concurrent Stop() and Start()'s own
        // orphaned-attempt cleanup to both call backend Stop() on this fake
        // from two different threads (see ConcurrentStopDuringStartDoesNotDeadlock
        // below), exactly as the real AndroidSensorBridge already tolerates
        // (Task ANDROID-BRIDGE-003). A plain bool/int here is a genuine data
        // race under that scenario -- TSan caught it -- even though the
        // production two-phase design itself has no bug; only this fake's own
        // unsynchronized bookkeeping did.
        std::atomic<bool> StopCalled{false};
        int StartCallCount = 0;
        std::atomic<int> StopCallCount{0};
        int SetSampleIntervalCallCount = 0;
        System::TimeSpan LastSetSampleInterval;
        ReadingCallback CapturedOnReading;
        CalibrationCallback CapturedOnCalibrationNeeded;

        // Task TEST2-001 (LIFE-001/LIFE-002 regression coverage): if set,
        // invoked synchronously from within Start(), after capturing the
        // callbacks but *before* Start() returns -- lets a test exercise
        // "the backend delivers a sample/calibration event, or a different
        // thread calls Stop(), before Compass::Start() itself has committed
        // Ready," without needing a real Android bridge.
        std::function<void()> OnStartCalledBeforeReturn;

        [[nodiscard]] bool IsSupported() override
        {
            return SupportedResult;
        }

        bool Start(const System::TimeSpan&, ReadingCallback onReading, CalibrationCallback onCalibrationNeeded) override
        {
            ++StartCallCount;
            if (!StartResult)
            {
                return false;
            }
            CapturedOnReading = std::move(onReading);
            CapturedOnCalibrationNeeded = std::move(onCalibrationNeeded);
            if (OnStartCalledBeforeReturn)
            {
                OnStartCalledBeforeReturn();
            }
            return true;
        }

        void Stop() override
        {
            StopCalled = true;
            ++StopCallCount;
        }

        void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates) override
        {
            ++SetSampleIntervalCallCount;
            LastSetSampleInterval = timeBetweenUpdates;
        }
    };
} // namespace

TEST(CompassTests, GetIsSupportedPropertyDoesNotCrash)
{
    EXPECT_FALSE(Compass::getIsSupportedProperty());
}

TEST(CompassTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Compass c; (void)c; });
}

// Task SENSORBASE-002: see AccelerometerTests.cpp's identical test for the
// full rationale (MonoGame cross-check confirms the real WP7 SensorBase<T>'s
// single shared 2ms default, not a per-sensor-class override).
TEST(CompassTests, DefaultTimeBetweenUpdatesIsTwoMilliseconds)
{
    const Compass c;
    EXPECT_EQ(c.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(2.0));
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

// Task SENSORBASE-006: no test anywhere confirmed that Dispose() itself
// (without an explicit Stop() call first) actually stops a running backend
// -- StopCallsBackendStop below only tests an explicit Stop() call.
// Dispose(bool)'s own wasStarted-then-Stop() branch (Compass.cpp) is
// exercised here directly via the fake backend, confirming no backend
// resources are left running across a Start()->Dispose() cycle (this
// task's acceptance criteria; also covered under devices-asan as part of
// this task's standard verification pass).
TEST(CompassTests, DisposeWhileStartedCallsBackendStopWithoutExplicitStopFirst)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    ASSERT_FALSE(fake->StopCalled);

    EXPECT_NO_THROW(c.Dispose());

    EXPECT_TRUE(fake->StopCalled);
}

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(CompassTests, StopAfterDisposeThrows)
{
    Compass c;
    c.Dispose();
    EXPECT_THROW(c.Stop(), System::ObjectDisposedException);
}

// Task DEVICES-0056: no test anywhere asserted Start()-after-Dispose()
// throws ObjectDisposedException specifically — Start()'s own disposed-check
// (Compass.cpp, top of Start()) runs before the always-throws-
// SensorFailedException stub body, but that ordering wasn't confirmed by a
// test; a regression could silently swap the exception type.
TEST(CompassTests, StartAfterDisposeThrows)
{
    Compass c;
    c.Dispose();
    EXPECT_THROW(c.Start(), System::ObjectDisposedException);
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

// Task P3-6: CurrentValueChanged subscription. On this (non-Android)
// platform Compass is still a permanent NotSupported stub — SDL3 never
// gains magnetometer support, and no backend is selected here — so the
// event can't fire in this environment; this only confirms subscribing
// doesn't crash. See WithInjectedSupportedBackendStartSucceeds below for
// the case where a backend genuinely delivers readings (via a fake, since
// Android's real Detail::AndroidCompassBackend can't run on this host).
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

// Task P3-8: Calibrate subscription. Same stub limitation on this platform
// as above — no backend is selected here, so Calibrate can't fire; this
// only confirms subscribing doesn't crash. See
// CalibrateFiresFromBackendCalibrationCallback below for the
// backend-delivers-a-real-calibration-event case.
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

// Task DEVICES-0096: with a supported fake backend injected, Start() must
// actually succeed (not throw) and transition to Ready — proving the
// delegation path itself works, independent of whether a real Android
// device is available.
TEST(CompassTests, WithInjectedSupportedBackendStartSucceeds)
{
    Compass c;
    c.SetBackendForTesting(std::make_unique<FakeCompassBackend>());

    EXPECT_NO_THROW(c.Start());
    EXPECT_EQ(c.getStateProperty(), SensorState::Ready);
}

// A backend reporting IsSupported() == false must still result in the
// existing SensorFailedException contract — supported-backend and
// no-backend cases must be indistinguishable from the caller's perspective.
TEST(CompassTests, WithInjectedUnsupportedBackendStartStillThrows)
{
    Compass c;
    auto fake = std::make_unique<FakeCompassBackend>();
    fake->SupportedResult = false;
    c.SetBackendForTesting(std::move(fake));

    EXPECT_THROW(c.Start(), SensorFailedException);
    EXPECT_EQ(c.getStateProperty(), SensorState::NotSupported);
}

// Confirms CurrentValueChanged actually fires with the backend's own
// reading data once Start() has wired the callback through — proves the
// C++ delegation plumbing (Compass::Start() -> ICompassBackend::Start() ->
// setCurrentValueProperty()) end to end, without needing real hardware.
TEST(CompassTests, CurrentValueChangedFiresFromBackendReading)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    bool invoked = false;
    CompassReading received;
    c.CurrentValueChanged += [&invoked, &received](
        System::Object*, const SensorReadingEventArgs<CompassReading>& args)
    {
        invoked = true;
        received = args.getSensorReadingProperty();
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading synthetic(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    fake->CapturedOnReading(synthetic);

    ASSERT_TRUE(invoked);
    EXPECT_EQ(received.getMagneticHeadingProperty(), 42.0);
    EXPECT_TRUE(c.getIsDataValidProperty());
    EXPECT_EQ(c.getCurrentValueProperty().getMagneticHeadingProperty(), 42.0);
}

// Task DEVPERF-004 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// mirrors AccelerometerTests/GyroscopeTests.RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt
// -- Compass dispatches CurrentValueChanged through the same
// SensorBase<T>::SetCurrentValueAndMarkDataValid()/System::EventHandler<T>::Raise()
// path as Accelerometer/Gyroscope, so it must honor the same snapshot-based
// handler-list mutation guarantee, proven here via the fake backend's
// synchronous CapturedOnReading() callback rather than InjectSyntheticSensorUpdate().
TEST(CompassTests, RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    using Args = SensorReadingEventArgs<CompassReading>;
    using Token = System::EventHandler<Args>::Token;

    Token secondToken{};
    bool secondHandlerInvoked = false;

    c.CurrentValueChanged.Add(
        [&c, &secondToken](System::Object*, const Args&)
        {
            c.CurrentValueChanged.Remove(secondToken);
        });

    secondToken = c.CurrentValueChanged.Add(
        [&secondHandlerInvoked](System::Object*, const Args&)
        {
            secondHandlerInvoked = true;
        });

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading firstReading(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    fake->CapturedOnReading(firstReading);

    EXPECT_TRUE(secondHandlerInvoked);

    secondHandlerInvoked = false;
    const CompassReading secondReading(
        6.0, 43.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 43.0);
    fake->CapturedOnReading(secondReading);
    EXPECT_FALSE(secondHandlerInvoked);
}

// Task DEVPERF-004: mirrors AccelerometerTests/GyroscopeTests.
// HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState -- proves the same
// reentrancy guarantee holds for Compass's CurrentValueChanged dispatch when driven
// by a backend callback rather than a synthetic-injection test hook.
TEST(CompassTests, HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    bool reentered = false;
    std::vector<double> observedHeadings;

    c.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<CompassReading>& args)
    {
        observedHeadings.push_back(args.getSensorReadingProperty().getMagneticHeadingProperty());
        if (!reentered)
        {
            reentered = true;
            const CompassReading innerReading(
                6.0, 99.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 99.0);
            fake->CapturedOnReading(innerReading);
        }
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading outerReading(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    fake->CapturedOnReading(outerReading);

    ASSERT_EQ(observedHeadings.size(), 2u);
    EXPECT_EQ(observedHeadings[0], 42.0);
    EXPECT_EQ(observedHeadings[1], 99.0);
    EXPECT_EQ(c.getCurrentValueProperty().getMagneticHeadingProperty(), 99.0);
}

// Task READINGS-003 (2026-07-06): the real timestamp-setting logic for
// Compass lives entirely in Detail::AndroidCompassBackend::PublishReading()
// (Android-only, #ifdef __ANDROID__, not reachable on this host), which
// this codebase's own wall-clock timestamp policy documents as always using
// System::DateTimeOffset::getUtcNowProperty() -- see that method's own doc
// comment. What *is* testable here, and was not previously covered by any
// test in this file, is that Compass::Start()'s callback lambda forwards
// whatever CompassReading the backend hands it through to
// CurrentValueChanged/CurrentValue completely unmodified -- i.e. the
// propagation path itself introduces no truncation, clamping, or
// re-timestamping of its own. A fixed, deliberately-distinguishable
// timestamp (not a fresh getUtcNowProperty() call at test time) proves
// exact passthrough rather than a loose "close enough" bracket check.
TEST(CompassTests, CurrentValueChangedPropagatesBackendTimestampExactly)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    System::DateTimeOffset receivedTimestamp;
    c.CurrentValueChanged += [&receivedTimestamp](
        System::Object*, const SensorReadingEventArgs<CompassReading>& args)
    {
        receivedTimestamp = args.getSensorReadingProperty().getTimestampProperty();
    };

    const System::DateTimeOffset fixedTimestamp(System::DateTime(637000000000000000LL), System::TimeSpan::Zero);
    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading synthetic(5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), fixedTimestamp, 42.0);
    fake->CapturedOnReading(synthetic);

    EXPECT_EQ(receivedTimestamp, fixedTimestamp);
    EXPECT_EQ(c.getCurrentValueProperty().getTimestampProperty(), fixedTimestamp);
}

// Task SENSORBASE-005: mirrors AccelerometerTests.
// CurrentValueAndIsDataValidRetainLastReadingAfterStop -- see that test for
// the full rationale. Compass::Stop() only clears started_/state_
// (confirmed by reading Compass::Stop() directly), so the last known
// reading and its validity are expected to persist.
TEST(CompassTests, CurrentValueAndIsDataValidRetainLastReadingAfterStop)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading synthetic(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    fake->CapturedOnReading(synthetic);
    ASSERT_TRUE(c.getIsDataValidProperty());

    c.Stop();

    EXPECT_TRUE(c.getIsDataValidProperty());
    EXPECT_EQ(c.getCurrentValueProperty().getMagneticHeadingProperty(), 42.0);
}

// Confirms Calibrate actually fires when the backend invokes its
// calibration-needed callback — proves the second delegation path
// (ICompassBackend's CalibrationCallback -> Compass::Calibrate.Raise()).
TEST(CompassTests, CalibrateFiresFromBackendCalibrationCallback)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    bool invoked = false;
    c.Calibrate += [&invoked](System::Object*, const CalibrationEventArgs&) { invoked = true; };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnCalibrationNeeded));
    fake->CapturedOnCalibrationNeeded();

    EXPECT_TRUE(invoked);
}

// Task DEVPERF-004 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// Calibrate.Raise() (Compass.cpp's calibration-needed lambda) goes through the same
// System::EventHandler<T>::Raise() snapshot mechanism as CurrentValueChanged, but was
// never itself proven -- only CurrentValueChanged had a "handler removes another
// handler during dispatch" test before this task. Closes that gap for Calibrate
// specifically, not just by analogy to CurrentValueChanged's own coverage.
TEST(CompassTests, RemovingAnotherNotYetInvokedCalibrateHandlerDuringDispatchStillInvokesIt)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    using Token = System::EventHandler<CalibrationEventArgs>::Token;

    Token secondToken{};
    bool secondHandlerInvoked = false;

    c.Calibrate.Add(
        [&c, &secondToken](System::Object*, const CalibrationEventArgs&)
        {
            c.Calibrate.Remove(secondToken);
        });

    secondToken = c.Calibrate.Add(
        [&secondHandlerInvoked](System::Object*, const CalibrationEventArgs&)
        {
            secondHandlerInvoked = true;
        });

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnCalibrationNeeded));
    fake->CapturedOnCalibrationNeeded();

    EXPECT_TRUE(secondHandlerInvoked);

    secondHandlerInvoked = false;
    fake->CapturedOnCalibrationNeeded();
    EXPECT_FALSE(secondHandlerInvoked);
}

// Task SENSORBASE-003: unlike Accelerometer/Gyroscope, this exact reentrancy
// scenario (an event handler calling Dispose() on its own sender, from
// within a callback the sender itself triggered) had never been tested for
// Compass at all before this task -- confirmed by grep, zero such tests
// existed. This proves Compass's own ClaimDisposalOnce()/Stop() reentrancy
// handling (shared with every SensorBase<T> derivative) doesn't deadlock or
// throw unexpectedly when driven through the fake-backend seam. It does
// NOT prove the deeper question of what happens if the real
// Detail::AndroidCompassBackend/AndroidSensorBridge chain is torn down
// (backend_ destroyed) while still executing one of its own member
// functions further up the call stack (e.g. AndroidCompassBackend::
// PublishReading() calling back into a handler that deletes the owning
// Compass instance, not just Dispose()s it) -- that scenario is Android-only
// and requires real hardware or an Android ASan build this container cannot
// run; left explicitly documented as unverified, not silently assumed safe,
// in plans/plan_devices.md's SENSORBASE-003 closing note.
TEST(CompassTests, DisposeFromWithinOwnCallbackDoesNotDeadlock)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    bool handlerRan = false;
    c.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<CompassReading>&)
    {
        handlerRan = true;
        c.Dispose();
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const CompassReading synthetic(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    EXPECT_NO_THROW(fake->CapturedOnReading(synthetic));
    EXPECT_TRUE(handlerRan);

    // The reentrant call above already disposed it; a second, external
    // Dispose() call must still throw exactly as it would for any other
    // already-disposed instance.
    EXPECT_THROW(c.Dispose(), System::ObjectDisposedException);
}

// Confirms Stop() actually calls through to the backend's own Stop(), not
// just flipping Compass's own started_/state_ bookkeeping.
TEST(CompassTests, StopCallsBackendStop)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    c.Stop();

    EXPECT_TRUE(fake->StopCalled);
    EXPECT_EQ(c.getStateProperty(), SensorState::Disabled);
}

// Task ANDROID-BRIDGE-002: SensorBase<T>::TimeBetweenUpdatesChanged is wired
// (Compass::Compass()) to forward the new value to the live backend, so
// Compass/Motion's Android bridge can honor a TimeBetweenUpdates change
// while already running, without requiring Stop()/Start().
TEST(CompassTests, SetTimeBetweenUpdatesPropertyForwardsToBackend)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));
    c.Start();

    c.setTimeBetweenUpdatesProperty(System::TimeSpan::FromMilliseconds(50.0));

    EXPECT_EQ(fake->SetSampleIntervalCallCount, 1);
    EXPECT_EQ(fake->LastSetSampleInterval, System::TimeSpan::FromMilliseconds(50.0));
}

// setTimeBetweenUpdatesProperty()'s own contract (SensorBase.hpp) only
// raises TimeBetweenUpdatesChanged when the value actually changes.
TEST(CompassTests, SetTimeBetweenUpdatesPropertyToSameValueDoesNotForwardToBackend)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));

    const System::TimeSpan current = c.getTimeBetweenUpdatesProperty();
    c.setTimeBetweenUpdatesProperty(current);

    EXPECT_EQ(fake->SetSampleIntervalCallCount, 0);
}

// Stabilization pass, Task 1 (repeated Start/Stop safety): calling Start()
// a second time while already started must not call through to the
// backend a second time (which, for the real AndroidCompassBackend, would
// crash by reassigning an already-joinable std::thread) -- it must throw a
// clear, documented failure instead, matching Accelerometer::Start()'s own
// "already started" convention.
TEST(CompassTests, StartTwiceThrowsWithoutCallingBackendAgain)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));

    c.Start();
    ASSERT_EQ(fake->StartCallCount, 1);

    EXPECT_THROW(c.Start(), SensorFailedException);
    EXPECT_EQ(fake->StartCallCount, 1); // Not called again.
    EXPECT_EQ(c.getStateProperty(), SensorState::Ready); // Unchanged by the failed second Start().
}

// After Stop(), Start() must succeed again (a restart, not a permanent
// lockout) and does call through to the backend again.
TEST(CompassTests, StartAfterStopSucceedsAndCallsBackendAgain)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));

    c.Start();
    c.Stop();
    EXPECT_NO_THROW(c.Start());
    EXPECT_EQ(fake->StartCallCount, 2);
}

// Stabilization pass, Task 6 (SetBackendForTesting() contract): the header
// documents "must be called before Start()" -- confirm this is now
// enforced, not just documented, and that the original backend is left
// completely untouched (still the one Stop() reaches) rather than silently
// swapped out.
TEST(CompassTests, SetBackendForTestingAfterStartThrowsAndDoesNotReplaceBackend)
{
    Compass c;
    auto firstOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* first = firstOwned.get();
    c.SetBackendForTesting(std::move(firstOwned));
    c.Start();

    // std::move(second) transfers ownership into the call before
    // SetBackendForTesting()'s body even runs, so if it throws (as
    // expected here), the moved-in unique_ptr is destroyed as the
    // exception unwinds -- do not dereference the raw pointer afterward.
    EXPECT_THROW(
        c.SetBackendForTesting(std::make_unique<FakeCompassBackend>()),
        SensorFailedException);

    // The original backend must still be the one attached -- proven by
    // Stop() reaching it, not a silently-swapped-in replacement.
    c.Stop();
    EXPECT_TRUE(first->StopCalled);
}

// Task SENSORBASE-004: mirrors AccelerometerTests.
// ConcurrentStartStopFromMultipleThreadsDoesNotCrash -- unlike Accelerometer/
// Gyroscope (whose started_/state_ are guarded by their shared
// Detail::SdlSensorSubsystem<TSensor>'s subsystem.mutex_), Compass has no
// per-instance mutex guarding started_/state_ at all (confirmed by reading
// Compass.hpp/.cpp directly -- only a *static* instanceCountMutex_ exists,
// which guards the shared instance counter, not per-instance state). This
// test exists specifically to let devices-tsan answer, empirically, whether
// that's a real, exploitable data race or merely a theoretical one nothing
// ever actually exercises concurrently.
TEST(CompassTests, ConcurrentStartStopFromMultipleThreadsDoesNotCrash)
{
    Compass c;
    c.SetBackendForTesting(std::make_unique<FakeCompassBackend>());

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&c]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                try
                {
                    c.Start();
                }
                catch (const SensorFailedException&)
                {
                    // Expected: another thread already started it first.
                }

                c.Stop();
                (void)c.getStateProperty();
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

// Task DEV-AUD-006 (2026-07-16, external audit `audit_devices.md`):
// TimeBetweenUpdatesChanged's handler previously read backend_ without
// mutex_, while SetBackendForTesting() replaces the same unique_ptr under
// mutex_ -- a real data race even with nothing ever Start()ed (so
// SetBackendForTesting() never throws here). This stress test exists
// specifically to let devices-tsan confirm the fix empirically, mirroring
// ConcurrentStartStopFromMultipleThreadsDoesNotCrash's own precedent.
TEST(CompassTests, ConcurrentSetTimeBetweenUpdatesAndSetBackendForTestingDoesNotCrash)
{
    Compass c;

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&c, t]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                if (t % 2 == 0)
                {
                    c.setTimeBetweenUpdatesProperty(TimeSpan::FromMilliseconds(1.0 + static_cast<double>(i)));
                }
                else
                {
                    c.SetBackendForTesting(std::make_unique<FakeCompassBackend>());
                }
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

// Task LIFE-002 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// a backend that synchronously invokes its reading callback from within its
// own Start() call -- before Start() returns and commits Ready -- must be
// handled safely: no deadlock, no corrupted state, and the reading is
// genuinely published. Previously Start() held mutex_ for its entire body,
// so this exact scenario would have been merely "safe" by accident (no
// reentrant call in the callback needed the same lock); this test exists to
// pin the behavior down as a permanent regression test now that the lock is
// released before calling into the backend.
TEST(CompassTests, SynchronousReadingCallbackDuringStartIsHandledSafely)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));

    bool invoked = false;
    CompassReading received;
    c.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<CompassReading>& args)
    {
        invoked = true;
        received = args.getSensorReadingProperty();
    };

    const CompassReading synthetic(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);
    fake->OnStartCalledBeforeReturn = [fake, synthetic]()
    {
        fake->CapturedOnReading(synthetic);
    };

    EXPECT_NO_THROW(c.Start());

    EXPECT_TRUE(invoked);
    EXPECT_EQ(received.getMagneticHeadingProperty(), 42.0);
    EXPECT_EQ(c.getStateProperty(), SensorState::Ready);
}

// Task LIFE-001 (2026-07-17): a concurrent Stop() call (from another thread)
// while Start() is still inside its own backend_->Start() call must not
// deadlock (the prior design held mutex_ across the entire backend_->Start()
// call, so a concurrent Stop() would have blocked on that same lock for the
// whole duration instead of proceeding independently), and must leave the
// instance in a consistent, fully-stopped state once both calls return.
// backend_->Stop() is called twice here (once by the superseding Stop(),
// once by Start()'s own orphaned-attempt cleanup) -- both against the same
// idempotent fake, mirroring the real AndroidSensorBridge/AndroidCompassBackend
// contract that repeated Stop() calls are always safe.
TEST(CompassTests, ConcurrentStopDuringStartDoesNotDeadlock)
{
    Compass c;
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    c.SetBackendForTesting(std::move(fakeOwned));

    fake->OnStartCalledBeforeReturn = [&c]()
    {
        std::thread stopper([&c]() { c.Stop(); });
        stopper.join();
    };

    // Start() itself still reports its own genuine outcome (the fake's
    // Start() returned true) even though a concurrent Stop() superseded it
    // before Start() could commit Ready.
    EXPECT_NO_THROW(c.Start());

    EXPECT_EQ(fake->StopCallCount, 2);
    EXPECT_EQ(c.getStateProperty(), SensorState::Disabled);
}

// Task LIFE-005 (2026-07-17): mirrors the exact hazard the audit found in
// AndroidCompassBackend::HandleMagneticFieldSample() -- a CurrentValueChanged
// handler fully destroys (not just Dispose()s) the owning Compass; a
// *separately captured* calibration callback, invoked afterward exactly as
// the real backend does, must not touch the destroyed object.
TEST(CompassTests, DestroyingOwnerFromCurrentValueChangedThenFiringCalibrateDoesNotCrash)
{
    auto compass = std::make_unique<Compass>();
    auto fakeOwned = std::make_unique<FakeCompassBackend>();
    FakeCompassBackend* fake = fakeOwned.get();
    compass->SetBackendForTesting(std::move(fakeOwned));
    compass->Start();

    bool calibrateInvoked = false;
    compass->Calibrate += [&calibrateInvoked](System::Object*, const CalibrationEventArgs&)
    {
        calibrateInvoked = true;
    };

    bool handlerRan = false;
    compass->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<CompassReading>&)
    {
        handlerRan = true;
        compass.reset(); // full destruction, not just Dispose(), from within this callback
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnCalibrationNeeded));
    const CompassReading synthetic(
        5.0, 42.0, Vector3(1.0f, 2.0f, 3.0f), System::DateTimeOffset::getUtcNowProperty(), 42.0);

    // Mirrors AndroidCompassBackend::HandleMagneticFieldSample()'s own
    // ordering exactly: the calibration callback is captured *before* the
    // reading callback runs, then invoked *after* it.
    auto calibrationCallback = fake->CapturedOnCalibrationNeeded;
    EXPECT_NO_THROW(fake->CapturedOnReading(synthetic));
    EXPECT_TRUE(handlerRan);
    EXPECT_NO_THROW(calibrationCallback());

    EXPECT_FALSE(calibrateInvoked); // the owner was already gone before this fired
}

// Task PERF2-002 (2026-07-18, external audit `audit_devices_2026-07-17.md`): at least 100,000
// construct/backend-inject/Start/Stop/Dispose cycles, checking this process's own
// open-file-descriptor and thread counts return to baseline afterward -- see
// AccelerometerTests.OneHundredThousandConstructProbeStartStopDisposeCyclesLeaveNoResourceLeak
// for the full rationale (LeakSanitizer non-functional in this container, no existing test in
// this file runs anywhere near this scale). Uses FakeCompassBackend (SupportedResult/StartResult
// both true by default) rather than a real backend, matching every other host-runnable Compass
// test in this file -- Compass has no real backend on this host at all (Android-NDK-only).
TEST(CompassTests, OneHundredThousandConstructBackendInjectStartStopDisposeCyclesLeaveNoResourceLeak)
{
#if !defined(__linux__)
    GTEST_SKIP() << "FD/thread leak tracking is Linux-specific (/proc/self/fd, /proc/self/status)";
#else
    constexpr int Cycles = 100000;

    {
        Compass warmup;
        warmup.SetBackendForTesting(std::make_unique<FakeCompassBackend>());
    }

    const int fdBefore = CnaTestSupport::CountOpenFileDescriptors();
    const int threadsBefore = CnaTestSupport::GetThreadCount();
    ASSERT_GE(fdBefore, 0);
    ASSERT_GE(threadsBefore, 0);

    for (int i = 0; i < Cycles; ++i)
    {
        Compass c;
        c.SetBackendForTesting(std::make_unique<FakeCompassBackend>());
        EXPECT_NO_THROW(c.Start());
        EXPECT_NO_THROW(c.Stop());
        // c's destructor (Dispose(bool)) runs here, at the end of each iteration's scope.
    }

    EXPECT_EQ(CnaTestSupport::CountOpenFileDescriptors(), fdBefore)
        << "open file descriptor count grew after " << Cycles << " cycles -- possible leak";
    EXPECT_EQ(CnaTestSupport::GetThreadCount(), threadsBefore)
        << "thread count grew after " << Cycles << " cycles -- possible leak";
#endif
}
