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

#include "../../../src/X11/X11Clipboard.hpp"
#include "../../../src/X11/X11Error.hpp"
#include "../../../src/X11/X11Mouse.hpp"
#include "../../../src/X11/X11Platform.hpp"
#include "../../../src/X11/X11Window.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <sys/wait.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::kCurrentTime;
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

/// Asks for a window to be closed the way a window manager's close button does, from a separate
/// X connection -- so the request reaches the backend through the server exactly as a real one
/// would, rather than being injected into its own queue.
///
/// `XSync` on the sending connection is what makes a test built on this deterministic: once it
/// returns, the server has processed the send and the event is already queued for the receiver.
bool SendWmDeleteWindowFromAnotherClient(const std::uintptr_t xid)
{
    ::Display* other = XOpenDisplay(nullptr);
    if (other == nullptr)
    {
        return false;
    }
    XEvent event{};
    event.type = ClientMessage;
    event.xclient.window = static_cast<::Window>(xid);
    event.xclient.message_type = XInternAtom(other, "WM_PROTOCOLS", kXFalse);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(XInternAtom(other, "WM_DELETE_WINDOW", kXFalse));
    event.xclient.data.l[1] = static_cast<long>(kCurrentTime);
    const bool sent = XSendEvent(other, static_cast<::Window>(xid), kXFalse, NoEventMask, &event) != 0;
    XSync(other, kXFalse);
    XCloseDisplay(other);
    return sent;
}

/// Moves keyboard focus to a window from a separate X connection, the way a window manager does.
bool FocusFromAnotherClient(const std::uintptr_t xid)
{
    ::Display* other = XOpenDisplay(nullptr);
    if (other == nullptr)
    {
        return false;
    }
    XSetInputFocus(other, static_cast<::Window>(xid), RevertToParent, kCurrentTime);
    XSync(other, kXFalse);
    XCloseDisplay(other);
    return true;
}

/// Asks for a window to be activated the way a pager or a taskbar does, from a separate X
/// connection. Under a window manager that is the request that moves focus -- a compositing one
/// applies focus-stealing prevention to a newly mapped window -- and on a bare server, where no
/// one would act on it, focus is set directly instead.
bool ActivateFromAnotherClient(const std::uintptr_t xid)
{
    ::Display* other = XOpenDisplay(nullptr);
    if (other == nullptr)
    {
        return false;
    }
    Atom type = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    XGetWindowProperty(other, DefaultRootWindow(other),
                       XInternAtom(other, "_NET_SUPPORTING_WM_CHECK", kXFalse), 0, 1, kXFalse,
                       XA_WINDOW, &type, &format, &count, &remaining, &data);
    const bool windowManager = data != nullptr && count == 1;
    if (data != nullptr) { XFree(data); }
    if (windowManager)
    {
        XEvent event{};
        event.xclient.type = ClientMessage;
        event.xclient.window = static_cast<::Window>(xid);
        event.xclient.message_type = XInternAtom(other, "_NET_ACTIVE_WINDOW", kXFalse);
        event.xclient.format = 32;
        event.xclient.data.l[0] = 2; // source indication: a pager
        event.xclient.data.l[1] = static_cast<long>(kCurrentTime);
        XSendEvent(other, DefaultRootWindow(other), kXFalse,
                   SubstructureRedirectMask | SubstructureNotifyMask, &event);
    }
    else
    {
        XSetInputFocus(other, static_cast<::Window>(xid), RevertToParent, kCurrentTime);
    }
    XSync(other, kXFalse);
    XCloseDisplay(other);
    return true;
}

/// A CLIPBOARD owner on its own X connection and thread that serves its text through INCR in
/// small chunks and waits after each of the requestor's deletes before writing the next one.
///
/// That pause is what real owners do under load and what xsel (4000-byte chunks) does routinely,
/// and it is what exposes a receiver that treats a stale PropertyNotify -- the one the INCR
/// announcement itself produced -- as "the next chunk is here".
class SlowIncrementalOwner
{
public:
    SlowIncrementalOwner(std::string text, const std::size_t chunk,
                         const std::chrono::milliseconds pause)
        : text_(std::move(text)), chunk_(chunk), pause_(pause)
    {
    }

    ~SlowIncrementalOwner()
    {
        stop_ = true;
        if (thread_.joinable()) { thread_.join(); }
        if (display_ != nullptr) { XCloseDisplay(display_); }
    }

    SlowIncrementalOwner(const SlowIncrementalOwner&) = delete;
    SlowIncrementalOwner& operator=(const SlowIncrementalOwner&) = delete;

    /// Takes the CLIPBOARD and starts serving. Returns false when there is no display.
    bool Start()
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) { return false; }
        clipboard_ = XInternAtom(display_, "CLIPBOARD", kXFalse);
        utf8_ = XInternAtom(display_, "UTF8_STRING", kXFalse);
        incr_ = XInternAtom(display_, "INCR", kXFalse);
        window_ = XCreateSimpleWindow(display_, DefaultRootWindow(display_), 0, 0, 1, 1, 0, 0, 0);
        XSetSelectionOwner(display_, clipboard_, window_, kCurrentTime);
        XSync(display_, kXFalse);
        if (XGetSelectionOwner(display_, clipboard_) != window_) { return false; }
        thread_ = std::thread([this] { Run(); });
        return true;
    }

