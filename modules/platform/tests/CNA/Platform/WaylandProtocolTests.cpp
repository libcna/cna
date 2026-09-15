// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0111: the Wayland backend against the in-process test compositor.
//
// Each test drives the backend through the public platform contract and checks what reached the
// compositor -- the requests it received, in the order and with the values the protocols require
// -- and what came back as platform events. The compositor posts the protocol error a strict
// compositor would for any rule broken, and every test ends by asserting that none was, so a
// protocol violation anywhere in a test's traffic fails it even when the test was about
// something else.

#include <gtest/gtest.h>

#if defined(CNA_WAYLAND_HAVE_TEST_COMPOSITOR)

#include "WaylandTestCompositor.hpp"

#include "../../../src/Wayland/WaylandPlatform.hpp"
#include "../../../src/Wayland/WaylandWindow.hpp"

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <poll.h>
#include <thread>
#include <unistd.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Wayland;
using namespace CNA::Platform::Wayland::Testing;
using namespace std::chrono_literals;

class WaylandProtocol : public ::testing::Test
{
protected:
    void Start(CompositorOptions options = {})
    {
        compositor_ = std::make_unique<TestCompositor>(std::move(options));
        compositor_->ExportSocket();
        platform_ = std::make_unique<WaylandPlatform>();
        // The socket is the platform's now, or the connection failed; either way no later
        // connection in this process may find it.
        ::unsetenv("WAYLAND_SOCKET");
        ASSERT_NE(platform_->GetConnectionForTesting(), nullptr) << platform_->GetConnectionError();
    }

    void TearDown() override
    {
        presenters_.clear();
        windows_.clear();
        const bool expectClean = compositor_ != nullptr && !expectDisconnect_;
        std::string error;
        std::vector<std::string> violations;
        if (expectClean)
        {
            // Everything the platform sent before it went away has been read.
            if (platform_ != nullptr && platform_->GetConnectionForTesting() != nullptr)
            {
                (void) platform_->GetConnectionForTesting()->Roundtrip(1s);
            }
            error = compositor_->GetPostedError();
            violations = compositor_->GetViolations();
        }
        platform_.reset();
        if (expectClean)
        {
            // The platform's own teardown is traffic too.
            error = compositor_->GetPostedError();
            const std::vector<std::string> late = compositor_->GetViolations();
            violations = late;
        }
        compositor_.reset();
        ::unsetenv("WAYLAND_SOCKET");
        EXPECT_EQ(error, "") << "the backend broke a protocol rule";
        for (const std::string& violation : violations)
        {
            ADD_FAILURE() << "protocol violation: " << violation;
        }
    }

    /// Pumps the platform once, collecting what it produced.
    const std::vector<PlatformEvent>& Pump()
    {
        batch_.clear();
        platform_->PollEvents(batch_);
        seen_.insert(seen_.end(), batch_.begin(), batch_.end());
        return batch_;
    }

    /// Makes the compositor process everything the platform has sent, then pumps its answers.
    void Settle()
    {
        (void) platform_->GetConnectionForTesting()->Roundtrip(2s);
        Pump();
    }

    bool PumpUntil(const std::function<bool()>& condition, const std::chrono::milliseconds budget = 3000ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (!condition())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return condition();
            }
            (void) platform_->GetConnectionForTesting()->DispatchFor(10ms);
            Pump();
        }
        return true;
    }

    IPlatformWindow& MakeWindow(WindowDescription description = {})
    {
        if (description.title.empty())
        {
            description.title = "protocol test";
        }
        windows_.push_back(platform_->CreateWindow(description));
        return *windows_.back();
    }

    IPlatformSurfacePresenter& PresenterFor(IPlatformWindow& window)
    {
        presenters_.push_back(platform_->CreateSurfacePresenter(window));
        presenters_.back()->SetVSync(false);
        return *presenters_.back();
    }

    /// Presents one frame of a solid colour (0xRRGGBB).
    void PresentColour(IPlatformSurfacePresenter& presenter, IPlatformWindow& window, const std::uint32_t rgb)
    {
        const WindowSize size = window.GetPixelSize();
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size.width) * size.height * 4);
        for (std::size_t index = 0; index < pixels.size(); index += 4)
        {
            pixels[index + 0] = static_cast<std::uint8_t>(rgb >> 16);
            pixels[index + 1] = static_cast<std::uint8_t>(rgb >> 8);
            pixels[index + 2] = static_cast<std::uint8_t>(rgb);
            pixels[index + 3] = 0xFF;
        }
        SurfaceFrame frame;
        frame.pixels = pixels.data();
        frame.width = size.width;
        frame.height = size.height;
        frame.strideBytes = size.width * 4;
        presenter.Present(frame);
    }

    template <typename T>
    [[nodiscard]] std::vector<T> SeenOf() const
    {
        std::vector<T> result;
        for (const PlatformEvent& event : seen_)
        {
            if (const T* typed = std::get_if<T>(&event))
            {
                result.push_back(*typed);
            }
        }
        return result;
    }

    [[nodiscard]] int CountWindowEvents(const WindowId window, const WindowEventKind kind) const
    {
        int count = 0;
        for (const WindowEvent& event : SeenOf<WindowEvent>())
        {
            count += event.window == window && event.kind == kind ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] bool SawQuit() const { return !SeenOf<QuitEvent>().empty(); }

    std::unique_ptr<TestCompositor> compositor_;
    std::unique_ptr<WaylandPlatform> platform_;
    std::vector<std::unique_ptr<IPlatformWindow>> windows_;
    std::vector<std::unique_ptr<IPlatformSurfacePresenter>> presenters_;
    std::vector<PlatformEvent> batch_;
    std::vector<PlatformEvent> seen_;
    bool expectDisconnect_ = false;
};

// --- connection and capabilities (WAYLAND-0030/0100) ----------------------------------------------

TEST_F(WaylandProtocol, ConnectsBindsAndReportsWhatTheCompositorOffers)
{
    Start();
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_TRUE(capabilities.multipleWindows);
    EXPECT_TRUE(capabilities.nativeWindowHandle);
    EXPECT_TRUE(capabilities.highDpi);
    EXPECT_TRUE(capabilities.multipleDisplays);
    EXPECT_TRUE(capabilities.surfacePresentation);
    EXPECT_TRUE(capabilities.relativeMouse);
    EXPECT_TRUE(capabilities.clipboard);
    EXPECT_TRUE(capabilities.primarySelection);
    EXPECT_TRUE(capabilities.dragAndDrop);
    EXPECT_TRUE(capabilities.ime);
    EXPECT_TRUE(capabilities.cursorShapes);
    EXPECT_FALSE(capabilities.globalPointer) << "Wayland gives a client no global pointer (D-19)";
    EXPECT_FALSE(capabilities.messageBox);
    EXPECT_FALSE(capabilities.nativeFileDialog) << "no session bus in tests, so no portal";
    EXPECT_EQ(platform_->GetSeatCount(), 1u);
    EXPECT_EQ(compositor_->GetKeyboardCount(), 1);
    EXPECT_EQ(compositor_->GetPointerInfo().pointers, 1);
    EXPECT_EQ(platform_->GetDisplays()->GetDisplays().size(), 1u);
}

