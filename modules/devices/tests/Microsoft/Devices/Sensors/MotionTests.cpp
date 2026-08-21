// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include "../Detail/ProcSelfResourceCounters.hpp"
#include "Microsoft/Devices/Sensors/AttitudeReading.hpp"
#include "Microsoft/Devices/Sensors/CalibrationEventArgs.hpp"
#include "Microsoft/Devices/Sensors/Detail/IMotionBackend.hpp"
#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorFailedException.hpp"
#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/DateTimeOffset.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::Sensors::AttitudeReading;
using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Motion;
using Microsoft::Devices::Sensors::MotionReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using System::TimeSpan;

namespace
{
    // Task DEVICES-0113: a fake IMotionBackend (not Detail::AndroidMotionBackend,
    // which is #ifdef __ANDROID__-only and cannot compile on this host) —
    // lets these tests exercise Motion::Start()/CurrentValueChanged's
    // delegation to any backend without needing real Android hardware.
    class FakeMotionBackend final : public Microsoft::Devices::Sensors::Detail::IMotionBackend
    {
    public:
        bool SupportedResult = true;
        bool StartResult = true;
        // Task TEST2-001 (2026-07-17): see FakeCompassBackend's identical
        // fields for the full rationale -- atomic because LIFE-001's design
        // deliberately allows a concurrent Stop() and Start()'s own
        // orphaned-attempt cleanup to both call backend Stop() on this fake
        // from two different threads.
        std::atomic<bool> StopCalled{false};
        int StartCallCount = 0;
        std::atomic<int> StopCallCount{0};
        int SetSampleIntervalCallCount = 0;
        System::TimeSpan LastSetSampleInterval;
        ReadingCallback CapturedOnReading;
        CalibrationCallback CapturedOnCalibrationNeeded;

        // Task MOT2-005 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
        // controllable so tests can simulate both the north-referenced and
        // drift-prone-fallback cases without needing a real Android device.
        bool UsingNorthReferencedAttitudeSourceResult = true;

        // Task TEST2-001 (LIFE-001/LIFE-002 regression coverage): see
        // FakeCompassBackend's identical hook for the full rationale.
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

        [[nodiscard]] bool IsUsingNorthReferencedAttitudeSource() override
        {
            return UsingNorthReferencedAttitudeSourceResult;
        }
    };
} // namespace

TEST(MotionTests, GetIsSupportedPropertyIsFalse)
{
    EXPECT_FALSE(Motion::getIsSupportedProperty());
}

TEST(MotionTests, ConstructorSucceedsUnderInstanceLimit)
{
    EXPECT_NO_THROW({ const Motion m; (void)m; });
}

// Task SENSORBASE-002: see AccelerometerTests.cpp's identical test for the
// full rationale (MonoGame cross-check confirms the real WP7 SensorBase<T>'s
// single shared 2ms default, not a per-sensor-class override).
TEST(MotionTests, DefaultTimeBetweenUpdatesIsTwoMilliseconds)
{
    const Motion m;
    EXPECT_EQ(m.getTimeBetweenUpdatesProperty(), TimeSpan::FromMilliseconds(2.0));
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

// Task SENSORBASE-006: mirrors CompassTests.
// DisposeWhileStartedCallsBackendStopWithoutExplicitStopFirst -- see that
// test for the full rationale.
TEST(MotionTests, DisposeWhileStartedCallsBackendStopWithoutExplicitStopFirst)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    ASSERT_FALSE(fake->StopCalled);

    EXPECT_NO_THROW(m.Dispose());

    EXPECT_TRUE(fake->StopCalled);
}

// Task P3-11: Stop()-after-Dispose() is a distinct, separately guarded code
// path (ObjectDisposedException::ThrowIf at the top of Stop()).
TEST(MotionTests, StopAfterDisposeThrows)
{
    Motion m;
    m.Dispose();
    EXPECT_THROW(m.Stop(), System::ObjectDisposedException);
}

