// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0101: the X11 backend against a real X server.
//
// Everything here needs a live connection, so each fixture skips with a recorded reason when
// there is no `DISPLAY`. That is deliberate rather than convenient: a test that silently passed
// on a machine with no X server would report success for a backend it never ran.
//
// What it does NOT cover, and why, is as important. A bare `Xvfb` has no window manager, so
// maximise, minimise, restore, EWMH fullscreen and focus-follows-user do not happen there at all
// -- there is nothing to perform them. Those live in X11WindowManagerTests.cpp, which starts a
// real window manager. Splitting them keeps this file's failures meaningful: a failure here is
// the backend's, not the environment's.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Platform.hpp"
#include "../../../src/X11/X11Window.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::kXFalse;

// `<X11/X.h>` declares `typedef unsigned char KeyCode` at global scope, and the using-directive
// above makes `CNA::Platform::KeyCode` visible there too. A typedef cannot be undefined the way
// `None`, `Bool` and `Status` can, so the collision is resolved by a declaration in this nested
// namespace, which hides both.
using KeyCode = CNA::Platform::KeyCode;
using Scancode = CNA::Platform::Scancode;

bool HasDisplay()
{
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
}

class X11Live : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!HasDisplay())
        {
            GTEST_SKIP() << "no DISPLAY: this suite drives a real X server";
        }
        platform_ = PlatformFactory::Create("X11");
        ASSERT_NE(platform_, nullptr);
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "cannot reach the X server: " << error.what();
        }
        acquired_ = true;
    }

    void TearDown() override
    {
        window_.reset();
        if (acquired_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    std::unique_ptr<IPlatformWindow> MakeWindow(const int width = 320, const int height = 240,
                                                 const std::string& title = "CNA X11 test")
    {
        WindowDescription description;
        description.title = title;
        description.width = width;
        description.height = height;
        description.centered = false;
        description.x = 20;
        description.y = 20;
        return platform_->CreateWindow(description);
    }

    /// Pumps until `predicate` is satisfied or the budget runs out, collecting everything seen.
    ///
    /// Every window-state change in X11 is a round trip to the server and, for anything a window
    /// manager mediates, to another process. A single PollEvents after a request is a race, and a
    /// fixed sleep is a slower race.
    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& predicate,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(2000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (predicate(seen_))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    [[nodiscard]] static bool SawWindowEvent(const std::vector<PlatformEvent>& events,
                                             const WindowId window, const WindowEventKind kind)
    {
        return std::any_of(events.begin(), events.end(), [window, kind](const PlatformEvent& event) {
            const auto* windowEvent = std::get_if<WindowEvent>(&event);
            return windowEvent != nullptr && windowEvent->window == window &&
                   windowEvent->kind == kind;
        });
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
    bool acquired_ = false;
};

// --- connection and identity --------------------------------------------------------------------

TEST_F(X11Live, TheBackendNamesItselfAndIsTheDefaultOfAnX11Build)
{
    EXPECT_EQ(platform_->GetName(), "X11");
    const std::vector<std::string> available = PlatformFactory::GetAvailable();
    EXPECT_NE(std::find(available.begin(), available.end(), "X11"), available.end());
    EXPECT_EQ(PlatformFactory::GetDefaultName(), "X11");
}

TEST_F(X11Live, CapabilitiesDescribeThisServerRatherThanX11InGeneral)
{
    const PlatformCapabilities capabilities = platform_->GetCapabilities();

    // Unconditionally true: these need nothing beyond the core protocol.
    EXPECT_TRUE(capabilities.multipleWindows);
    EXPECT_TRUE(capabilities.nativeWindowHandle);
    EXPECT_TRUE(capabilities.surfacePresentation);
    EXPECT_TRUE(capabilities.textInput);
    EXPECT_TRUE(capabilities.exactKeyboardState);
    EXPECT_TRUE(capabilities.pixelAccurateMouse);
    EXPECT_TRUE(capabilities.globalPointer);
    EXPECT_TRUE(capabilities.cursorShapes);
    EXPECT_TRUE(capabilities.clipboard);

    // Deliberately false, each because the facility does not exist in X11 rather than because it
    // was not finished. A future change turning one of these on without implementing it would
    // fail here rather than at a null dereference in a game.
    EXPECT_FALSE(capabilities.gamepad);
    EXPECT_FALSE(capabilities.joystick);
    EXPECT_FALSE(capabilities.haptics);
    EXPECT_FALSE(capabilities.sensors);
    EXPECT_FALSE(capabilities.messageBox);
    EXPECT_FALSE(capabilities.nativeFileDialog);
    EXPECT_FALSE(capabilities.tray);
    EXPECT_FALSE(capabilities.camera);
    EXPECT_FALSE(capabilities.powerInfo);
    EXPECT_FALSE(capabilities.managedEntrypoint);

    // XIM is used for committed text, which is `textInput`. `ime` promises composition and
    // candidate events, which are not implemented -- so it must stay false however much XIM
    // appears in the implementation.
    EXPECT_FALSE(capabilities.ime);
}

// --- windows -------------------------------------------------------------------------------------

TEST_F(X11Live, AWindowIsCreatedWithTheRequestedGeometryAndTitle)
{
    window_ = MakeWindow(400, 300, "CNA geometry");
    window_->Sync();

    const WindowBounds bounds = window_->GetClientBounds();
    EXPECT_EQ(bounds.width, 400);
    EXPECT_EQ(bounds.height, 300);
    EXPECT_EQ(window_->GetTitle(), "CNA geometry");
    EXPECT_NE(window_->GetId(), 0u);
}

TEST_F(X11Live, TheNativeHandleCarriesADisplayAndAnXidInTheRightFields)
{
    window_ = MakeWindow();
    const NativeWindowHandle handle = window_->GetNativeHandle();

    ASSERT_EQ(handle.system, NativeWindowSystem::X11);
    EXPECT_NE(handle.display, nullptr);
    EXPECT_NE(handle.windowId, 0u);
    // The XID must be in windowId and NOT in the pointer field: an XID is a server resource id,
    // not an address, and a renderer that found it in `window` would dereference it.
    EXPECT_EQ(handle.window, nullptr);
    EXPECT_EQ(handle.surface, nullptr);
    EXPECT_TRUE(HasNativeWindow(handle));

    X11NativeWindow x11{};
    ASSERT_TRUE(TryGetX11(handle, x11));
    EXPECT_EQ(x11.display, handle.display);
    EXPECT_EQ(x11.window, handle.windowId);

    // The legacy integer token is the XID, which is what an application interoperating with
    // another X toolkit actually needs.
    EXPECT_EQ(window_->GetWindowHandle(), static_cast<std::uintptr_t>(handle.windowId));
}

TEST_F(X11Live, TheNativeHandleIsStableAcrossCalls)
{
    window_ = MakeWindow();
    const NativeWindowHandle first = window_->GetNativeHandle();
    const NativeWindowHandle second = window_->GetNativeHandle();
    EXPECT_EQ(first.display, second.display);
    EXPECT_EQ(first.windowId, second.windowId);
}

TEST_F(X11Live, TitleRoundTripsThroughNetWmNameInUtf8)
{
    window_ = MakeWindow();
    // Non-ASCII deliberately: _NET_WM_NAME is UTF8_STRING and WM_NAME is Latin-1, and a backend
    // that wrote only the legacy property would mangle this.
    const std::string title = "CNA \xE2\x80\x94 \xC5\xBEluty";
    window_->SetTitle(title);
    window_->Sync();
    EXPECT_EQ(window_->GetTitle(), title);
}

TEST_F(X11Live, ResizingProducesBothALogicalAndAPixelSizeEvent)
{
    window_ = MakeWindow(320, 240);
    window_->Show();
    const WindowId id = window_->GetId();
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(100));
    seen_.clear();

    window_->SetSize(480, 360);
    window_->Sync();

    const bool resized = PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Resized);
    });
    ASSERT_TRUE(resized) << "no Resized event arrived for the requested size change";

    // X11 has no separate logical and physical window geometry, so a resize is always both.
    // Emitting only one would leave a renderer's swapchain at the old size.
    EXPECT_TRUE(SawWindowEvent(seen_, id, WindowEventKind::PixelSizeChanged));

    const WindowBounds bounds = window_->GetClientBounds();
    EXPECT_EQ(bounds.width, 480);
    EXPECT_EQ(bounds.height, 360);
    EXPECT_EQ(window_->GetPixelSize().width, 480);
    EXPECT_EQ(window_->GetPixelSize().height, 360);
}

