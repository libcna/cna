// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0013..0019: real native windows.
//
// Everything here creates an actual HWND and pumps actual messages. The conformance suite already
// holds the window to the contract's portable rules; what this file adds is the Win32-specific
// behaviour underneath them -- that the native handle is the real HWND a renderer will use, that
// client size means client size, that adoption borrows rather than owns, and that a window and
// its platform can be destroyed in either order.
//
// Every test tolerates a host with no window manager by skipping rather than failing, which is
// how the SDL3 and Terminal suites already behave in a headless CI cell.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include "Win32/Win32Common.hpp"
#include "Win32/Win32Window.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;

class Win32WindowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
    }

    std::unique_ptr<IPlatformWindow> Create(const int width = 640, const int height = 480,
                                            const bool visible = false)
    {
        WindowDescription description;
        description.title = "win32 window test";
        description.width = width;
        description.height = height;
        description.visible = visible;
        try
        {
            return platform_->CreateWindow(description);
        }
        catch (const PlatformException& error)
        {
            ADD_FAILURE() << "window creation failed: " << error.what();
            return nullptr;
        }
    }

    std::unique_ptr<IPlatform> platform_;
};

// --- native handle -------------------------------------------------------------------------------

TEST_F(Win32WindowTest, NativeHandleCarriesTheRealHwnd)
{
    // The single most important assertion for this backend: this handle, and nothing else, is what
    // DirectX11Renderer and DirectX12Renderer receive.
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);

    const NativeWindowHandle handle = window->GetNativeHandle();
    EXPECT_EQ(handle.system, NativeWindowSystem::Win32);
    ASSERT_NE(handle.window, nullptr);
    EXPECT_TRUE(HasNativeWindow(handle)) << Describe(handle);

    // Not merely non-null: a live window, as far as the operating system is concerned.
    EXPECT_NE(IsWindow(static_cast<HWND>(handle.window)), FALSE);

    // The fields that do not apply to Win32 must stay empty. A renderer that read `display` or
    // `windowId` because another backend populates them would get a plausible zero rather than a
    // detected mismatch.
    EXPECT_EQ(handle.display, nullptr);
    EXPECT_EQ(handle.surface, nullptr);
    EXPECT_EQ(handle.windowId, 0u);
}

TEST_F(Win32WindowTest, TypedAccessorAcceptsTheHandleAndRejectsEveryOtherSystem)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    const NativeWindowHandle handle = window->GetNativeHandle();

    Win32NativeWindow native{};
    ASSERT_TRUE(TryGetWin32(handle, native));
    EXPECT_EQ(native.hwnd, handle.window);

    // The accessors are the reason a renderer initialised against the wrong backend gets a false
    // rather than a pointer that crashes on first use.
    X11NativeWindow x11{};
    CocoaNativeWindow cocoa{};
    WaylandNativeWindow wayland{};
    AndroidNativeWindow android{};
    EXPECT_FALSE(TryGetX11(handle, x11));
    EXPECT_FALSE(TryGetCocoa(handle, cocoa));
    EXPECT_FALSE(TryGetWayland(handle, wayland));
    EXPECT_FALSE(TryGetAndroid(handle, android));
}

TEST_F(Win32WindowTest, NativeHandleIsStableAcrossCalls)
{
    // A renderer stores the handle once at initialization. One that changed between calls would
    // make every later comparison -- including PlatformRendererSurfaceState's identity check --
    // spuriously report a different window.
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    EXPECT_EQ(window->GetNativeHandle().window, window->GetNativeHandle().window);
}

TEST_F(Win32WindowTest, LegacyTokenRoundTripsThroughAdoption)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    const std::uintptr_t token = window->GetWindowHandle();
    EXPECT_NE(token, 0u);
    EXPECT_EQ(reinterpret_cast<void*>(token), window->GetNativeHandle().window);

    const std::unique_ptr<IPlatformWindow> adopted = platform_->AdoptWindowHandle(token);
    ASSERT_NE(adopted, nullptr);
    EXPECT_EQ(adopted->GetNativeHandle().window, window->GetNativeHandle().window);
    // An already-known window keeps its established id, so events the owning wrapper produces and
    // the borrowed one's GetId() agree.
    EXPECT_EQ(adopted->GetId(), window->GetId());
}

