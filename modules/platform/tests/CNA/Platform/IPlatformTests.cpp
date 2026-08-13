// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace CNA::Platform;

/// The smallest window that satisfies IPlatformWindow. Exists so the refusal test below can pass
/// a real reference: forming one from nullptr is undefined behaviour, and this project builds
/// under ASan+UBSan.
class InertWindow final : public IPlatformWindow
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

/// A complete IPlatform with no windowing system behind it.
///
/// This exists to answer one question the rest of Phase 1 cannot: is the contract actually
/// implementable, end to end, by something that is not SDL? Every interface can look reasonable
/// in isolation and still compose into something only SDL could satisfy. Implementing the whole
/// root interface against nothing is what proves otherwise -- and it is a sketch of PLAT-113's
/// HeadlessPlatform.
class FakePlatform final : public IPlatform
{
public:
    [[nodiscard]] const std::string& GetName() const override
    {
        static const std::string name = "Fake";
        return name;
    }

    [[nodiscard]] PlatformCapabilities GetCapabilities() const override
    {
        PlatformCapabilities capabilities;
        capabilities.multipleWindows = true;
        return capabilities;  // everything else false: a deliberately minimal platform
    }

    void AcquireSubsystem(const PlatformSubsystem subsystem) override { ++refCounts_[subsystem]; }

    void ReleaseSubsystem(const PlatformSubsystem subsystem) override
    {
        const auto it = refCounts_.find(subsystem);
        if (it == refCounts_.end() || it->second == 0)
        {
            return;  // unpaired release is a documented no-op, not an error
        }
        --it->second;
    }

    [[nodiscard]] bool IsSubsystemInitialized(const PlatformSubsystem subsystem) const override
    {
        const auto it = refCounts_.find(subsystem);
        return it != refCounts_.end() && it->second > 0;
    }

