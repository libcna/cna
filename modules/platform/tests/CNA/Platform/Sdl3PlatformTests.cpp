// SPDX-License-Identifier: MS-PL
//
// PLAT-28/29/39/40: the SDL3 platform against real SDL3.
//
// These run without a display server by asking SDL for its dummy video driver, which is what
// lets subsystem lifecycle and timing be verified in CI rather than only on a developer desktop.

#include "CNA/Platform/PlatformException.hpp"
#include "System/Environment.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace CNA::Platform;

bool Sdl3IsAvailable()
{
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    return std::find(available.begin(), available.end(), "SDL3") != available.end();
}

/// A window that satisfies the interface without touching SDL, so the refusal test can pass a
/// real reference. Forming one from nullptr is undefined behaviour, and this project builds
/// under ASan+UBSan.
class InertWindowForRefusalTest final : public IPlatformWindow
{
public:
    [[nodiscard]] WindowId GetId() const override { return 1; }
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const override { return {}; }
    [[nodiscard]] std::string GetTitle() const override { return {}; }
    void SetTitle(const std::string&) override {}
    [[nodiscard]] WindowBounds GetClientBounds() const override { return {}; }
    [[nodiscard]] WindowSize GetPixelSize() const override { return {}; }
    void SetSize(int, int) override {}
    [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
    [[nodiscard]] bool IsResizable() const override { return false; }
    void SetResizable(bool) override {}
    [[nodiscard]] bool IsBorderless() const override { return false; }
    void SetBorderless(bool) override {}
    void SetFullscreenMode(WindowFullscreenMode) override {}
    [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override
    {
        return WindowFullscreenMode::Windowed;
    }
    void Show() override {}
    void Hide() override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void Sync() override {}
    [[nodiscard]] bool HasFocus() const override { return false; }
    [[nodiscard]] bool IsMinimized() const override { return false; }
    [[nodiscard]] std::string GetDisplayName() const override { return {}; }
};

class Sdl3PlatformTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!Sdl3IsAvailable())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        // The dummy driver gives a real video subsystem with no display server, so these tests
        // exercise the genuine SDL code path rather than being skipped in a headless CI.
        System::Environment::SetEnvironmentVariable("SDL_VIDEODRIVER", "dummy");
        platform_ = PlatformFactory::Create("SDL3");
        ASSERT_NE(platform_, nullptr);
    }

    /// SDL's subsystem refcount is process-global and shared with every other test in this
    /// binary -- and, in a real game, with the host application. So these tests assert
    /// TRANSITIONS from whatever the state happens to be, never absolute state. Asserting
    /// "video is down" is only true when this suite runs alone, which is exactly the kind of
    /// test that passes in isolation and fails in the suite.
    [[nodiscard]] bool Up() const { return platform_->IsSubsystemInitialized(subsystem_); }

    /// Picks a subsystem that can actually start on this host, and skips the test when none can.
    ///
    /// Video is the obvious choice and the wrong one: this container has no display server, and
    /// SDL_VIDEODRIVER=dummy only takes effect if it is set before SDL commits to a driver --
    /// which another test in this shared binary may already have done. The refcounting semantics
    /// under test are subsystem-independent, so the test picks whatever works instead of
    /// depending on an environment it cannot control.
    void SelectAWorkingSubsystem()
    {
        for (const PlatformSubsystem candidate :
             {PlatformSubsystem::Sensor, PlatformSubsystem::Haptic, PlatformSubsystem::Gamepad,
              PlatformSubsystem::Audio, PlatformSubsystem::Video})
        {
            try
            {
                platform_->AcquireSubsystem(candidate);
                platform_->ReleaseSubsystem(candidate);
                subsystem_ = candidate;
                return;
            }
            catch (const PlatformException&)
            {
                continue;
            }
        }
        GTEST_SKIP() << "no SDL subsystem can start in this environment";
    }

    std::unique_ptr<IPlatform> platform_;
    PlatformSubsystem subsystem_ = PlatformSubsystem::Sensor;
};

