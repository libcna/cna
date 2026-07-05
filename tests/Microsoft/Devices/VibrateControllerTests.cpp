// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>

#include "Microsoft/Devices/VibrateController.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Devices::VibrateController;
using System::TimeSpan;

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
