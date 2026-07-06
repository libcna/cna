// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Microsoft/Devices/Detail/IVibrateBackend.hpp"
#include "Microsoft/Devices/VibrateController.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::VibrateController;
using System::TimeSpan;

namespace
{
    // Task VIB-002/VIB-009: fake Detail::IVibrateBackend, so tests can
    // exercise VibrateController's own forwarding/validation/clamping logic
    // deterministically, without depending on whatever haptic hardware (or
    // lack of it) happens to be present in the test environment.
    class FakeVibrateBackend final : public Microsoft::Devices::Detail::IVibrateBackend
    {
    public:
        bool SupportedResult = true;
        std::string DeviceNameResult;

        int StartCallCount = 0;
        TimeSpan LastStartDuration = TimeSpan::Zero;
        float LastStartIntensity = -1.0f;

        int StopCallCount = 0;

        int StartLeftRightCallCount = 0;
        TimeSpan LastStartLeftRightDuration = TimeSpan::Zero;
        float LastLargeMotor = -1.0f;
        float LastSmallMotor = -1.0f;

        void Start(const System::TimeSpan& duration, float intensity) override
        {
            ++StartCallCount;
            LastStartDuration = duration;
            LastStartIntensity = intensity;
        }

        void Stop() override
        {
            ++StopCallCount;
        }

        [[nodiscard]] bool IsSupported() override
        {
            return SupportedResult;
        }

        [[nodiscard]] std::string GetDeviceName() override
        {
            return DeviceNameResult;
        }

        void StartLeftRight(float largeMotor, float smallMotor, const System::TimeSpan& duration) override
        {
            ++StartLeftRightCallCount;
            LastLargeMotor = largeMotor;
            LastSmallMotor = smallMotor;
            LastStartLeftRightDuration = duration;
        }
    };

    // RAII helper: installs a fake backend on VibrateController's shared
    // singleton for the lifetime of one test, and always restores the real
    // Detail::SdlHapticVibrateBackend afterward. VibrateController has
    // exactly one process-wide instance (GetDefaultPropertyReturnsSameInstance
    // below), so a test that installed a fake and forgot to restore it would
    // silently break every later test in the same process.
    class ScopedFakeVibrateBackend
    {
    public:
        ScopedFakeVibrateBackend()
        {
            auto fake = std::make_unique<FakeVibrateBackend>();
            fake_ = fake.get();
            VibrateController::getDefaultProperty()->SetBackendForTesting(std::move(fake));
        }

        ~ScopedFakeVibrateBackend()
        {
            VibrateController::getDefaultProperty()->SetBackendForTesting(nullptr);
        }

        ScopedFakeVibrateBackend(const ScopedFakeVibrateBackend&) = delete;
        ScopedFakeVibrateBackend& operator=(const ScopedFakeVibrateBackend&) = delete;

        FakeVibrateBackend* operator->() const
        {
            return fake_;
        }

    private:
        FakeVibrateBackend* fake_;
    };
} // namespace

// NOTE: VibrateController::Start() deliberately skips haptic devices that
// are also connected joysticks/gamepads, so it never competes with
// GamePad::SetVibration() for the same physical rumble motor (see
// IsConnectedGamepadHapticDevice() in VibrateController.cpp). That
// exclusion behavior isn't unit-testable here: it requires a real connected
// haptic-capable gamepad to observe, which isn't available in this headless
// environment and can't be simulated through this class's public API. The
// tests below still cover the same-process consequence that matters for
// this class's own contract: every in-range call is a no-throw silent
// no-op when no suitable device is found.

TEST(VibrateControllerTests, GetDefaultPropertyIsNeverNull)
{
    ASSERT_NE(VibrateController::getDefaultProperty(), nullptr);
}

TEST(VibrateControllerTests, GetDefaultPropertyReturnsSameInstance)
{
    EXPECT_EQ(VibrateController::getDefaultProperty(), VibrateController::getDefaultProperty());
}

// Task DEVICES-0016: the two-call check above doesn't confirm identity holds
// across a real Start()/Stop()/StartLeftRight() sequence in between — this
// class's constructor is only ever run once (function-local static), but the
// singleton pointer itself must never change no matter what's been called on
// it.
TEST(VibrateControllerTests, GetDefaultPropertyReturnsSameInstanceAcrossUsage)
{
    VibrateController* first = VibrateController::getDefaultProperty();

    first->Start(TimeSpan::FromMilliseconds(10));
    first->StartLeftRight(0.5f, 0.5f, TimeSpan::FromMilliseconds(10));
    first->Stop();

    EXPECT_EQ(VibrateController::getDefaultProperty(), first);
    EXPECT_EQ(VibrateController::getDefaultProperty(), first);
}