TEST_F(Sdl3PlatformTest, FactoryProducesTheSdl3Platform)
{
    EXPECT_EQ(platform_->GetName(), "SDL3");
    EXPECT_EQ(PlatformFactory::GetDefaultName(), "SDL3");
}

TEST_F(Sdl3PlatformTest, ReportsTheCapabilitiesSdl3ActuallyHas)
{
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_TRUE(capabilities.nativeWindowHandle);
    EXPECT_TRUE(capabilities.multipleWindows);
    EXPECT_TRUE(capabilities.highDpi);

    // The two capabilities that exist because a terminal cannot provide them. SDL delivers real
    // key-release events and true pixel coordinates, so both are honestly true here -- and they
    // became true only when PLAT-79/80 actually wired the services, not when SDL gained the
    // ability.
    EXPECT_TRUE(capabilities.exactKeyboardState);
    EXPECT_TRUE(capabilities.pixelAccurateMouse);
}

TEST_F(Sdl3PlatformTest, CapabilitiesReflectWhatIsActuallyWiredUp)
{
    // Not "SDL3 can do everything": a capability is true only once its service accessor returns
    // something. The conformance suite found the earlier, aspirational version advertising
    // gamepad and text input while their accessors returned null.
    const PlatformCapabilities capabilities = platform_->GetCapabilities();

    EXPECT_TRUE(capabilities.multipleWindows);
    EXPECT_TRUE(capabilities.nativeWindowHandle);
    EXPECT_TRUE(capabilities.surfacePresentation);
    EXPECT_TRUE(capabilities.clipboard);

    // Wired in this phase.
    EXPECT_TRUE(capabilities.gamepad);
    EXPECT_TRUE(capabilities.joystick);
    EXPECT_TRUE(capabilities.textInput);

    EXPECT_TRUE(capabilities.sensors);
    EXPECT_TRUE(capabilities.haptics);
    EXPECT_TRUE(capabilities.globalPointer);
    EXPECT_TRUE(capabilities.inputDeviceEnumeration);
    EXPECT_TRUE(capabilities.messageBox);
    EXPECT_TRUE(capabilities.nativeFileDialog);
    EXPECT_EQ(platform_->GetTray() != nullptr, capabilities.tray);
    EXPECT_EQ(platform_->GetCamera() != nullptr, capabilities.camera);

    // Still unwired, so still false -- the pairing the conformance suite enforces.
    EXPECT_TRUE(capabilities.gamepadSensors);
}

TEST_F(Sdl3PlatformTest, HostDependentCapabilitiesAgreeWithTheirServices)
{
    // The contract's rule: a service is null exactly when its capability is false. Advertising
    // Vulkan on a machine with no loader would hand a caller a service that refuses every call.
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_EQ(platform_->GetVulkanSurface() != nullptr, capabilities.vulkanSurface);
}

TEST_F(Sdl3PlatformTest, VulkanCapabilityProbePreservesTheHostsVideoSubsystemState)
{
    const bool initiallyUp = platform_->IsSubsystemInitialized(PlatformSubsystem::Video);
    (void)platform_->GetCapabilities();
    EXPECT_EQ(platform_->IsSubsystemInitialized(PlatformSubsystem::Video), initiallyUp)
        << "the loader probe must balance its temporary Video and Vulkan references";
}

// --- subsystem lifecycle (PLAT-29) --------------------------------------------------------------

TEST_F(Sdl3PlatformTest, AcquiringBringsASubsystemUpAndReleasingRestoresThePriorState)
{
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();
    platform_->AcquireSubsystem(subsystem_);
    EXPECT_TRUE(Up());
    platform_->ReleaseSubsystem(subsystem_);
    EXPECT_EQ(Up(), initiallyUp) << "a matched acquire/release must leave global state as it was";
}