private:
    void Run()
    {
        ::Window requestor = 0;
        Atom property = 0;
        std::size_t offset = 0;
        bool sending = false;
        while (!stop_)
        {
            while (XPending(display_) > 0)
            {
                XEvent event;
                XNextEvent(display_, &event);
                if (event.type == SelectionRequest && event.xselectionrequest.target == utf8_)
                {
                    const XSelectionRequestEvent& request = event.xselectionrequest;
                    requestor = request.requestor;
                    property = request.property;
                    offset = 0;
                    sending = true;
                    XSelectInput(display_, requestor, PropertyChangeMask);
                    const long total = static_cast<long>(text_.size());
                    XChangeProperty(display_, requestor, property, incr_, 32, PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(&total), 1);
                    XEvent notify{};
                    notify.xselection.type = SelectionNotify;
                    notify.xselection.requestor = requestor;
                    notify.xselection.selection = request.selection;
                    notify.xselection.target = request.target;
                    notify.xselection.property = property;
                    notify.xselection.time = request.time;
                    XSendEvent(display_, requestor, kXFalse, NoEventMask, &notify);
                    XFlush(display_);
                }
                else if (event.type == SelectionRequest)
                {
                    XEvent refuse{};
                    refuse.xselection.type = SelectionNotify;
                    refuse.xselection.requestor = event.xselectionrequest.requestor;
                    refuse.xselection.selection = event.xselectionrequest.selection;
                    refuse.xselection.target = event.xselectionrequest.target;
                    refuse.xselection.property = 0;
                    XSendEvent(display_, event.xselectionrequest.requestor, kXFalse, NoEventMask,
                               &refuse);
                    XFlush(display_);
                }
                else if (event.type == PropertyNotify && sending &&
                         event.xproperty.window == requestor && event.xproperty.atom == property &&
                         event.xproperty.state == PropertyDelete)
                {
                    std::this_thread::sleep_for(pause_);
                    const std::size_t length = std::min(chunk_, text_.size() - offset);
                    XChangeProperty(display_, requestor, property, utf8_, 8, PropModeReplace,
                                    reinterpret_cast<const unsigned char*>(text_.data() + offset),
                                    static_cast<int>(length));
                    XFlush(display_);
                    offset += length;
                    if (length == 0)
                    {
                        sending = false;
                        XSelectInput(display_, requestor, NoEventMask);
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::string text_;
    std::size_t chunk_;
    std::chrono::milliseconds pause_;
    ::Display* display_ = nullptr;
    ::Window window_ = 0;
    Atom clipboard_ = 0;
    Atom utf8_ = 0;
    Atom incr_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

/// An external CLIPBOARD reader on its own X connection and thread that pulls a selection through
/// INCR one chunk at a time, calling @p afterFirstChunk once the transfer is under way.
class IncrementalRequestor
{
public:
    explicit IncrementalRequestor(std::function<void()> afterFirstChunk,
                                  const bool abandonAfterFirstChunk = false)
        : afterFirstChunk_(std::move(afterFirstChunk)), abandon_(abandonAfterFirstChunk)
    {
    }

    ~IncrementalRequestor()
    {
        if (thread_.joinable()) { thread_.join(); }
    }

    IncrementalRequestor(const IncrementalRequestor&) = delete;
    IncrementalRequestor& operator=(const IncrementalRequestor&) = delete;

    void Start() { thread_ = std::thread([this] { Run(); }); }
    [[nodiscard]] bool Done() const { return done_; }
    [[nodiscard]] bool SawIncr() const { return sawIncr_; }
    [[nodiscard]] const std::string& Text() const { return text_; }

private:
    void Run()
    {
        ::Display* display = XOpenDisplay(nullptr);
        if (display == nullptr) { done_ = true; return; }
        const Atom clipboard = XInternAtom(display, "CLIPBOARD", kXFalse);
        const Atom utf8 = XInternAtom(display, "UTF8_STRING", kXFalse);
        const Atom incr = XInternAtom(display, "INCR", kXFalse);
        const Atom property = XInternAtom(display, "CNA_TEST_INCR_PASTE", kXFalse);
        const ::Window window =
            XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
        XSelectInput(display, window, PropertyChangeMask);
        XConvertSelection(display, clipboard, utf8, property, window, kCurrentTime);
        XFlush(display);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        const auto readProperty = [&](Atom& type, std::string& chunk) {
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            chunk.clear();
            if (XGetWindowProperty(display, window, property, 0, 0x7FFFFFFF, kXFalse,
                                   AnyPropertyType, &type, &format, &count, &remaining,
                                   &data) != 0)
            {
                return false;
            }
            if (data != nullptr)
            {
                if (format == 8) { chunk.assign(reinterpret_cast<const char*>(data), count); }
                XFree(data);
            }
            return type != 0;
        };

        bool notified = false;
        while (!notified && std::chrono::steady_clock::now() < deadline)
        {
            XEvent event;
            if (XCheckTypedWindowEvent(display, window, SelectionNotify, &event) == kXFalse)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            notified = event.xselection.property != 0;
            if (!notified) { break; }
        }
        Atom type = 0;
        std::string chunk;
        if (notified && readProperty(type, chunk) && type == incr)
        {
            sawIncr_ = true;
            XDeleteProperty(display, window, property);
            XFlush(display);
            bool first = true;
            while (std::chrono::steady_clock::now() < deadline)
            {
                XEvent event;
                if (XCheckTypedWindowEvent(display, window, PropertyNotify, &event) == kXFalse ||
                    event.xproperty.atom != property || event.xproperty.state != PropertyNewValue)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                if (!readProperty(type, chunk)) { continue; }
                XDeleteProperty(display, window, property);
                XFlush(display);
                if (chunk.empty()) { break; }
                text_ += chunk;
                if (first)
                {
                    first = false;
                    afterFirstChunk_();
                    if (abandon_)
                    {
                        // A requestor that dies mid-transfer: its window goes, with the
                        // connection, and it never asks for another chunk.
                        break;
                    }
                }
            }
        }
        XDestroyWindow(display, window);
        XCloseDisplay(display);
        done_ = true;
    }

    std::function<void()> afterFirstChunk_;
    bool abandon_ = false;
    std::thread thread_;
    std::string text_;
    std::atomic<bool> done_{false};
    std::atomic<bool> sawIncr_{false};
};

/// A scripted window manager, on its own X connection and thread, that iconifies the way mutter
/// (GNOME) does: the window stays MAPPED and only gains _NET_WM_STATE_HIDDEN and WM_STATE
/// IconicState, and nothing but an activation request (_NET_ACTIVE_WINDOW) brings it back.
///
/// openbox, which the window-manager suite runs, unmaps an iconified window instead, so a map
/// request de-iconifies it there and a Restore() that only maps passes; this is the window
/// manager that tells the two apart. It needs a display no other window manager holds, which is
/// what this suite's private bare server is.
class KeepsIconifiedWindowsMappedWm
{
public:
    ~KeepsIconifiedWindowsMappedWm() { Stop(); }

    KeepsIconifiedWindowsMappedWm() = default;
    KeepsIconifiedWindowsMappedWm(const KeepsIconifiedWindowsMappedWm&) = delete;
    KeepsIconifiedWindowsMappedWm& operator=(const KeepsIconifiedWindowsMappedWm&) = delete;

    /// Becomes the window manager. False when there is no display or another one already is.
    bool Start()
    {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) { return false; }
        root_ = DefaultRootWindow(display_);
        XWindowAttributes attributes{};
        XGetWindowAttributes(display_, root_, &attributes);
        if ((attributes.all_event_masks & SubstructureRedirectMask) != 0)
        {
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }
        // Without this an X error on the test's own connection reaches Xlib's default handler,
        // which exits the process.
        CNA::Platform::X11::X11ErrorPolicy::Register(display_);
        netSupported_ = XInternAtom(display_, "_NET_SUPPORTED", kXFalse);
        netSupportingWmCheck_ = XInternAtom(display_, "_NET_SUPPORTING_WM_CHECK", kXFalse);
        netActiveWindow_ = XInternAtom(display_, "_NET_ACTIVE_WINDOW", kXFalse);
        netWmState_ = XInternAtom(display_, "_NET_WM_STATE", kXFalse);
        netWmStateHidden_ = XInternAtom(display_, "_NET_WM_STATE_HIDDEN", kXFalse);
        wmState_ = XInternAtom(display_, "WM_STATE", kXFalse);
        wmChangeState_ = XInternAtom(display_, "WM_CHANGE_STATE", kXFalse);

        XSelectInput(display_, root_, SubstructureRedirectMask | SubstructureNotifyMask);
        check_ = XCreateSimpleWindow(display_, root_, 0, 0, 1, 1, 0, 0, 0);
        XChangeProperty(display_, check_, netSupportingWmCheck_, XA_WINDOW, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&check_), 1);
        XChangeProperty(display_, root_, netSupportingWmCheck_, XA_WINDOW, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&check_), 1);
        const long supported[] = {static_cast<long>(netActiveWindow_),
                                  static_cast<long>(netWmState_),
                                  static_cast<long>(netWmStateHidden_)};
        XChangeProperty(display_, root_, netSupported_, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(supported), 3);
        XSync(display_, kXFalse);
        thread_ = std::thread([this] { Run(); });
        return true;
    }

    /// Stops managing and takes the window manager's root properties with it: the test server
    /// runs with -noreset, so they would otherwise outlive this test and mislead the next one.
    void Stop()
    {
        stop_ = true;
        if (thread_.joinable()) { thread_.join(); }
        if (display_ == nullptr) { return; }
        XDeleteProperty(display_, root_, netSupported_);
        XDeleteProperty(display_, root_, netSupportingWmCheck_);
        XDestroyWindow(display_, check_);
        XSync(display_, kXFalse);
        CNA::Platform::X11::X11ErrorPolicy::Unregister(display_);
        XCloseDisplay(display_);
        display_ = nullptr;
    }

    [[nodiscard]] int Activations() const { return activations_; }

private:
    void Run()
    {
        while (!stop_)
        {
            while (XPending(display_) > 0)
            {
                XEvent event;
                XNextEvent(display_, &event);
                if (event.type == MapRequest)
                {
                    XMapWindow(display_, event.xmaprequest.window);
                    SetIconic(event.xmaprequest.window, false);
                }
                else if (event.type == ConfigureRequest)
                {
                    const XConfigureRequestEvent& request = event.xconfigurerequest;
                    XWindowChanges changes{};
                    changes.x = request.x;
                    changes.y = request.y;
                    changes.width = request.width;
                    changes.height = request.height;
                    changes.border_width = request.border_width;
                    changes.sibling = request.above;
                    changes.stack_mode = request.detail;
                    XConfigureWindow(display_, request.window,
                                     static_cast<unsigned>(request.value_mask), &changes);
                }
                else if (event.type == ClientMessage &&
                         event.xclient.message_type == wmChangeState_ &&
                         event.xclient.data.l[0] == IconicState)
                {
                    SetIconic(event.xclient.window, true);
                }
                else if (event.type == ClientMessage &&
                         event.xclient.message_type == netActiveWindow_)
                {
                    ++activations_;
                    SetIconic(event.xclient.window, false);
                }
                XFlush(display_);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    void SetIconic(const ::Window window, const bool iconic)
    {
        const long state[] = {iconic ? IconicState : NormalState, 0};
        XChangeProperty(display_, window, wmState_, wmState_, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(state), 2);
        const long hidden = static_cast<long>(netWmStateHidden_);
        XChangeProperty(display_, window, netWmState_, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&hidden), iconic ? 1 : 0);
    }

    ::Display* display_ = nullptr;
    ::Window root_ = 0;
    ::Window check_ = 0;
    Atom netSupported_ = 0;
    Atom netSupportingWmCheck_ = 0;
    Atom netActiveWindow_ = 0;
    Atom netWmState_ = 0;
    Atom netWmStateHidden_ = 0;
    Atom wmState_ = 0;
    Atom wmChangeState_ = 0;
    std::atomic<bool> stop_{false};
    std::atomic<int> activations_{0};
    std::thread thread_;
};

[[nodiscard]] WindowId EventWindow(const PlatformEvent& event)
{
    if (const auto* window = std::get_if<WindowEvent>(&event)) { return window->window; }
    if (const auto* key = std::get_if<KeyEvent>(&event)) { return key->window; }
    if (const auto* text = std::get_if<TextInputEvent>(&event)) { return text->window; }
    if (const auto* button = std::get_if<MouseButtonEvent>(&event)) { return button->window; }
    if (const auto* motion = std::get_if<MouseMotionEvent>(&event)) { return motion->window; }
    if (const auto* wheel = std::get_if<MouseWheelEvent>(&event)) { return wheel->window; }
    return 0;
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
    // X has no dialog service; the backend draws its own boxes (plans/plan_x11.md X11-0167).
    EXPECT_TRUE(capabilities.messageBox);
    EXPECT_NE(platform_->GetDialogs(), nullptr);

    // Controllers are not an X facility: they come from the kernel's evdev nodes, wherever the
    // build has them and the machine has /dev/input (plans/plan_x11.md X11-0150).
#ifdef CNA_PLATFORM_HAVE_EVDEV
    const bool evdev = std::filesystem::is_directory("/dev/input");
#else
    const bool evdev = false;
#endif
    EXPECT_EQ(capabilities.gamepad, evdev);
    EXPECT_EQ(capabilities.joystick, evdev);
    EXPECT_EQ(capabilities.gamepadRumble, evdev);
    EXPECT_EQ(capabilities.gamepadSensors, evdev);  // Each pad says whether it has them (X11-0166).
    EXPECT_EQ(capabilities.haptics, evdev);         // Force feedback through the same nodes (X11-0168).
    EXPECT_EQ(platform_->GetHaptics() != nullptr, evdev);

    // Deliberately false, each because the facility does not exist in X11 rather than because it
    // was not finished. A future change turning one of these on without implementing it would
    // fail here rather than at a null dereference in a game.
    EXPECT_FALSE(capabilities.sensors);
    // File dialogs are the desktop portal's (X11-0169), and no test reaches a session bus that
    // has one (X11DesktopPortalTests.cpp).
    EXPECT_FALSE(capabilities.nativeFileDialog);
    // A tray is a client owning _NET_SYSTEM_TRAY_S0 (X11-0171), and this server runs none.
    EXPECT_FALSE(capabilities.tray);
    EXPECT_FALSE(capabilities.camera);
    EXPECT_FALSE(capabilities.managedEntrypoint);
    // Battery state is Linux's, not X's: the kernel's power supplies (plans/plan_x11.md X11-0163).
#ifdef CNA_PLATFORM_HAVE_EVDEV
    EXPECT_TRUE(capabilities.powerInfo);
#else
    EXPECT_FALSE(capabilities.powerInfo);
#endif

    // XIM is used for committed text, which is `textInput`. `ime` promises composition events,
    // and those reach the application only when it asked to draw the composition
    // (CNA_IME_IMPLEMENTED_UI=composition) and the input method agreed -- neither of which holds
    // here (plans/plan_x11.md X11-0152; X11InputMethodTests covers the case where both do).
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

TEST_F(X11Live, ADestroyedWindowLeavesThePlatformBeforeItsLastEventsArrive)
{
    // plans/plan_native_platform_validation.md NPV-0101. Destroying a window is the
    // application's decision, and the X server's UnmapNotify/DestroyNotify for it are still on
    // their way when the wrapper is gone. The platform's registry must already have forgotten
    // it by then: the first version kept the raw pointer, so the next pump walked freed memory
    // and the window count behind "last window closed" stayed one too high for good.
    auto kept = MakeWindow(200, 150, "kept");
    auto destroyed = MakeWindow(200, 150, "destroyed");
    const WindowId keptId = kept->GetId();
    const WindowId destroyedId = destroyed->GetId();
    kept->Show();
    destroyed->Show();
    ASSERT_TRUE(PumpUntil([keptId, destroyedId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, keptId, WindowEventKind::Exposed) &&
               SawWindowEvent(events, destroyedId, WindowEventKind::Exposed);
    })) << "both windows should become viewable";

    destroyed.reset();
    seen_.clear();
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(300));
    for (const PlatformEvent& event : seen_)
    {
        EXPECT_NE(EventWindow(event), destroyedId)
            << "an event was reported for a window the application had already destroyed";
    }

    // The kept window is now the only one, so a close request for it is the application's last
    // window closing -- which is exactly when the contract appends a QuitEvent.
    seen_.clear();
    ASSERT_TRUE(SendWmDeleteWindowFromAnotherClient(kept->GetWindowHandle()));
    ASSERT_TRUE(PumpUntil([keptId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, keptId, WindowEventKind::CloseRequested);
    }));
    EXPECT_TRUE(std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<QuitEvent>(event);
    })) << "closing the last live window must ask the application to quit";
}

