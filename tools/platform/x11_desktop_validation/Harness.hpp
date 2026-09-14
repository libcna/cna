// SPDX-License-Identifier: MS-PL
//
// plans/plan_native_platform_validation.md: shared pieces of the real-desktop X11 validation
// harness (`cna_x11_desktop_validation`).
//
// The harness drives CNA's X11 backend through the public platform contract only, and checks what
// it did through an INDEPENDENT X connection (the "driver"): a second client that plays the part
// of the user's pager and of an outside observer. Asking CNA whether its window has focus proves
// little; asking the X server, over a connection CNA does not own, proves what another program
// would see.
//
// It is not a GoogleTest suite on purpose. Most of what it does needs a real desktop, a real GPU
// or a person-sized amount of time (thousands of window operations, minutes of rendering), and it
// reports each check as a PASS/FAIL/SKIP line that a validation log can quote verbatim.
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Xlib is reached through the backend's own header so its macros (None, Bool, Status,
// CurrentTime) are captured and undefined exactly as they are inside the backend. The optional
// extension headers the harness itself uses come first: they declare functions in terms of those
// macros, so they must be seen before X11Headers.hpp removes them.
#include <X11/Xlib.h>
#if defined(CNA_X11_VALIDATION_HAVE_XTEST)
#  include <X11/extensions/XTest.h>
#endif
#if defined(CNA_X11_VALIDATION_HAVE_XRES)
#  include <X11/extensions/XRes.h>
#endif
#include "../../../modules/platform/src/X11/X11Headers.hpp"

namespace CnaX11Validation {

    using namespace CNA::Platform;
    // `<X11/X.h>` declares a global `KeyCode` typedef; these keep CNA's own names unambiguous.
    using KeyCode = CNA::Platform::KeyCode;
    using Scancode = CNA::Platform::Scancode;

    // --- reporting ------------------------------------------------------------------------------

    /** @brief Running totals of every check the harness made. */
    struct Tally
    {
        int passed = 0;
        int failed = 0;
        int skipped = 0;
    };

    /** @brief Gets the process-wide tally. */
    Tally& Results();

    /** @brief Records and prints a passing check. */
    void Pass(const std::string& check, const std::string& detail = {});

    /** @brief Records and prints a failing check. */
    void Fail(const std::string& check, const std::string& detail = {});

    /** @brief Records and prints a check that could not run here, with the reason. */
    void Skip(const std::string& check, const std::string& reason);

    /** @brief Prints an observation that is evidence but not a check. */
    void Info(const std::string& text);

    /** @brief Passes or fails @p check on @p condition. @return @p condition. */
    bool Check(bool condition, const std::string& check, const std::string& detail = {});

    // --- process resources ----------------------------------------------------------------------

    /** @brief What this process holds, for leak detection across long loops. */
    struct ProcessSample
    {
        long residentKb = 0;
        int openDescriptors = 0;
    };

    /** @brief Reads the current resident set size and open descriptor count from /proc. */
    ProcessSample SampleProcess();

    // --- the independent X client ---------------------------------------------------------------

    /**
     * @brief A second X connection that acts on CNA's windows from outside.
     *
     * Everything a window manager, a pager or a user's other programs can do to a window, done
     * over a connection CNA does not own -- so the backend meets these requests the way it meets
     * them in a real session.
     */
    class Driver
    {
    public:
        Driver();
        ~Driver();
        Driver(const Driver&) = delete;
        Driver& operator=(const Driver&) = delete;

        /** @brief True when the connection opened. */
        [[nodiscard]] bool Ok() const { return display_ != nullptr; }

        /** @brief Gets the driver's own connection. */
        [[nodiscard]] ::Display* GetDisplay() const { return display_; }

        /** @brief Asks the window manager to activate a window, as a pager does. */
        bool Activate(::Window window);

        /** @brief Asks the window manager to close a window, as its close button does. */
        bool CloseThroughWindowManager(::Window window);

        /** @brief Moves a window's client area to a root position. */
        bool Move(::Window window, int x, int y);

        /** @brief Gets the window that currently holds keyboard focus. */
        [[nodiscard]] ::Window FocusedWindow() const;

        /** @brief True when @p focus is @p window or one of its descendants. */
        [[nodiscard]] bool FocusIsWithin(::Window window) const;