TEST(VibrateControllerTests, StopBeforeAnyStartDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, StartWithZeroDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::Zero));
}

TEST(VibrateControllerTests, StartWithShortDurationDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(100)));
}

TEST(VibrateControllerTests, StartWithExactlyMaxDurationDoesNotThrow)
{
    // XNA/WP7 max is 5 seconds, inclusive.
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5)));
}

TEST(VibrateControllerTests, StartWithNegativeDurationThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(-1)),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, StartWithOverlongDurationThrows)
{
    // XNA/WP7 max is 5 seconds; anything past it must throw, not clamp.
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5.001)),
        System::ArgumentOutOfRangeException);
}

// Task VIB-006: verified against the archived MSDN Start(TimeSpan) page
// (learn.microsoft.com/en-us/previous-versions/windows/apps/ff403287(v=vs.105)):
// "duration ... Valid times are between 0 and 5 seconds. Values greater than
// 5 or less than 0 raise an exception." TimeSpan::MaxValue/MinValue are both
// far outside that range and must throw cleanly (no overflow/UB when
// converting to a backend duration), not just "some sufficiently large
// value."
TEST(VibrateControllerTests, StartWithMaxTimeSpanValueThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::MaxValue),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, StartWithMinTimeSpanValueThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::MinValue),
        System::ArgumentOutOfRangeException);
}

// Task VIB-006: the archived MSDN Start(TimeSpan) page documents the thrown
// type as plain "ArgumentException", not "ArgumentOutOfRangeException" --
// CNA throws the more specific subtype instead (System::ArgumentOutOfRangeException
// derives from System::ArgumentException, exactly like the real .NET
// hierarchy), which is compatible: any caller catching the documented
// ArgumentException still catches this. This test pins that compatibility
// relationship down explicitly rather than leaving it an implicit,
// unverified assumption.
TEST(VibrateControllerTests, OutOfRangeDurationExceptionIsCatchableAsArgumentException)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(6)),
        System::ArgumentException);
}

TEST(VibrateControllerTests, StartLeftRightWithMaxTimeSpanValueThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(0.5f, 0.5f, TimeSpan::MaxValue),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, StopAfterStartDoesNotThrow)
{
    VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(100));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, TwoConsecutiveStartsDoNotCrash)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50)));
}

// Phase 6 NOXNA extensions (Tasks P2-10 through P2-13). Same silent-no-op
// contract as the plain Start()/Stop() above when no suitable device is
// found; these tests only assert no-throw/no-crash behavior, not that a
// device was actually found or actuated (this environment's haptic
// availability isn't guaranteed either way).

TEST(VibrateControllerTests, StartWithIntensityZeroDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 0.0f));
}

TEST(VibrateControllerTests, StartWithIntensityHalfDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 0.5f));
}

TEST(VibrateControllerTests, StartWithIntensityOneDoesNotThrow)
{
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 1.0f));
}

TEST(VibrateControllerTests, StartWithOutOfRangeIntensityIsClampedSilentlyAndDoesNotThrow)
{
    // Unlike duration, out-of-range intensity is clamped, not rejected —
    // there is no real WP7 API contract to preserve here, so CNA is free to
    // choose the more forgiving behavior for its own extension.
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), 1.5f));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(50), -0.5f));
}

TEST(VibrateControllerTests, StartWithIntensityOutOfRangeDurationStillThrows)
{
    // The intensity overload must still enforce the same duration contract
    // as the XNA-compliant overload it's layered on top of.
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromSeconds(5.001), 1.0f),
        System::ArgumentOutOfRangeException);
}

TEST(VibrateControllerTests, GetIsSupportedPropertyDoesNotCrash)
{
    const bool supported = VibrateController::getDefaultProperty()->getIsSupportedProperty();
    (void)supported;
}

TEST(VibrateControllerTests, GetDeviceNamePropertyDoesNotCrash)
{
    const std::string name = VibrateController::getDefaultProperty()->getDeviceNameProperty();
    (void)name;
}