TEST_F(X11Live, ACloseRequestStillQueuedForAWindowTheApplicationDestroyedIsIgnored)
{
    // NPV-0101. The user clicks a secondary window's close button twice. The application answers
    // the first request by destroying the window; the second is already queued behind it for an
    // XID nothing owns any more. It must not turn into a close request for "window 0" -- and
    // above all not into a QuitEvent while the main window is still open.
    auto primary = MakeWindow(200, 150, "main");
    auto secondary = MakeWindow(200, 150, "secondary");
    const WindowId secondaryId = secondary->GetId();
    primary->Show();
    secondary->Show();
    primary->Sync();
    secondary->Sync();

    ASSERT_TRUE(SendWmDeleteWindowFromAnotherClient(secondary->GetWindowHandle()));
    ASSERT_TRUE(PumpUntil([secondaryId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, secondaryId, WindowEventKind::CloseRequested);
    }));

    ASSERT_TRUE(SendWmDeleteWindowFromAnotherClient(secondary->GetWindowHandle()));
    secondary.reset();
    seen_.clear();
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(300));

    EXPECT_FALSE(std::any_of(seen_.begin(), seen_.end(), [](const PlatformEvent& event) {
        return std::holds_alternative<QuitEvent>(event);
    })) << "a stale close request must not end an application whose main window is open";
    for (const PlatformEvent& event : seen_)
    {
        const auto* window = std::get_if<WindowEvent>(&event);
        EXPECT_FALSE(window != nullptr && window->kind == WindowEventKind::CloseRequested)
            << "a close request was reported for window " << window->window
            << ", which the application no longer has";
    }
    EXPECT_EQ(primary->GetTitle(), "main");
}

