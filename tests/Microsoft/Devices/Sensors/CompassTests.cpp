// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

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

using Microsoft::Devices::Sensors::CalibrationEventArgs;
using Microsoft::Devices::Sensors::Compass;
using Microsoft::Devices::Sensors::CompassReading;
using Microsoft::Devices::Sensors::SensorFailedException;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;
using Microsoft::Devices::Sensors::SensorState;
using Microsoft::Xna::Framework::Vector3;

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
        bool StopCalled = false;
        ReadingCallback CapturedOnReading;
        CalibrationCallback CapturedOnCalibrationNeeded;

        [[nodiscard]] bool IsSupported() override
        {
            return SupportedResult;
        }

        bool Start(const System::TimeSpan&, ReadingCallback onReading, CalibrationCallback onCalibrationNeeded) override
        {
            if (!StartResult)
            {
                return false;
            }
            CapturedOnReading = std::move(onReading);
            CapturedOnCalibrationNeeded = std::move(onCalibrationNeeded);
            return true;
        }

        void Stop() override
        {
            StopCalled = true;
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