// Task DEVICES-0056: no test anywhere asserted Start()-after-Dispose()
// throws ObjectDisposedException specifically — Start()'s own disposed-check
// (Motion.cpp, top of Start()) runs before the always-throws-
// SensorFailedException stub body, but that ordering wasn't confirmed by a
// test; a regression could silently swap the exception type.
TEST(MotionTests, StartAfterDisposeThrows)
{
    Motion m;
    m.Dispose();
    EXPECT_THROW(m.Start(), System::ObjectDisposedException);
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

// Task P6-3: see CompassTests.cpp's identical test for the full rationale —
// ClaimDisposalOnce() closes a race where two threads calling Dispose() on
// the same instance concurrently could both decrement instanceCount_.
TEST(MotionTests, ConcurrentDisposeFromMultipleThreadsNeverCorruptsInstanceCount)
{
    constexpr int Rounds = 30;

    for (int round = 0; round < Rounds; ++round)
    {
        auto instance = std::make_shared<Motion>();

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

// Task P3-6: CurrentValueChanged subscription. On this (non-Android)
// platform Motion is still a permanent NotSupported stub — no backend is
// selected here — so the event can't fire in this environment; this only
// confirms subscribing doesn't crash. See
// CurrentValueChangedFiresFromBackendReading below for the case where a
// backend genuinely delivers readings (via a fake, since Android's real
// Detail::AndroidMotionBackend can't run on this host).
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

// Task P3-8: Calibrate subscription. On this (non-Android) platform, Motion
// has no backend at all, so Calibrate can never fire here regardless of
// Task MOTION-011's fix — this only confirms subscribing doesn't crash. See
// CalibrateFiresFromBackendCalibrationCallback below for the
// backend-delivers-a-real-calibration-event case.
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

// Task MOTION-011 (2026-07-16): confirms Calibrate actually fires when the
// backend invokes its calibration-needed callback — proves the delegation
// path (IMotionBackend's CalibrationCallback -> Motion::Calibrate.Raise()),
// mirroring CompassTests.CalibrateFiresFromBackendCalibrationCallback.
TEST(MotionTests, CalibrateFiresFromBackendCalibrationCallback)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    bool invoked = false;
    m.Calibrate += [&invoked](System::Object*, const CalibrationEventArgs&) { invoked = true; };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnCalibrationNeeded));
    fake->CapturedOnCalibrationNeeded();

    EXPECT_TRUE(invoked);
}