TEST_F(WaylandProtocol, EachMissingOptionalProtocolTurnsOffExactlyItsCapability)
{
    CompositorOptions options;
    options.relativePointer = false;
    options.primarySelection = false;
    options.textInput = false;
    options.cursorShape = false;
    Start(options);
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_FALSE(capabilities.relativeMouse);
    EXPECT_FALSE(capabilities.primarySelection);
    EXPECT_EQ(platform_->GetPrimarySelection(), nullptr);
    EXPECT_FALSE(capabilities.ime);
    EXPECT_TRUE(capabilities.clipboard);
    EXPECT_TRUE(capabilities.textInput) << "committed text comes from xkbcommon, not from text-input-v3";
    EXPECT_NE(platform_->GetTextInput(), nullptr);
    EXPECT_THROW(platform_->GetMouse()->SetRelativeMode(1, true), PlatformNotSupportedException);
}

TEST_F(WaylandProtocol, WithoutASeatThereIsNoClipboardAndNoInputDevices)
{
    CompositorOptions options;
    options.seat = false;
    Start(options);
    EXPECT_EQ(platform_->GetSeatCount(), 0u);
    EXPECT_FALSE(platform_->GetCapabilities().clipboard);
    EXPECT_EQ(platform_->GetClipboard(), nullptr);
    EXPECT_FALSE(platform_->GetKeyboard()->HasKeyboard());
}

TEST_F(WaylandProtocol, AnOldCompositorIsBoundAtItsOwnVersions)
{
    CompositorOptions options;
    options.compositorVersion = 4;
    options.wmBaseVersion = 2;
    options.seatVersion = 5;
    options.outputVersion = 2;
    Start(options);
    const WaylandGlobals& globals = platform_->GetConnectionForTesting()->GetGlobals();
    EXPECT_EQ(globals.compositorVersion, 4u);
    EXPECT_EQ(globals.wmBaseVersion, 2u);
    IPlatformWindow& window = MakeWindow();
    Settle();
    EXPECT_TRUE(compositor_->GetToplevel(0).has_value());
    EXPECT_EQ(window.GetClientBounds().width, 800);
}

TEST_F(WaylandProtocol, APingIsAnsweredFromTheOrdinaryPump)
{
    Start();
    compositor_->Ping();
    Pump();
    Settle();
    EXPECT_EQ(compositor_->GetPongCount(), 1);
}

// --- windows (WAYLAND-0033..0035) -----------------------------------------------------------------

TEST_F(WaylandProtocol, AShownWindowIsConfiguredAcknowledgedAndDescribed)
{
    Start();
    WindowDescription description;
    description.title = "Configured";
    description.width = 640;
    description.height = 360;
    description.minimumWidth = 320;
    description.minimumHeight = 200;
    IPlatformWindow& window = MakeWindow(description);
    Settle();

    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    ASSERT_TRUE(toplevel.has_value());
    EXPECT_EQ(toplevel->title, "Configured");
    EXPECT_FALSE(toplevel->appId.empty()) << "a compositor matches the .desktop file by app id";
    EXPECT_GE(toplevel->acks, 1);
    EXPECT_EQ(toplevel->lastAckedSerial, toplevel->lastConfigureSerial);
    EXPECT_TRUE(toplevel->hasGeometry);
    // GNOME has no server-side decorations: a 32-unit title bar of CNA's own sits above the
    // content, inside the window geometry.
    EXPECT_EQ(toplevel->geometryY, -32);
    EXPECT_EQ(toplevel->geometryWidth, 640);
    EXPECT_EQ(toplevel->geometryHeight, 360 + 32);
    EXPECT_EQ(toplevel->minWidth, 320);
    EXPECT_EQ(toplevel->minHeight, 200 + 32);
    EXPECT_EQ(toplevel->viewportWidth, 640);
    EXPECT_EQ(toplevel->viewportHeight, 360);
    EXPECT_TRUE(toplevel->opaqueRegion);
    EXPECT_FALSE(toplevel->mapped) << "nothing has drawn into the window yet";
    EXPECT_EQ(window.GetClientBounds().width, 640);
    EXPECT_EQ(window.GetClientBounds().height, 360);
    EXPECT_EQ(window.GetPixelSize().width, 640);
}

TEST_F(WaylandProtocol, AHiddenWindowHasNoRoleUntilShown)
{
    Start();
    WindowDescription description;
    description.visible = false;
    IPlatformWindow& window = MakeWindow(description);
    Settle();
    EXPECT_TRUE(compositor_->GetToplevels().empty());
    window.Show();
    Settle();
    EXPECT_EQ(compositor_->GetToplevels().size(), 1u);
    window.Hide();
    Settle();
    EXPECT_TRUE(compositor_->GetToplevels().empty());
    window.Show();
    Settle();
    ASSERT_EQ(compositor_->GetToplevels().size(), 1u);
    EXPECT_GE(compositor_->GetToplevel(0)->acks, 1);
}

TEST_F(WaylandProtocol, ThePresenterMapsTheWindowWithABufferOfItsPixelSize)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x3366CC);
    Settle();
    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    ASSERT_TRUE(toplevel.has_value());
    EXPECT_TRUE(toplevel->mapped);
    EXPECT_EQ(toplevel->bufferWidth, 800);
    EXPECT_EQ(toplevel->bufferHeight, 480);
    EXPECT_EQ(toplevel->centrePixel & 0x00FFFFFFu, 0x3366CCu) << std::hex << toplevel->centrePixel;
    // The title bar is drawn and committed too.
    bool titleBar = false;
    for (const SubsurfaceInfo& sub : compositor_->GetSubsurfaces())
    {
        titleBar = titleBar || (sub.y == -32 && sub.bufferWidth > 0);
    }
    EXPECT_TRUE(titleBar);
}

TEST_F(WaylandProtocol, ACompositorResizeReachesTheApplicationAndTheNextBuffer)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x102030);
    Settle();
    seen_.clear();

    compositor_->Configure(0, 1024, 600 + 32, {XDG_TOPLEVEL_STATE_ACTIVATED});
    Settle();
    EXPECT_EQ(window.GetClientBounds().width, 1024);
    EXPECT_EQ(window.GetClientBounds().height, 600);
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Resized), 1);
    PresentColour(presenter, window, 0x102030);
    Settle();
    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    EXPECT_EQ(toplevel->lastAckedSerial, toplevel->lastConfigureSerial);
    EXPECT_EQ(toplevel->geometryWidth, 1024);
    EXPECT_EQ(toplevel->geometryHeight, 632);
    EXPECT_EQ(toplevel->bufferWidth, 1024);
    EXPECT_EQ(toplevel->bufferHeight, 600);
}

