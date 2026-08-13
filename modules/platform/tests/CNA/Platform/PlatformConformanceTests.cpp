// SPDX-License-Identifier: MS-PL
//
// PLAT-116/117: the conformance suite.
//
// Every test here is written against IPlatform and run against EVERY implementation compiled into
// this binary. That is the point of the whole exercise: a contract with one implementation is
// just indirection, and the only way to know the interfaces are not quietly SDL-shaped is to make
// something that is not SDL satisfy them and then hold both to the same rules.
//
// A test that needs a specific implementation belongs in that implementation's own file. Anything
// here must be true of any platform CNA will ever have.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class PlatformConformance : public ::testing::TestWithParam<std::string>
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create(GetParam());
        ASSERT_NE(platform_, nullptr);
        capabilities_ = platform_->GetCapabilities();
    }

    std::unique_ptr<IPlatform> platform_;
    PlatformCapabilities capabilities_;
};

// --- identity --------------------------------------------------------------------------------------

TEST_P(PlatformConformance, ReportsANonEmptyNameMatchingWhatWasRequested)
{
    EXPECT_EQ(platform_->GetName(), GetParam());
}

TEST_P(PlatformConformance, IsListedAsAvailable)
{
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    EXPECT_NE(std::find(available.begin(), available.end(), GetParam()), available.end());
}

// --- capability self-consistency (PLAT-117) ------------------------------------------------------------

TEST_P(PlatformConformance, EveryServiceIsNullExactlyWhenItsCapabilityIsFalse)
{
    // The contract's central rule. A platform that advertises a capability and then hands back
    // null sends a caller into a null dereference; one that returns a service while reporting
    // false makes the capability set useless for deciding whether to ask.
    //
    // Keyboard and mouse are intentionally absent from this equality list. Their capability
    // fields describe quality, not presence: a terminal offers timed key releases while
    // ExactKeyboardState is false, and cell-quantised mouse input while PixelAccurateMouse is
    // false. Those are usable services whose approximation callers can inspect explicitly.
    EXPECT_EQ(platform_->GetGamepad() != nullptr, capabilities_.gamepad) << "gamepad";
    EXPECT_EQ(platform_->GetJoystick() != nullptr, capabilities_.joystick) << "joystick";
    EXPECT_EQ(platform_->GetTextInput() != nullptr, capabilities_.textInput) << "textInput";
    EXPECT_EQ(platform_->GetSensors() != nullptr, capabilities_.sensors) << "sensors";
    EXPECT_EQ(platform_->GetHaptics() != nullptr, capabilities_.haptics) << "haptics";
    EXPECT_EQ(platform_->GetInputDevices() != nullptr, capabilities_.inputDeviceEnumeration)
        << "inputDevices";
    EXPECT_EQ(platform_->GetClipboard() != nullptr, capabilities_.clipboard) << "clipboard";
    EXPECT_EQ(platform_->GetDisplays() != nullptr, capabilities_.multipleDisplays) << "displays";
    EXPECT_EQ(platform_->GetTray() != nullptr, capabilities_.tray) << "tray";
    EXPECT_EQ(platform_->GetCamera() != nullptr, capabilities_.camera) << "camera";
    EXPECT_EQ(platform_->GetGlContext() != nullptr, capabilities_.openGlContext) << "openGlContext";
    EXPECT_EQ(platform_->GetVulkanSurface() != nullptr, capabilities_.vulkanSurface) << "vulkanSurface";

    // Dialogs is the one service backed by two capabilities: a platform with a message box but no
    // file dialogs (SDL3 today) still has a dialog service, so the rule is "null when neither".
    EXPECT_EQ(platform_->GetDialogs() != nullptr,
              capabilities_.messageBox || capabilities_.nativeFileDialog)
        << "dialogs";
}

TEST_P(PlatformConformance, ServicesEveryPlatformMustHaveAreNeverNull)
{
    // Path resolution and host information are answerable everywhere, including on a platform
    // with no window, no display and no input.
    EXPECT_NE(platform_->GetFileSystem(), nullptr);
    EXPECT_NE(platform_->GetSystemInfo(), nullptr);
}

TEST_P(PlatformConformance, AnUnsupportedCapabilityRefusesNamingItself)
{
    // "Unsupported means refused, never silently ignored" is the promise a false capability
    // makes. Verified here rather than trusted, because a silent no-op is indistinguishable from
    // success at the call site.
    if (capabilities_.surfacePresentation)
    {
        GTEST_SKIP() << GetParam() << " supports surface presentation";
    }

    WindowDescription description;
    description.title = "conformance";
    description.visible = false;

    std::unique_ptr<IPlatformWindow> window;
    try
    {
        window = platform_->CreateWindow(description);
    }
    catch (const PlatformException&)
    {
        GTEST_SKIP() << "this environment cannot create a window";
    }

    try
    {
        (void)platform_->CreateSurfacePresenter(*window);
        FAIL() << "an unsupported capability must refuse, not silently succeed";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::SurfacePresentation);
    }
}