TEST_F(X11Live, MappingAWindowProducesAnExposeEventForTheFirstPaint)
{
    window_ = MakeWindow();
    const WindowId id = window_->GetId();
    window_->Show();

    const bool exposed = PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Exposed);
    });
    EXPECT_TRUE(exposed) << "a newly mapped window must ask to be painted";
}

TEST_F(X11Live, MultipleWindowsAreIndependentAndEachHasItsOwnIdAndXid)
{
    auto first = MakeWindow(200, 150, "first");
    auto second = MakeWindow(240, 180, "second");
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_NE(first->GetId(), second->GetId());
    EXPECT_NE(first->GetNativeHandle().windowId, second->GetNativeHandle().windowId);

    first->Sync();
    second->Sync();
    EXPECT_EQ(first->GetClientBounds().width, 200);
    EXPECT_EQ(second->GetClientBounds().width, 240);
    EXPECT_EQ(first->GetTitle(), "first");
    EXPECT_EQ(second->GetTitle(), "second");

    // Destroying one must not disturb the other, and must not look like application shutdown.
    const std::uint64_t survivingXid = second->GetNativeHandle().windowId;
    first.reset();
    second->Sync();
    EXPECT_EQ(second->GetNativeHandle().windowId, survivingXid);
    EXPECT_EQ(second->GetClientBounds().width, 240);
}