TEST_F(WaylandProtocol, AFloatingSizeRespectsTheApplicationsLimits)
{
    Start();
    WindowDescription description;
    description.minimumWidth = 400;
    description.minimumHeight = 300;
    description.maximumWidth = 1000;
    description.maximumHeight = 700;
    IPlatformWindow& window = MakeWindow(description);
    Settle();
    compositor_->Configure(0, 200, 100, {});
    Settle();
    EXPECT_EQ(window.GetClientBounds().width, 400);
    EXPECT_EQ(window.GetClientBounds().height, 300);
    compositor_->Configure(0, 3000, 3000, {});
    Settle();
    EXPECT_EQ(window.GetClientBounds().width, 1000);
    EXPECT_EQ(window.GetClientBounds().height, 700);
}

TEST_F(WaylandProtocol, MaximizeTakesExactlyTheConfiguredGeometryAndRestoreBringsTheSizeBack)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x445566);
    Settle();
    seen_.clear();

    window.Maximize();
    window.Sync();
    Pump();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Maximized), 1);
    // The whole output is the window geometry: 1920x1080 with the title bar inside it.
    EXPECT_EQ(window.GetClientBounds().width, 1920);
    EXPECT_EQ(window.GetClientBounds().height, 1080 - 32);
    PresentColour(presenter, window, 0x445566);
    Settle();
    std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    EXPECT_EQ(toplevel->geometryWidth, 1920);
    EXPECT_EQ(toplevel->geometryHeight, 1080);

    window.Restore();
    window.Sync();
    Pump();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Restored), 1);
    EXPECT_EQ(window.GetClientBounds().width, 800);
    EXPECT_EQ(window.GetClientBounds().height, 480);
    PresentColour(presenter, window, 0x445566);
    Settle();
    toplevel = compositor_->GetToplevel(0);
    EXPECT_FALSE(toplevel->maximizeRequested);
    EXPECT_EQ(toplevel->geometryWidth, 800);
}

TEST_F(WaylandProtocol, FullscreenIsBorderlessNeverExclusiveAndHasNoTitleBar)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x000000);
    Settle();
    window.SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::BorderlessFullscreen)
        << "an ordinary Wayland client cannot own a display mode (D-11)";
    EXPECT_EQ(window.GetClientBounds().width, 1920);
    EXPECT_EQ(window.GetClientBounds().height, 1080) << "no title bar while fullscreen";
    PresentColour(presenter, window, 0x000000);
    Settle();
    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    EXPECT_TRUE(toplevel->fullscreenRequested);
    EXPECT_EQ(toplevel->geometryY, 0);
    EXPECT_EQ(toplevel->geometryHeight, 1080);

    window.SetFullscreenMode(WindowFullscreenMode::Windowed);
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::Windowed);
    EXPECT_EQ(window.GetClientBounds().width, 800);
}

TEST_F(WaylandProtocol, ACompositorThatIgnoresStateRequestsIsNotBelievedToHaveHonouredThem)
{
    CompositorOptions options;
    options.answerStateRequests = false;
    Start(options);
    IPlatformWindow& window = MakeWindow();
    Settle();
    window.Maximize();
    window.Sync();
    Pump();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Maximized), 0);
    window.SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    window.Sync();
    Pump();
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::Windowed) << "what was confirmed, not what was asked";
}

TEST_F(WaylandProtocol, CloseAsksTheApplicationAndQuitsOnlyWithTheLastWindow)
{
    Start();
    IPlatformWindow& first = MakeWindow();
    IPlatformWindow& second = MakeWindow();
    Settle();
    ASSERT_EQ(compositor_->GetToplevels().size(), 2u);
    compositor_->Close(1);
    Settle();
    EXPECT_EQ(CountWindowEvents(second.GetId(), WindowEventKind::CloseRequested), 1);
    EXPECT_FALSE(SawQuit()) << "closing a secondary window does not end the application";
    windows_.pop_back();
    Settle();
    ASSERT_EQ(compositor_->GetToplevels().size(), 1u);
    compositor_->Close(0);
    Settle();
    EXPECT_EQ(CountWindowEvents(first.GetId(), WindowEventKind::CloseRequested), 1);
    EXPECT_TRUE(SawQuit());
}

TEST_F(WaylandProtocol, DestroyingWindowsLeavesNothingBehind)
{
    Start();
    const int baseline = compositor_->GetSurfaceCount();
    for (int round = 0; round < 5; ++round)
    {
        IPlatformWindow& window = MakeWindow();
        IPlatformSurfacePresenter& presenter = PresenterFor(window);
        PresentColour(presenter, window, 0xFF0000);
        platform_->GetMouse()->SetCursor(SystemCursor::Pointer);
        presenters_.clear();
        windows_.clear();
        Settle();
    }
    EXPECT_EQ(compositor_->GetSurfaceCount(), baseline);
    EXPECT_TRUE(compositor_->GetToplevels().empty());
    EXPECT_TRUE(compositor_->GetSubsurfaces().empty());
}

TEST_F(WaylandProtocol, StateChangesAfterTheLastWindowAreHarmless)
{
    Start();
    {
        IPlatformWindow& window = MakeWindow();
        (void) window;
        Settle();
    }
    windows_.clear();
    Settle();
    // A configure or close in flight for a destroyed window arrives at an object libwayland has
    // already dropped; nothing may reach the application.
    seen_.clear();
    Pump();
    EXPECT_TRUE(SeenOf<WindowEvent>().empty());
}

// --- scaling (WAYLAND-0042) ------------------------------------------------------------------

TEST_F(WaylandProtocol, AFractionalScaleResizesTheBufferNotTheWindow)
{
    Start();
    WindowDescription description;
    description.highDpi = true;
    description.width = 800;
    description.height = 600;
    IPlatformWindow& window = MakeWindow(description);
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x808080);
    Settle();
    seen_.clear();

    compositor_->SendPreferredScale(0, 150);  // 1.25
    Settle();
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.25f);
    EXPECT_EQ(window.GetClientBounds().width, 800);
    EXPECT_EQ(window.GetPixelSize().width, 1000);
    EXPECT_EQ(window.GetPixelSize().height, 750);
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::DisplayScaleChanged), 1);
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::PixelSizeChanged), 1);
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::Resized), 0);

    PresentColour(presenter, window, 0x808080);
    Settle();
    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    EXPECT_EQ(toplevel->bufferWidth, 1000);
    EXPECT_EQ(toplevel->bufferHeight, 750);
    EXPECT_EQ(toplevel->bufferScale, 1) << "never set_buffer_scale while a viewport scales";
    EXPECT_EQ(toplevel->viewportWidth, 800);
    EXPECT_EQ(toplevel->viewportHeight, 600);
}