TEST_F(X11Live, FocusCanMoveOnAfterTheFocusedWindowWasDestroyed)
{
    // NPV-0101. Text input remembers which window holds focus so it can move the input method's
    // focus with it. Destroying that window must not leave the memory behind: the next focus
    // change would otherwise unset the input context of a window that no longer exists.
    auto first = MakeWindow(200, 150, "first");
    auto second = MakeWindow(200, 150, "second");
    const WindowId firstId = first->GetId();
    const WindowId secondId = second->GetId();
    first->Show();
    second->Show();
    ASSERT_TRUE(PumpUntil([firstId, secondId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, firstId, WindowEventKind::Exposed) &&
               SawWindowEvent(events, secondId, WindowEventKind::Exposed);
    }));

    ASSERT_TRUE(FocusFromAnotherClient(first->GetWindowHandle()));
    ASSERT_TRUE(PumpUntil([firstId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, firstId, WindowEventKind::FocusGained);
    })) << "the first window never received focus";

    first.reset();
    ASSERT_TRUE(FocusFromAnotherClient(second->GetWindowHandle()));
    EXPECT_TRUE(PumpUntil([secondId](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, secondId, WindowEventKind::FocusGained);
    })) << "focus did not reach the surviving window";
    EXPECT_TRUE(second->HasFocus());
}