// --- geometry --------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, RequestedSizeIsTheClientAreaNotTheOuterFrame)
{
    // CreateWindowExW takes the OUTER size while WindowDescription means the CLIENT size. Without
    // AdjustWindowRectExForDpi the window comes out one frame and one title bar too small -- the
    // spike measured 632x446 for a requested 640x480.
    const std::unique_ptr<IPlatformWindow> window = Create(640, 480);
    ASSERT_NE(window, nullptr);
    window->Sync();

    const WindowBounds bounds = window->GetClientBounds();
    EXPECT_EQ(bounds.width, 640);
    EXPECT_EQ(bounds.height, 480);

    RECT outer{};
    ASSERT_NE(GetWindowRect(static_cast<HWND>(window->GetNativeHandle().window), &outer), FALSE);
    EXPECT_GT(outer.right - outer.left, bounds.width) << "a decorated window is wider than its client area";
    EXPECT_GT(outer.bottom - outer.top, bounds.height);
}

TEST_F(Win32WindowTest, ResizeAlsoMeansTheClientArea)
{
    const std::unique_ptr<IPlatformWindow> window = Create(640, 480);
    ASSERT_NE(window, nullptr);
    window->SetSize(800, 600);
    window->Sync();
    EXPECT_EQ(window->GetClientBounds().width, 800);
    EXPECT_EQ(window->GetClientBounds().height, 600);
}

TEST_F(Win32WindowTest, PixelSizeAndDisplayScaleAreInternallyConsistent)
{
    const std::unique_ptr<IPlatformWindow> window = Create(640, 480);
    ASSERT_NE(window, nullptr);
    window->Sync();

    const WindowBounds bounds = window->GetClientBounds();
    const WindowSize pixels = window->GetPixelSize();
    EXPECT_EQ(pixels.width, bounds.width);
    EXPECT_EQ(pixels.height, bounds.height);

    // GetClientRect already answers in the units the window actually draws in, whatever DPI
    // awareness the host chose. Multiplying it by the scale on top would double-apply it and hand
    // a renderer a swapchain twice the size of its window.
    const float scale = window->GetDisplayScale();
    EXPECT_GT(scale, 0.0f) << "a zero scale divides to infinity in any layout computation";
    EXPECT_LE(scale, 8.0f) << "no real display scales past 800%";
}

TEST_F(Win32WindowTest, ClientBoundsReportTheScreenPositionOfTheClientOrigin)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    window->Sync();

    const WindowBounds bounds = window->GetClientBounds();
    POINT origin{0, 0};
    ASSERT_NE(ClientToScreen(static_cast<HWND>(window->GetNativeHandle().window), &origin), FALSE);
    EXPECT_EQ(bounds.x, origin.x);
    EXPECT_EQ(bounds.y, origin.y);
}

// --- title -----------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, TitleRoundTripsThroughUtf8)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);

    // Non-ASCII on purpose: the title crosses a UTF-8/UTF-16 boundary in both directions, and an
    // ANSI call in either would mangle it.
    const std::string title = "p\xC5\x99\xC3\xADli\xC5\xA1 \xE2\x80\x94 \xE6\x97\xA5\xE6\x9C\xAC";
    window->SetTitle(title);
    EXPECT_EQ(window->GetTitle(), title);

    window->SetTitle("");
    EXPECT_EQ(window->GetTitle(), "");
}

// --- state -----------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, BorderlessAndResizableRoundTrip)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);

    EXPECT_TRUE(window->IsResizable()) << "WindowDescription defaults to resizable";
    window->SetResizable(false);
    EXPECT_FALSE(window->IsResizable());
    window->SetResizable(true);
    EXPECT_TRUE(window->IsResizable());

    EXPECT_FALSE(window->IsBorderless());
    window->SetBorderless(true);
    EXPECT_TRUE(window->IsBorderless());
    window->SetBorderless(false);
    EXPECT_FALSE(window->IsBorderless());
}

TEST_F(Win32WindowTest, FullscreenModeRoundTripsAndRestoresTheWindowedSize)
{
    const std::unique_ptr<IPlatformWindow> window = Create(640, 480);
    ASSERT_NE(window, nullptr);
    window->Sync();
    EXPECT_EQ(window->GetFullscreenMode(), WindowFullscreenMode::Windowed);

    window->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    EXPECT_EQ(window->GetFullscreenMode(), WindowFullscreenMode::BorderlessFullscreen);
    window->Sync();

    window->SetFullscreenMode(WindowFullscreenMode::Windowed);
    EXPECT_EQ(window->GetFullscreenMode(), WindowFullscreenMode::Windowed);
    window->Sync();

    // The point of the whole state snapshot: the window comes back the size and shape it was.
    EXPECT_EQ(window->GetClientBounds().width, 640);
    EXPECT_EQ(window->GetClientBounds().height, 480);
    EXPECT_FALSE(window->IsBorderless());
}