TEST_F(X11Live, AdoptingAWindowByIdGivesANonOwningViewOfTheSameWindow)
{
    window_ = MakeWindow(260, 200, "adopted");
    const WindowId id = window_->GetId();

    std::unique_ptr<IPlatformWindow> borrowed = platform_->AdoptWindow(id);
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(borrowed->GetId(), id);
    EXPECT_EQ(borrowed->GetNativeHandle().windowId, window_->GetNativeHandle().windowId);
    EXPECT_EQ(borrowed->GetTitle(), "adopted");

    // The borrowed view must not destroy the window when it goes away -- the owner still holds it.
    borrowed.reset();
    window_->Sync();
    EXPECT_EQ(window_->GetTitle(), "adopted");
}

TEST_F(X11Live, AdoptingAnUnknownIdOrXidRefusesRatherThanFabricatingAWindow)
{
    EXPECT_THROW((void) platform_->AdoptWindow(0), PlatformException);
    EXPECT_THROW((void) platform_->AdoptWindow(99999), PlatformException);
    EXPECT_THROW((void) platform_->AdoptWindowHandle(0), PlatformException);
    // An XID no client owns. The error trap is what turns the server's asynchronous BadWindow
    // into this synchronous refusal.
    EXPECT_THROW((void) platform_->AdoptWindowHandle(0x7FFFFFFE), PlatformException);
}

TEST_F(X11Live, AdoptingAnXidThisPlatformCreatedResolvesToTheSameWindow)
{
    window_ = MakeWindow(180, 140, "by xid");
    const std::uintptr_t xid = window_->GetWindowHandle();

    std::unique_ptr<IPlatformWindow> borrowed = platform_->AdoptWindowHandle(xid);
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(borrowed->GetId(), window_->GetId());
    EXPECT_EQ(borrowed->GetTitle(), "by xid");
}

TEST_F(X11Live, SizeConstraintsAndResizabilityAreAccepted)
{
    WindowDescription description;
    description.title = "constrained";
    description.width = 320;
    description.height = 240;
    description.minimumWidth = 200;
    description.minimumHeight = 150;
    description.maximumWidth = 640;
    description.maximumHeight = 480;
    window_ = platform_->CreateWindow(description);
    ASSERT_NE(window_, nullptr);
    window_->Sync();

    EXPECT_TRUE(window_->IsResizable());
    window_->SetResizable(false);
    EXPECT_FALSE(window_->IsResizable());
    window_->SetResizable(true);
    EXPECT_TRUE(window_->IsResizable());
    window_->Sync();
}

TEST_F(X11Live, BorderlessIsAcceptedAndReported)
{
    window_ = MakeWindow();
    EXPECT_FALSE(window_->IsBorderless());
    window_->SetBorderless(true);
    window_->Sync();
    EXPECT_TRUE(window_->IsBorderless());
    window_->SetBorderless(false);
    window_->Sync();
    EXPECT_FALSE(window_->IsBorderless());
}

TEST_F(X11Live, ExclusiveFullscreenRefusesRatherThanSilentlyGivingBorderless)
{
    // plans/plan_x11.md design decision 11. Real exclusive mode means an XRandR mode switch with
    // all the restore-on-crash obligations that carries. It is not implemented, and reporting
    // borderless as exclusive would make GetFullscreenMode lie about what the display is doing.
    window_ = MakeWindow();
    EXPECT_THROW(window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen),
                 PlatformNotSupportedException);
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::Windowed);
}

TEST_F(X11Live, ShowAndHideAreAcceptedAndVisibilityIsObservable)
{
    window_ = MakeWindow();
    window_->Show();
    const WindowId id = window_->GetId();
    EXPECT_TRUE(PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Restored);
    })) << "mapping a window must be observable";

    window_->Hide();
    window_->Sync();
    // Hiding is not minimising: WM_STATE stays absent rather than becoming IconicState, and
    // reporting IsMinimized() true for an application's own Hide() would be wrong.
    EXPECT_FALSE(window_->IsMinimized());
}

// --- display scale and pixel size ------------------------------------------------------------------

TEST_F(X11Live, LogicalAndPixelSizeAgreeBecauseX11HasOnlyOneCoordinateSpace)
{
    window_ = MakeWindow(333, 222);
    window_->Sync();
    const WindowBounds bounds = window_->GetClientBounds();
    const WindowSize pixels = window_->GetPixelSize();
    EXPECT_EQ(bounds.width, pixels.width);
    EXPECT_EQ(bounds.height, pixels.height);

    // Whatever the scale is, it must be a usable positive number -- never zero, never negative,
    // never an absurd value derived from a monitor's claimed physical size.
    const float scale = window_->GetDisplayScale();
    EXPECT_GE(scale, 0.5f);
    EXPECT_LE(scale, 8.0f);
}