TEST_F(X11Live, AProcessExitingWithTheCurrentPlatformOpenTearsDownCleanly)
{
    // NPV-0102. The ordinary end of every XNA-API application: main() returns while
    // CurrentPlatform's lazily created X11 platform is still open, and exit() destroys it among
    // the static destructors. That ran after the X11 error policy's own statics were gone, so the
    // connection unregistered itself from freed memory. An ordinary build cannot see it;
    // AddressSanitizer turns it into a non-zero exit status, which is what this asserts on -- the
    // assertion that matters therefore lives in the sanitizer build.
#if !defined(CNA_PLATFORM_X11_EXIT_HARNESS_PATH)
    GTEST_SKIP() << "cna_platform_x11_exit_harness is not built in this configuration";
#else
    const int status = std::system(CNA_PLATFORM_X11_EXIT_HARNESS_PATH);
    ASSERT_NE(status, -1);
    ASSERT_TRUE(WIFEXITED(status)) << "the harness was killed by a signal";
    if (WEXITSTATUS(status) == 77)
    {
        GTEST_SKIP() << "the harness could not reach the X server";
    }
    EXPECT_EQ(WEXITSTATUS(status), 0) << "exiting with the current platform open must be clean";
#endif
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

TEST_F(X11Live, ExclusiveFullscreenWithoutAWindowManagerRefusesAndLeavesTheModeAlone)
{
    // plans/plan_x11.md design decision 11 and X11-0153. Exclusive fullscreen is borderless
    // fullscreen with a display mode of its own, and fullscreen is the window manager's to
    // perform; this server has none. So it refuses -- before any display mode is touched, which
    // is what the screen size read back afterwards shows. Exclusive fullscreen itself runs in
    // X11ExclusiveFullscreenTests.cpp, on a private server with a window manager.
    window_ = MakeWindow();
    ::Display* observer = XOpenDisplay(nullptr);
    ASSERT_NE(observer, nullptr);
    const ::Window root = DefaultRootWindow(observer);
    const auto rootSize = [observer, root] {
        ::Window rootReturn = CNA::Platform::X11::kNone;
        int x = 0;
        int y = 0;
        unsigned int width = 0;
        unsigned int height = 0;
        unsigned int border = 0;
        unsigned int depth = 0;
        XGetGeometry(observer, root, &rootReturn, &x, &y, &width, &height, &border, &depth);
        return std::pair<unsigned int, unsigned int>(width, height);
    };
    const auto before = rootSize();
    EXPECT_THROW(window_->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen),
                 PlatformNotSupportedException);
    EXPECT_EQ(window_->GetFullscreenMode(), WindowFullscreenMode::Windowed);
    EXPECT_EQ(rootSize(), before);
    XCloseDisplay(observer);
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

// plans/plan_native_platform_validation.md NPV-0113: on GNOME, Restore() of a minimised window
// left it minimised. Mapping is ICCCM's de-iconify, but mutter keeps iconified windows mapped, so
// the map was a no-op and no request ever reached the window manager.
TEST_F(X11Live, RestoreBringsBackAWindowFromAWindowManagerThatKeepsIconifiedWindowsMapped)
{
    KeepsIconifiedWindowsMappedWm windowManager;
    if (!windowManager.Start())
    {
        GTEST_SKIP() << "another window manager owns this display; the scripted one needs it bare";
    }
    // The platform re-reads _NET_SUPPORTED when the root's properties change under it.
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(100));

    window_ = MakeWindow();
    const WindowId id = window_->GetId();
    window_->Show();
    ASSERT_TRUE(PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Restored);
    })) << "the scripted window manager did not map the window";

    window_->Minimize();
    ASSERT_TRUE(PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return window_->IsMinimized() && SawWindowEvent(events, id, WindowEventKind::Minimized);
    })) << "the scripted window manager did not iconify the window";
    seen_.clear();

    window_->Restore();
    EXPECT_TRUE(PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return !window_->IsMinimized() && SawWindowEvent(events, id, WindowEventKind::Restored);
    })) << "Restore() left the window minimised: with iconified windows kept mapped, only an "
           "activation request can bring one back";
    EXPECT_EQ(windowManager.Activations(), 1);

    window_.reset();
    windowManager.Stop();
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

    // X11-0156, D-16: the display scale is pixels per logical unit, and with one coordinate space
    // that is 1 whatever the session's scale preference is -- that belongs to the display's
    // content scale (X11ContentScaleLive).
    EXPECT_EQ(window_->GetDisplayScale(), 1.0f);
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