// Task DEVPERF-004 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// mirrors CompassTests.RemovingAnotherNotYetInvokedCalibrateHandlerDuringDispatchStillInvokesIt
// -- Motion's Calibrate.Raise() goes through the same System::EventHandler<T>::Raise()
// snapshot mechanism and needed its own direct proof, not just analogy to Compass.
TEST(MotionTests, RemovingAnotherNotYetInvokedCalibrateHandlerDuringDispatchStillInvokesIt)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    using Token = System::EventHandler<CalibrationEventArgs>::Token;

    Token secondToken{};
    bool secondHandlerInvoked = false;

    m.Calibrate.Add(
        [&m, &secondToken](System::Object*, const CalibrationEventArgs&)
        {
            m.Calibrate.Remove(secondToken);
        });

    secondToken = m.Calibrate.Add(
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

// Task DEVICES-0113: with a supported fake backend injected, Start() must
// actually succeed (not throw) and transition to Ready.
TEST(MotionTests, WithInjectedSupportedBackendStartSucceeds)
{
    Motion m;
    m.SetBackendForTesting(std::make_unique<FakeMotionBackend>());

    EXPECT_NO_THROW(m.Start());
    EXPECT_EQ(m.getStateProperty(), SensorState::Ready);
}

TEST(MotionTests, WithInjectedUnsupportedBackendStartStillThrows)
{
    Motion m;
    auto fake = std::make_unique<FakeMotionBackend>();
    fake->SupportedResult = false;
    m.SetBackendForTesting(std::move(fake));

    EXPECT_THROW(m.Start(), SensorFailedException);
    EXPECT_EQ(m.getStateProperty(), SensorState::NotSupported);
}

// Confirms CurrentValueChanged actually fires with the backend's own
// reading data once Start() has wired the callback through.
TEST(MotionTests, CurrentValueChangedFiresFromBackendReading)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    bool invoked = false;
    MotionReading received;
    m.CurrentValueChanged += [&invoked, &received](
        System::Object*, const SensorReadingEventArgs<MotionReading>& args)
    {
        invoked = true;
        received = args.getSensorReadingProperty();
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->CapturedOnReading(synthetic);

    ASSERT_TRUE(invoked);
    EXPECT_EQ(received.getDeviceRotationRateProperty(), Vector3(0.01f, 0.02f, 0.03f));
    EXPECT_TRUE(m.getIsDataValidProperty());
}

// Task DEVPERF-004 (2026-07-18, external audit `audit_devices_2026-07-17.md`):
// mirrors AccelerometerTests/GyroscopeTests/CompassTests.
// RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt -- Motion dispatches
// CurrentValueChanged through the same SensorBase<T>::SetCurrentValueAndMarkDataValid()/
// System::EventHandler<T>::Raise() path as the other three sensor classes, so it must
// honor the same snapshot-based handler-list mutation guarantee.
TEST(MotionTests, RemovingAnotherNotYetInvokedHandlerDuringDispatchStillInvokesIt)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    using Args = SensorReadingEventArgs<MotionReading>;
    using Token = System::EventHandler<Args>::Token;

    Token secondToken{};
    bool secondHandlerInvoked = false;

    m.CurrentValueChanged.Add(
        [&m, &secondToken](System::Object*, const Args&)
        {
            m.CurrentValueChanged.Remove(secondToken);
        });

    secondToken = m.CurrentValueChanged.Add(
        [&secondHandlerInvoked](System::Object*, const Args&)
        {
            secondHandlerInvoked = true;
        });

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading firstReading(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->CapturedOnReading(firstReading);

    EXPECT_TRUE(secondHandlerInvoked);

    secondHandlerInvoked = false;
    const MotionReading secondReading(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.04f, 0.05f, 0.06f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->CapturedOnReading(secondReading);
    EXPECT_FALSE(secondHandlerInvoked);
}

// Task DEVPERF-004: mirrors AccelerometerTests/GyroscopeTests/CompassTests.
// HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState -- proves the same
// reentrancy guarantee holds for Motion's CurrentValueChanged dispatch when driven by
// a backend callback rather than a synthetic-injection test hook.
TEST(MotionTests, HandlerTriggeringAReentrantUpdateDoesNotDeadlockOrCorruptState)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    bool reentered = false;
    std::vector<float> observedRotationRateX;

    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());

    m.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<MotionReading>& args)
    {
        observedRotationRateX.push_back(args.getSensorReadingProperty().getDeviceRotationRateProperty().X);
        if (!reentered)
        {
            reentered = true;
            const MotionReading innerReading(
                attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.99f, 0.05f, 0.06f), Vector3(0.0f, -1.0f, 0.0f),
                System::DateTimeOffset::getUtcNowProperty());
            fake->CapturedOnReading(innerReading);
        }
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const MotionReading outerReading(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->CapturedOnReading(outerReading);

    ASSERT_EQ(observedRotationRateX.size(), 2u);
    EXPECT_FLOAT_EQ(observedRotationRateX[0], 0.01f);
    EXPECT_FLOAT_EQ(observedRotationRateX[1], 0.99f);
    EXPECT_FLOAT_EQ(m.getCurrentValueProperty().getDeviceRotationRateProperty().X, 0.99f);
}

// Task MOT2-005 (2026-07-17, external audit `audit_devices_2026-07-17.md`):
// no backend at all (this host build has no native Motion backend) means
// nothing has ever claimed a north-referenced heading -- true is a vacuous
// "nothing to warn about" default, not an affirmative claim.
TEST(MotionTests, GetIsAttitudeNorthReferencedPropertyIsTrueWithNoBackend)
{
    const Motion m;
    EXPECT_TRUE(m.getIsAttitudeNorthReferencedProperty());
}

TEST(MotionTests, GetIsAttitudeNorthReferencedPropertyReflectsBackendTrue)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    fakeOwned->UsingNorthReferencedAttitudeSourceResult = true;
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    EXPECT_TRUE(m.getIsAttitudeNorthReferencedProperty());
}

TEST(MotionTests, GetIsAttitudeNorthReferencedPropertyReflectsBackendFalse)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    fakeOwned->UsingNorthReferencedAttitudeSourceResult = false;
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    EXPECT_FALSE(m.getIsAttitudeNorthReferencedProperty());
}

TEST(MotionTests, GetIsAttitudeNorthReferencedPropertyThrowsAfterDispose)
{
    Motion m;
    m.Dispose();
    EXPECT_THROW((void)m.getIsAttitudeNorthReferencedProperty(), System::ObjectDisposedException);
}