TEST_F(WaylandProtocol, AnApplicationThatIsNotHighDpiIsLeftToTheCompositorToScale)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->SendPreferredScale(0, 240);
    compositor_->SendPreferredBufferScale(0, 2);
    Settle();
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.0f);
    EXPECT_EQ(window.GetPixelSize().width, 800);
}

TEST_F(WaylandProtocol, WithoutAViewporterAnIntegerScaleIsTheBufferScale)
{
    CompositorOptions options;
    options.viewporter = false;
    options.fractionalScale = false;
    Start(options);
    WindowDescription description;
    description.highDpi = true;
    description.width = 401;  // odd: the buffer must still be a multiple of the scale
    description.height = 301;
    IPlatformWindow& window = MakeWindow(description);
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    compositor_->SendPreferredBufferScale(0, 2);
    Settle();
    EXPECT_EQ(window.GetPixelSize().width, 802);
    PresentColour(presenter, window, 0x00FF00);
    Settle();
    const std::optional<ToplevelInfo> toplevel = compositor_->GetToplevel(0);
    EXPECT_EQ(toplevel->bufferScale, 2);
    EXPECT_EQ(toplevel->bufferWidth, 802);
    EXPECT_EQ(toplevel->bufferHeight, 602);
}

TEST_F(WaylandProtocol, TheOutputsAWindowIsOnDecideItsIntegerScaleOnOldCompositors)
{
    CompositorOptions options;
    options.compositorVersion = 5;  // no preferred_buffer_scale
    options.fractionalScale = false;
    Start(options);
    OutputSpec second;
    second.name = "CNA-2";
    second.x = 1920;
    second.scale = 2;
    second.width = 3840;
    second.height = 2160;
    const int hidpi = compositor_->AddOutput(second);
    Settle();
    WindowDescription description;
    description.highDpi = true;
    IPlatformWindow& window = MakeWindow(description);
    Settle();
    compositor_->EnterOutput(0, 0);
    Settle();
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.0f);
    compositor_->EnterOutput(0, hidpi);
    Settle();
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), 2.0f);
    EXPECT_EQ(window.GetDisplayName(), "CNA-2");
    compositor_->LeaveOutput(0, hidpi);
    Settle();
    EXPECT_FLOAT_EQ(window.GetDisplayScale(), 1.0f);
}

// --- outputs (WAYLAND-0040/0041) ------------------------------------------------------------------

TEST_F(WaylandProtocol, OutputsComeAndGoWhileRunning)
{
    Start();
    IPlatformDisplays* displays = platform_->GetDisplays();
    ASSERT_EQ(displays->GetDisplays().size(), 1u);
    OutputSpec second;
    second.name = "CNA-2";
    second.x = 1920;
    second.width = 2560;
    second.height = 1440;
    second.scale = 2;
    second.logicalWidth = 2048;  // a 125 % desktop
    second.logicalHeight = 1152;
    const int index = compositor_->AddOutput(second);
    Settle();
    Settle();
    const std::vector<DisplayInfo> both = displays->GetDisplays();
    ASSERT_EQ(both.size(), 2u);
    const auto found = std::find_if(both.begin(), both.end(), [](const DisplayInfo& info) { return info.x == 1920; });
    ASSERT_NE(found, both.end());
    EXPECT_EQ(found->width, 2048);
    EXPECT_FLOAT_EQ(found->contentScale, 1.25f);

    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->EnterOutput(0, index);
    Settle();
    compositor_->RemoveOutput(index);
    Settle();
    EXPECT_EQ(displays->GetDisplays().size(), 1u);
    EXPECT_EQ(window.GetDisplayName(), "") << "the window's output is gone, and it knows it";
}

TEST_F(WaylandProtocol, AnOutputsScaleChangeIsSeenAtOnce)
{
    Start();
    IPlatformDisplays* displays = platform_->GetDisplays();
    compositor_->SetOutputScale(0, 2);
    Settle();
    const std::vector<DisplayInfo> after = displays->GetDisplays();
    ASSERT_EQ(after.size(), 1u);
    EXPECT_FLOAT_EQ(after.front().contentScale, 2.0f);
    EXPECT_EQ(after.front().width, 960);
}

TEST_F(WaylandProtocol, AWithdrawnSingletonIsNoLongerUsed)
{
    Start();
    compositor_->RemoveGlobal("zwp_relative_pointer_manager_v1");
    Settle();
    EXPECT_EQ(platform_->GetConnectionForTesting()->GetGlobals().relativePointerManager, nullptr);
    IPlatformWindow& window = MakeWindow();
    Settle();
    EXPECT_THROW(platform_->GetMouse()->SetRelativeMode(window.GetId(), true), PlatformNotSupportedException);
}

// --- keyboard (WAYLAND-0050..0053) -----------------------------------------------------------------

TEST_F(WaylandProtocol, FocusAndKeysArriveWithScancodesKeyCodesAndText)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    compositor_->KeyboardEnter(0);
    Settle();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::FocusGained), 1);
    EXPECT_TRUE(window.HasFocus());

    compositor_->Key(KEY_A, true);
    compositor_->Key(KEY_A, false);
    compositor_->Key(KEY_LEFTSHIFT, true);
    compositor_->Key(KEY_B, true);
    compositor_->Key(KEY_B, false);
    compositor_->Key(KEY_LEFTSHIFT, false);
    Settle();

    const std::vector<KeyEvent> keys = SeenOf<KeyEvent>();
    ASSERT_EQ(keys.size(), 6u);
    EXPECT_EQ(keys[0].scancode, Scancode::A);
    EXPECT_EQ(keys[0].keycode, KeyCode::A);
    EXPECT_TRUE(keys[0].pressed);
    EXPECT_FALSE(keys[1].pressed);
    EXPECT_EQ(keys[2].scancode, Scancode::LeftShift);
    EXPECT_EQ(keys[3].scancode, Scancode::B);
    EXPECT_NE(keys[3].modifiers & static_cast<std::uint16_t>(KeyModifier::Shift), 0);
    std::string text;
    for (const TextInputEvent& event : SeenOf<TextInputEvent>())
    {
        text += event.text;
    }
    EXPECT_EQ(text, "aB");

    compositor_->KeyboardLeave();
    Settle();
    EXPECT_EQ(CountWindowEvents(window.GetId(), WindowEventKind::FocusLost), 1);
    EXPECT_FALSE(window.HasFocus());
}

TEST_F(WaylandProtocol, KeysHeldAtEnterAreStateNotEventsAndLeaveReleasesThemQuietly)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0, {KEY_W});
    Settle();
    IPlatformKeyboard* keyboard = platform_->GetKeyboard();
    keyboard->Update();
    const std::vector<KeyCode>& held = keyboard->GetSnapshot().pressedKeys;
    EXPECT_NE(std::find(held.begin(), held.end(), KeyCode::W), held.end());
    EXPECT_TRUE(SeenOf<KeyEvent>().empty()) << "a key held before focus was never pressed in this window";
    compositor_->KeyboardLeave();
    Settle();
    keyboard->Update();
    EXPECT_TRUE(keyboard->GetSnapshot().pressedKeys.empty());
    EXPECT_TRUE(SeenOf<KeyEvent>().empty());
}