TEST_F(Sdl3PlatformTest, NestedAcquisitionIsRefcountedAgainstRealSdl)
{
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();
    platform_->AcquireSubsystem(subsystem_);
    platform_->AcquireSubsystem(subsystem_);
    platform_->ReleaseSubsystem(subsystem_);
    // Still up after one release of two acquisitions -- verified against SDL's own refcount,
    // not a CNA-side counter.
    EXPECT_TRUE(Up());
    platform_->ReleaseSubsystem(subsystem_);
    EXPECT_EQ(Up(), initiallyUp);
}

TEST_F(Sdl3PlatformTest, UnpairedReleaseIsANoOpNotAnError)
{
    // GraphicsDevice::Dispose releases video unconditionally; the lifecycle audit records that
    // tolerance as deliberate, so this must not throw or disturb SDL's refcount -- including a
    // refcount some other holder is relying on.
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();
    EXPECT_NO_THROW(platform_->ReleaseSubsystem(subsystem_));
    EXPECT_EQ(Up(), initiallyUp);
}

TEST_F(Sdl3PlatformTest, ReleasingMoreThanWasAcquiredDoesNotUnderflow)
{
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();
    platform_->AcquireSubsystem(subsystem_);
    platform_->ReleaseSubsystem(subsystem_);
    // The extra releases must not decrement past this instance's own count and disturb another
    // holder's subsystem -- the concrete hazard the per-instance count exists to prevent.
    EXPECT_NO_THROW(platform_->ReleaseSubsystem(subsystem_));
    EXPECT_NO_THROW(platform_->ReleaseSubsystem(subsystem_));
    EXPECT_EQ(Up(), initiallyUp);
}

TEST_F(Sdl3PlatformTest, SubsystemsAreIndependent)
{
    SelectAWorkingSubsystem();
    const PlatformSubsystem other = subsystem_ == PlatformSubsystem::Video
                                        ? PlatformSubsystem::Sensor
                                        : PlatformSubsystem::Video;
    const bool otherInitiallyUp = platform_->IsSubsystemInitialized(other);
    platform_->AcquireSubsystem(subsystem_);
    EXPECT_TRUE(Up());
    EXPECT_EQ(platform_->IsSubsystemInitialized(other), otherInitiallyUp)
        << "acquiring one subsystem must not bring an unrelated one up";
    platform_->ReleaseSubsystem(subsystem_);
}

TEST_F(Sdl3PlatformTest, DestructorReleasesOnlyWhatThisInstanceAcquired)
{
    // The host application may hold subsystems of its own. Destroying a platform must not tear
    // those down, which is why the instance tracks its own count instead of calling SDL_Quit().
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();
    {
        const std::unique_ptr<IPlatform> outer = PlatformFactory::Create("SDL3");
        outer->AcquireSubsystem(subsystem_);
        {
            const std::unique_ptr<IPlatform> inner = PlatformFactory::Create("SDL3");
            inner->AcquireSubsystem(subsystem_);
        }  // inner destructs, releasing exactly its one acquisition
        EXPECT_TRUE(outer->IsSubsystemInitialized(subsystem_))
            << "the inner platform's destructor must not release the outer one's hold";
    }
    EXPECT_EQ(Up(), initiallyUp);
}