// Task READINGS-003 (2026-07-06): the real timestamp-setting logic for
// Motion lives entirely in Detail::AndroidSensorBridge.cpp/
// AndroidMotionBackend.cpp (Android-only, #ifdef __ANDROID__, not reachable
// on this host), which this codebase's own wall-clock timestamp policy
// documents as always using System::DateTimeOffset::getUtcNowProperty()
// (AndroidSensorSample::Timestamp, propagated into MotionReading.Timestamp
// per Task MOTION-006's fix). What *is* testable here, and was not
// previously covered by any test in this file, is that Motion::Start()'s
// callback lambda forwards whatever MotionReading the backend hands it
// through to CurrentValueChanged/CurrentValue completely unmodified -- a
// fixed, deliberately-distinguishable timestamp (not a fresh
// getUtcNowProperty() call at test time) proves exact passthrough rather
// than a loose "close enough" bracket check.
TEST(MotionTests, CurrentValueChangedPropagatesBackendTimestampExactly)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    System::DateTimeOffset receivedTimestamp;
    m.CurrentValueChanged += [&receivedTimestamp](
        System::Object*, const SensorReadingEventArgs<MotionReading>& args)
    {
        receivedTimestamp = args.getSensorReadingProperty().getTimestampProperty();
    };

    const System::DateTimeOffset fixedTimestamp(System::DateTime(637000000000000000LL), System::TimeSpan::Zero);
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(), fixedTimestamp);
    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        fixedTimestamp);
    fake->CapturedOnReading(synthetic);

    EXPECT_EQ(receivedTimestamp, fixedTimestamp);
    EXPECT_EQ(m.getCurrentValueProperty().getTimestampProperty(), fixedTimestamp);
}

// Task SENSORBASE-005: mirrors AccelerometerTests.
// CurrentValueAndIsDataValidRetainLastReadingAfterStop -- see that test for
// the full rationale. Motion::Stop() only clears started_/state_ (confirmed
// by reading Motion::Stop() directly), so the last known reading and its
// validity are expected to persist.
TEST(MotionTests, CurrentValueAndIsDataValidRetainLastReadingAfterStop)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->CapturedOnReading(synthetic);
    ASSERT_TRUE(m.getIsDataValidProperty());

    m.Stop();

    EXPECT_TRUE(m.getIsDataValidProperty());
    EXPECT_EQ(m.getCurrentValueProperty().getDeviceRotationRateProperty(), Vector3(0.01f, 0.02f, 0.03f));
}

// Task SENSORBASE-003: see CompassTests.cpp's identical test for the full
// rationale -- unlike Accelerometer/Gyroscope, this exact reentrancy
// scenario had never been tested for Motion at all before this task.
// Proves Motion's own ClaimDisposalOnce()/Stop() reentrancy handling doesn't
// deadlock via the fake-backend seam; does NOT prove the deeper question of
// tearing down the real Detail::AndroidMotionBackend/AndroidSensorBridge
// chain mid-callback, which is Android-only and unverified here (see
// plans/plan_devices.md's SENSORBASE-003 closing note).
TEST(MotionTests, DisposeFromWithinOwnCallbackDoesNotDeadlock)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    bool handlerRan = false;
    m.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<MotionReading>&)
    {
        handlerRan = true;
        m.Dispose();
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    EXPECT_NO_THROW(fake->CapturedOnReading(synthetic));
    EXPECT_TRUE(handlerRan);

    EXPECT_THROW(m.Dispose(), System::ObjectDisposedException);
}

// Confirms Stop() actually calls through to the backend's own Stop().
TEST(MotionTests, StopCallsBackendStop)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    m.Stop();

    EXPECT_TRUE(fake->StopCalled);
    EXPECT_EQ(m.getStateProperty(), SensorState::Disabled);
}

// Task ANDROID-BRIDGE-002: see CompassTests.cpp's identical pair of tests
// for the full rationale — SensorBase<T>::TimeBetweenUpdatesChanged is wired
// (Motion::Motion()) to forward the new value to the live backend.
TEST(MotionTests, SetTimeBetweenUpdatesPropertyForwardsToBackend)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));
    m.Start();

    m.setTimeBetweenUpdatesProperty(System::TimeSpan::FromMilliseconds(50.0));

    EXPECT_EQ(fake->SetSampleIntervalCallCount, 1);
    EXPECT_EQ(fake->LastSetSampleInterval, System::TimeSpan::FromMilliseconds(50.0));
}

TEST(MotionTests, SetTimeBetweenUpdatesPropertyToSameValueDoesNotForwardToBackend)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));

    const System::TimeSpan current = m.getTimeBetweenUpdatesProperty();
    m.setTimeBetweenUpdatesProperty(current);

    EXPECT_EQ(fake->SetSampleIntervalCallCount, 0);
}

