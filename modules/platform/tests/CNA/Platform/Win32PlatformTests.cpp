// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0003, WIN32-0020, WIN32-0044: the Win32 platform object itself --
// factory registration, capability truthfulness, subsystem refcounting, timing and the service
// table.
//
// The implementation-neutral rules live in PlatformConformanceTests and apply here automatically
// because "Win32" is in PlatformFactory::GetAvailable(). What is asserted below is what is
// specific to this backend: which capabilities it claims, which it honestly refuses, and that
// every refusal names the capability a caller would have checked.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;

class Win32PlatformTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        capabilities_ = platform_->GetCapabilities();
    }

    std::unique_ptr<IPlatform> platform_;
    PlatformCapabilities capabilities_;
};

// --- factory ------------------------------------------------------------------------------------

TEST(Win32PlatformFactory, IsAvailableUnderItsStableName)
{
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    EXPECT_NE(std::find(available.begin(), available.end(), "Win32"), available.end());
}

TEST(Win32PlatformFactory, IsTheDefaultWhenItWasSelectedAtBuildTime)
{
    // This file only compiles under CNA_PLATFORM=WIN32 (cmake/UnitTests.cmake), so the selected
    // default must be this backend and not a silent fallback to something else.
    EXPECT_EQ(PlatformFactory::GetDefaultName(), "Win32");
    const std::unique_ptr<IPlatform> platform = PlatformFactory::Create();
    ASSERT_NE(platform, nullptr);
    EXPECT_EQ(platform->GetName(), "Win32");
}

TEST(Win32PlatformFactory, TwoInstancesCanBeAliveAtOnce)
{
    // The conformance suite really does this, and the window class is registered per HINSTANCE:
    // a second registration fails with ERROR_CLASS_ALREADY_EXISTS unless it is refcounted.
    const std::unique_ptr<IPlatform> first = PlatformFactory::Create("Win32");
    const std::unique_ptr<IPlatform> second = PlatformFactory::Create("Win32");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    WindowDescription description;
    description.title = "two platforms";
    description.visible = false;
    EXPECT_NE(first->CreateWindow(description), nullptr);
    EXPECT_NE(second->CreateWindow(description), nullptr);
}

// --- capability truthfulness -----------------------------------------------------------------------

TEST_F(Win32PlatformTest, ClaimsTheCapabilitiesItImplements)
{
    EXPECT_TRUE(capabilities_.multipleWindows);
    EXPECT_TRUE(capabilities_.highDpi);
    EXPECT_TRUE(capabilities_.multipleDisplays);
    EXPECT_TRUE(capabilities_.borderlessFullscreen);
    EXPECT_TRUE(capabilities_.nativeWindowHandle);
    EXPECT_TRUE(capabilities_.surfacePresentation);
    EXPECT_TRUE(capabilities_.openGlContext);
    EXPECT_TRUE(capabilities_.clipboard);
    EXPECT_TRUE(capabilities_.textInput);
    EXPECT_TRUE(capabilities_.exactKeyboardState);
    EXPECT_TRUE(capabilities_.pixelAccurateMouse);
    EXPECT_TRUE(capabilities_.relativeMouse);
    EXPECT_TRUE(capabilities_.cursorShapes);
    EXPECT_TRUE(capabilities_.globalPointer);
    EXPECT_TRUE(capabilities_.inputDeviceEnumeration);
    EXPECT_TRUE(capabilities_.powerInfo);
    EXPECT_TRUE(capabilities_.messageBox);
    EXPECT_TRUE(capabilities_.nativeFileDialog);
}

TEST_F(Win32PlatformTest, DoesNotClaimWhatItHasNotImplemented)
{
    // Each of these is recorded in plans/plan_win32.md section 15 with the work it still needs.
    // A capability reported true and backed by a stub is worse than an honest false, because a
    // caller branches on it -- so this test is as important as the one above.
    EXPECT_FALSE(capabilities_.ime) << "composition and candidates are not delivered";
    EXPECT_FALSE(capabilities_.gamepad) << "XInput is not wired up";
    EXPECT_FALSE(capabilities_.joystick);
    EXPECT_FALSE(capabilities_.gamepadRumble);
    EXPECT_FALSE(capabilities_.gamepadSensors);
    EXPECT_FALSE(capabilities_.haptics);
    EXPECT_FALSE(capabilities_.sensors);
    EXPECT_FALSE(capabilities_.tray);
    EXPECT_FALSE(capabilities_.camera);
    EXPECT_FALSE(capabilities_.managedEntrypoint)
        << "the Win32 backend never renames the host's main()";
}