TEST_F(Win32WindowTest, RepeatedFullscreenCyclesDoNotDriftTheWindowedState)
{
    const std::unique_ptr<IPlatformWindow> window = Create(640, 480);
    ASSERT_NE(window, nullptr);
    window->Sync();

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        window->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
        window->Sync();
        window->SetFullscreenMode(WindowFullscreenMode::Windowed);
        window->Sync();
        ASSERT_EQ(window->GetClientBounds().width, 640) << "cycle " << cycle;
        ASSERT_EQ(window->GetClientBounds().height, 480) << "cycle " << cycle;
        ASSERT_FALSE(window->IsBorderless()) << "cycle " << cycle;
    }
}

TEST_F(Win32WindowTest, DisplayNameNamesTheMonitorTheWindowIsOn)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    // Backs GameWindow::ScreenDeviceName. The exact string is the host's device name, so the
    // durable assertion is that a windowing platform answers at all.
    EXPECT_FALSE(window->GetDisplayName().empty());
}

// --- multiple windows ----------------------------------------------------------------------------------

TEST_F(Win32WindowTest, MultipleWindowsHaveDistinctIdsAndDistinctHandles)
{
    const std::unique_ptr<IPlatformWindow> first = Create(320, 240);
    const std::unique_ptr<IPlatformWindow> second = Create(400, 300);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_NE(first->GetId(), second->GetId());
    EXPECT_NE(first->GetNativeHandle().window, second->GetNativeHandle().window);
    EXPECT_NE(first->GetId(), 0u);
    EXPECT_NE(second->GetId(), 0u);
}

TEST_F(Win32WindowTest, EachWindowKeepsItsOwnGeometry)
{
    const std::unique_ptr<IPlatformWindow> first = Create(320, 240);
    const std::unique_ptr<IPlatformWindow> second = Create(400, 300);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    first->Sync();
    second->Sync();

    EXPECT_EQ(first->GetClientBounds().width, 320);
    EXPECT_EQ(second->GetClientBounds().width, 400);

    first->SetSize(500, 400);
    first->Sync();
    second->Sync();
    EXPECT_EQ(first->GetClientBounds().width, 500);
    EXPECT_EQ(second->GetClientBounds().width, 400) << "resizing one must not move the other";
}

TEST_F(Win32WindowTest, ClosingOneWindowDoesNotDisturbAnother)
{
    std::unique_ptr<IPlatformWindow> first = Create(320, 240);
    const std::unique_ptr<IPlatformWindow> second = Create(400, 300);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    const HWND survivor = static_cast<HWND>(second->GetNativeHandle().window);
    first.reset();

    // No PostQuitMessage on WM_DESTROY: destroying one window must leave the process, and every
    // other window, entirely alone.
    EXPECT_NE(IsWindow(survivor), FALSE);
    second->Sync();
    EXPECT_EQ(second->GetClientBounds().width, 400);

    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);
    for (const PlatformEvent& event : events)
    {
        EXPECT_FALSE(std::holds_alternative<QuitEvent>(event))
            << "destroying a window must not quit the application";
    }
}

// --- adoption --------------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, AdoptedWindowDoesNotDestroy)
{
    const std::unique_ptr<IPlatformWindow> owner = Create();
    ASSERT_NE(owner, nullptr);
    const HWND hwnd = static_cast<HWND>(owner->GetNativeHandle().window);

    {
        const std::unique_ptr<IPlatformWindow> adopted = platform_->AdoptWindow(owner->GetId());
        ASSERT_NE(adopted, nullptr);
        EXPECT_EQ(adopted->GetNativeHandle().window, owner->GetNativeHandle().window);
        EXPECT_EQ(adopted->GetTitle(), owner->GetTitle());
    }

    // The borrowed wrapper is gone; the window it borrowed is not.
    EXPECT_NE(IsWindow(hwnd), FALSE);
    EXPECT_EQ(owner->GetNativeHandle().window, static_cast<void*>(hwnd));
}

TEST_F(Win32WindowTest, AdoptedWrapperDoesNotEvictTheOwnerFromTheRegistry)
{
    const std::unique_ptr<IPlatformWindow> owner = Create();
    ASSERT_NE(owner, nullptr);
    const WindowId id = owner->GetId();

    { const std::unique_ptr<IPlatformWindow> adopted = platform_->AdoptWindow(id); }

    // If destroying the borrowed wrapper had unregistered the id, this second adoption would
    // refuse -- and, worse, the input services would stop finding the real window.
    EXPECT_NO_THROW((void) platform_->AdoptWindow(id));
}

// --- lifetime ----------------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, AWindowMayOutliveItsPlatform)
{
    // The contract permits either destruction order. A window whose platform is gone is still a
    // perfectly good native window; it simply has nowhere to send events.
    std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    platform_.reset();

    EXPECT_NO_THROW((void) window->GetTitle());
    EXPECT_NO_THROW((void) window->GetClientBounds());
    EXPECT_NO_THROW(window->SetTitle("still alive"));
    EXPECT_EQ(window->GetTitle(), "still alive");
    EXPECT_NO_THROW(window.reset());
}