        /**
         * @brief Tries to grab the pointer from this connection, and releases it at once.
         *
         * `AlreadyGrabbed` is the X server's own statement that another client holds the pointer
         * -- which is the observable definition of "the desktop is grabbed".
         *
         * @return The raw `XGrabPointer` status.
         */
        int ProbePointerGrab();

        /** @brief True when another client (CNA) holds the pointer grab right now. */
        bool PointerIsGrabbedElsewhere() { return ProbePointerGrab() == AlreadyGrabbed; }

        /** @brief Creates an ordinary mapped window of the driver's own, e.g. to take focus. */
        ::Window CreatePlainWindow(const std::string& title, int x, int y, int width, int height);

        /** @brief Destroys a window the driver created. */
        void DestroyWindow(::Window window);

        /**
         * @brief Counts the server-side resources owned by the client that owns @p anyXid.
         *
         * Uses the X-Resource extension. Windows, pixmaps, GCs, cursors, colormaps and so on are
         * counted per type, so a leak shows up as a type whose count only grows.
         *
         * @param anyXid Any resource id of the client in question (e.g. one of CNA's windows).
         * @param byType Receives the per-type counts.
         * @return The total, or -1 when X-Resource is unavailable.
         */
        long ClientResources(std::uintptr_t anyXid, std::map<std::string, long>* byType) const;

        /** @brief Why the last ClientResources() call returned -1. */
        [[nodiscard]] const std::string& ResourceProblem() const { return resourceProblem_; }

        /**
         * @brief Reads where the X server believes the pointer is, relative to @p window.
         *
         * On Xwayland the server learns the real cursor position only while the cursor is over
         * one of its own surfaces, so this is exact inside an X window and stale elsewhere.
         *
         * @return True when the reported position lies inside @p window.
         */
        bool PointerInside(::Window window, int& windowX, int& windowY, int& rootX,
                           int& rootY) const;

        /** @brief Round-trips the driver's connection. */
        void Sync() const;

        /** @brief Counts X protocol errors the driver's own requests produced. */
        [[nodiscard]] int ErrorCount() const;

    private:
        /** @brief Reads the X server's current time, for requests a window manager validates. */
        Time ServerTime();

        ::Display* display_ = nullptr;
        ::Window timeWindow_ = 0;
        mutable std::string resourceProblem_;
    };

    // --- a CNA platform session -----------------------------------------------------------------

    /** @brief One X11 platform instance plus the event bookkeeping every scenario needs. */
    class Session
    {
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        /** @brief True when the platform was created and Video acquired. */
        [[nodiscard]] bool Ok() const { return platform_ != nullptr && acquired_; }

        /** @brief Why the session could not start. */
        [[nodiscard]] const std::string& Error() const { return error_; }

        /** @brief Gets the platform. */
        [[nodiscard]] IPlatform& Platform() { return *platform_; }

        /** @brief Creates a window. */
        std::unique_ptr<IPlatformWindow> Make(const std::string& title, int width, int height,
                                              bool visible = true,
                                              WindowRenderIntent intent = WindowRenderIntent::None,
                                              int x = 60, int y = 60);

        /** @brief Polls once, appending to the seen list. @return The events of this poll. */
        const std::vector<PlatformEvent>& Poll();

        /** @brief Pumps until @p predicate holds or @p budget passes. */
        bool PumpUntil(const std::function<bool()>& predicate, std::chrono::milliseconds budget);

        /** @brief Pumps for a fixed time, collecting everything. */
        void PumpFor(std::chrono::milliseconds duration);

        /** @brief Forgets everything seen so far. */
        void Clear() { seen_.clear(); }

        /** @brief Everything seen since the last Clear(). */
        [[nodiscard]] const std::vector<PlatformEvent>& Seen() const { return seen_; }

        /** @brief True when a window event of @p kind for @p window was seen. */
        [[nodiscard]] bool Saw(WindowId window, WindowEventKind kind) const;

        /** @brief Counts window events of @p kind for @p window. */
        [[nodiscard]] int Count(WindowId window, WindowEventKind kind) const;

        /** @brief True when a QuitEvent was seen. */
        [[nodiscard]] bool SawQuit() const;

        /** @brief Pumps until @p window reported @p kind. */
        bool WaitFor(WindowId window, WindowEventKind kind,
                     std::chrono::milliseconds budget = std::chrono::milliseconds(3000));