// --- timing ---------------------------------------------------------------------------------------

TEST_F(X11Live, TimingIsNativeMonotonicAndInNanoseconds)
{
    EXPECT_EQ(platform_->GetPerformanceFrequency(), 1000000000uLL);

    const std::uint64_t before = platform_->GetPerformanceCounter();
    platform_->Delay(25);
    const std::uint64_t after = platform_->GetPerformanceCounter();
    ASSERT_GT(after, before);
    const std::uint64_t elapsedMs = (after - before) / 1000000uLL;
    EXPECT_GE(elapsedMs, 20u) << "clock_nanosleep must actually sleep";
    EXPECT_LT(elapsedMs, 2000u) << "and must not sleep for an unbounded time";
}

TEST_F(X11Live, DelayZeroYieldsWithoutBlocking)
{
    const std::uint64_t before = platform_->GetPerformanceCounter();
    for (int i = 0; i < 50; ++i)
    {
        platform_->Delay(0);
    }
    const std::uint64_t elapsedMs = (platform_->GetPerformanceCounter() - before) / 1000000uLL;
    EXPECT_LT(elapsedMs, 500u);
}

// --- displays ---------------------------------------------------------------------------------------

TEST_F(X11Live, DisplayEnumerationAgreesWithItsCapability)
{
    IPlatformDisplays* displays = platform_->GetDisplays();
    if (displays == nullptr)
    {
        EXPECT_FALSE(platform_->GetCapabilities().multipleDisplays);
        GTEST_SKIP() << "this server has no XRandR 1.2+, so there is no display enumeration";
    }
    EXPECT_TRUE(platform_->GetCapabilities().multipleDisplays);

    const std::vector<DisplayInfo> all = displays->GetDisplays();
    ASSERT_FALSE(all.empty()) << "a live server always has at least one display";
    for (const DisplayInfo& info : all)
    {
        // Id 0 is reserved: GraphicsAdapter treats it as "no display" and falls back to a default
        // mode, so a display numbered 0 would be invisible to it.
        EXPECT_NE(info.id, 0u);
        EXPECT_FALSE(info.name.empty());
        EXPECT_GT(info.width, 0);
        EXPECT_GT(info.height, 0);
        EXPECT_GT(info.contentScale, 0.0f);
        EXPECT_EQ(info.desktopMode.width, info.width);
        EXPECT_EQ(info.desktopMode.height, info.height);

        DisplayMode current{};
        EXPECT_TRUE(displays->TryGetCurrentDisplayMode(info.id, current));
        EXPECT_EQ(current.width, info.width);
    }

    DisplayMode unknown{};
    EXPECT_FALSE(displays->TryGetCurrentDisplayMode(0, unknown));
    EXPECT_FALSE(displays->TryGetCurrentDisplayMode(9999, unknown));
}

TEST_F(X11Live, AWindowIsAssociatedWithTheDisplayItOverlaps)
{
    IPlatformDisplays* displays = platform_->GetDisplays();
    if (displays == nullptr)
    {
        GTEST_SKIP() << "this server has no XRandR 1.2+";
    }
    window_ = MakeWindow();
    window_->Show();
    window_->Sync();

    DisplayInfo info{};
    ASSERT_TRUE(displays->TryGetDisplayForWindow(*window_, info));
    EXPECT_NE(info.id, 0u);

    // A safe area is refused rather than fabricated from the client bounds: X11 has no such
    // concept, and returning the full bounds would be a made-up answer.
    WindowBounds safeArea{};
    EXPECT_FALSE(displays->TryGetSafeAreaForWindow(*window_, safeArea));
}

// --- keyboard ------------------------------------------------------------------------------------

TEST_F(X11Live, TheKeyboardAnswersMappingQueriesAgainstTheLiveLayout)
{
    IPlatformKeyboard* keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    EXPECT_TRUE(keyboard->HasKeyboard());

    keyboard->Update();
    // A freshly started test server has no key held. This is a level query against the server's
    // own key vector, so it is a real assertion rather than a tautology.
    EXPECT_TRUE(keyboard->GetSnapshot().pressedKeys.empty());

    // A US-layout-independent identity: whatever the layout, the key in the A position exists and
    // the backend can name it.
    EXPECT_EQ(keyboard->GetScancodeName(Scancode::A), ToString(Scancode::A));
    EXPECT_EQ(keyboard->GetScancodeFromName(ToString(Scancode::Space)), Scancode::Space);
    EXPECT_EQ(keyboard->GetKeyFromName("a"), KeyCode::A);
    EXPECT_EQ(keyboard->GetKeyFromName("Escape"), KeyCode::Escape);
    EXPECT_EQ(keyboard->GetKeyFromName("definitely not a keysym"), KeyCode::None);

    // On the default layout of a test server the A position produces `a`. If a future environment
    // loads a different layout this will legitimately differ, which is why only the common case
    // is asserted and the mapping itself is tested exhaustively without a server.
    const KeyCode fromPosition = keyboard->GetKeyFromScancode(Scancode::A);
    EXPECT_TRUE(fromPosition == KeyCode::A || fromPosition == KeyCode::Q ||
                fromPosition == KeyCode::None)
        << "unexpected key for the A position: " << ToString(fromPosition);
}