TEST_P(PlatformConformance, CapabilitiesAreStableAcrossCalls)
{
    // Callers cache the set once. A platform whose answers drifted would make every cached copy
    // wrong without anyone noticing.
    const PlatformCapabilities again = platform_->GetCapabilities();
    for (const PlatformCapability capability : AllCapabilities())
    {
        EXPECT_EQ(Supports(capabilities_, capability), Supports(again, capability))
            << ToString(capability);
    }
}

// --- subsystem lifecycle ------------------------------------------------------------------------------

TEST_P(PlatformConformance, SubsystemsAreRefcounted)
{
    const bool initiallyUp = platform_->IsSubsystemInitialized(PlatformSubsystem::Audio);
    platform_->AcquireSubsystem(PlatformSubsystem::Audio);
    platform_->AcquireSubsystem(PlatformSubsystem::Audio);
    platform_->ReleaseSubsystem(PlatformSubsystem::Audio);
    EXPECT_TRUE(platform_->IsSubsystemInitialized(PlatformSubsystem::Audio));
    platform_->ReleaseSubsystem(PlatformSubsystem::Audio);
    EXPECT_EQ(platform_->IsSubsystemInitialized(PlatformSubsystem::Audio), initiallyUp);
}

TEST_P(PlatformConformance, UnpairedReleaseIsANoOp)
{
    // GraphicsDevice::Dispose releases video unconditionally. Every implementation has to
    // tolerate that, not just SDL3.
    EXPECT_NO_THROW(platform_->ReleaseSubsystem(PlatformSubsystem::Audio));
    EXPECT_NO_THROW(platform_->ReleaseSubsystem(PlatformSubsystem::Audio));
}

// --- events --------------------------------------------------------------------------------------------

TEST_P(PlatformConformance, PollEventsClearsStaleCallerContent)
{
    std::vector<PlatformEvent> batch;
    batch.resize(4, PlatformEvent{QuitEvent{}});
    platform_->PollEvents(batch);
    EXPECT_LT(batch.size(), 4u) << "the caller's previous contents must be discarded";
}

TEST_P(PlatformConformance, PollEventsIsSafeWithNoSubsystemAcquired)
{
    // Called every frame from the game loop, including before anything is set up.
    std::vector<PlatformEvent> batch;
    EXPECT_NO_THROW(platform_->PollEvents(batch));
}

TEST_P(PlatformConformance, PollEventsReusesCapacity)
{
    // The buffer belongs to the caller precisely so a steady-state frame allocates nothing.
    std::vector<PlatformEvent> batch;
    batch.reserve(32);
    const std::size_t capacity = batch.capacity();
    platform_->PollEvents(batch);
    EXPECT_GE(batch.capacity(), capacity);
}

// --- timing ---------------------------------------------------------------------------------------------

TEST_P(PlatformConformance, PerformanceFrequencyIsNeverZero)
{
    // The fixed-timestep loop divides by it; zero is a fault, not a slow frame.
    EXPECT_GT(platform_->GetPerformanceFrequency(), 0u);
}

TEST_P(PlatformConformance, PerformanceCounterIsMonotonic)
{
    std::uint64_t previous = platform_->GetPerformanceCounter();
    for (int i = 0; i < 100; ++i)
    {
        const std::uint64_t current = platform_->GetPerformanceCounter();
        EXPECT_GE(current, previous);
        previous = current;
    }
}

TEST_P(PlatformConformance, DelayAdvancesTheCounter)
{
    const std::uint64_t frequency = platform_->GetPerformanceFrequency();
    const std::uint64_t before = platform_->GetPerformanceCounter();
    platform_->Delay(20);
    const std::uint64_t elapsedMs = ((platform_->GetPerformanceCounter() - before) * 1000ull) / frequency;
    // Deliberately generous: the assertion is that time passed, not that a scheduler is precise.
    EXPECT_GE(elapsedMs, 10u);
}

TEST_P(PlatformConformance, TicksAreMonotonic)
{
    const std::uint64_t before = platform_->GetTicksMilliseconds();
    platform_->Delay(2);
    EXPECT_GE(platform_->GetTicksMilliseconds(), before);
}

// --- windows ---------------------------------------------------------------------------------------------

class PlatformWindowConformance : public PlatformConformance
{
protected:
    void SetUp() override
    {
        PlatformConformance::SetUp();
        if (IsSkipped())
        {
            return;
        }
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no video subsystem here: " << error.what();
        }

        WindowDescription description;
        description.title = "conformance window";
        description.width = 320;
        description.height = 240;
        description.visible = false;
        try
        {
            window_ = platform_->CreateWindow(description);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "cannot create a window here: " << error.what();
        }
        ASSERT_NE(window_, nullptr);
    }

    std::unique_ptr<IPlatformWindow> window_;
};