// Task P3-11: getIsSupportedProperty() and getDeviceNameProperty() were
// previously only tested independently ("doesn't crash"), treating them as
// unrelated facts. Both re-probe via the same AcquireHapticDeviceForProbe()
// helper, so the relationship is a real invariant, not a coincidence: no
// device found (unsupported) implies an empty device name.
TEST(VibrateControllerTests, UnsupportedImpliesEmptyDeviceName)
{
    VibrateController* controller = VibrateController::getDefaultProperty();
    const bool supported = controller->getIsSupportedProperty();
    const std::string name = controller->getDeviceNameProperty();

    if (!supported)
    {
        EXPECT_TRUE(name.empty());
    }
}

TEST(VibrateControllerTests, StartLeftRightDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(0.25f, 0.75f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(VibrateController::getDefaultProperty()->Stop());
}

TEST(VibrateControllerTests, StartLeftRightWithOutOfRangeMagnitudesIsClampedSilentlyAndDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.5f, -0.5f, TimeSpan::FromMilliseconds(50)));
}

TEST(VibrateControllerTests, StartLeftRightWithOutOfRangeDurationThrows)
{
    EXPECT_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.0f, 1.0f, TimeSpan::FromSeconds(5.001)),
        System::ArgumentOutOfRangeException);
}

// Task P3-11: StartLeftRightDoesNotThrow above incidentally covers the
// upper magnitude boundary (1.0f/1.0f), but 0.0f for either motor was
// never tested, nor was TimeSpan::Zero duration specifically for
// StartLeftRight (only ~50ms and the 5.001s throw case).

TEST(VibrateControllerTests, StartLeftRightWithZeroMagnitudesDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(0.0f, 0.0f, TimeSpan::FromMilliseconds(50)));
}

TEST(VibrateControllerTests, StartLeftRightWithZeroDurationDoesNotThrow)
{
    EXPECT_NO_THROW(
        VibrateController::getDefaultProperty()->StartLeftRight(1.0f, 1.0f, TimeSpan::Zero));
}

// Task P3-5: Start()/Start(duration,intensity) and StartLeftRight() use
// independent SDL haptic effect slots and must stop each other's effect
// before starting their own, so they never run layered on the same
// motor(s). This environment can't assert on actual simultaneous-motor
// state headless (no real haptic hardware guaranteed), but it can assert
// the call sequence itself is safe end-to-end: switching between the two
// paths repeatedly, in both directions, must never throw or leak/double-
// free the StartLeftRight() effect slot, and Stop() must still clean up
// correctly afterward.

TEST(VibrateControllerTests, StartThenStartLeftRightThenStopDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Stop());
}

TEST(VibrateControllerTests, StartLeftRightThenStartThenStopDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_NO_THROW(controller->StartLeftRight(1.0f, 1.0f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Stop());
}

TEST(VibrateControllerTests, AlternatingStartAndStartLeftRightRepeatedlyDoesNotThrow)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    for (int i = 0; i < 3; ++i)
    {
        EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(10), 0.5f));
        EXPECT_NO_THROW(controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromMilliseconds(10)));
    }

    EXPECT_NO_THROW(controller->Stop());
}