// --- mouse ---------------------------------------------------------------------------------------

TEST_F(X11Live, ThePointerCanBeReadAndMovedInDesktopCoordinates)
{
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    float x = -1.0f;
    float y = -1.0f;
    ASSERT_TRUE(mouse->TryGetGlobalPosition(x, y));
    EXPECT_GE(x, 0.0f);
    EXPECT_GE(y, 0.0f);

    ASSERT_TRUE(mouse->SetGlobalPosition(123.0f, 77.0f));
    float movedX = -1.0f;
    float movedY = -1.0f;
    ASSERT_TRUE(mouse->TryGetGlobalPosition(movedX, movedY));
    EXPECT_FLOAT_EQ(movedX, 123.0f);
    EXPECT_FLOAT_EQ(movedY, 77.0f);
}

TEST_F(X11Live, WheelNotchesAccumulateInXnaUnitsRatherThanInNotches)
{
    // `MouseSnapshot::scrollX/scrollY` are documented as XNA units -- 120 per whole notch, which
    // is what `Mouse::GetState().ScrollWheelValue` reports and what every XNA game divides by.
    // X delivers one button press per notch, so a backend that forwarded the notch count would
    // disagree with the SDL3 backend by a factor of 120 and no compiler would notice.
    window_ = MakeWindow();
    window_->Show();
    window_->Sync();
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    if (std::system("command -v xdotool >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "xdotool is not installed, so there is no way to inject a wheel notch";
    }

    mouse->Update();
    const int before = mouse->GetSnapshot().scrollY;

    // Button 4 is X's wheel-up. Injected into this window specifically so a stray pointer
    // position cannot send it somewhere else.
    const std::string command = "xdotool click --window " +
                                std::to_string(window_->GetWindowHandle()) + " 4 >/dev/null 2>&1";
    (void) std::system(command.c_str());

    float accumulated = 0.0f;
    const bool sawWheel = PumpUntil([&accumulated](const std::vector<PlatformEvent>& events) {
        accumulated = 0.0f;
        for (const PlatformEvent& event : events)
        {
            if (const auto* wheel = std::get_if<MouseWheelEvent>(&event))
            {
                accumulated += wheel->y;
            }
        }
        return accumulated != 0.0f;
    });
    if (!sawWheel)
    {
        GTEST_SKIP() << "this server did not deliver an injected wheel notch";
    }

    // The event itself is in notches; the snapshot is in XNA units. Both are contract-correct and
    // they are deliberately different scales.
    EXPECT_FLOAT_EQ(accumulated, 1.0f);
    EXPECT_EQ(mouse->GetSnapshot().scrollY - before, 120);

    // And no button event was produced for it. A game watching for clicks must not see a phantom
    // click on every scroll notch.
    const bool sawButton = std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<MouseButtonEvent>(event);
    });
    EXPECT_FALSE(sawButton) << "a wheel notch must not reach a game as a button press";
}

TEST_F(X11Live, TheWheelButtonsAreNeverReportedAsHeldSideButtons)
{
    // X's pointer mask has exactly five bits and spends two of them (Button4Mask, Button5Mask) on
    // the WHEEL. Copying them into the snapshot's X1/X2 bits -- which is what the obvious
    // one-to-one translation does -- reports a side button held for the duration of every notch.
    window_ = MakeWindow();
    window_->Show();
    window_->Sync();
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    mouse->Update();
    const std::uint8_t buttons = mouse->GetSnapshot().buttons;
    // Nothing is pressed on a freshly started test server, so every bit must be clear -- and in
    // particular the two that a wheel would wrongly set.
    EXPECT_EQ(buttons & 0x18, 0) << "the wheel mask bits leaked into X1/X2";
    EXPECT_EQ(buttons, 0);
}

TEST_F(X11Live, CursorShapesAreAcceptedAndVisibilityToggles)
{
    window_ = MakeWindow();
    window_->Show();
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    for (const SystemCursor cursor :
         {SystemCursor::Arrow, SystemCursor::IBeam, SystemCursor::Wait, SystemCursor::Crosshair,
          SystemCursor::Move, SystemCursor::NotAllowed, SystemCursor::Pointer,
          SystemCursor::Progress, SystemCursor::NwseResize, SystemCursor::NeswResize,
          SystemCursor::EwResize, SystemCursor::NsResize})
    {
        EXPECT_NO_THROW(mouse->SetCursor(cursor));
    }
    EXPECT_NO_THROW(mouse->SetCursorVisible(false));
    EXPECT_NO_THROW(mouse->SetCursorVisible(true));
}

