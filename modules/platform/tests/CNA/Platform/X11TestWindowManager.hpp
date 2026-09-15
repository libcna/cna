// SPDX-License-Identifier: MS-PL
//
// A window manager for the X11 suites that need one, shared by X11WindowManagerTests.cpp and
// X11ExclusiveFullscreenTests.cpp (plans/plan_x11.md X11-0102, X11-0153). Header-only and inline,
// so the suites in one test binary share one instance -- one openbox for the whole process.

#pragma once

#include "../../../src/X11/X11Headers.hpp"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace CNA::Platform::X11::Testing {

inline bool HasDisplay()
{
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && display[0] != '\0';
}

inline bool HasWindowManagerBinary()
{
    return std::system("command -v openbox >/dev/null 2>&1") == 0;
}

/// What the X server itself says about the window manager, read with plain Xlib.
///
/// Deliberately independent of the backend under test: the fixture uses it to decide whether to
/// start a window manager at all, and to tell "the window manager is not there" apart from "the
/// window manager is there and CNA failed to see it" -- which is a failure, not a skip.
struct EwmhState
{
    bool windowManager = false;
    bool advertisesFullscreen = false;
};

inline EwmhState ReadEwmhState()
{
    using CNA::Platform::X11::kNone;
    using CNA::Platform::X11::kXFalse;

    EwmhState state;
    ::Display* display = XOpenDisplay(nullptr);
    if (display == nullptr)
    {
        return state;
    }
    const ::Window root = DefaultRootWindow(display);
    const Atom check = XInternAtom(display, "_NET_SUPPORTING_WM_CHECK", kXFalse);
    const Atom supported = XInternAtom(display, "_NET_SUPPORTED", kXFalse);
    const Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", kXFalse);

    const auto readWindow = [display, check](const ::Window window) -> ::Window {
        Atom type = kNone;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char* data = nullptr;
        ::Window value = kNone;
        if (XGetWindowProperty(display, window, check, 0, 1, kXFalse, XA_WINDOW, &type, &format,
                               &count, &remaining, &data) == Success &&
            data != nullptr && format == 32 && count == 1)
        {
            value = static_cast<::Window>(*reinterpret_cast<unsigned long*>(data));
        }
        if (data != nullptr) { XFree(data); }
        return value;
    };

    // The EWMH handshake: the root names a check window, and that window names itself. A stale
    // root property left behind by a window manager that exited fails the second half.
    const ::Window child = readWindow(root);
    if (child != kNone)
    {
        int (*previous)(::Display*, XErrorEvent*) =
            XSetErrorHandler([](::Display*, XErrorEvent*) { return 0; });
        state.windowManager = readWindow(child) == child;
        XSync(display, kXFalse);
        XSetErrorHandler(previous);
    }

    Atom type = kNone;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(display, root, supported, 0, 4096, kXFalse, XA_ATOM, &type, &format,
                           &count, &remaining, &data) == Success &&
        data != nullptr && format == 32)
    {
        const auto* atoms = reinterpret_cast<const unsigned long*>(data);
        state.advertisesFullscreen = std::find(atoms, atoms + count, fullscreen) != atoms + count;
    }
    if (data != nullptr) { XFree(data); }
    XCloseDisplay(display);
    return state;
}

/// Starts one window manager for the whole suite and stops it when the process exits.
///
/// ### Why once, and not per test
///
/// The first version started an `openbox --replace` per test and killed it in `TearDown`. Twelve
/// start/replace/SIGTERM cycles in a row is itself unstable: `--replace` handshakes with the
/// outgoing window manager, and terminating one mid-handshake leaves the display unmanaged for a
/// moment. `MinimizeAndRestoreRoundTrip` failed intermittently inside the full suite while passing
/// every time in isolation -- which is the signature of exactly that, and reads as flakiness in
/// the backend rather than as a fault in the harness.
///
/// Owning it here rather than expecting the harness to provide one keeps the test honest about
/// what it needs: a run with no window manager skips instead of quietly measuring nothing.
class SharedWindowManager
{
public:
    /// Gets the process-wide window manager, starting it on first use.
    static SharedWindowManager& Instance()
    {
        static SharedWindowManager manager;
        return manager;
    }

    /// True when a window manager is available: the display's own, or the one this started.
    [[nodiscard]] bool Available() const { return external_ || pid_ > 0; }

    /// True when the display already had a window manager and nothing was started.
    [[nodiscard]] bool External() const { return external_; }

    /// Why the window manager this started is gone, or empty while it is still running.
    [[nodiscard]] std::string ExitReason()
    {
        if (external_ || pid_ <= 0)
        {
            return {};
        }
        int status = 0;
        if (::waitpid(pid_, &status, WNOHANG) != pid_)
        {
            return {};
        }
        pid_ = -1;
        return WIFEXITED(status) ? "openbox exited with status " + std::to_string(WEXITSTATUS(status))
                                 : std::string("openbox was killed by a signal");
    }

private:
    SharedWindowManager()
    {
        // A display that already has an EWMH window manager keeps it. Replacing it would evict a
        // developer's desktop session from under them; using it tests CNA against the window
        // manager it will really meet.
        if (ReadEwmhState().windowManager)
        {
            external_ = true;
            return;
        }
        pid_ = fork();
        if (pid_ == 0)
        {
            // No --replace: the check above found no window manager to replace, and should one
            // appear in between, openbox failing is correct where replacing it would not be.
            // stdio is closed so its startup chatter does not interleave with the test output.
            ::freopen("/dev/null", "w", stdout);
            ::freopen("/dev/null", "w", stderr);
            ::execlp("openbox", "openbox", static_cast<char*>(nullptr));
            ::_exit(127);
        }
    }

    ~SharedWindowManager()
    {
        if (pid_ > 0)
        {
            ::kill(pid_, SIGTERM);
            int status = 0;
            ::waitpid(pid_, &status, 0);
        }
    }

    SharedWindowManager(const SharedWindowManager&) = delete;
    SharedWindowManager& operator=(const SharedWindowManager&) = delete;

    ::pid_t pid_ = -1;
    bool external_ = false;
};

} // namespace CNA::Platform::X11::Testing