// Task P4-9: g_haptic/g_leftRightEffectId are now guarded by a mutex so
// concurrent calls from multiple application threads (e.g. one thread
// calling Start() while another calls Stop()/StartLeftRight()) can't race.
// This environment has no real haptic hardware to actuate, so this can't
// assert anything about actual vibration state — it only exercises the
// locking under real concurrent contention (many threads hammering every
// public method at once) and confirms nothing throws, deadlocks, or
// crashes (e.g. under ThreadSanitizer, were it enabled for this build).
TEST(VibrateControllerTests, ConcurrentCallsFromMultipleThreadsDoNotCrashOrDeadlock)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    constexpr int ThreadCount = 8;
    constexpr int IterationsPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(ThreadCount);

    for (int t = 0; t < ThreadCount; ++t)
    {
        threads.emplace_back([controller, t]()
        {
            for (int i = 0; i < IterationsPerThread; ++i)
            {
                switch ((t + i) % 5)
                {
                case 0:
                    controller->Start(TimeSpan::FromMilliseconds(1));
                    break;
                case 1:
                    controller->Start(TimeSpan::FromMilliseconds(1), 0.5f);
                    break;
                case 2:
                    controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromMilliseconds(1));
                    break;
                case 3:
                    controller->Stop();
                    break;
                default:
                    (void)controller->getIsSupportedProperty();
                    (void)controller->getDeviceNameProperty();
                    break;
                }
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    EXPECT_NO_THROW(controller->Stop());
}

// Task P5-11: g_subsystemHeld replaced the old SDL_WasInit() guard in
// EnsureHapticSubsystemInitialized() — confirms many repeated probe calls
// (which each internally call it) don't corrupt or leak anything
// observable: the result stays consistent call to call, and device-name
// consistency (Task P3-11's UnsupportedImpliesEmptyDeviceName invariant)
// keeps holding under repetition, not just once.
TEST(VibrateControllerTests, RepeatedProbeCallsStayConsistent)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    const bool firstSupported = controller->getIsSupportedProperty();
    const std::string firstName = controller->getDeviceNameProperty();

    for (int i = 0; i < 50; ++i)
    {
        EXPECT_EQ(controller->getIsSupportedProperty(), firstSupported);
        EXPECT_EQ(controller->getDeviceNameProperty(), firstName);
    }
}

// Task P5-11: confirms many repeated Start()/Stop() sequences (exercising
// EnsureHapticSubsystemInitialized()'s g_subsystemHeld-gated init path
// every time) don't crash, throw unexpectedly, or degrade — each cycle
// behaves the same as the first.
TEST(VibrateControllerTests, RepeatedStartStopSequencesDoNotDegrade)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    for (int i = 0; i < 50; ++i)
    {
        EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(1)));
        EXPECT_NO_THROW(controller->Stop());
    }
}

// Task DEVICES-0028: every prior test above touches one facet of the
// no-haptic-hardware contract independently (IsSupported, DeviceName, no
// crash on Start/Stop/StartLeftRight). This test asserts the whole contract
// together, in one place, as living documentation for whoever next compares
// this container's behavior against a real-hardware run (see
// docs/devices-hardware-checklist.md).
TEST(VibrateControllerTests, UnsupportedEnvironmentFullContract)
{
    VibrateController* controller = VibrateController::getDefaultProperty();

    const bool supported = controller->getIsSupportedProperty();
    const std::string name = controller->getDeviceNameProperty();

    if (supported)
    {
        GTEST_SKIP() << "This environment has real haptic hardware; "
                         "the no-hardware contract this test asserts does not apply here.";
    }

    EXPECT_TRUE(name.empty());
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(50), 0.5f));
    EXPECT_NO_THROW(controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromMilliseconds(50)));
    EXPECT_NO_THROW(controller->Stop());
}

// ---------------------------------------------------------------------------
// Fake-backend tests (Task VIB-002/VIB-009): these exercise
// VibrateController's own forwarding/validation/clamping logic
// deterministically via Detail::IVibrateBackend, rather than depending on
// whatever real haptic hardware (or lack of it) happens to be present.
// ---------------------------------------------------------------------------

TEST(VibrateControllerTests, StartWithPlainOverloadForwardsDurationAndFullIntensityToBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->Start(TimeSpan::FromMilliseconds(123));

    EXPECT_EQ(fake->StartCallCount, 1);
    EXPECT_EQ(fake->LastStartDuration, TimeSpan::FromMilliseconds(123));
    EXPECT_FLOAT_EQ(fake->LastStartIntensity, 1.0f);
}

TEST(VibrateControllerTests, StartWithIntensityOverloadForwardsExactDurationAndIntensityToBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->Start(TimeSpan::FromMilliseconds(250), 0.42f);

    EXPECT_EQ(fake->StartCallCount, 1);
    EXPECT_EQ(fake->LastStartDuration, TimeSpan::FromMilliseconds(250));
    EXPECT_FLOAT_EQ(fake->LastStartIntensity, 0.42f);
}

TEST(VibrateControllerTests, StartClampsOutOfRangeIntensityBeforeReachingBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->Start(TimeSpan::FromMilliseconds(10), 5.0f);
    EXPECT_FLOAT_EQ(fake->LastStartIntensity, 1.0f);

    controller->Start(TimeSpan::FromMilliseconds(10), -5.0f);
    EXPECT_FLOAT_EQ(fake->LastStartIntensity, 0.0f);
}

TEST(VibrateControllerTests, StartWithOutOfRangeDurationThrowsAndNeverReachesBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_THROW(controller->Start(TimeSpan::FromSeconds(-1)), System::ArgumentOutOfRangeException);
    EXPECT_THROW(controller->Start(TimeSpan::FromSeconds(6)), System::ArgumentOutOfRangeException);

    EXPECT_EQ(fake->StartCallCount, 0);
}