TEST_F(WaylandProtocol, TheCzechLayoutTypesCzechAndKeepsGameKeysWhereTheyAre)
{
    CompositorOptions options;
    options.layout = "cz";
    Start(options);
    IPlatformWindow& window = MakeWindow();
    Settle();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    compositor_->KeyboardEnter(0);
    for (const std::uint32_t key : {KEY_2, KEY_3, KEY_Z, KEY_Y})
    {
        compositor_->Key(key, true);
        compositor_->Key(key, false);
    }
    Settle();
    std::string text;
    for (const TextInputEvent& event : SeenOf<TextInputEvent>())
    {
        text += event.text;
    }
    // cz: the number row types ě š, and Y and Z trade places (QWERTZ): the key where a US
    // keyboard has Z types y, and the one where it has Y types z.
    EXPECT_EQ(text, "\xc4\x9b\xc5\xa1yz");
    const std::vector<KeyEvent> keys = SeenOf<KeyEvent>();
    ASSERT_EQ(keys.size(), 8u);
    EXPECT_EQ(keys[0].scancode, Scancode::D2) << "the physical key";
    EXPECT_EQ(keys[0].keycode, KeyCode::D2) << "the number row keeps its digits for games";
    EXPECT_EQ(keys[4].scancode, Scancode::Z);
    EXPECT_EQ(keys[4].keycode, KeyCode::Y) << "the key labelled Y on a Czech keyboard";
}

TEST_F(WaylandProtocol, ANewKeymapTakesEffectForTheNextKey)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    platform_->GetTextInput()->Start(window.GetId(), TextInputType::Text);
    compositor_->KeyboardEnter(0);
    compositor_->Key(KEY_Y, true);
    compositor_->Key(KEY_Y, false);
    Settle();
    compositor_->ReplaceKeymap("de");
    compositor_->Key(KEY_Y, true);
    compositor_->Key(KEY_Y, false);
    Settle();
    std::string text;
    for (const TextInputEvent& event : SeenOf<TextInputEvent>())
    {
        text += event.text;
    }
    EXPECT_EQ(text, "yz");
    EXPECT_EQ(platform_->GetKeyboard()->GetKeyFromScancode(Scancode::Y), KeyCode::Z);
}

TEST_F(WaylandProtocol, AutoRepeatFollowsTheCompositorsRateAndStopsOnRelease)
{
    CompositorOptions options;
    options.repeatRate = 50;
    options.repeatDelay = 100;
    Start(options);
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0);
    compositor_->Key(KEY_RIGHT, true);
    Settle();
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 400ms)
    {
        Pump();
        std::this_thread::sleep_for(5ms);
    }
    compositor_->Key(KEY_RIGHT, false);
    Settle();
    int repeats = 0;
    for (const KeyEvent& key : SeenOf<KeyEvent>())
    {
        repeats += key.repeat ? 1 : 0;
    }
    // 100 ms delay then 50 per second over ~300 ms: about 15, allowing for scheduling.
    EXPECT_GE(repeats, 8);
    EXPECT_LE(repeats, 20);
    seen_.clear();
    const auto after = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - after < 200ms)
    {
        Pump();
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(SeenOf<KeyEvent>().empty()) << "no repeat after the release";
}

TEST_F(WaylandProtocol, ARepeatRateOfZeroMeansNoRepeat)
{
    CompositorOptions options;
    options.repeatRate = 0;
    Start(options);
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0);
    compositor_->Key(KEY_SPACE, true);
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < 900ms)
    {
        Pump();
        std::this_thread::sleep_for(10ms);
    }
    compositor_->Key(KEY_SPACE, false);
    Settle();
    for (const KeyEvent& key : SeenOf<KeyEvent>())
    {
        EXPECT_FALSE(key.repeat);
    }
}

// --- pointer (WAYLAND-0055..0057) -----------------------------------------------------------------

TEST_F(WaylandProtocol, PointerMotionButtonsAndDoubleClicks)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 10.5, 20.25);
    compositor_->PointerMotion(100.75, 200.5);
    compositor_->PointerButton(BTN_LEFT, true);
    compositor_->PointerButton(BTN_LEFT, false);
    compositor_->PointerButton(BTN_LEFT, true);
    compositor_->PointerButton(BTN_LEFT, false);
    compositor_->PointerButton(BTN_EXTRA, true);
    Settle();
    const std::vector<MouseMotionEvent> motion = SeenOf<MouseMotionEvent>();
    ASSERT_FALSE(motion.empty());
    EXPECT_EQ(motion.back().window, window.GetId());
    EXPECT_FLOAT_EQ(motion.back().x, 100.75f);
    EXPECT_FLOAT_EQ(motion.back().y, 200.5f);
    const std::vector<MouseButtonEvent> buttons = SeenOf<MouseButtonEvent>();
    ASSERT_EQ(buttons.size(), 5u);
    EXPECT_EQ(buttons[0].button, 1);
    EXPECT_EQ(buttons[0].clicks, 1);
    EXPECT_EQ(buttons[2].clicks, 2) << "a second press within the interval and slop is a double click";
    EXPECT_EQ(buttons[4].button, 5);
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->Update();
    EXPECT_EQ(mouse->GetSnapshot().window, window.GetId());
    EXPECT_EQ(mouse->GetSnapshot().x, 100);
    EXPECT_NE(mouse->GetSnapshot().buttons & (1u << 4), 0) << "X2 held";
    compositor_->PointerButton(BTN_EXTRA, false);
    compositor_->PointerLeave();
    Settle();
}

TEST_F(WaylandProtocol, WheelNotchesHighResolutionSlicesAndTouchpadScrollAllCount)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 50, 50);
    compositor_->PointerAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, 15.0, 120, 0, WL_POINTER_AXIS_SOURCE_WHEEL);
    compositor_->PointerAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, 7.5, 60, 0, WL_POINTER_AXIS_SOURCE_WHEEL);
    compositor_->PointerAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, 7.5, 60, 0, WL_POINTER_AXIS_SOURCE_WHEEL);
    Settle();
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->Update();
    // Down three half... one notch and two half notches: two notches down, which is -240 in XNA's
    // direction (positive is away from the user).
    EXPECT_EQ(mouse->GetSnapshot().scrollY, -240);
    const std::vector<MouseWheelEvent> wheels = SeenOf<MouseWheelEvent>();
    ASSERT_EQ(wheels.size(), 3u);
    EXPECT_FLOAT_EQ(wheels[0].y, -1.0f);
    EXPECT_FLOAT_EQ(wheels[1].y, -0.5f);

    compositor_->PointerAxis(WL_POINTER_AXIS_HORIZONTAL_SCROLL, 5.0, 0, 0, WL_POINTER_AXIS_SOURCE_FINGER);
    Settle();
    mouse->Update();
    EXPECT_EQ(mouse->GetSnapshot().scrollX, 60) << "a touchpad's continuous scroll is half a notch per 5 units";
}