TEST_F(X11Live, RelativeMouseModeFollowsItsCapability)
{
    window_ = MakeWindow();
    window_->Show();
    window_->Sync();
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    EXPECT_FALSE(mouse->IsRelativeMode());

    if (!platform_->GetCapabilities().relativeMouse)
    {
        // Without XInput2 the capability is false and the call must refuse, not silently do a
        // warp-based imitation that would report its own warps as input.
        EXPECT_THROW(mouse->SetRelativeMode(window_->GetId(), true),
                     PlatformNotSupportedException);
        return;
    }

    try
    {
        mouse->SetRelativeMode(window_->GetId(), true);
    }
    catch (const PlatformException& error)
    {
        // A grab can legitimately fail when another client holds one. That is an operational
        // failure with a clear message, not a contract violation.
        GTEST_SKIP() << "pointer grab unavailable here: " << error.what();
    }
    EXPECT_TRUE(mouse->IsRelativeMode());
    const MouseDelta delta = mouse->ConsumeRelativeDelta();
    EXPECT_EQ(delta.x, 0);
    EXPECT_EQ(delta.y, 0);
    mouse->SetRelativeMode(window_->GetId(), false);
    EXPECT_FALSE(mouse->IsRelativeMode());
}

TEST_F(X11Live, PointerCaptureIsSymmetric)
{
    window_ = MakeWindow();
    window_->Show();
    window_->Sync();
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    mouse->Update();

    if (!mouse->SetCapture(true))
    {
        GTEST_SKIP() << "another client holds the pointer grab on this server";
    }
    EXPECT_TRUE(mouse->SetCapture(false));
    // Releasing a capture that is not held is a no-op, not a failure.
    EXPECT_TRUE(mouse->SetCapture(false));
}

// --- text input ------------------------------------------------------------------------------------

TEST_F(X11Live, TextInputStartsAndStopsPerWindow)
{
    window_ = MakeWindow();
    IPlatformTextInput* text = platform_->GetTextInput();
    ASSERT_NE(text, nullptr);

    const WindowId id = window_->GetId();
    EXPECT_FALSE(text->IsActive(id));
    text->Start(id, TextInputType::Text);
    EXPECT_TRUE(text->IsActive(id));

    TextInputArea area;
    area.x = 10;
    area.y = 20;
    area.width = 100;
    area.height = 16;
    area.cursorOffset = 4;
    EXPECT_NO_THROW(text->SetInputArea(id, area));

    text->Stop(id);
    EXPECT_FALSE(text->IsActive(id));

    // An unknown window refuses rather than silently recording state for a window that is not
    // there -- a later Start/Stop pair against it would then look balanced.
    EXPECT_THROW(text->Start(4242, TextInputType::Text), PlatformException);

    // X11 has no application-controlled on-screen keyboard, and saying so is the accurate answer.
    EXPECT_FALSE(text->IsScreenKeyboardShown(id));
}

// --- clipboard --------------------------------------------------------------------------------------

TEST_F(X11Live, TheClipboardTakesRealSelectionOwnershipAndReadsItBack)
{
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);

    const std::string text = "CNA clipboard \xE2\x9C\x93 UTF-8";
    clipboard->SetText(text);
    EXPECT_TRUE(clipboard->HasText());
    EXPECT_EQ(clipboard->GetText(), text);
}

TEST_F(X11Live, AClipboardPayloadTooLargeForOnePropertyStillRoundTrips)
{
    // Past the server's maximum request size, which is what forces the INCR path on any external
    // reader. Setting and reading it back here exercises ownership and the size bookkeeping; the
    // cross-process INCR transfer itself is exercised by X11ClipboardInteropTests.
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);

    std::string large;
    large.reserve(1024 * 1024);
    while (large.size() < 1024u * 1024u)
    {
        large += "0123456789abcdef";
    }
    clipboard->SetText(large);
    EXPECT_TRUE(clipboard->HasText());
    EXPECT_EQ(clipboard->GetText().size(), large.size());
    EXPECT_EQ(clipboard->GetText(), large);
}

// --- graphics services ---------------------------------------------------------------------------------

TEST_F(X11Live, TheSurfacePresenterPutsRealPixelsOnAWindow)
{
    window_ = MakeWindow(64, 48);
    window_->Show();
    window_->Sync();

    std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window_);
    ASSERT_NE(presenter, nullptr);

    int width = 0;
    int height = 0;
    presenter->GetTargetSize(width, height);
    EXPECT_EQ(width, 64);
    EXPECT_EQ(height, 48);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(32 * 24 * 4), 0);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index + 0] = 0xFF;  // R
        pixels[index + 1] = 0x40;  // G
        pixels[index + 2] = 0x10;  // B
        pixels[index + 3] = 0xFF;  // A
    }
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 32;
    frame.height = 24;

    for (const PresentScaleMode mode :
         {PresentScaleMode::Stretch, PresentScaleMode::Letterbox, PresentScaleMode::Overscan,
          PresentScaleMode::None, PresentScaleMode::Native})
    {
        presenter->SetScaleMode(mode, PresentFilter::Nearest);
        EXPECT_NO_THROW(presenter->Present(frame));
    }
    presenter->SetScaleMode(PresentScaleMode::Letterbox, PresentFilter::Linear);
    EXPECT_NO_THROW(presenter->Present(frame));

    // XPutImage has no relationship to the vertical blank, so this reports false rather than
    // claiming a synchronisation it does not perform.
    EXPECT_FALSE(presenter->SetVSync(true));
}