// plans/plan_native_platform_validation.md NPV-0117: switching layouts never reached a key code.
// Every XKB event rebuilt the table, but always from group 0, so after "us" -> "cz" the Y
// position still reported Y and the number row stayed whatever the first layout made it.
TEST_F(X11Live, KeyCodesFollowALayoutSwitch)
{
    // setxkbmap changes the keymap of whatever server DISPLAY names; on a developer's desktop
    // that would be their own keyboard.
    if (std::getenv("CNA_X11_PRIVATE_TEST_SERVER") == nullptr)
    {
        GTEST_SKIP() << "changes the server's keymap, so it runs only on the private server "
                        "tools/platform/x11_test_server.sh starts";
    }
    if (std::system("setxkbmap -layout us,cz >/dev/null 2>&1") != 0)
    {
        GTEST_SKIP() << "setxkbmap could not load a us,cz keymap on this server";
    }
    struct RestoreKeymap
    {
        ~RestoreKeymap() { (void) std::system("setxkbmap -layout us >/dev/null 2>&1"); }
    } restore;

    IPlatformKeyboard* keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    ::Display* other = XOpenDisplay(nullptr);
    ASSERT_NE(other, nullptr);
    const auto lockGroup = [other](const unsigned int group) {
        XkbLockGroup(other, XkbUseCoreKbd, group);
        XSync(other, kXFalse);
    };
    const auto yKey = [keyboard] { return keyboard->GetKeyFromScancode(Scancode::Y); };

    lockGroup(0);
    ASSERT_TRUE(PumpUntil([&](const std::vector<PlatformEvent>&) { return yKey() == KeyCode::Y; }))
        << "on us the Y position reports " << ToString(yKey());

    lockGroup(1);
    EXPECT_TRUE(PumpUntil([&](const std::vector<PlatformEvent>&) { return yKey() == KeyCode::Z; }))
        << "after switching to cz the Y position still reports " << ToString(yKey());
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::Z), KeyCode::Y);
    // Czech types e-caron over 2 on that key; the virtual key is still D2, as on Windows.
    EXPECT_EQ(keyboard->GetKeyFromScancode(Scancode::D2), KeyCode::D2);
    EXPECT_EQ(keyboard->GetKeyName(Scancode::Y), "Z");

    lockGroup(0);
    EXPECT_TRUE(PumpUntil([&](const std::vector<PlatformEvent>&) { return yKey() == KeyCode::Y; }))
        << "after switching back to us the Y position reports " << ToString(yKey());
    XCloseDisplay(other);
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

TEST_F(X11Live, RelativeMotionCarriesFractionsInsteadOfTruncatingEachReport)
{
    // NPV-0110. XInput2 raw deltas are doubles, and a high-resolution device normalised to the
    // server's units moves in fractions of a unit per report. Truncating every report to an
    // integer turned slow, steady movement into no movement at all.
    if (!platform_->GetCapabilities().relativeMouse)
    {
        GTEST_SKIP() << "no XInput2 on this server";
    }
    window_ = MakeWindow();
    const WindowId id = window_->GetId();
    window_->Show();
    ASSERT_TRUE(PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Exposed);
    }));
    auto* mouse = dynamic_cast<CNA::Platform::X11::X11Mouse*>(platform_->GetMouse());
    ASSERT_NE(mouse, nullptr);
    try
    {
        mouse->SetRelativeMode(id, true);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "pointer grab unavailable here: " << error.what();
    }
    // On a bare server a viewable window is enough to hold the grab. Under a window manager it
    // is held only while the window has focus (NPV-0109), which a newly mapped window need not
    // get on its own, so the test asks for it as a pager would.
    const auto held = [mouse](const std::vector<PlatformEvent>&) {
        return mouse->IsRelativeGrabHeld();
    };
    if (!PumpUntil(held, std::chrono::milliseconds(300)))
    {
        ASSERT_TRUE(ActivateFromAnotherClient(window_->GetWindowHandle()));
        if (!PumpUntil(held, std::chrono::milliseconds(3000)))
        {
            if (!window_->HasFocus())
            {
                GTEST_SKIP() << "the window manager never gave the test window focus, and "
                                "relative mode holds the pointer only while it has it";
            }
            FAIL() << "the window has focus, but the relative grab was not taken";
        }
    }
    (void) mouse->ConsumeRelativeDelta();

    for (int report = 0; report < 8; ++report)
    {
        mouse->AccumulateRawMotion(0.25, -0.125);
    }
    const MouseDelta delta = mouse->ConsumeRelativeDelta();
    EXPECT_EQ(delta.x, 2) << "eight quarter-unit reports are two units";
    EXPECT_EQ(delta.y, -1) << "eight eighth-unit reports are one unit";
    mouse->SetRelativeMode(id, false);
}

TEST_F(X11Live, PointerCaptureIsSymmetric)
{
    window_ = MakeWindow();
    const WindowId id = window_->GetId();
    window_->Show();
    // A pointer grab needs a viewable window (GrabNotViewable otherwise), and under a window
    // manager the map is a request that takes a round trip through it.
    ASSERT_TRUE(PumpUntil([id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Exposed);
    }));
    IPlatformMouse* mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    mouse->Update();

    if (!mouse->SetCapture(true))
    {
        GTEST_SKIP() << "the X server refused the pointer grab (another client holds one, or "
                        "this server grants grabs only to the focused client)";
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

// plans/plan_native_platform_validation.md NPV-0115: pastes from CNA intermittently came back
// empty (xsel, about one run in four). Losing the clipboard queues a SelectionClear at once; an
// application that copied again before its next pump took the clipboard back, and the pump then
// acted on the old SelectionClear and threw the new text away.
TEST_F(X11Live, ASelectionClearQueuedBeforeCopyingAgainDoesNotDropTheNewText)
{
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    clipboard->SetText("first copy");

    ::Display* other = XOpenDisplay(nullptr);
    ASSERT_NE(other, nullptr);
    const Atom clipboardAtom = XInternAtom(other, "CLIPBOARD", kXFalse);
    const Atom utf8 = XInternAtom(other, "UTF8_STRING", kXFalse);
    const Atom property = XInternAtom(other, "CNA_TEST_PASTE", kXFalse);
    const ::Window otherWindow =
        XCreateSimpleWindow(other, DefaultRootWindow(other), 0, 0, 1, 1, 0, 0, 0);
    XSetSelectionOwner(other, clipboardAtom, otherWindow, kCurrentTime);
    XSync(other, kXFalse);

    // The application copies again before its pump has seen the SelectionClear above.
    clipboard->SetText("second copy");
    PumpUntil([](const std::vector<PlatformEvent>&) { return false; },
              std::chrono::milliseconds(100));
    EXPECT_TRUE(clipboard->HasText());
    EXPECT_EQ(clipboard->GetText(), "second copy");

    // And another application pasting now gets it: this is what came back empty.
    XConvertSelection(other, clipboardAtom, utf8, property, otherWindow, kCurrentTime);
    XFlush(other);
    std::string pasted;
    bool answered = false;
    PumpUntil([&](const std::vector<PlatformEvent>&) {
        XEvent event;
        if (XCheckTypedWindowEvent(other, otherWindow, SelectionNotify, &event) == kXFalse)
        {
            return false;
        }
        answered = true;
        if (event.xselection.property != 0)
        {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(other, otherWindow, property, 0, 1024, kXFalse, AnyPropertyType,
                                   &type, &format, &count, &remaining, &data) == 0 &&
                data != nullptr)
            {
                pasted.assign(reinterpret_cast<const char*>(data), count);
                XFree(data);
            }
        }
        return true;
    });
    EXPECT_TRUE(answered) << "the paste request was never answered";
    EXPECT_EQ(pasted, "second copy");

    XDestroyWindow(other, otherWindow);
    XCloseDisplay(other);
}