TEST_F(WaylandProtocol, OldSeatsSendDiscreteStepsInstead)
{
    CompositorOptions options;
    options.seatVersion = 7;
    Start(options);
    MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 50, 50);
    compositor_->PointerAxis(WL_POINTER_AXIS_VERTICAL_SCROLL, -10.0, 0, -1, WL_POINTER_AXIS_SOURCE_WHEEL);
    Settle();
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->Update();
    EXPECT_EQ(mouse->GetSnapshot().scrollY, 120);
}

TEST_F(WaylandProtocol, RelativeModeLocksReadsUnacceleratedMotionAndAlwaysUnlocks)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 400, 240);
    Settle();
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->SetRelativeMode(window.GetId(), true);
    Settle();
    PointerInfo info = compositor_->GetPointerInfo();
    EXPECT_EQ(info.locks, 1);
    EXPECT_EQ(info.lockLifetime, 2u) << "persistent: a lock that ends on the first unlock loses the game's mouse";
    EXPECT_EQ(info.relativePointers, 1);
    EXPECT_TRUE(compositor_->ActivateLock());
    Settle();

    compositor_->RelativeMotion(10.0, -4.0, 3.25, -1.5);
    compositor_->RelativeMotion(10.0, -4.0, 3.25, -1.5);
    Settle();
    const MouseDelta delta = mouse->ConsumeRelativeDelta();
    EXPECT_EQ(delta.x, 6) << "unaccelerated motion, fractions carried: 6.5 so far";
    EXPECT_EQ(delta.y, -3);
    compositor_->RelativeMotion(1.0, 1.0, 0.25, 0.0);
    Settle();
    EXPECT_EQ(mouse->ConsumeRelativeDelta().x, 0) << "the carried half and this quarter make 0.75: not yet a unit";
    compositor_->RelativeMotion(1.0, 1.0, 0.25, 0.0);
    Settle();
    EXPECT_EQ(mouse->ConsumeRelativeDelta().x, 1) << "and another quarter makes one";

    // A focus loss ends the lock on the compositor's side; the object persists and relocks.
    compositor_->DeactivateLock();
    Settle();
    EXPECT_TRUE(mouse->IsRelativeMode());
    EXPECT_EQ(compositor_->GetPointerInfo().locks, 1);

    mouse->SetRelativeMode(window.GetId(), false);
    Settle();
    info = compositor_->GetPointerInfo();
    EXPECT_EQ(info.locks, 0);
    EXPECT_FALSE(mouse->IsRelativeMode());
}

TEST_F(WaylandProtocol, DestroyingALockedWindowReleasesTheLockFirst)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 10, 10);
    platform_->GetMouse()->SetRelativeMode(window.GetId(), true);
    Settle();
    ASSERT_TRUE(compositor_->ActivateLock());
    Settle();
    windows_.clear();
    Settle();
    EXPECT_EQ(compositor_->GetPointerInfo().locks, 0);
    EXPECT_FALSE(platform_->GetMouse()->IsRelativeMode());
}

TEST_F(WaylandProtocol, SetPositionWhileLockedBecomesTheUnlockHint)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 10, 10);
    IPlatformMouse* mouse = platform_->GetMouse();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    mouse->SetRelativeMode(window.GetId(), true);
    Settle();
    // The hint is surface state: it takes effect with the game's next frame.
    mouse->SetPosition(window.GetId(), 320, 200);
    PresentColour(presenter, window, 0x202020);
    Settle();
    const PointerInfo info = compositor_->GetPointerInfo();
    EXPECT_TRUE(info.hasHint);
    EXPECT_DOUBLE_EQ(info.hintX, 320.0);
    EXPECT_DOUBLE_EQ(info.hintY, 200.0);
    mouse->SetRelativeMode(window.GetId(), false);
    Settle();
}

TEST_F(WaylandProtocol, TheGlobalPointerIsRefusedByName)
{
    Start();
    IPlatformMouse* mouse = platform_->GetMouse();
    float x = 0;
    float y = 0;
    try
    {
        (void) mouse->TryGetGlobalPosition(x, y);
        FAIL() << "a Wayland client has no global pointer";
    }
    catch (const PlatformNotSupportedException& refusal)
    {
        EXPECT_EQ(refusal.GetCapability(), PlatformCapability::GlobalPointer);
    }
    EXPECT_THROW(mouse->SetGlobalPosition(1, 1), PlatformNotSupportedException);
    EXPECT_THROW(mouse->SetCapture(true), PlatformNotSupportedException);
}

TEST_F(WaylandProtocol, SystemCursorsGoThroughCursorShapeWithTheEnterSerial)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 10, 10);
    Settle();
    IPlatformMouse* mouse = platform_->GetMouse();
    mouse->SetCursor(SystemCursor::Pointer);
    Settle();
    EXPECT_EQ(compositor_->GetPointerInfo().shape, 4u);  // WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER
    mouse->SetCursor(SystemCursor::IBeam);
    Settle();
    EXPECT_EQ(compositor_->GetPointerInfo().shape, 9u);  // ..._SHAPE_TEXT
    mouse->SetCursorVisible(false);
    Settle();
    PointerInfo info = compositor_->GetPointerInfo();
    EXPECT_TRUE(info.cursorSet);
    EXPECT_EQ(info.cursorSurface, 0u) << "hidden is set_cursor with no surface";
    mouse->SetCursorVisible(true);
    Settle();
    EXPECT_GE(compositor_->GetPointerInfo().shapeRequests, 3);
}

TEST_F(WaylandProtocol, ACustomCursorIsASurfaceWithItsHotSpot)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->PointerEnter(0, 10, 10);
    Settle();
    std::vector<std::uint32_t> pixels(16 * 16, 0xFF0000FFu);
    CursorImage image;
    image.width = 16;
    image.height = 16;
    image.hotSpotX = 3;
    image.hotSpotY = 5;
    image.rgba = pixels;
    platform_->GetMouse()->SetCursor(image);
    Settle();
    const PointerInfo info = compositor_->GetPointerInfo();
    EXPECT_NE(info.cursorSurface, 0u);
    EXPECT_EQ(info.hotspotX, 3);
    EXPECT_EQ(info.hotspotY, 5);
}

// --- touch (WAYLAND-0058) -------------------------------------------------------------------------