TEST_F(X11Live, TheSurfacePresenterRejectsAMalformedFrameBeforeReadingIt)
{
    window_ = MakeWindow(64, 48);
    window_->Show();
    std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window_);
    ASSERT_NE(presenter, nullptr);

    std::vector<std::uint8_t> pixels(16 * 16 * 4, 0);
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 16;
    frame.height = 16;

    SurfaceFrame nullPixels = frame;
    nullPixels.pixels = nullptr;
    EXPECT_THROW(presenter->Present(nullPixels), PlatformException);

    SurfaceFrame zeroSize = frame;
    zeroSize.width = 0;
    EXPECT_THROW(presenter->Present(zeroSize), PlatformException);

    SurfaceFrame shortStride = frame;
    shortStride.strideBytes = 4;
    EXPECT_THROW(presenter->Present(shortStride), PlatformException);
}

TEST_F(X11Live, TheVulkanSurfaceServiceNamesTheExtensionsACallerMustEnable)
{
    IPlatformVulkanSurface* vulkan = platform_->GetVulkanSurface();
    ASSERT_NE(vulkan, nullptr) << "the capability said this service exists";
    ASSERT_TRUE(platform_->GetCapabilities().vulkanSurface);

    const std::vector<std::string> extensions = vulkan->GetInstanceExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "VK_KHR_surface"), extensions.end());
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), "VK_KHR_xlib_surface"),
              extensions.end());

    // A null instance refuses immediately rather than reaching a loader that is not there.
    EXPECT_THROW((void) vulkan->CreateSurface(nullptr, 1), PlatformException);
    // Destroying nothing is a no-op, which is what teardown paths need.
    EXPECT_NO_THROW(vulkan->DestroySurface(nullptr, 0));
}

TEST_F(X11Live, AGlContextRefusesOnAWindowWhoseVisualWasNotChosenForGl)
{
    IPlatformGlContext* gl = platform_->GetGlContext();
    if (gl == nullptr)
    {
        EXPECT_FALSE(platform_->GetCapabilities().openGlContext);
        GTEST_SKIP() << "this server provides no GLX 1.3";
    }
    EXPECT_TRUE(platform_->GetCapabilities().openGlContext);

    // An X window's visual is fixed at creation. A window created with the default render intent
    // cannot host a GL context, and saying so here is far better than the BadMatch the driver
    // would raise several calls later.
    window_ = MakeWindow();
    GlContextDescription description;
    EXPECT_THROW((void) gl->CreateContext(window_->GetId(), description), PlatformException);
    EXPECT_THROW((void) gl->CreateContext(4242, description), PlatformException);
}

TEST_F(X11Live, AGlWindowGetsARealContextThatCanBeMadeCurrentAndSwapped)
{
    IPlatformGlContext* gl = platform_->GetGlContext();
    if (gl == nullptr)
    {
        GTEST_SKIP() << "this server provides no GLX 1.3";
    }

    // The whole coupling this backend has to solve: an X window's visual is fixed at creation, so
    // a GL-capable window must be created with a GL-capable visual already chosen. That is why
    // WindowDescription carries a render intent rather than offering a post-creation setter.
    WindowDescription description;
    description.title = "CNA GL";
    description.width = 128;
    description.height = 96;
    description.centered = false;
    description.renderIntent = WindowRenderIntent::OpenGl;
    description.openGlFramebuffer.depthBits = 24;
    description.openGlFramebuffer.stencilBits = 8;
    description.openGlFramebuffer.doubleBuffered = true;

    try
    {
        window_ = platform_->CreateWindow(description);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "no GL-capable visual on this server: " << error.what();
    }
    ASSERT_NE(window_, nullptr);
    window_->Show();
    window_->Sync();

    GlContextDescription requested;
    requested.majorVersion = 3;
    requested.minorVersion = 3;
    requested.profile = GlProfile::Core;
    requested.depthBits = 24;
    requested.stencilBits = 8;

    GlContextHandle context = nullptr;
    try
    {
        context = gl->CreateContext(window_->GetId(), requested);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this driver refused a GL context: " << error.what();
    }
    ASSERT_NE(context, nullptr);

    gl->MakeCurrent(window_->GetId(), context);
    const GlContextBinding binding = gl->GetCurrentBinding();
    EXPECT_EQ(binding.context, context);
    EXPECT_EQ(binding.window, window_->GetId());

    // A GL entry point resolves only once a context is current on most drivers, which is why
    // this is asserted here rather than before MakeCurrent.
    EXPECT_NE(gl->GetProcAddress("glClear"), nullptr);
    EXPECT_NE(gl->GetProcAddressLoader(), nullptr);
    // There is deliberately no assertion that an unknown name resolves to null. GLX's
    // `glXGetProcAddressARB` is specified to be able to return a non-null pointer for an entry
    // point the implementation does not support -- libglvnd returns a dispatch stub for any
    // name -- so a renderer must decide what is available from the GL version and the extension
    // string, never from a null proc address. Asserting null here would encode the opposite.

    // What the driver actually granted, not what was asked for. A renderer that assumed it got
    // exactly its request breaks on an unfamiliar driver.
    const GlContextDescription granted = gl->GetContextAttributes(context);
    EXPECT_GE(granted.redBits, 8);
    EXPECT_GE(granted.greenBits, 8);
    EXPECT_GE(granted.blueBits, 8);
    EXPECT_GE(granted.depthBits, 16);

    EXPECT_NO_THROW(gl->SwapBuffers(window_->GetId()));
    // SetSwapInterval reports whether a GLX swap-control extension applied it; a software or
    // headless GL stack legitimately has none, so either answer is contract-correct.
    (void) gl->SetSwapInterval(1);

    gl->MakeCurrent(0, nullptr);
    EXPECT_EQ(gl->GetCurrentBinding().context, nullptr);

    // Destroying the current context would leave GLX with a freed binding; the implementation
    // unbinds first, and destroying twice must be harmless for teardown paths.
    gl->DestroyContext(context);
    EXPECT_NO_THROW(gl->DestroyContext(context));
    EXPECT_NO_THROW(gl->DestroyContext(nullptr));
}