// Task DEVICES-0114: confirms Motion does not require constructing a live
// Accelerometer/Compass/Gyroscope instance to work — its (fake) backend is
// entirely self-contained.
TEST(MotionTests, DoesNotRequireOtherSensorInstancesToBeConstructed)
{
    Motion m;
    m.SetBackendForTesting(std::make_unique<FakeMotionBackend>());

    EXPECT_NO_THROW(m.Start());
}

// Stabilization pass, Task 1 (repeated Start/Stop safety): see
// CompassTests.cpp's identical test for the full rationale -- calling
// Start() a second time while already started must not call through to the
// backend again, and must throw rather than misreport state.
TEST(MotionTests, StartTwiceThrowsWithoutCallingBackendAgain)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));

    m.Start();
    ASSERT_EQ(fake->StartCallCount, 1);

    EXPECT_THROW(m.Start(), SensorFailedException);
    EXPECT_EQ(fake->StartCallCount, 1);
    EXPECT_EQ(m.getStateProperty(), SensorState::Ready);
}

TEST(MotionTests, StartAfterStopSucceedsAndCallsBackendAgain)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));

    m.Start();
    m.Stop();
    EXPECT_NO_THROW(m.Start());
    EXPECT_EQ(fake->StartCallCount, 2);
}

// Stabilization pass, Task 6 (SetBackendForTesting() contract): see
// CompassTests.cpp's identical test for the full rationale, including why
// the second (attempted-replacement) backend cannot be inspected after the
// throwing call -- std::move() transfers ownership into the call before
// the exception unwinds and destroys it.
TEST(MotionTests, SetBackendForTestingAfterStartThrowsAndDoesNotReplaceBackend)
{
    Motion m;
    auto firstOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* first = firstOwned.get();
    m.SetBackendForTesting(std::move(firstOwned));
    m.Start();

    EXPECT_THROW(
        m.SetBackendForTesting(std::make_unique<FakeMotionBackend>()),
        SensorFailedException);

    m.Stop();
    EXPECT_TRUE(first->StopCalled);
}

// ConcurrentStartStopFromMultipleThreadsDoesNotCrash -- Motion is
// structurally identical to Compass, which a real devices-tsan run confirmed
// had an unguarded data race between Start()/Stop() writing started_/state_
// and getStateProperty() reading state_ (unlike Accelerometer/Gyroscope,
// whose equivalent fields are guarded by their shared
// Detail::SdlSensorSubsystem<TSensor>'s subsystem.mutex_). This test
// empirically confirms the same race exists for Motion and that Motion's own
// mutex_ (Task SENSORBASE-004) fixes it.
TEST(MotionTests, ConcurrentStartStopFromMultipleThreadsDoesNotCrash)
{
    Motion m;
    m.SetBackendForTesting(std::make_unique<FakeMotionBackend>());

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&m]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                try
                {
                    m.Start();
                }
                catch (const SensorFailedException&)
                {
                    // Expected: another thread already started it first.
                }

                m.Stop();
                (void)m.getStateProperty();
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }
}

// Task DEV-AUD-006 (2026-07-16, external audit `audit_devices.md`): mirrors
// CompassTests.ConcurrentSetTimeBetweenUpdatesAndSetBackendForTestingDoesNotCrash
// -- see that test for the full rationale.
TEST(MotionTests, ConcurrentSetTimeBetweenUpdatesAndSetBackendForTestingDoesNotCrash)
{
    Motion m;

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([&m, t]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                if (t % 2 == 0)
                {
                    m.setTimeBetweenUpdatesProperty(TimeSpan::FromMilliseconds(1.0 + static_cast<double>(i)));
                }
                else
                {
                    m.SetBackendForTesting(std::make_unique<FakeMotionBackend>());
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
// mirrors CompassTests.SynchronousReadingCallbackDuringStartIsHandledSafely
// -- see that test for the full rationale.
TEST(MotionTests, SynchronousReadingCallbackDuringStartIsHandledSafely)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));

    bool invoked = false;
    MotionReading received;
    m.CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<MotionReading>& args)
    {
        invoked = true;
        received = args.getSensorReadingProperty();
    };

    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());
    fake->OnStartCalledBeforeReturn = [fake, synthetic]()
    {
        fake->CapturedOnReading(synthetic);
    };

    EXPECT_NO_THROW(m.Start());

    EXPECT_TRUE(invoked);
    EXPECT_EQ(received.getDeviceRotationRateProperty(), Vector3(0.01f, 0.02f, 0.03f));
    EXPECT_EQ(m.getStateProperty(), SensorState::Ready);
}