TEST_F(WaylandProtocol, TouchesAreNormalisedAndCancelled)
{
    CompositorOptions options;
    options.touch = true;
    Start(options);
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->TouchDown(0, 7, 400, 240);
    compositor_->TouchFrame();
    compositor_->TouchMotion(7, 600, 240);
    compositor_->TouchFrame();
    compositor_->TouchCancel();
    Settle();
    const std::vector<TouchEvent> touches = SeenOf<TouchEvent>();
    ASSERT_GE(touches.size(), 3u);
    EXPECT_EQ(touches[0].kind, TouchEventKind::Down);
    EXPECT_EQ(touches[0].window, window.GetId());
    EXPECT_FLOAT_EQ(touches[0].x, 0.5f);
    EXPECT_FLOAT_EQ(touches[0].y, 0.5f);
    EXPECT_EQ(touches[1].kind, TouchEventKind::Motion);
    EXPECT_FLOAT_EQ(touches[1].x, 0.75f);
    EXPECT_EQ(touches.back().kind, TouchEventKind::Cancelled);
}

// --- clipboard, primary selection, drag and drop (WAYLAND-0070..0073) ------------------------------

std::vector<std::uint8_t> Bytes(const std::string& text)
{
    return {text.begin(), text.end()};
}

TEST_F(WaylandProtocol, AnotherProgramsClipboardIsReadWhenFocused)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->OfferSelection({{"text/plain;charset=utf-8", Bytes("P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd")}});
    compositor_->KeyboardEnter(0);
    Settle();
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    EXPECT_TRUE(clipboard->HasText());
    EXPECT_EQ(clipboard->GetText(), "P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd");
}

TEST_F(WaylandProtocol, ALargeClipboardArrivesWhole)
{
    Start();
    MakeWindow();
    Settle();
    std::string big(24u << 20, 'x');
    for (std::size_t index = 0; index < big.size(); index += 4096)
    {
        big[index] = static_cast<char>('a' + (index / 4096) % 26);
    }
    compositor_->OfferSelection({{"text/plain;charset=utf-8", Bytes(big)}});
    compositor_->KeyboardEnter(0);
    Settle();
    const std::string text = platform_->GetClipboard()->GetText();
    EXPECT_EQ(text.size(), big.size());
    EXPECT_TRUE(text == big);
}

TEST_F(WaylandProtocol, OurClipboardIsServedToOtherProgramsWhileWeKeepRunning)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0);
    Settle();
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    const std::string payload(3u << 20, 'q');
    clipboard->SetText(payload);
    Settle();
    const ClientSelectionInfo info = compositor_->GetClientSelection();
    ASSERT_TRUE(info.owned);
    EXPECT_NE(std::find(info.mimeTypes.begin(), info.mimeTypes.end(), "text/plain;charset=utf-8"), info.mimeTypes.end());
    EXPECT_NE(std::find(info.mimeTypes.begin(), info.mimeTypes.end(), "UTF8_STRING"), info.mimeTypes.end());
    // Reading it back is served from memory, not through the compositor.
    EXPECT_EQ(clipboard->GetText(), payload);

    const int fd = compositor_->RequestClientSelection("text/plain;charset=utf-8");
    ASSERT_GE(fd, 0);
    std::string received;
    char chunk[65536];
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        Pump();  // the game's own loop is what writes the transfer
        pollfd descriptor{fd, POLLIN, 0};
        if (::poll(&descriptor, 1, 5) <= 0)
        {
            continue;
        }
        const ssize_t got = ::read(fd, chunk, sizeof(chunk));
        if (got <= 0)
        {
            break;
        }
        received.append(chunk, static_cast<std::size_t>(got));
    }
    ::close(fd);
    EXPECT_EQ(received.size(), payload.size());
}

TEST_F(WaylandProtocol, APasteTargetThatGivesUpDoesNotHurtUs)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0);
    Settle();
    platform_->GetClipboard()->SetText(std::string(8u << 20, 'z'));
    Settle();
    const int fd = compositor_->RequestClientSelection("text/plain;charset=utf-8");
    ASSERT_GE(fd, 0);
    char chunk[4096];
    Pump();
    (void) ::read(fd, chunk, sizeof(chunk));
    ::close(fd);  // the reader hangs up mid-transfer: EPIPE for us, never SIGPIPE
    for (int round = 0; round < 20; ++round)
    {
        Pump();
    }
    EXPECT_TRUE(platform_->GetConnectionForTesting()->IsAlive());
}

TEST_F(WaylandProtocol, LosingTheSelectionForgetsWhatWeOffered)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->KeyboardEnter(0);
    Settle();
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    clipboard->SetText("mine");
    Settle();
    compositor_->OfferSelection({{"text/plain", Bytes("theirs")}});
    Settle();
    EXPECT_EQ(clipboard->GetText(), "theirs");
    compositor_->ClearSelection();
    Settle();
    EXPECT_FALSE(clipboard->HasText());
}

TEST_F(WaylandProtocol, BinaryClipboardDataRoundTripsUnderItsOwnMimeType)
{
    Start();
    MakeWindow();
    Settle();
    std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0xFF};
    compositor_->OfferSelection({{"image/png", png}, {"text/plain", Bytes("an image")}});
    compositor_->KeyboardEnter(0);
    Settle();
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    const std::vector<std::string> types = clipboard->GetMimeTypes();
    EXPECT_NE(std::find(types.begin(), types.end(), "image/png"), types.end());
    EXPECT_TRUE(clipboard->HasData("image/png"));
    EXPECT_EQ(clipboard->GetData("image/png"), png);
}

TEST_F(WaylandProtocol, ThePrimarySelectionIsASeparateSelection)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->OfferPrimarySelection({{"text/plain;charset=utf-8", Bytes("highlighted")}});
    compositor_->OfferSelection({{"text/plain;charset=utf-8", Bytes("copied")}});
    compositor_->KeyboardEnter(0);
    Settle();
    EXPECT_EQ(platform_->GetPrimarySelection()->GetText(), "highlighted");
    EXPECT_EQ(platform_->GetClipboard()->GetText(), "copied");
    platform_->GetPrimarySelection()->SetText("selected here");
    Settle();
    EXPECT_TRUE(compositor_->GetClientPrimarySelection().owned);
    EXPECT_FALSE(compositor_->GetClientSelection().owned);
}

TEST_F(WaylandProtocol, DroppedFilesArriveAsLocalPathsAndTheDropIsFinished)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    compositor_->DragEnter(0, 100, 120, {{"text/uri-list", Bytes("file:///tmp/a%20b.txt\r\nfile:///home/x/%C5%99.png\r\n")}});
    compositor_->DragMotion(150, 160);
    Settle();
    DragInfo info = compositor_->GetDragInfo();
    EXPECT_TRUE(info.acceptCalled);
    EXPECT_EQ(info.accepted, "text/uri-list");
    EXPECT_EQ(info.preferred, 1u) << "copy: CNA never takes a dragged file away from its owner";
    compositor_->Drop();
    ASSERT_TRUE(PumpUntil([&] {
        for (const DropEvent& drop : SeenOf<DropEvent>())
        {
            if (drop.kind == DropEventKind::Complete)
            {
                return true;
            }
        }
        return false;
    }));
    std::vector<std::string> files;
    for (const DropEvent& drop : SeenOf<DropEvent>())
    {
        EXPECT_EQ(drop.window, window.GetId());
        if (drop.kind == DropEventKind::File)
        {
            files.push_back(drop.data);
        }
    }
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0], "/tmp/a b.txt");
    EXPECT_EQ(files[1], "/home/x/\xc5\x99.png");
    Settle();
    info = compositor_->GetDragInfo();
    EXPECT_TRUE(info.finished);
}