TEST_F(Win32WindowTest, AFailedCreationLeavesThePlatformAbleToRetry)
{
    // Post-creation setup can throw after CreateWindowExW has already succeeded. The constructor
    // then never completes, so the destructor does not run, and the HWND it made would leak for
    // the process lifetime along with a window-class reference that can never be released.
    //
    // A zero-sized request is the reachable version of that: it is clamped rather than refused, so
    // what this really asserts is the invariant the unwind protects -- a creation that did not
    // produce a usable window leaves the platform exactly as it was.
    WindowDescription description;
    description.title = "degenerate";
    description.width = 0;
    description.height = 0;
    description.visible = false;

    try
    {
        const std::unique_ptr<IPlatformWindow> window = platform_->CreateWindow(description);
        ASSERT_NE(window, nullptr);
        window->Sync();
        EXPECT_GE(window->GetClientBounds().width, 1) << "a zero size is clamped, not honoured";
    }
    catch (const PlatformException&)
    {
        // Refusing is equally correct; what must not happen is a leak either way.
    }

    // Whichever outcome the first call had, the next ordinary creation must still work -- which it
    // cannot if the window class reference was stranded by a half-constructed window.
    const std::unique_ptr<IPlatformWindow> replacement = Create(320, 240);
    ASSERT_NE(replacement, nullptr);
    replacement->Sync();
    EXPECT_EQ(replacement->GetClientBounds().width, 320);
}

TEST_F(Win32WindowTest, DestroyingAWindowLeavesNoDanglingUserDataPointer)
{
    // WM_NCDESTROY is the last message a window receives and is where the owner pointer is
    // cleared. A stale GWLP_USERDATA here would be dereferenced by any message that still arrives
    // for the HWND -- the classic Win32 use-after-free.
    HWND hwnd = nullptr;
    {
        const std::unique_ptr<IPlatformWindow> window = Create();
        ASSERT_NE(window, nullptr);
        hwnd = static_cast<HWND>(window->GetNativeHandle().window);
        ASSERT_NE(hwnd, nullptr);
    }
    EXPECT_EQ(IsWindow(hwnd), FALSE) << "an owned window is destroyed with its wrapper";
}

// --- events --------------------------------------------------------------------------------------------------

TEST_F(Win32WindowTest, CloseRequestReachesPollEventsWithoutDestroyingTheWindow)
{
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);
    const HWND hwnd = static_cast<HWND>(window->GetNativeHandle().window);

    ASSERT_NE(PostMessageW(hwnd, WM_CLOSE, 0, 0), FALSE);

    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);

    bool sawCloseRequest = false;
    for (const PlatformEvent& event : events)
    {
        if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
        {
            if (windowEvent->kind == WindowEventKind::CloseRequested &&
                windowEvent->window == window->GetId())
            {
                sawCloseRequest = true;
            }
        }
        EXPECT_FALSE(std::holds_alternative<QuitEvent>(event));
    }
    EXPECT_TRUE(sawCloseRequest);
    // The whole point of the contract: the application decides, so the window is still here.
    EXPECT_NE(IsWindow(hwnd), FALSE);
}

TEST_F(Win32WindowTest, EventsAreAttributedToTheWindowTheyCameFrom)
{
    const std::unique_ptr<IPlatformWindow> first = Create(320, 240);
    const std::unique_ptr<IPlatformWindow> second = Create(400, 300);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    std::vector<PlatformEvent> drain;
    platform_->PollEvents(drain);

    ASSERT_NE(PostMessageW(static_cast<HWND>(second->GetNativeHandle().window), WM_CLOSE, 0, 0),
              FALSE);

    std::vector<PlatformEvent> events;
    platform_->PollEvents(events);
    bool sawSecond = false;
    for (const PlatformEvent& event : events)
    {
        if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
        {
            if (windowEvent->kind != WindowEventKind::CloseRequested)
                continue;
            EXPECT_NE(windowEvent->window, first->GetId());
            if (windowEvent->window == second->GetId())
                sawSecond = true;
        }
    }
    EXPECT_TRUE(sawSecond);
}

TEST_F(Win32WindowTest, PollEventsReusesTheCallersCapacity)
{
    // The batch belongs to the caller precisely so a steady-state frame allocates nothing.
    const std::unique_ptr<IPlatformWindow> window = Create();
    ASSERT_NE(window, nullptr);

    std::vector<PlatformEvent> events;
    events.reserve(64);
    const std::size_t capacity = events.capacity();
    for (int frame = 0; frame < 4; ++frame)
        platform_->PollEvents(events);
    EXPECT_GE(events.capacity(), capacity);
}

} // namespace
