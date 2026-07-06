// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

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
        bool StopCalled = false;
        int StartCallCount = 0;
        int SetSampleIntervalCallCount = 0;
        System::TimeSpan LastSetSampleInterval;
        ReadingCallback CapturedOnReading;

        [[nodiscard]] bool IsSupported() override
        {
            return SupportedResult;
        }

        bool Start(const System::TimeSpan&, ReadingCallback onReading) override
        {
            ++StartCallCount;
            if (!StartResult)
            {
                return false;
            }
            CapturedOnReading = std::move(onReading);
            return true;
        }

        void Stop() override
        {
            StopCalled = true;
        }

        void SetSampleInterval(const System::TimeSpan& timeBetweenUpdates) override
        {
            ++SetSampleIntervalCallCount;
            LastSetSampleInterval = timeBetweenUpdates;
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

// Task P3-8: Calibrate subscription. Motion.Calibrate is never raised by
// this implementation on any platform — Detail::IMotionBackend has no
// calibration callback at all (unlike ICompassBackend), since
// AndroidMotionBackend never detects a calibration-needed condition itself
// (docs/devices-native-backend-design.md) — so this only confirms
// subscribing doesn't crash, on every platform, not just this one.
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