TEST_F(Sdl3PlatformTest, ConcurrentInstancesShareOneBalancedSubsystemLifecycle)
{
    SelectAWorkingSubsystem();
    const bool initiallyUp = Up();

    // Keep one reference alive while two independent platform instances repeatedly adjust SDL's
    // process-global count. The sentinel must remain valid throughout and the complete operation
    // must restore the host's prior state. This is the shape that a per-instance mutex failed to
    // serialize even though each individual instance's map remained internally consistent.
    platform_->AcquireSubsystem(subsystem_);
    auto first = PlatformFactory::Create("SDL3");
    auto second = PlatformFactory::Create("SDL3");
    std::atomic<bool> failed = false;

    const auto exercise = [this, &failed](IPlatform& instance)
    {
        try
        {
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                instance.AcquireSubsystem(subsystem_);
                instance.ReleaseSubsystem(subsystem_);
            }
        }
        catch (...)
        {
            failed.store(true, std::memory_order_release);
        }
    };

    std::thread firstThread(exercise, std::ref(*first));
    std::thread secondThread(exercise, std::ref(*second));
    firstThread.join();
    secondThread.join();

    EXPECT_FALSE(failed.load(std::memory_order_acquire));
    EXPECT_TRUE(Up()) << "one platform's live reference must survive other instances' churn";
    first.reset();
    second.reset();
    EXPECT_TRUE(Up());
    platform_->ReleaseSubsystem(subsystem_);
    EXPECT_EQ(Up(), initiallyUp);
}

// --- timing (PLAT-39) ----------------------------------------------------------------------------

TEST_F(Sdl3PlatformTest, PerformanceFrequencyIsNonZero)
{
    // The fixed-timestep loop divides by it.
    EXPECT_GT(platform_->GetPerformanceFrequency(), 0u);
}

TEST_F(Sdl3PlatformTest, PerformanceCounterAdvancesAcrossADelay)
{
    const std::uint64_t before = platform_->GetPerformanceCounter();
    platform_->Delay(2);
    EXPECT_GT(platform_->GetPerformanceCounter(), before);
}

TEST_F(Sdl3PlatformTest, DelayActuallyWaitsAtLeastRoughlyTheRequestedTime)
{
    const std::uint64_t frequency = platform_->GetPerformanceFrequency();
    const std::uint64_t before = platform_->GetPerformanceCounter();
    platform_->Delay(20);
    const std::uint64_t elapsedMs =
        ((platform_->GetPerformanceCounter() - before) * 1000ull) / frequency;
    // Generous lower bound: schedulers are imprecise, and the assertion under test is "it waited
    // at all", not "it waited exactly".
    EXPECT_GE(elapsedMs, 10u);
}

TEST_F(Sdl3PlatformTest, TicksAreMonotonic)
{
    const std::uint64_t before = platform_->GetTicksMilliseconds();
    platform_->Delay(2);
    EXPECT_GE(platform_->GetTicksMilliseconds(), before);
}

// --- events ---------------------------------------------------------------------------------------

TEST_F(Sdl3PlatformTest, PollEventsDiscardsStaleBufferContent)
{
    // No subsystem acquired on purpose: PollEvents must be safe to call regardless, and
    // asserting the batch comes back EMPTY would be wrong in a shared binary where other tests
    // can leave hundreds of real events queued. What the contract promises is that the caller's
    // previous contents are discarded, so use a payload that no native event can coincidentally
    // reproduce and assert that it is gone instead of making an assumption about the new size.
    const std::string sentinel = "CNA stale PlatformEvent sentinel";
    TextEditingCandidatesEvent stale;
    stale.candidates = {sentinel};

    std::vector<PlatformEvent> batch;
    batch.resize(5, PlatformEvent{stale});
    platform_->PollEvents(batch);

    EXPECT_TRUE(std::none_of(batch.begin(), batch.end(), [&](const PlatformEvent& event) {
        const auto* candidates = std::get_if<TextEditingCandidatesEvent>(&event);
        return candidates != nullptr && candidates->candidates == std::vector<std::string>{sentinel};
    }));
}

// --- not-yet-implemented surfaces refuse rather than returning something broken ---------------------

TEST_F(Sdl3PlatformTest, NotYetImplementedSurfacesRefuseInsteadOfReturningSomethingBroken)
{
    // CreateWindow used to be the example here; PLAT-30 landed and it now works, so this points
    // at what is genuinely still missing. The property under test is the one that matters
    // throughout Phase 2: an unfinished surface refuses explicitly rather than handing back a
    // null or a stub the caller would treat as working.
    InertWindowForRefusalTest window;
    EXPECT_THROW((void)platform_->CreateSurfacePresenter(window), PlatformException);
}