TEST_F(Win32PlatformTest, UnimplementedServicesAreNullRatherThanInertStubs)
{
    // The conformance suite asserts the general equality; this states the specific answers, so a
    // future change that quietly returns a do-nothing gamepad service is caught here with a name
    // attached rather than as an abstract capability mismatch.
    EXPECT_EQ(platform_->GetGamepad(), nullptr);
    EXPECT_EQ(platform_->GetJoystick(), nullptr);
    EXPECT_EQ(platform_->GetSensors(), nullptr);
    EXPECT_EQ(platform_->GetHaptics(), nullptr);
    EXPECT_EQ(platform_->GetTray(), nullptr);
    EXPECT_EQ(platform_->GetCamera(), nullptr);
}

TEST_F(Win32PlatformTest, ImplementedServicesAreNeverNull)
{
    EXPECT_NE(platform_->GetKeyboard(), nullptr);
    EXPECT_NE(platform_->GetMouse(), nullptr);
    EXPECT_NE(platform_->GetTextInput(), nullptr);
    EXPECT_NE(platform_->GetInputDevices(), nullptr);
    EXPECT_NE(platform_->GetClipboard(), nullptr);
    EXPECT_NE(platform_->GetDisplays(), nullptr);
    EXPECT_NE(platform_->GetDialogs(), nullptr);
    EXPECT_NE(platform_->GetFileSystem(), nullptr);
    EXPECT_NE(platform_->GetSystemInfo(), nullptr);
    EXPECT_NE(platform_->GetGlContext(), nullptr);
}

TEST_F(Win32PlatformTest, VulkanSurfaceServiceMatchesWhetherALoaderIsPresent)
{
    // The one capability decided by the host rather than by this code. Whatever the answer is on
    // this machine, the service and the capability must agree -- a renderer checks the capability
    // and then dereferences the service.
    EXPECT_EQ(platform_->GetVulkanSurface() != nullptr, capabilities_.vulkanSurface);
}

// --- subsystems ---------------------------------------------------------------------------------

TEST_F(Win32PlatformTest, SubsystemsAreRefcountedPerInstance)
{
    EXPECT_FALSE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video));
    platform_->AcquireSubsystem(PlatformSubsystem::Video);
    platform_->AcquireSubsystem(PlatformSubsystem::Video);
    EXPECT_TRUE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video));

    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    EXPECT_TRUE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video))
        << "one of two acquisitions is still outstanding";
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    EXPECT_FALSE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video));
}

TEST_F(Win32PlatformTest, ReleasingMoreThanWasAcquiredIsToleratedAndDoesNotUnderflow)
{
    // GraphicsDevice::Dispose releases video unconditionally, so this is ordinary rather than a
    // defence against misuse. An unguarded counter would go negative and make the NEXT acquisition
    // look already-satisfied.
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    platform_->AcquireSubsystem(PlatformSubsystem::Video);
    EXPECT_TRUE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video));
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    EXPECT_FALSE(platform_->IsSubsystemInitialized(PlatformSubsystem::Video));
}

TEST_F(Win32PlatformTest, EverySubsystemCanBeAcquiredEvenWhereNoDeviceExists)
{
    // Acquisition is not a claim that a device is attached -- that is what the capabilities say.
    // Refusing here would make a caller that acquires Gamepad defensively fail on a machine with
    // no controller plugged in.
    for (const PlatformSubsystem subsystem :
         {PlatformSubsystem::Video, PlatformSubsystem::Audio, PlatformSubsystem::Gamepad,
          PlatformSubsystem::Haptic, PlatformSubsystem::Sensor})
    {
        EXPECT_NO_THROW(platform_->AcquireSubsystem(subsystem)) << ToString(subsystem);
        EXPECT_TRUE(platform_->IsSubsystemInitialized(subsystem)) << ToString(subsystem);
        EXPECT_NO_THROW(platform_->ReleaseSubsystem(subsystem)) << ToString(subsystem);
    }
}