// plans/plan_native_platform_validation.md NPV-0119: an INCR transfer already under way was
// abandoned the moment CNA lost the clipboard (the requestor waited forever for its next chunk --
// xclip has no timeout), and continued from the same offset in the NEW text when the application
// copied again. Qt and GTK finish a transfer they started from their own copy of the data.
std::string LargeClipboardText(const char seed)
{
    std::string text;
    text.reserve(400 * 1024);
    while (text.size() < 400u * 1024u)
    {
        text += seed;
        text += " INCR transfer under way, \xC5\x99\xC3\xAD\xC5\xA1 0123456789;";
    }
    return text;
}

TEST_F(X11Live, AnIncrementalTransferUnderWayFinishesAfterAnotherClientTakesTheClipboard)
{
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    const std::string text = LargeClipboardText('a');
    clipboard->SetText(text);

    IncrementalRequestor requestor([] {
        ::Display* thief = XOpenDisplay(nullptr);
        if (thief == nullptr) { return; }
        const ::Window owner =
            XCreateSimpleWindow(thief, DefaultRootWindow(thief), 0, 0, 1, 1, 0, 0, 0);
        XSetSelectionOwner(thief, XInternAtom(thief, "CLIPBOARD", kXFalse), owner, kCurrentTime);
        XSync(thief, kXFalse);
        XCloseDisplay(thief);
    });
    requestor.Start();
    ASSERT_TRUE(PumpUntil([&](const std::vector<PlatformEvent>&) { return requestor.Done(); },
                          std::chrono::milliseconds(20000)));
    ASSERT_TRUE(requestor.SawIncr()) << "the payload did not go through INCR";
    EXPECT_EQ(requestor.Text().size(), text.size());
    EXPECT_TRUE(requestor.Text() == text) << "the transfer was abandoned or corrupted";
}

TEST_F(X11Live, AnIncrementalTransferUnderWayKeepsTheTextItStartedWith)
{
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    const std::string text = LargeClipboardText('b');
    clipboard->SetText(text);

    // The application copies again while the paste is being transferred. Calling into CNA from
    // the requestor's thread would race the pump, so the copy is requested and made here.
    std::atomic<bool> copyAgain{false};
    IncrementalRequestor requestor([&copyAgain] { copyAgain = true; });
    requestor.Start();
    bool copied = false;
    ASSERT_TRUE(PumpUntil(
        [&](const std::vector<PlatformEvent>&) {
            if (copyAgain && !copied)
            {
                clipboard->SetText(LargeClipboardText('c'));
                copied = true;
            }
            return requestor.Done();
        },
        std::chrono::milliseconds(20000)));
    ASSERT_TRUE(requestor.SawIncr()) << "the payload did not go through INCR";
    EXPECT_TRUE(copied);
    EXPECT_EQ(requestor.Text().size(), text.size());
    EXPECT_TRUE(requestor.Text() == text) << "the transfer switched to the newer text part-way";
}