    [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(const WindowDescription&) override
    {
        return nullptr;  // windowless: exercised by PLAT-113, not needed to prove the shape
    }

    void PollEvents(std::vector<PlatformEvent>& destination) override
    {
        destination.clear();
        for (const PlatformEvent& event : queued_)
        {
            destination.push_back(event);
        }
        queued_.clear();
    }

    [[nodiscard]] std::uint64_t GetPerformanceCounter() const override { return counter_; }
    [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override { return 1000000; }
    [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override { return counter_ / 1000; }
    void Delay(const std::uint32_t milliseconds) override { counter_ += milliseconds * 1000ull; }

    [[nodiscard]] IPlatformKeyboard* GetKeyboard() override { return nullptr; }
    [[nodiscard]] IPlatformMouse* GetMouse() override { return nullptr; }
    [[nodiscard]] IPlatformGamepad* GetGamepad() override { return nullptr; }
    [[nodiscard]] IPlatformJoystick* GetJoystick() override { return nullptr; }
    [[nodiscard]] IPlatformTextInput* GetTextInput() override { return nullptr; }
    [[nodiscard]] IPlatformSensors* GetSensors() override { return nullptr; }
    [[nodiscard]] IPlatformHaptics* GetHaptics() override { return nullptr; }
    [[nodiscard]] IPlatformInputDevices* GetInputDevices() override { return nullptr; }
    [[nodiscard]] IPlatformClipboard* GetClipboard() override { return nullptr; }
    [[nodiscard]] IPlatformDisplays* GetDisplays() override { return nullptr; }
    [[nodiscard]] IPlatformDialogs* GetDialogs() override { return nullptr; }
    [[nodiscard]] IPlatformTray* GetTray() override { return nullptr; }
    [[nodiscard]] IPlatformCameraProvider* GetCamera() override { return nullptr; }
    [[nodiscard]] IPlatformFileSystem* GetFileSystem() override { return nullptr; }
    [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override { return nullptr; }
    [[nodiscard]] IPlatformGlContext* GetGlContext() override { return nullptr; }
    [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override { return nullptr; }

    [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
        IPlatformWindow&) override
    {
        throw PlatformNotSupportedException(PlatformCapability::SurfacePresentation, GetName());
    }

    void Queue(const PlatformEvent& event) { queued_.push_back(event); }

private:
    std::map<PlatformSubsystem, int> refCounts_;
    std::vector<PlatformEvent> queued_;
    std::uint64_t counter_ = 0;
};

// --- the contract is implementable without a windowing system ---------------------------------

TEST(IPlatformTests, IsImplementableWithNoWindowingSystem)
{
    const std::unique_ptr<IPlatform> platform = std::make_unique<FakePlatform>();
    EXPECT_EQ(platform->GetName(), "Fake");
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatform>);
}

TEST(IPlatformTests, HasNoInitializeOrShutdownPair)
{
    // PLAT-4's central finding: CNA production code never calls SDL_Init/SDL_Quit, so global
    // library lifetime belongs to the host application. The contract exposes acquire/release
    // instead, and this test documents that absence as intentional rather than an oversight.
    FakePlatform platform;
    EXPECT_FALSE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
    platform.AcquireSubsystem(PlatformSubsystem::Video);
    EXPECT_TRUE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
}

// --- subsystem refcounting --------------------------------------------------------------------

TEST(IPlatformTests, SubsystemsAreRefcountedNotBoolean)
{
    // Nested acquisition is normal: GraphicsDevice and the input bridge both want video up.
    // A boolean would let the first release tear it down under the second holder.
    FakePlatform platform;
    platform.AcquireSubsystem(PlatformSubsystem::Video);
    platform.AcquireSubsystem(PlatformSubsystem::Video);
    platform.ReleaseSubsystem(PlatformSubsystem::Video);
    EXPECT_TRUE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
    platform.ReleaseSubsystem(PlatformSubsystem::Video);
    EXPECT_FALSE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
}

TEST(IPlatformTests, UnpairedReleaseIsANoOpNotAnError)
{
    // Cleanup after partial initialization may still issue an unmatched release even though
    // GraphicsDevice now records and balances its own acquisition. The contract preserves that
    // tolerance rather than forcing every failure path to fabricate ownership.
    FakePlatform platform;
    EXPECT_NO_THROW(platform.ReleaseSubsystem(PlatformSubsystem::Video));
    EXPECT_FALSE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
}

TEST(IPlatformTests, UnsupportedExternalWindowAdoptionRefusesExplicitly)
{
    FakePlatform platform;
    EXPECT_THROW((void)platform.AdoptWindow(1), PlatformException);
    EXPECT_THROW((void)platform.AdoptWindowHandle(1), PlatformException);
    EXPECT_THROW((void)platform.AdoptWindowHandle(0), PlatformException);
}

TEST(IPlatformTests, SubsystemsAreIndependent)
{
    FakePlatform platform;
    platform.AcquireSubsystem(PlatformSubsystem::Audio);
    EXPECT_TRUE(platform.IsSubsystemInitialized(PlatformSubsystem::Audio));
    EXPECT_FALSE(platform.IsSubsystemInitialized(PlatformSubsystem::Video));
}

TEST(IPlatformTests, SubsystemNamesAreDistinct)
{
    std::set<std::string> names;
    for (const PlatformSubsystem subsystem :
         {PlatformSubsystem::Video, PlatformSubsystem::Audio, PlatformSubsystem::Gamepad,
          PlatformSubsystem::Haptic, PlatformSubsystem::Sensor})
    {
        EXPECT_FALSE(ToString(subsystem).empty());
        names.insert(ToString(subsystem));
    }
    EXPECT_EQ(names.size(), 5u);
}

// --- event batching ---------------------------------------------------------------------------

TEST(IPlatformTests, PollEventsClearsAndRefillsTheCallerBuffer)
{
    FakePlatform platform;
    std::vector<PlatformEvent> batch;
    batch.push_back(QuitEvent{});  // stale content from a previous frame

    platform.Queue(PlatformEvent{WindowEvent{}});
    platform.PollEvents(batch);

    ASSERT_EQ(batch.size(), 1u);
    EXPECT_EQ(GetEventTypeName(batch[0]), "WindowEvent");
}

TEST(IPlatformTests, PollEventsReusesTheBuffersCapacityAcrossFrames)
{
    // The buffer is the caller's precisely so it can be reused; per-frame allocation is what the
    // batch signature exists to avoid.
    FakePlatform platform;
    std::vector<PlatformEvent> batch;
    for (int i = 0; i < 8; ++i)
    {
        platform.Queue(PlatformEvent{QuitEvent{}});
    }
    platform.PollEvents(batch);
    const std::size_t capacityAfterFirstFrame = batch.capacity();
    ASSERT_GE(capacityAfterFirstFrame, 8u);

    platform.Queue(PlatformEvent{QuitEvent{}});
    platform.PollEvents(batch);
    EXPECT_EQ(batch.capacity(), capacityAfterFirstFrame);
    EXPECT_EQ(batch.size(), 1u);
}

TEST(IPlatformTests, PollEventsOnAQuietFrameYieldsAnEmptyBatch)
{
    FakePlatform platform;
    std::vector<PlatformEvent> batch;
    platform.PollEvents(batch);
    EXPECT_TRUE(batch.empty());
}

// --- timing ------------------------------------------------------------------------------------

TEST(IPlatformTests, PerformanceFrequencyIsNeverZero)
{
    // The game loop divides by it; zero would be a division fault, not a slow frame.
    const FakePlatform platform;
    EXPECT_GT(platform.GetPerformanceFrequency(), 0u);
}

TEST(IPlatformTests, PerformanceCounterIsMonotonic)
{
    FakePlatform platform;
    const std::uint64_t before = platform.GetPerformanceCounter();
    platform.Delay(5);
    EXPECT_GE(platform.GetPerformanceCounter(), before);
}

// --- capability-gated services -------------------------------------------------------------------

TEST(IPlatformTests, UnsupportedServicesReturnNullRatherThanASilentStub)
{
    // A stub that accepted calls and did nothing would let a caller believe it had working
    // clipboard support. Null forces the caller back to the capability set.
    FakePlatform platform;
    const PlatformCapabilities capabilities = platform.GetCapabilities();
    EXPECT_FALSE(capabilities.clipboard);
    EXPECT_EQ(platform.GetClipboard(), nullptr);
    EXPECT_FALSE(capabilities.gamepad);
    EXPECT_EQ(platform.GetGamepad(), nullptr);
}

TEST(IPlatformTests, UnsupportedOperationsRefuseNamingTheCapability)
{
    FakePlatform platform;
    InertWindow window;
    try
    {
        (void)platform.CreateSurfacePresenter(window);
        FAIL() << "expected a refusal";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::SurfacePresentation);
    }
}

// --- factory --------------------------------------------------------------------------------------

TEST(PlatformFactoryTests, DefaultNameMatchesTheBuildTimeSelection)
{
    // cmake/PlatformSelection.cmake defines CNA_PLATFORM_<NAME>; the factory is the one place
    // that knows which implementations exist.
    EXPECT_FALSE(PlatformFactory::GetDefaultName().empty());
}

TEST(PlatformFactoryTests, ReportsWhichImplementationsAreCompiledIn)
{
    // GetAvailable() is what the conformance suite enumerates, and what a refusal message names.
    // Its contents depend on CNA_PLATFORM, so the assertion is on consistency rather than on a
    // fixed list: whatever is advertised must actually be creatable.
    for (const std::string& name : PlatformFactory::GetAvailable())
    {
        EXPECT_NO_THROW((void)PlatformFactory::Create(name)) << "advertised but not creatable: " << name;
    }
}

TEST(PlatformFactoryTests, RefusesAnImplementationThatIsNotCompiledIn)
{
    // A name that is not built in must refuse rather than fall back to whatever is. Falling back
    // would hand the caller a platform it did not ask for -- the same failure CNA_PLATFORM's
    // reserved-identifier check prevents at configure time.
    EXPECT_THROW((void)PlatformFactory::Create("NoSuchPlatform"), PlatformException);
}

} // namespace