// --- timing -------------------------------------------------------------------------------------

TEST_F(Win32PlatformTest, PerformanceCounterUsesTheHighResolutionSource)
{
    // QueryPerformanceFrequency is at least 1 MHz on every supported Windows version. A frequency
    // of 1000 would mean something fell back to GetTickCount, which cannot resolve a frame.
    EXPECT_GE(platform_->GetPerformanceFrequency(), 1000000u);
}

TEST_F(Win32PlatformTest, TicksAgreeWithThePerformanceCounter)
{
    // Both derive from the same source deliberately, so they cannot disagree about how much time
    // passed. Reading milliseconds from GetTickCount64 while the counter came from QPC is how a
    // fixed-timestep loop ends up with two different opinions of one frame.
    const std::uint64_t frequency = platform_->GetPerformanceFrequency();
    const std::uint64_t counterBefore = platform_->GetPerformanceCounter();
    const std::uint64_t ticksBefore = platform_->GetTicksMilliseconds();

    platform_->Delay(50);

    const std::uint64_t counterElapsedMs =
        ((platform_->GetPerformanceCounter() - counterBefore) * 1000ull) / frequency;
    const std::uint64_t ticksElapsedMs = platform_->GetTicksMilliseconds() - ticksBefore;

    EXPECT_GE(counterElapsedMs, 25u) << "Sleep(50) must advance the counter";
    // Generous: this asserts the two clocks agree, not that a scheduler is precise.
    const std::uint64_t difference = counterElapsedMs > ticksElapsedMs
                                         ? counterElapsedMs - ticksElapsedMs
                                         : ticksElapsedMs - counterElapsedMs;
    EXPECT_LE(difference, 20u) << "counter=" << counterElapsedMs << " ticks=" << ticksElapsedMs;
}

TEST_F(Win32PlatformTest, TicksStartNearZeroForANewPlatform)
{
    // Milliseconds are "since platform creation", not since boot. Returning GetTickCount64 raw
    // would report weeks and overflow every elapsed-time subtraction a caller writes.
    const std::unique_ptr<IPlatform> fresh = PlatformFactory::Create("Win32");
    EXPECT_LT(fresh->GetTicksMilliseconds(), 1000u);
}

TEST_F(Win32PlatformTest, ZeroDelayReturnsPromptly)
{
    const std::uint64_t before = platform_->GetTicksMilliseconds();
    platform_->Delay(0);
    EXPECT_LT(platform_->GetTicksMilliseconds() - before, 500u);
}

// --- adoption -----------------------------------------------------------------------------------

TEST_F(Win32PlatformTest, AdoptionRefusesAnIdThatNamesNoWindow)
{
    EXPECT_THROW((void) platform_->AdoptWindow(4242), PlatformException);
}

TEST_F(Win32PlatformTest, AdoptionRefusesAZeroOrDeadHandleToken)
{
    EXPECT_THROW((void) platform_->AdoptWindowHandle(0), PlatformException);
    // A plausible-looking but invalid pointer value: IsWindow rejects it, which is the check that
    // keeps AdoptWindowHandle from handing back a wrapper around nothing.
    EXPECT_THROW((void) platform_->AdoptWindowHandle(0xDEAD0000u), PlatformException);
}

// --- surface presentation --------------------------------------------------------------------------

TEST_F(Win32PlatformTest, SurfacePresenterRefusesAWindowFromAnotherPlatform)
{
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    WindowDescription description;
    description.title = "foreign";
    description.visible = false;
    const std::unique_ptr<IPlatformWindow> foreign = headless->CreateWindow(description);
    ASSERT_NE(foreign, nullptr);

    // Not a cast that happens to work: a foreign window has no HWND, and presenting to it would
    // dereference a null one frame later rather than at the call the caller can see.
    EXPECT_THROW((void) platform_->CreateSurfacePresenter(*foreign), PlatformException);
}

} // namespace