// plans/plan_native_platform_validation.md NPV-0127: a requestor that went away in the middle of
// an INCR transfer left the transfer -- and, since NPV-0119, its copy of the text -- behind for
// good: nothing told the clipboard the window was gone.
TEST_F(X11Live, AnIncrementalTransferWhoseRequestorDiesIsForgotten)
{
    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    auto* x11Clipboard = dynamic_cast<CNA::Platform::X11::X11Clipboard*>(clipboard);
    ASSERT_NE(x11Clipboard, nullptr);
    clipboard->SetText(LargeClipboardText('d'));

    IncrementalRequestor requestor([] {}, true);
    requestor.Start();
    ASSERT_TRUE(PumpUntil([&](const std::vector<PlatformEvent>&) { return requestor.Done(); },
                          std::chrono::milliseconds(20000)));
    ASSERT_TRUE(requestor.SawIncr()) << "the payload did not go through INCR";
    EXPECT_TRUE(PumpUntil(
        [&](const std::vector<PlatformEvent>&) {
            return x11Clipboard->GetIncrementalTransferCount() == 0;
        },
        std::chrono::milliseconds(2000)))
        << x11Clipboard->GetIncrementalTransferCount()
        << " transfer(s) still kept for a requestor that no longer exists";
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

TEST_F(X11Live, AnIncrementalPasteFromASlowOwnerArrivesWhole)
{
    // NPV-0112. Reading a selection through INCR is a conversation: the receiver deletes the
    // property, the owner writes the next chunk, a PropertyNotify(NewValue) announces it. The
    // receiver's queue also holds notifications that announce nothing -- the one the INCR size
    // announcement produced, above all -- and the first version took the first of those as "the
    // next chunk is here", found the property absent, and ended the transfer: a paste from xsel
    // (4000-byte chunks) stopped at 4000 bytes, and a large one from xclip came back empty.
    std::string text;
    text.reserve(160 * 1024);
    while (text.size() < 160u * 1024u)
    {
        text += "slow INCR owner, \xC5\x99\xC3\xAD\xC5\xA1 0123456789;";
    }
    SlowIncrementalOwner owner(text, 4000, std::chrono::milliseconds(8));
    ASSERT_TRUE(owner.Start()) << "could not take the CLIPBOARD for the test owner";

    IPlatformClipboard* clipboard = platform_->GetClipboard();
    ASSERT_NE(clipboard, nullptr);
    const std::string pasted = clipboard->GetText();
    EXPECT_EQ(pasted.size(), text.size());
    EXPECT_TRUE(pasted == text) << "the transfer was cut short or corrupted";
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

TEST_F(X11Live, PresentedPixelsAreReadableBackOffTheRealWindow)
{
    // The presenter's other test proves the calls are accepted. This one proves pixels arrived.
    //
    // The read-back goes through `XGetImage` on the window itself -- the X server's own copy of
    // what is on screen -- so it measures the whole path: the RGBA8 to visual-format conversion
    // derived from the visual's masks, the scaling, and the transfer. A conversion that packed
    // the channels in the wrong order would pass every other assertion in this file and fail
    // here, which is the point.
    window_ = MakeWindow(64, 64);
    window_->Show();
    window_->Sync();
    // A mapped window is not yet a *viewable* one: the server has to process the map before its
    // contents can be read. The first Expose is that signal.
    const WindowId id = window_->GetId();
    ASSERT_TRUE(PumpUntil([this, id](const std::vector<PlatformEvent>& events) {
        return SawWindowEvent(events, id, WindowEventKind::Exposed);
    })) << "the window never became viewable";

    std::unique_ptr<IPlatformSurfacePresenter> presenter =
        platform_->CreateSurfacePresenter(*window_);
    ASSERT_NE(presenter, nullptr);

    // A deliberately asymmetric colour: 0xFF/0x80/0x20 is different in every channel, so a
    // swapped red and blue is visible where a grey or a pure primary would not be.
    constexpr std::uint8_t kRed = 0xFF;
    constexpr std::uint8_t kGreen = 0x80;
    constexpr std::uint8_t kBlue = 0x20;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(64 * 64 * 4), 0);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        pixels[index + 0] = kRed;
        pixels[index + 1] = kGreen;
        pixels[index + 2] = kBlue;
        pixels[index + 3] = 0xFF;
    }
    SurfaceFrame frame;
    frame.pixels = pixels.data();
    frame.width = 64;
    frame.height = 64;
    presenter->SetScaleMode(PresentScaleMode::Stretch, PresentFilter::Nearest);
    ASSERT_NO_THROW(presenter->Present(frame));

    const NativeWindowHandle handle = window_->GetNativeHandle();
    X11NativeWindow x11{};
    ASSERT_TRUE(TryGetX11(handle, x11));
    auto* display = static_cast<::Display*>(x11.display);
    XSync(display, kXFalse);

    XImage* image = XGetImage(display, static_cast<::Window>(x11.window), 16, 16, 8, 8, AllPlanes,
                              ZPixmap);
    ASSERT_NE(image, nullptr) << "the X server would not return the window's contents";

    XWindowAttributes attributes{};
    ASSERT_NE(XGetWindowAttributes(display, static_cast<::Window>(x11.window), &attributes), 0);
    Visual* visual = attributes.visual;
    ASSERT_NE(visual, nullptr);

    // Unpacked through the visual's own masks, the same way the presenter packed them. Comparing
    // raw pixel values would only assert that two pieces of code share a bug.
    const auto unpack = [](const unsigned long value, const unsigned long mask) -> int {
        if (mask == 0) { return -1; }
        int shift = 0;
        unsigned long probe = mask;
        while ((probe & 1u) == 0) { probe >>= 1; ++shift; }
        int bits = 0;
        while ((probe & 1u) != 0) { probe >>= 1; ++bits; }
        const unsigned long channel = (value & mask) >> shift;
        return static_cast<int>(bits >= 8 ? (channel >> (bits - 8)) : (channel << (8 - bits)));
    };

    const unsigned long sample = XGetPixel(image, 4, 4);
    const int red = unpack(sample, visual->red_mask);
    const int green = unpack(sample, visual->green_mask);
    const int blue = unpack(sample, visual->blue_mask);
    XDestroyImage(image);

    // Tolerance, not equality: a 16-bit visual carries 5 or 6 bits per channel, so the value
    // round-trips lossily by construction. Wide enough to survive that, far narrower than the
    // distance between these three channel values -- so a swap still fails.
    constexpr int kTolerance = 8;
    EXPECT_NEAR(red, kRed, kTolerance) << "red channel";
    EXPECT_NEAR(green, kGreen, kTolerance) << "green channel";
    EXPECT_NEAR(blue, kBlue, kTolerance) << "blue channel";
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

TEST_F(X11Live, AGlWindowWithoutAFramebufferRequestGetsWhatAGameNeeds)
{
    IPlatformGlContext* gl = platform_->GetGlContext();
    if (gl == nullptr)
    {
        GTEST_SKIP() << "this server provides no GLX 1.3";
    }
    // plans/plan_x11.md X11-0172: the EasyGL renderers state no framebuffer when the window is
    // made -- zero is "the platform default" -- and ask for 24/8 and double buffering only when they
    // create the context. On X11 the window's visual, and with it the depth buffer, is fixed by
    // then. A platform default of "whatever comes first" was a single-buffered visual without a
    // depth buffer: the house demo drew every wall through every other.
    WindowDescription description;
    description.title = "CNA GL default";
    description.width = 128;
    description.height = 96;
    description.centered = false;
    description.renderIntent = WindowRenderIntent::OpenGl;
    try
    {
        window_ = platform_->CreateWindow(description);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "no GL-capable visual on this server: " << error.what();
    }
    window_->Show();
    window_->Sync();

    GlContextDescription requested;
    requested.majorVersion = 3;
    requested.minorVersion = 3;
    requested.profile = GlProfile::Core;
    requested.depthBits = 24;
    requested.stencilBits = 8;
    requested.doubleBuffer = true;
    GlContextHandle context = nullptr;
    try
    {
        context = gl->CreateContext(window_->GetId(), requested);
    }
    catch (const PlatformException& error)
    {
        GTEST_SKIP() << "this driver refused a GL context: " << error.what();
    }
    const GlContextDescription granted = gl->GetContextAttributes(context);
    EXPECT_GE(granted.depthBits, 24) << "a game's back buffer has a depth buffer";
    EXPECT_GE(granted.stencilBits, 8) << "and XNA's Depth24Stencil8 a stencil buffer";
    EXPECT_TRUE(granted.doubleBuffer) << "and it is double-buffered";
    gl->DestroyContext(context);
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