// Task LIFE-001 (2026-07-17): mirrors
// CompassTests.ConcurrentStopDuringStartDoesNotDeadlock -- see that test for
// the full rationale.
TEST(MotionTests, ConcurrentStopDuringStartDoesNotDeadlock)
{
    Motion m;
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    m.SetBackendForTesting(std::move(fakeOwned));

    fake->OnStartCalledBeforeReturn = [&m]()
    {
        std::thread stopper([&m]() { m.Stop(); });
        stopper.join();
    };

    EXPECT_NO_THROW(m.Start());

    EXPECT_EQ(fake->StopCallCount, 2);
    EXPECT_EQ(m.getStateProperty(), SensorState::Disabled);
}

// Task LIFE-005 (2026-07-17): mirrors
// CompassTests.DestroyingOwnerFromCurrentValueChangedThenFiringCalibrateDoesNotCrash
// -- see that test for the full rationale.
TEST(MotionTests, DestroyingOwnerFromCurrentValueChangedThenFiringCalibrateDoesNotCrash)
{
    auto motion = std::make_unique<Motion>();
    auto fakeOwned = std::make_unique<FakeMotionBackend>();
    FakeMotionBackend* fake = fakeOwned.get();
    motion->SetBackendForTesting(std::move(fakeOwned));
    motion->Start();

    bool calibrateInvoked = false;
    motion->Calibrate += [&calibrateInvoked](System::Object*, const CalibrationEventArgs&)
    {
        calibrateInvoked = true;
    };

    bool handlerRan = false;
    motion->CurrentValueChanged += [&](System::Object*, const SensorReadingEventArgs<MotionReading>&)
    {
        handlerRan = true;
        motion.reset(); // full destruction, not just Dispose(), from within this callback
    };

    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnReading));
    ASSERT_TRUE(static_cast<bool>(fake->CapturedOnCalibrationNeeded));
    const AttitudeReading attitude(
        0.1f, 0.2f, 0.3f, Quaternion::Identity, Matrix::getIdentityProperty(),
        System::DateTimeOffset::getUtcNowProperty());
    const MotionReading synthetic(
        attitude, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.01f, 0.02f, 0.03f), Vector3(0.0f, -1.0f, 0.0f),
        System::DateTimeOffset::getUtcNowProperty());

    auto calibrationCallback = fake->CapturedOnCalibrationNeeded;
    EXPECT_NO_THROW(fake->CapturedOnReading(synthetic));
    EXPECT_TRUE(handlerRan);
    EXPECT_NO_THROW(calibrationCallback());

    EXPECT_FALSE(calibrateInvoked);
}

// Task PERF2-002 (2026-07-18, external audit `audit_devices_2026-07-17.md`): mirrors
// CompassTests.OneHundredThousandConstructBackendInjectStartStopDisposeCyclesLeaveNoResourceLeak
// -- see that test (and AccelerometerTests's own version) for the full rationale.
TEST(MotionTests, OneHundredThousandConstructBackendInjectStartStopDisposeCyclesLeaveNoResourceLeak)
{
#if !defined(__linux__)
    GTEST_SKIP() << "FD/thread leak tracking is Linux-specific (/proc/self/fd, /proc/self/status)";
#else
    constexpr int Cycles = 100000;

    {
        Motion warmup;
        warmup.SetBackendForTesting(std::make_unique<FakeMotionBackend>());
    }

    const int fdBefore = CnaTestSupport::CountOpenFileDescriptors();
    const int threadsBefore = CnaTestSupport::GetThreadCount();
    ASSERT_GE(fdBefore, 0);
    ASSERT_GE(threadsBefore, 0);

    for (int i = 0; i < Cycles; ++i)
    {
        Motion m;
        m.SetBackendForTesting(std::make_unique<FakeMotionBackend>());
        EXPECT_NO_THROW(m.Start());
        EXPECT_NO_THROW(m.Stop());
        // m's destructor (Dispose(bool)) runs here, at the end of each iteration's scope.
    }

    EXPECT_LE(CnaTestSupport::CountOpenFileDescriptors(), fdBefore)
        << "open file descriptor count grew after " << Cycles << " cycles -- possible leak";
    EXPECT_LE(CnaTestSupport::GetThreadCount(), threadsBefore)
        << "thread count grew after " << Cycles << " cycles -- possible leak";
#endif
}