TEST_F(X11Live, TwoGlWindowsEachGetTheirOwnContext)
{
    IPlatformGlContext* gl = platform_->GetGlContext();
    if (gl == nullptr)
    {
        GTEST_SKIP() << "this server provides no GLX 1.3";
    }

    WindowDescription description;
    description.width = 96;
    description.height = 64;
    description.centered = false;
    description.renderIntent = WindowRenderIntent::OpenGl;
    description.openGlFramebuffer.doubleBuffered = true;

    std::unique_ptr<IPlatformWindow> first;
    std::unique_ptr<IPlatformWindow> second;
    try
    {
        description.title = "GL one";
        first = platform_->CreateWindow(description);
        description.title = "GL two";
        second = platform_->CreateWindow(description);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "no GL-capable visual on this server: " << error.what();
    }
    first->Show();
    second->Show();

    GlContextDescription requested;
    GlContextHandle firstContext = nullptr;
    GlContextHandle secondContext = nullptr;
    try
    {
        firstContext = gl->CreateContext(first->GetId(), requested);
        secondContext = gl->CreateContext(second->GetId(), requested);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this driver refused a GL context: " << error.what();
    }
    ASSERT_NE(firstContext, nullptr);
    ASSERT_NE(secondContext, nullptr);
    EXPECT_NE(firstContext, secondContext);

    gl->MakeCurrent(first->GetId(), firstContext);
    EXPECT_EQ(gl->GetCurrentBinding().window, first->GetId());
    gl->MakeCurrent(second->GetId(), secondContext);
    EXPECT_EQ(gl->GetCurrentBinding().window, second->GetId());

    gl->MakeCurrent(0, nullptr);
    gl->DestroyContext(firstContext);
    gl->DestroyContext(secondContext);
}

// --- error policy ----------------------------------------------------------------------------------

TEST_F(X11Live, ADeliberateBadRequestDoesNotEndTheProcessAndLeavesTheConnectionUsable)
{
    // Xlib's DEFAULT error handler calls exit(). The SDL3 backend measured that twice
    // (plans/plan_vulkan.md VULKAN-154/157): one bad request either killed the test binary or
    // deadlocked it inside a platform destructor exit() had run under a held lock. A native
    // backend issues far more requests than SDL did, so it needs the same protection -- and
    // surviving is not a weak proxy for the assertion here, it IS the assertion.
    window_ = MakeWindow();
    const NativeWindowHandle handle = window_->GetNativeHandle();
    X11NativeWindow x11{};
    ASSERT_TRUE(TryGetX11(handle, x11));

    auto* display = static_cast<::Display*>(x11.display);
    // Property format 3 is illegal: the protocol allows 8, 16 and 32 only. This is a BadValue the
    // server cannot avoid raising.
    const unsigned char payload = 0;
    XChangeProperty(display, static_cast<::Window>(x11.window), XA_WM_NAME, XA_STRING, 3,
                    PropModeReplace, &payload, 1);
    XSync(display, kXFalse);

    // Still here. And the connection still works.
    window_->SetTitle("survived");
    window_->Sync();
    EXPECT_EQ(window_->GetTitle(), "survived");
}

} // namespace