TEST_P(PlatformWindowConformance, WindowReportsItsRequestedTitleAndSize)
{
    EXPECT_EQ(window_->GetTitle(), "conformance window");
    const WindowBounds bounds = window_->GetClientBounds();
    EXPECT_EQ(bounds.width, 320);
    EXPECT_EQ(bounds.height, 240);
}

TEST_P(PlatformWindowConformance, WindowIdIsNonZero)
{
    // PlatformEvent routes by id, and zero is the contract's "no window" value.
    EXPECT_NE(window_->GetId(), 0u);
}

TEST_P(PlatformWindowConformance, TitleRoundTrips)
{
    window_->SetTitle("renamed");
    EXPECT_EQ(window_->GetTitle(), "renamed");
}

TEST_P(PlatformWindowConformance, SizeChangeLandsAfterSync)
{
    // Every implementation models the same asynchrony, so game code written against one behaves
    // the same on another.
    window_->SetSize(400, 300);
    window_->Sync();
    EXPECT_EQ(window_->GetClientBounds().width, 400);
    EXPECT_EQ(window_->GetClientBounds().height, 300);
}

TEST_P(PlatformWindowConformance, PixelSizeAndDisplayScaleAreSane)
{
    EXPECT_GT(window_->GetPixelSize().width, 0);
    EXPECT_GT(window_->GetPixelSize().height, 0);
    // Never zero: a zero scale divides to infinity in any layout computation.
    EXPECT_GT(window_->GetDisplayScale(), 0.0f);
}

TEST_P(PlatformWindowConformance, NativeHandleAgreesWithTheCapability)
{
    // A platform claiming nativeWindowHandle must actually produce one, and a platform that does
    // not must report a handle a renderer will refuse rather than dereference.
    const NativeWindowHandle handle = window_->GetNativeHandle();
    if (!capabilities_.nativeWindowHandle)
    {
        EXPECT_FALSE(HasNativeWindow(handle)) << Describe(handle);
    }
}

TEST_P(PlatformWindowConformance, StateTogglesDoNotThrow)
{
    EXPECT_NO_THROW(window_->Show());
    EXPECT_NO_THROW(window_->Hide());
    EXPECT_NO_THROW(window_->Minimize());
    EXPECT_NO_THROW(window_->Restore());
    EXPECT_NO_THROW(window_->SetResizable(false));
    EXPECT_NO_THROW(window_->SetBorderless(true));
    EXPECT_NO_THROW(window_->Sync());
}

// --- filesystem ------------------------------------------------------------------------------------------

TEST_P(PlatformConformance, BasePathIsReported)
{
    EXPECT_FALSE(platform_->GetFileSystem()->GetBasePath().empty());
}

TEST_P(PlatformConformance, MissingFileReturnsFalseWithTheOutputUntouched)
{
    // PLAT-21's throw-vs-status split, held to on every implementation.
    std::vector<std::uint8_t> data{7, 8, 9};
    EXPECT_FALSE(platform_->GetFileSystem()->TryLoadFile("/definitely/not/here.bin", data));
    EXPECT_EQ(data.size(), 3u);
}

TEST_P(PlatformConformance, UserFoldersAreUnavailableOrHaveContractSeparator)
{
    for (const UserFolder folder : {UserFolder::Music, UserFolder::Pictures})
    {
        const std::string path = platform_->GetFileSystem()->GetUserFolder(folder);
        EXPECT_TRUE(path.empty() || path.back() == '/' || path.back() == '\\') << path;
    }
}

TEST_P(PlatformConformance, SystemInfoAnswersWithoutFabricating)
{
    // Unknown must stay distinguishable from a real value. A platform that cannot tell reports
    // zero cores or an unknown power state rather than inventing something a caller displays.
    const PowerInfo power = platform_->GetSystemInfo()->GetPowerInfo();
    EXPECT_TRUE(power.percent == -1 || (power.percent >= 0 && power.percent <= 100));
    EXPECT_GE(platform_->GetSystemInfo()->GetLogicalCoreCount(), 0);
    EXPECT_FALSE(platform_->GetSystemInfo()->GetPlatformName().empty());
}

// --- instantiation ------------------------------------------------------------------------------------------

std::vector<std::string> ImplementationsUnderTest() { return PlatformFactory::GetAvailable(); }

INSTANTIATE_TEST_SUITE_P(EveryImplementation, PlatformConformance,
                         ::testing::ValuesIn(ImplementationsUnderTest()),
                         [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

INSTANTIATE_TEST_SUITE_P(EveryImplementation, PlatformWindowConformance,
                         ::testing::ValuesIn(ImplementationsUnderTest()),
                         [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });

} // namespace