        /**
         * @brief Makes a window the focused one, the way a user clicking it would.
         *
         * Maps are not focus: a compositing window manager applies focus-stealing prevention, so
         * the driver asks for activation as a pager does and the backend's own FocusGained is
         * what is waited for.
         */
        bool Focus(IPlatformWindow& window, Driver& driver,
                   std::chrono::milliseconds budget = std::chrono::milliseconds(3000));

    private:
        std::unique_ptr<IPlatform> platform_;
        bool acquired_ = false;
        std::string error_;
        std::vector<PlatformEvent> batch_;
        std::vector<PlatformEvent> seen_;
    };

    class VirtualInput;

    /**
     * @brief Moves the REAL desktop cursor to a point inside an X window, using a virtual mouse.
     *
     * On a Wayland desktop an Xwayland client only receives pointer input -- motion, buttons,
     * wheel, and the XI2 raw events relative mode reads -- while the compositor's cursor is over
     * one of its surfaces or locked to it. XTest cannot move that cursor; a uinput mouse can.
     * Starts from the desktop's bottom-left corner (never the top-left: that is GNOME's
     * Activities hot corner) and steers with feedback from the X server's own pointer position,
     * which is exact once the cursor is over an X window.
     *
     * @return True when the cursor is within a couple of pixels of (@p targetX, @p targetY).
     */
    bool SteerPointerInto(VirtualInput& mouse, Driver& driver, ::Window window, int targetX,
                          int targetY);

    /**
     * @brief Whether kernel-level (uinput) input can reach windows on this display at all.
     *
     * A uinput device feeds the seat's compositor or X server -- the real desktop -- whatever
     * $DISPLAY says. On an Xvfb it would type into whatever the user has focused on the real
     * desktop instead. So uinput is allowed only on Xwayland (the XWAYLAND extension is present),
     * or on a native X server when `--native-x-input` says the seat belongs to it.
     */
    bool UinputReachesDisplay(Driver& driver, const std::vector<std::string>& arguments,
                              std::string& reason);

    /**
     * @brief Proves end to end that the virtual keyboard's keys arrive in @p window.
     *
     * Taps Right Ctrl -- a key that does nothing on its own almost anywhere -- and requires the
     * CNA window to report it. Every keyboard-driven scenario calls this before its first real key.
     */
    bool ProbeKeyboardReachesWindow(VirtualInput& keyboard, Session& session,
                                    IPlatformWindow& window);

    /** @brief The window an event belongs to, or 0 for events that have none. */
    WindowId EventWindow(const PlatformEvent& event);

    /** @brief Formats a number with a sign, for delta reporting. */
    std::string Signed(long value);

    /** @brief Milliseconds since an arbitrary monotonic epoch. */
    double NowMs();

    /** @brief Parses `--name=value` or `--name value` from the scenario's arguments. */
    long OptionInt(const std::vector<std::string>& arguments, const std::string& name,
                   long fallback);

    /** @brief True when `--name` is present. */
    bool OptionFlag(const std::vector<std::string>& arguments, const std::string& name);

    // --- scenarios -----------------------------------------------------------------------------

    int RunInfo(const std::vector<std::string>& arguments);
    int RunLifecycle(const std::vector<std::string>& arguments);
    int RunStress(const std::vector<std::string>& arguments);
    int RunKeyboard(const std::vector<std::string>& arguments);
    int RunText(const std::vector<std::string>& arguments);
    int RunMouse(const std::vector<std::string>& arguments);
    int RunRelative(const std::vector<std::string>& arguments);
    int RunClipboard(const std::vector<std::string>& arguments);
    int RunWindowManager(const std::vector<std::string>& arguments);
    int RunDisplays(const std::vector<std::string>& arguments);
    int RunGlx(const std::vector<std::string>& arguments);
    int RunVulkan(const std::vector<std::string>& arguments);
    int RunPresenter(const std::vector<std::string>& arguments);
    int RunLifetime(const std::vector<std::string>& arguments);
    int RunSoak(const std::vector<std::string>& arguments);
    int RunRawProbe(const std::vector<std::string>& arguments);
    int RunInteractive(const std::vector<std::string>& arguments);

} // namespace CnaX11Validation