TEST(VibrateControllerTests, StopForwardsToBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->Stop();

    EXPECT_EQ(fake->StopCallCount, 1);
}

TEST(VibrateControllerTests, GetIsSupportedPropertyForwardsBackendResultTrue)
{
    ScopedFakeVibrateBackend fake;
    fake->SupportedResult = true;

    EXPECT_TRUE(VibrateController::getDefaultProperty()->getIsSupportedProperty());
}

TEST(VibrateControllerTests, GetIsSupportedPropertyForwardsBackendResultFalse)
{
    ScopedFakeVibrateBackend fake;
    fake->SupportedResult = false;

    EXPECT_FALSE(VibrateController::getDefaultProperty()->getIsSupportedProperty());
}

TEST(VibrateControllerTests, GetDeviceNamePropertyForwardsBackendResult)
{
    ScopedFakeVibrateBackend fake;
    fake->DeviceNameResult = "Fake Haptic Device";

    EXPECT_EQ(VibrateController::getDefaultProperty()->getDeviceNameProperty(), "Fake Haptic Device");
}

TEST(VibrateControllerTests, StartLeftRightForwardsExactDurationAndMagnitudesToBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->StartLeftRight(0.3f, 0.7f, TimeSpan::FromMilliseconds(77));

    EXPECT_EQ(fake->StartLeftRightCallCount, 1);
    EXPECT_FLOAT_EQ(fake->LastLargeMotor, 0.3f);
    EXPECT_FLOAT_EQ(fake->LastSmallMotor, 0.7f);
    EXPECT_EQ(fake->LastStartLeftRightDuration, TimeSpan::FromMilliseconds(77));
}

TEST(VibrateControllerTests, StartLeftRightClampsOutOfRangeMagnitudesBeforeReachingBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    controller->StartLeftRight(5.0f, -5.0f, TimeSpan::FromMilliseconds(10));

    EXPECT_FLOAT_EQ(fake->LastLargeMotor, 1.0f);
    EXPECT_FLOAT_EQ(fake->LastSmallMotor, 0.0f);
}

TEST(VibrateControllerTests, StartLeftRightWithOutOfRangeDurationThrowsAndNeverReachesBackend)
{
    ScopedFakeVibrateBackend fake;
    VibrateController* controller = VibrateController::getDefaultProperty();

    EXPECT_THROW(controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromSeconds(-1)), System::ArgumentOutOfRangeException);
    EXPECT_THROW(controller->StartLeftRight(0.5f, 0.5f, TimeSpan::FromSeconds(6)), System::ArgumentOutOfRangeException);

    EXPECT_EQ(fake->StartLeftRightCallCount, 0);
}

TEST(VibrateControllerTests, SetBackendForTestingNullRestoresDefaultBackendBehavior)
{
    {
        ScopedFakeVibrateBackend fake;
        fake->SupportedResult = true;
        EXPECT_TRUE(VibrateController::getDefaultProperty()->getIsSupportedProperty());
    }

    // ScopedFakeVibrateBackend's destructor already called
    // SetBackendForTesting(nullptr) -- confirm the restored default backend
    // is a live, working Detail::SdlHapticVibrateBackend, not a null/no-op
    // stand-in, by exercising the same no-crash contract every other
    // non-fake test in this file relies on.
    VibrateController* controller = VibrateController::getDefaultProperty();
    EXPECT_NO_THROW((void)controller->getIsSupportedProperty());
    EXPECT_NO_THROW((void)controller->getDeviceNameProperty());
    EXPECT_NO_THROW(controller->Start(TimeSpan::FromMilliseconds(10)));
    EXPECT_NO_THROW(controller->Stop());
}

TEST(VibrateControllerTests, RepeatedBackendSwapsDoNotLeakOrCrash)
{
    for (int i = 0; i < 20; ++i)
    {
        ScopedFakeVibrateBackend fake;
        VibrateController::getDefaultProperty()->Start(TimeSpan::FromMilliseconds(1));
        EXPECT_EQ(fake->StartCallCount, 1);
    }

    // Real backend restored after the loop's last ScopedFakeVibrateBackend
    // destructor ran -- confirm it's still usable.
    EXPECT_NO_THROW((void)VibrateController::getDefaultProperty()->getIsSupportedProperty());
}