TEST(Sdl3PlatformLinuxHintsTest, ClassicJoystickDiscoveryIsTheDefaultButHostOverrideWins)
{
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
    if (!Sdl3IsAvailable())
    {
        GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
    }

    const char* previousValue = SDL_GetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC);
    const bool hadPreviousValue = previousValue != nullptr;
    const std::string previousCopy = hadPreviousValue ? previousValue : "";

    SDL_ResetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC);
    {
        const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("SDL3");
        EXPECT_NE(platform, nullptr);
        if (platform != nullptr)
        {
            EXPECT_STREQ(SDL_GetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC), "1")
                << "CNA's Linux default must avoid blocking the first GamePad.GetState() on udev";
        }
    }

    const bool overrideSet = SDL_SetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC, "0");
    EXPECT_TRUE(overrideSet);
    if (overrideSet)
    {
        const std::unique_ptr<IPlatform> platform = PlatformFactory::Create("SDL3");
        EXPECT_NE(platform, nullptr);
        if (platform != nullptr)
        {
            EXPECT_STREQ(SDL_GetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC), "0")
                << "an embedding host's explicit SDL discovery policy must take precedence";
        }
    }

    if (hadPreviousValue)
    {
        (void) SDL_SetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC, previousCopy.c_str());
    }
    else
    {
        SDL_ResetHint(SDL_HINT_JOYSTICK_LINUX_CLASSIC);
    }
#else
    GTEST_SKIP() << "the joystick discovery choice is Linux-specific";
#endif
}

TEST_F(Sdl3PlatformTest, TheControllerSubsystemStartsOnDemandRatherThanWithThePlatform)
{
    // The gamepad subsystem used to cost a full udev device enumeration to start -- ~1.9 seconds
    // on this project's Linux reference machine. plans/plan_platform.md PLAT-83 first removed that
    // work from Game::DoInitialize(), and CNA now also selects SDL's cheap classic discovery as its
    // desktop-Linux default. The platform still starts the subsystem only when something asks for
    // the service: constructing a platform must not start it, and asking for the gamepad must.
    //
    // A transition, not absolute state, per this fixture's own rule: SDL's refcount is
    // process-global and another test in this binary may already hold the subsystem up, in which
    // case there is no transition left to observe and nothing to assert.
    std::unique_ptr<IPlatform> platform = PlatformFactory::Create("SDL3");
    ASSERT_NE(platform, nullptr);

    if (platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad))
    {
        GTEST_SKIP() << "something else in this process already holds the controller subsystem up";
    }

    EXPECT_FALSE(platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad))
        << "constructing a platform must not start the controller subsystem";

    EXPECT_NE(platform->GetGamepad(), nullptr);
    EXPECT_TRUE(platform->IsSubsystemInitialized(PlatformSubsystem::Gamepad))
        << "asking for the gamepad service is what starts it";

    // Second time through is the same instance's already-acquired subsystem, not a second
    // acquisition: the platform releases exactly what it took, and taking twice would leave SDL's
    // refcount one above where this test found it.
    EXPECT_NE(platform->GetJoystick(), nullptr);

    platform.reset();
    EXPECT_FALSE(PlatformFactory::Create("SDL3")->IsSubsystemInitialized(PlatformSubsystem::Gamepad))
        << "the destroyed platform must have released what it acquired";
}

TEST_F(Sdl3PlatformTest, UnknownPlatformNameRefusesAndListsWhatIsAvailable)
{
    try
    {
        (void)PlatformFactory::Create("SDL2");
        FAIL() << "expected a refusal";
    }
    catch (const PlatformException& error)
    {
        EXPECT_NE(std::string(error.what()).find("SDL3"), std::string::npos)
            << "the refusal should name what IS available";
    }
}

} // namespace