TEST_F(WaylandProtocol, ADragThatLeavesDropsNothing)
{
    Start();
    MakeWindow();
    Settle();
    compositor_->DragEnter(0, 10, 10, {{"text/plain;charset=utf-8", Bytes("dragged")}});
    Settle();
    compositor_->DragLeave();
    Settle();
    // Begin, then Complete alone: the contract's sequence for a drag that left.
    const std::vector<DropEvent> drops = SeenOf<DropEvent>();
    ASSERT_FALSE(drops.empty());
    EXPECT_EQ(drops.front().kind, DropEventKind::Begin);
    EXPECT_EQ(drops.back().kind, DropEventKind::Complete);
    for (const DropEvent& drop : drops)
    {
        EXPECT_NE(drop.kind, DropEventKind::Text);
        EXPECT_NE(drop.kind, DropEventKind::File);
    }
    EXPECT_TRUE(compositor_->GetDragInfo().received.empty()) << "nothing is read from a drag that was not dropped";
}

// --- text input (WAYLAND-0054) --------------------------------------------------------------------

TEST_F(WaylandProtocol, AnInputMethodComposesAndCommitsThroughTextInputV3)
{
    Start();
    IPlatformWindow& window = MakeWindow();
    Settle();
    IPlatformTextInput* input = platform_->GetTextInput();
    input->Start(window.GetId(), TextInputType::TextEmail);
    TextInputArea area;
    area.x = 10;
    area.y = 20;
    area.width = 200;
    area.height = 24;
    area.cursorOffset = 0;
    input->SetInputArea(window.GetId(), area);
    compositor_->KeyboardEnter(0);
    compositor_->TextInputEnter(0);
    Settle();
    TextInputInfo info = compositor_->GetTextInputInfo();
    EXPECT_TRUE(info.enabled);
    EXPECT_EQ(info.purpose, 6u);  // ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL
    // The caret, not the whole field: where an input method puts its candidate window.
    EXPECT_EQ(info.cursorX, 10);
    EXPECT_EQ(info.cursorY, 20);
    EXPECT_EQ(info.cursorWidth, 1);
    EXPECT_EQ(info.cursorHeight, 24);

    compositor_->TextInputPreedit("\xe3\x81\x8b", 3, 3);  // か
    compositor_->TextInputDone();
    Settle();
    const std::vector<TextEditingEvent> editing = SeenOf<TextEditingEvent>();
    ASSERT_FALSE(editing.empty());
    EXPECT_EQ(editing.back().text, "\xe3\x81\x8b");
    EXPECT_EQ(editing.back().cursor, 1) << "three bytes are one character";

    compositor_->TextInputPreedit("", 0, 0);
    compositor_->TextInputCommit("\xe6\xbc\xa2");  // 漢
    compositor_->TextInputDone();
    Settle();
    const std::vector<TextInputEvent> committed = SeenOf<TextInputEvent>();
    ASSERT_FALSE(committed.empty());
    EXPECT_EQ(committed.back().text, "\xe6\xbc\xa2");

    input->Stop(window.GetId());
    Settle();
    EXPECT_FALSE(compositor_->GetTextInputInfo().enabled);
}

// --- desktop integration (WAYLAND-0092..0094) -----------------------------------------------------

TEST_F(WaylandProtocol, DisablingTheScreenSaverInhibitsIdleOnEveryWindow)
{
    Start();
    MakeWindow();
    MakeWindow();
    Settle();
    IPlatformDisplays* displays = platform_->GetDisplays();
    displays->SetScreenSaverEnabled(false);
    Settle();
    EXPECT_EQ(compositor_->GetIdleInhibitorCount(), 2);
    EXPECT_FALSE(displays->IsScreenSaverEnabled());
    MakeWindow();
    Settle();
    EXPECT_EQ(compositor_->GetIdleInhibitorCount(), 3) << "a window made while inhibiting inhibits too";
    displays->SetScreenSaverEnabled(true);
    Settle();
    EXPECT_EQ(compositor_->GetIdleInhibitorCount(), 0);
}

TEST_F(WaylandProtocol, TheLaunchersActivationTokenIsSpentOnTheFirstWindowOnly)
{
    ::setenv("XDG_ACTIVATION_TOKEN", "from-the-launcher", 1);
    Start();
    MakeWindow();
    Settle();
    EXPECT_EQ(std::getenv("XDG_ACTIVATION_TOKEN"), nullptr) << "a child process must not spend it again";
    std::vector<std::string> activations = compositor_->GetActivations();
    ASSERT_EQ(activations.size(), 1u);
    EXPECT_EQ(activations.front(), "from-the-launcher");

    // A later window, opened in answer to a click, asks for a token with that click's serial.
    compositor_->PointerEnter(0, 5, 5);
    compositor_->PointerButton(BTN_LEFT, true);
    compositor_->PointerButton(BTN_LEFT, false);
    Settle();
    MakeWindow();
    Settle();
    activations = compositor_->GetActivations();
    ASSERT_EQ(activations.size(), 2u);
    EXPECT_EQ(compositor_->GetActivationTokenCount(), 1);
}

// --- the connection ends (WAYLAND-0030) -----------------------------------------------------------

TEST_F(WaylandProtocol, ACompositorThatGoesAwayEndsTheApplicationOnceAndSafely)
{
    expectDisconnect_ = true;
    Start();
    IPlatformWindow& window = MakeWindow();
    IPlatformSurfacePresenter& presenter = PresenterFor(window);
    PresentColour(presenter, window, 0x00FF00);
    Settle();
    compositor_->DisconnectClient();
    ASSERT_TRUE(PumpUntil([&] { return SawQuit(); }));
    EXPECT_EQ(SeenOf<QuitEvent>().size(), 1u);
    // Everything after the loss refuses or does nothing -- nothing crashes.
    PresentColour(presenter, window, 0x00FF00);
    window.SetTitle("still here");
    window.Maximize();
    window.Sync();
    Pump();
    EXPECT_EQ(SeenOf<QuitEvent>().size(), 1u) << "told once";
    EXPECT_THROW((void) platform_->CreateWindow(WindowDescription{}), PlatformException);
    presenters_.clear();
    windows_.clear();
}

} // namespace

#endif
